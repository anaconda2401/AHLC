#include "bit_reader.hpp"
#include <iostream>

namespace ahlc {

bool BitReader::open(const std::string& path)
{
    file_.open(path, std::ios::binary);
    if (!file_) {
        std::cerr << "BitReader: cannot open file: " << path << "\n";
        return false;
    }

    // Read the padding count from byte 0.
    char hdr = 0;
    if (!file_.get(hdr)) {
        std::cerr << "BitReader: file too short (missing header).\n";
        return false;
    }
    padding_ = static_cast<uint8_t>(hdr);

    // Measure the remaining data bytes.
    auto data_start = file_.tellg();
    file_.seekg(0, std::ios::end);
    auto file_end = file_.tellg();
    uint64_t data_bytes = static_cast<uint64_t>(file_end - data_start);
    file_.seekg(data_start);

    if (data_bytes == 0) {
        // No data bytes at all — stream is empty.
        total_bits_ = 0;
        eof_        = true;
        return true;
    }

    // Total valid bits = (data bytes * 8) minus padding bits.
    total_bits_ = data_bytes * 8 - static_cast<uint64_t>(padding_);
    bits_read_  = 0;
    eof_        = (total_bits_ == 0);
    bits_left_  = 0;
    return true;
}

bool BitReader::load_next_byte()
{
    char c = 0;
    if (!file_.get(c)) {
        eof_ = true;
        return false;
    }
    buf_       = static_cast<uint8_t>(c);
    bits_left_ = 8;
    return true;
}

bool BitReader::read_bit(uint8_t& bit)
{
    if (eof_ || bits_read_ >= total_bits_) {
        eof_ = true;
        return false;
    }

    // Load a new byte when the current one is exhausted.
    if (bits_left_ == 0) {
        if (!load_next_byte())
            return false;
    }

    // Extract the MSB of the current buffer byte.
    --bits_left_;
    bit = (buf_ >> bits_left_) & 1u;
    ++bits_read_;

    if (bits_read_ >= total_bits_)
        eof_ = true;

    return true;
}

bool BitReader::read_bits(uint64_t& value, int num_bits)
{
    if (num_bits < 1 || num_bits > 64) {
        std::cerr << "BitReader: num_bits must be 1–64.\n";
        return false;
    }
    value = 0;
    for (int i = 0; i < num_bits; ++i) {
        uint8_t bit = 0;
        if (!read_bit(bit))
            return false;
        value = (value << 1) | bit;
    }
    return true;
}

void BitReader::close()
{
    file_.close();
    bits_left_ = 0;
    eof_       = true;
}

} // namespace ahlc
