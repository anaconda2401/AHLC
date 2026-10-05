#include "audio.hpp"

#include <fstream>
#include <iostream>
#include <iomanip>
#include <array>
#include <cstring>
#include <cmath>

namespace ahlc {

// ---------------------------------------------------------------
// Little-endian I/O helpers
// ---------------------------------------------------------------
static uint16_t wav_rd16(std::ifstream& f) {
    uint8_t b[2] = {}; f.read(reinterpret_cast<char*>(b), 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}
static uint32_t wav_rd32(std::ifstream& f) {
    uint8_t b[4] = {}; f.read(reinterpret_cast<char*>(b), 4);
    return b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24);
}
static void wav_wr16(std::ofstream& f, uint16_t v) {
    f.put(static_cast<char>(v & 0xFF));
    f.put(static_cast<char>((v >> 8) & 0xFF));
}
static void wav_wr32(std::ofstream& f, uint32_t v) {
    f.put(static_cast<char>(v & 0xFF));
    f.put(static_cast<char>((v >> 8) & 0xFF));
    f.put(static_cast<char>((v >> 16) & 0xFF));
    f.put(static_cast<char>((v >> 24) & 0xFF));
}

// ---------------------------------------------------------------
// read_wav — chunk-based parser (handles LIST, INFO, etc.)
// ---------------------------------------------------------------
bool read_wav(const std::string& path, AudioBuffer& audio)
{
    audio = {};
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "WAV: cannot open: " << path << "\n"; return false; }

    // RIFF header
    char riff[4] = {}, wave[4] = {};
    f.read(riff, 4);
    if (std::memcmp(riff, "RIFF", 4) != 0) {
        std::cerr << "WAV: not a RIFF file.\n"; return false;
    }
    wav_rd32(f); // file size (unreliable — skip)
    f.read(wave, 4);
    if (std::memcmp(wave, "WAVE", 4) != 0) {
        std::cerr << "WAV: RIFF type is not WAVE.\n"; return false;
    }

    bool got_fmt = false, got_data = false;

    // Iterate over chunks until we have both fmt and data.
    while (f.good() && !(got_fmt && got_data)) {
        char cid[4] = {};
        if (!f.read(cid, 4)) break;
        uint32_t csz = wav_rd32(f);
        auto chunk_end = static_cast<std::streamoff>(f.tellg()) + csz + (csz & 1); // even-byte pad

        if (std::memcmp(cid, "fmt ", 4) == 0) {
            // ---- fmt chunk ----
            audio.format          = wav_rd16(f);
            audio.channels        = wav_rd16(f);
            audio.sample_rate     = wav_rd32(f);
            /*byte_rate*/           wav_rd32(f);
            /*block_align*/         wav_rd16(f);
            audio.bits_per_sample = wav_rd16(f);

            if (audio.format != 1) {
                std::cerr << "WAV: only PCM (format=1) supported.\n"; return false;
            }
            if (audio.bits_per_sample != 8  && audio.bits_per_sample != 16 &&
                audio.bits_per_sample != 24 && audio.bits_per_sample != 32) {
                std::cerr << "WAV: unsupported bits_per_sample=" << audio.bits_per_sample << "\n";
                return false;
            }
            if (audio.channels == 0) {
                std::cerr << "WAV: zero channels.\n"; return false;
            }
            got_fmt = true;
            f.seekg(chunk_end, std::ios::beg); // skip any extra fmt extension bytes

        } else if (std::memcmp(cid, "data", 4) == 0) {
            // ---- data chunk ----
            if (!got_fmt) {
                std::cerr << "WAV: data chunk before fmt chunk.\n"; return false;
            }
            audio.pcm.resize(csz);
            f.read(reinterpret_cast<char*>(audio.pcm.data()), csz);
            audio.total_samples = csz / audio.block_align();
            got_data = true;
            // (don't need to read further)
            break;

        } else {
            // Unknown/unneeded chunk — skip.
            f.seekg(chunk_end, std::ios::beg);
        }
    }

    if (!got_fmt)  { std::cerr << "WAV: fmt chunk not found.\n";  return false; }
    if (!got_data) { std::cerr << "WAV: data chunk not found.\n"; return false; }
    return true;
}

// ---------------------------------------------------------------
// write_wav — writes a minimal valid PCM WAV
// ---------------------------------------------------------------
bool write_wav(const std::string& path, const AudioBuffer& audio)
{
    if (audio.channels == 0 || audio.sample_rate == 0 || audio.bits_per_sample == 0) {
        std::cerr << "WAV: invalid AudioBuffer.\n"; return false;
    }

    uint32_t pcm_bytes   = static_cast<uint32_t>(audio.pcm_bytes());
    uint32_t data_pad    = pcm_bytes & 1u; // 0 or 1
    // file_size = 4 (WAVE) + 8+16 (fmt chunk) + 8+pcm_bytes (data chunk) + pad
    uint32_t file_size   = 4u + 24u + 8u + pcm_bytes + data_pad;
    uint32_t byte_rate   = audio.sample_rate * audio.block_align();

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { std::cerr << "WAV: cannot create: " << path << "\n"; return false; }

    // RIFF header
    f.write("RIFF", 4);
    wav_wr32(f, file_size);
    f.write("WAVE", 4);

    // fmt chunk (16 bytes for PCM)
    f.write("fmt ", 4);
    wav_wr32(f, 16);
    wav_wr16(f, 1);                          // PCM
    wav_wr16(f, audio.channels);
    wav_wr32(f, audio.sample_rate);
    wav_wr32(f, byte_rate);
    wav_wr16(f, static_cast<uint16_t>(audio.block_align()));
    wav_wr16(f, audio.bits_per_sample);

    // data chunk
    f.write("data", 4);
    wav_wr32(f, pcm_bytes);
    f.write(reinterpret_cast<const char*>(audio.pcm.data()),
            static_cast<std::streamsize>(pcm_bytes));
    if (data_pad) f.put('\0'); // WAV spec: pad to even byte boundary

    return true;
}

// ---------------------------------------------------------------
// audio_entropy
// ---------------------------------------------------------------
double audio_entropy(const std::vector<uint8_t>& data)
{
    if (data.empty()) return 0.0;
    std::array<uint64_t, 256> freq = {};
    for (uint8_t b : data) freq[b]++;
    double h = 0.0, n = static_cast<double>(data.size());
    for (auto c : freq) {
        if (!c) continue;
        double p = static_cast<double>(c) / n;
        h -= p * std::log2(p);
    }
    return h;
}

// ---------------------------------------------------------------
// print_audio_analysis
// ---------------------------------------------------------------
void print_audio_analysis(const std::string& filename, const AudioBuffer& audio)
{
    double entropy  = audio_entropy(audio.pcm);
    double duration = audio.duration_sec();

    std::cout << std::fixed;
    std::cout << "--------------------------------------------\n";
    std::cout << " AHLC Audio Analysis\n";
    std::cout << "--------------------------------------------\n";
    std::cout << " File            : " << filename            << "\n";
    std::cout << " Format          : PCM\n";
    std::cout << " Channels        : " << audio.channels      << "\n";
    std::cout << " Sample rate     : " << audio.sample_rate   << " Hz\n";
    std::cout << " Bits per sample : " << audio.bits_per_sample << "\n";
    std::cout << " Total samples   : " << audio.total_samples  << " (per channel)\n";
    std::cout << " PCM data bytes  : " << audio.pcm_bytes()   << "\n";
    std::cout << std::setprecision(3);
    std::cout << " Duration        : " << duration << " s\n";
    std::cout << std::setprecision(6);
    std::cout << " Entropy         : " << entropy  << " bits/byte\n";
    std::cout << "--------------------------------------------\n";
}

// ---------------------------------------------------------------
// audios_equal
// ---------------------------------------------------------------
bool audios_equal(const AudioBuffer& a, const AudioBuffer& b)
{
    return a.channels        == b.channels        &&
           a.sample_rate     == b.sample_rate     &&
           a.bits_per_sample == b.bits_per_sample &&
           a.total_samples   == b.total_samples   &&
           a.pcm             == b.pcm;
}

} // namespace ahlc
