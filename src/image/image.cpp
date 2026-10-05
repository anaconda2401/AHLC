#include "image.hpp"

#include <fstream>
#include <iostream>
#include <iomanip>
#include <array>
#include <cmath>
#include <cstring>

namespace ahlc {

// ---------------------------------------------------------------
// Little-endian helpers (shared with bmp_reader.cpp via static)
// ---------------------------------------------------------------
static uint16_t rd_u16(std::ifstream& f) {
    uint8_t b[2] = {}; f.read(reinterpret_cast<char*>(b), 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}
static uint32_t rd_u32(std::ifstream& f) {
    uint8_t b[4] = {}; f.read(reinterpret_cast<char*>(b), 4);
    return b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24);
}
static int32_t rd_i32(std::ifstream& f) {
    return static_cast<int32_t>(rd_u32(f));
}

static void wr_u16(std::ofstream& f, uint16_t v) {
    f.put(static_cast<char>(v & 0xFF));
    f.put(static_cast<char>((v >> 8) & 0xFF));
}
static void wr_u32(std::ofstream& f, uint32_t v) {
    f.put(static_cast<char>(v & 0xFF));
    f.put(static_cast<char>((v >> 8) & 0xFF));
    f.put(static_cast<char>((v >> 16) & 0xFF));
    f.put(static_cast<char>((v >> 24) & 0xFF));
}
static void wr_i32(std::ofstream& f, int32_t v) {
    wr_u32(f, static_cast<uint32_t>(v));
}

// ---------------------------------------------------------------
// read_bmp
// ---------------------------------------------------------------
bool read_bmp(const std::string& path, BMPImage& img)
{
    img = {};
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "BMP: cannot open: " << path << "\n"; return false; }

    // --- File header (14 bytes) ---
    uint16_t bf_type    = rd_u16(f);
    if (bf_type != 0x4D42) { std::cerr << "BMP: not a BMP file.\n"; return false; }
    /*uint32_t bf_size =*/ rd_u32(f);
    rd_u16(f); rd_u16(f);               // reserved
    uint32_t bf_off_bits = rd_u32(f);   // offset to pixel data

    // --- Info header (at least 40 bytes) ---
    uint32_t bi_size        = rd_u32(f);
    if (bi_size < 40) { std::cerr << "BMP: unsupported header size.\n"; return false; }
    int32_t  bi_width       = rd_i32(f);
    int32_t  bi_height      = rd_i32(f);
    /*uint16_t bi_planes =*/ rd_u16(f);
    uint16_t bi_bit_count   = rd_u16(f);
    uint32_t bi_compression = rd_u32(f);
    /*uint32_t bi_size_image=*/ rd_u32(f);
    rd_i32(f); rd_i32(f);               // pixels per meter
    rd_u32(f); rd_u32(f);               // clr used / important

    if (bi_compression != 0) {
        std::cerr << "BMP: only uncompressed (BI_RGB) supported.\n"; return false;
    }
    if (bi_bit_count != 24 && bi_bit_count != 32) {
        std::cerr << "BMP: only 24-bit and 32-bit supported.\n"; return false;
    }
    if (bi_width <= 0) {
        std::cerr << "BMP: invalid width.\n"; return false;
    }

    bool top_down = (bi_height < 0);
    img.width  = static_cast<uint32_t>(bi_width);
    img.height = static_cast<uint32_t>(top_down ? -bi_height : bi_height);
    img.bpp    = bi_bit_count;

    uint32_t row_bytes = img.row_bytes();
    // BMP rows are padded to 4-byte boundaries in the file.
    uint32_t file_row_stride = ((row_bytes + 3) / 4) * 4;
    uint32_t padding         = file_row_stride - row_bytes;

    img.pixels.resize(img.height * row_bytes);

    // Seek to pixel data start.
    f.seekg(static_cast<std::streamoff>(bf_off_bits), std::ios::beg);

    std::vector<char> pad_buf(4, 0);

    if (top_down) {
        for (uint32_t row = 0; row < img.height; ++row) {
            f.read(reinterpret_cast<char*>(img.pixels.data() + row * row_bytes), row_bytes);
            if (padding) f.read(pad_buf.data(), padding);
        }
    } else {
        // Standard BMP: rows stored bottom-up. Read in reverse so pixels are top-to-bottom.
        for (uint32_t row = img.height; row-- > 0;) {
            f.read(reinterpret_cast<char*>(img.pixels.data() + row * row_bytes), row_bytes);
            if (padding) f.read(pad_buf.data(), padding);
        }
    }

    return true;
}

// ---------------------------------------------------------------
// write_bmp  — always bottom-up, matching the original bpp
// ---------------------------------------------------------------
bool write_bmp(const std::string& path, const BMPImage& img)
{
    if (img.width == 0 || img.height == 0) {
        std::cerr << "BMP: cannot write empty image.\n"; return false;
    }

    uint32_t row_bytes       = img.row_bytes();
    uint32_t file_row_stride = ((row_bytes + 3) / 4) * 4;
    uint32_t padding         = file_row_stride - row_bytes;
    uint32_t pixel_data_size = file_row_stride * img.height;
    uint32_t bf_off_bits     = 14u + 40u;    // no color table for 24/32-bit
    uint32_t file_size       = bf_off_bits + pixel_data_size;

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { std::cerr << "BMP: cannot create: " << path << "\n"; return false; }

    // File header
    wr_u16(f, 0x4D42);          // 'BM'
    wr_u32(f, file_size);
    wr_u16(f, 0); wr_u16(f, 0); // reserved
    wr_u32(f, bf_off_bits);

    // Info header (BITMAPINFOHEADER, 40 bytes)
    wr_u32(f, 40);
    wr_i32(f, static_cast<int32_t>(img.width));
    wr_i32(f, static_cast<int32_t>(img.height)); // positive = bottom-up
    wr_u16(f, 1);                // biPlanes
    wr_u16(f, img.bpp);
    wr_u32(f, 0);                // BI_RGB
    wr_u32(f, pixel_data_size);
    wr_i32(f, 2835); wr_i32(f, 2835); // ~72 DPI
    wr_u32(f, 0); wr_u32(f, 0); // clrUsed, clrImportant

    // Pixel data — write rows in reverse (bottom row first) for bottom-up BMP.
    static const char zeros[4] = {0, 0, 0, 0};
    for (uint32_t row = img.height; row-- > 0;) {
        f.write(reinterpret_cast<const char*>(img.pixels.data() + row * row_bytes), row_bytes);
        if (padding) f.write(zeros, padding);
    }

    return true;
}

// ---------------------------------------------------------------
// img_entropy — Shannon entropy of a byte sequence
// ---------------------------------------------------------------
double img_entropy(const std::vector<uint8_t>& data)
{
    if (data.empty()) return 0.0;
    std::array<uint64_t, 256> freq = {};
    for (uint8_t b : data) freq[b]++;
    double h = 0.0;
    double n = static_cast<double>(data.size());
    for (auto c : freq) {
        if (c == 0) continue;
        double p = static_cast<double>(c) / n;
        h -= p * std::log2(p);
    }
    return h;
}

// ---------------------------------------------------------------
// print_image_analysis
// ---------------------------------------------------------------
void print_image_analysis(const std::string& filename, const BMPImage& img)
{
    double entropy = img_entropy(img.pixels);

    // Average per channel (BGR order)
    double sum_b = 0, sum_g = 0, sum_r = 0;
    uint32_t Bpp = img.bytes_per_pixel();
    uint64_t pixel_count = static_cast<uint64_t>(img.width) * img.height;

    for (uint32_t row = 0; row < img.height; ++row) {
        for (uint32_t col = 0; col < img.width; ++col) {
            uint32_t base = (row * img.width + col) * Bpp;
            sum_b += img.pixels[base + 0];
            sum_g += img.pixels[base + 1];
            if (Bpp >= 3) sum_r += img.pixels[base + 2];
        }
    }
    double avg_b = (pixel_count > 0) ? sum_b / pixel_count : 0;
    double avg_g = (pixel_count > 0) ? sum_g / pixel_count : 0;
    double avg_r = (pixel_count > 0) ? sum_r / pixel_count : 0;

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "--------------------------------------------\n";
    std::cout << " AHLC Image Analysis\n";
    std::cout << "--------------------------------------------\n";
    std::cout << " File            : " << filename        << "\n";
    std::cout << " Width           : " << img.width       << " px\n";
    std::cout << " Height          : " << img.height      << " px\n";
    std::cout << " Bits per pixel  : " << img.bpp         << "\n";
    std::cout << " Raw pixel bytes : " << img.total_bytes()<< "\n";
    std::cout << " Avg B/G/R       : "
              << avg_b << " / " << avg_g << " / " << avg_r << "\n";
    std::cout << " Entropy         : " << entropy << " bits/byte\n";
    std::cout << "--------------------------------------------\n";
}

// ---------------------------------------------------------------
// images_equal
// ---------------------------------------------------------------
bool images_equal(const BMPImage& a, const BMPImage& b)
{
    if (a.width != b.width || a.height != b.height || a.bpp != b.bpp)
        return false;
    return a.pixels == b.pixels;
}

} // namespace ahlc
