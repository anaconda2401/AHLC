#include "bit_writer.hpp"
#include <iostream>

namespace ahlc {

BitWriter::~BitWriter()
{
    close();
}

bool BitWriter::open(const std::string& path)
{
    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_) {
        std::cerr << "BitWriter: cannot open file: " << path << "\n";
        return false;
    }
    buf_     = 0;
    buf_pos_ = 7;

    // Reserve byte 0 for the padding count; we will seek back and fill it on close().
    char placeholder = 0;
    file_.write(&placeholder, 1);
    return true;
}

bool BitWriter::write_bit(uint8_t bit)
{
    if (!file_) {
        std::cerr << "BitWriter: file not open.\n";
        return false;
    }
    // Place the bit at buf_pos_ (MSB-first).
    if (bit & 1)
        buf_ |= static_cast<uint8_t>(1u << buf_pos_);

    --buf_pos_;
    if (buf_pos_ < 0)   // byte is full — flush it
        flush_byte();

    return true;
}

bool BitWriter::write_bits(uint64_t value, int num_bits)
{
    if (num_bits < 1 || num_bits > 64) {
        std::cerr << "BitWriter: num_bits must be 1–64.\n";
        return false;
    }
    // Write from the most-significant requested bit down to bit 0.
    for (int i = num_bits - 1; i >= 0; --i) {
        uint8_t bit = static_cast<uint8_t>((value >> i) & 1u);
        if (!write_bit(bit))
            return false;
    }
    return true;
}

void BitWriter::flush_byte()
{
    // Write the accumulated byte and reset.
    file_.put(static_cast<char>(buf_));
    buf_     = 0;
    buf_pos_ = 7;
}

void BitWriter::close()
{
    if (!file_.is_open())
        return;

    // How many bits of the current buffer byte are still unfilled?
    int padding = 0;
    if (buf_pos_ < 7) {
        // There are bits waiting in the buffer — flush them (low bits stay 0).
        padding = buf_pos_ + 1; // number of unused low bits
        flush_byte();
    }

    // Seek back to byte 0 and write the actual padding count.
    file_.seekp(0, std::ios::beg);
    file_.put(static_cast<char>(padding));

    file_.close();
}

} // namespace ahlc
