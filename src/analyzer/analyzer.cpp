#include "analyzer.hpp"

#include <fstream>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace ahlc {

static constexpr size_t BUFFER_SIZE = 65536; // 64 KB read buffer

bool analyze_file(const std::string& path, AnalysisResult& result)
{
    result = {};
    result.filename = path;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file: " << path << "\n";
        return false;
    }

    // Read in chunks to avoid loading the whole file into memory.
    std::array<char, BUFFER_SIZE> buffer;
    while (file.read(buffer.data(), BUFFER_SIZE) || file.gcount() > 0) {
        auto count = static_cast<size_t>(file.gcount());
        for (size_t i = 0; i < count; ++i) {
            result.freq[static_cast<uint8_t>(buffer[i])]++;
        }
        result.file_size += count;
    }

    if (result.file_size == 0) {
        std::cerr << "Error: File is empty: " << path << "\n";
        return false;
    }

    // Compute derived statistics.
    double entropy = 0.0;
    uint64_t max_count = 0;
    for (int b = 0; b < 256; ++b) {
        if (result.freq[b] == 0) continue;
        ++result.unique_bytes;
        double p = static_cast<double>(result.freq[b]) / static_cast<double>(result.file_size);
        entropy -= p * std::log2(p);
        if (result.freq[b] > max_count) {
            max_count = result.freq[b];
            result.most_freq_byte  = static_cast<uint8_t>(b);
            result.most_freq_count = max_count;
        }
    }
    result.entropy = entropy;
    return true;
}

void print_report(const AnalysisResult& result)
{
    double size_mb = static_cast<double>(result.file_size) / (1024.0 * 1024.0);
    double most_freq_pct =
        100.0 * static_cast<double>(result.most_freq_count) /
        static_cast<double>(result.file_size);

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "--------------------------------------------\n";
    std::cout << " AHLC File Analysis Report\n";
    std::cout << "--------------------------------------------\n";
    std::cout << " File            : " << result.filename        << "\n";
    std::cout << " Size (bytes)    : " << result.file_size       << "\n";
    std::cout << std::setprecision(4);
    std::cout << " Size (MB)       : " << size_mb                << "\n";
    std::cout << " Unique bytes    : " << result.unique_bytes
              << " / 256\n";
    std::cout << std::setprecision(6);
    std::cout << " Shannon entropy : " << result.entropy
              << " bits/byte\n";
    std::cout << " Most freq. byte : 0x"
              << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
              << static_cast<int>(result.most_freq_byte)
              << std::dec << std::setfill(' ')
              << "  (" << result.most_freq_count << " times, "
              << std::setprecision(2) << most_freq_pct << "%)\n";
    std::cout << "--------------------------------------------\n";
}

} // namespace ahlc
