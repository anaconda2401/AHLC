/*
 * image_predictor.cpp
 *
 * Spatial (left-neighbour) predictor + adaptive entropy coding pipeline.
 *
 * .ahmc image format:
 *   v1 (legacy, RLE-only):
 *     [0-3]   "AHLC"   [4] version=1  [5-8] width  [9-12] height
 *     [13-14] bpp  [15] predictor  [16-19] orig_px  [20-23] comp_size
 *     [24+]   RLE data
 *
 *   v2 (adaptive):
 *     [0-3]   "AHLC"   [4] version=2  [5-8] width  [9-12] height
 *     [13-14] bpp  [15] predictor  [16] codec  [17-20] orig_px  [21-24] comp_size
 *     [25+]   codec data
 */

#include "image_predictor.hpp"
#include "../rle/rle_encoder.hpp"
#include "../rle/rle_decoder.hpp"
#include "../entropy/huffman.hpp"

#include <fstream>
#include <iostream>
#include <iomanip>
#include <array>
#include <cstring>
#include <cmath>

namespace ahlc {

static constexpr char AHMC_MAGIC[4] = {'A','H','L','C'};

// Little-endian helpers
static void wr32(std::ofstream& f, uint32_t v){f.put(v&0xFF);f.put((v>>8)&0xFF);f.put((v>>16)&0xFF);f.put((v>>24)&0xFF);}
static void wr16(std::ofstream& f, uint16_t v){f.put(v&0xFF);f.put((v>>8)&0xFF);}
static uint32_t rd32(std::ifstream& f){uint8_t b[4]={};f.read(reinterpret_cast<char*>(b),4);return b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24);}
static uint16_t rd16(std::ifstream& f){uint8_t b[2]={};f.read(reinterpret_cast<char*>(b),2);return static_cast<uint16_t>(b[0]|(b[1]<<8));}

// ---------------------------------------------------------------
// encode_spatial_predictor
// ---------------------------------------------------------------
std::vector<uint8_t> encode_spatial_predictor(const BMPImage& img)
{
    uint32_t Bpp       = img.bytes_per_pixel();
    uint32_t row_bytes = img.row_bytes();
    std::vector<uint8_t> residuals(img.pixels.size());

    for (uint32_t row = 0; row < img.height; ++row)
        for (uint32_t col = 0; col < img.width; ++col) {
            uint32_t base = row * row_bytes + col * Bpp;
            for (uint32_t ch = 0; ch < Bpp; ++ch) {
                uint8_t cur  = img.pixels[base + ch];
                uint8_t pred = (col == 0) ? 0 : img.pixels[base + ch - Bpp];
                residuals[base + ch] = static_cast<uint8_t>((cur - pred + 256) % 256);
            }
        }
    return residuals;
}

// ---------------------------------------------------------------
// decode_spatial_predictor
// ---------------------------------------------------------------
bool decode_spatial_predictor(const std::vector<uint8_t>& residuals, BMPImage& img)
{
    uint32_t Bpp       = img.bytes_per_pixel();
    uint32_t row_bytes = img.row_bytes();
    if (residuals.size() != img.total_bytes()) {
        std::cerr << "Predictor: residual size mismatch.\n"; return false;
    }
    img.pixels.resize(residuals.size());
    for (uint32_t row = 0; row < img.height; ++row)
        for (uint32_t col = 0; col < img.width; ++col) {
            uint32_t base = row * row_bytes + col * Bpp;
            for (uint32_t ch = 0; ch < Bpp; ++ch) {
                uint8_t res  = residuals[base + ch];
                uint8_t pred = (col == 0) ? 0 : img.pixels[base + ch - Bpp];
                img.pixels[base + ch] = static_cast<uint8_t>((res + pred) % 256);
            }
        }
    return true;
}

// ---------------------------------------------------------------
// print_predictor_analysis
// ---------------------------------------------------------------
void print_predictor_analysis(const BMPImage& img, const std::vector<uint8_t>& residuals)
{
    std::array<uint64_t,256> fo={},fr={};
    for (uint8_t b : img.pixels) fo[b]++;
    for (uint8_t b : residuals)  fr[b]++;
    uint64_t uo=0,ur=0,mc=0; uint8_t mb=0;
    for (int i=0;i<256;++i){if(fo[i])++uo;if(fr[i]){++ur;if(fr[i]>mc){mc=fr[i];mb=static_cast<uint8_t>(i);}}}
    double ho=img_entropy(img.pixels), hr=img_entropy(residuals);
    std::cout<<std::fixed<<std::setprecision(6);
    std::cout<<"--------------------------------------------\n AHLC Spatial Predictor Analysis\n Predictor: left neighbour (per channel)\n--------------------------------------------\n";
    std::cout<<" Original pixel bytes   : "<<img.pixels.size()<<"\n";
    std::cout<<" Residual bytes         : "<<residuals.size()<<"\n";
    std::cout<<" Original entropy       : "<<ho<<" bits/byte\n";
    std::cout<<" Residual entropy       : "<<hr<<" bits/byte\n";
    std::cout<<" Unique original values : "<<uo<<" / 256\n";
    std::cout<<" Unique residual values : "<<ur<<" / 256\n";
    std::cout<<" Most freq residual     : 0x"<<std::hex<<std::uppercase<<std::setw(2)<<std::setfill('0')<<(int)mb<<std::dec<<std::setfill(' ')<<"  ("<<mc<<" times, "<<std::setprecision(2)<<(100.0*mc/residuals.size())<<"%)\n";
    std::cout<<(hr<ho?" Result: predictor REDUCED entropy.\n":" Result: predictor did not reduce entropy.\n");
    std::cout<<"--------------------------------------------\n";
}

// ---------------------------------------------------------------
// compress_image — adaptive: tries RLE and Huffman, picks smaller
// ---------------------------------------------------------------
bool compress_image(const std::string& bmp_path,
                    const std::string& ahmc_path,
                    ImageCompressStats& stats)
{
    stats = {};

    // 1. Read BMP
    BMPImage img;
    if (!read_bmp(bmp_path, img)) return false;
    stats.original_pixel_bytes = img.total_bytes();
    stats.predictor_type       = PREDICTOR_LEFT;
    stats.entropy_before       = img_entropy(img.pixels);
    stats.width                = img.width;
    stats.height               = img.height;
    stats.bpp                  = img.bpp;
    {
        std::ifstream chk(bmp_path, std::ios::binary | std::ios::ate);
        if (chk) stats.original_file_size = static_cast<uint64_t>(chk.tellg());
    }

    // 2. Spatial predictor → residuals
    auto residuals = encode_spatial_predictor(img);
    stats.entropy_after = img_entropy(residuals);

    // 3. Try both codecs
    std::vector<uint8_t> rle_out, huff_out;
    if (!rle_compress_mem(residuals, rle_out))       return false;
    if (!huffman_compress_mem(residuals, huff_out))  return false;

    stats.rle_candidate_size     = rle_out.size();
    stats.huffman_candidate_size = huff_out.size();

    // 4. Adaptive selection: pick smaller
    const std::vector<uint8_t>* chosen_data;
    if (rle_out.size() <= huff_out.size()) {
        stats.codec_used  = CODEC_RLE;
        chosen_data       = &rle_out;
    } else {
        stats.codec_used  = CODEC_HUFFMAN;
        chosen_data       = &huff_out;
    }
    stats.compressed_data_size = chosen_data->size();

    // 5. Write .ahmc v2
    std::ofstream out(ahmc_path, std::ios::binary | std::ios::trunc);
    if (!out) { std::cerr << "Image: cannot create: " << ahmc_path << "\n"; return false; }

    out.write(AHMC_MAGIC, 4);
    out.put(2);                                       // version 2
    wr32(out, img.width);
    wr32(out, img.height);
    wr16(out, img.bpp);
    out.put(static_cast<char>(PREDICTOR_LEFT));
    out.put(static_cast<char>(stats.codec_used));     // NEW: codec field
    wr32(out, static_cast<uint32_t>(img.total_bytes()));
    wr32(out, static_cast<uint32_t>(chosen_data->size()));
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
// decompress_image — handles v1 (RLE) and v2 (adaptive)
// ---------------------------------------------------------------
bool decompress_image(const std::string& ahmc_path, const std::string& bmp_path)
{
    std::ifstream in(ahmc_path, std::ios::binary);
    if (!in) { std::cerr << "Image: cannot open: " << ahmc_path << "\n"; return false; }

    char magic[4] = {};
    in.read(magic, 4);
    if (std::memcmp(magic, AHMC_MAGIC, 4) != 0) {
        std::cerr << "Image: invalid .ahmc magic.\n"; return false;
    }

    uint8_t version = 0;
    in.read(reinterpret_cast<char*>(&version), 1);
    if (version < 1 || version > 2) {
        std::cerr << "Image: unsupported version " << (int)version << ".\n"; return false;
    }

    BMPImage img;
    img.width  = rd32(in);
    img.height = rd32(in);
    img.bpp    = rd16(in);

    uint8_t pred_type = 0;
    in.read(reinterpret_cast<char*>(&pred_type), 1);

    // Codec field only in v2. v1 always used RLE.
    uint8_t codec = CODEC_RLE;
    if (version >= 2) {
        in.read(reinterpret_cast<char*>(&codec), 1);
    }

    uint32_t orig_px  = rd32(in);
    uint32_t comp_sz  = rd32(in);

    std::vector<uint8_t> compressed(comp_sz);
    in.read(reinterpret_cast<char*>(compressed.data()),
            static_cast<std::streamsize>(comp_sz));

    // Decompress residuals using stored codec
    std::vector<uint8_t> residuals;
    if (codec == CODEC_RLE) {
        if (!rle_decompress_mem(compressed, residuals)) return false;
    } else if (codec == CODEC_HUFFMAN) {
        if (!huffman_decompress_mem(compressed, residuals)) return false;
    } else {
        std::cerr << "Image: unknown codec id " << (int)codec << ".\n"; return false;
    }

    if (residuals.size() != orig_px) {
        std::cerr << "Image: decompressed size mismatch.\n"; return false;
    }

    if (!decode_spatial_predictor(residuals, img)) return false;
    return write_bmp(bmp_path, img);
}

// ---------------------------------------------------------------
// print_image_compress_stats
// ---------------------------------------------------------------
void print_image_compress_stats(const std::string& bmp_path,
                                const std::string& ahmc_path,
                                const ImageCompressStats& stats)
{
    double ratio = (stats.compressed_file_size > 0)
        ? static_cast<double>(stats.original_file_size) / stats.compressed_file_size : 0.0;
    double saved = (stats.original_file_size > 0)
        ? (1.0 - static_cast<double>(stats.compressed_file_size) /
                  static_cast<double>(stats.original_file_size)) * 100.0 : 0.0;

    const char* codec_name = (stats.codec_used == CODEC_HUFFMAN) ? "HUFFMAN" : "RLE";

    std::cout << std::fixed;
    std::cout << "--------------------------------------------\n";
    std::cout << " AHLC Adaptive Image Compression\n";
    std::cout << "--------------------------------------------\n";
    std::cout << " Input            : " << bmp_path  << "\n";
    std::cout << " Output           : " << ahmc_path << "\n";
    std::cout << " Original size    : " << stats.original_file_size   << " bytes\n";
    std::cout << "\n";
    std::cout << " Predictor        : Left neighbour (per channel)\n";
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
        std::cout << " Note: file is LARGER than original (low spatial redundancy).\n";
    std::cout << "--------------------------------------------\n";
}

} // namespace ahlc
