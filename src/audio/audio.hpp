#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ahlc {

/**
 * AudioBuffer — in-memory representation of a PCM WAV file.
 *
 * pcm holds raw interleaved PCM bytes exactly as stored in the WAV data chunk.
 * Byte order is little-endian for all sample widths.
 * 8-bit PCM is unsigned (0–255). 16/24/32-bit PCM is signed.
 *
 * total_samples = number of frames (one frame = one sample per channel).
 */
struct AudioBuffer {
    uint16_t format          = 1;  // 1 = PCM
    uint16_t channels        = 0;
    uint32_t sample_rate     = 0;
    uint16_t bits_per_sample = 0;
    uint64_t total_samples   = 0;  // frames (samples per channel)

    std::vector<uint8_t> pcm; // raw interleaved PCM bytes

    uint32_t bytes_per_sample() const { return bits_per_sample / 8u; }
    uint32_t block_align()      const { return channels * bytes_per_sample(); }
    uint64_t pcm_bytes()        const { return total_samples * block_align(); }
    double   duration_sec()     const {
        return (sample_rate > 0) ? static_cast<double>(total_samples) / sample_rate : 0.0;
    }
};

// ---- WAV I/O ----

// Read a PCM WAV file. Handles fmt/data chunks in any order.
// Supports 8/16/24/32-bit, mono or stereo.
bool read_wav(const std::string& path, AudioBuffer& audio);

// Write a valid PCM WAV file from an AudioBuffer.
bool write_wav(const std::string& path, const AudioBuffer& audio);

// ---- Utilities ----

// Shannon entropy of raw PCM bytes.
double audio_entropy(const std::vector<uint8_t>& data);

// Print a formatted audio analysis report.
void print_audio_analysis(const std::string& filename, const AudioBuffer& audio);

// True only if both AudioBuffers have identical format fields and identical pcm bytes.
bool audios_equal(const AudioBuffer& a, const AudioBuffer& b);

} // namespace ahlc
