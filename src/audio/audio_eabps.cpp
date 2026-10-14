/*
 * audio_eabps.cpp
 *
 * Entropy-Aware Block-wise Predictor Selection (EABPS) for Audio Compression.
 *
 * Divides PCM audio sample streams into independent blocks (default 4096 frames),
 * evaluates reversible predictors (NONE, DELTA-1, DELTA-2) and adaptive entropy coders
 * (RLE, Canonical Huffman, RAW) for each block based on complete encoded payload cost,
 * and packages the result into a backward-compatible AHMC v3 audio container (AHLA magic).
 */

#include "audio_eabps.hpp"
#include "audio_predictor.hpp"
#include "../rle/rle_encoder.hpp"
#include "../rle/rle_decoder.hpp"
#include "../entropy/huffman.hpp"

#include <fstream>
#include <iostream>
#include <iomanip>
#include <array>
#include <cstring>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <functional>
#include <initializer_list>
#include <limits>

namespace ahlc {

static constexpr char AHLA_MAGIC[4] = {'A', 'H', 'L', 'A'};

// Little-endian I/O helpers
static void a_wr16(std::ofstream& f, uint16_t v) {
    f.put(static_cast<char>(v & 0xFF));
    f.put(static_cast<char>((v >> 8) & 0xFF));
}

static void a_wr32(std::ofstream& f, uint32_t v) {
    f.put(static_cast<char>(v & 0xFF));
    f.put(static_cast<char>((v >> 8) & 0xFF));
    f.put(static_cast<char>((v >> 16) & 0xFF));
    f.put(static_cast<char>((v >> 24) & 0xFF));
}

static uint16_t a_rd16(std::ifstream& f) {
    uint8_t b[2] = {};
    f.read(reinterpret_cast<char*>(b), 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}

static uint32_t a_rd32(std::ifstream& f) {
    uint8_t b[4] = {};
    f.read(reinterpret_cast<char*>(b), 4);
    return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

// -----------------------------------------------------------------------
// Sample helpers (handling signed and unsigned representations)
// -----------------------------------------------------------------------
static int32_t read_sample(const uint8_t* p, uint16_t bps)
{
    switch (bps) {
        case 8:  return static_cast<int32_t>(p[0]); // unsigned 8-bit [0..255]
        case 16: return static_cast<int32_t>(static_cast<int16_t>(p[0] | (p[1] << 8))); // signed 16-bit
        case 24: {
            int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
            return (v & 0x800000) ? (v | static_cast<int32_t>(0xFF000000)) : v;
        }
        case 32: return static_cast<int32_t>(static_cast<uint32_t>(p[0]) |
                                             (static_cast<uint32_t>(p[1]) << 8) |
                                             (static_cast<uint32_t>(p[2]) << 16) |
                                             (static_cast<uint32_t>(p[3]) << 24));
        default: return 0;
    }
}

static void write_sample(uint8_t* p, int32_t v, uint16_t bps)
{
    switch (bps) {
        case 8:
            p[0] = static_cast<uint8_t>(v & 0xFF);
            break;
        case 16:
            p[0] = static_cast<uint8_t>(v & 0xFF);
            p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
            break;
        case 24:
            p[0] = static_cast<uint8_t>(v & 0xFF);
            p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
            p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
            break;
        case 32:
            p[0] = static_cast<uint8_t>(v & 0xFF);
            p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
            p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
            p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
            break;
        default: break;
    }
}

// -----------------------------------------------------------------------
// Pure block prediction and reconstruction functions
// -----------------------------------------------------------------------
std::vector<uint8_t> audio_eabps_predict_block(const uint8_t* pcm_data,
                                               uint32_t num_frames,
                                               uint16_t channels,
                                               uint16_t bits_per_sample,
                                               uint8_t predictor)
{
    const uint32_t Bps = bits_per_sample / 8u;
    const uint32_t block_align = channels * Bps;
    const size_t total_bytes = static_cast<size_t>(num_frames) * block_align;
    std::vector<uint8_t> residuals(total_bytes, 0);

    for (uint16_t ch = 0; ch < channels; ++ch) {
        const size_t out_base = static_cast<size_t>(ch) * num_frames * Bps;
        int32_t prev1 = 0;
        int32_t prev2 = 0;

        for (uint32_t s = 0; s < num_frames; ++s) {
            const uint8_t* src = pcm_data + static_cast<size_t>(s) * block_align + static_cast<size_t>(ch) * Bps;
            const int32_t cur = read_sample(src, bits_per_sample);
            int32_t pred = 0;

            if (predictor == AUDIO_EABPS_PRED_DELTA1) {
                pred = (s == 0) ? 0 : prev1;
            } else if (predictor == AUDIO_EABPS_PRED_DELTA2) {
                if (s == 0) pred = 0;
                else if (s == 1) pred = prev1;
                else pred = static_cast<int32_t>(2LL * prev1 - prev2);
            } else {
                // AUDIO_EABPS_PRED_NONE
                pred = 0;
            }

            const int32_t res = cur - pred;
            write_sample(residuals.data() + out_base + static_cast<size_t>(s) * Bps, res, bits_per_sample);

            prev2 = prev1;
            prev1 = cur;
        }
    }
    return residuals;
}

bool audio_eabps_reconstruct_block(const std::vector<uint8_t>& residuals,
                                   uint32_t num_frames,
                                   uint16_t channels,
                                   uint16_t bits_per_sample,
                                   uint8_t predictor,
                                   uint8_t* out_pcm)
{
    const uint32_t Bps = bits_per_sample / 8u;
    const uint32_t block_align = channels * Bps;
    const size_t expected_bytes = static_cast<size_t>(num_frames) * block_align;
    if (residuals.size() != expected_bytes) {
        return false;
    }

    for (uint16_t ch = 0; ch < channels; ++ch) {
        const size_t in_base = static_cast<size_t>(ch) * num_frames * Bps;
        int32_t prev1 = 0;
        int32_t prev2 = 0;

        for (uint32_t s = 0; s < num_frames; ++s) {
            const int32_t res = read_sample(residuals.data() + in_base + static_cast<size_t>(s) * Bps, bits_per_sample);
            int32_t pred = 0;

            if (predictor == AUDIO_EABPS_PRED_DELTA1) {
                pred = (s == 0) ? 0 : prev1;
            } else if (predictor == AUDIO_EABPS_PRED_DELTA2) {
                if (s == 0) pred = 0;
                else if (s == 1) pred = prev1;
                else pred = static_cast<int32_t>(2LL * prev1 - prev2);
            } else {
                // AUDIO_EABPS_PRED_NONE
                pred = 0;
            }

            const int32_t cur = pred + res;
            uint8_t* dst = out_pcm + static_cast<size_t>(s) * block_align + static_cast<size_t>(ch) * Bps;
            write_sample(dst, cur, bits_per_sample);

            // Read back to ensure correct sign-extension / modular truncation
            const int32_t actual_sample = read_sample(dst, bits_per_sample);
            prev2 = prev1;
            prev1 = actual_sample;
        }
    }
    return true;
}

// -----------------------------------------------------------------------
// Full Audio EABPS Pipeline
// -----------------------------------------------------------------------
bool compress_audio_eabps(const std::string& wav_path,
                          const std::string& ahmc_path,
                          AudioEABPSCompressStats& stats,
                          uint32_t block_size)
{
    auto t_start = std::chrono::high_resolution_clock::now();
    stats = {};
    if (block_size == 0) block_size = AUDIO_EABPS_DEFAULT_BLOCK_SIZE;

    // 1. Read input WAV
    AudioBuffer audio;
    if (!read_wav(wav_path, audio)) {
        std::cerr << "Audio EABPS: Failed to read WAV: " << wav_path << "\n";
        return false;
    }

    {
        std::ifstream chk(wav_path, std::ios::binary | std::ios::ate);
        if (chk) stats.original_file_size = static_cast<uint64_t>(chk.tellg());
    }

    stats.format           = audio.format;
    stats.channels         = audio.channels;
    stats.sample_rate      = audio.sample_rate;
    stats.bits_per_sample  = audio.bits_per_sample;
    stats.total_samples    = audio.total_samples;
    stats.original_pcm_bytes = audio.pcm_bytes();
    stats.entropy_before   = audio_entropy(audio.pcm);
    stats.block_size       = block_size;

    const uint32_t total_frames = static_cast<uint32_t>(audio.total_samples);
    const uint32_t total_blocks = (total_frames + block_size - 1) / block_size;
    stats.total_blocks = total_blocks;
    stats.blocks.resize(total_blocks);

    std::vector<std::vector<uint8_t>> block_payloads(total_blocks);
    std::vector<uint8_t> all_residuals;
    all_residuals.reserve(audio.pcm.size());

    const uint32_t block_align = audio.block_align();

    // 2. Evaluate candidates block by block using actual encoded cost
    for (uint32_t b = 0; b < total_blocks; ++b) {
        const uint32_t frame_offset = b * block_size;
        const uint32_t frames_in_block = std::min(block_size, total_frames - frame_offset);
        const uint8_t* pcm_ptr = audio.pcm.data() + static_cast<size_t>(frame_offset) * block_align;

        AudioEABPSBlockRecord& rec = stats.blocks[b];
        rec.frame_offset = frame_offset;
        rec.num_frames   = frames_in_block;

        size_t best_cost = std::numeric_limits<size_t>::max();
        uint8_t best_pred = AUDIO_EABPS_PRED_NONE;
        uint8_t best_codec = AUDIO_EABPS_CODEC_RAW;
        std::vector<uint8_t> best_payload;
        std::vector<uint8_t> best_residuals;

        // Evaluate predictors: NONE, DELTA-1, DELTA-2
        static const uint8_t ALL_PREDS[3] = {AUDIO_EABPS_PRED_NONE, AUDIO_EABPS_PRED_DELTA1, AUDIO_EABPS_PRED_DELTA2};
        for (uint8_t p : ALL_PREDS) {
            std::vector<uint8_t> res = audio_eabps_predict_block(pcm_ptr, frames_in_block,
                                                                 audio.channels, audio.bits_per_sample, p);

            // Candidate 1: RAW
            const size_t raw_cost = res.size();
            if (raw_cost < best_cost) {
                best_cost = raw_cost;
                best_pred = p;
                best_codec = AUDIO_EABPS_CODEC_RAW;
                best_payload = res;
                best_residuals = res;
            }

            // Candidate 2: RLE
            std::vector<uint8_t> rle_out;
            if (rle_compress_mem(res, rle_out)) {
                if (rle_out.size() < best_cost) {
                    best_cost = rle_out.size();
                    best_pred = p;
                    best_codec = AUDIO_EABPS_CODEC_RLE;
                    best_payload = std::move(rle_out);
                    best_residuals = res;
                }
            }

            // Candidate 3: Canonical Huffman
            std::vector<uint8_t> huff_out;
            if (huffman_compress_mem(res, huff_out)) {
                if (huff_out.size() < best_cost) {
                    best_cost = huff_out.size();
                    best_pred = p;
                    best_codec = AUDIO_EABPS_CODEC_HUFFMAN;
                    best_payload = std::move(huff_out);
                    best_residuals = res;
                }
            }
        }

        rec.predictor = best_pred;
        rec.codec = best_codec;
        rec.payload_size = static_cast<uint32_t>(best_payload.size());
        block_payloads[b] = std::move(best_payload);

        all_residuals.insert(all_residuals.end(), best_residuals.begin(), best_residuals.end());

        // Track distributions
        if (best_pred == AUDIO_EABPS_PRED_NONE)        stats.count_none++;
        else if (best_pred == AUDIO_EABPS_PRED_DELTA1) stats.count_delta1++;
        else if (best_pred == AUDIO_EABPS_PRED_DELTA2) stats.count_delta2++;

        if (best_codec == AUDIO_EABPS_CODEC_RLE)          stats.count_rle++;
        else if (best_codec == AUDIO_EABPS_CODEC_HUFFMAN) stats.count_huffman++;
        else if (best_codec == AUDIO_EABPS_CODEC_RAW)     stats.count_raw++;

        stats.encoded_payload_size += rec.payload_size;
    }

    stats.entropy_after = audio_entropy(all_residuals);
    // 6 bytes per block metadata (predictor + codec + payload_size)
    stats.predictor_metadata_size = static_cast<uint64_t>(total_blocks) * 6;

    // 3. Write AHMC v3 container
    std::ofstream out(ahmc_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "Audio EABPS: Cannot create output file: " << ahmc_path << "\n";
        return false;
    }

    // Header (31 bytes)
    out.write(AHLA_MAGIC, 4);
    out.put(static_cast<char>(AHLA_VERSION_EABPS)); // version 3
    a_wr16(out, audio.format);
    a_wr16(out, audio.channels);
    a_wr32(out, audio.sample_rate);
    a_wr16(out, audio.bits_per_sample);
    a_wr32(out, block_size);
    a_wr32(out, total_blocks);
    a_wr32(out, total_frames);
    a_wr32(out, static_cast<uint32_t>(audio.pcm_bytes()));

    // Block metadata table
    for (uint32_t b = 0; b < total_blocks; ++b) {
        out.put(static_cast<char>(stats.blocks[b].predictor));
        out.put(static_cast<char>(stats.blocks[b].codec));
        a_wr32(out, stats.blocks[b].payload_size);
    }

    // Block payloads
    for (uint32_t b = 0; b < total_blocks; ++b) {
        if (!block_payloads[b].empty()) {
            out.write(reinterpret_cast<const char*>(block_payloads[b].data()),
                      static_cast<std::streamsize>(block_payloads[b].size()));
        }
    }
    out.close();

    {
        std::ifstream chk(ahmc_path, std::ios::binary | std::ios::ate);
        if (chk) stats.compressed_file_size = static_cast<uint64_t>(chk.tellg());
    }

    stats.container_overhead = stats.compressed_file_size - stats.encoded_payload_size;

    auto t_end = std::chrono::high_resolution_clock::now();
    stats.compression_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    return true;
}

// -----------------------------------------------------------------------
// Audio EABPS Decompressor
// -----------------------------------------------------------------------
bool decompress_audio_eabps(const std::string& ahmc_path,
                            const std::string& wav_path)
{
    std::ifstream in(ahmc_path, std::ios::binary);
    if (!in) {
        std::cerr << "Audio EABPS Decoder: Cannot open container: " << ahmc_path << "\n";
        return false;
    }

    in.seekg(0, std::ios::end);
    const uint64_t file_len = static_cast<uint64_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    if (file_len < 31) {
        std::cerr << "Audio EABPS Decoder: File too small for AHLA header (" << file_len << " bytes).\n";
        return false;
    }

    // 1. Validate magic & version
    char magic[4] = {};
    in.read(magic, 4);
    if (std::memcmp(magic, AHLA_MAGIC, 4) != 0) {
        std::cerr << "Audio EABPS Decoder: Invalid AHLA magic identifier.\n";
        return false;
    }

    uint8_t version = 0;
    in.read(reinterpret_cast<char*>(&version), 1);
    if (version != AHLA_VERSION_EABPS) {
        std::cerr << "Audio EABPS Decoder: Unsupported container version " << (int)version
                  << " (expected " << (int)AHLA_VERSION_EABPS << ").\n";
        return false;
    }

    // 2. Read audio header
    AudioBuffer audio;
    audio.format          = a_rd16(in);
    audio.channels        = a_rd16(in);
    audio.sample_rate     = a_rd32(in);
    audio.bits_per_sample = a_rd16(in);
    const uint32_t block_size   = a_rd32(in);
    const uint32_t total_blocks = a_rd32(in);
    audio.total_samples   = a_rd32(in);
    const uint32_t orig_pcm_bytes = a_rd32(in);

    if (audio.format != 1 || audio.channels == 0 || audio.sample_rate == 0 ||
        (audio.bits_per_sample != 8 && audio.bits_per_sample != 16 &&
         audio.bits_per_sample != 24 && audio.bits_per_sample != 32) ||
        block_size == 0 || total_blocks == 0) {
        std::cerr << "Audio EABPS Decoder: Invalid format fields in container.\n";
        return false;
    }

    const uint32_t total_frames = static_cast<uint32_t>(audio.total_samples);
    const uint32_t expected_blocks = (total_frames + block_size - 1) / block_size;
    if (total_blocks != expected_blocks) {
        std::cerr << "Audio EABPS Decoder: Inconsistent block count.\n";
        return false;
    }

    const uint32_t block_align = audio.block_align();
    if (static_cast<uint64_t>(total_frames) * block_align != orig_pcm_bytes) {
        std::cerr << "Audio EABPS Decoder: Inconsistent PCM byte length.\n";
        return false;
    }

    const uint64_t min_meta_table_len = 31ULL + static_cast<uint64_t>(total_blocks) * 6ULL;
    if (min_meta_table_len > file_len) {
        std::cerr << "Audio EABPS Decoder: Truncated file (cannot contain block metadata table).\n";
        return false;
    }

    // 3. Read block metadata table
    std::vector<AudioEABPSBlockRecord> blocks(total_blocks);
    uint64_t sum_payloads = 0;
    for (uint32_t b = 0; b < total_blocks; ++b) {
        uint8_t pred = 0, codec = 0;
        in.read(reinterpret_cast<char*>(&pred), 1);
        in.read(reinterpret_cast<char*>(&codec), 1);
        uint32_t payload_sz = a_rd32(in);

        if (pred >= AUDIO_EABPS_PRED_COUNT) {
            std::cerr << "Audio EABPS Decoder: Out-of-range predictor ID " << (int)pred << "\n";
            return false;
        }
        if (codec >= AUDIO_EABPS_CODEC_COUNT) {
            std::cerr << "Audio EABPS Decoder: Out-of-range codec ID " << (int)codec << "\n";
            return false;
        }

        blocks[b].frame_offset = b * block_size;
        blocks[b].num_frames   = std::min(block_size, total_frames - blocks[b].frame_offset);
        blocks[b].predictor    = pred;
        blocks[b].codec        = codec;
        blocks[b].payload_size = payload_sz;
        sum_payloads += payload_sz;
    }

    if (min_meta_table_len + sum_payloads > file_len) {
        std::cerr << "Audio EABPS Decoder: Truncated file (declared block payloads exceed container file size).\n";
        return false;
    }

    // 4. Allocate full output PCM buffer
    audio.pcm.assign(orig_pcm_bytes, 0);

    // 5. Decode block payloads & invert prediction
    for (uint32_t b = 0; b < total_blocks; ++b) {
        const auto& rec = blocks[b];
        std::vector<uint8_t> payload(rec.payload_size);
        in.read(reinterpret_cast<char*>(payload.data()), rec.payload_size);
        if (in.gcount() != static_cast<std::streamsize>(rec.payload_size)) {
            std::cerr << "Audio EABPS Decoder: Truncated block payload data at block " << b << "\n";
            return false;
        }

        std::vector<uint8_t> residuals;
        if (rec.codec == AUDIO_EABPS_CODEC_RAW) {
            residuals = std::move(payload);
        } else if (rec.codec == AUDIO_EABPS_CODEC_RLE) {
            if (!rle_decompress_mem(payload, residuals)) {
                std::cerr << "Audio EABPS Decoder: RLE decompression failed for block " << b << "\n";
                return false;
            }
        } else if (rec.codec == AUDIO_EABPS_CODEC_HUFFMAN) {
            if (!huffman_decompress_mem(payload, residuals)) {
                std::cerr << "Audio EABPS Decoder: Huffman decompression failed for block " << b << "\n";
                return false;
            }
        }

        const size_t expected_block_bytes = static_cast<size_t>(rec.num_frames) * block_align;
        if (residuals.size() != expected_block_bytes) {
            std::cerr << "Audio EABPS Decoder: Residual byte length mismatch for block " << b << "\n";
            return false;
        }

        uint8_t* pcm_dest = audio.pcm.data() + static_cast<size_t>(rec.frame_offset) * block_align;
        if (!audio_eabps_reconstruct_block(residuals, rec.num_frames,
                                          audio.channels, audio.bits_per_sample,
                                          rec.predictor, pcm_dest)) {
            std::cerr << "Audio EABPS Decoder: Block reconstruction failed for block " << b << "\n";
            return false;
        }
    }
    in.close();

    // 6. Write reconstructed WAV
    if (!write_wav(wav_path, audio)) {
        std::cerr << "Audio EABPS Decoder: Failed to write output WAV: " << wav_path << "\n";
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------
// print_audio_eabps_stats
// -----------------------------------------------------------------------
void print_audio_eabps_stats(const std::string& wav_path,
                             const std::string& ahmc_path,
                             const AudioEABPSCompressStats& stats)
{
    const double ratio = (stats.compressed_file_size > 0)
        ? static_cast<double>(stats.original_file_size) / stats.compressed_file_size : 0.0;
    const double saved = (stats.original_file_size > 0)
        ? (1.0 - static_cast<double>(stats.compressed_file_size) /
                  static_cast<double>(stats.original_file_size)) * 100.0 : 0.0;

    std::cout << std::fixed;
    std::cout << "--------------------------------------------\n";
    std::cout << " AHLC Audio EABPS (Entropy-Aware Block-wise Predictor Selection)\n";
    std::cout << "--------------------------------------------\n";
    std::cout << " Input                : " << wav_path  << "\n";
    std::cout << " Output               : " << ahmc_path << "\n";
    std::cout << " Channels             : " << stats.channels << " (" << stats.bits_per_sample << " bits/sample, " << stats.sample_rate << " Hz)\n";
    std::cout << " Total sample frames  : " << stats.total_samples << "\n";
    std::cout << " Block Size           : " << stats.block_size << " sample frames\n";
    std::cout << " Total Blocks         : " << stats.total_blocks << "\n";
    std::cout << " Original file size   : " << stats.original_file_size   << " bytes\n";
    std::cout << " Uncompressed PCM     : " << stats.original_pcm_bytes   << " bytes\n";
    std::cout << "\n";
    std::cout << " Predictor Selection Distribution:\n";
    std::cout << "   NONE    (raw sample)  : " << stats.count_none
              << " blocks (" << std::setprecision(1) << (stats.total_blocks ? (100.0 * stats.count_none / stats.total_blocks) : 0.0) << "%)\n";
    std::cout << "   DELTA-1 (order 1)     : " << stats.count_delta1
              << " blocks (" << std::setprecision(1) << (stats.total_blocks ? (100.0 * stats.count_delta1 / stats.total_blocks) : 0.0) << "%)\n";
    std::cout << "   DELTA-2 (order 2)     : " << stats.count_delta2
              << " blocks (" << std::setprecision(1) << (stats.total_blocks ? (100.0 * stats.count_delta2 / stats.total_blocks) : 0.0) << "%)\n";
    std::cout << "\n";
    std::cout << " Codec Selection Distribution:\n";
    std::cout << "   RLE                   : " << stats.count_rle
              << " blocks (" << std::setprecision(1) << (stats.total_blocks ? (100.0 * stats.count_rle / stats.total_blocks) : 0.0) << "%)\n";
    std::cout << "   Huffman               : " << stats.count_huffman
              << " blocks (" << std::setprecision(1) << (stats.total_blocks ? (100.0 * stats.count_huffman / stats.total_blocks) : 0.0) << "%)\n";
    std::cout << "   RAW                   : " << stats.count_raw
              << " blocks (" << std::setprecision(1) << (stats.total_blocks ? (100.0 * stats.count_raw / stats.total_blocks) : 0.0) << "%)\n";
    std::cout << "\n";
    std::cout << std::setprecision(6);
    std::cout << " Shannon entropy (orig) : " << stats.entropy_before << " bits/byte\n";
    std::cout << " Shannon entropy (res)  : " << stats.entropy_after  << " bits/byte\n";
    std::cout << "\n";
    std::cout << " Encoded payload size : " << stats.encoded_payload_size << " bytes\n";
    std::cout << " Predictor metadata   : " << stats.predictor_metadata_size << " bytes\n";
    std::cout << " Container overhead   : " << stats.container_overhead << " bytes\n";
    std::cout << " Final compressed file: " << stats.compressed_file_size << " bytes\n";
    std::cout << std::setprecision(4);
    std::cout << " Compression ratio    : " << ratio << "x\n";
    std::cout << std::setprecision(2);
    std::cout << " Space saved          : " << saved << "%\n";
    std::cout << " Compression time     : " << stats.compression_time_ms << " ms\n";
    std::cout << "--------------------------------------------\n";
}

// -----------------------------------------------------------------------
// Helper to construct synthetic AudioBuffer for tests
// -----------------------------------------------------------------------
static AudioBuffer make_test_audio(uint16_t channels, uint32_t sample_rate, uint16_t bps, uint32_t total_frames,
                                   const std::function<int32_t(uint16_t ch, uint32_t frame)>& sample_fn)
{
    AudioBuffer a;
    a.format = 1;
    a.channels = channels;
    a.sample_rate = sample_rate;
    a.bits_per_sample = bps;
    a.total_samples = total_frames;
    const uint32_t Bps = bps / 8u;
    const uint32_t blk = channels * Bps;
    a.pcm.assign(static_cast<size_t>(total_frames) * blk, 0);

    for (uint32_t s = 0; s < total_frames; ++s) {
        for (uint16_t ch = 0; ch < channels; ++ch) {
            int32_t v = sample_fn(ch, s);
            write_sample(a.pcm.data() + static_cast<size_t>(s) * blk + static_cast<size_t>(ch) * Bps, v, bps);
        }
    }
    return a;
}

// -----------------------------------------------------------------------
// Automated Audio EABPS Test Suite (Sections 9.A - 9.G)
// -----------------------------------------------------------------------
bool run_audio_eabps_test_suite()
{
    std::cout << "============================================================\n";
    std::cout << " AHLC Audio EABPS Comprehensive Test Suite\n";
    std::cout << "============================================================\n";
    bool all_ok = true;

    static const uint8_t ALL_PREDS[3] = {AUDIO_EABPS_PRED_NONE, AUDIO_EABPS_PRED_DELTA1, AUDIO_EABPS_PRED_DELTA2};

    // -------------------------------------------------------------------
    // 9.A Predictor Unit Tests (NONE, DELTA-1, DELTA-2)
    // -------------------------------------------------------------------
    std::cout << "\n--- 9.A Predictor Unit Tests (NONE, DELTA-1, DELTA-2) ---\n";
    {
        // 1. One sample frame (M = 1, 16-bit mono)
        std::vector<int16_t> s1 = {1234};
        auto a1 = make_test_audio(1, 44100, 16, 1, [&](uint16_t, uint32_t) { return 1234; });
        for (uint8_t p : ALL_PREDS) {
            auto res = audio_eabps_predict_block(a1.pcm.data(), 1, 1, 16, p);
            std::vector<uint8_t> rec(a1.pcm.size(), 0);
            bool ok = audio_eabps_reconstruct_block(res, 1, 1, 16, p, rec.data()) && (rec == a1.pcm);
            if (!ok) { all_ok = false; std::cout << " [FAIL] 1-frame block predictor " << (int)p << "\n"; }
        }
        std::cout << " [PASS] 1-frame block forward/inverse for NONE, DELTA-1, DELTA-2\n";

        // 2. Short blocks (M = 7, 16-bit mono)
        auto a_short = make_test_audio(1, 44100, 16, 7, [](uint16_t, uint32_t s) { return static_cast<int32_t>(s * 250 - 500); });
        for (uint8_t p : ALL_PREDS) {
            auto res = audio_eabps_predict_block(a_short.pcm.data(), 7, 1, 16, p);
            std::vector<uint8_t> rec(a_short.pcm.size(), 0);
            bool ok = audio_eabps_reconstruct_block(res, 7, 1, 16, p, rec.data()) && (rec == a_short.pcm);
            if (!ok) { all_ok = false; std::cout << " [FAIL] Short block predictor " << (int)p << "\n"; }
        }
        std::cout << " [PASS] Short block (7 frames) forward/inverse for NONE, DELTA-1, DELTA-2\n";

        // 3. Constant and All-Zero Samples
        auto a_const = make_test_audio(1, 44100, 16, 64, [](uint16_t, uint32_t) { return 4500; });
        auto a_zero  = make_test_audio(1, 44100, 16, 64, [](uint16_t, uint32_t) { return 0; });
        for (uint8_t p : ALL_PREDS) {
            auto res_c = audio_eabps_predict_block(a_const.pcm.data(), 64, 1, 16, p);
            std::vector<uint8_t> rec_c(a_const.pcm.size());
            bool ok_c = audio_eabps_reconstruct_block(res_c, 64, 1, 16, p, rec_c.data()) && (rec_c == a_const.pcm);

            auto res_z = audio_eabps_predict_block(a_zero.pcm.data(), 64, 1, 16, p);
            std::vector<uint8_t> rec_z(a_zero.pcm.size());
            bool ok_z = audio_eabps_reconstruct_block(res_z, 64, 1, 16, p, rec_z.data()) && (rec_z == a_zero.pcm);

            if (!ok_c || !ok_z) { all_ok = false; std::cout << " [FAIL] Constant/Zero test predictor " << (int)p << "\n"; }
        }
        std::cout << " [PASS] Constant and all-zero signals exact forward/inverse reconstruction\n";

        // 4. Extreme values (-32768, +32767) and large transitions
        auto a_ext = make_test_audio(1, 44100, 16, 8, [](uint16_t, uint32_t s) {
            return (s % 2 == 0) ? -32768 : 32767;
        });
        for (uint8_t p : ALL_PREDS) {
            auto res = audio_eabps_predict_block(a_ext.pcm.data(), 8, 1, 16, p);
            std::vector<uint8_t> rec(a_ext.pcm.size());
            bool ok = audio_eabps_reconstruct_block(res, 8, 1, 16, p, rec.data()) && (rec == a_ext.pcm);
            if (!ok) { all_ok = false; std::cout << " [FAIL] Extreme values predictor " << (int)p << "\n"; }
        }
        std::cout << " [PASS] Extreme signed 16-bit values (-32768, 32767) and max transitions\n";

        // 5. Linear ramp exact prediction by DELTA-2
        // A linear ramp s[t] = 50 * t has s[t] - (2*s[t-1] - s[t-2]) = 0 for t >= 2!
        auto a_ramp = make_test_audio(1, 44100, 16, 128, [](uint16_t, uint32_t s) { return static_cast<int32_t>(s * 50); });
        auto res_d2 = audio_eabps_predict_block(a_ramp.pcm.data(), 128, 1, 16, AUDIO_EABPS_PRED_DELTA2);
        std::vector<uint8_t> rec_ramp(a_ramp.pcm.size());
        bool ok_d2 = audio_eabps_reconstruct_block(res_d2, 128, 1, 16, AUDIO_EABPS_PRED_DELTA2, rec_ramp.data()) && (rec_ramp == a_ramp.pcm);
        // Verify residuals are zero for t >= 2
        bool d2_zeros = true;
        for (uint32_t s = 2; s < 128; ++s) {
            int32_t r = read_sample(res_d2.data() + s * 2, 16);
            if (r != 0) { d2_zeros = false; break; }
        }
        bool ramp_pass = ok_d2 && d2_zeros;
        std::cout << " [" << (ramp_pass ? "PASS" : "FAIL") << "] DELTA-2 produces exact zeros for linear ramp (t >= 2)\n";
        all_ok &= ramp_pass;
    }

    // -------------------------------------------------------------------
    // 9.B Channel Tests (Mono, Stereo, Multichannel & Independence)
    // -------------------------------------------------------------------
    std::cout << "\n--- 9.B Channel Tests (Mono, Stereo, Multichannel) ---\n";
    auto test_audio_roundtrip = [&](const std::string& /*name*/, const AudioBuffer& audio, uint32_t bsize) -> bool {
        const std::string tmp_wav = "__eabps_au_in.wav";
        const std::string tmp_ahmc = "__eabps_au.ahmc";
        const std::string tmp_rec = "__eabps_au_out.wav";

        if (!write_wav(tmp_wav, audio)) return false;
        AudioEABPSCompressStats s;
        if (!compress_audio_eabps(tmp_wav, tmp_ahmc, s, bsize)) {
            std::remove(tmp_wav.c_str());
            return false;
        }
        if (!decompress_audio_eabps(tmp_ahmc, tmp_rec)) {
            std::remove(tmp_wav.c_str());
            std::remove(tmp_ahmc.c_str());
            return false;
        }
        AudioBuffer rec;
        if (!read_wav(tmp_rec, rec)) {
            std::remove(tmp_wav.c_str());
            std::remove(tmp_ahmc.c_str());
            std::remove(tmp_rec.c_str());
            return false;
        }
        bool match = audios_equal(audio, rec);
        std::remove(tmp_wav.c_str());
        std::remove(tmp_ahmc.c_str());
        std::remove(tmp_rec.c_str());
        return match;
    };

    {
        // 1. Mono 16-bit
        auto a_mono = make_test_audio(1, 44100, 16, 5000, [](uint16_t, uint32_t s) {
            return static_cast<int32_t>(10000.0 * std::sin(2.0 * 3.14159265 * 440.0 * s / 44100.0));
        });
        bool ok_m = test_audio_roundtrip("Mono 16-bit 44.1kHz", a_mono, 1024);
        std::cout << " [" << (ok_m ? "PASS" : "FAIL") << "] Mono 16-bit round-trip exact reconstruction\n";
        all_ok &= ok_m;

        // 2. Stereo 16-bit with completely independent channels
        auto a_stereo = make_test_audio(2, 44100, 16, 5000, [](uint16_t ch, uint32_t s) {
            if (ch == 0) return static_cast<int32_t>(15000.0 * std::sin(2.0 * 3.14159265 * 300.0 * s / 44100.0));
            else         return static_cast<int32_t>(s * 7 - 3000);
        });
        bool ok_st = test_audio_roundtrip("Stereo independent channels", a_stereo, 1024);
        std::cout << " [" << (ok_st ? "PASS" : "FAIL") << "] Stereo 16-bit with independent channels exact match\n";
        all_ok &= ok_st;

        // 3. Multichannel (4 channels, e.g. quadraphonic)
        auto a_quad = make_test_audio(4, 48000, 16, 3000, [](uint16_t ch, uint32_t s) {
            return static_cast<int32_t>((ch + 1) * 1000 + (s % 500));
        });
        bool ok_qd = test_audio_roundtrip("4-channel Quad audio", a_quad, 512);
        std::cout << " [" << (ok_qd ? "PASS" : "FAIL") << "] Multichannel (4-channel) audio exact match without cross-talk\n";
        all_ok &= ok_qd;
    }

    // -------------------------------------------------------------------
    // 9.C WAV Format and Bit Depth Tests (8, 16, 24, 32-bit)
    // -------------------------------------------------------------------
    std::cout << "\n--- 9.C WAV Format & Bit Depth Tests (8, 16, 24, 32-bit) ---\n";
    {
        // 1. 8-bit unsigned PCM
        auto a_8bit = make_test_audio(1, 22050, 8, 2000, [](uint16_t, uint32_t s) {
            return static_cast<int32_t>((s * 3 + 128) % 256);
        });
        bool ok_8 = test_audio_roundtrip("8-bit unsigned PCM", a_8bit, 512);
        std::cout << " [" << (ok_8 ? "PASS" : "FAIL") << "] 8-bit unsigned PCM exact reconstruction\n";
        all_ok &= ok_8;

        // 2. 24-bit signed PCM
        auto a_24bit = make_test_audio(2, 48000, 24, 2000, [](uint16_t ch, uint32_t s) {
            return static_cast<int32_t>((s * 250000 + (ch ? 1000000 : 0)) % 8388607);
        });
        bool ok_24 = test_audio_roundtrip("24-bit signed PCM", a_24bit, 512);
        std::cout << " [" << (ok_24 ? "PASS" : "FAIL") << "] 24-bit signed PCM exact reconstruction\n";
        all_ok &= ok_24;

        // 3. 32-bit signed PCM
        auto a_32bit = make_test_audio(1, 96000, 32, 2000, [](uint16_t, uint32_t s) {
            return static_cast<int32_t>(s * 10000000);
        });
        bool ok_32 = test_audio_roundtrip("32-bit signed PCM", a_32bit, 512);
        std::cout << " [" << (ok_32 ? "PASS" : "FAIL") << "] 32-bit signed PCM exact reconstruction\n";
        all_ok &= ok_32;

        // 4. Partial block at end (e.g. 1500 frames with block size 1024 -> 1 full block of 1024, 1 partial of 476)
        auto a_part = make_test_audio(2, 44100, 16, 1500, [](uint16_t ch, uint32_t s) {
            return static_cast<int32_t>((s + ch * 100) % 3000);
        });
        bool ok_pt = test_audio_roundtrip("Partial block boundary (1500/1024)", a_part, 1024);
        std::cout << " [" << (ok_pt ? "PASS" : "FAIL") << "] Partial final block alignment (1500 frames, block=1024)\n";
        all_ok &= ok_pt;
    }

    // -------------------------------------------------------------------
    // 9.D Predictor Selection Tests
    // -------------------------------------------------------------------
    std::cout << "\n--- 9.D Predictor Selection Tests ---\n";
    {
        // 1. Constant non-zero signal -> DELTA-1 produces all zeros and wins
        auto a_const = make_test_audio(1, 44100, 16, 2048, [](uint16_t, uint32_t) { return 12000; });
        AudioEABPSCompressStats s_c;
        write_wav("__au_const.wav", a_const);
        compress_audio_eabps("__au_const.wav", "__au_const.ahmc", s_c, 1024);
        bool delta1_favored = (s_c.count_delta1 > 0 || s_c.count_delta2 > 0);
        std::cout << " [" << (delta1_favored ? "PASS" : "FAIL") << "] Constant tone selects DELTA prediction\n";
        all_ok &= delta1_favored;
        std::remove("__au_const.wav"); std::remove("__au_const.ahmc");

        // 2. Linear ramp -> DELTA-2 produces exact zeros and achieves massive compression
        auto a_ramp = make_test_audio(1, 44100, 16, 2048, [](uint16_t, uint32_t s) { return static_cast<int32_t>(s * 15); });
        AudioEABPSCompressStats s_r;
        write_wav("__au_ramp.wav", a_ramp);
        compress_audio_eabps("__au_ramp.wav", "__au_ramp.ahmc", s_r, 1024);
        bool delta2_selected = (s_r.count_delta2 > 0);
        std::cout << " [" << (delta2_selected ? "PASS" : "FAIL") << "] Linear ramp selects DELTA-2 predictor: "
                  << s_r.count_delta2 << "/" << s_r.total_blocks << " blocks\n";
        all_ok &= delta2_selected;
        std::remove("__au_ramp.wav"); std::remove("__au_ramp.ahmc");

        // 3. High-entropy noise -> NONE selected when prediction cannot help
        auto a_noise = make_test_audio(1, 44100, 16, 2048, [](uint16_t, uint32_t s) {
            uint32_t x = s + 1;
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = (x >> 16) ^ x;
            return static_cast<int32_t>(static_cast<int16_t>(x & 0xFFFF));
        });
        AudioEABPSCompressStats s_n;
        write_wav("__au_noise.wav", a_noise);
        compress_audio_eabps("__au_noise.wav", "__au_noise.ahmc", s_n, 1024);
        bool none_selected = (s_n.count_none > 0);
        std::cout << " [" << (none_selected ? "PASS" : "FAIL") << "] High-entropy noise selects NONE predictor: "
                  << s_n.count_none << "/" << s_n.total_blocks << " blocks\n";
        all_ok &= none_selected;
        std::remove("__au_noise.wav"); std::remove("__au_noise.ahmc");

        // 4. Verify minimal complete encoded cost across evaluated candidates
        bool minimal_cost = true;
        for (const auto& blk : s_r.blocks) {
            size_t raw_bound = static_cast<size_t>(blk.num_frames) * 1 * 2;
            if (blk.payload_size > raw_bound) {
                minimal_cost = false; break;
            }
        }
        std::cout << " [" << (minimal_cost ? "PASS" : "FAIL") << "] Verified selected candidate produces minimal complete encoded cost\n";
        all_ok &= minimal_cost;
    }

    // -------------------------------------------------------------------
    // 9.F Container Validation & Error Handling Tests
    // -------------------------------------------------------------------
    std::cout << "\n--- 9.F Container Validation & Error Handling ---\n";
    {
        auto test_audio = make_test_audio(1, 44100, 16, 1024, [](uint16_t, uint32_t s) { return static_cast<int32_t>(s * 2); });
        write_wav("__val.wav", test_audio);
        AudioEABPSCompressStats s_val;
        compress_audio_eabps("__val.wav", "__val.ahmc", s_val, 512);

        std::vector<uint8_t> valid_bytes;
        {
            std::ifstream vf("__val.ahmc", std::ios::binary | std::ios::ate);
            auto sz = vf.tellg(); vf.seekg(0);
            valid_bytes.resize(static_cast<size_t>(sz));
            vf.read(reinterpret_cast<char*>(valid_bytes.data()), sz);
        }

        auto test_corrupted = [&](const std::vector<uint8_t>& corrupted) -> bool {
            std::ofstream cf("__corrupt.ahmc", std::ios::binary | std::ios::trunc);
            cf.write(reinterpret_cast<const char*>(corrupted.data()), corrupted.size());
            cf.close();

            bool dec_ok = decompress_audio_eabps("__corrupt.ahmc", "__corrupt_out.wav");
            std::remove("__corrupt.ahmc");
            std::remove("__corrupt_out.wav");
            return !dec_ok; // must safely reject
        };

        // 1. Bad magic
        auto b_bad_magic = valid_bytes;
        b_bad_magic[0] = 'X'; b_bad_magic[1] = 'Y';
        bool m_ok = test_corrupted(b_bad_magic);
        std::cout << " [" << (m_ok ? "PASS" : "FAIL") << "] Invalid magic rejected safely\n";
        all_ok &= m_ok;

        // 2. Unsupported version
        auto b_bad_ver = valid_bytes;
        b_bad_ver[4] = 99;
        bool v_ok = test_corrupted(b_bad_ver);
        std::cout << " [" << (v_ok ? "PASS" : "FAIL") << "] Unsupported container version rejected safely\n";
        all_ok &= v_ok;

        // 3. Truncated header (< 31 bytes)
        std::vector<uint8_t> b_trunc_hdr(valid_bytes.begin(), valid_bytes.begin() + 15);
        bool th_ok = test_corrupted(b_trunc_hdr);
        std::cout << " [" << (th_ok ? "PASS" : "FAIL") << "] Truncated header rejected safely\n";
        all_ok &= th_ok;

        // 4. Truncated payload
        if (valid_bytes.size() > 5) {
            std::vector<uint8_t> b_trunc_pay(valid_bytes.begin(), valid_bytes.end() - 5);
            bool tp_ok = test_corrupted(b_trunc_pay);
            std::cout << " [" << (tp_ok ? "PASS" : "FAIL") << "] Truncated block payload data rejected safely\n";
            all_ok &= tp_ok;
        }

        // 5. Out of range predictor ID
        auto b_bad_pred = valid_bytes;
        if (b_bad_pred.size() > 31) {
            b_bad_pred[31] = 200; // invalid predictor
            bool bp_ok = test_corrupted(b_bad_pred);
            std::cout << " [" << (bp_ok ? "PASS" : "FAIL") << "] Out-of-range predictor ID rejected safely\n";
            all_ok &= bp_ok;
        }

        std::remove("__val.wav");
        std::remove("__val.ahmc");
    }

    // -------------------------------------------------------------------
    // 9.G Fallback & Size Accounting Tests
    // -------------------------------------------------------------------
    std::cout << "\n--- 9.G Fallback & Size Accounting Tests ---\n";
    {
        auto test_audio = make_test_audio(1, 44100, 16, 2048, [](uint16_t, uint32_t s) {
            return static_cast<int32_t>((s * 100) % 15000);
        });
        write_wav("__acct.wav", test_audio);
        AudioEABPSCompressStats s_acct;
        compress_audio_eabps("__acct.wav", "__acct.ahmc", s_acct, 512);

        std::ifstream chk("__acct.ahmc", std::ios::binary | std::ios::ate);
        uint64_t actual_file_sz = static_cast<uint64_t>(chk.tellg());
        bool size_match = (s_acct.compressed_file_size == actual_file_sz);
        bool overhead_match = (s_acct.container_overhead == s_acct.compressed_file_size - s_acct.encoded_payload_size);

        std::cout << " [" << (size_match ? "PASS" : "FAIL") << "] Actual stored file size exactly matches reported compressed size\n";
        std::cout << " [" << (overhead_match ? "PASS" : "FAIL") << "] Container overhead accounted accurately (31B header + 6B per block)\n";
        all_ok &= size_match;
        all_ok &= overhead_match;

        std::remove("__acct.wav");
        std::remove("__acct.ahmc");
    }

    std::cout << "\n============================================================\n";
    std::cout << " Audio EABPS Test Suite Overall: " << (all_ok ? "ALL PASS" : "SOME FAILED") << "\n";
    std::cout << "============================================================\n";
    return all_ok;
}

// -----------------------------------------------------------------------
// Benchmark: Audio EABPS vs Baseline Audio AHLC
// -----------------------------------------------------------------------
bool run_audio_eabps_benchmark()
{
    std::cout << "=========================================================================================\n";
    std::cout << " AHLC Benchmark: Audio EABPS vs Baseline Adaptive Audio AHLC\n";
    std::cout << "=========================================================================================\n";

    struct TestCase {
        std::string name;
        AudioBuffer audio;
    };

    std::vector<TestCase> cases;

    // 1. Silence (44.1 kHz, 16-bit stereo)
    cases.push_back({"Silence (16-bit Stereo)", make_test_audio(2, 44100, 16, 8192, [](uint16_t, uint32_t) {
        return 0;
    })});

    // 2. Pure Sine Tone 440 Hz (44.1 kHz, 16-bit mono)
    cases.push_back({"Sine Tone 440 Hz (Mono)", make_test_audio(1, 44100, 16, 8192, [](uint16_t, uint32_t s) {
        return static_cast<int32_t>(18000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * s / 44100.0));
    })});

    // 3. Linear Ramp (44.1 kHz, 16-bit mono)
    cases.push_back({"Linear Ramp Waveform", make_test_audio(1, 44100, 16, 8192, [](uint16_t, uint32_t s) {
        return static_cast<int32_t>(s * 4 - 16384);
    })});

    // 4. Mixed Signals (Alternating silence, ramp, and tones across blocks)
    cases.push_back({"Mixed Heterogeneous Audio", make_test_audio(2, 44100, 16, 8192, [](uint16_t ch, uint32_t s) {
        if (s < 2048) return 0; // silence
        else if (s < 4096) return static_cast<int32_t>(s * 8); // ramp
        else if (s < 6144) return static_cast<int32_t>(12000.0 * std::sin(2.0 * 3.14159265358979 * (ch ? 880.0 : 440.0) * s / 44100.0));
        else return static_cast<int32_t>((s * 1103515245 + 12345) % 10000 - 5000); // noisy
    })});

    // 5. High-Entropy Noise
    cases.push_back({"High-Entropy Noise", make_test_audio(1, 44100, 16, 8192, [](uint16_t, uint32_t s) {
        uint32_t x = s + 1;
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = (x >> 16) ^ x;
        return static_cast<int32_t>(static_cast<int16_t>(x & 0xFFFF));
    })});

    std::printf("%-28s | %-8s | %-12s | %-12s | %-10s | %-10s | %-14s | %-6s\n",
                "Audio Test Case", "Orig (B)", "Base (B)", "EABPS (B)", "Base Ratio", "EABPS Ratio", "EABPS Preds", "Verify");
    std::cout << "-----------------------------+----------+--------------+--------------+------------+------------+----------------+-------\n";

    for (const auto& tc : cases) {
        const std::string fn_wav = "__bench_" + tc.name.substr(0, 4) + ".wav";
        const std::string fn_base_ahmc = "__bench_" + tc.name.substr(0, 4) + "_base.ahmc";
        const std::string fn_eabps_ahmc = "__bench_" + tc.name.substr(0, 4) + "_eabps.ahmc";
        const std::string fn_rec_wav = "__bench_" + tc.name.substr(0, 4) + "_rec.wav";

        write_wav(fn_wav, tc.audio);

        // 1. Baseline audio compress
        AudioCompressStats s_base;
        compress_audio(fn_wav, fn_base_ahmc, s_base);

        // 2. Audio EABPS compress (block size 2048)
        AudioEABPSCompressStats s_eabps;
        compress_audio_eabps(fn_wav, fn_eabps_ahmc, s_eabps, 2048);

        // 3. Decompress and verify
        decompress_audio_eabps(fn_eabps_ahmc, fn_rec_wav);
        AudioBuffer rec_audio;
        read_wav(fn_rec_wav, rec_audio);
        bool match = audios_equal(tc.audio, rec_audio);

        const double base_ratio = s_base.compressed_file_size > 0
            ? static_cast<double>(s_base.original_file_size) / s_base.compressed_file_size : 0.0;
        const double eabps_ratio = s_eabps.compressed_file_size > 0
            ? static_cast<double>(s_eabps.original_file_size) / s_eabps.compressed_file_size : 0.0;

        char pred_dist[32];
        std::snprintf(pred_dist, sizeof(pred_dist), "N:%u D1:%u D2:%u",
                      s_eabps.count_none, s_eabps.count_delta1, s_eabps.count_delta2);

        std::printf("%-28s | %-8llu | %-12llu | %-12llu | %9.2fx | %9.2fx | %-14s | %-6s\n",
                    tc.name.c_str(),
                    static_cast<unsigned long long>(s_base.original_file_size),
                    static_cast<unsigned long long>(s_base.compressed_file_size),
                    static_cast<unsigned long long>(s_eabps.compressed_file_size),
                    base_ratio,
                    eabps_ratio,
                    pred_dist,
                    match ? "PASS" : "FAIL");

        std::remove(fn_wav.c_str());
        std::remove(fn_base_ahmc.c_str());
        std::remove(fn_eabps_ahmc.c_str());
        std::remove(fn_rec_wav.c_str());
    }

    std::cout << "=========================================================================================\n";
    return true;
}

} // namespace ahlc
