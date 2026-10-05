#include "rle_encoder.hpp"

#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdint>

namespace ahlc {

// Maximum literals per packet  (control 0x7F → 0x7F+1 = 128 bytes)
static constexpr int MAX_LIT = 128;
// Maximum repetitions per run packet (control 0xFF → 0xFF-0x80+2 = 129 reps)
static constexpr int MAX_RUN = 129;

bool rle_compress(const std::string& input_path,
                  const std::string& output_path,
                  RleStats& stats)
{
    stats = {};

    // --- Read entire input ---
    // RLE needs 1-byte lookahead, so we load the file into memory.
    std::ifstream in(input_path, std::ios::binary);
    if (!in) {
        std::cerr << "RLE: cannot open input: " << input_path << "\n";
        return false;
    }
    std::vector<uint8_t> data(
        (std::istreambuf_iterator<char>(in)),
         std::istreambuf_iterator<char>()
    );
    in.close();

    stats.original_size = data.size();

    // --- Open output ---
    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "RLE: cannot open output: " << output_path << "\n";
        return false;
    }

    // Empty file → empty output, nothing to write.
    if (data.empty()) {
        stats.compressed_size = 0;
        return true;
    }

    // --- Encode ---
    size_t i = 0;
    while (i < data.size()) {

        // Count how many consecutive identical bytes start at position i.
        size_t run = 1;
        while (i + run < data.size() &&
               data[i + run] == data[i] &&
               run < static_cast<size_t>(MAX_RUN)) {
            ++run;
        }

        if (run >= 2) {
            // ---- RUN packet ----
            // Control byte encodes repetition count: ctrl = 0x80 + (run - 2)
            //   run=2 → ctrl=0x80, run=129 → ctrl=0xFF
            uint8_t ctrl = static_cast<uint8_t>(0x80 + run - 2);
            out.put(static_cast<char>(ctrl));
            out.put(static_cast<char>(data[i]));
            i += run;

        } else {
            // ---- LITERAL packet ----
            // Collect bytes until we hit a run of >= 2 or reach MAX_LIT.
            size_t lit_start = i;
            size_t lit_count = 0;

            while (i < data.size() && lit_count < static_cast<size_t>(MAX_LIT)) {
                // If a run of >= 2 starts here and we already have some literals,
                // stop so the outer loop can encode the run efficiently.
                if (lit_count > 0 &&
                    i + 1 < data.size() &&
                    data[i] == data[i + 1]) {
                    break;
                }
                ++lit_count;
                ++i;
            }

            // Control byte encodes literal count: ctrl = lit_count - 1
            //   1 byte → ctrl=0x00, 128 bytes → ctrl=0x7F
            uint8_t ctrl = static_cast<uint8_t>(lit_count - 1);
            out.put(static_cast<char>(ctrl));
            out.write(
                reinterpret_cast<const char*>(data.data() + lit_start),
                static_cast<std::streamsize>(lit_count)
            );
        }
    }

    out.close();

    // Measure actual compressed size.
    {
        std::ifstream check(output_path, std::ios::binary | std::ios::ate);
        if (check)
            stats.compressed_size = static_cast<uint64_t>(check.tellg());
    }

    return true;
}

void print_rle_stats(const RleStats& stats)
{
    std::cout << " Original size   : " << stats.original_size   << " bytes\n";
    std::cout << " Compressed size : " << stats.compressed_size << " bytes\n";

    if (stats.original_size == 0) {
        std::cout << " (Empty file)\n";
        return;
    }
    if (stats.compressed_size == 0) {
        std::cout << " (Empty compressed output)\n";
        return;
    }

    double ratio = static_cast<double>(stats.original_size) /
                   static_cast<double>(stats.compressed_size);
    double saved = (1.0 - static_cast<double>(stats.compressed_size) /
                          static_cast<double>(stats.original_size)) * 100.0;

    std::cout << std::fixed << std::setprecision(4);
    std::cout << " Compression ratio: " << ratio << "x\n";
    std::cout << std::setprecision(2);
    std::cout << " Space saved      : " << saved << "%\n";
    if (saved < 0.0)
        std::cout << " Note: RLE expanded this file — data has low repetition.\n";
}

// -----------------------------------------------------------------------
// In-memory version — same algorithm as rle_compress but vector-based.
// Used by the image compression pipeline to avoid temporary files.
// -----------------------------------------------------------------------
bool rle_compress_mem(const std::vector<uint8_t>& input, std::vector<uint8_t>& output)
{
    output.clear();
    if (input.empty()) return true;

    size_t i = 0;
    while (i < input.size()) {
        // Count identical consecutive bytes starting at i.
        size_t run = 1;
        while (i + run < input.size() &&
               input[i + run] == input[i] &&
               run < static_cast<size_t>(MAX_RUN)) {
            ++run;
        }

        if (run >= 2) {
            // Run packet
            output.push_back(static_cast<uint8_t>(0x80 + run - 2));
            output.push_back(input[i]);
            i += run;
        } else {
            // Literal packet
            size_t lit_start = i;
            size_t lit_count = 0;
            while (i < input.size() && lit_count < static_cast<size_t>(MAX_LIT)) {
                if (lit_count > 0 &&
                    i + 1 < input.size() &&
                    input[i] == input[i + 1]) {
                    break;
                }
                ++lit_count;
                ++i;
            }
            output.push_back(static_cast<uint8_t>(lit_count - 1));
            for (size_t k = 0; k < lit_count; ++k)
                output.push_back(input[lit_start + k]);
        }
    }
    return true;
}

} // namespace ahlc
