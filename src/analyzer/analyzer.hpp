#pragma once

#include <string>
#include <array>
#include <cstdint>

namespace ahlc {

// Results from analyzing a binary file.
struct AnalysisResult {
    std::string  filename;
    uint64_t     file_size       = 0;   // bytes
    uint64_t     unique_bytes    = 0;   // how many of the 256 values appear
    double       entropy         = 0.0; // Shannon entropy (bits per byte)
    uint8_t      most_freq_byte  = 0;
    uint64_t     most_freq_count = 0;
    std::array<uint64_t, 256> freq = {}; // frequency table
};

// Analyze a file in buffered binary mode (safe for large files).
// Returns false and sets result.filename to an error message on failure.
bool analyze_file(const std::string& path, AnalysisResult& result);

// Print a formatted report to stdout.
void print_report(const AnalysisResult& result);

} // namespace ahlc
