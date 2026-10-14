#pragma once

#include "image.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace ahlc {

// -----------------------------------------------------------------------
// EABPS Predictor and Codec Identifiers
// -----------------------------------------------------------------------
static constexpr uint8_t EABPS_PRED_NONE = 0; // No prediction (raw sample)
static constexpr uint8_t EABPS_PRED_LEFT = 1; // Left-neighbour sample
static constexpr uint8_t EABPS_PRED_UP   = 2; // Upper-neighbour sample
static constexpr uint8_t EABPS_PRED_COUNT = 3;

static constexpr uint8_t EABPS_CODEC_RLE     = 0; // Packet-based RLE
static constexpr uint8_t EABPS_CODEC_HUFFMAN = 1; // Canonical Huffman
static constexpr uint8_t EABPS_CODEC_RAW     = 2; // Uncompressed residual
static constexpr uint8_t EABPS_CODEC_COUNT   = 3;

static constexpr uint16_t EABPS_DEFAULT_BLOCK_SIZE = 16;
static constexpr uint8_t  AHMC_VERSION_EABPS       = 3;

// -----------------------------------------------------------------------
// EABPS Block Information & Metadata
// -----------------------------------------------------------------------
struct EABPSBlockRecord {
    uint32_t x0           = 0;
    uint32_t y0           = 0;
    uint32_t width        = 0;
    uint32_t height       = 0;
    uint8_t  predictor    = EABPS_PRED_NONE;
    uint8_t  codec        = EABPS_CODEC_RAW;
    uint32_t payload_size = 0;
};

// -----------------------------------------------------------------------
// Compression Statistics for EABPS
// -----------------------------------------------------------------------
struct EABPSCompressStats {
    uint64_t original_file_size      = 0; // full BMP file in bytes
    uint64_t compressed_file_size    = 0; // full .ahmc file in bytes
    uint64_t original_pixel_bytes    = 0; // uncompressed pixel data
    uint64_t encoded_payload_size    = 0; // sum of block payloads
    uint64_t predictor_metadata_size = 0; // per-block metadata bytes
    uint64_t container_overhead      = 0; // headers + block metadata

    uint32_t width                   = 0;
    uint32_t height                  = 0;
    uint16_t bpp                     = 0;
    uint16_t block_size              = EABPS_DEFAULT_BLOCK_SIZE;
    uint32_t num_blocks_x            = 0;
    uint32_t num_blocks_y            = 0;
    uint32_t total_blocks            = 0;

    // Predictor selection distribution
    uint32_t count_none              = 0;
    uint32_t count_left              = 0;
    uint32_t count_up                = 0;

    // Codec selection distribution
    uint32_t count_rle               = 0;
    uint32_t count_huffman           = 0;
    uint32_t count_raw               = 0;

    double   entropy_before          = 0.0;
    double   entropy_after           = 0.0; // combined residual entropy
    double   compression_time_ms     = 0.0;
    double   decompression_time_ms   = 0.0;

    // Detailed per-block records
    std::vector<EABPSBlockRecord> blocks;
};

// -----------------------------------------------------------------------
// Pure block prediction and inverse reconstruction (standalone)
// Useful for unit testing on arbitrary block dimensions (1x1, 1xN, Nx1, etc.)
// -----------------------------------------------------------------------
std::vector<uint8_t> eabps_predict_block_pixels(const std::vector<uint8_t>& block_pixels,
                                                uint32_t bw, uint32_t bh, uint32_t bpp,
                                                uint8_t predictor);

bool eabps_reconstruct_block_pixels(const std::vector<uint8_t>& residuals,
                                    uint32_t bw, uint32_t bh, uint32_t bpp,
                                    uint8_t predictor,
                                    std::vector<uint8_t>& out_pixels);

// -----------------------------------------------------------------------
// Block prediction & reconstruction directly against a full BMPImage
// -----------------------------------------------------------------------
std::vector<uint8_t> eabps_extract_and_predict_block(const BMPImage& img,
                                                     uint32_t x0, uint32_t y0,
                                                     uint32_t bw, uint32_t bh,
                                                     uint8_t predictor);

bool eabps_reconstruct_and_insert_block(BMPImage& img,
                                        uint32_t x0, uint32_t y0,
                                        uint32_t bw, uint32_t bh,
                                        uint8_t predictor,
                                        const std::vector<uint8_t>& residuals);

// -----------------------------------------------------------------------
// Core EABPS Encoder and Decoder
// -----------------------------------------------------------------------
bool compress_image_eabps(const std::string& bmp_path,
                          const std::string& ahmc_path,
                          EABPSCompressStats& stats,
                          uint16_t block_size = EABPS_DEFAULT_BLOCK_SIZE);

bool decompress_image_eabps(const std::string& ahmc_path,
                            const std::string& bmp_path);

// Print detailed EABPS report to stdout
void print_eabps_stats(const std::string& bmp_path,
                       const std::string& ahmc_path,
                       const EABPSCompressStats& stats);

// -----------------------------------------------------------------------
// Comprehensive EABPS Test Suite and Benchmark Runner
// -----------------------------------------------------------------------
bool run_eabps_test_suite();
bool run_eabps_benchmark();

} // namespace ahlc
