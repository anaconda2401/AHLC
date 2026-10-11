#pragma once
/**
 * image_frame.hpp  —  Common internal image representation.
 *
 * RawImageFrame is the target format every image decoder must produce.
 * It is format-agnostic: pixels are stored as top-to-bottom, left-to-right,
 * interleaved bytes (e.g. RGB or RGBA, 8 or 16 bits per channel).
 *
 * The existing BMPImage in image/image.hpp is the BMP-specific reader;
 * it can be converted into a RawImageFrame via to_raw_frame() (image.hpp).
 * Future PNG/JPEG decoders will produce RawImageFrame directly.
 */

#include <cstdint>
#include <vector>
#include <string>

namespace ahlc::media {

// ----------------------------------------------------------------
// Pixel format — byte layout in the pixel buffer
// ----------------------------------------------------------------
enum class PixelFormat {
    Unknown,
    Gray8,     // 1 byte/pixel
    RGB8,      // 3 bytes/pixel — R G B
    BGR8,      // 3 bytes/pixel — B G R  (native BMP order)
    RGBA8,     // 4 bytes/pixel — R G B A
    BGRA8,     // 4 bytes/pixel — B G R A (native BMP 32-bit order)
    Gray16LE,  // 2 bytes/pixel, little-endian
    RGB16LE,   // 6 bytes/pixel, little-endian per channel
};

inline int channels_of(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::Gray8:   case PixelFormat::Gray16LE: return 1;
        case PixelFormat::RGB8:    case PixelFormat::BGR8:     return 3;
        case PixelFormat::RGB16LE:                             return 3;
        case PixelFormat::RGBA8:   case PixelFormat::BGRA8:    return 4;
        default:                                               return 0;
    }
}

inline int bytes_per_channel_of(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::Gray16LE:
        case PixelFormat::RGB16LE: return 2;
        default:                   return 1;
    }
}

// ----------------------------------------------------------------
// RawImageFrame — the common image representation used by AHLC
// ----------------------------------------------------------------
struct RawImageFrame {
    uint32_t    width       = 0;
    uint32_t    height      = 0;
    PixelFormat pixel_fmt   = PixelFormat::Unknown;
    int         bit_depth   = 8;    // bits per channel (8 or 16)

    // Top-to-bottom, left-to-right, no row padding.
    // Size must equal width * height * channels_of(pixel_fmt) * bytes_per_channel_of(pixel_fmt).
    std::vector<uint8_t> pixels;

    // Derived helpers
    int      channel_count()   const { return channels_of(pixel_fmt); }
    int      bytes_per_pixel() const { return channel_count() * bytes_per_channel_of(pixel_fmt); }
    uint64_t row_bytes()       const { return static_cast<uint64_t>(width) * bytes_per_pixel(); }
    uint64_t total_bytes()     const { return static_cast<uint64_t>(height) * row_bytes(); }

    bool valid() const {
        return width > 0 && height > 0 &&
               pixel_fmt != PixelFormat::Unknown &&
               pixels.size() == total_bytes();
    }

    // Source file info (optional, set by decoders)
    std::string source_path;
    std::string source_format; // "BMP", "PNG", etc.
};

} // namespace ahlc::media
