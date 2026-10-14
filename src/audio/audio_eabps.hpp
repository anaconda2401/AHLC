#pragma once

#include "audio.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace ahlc {

// -----------------------------------------------------------------------
// Audio EABPS Predictor and Codec Identifiers
// -----------------------------------------------------------------------
static constexpr uint8_t AUDIO_EABPS_PRED_NONE   = 0; // Raw sample
static constexpr uint8_t AUDIO_EABPS_PRED_DELTA1 = 1; // 1st-order delta: s[t] - s[t-1]
static constexpr uint8_t AUDIO_EABPS_PRED_DELTA2 = 2; // 2nd-order delta: s[t] - (2*s[t-1] - s[t-2])
static constexpr uint8_t AUDIO_EABPS_PRED_COUNT  = 3;

static constexpr uint8_t AUDIO_EABPS_CODEC_RLE     = 0; // Packet-based RLE
static constexpr uint8_t AUDIO_EABPS_CODEC_HUFFMAN = 1; // Canonical Huffman
static constexpr uint8_t AUDIO_EABPS_CODEC_RAW     = 2; // Uncompressed residual
static constexpr uint8_t AUDIO_EABPS_CODEC_COUNT   = 3;

static constexpr uint32_t AUDIO_EABPS_DEFAULT_BLOCK_SIZE = 4096; // sample frames per block
static constexpr uint8_t  AHLA_VERSION_EABPS             = 3;

// -----------------------------------------------------------------------
// Audio EABPS Block Record
// -----------------------------------------------------------------------
struct AudioEABPSBlockRecord {
    uint32_t frame_offset = 0;
    uint32_t num_frames   = 0;
    uint8_t  predictor    = AUDIO_EABPS_PRED_NONE;
    uint8_t  codec        = AUDIO_EABPS_CODEC_RAW;
    uint32_t payload_size = 0;
};

// -----------------------------------------------------------------------
// Audio EABPS Compression Statistics
// -----------------------------------------------------------------------
struct AudioEABPSCompressStats {
    uint64_t original_file_size      = 0; // original WAV file in bytes
    uint64_t compressed_file_size    = 0; // full .ahmc file in bytes
    uint64_t original_pcm_bytes      = 0; // uncompressed PCM bytes
    uint64_t encoded_payload_size    = 0; // sum of block payloads
    uint64_t predictor_metadata_size = 0; // 6 bytes per block
    uint64_t container_overhead      = 0; // header (31 bytes) + metadata

    uint16_t format                  = 1;
    uint16_t channels                = 0;
    uint32_t sample_rate             = 0;
    uint16_t bits_per_sample         = 0;
    uint64_t total_samples           = 0; // total frames
    uint32_t block_size              = AUDIO_EABPS_DEFAULT_BLOCK_SIZE;
    uint32_t total_blocks            = 0;

    // Predictor selection distribution
    uint32_t count_none              = 0;
    uint32_t count_delta1            = 0;
    uint32_t count_delta2            = 0;

    // Codec selection distribution
    uint32_t count_rle               = 0;
    uint32_t count_huffman           = 0;
    uint32_t count_raw               = 0;

    double   entropy_before          = 0.0;
    double   entropy_after           = 0.0; // combined residual entropy
    double   compression_time_ms     = 0.0;
    double   decompression_time_ms   = 0.0;

    std::vector<AudioEABPSBlockRecord> blocks;
};

// -----------------------------------------------------------------------
// Block Prediction and Reconstruction Functions
// -----------------------------------------------------------------------
std::vector<uint8_t> audio_eabps_predict_block(const uint8_t* pcm_data,
                                               uint32_t num_frames,
                                               uint16_t channels,
                                               uint16_t bits_per_sample,
                                               uint8_t predictor);

bool audio_eabps_reconstruct_block(const std::vector<uint8_t>& residuals,
                                   uint32_t num_frames,
                                   uint16_t channels,
                                   uint16_t bits_per_sample,
                                   uint8_t predictor,
                                   uint8_t* out_pcm);

// -----------------------------------------------------------------------
// Full Audio EABPS Pipeline
// -----------------------------------------------------------------------
bool compress_audio_eabps(const std::string& wav_path,
                          const std::string& ahmc_path,
                          AudioEABPSCompressStats& stats,
                          uint32_t block_size = AUDIO_EABPS_DEFAULT_BLOCK_SIZE);

bool decompress_audio_eabps(const std::string& ahmc_path,
                            const std::string& wav_path);

void print_audio_eabps_stats(const std::string& wav_path,
                             const std::string& ahmc_path,
                             const AudioEABPSCompressStats& stats);

// -----------------------------------------------------------------------
// Automated Audio EABPS Test Suite & Benchmark
// -----------------------------------------------------------------------
bool run_audio_eabps_test_suite();
bool run_audio_eabps_benchmark();

} // namespace ahlc
