#pragma once

#include "audio.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace ahlc {

// -----------------------------------------------------------------------
// Predictor and codec type IDs — stored in the .ahmc audio header.
// -----------------------------------------------------------------------
static constexpr uint8_t AUDIO_PREDICTOR_DELTA = 1; // predicted = previous sample

static constexpr uint8_t AUDIO_CODEC_RLE     = 0;  // packet-based RLE
static constexpr uint8_t AUDIO_CODEC_HUFFMAN = 1;  // canonical Huffman

// -----------------------------------------------------------------------
// Delta predictor — encode
// Each channel is processed independently.
// Output: de-interleaved [ch0 residuals | ch1 residuals | ...]
// Residuals use the same byte width as samples (modular wrapping).
// -----------------------------------------------------------------------
std::vector<uint8_t> encode_audio_predictor(const AudioBuffer& audio);

// -----------------------------------------------------------------------
// Delta predictor — decode (exact inverse)
// audio.channels, audio.total_samples, audio.bits_per_sample must be set.
// -----------------------------------------------------------------------
bool decode_audio_predictor(const std::vector<uint8_t>& residuals, AudioBuffer& audio);

// -----------------------------------------------------------------------
// Print predictor analysis (for ahmc audio-predict)
// -----------------------------------------------------------------------
void print_audio_predictor_analysis(const AudioBuffer& audio,
                                    const std::vector<uint8_t>& residuals);

// -----------------------------------------------------------------------
// Audio compression statistics
// -----------------------------------------------------------------------
struct AudioCompressStats {
    uint64_t original_file_size      = 0;
    uint64_t compressed_file_size    = 0;
    uint64_t original_pcm_bytes      = 0;
    uint64_t compressed_data_size    = 0;
    double   entropy_before          = 0.0;
    double   entropy_after           = 0.0;
    uint8_t  predictor_type          = AUDIO_PREDICTOR_DELTA;
    // Adaptive codec selection
    uint64_t rle_candidate_size      = 0;
    uint64_t huffman_candidate_size  = 0;
    uint8_t  codec_used              = AUDIO_CODEC_RLE;
    // Audio metadata (populated by compress_audio for JSON output)
    uint16_t channels                = 0;
    uint32_t sample_rate             = 0;
    uint16_t bits_per_sample         = 0;
    uint64_t total_samples           = 0;
};

// -----------------------------------------------------------------------
// Full adaptive pipeline: WAV → delta predictor → (RLE vs Huffman) → .ahmc
// -----------------------------------------------------------------------
bool compress_audio(const std::string& wav_path,
                    const std::string& ahmc_path,
                    AudioCompressStats& stats);

// -----------------------------------------------------------------------
// Decompress .ahmc → WAV.
// Handles both v1 (RLE only) and v2 (adaptive codec field) formats.
// -----------------------------------------------------------------------
bool decompress_audio(const std::string& ahmc_path,
                      const std::string& wav_path);

// Print adaptive compression report
void print_audio_compress_stats(const std::string& wav_path,
                                const std::string& ahmc_path,
                                const AudioCompressStats& stats);

} // namespace ahlc
