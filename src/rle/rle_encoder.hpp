#pragma once
#include <vector>

#include <string>
#include <cstdint>

namespace ahlc {

/**
 * Statistics returned after RLE compression.
 */
struct RleStats {
    uint64_t original_size   = 0;
    uint64_t compressed_size = 0;
};

/**
 * Compress input_path → output_path using packet-based RLE.
 *
 * Packet format (byte-level, no bit I/O needed):
 *   Control byte c:
 *     0x00 – 0x7F  →  LITERAL packet: the next (c + 1) bytes are copied as-is   [1–128 bytes]
 *     0x80 – 0xFF  →  RUN    packet: the next single byte is repeated (c−0x80+2) times [2–129 reps]
 *
 * Runs of 1 are always stored as literals (a run packet needs at least 2 repetitions to be useful).
 * Long runs (> 129) are split into multiple run packets automatically.
 *
 * Returns false on I/O error.
 */
bool rle_compress(const std::string& input_path,
                  const std::string& output_path,
                  RleStats& stats);

// Print a human-readable compression summary.
void print_rle_stats(const RleStats& stats);

// In-memory RLE: compress a byte vector into another byte vector.
bool rle_compress_mem(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);

} // namespace ahlc
