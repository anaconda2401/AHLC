#include "format_detector.hpp"
#include <iostream>
#include <iomanip>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
}

namespace ahlc::media {

static void map_ffmpeg_codec(AVCodecID codec_id, AVMediaType type, const char* format_name, FormatInfo& info)
{
    // Default mapping based on codec ID
    switch (codec_id) {
        case AV_CODEC_ID_BMP:
            info.format = MediaFormat::BMP;
            info.source_compression = SourceCompression::Uncompressed;
            info.decoder_status = DecoderStatus::Available; // Built-in
            info.format_name = "BMP";
            break;
        case AV_CODEC_ID_PNG:
            info.format = MediaFormat::PNG;
            info.source_compression = SourceCompression::Lossless;
            info.decoder_status = DecoderStatus::PendingIntegration;
            info.format_name = "PNG";
            break;
        case AV_CODEC_ID_MJPEG:
            info.format = MediaFormat::JPEG;
            info.source_compression = SourceCompression::Lossy;
            info.decoder_status = DecoderStatus::PendingIntegration;
            info.format_name = "JPEG";
            break;
        case AV_CODEC_ID_TIFF:
            info.format = MediaFormat::TIFF;
            info.source_compression = SourceCompression::Lossless;
            info.decoder_status = DecoderStatus::PendingIntegration;
            info.format_name = "TIFF";
            break;
        case AV_CODEC_ID_WEBP:
            info.format = MediaFormat::WebP;
            info.source_compression = SourceCompression::Lossy; // Often lossy, could be lossless
            info.decoder_status = DecoderStatus::PendingIntegration;
            info.format_name = "WebP";
            break;
        case AV_CODEC_ID_GIF:
            info.format = MediaFormat::GIF;
            info.source_compression = SourceCompression::Lossless;
            info.decoder_status = DecoderStatus::PendingIntegration;
            info.format_name = "GIF";
            break;
        case AV_CODEC_ID_FLAC:
            info.format = MediaFormat::FLAC;
            info.source_compression = SourceCompression::Lossless;
            info.decoder_status = DecoderStatus::PendingIntegration;
            info.format_name = "FLAC";
            break;
        case AV_CODEC_ID_MP3:
            info.format = MediaFormat::MP3;
            info.source_compression = SourceCompression::Lossy;
            info.decoder_status = DecoderStatus::PendingIntegration;
            info.format_name = "MP3";
            break;
        case AV_CODEC_ID_AAC:
            info.format = MediaFormat::AAC;
            info.source_compression = SourceCompression::Lossy;
            info.decoder_status = DecoderStatus::PendingIntegration;
            info.format_name = "AAC";
            break;
        case AV_CODEC_ID_VORBIS:
        case AV_CODEC_ID_OPUS:
            info.format = MediaFormat::OGG; // Simplification
            info.source_compression = SourceCompression::Lossy;
            info.decoder_status = DecoderStatus::PendingIntegration;
            info.format_name = (codec_id == AV_CODEC_ID_VORBIS) ? "Vorbis" : "Opus";
            break;
        default:
            // Handle uncompressed PCM audio
            if (codec_id >= AV_CODEC_ID_PCM_S16LE && codec_id <= AV_CODEC_ID_PCM_F64BE) {
                if (std::string(format_name).find("wav") != std::string::npos) {
                    info.format = MediaFormat::WAV;
                    info.format_name = "WAV";
                } else if (std::string(format_name).find("aiff") != std::string::npos) {
                    info.format = MediaFormat::AIFF;
                    info.format_name = "AIFF";
                } else {
                    info.format = MediaFormat::WAV; // Default uncompressed assumption
                    info.format_name = "PCM Audio";
                }
                info.source_compression = SourceCompression::Uncompressed;
                // Currently WAV is built-in
                if (info.format == MediaFormat::WAV) {
                    info.decoder_status = DecoderStatus::Available;
                } else {
                    info.decoder_status = DecoderStatus::PendingIntegration;
                }
            }
            break;
    }

    // Video mapping if codec wasn't matched above or if format gives a better hint
    if (type == AVMEDIA_TYPE_VIDEO && info.format == MediaFormat::Unknown) {
        std::string fname(format_name);
        if (fname.find("mp4") != std::string::npos) {
            info.format = MediaFormat::MP4;
            info.format_name = "MP4";
        } else if (fname.find("matroska") != std::string::npos || fname.find("webm") != std::string::npos) {
            if (fname.find("webm") != std::string::npos) {
                info.format = MediaFormat::WebM;
                info.format_name = "WebM";
            } else {
                info.format = MediaFormat::MKV;
                info.format_name = "MKV";
            }
        } else if (fname.find("avi") != std::string::npos) {
            info.format = MediaFormat::AVI;
            info.format_name = "AVI";
        } else if (fname.find("mov") != std::string::npos) {
            info.format = MediaFormat::MOV;
            info.format_name = "MOV";
        } else if (fname.find("flv") != std::string::npos) {
            info.format = MediaFormat::FLV;
            info.format_name = "FLV";
        }
        
        if (info.format != MediaFormat::Unknown) {
            // Assume video is usually lossy compression unless we know it's FFV1 or similar
            if (codec_id == AV_CODEC_ID_FFV1) {
                info.source_compression = SourceCompression::Lossless;
            } else {
                info.source_compression = SourceCompression::Lossy;
            }
            info.decoder_status = DecoderStatus::PendingIntegration;
        }
    }
}

FormatInfo detect_format(const std::string& path)
{
    FormatInfo info;
    
    // Silence FFmpeg output for probing
    av_log_set_level(AV_LOG_QUIET);

    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, path.c_str(), nullptr, nullptr) < 0) {
        info.error_message = "Could not open file or unsupported format";
        return info;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        info.error_message = "Could not find stream information";
        avformat_close_input(&fmt_ctx);
        return info;
    }

    info.ok = true;
    if (fmt_ctx->iformat && fmt_ctx->iformat->name) {
        info.ffmpeg_fmt_name = fmt_ctx->iformat->name;
    }

    // Find the primary stream (video first, then audio)
    int stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_idx < 0) {
        stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    }
    
    // If no video/audio found, try to just pick the first valid stream
    if (stream_idx < 0 && fmt_ctx->nb_streams > 0) {
        stream_idx = 0;
    }

    if (stream_idx >= 0 && stream_idx < static_cast<int>(fmt_ctx->nb_streams)) {
        AVStream* stream = fmt_ctx->streams[stream_idx];
        AVCodecParameters* codecpar = stream->codecpar;

        if (codecpar) {
            const AVCodecDescriptor* desc = avcodec_descriptor_get(codecpar->codec_id);
            if (desc) {
                info.codec_name = desc->name;
            }

            if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                // Could be an image or a video sequence
                // Often image formats have only 1 frame
                if (stream->nb_frames == 1 || 
                    codecpar->codec_id == AV_CODEC_ID_BMP || 
                    codecpar->codec_id == AV_CODEC_ID_PNG || 
                    codecpar->codec_id == AV_CODEC_ID_MJPEG ||
                    codecpar->codec_id == AV_CODEC_ID_TIFF ||
                    codecpar->codec_id == AV_CODEC_ID_WEBP ||
                    std::string(info.ffmpeg_fmt_name).find("image") != std::string::npos) 
                {
                    info.media_type = MediaType::Image;
                } else {
                    info.media_type = MediaType::Video;
                }
                
                info.width = codecpar->width;
                info.height = codecpar->height;
                
                // Try to get pixel format info
                const AVPixFmtDescriptor* pix_desc = av_pix_fmt_desc_get((AVPixelFormat)codecpar->format);
                if (pix_desc) {
                    info.channels = pix_desc->nb_components;
                    // Approximation for bit depth
                    if (pix_desc->nb_components > 0) {
                        info.bit_depth = pix_desc->comp[0].depth;
                    }
                }
                
                if (info.media_type == MediaType::Video) {
                    if (stream->avg_frame_rate.den > 0) {
                        info.frame_rate = av_q2d(stream->avg_frame_rate);
                    }
                    if (stream->nb_frames > 0) {
                        info.frame_count = stream->nb_frames;
                    }
                }

            } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                info.media_type = MediaType::Audio;
                info.sample_rate = codecpar->sample_rate;
#if LIBAVUTIL_VERSION_MAJOR >= 58
                info.audio_channels = codecpar->ch_layout.nb_channels;
#else
                info.audio_channels = codecpar->channels;
#endif
                info.bits_per_sample = av_get_bytes_per_sample((AVSampleFormat)codecpar->format) * 8;
                if (info.bits_per_sample == 0) {
                    info.bits_per_sample = codecpar->bits_per_raw_sample > 0 ? codecpar->bits_per_raw_sample : 
                                         (codecpar->bits_per_coded_sample > 0 ? codecpar->bits_per_coded_sample : 16);
                }
                
                if (fmt_ctx->duration != AV_NOPTS_VALUE) {
                    info.duration_sec = static_cast<double>(fmt_ctx->duration) / AV_TIME_BASE;
                }
            }
            
            map_ffmpeg_codec(codecpar->codec_id, codecpar->codec_type, info.ffmpeg_fmt_name.c_str(), info);
        }
    }

    if (info.format == MediaFormat::Unknown && info.format_name.empty()) {
        info.format_name = info.ffmpeg_fmt_name.empty() ? "Unknown" : info.ffmpeg_fmt_name;
    }

    avformat_close_input(&fmt_ctx);
    
    // Restore log level
    av_log_set_level(AV_LOG_INFO);
    
    return info;
}

void print_format_analysis(const std::string& path, const FormatInfo& info)
{
    std::string filename = path;
    size_t slash = filename.find_last_of("/\\");
    if (slash != std::string::npos) {
        filename = filename.substr(slash + 1);
    }

    std::cout << "AHLC Format Analysis\n";
    std::cout << "--------------------\n";
    std::cout << "File: " << filename << "\n";
    
    if (!info.ok) {
        std::cout << "Status: " << info.error_message << "\n";
        return;
    }

    std::cout << "Media type: " << to_string(info.media_type) << "\n";
    std::cout << "Format: " << (info.format_name.empty() ? to_string(info.format) : info.format_name) << "\n";
    std::cout << "Source compression: " << to_string(info.source_compression) << "\n";
    
    std::string decoder_str = "FFmpeg";
    if (info.format == MediaFormat::BMP || info.format == MediaFormat::WAV) {
        decoder_str = "AHLC Built-in";
    } else if (info.decoder_status == DecoderStatus::Unsupported) {
        decoder_str = "None";
    }
    
    std::cout << "Decoder: " << decoder_str << "\n";
    std::cout << "Decoder status: " << to_string(info.decoder_status) << "\n";

    if (info.media_type == MediaType::Image && info.width > 0 && info.height > 0) {
        std::cout << "\n[Image Metadata]\n";
        std::cout << "Resolution: " << info.width << "x" << info.height << "\n";
        if (info.channels > 0) std::cout << "Channels: " << info.channels << "\n";
        if (info.bit_depth > 0) std::cout << "Bit depth: " << info.bit_depth << "\n";
        if (!info.codec_name.empty()) std::cout << "Codec: " << info.codec_name << "\n";
    } else if (info.media_type == MediaType::Audio && info.sample_rate > 0) {
        std::cout << "\n[Audio Metadata]\n";
        std::cout << "Sample rate: " << info.sample_rate << " Hz\n";
        if (info.audio_channels > 0) std::cout << "Channels: " << info.audio_channels << "\n";
        if (info.bits_per_sample > 0) std::cout << "Bits per sample: " << info.bits_per_sample << "\n";
        if (info.duration_sec > 0) std::cout << "Duration: " << std::fixed << std::setprecision(2) << info.duration_sec << " s\n";
        if (!info.codec_name.empty()) std::cout << "Codec: " << info.codec_name << "\n";
    } else if (info.media_type == MediaType::Video && info.width > 0 && info.height > 0) {
        std::cout << "\n[Video Metadata]\n";
        std::cout << "Resolution: " << info.width << "x" << info.height << "\n";
        if (info.frame_rate > 0) std::cout << "Frame rate: " << std::fixed << std::setprecision(2) << info.frame_rate << " fps\n";
        if (info.frame_count > 0) std::cout << "Frame count: " << info.frame_count << "\n";
        if (!info.codec_name.empty()) std::cout << "Codec: " << info.codec_name << "\n";
    }
}

} // namespace ahlc::media
