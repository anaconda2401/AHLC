#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <memory>

namespace ahlc {

// -----------------------------------------------------------------------
// Video TEABPS Predictor and Codec Identifiers
// -----------------------------------------------------------------------
static constexpr uint8_t VIDEO_TEABPS_PRED_NONE        = 0; // Raw pixel values (no temporal prediction)
static constexpr uint8_t VIDEO_TEABPS_PRED_TEMPORAL_D1 = 1; // 1st-order temporal delta: F[t] - F[t-1]
static constexpr uint8_t VIDEO_TEABPS_PRED_TEMPORAL_D2 = 2; // 2nd-order temporal delta: F[t] - (2*F[t-1] - F[t-2])
static constexpr uint8_t VIDEO_TEABPS_PRED_COUNT       = 3;

static constexpr uint8_t VIDEO_TEABPS_CODEC_RLE        = 0; // Run-Length Encoding
static constexpr uint8_t VIDEO_TEABPS_CODEC_HUFFMAN    = 1; // Canonical Huffman
static constexpr uint8_t VIDEO_TEABPS_CODEC_RAW        = 2; // Uncompressed residual
static constexpr uint8_t VIDEO_TEABPS_CODEC_COUNT      = 3;

static constexpr uint16_t VIDEO_TEABPS_DEFAULT_BLOCK_SIZE = 16; // 16x16 luma pixels
static constexpr uint8_t  VIDEO_TEABPS_PIX_FMT_YUV420P    = 1;
static constexpr uint8_t  AHLV_VERSION_TEABPS             = 3;
static const char         AHLV_MAGIC[4]                   = {'A', 'H', 'L', 'V'};

// -----------------------------------------------------------------------
// Raw Planar YUV Representation
// -----------------------------------------------------------------------
struct RawYUVPlane {
    uint32_t width  = 0;
    uint32_t height = 0;
    std::vector<uint8_t> data;
};

struct RawYUVFrame {
    RawYUVPlane planes[3]; // 0: Y (W x H), 1: U (W/2 x H/2), 2: V (W/2 x H/2)
    uint64_t total_bytes() const {
        return static_cast<uint64_t>(planes[0].data.size()) +
               static_cast<uint64_t>(planes[1].data.size()) +
               static_cast<uint64_t>(planes[2].data.size());
    }
};

struct RawYUVSequence {
    uint32_t width       = 0;
    uint32_t height      = 0;
    uint32_t fps_num     = 25;
    uint32_t fps_den     = 1;
    uint32_t frame_count = 0;
    std::vector<RawYUVFrame> frames;

    double fps() const {
        return fps_den > 0 ? static_cast<double>(fps_num) / fps_den : 25.0;
    }

    uint64_t raw_bytes() const {
        uint64_t sum = 0;
        for (const auto& f : frames) sum += f.total_bytes();
        return sum;
    }
};

// -----------------------------------------------------------------------
// Video TEABPS Block Record
// -----------------------------------------------------------------------
struct VideoTEABPSBlockRecord {
    uint32_t frame_idx    = 0;
    uint8_t  plane_idx    = 0; // 0=Y, 1=U, 2=V
    uint16_t bx           = 0; // block X coord in plane
    uint16_t by           = 0; // block Y coord in plane
    uint16_t bw           = 0; // actual block width (handles right boundary)
    uint16_t bh           = 0; // actual block height (handles bottom boundary)
    uint8_t  predictor    = VIDEO_TEABPS_PRED_NONE;
    uint8_t  codec        = VIDEO_TEABPS_CODEC_RAW;
    uint32_t payload_size = 0;
};

// -----------------------------------------------------------------------
// Video TEABPS Compression Statistics
// -----------------------------------------------------------------------
struct VideoTEABPSCompressStats {
    uint64_t original_file_size      = 0; // source file size if available
    uint64_t compressed_file_size    = 0; // .ahmc container size
    uint64_t original_raw_bytes      = 0; // uncompressed YUV byte count
    uint64_t encoded_payload_size    = 0; // sum of block payloads
    uint64_t predictor_metadata_size = 0; // 6 bytes per block
    uint64_t container_overhead      = 0; // header (41 bytes) + block table metadata

    uint32_t width                   = 0;
    uint32_t height                  = 0;
    double   fps                     = 0.0;
    uint32_t frame_count             = 0;
    uint16_t block_size              = VIDEO_TEABPS_DEFAULT_BLOCK_SIZE;
    uint32_t total_blocks            = 0;

    // Predictor distribution
    uint32_t count_none              = 0;
    uint32_t count_d1                = 0;
    uint32_t count_d2                = 0;

    // Codec distribution
    uint32_t count_rle               = 0;
    uint32_t count_huffman           = 0;
    uint32_t count_raw               = 0;

    double   entropy_before          = 0.0;
    double   entropy_after           = 0.0;
    double   compression_time_ms     = 0.0;
    double   decompression_time_ms   = 0.0;

    std::vector<VideoTEABPSBlockRecord> blocks;
};

// -----------------------------------------------------------------------
// Block Prediction and Reconstruction Functions
// -----------------------------------------------------------------------
std::vector<uint8_t> video_teabps_predict_block(const uint8_t* cur_plane,
                                                const uint8_t* prev1_plane,
                                                const uint8_t* prev2_plane,
                                                uint32_t plane_w,
                                                uint32_t bx, uint32_t by,
                                                uint32_t bw, uint32_t bh,
                                                uint8_t predictor);

bool video_teabps_reconstruct_block(const uint8_t* residuals,
                                    const uint8_t* prev1_plane,
                                    const uint8_t* prev2_plane,
                                    uint32_t plane_w,
                                    uint32_t bx, uint32_t by,
                                    uint32_t bw, uint32_t bh,
                                    uint8_t predictor,
                                    uint8_t* out_plane);

// -----------------------------------------------------------------------
// Sequence I/O and Comparison
// -----------------------------------------------------------------------
bool decode_video_to_yuv_sequence(const std::string& input_path,
                                  RawYUVSequence& seq,
                                  uint32_t max_frames = 0);

bool write_yuv_sequence(const std::string& output_path,
                        const RawYUVSequence& seq);

bool read_yuv_sequence(const std::string& input_path,
                       uint32_t width, uint32_t height,
                       uint32_t frame_count,
                       RawYUVSequence& seq);

bool yuv_sequences_equal(const RawYUVSequence& a, const RawYUVSequence& b);

// -----------------------------------------------------------------------
// TEABPS Core Compression & Decompression
// -----------------------------------------------------------------------
bool compress_video_teabps(const RawYUVSequence& seq,
                           const std::string& ahmc_path,
                           VideoTEABPSCompressStats& stats,
                           uint16_t block_size = VIDEO_TEABPS_DEFAULT_BLOCK_SIZE);

bool decompress_video_teabps(const std::string& ahmc_path,
                             RawYUVSequence& seq);

bool compress_video_file_teabps(const std::string& video_path,
                                const std::string& ahmc_path,
                                VideoTEABPSCompressStats& stats,
                                uint16_t block_size = VIDEO_TEABPS_DEFAULT_BLOCK_SIZE,
                                uint32_t max_frames = 0);

void print_video_teabps_stats(const std::string& in_path,
                              const std::string& ahmc_path,
                              const VideoTEABPSCompressStats& stats);

// -----------------------------------------------------------------------
// Automated Test Suite & Benchmark
// -----------------------------------------------------------------------
bool run_video_teabps_test_suite();
bool run_video_teabps_benchmark();

} // namespace ahlc
