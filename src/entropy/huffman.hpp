#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ahlc {

// Compression statistics returned by huffman_compress / huffman_compress_mem.
struct HuffStats {
    uint64_t original_size   = 0;
    uint64_t compressed_size = 0;  // includes header + packed bit data
    uint64_t unique_symbols  = 0;  // how many of the 256 byte values appear
    double   entropy         = 0.0;
    double   avg_code_length = 0.0;
};

// ---- File-based API ----

// Canonical Huffman compress input_path → output_path.
// stats is filled with compression details.
bool huffman_compress(const std::string& input_path,
                      const std::string& output_path,
                      HuffStats& stats);

// Decompress a file produced by huffman_compress.
bool huffman_decompress(const std::string& input_path,
                        const std::string& output_path);

// Print a formatted compression report.
void print_huff_stats(const HuffStats& stats);

// ---- In-memory API (used by image/audio pipelines) ----

// Compress a byte vector → HUFF-format byte vector.
bool huffman_compress_mem(const std::vector<uint8_t>& input,
                          std::vector<uint8_t>& output);

// Decompress a HUFF-format byte vector → original bytes.
bool huffman_decompress_mem(const std::vector<uint8_t>& input,
                            std::vector<uint8_t>& output);

} // namespace ahlc
