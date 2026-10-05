#pragma once

#include <fstream>
#include <string>
#include <cstdint>

namespace ahlc {

/**
 * BitWriter — writes individual bits to a binary file.
 *
 * Bit ordering: MSB-first within each byte.
 * Example: writing bits 1,0,1,1,0,0,1,0 produces byte 0xB2 (10110010).
 *
 * File format:
 *   Byte 0   : padding count (0–7), i.e. how many LOW bits of the last
 *              data byte are unused padding zeros.
 *   Bytes 1+ : packed bit data, MSB-first.
 *
 * Always call close() (or let the destructor run) to flush and finalize.
 */
class BitWriter {
public:
    BitWriter() = default;
    ~BitWriter();

    // Open/create a file. Returns false on failure.
    bool open(const std::string& path);

    // Write a single bit (0 or 1). Returns false on error.
    bool write_bit(uint8_t bit);

    // Write the lowest `num_bits` bits of `value`, MSB-first.
    // num_bits must be in [1, 64].
    bool write_bits(uint64_t value, int num_bits);

    // Flush remaining bits (padding with zeros) and close the file.
    void close();

    bool is_open() const { return file_.is_open(); }

private:
    std::ofstream file_;
    uint8_t       buf_     = 0; // accumulates bits into one byte
    int           buf_pos_ = 7; // next bit position to fill (7=MSB ... 0=LSB)

    void flush_byte();
};

} // namespace ahlc
