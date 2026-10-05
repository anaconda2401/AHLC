/*
 * Canonical Huffman Entropy Coder
 * ---------------------------------
 * Algorithm:
 *   1. Count byte frequencies.
 *   2. Build Huffman tree (min-heap / priority queue).
 *   3. Walk tree to assign code LENGTHS (depth of each leaf).
 *   4. Convert to CANONICAL codes: sort symbols by (length, value),
 *      assign codes in order — decoder needs only the length table.
 *   5. Serialize: header + 256-byte length table + MSB-first bit data.
 *   6. Decoder: rebuild canonical code ranges from lengths, decode bit-by-bit.
 *
 * File format (.huff):
 *   Bytes  0- 3  Magic: 'H','U','F','F'
 *   Byte   4     Version: 1
 *   Bytes  5- 8  Original byte count (uint32 LE)
 *   Bytes  9-12  Encoded bit count   (uint32 LE)
 *   Bytes 13-268 code_lengths[0..255] — 0 = symbol unused
 *   Bytes 269+   Packed bit data (MSB-first)
 *
 * Total header: 269 bytes.
 */

#include "huffman.hpp"

#include <fstream>
#include <iostream>
#include <iomanip>
#include <queue>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace ahlc {

// ================================================================
// Internal bit packing — MSB first, same convention as BitWriter.
// Inline so huffman.cpp is self-contained.
// ================================================================

class BitPack {
public:
    explicit BitPack(std::vector<uint8_t>& buf) : buf_(buf) {}

    void write_bit(int bit) {
        if (bit_pos_ == 8) { buf_.push_back(0u); bit_pos_ = 0; }
        if (bit) buf_.back() |= static_cast<uint8_t>(0x80u >> bit_pos_);
        ++bit_pos_;
    }
    void write_bits(uint32_t val, int n) {
        for (int i = n - 1; i >= 0; --i)
            write_bit(static_cast<int>((val >> i) & 1u));
    }

private:
    std::vector<uint8_t>& buf_;
    int bit_pos_ = 8; // 8 = full, needs new byte next time
};

class BitUnpack {
public:
    BitUnpack(const uint8_t* data, uint64_t total_bits)
        : data_(data), total_(total_bits) {}

    bool read_bit(uint8_t& out) {
        if (pos_ >= total_) return false;
        out = (data_[pos_ / 8] >> (7 - pos_ % 8)) & 1u;
        ++pos_;
        return true;
    }
    bool eof() const { return pos_ >= total_; }

private:
    const uint8_t* data_;
    uint64_t total_, pos_ = 0;
};

// ================================================================
// Little-endian I/O helpers (header only, no BitWriter dependency)
// ================================================================

static void h_wr32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 24));
}
static uint32_t h_rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

// ================================================================
// Huffman tree
// ================================================================

struct HNode {
    uint64_t freq;
    int      symbol; // 0-255 for leaves, -1 for internal
    int      left, right; // indices, -1 = none
};

// Build Huffman tree. Returns root index, or -1 for empty input.
// 'nodes' is filled by this function.
static int build_tree(const uint64_t freq[256], std::vector<HNode>& nodes)
{
    nodes.clear();

    // Min-heap of (freq, node_index)
    using P = std::pair<uint64_t, int>;
    std::priority_queue<P, std::vector<P>, std::greater<P>> pq;

    for (int s = 0; s < 256; ++s) {
        if (freq[s] == 0) continue;
        int idx = static_cast<int>(nodes.size());
        nodes.push_back({freq[s], s, -1, -1});
        pq.push({freq[s], idx});
    }
    if (pq.empty()) return -1;          // no symbols
    if (pq.size() == 1) return pq.top().second; // single symbol

    while (pq.size() > 1) {
        auto [f1, i1] = pq.top(); pq.pop();
        auto [f2, i2] = pq.top(); pq.pop();
        int idx = static_cast<int>(nodes.size());
        nodes.push_back({f1 + f2, -1, i1, i2});
        pq.push({f1 + f2, idx});
    }
    return pq.top().second;
}

// Recursively assign code lengths (tree depth of each leaf).
static void assign_lengths(const std::vector<HNode>& nodes,
                            int root, int depth,
                            uint8_t lengths[256])
{
    if (root < 0) return;
    const auto& n = nodes[static_cast<size_t>(root)];
    if (n.symbol >= 0) {
        // Leaf: depth = code length. Minimum 1 (handles single-symbol case).
        lengths[n.symbol] = static_cast<uint8_t>(depth > 0 ? depth : 1);
        return;
    }
    assign_lengths(nodes, n.left,  depth + 1, lengths);
    assign_lengths(nodes, n.right, depth + 1, lengths);
}

// ================================================================
// Canonical codes
// ================================================================

// From a code_lengths[] table, assign canonical codes[] to each symbol.
// Returns false if any code length exceeds 32 (pathological input).
static bool build_canonical_codes(const uint8_t lengths[256],
                                   uint32_t codes[256])
{
    std::memset(codes, 0, 256 * sizeof(uint32_t));

    // Find max code length.
    uint8_t max_len = 0;
    for (int s = 0; s < 256; ++s) max_len = std::max(max_len, lengths[s]);
    if (max_len == 0) return true; // no symbols
    if (max_len > 32) { std::cerr << "Huffman: code length > 32 (pathological input).\n"; return false; }

    // Count symbols per length.
    uint32_t count[33] = {};
    for (int s = 0; s < 256; ++s) if (lengths[s]) count[lengths[s]]++;

    // First canonical code for each length.
    // Standard formulation: first_code[1] = 0;
    //                       first_code[l] = (first_code[l-1] + count[l-1]) << 1
    uint32_t first[33] = {};
    first[1] = 0;
    for (int l = 2; l <= 32; ++l)
        first[l] = (first[l - 1] + count[l - 1]) << 1;

    // Assign codes in symbol order (canonical: same length → ascending symbol value).
    uint32_t next[33] = {};
    for (int l = 1; l <= 32; ++l) next[l] = first[l];

    for (int s = 0; s < 256; ++s) {
        if (lengths[s]) {
            codes[s] = next[lengths[s]]++;
        }
    }
    return true;
}

// ================================================================
// Canonical decoder table
// ================================================================

struct DecodeTable {
    uint8_t  max_len = 0;
    uint32_t first_code[33] = {};           // first canonical code at each length
    std::vector<uint8_t> sym_at_len[33];   // symbols in canonical order at each length
};

static void build_decode_table(const uint8_t lengths[256], DecodeTable& dt)
{
    dt.max_len = 0;
    for (int l = 0; l <= 32; ++l) dt.sym_at_len[l].clear();

    uint32_t count[33] = {};
    for (int s = 0; s < 256; ++s) {
        if (lengths[s]) {
            count[lengths[s]]++;
            if (lengths[s] > dt.max_len) dt.max_len = lengths[s];
        }
    }

    // Compute first_code (same as encoder).
    dt.first_code[1] = 0;
    for (int l = 2; l <= 32; ++l)
        dt.first_code[l] = (dt.first_code[l - 1] + count[l - 1]) << 1;

    // Build symbol list per length, in ascending symbol order (= canonical order).
    for (int s = 0; s < 256; ++s)
        if (lengths[s])
            dt.sym_at_len[lengths[s]].push_back(static_cast<uint8_t>(s));
}

// ================================================================
// Core compress — takes raw input bytes, returns HUFF-format bytes.
// ================================================================

static bool compress_core(const uint8_t*     input,
                           uint32_t           in_size,
                           std::vector<uint8_t>& output,
                           HuffStats*         stats)
{
    output.clear();

    // --- Frequency table ---
    uint64_t freq[256] = {};
    for (uint32_t i = 0; i < in_size; ++i) freq[input[i]]++;

    uint64_t unique = 0;
    for (int s = 0; s < 256; ++s) if (freq[s]) ++unique;

    // --- Build tree and assign lengths ---
    uint8_t  lengths[256] = {};
    std::vector<HNode> nodes;
    int root = build_tree(freq, nodes);

    if (root >= 0) {
        assign_lengths(nodes, root, 0, lengths);
    }
    // (if root == -1, all lengths stay 0 → empty input)

    // --- Canonical codes ---
    uint32_t codes[256] = {};
    if (!build_canonical_codes(lengths, codes)) return false;

    // --- Compute encoded bit count ---
    uint64_t total_bits = 0;
    double   avg_len    = 0.0;
    double   entropy    = 0.0;
    double   n          = static_cast<double>(in_size);

    for (int s = 0; s < 256; ++s) {
        if (freq[s] == 0) continue;
        total_bits += freq[s] * lengths[s];
        avg_len    += static_cast<double>(freq[s]) * lengths[s];
        double p    = static_cast<double>(freq[s]) / n;
        entropy    -= p * std::log2(p);
    }
    if (in_size > 0) avg_len /= n;

    // Protect against input so large the bit count overflows uint32_t.
    if (total_bits > 0xFFFFFFFFULL) {
        std::cerr << "Huffman: input too large for this implementation.\n";
        return false;
    }

    // --- Populate stats ---
    if (stats) {
        stats->original_size  = in_size;
        stats->unique_symbols = unique;
        stats->entropy        = entropy;
        stats->avg_code_length= avg_len;
    }

    // --- Write header ---
    output.push_back('H'); output.push_back('U');
    output.push_back('F'); output.push_back('F');
    output.push_back(1u); // version
    h_wr32(output, in_size);
    h_wr32(output, static_cast<uint32_t>(total_bits));
    for (int s = 0; s < 256; ++s)
        output.push_back(lengths[s]);

    // --- Encode data ---
    {
        BitPack bp(output);
        for (uint32_t i = 0; i < in_size; ++i) {
            uint8_t sym = input[i];
            bp.write_bits(codes[sym], static_cast<int>(lengths[sym]));
        }
        // BitPack fills the last byte's spare bits with 0 automatically.
    }

    if (stats) {
        stats->compressed_size = output.size();
    }
    return true;
}

// ================================================================
// Core decompress — takes HUFF-format bytes, returns original bytes.
// ================================================================

static bool decompress_core(const uint8_t*        input,
                             size_t                in_size,
                             std::vector<uint8_t>& output)
{
    output.clear();
    constexpr size_t HDR = 269;
    if (in_size < HDR) {
        std::cerr << "Huffman: file too small to contain a valid header.\n"; return false;
    }

    // Magic
    if (input[0]!='H'||input[1]!='U'||input[2]!='F'||input[3]!='F') {
        std::cerr << "Huffman: invalid magic.\n"; return false;
    }
    if (input[4] != 1) {
        std::cerr << "Huffman: unsupported version " << (int)input[4] << ".\n"; return false;
    }

    uint32_t orig_size = h_rd32(input + 5);
    uint32_t bit_count = h_rd32(input + 9);
    const uint8_t* lengths = input + 13; // code_lengths[0..255]
    const uint8_t* bit_data = input + HDR;

    // Trivial case: empty original file.
    if (orig_size == 0) return true;

    // Build canonical decode table.
    DecodeTable dt;
    build_decode_table(lengths, dt);

    if (dt.max_len == 0) {
        std::cerr << "Huffman: no valid symbols in header.\n"; return false;
    }

    output.reserve(orig_size);

    BitUnpack bp(bit_data, static_cast<uint64_t>(bit_count));

    // Decode: accumulate bits, match against canonical ranges.
    uint32_t code   = 0;
    uint8_t  len    = 0;
    uint32_t decoded = 0;

    while (decoded < orig_size) {
        uint8_t bit = 0;
        if (!bp.read_bit(bit)) {
            std::cerr << "Huffman: bit stream exhausted before decoding complete.\n";
            return false;
        }
        code = (code << 1) | bit;
        ++len;

        if (len > 32) {
            std::cerr << "Huffman: code length exceeded 32 during decode.\n";
            return false;
        }

        // Check if (code, len) matches a canonical code at this length.
        if (len <= dt.max_len && !dt.sym_at_len[len].empty()) {
            uint32_t fc    = dt.first_code[len];
            uint32_t count = static_cast<uint32_t>(dt.sym_at_len[len].size());
            if (code >= fc && (code - fc) < count) {
                output.push_back(dt.sym_at_len[len][code - fc]);
                code    = 0;
                len     = 0;
                ++decoded;
            }
        }
    }
    return true;
}

// ================================================================
// Public in-memory API
// ================================================================

bool huffman_compress_mem(const std::vector<uint8_t>& input,
                          std::vector<uint8_t>& output)
{
    HuffStats s;
    return compress_core(input.data(),
                         static_cast<uint32_t>(input.size()),
                         output, &s);
}

bool huffman_decompress_mem(const std::vector<uint8_t>& input,
                            std::vector<uint8_t>& output)
{
    return decompress_core(input.data(), input.size(), output);
}

// ================================================================
// Public file-based API
// ================================================================

bool huffman_compress(const std::string& in_path,
                      const std::string& out_path,
                      HuffStats& stats)
{
    // Read entire input file.
    std::ifstream fi(in_path, std::ios::binary | std::ios::ate);
    if (!fi) { std::cerr << "Huffman: cannot open: " << in_path << "\n"; return false; }
    auto fsz = fi.tellg();
    fi.seekg(0);
    std::vector<uint8_t> raw(static_cast<size_t>(fsz));
    if (fsz > 0) fi.read(reinterpret_cast<char*>(raw.data()), fsz);
    fi.close();

    std::vector<uint8_t> compressed;
    if (!compress_core(raw.data(), static_cast<uint32_t>(raw.size()),
                       compressed, &stats)) return false;

    std::ofstream fo(out_path, std::ios::binary | std::ios::trunc);
    if (!fo) { std::cerr << "Huffman: cannot create: " << out_path << "\n"; return false; }
    if (!compressed.empty())
        fo.write(reinterpret_cast<const char*>(compressed.data()),
                 static_cast<std::streamsize>(compressed.size()));
    return true;
}

bool huffman_decompress(const std::string& in_path, const std::string& out_path)
{
    std::ifstream fi(in_path, std::ios::binary | std::ios::ate);
    if (!fi) { std::cerr << "Huffman: cannot open: " << in_path << "\n"; return false; }
    auto fsz = fi.tellg();
    fi.seekg(0);
    std::vector<uint8_t> raw(static_cast<size_t>(fsz));
    if (fsz > 0) fi.read(reinterpret_cast<char*>(raw.data()), fsz);
    fi.close();

    std::vector<uint8_t> original;
    if (!decompress_core(raw.data(), raw.size(), original)) return false;

    std::ofstream fo(out_path, std::ios::binary | std::ios::trunc);
    if (!fo) { std::cerr << "Huffman: cannot create: " << out_path << "\n"; return false; }
    if (!original.empty())
        fo.write(reinterpret_cast<const char*>(original.data()),
                 static_cast<std::streamsize>(original.size()));
    return true;
}

// ================================================================
// print_huff_stats
// ================================================================

void print_huff_stats(const HuffStats& stats)
{
    double ratio = (stats.compressed_size > 0)
        ? static_cast<double>(stats.original_size) / stats.compressed_size
        : 0.0;
    double saved = (stats.original_size > 0)
        ? (1.0 - static_cast<double>(stats.compressed_size) /
                  static_cast<double>(stats.original_size)) * 100.0
        : 0.0;

    std::cout << std::fixed;
    std::cout << "--------------------------------------------\n";
    std::cout << " AHLC Huffman Compression\n";
    std::cout << "--------------------------------------------\n";
    std::cout << " Original size    : " << stats.original_size   << " bytes\n";
    std::cout << " Compressed size  : " << stats.compressed_size << " bytes (incl. header)\n";
    std::cout << std::setprecision(4);
    std::cout << " Compression ratio: " << ratio << "x\n";
    std::cout << std::setprecision(2);
    std::cout << " Space saved      : " << saved << "%\n";
    std::cout << " Unique symbols   : " << stats.unique_symbols  << " / 256\n";
    std::cout << std::setprecision(6);
    std::cout << " Shannon entropy  : " << stats.entropy        << " bits/byte\n";
    std::cout << " Avg code length  : " << stats.avg_code_length<< " bits/symbol\n";
    if (saved < 0.0)
        std::cout << " Note: compressed file is LARGER (header overhead dominates small input).\n";
    std::cout << "--------------------------------------------\n";
}

} // namespace ahlc
