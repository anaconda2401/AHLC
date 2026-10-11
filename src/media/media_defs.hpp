#pragma once
/**
 * media_defs.hpp  —  AHLC format-abstraction layer: common enumerations.
 *
 * These types are shared by the format detector, decoder registry, and
 * every future format-specific decoder.  They do NOT touch or replace
 * the existing BMPImage / AudioBuffer internal representations; they sit
 * above them as a discovery / routing layer.
 */

#include <string>
#include <cstdint>

namespace ahlc::media {

// ----------------------------------------------------------------
// MediaType — broad category of the content
// ----------------------------------------------------------------
enum class MediaType {
    Unknown,
    Image,
    Audio,
    Video,
};

// ----------------------------------------------------------------
// MediaFormat — specific container / codec format
// ----------------------------------------------------------------
enum class MediaFormat {
    Unknown,
    // Raster image
    BMP,
    PNG,
    JPEG,
    TIFF,
    WebP,
    GIF,
    // Lossless audio
    WAV,
    FLAC,
    AIFF,
    // Lossy audio
    MP3,
    AAC,
    OGG,
    // Video containers
    MP4,
    MKV,
    AVI,
    MOV,
    WebM,
    FLV,
};

// ----------------------------------------------------------------
// SourceCompression — compression used by the *source* format
// (not the AHLC compression we are about to apply)
// ----------------------------------------------------------------
enum class SourceCompression {
    Unknown,
    Uncompressed,  // BMP, WAV/PCM, AIFF/PCM
    Lossless,      // PNG, FLAC, GIF (LZW)
    Lossy,         // JPEG, MP3, AAC, OGG
};

// ----------------------------------------------------------------
// DecoderStatus — is a decoder currently available in this build?
// ----------------------------------------------------------------
enum class DecoderStatus {
    Available,              // decoder implemented and tested
    PendingIntegration,     // FFmpeg can read it; AHLC wrapper not yet wired
    Unsupported,            // cannot decode (format unknown or explicitly excluded)
};

// ----------------------------------------------------------------
// FormatInfo — result returned by the format detector
// ----------------------------------------------------------------
struct FormatInfo {
    MediaType        media_type          = MediaType::Unknown;
    MediaFormat      format              = MediaFormat::Unknown;
    SourceCompression source_compression = SourceCompression::Unknown;
    DecoderStatus    decoder_status      = DecoderStatus::Unsupported;

    // Human-readable strings
    std::string format_name;        // e.g. "BMP", "JPEG", "FLAC"
    std::string codec_name;         // FFmpeg codec name, e.g. "bmp", "mjpeg"
    std::string ffmpeg_fmt_name;    // raw FFmpeg iformat->name

    // Basic metadata (filled in where detectable from headers only)
    // Image
    int width    = 0;
    int height   = 0;
    int channels = 0;
    int bit_depth = 0;
    // Audio
    int    sample_rate     = 0;
    int    audio_channels  = 0;
    int    bits_per_sample = 0;
    double duration_sec    = 0.0;
    // Video
    double frame_rate    = 0.0;
    int64_t frame_count  = -1;  // -1 = unknown

    bool ok = false;   // false if detection itself failed (file unreadable etc.)
    std::string error_message;
};

// ----------------------------------------------------------------
// Helper: human-readable names for enums
// ----------------------------------------------------------------
inline const char* to_string(MediaType t) {
    switch (t) {
        case MediaType::Image: return "Image";
        case MediaType::Audio: return "Audio";
        case MediaType::Video: return "Video";
        default:               return "Unknown";
    }
}

inline const char* to_string(MediaFormat f) {
    switch (f) {
        case MediaFormat::BMP:  return "BMP";
        case MediaFormat::PNG:  return "PNG";
        case MediaFormat::JPEG: return "JPEG";
        case MediaFormat::TIFF: return "TIFF";
        case MediaFormat::WebP: return "WebP";
        case MediaFormat::GIF:  return "GIF";
        case MediaFormat::WAV:  return "WAV";
        case MediaFormat::FLAC: return "FLAC";
        case MediaFormat::AIFF: return "AIFF";
        case MediaFormat::MP3:  return "MP3";
        case MediaFormat::AAC:  return "AAC";
        case MediaFormat::OGG:  return "OGG";
        case MediaFormat::MP4:  return "MP4";
        case MediaFormat::MKV:  return "MKV";
        case MediaFormat::AVI:  return "AVI";
        case MediaFormat::MOV:  return "MOV";
        case MediaFormat::WebM: return "WebM";
        case MediaFormat::FLV:  return "FLV";
        default:                return "Unknown";
    }
}

inline const char* to_string(SourceCompression c) {
    switch (c) {
        case SourceCompression::Uncompressed: return "Uncompressed";
        case SourceCompression::Lossless:     return "Lossless";
        case SourceCompression::Lossy:        return "Lossy";
        default:                              return "Unknown";
    }
}

inline const char* to_string(DecoderStatus s) {
    switch (s) {
        case DecoderStatus::Available:           return "Available";
        case DecoderStatus::PendingIntegration:  return "Available, integration pending";
        case DecoderStatus::Unsupported:         return "Unsupported";
        default:                                 return "Unknown";
    }
}

} // namespace ahlc::media
