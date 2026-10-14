/*
 * audio_predictor.cpp
 *
 * Delta predictor + adaptive entropy coding pipeline.
 *
 * .ahmc audio format:
 *   v1 (legacy, RLE-only):
 *     [0-3]  "AHLA"  [4] version=1  [5-6] fmt  [7-8] ch  [9-12] sr
 *     [13-14] bps  [15-18] total_samples  [19] predictor
 *     [20-23] orig_pcm  [24-27] comp_size  [28+] RLE data
 *
 *   v2 (adaptive):
 *     [0-3]  "AHLA"  [4] version=2  [5-6] fmt  [7-8] ch  [9-12] sr
 *     [13-14] bps  [15-18] total_samples  [19] predictor  [20] codec
 *     [21-24] orig_pcm  [25-28] comp_size  [29+] codec data
 */

#include "audio_predictor.hpp"
#include "audio_eabps.hpp"
#include "../rle/rle_encoder.hpp"
#include "../rle/rle_decoder.hpp"
#include "../entropy/huffman.hpp"

#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <cmath>

namespace ahlc {

static constexpr char AHLA_MAGIC[4] = {'A','H','L','A'};

// Little-endian helpers
static void a_wr16(std::ofstream& f,uint16_t v){f.put(v&0xFF);f.put((v>>8)&0xFF);}
static void a_wr32(std::ofstream& f,uint32_t v){f.put(v&0xFF);f.put((v>>8)&0xFF);f.put((v>>16)&0xFF);f.put((v>>24)&0xFF);}
static uint16_t a_rd16(std::ifstream& f){uint8_t b[2]={};f.read(reinterpret_cast<char*>(b),2);return static_cast<uint16_t>(b[0]|(b[1]<<8));}
static uint32_t a_rd32(std::ifstream& f){uint8_t b[4]={};f.read(reinterpret_cast<char*>(b),4);return b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24);}

// ---------------------------------------------------------------
// Sample helpers
// ---------------------------------------------------------------
static int32_t read_sample(const uint8_t* p, uint16_t bps)
{
    switch (bps) {
        case 8:  return static_cast<int32_t>(p[0]);
        case 16: return static_cast<int32_t>(static_cast<int16_t>(p[0]|(p[1]<<8)));
        case 24: { int32_t v=p[0]|(p[1]<<8)|(p[2]<<16); return (v&0x800000)?v|static_cast<int32_t>(0xFF000000):v; }
        case 32: return static_cast<int32_t>(static_cast<uint32_t>(p[0])|(static_cast<uint32_t>(p[1])<<8)|(static_cast<uint32_t>(p[2])<<16)|(static_cast<uint32_t>(p[3])<<24));
        default: return 0;
    }
}
static void write_sample(uint8_t* p, int32_t v, uint16_t bps)
{
    switch (bps) {
        case 8:  p[0]=v&0xFF; break;
        case 16: p[0]=v&0xFF;p[1]=(v>>8)&0xFF; break;
        case 24: p[0]=v&0xFF;p[1]=(v>>8)&0xFF;p[2]=(v>>16)&0xFF; break;
        case 32: p[0]=v&0xFF;p[1]=(v>>8)&0xFF;p[2]=(v>>16)&0xFF;p[3]=(v>>24)&0xFF; break;
        default: break;
    }
}

// ---------------------------------------------------------------
// encode_audio_predictor
// ---------------------------------------------------------------
std::vector<uint8_t> encode_audio_predictor(const AudioBuffer& audio)
{
    uint32_t Bps = audio.bytes_per_sample();
    uint32_t blk = audio.block_align();
    uint64_t N   = audio.total_samples;
    std::vector<uint8_t> residuals(audio.pcm.size(), 0);

    for (uint16_t ch = 0; ch < audio.channels; ++ch) {
        int32_t prev = 0;
        uint64_t out_base = static_cast<uint64_t>(ch) * N * Bps;
        for (uint64_t s = 0; s < N; ++s) {
            const uint8_t* src = audio.pcm.data() + s*blk + ch*Bps;
            int32_t cur = read_sample(src, audio.bits_per_sample);
            write_sample(residuals.data() + out_base + s*Bps, cur - prev, audio.bits_per_sample);
            prev = cur;
        }
    }
    return residuals;
}

// ---------------------------------------------------------------
// decode_audio_predictor
// ---------------------------------------------------------------
bool decode_audio_predictor(const std::vector<uint8_t>& residuals, AudioBuffer& audio)
{
    uint32_t Bps = audio.bytes_per_sample();
    uint32_t blk = audio.block_align();
    uint64_t N   = audio.total_samples;
    if (residuals.size() != audio.pcm_bytes()) {
        std::cerr << "Audio: residual size mismatch.\n"; return false;
    }
    audio.pcm.assign(audio.pcm_bytes(), 0);
    for (uint16_t ch = 0; ch < audio.channels; ++ch) {
        int32_t prev = 0;
        uint64_t in_base = static_cast<uint64_t>(ch) * N * Bps;
        for (uint64_t s = 0; s < N; ++s) {
            int32_t res = read_sample(residuals.data() + in_base + s*Bps, audio.bits_per_sample);
            int32_t cur = prev + res;
            uint8_t* dst = audio.pcm.data() + s*blk + ch*Bps;
            write_sample(dst, cur, audio.bits_per_sample);
            prev = read_sample(dst, audio.bits_per_sample);
        }
    }
    return true;
}

// ---------------------------------------------------------------
// print_audio_predictor_analysis
// ---------------------------------------------------------------
void print_audio_predictor_analysis(const AudioBuffer& audio,
                                    const std::vector<uint8_t>& residuals)
{
    double ho = audio_entropy(audio.pcm);
    double hr = audio_entropy(residuals);
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "--------------------------------------------\n AHLC Audio Prediction Analysis\n Predictor: delta (current − previous sample)\n--------------------------------------------\n";
    std::cout << " Original PCM bytes : " << audio.pcm.size() << "\n";
    std::cout << " Residual bytes     : " << residuals.size()  << "\n";
    std::cout << " Original entropy   : " << ho << " bits/byte\n";
    std::cout << " Residual entropy   : " << hr << " bits/byte\n";
    std::cout << " Channels           : " << audio.channels    << "\n";
    std::cout << " Sample rate        : " << audio.sample_rate << " Hz\n";
    std::cout << (hr<ho ? " Result: prediction REDUCED entropy.\n" : " Result: prediction did not reduce entropy.\n");
    std::cout << "--------------------------------------------\n";
}

// ---------------------------------------------------------------
// compress_audio — adaptive: tries RLE and Huffman, picks smaller
// ---------------------------------------------------------------
bool compress_audio(const std::string& wav_path,
                    const std::string& ahmc_path,
                    AudioCompressStats& stats)
{
    stats = {};

    // 1. Read WAV
    AudioBuffer audio;
    if (!read_wav(wav_path, audio)) return false;
    stats.original_pcm_bytes = audio.pcm_bytes();
    stats.entropy_before     = audio_entropy(audio.pcm);
    stats.channels           = audio.channels;
    stats.sample_rate        = audio.sample_rate;
    stats.bits_per_sample    = audio.bits_per_sample;
    stats.total_samples      = audio.total_samples;

    {
        std::ifstream chk(wav_path, std::ios::binary | std::ios::ate);
        if (chk) stats.original_file_size = static_cast<uint64_t>(chk.tellg());
    }

    // 2. Delta predictor → residuals
    auto residuals = encode_audio_predictor(audio);
    stats.entropy_after = audio_entropy(residuals);

    // 3. Try both codecs
    std::vector<uint8_t> rle_out, huff_out;
    if (!rle_compress_mem(residuals, rle_out))      return false;
    if (!huffman_compress_mem(residuals, huff_out)) return false;

    stats.rle_candidate_size     = rle_out.size();
    stats.huffman_candidate_size = huff_out.size();

    // 4. Pick smaller
    const std::vector<uint8_t>* chosen_data;
    if (rle_out.size() <= huff_out.size()) {
        stats.codec_used  = AUDIO_CODEC_RLE;
        chosen_data       = &rle_out;
    } else {
        stats.codec_used  = AUDIO_CODEC_HUFFMAN;
        chosen_data       = &huff_out;
    }
    stats.compressed_data_size = chosen_data->size();

    // 5. Write .ahmc v2 audio format
    std::ofstream out(ahmc_path, std::ios::binary | std::ios::trunc);
    if (!out) { std::cerr << "Audio: cannot create: " << ahmc_path << "\n"; return false; }

    out.write(AHLA_MAGIC, 4);
    out.put(2);                                         // version 2
    a_wr16(out, audio.format);
    a_wr16(out, audio.channels);
    a_wr32(out, audio.sample_rate);
    a_wr16(out, audio.bits_per_sample);
    a_wr32(out, static_cast<uint32_t>(audio.total_samples));
    out.put(static_cast<char>(AUDIO_PREDICTOR_DELTA));
    out.put(static_cast<char>(stats.codec_used));       // NEW: codec field
    a_wr32(out, static_cast<uint32_t>(audio.pcm_bytes()));
    a_wr32(out, static_cast<uint32_t>(chosen_data->size()));
    out.write(reinterpret_cast<const char*>(chosen_data->data()),
              static_cast<std::streamsize>(chosen_data->size()));
    out.close();

    {
        std::ifstream chk(ahmc_path, std::ios::binary | std::ios::ate);
        if (chk) stats.compressed_file_size = static_cast<uint64_t>(chk.tellg());
    }
    return true;
}

// ---------------------------------------------------------------
// decompress_audio — handles v1 (RLE) and v2 (adaptive)
// ---------------------------------------------------------------
bool decompress_audio(const std::string& ahmc_path, const std::string& wav_path)
{
    std::ifstream in(ahmc_path, std::ios::binary);
    if (!in) { std::cerr << "Audio: cannot open: " << ahmc_path << "\n"; return false; }

    char magic[4] = {};
    in.read(magic, 4);
    if (std::memcmp(magic, AHLA_MAGIC, 4) != 0) {
        std::cerr << "Audio: invalid magic (expected AHLA).\n"; return false;
    }

    uint8_t version = 0;
    in.read(reinterpret_cast<char*>(&version), 1);
    if (version == AHLA_VERSION_EABPS) {
        in.close();
        return decompress_audio_eabps(ahmc_path, wav_path);
    }
    if (version < 1 || version > 2) {
        std::cerr << "Audio: unsupported version " << (int)version << ".\n"; return false;
    }

    AudioBuffer audio;
    audio.format          = a_rd16(in);
    audio.channels        = a_rd16(in);
    audio.sample_rate     = a_rd32(in);
    audio.bits_per_sample = a_rd16(in);
    audio.total_samples   = a_rd32(in);

    uint8_t pred = 0;
    in.read(reinterpret_cast<char*>(&pred), 1);

    // Codec field only in v2. v1 always used RLE.
    uint8_t codec = AUDIO_CODEC_RLE;
    if (version >= 2) {
        in.read(reinterpret_cast<char*>(&codec), 1);
    }

    uint32_t orig_pcm  = a_rd32(in);
    uint32_t comp_size = a_rd32(in);

    std::vector<uint8_t> compressed(comp_size);
    in.read(reinterpret_cast<char*>(compressed.data()),
            static_cast<std::streamsize>(comp_size));

    std::vector<uint8_t> residuals;
    if (codec == AUDIO_CODEC_RLE) {
        if (!rle_decompress_mem(compressed, residuals)) return false;
    } else if (codec == AUDIO_CODEC_HUFFMAN) {
        if (!huffman_decompress_mem(compressed, residuals)) return false;
    } else {
        std::cerr << "Audio: unknown codec id " << (int)codec << ".\n"; return false;
    }

    if (residuals.size() != orig_pcm) {
        std::cerr << "Audio: decompressed size mismatch.\n"; return false;
    }

    if (!decode_audio_predictor(residuals, audio)) return false;
    return write_wav(wav_path, audio);
}

// ---------------------------------------------------------------
// print_audio_compress_stats
// ---------------------------------------------------------------
void print_audio_compress_stats(const std::string& wav_path,
                                const std::string& ahmc_path,
                                const AudioCompressStats& stats)
{
    double ratio = (stats.compressed_file_size > 0)
        ? static_cast<double>(stats.original_file_size) / stats.compressed_file_size : 0.0;
    double saved = (stats.original_file_size > 0)
        ? (1.0 - static_cast<double>(stats.compressed_file_size) /
                  static_cast<double>(stats.original_file_size)) * 100.0 : 0.0;

    const char* codec_name = (stats.codec_used == AUDIO_CODEC_HUFFMAN) ? "HUFFMAN" : "RLE";

    std::cout << std::fixed;
    std::cout << "--------------------------------------------\n";
    std::cout << " AHLC Adaptive Audio Compression\n";
    std::cout << "--------------------------------------------\n";
    std::cout << " Input            : " << wav_path  << "\n";
    std::cout << " Output           : " << ahmc_path << "\n";
    std::cout << " Original size    : " << stats.original_file_size << " bytes\n";
    std::cout << "\n";
    std::cout << " Predictor        : Delta (previous sample, per channel)\n";
    std::cout << std::setprecision(6);
    std::cout << " Residual entropy : " << stats.entropy_after << " bits/byte\n";
    std::cout << "\n";
    std::cout << " RLE candidate    : " << stats.rle_candidate_size     << " bytes\n";
    std::cout << " Huffman candidate: " << stats.huffman_candidate_size << " bytes\n";
    std::cout << " Selected codec   : " << codec_name << "\n";
    std::cout << "\n";
    std::cout << " Final compressed : " << stats.compressed_file_size << " bytes (incl. header)\n";
    std::cout << std::setprecision(4);
    std::cout << " Compression ratio: " << ratio << "x\n";
    std::cout << std::setprecision(2);
    std::cout << " Space saved      : " << saved << "%\n";
    if (saved < 0.0)
        std::cout << " Note: file is LARGER than original (low statistical redundancy).\n";
    std::cout << "--------------------------------------------\n";
}

} // namespace ahlc
