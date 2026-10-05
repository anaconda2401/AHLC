#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ahlc {

/**
 * BMPImage — in-memory representation of an uncompressed BMP.
 *
 * pixels are stored top-to-bottom, no row padding.
 * Each row is exactly (width * bytes_per_pixel()) bytes.
 * Byte order follows BMP convention: BGR for 24-bit, BGRA for 32-bit.
 */
struct BMPImage {
    uint32_t width  = 0;
    uint32_t height = 0;
    uint16_t bpp    = 0;  // bits per pixel: 24 or 32

    std::vector<uint8_t> pixels; // raw pixel bytes, top-to-bottom, no padding

    uint32_t bytes_per_pixel() const { return bpp / 8u; }
    uint32_t row_bytes()       const { return width * bytes_per_pixel(); }
    uint64_t total_bytes()     const { return static_cast<uint64_t>(height) * row_bytes(); }
};

// ---- BMP I/O ----

// Read a 24-bit or 32-bit uncompressed BMP.
// Pixels are reordered to top-to-bottom regardless of BMP orientation.
bool read_bmp(const std::string& path, BMPImage& img);

// Write img as a standard bottom-up uncompressed BMP.
bool write_bmp(const std::string& path, const BMPImage& img);

// ---- Analysis ----

// Compute Shannon entropy (bits/byte) for a byte sequence.
double img_entropy(const std::vector<uint8_t>& data);

// Print an image analysis report to stdout.
void print_image_analysis(const std::string& filename, const BMPImage& img);

// Compare two BMPImages pixel-for-pixel.
// Returns true only if width, height, bpp, and all pixel bytes are identical.
bool images_equal(const BMPImage& a, const BMPImage& b);

} // namespace ahlc
