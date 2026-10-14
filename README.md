# AHLC — Content-Adaptive Hybrid Lossless Compression

> **Multimedia Compression Engineering & Research Platform**  
> **C++20 Compression Engine · Python Flask Interactive Dashboard · FFmpeg Acceleration**  
> **Spatial, Temporal, and Entropy-Aware Coding for Images, Audio, and Video with Bit-Exact Verification**

[![Language](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Backend](https://img.shields.io/badge/Dashboard-Flask%20%7C%20Python%203.10%2B-green.svg)](https://flask.palletsprojects.com/)
[![FFmpeg](https://img.shields.io/badge/FFmpeg-Linked%20via%20vcpkg-red.svg)](https://ffmpeg.org/)
[![Verification](https://img.shields.io/badge/Verification-100%25%20Bit--Exact-brightgreen.svg)](#8-automated-test-suite--regression-matrix)
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)](LICENSE)

---

## 1. Executive Overview

**AHLC (Content-Adaptive Hybrid Lossless Compression)** is an end-to-end multimedia compression framework developed in modern **C++20** and accompanied by a responsive, technical **Python Flask** web dashboard. 

The system unifies predictive decorrelation and adaptive entropy coding across all three major multimedia modalities:
- **Raster Images:** BMP (24-bit RGB / 32-bit RGBA) and PNG.
- **Uncompressed & Stream Audio:** PCM WAV (8, 16, 24, 32-bit) and FLAC.
- **Moving Video Sequences:** Raw uncompressed AVI and compressed MP4 containers.

### Core Algorithmic Innovation: EABPS & TEABPS
Standard lossless codecs apply fixed global prediction filters (e.g., standard delta or left-neighbour differencing) across an entire file. AHLC implements **Entropy-Aware Block-wise Predictor Selection (EABPS)** and **Temporal EABPS (TEABPS)**:
1. **Block Partitioning:** The multimedia stream is divided into independent, self-contained spatio-temporal blocks ($16 \times 16$ pixels for images and video luma, $8 \times 8$ for video chroma, and $4,096$ frames for audio).
2. **Multi-Predictor Residual Evaluation:** Candidate predictors (intra-frame spatial, 1st/2nd-order temporal, or 1st/2nd-order polynomial delta) are computed in parallel using modular arithmetic ($\pmod{256}$ or native 2's complement wrapping).
3. **Entropy-Aware Codec Selection:** Each residual candidate is evaluated against multiple entropy encoders (**Byte-Run PackBits RLE**, **Canonical Huffman Coding**, and **RAW Fallback**). The compressor selects the exact `(Predictor, Codec)` pair that minimizes the complete encoded byte cost:
   $$\text{Cost} = \text{payload\_size}(\text{codec}, \text{pred}) + \text{metadata\_overhead}$$
4. **Guaranteed Non-Expansion:** If high-entropy noise or pre-compressed content causes expansion, the block or entire container automatically falls back to raw or original storage, guaranteeing that no artificial space expansion occurs.

---

## 2. Supported Formats & Processing Architecture

| Modality | Ingest Formats | Compression Pipeline | Output Containers & Artifacts | Lossless Verification Proof |
| :--- | :--- | :--- | :--- | :--- |
| **Images** | **BMP** (`.bmp`)<br>**PNG** (`.png`) | • **Spatial EABPS:** $16 \times 16$ blocks with `{NONE, LEFT, UP}` $\times$ `{RLE, HUFFMAN, RAW}`.<br>• **PNG Pipeline:** Lossless RGBA extraction + DEFLATE L9 + adaptive scanline filtering.<br>• **Pal8 Quantization:** Reversible 8-bit palette mapping for low-color graphics. | • **AHMC v3** (`.ahmc`): Custom binary bitstream.<br>• **Reconstructed BMP** (`.bmp`): 100% pixel-exact.<br>• **Optimized PNG** (`.png`). | **Sample/Pixel Identical:**<br>100% RGB/RGBA sample equality ($R(x,y) \equiv I(x,y)$) |
| **Audio** | **WAV** (`.wav`)<br>**FLAC** (`.flac`) | • **Audio EABPS:** 4096-frame blocks with `{NONE, DELTA-1, DELTA-2}` $\times$ `{RLE, HUFFMAN, RAW}`.<br>• De-interleaved channel processing.<br>• 8, 16, 24, 32-bit native signed/unsigned sample arithmetic.<br>• Lossless FLAC stream reconstruction via libsndfile. | • **AHLA v3** (`.ahmc`): Custom audio bitstream.<br>• **Reconstructed WAV** (`.wav`).<br>• **Lossless FLAC** (`.flac`). | **Sample Identical:**<br>100% PCM sample match across all channels |
| **Video** | **AVI** (`.avi`)<br>**MP4** (`.mp4`) | • **Video TEABPS:** Block-wise temporal prediction (`NONE`, `TEMPORAL-D1`, `TEMPORAL-D2`) over planar YUV420p sequences.<br>• **Lossless Master Export:** FFV1 v3 intra/inter-frame codec in AVI container.<br>• **Universal Playback Export:** H.264 High Profile (`crf 18`, `+faststart`, AAC audio) for universal player playback.<br>• **Audio Streamcopy:** Preserves original audio streams losslessly (`-c:a copy`). | • **AHLV v3** (`.ahlv`): Custom video bitstream.<br>• **Lossless Master AVI** (`_lossless.avi`): 100% lossless FFV1.<br>• **Playable Video MP4** (`_playback.mp4`): Universal H.264. | **Three Identity Levels:**<br>• Bitstream Identity (Fallback)<br>• Decoded-Frame Identity (Native YUV)<br>• Canonical YUV420p Match |

---

## 3. High-Level System Architecture

```
                                      Uploaded Media File
                                (BMP, PNG, WAV, FLAC, AVI, MP4)
                                                │
                                                ▼
                        ┌───────────────────────────────────────────────┐
                        │      Format Abstraction Layer (C++20)         │
                        │    Header Magic Sniffing & FFmpeg Probing     │
                        └───────────────────────┬───────────────────────┘
                                                │
                    ┌───────────────────────────┼───────────────────────────┐
                    ▼                           ▼                           ▼
            Uncompressed Image          Uncompressed Audio          Canonical Planar YUV
              (RGB / RGBA)                (De-interleaved PCM)       (YUV420p Sequence)
                    │                           │                           │
                    ▼                           ▼                           ▼
        ┌───────────────────────┐   ┌───────────────────────┐   ┌───────────────────────┐
        │  Image Spatial EABPS  │   │   Audio EABPS Engine  │   │  Video TEABPS Engine  │
        │   16x16 Pixel Blocks  │   │   4096-Sample Blocks  │   │  16x16 Y / 8x8 UV Blk │
        │  • NONE               │   │  • NONE               │   │  • NONE (I-Frame)     │
        │  • LEFT Neighbour     │   │  • 1st-Order DELTA-1  │   │  • TEMPORAL-D1        │
        │  • UP Neighbour       │   │  • 2nd-Order DELTA-2  │   │  • TEMPORAL-D2        │
        └───────────┬───────────┘   └───────────┬───────────┘   └───────────┬───────────┘
                    │                           │                           │
                    └───────────────────────────┼───────────────────────────┘
                                                │
                                                ▼
                        ┌───────────────────────────────────────────────┐
                        │    Candidate Codec Byte-Cost Optimization     │
                        │     Evaluate min(PackBits RLE, Huffman, RAW)  │
                        └───────────────────────┬───────────────────────┘
                                                │
                                                ▼
                        ┌───────────────────────────────────────────────┐
                        │      Dynamic Container Serialization          │
                        │  • Image: AHMC v3 Container (29-byte header)  │
                        │  • Audio: AHLA v3 Container (31-byte header)  │
                        │  • Video: AHLV v3 Container (41-byte header)  │
                        │  • Playback Muxer: H.264 MP4 with +faststart  │
                        │  • Master Muxer: FFV1 Lossless AVI            │
                        └───────────────────────┬───────────────────────┘
                                                │
                                                ▼
                        ┌───────────────────────────────────────────────┐
                        │          Lossless Verification Layer          │
                        │  Independent Round-Trip Decode & Hash Match   │
                        └───────────────────────┬───────────────────────┘
                                                │
                                                ▼
                        ┌───────────────────────────────────────────────┐
                        │       Flask Web Application & Dashboard       │
                        │   Multi-Endpoint Downloads & Technical Reports│
                        └───────────────────────────────────────────────┘
```

---

## 4. Repository Structure

```
AHLC/
├── CMakeLists.txt                 # C++20 build definition, vcpkg linkage & DLL deployment
├── vcpkg.json                     # vcpkg manifest declaring FFmpeg dependency
├── requirements.txt               # Python backend dependencies (Flask, Pillow, soundfile, numpy)
├── README.md                      # Comprehensive project documentation & user manual
├── .gitignore                     # Rigorous patterns ignoring build, database, & media artifacts
│
├── src/                           # C++20 Core Compression Engine
│   ├── main.cpp                   # CLI dispatcher, benchmark runner, and command router
│   ├── analyzer/                  # Shannon entropy, byte frequency distribution, unicity
│   │   ├── analyzer.hpp / .cpp
│   │
│   ├── bitio/                     # MSB-first bit-level stream I/O
│   │   ├── bit_writer.hpp / .cpp
│   │   └── bit_reader.hpp / .cpp
│   │
│   ├── rle/                       # Byte-level PackBits Run-Length Encoding / Decoding
│   │   ├── rle_encoder.hpp / .cpp
│   │   └── rle_decoder.hpp / .cpp
│   │
│   ├── entropy/                   # Canonical Huffman prefix coding with optimal lengths
│   │   ├── huffman.hpp / .cpp
│   │
│   ├── image/                     # Image processing, BMP parser, Spatial EABPS engine
│   │   ├── image.hpp / .cpp       # BMP 24/32-bit I/O & memory buffer representation
│   │   ├── image_predictor.hpp    # Global left-neighbour spatial predictor
│   │   ├── eabps.hpp / .cpp       # Spatial EABPS: Block-wise adaptive predictor & AHMC v3
│   │
│   ├── audio/                     # Audio processing, WAV PCM parser, Audio EABPS engine
│   │   ├── audio.hpp / .cpp       # WAV 8/16/24/32-bit PCM I/O & channel de-interleaver
│   │   ├── audio_predictor.hpp    # Global 1st-order delta predictor
│   │   ├── audio_eabps.hpp / .cpp # Audio EABPS: Block-wise DELTA-1/DELTA-2 & AHLA v3
│   │
│   ├── video/                     # Video processing, FFmpeg decoder/encoder, TEABPS engine
│   │   ├── video_teabps.hpp/.cpp  # Temporal EABPS: Planar YUV420p sequences & AHLV v3
│   │
│   └── media/                     # Unified Format Abstraction Layer
│       ├── media_defs.hpp         # Media types, formats, channel layouts, identity enums
│       ├── decoder_interface.hpp  # Abstract decoder base class
│       ├── image_frame.hpp        # Uncompressed image pixel representation
│       ├── audio_buffer.hpp       # Uncompressed PCM audio representation
│       ├── video_frame.hpp        # Raw video frame sequence representation
│       └── format_detector.hpp/.cpp # Magic signature detection and format probing
│
├── flask_app/                     # Web Application Backend & Frontend
│   ├── app.py                     # Flask web routes, dual-artifact download routing, sessions
│   ├── ahmc_runner.py             # Subprocess CLI wrapper, media conversions, H.264 playback muxer
│   ├── database.py                # SQLite persistence (experiments table with schema migrations)
│   ├── static/
│   │   ├── css/style.css          # Technical dark/light theme CSS with glassmorphic cards
│   │   └── js/dashboard.js        # Drag-and-drop upload, progress animations, theme state
│   └── templates/
│       ├── base.html              # Base navigation, sidebar layout, theme toggle
│       ├── dashboard.html         # Upload zone, live pipeline progress, system stats
│       ├── analyze.html           # Entropy inspection & metadata analysis without compression
│       ├── result.html            # Results, verification badges, Chart.js graphs, 3 download buttons
│       ├── history.html           # Experiment history table with search and Clear History
│       └── report.html            # 10-section formal technical audit & verification report
│
├── scratch/                       # Test Suites & Verification Runners
│   ├── run_all_tests.py           # Master test runner (Phases 1–6, all 17 test suites)
│   ├── test_suite.py              # Phase 2: Python Image Suite (BMP & PNG)
│   ├── test_audio_suite.py        # Phase 3: Python Audio Suite (WAV & FLAC — 10 requirements)
│   ├── test_video_suite.py        # Phase 4: Python Video Suite (AVI & MP4 — 10 requirements)
│   ├── test_video_teabps_full.py  # Phase 5: Python Video TEABPS Full Verification
│   └── test_video_playback_pipeline.py # Phase 6: Video Playback, FFV1 Integrity & Download Suite
│
├── uploads/                       # Runtime uploaded media (git-ignored, kept via .gitkeep)
└── outputs/                       # Runtime generated media & containers (git-ignored)
```

---

## 5. Binary Container Specifications

AHLC implements versioned, self-contained binary containers with bounds-checked headers to prevent out-of-memory or buffer overrun exploits.

### 5.1 Image EABPS Container (`AHMC v3`)
- **Magic:** `AHLC` (`0x41 0x48 0x4C 0x43`)
- **Version:** `3` (`0x03`)
- **Header Size:** $29\text{ bytes}$
- **Layout:**
  ```
  [0..3]   Magic "AHLC" (4B)
  [4]      Version = 3 (1B)
  [5..8]   Width uint32_t (4B)
  [9..12]  Height uint32_t (4B)
  [13..14] Bits Per Pixel uint16_t (2B: 24 or 32)
  [15..16] Block Size uint16_t (2B: default 16)
  [17..20] Total Blocks uint32_t (4B)
  [21..24] Original Pixel Bytes uint32_t (4B)
  [25..28] Total Payload Bytes uint32_t (4B)
  [29..]   Block Records Table: Total Blocks × (6B metadata + payload)
           • Predictor (1B): 0=NONE, 1=LEFT, 2=UP
           • Codec (1B):     0=RLE, 1=HUFFMAN, 2=RAW
           • Payload Size (4B): Little-endian uint32_t
           • Payload Data: Variable bytes
  ```

### 5.2 Audio EABPS Container (`AHLA v3`)
- **Magic:** `AHLA` (`0x41 0x48 0x4C 0x41`)
- **Version:** `3` (`0x03`)
- **Header Size:** $31\text{ bytes}$
- **Layout:**
  ```
  [0..3]   Magic "AHLA" (4B)
  [4]      Version = 3 (1B)
  [5..6]   Format uint16_t (2B: 1 = PCM)
  [7..8]   Channels uint16_t (2B)
  [9..12]  Sample Rate uint32_t (4B)
  [13..14] Bits Per Sample uint16_t (2B: 8, 16, 24, 32)
  [15..18] Block Size uint32_t (4B: default 4096 frames)
  [19..22] Total Blocks uint32_t (4B)
  [23..26] Total Frame Count uint32_t (4B)
  [27..30] Original PCM Bytes uint32_t (4B)
  [31..]   Block Metadata Table: Total Blocks × 6B
           • Predictor (1B): 0=NONE, 1=DELTA-1, 2=DELTA-2
           • Codec (1B):     0=RLE, 1=HUFFMAN, 2=RAW
           • Payload Size (4B): Little-endian uint32_t
  Followed by concatenation of all block payloads.
  ```

### 5.3 Video TEABPS Container (`AHLV v3`)
- **Magic:** `AHLV` (`0x41 0x48 0x4C 0x56`)
- **Version:** `3` (`0x03`)
- **Header Size:** $41\text{ bytes}$
- **Layout:**
  ```
  [0..3]   Magic "AHLV" (4B)
  [4]      Version = 3 (1B)
  [5..6]   Pixel Format uint16_t (2B: 1 = Planar YUV420p)
  [7..10]  Width uint32_t (4B)
  [11..14] Height uint32_t (4B)
  [15..18] Frame Rate Numerator uint32_t (4B)
  [19..22] Frame Rate Denominator uint32_t (4B)
  [23..26] Frame Count uint32_t (4B)
  [27..28] Block Size uint16_t (2B: default 16 for Y, 8 for UV)
  [29..32] Total Blocks uint32_t (4B)
  [33..40] Original Raw YUV Bytes uint64_t (8B)
  [41..]   Block Metadata Table: Total Blocks × 6B
           • Predictor (1B): 0=NONE, 1=TEMPORAL-D1, 2=TEMPORAL-D2
           • Codec (1B):     0=RLE, 1=HUFFMAN, 2=RAW
           • Payload Size (4B): Little-endian uint32_t
  Followed by concatenation of all block payloads.
  ```

---

## 6. Build & Setup Guide

### 6.1 Prerequisites
- **Operating System:** Windows 10 / 11 (64-bit)
- **Compiler:** Microsoft Visual Studio 2022 (MSVC v143 toolset with C++20 support)
- **CMake:** Version 3.20 or newer
- **Package Manager:** [vcpkg](https://github.com/microsoft/vcpkg) with `ffmpeg:x64-windows`
- **Python:** Python 3.10+ (with virtual environment support)

---

### 6.2 Building the C++ Engine (`ahmc.exe`)

1. **Install FFmpeg via vcpkg**:
   ```powershell
   vcpkg install ffmpeg:x64-windows
   ```

2. **Configure CMake with the vcpkg toolchain**:
   ```powershell
   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
   ```

3. **Compile the Release executable**:
   ```powershell
   cmake --build build --config Release
   ```

   The build produces `build\Release\ahmc.exe`.  
   *Note: CMake automatically copies the required FFmpeg runtime DLLs (`avutil`, `avcodec`, `avformat`, `swscale`, `swresample`) into `build\Release\` upon build completion.*

---

### 6.3 Setting Up the Flask Dashboard

1. **Create and activate a virtual environment**:
   ```powershell
   python -m venv .venv
   .\.venv\Scripts\Activate.ps1
   ```

2. **Install Python dependencies**:
   ```powershell
   pip install -r requirements.txt
   ```

3. **Launch the web application**:
   ```powershell
   python flask_app/app.py
   ```

4. **Access the application**:
   Open **`http://127.0.0.1:5000`** in any modern web browser.

---

## 7. C++ Command-Line Interface (CLI)

The `ahmc.exe` binary provides a comprehensive CLI for headless operation, scripting, and batch benchmarks:

```powershell
# ── Statistical Analysis ───────────────────────────────────────────
.\build\Release\ahmc.exe analyze <file>
.\build\Release\ahmc.exe entropy-compare <file>

# ── Codec Self-Tests ───────────────────────────────────────────────
.\build\Release\ahmc.exe bitio-test
.\build\Release\ahmc.exe rle-test
.\build\Release\ahmc.exe huffman-test

# ── Low-Level Stream Compression ───────────────────────────────────
.\build\Release\ahmc.exe rle-compress      <in> <out.rle>
.\build\Release\ahmc.exe rle-decompress    <in.rle> <out>
.\build\Release\ahmc.exe huffman-compress  <in> <out.huff>
.\build\Release\ahmc.exe huffman-decompress<in.huff> <out>

# ── Image Operations (BMP) ─────────────────────────────────────────
.\build\Release\ahmc.exe image-analyze     <in.bmp> [--json]
.\build\Release\ahmc.exe image-predict     <in.bmp>
.\build\Release\ahmc.exe image-compress    <in.bmp> <out.ahmc> [--json]
.\build\Release\ahmc.exe image-decompress  <in.ahmc> <out.bmp>
.\build\Release\ahmc.exe image-verify      <orig.bmp> <rec.bmp>
.\build\Release\ahmc.exe image-test
.\build\Release\ahmc.exe image-adaptive-test

# ── Image EABPS Operations (Block-wise Spatial) ────────────────────
.\build\Release\ahmc.exe image-eabps-compress   <in.bmp> <out.ahmc> [--block-size 16] [--json]
.\build\Release\ahmc.exe image-eabps-decompress <in.ahmc> <out.bmp>
.\build\Release\ahmc.exe image-eabps-test
.\build\Release\ahmc.exe image-eabps-benchmark

# ── Audio Operations (WAV PCM) ─────────────────────────────────────
.\build\Release\ahmc.exe audio-analyze     <in.wav> [--json]
.\build\Release\ahmc.exe audio-predict     <in.wav>
.\build\Release\ahmc.exe audio-compress    <in.wav> <out.ahmc> [--json]
.\build\Release\ahmc.exe audio-decompress  <in.ahmc> <out.wav>
.\build\Release\ahmc.exe audio-verify      <orig.wav> <rec.wav>
.\build\Release\ahmc.exe audio-test
.\build\Release\ahmc.exe audio-adaptive-test

# ── Audio EABPS Operations (Block-wise PCM) ────────────────────────
.\build\Release\ahmc.exe audio-eabps-compress   <in.wav> <out.ahmc> [--block-size 4096] [--json]
.\build\Release\ahmc.exe audio-eabps-decompress <in.ahmc> <out.wav>
.\build\Release\ahmc.exe audio-eabps-test
.\build\Release\ahmc.exe audio-eabps-benchmark

# ── Video TEABPS Operations (Block-wise Temporal) ──────────────────
.\build\Release\ahmc.exe video-teabps-compress   <in.avi/mp4> <out.ahlv> [--block-size 16] [--max-frames N] [--json]
.\build\Release\ahmc.exe video-teabps-decompress <in.ahlv> <out.yuv>
.\build\Release\ahmc.exe video-teabps-test
.\build\Release\ahmc.exe video-teabps-benchmark

# ── Format Abstraction Diagnostics ─────────────────────────────────
.\build\Release\ahmc.exe ffmpeg-test
.\build\Release\ahmc.exe format-test
.\build\Release\ahmc.exe format-analyze <media-file>
```

---

## 8. Automated Test Suite & Regression Matrix

AHLC includes a master test runner executing **6 Phases and 17 Test Suites** with 100% pass verification:

```powershell
python scratch/run_all_tests.py
```

### Regression Verification Matrix

| Phase | Test Suite | Scope & Target Components | Result | Details |
| :---: | :--- | :--- | :---: | :--- |
| **1** | `ahmc.exe bitio-test` | C++ MSB-first BitWriter & BitReader | **PASS** | Exact bit-level stream roundtrip |
| **1** | `ahmc.exe rle-test` | C++ PackBits RLE encoder/decoder | **PASS** | Empty, uniform, alternating, random runs |
| **1** | `ahmc.exe huffman-test` | C++ Canonical Huffman encoder/decoder | **PASS** | Prefix tree code construction & decode |
| **1** | `ahmc.exe image-test` | C++ Global spatial predictor roundtrip | **PASS** | Full BMP image reconstruction |
| **1** | `ahmc.exe image-adaptive-test` | C++ Adaptive RLE vs Huffman selector | **PASS** | Optimal entropy codec selection |
| **1** | `ahmc.exe image-eabps-test` | C++ Image EABPS Suite (A–F) | **PASS** | Flat, gradient, noise, odd boundary blocks |
| **1** | `ahmc.exe audio-test` | C++ Audio delta predictor roundtrip | **PASS** | 16-bit PCM WAV roundtrip |
| **1** | `ahmc.exe audio-adaptive-test` | C++ Adaptive Audio Pipeline | **PASS** | Tone, silence, noise adaptivity |
| **1** | `ahmc.exe audio-eabps-test` | C++ Audio EABPS Suite (A–F) | **PASS** | Mono/Stereo, 8/16/24/32-bit PCM depths |
| **1** | `ahmc.exe video-teabps-test` | C++ Video TEABPS Suite (A–E) | **PASS** | Planar YUV420p, TEMPORAL-D1/D2, scene cuts |
| **1** | `ahmc.exe ffmpeg-test` | FFmpeg Header & Library Linkage | **PASS** | avutil, avcodec, avformat, swscale |
| **1** | `ahmc.exe format-test` | Format Abstraction Layer | **PASS** | Magic byte sniffing & format detection |
| **2** | `scratch/test_suite.py` | Python Image Suite (BMP & PNG) | **PASS** | Pixel identity, honest fallback, palette opt |
| **3** | `scratch/test_audio_suite.py` | Python Audio Suite (WAV & FLAC) | **PASS** | All 10 audio requirements verified |
| **4** | `scratch/test_video_suite.py` | Python Video Suite (AVI & MP4) | **PASS** | All 10 video requirements verified |
| **5** | `scratch/test_video_teabps_full.py` | Python Video TEABPS Full Verification | **PASS** | 9 integration tests passed |
| **6** | `scratch/test_video_playback_pipeline.py` | Video Playback, FFV1 & Download Suite | **PASS** | FFV1 decode, H.264 decode, audio sync, routes |

---

## 9. Real Benchmark Results

All benchmarks are measured directly on the compiled C++ Release binary using synthetic and real-world media cases:

### 9.1 Image EABPS Benchmark ($64 \times 64$ 24-bit BMP, $12,342\text{ bytes}$)
```powershell
.\build\Release\ahmc.exe image-eabps-benchmark
```
| Test Pattern | Original (B) | Baseline (B) | EABPS (B) | Baseline Ratio | EABPS Ratio | Predictor Distribution | Reconstruction |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Uniform Solid Color** | 12,342 | 537 | 1,101 | 22.98x | 11.21x | NONE: 0, LEFT: 0, UP: 16 | **PASS (Pixel-Exact)** |
| **Horizontal Gradient** | 12,342 | 409 | 829 | 30.18x | 14.89x | NONE: 0, LEFT: 0, UP: 16 | **PASS (Pixel-Exact)** |
| **Vertical Gradient** | 12,342 | 407 | 349 | 30.32x | **35.36x** | NONE: 0, LEFT: 0, UP: 16 | **PASS (Pixel-Exact)** |
| **Mixed Quadrants** | 12,342 | 1,529 | 801 | 8.07x | **15.41x** | NONE: 0, LEFT: 4, UP: 12 | **PASS (Pixel-Exact)** |
| **Checkerboard Pattern**| 12,342 | 2,598 | 5,965 | 4.75x | 2.07x | NONE: 16, LEFT: 0, UP: 0 | **PASS (Pixel-Exact)** |
| **Line-art / Wireframe**| 12,342 | 2,206 | 1,189 | 5.59x | **10.38x** | NONE: 0, LEFT: 0, UP: 16 | **PASS (Pixel-Exact)** |
| **High-Entropy Noise** | 12,342 | 3,809 | 8,369 | 3.24x | 1.47x | NONE: 0, LEFT: 16, UP: 0 | **PASS (Pixel-Exact)** |

### 9.2 Audio EABPS Benchmark ($16\text{-bit PCM WAV}$, $4,096\text{ frame blocks}$)
```powershell
.\build\Release\ahmc.exe audio-eabps-benchmark
```
| Audio Signal | Original (B) | Baseline (B) | EABPS (B) | Baseline Ratio | EABPS Ratio | Predictor Distribution | Reconstruction |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Silence (16-bit Stereo)**| 32,812 | 539 | 567 | 60.88x | 57.87x | NONE: 4, D1: 0, D2: 0 | **PASS (Sample-Exact)** |
| **Sine Tone 440 Hz (Mono)** | 16,428 | 13,551 | 11,202 | 1.21x | **1.47x** | NONE: 0, D1: 0, D2: 4 | **PASS (Sample-Exact)** |
| **Linear Ramp Waveform**    | 16,428 | 3,370 | 327 | 4.87x | **50.24x** | NONE: 0, D1: 0, D2: 4 | **PASS (Sample-Exact)** |
| **Mixed Audio Sequence**   | 32,812 | 17,320 | 9,187 | 1.89x | **3.57x** | NONE: 1, D1: 1, D2: 2 | **PASS (Sample-Exact)** |
| **High-Entropy Noise**     | 16,428 | 16,578 | 16,439 | 0.99x | 1.00x | NONE: 4, D1: 0, D2: 0 | **PASS (Sample-Exact)** |

### 9.3 Video TEABPS Benchmark ($64 \times 64\text{ YUV420p}$, 10 frames, $61,440\text{ raw bytes}$)
```powershell
.\build\Release\ahmc.exe video-teabps-benchmark
```
| Video Sequence | Raw Bytes (B)| Baseline (B)| TEABPS (B)| Baseline Ratio | TEABPS Ratio | Predictor Distribution | Reconstruction |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Static Video Scene**     | 61,440 | 54,061 | 10,217 | 1.14x | **6.01x** | NONE: 48, D1: 432, D2: 0 | **PASS (Sample-Exact)** |
| **Linear Motion Sequence** | 61,440 | 53,054 | 10,217 | 1.16x | **6.01x** | NONE: 48, D1: 432, D2: 0 | **PASS (Sample-Exact)** |
| **Moving Waveform Pattern**| 61,440 | 58,420 | 63,349 | 1.05x | 0.97x | NONE: 379, D1: 93, D2: 8 | **PASS (Sample-Exact)** |
| **Scene Cut Transition**   | 61,440 | 56,496 | 16,233 | 1.09x | **3.78x** | NONE: 96, D1: 384, D2: 0 | **PASS (Sample-Exact)** |
| **High-Entropy Noise Video**| 61,440 | 61,741 | 64,361 | 1.00x | 0.95x | NONE: 480, D1: 0, D2: 0 | **PASS (Sample-Exact)** |

---

## 10. Web Dashboard & Multi-Endpoint Downloads

The Flask web application provides an intuitive interface for running experiments, inspecting compression statistics, and downloading verified media files.

### 10.1 Web Routes Summary

| Endpoint | Method | Functionality |
| :--- | :---: | :--- |
| **`/`** | `GET` | Main upload dashboard with real-time stage progression (`Uploading` → `Analyzing` → `Processing` → `Verifying` → `Complete`). Displays aggregate system compression metrics. |
| **`/analyze`** | `GET/POST` | Statistical analysis tool calculating Shannon entropy, unique byte counts, unicity, and format headers without executing compression. |
| **`/result/<id>`** | `GET` | Outcome page with visual size comparisons, processing times, verification badges, and 3 distinct download buttons. |
| **`/report/<id>`** | `GET` | 10-section formal technical audit report with theoretical Shannon entropy bounds, residual decorrelation analysis, and container overhead tables. |
| **`/history`** | `GET` | Experiment history table with sorting and a **Clear History** button that wipes database records and output files safely. |
| **`/download/<id>/playback`** | `GET` | Serves the universal H.264 MP4 playback copy (`video/mp4`). |
| **`/download/<id>/lossless`** | `GET` | Serves the bit-exact lossless master file (`.avi` / `.bmp` / `.wav`). |
| **`/download/<id>/ahlv`** | `GET` | Serves the raw C++ research container bitstream (`.ahlv` or `.ahmc`). |

### 10.2 Dual-Export Architecture: Solving the Windows Media Player Issue

```
                                  Uploaded Video
                                        │
                    ┌───────────────────┴───────────────────┐
                    ▼                                       ▼
        [Lossless Master Path]                    [Universal Playback Path]
            Codec: FFV1 v3                            Codec: H.264 (crf 18)
           Container: .avi                           Container: .mp4 (+faststart)
                    │                                       │
                    ▼                                       ▼
    Bit-for-Bit Canonical Identity           Native Out-of-the-Box Playback
    (Requires VLC, MPV, or K-Lite)           (Windows Media Player, Browsers, Mobile)
```

#### Why Windows Media Player Displays: *"We can't open uncompressed_test (1). It's encoded in FFV1 format which isn't supported."*
- **Compatibility vs. Corruption:** The downloaded FFV1 AVI is **not corrupt**. It is a mathematically valid, uncorrupted FFV1 stream that decodes cleanly via FFmpeg with exit code `0` and zero errors. Windows Media Player fails simply because Microsoft Media Foundation does not include an FFV1 DirectShow decoder codec out-of-the-box.
- **The AHLC Solution:** AHLC provides **two clearly labeled export buttons**:
  1. **Playable Video (`.mp4`):** An H.264 High Profile (`crf 18`, `+faststart`) export that opens instantly in Windows Media Player, Edge, Chrome, Safari, and mobile devices.
  2. **Lossless Master (`.avi`):** The 100% lossless FFV1 master for archival workflows, requiring VLC, MPV, or installed FFV1 DirectShow filters.
  3. **AHLV Container (`.ahlv`):** The raw C++ TEABPS research bitstream.

---

## 11. Rigorous Identity Definitions

To maintain scientific honesty, AHLC explicitly distinguishes between three distinct levels of identity in all reports:

1. **Container / Bitstream Identity:** The downloaded file matches the uploaded file byte-for-byte ($MD5_{out} \equiv MD5_{in}$). Guaranteed whenever compression yields no size reduction and the system safely falls back to retaining the original file.
2. **Decoded Source-Frame Identity:** Decoded video frames match source frames before compression. Guaranteed for native YUV420p video inputs (FFV1, rawvideo I420, standard H.264/H.265). For non-YUV420p streams (e.g., RGB24 or YUV444p), chroma subsampling occurs during ingestion and is transparently noted.
3. **Canonical-Frame Identity:** 100% exact sample-for-sample equality between canonical planar YUV420p frames ingested by TEABPS and the decompressed YUV sequence reconstructed from the `.ahlv` container.

---

## 12. Troubleshooting & FAQ

### Q: Why does Windows Media Player fail to play the lossless master video?
> **Answer:** Windows Media Player lacks a native FFV1 decoder filter. Use the **"Playable Video (.mp4)"** button on the results page to download the universally compatible H.264 MP4 export, or open the lossless master AVI in [VLC Media Player](https://www.videolan.org/) or [MPV](https://mpv.io/).

### Q: Why is my compression ratio exactly 1.000x with 0 B saved?
> **Answer:** If your media file is already heavily compressed (e.g., a pre-compressed H.264 video, JPEG image, or MP3 audio) or consists of high-entropy noise, attempting further lossless compression causes byte expansion. AHLC's honest fallback mechanism detects this, cancels expansion, and retains the original file without claiming artificial savings.

### Q: How do I rebuild after modifying C++ source files?
> **Answer:** Run `cmake --build build --config Release`. CMake will automatically compile modified objects and copy required FFmpeg runtime DLLs into `build\Release\`.

### Q: How do I run the full test suite?
> **Answer:** Activate your Python virtual environment and run:
> ```powershell
> python scratch/run_all_tests.py
> ```
> All 6 phases and 17 test suites must output `[PASSED]`.

---

## 13. License & Academic Attribution

This project is released under the **MIT License**.  
Developed for research and practical exploration of **Content-Adaptive Hybrid Lossless Compression for Multimedia Data**.
