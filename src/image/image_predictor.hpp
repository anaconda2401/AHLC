#pragma once

#include "image.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace ahlc {

// -----------------------------------------------------------------------
// Predictor and codec type IDs — stored in the .ahmc header.
// -----------------------------------------------------------------------
static constexpr uint8_t PREDICTOR_LEFT  = 1; // left-neighbour per channel

static constexpr uint8_t CODEC_RLE     = 0;  // packet-based RLE
static constexpr uint8_t CODEC_HUFFMAN = 1;  // canonical Huffman

// -----------------------------------------------------------------------
// Spatial predictor — encode
// residual = (current − left + 256) % 256 per channel byte
// Returned vector has the same length as img.pixels.
// -----------------------------------------------------------------------
std::vector<uint8_t> encode_spatial_predictor(const BMPImage& img);

// -----------------------------------------------------------------------
// Spatial predictor — decode (exact inverse)
// img.width, img.height, img.bpp must be set before calling.
// -----------------------------------------------------------------------
bool decode_spatial_predictor(const std::vector<uint8_t>& residuals, BMPImage& img);

// -----------------------------------------------------------------------
// Print predictor analysis (for ahmc image-predict command)
// -----------------------------------------------------------------------
void print_predictor_analysis(const BMPImage& img,
                              const std::vector<uint8_t>& residuals);

// -----------------------------------------------------------------------
// Image compression statistics
// -----------------------------------------------------------------------
struct ImageCompressStats {
    uint64_t original_file_size      = 0;  // full BMP file in bytes
    uint64_t compressed_file_size    = 0;  // full .ahmc file in bytes
    uint64_t original_pixel_bytes    = 0;  // uncompressed pixel data
    uint64_t compressed_data_size    = 0;  // residual after codec
    double   entropy_before          = 0.0;
    double   entropy_after           = 0.0; // residual entropy
    uint8_t  predictor_type          = PREDICTOR_LEFT;
    // Adaptive codec selection
    uint64_t rle_candidate_size      = 0;  // RLE residual bytes
    uint64_t huffman_candidate_size  = 0;  // Huffman residual bytes
    uint8_t  codec_used              = CODEC_RLE;
    // Image metadata (populated by compress_image for JSON output)
    uint32_t width                   = 0;
    uint32_t height                  = 0;
    uint16_t bpp                     = 0;
};

// -----------------------------------------------------------------------
// Full adaptive pipeline: BMP → predictor → (RLE vs Huffman) → .ahmc
// Selects whichever codec produces the smaller residual output.
// The chosen codec ID is stored in the .ahmc header (format v2).
// -----------------------------------------------------------------------
bool compress_image(const std::string& bmp_path,
                    const std::string& ahmc_path,
                    ImageCompressStats& stats);

// -----------------------------------------------------------------------
// Decompress .ahmc → BMP.
// Handles both v1 (RLE only) and v2 (adaptive codec field) formats.
// -----------------------------------------------------------------------
bool decompress_image(const std::string& ahmc_path,
                      const std::string& bmp_path);

// Print adaptive compression report
void print_image_compress_stats(const std::string& bmp_path,
                                const std::string& ahmc_path,
                                const ImageCompressStats& stats);

} // namespace ahlc
