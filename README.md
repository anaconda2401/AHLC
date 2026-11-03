# AHLC — Content-Adaptive Hybrid Lossless Compression

> **High-performance multimedia compression engine in C++20 with an interactive Flask web dashboard.**  
> Lossless compression for **Images (BMP, PNG)**, **Audio (WAV, FLAC)**, and **Video (AVI, MP4)** with 100% bit-exact verification.

[![Language](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Framework](https://img.shields.io/badge/Web%20App-Flask%203.x-green.svg)](https://flask.palletsprojects.com/)
[![FFmpeg](https://img.shields.io/badge/FFmpeg-Linked-red.svg)](https://ffmpeg.org/)
[![Tests](https://img.shields.io/badge/Tests-55%20Passing-brightgreen.svg)](#running-tests)
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)](LICENSE)

---

## Overview

**AHLC (Adaptive Hybrid Lossless Compression)** is an end-to-end multimedia compression platform that combines predictive decorrelation with adaptive entropy coding.

Standard lossless codecs rely on static prediction filters applied globally across an entire file. AHLC introduces **Entropy-Aware Block-wise Predictor Selection (EABPS)** and **Temporal EABPS (TEABPS)**:
- **Block Partitioning**: Media streams are partitioned into spatio-temporal blocks ($16 \times 16$ for images and video, $1,024$ samples for audio).
- **Candidate Evaluation**: Multiple spatial and temporal predictors (`Left`, `Up`, `Paeth`, `MAP`, `Delta-1`, `Delta-2`, `Temporal-D1/D2`) are evaluated alongside candidate entropy encoders (Byte-Run PackBits RLE, Canonical Huffman, and RAW).
- **Cost Minimization**: The engine selects the exact `(Predictor, Codec)` pair that produces the minimal byte cost for every individual block.
- **Guaranteed Non-Expansion**: If high-entropy or near-random data expands under compression, AHLC automatically falls back to honest original retention with 0 fake bytes saved and 100% exact verification.
- **Bit-Exact Verification**: Every compressed file is decoded and verified down to the exact pixel, PCM audio sample, and video frame checksum.

---

## Supported Formats

| Media Type | Formats | Compression Pipeline | Primary Output | Verification Standard |
| :--- | :--- | :--- | :--- | :--- |
| **Images** | **BMP**, **PNG** | • Spatial EABPS ($16 \times 16$ blocks)<br>• Full-frame Median Adaptive Predictor<br>• PNG DEFLATE L9 + adaptive row filters | `.ahmc` / `.bmp`<br>Optimized `.png` | **Pixel-Exact** (RGB/RGBA sample match) |
| **Audio** | **WAV**, **FLAC** | • Audio EABPS ($1,024$-sample blocks)<br>• 1st & 2nd-order PCM Delta prediction<br>• FLAC candidate analysis & retention | `.ahmc` / `.wav`<br>Optimized `.flac` | **Sample-Exact** (Bit-identical PCM audio) |
| **Video** | **AVI**, **MP4** | • Video TEABPS (Multi-threaded planar YUV420p)<br>• Lossless FFV1 intra/inter-frame (16 slices)<br>• H.264 preview generation (`+faststart`) | `_lossless.avi`<br>`_playback.mp4` | **Frame-Exact** (Streaming `framemd5` match) |

---

## Quick Start (Clone & Run)

This repository includes automated setup and launch scripts so the project can be reproduced immediately after cloning.

### Prerequisites

1. **C++20 Compiler**: Visual Studio 2022+ (Desktop development with C++), GCC 12+, or Clang 15+.
2. **CMake**: Version 3.20 or newer.
3. **vcpkg** (C++ package manager): Required to provide FFmpeg C++ development headers & libraries.
4. **FFmpeg CLI Tools**: `ffmpeg` and `ffprobe` installed and accessible in your system `PATH` (e.g. via `winget install Gyan.FFmpeg`, `scoop install ffmpeg`, or package manager).
5. **Python**: 3.10+ with `venv`.

---

### Method A: One-Click Automated Scripts (Recommended)

#### 1. Clone the repository
```powershell
git clone <repository-url>
cd AHLC
```

#### 2. Build the C++ Engine & Python Environment
Run the automated build script (detects CMake, locates the vcpkg toolchain, compiles `ahmc.exe` in Release mode, creates `.venv`, and installs Python dependencies):

```powershell
# Using Command Prompt (cmd.exe):
build.bat

# Or using PowerShell:
.\build.ps1
```

#### 3. Run the Web Dashboard
Launch the interactive web UI:

```powershell
# Using Command Prompt:
run.bat

# Or using PowerShell:
.\run.ps1
```
Open **`http://127.0.0.1:5000`** in your browser to access the dashboard.

#### 4. Run the Test Suite
Verify all 55 test cases across 6 phases:

```powershell
# Using Command Prompt:
test.bat

# Or using PowerShell:
.\test.ps1
```

---

### Method B: Manual Step-by-Step Setup

#### 1. Set up vcpkg & Install FFmpeg
If you do not have `vcpkg` installed:
```powershell
git clone https://github.com/microsoft/vcpkg.git %USERPROFILE%\vcpkg
%USERPROFILE%\vcpkg\bootstrap-vcpkg.bat
set VCPKG_ROOT=%USERPROFILE%\vcpkg
```

#### 2. Configure and Compile the C++ Engine
Using the vcpkg CMake toolchain file:

```powershell
# Set toolchain path (PowerShell):
$vcpkgToolchain = "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

# Configure build
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$vcpkgToolchain" -DVCPKG_TARGET_TRIPLET=x64-windows

# Compile Release binary
cmake --build build --config Release
```
*The compiled binary will be located at `build/Release/ahmc.exe` (or `build/ahmc.exe`).*

#### 3. Set Up Python Virtual Environment
```powershell
# Create virtual environment
python -m venv .venv

# Install Python requirements
.\.venv\Scripts\pip install -r requirements.txt
```

#### 4. Launch the Web Application
```powershell
.\.venv\Scripts\python.exe flask_app/app.py
```
Open **`http://127.0.0.1:5000`** in your browser.

---

## Web Dashboard & Visual Theme

The web interface features an oceanic **Precious Blue with Softened Green** atmospheric theme with frosted glassmorphism:
- **No Harsh White**: Pure `#ffffff` surfaces are replaced by soft frosted tinted glass panels (`rgba(14, 34, 44, 0.72); backdrop-filter: blur(16px)`).
- **Responsive Micro-Interactions**: Hover elevation lifts, tactile active button presses, luminous sheen animations, and animated drop zones.
- **Live Verification Badge**: Displays pixel-exact, sample-exact, and frame-exact verification results with detailed metrics.
- **9-Section Technical Report**: Inspect input metadata, raw vs residual Shannon entropy, predictor distributions, container overhead, timing, and decompression benchmarks.
- **Universal Downloads**: Supports downloading the optimized master file, research container (`.ahmc` / `.ahlv`), and playable video preview (`.mp4`).

---

## Command Line Interface (CLI)

The compiled `ahmc.exe` engine can also be executed directly from the command line:

```powershell
# Generic Shannon Entropy Analysis
.\build\Release\ahmc.exe analyze sample.bin

# BMP Image Workflow
.\build\Release\ahmc.exe image-analyze sample.bmp
.\build\Release\ahmc.exe image-compress sample.bmp output.ahmc
.\build\Release\ahmc.exe image-decompress output.ahmc restored.bmp
.\build\Release\ahmc.exe image-verify sample.bmp restored.bmp

# Image EABPS Block-wise Compression
.\build\Release\ahmc.exe image-eabps-compress sample.bmp output_eabps.ahmc --block-size 16 --json
.\build\Release\ahmc.exe image-eabps-decompress output_eabps.ahmc restored_eabps.bmp

# WAV Audio PCM Workflow
.\build\Release\ahmc.exe audio-analyze sample.wav
.\build\Release\ahmc.exe audio-compress sample.wav output.ahmc
.\build\Release\ahmc.exe audio-decompress output.ahmc restored.wav
.\build\Release\ahmc.exe audio-verify sample.wav restored.wav

# Audio EABPS Block-wise Compression
.\build\Release\ahmc.exe audio-eabps-compress sample.wav output_audio.ahmc --block-size 1024 --json
.\build\Release\ahmc.exe audio-eabps-decompress output_audio.ahmc restored_audio.wav

# Video TEABPS Workflow (Planar YUV420p Multi-threaded)
.\build\Release\ahmc.exe video-teabps-compress input.avi output.ahlv --block-size 16 --json
.\build\Release\ahmc.exe video-teabps-decompress output.ahlv restored.yuv

# Standalone Codecs
.\build\Release\ahmc.exe rle-compress input.bin output.rle
.\build\Release\ahmc.exe huffman-compress input.bin output.huff
```

---

## Running Tests

AHLC includes an automated regression test suite covering all C++ core algorithms, format handlers, audio/video pipelines, and web endpoints:

```powershell
# One-click test runner:
.\test.bat    # cmd.exe
.\test.ps1    # PowerShell

# Master Python regression runner (all 6 phases, 55 test cases):
.\.venv\Scripts\python.exe scratch\run_all_tests.py

# Individual test suites:
.\.venv\Scripts\python.exe scratch\test_suite.py        # Image suite
.\.venv\Scripts\python.exe scratch\test_audio_suite.py  # Audio suite
.\.venv\Scripts\python.exe scratch\test_video_suite.py  # Video suite
```

---

## Project Structure

```
AHLC/
├── CMakeLists.txt              # CMake build configuration (C++20)
├── vcpkg.json                  # vcpkg manifest for FFmpeg dependency
├── requirements.txt            # Python dependencies (Flask, Pillow, soundfile, numpy)
├── build.bat / build.ps1       # Automated build & setup scripts
├── run.bat / run.ps1           # Automated web dashboard launch scripts
├── test.bat / test.ps1         # Automated regression test scripts
├── README.md                   # Comprehensive project documentation
├── command.md                  # Complete CLI & API command reference
│
├── src/                        # C++20 Multimedia Compression Engine
│   ├── main.cpp                # CLI dispatch, test harness & JSON output
│   ├── analyzer/               # Byte frequency & Shannon entropy engine
│   ├── bitio/                  # Bit-level stream reader & writer
│   ├── rle/                    # PackBits byte-run RLE codec
│   ├── entropy/                # Canonical Huffman encoder & decoder
│   ├── image/                  # Spatial predictor & Image EABPS engine
│   ├── audio/                  # WAV PCM delta predictor & Audio EABPS engine
│   ├── video/                  # Multi-threaded planar YUV420p TEABPS engine
│   └── media/                  # Format detector & media buffer interfaces
│
├── flask_app/                  # Interactive Web Application
│   ├── app.py                  # Flask route handlers & endpoints
│   ├── ahmc_runner.py          # Dynamic engine subprocess runner & FFmpeg wrapper
│   ├── database.py             # SQLite persistence layer (auto-initializes ahlc.db)
│   ├── templates/              # Jinja2 HTML templates (Dashboard, Analyze, Result, Report, History)
│   └── static/                 # Precious Blue CSS styling & JavaScript
│
├── uploads/                    # User uploaded media (.gitkeep tracked)
├── outputs/                    # Processed & compressed files (.gitkeep tracked)
│
└── scratch/                    # Automated Regression Test Suites
    ├── run_all_tests.py        # Master test runner (Phases 1-6)
    ├── test_suite.py           # Image test suite (BMP & PNG)
    ├── test_audio_suite.py     # Audio test suite (WAV & FLAC)
    ├── test_video_suite.py     # Video test suite (AVI & MP4)
    ├── test_video_teabps_full.py # TEABPS container validation
    └── benchmark_video.py      # Video benchmark utility
```

---

## Troubleshooting

- **FFmpeg not found**: Ensure `ffmpeg` and `ffprobe` are on your system `PATH`. You can check by running `ffmpeg -version` in your terminal. You can also specify paths explicitly using environment variables:
  `$env:AHLC_FFMPEG_PATH = "C:\path\to\ffmpeg.exe"` and `$env:AHLC_FFPROBE_PATH = "C:\path\to\ffprobe.exe"`.
- **vcpkg toolchain error**: Set `VCPKG_ROOT` in your environment (e.g., `set VCPKG_ROOT=C:\vcpkg` or `$env:VCPKG_ROOT = "C:\vcpkg"`).
- **Missing DLLs at runtime on Windows**: The CMake build system automatically copies FFmpeg DLLs to the target directory containing `ahmc.exe`. If running outside CMake, ensure the FFmpeg DLLs (`avcodec-*.dll`, `avutil-*.dll`, etc.) are alongside `ahmc.exe` or on your `PATH`.

---

## License

This project is licensed under the [MIT License](LICENSE).
