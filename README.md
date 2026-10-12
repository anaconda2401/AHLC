# AHLC — Adaptive Hybrid Lossless Compression

> **Academic Project** · Advanced Data Compression Techniques · C++20 · Flask Web Dashboard  
> **Content-Adaptive Hybrid Lossless Compression for Multimedia Data (Images, Audio, Video)**  
> Integrating Spatial, Temporal, and Entropy-Aware Coding with Bit-Exact Verification

---

## 1. Overview

**AHLC (Adaptive Hybrid Lossless Compression)** is an engineering and research-oriented multimedia compression system implemented in **C++20**, accompanied by an interactive **Python Flask** web dashboard.

The system decouples format decoding from core entropy coding via an extensible **Format Abstraction Layer**, evaluates statistical data properties (Shannon entropy and byte frequency), applies domain-specific predictive decorrelation (spatial, delta, and inter-frame temporal differencing), and dynamically selects the best compression codec between **Packet RLE** and **Canonical Huffman Coding**.

### Supported Media Formats

| Media Category | Formats | Processing & Packaging Method | Lossless Verification |
| :--- | :--- | :--- | :--- |
| **Images** | **BMP** (`.bmp`)<br>**PNG** (`.png`) | Spatial left-neighbour prediction + Adaptive RLE/Huffman (AHMC v2); Lossless DEFLATE L9 + adaptive filtering for PNG. | **Pixel-identical** (100% RGB/RGBA match) |
| **Audio** | **WAV** (`.wav`)<br>**FLAC** (`.flac`) | De-interleaved channel delta prediction + Adaptive RLE/Huffman; Lossless FLAC stream decoding & reconstruction. | **Sample-identical** (100% PCM sample match) |
| **Video** | **AVI** (`.avi`)<br>**MP4** (`.mp4`) | Intra-frame spatial + inter-frame temporal residual coding; Lossless FFV1 in AVI container; Honest retention fallback for pre-compressed H.264 MP4. | **Decoded-frame identical** (100% frame hash match) |

### Core Philosophy: Honest Reporting & Bit-Exact Verification

1. **No Artificial Savings:** If an input file is already compressed or high-entropy (such that compression causes expansion), the system honestly reports the candidate sizes, triggers a fallback to retain the original file, and displays 0 B saved, 1.00x compression ratio, and zero negative container overhead.
2. **Mathematical Verification:** Every compression operation is subjected to a mandatory round-trip verification pass. Recovered media is compared against original raw pixels, PCM audio samples, or decoded video frames.
3. **Audio Preservation:** Video files containing audio tracks automatically preserve audio streams losslessly via streamcopy (`-c:a copy`).

---

## 2. Architecture & Pipeline

```
                                  Input Media File
                        (BMP, PNG, WAV, FLAC, AVI, MP4)
                                         ↓
                     ┌───────────────────────────────────────┐
                     │   Format Abstraction Layer (C++)      │
                     │         Format Detection              │
                     └──────────────────┬────────────────────┘
                                        ↓
                       Common Raw Media Representations
                     ┌──────────────────┼────────────────────┐
                     ↓                  ↓                    ↓
                ImageFrame         AudioBuffer           VideoFrame
              (W × H, BPP,        (Channels, SR,       (W × H, FPS,
              pixel buffer)        sample buffer)      frame buffers)
                     ↓                  ↓                    ↓
                     └──────────────────┼────────────────────┘
                                        ↓
                     ┌───────────────────────────────────────┐
                     │       Domain-Specific Predictor       │
                     │  • Image: Left-neighbour spatial      │
                     │  • Audio: Previous-sample delta       │
                     │  • Video: Spatial + Inter-frame delta │
                     └──────────────────┬────────────────────┘
                                        ↓
                                 Residual Stream
                                        ↓
                     ┌──────────────────┴────────────────────┐
                     ↓                                       ↓
           Packet RLE Compressor                  Canonical Huffman Coder
           (Byte-run PackBits)                    (256-symbol prefix tree)
                     ↓                                       ↓
                     └──────────────────┬────────────────────┘
                                        ↓
                     ┌───────────────────────────────────────┐
                     │     Adaptive Decision & Fallback      │
                     │      min(RLE, Huffman, Original)      │
                     └──────────────────┬────────────────────┘
                                        ↓
                     ┌───────────────────────────────────────┐
                     │     Container Packaging & Storage     │
                     │  • AHMC v2 container (Images / Audio) │
                     │  • FFV1 Lossless AVI / MP4 Retention  │
                     └──────────────────┬────────────────────┘
                                        ↓
                     ┌───────────────────────────────────────┐
                     │      Lossless Verification Layer      │
                     │   Sample / Pixel / Frame Equality     │
                     └──────────────────┬────────────────────┘
                                        ↓
                         Flask Dashboard & Report Engine
                       (Download, Charts, Technical Audit)
```

---

## 3. Repository Structure

```
AHLC/
├── CMakeLists.txt                 # C++20 CMake build configuration & FFmpeg linkage
├── vcpkg.json                     # vcpkg manifest declaring FFmpeg dependency
├── requirements.txt               # Python backend dependencies (Flask, Pillow, soundfile, numpy)
├── README.md                      # Complete system documentation
├── .gitignore                     # Repository ignore patterns for build & media artifacts
│
├── src/                           # C++ Compression Engine (ahmc)
│   ├── main.cpp                   # CLI dispatcher & command router
│   ├── analyzer/                  # Shannon entropy, byte frequency analysis
│   │   ├── analyzer.hpp
│   │   └── analyzer.cpp
│   ├── bitio/                     # MSB-first bit-level stream I/O
│   │   ├── bit_writer.hpp / .cpp
│   │   └── bit_reader.hpp / .cpp
│   ├── rle/                       # Byte-level PackBits Run-Length Encoding
│   │   ├── rle_encoder.hpp / .cpp
│   │   └── rle_decoder.hpp / .cpp
│   ├── entropy/                   # Canonical Huffman prefix coding
│   │   ├── huffman.hpp
│   │   └── huffman.cpp
│   ├── image/                     # BMP read/write, spatial left predictor, AHMC packaging
│   │   ├── image.hpp / .cpp
│   │   ├── image_predictor.hpp
│   │   └── image_predictor.cpp
│   ├── audio/                     # WAV PCM read/write, delta predictor, AHMC packaging
│   │   ├── audio.hpp / .cpp
│   │   ├── audio_predictor.hpp
│   │   └── audio_predictor.cpp
│   └── media/                     # Unified Format Abstraction Layer
│       ├── media_defs.hpp         # Media types, formats, channel layouts
│       ├── decoder_interface.hpp  # Abstract decoder base class
│       ├── image_frame.hpp        # Uncompressed image pixel representation
│       ├── audio_buffer.hpp       # Uncompressed PCM audio representation
│       ├── video_frame.hpp        # Raw video frame sequence representation
│       ├── format_detector.hpp    # Format signature detection & probing
│       └── format_detector.cpp
│
├── flask_app/                     # Web Application Backend & Frontend
│   ├── app.py                     # Flask routes, error handlers, upload security
│   ├── ahmc_runner.py             # Subprocess wrapper, media conversion, timing
│   ├── database.py                # SQLite persistence (experiments table & history)
│   ├── static/
│   │   ├── css/style.css          # Clean technical UI design with Light/Dark mode
│   │   └── js/dashboard.js        # Drag-and-drop, progress animation, theme switcher
│   └── templates/
│       ├── base.html              # Base layout with sidebar and theme state
│       ├── dashboard.html         # Upload zone, progress steps, summary metrics
│       ├── analyze.html           # Entropy inspection & metadata analysis
│       ├── result.html            # Metrics, verification badge, Chart.js graphs
│       ├── history.html           # Session table with Clear History functionality
│       └── report.html            # 9-section structured technical report
│
├── scratch/                       # Test Suites & Verification Runners
│   ├── run_all_tests.py           # Master test runner (Phases 1–4)
│   ├── test_suite.py              # Python Image Suite (BMP & PNG)
│   ├── test_audio_suite.py        # Python Audio Suite (WAV & FLAC — 10 tests)
│   └── test_video_suite.py        # Python Video Suite (AVI & MP4 — 12 tests)
│
├── uploads/                       # Runtime uploaded files (git-ignored, kept via .gitkeep)
└── outputs/                       # Runtime compressed & recovered files (git-ignored)
```

---

## 4. Build & Setup Guide

### 4.1 Prerequisites

- **Operating System:** Windows 10 / 11 (64-bit)
- **C++ Compiler:** Microsoft Visual Studio 2022 (MSVC v143, C++20 support)
- **CMake:** Version 3.20 or newer
- **Package Manager:** [vcpkg](https://github.com/microsoft/vcpkg) with `ffmpeg` installed
- **Python:** Python 3.10+ (with virtual environment support)

---

### 4.2 Building the C++ Engine (`ahmc.exe`)

1. **Install FFmpeg via vcpkg** (if not already installed):
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
   *Note: CMake automatically copies the required FFmpeg runtime DLLs (`avutil`, `avcodec`, `avformat`, `swscale`, `swresample`) into `build\Release\` upon completion.*

---

### 4.3 Setting Up the Flask Dashboard

1. **Create and activate a Python virtual environment**:
   ```powershell
   python -m venv .venv
   .\.venv\Scripts\Activate.ps1
   ```

2. **Install Python dependencies**:
   ```powershell
   pip install -r requirements.txt
   ```

3. **Start the Flask development server**:
   ```powershell
   python flask_app/app.py
   ```

4. **Access the application**:
   Open **`http://127.0.0.1:5000`** in any web browser.

---

## 5. Web Dashboard Features

| Route | Feature Description |
| :--- | :--- |
| **`/` (Dashboard)** | Drag-and-drop file upload with animated stage indicator (`Uploading` → `Analyzing` → `Processing` → `Verifying` → `Complete`). Displays aggregate system statistics and recent experiments. |
| **`/analyze`** | Statistical analysis page. Calculates Shannon entropy, byte frequency distribution, and format metadata without executing compression. |
| **`/result/<id>`** | Compression outcome page. Displays original vs. compressed size, bytes saved, compression ratio, processing time, Chart.js visual comparisons, and direct download (`Download <Format> File`). |
| **`/history`** | Comprehensive experiment history table with sorting. Includes a **Clear History** button with confirmation prompt to safely wipe past sessions and output files. |
| **`/report/<id>`** | 9-section formal technical compression report detailing theoretical entropy limits, residual decorrelation, container overhead layouts, and verification proofs. |
| **`/download/<id>`** | Secure download endpoint serving the recovered, bit-verified media file. |

### UI Design & Theme System
- **Theme Toggle:** Switch between a sleek, high-contrast dark theme and a clean, legible light theme. State is saved in `localStorage`.
- **Demo Guard:** Video uploads are restricted to a configurable 50 MB limit (`AHLC_MAX_VIDEO_MB=50`) to ensure fast, responsive demonstrations.

---

## 6. C++ Command-Line Interface (CLI)

The `ahmc.exe` binary can be executed directly for headless processing and integration testing:

```powershell
# ── Statistical Analysis ───────────────────────────────────────────
.\build\Release\ahmc.exe analyze <file>
.\build\Release\ahmc.exe entropy-compare <file>

# ── Low-Level Codec Testing ────────────────────────────────────────
.\build\Release\ahmc.exe bitio-test
.\build\Release\ahmc.exe rle-test
.\build\Release\ahmc.exe huffman-test

# ── Packet RLE Operations ──────────────────────────────────────────
.\build\Release\ahmc.exe rle-compress   <input> <output.rle>
.\build\Release\ahmc.exe rle-decompress <input.rle> <output>
.\build\Release\ahmc.exe rle-verify     <original> <recovered>

# ── Canonical Huffman Operations ───────────────────────────────────
.\build\Release\ahmc.exe huffman-compress   <input> <output.huff>
.\build\Release\ahmc.exe huffman-decompress <input.huff> <output>
.\build\Release\ahmc.exe huffman-verify     <original> <recovered>

# ── Image Operations (BMP) ─────────────────────────────────────────
.\build\Release\ahmc.exe image-analyze      <input.bmp> [--json]
.\build\Release\ahmc.exe image-predict      <input.bmp>
.\build\Release\ahmc.exe image-compress     <input.bmp> <output.ahmc> [--json]
.\build\Release\ahmc.exe image-decompress   <input.ahmc> <output.bmp>
.\build\Release\ahmc.exe image-verify       <original.bmp> <recovered.bmp>
.\build\Release\ahmc.exe image-test
.\build\Release\ahmc.exe image-adaptive-test

# ── Audio Operations (WAV PCM) ─────────────────────────────────────
.\build\Release\ahmc.exe audio-analyze      <input.wav> [--json]
.\build\Release\ahmc.exe audio-predict      <input.wav>
.\build\Release\ahmc.exe audio-compress     <input.wav> <output.ahmc> [--json]
.\build\Release\ahmc.exe audio-decompress   <input.ahmc> <output.wav>
.\build\Release\ahmc.exe audio-verify       <original.wav> <recovered.wav>
.\build\Release\ahmc.exe audio-test
.\build\Release\ahmc.exe audio-adaptive-test

# ── FFmpeg & Format Abstraction Diagnostics ────────────────────────
.\build\Release\ahmc.exe ffmpeg-test
.\build\Release\ahmc.exe format-test
.\build\Release\ahmc.exe format-analyze <media-file>
```

*Note: The `--json` flag suppresses console text and outputs a machine-readable JSON string used by the Flask backend.*

---

## 7. AHMC v2 Container Specification

Custom compressed media artifacts utilize the **AHMC v2** binary container format:

| Offset (Bytes) | Field Name | Type | Description |
| :---: | :---: | :---: | :--- |
| `0x00 – 0x03` | Magic | `char[4]` | Container identifier (`"AHLC"`) |
| `0x04` | Version | `uint8_t` | Container version (`2`) |
| `0x05` | Predictor | `uint8_t` | Predictor applied (`1` = Left Spatial, `2` = Delta) |
| `0x06` | Codec ID | `uint8_t` | Selected codec (`0` = Packet RLE, `1` = Canonical Huffman) |
| `0x07 – 0x0E` | Original Size | `uint64_t` | Uncompressed payload size in bytes (Little Endian) |
| `0x0F – 0x16` | Payload Size | `uint64_t` | Compressed payload byte count (Little Endian) |
| `0x17 – 0x1E` | Metadata 1 | `uint64_t` | Width & Height (Image) or Channels & Sample Rate (Audio) |
| `0x1F – 0x26` | Metadata 2 | `uint64_t` | Bits per sample / Color depth |
| `0x27+` | Payload | `uint8_t[]` | Encoded residual stream bitstream |

---

## 8. Automated Test Suite

A comprehensive master regression test runner is provided to verify the complete system across all media types:

```powershell
python scratch/run_all_tests.py
```

### Verification Phases

```
======================================================================
   AHLC MASTER TEST RUNNER — COMPLETE SYSTEM VERIFICATION
======================================================================

--- Phase 1: C++ Engine Regression Tests ---
>> Running: C++ Bit I/O Test                 [PASSED]
>> Running: C++ RLE Compression Test         [PASSED]
>> Running: C++ Huffman Coding Test          [PASSED]
>> Running: C++ Image Round-Trip Test        [PASSED]
>> Running: C++ Adaptive Image Test          [PASSED]
>> Running: C++ Audio Round-Trip Test        [PASSED]
>> Running: C++ Adaptive Audio Test          [PASSED]
>> Running: C++ FFmpeg Integration Test      [PASSED]
>> Running: C++ Format Abstraction Test      [PASSED]

--- Phase 2: Python Image Tests (BMP & PNG) ---
>> Running: Image Test Suite (BMP & PNG)     [PASSED]

--- Phase 3: Python Audio Tests (WAV & FLAC - 10 Requirements) ---
>> Running: Audio Test Suite (WAV & FLAC)    [PASSED]

--- Phase 4: Python Video Tests (AVI & MP4 - 12 Requirements) ---
>> Running: Video Test Suite (AVI & MP4)     [PASSED]

======================================================================
   ALL TESTS PASSED! FULL SYSTEM COMPLIANT & VERIFIED.
======================================================================
```
