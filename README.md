# AHLC — Content-Adaptive Hybrid Lossless Compression

> **Research Project** · Final Year · C++20 · Flask Dashboard  
> Content-Adaptive Hybrid Lossless Compression for Multimedia Data  
> Using Spatial, Temporal, and Entropy-Aware Coding

---

## Overview

AHLC is a complete lossless multimedia compression research engine built in C++20,
with a professional Flask web dashboard for demonstration, analysis, and verification.

Supported media formats:
- **Images:** BMP (`.bmp`) and PNG (`.png`)
- **Audio:** WAV (`.wav`) and FLAC (`.flac`)

The engine automatically selects the best compression codec for each input by
analysing its statistical properties — no manual tuning required.

**Adaptive Multimedia Pipeline:**

```
Input Media (BMP / PNG / WAV / FLAC)
        ↓
  Format Abstraction / Decoding to Raw Data
  (BMP/PNG -> Raw pixels; WAV/FLAC -> Raw PCM samples)
        ↓
  Media-specific Predictor
  (Spatial left-neighbour for image, delta for audio)
        ↓
  Residual Stream
        ↓
  ┌─────────────┐    ┌──────────────┐
  │  RLE Codec  │    │ Huffman Codec│
  └──────┬──────┘    └──────┬───────┘
         └─────── compare ──┘
                    ↓
           Select smaller payload
                    ↓
           AHMC v2 Container / Optimized Format
                    ↓
         Decompress / Reconstruct Media
                    ↓
    Exact Lossless Verification (Sample / Pixel identical)
```

---

## Project Structure

```
AHLC/
│
├── CMakeLists.txt
├── README.md
├── requirements.txt               ← Python deps (Flask only)
│
├── src/
│   ├── main.cpp                   ← CLI dispatcher, --json mode for Flask
│   │
│   ├── analyzer/
│   │   ├── analyzer.hpp / .cpp    ← byte freq, Shannon entropy, file stats
│   │
│   ├── bitio/
│   │   ├── bit_writer.hpp / .cpp  ← MSB-first bit-stream writer
│   │   └── bit_reader.hpp / .cpp  ← MSB-first bit-stream reader
│   │
│   ├── rle/
│   │   ├── rle_encoder.hpp / .cpp ← packet RLE compressor (file + in-memory)
│   │   └── rle_decoder.hpp / .cpp ← packet RLE decompressor + file compare
│   │
│   ├── entropy/
│   │   └── huffman.hpp / .cpp     ← canonical Huffman encoder/decoder
│   │
│   ├── image/
│   │   ├── image.hpp / .cpp       ← BMPImage struct, BMP read/write, entropy
│   │   ├── image_predictor.hpp    ← spatial predictor + compress API
│   │   └── image_predictor.cpp    ← AHMC v2 compress/decompress + stats
│   │
│   └── audio/
│       ├── audio.hpp / .cpp       ← AudioBuffer, WAV read/write, entropy
│       ├── audio_predictor.hpp    ← delta predictor + compress API
│       └── audio_predictor.cpp    ← AHMC v2 compress/decompress + stats
│
├── flask_app/
│   ├── app.py                     ← Flask routes + security
│   ├── ahmc_runner.py             ← subprocess wrapper, JSON parser, timing
│   ├── database.py                ← SQLite persistence (stdlib)
│   ├── templates/
│   │   ├── base.html              ← sidebar layout, flash messages
│   │   ├── dashboard.html         ← stats cards + drag-drop upload
│   │   ├── analyze.html           ← entropy analysis page
│   │   ├── result.html            ← compression result + Chart.js charts
│   │   ├── history.html           ← experiment history table
│   │   └── report.html            ← 9-section research report
│   └── static/
│       ├── css/style.css          ← dark-mode dashboard theme
│       └── js/dashboard.js        ← drag-drop, progress animation
│
├── uploads/                       ← uploaded media (auto-created)
├── outputs/                       ← compressed + recovered files (auto-created)
│
└── build/
    └── Release/
        └── ahmc.exe               ← compiled C++ engine
```

---

## Completed Phases

| Phase | Name | Status |
|-------|------|--------|
| 1 | Project Foundation (CMake, MSVC, C++20) | ✅ Complete |
| 2 | Core Binary Analyzer (entropy, byte freq) | ✅ Complete |
| 3A | Bit-Level I/O (BitWriter / BitReader) | ✅ Complete |
| 3B | RLE Compression | ✅ Complete |
| 3C | Canonical Huffman Entropy Coder | ✅ Complete |
| 4A | BMP Image Support + Spatial Predictor | ✅ Complete |
| 4B | PNG Image Lossless Optimization | ✅ Complete |
| 5A | WAV Audio Support + Delta Predictor | ✅ Complete |
| 5B | Adaptive Codec Selection (RLE vs Huffman) | ✅ Complete |
| 6 | FFmpeg Integration & Format Abstraction Layer | ✅ Complete |
| 7 | WAV & FLAC Audio Compression & Flask Integration | ✅ Complete |

---

## Build (C++ Engine)

**Prerequisites:** Windows 11 · Visual Studio 2022 (MSVC v143) · CMake 3.20+ · C++20 · FFmpeg 9.0.2 (via vcpkg)

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="E:\Programs\vcpkg-ahcl\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
```

Output: `build\Release\ahmc.exe`

---

## Flask Dashboard

**Prerequisites:** Python 3.10+ · `.venv` virtual environment

### Install dependencies
```powershell
python -m venv .venv
.\.venv\Scripts\pip install -r requirements.txt
```

### Start the dashboard
```powershell
.\.venv\Scripts\python.exe flask_app/app.py
```

Open **http://127.0.0.1:5000**

### Dashboard pages

| URL | Page |
|-----|------|
| `/` | Main dashboard — stats cards + BMP, PNG, WAV, FLAC upload |
| `/analyze` | Analyze media (entropy, metadata) without compressing |
| `/result/<id>` | Compression result with metadata + Chart.js charts |
| `/history` | Full experiment history table |
| `/report/<id>` | 9-section research report + pipeline diagram |
| `/download/<id>` | Download valid audio/image file (`.bmp`, `.png`, `.wav`, or `.flac`) |

The Flask layer coordinates format handling and verification:
- For WAV: passes through the C++ AHLC audio compression engine (`audio-compress` / `audio-decompress`) and reconstructs standard PCM WAV.
- For FLAC: decodes FLAC to PCM -> processes through AHLC audio prediction & adaptive coding -> reconstructs PCM -> encodes to standard valid FLAC.
- Decoded samples and audio metadata are verified 100% losslessly (`soundfile` + C++ `audio-verify`).
- When input is already optimal (e.g. FLAC), honest reporting is displayed (0 B saved, 1.00x ratio, original retained).

---

## C++ CLI Reference

```powershell
# ── Generic Analysis ──────────────────────────────────────────────
.\build\Release\ahmc.exe analyze <file>

# ── Bit I/O ───────────────────────────────────────────────────────
.\build\Release\ahmc.exe bitio-test

# ── RLE ───────────────────────────────────────────────────────────
.\build\Release\ahmc.exe rle-compress   <in> <out>
.\build\Release\ahmc.exe rle-decompress <in> <out>
.\build\Release\ahmc.exe rle-verify     <original> <recovered>
.\build\Release\ahmc.exe rle-test

# ── Huffman ───────────────────────────────────────────────────────
.\build\Release\ahmc.exe huffman-compress   <in> <out>
.\build\Release\ahmc.exe huffman-decompress <in> <out>
.\build\Release\ahmc.exe huffman-verify     <original> <recovered>
.\build\Release\ahmc.exe huffman-test

# ── Image (BMP) ───────────────────────────────────────────────────
.\build\Release\ahmc.exe image-analyze      <input.bmp> [--json]
.\build\Release\ahmc.exe image-predict      <input.bmp>
.\build\Release\ahmc.exe image-compress     <input.bmp>  <output.ahmc> [--json]
.\build\Release\ahmc.exe image-decompress   <input.ahmc> <output.bmp>
.\build\Release\ahmc.exe image-verify       <original.bmp> <recovered.bmp>
.\build\Release\ahmc.exe image-test
.\build\Release\ahmc.exe image-adaptive-test

# ── Audio (WAV PCM) ───────────────────────────────────────────────
.\build\Release\ahmc.exe audio-analyze      <input.wav> [--json]
.\build\Release\ahmc.exe audio-predict      <input.wav>
.\build\Release\ahmc.exe audio-compress     <input.wav>  <output.ahmc> [--json]
.\build\Release\ahmc.exe audio-decompress   <input.ahmc> <output.wav>
.\build\Release\ahmc.exe audio-verify       <original.wav> <recovered.wav>
.\build\Release\ahmc.exe audio-test
.\build\Release\ahmc.exe audio-adaptive-test

# ── Codec Comparison ──────────────────────────────────────────────
.\build\Release\ahmc.exe entropy-compare <file>
```

The `--json` flag suppresses human-readable output and emits a single JSON object
to stdout — used internally by the Flask dashboard.

---

## AHMC v2 File Format

All compressed files use the AHMC v2 container:

| Offset | Size | Field |
|--------|------|-------|
| 0–3 | 4 B | Magic: `AHLC` |
| 4 | 1 B | Version: `2` |
| 5 | 1 B | Predictor type (1 = LEFT, 2 = DELTA) |
| 6 | 1 B | Codec ID (0 = RLE, 1 = Huffman) |
| 7–14 | 8 B | Original data byte count (uint64 LE) |
| 15–22 | 8 B | Compressed payload byte count (uint64 LE) |
| 23–30 | 8 B | Media-specific metadata (width/height or channels/rate) |
| 31–38 | 8 B | Additional metadata |
| 39+ | N B | Compressed payload |

**Compression ratio** is always calculated as:  
`original_input_file_size / actual_ahmc_file_size`  
(includes full container overhead — no inflation hiding)

---

## Algorithms Implemented

### Predictors
| Media | Predictor | Formula |
|-------|-----------|---------|
| Image | Left neighbour (per channel byte) | `residual = (current − left + 256) % 256` |
| Audio | Delta (previous sample, per channel) | `residual = (current − prev + 65536) % 65536` (16-bit) |

### Entropy Coders
| Codec | Type | Best for |
|-------|------|----------|
| RLE | PackBits-style (control byte + payload) | Repetitive / sparse residuals |
| Huffman | Canonical prefix coding, 256-symbol alphabet | Skewed byte distributions |

The engine runs **both** codecs on every residual stream and selects the smaller output.

---

## Test Suites

All test suites run self-contained without external test files:

```powershell
.\build\Release\ahmc.exe bitio-test          # Bit I/O round-trip
.\build\Release\ahmc.exe rle-test            # 7 RLE cases
.\build\Release\ahmc.exe huffman-test        # 10 Huffman cases (incl. binary file)
.\build\Release\ahmc.exe image-test          # 7 BMP round-trip cases
.\build\Release\ahmc.exe image-adaptive-test # Adaptive codec selection + verify
.\build\Release\ahmc.exe audio-test          # 7 WAV round-trip cases
.\build\Release\ahmc.exe audio-adaptive-test # Adaptive codec selection + verify
```

Expected: **ALL PASS** on all suites.

---

## Quick Example — Image

```powershell
# Compress a BMP losslessly
.\build\Release\ahmc.exe image-compress photo.bmp photo.ahmc

# Decompress
.\build\Release\ahmc.exe image-decompress photo.ahmc photo_restored.bmp

# Verify pixel-exact reconstruction
.\build\Release\ahmc.exe image-verify photo.bmp photo_restored.bmp
# → Lossless verification: PASS

# JSON output (used by Flask)
.\build\Release\ahmc.exe image-compress photo.bmp photo.ahmc --json
```

## Quick Example — Audio

```powershell
.\build\Release\ahmc.exe audio-compress   recording.wav recording.ahmc
.\build\Release\ahmc.exe audio-decompress recording.ahmc recording_restored.wav
.\build\Release\ahmc.exe audio-verify     recording.wav recording_restored.wav
# → Lossless verification: PASS
```

---

## Supported Formats

| Format | Read | Write | Notes |
|--------|------|-------|-------|
| BMP 24-bit | ✅ | ✅ | Top-down and bottom-up |
| BMP 32-bit | ✅ | ✅ | Alpha channel preserved |
| WAV PCM 16-bit mono | ✅ | ✅ | Any sample rate |
| WAV PCM 16-bit stereo | ✅ | ✅ | De-interleaved prediction |

---

## Compression Characteristics

| Input type | Typical ratio | Best codec |
|------------|---------------|------------|
| Uniform / solid-colour BMP | 10–100x | RLE |
| Smooth gradient BMP | 5–30x | RLE or Huffman |
| Natural photo BMP | 1.1–2.5x | Huffman |
| Silence / constant audio | 20–50x | RLE |
| Sine wave audio | 1.1–2.0x | Huffman |
| High-entropy (random) | < 1.0x | expansion noted |

Expansion is **never hidden** — a ratio < 1.0 and negative space-saved % are
reported correctly when the AHMC file is larger than the original.

---

## Security (Flask Dashboard)

- `werkzeug.utils.secure_filename()` on all uploads
- Extension allow-list: `.bmp` and `.wav` only
- `MAX_CONTENT_LENGTH = 100 MB`
- UUID-prefixed filenames (no path traversal)
- `subprocess` always uses argument lists — `shell=False`
- Uploads stored outside `templates/` and `static/`
- `ahmc.exe` is never passed user input through the shell

---

## Requirements

### C++ Engine
- Windows 11
- Visual Studio 2022 (MSVC v143, C++20)
- CMake 3.20+
- No external libraries

### Flask Dashboard
- Python 3.10+
- Flask 3.x (see `requirements.txt`)
- No other runtime dependencies — SQLite is Python stdlib
