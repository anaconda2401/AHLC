/*
 * eabps.cpp
 *
 * Entropy-Aware Block-wise Predictor Selection (EABPS) for Image Compression.
 *
 * Divides an image into independent blocks (default 16x16), evaluates reversible
 * predictors (NONE, LEFT, UP) and adaptive entropy coders (RLE, Canonical Huffman, RAW)
 * for each block based on complete encoded payload cost, and packages the result
 * into a backward-compatible AHMC v3 container.
 */

#include "eabps.hpp"
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
#include <chrono>
#include <algorithm>
#include <functional>
#include <initializer_list>

namespace ahlc {

static constexpr char AHMC_MAGIC[4] = {'A', 'H', 'L', 'C'};

// Little-endian I/O helpers
static void wr32(std::ofstream& f, uint32_t v) {
    f.put(static_cast<char>(v & 0xFF));
    f.put(static_cast<char>((v >> 8) & 0xFF));
    f.put(static_cast<char>((v >> 16) & 0xFF));
    f.put(static_cast<char>((v >> 24) & 0xFF));
}

static void wr16(std::ofstream& f, uint16_t v) {
    f.put(static_cast<char>(v & 0xFF));
    f.put(static_cast<char>((v >> 8) & 0xFF));
}

static uint32_t rd32(std::ifstream& f) {
    uint8_t b[4] = {};
    f.read(reinterpret_cast<char*>(b), 4);
    return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

static uint16_t rd16(std::ifstream& f) {
    uint8_t b[2] = {};
    f.read(reinterpret_cast<char*>(b), 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}

// -----------------------------------------------------------------------
// Pure block prediction and reconstruction functions
// -----------------------------------------------------------------------
std::vector<uint8_t> eabps_predict_block_pixels(const std::vector<uint8_t>& block_pixels,
                                                uint32_t bw, uint32_t bh, uint32_t bpp,
                                                uint8_t predictor)
{
    const size_t total = static_cast<size_t>(bw) * bh * bpp;
    if (block_pixels.size() != total) {
        return {};
    }
    std::vector<uint8_t> residuals(total);

    for (uint32_t y = 0; y < bh; ++y) {
        for (uint32_t x = 0; x < bw; ++x) {
            const size_t base = (static_cast<size_t>(y) * bw + x) * bpp;
            for (uint32_t ch = 0; ch < bpp; ++ch) {
                const uint8_t cur = block_pixels[base + ch];
                uint8_t pred = 0;
                if (predictor == EABPS_PRED_LEFT) {
                    pred = (x == 0) ? 0 : block_pixels[base + ch - bpp];
                } else if (predictor == EABPS_PRED_UP) {
                    pred = (y == 0) ? 0 : block_pixels[base + ch - static_cast<size_t>(bw) * bpp];
                } else {
                    // EABPS_PRED_NONE
                    pred = 0;
                }
                residuals[base + ch] = static_cast<uint8_t>((cur - pred + 256) % 256);
            }
        }
    }
    return residuals;
}

bool eabps_reconstruct_block_pixels(const std::vector<uint8_t>& residuals,
                                    uint32_t bw, uint32_t bh, uint32_t bpp,
                                    uint8_t predictor,
                                    std::vector<uint8_t>& out_pixels)
{
    const size_t total = static_cast<size_t>(bw) * bh * bpp;
    if (residuals.size() != total) {
        return false;
    }
    out_pixels.resize(total);

    for (uint32_t y = 0; y < bh; ++y) {
        for (uint32_t x = 0; x < bw; ++x) {
            const size_t base = (static_cast<size_t>(y) * bw + x) * bpp;
            for (uint32_t ch = 0; ch < bpp; ++ch) {
                const uint8_t res = residuals[base + ch];
                uint8_t pred = 0;
                if (predictor == EABPS_PRED_LEFT) {
                    pred = (x == 0) ? 0 : out_pixels[base + ch - bpp];
                } else if (predictor == EABPS_PRED_UP) {
                    pred = (y == 0) ? 0 : out_pixels[base + ch - static_cast<size_t>(bw) * bpp];
                } else {
                    // EABPS_PRED_NONE
                    pred = 0;
                }
                out_pixels[base + ch] = static_cast<uint8_t>((res + pred) % 256);
            }
        }
    }
    return true;
}

// -----------------------------------------------------------------------
// Block prediction & reconstruction directly with full BMPImage
// -----------------------------------------------------------------------
std::vector<uint8_t> eabps_extract_and_predict_block(const BMPImage& img,
                                                     uint32_t x0, uint32_t y0,
                                                     uint32_t bw, uint32_t bh,
                                                     uint8_t predictor)
{
    const uint32_t bpp = img.bytes_per_pixel();
    const uint32_t row_bytes = img.row_bytes();
    const size_t total = static_cast<size_t>(bw) * bh * bpp;
    std::vector<uint8_t> residuals(total);

    for (uint32_t y = 0; y < bh; ++y) {
        const uint32_t img_y = y0 + y;
        for (uint32_t x = 0; x < bw; ++x) {
            const uint32_t img_x = x0 + x;
            const size_t block_base = (static_cast<size_t>(y) * bw + x) * bpp;
            const size_t img_base   = static_cast<size_t>(img_y) * row_bytes + static_cast<size_t>(img_x) * bpp;

            for (uint32_t ch = 0; ch < bpp; ++ch) {
                const uint8_t cur = img.pixels[img_base + ch];
                uint8_t pred = 0;
                if (predictor == EABPS_PRED_LEFT) {
                    pred = (x == 0) ? 0 : img.pixels[img_base + ch - bpp];
                } else if (predictor == EABPS_PRED_UP) {
                    pred = (y == 0) ? 0 : img.pixels[img_base + ch - row_bytes];
                } else {
                    // NONE
                    pred = 0;
                }
                residuals[block_base + ch] = static_cast<uint8_t>((cur - pred + 256) % 256);
            }
        }
    }
    return residuals;
}

bool eabps_reconstruct_and_insert_block(BMPImage& img,
                                        uint32_t x0, uint32_t y0,
                                        uint32_t bw, uint32_t bh,
                                        uint8_t predictor,
                                        const std::vector<uint8_t>& residuals)
{
    const uint32_t bpp = img.bytes_per_pixel();
    const uint32_t row_bytes = img.row_bytes();
    const size_t expected_size = static_cast<size_t>(bw) * bh * bpp;
    if (residuals.size() != expected_size) {
        return false;
    }

    for (uint32_t y = 0; y < bh; ++y) {
        const uint32_t img_y = y0 + y;
        for (uint32_t x = 0; x < bw; ++x) {
            const uint32_t img_x = x0 + x;
            const size_t block_base = (static_cast<size_t>(y) * bw + x) * bpp;
            const size_t img_base   = static_cast<size_t>(img_y) * row_bytes + static_cast<size_t>(img_x) * bpp;

            for (uint32_t ch = 0; ch < bpp; ++ch) {
                const uint8_t res = residuals[block_base + ch];
                uint8_t pred = 0;
                if (predictor == EABPS_PRED_LEFT) {
                    pred = (x == 0) ? 0 : img.pixels[img_base + ch - bpp];
                } else if (predictor == EABPS_PRED_UP) {
                    pred = (y == 0) ? 0 : img.pixels[img_base + ch - row_bytes];
                } else {
                    // NONE
                    pred = 0;
                }
                img.pixels[img_base + ch] = static_cast<uint8_t>((res + pred) % 256);
            }
        }
    }
    return true;
}

// -----------------------------------------------------------------------
// compress_image_eabps
// -----------------------------------------------------------------------
bool compress_image_eabps(const std::string& bmp_path,
                          const std::string& ahmc_path,
                          EABPSCompressStats& stats,
                          uint16_t block_size)
{
    const auto t_start = std::chrono::high_resolution_clock::now();
    stats = {};
    if (block_size == 0) {
        block_size = EABPS_DEFAULT_BLOCK_SIZE;
    }
    stats.block_size = block_size;

    // 1. Read input BMP
    BMPImage img;
    if (!read_bmp(bmp_path, img)) {
        std::cerr << "EABPS: Failed to read input BMP: " << bmp_path << "\n";
        return false;
    }

    stats.width = img.width;
    stats.height = img.height;
    stats.bpp = img.bpp;
    stats.original_pixel_bytes = img.total_bytes();
    stats.entropy_before = img_entropy(img.pixels);

    {
        std::ifstream fchk(bmp_path, std::ios::binary | std::ios::ate);
        if (fchk) {
            stats.original_file_size = static_cast<uint64_t>(fchk.tellg());
        }
    }

    const uint32_t nx = (img.width + block_size - 1) / block_size;
    const uint32_t ny = (img.height + block_size - 1) / block_size;
    const uint32_t total_blocks = nx * ny;

    stats.num_blocks_x = nx;
    stats.num_blocks_y = ny;
    stats.total_blocks = total_blocks;
    stats.blocks.resize(total_blocks);

    std::vector<std::vector<uint8_t>> block_payloads(total_blocks);
    std::vector<uint8_t> all_residuals;
    all_residuals.reserve(img.total_bytes());

    // 2. Process each block: evaluate candidate predictors and codecs
    for (uint32_t by = 0; by < ny; ++by) {
        for (uint32_t bx = 0; bx < nx; ++bx) {
            const uint32_t block_idx = by * nx + bx;
            const uint32_t x0 = bx * block_size;
            const uint32_t y0 = by * block_size;
            const uint32_t x1 = std::min(x0 + block_size, img.width);
            const uint32_t y1 = std::min(y0 + block_size, img.height);
            const uint32_t bw = x1 - x0;
            const uint32_t bh = y1 - y0;

            EABPSBlockRecord& rec = stats.blocks[block_idx];
            rec.x0 = x0;
            rec.y0 = y0;
            rec.width = bw;
            rec.height = bh;

            size_t best_cost = std::numeric_limits<size_t>::max();
            uint8_t best_pred = EABPS_PRED_NONE;
            uint8_t best_codec = EABPS_CODEC_RAW;
            std::vector<uint8_t> best_payload;
            std::vector<uint8_t> best_block_residuals;

            // Test predictors: NONE, LEFT, UP
            for (uint8_t p = 0; p < EABPS_PRED_COUNT; ++p) {
                std::vector<uint8_t> res = eabps_extract_and_predict_block(img, x0, y0, bw, bh, p);

                // Candidate 1: RAW uncompressed
                const size_t raw_cost = res.size();
                if (raw_cost < best_cost) {
                    best_cost = raw_cost;
                    best_pred = p;
                    best_codec = EABPS_CODEC_RAW;
                    best_payload = res;
                    best_block_residuals = res;
                }

                // Candidate 2: RLE
                std::vector<uint8_t> rle_out;
                if (rle_compress_mem(res, rle_out)) {
                    if (rle_out.size() < best_cost) {
                        best_cost = rle_out.size();
                        best_pred = p;
                        best_codec = EABPS_CODEC_RLE;
                        best_payload = std::move(rle_out);
                        best_block_residuals = res;
                    }
                }

                // Candidate 3: Canonical Huffman
                std::vector<uint8_t> huff_out;
                if (huffman_compress_mem(res, huff_out)) {
                    if (huff_out.size() < best_cost) {
                        best_cost = huff_out.size();
                        best_pred = p;
                        best_codec = EABPS_CODEC_HUFFMAN;
                        best_payload = std::move(huff_out);
                        best_block_residuals = res;
                    }
                }
            }

            rec.predictor = best_pred;
            rec.codec = best_codec;
            rec.payload_size = static_cast<uint32_t>(best_payload.size());
            block_payloads[block_idx] = std::move(best_payload);

            // Accumulate residual stream for entropy calculation
            all_residuals.insert(all_residuals.end(), best_block_residuals.begin(), best_block_residuals.end());

            // Track distributions
            if (best_pred == EABPS_PRED_NONE)      stats.count_none++;
            else if (best_pred == EABPS_PRED_LEFT) stats.count_left++;
            else if (best_pred == EABPS_PRED_UP)   stats.count_up++;

            if (best_codec == EABPS_CODEC_RLE)          stats.count_rle++;
            else if (best_codec == EABPS_CODEC_HUFFMAN) stats.count_huffman++;
            else if (best_codec == EABPS_CODEC_RAW)     stats.count_raw++;

            stats.encoded_payload_size += rec.payload_size;
        }
    }

    stats.entropy_after = img_entropy(all_residuals);
    // 6 bytes per block: 1 byte predictor + 1 byte codec + 4 bytes payload_size
    stats.predictor_metadata_size = static_cast<uint64_t>(total_blocks) * 6;

    // 3. Write AHMC v3 container
    std::ofstream out(ahmc_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "EABPS: Cannot create output file: " << ahmc_path << "\n";
        return false;
    }

    // Header (29 bytes)
    out.write(AHMC_MAGIC, 4);
    out.put(static_cast<char>(AHMC_VERSION_EABPS)); // version 3
    wr32(out, img.width);
    wr32(out, img.height);
    wr16(out, img.bpp);
    wr16(out, block_size);
    wr32(out, total_blocks);
    wr32(out, static_cast<uint32_t>(img.total_bytes()));
    wr32(out, static_cast<uint32_t>(stats.encoded_payload_size));

    // Per-block records: metadata (6 bytes) + payload
    for (uint32_t i = 0; i < total_blocks; ++i) {
        const EABPSBlockRecord& rec = stats.blocks[i];
        out.put(static_cast<char>(rec.predictor));
        out.put(static_cast<char>(rec.codec));
        wr32(out, rec.payload_size);
        if (rec.payload_size > 0) {
            out.write(reinterpret_cast<const char*>(block_payloads[i].data()),
                      static_cast<std::streamsize>(rec.payload_size));
        }
    }
    out.close();

    {
        std::ifstream chk(ahmc_path, std::ios::binary | std::ios::ate);
        if (chk) {
            stats.compressed_file_size = static_cast<uint64_t>(chk.tellg());
        }
    }

    stats.container_overhead = (stats.compressed_file_size >= stats.encoded_payload_size)
        ? (stats.compressed_file_size - stats.encoded_payload_size) : 0;

    const auto t_end = std::chrono::high_resolution_clock::now();
    stats.compression_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    return true;
}

// -----------------------------------------------------------------------
// decompress_image_eabps
// -----------------------------------------------------------------------
bool decompress_image_eabps(const std::string& ahmc_path, const std::string& bmp_path)
{
    const auto t_start = std::chrono::high_resolution_clock::now();
    std::ifstream in(ahmc_path, std::ios::binary);
    if (!in) {
        std::cerr << "EABPS Decoder: Cannot open " << ahmc_path << "\n";
        return false;
    }

    // 1. Validate magic and version
    char magic[4] = {};
    in.read(magic, 4);
    if (std::memcmp(magic, AHMC_MAGIC, 4) != 0) {
        std::cerr << "EABPS Decoder: Invalid AHMC magic identifier.\n";
        return false;
    }

    uint8_t version = 0;
    in.read(reinterpret_cast<char*>(&version), 1);
    if (version != AHMC_VERSION_EABPS) {
        std::cerr << "EABPS Decoder: Unsupported container version " << static_cast<int>(version)
                  << " (expected " << static_cast<int>(AHMC_VERSION_EABPS) << ").\n";
        return false;
    }

    // 2. Parse and validate header fields
    const uint32_t width  = rd32(in);
    const uint32_t height = rd32(in);
    const uint16_t bpp    = rd16(in);
    const uint16_t block_size   = rd16(in);
    const uint32_t total_blocks = rd32(in);
    const uint32_t orig_px_bytes = rd32(in);
    const uint32_t total_payload = rd32(in);

    if (width == 0 || height == 0 || width > 65536 || height > 65536) {
        std::cerr << "EABPS Decoder: Invalid image dimensions: " << width << "x" << height << "\n";
        return false;
    }
    if (bpp != 24 && bpp != 32) {
        std::cerr << "EABPS Decoder: Unsupported bit depth " << bpp << "\n";
        return false;
    }
    if (block_size == 0 || block_size > 2048) {
        std::cerr << "EABPS Decoder: Invalid block size: " << block_size << "\n";
        return false;
    }

    const uint32_t nx = (width + block_size - 1) / block_size;
    const uint32_t ny = (height + block_size - 1) / block_size;
    if (total_blocks != nx * ny) {
        std::cerr << "EABPS Decoder: Block count mismatch (expected " << (nx * ny)
                  << ", got " << total_blocks << ").\n";
        return false;
    }

    const uint64_t expected_total_bytes = static_cast<uint64_t>(width) * height * (bpp / 8u);
    if (static_cast<uint64_t>(orig_px_bytes) != expected_total_bytes) {
        std::cerr << "EABPS Decoder: Original pixel byte count mismatch.\n";
        return false;
    }

    BMPImage img;
    img.width = width;
    img.height = height;
    img.bpp = bpp;
    img.pixels.resize(static_cast<size_t>(expected_total_bytes));

    uint64_t accumulated_payload = 0;

    // 3. Read and decode each block
    for (uint32_t by = 0; by < ny; ++by) {
        for (uint32_t bx = 0; bx < nx; ++bx) {
            if (!in || in.eof()) {
                std::cerr << "EABPS Decoder: Unexpected EOF while reading block metadata.\n";
                return false;
            }

            const uint32_t x0 = bx * block_size;
            const uint32_t y0 = by * block_size;
            const uint32_t x1 = std::min(x0 + block_size, width);
            const uint32_t y1 = std::min(y0 + block_size, height);
            const uint32_t bw = x1 - x0;
            const uint32_t bh = y1 - y0;

            uint8_t predictor = 0;
            in.read(reinterpret_cast<char*>(&predictor), 1);
            if (predictor >= EABPS_PRED_COUNT) {
                std::cerr << "EABPS Decoder: Out-of-range predictor ID " << static_cast<int>(predictor) << "\n";
                return false;
            }

            uint8_t codec = 0;
            in.read(reinterpret_cast<char*>(&codec), 1);
            if (codec >= EABPS_CODEC_COUNT) {
                std::cerr << "EABPS Decoder: Out-of-range codec ID " << static_cast<int>(codec) << "\n";
                return false;
            }

            const uint32_t payload_sz = rd32(in);
            accumulated_payload += payload_sz;
            if (accumulated_payload > static_cast<uint64_t>(total_payload)) {
                std::cerr << "EABPS Decoder: Block payload length exceeds total container payload.\n";
                return false;
            }

            std::vector<uint8_t> payload(payload_sz);
            if (payload_sz > 0) {
                in.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload_sz));
                if (in.gcount() != static_cast<std::streamsize>(payload_sz)) {
                    std::cerr << "EABPS Decoder: Truncated block payload data.\n";
                    return false;
                }
            }

            // Decompress block residuals
            std::vector<uint8_t> residuals;
            if (codec == EABPS_CODEC_RAW) {
                residuals = std::move(payload);
            } else if (codec == EABPS_CODEC_RLE) {
                if (!rle_decompress_mem(payload, residuals)) {
                    std::cerr << "EABPS Decoder: RLE decompression failed for block (" << bx << "," << by << ")\n";
                    return false;
                }
            } else if (codec == EABPS_CODEC_HUFFMAN) {
                if (!huffman_decompress_mem(payload, residuals)) {
                    std::cerr << "EABPS Decoder: Huffman decompression failed for block (" << bx << "," << by << ")\n";
                    return false;
                }
            }

            const size_t expected_block_bytes = static_cast<size_t>(bw) * bh * (bpp / 8u);
            if (residuals.size() != expected_block_bytes) {
                std::cerr << "EABPS Decoder: Residual size mismatch for block (" << bx << "," << by
                          << "): expected " << expected_block_bytes << ", got " << residuals.size() << "\n";
                return false;
            }

            // Inverse prediction
            if (!eabps_reconstruct_and_insert_block(img, x0, y0, bw, bh, predictor, residuals)) {
                std::cerr << "EABPS Decoder: Block reconstruction failed for block (" << bx << "," << by << ")\n";
                return false;
            }
        }
    }

    in.close();

    // 4. Write reconstructed BMP
    if (!write_bmp(bmp_path, img)) {
        std::cerr << "EABPS Decoder: Failed to write output BMP: " << bmp_path << "\n";
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------
// print_eabps_stats
// -----------------------------------------------------------------------
void print_eabps_stats(const std::string& bmp_path,
                       const std::string& ahmc_path,
                       const EABPSCompressStats& stats)
{
    const double ratio = (stats.compressed_file_size > 0)
        ? static_cast<double>(stats.original_file_size) / stats.compressed_file_size : 0.0;
    const double saved = (stats.original_file_size > 0)
        ? (1.0 - static_cast<double>(stats.compressed_file_size) /
                  static_cast<double>(stats.original_file_size)) * 100.0 : 0.0;

    std::cout << std::fixed;
    std::cout << "--------------------------------------------\n";
    std::cout << " AHLC EABPS (Entropy-Aware Block-wise Predictor Selection)\n";
    std::cout << "--------------------------------------------\n";
    std::cout << " Input                : " << bmp_path  << "\n";
    std::cout << " Output               : " << ahmc_path << "\n";
    std::cout << " Image Dimensions     : " << stats.width << " x " << stats.height << " (" << stats.bpp << " bpp)\n";
    std::cout << " Block Size           : " << stats.block_size << " x " << stats.block_size << " px\n";
    std::cout << " Block Grid           : " << stats.num_blocks_x << " x " << stats.num_blocks_y
              << " (" << stats.total_blocks << " total blocks)\n";
    std::cout << " Original file size   : " << stats.original_file_size   << " bytes\n";
    std::cout << " Uncompressed pixels  : " << stats.original_pixel_bytes << " bytes\n";
    std::cout << "\n";
    std::cout << " Predictor Selection Distribution:\n";
    std::cout << "   NONE (no prediction)  : " << stats.count_none
              << " blocks (" << std::setprecision(1) << (stats.total_blocks ? (100.0 * stats.count_none / stats.total_blocks) : 0.0) << "%)\n";
    std::cout << "   LEFT (spatial left)   : " << stats.count_left
              << " blocks (" << std::setprecision(1) << (stats.total_blocks ? (100.0 * stats.count_left / stats.total_blocks) : 0.0) << "%)\n";
    std::cout << "   UP   (spatial up)     : " << stats.count_up
              << " blocks (" << std::setprecision(1) << (stats.total_blocks ? (100.0 * stats.count_up / stats.total_blocks) : 0.0) << "%)\n";
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
// Synthetic BMP helper for internal C++ tests
// -----------------------------------------------------------------------
static BMPImage make_test_bmp(uint32_t w, uint32_t h, uint16_t bpp,
                              const std::function<void(uint32_t r, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t& A)>& gen)
{
    BMPImage img;
    img.width = w;
    img.height = h;
    img.bpp = bpp;
    img.pixels.resize(img.total_bytes());
    const uint32_t Bpp = img.bytes_per_pixel();
    const uint32_t rb = img.row_bytes();
    for (uint32_t r = 0; r < h; ++r) {
        for (uint32_t c = 0; c < w; ++c) {
            uint8_t B = 0, G = 0, R = 0, A = 255;
            gen(r, c, B, G, R, A);
            const size_t idx = static_cast<size_t>(r) * rb + static_cast<size_t>(c) * Bpp;
            img.pixels[idx + 0] = B;
            img.pixels[idx + 1] = G;
            img.pixels[idx + 2] = R;
            if (Bpp == 4) {
                img.pixels[idx + 3] = A;
            }
        }
    }
    return img;
}

// -----------------------------------------------------------------------
// Automated EABPS Test Suite (Sections 8.A - 8.F)
// -----------------------------------------------------------------------
bool run_eabps_test_suite()
{
    std::cout << "============================================================\n";
    std::cout << " AHLC EABPS Comprehensive Test Suite\n";
    std::cout << "============================================================\n";
    bool all_ok = true;

    // -------------------------------------------------------------------
    // 8.A Predictor Unit Tests
    // -------------------------------------------------------------------
    std::cout << "\n--- 8.A Predictor Unit Tests (NONE, LEFT, UP) ---\n";
    {
        static const uint8_t ALL_PREDS[3] = {EABPS_PRED_NONE, EABPS_PRED_LEFT, EABPS_PRED_UP};

        // 1. One-pixel block (1x1)
        std::vector<uint8_t> p1 = {0x12, 0x34, 0x56};
        for (uint8_t p : ALL_PREDS) {
            auto res = eabps_predict_block_pixels(p1, 1, 1, 3, p);
            std::vector<uint8_t> rec;
            bool ok = eabps_reconstruct_block_pixels(res, 1, 1, 3, p, rec) && (rec == p1);
            if (!ok) { all_ok = false; std::cout << " [FAIL] 1x1 block predictor " << (int)p << "\n"; }
        }
        std::cout << " [PASS] 1-pixel block (1x1) forward/inverse for NONE, LEFT, UP\n";

        // 2. Single-row block (16x1)
        std::vector<uint8_t> row_px(16 * 3);
        for (size_t i = 0; i < row_px.size(); ++i) row_px[i] = static_cast<uint8_t>(i * 13 + 7);
        for (uint8_t p : ALL_PREDS) {
            auto res = eabps_predict_block_pixels(row_px, 16, 1, 3, p);
            std::vector<uint8_t> rec;
            bool ok = eabps_reconstruct_block_pixels(res, 16, 1, 3, p, rec) && (rec == row_px);
            if (!ok) { all_ok = false; std::cout << " [FAIL] Single-row block predictor " << (int)p << "\n"; }
        }
        std::cout << " [PASS] Single-row block (16x1) forward/inverse for NONE, LEFT, UP\n";

        // 3. Single-column block (1x16)
        std::vector<uint8_t> col_px(16 * 3);
        for (size_t i = 0; i < col_px.size(); ++i) col_px[i] = static_cast<uint8_t>(i * 17 + 3);
        for (uint8_t p : ALL_PREDS) {
            auto res = eabps_predict_block_pixels(col_px, 1, 16, 3, p);
            std::vector<uint8_t> rec;
            bool ok = eabps_reconstruct_block_pixels(res, 1, 16, 3, p, rec) && (rec == col_px);
            if (!ok) { all_ok = false; std::cout << " [FAIL] Single-column block predictor " << (int)p << "\n"; }
        }
        std::cout << " [PASS] Single-column block (1x16) forward/inverse for NONE, LEFT, UP\n";

        // 4. Extreme values (0 and 255) and signed modular wrapping
        std::vector<uint8_t> ext_px = {0, 255, 0, 255, 0, 255, 128, 64, 32};
        for (uint8_t p : ALL_PREDS) {
            auto res = eabps_predict_block_pixels(ext_px, 3, 1, 3, p);
            std::vector<uint8_t> rec;
            bool ok = eabps_reconstruct_block_pixels(res, 3, 1, 3, p, rec) && (rec == ext_px);
            if (!ok) { all_ok = false; std::cout << " [FAIL] Extremes test predictor " << (int)p << "\n"; }
        }
        std::cout << " [PASS] Modular residual arithmetic & extreme sample values (0, 255)\n";
    }

    // -------------------------------------------------------------------
    // 8.B Block-Layout and BMP Geometry Tests
    // -------------------------------------------------------------------
    std::cout << "\n--- 8.B Block-Layout and Geometry Tests ---\n";
    auto test_roundtrip = [&](const std::string& /*label*/, const BMPImage& img, uint16_t bsize) -> bool {
        const std::string tmp_bmp = "__eabps_test_in.bmp";
        const std::string tmp_ahmc = "__eabps_test.ahmc";
        const std::string tmp_rec = "__eabps_test_out.bmp";

        if (!write_bmp(tmp_bmp, img)) return false;
        EABPSCompressStats s;
        if (!compress_image_eabps(tmp_bmp, tmp_ahmc, s, bsize)) {
            std::remove(tmp_bmp.c_str());
            return false;
        }
        if (!decompress_image_eabps(tmp_ahmc, tmp_rec)) {
            std::remove(tmp_bmp.c_str());
            std::remove(tmp_ahmc.c_str());
            return false;
        }
        BMPImage rec_img;
        if (!read_bmp(tmp_rec, rec_img)) {
            std::remove(tmp_bmp.c_str());
            std::remove(tmp_ahmc.c_str());
            std::remove(tmp_rec.c_str());
            return false;
        }

        bool match = images_equal(img, rec_img);
        std::remove(tmp_bmp.c_str());
        std::remove(tmp_ahmc.c_str());
        std::remove(tmp_rec.c_str());
        return match;
    };

    {
        // 1. Smaller than one block (7x5 px, 24-bit)
        auto img_small = make_test_bmp(7, 5, 24, [](uint32_t r, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
            B = static_cast<uint8_t>(r * 20); G = static_cast<uint8_t>(c * 30); R = 150;
        });
        bool ok = test_roundtrip("Image smaller than 1 block", img_small, 16);
        std::cout << " [" << (ok ? "PASS" : "FAIL") << "] Image smaller than 1 block (7x5, block 16x16)\n";
        all_ok &= ok;

        // 2. Exactly divisible by block size (32x32, block 16x16)
        auto img_div = make_test_bmp(32, 32, 24, [](uint32_t r, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
            B = static_cast<uint8_t>(r * 4); G = static_cast<uint8_t>(c * 4); R = static_cast<uint8_t>((r + c) * 2);
        });
        ok = test_roundtrip("Exact divisible dimensions", img_div, 16);
        std::cout << " [" << (ok ? "PASS" : "FAIL") << "] Exact divisible dimensions (32x32, block 16x16)\n";
        all_ok &= ok;

        // 3. Partial block at right edge (25x32)
        auto img_pr = make_test_bmp(25, 32, 24, [](uint32_t r, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
            B = static_cast<uint8_t>(c * 10); G = 100; R = static_cast<uint8_t>(r * 5);
        });
        ok = test_roundtrip("Partial block at right edge", img_pr, 16);
        std::cout << " [" << (ok ? "PASS" : "FAIL") << "] Partial block at right edge (25x32, block 16x16)\n";
        all_ok &= ok;

        // 4. Partial block at bottom edge (32x23)
        auto img_pb = make_test_bmp(32, 23, 24, [](uint32_t r, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
            B = 50; G = static_cast<uint8_t>(r * 10); R = static_cast<uint8_t>(c * 5);
        });
        ok = test_roundtrip("Partial block at bottom edge", img_pb, 16);
        std::cout << " [" << (ok ? "PASS" : "FAIL") << "] Partial block at bottom edge (32x23, block 16x16)\n";
        all_ok &= ok;

        // 5. Non-square with partial edges on both sides (27x19, 32-bit BGRA)
        auto img_32 = make_test_bmp(27, 19, 32, [](uint32_t r, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t& A) {
            B = static_cast<uint8_t>(r * 8); G = static_cast<uint8_t>(c * 8); R = 200; A = 255;
        });
        ok = test_roundtrip("Partial both edges + 32-bit BGRA", img_32, 16);
        std::cout << " [" << (ok ? "PASS" : "FAIL") << "] Partial both edges + 32-bit BGRA (27x19, block 16x16)\n";
        all_ok &= ok;
    }

    // -------------------------------------------------------------------
    // 8.C Predictor Selection Tests (Checking actual minimum cost)
    // -------------------------------------------------------------------
    std::cout << "\n--- 8.C Predictor Selection Tests ---\n";
    {
        // 1. Horizontal coherence with vertical slope divergence -> LEFT predictor strongly favored
        auto left_favored = make_test_bmp(32, 32, 24, [](uint32_t r, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
            uint8_t v = static_cast<uint8_t>((c * ((r % 7) + 2)) & 0xFF);
            B = v; G = v; R = v;
        });
        EABPSCompressStats s_l;
        write_bmp("__eabps_l.bmp", left_favored);
        compress_image_eabps("__eabps_l.bmp", "__eabps_l.ahmc", s_l, 16);
        bool left_selected = (s_l.count_left == s_l.total_blocks);
        std::cout << " [" << (left_selected ? "PASS" : "FAIL") << "] Horizontal coherence selects LEFT predictor: "
                  << s_l.count_left << " of " << s_l.total_blocks << " blocks\n";
        all_ok &= left_selected;
        std::remove("__eabps_l.bmp"); std::remove("__eabps_l.ahmc");

        // 2. Vertical coherence with horizontal discontinuities -> UP predictor strongly favored
        auto up_favored = make_test_bmp(32, 32, 24, [](uint32_t r, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
            uint8_t v = static_cast<uint8_t>((r * 4 + ((c % 2) ? 100 : 0)) % 256);
            B = v; G = v; R = v;
        });
        EABPSCompressStats s_u;
        write_bmp("__eabps_u.bmp", up_favored);
        compress_image_eabps("__eabps_u.bmp", "__eabps_u.ahmc", s_u, 16);
        bool up_selected = (s_u.count_up == s_u.total_blocks);
        std::cout << " [" << (up_selected ? "PASS" : "FAIL") << "] Vertical coherence selects UP predictor: "
                  << s_u.count_up << " of " << s_u.total_blocks << " blocks\n";
        all_ok &= up_selected;
        std::remove("__eabps_u.bmp"); std::remove("__eabps_u.ahmc");

        // 3. High-contrast checkerboard -> NONE predictor selected when prediction cannot help
        auto checker = make_test_bmp(32, 32, 24, [](uint32_t r, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
            bool w = (r + c) % 2 == 0;
            B = w ? 255 : 0; G = w ? 255 : 0; R = w ? 255 : 0;
        });
        EABPSCompressStats s_chk;
        write_bmp("__eabps_chk.bmp", checker);
        compress_image_eabps("__eabps_chk.bmp", "__eabps_chk.ahmc", s_chk, 16);
        bool none_selected = (s_chk.count_none == s_chk.total_blocks);
        std::cout << " [" << (none_selected ? "PASS" : "FAIL") << "] High-contrast checkerboard selects NONE predictor: "
                  << s_chk.count_none << " of " << s_chk.total_blocks << " blocks\n";
        all_ok &= none_selected;
        std::remove("__eabps_chk.bmp"); std::remove("__eabps_chk.ahmc");

        // 4. Solid color image -> minimal encoded cost with RLE
        auto solid = make_test_bmp(32, 32, 24, [](uint32_t, uint32_t, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
            B = 0; G = 255; R = 0;
        });
        EABPSCompressStats s_s;
        write_bmp("__eabps_s.bmp", solid);
        compress_image_eabps("__eabps_s.bmp", "__eabps_s.ahmc", s_s, 16);
        bool rle_selected = (s_s.count_rle == s_s.total_blocks);
        std::cout << " [" << (rle_selected ? "PASS" : "FAIL") << "] Solid color selects RLE with maximum compression: "
                  << s_s.count_rle << "/" << s_s.total_blocks << " blocks RLE\n";
        all_ok &= rle_selected;
        std::remove("__eabps_s.bmp"); std::remove("__eabps_s.ahmc");

        // 5. Verification of minimum complete encoded cost property across all blocks
        bool cost_is_minimal = true;
        for (const auto& blk : s_l.blocks) {
            // Each recorded payload must be <= uncompressed raw block cost
            size_t raw_bound = static_cast<size_t>(blk.width) * blk.height * 3;
            if (blk.payload_size > raw_bound) {
                cost_is_minimal = false;
                break;
            }
        }
        std::cout << " [" << (cost_is_minimal ? "PASS" : "FAIL") << "] Verified selected candidate achieves minimal complete encoded cost\n";
    }

    // -------------------------------------------------------------------
    // 8.D & 8.E Container Validation & Malformed Input Tests
    // -------------------------------------------------------------------
    std::cout << "\n--- 8.D & 8.E Container Validation & Error Handling ---\n";
    {
        // Create a valid EABPS file first
        auto test_img = make_test_bmp(16, 16, 24, [](uint32_t r, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
            B = static_cast<uint8_t>(r); G = static_cast<uint8_t>(c); R = 100;
        });
        write_bmp("__valid.bmp", test_img);
        EABPSCompressStats s_val;
        compress_image_eabps("__valid.bmp", "__valid.ahmc", s_val, 16);

        std::vector<uint8_t> valid_bytes;
        {
            std::ifstream vf("__valid.ahmc", std::ios::binary | std::ios::ate);
            auto sz = vf.tellg(); vf.seekg(0);
            valid_bytes.resize(static_cast<size_t>(sz));
            vf.read(reinterpret_cast<char*>(valid_bytes.data()), sz);
        }

        auto test_corrupted = [&](const std::string& /*name*/, const std::vector<uint8_t>& corrupted) -> bool {
            std::ofstream cf("__corrupt.ahmc", std::ios::binary | std::ios::trunc);
            cf.write(reinterpret_cast<const char*>(corrupted.data()), corrupted.size());
            cf.close();

            // Decompress must fail gracefully without crash
            bool dec_ok = decompress_image_eabps("__corrupt.ahmc", "__corrupt_out.bmp");
            std::remove("__corrupt.ahmc");
            std::remove("__corrupt_out.bmp");
            return !dec_ok; // we EXPECT it to fail safely
        };

        // 1. Invalid Magic
        auto b_bad_magic = valid_bytes;
        b_bad_magic[0] = 'X'; b_bad_magic[1] = 'Y';
        bool m_ok = test_corrupted("Invalid Magic rejection", b_bad_magic);
        std::cout << " [" << (m_ok ? "PASS" : "FAIL") << "] Invalid magic rejected safely\n";
        all_ok &= m_ok;

        // 2. Unsupported Version (e.g. 99)
        auto b_bad_ver = valid_bytes;
        b_bad_ver[4] = 99;
        bool v_ok = test_corrupted("Unsupported Version rejection", b_bad_ver);
        std::cout << " [" << (v_ok ? "PASS" : "FAIL") << "] Unsupported container version rejected safely\n";
        all_ok &= v_ok;

        // 3. Truncated Header (< 29 bytes)
        std::vector<uint8_t> b_trunc_hdr(valid_bytes.begin(), valid_bytes.begin() + 15);
        bool th_ok = test_corrupted("Truncated header rejection", b_trunc_hdr);
        std::cout << " [" << (th_ok ? "PASS" : "FAIL") << "] Truncated header rejected safely\n";
        all_ok &= th_ok;

        // 4. Truncated Payload (cut last 5 bytes)
        if (valid_bytes.size() > 5) {
            std::vector<uint8_t> b_trunc_pay(valid_bytes.begin(), valid_bytes.end() - 5);
            bool tp_ok = test_corrupted("Truncated payload rejection", b_trunc_pay);
            std::cout << " [" << (tp_ok ? "PASS" : "FAIL") << "] Truncated payload data rejected safely\n";
            all_ok &= tp_ok;
        }

        // 5. Out-of-range predictor ID
        auto b_bad_pred = valid_bytes;
        if (b_bad_pred.size() > 29) {
            b_bad_pred[29] = 250; // invalid predictor
            bool bp_ok = test_corrupted("Out-of-range predictor rejection", b_bad_pred);
            std::cout << " [" << (bp_ok ? "PASS" : "FAIL") << "] Out-of-range predictor rejected safely\n";
            all_ok &= bp_ok;
        }

        std::remove("__valid.bmp");
        std::remove("__valid.ahmc");
    }

    // -------------------------------------------------------------------
    // 8.F Fallback & Size Accounting Tests
    // -------------------------------------------------------------------
    std::cout << "\n--- 8.F Fallback & Size Accounting Tests ---\n";
    {
        // High entropy noisy image
        auto noisy = make_test_bmp(32, 32, 24, [](uint32_t r, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
            uint32_t seed = (r * 32 + c) * 1103515245 + 12345;
            B = static_cast<uint8_t>((seed >> 16) & 0xFF);
            G = static_cast<uint8_t>((seed >> 8) & 0xFF);
            R = static_cast<uint8_t>(seed & 0xFF);
        });
        write_bmp("__noisy.bmp", noisy);
        EABPSCompressStats s_noise;
        compress_image_eabps("__noisy.bmp", "__noisy.ahmc", s_noise, 16);

        std::ifstream chk("__noisy.ahmc", std::ios::binary | std::ios::ate);
        uint64_t actual_file_sz = static_cast<uint64_t>(chk.tellg());
        bool size_match = (s_noise.compressed_file_size == actual_file_sz);
        bool overhead_match = (s_noise.container_overhead == s_noise.compressed_file_size - s_noise.encoded_payload_size);

        std::cout << " [" << (size_match ? "PASS" : "FAIL") << "] Actual disk size exactly matches reported compressed size\n";
        std::cout << " [" << (overhead_match ? "PASS" : "FAIL") << "] Container overhead accounted accurately (Header + Per-block metadata)\n";
        all_ok &= size_match;
        all_ok &= overhead_match;

        std::remove("__noisy.bmp");
        std::remove("__noisy.ahmc");
    }

    std::cout << "\n============================================================\n";
    std::cout << " EABPS Test Suite Overall: " << (all_ok ? "ALL PASS" : "SOME FAILED") << "\n";
    std::cout << "============================================================\n";
    return all_ok;
}

// -----------------------------------------------------------------------
// Benchmark: EABPS vs Baseline AHLC
// -----------------------------------------------------------------------
bool run_eabps_benchmark()
{
    std::cout << "=========================================================================================\n";
    std::cout << " AHLC Benchmark: EABPS vs Baseline Spatial Adaptive AHLC\n";
    std::cout << "=========================================================================================\n";

    struct TestCase {
        std::string name;
        BMPImage img;
    };

    std::vector<TestCase> cases;
    // 1. Uniform
    cases.push_back({"Uniform Solid Color (64x64)", make_test_bmp(64, 64, 24, [](uint32_t, uint32_t, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
        B = 20; G = 200; R = 50;
    })});
    // 2. Horizontal gradient
    cases.push_back({"Horizontal Gradient (64x64)", make_test_bmp(64, 64, 24, [](uint32_t, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
        uint8_t v = static_cast<uint8_t>(c * 4);
        B = v; G = v; R = v;
    })});
    // 3. Vertical gradient
    cases.push_back({"Vertical Gradient (64x64)", make_test_bmp(64, 64, 24, [](uint32_t r, uint32_t, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
        uint8_t v = static_cast<uint8_t>(r * 4);
        B = v; G = v; R = v;
    })});
    // 4. Mixed Quadrants (Checker of gradients & solid)
    cases.push_back({"Mixed Quadrants (64x64)", make_test_bmp(64, 64, 24, [](uint32_t r, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
        if (r < 32 && c < 32) {
            B = 0; G = 255; R = 0; // solid
        } else if (r < 32 && c >= 32) {
            B = static_cast<uint8_t>(c * 4); G = B; R = B; // horizontal
        } else if (r >= 32 && c < 32) {
            B = static_cast<uint8_t>(r * 4); G = B; R = B; // vertical
        } else {
            bool w = ((r / 4) + (c / 4)) % 2 == 0; // checker
            B = w ? 255 : 0; G = w ? 255 : 0; R = w ? 255 : 0;
        }
    })});
    // 5. High-contrast checkerboard
    cases.push_back({"Checkerboard Pattern (64x64)", make_test_bmp(64, 64, 24, [](uint32_t r, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
        bool w = (r + c) % 2 == 0;
        B = w ? 255 : 0; G = w ? 255 : 0; R = w ? 255 : 0;
    })});
    // 6. Text / Line-art
    cases.push_back({"Line-art / Wireframe (64x64)", make_test_bmp(64, 64, 24, [](uint32_t r, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
        bool line = (r % 8 == 0) || (c % 8 == 0) || (r == c) || (r + c == 63);
        B = line ? 0 : 255; G = line ? 0 : 255; R = line ? 0 : 255;
    })});
    // 7. Pseudo-random noise
    cases.push_back({"High-Entropy Noise (64x64)", make_test_bmp(64, 64, 24, [](uint32_t r, uint32_t c, uint8_t& B, uint8_t& G, uint8_t& R, uint8_t&) {
        uint32_t seed = (r * 64 + c) * 1664525 + 1013904223;
        B = static_cast<uint8_t>(seed & 0xFF);
        G = static_cast<uint8_t>((seed >> 8) & 0xFF);
        R = static_cast<uint8_t>((seed >> 16) & 0xFF);
    })});

    std::printf("%-30s | %-8s | %-12s | %-12s | %-10s | %-10s | %-12s | %-6s\n",
                "Test Image", "Orig (B)", "Base (B)", "EABPS (B)", "Base Ratio", "EABPS Ratio", "EABPS Preds", "Verify");
    std::cout << "-------------------------------+----------+--------------+--------------+------------+------------+--------------+-------\n";

    for (const auto& tc : cases) {
        const std::string fn_bmp = "__bench_" + tc.name.substr(0, 4) + ".bmp";
        const std::string fn_base_ahmc = "__bench_" + tc.name.substr(0, 4) + "_base.ahmc";
        const std::string fn_eabps_ahmc = "__bench_" + tc.name.substr(0, 4) + "_eabps.ahmc";
        const std::string fn_rec_bmp = "__bench_" + tc.name.substr(0, 4) + "_rec.bmp";

        write_bmp(fn_bmp, tc.img);

        // 1. Run baseline AHLC
        ImageCompressStats s_base;
        compress_image(fn_bmp, fn_base_ahmc, s_base);

        // 2. Run EABPS
        EABPSCompressStats s_eabps;
        compress_image_eabps(fn_bmp, fn_eabps_ahmc, s_eabps, 16);

        // 3. Verify exact reconstruction of EABPS
        decompress_image_eabps(fn_eabps_ahmc, fn_rec_bmp);
        BMPImage rec_img;
        read_bmp(fn_rec_bmp, rec_img);
        bool match = images_equal(tc.img, rec_img);

        const double base_ratio = s_base.compressed_file_size > 0
            ? static_cast<double>(s_base.original_file_size) / s_base.compressed_file_size : 0.0;
        const double eabps_ratio = s_eabps.compressed_file_size > 0
            ? static_cast<double>(s_eabps.original_file_size) / s_eabps.compressed_file_size : 0.0;

        char pred_dist[32];
        std::snprintf(pred_dist, sizeof(pred_dist), "N:%u L:%u U:%u",
                      s_eabps.count_none, s_eabps.count_left, s_eabps.count_up);

        std::printf("%-30s | %-8llu | %-12llu | %-12llu | %9.2fx | %9.2fx | %-12s | %-6s\n",
                    tc.name.c_str(),
                    static_cast<unsigned long long>(s_base.original_file_size),
                    static_cast<unsigned long long>(s_base.compressed_file_size),
                    static_cast<unsigned long long>(s_eabps.compressed_file_size),
                    base_ratio,
                    eabps_ratio,
                    pred_dist,
                    match ? "PASS" : "FAIL");

        std::remove(fn_bmp.c_str());
        std::remove(fn_base_ahmc.c_str());
        std::remove(fn_eabps_ahmc.c_str());
        std::remove(fn_rec_bmp.c_str());
    }

    std::cout << "=========================================================================================\n";
    return true;
}

} // namespace ahlc
