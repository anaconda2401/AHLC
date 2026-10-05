#pragma once
#include <vector>

#include <string>
#include <cstdint>

namespace ahlc {

/**
 * Decompress a file produced by rle_compress().
 * Returns false on I/O error or malformed stream.
 */
bool rle_decompress(const std::string& input_path,
                    const std::string& output_path);

/**
 * Compare two files byte-for-byte.
 * Returns true if and only if the files exist, have the same length,
 * and every byte is identical.
 *
 * Used to verify lossless round-trips: original == decompressed.
 */
bool files_identical(const std::string& path_a, const std::string& path_b);

// In-memory RLE: decompress a byte vector into another byte vector.
bool rle_decompress_mem(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);

} // namespace ahlc
