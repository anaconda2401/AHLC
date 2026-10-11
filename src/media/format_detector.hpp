#pragma once

#include "media_defs.hpp"
#include <string>

namespace ahlc::media {

/**
 * Probes the file at the given path and identifies its format.
 * Uses FFmpeg libavformat / libavcodec under the hood to recognize signatures.
 */
FormatInfo detect_format(const std::string& path);

/**
 * Prints the result of detect_format to standard output
 * matching the required format.
 */
void print_format_analysis(const std::string& path, const FormatInfo& info);

} // namespace ahlc::media
