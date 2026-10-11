#pragma once
/**
 * audio_buffer.hpp  —  Common internal audio representation.
 *
 * RawAudioBuffer is the target format every audio decoder must produce.
 * It holds interleaved PCM samples.
 */

#include <cstdint>
#include <vector>
#include <string>

namespace ahlc::media {

// ----------------------------------------------------------------
// Sample format
// ----------------------------------------------------------------
enum class SampleFormat {
    Unknown,
    U8,        // Unsigned 8-bit
    S16LE,     // Signed 16-bit little-endian
    S24LE,     // Signed 24-bit little-endian
    S32LE,     // Signed 32-bit little-endian
    FLT,       // 32-bit float
    DBL,       // 64-bit double
};

inline int bytes_per_sample_of(SampleFormat fmt) {
    switch (fmt) {
        case SampleFormat::U8:    return 1;
        case SampleFormat::S16LE: return 2;
        case SampleFormat::S24LE: return 3;
        case SampleFormat::S32LE:
        case SampleFormat::FLT:   return 4;
        case SampleFormat::DBL:   return 8;
        default:                  return 0;
    }
}

// ----------------------------------------------------------------
// RawAudioBuffer — the common audio representation used by AHLC
// ----------------------------------------------------------------
struct RawAudioBuffer {
    uint32_t     sample_rate = 0;
    uint32_t     channels    = 0;
    SampleFormat sample_fmt  = SampleFormat::Unknown;

    // The raw interleaved PCM samples
    std::vector<uint8_t> pcm;

    // Derived helpers
    int      bytes_per_sample() const { return bytes_per_sample_of(sample_fmt); }
    int      bytes_per_frame()  const { return channels * bytes_per_sample(); }
    uint64_t total_frames()     const { return bytes_per_frame() > 0 ? (pcm.size() / bytes_per_frame()) : 0; }
    double   duration_sec()     const { return sample_rate > 0 ? (static_cast<double>(total_frames()) / sample_rate) : 0.0; }

    bool valid() const {
        return sample_rate > 0 && channels > 0 &&
               sample_fmt != SampleFormat::Unknown &&
               pcm.size() > 0 &&
               (pcm.size() % bytes_per_frame() == 0);
    }

    // Source file info
    std::string source_path;
    std::string source_format; // "WAV", "FLAC", etc.
};

} // namespace ahlc::media
