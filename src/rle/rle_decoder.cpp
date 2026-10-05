#include "rle_decoder.hpp"

#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdint>

namespace ahlc {

bool rle_decompress(const std::string& input_path,
                    const std::string& output_path)
{
    std::ifstream in(input_path, std::ios::binary);
    if (!in) {
        std::cerr << "RLE: cannot open input: " << input_path << "\n";
        return false;
    }

    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "RLE: cannot open output: " << output_path << "\n";
        return false;
    }

    uint8_t ctrl = 0;

    // Process packets until EOF.
    while (in.read(reinterpret_cast<char*>(&ctrl), 1)) {

        if (ctrl <= 0x7F) {
            // ---- LITERAL packet ----
            // Read (ctrl + 1) bytes and write them verbatim.
            int count = static_cast<int>(ctrl) + 1;
            for (int k = 0; k < count; ++k) {
                uint8_t byte = 0;
                if (!in.read(reinterpret_cast<char*>(&byte), 1)) {
                    std::cerr << "RLE: unexpected EOF inside literal packet.\n";
                    return false;
                }
                out.put(static_cast<char>(byte));
            }

        } else {
            // ---- RUN packet ----
            // Read one byte and write it (ctrl - 0x80 + 2) times.
            int count = static_cast<int>(ctrl) - 0x80 + 2;
            uint8_t byte = 0;
            if (!in.read(reinterpret_cast<char*>(&byte), 1)) {
                std::cerr << "RLE: unexpected EOF inside run packet.\n";
                return false;
            }
            for (int k = 0; k < count; ++k) {
                out.put(static_cast<char>(byte));
            }
        }
    }

    return true;
}

bool files_identical(const std::string& path_a, const std::string& path_b)
{
    std::ifstream fa(path_a, std::ios::binary);
    std::ifstream fb(path_b, std::ios::binary);
    if (!fa || !fb) return false;

    constexpr size_t BUF = 65536;
    std::vector<char> ba(BUF), bb(BUF);

    while (true) {
        fa.read(ba.data(), static_cast<std::streamsize>(BUF));
        fb.read(bb.data(), static_cast<std::streamsize>(BUF));

        auto na = fa.gcount();
        auto nb = fb.gcount();

        if (na != nb) return false;          // different lengths
        if (na == 0)  break;                 // both at EOF — equal
        if (std::memcmp(ba.data(), bb.data(), static_cast<size_t>(na)) != 0)
            return false;                    // mismatch
    }
    return true;
}

// -----------------------------------------------------------------------
// In-memory version — used by the image decompression pipeline.
// -----------------------------------------------------------------------
bool rle_decompress_mem(const std::vector<uint8_t>& input, std::vector<uint8_t>& output)
{
    output.clear();
    size_t i = 0;
    while (i < input.size()) {
        uint8_t ctrl = input[i++];
        if (ctrl <= 0x7F) {
            // Literal packet: copy (ctrl + 1) bytes
            int count = static_cast<int>(ctrl) + 1;
            if (i + static_cast<size_t>(count) > input.size()) {
                std::cerr << "RLE mem: truncated literal packet.\n";
                return false;
            }
            for (int k = 0; k < count; ++k)
                output.push_back(input[i++]);
        } else {
            // Run packet: repeat next byte (ctrl - 0x80 + 2) times
            int count = static_cast<int>(ctrl) - 0x80 + 2;
            if (i >= input.size()) {
                std::cerr << "RLE mem: truncated run packet.\n";
                return false;
            }
            uint8_t byte = input[i++];
            for (int k = 0; k < count; ++k)
                output.push_back(byte);
        }
    }
    return true;
}

} // namespace ahlc
