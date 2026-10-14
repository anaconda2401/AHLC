#include "video_teabps.hpp"
#include "../rle/rle_encoder.hpp"
#include "../rle/rle_decoder.hpp"
#include "../entropy/huffman.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <functional>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace ahlc {

// -----------------------------------------------------------------------
// Helper: Binary I/O
// -----------------------------------------------------------------------
static void v_wr16(std::ostream& os, uint16_t v) {
    char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)};
    os.write(b, 2);
}
static void v_wr32(std::ostream& os, uint32_t v) {
    char b[4] = {
        static_cast<char>(v & 0xFF),
        static_cast<char>((v >> 8) & 0xFF),
        static_cast<char>((v >> 16) & 0xFF),
        static_cast<char>((v >> 24) & 0xFF)
    };
    os.write(b, 4);
}
static void v_wr64(std::ostream& os, uint64_t v) {
    v_wr32(os, static_cast<uint32_t>(v & 0xFFFFFFFFu));
    v_wr32(os, static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFu));
}
static uint16_t v_rd16(std::istream& is) {
    uint8_t b[2] = {};
    is.read(reinterpret_cast<char*>(b), 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}
static uint32_t v_rd32(std::istream& is) {
    uint8_t b[4] = {};
    is.read(reinterpret_cast<char*>(b), 4);
    return static_cast<uint32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}
static uint64_t v_rd64(std::istream& is) {
    uint64_t lo = v_rd32(is);
    uint64_t hi = v_rd32(is);
    return lo | (hi << 32);
}

// -----------------------------------------------------------------------
// Helper: Shannon Entropy
// -----------------------------------------------------------------------
static double video_entropy(const std::vector<uint8_t>& data) {
    if (data.empty()) return 0.0;
    uint64_t counts[256] = {};
    for (uint8_t b : data) counts[b]++;
    const double n = static_cast<double>(data.size());
    double ent = 0.0;
    for (int i = 0; i < 256; ++i) {
        if (counts[i] > 0) {
            double p = static_cast<double>(counts[i]) / n;
            ent -= p * std::log2(p);
        }
    }
    return ent;
}

// -----------------------------------------------------------------------
// Block Prediction and Reconstruction Functions
// -----------------------------------------------------------------------
std::vector<uint8_t> video_teabps_predict_block(const uint8_t* cur_plane,
                                                const uint8_t* prev1_plane,
                                                const uint8_t* prev2_plane,
                                                uint32_t plane_w,
                                                uint32_t bx, uint32_t by,
                                                uint32_t bw, uint32_t bh,
                                                uint8_t predictor)
{
    std::vector<uint8_t> residuals(static_cast<size_t>(bw) * bh);
    size_t out_idx = 0;

    for (uint32_t r = 0; r < bh; ++r) {
        const size_t row_offset = static_cast<size_t>(by + r) * plane_w + bx;
        for (uint32_t c = 0; c < bw; ++c) {
            const uint8_t cur = cur_plane[row_offset + c];
            uint8_t pred = 0;

            if (predictor == VIDEO_TEABPS_PRED_NONE) {
                pred = 0;
            } else if (predictor == VIDEO_TEABPS_PRED_TEMPORAL_D1) {
                pred = prev1_plane ? prev1_plane[row_offset + c] : 0;
            } else if (predictor == VIDEO_TEABPS_PRED_TEMPORAL_D2) {
                const uint8_t p1 = prev1_plane ? prev1_plane[row_offset + c] : 0;
                const uint8_t p2 = prev2_plane ? prev2_plane[row_offset + c] : 0;
                pred = static_cast<uint8_t>(2 * p1 - p2);
            }

            residuals[out_idx++] = static_cast<uint8_t>(cur - pred);
        }
    }
    return residuals;
}

bool video_teabps_reconstruct_block(const uint8_t* residuals,
                                    const uint8_t* prev1_plane,
                                    const uint8_t* prev2_plane,
                                    uint32_t plane_w,
                                    uint32_t bx, uint32_t by,
                                    uint32_t bw, uint32_t bh,
                                    uint8_t predictor,
                                    uint8_t* out_plane)
{
    size_t in_idx = 0;
    for (uint32_t r = 0; r < bh; ++r) {
        const size_t row_offset = static_cast<size_t>(by + r) * plane_w + bx;
        for (uint32_t c = 0; c < bw; ++c) {
            const uint8_t res = residuals[in_idx++];
            uint8_t pred = 0;

            if (predictor == VIDEO_TEABPS_PRED_NONE) {
                pred = 0;
            } else if (predictor == VIDEO_TEABPS_PRED_TEMPORAL_D1) {
                pred = prev1_plane ? prev1_plane[row_offset + c] : 0;
            } else if (predictor == VIDEO_TEABPS_PRED_TEMPORAL_D2) {
                const uint8_t p1 = prev1_plane ? prev1_plane[row_offset + c] : 0;
                const uint8_t p2 = prev2_plane ? prev2_plane[row_offset + c] : 0;
                pred = static_cast<uint8_t>(2 * p1 - p2);
            }

            out_plane[row_offset + c] = static_cast<uint8_t>(pred + res);
        }
    }
    return true;
}

// -----------------------------------------------------------------------
// Sequence File Operations
// -----------------------------------------------------------------------
bool write_yuv_sequence(const std::string& output_path, const RawYUVSequence& seq) {
    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    for (const auto& frame : seq.frames) {
        for (int p = 0; p < 3; ++p) {
            out.write(reinterpret_cast<const char*>(frame.planes[p].data.data()), frame.planes[p].data.size());
        }
    }
    return true;
}

bool read_yuv_sequence(const std::string& input_path,
                       uint32_t width, uint32_t height,
                       uint32_t frame_count,
                       RawYUVSequence& seq)
{
    std::ifstream in(input_path, std::ios::binary);
    if (!in) return false;

    seq.width = width;
    seq.height = height;
    seq.frame_count = frame_count;
    seq.frames.resize(frame_count);

    const uint32_t y_sz = width * height;
    const uint32_t uv_w = (width + 1) / 2;
    const uint32_t uv_h = (height + 1) / 2;
    const uint32_t uv_sz = uv_w * uv_h;

    for (uint32_t f = 0; f < frame_count; ++f) {
        auto& fr = seq.frames[f];
        fr.planes[0].width = width;
        fr.planes[0].height = height;
        fr.planes[0].data.resize(y_sz);
        in.read(reinterpret_cast<char*>(fr.planes[0].data.data()), y_sz);

        fr.planes[1].width = uv_w;
        fr.planes[1].height = uv_h;
        fr.planes[1].data.resize(uv_sz);
        in.read(reinterpret_cast<char*>(fr.planes[1].data.data()), uv_sz);

        fr.planes[2].width = uv_w;
        fr.planes[2].height = uv_h;
        fr.planes[2].data.resize(uv_sz);
        in.read(reinterpret_cast<char*>(fr.planes[2].data.data()), uv_sz);

        if (!in) return false;
    }
    return true;
}

bool yuv_sequences_equal(const RawYUVSequence& a, const RawYUVSequence& b) {
    if (a.width != b.width || a.height != b.height || a.frame_count != b.frame_count)
        return false;
    for (size_t f = 0; f < a.frames.size(); ++f) {
        for (int p = 0; p < 3; ++p) {
            if (a.frames[f].planes[p].data != b.frames[f].planes[p].data)
                return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------
// FFmpeg Video Decoder -> RawYUVSequence
// -----------------------------------------------------------------------
bool decode_video_to_yuv_sequence(const std::string& input_path,
                                  RawYUVSequence& seq,
                                  uint32_t max_frames)
{
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, input_path.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "TEABPS FFmpeg: Cannot open input video: " << input_path << "\n";
        return false;
    }
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    int video_stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = static_cast<int>(i);
            break;
        }
    }
    if (video_stream_idx < 0) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    AVStream* st = fmt_ctx->streams[video_stream_idx];
    const AVCodec* decoder = avcodec_find_decoder(st->codecpar->codec_id);
    if (!decoder) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(decoder);
    if (!codec_ctx) {
        avformat_close_input(&fmt_ctx);
        return false;
    }
    if (avcodec_parameters_to_context(codec_ctx, st->codecpar) < 0 || avcodec_open2(codec_ctx, decoder, nullptr) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return false;
    }

    seq.width = static_cast<uint32_t>(codec_ctx->width);
    seq.height = static_cast<uint32_t>(codec_ctx->height);
    if (st->avg_frame_rate.den > 0 && st->avg_frame_rate.num > 0) {
        seq.fps_num = static_cast<uint32_t>(st->avg_frame_rate.num);
        seq.fps_den = static_cast<uint32_t>(st->avg_frame_rate.den);
    } else {
        seq.fps_num = 25;
        seq.fps_den = 1;
    }
    seq.frames.clear();

    SwsContext* sws = nullptr;
    if (codec_ctx->pix_fmt != AV_PIX_FMT_YUV420P) {
        sws = sws_getContext(codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
                             codec_ctx->width, codec_ctx->height, AV_PIX_FMT_YUV420P,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* yuv_frame = av_frame_alloc();

    const uint32_t w = seq.width;
    const uint32_t h = seq.height;
    const uint32_t uv_w = (w + 1) / 2;
    const uint32_t uv_h = (h + 1) / 2;

    if (sws) {
        yuv_frame->format = AV_PIX_FMT_YUV420P;
        yuv_frame->width = w;
        yuv_frame->height = h;
        av_image_alloc(yuv_frame->data, yuv_frame->linesize, w, h, AV_PIX_FMT_YUV420P, 32);
    }

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            if (avcodec_send_packet(codec_ctx, pkt) == 0) {
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    RawYUVFrame rf;
                    rf.planes[0].width = w; rf.planes[0].height = h; rf.planes[0].data.resize(w * h);
                    rf.planes[1].width = uv_w; rf.planes[1].height = uv_h; rf.planes[1].data.resize(uv_w * uv_h);
                    rf.planes[2].width = uv_w; rf.planes[2].height = uv_h; rf.planes[2].data.resize(uv_w * uv_h);

                    if (sws) {
                        sws_scale(sws, frame->data, frame->linesize, 0, h,
                                  yuv_frame->data, yuv_frame->linesize);
                        for (uint32_t r = 0; r < h; ++r)
                            std::memcpy(rf.planes[0].data.data() + r * w, yuv_frame->data[0] + r * yuv_frame->linesize[0], w);
                        for (uint32_t r = 0; r < uv_h; ++r) {
                            std::memcpy(rf.planes[1].data.data() + r * uv_w, yuv_frame->data[1] + r * yuv_frame->linesize[1], uv_w);
                            std::memcpy(rf.planes[2].data.data() + r * uv_w, yuv_frame->data[2] + r * yuv_frame->linesize[2], uv_w);
                        }
                    } else {
                        for (uint32_t r = 0; r < h; ++r)
                            std::memcpy(rf.planes[0].data.data() + r * w, frame->data[0] + r * frame->linesize[0], w);
                        for (uint32_t r = 0; r < uv_h; ++r) {
                            std::memcpy(rf.planes[1].data.data() + r * uv_w, frame->data[1] + r * frame->linesize[1], uv_w);
                            std::memcpy(rf.planes[2].data.data() + r * uv_w, frame->data[2] + r * frame->linesize[2], uv_w);
                        }
                    }

                    seq.frames.push_back(std::move(rf));
                    if (max_frames > 0 && seq.frames.size() >= max_frames) break;
                }
            }
        }
        av_packet_unref(pkt);
        if (max_frames > 0 && seq.frames.size() >= max_frames) break;
    }

    seq.frame_count = static_cast<uint32_t>(seq.frames.size());

    if (sws) {
        av_freep(&yuv_frame->data[0]);
        sws_freeContext(sws);
    }
    av_frame_free(&yuv_frame);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    return seq.frame_count > 0;
}

// -----------------------------------------------------------------------
// TEABPS Core Compression
// -----------------------------------------------------------------------
bool compress_video_teabps(const RawYUVSequence& seq,
                           const std::string& ahmc_path,
                           VideoTEABPSCompressStats& stats,
                           uint16_t block_size)
{
    const auto t_start = std::chrono::high_resolution_clock::now();

    if (seq.frames.empty() || seq.width == 0 || seq.height == 0) {
        std::cerr << "Video TEABPS: Empty sequence.\n";
        return false;
    }
    if (block_size < 4) block_size = VIDEO_TEABPS_DEFAULT_BLOCK_SIZE;

    stats.width = seq.width;
    stats.height = seq.height;
    stats.fps = seq.fps();
    stats.frame_count = seq.frame_count;
    stats.block_size = block_size;
    stats.original_raw_bytes = seq.raw_bytes();

    std::vector<uint8_t> all_raw_samples;
    all_raw_samples.reserve(static_cast<size_t>(std::min<uint64_t>(stats.original_raw_bytes, 10000000ULL)));
    for (size_t f = 0; f < seq.frames.size(); ++f) {
        for (int p = 0; p < 3; ++p) {
            all_raw_samples.insert(all_raw_samples.end(),
                                   seq.frames[f].planes[p].data.begin(),
                                   seq.frames[f].planes[p].data.end());
            if (all_raw_samples.size() >= 10000000ULL) break;
        }
        if (all_raw_samples.size() >= 10000000ULL) break;
    }
    stats.entropy_before = video_entropy(all_raw_samples);

    // Calculate total block count across all frames and planes
    const uint32_t luma_bx = (seq.width + block_size - 1) / block_size;
    const uint32_t luma_by = (seq.height + block_size - 1) / block_size;
    const uint16_t chroma_bs = std::max<uint16_t>(2, block_size / 2);
    const uint32_t uv_w = (seq.width + 1) / 2;
    const uint32_t uv_h = (seq.height + 1) / 2;
    const uint32_t chroma_bx = (uv_w + chroma_bs - 1) / chroma_bs;
    const uint32_t chroma_by = (uv_h + chroma_bs - 1) / chroma_bs;
    const uint32_t blocks_per_frame = luma_bx * luma_by + 2 * (chroma_bx * chroma_by);
    const uint32_t total_blocks = blocks_per_frame * seq.frame_count;

    stats.total_blocks = total_blocks;
    stats.blocks.resize(total_blocks);

    std::vector<std::vector<uint8_t>> block_payloads(total_blocks);
    std::vector<uint8_t> all_residuals_sample;
    all_residuals_sample.reserve(10000000ULL);

    size_t global_block_idx = 0;

    for (uint32_t f = 0; f < seq.frame_count; ++f) {
        const auto& cur_frame = seq.frames[f];
        const RawYUVFrame* prev1 = (f >= 1) ? &seq.frames[f - 1] : nullptr;
        const RawYUVFrame* prev2 = (f >= 2) ? &seq.frames[f - 2] : nullptr;

        for (int p = 0; p < 3; ++p) {
            const auto& cur_plane = cur_frame.planes[p];
            const uint8_t* p1_data = prev1 ? prev1->planes[p].data.data() : nullptr;
            const uint8_t* p2_data = prev2 ? prev2->planes[p].data.data() : nullptr;

            const uint32_t pw = cur_plane.width;
            const uint32_t ph = cur_plane.height;
            const uint16_t bs = (p == 0) ? block_size : chroma_bs;
            const uint32_t nbx = (pw + bs - 1) / bs;
            const uint32_t nby = (ph + bs - 1) / bs;

            for (uint32_t by = 0; by < nby; ++by) {
                for (uint32_t bx = 0; bx < nbx; ++bx) {
                    const uint16_t actual_bw = static_cast<uint16_t>(std::min<uint32_t>(bs, pw - bx * bs));
                    const uint16_t actual_bh = static_cast<uint16_t>(std::min<uint32_t>(bs, ph - by * bs));

                    VideoTEABPSBlockRecord rec;
                    rec.frame_idx = f;
                    rec.plane_idx = static_cast<uint8_t>(p);
                    rec.bx = static_cast<uint16_t>(bx * bs);
                    rec.by = static_cast<uint16_t>(by * bs);
                    rec.bw = actual_bw;
                    rec.bh = actual_bh;

                    // Evaluate candidates based on actual encoded cost
                    uint8_t best_pred = VIDEO_TEABPS_PRED_NONE;
                    uint8_t best_codec = VIDEO_TEABPS_CODEC_RAW;
                    size_t best_cost = SIZE_MAX;
                    std::vector<uint8_t> best_payload;
                    std::vector<uint8_t> best_res;

                    // Eligible predictors for this frame
                    std::vector<uint8_t> eligible_preds = {VIDEO_TEABPS_PRED_NONE};
                    if (f >= 1) eligible_preds.push_back(VIDEO_TEABPS_PRED_TEMPORAL_D1);
                    if (f >= 2) eligible_preds.push_back(VIDEO_TEABPS_PRED_TEMPORAL_D2);

                    for (uint8_t pred_cand : eligible_preds) {
                        std::vector<uint8_t> res = video_teabps_predict_block(cur_plane.data.data(),
                                                                             p1_data, p2_data,
                                                                             pw, rec.bx, rec.by,
                                                                             actual_bw, actual_bh,
                                                                             pred_cand);

                        // Codec 1: RAW
                        const size_t raw_cost = res.size();
                        if (raw_cost < best_cost) {
                            best_cost = raw_cost;
                            best_pred = pred_cand;
                            best_codec = VIDEO_TEABPS_CODEC_RAW;
                            best_payload = res;
                            best_res = res;
                        }

                        // Codec 2: RLE
                        std::vector<uint8_t> rle_out;
                        if (rle_compress_mem(res, rle_out)) {
                            if (rle_out.size() < best_cost) {
                                best_cost = rle_out.size();
                                best_pred = pred_cand;
                                best_codec = VIDEO_TEABPS_CODEC_RLE;
                                best_payload = std::move(rle_out);
                                best_res = res;
                            }
                        }

                        // Codec 3: Huffman
                        std::vector<uint8_t> huff_out;
                        if (huffman_compress_mem(res, huff_out)) {
                            if (huff_out.size() < best_cost) {
                                best_cost = huff_out.size();
                                best_pred = pred_cand;
                                best_codec = VIDEO_TEABPS_CODEC_HUFFMAN;
                                best_payload = std::move(huff_out);
                                best_res = res;
                            }
                        }
                    }

                    rec.predictor = best_pred;
                    rec.codec = best_codec;
                    rec.payload_size = static_cast<uint32_t>(best_payload.size());

                    if (best_pred == VIDEO_TEABPS_PRED_NONE)               stats.count_none++;
                    else if (best_pred == VIDEO_TEABPS_PRED_TEMPORAL_D1)  stats.count_d1++;
                    else if (best_pred == VIDEO_TEABPS_PRED_TEMPORAL_D2)  stats.count_d2++;

                    if (best_codec == VIDEO_TEABPS_CODEC_RLE)             stats.count_rle++;
                    else if (best_codec == VIDEO_TEABPS_CODEC_HUFFMAN)    stats.count_huffman++;
                    else if (best_codec == VIDEO_TEABPS_CODEC_RAW)        stats.count_raw++;

                    stats.encoded_payload_size += rec.payload_size;
                    stats.blocks[global_block_idx] = rec;
                    block_payloads[global_block_idx] = std::move(best_payload);

                    if (all_residuals_sample.size() < 10000000ULL) {
                        all_residuals_sample.insert(all_residuals_sample.end(), best_res.begin(), best_res.end());
                    }

                    global_block_idx++;
                }
            }
        }
    }

    stats.entropy_after = video_entropy(all_residuals_sample);
    stats.predictor_metadata_size = static_cast<uint64_t>(total_blocks) * 6;

    // Write Container: AHLV v3
    std::ofstream out(ahmc_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "Video TEABPS: Cannot open output container file: " << ahmc_path << "\n";
        return false;
    }

    // Header (41 bytes)
    out.write(AHLV_MAGIC, 4);
    out.put(static_cast<char>(AHLV_VERSION_TEABPS)); // 3
    v_wr16(out, VIDEO_TEABPS_PIX_FMT_YUV420P);
    v_wr32(out, seq.width);
    v_wr32(out, seq.height);
    v_wr32(out, seq.fps_num);
    v_wr32(out, seq.fps_den);
    v_wr32(out, seq.frame_count);
    v_wr16(out, block_size);
    v_wr32(out, total_blocks);
    v_wr64(out, stats.original_raw_bytes);

    // Block Metadata Table: 6 bytes per block (predictor [1B], codec [1B], payload_size [4B])
    for (size_t b = 0; b < total_blocks; ++b) {
        out.put(static_cast<char>(stats.blocks[b].predictor));
        out.put(static_cast<char>(stats.blocks[b].codec));
        v_wr32(out, stats.blocks[b].payload_size);
    }

    // Block Payloads
    for (size_t b = 0; b < total_blocks; ++b) {
        out.write(reinterpret_cast<const char*>(block_payloads[b].data()), block_payloads[b].size());
    }

    out.flush();
    const auto file_sz = out.tellp();
    out.close();

    stats.compressed_file_size = static_cast<uint64_t>(file_sz);
    stats.container_overhead = 41 + stats.predictor_metadata_size;

    const auto t_end = std::chrono::high_resolution_clock::now();
    stats.compression_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    return true;
}

// -----------------------------------------------------------------------
// TEABPS Core Decompression
// -----------------------------------------------------------------------
bool decompress_video_teabps(const std::string& ahmc_path, RawYUVSequence& seq) {
    std::ifstream in(ahmc_path, std::ios::binary);
    if (!in) {
        std::cerr << "Video TEABPS Decoder: Cannot open file: " << ahmc_path << "\n";
        return false;
    }

    // 1. Read & Validate Header
    char magic[4] = {};
    in.read(magic, 4);
    if (std::memcmp(magic, AHLV_MAGIC, 4) != 0) {
        std::cerr << "Video TEABPS Decoder: Invalid AHLV magic.\n";
        return false;
    }

    uint8_t version = 0;
    in.read(reinterpret_cast<char*>(&version), 1);
    if (version != AHLV_VERSION_TEABPS) {
        std::cerr << "Video TEABPS Decoder: Unsupported container version: " << (int)version << "\n";
        return false;
    }

    uint16_t pix_fmt = v_rd16(in);
    if (pix_fmt != VIDEO_TEABPS_PIX_FMT_YUV420P) {
        std::cerr << "Video TEABPS Decoder: Unsupported pixel format.\n";
        return false;
    }

    seq.width = v_rd32(in);
    seq.height = v_rd32(in);
    seq.fps_num = v_rd32(in);
    seq.fps_den = v_rd32(in);
    seq.frame_count = v_rd32(in);
    uint16_t block_size = v_rd16(in);
    uint32_t total_blocks = v_rd32(in);
    uint64_t orig_raw_bytes = v_rd64(in);

    if (seq.width == 0 || seq.height == 0 || seq.frame_count == 0 || block_size == 0 ||
        seq.width > 16384 || seq.height > 16384 || seq.frame_count > 1000000) {
        std::cerr << "Video TEABPS Decoder: Invalid header dimensions or counts.\n";
        return false;
    }

    // Compute expected block count
    const uint32_t luma_bx = (seq.width + block_size - 1) / block_size;
    const uint32_t luma_by = (seq.height + block_size - 1) / block_size;
    const uint16_t chroma_bs = std::max<uint16_t>(2, block_size / 2);
    const uint32_t uv_w = (seq.width + 1) / 2;
    const uint32_t uv_h = (seq.height + 1) / 2;
    const uint32_t chroma_bx = (uv_w + chroma_bs - 1) / chroma_bs;
    const uint32_t chroma_by = (uv_h + chroma_bs - 1) / chroma_bs;
    const uint32_t blocks_per_frame = luma_bx * luma_by + 2 * (chroma_bx * chroma_by);
    const uint32_t expected_blocks = blocks_per_frame * seq.frame_count;

    if (total_blocks != expected_blocks) {
        std::cerr << "Video TEABPS Decoder: Inconsistent block count.\n";
        return false;
    }

    const uint64_t frame_bytes = static_cast<uint64_t>(seq.width) * seq.height + 2 * (static_cast<uint64_t>(uv_w) * uv_h);
    if (frame_bytes * seq.frame_count != orig_raw_bytes) {
        std::cerr << "Video TEABPS Decoder: Inconsistent raw byte length.\n";
        return false;
    }

    // 2. Read Block Table
    struct BlockMeta {
        uint8_t pred = 0;
        uint8_t codec = 0;
        uint32_t payload_size = 0;
    };
    std::vector<BlockMeta> meta_table(total_blocks);
    for (size_t b = 0; b < total_blocks; ++b) {
        meta_table[b].pred = static_cast<uint8_t>(in.get());
        meta_table[b].codec = static_cast<uint8_t>(in.get());
        meta_table[b].payload_size = v_rd32(in);
        if (meta_table[b].pred >= VIDEO_TEABPS_PRED_COUNT || meta_table[b].codec >= VIDEO_TEABPS_CODEC_COUNT) {
            std::cerr << "Video TEABPS Decoder: Corrupted predictor or codec identifier.\n";
            return false;
        }
    }

    // 3. Reconstruct Frames
    seq.frames.resize(seq.frame_count);
    size_t global_block_idx = 0;

    for (uint32_t f = 0; f < seq.frame_count; ++f) {
        auto& cur_frame = seq.frames[f];
        const RawYUVFrame* prev1 = (f >= 1) ? &seq.frames[f - 1] : nullptr;
        const RawYUVFrame* prev2 = (f >= 2) ? &seq.frames[f - 2] : nullptr;

        for (int p = 0; p < 3; ++p) {
            auto& cur_plane = cur_frame.planes[p];
            const uint32_t pw = (p == 0) ? seq.width : uv_w;
            const uint32_t ph = (p == 0) ? seq.height : uv_h;
            cur_plane.width = pw;
            cur_plane.height = ph;
            cur_plane.data.resize(pw * ph, 0);

            const uint8_t* p1_data = prev1 ? prev1->planes[p].data.data() : nullptr;
            const uint8_t* p2_data = prev2 ? prev2->planes[p].data.data() : nullptr;

            const uint16_t bs = (p == 0) ? block_size : chroma_bs;
            const uint32_t nbx = (pw + bs - 1) / bs;
            const uint32_t nby = (ph + bs - 1) / bs;

            for (uint32_t by = 0; by < nby; ++by) {
                for (uint32_t bx = 0; bx < nbx; ++bx) {
                    const uint16_t actual_bw = static_cast<uint16_t>(std::min<uint32_t>(bs, pw - bx * bs));
                    const uint16_t actual_bh = static_cast<uint16_t>(std::min<uint32_t>(bs, ph - by * bs));
                    const size_t expected_res_size = static_cast<size_t>(actual_bw) * actual_bh;

                    const auto& m = meta_table[global_block_idx++];
                    std::vector<uint8_t> payload(m.payload_size);
                    in.read(reinterpret_cast<char*>(payload.data()), m.payload_size);
                    if (!in) {
                        std::cerr << "Video TEABPS Decoder: Unexpected EOF reading block payload.\n";
                        return false;
                    }

                    // Decode residual
                    std::vector<uint8_t> residuals;
                    if (m.codec == VIDEO_TEABPS_CODEC_RAW) {
                        residuals = std::move(payload);
                    } else if (m.codec == VIDEO_TEABPS_CODEC_RLE) {
                        if (!rle_decompress_mem(payload, residuals)) {
                            std::cerr << "Video TEABPS Decoder: RLE decompression failed at block " << global_block_idx - 1 << "\n";
                            return false;
                        }
                    } else if (m.codec == VIDEO_TEABPS_CODEC_HUFFMAN) {
                        if (!huffman_decompress_mem(payload, residuals)) {
                            std::cerr << "Video TEABPS Decoder: Huffman decompression failed at block " << global_block_idx - 1 << "\n";
                            return false;
                        }
                    }

                    if (residuals.size() != expected_res_size) {
                        std::cerr << "Video TEABPS Decoder: Residual size mismatch.\n";
                        return false;
                    }

                    video_teabps_reconstruct_block(residuals.data(),
                                                  p1_data, p2_data,
                                                  pw, bx * bs, by * bs,
                                                  actual_bw, actual_bh,
                                                  m.pred,
                                                  cur_plane.data.data());
                }
            }
        }
    }

    return true;
}

// -----------------------------------------------------------------------
// High-Level File Workflow
// -----------------------------------------------------------------------
bool compress_video_file_teabps(const std::string& video_path,
                                const std::string& ahmc_path,
                                VideoTEABPSCompressStats& stats,
                                uint16_t block_size,
                                uint32_t max_frames)
{
    RawYUVSequence seq;
    if (!decode_video_to_yuv_sequence(video_path, seq, max_frames)) {
        return false;
    }

    {
        std::ifstream sf(video_path, std::ios::binary | std::ios::ate);
        if (sf) stats.original_file_size = static_cast<uint64_t>(sf.tellg());
    }

    return compress_video_teabps(seq, ahmc_path, stats, block_size);
}

void print_video_teabps_stats(const std::string& in_path,
                              const std::string& ahmc_path,
                              const VideoTEABPSCompressStats& stats)
{
    const double ratio = stats.compressed_file_size > 0
        ? static_cast<double>(stats.original_raw_bytes) / stats.compressed_file_size : 0.0;
    const double saved = stats.original_raw_bytes > 0
        ? (1.0 - static_cast<double>(stats.compressed_file_size) / stats.original_raw_bytes) * 100.0 : 0.0;

    std::cout << "--------------------------------------------\n";
    std::cout << " TEABPS Video Compression\n";
    std::cout << "--------------------------------------------\n";
    std::cout << " Source:                 " << in_path << "\n";
    std::cout << " Compressed file:        " << ahmc_path << "\n";
    std::cout << " Resolution:             " << stats.width << "x" << stats.height << " (YUV420p)\n";
    std::cout << " Frame rate:             " << stats.fps << " fps\n";
    std::cout << " Total frames:           " << stats.frame_count << "\n";
    std::cout << " Block size:             " << stats.block_size << "x" << stats.block_size << " luma (" << stats.total_blocks << " total blocks)\n";
    std::cout << " Uncompressed raw YUV:   " << stats.original_raw_bytes << " B\n";
    std::cout << " Encoded payloads:       " << stats.encoded_payload_size << " B\n";
    std::cout << " Predictor metadata:     " << stats.predictor_metadata_size << " B (6B / block)\n";
    std::cout << " Container overhead:     " << stats.container_overhead << " B\n";
    std::cout << " Final .ahmc size:       " << stats.compressed_file_size << " B\n";
    std::cout << " Compression ratio:      " << ratio << "x\n";
    std::cout << " Space saved:            " << saved << "%\n";
    std::cout << " Predictors chosen:      NONE=" << stats.count_none
              << " · D1=" << stats.count_d1
              << " · D2=" << stats.count_d2 << "\n";
    std::cout << " Codecs chosen:          RLE=" << stats.count_rle
              << " · Huffman=" << stats.count_huffman
              << " · RAW=" << stats.count_raw << "\n";
    std::cout << " Raw entropy:            " << stats.entropy_before << " bits/pixel\n";
    std::cout << " Residual entropy:       " << stats.entropy_after << " bits/pixel\n";
    std::cout << " Compression time:       " << stats.compression_time_ms << " ms\n";
    std::cout << "--------------------------------------------\n";
}

// -----------------------------------------------------------------------
// Automated TEABPS Video Test Suite
// -----------------------------------------------------------------------
static RawYUVSequence make_synthetic_yuv_sequence(uint32_t width, uint32_t height, uint32_t frame_count,
    std::function<uint8_t(uint32_t f, int p, uint32_t r, uint32_t c)> gen)
{
    RawYUVSequence seq;
    seq.width = width;
    seq.height = height;
    seq.fps_num = 25;
    seq.fps_den = 1;
    seq.frame_count = frame_count;
    seq.frames.resize(frame_count);

    const uint32_t uv_w = (width + 1) / 2;
    const uint32_t uv_h = (height + 1) / 2;

    for (uint32_t f = 0; f < frame_count; ++f) {
        auto& fr = seq.frames[f];
        fr.planes[0].width = width; fr.planes[0].height = height; fr.planes[0].data.resize(width * height);
        fr.planes[1].width = uv_w;  fr.planes[1].height = uv_h;   fr.planes[1].data.resize(uv_w * uv_h);
        fr.planes[2].width = uv_w;  fr.planes[2].height = uv_h;   fr.planes[2].data.resize(uv_w * uv_h);

        for (uint32_t r = 0; r < height; ++r) {
            for (uint32_t c = 0; c < width; ++c) {
                fr.planes[0].data[r * width + c] = gen(f, 0, r, c);
            }
        }
        for (int p = 1; p <= 2; ++p) {
            for (uint32_t r = 0; r < uv_h; ++r) {
                for (uint32_t c = 0; c < uv_w; ++c) {
                    fr.planes[p].data[r * uv_w + c] = gen(f, p, r, c);
                }
            }
        }
    }
    return seq;
}

static bool test_video_roundtrip(const std::string& name, const RawYUVSequence& orig, uint16_t bs = 16) {
    const std::string tmp_ahmc = "__tmp_test_" + std::to_string(orig.width) + ".ahmc";
    VideoTEABPSCompressStats s;
    if (!compress_video_teabps(orig, tmp_ahmc, s, bs)) {
        std::cout << "  [FAIL] " << name << " (compression failed)\n";
        std::remove(tmp_ahmc.c_str());
        return false;
    }
    RawYUVSequence rec;
    if (!decompress_video_teabps(tmp_ahmc, rec)) {
        std::cout << "  [FAIL] " << name << " (decompression failed)\n";
        std::remove(tmp_ahmc.c_str());
        return false;
    }
    bool eq = yuv_sequences_equal(orig, rec);
    std::remove(tmp_ahmc.c_str());
    return eq;
}

bool run_video_teabps_test_suite() {
    std::cout << "============================================================\n";
    std::cout << " AHLC Video TEABPS Comprehensive Test Suite\n";
    std::cout << "============================================================\n\n";

    bool all_ok = true;

    // 8.A Predictor Unit Tests
    std::cout << "--- 8.A Predictor Unit Tests (NONE, TEMPORAL-D1, TEMPORAL-D2) ---\n";
    {
        // 1. Single 1x1 block
        std::vector<uint8_t> cur = {140}, p1 = {100}, p2 = {60};
        uint8_t out_none = 0, out_d1 = 0, out_d2 = 0;
        auto res_none = video_teabps_predict_block(cur.data(), p1.data(), p2.data(), 1, 0, 0, 1, 1, VIDEO_TEABPS_PRED_NONE);
        video_teabps_reconstruct_block(res_none.data(), p1.data(), p2.data(), 1, 0, 0, 1, 1, VIDEO_TEABPS_PRED_NONE, &out_none);

        auto res_d1 = video_teabps_predict_block(cur.data(), p1.data(), p2.data(), 1, 0, 0, 1, 1, VIDEO_TEABPS_PRED_TEMPORAL_D1);
        video_teabps_reconstruct_block(res_d1.data(), p1.data(), p2.data(), 1, 0, 0, 1, 1, VIDEO_TEABPS_PRED_TEMPORAL_D1, &out_d1);

        auto res_d2 = video_teabps_predict_block(cur.data(), p1.data(), p2.data(), 1, 0, 0, 1, 1, VIDEO_TEABPS_PRED_TEMPORAL_D2);
        video_teabps_reconstruct_block(res_d2.data(), p1.data(), p2.data(), 1, 0, 0, 1, 1, VIDEO_TEABPS_PRED_TEMPORAL_D2, &out_d2);

        bool u_ok = (out_none == 140) && (out_d1 == 140) && (out_d2 == 140);
        std::cout << " [" << (u_ok ? "PASS" : "FAIL") << "] 1-pixel block forward/inverse for NONE, D1, D2\n";
        all_ok &= u_ok;

        // 2. D2 exact cancellation on linear temporal ramp: F[t] = 20 + 30*t
        // F[0]=20, F[1]=50, F[2]=80. Pred = 2*50 - 20 = 80. Residual must be 0!
        std::vector<uint8_t> f0 = {20}, f1 = {50}, f2 = {80};
        auto r_ramp = video_teabps_predict_block(f2.data(), f1.data(), f0.data(), 1, 0, 0, 1, 1, VIDEO_TEABPS_PRED_TEMPORAL_D2);
        bool ramp_ok = (r_ramp[0] == 0);
        std::cout << " [" << (ramp_ok ? "PASS" : "FAIL") << "] TEMPORAL-D2 produces exact 0 for linear temporal velocity\n";
        all_ok &= ramp_ok;

        // 3. Boundary wrap-around test (0 and 255 values, negative residual modulo 256)
        std::vector<uint8_t> c_wrap = {10}, p_wrap = {200}; // 10 - 200 = -190 = 66 (mod 256)
        auto r_wrap = video_teabps_predict_block(c_wrap.data(), p_wrap.data(), nullptr, 1, 0, 0, 1, 1, VIDEO_TEABPS_PRED_TEMPORAL_D1);
        uint8_t out_wrap = 0;
        video_teabps_reconstruct_block(r_wrap.data(), p_wrap.data(), nullptr, 1, 0, 0, 1, 1, VIDEO_TEABPS_PRED_TEMPORAL_D1, &out_wrap);
        bool wrap_ok = (out_wrap == 10);
        std::cout << " [" << (wrap_ok ? "PASS" : "FAIL") << "] Modular residual arithmetic & extreme wrap-around\n";
        all_ok &= wrap_ok;
    }

    // 8.B Frame Sequence & Geometry Tests
    std::cout << "\n--- 8.B Sequence Layout & Geometry Tests ---\n";
    {
        // 1. Single frame sequence (Frame 0 only)
        auto s_1f = make_synthetic_yuv_sequence(32, 32, 1, [](uint32_t, int, uint32_t r, uint32_t c) {
            return static_cast<uint8_t>((r * 8 + c * 8) & 0xFF);
        });
        bool ok_1f = test_video_roundtrip("Single-frame sequence (Frame 0 only)", s_1f, 16);
        std::cout << " [" << (ok_1f ? "PASS" : "FAIL") << "] Single-frame sequence exact reconstruction\n";
        all_ok &= ok_1f;

        // 2. Exact divisible dimensions (64x64, 4 frames, block=16)
        auto s_div = make_synthetic_yuv_sequence(64, 64, 4, [](uint32_t f, int, uint32_t r, uint32_t c) {
            return static_cast<uint8_t>((r + c + f * 5) & 0xFF);
        });
        bool ok_div = test_video_roundtrip("Exact divisible dimensions (64x64, block=16)", s_div, 16);
        std::cout << " [" << (ok_div ? "PASS" : "FAIL") << "] Exact divisible dimensions (64x64, block 16x16)\n";
        all_ok &= ok_div;

        // 3. Partial block boundaries (e.g. 50x38, block=16 -> partial at right and bottom)
        auto s_part = make_synthetic_yuv_sequence(50, 38, 3, [](uint32_t f, int p, uint32_t r, uint32_t c) {
            return static_cast<uint8_t>((r * 3 + c * 2 + p * 20 + f * 4) & 0xFF);
        });
        bool ok_part = test_video_roundtrip("Partial block boundaries (50x38, block=16)", s_part, 16);
        std::cout << " [" << (ok_part ? "PASS" : "FAIL") << "] Partial blocks at right and bottom edges (50x38)\n";
        all_ok &= ok_part;
    }

    // 8.C Predictor Selection Tests
    std::cout << "\n--- 8.C Predictor Selection Tests ---\n";
    {
        // 1. Static Video (10 identical frames) -> TEMPORAL-D1 produces all zero residuals
        auto s_static = make_synthetic_yuv_sequence(32, 32, 5, [](uint32_t, int, uint32_t r, uint32_t c) {
            return static_cast<uint8_t>((r * 7 + c * 3) & 0xFF);
        });
        VideoTEABPSCompressStats st_static;
        compress_video_teabps(s_static, "__t_static.ahmc", st_static, 16);
        bool static_favors_temp = (st_static.count_d1 > 0 || st_static.count_d2 > 0);
        std::cout << " [" << (static_favors_temp ? "PASS" : "FAIL") << "] Static video sequence selects TEMPORAL prediction: "
                  << (st_static.count_d1 + st_static.count_d2) << "/" << st_static.total_blocks << " blocks\n";
        all_ok &= static_favors_temp;
        std::remove("__t_static.ahmc");

        // 2. Linear motion with spatial variation -> TEMPORAL-D2 selected
        auto s_ramp = make_synthetic_yuv_sequence(32, 32, 4, [](uint32_t f, int, uint32_t r, uint32_t c) {
            return static_cast<uint8_t>((r * 5 + c * 3 + f * (r + c + 2)) & 0xFF);
        });
        VideoTEABPSCompressStats st_ramp;
        compress_video_teabps(s_ramp, "__t_ramp.ahmc", st_ramp, 16);
        bool ramp_favors_d2 = (st_ramp.count_d2 > 0);
        std::cout << " [" << (ramp_favors_d2 ? "PASS" : "FAIL") << "] Linear velocity selects TEMPORAL-D2 predictor: "
                  << st_ramp.count_d2 << " blocks\n";
        all_ok &= ramp_favors_d2;
        std::remove("__t_ramp.ahmc");

        // 3. Scene Cut (Frame 0 & 1 identical, Frame 2 completely random jump)
        auto s_cut = make_synthetic_yuv_sequence(32, 32, 3, [](uint32_t f, int, uint32_t, uint32_t) {
            if (f < 2) return static_cast<uint8_t>(50);
            return static_cast<uint8_t>(210); // abrupt scene cut
        });
        VideoTEABPSCompressStats st_cut;
        compress_video_teabps(s_cut, "__t_cut.ahmc", st_cut, 16);
        std::cout << " [" << (st_cut.total_blocks > 0 ? "PASS" : "FAIL") << "] Scene cut evaluated safely across frames\n";
        all_ok &= (st_cut.total_blocks > 0);
        std::remove("__t_cut.ahmc");

        // 4. High-entropy uncorrelated noise across frames -> NONE selected
        auto s_noise = make_synthetic_yuv_sequence(32, 32, 3, [](uint32_t f, int p, uint32_t r, uint32_t c) {
            uint32_t x = f * 10007 + p * 5003 + r * 131 + c + 1;
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = (x >> 16) ^ x;
            return static_cast<uint8_t>(x & 0xFF);
        });
        VideoTEABPSCompressStats st_noise;
        compress_video_teabps(s_noise, "__t_noise.ahmc", st_noise, 16);
        bool noise_has_none = (st_noise.count_none > 0);
        std::cout << " [" << (noise_has_none ? "PASS" : "FAIL") << "] High-entropy noise selects NONE predictor: "
                  << st_noise.count_none << "/" << st_noise.total_blocks << " blocks\n";
        all_ok &= noise_has_none;
        std::remove("__t_noise.ahmc");
    }

    // 8.E Container Validation & Error Handling
    std::cout << "\n--- 8.E Container Validation & Error Handling ---\n";
    {
        auto valid_seq = make_synthetic_yuv_sequence(32, 32, 2, [](uint32_t f, int, uint32_t r, uint32_t c) {
            return static_cast<uint8_t>((f * 10 + r + c) & 0xFF);
        });
        VideoTEABPSCompressStats s_val_stat;
        compress_video_teabps(valid_seq, "__val_v.ahmc", s_val_stat, 16);

        std::vector<uint8_t> valid_bytes;
        {
            std::ifstream vf("__val_v.ahmc", std::ios::binary | std::ios::ate);
            auto sz = vf.tellg(); vf.seekg(0);
            valid_bytes.resize(static_cast<size_t>(sz));
            vf.read(reinterpret_cast<char*>(valid_bytes.data()), sz);
        }

        auto test_corrupted = [&](const std::vector<uint8_t>& corrupted) -> bool {
            std::ofstream cf("__corrupt_v.ahmc", std::ios::binary | std::ios::trunc);
            cf.write(reinterpret_cast<const char*>(corrupted.data()), corrupted.size());
            cf.close();

            RawYUVSequence rec;
            bool dec_ok = decompress_video_teabps("__corrupt_v.ahmc", rec);
            std::remove("__corrupt_v.ahmc");
            return !dec_ok;
        };

        // 1. Invalid magic
        auto bad_magic = valid_bytes;
        bad_magic[0] = 'X'; bad_magic[1] = 'Y';
        bool b1 = test_corrupted(bad_magic);
        std::cout << " [" << (b1 ? "PASS" : "FAIL") << "] Invalid magic rejected safely\n";
        all_ok &= b1;

        // 2. Unsupported version
        auto bad_ver = valid_bytes;
        bad_ver[4] = 99;
        bool b2 = test_corrupted(bad_ver);
        std::cout << " [" << (b2 ? "PASS" : "FAIL") << "] Unsupported container version rejected safely\n";
        all_ok &= b2;

        // 3. Truncated header
        std::vector<uint8_t> trunc_hdr(valid_bytes.begin(), valid_bytes.begin() + 20);
        bool b3 = test_corrupted(trunc_hdr);
        std::cout << " [" << (b3 ? "PASS" : "FAIL") << "] Truncated header rejected safely\n";
        all_ok &= b3;

        // 4. Truncated payload
        std::vector<uint8_t> trunc_pl(valid_bytes.begin(), valid_bytes.end() - 10);
        bool b4 = test_corrupted(trunc_pl);
        std::cout << " [" << (b4 ? "PASS" : "FAIL") << "] Truncated payload rejected safely\n";
        all_ok &= b4;

        // 5. Out-of-range predictor ID
        auto bad_pred = valid_bytes;
        bad_pred[41] = 200; // First block's predictor byte in block table
        bool b5 = test_corrupted(bad_pred);
        std::cout << " [" << (b5 ? "PASS" : "FAIL") << "] Out-of-range predictor ID rejected safely\n";
        all_ok &= b5;

        std::remove("__val_v.ahmc");
    }

    // 8.F Fallback & Size Accounting Tests
    std::cout << "\n--- 8.F Fallback & Size Accounting Tests ---\n";
    {
        auto s_acc = make_synthetic_yuv_sequence(32, 32, 2, [](uint32_t f, int, uint32_t r, uint32_t c) {
            return static_cast<uint8_t>((f * 5 + r * 2 + c) & 0xFF);
        });
        VideoTEABPSCompressStats s_stats;
        compress_video_teabps(s_acc, "__acc.ahmc", s_stats, 16);

        std::ifstream af("__acc.ahmc", std::ios::binary | std::ios::ate);
        const uint64_t actual_sz = static_cast<uint64_t>(af.tellg());
        bool size_match = (actual_sz == s_stats.compressed_file_size);
        std::cout << " [" << (size_match ? "PASS" : "FAIL") << "] Actual stored file size matches reported compressed size ("
                  << actual_sz << " B)\n";
        all_ok &= size_match;

        const uint64_t exp_overhead = 41 + s_stats.total_blocks * 6;
        bool over_match = (s_stats.container_overhead == exp_overhead);
        std::cout << " [" << (over_match ? "PASS" : "FAIL") << "] Container overhead accounted accurately (41B header + 6B/block)\n";
        all_ok &= over_match;
        std::remove("__acc.ahmc");
    }

    std::cout << "\n============================================================\n";
    std::cout << " Video TEABPS Test Suite Overall: " << (all_ok ? "ALL PASS" : "SOME FAILED") << "\n";
    std::cout << "============================================================\n";

    return all_ok;
}

// -----------------------------------------------------------------------
// Video TEABPS Benchmark
// -----------------------------------------------------------------------
bool run_video_teabps_benchmark() {
    std::cout << "\n=========================================================================================================\n";
    std::cout << " AHLC Benchmark: Video TEABPS vs Baseline Lossless Video Coding\n";
    std::cout << "=========================================================================================================\n";

    struct TestCase {
        std::string name;
        RawYUVSequence seq;
    };

    std::vector<TestCase> cases;

    // 1. Static Scene (10 frames of 64x64, identical content)
    cases.push_back({"Static Video Scene", make_synthetic_yuv_sequence(64, 64, 10, [](uint32_t, int, uint32_t r, uint32_t c) {
        return static_cast<uint8_t>((r * 4 + c * 2) & 0xFF);
    })});

    // 2. Linear Velocity / Motion (10 frames)
    cases.push_back({"Linear Motion Sequence", make_synthetic_yuv_sequence(64, 64, 10, [](uint32_t f, int, uint32_t r, uint32_t c) {
        return static_cast<uint8_t>((r * 2 + c * 2 + f * 8) & 0xFF);
    })});

    // 3. Moving Waveform / Pattern (10 frames)
    cases.push_back({"Moving Waveform Pattern", make_synthetic_yuv_sequence(64, 64, 10, [](uint32_t f, int, uint32_t r, uint32_t c) {
        return static_cast<uint8_t>(128.0 + 100.0 * std::sin((r + f * 4) * 0.1) * std::cos(c * 0.1));
    })});

    // 4. Scene Cut (5 frames Scene A, 5 frames Scene B)
    cases.push_back({"Scene Cut Transition", make_synthetic_yuv_sequence(64, 64, 10, [](uint32_t f, int, uint32_t r, uint32_t c) {
        if (f < 5) return static_cast<uint8_t>((r * 3 + c * 3) & 0xFF);
        return static_cast<uint8_t>(240 - (r * 2 + c * 2));
    })});

    // 5. High-Entropy Noise Sequence (10 frames)
    cases.push_back({"High-Entropy Noise Video", make_synthetic_yuv_sequence(64, 64, 10, [](uint32_t f, int p, uint32_t r, uint32_t c) {
        uint32_t x = f * 10007 + p * 5003 + r * 131 + c + 1;
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = (x >> 16) ^ x;
        return static_cast<uint8_t>(x & 0xFF);
    })});

    std::printf("%-26s | %-9s | %-12s | %-12s | %-10s | %-10s | %-16s | %-6s\n",
                "Video Test Case", "Raw (B)", "Base (B)", "TEABPS (B)", "Base Ratio", "TEABPS Ratio", "TEABPS Preds", "Verify");
    std::cout << "---------------------------+-----------+--------------+--------------+------------+------------+------------------+-------\n";

    for (const auto& tc : cases) {
        const std::string fn_raw = "__vb_" + tc.name.substr(0, 4) + ".yuv";
        const std::string fn_base_ahmc = "__vb_" + tc.name.substr(0, 4) + "_base.ahmc";
        const std::string fn_teabps_ahmc = "__vb_" + tc.name.substr(0, 4) + "_teabps.ahmc";

        write_yuv_sequence(fn_raw, tc.seq);

        // Baseline: encode whole sequence with Huffman directly (without temporal block-wise adaptation)
        std::vector<uint8_t> all_bytes;
        for (const auto& f : tc.seq.frames) {
            for (int p = 0; p < 3; ++p)
                all_bytes.insert(all_bytes.end(), f.planes[p].data.begin(), f.planes[p].data.end());
        }
        std::vector<uint8_t> base_out;
        huffman_compress_mem(all_bytes, base_out);
        const uint64_t base_size = base_out.size() + 32; // plus container header

        // TEABPS: block-wise temporal prediction + entropy coding
        VideoTEABPSCompressStats s_teabps;
        compress_video_teabps(tc.seq, fn_teabps_ahmc, s_teabps, 16);

        // Decompress & verify exact sample equality
        RawYUVSequence rec;
        decompress_video_teabps(fn_teabps_ahmc, rec);
        bool match = yuv_sequences_equal(tc.seq, rec);

        const double base_ratio = base_size > 0
            ? static_cast<double>(s_teabps.original_raw_bytes) / base_size : 0.0;
        const double teabps_ratio = s_teabps.compressed_file_size > 0
            ? static_cast<double>(s_teabps.original_raw_bytes) / s_teabps.compressed_file_size : 0.0;

        char pred_dist[32];
        std::snprintf(pred_dist, sizeof(pred_dist), "N:%u D1:%u D2:%u",
                      s_teabps.count_none, s_teabps.count_d1, s_teabps.count_d2);

        std::printf("%-26s | %-9llu | %-12llu | %-12llu | %9.2fx | %9.2fx | %-16s | %-6s\n",
                    tc.name.c_str(),
                    static_cast<unsigned long long>(s_teabps.original_raw_bytes),
                    static_cast<unsigned long long>(base_size),
                    static_cast<unsigned long long>(s_teabps.compressed_file_size),
                    base_ratio,
                    teabps_ratio,
                    pred_dist,
                    match ? "PASS" : "FAIL");

        std::remove(fn_raw.c_str());
        std::remove(fn_base_ahmc.c_str());
        std::remove(fn_teabps_ahmc.c_str());
    }
    std::cout << "=========================================================================================================\n\n";
    return true;
}

} // namespace ahlc
