#pragma once
/**
 * video_frame.hpp  —  Common internal video representation.
 */

#include "image_frame.hpp"
#include <cstdint>
#include <vector>
#include <string>

namespace ahlc::media {

// ----------------------------------------------------------------
// RawVideoSequence — the common video representation used by AHLC
// ----------------------------------------------------------------
struct RawVideoSequence {
    uint32_t width       = 0;
    uint32_t height      = 0;
    double   frame_rate  = 0.0;
    uint64_t frame_count = 0;

    // The frames (for small sequences or tests, holding all frames in memory)
    // For large videos, we would need a stream interface instead of holding all.
    std::vector<RawImageFrame> frames;

    bool valid() const {
        return width > 0 && height > 0 &&
               !frames.empty() &&
               frames.size() == frame_count;
    }

    std::string source_path;
    std::string source_format;
};

} // namespace ahlc::media
