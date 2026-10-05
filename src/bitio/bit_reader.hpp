#pragma once

#include <fstream>
#include <string>
#include <cstdint>

namespace ahlc {

/**
 * BitReader — reads individual bits from a file written by BitWriter.
 *
 * Bit ordering: MSB-first, matching BitWriter exactly.
 *
 * Reads the padding count from byte 0 of the file, then streams bits
 * from byte 1 onward, stopping before the padding bits at the end.
 */
class BitReader {
public:
    BitReader() = default;
    ~BitReader() { close(); }

    // Open a bit-stream file. Returns false on failure.
    bool open(const std::string& path);

    // Read one bit into `bit`. Returns false at end-of-stream or on error.
    bool read_bit(uint8_t& bit);

    // Read `num_bits` bits into `value` (MSB-first). Returns false on error/EOF.
    bool read_bits(uint64_t& value, int num_bits);

    // True when all valid bits have been consumed.
    bool eof() const { return eof_; }

    void close();
    bool is_open() const { return file_.is_open(); }

private:
    std::ifstream file_;
    uint8_t       buf_         = 0; // current byte being consumed
    int           bits_left_   = 0; // valid bits remaining in buf_
    int           padding_     = 0; // padding bits in last byte (from header)
    uint64_t      total_bits_  = 0; // total valid bits in the stream
    uint64_t      bits_read_   = 0; // bits consumed so far
    bool          eof_         = false;

    bool load_next_byte();
};

} // namespace ahlc
