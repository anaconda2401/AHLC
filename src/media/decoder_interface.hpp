#pragma once
/**
 * decoder_interface.hpp  —  AHLC abstract decoder interfaces.
 */

#include "image_frame.hpp"
#include "audio_buffer.hpp"
#include "video_frame.hpp"
#include <string>

namespace ahlc::media {

class IImageDecoder {
public:
    virtual ~IImageDecoder() = default;
    virtual bool decode(const std::string& path, RawImageFrame& out_frame) = 0;
};

class IAudioDecoder {
public:
    virtual ~IAudioDecoder() = default;
    virtual bool decode(const std::string& path, RawAudioBuffer& out_buffer) = 0;
};

class IVideoDecoder {
public:
    virtual ~IVideoDecoder() = default;
    virtual bool decode(const std::string& path, RawVideoSequence& out_sequence) = 0;
};

} // namespace ahlc::media
