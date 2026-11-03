# ============================================================
# AHLC — Build, Test & Deployment Command Reference
# ============================================================


# ============================================================
# SECTION 1 — BUILD (C++ Engine)
# ============================================================

# Quick Build (Automated):
#   Windows: .\build.bat  or  .\build.ps1

# Manual CMake Build:
# 1a. Configure CMake with vcpkg toolchain
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows

# PowerShell equivalent:
# cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows

# 1b. Compile the Release binary
cmake --build build --config Release

# Output binary location:
#   Windows: build\Release\ahmc.exe (or build\ahmc.exe)
#   Linux/macOS: build/ahmc


# ============================================================
# SECTION 2 — REGRESSION TEST SUITE
# Run all suites after building to confirm complete compliance.
# ============================================================

# One-Click Test Runner:
#   .\test.bat  or  .\test.ps1

# Master Test Runner (all 6 phases):
.\.venv\Scripts\python.exe scratch\run_all_tests.py

# Individual C++ Unit Tests:
.\build\Release\ahmc.exe bitio-test             # BitWriter / BitReader round-trip
.\build\Release\ahmc.exe rle-test               # RLE compression & decompression
.\build\Release\ahmc.exe huffman-test           # Canonical Huffman encoder & decoder
.\build\Release\ahmc.exe image-test             # BMP image round-trip
.\build\Release\ahmc.exe image-adaptive-test    # BMP adaptive codec selection
.\build\Release\ahmc.exe image-eabps-test       # BMP EABPS block evaluation
.\build\Release\ahmc.exe audio-test             # WAV audio round-trip
.\build\Release\ahmc.exe audio-adaptive-test    # WAV adaptive codec selection
.\build\Release\ahmc.exe audio-eabps-test       # WAV EABPS block evaluation
.\build\Release\ahmc.exe video-teabps-test      # Video TEABPS planar YUV suite
.\build\Release\ahmc.exe ffmpeg-test            # FFmpeg library integration
.\build\Release\ahmc.exe format-test            # Format abstraction layer

# Individual Python Test Suites:
.\.venv\Scripts\python.exe scratch\test_suite.py        # BMP & PNG image suite
.\.venv\Scripts\python.exe scratch\test_audio_suite.py  # WAV & FLAC audio suite
.\.venv\Scripts\python.exe scratch\test_video_suite.py  # AVI & MP4 video suite


# ============================================================
# SECTION 3 — IMAGE WORKFLOW (BMP)
# ============================================================

# 3a. Analyze a BMP — entropy, dimensions, unique bytes
.\build\Release\ahmc.exe image-analyze .\sample.bmp

# 3b. Preview spatial prediction effect (entropy before vs after)
.\build\Release\ahmc.exe image-predict .\sample.bmp

# 3c. Compress — adaptive (RLE vs Huffman), outputs AHMC v2
.\build\Release\ahmc.exe image-compress .\sample.bmp .\sample.ahmc

# 3d. Decompress
.\build\Release\ahmc.exe image-decompress .\sample.ahmc .\sample_recovered.bmp

# 3e. Verify pixel-exact lossless reconstruction
.\build\Release\ahmc.exe image-verify .\sample.bmp .\sample_recovered.bmp

# 3f. EABPS Block-wise compression
.\build\Release\ahmc.exe image-eabps-compress .\sample.bmp .\sample_eabps.ahmc --block-size 16 --json
.\build\Release\ahmc.exe image-eabps-decompress .\sample_eabps.ahmc .\sample_eabps_rec.bmp


# ============================================================
# SECTION 4 — AUDIO WORKFLOW (WAV PCM)
# ============================================================

# 4a. Analyze a WAV — entropy, channels, sample rate, duration
.\build\Release\ahmc.exe audio-analyze .\sample.wav

# 4b. Preview delta prediction effect
.\build\Release\ahmc.exe audio-predict .\sample.wav

# 4c. Compress — adaptive (RLE vs Huffman), outputs AHMC v2
.\build\Release\ahmc.exe audio-compress .\sample.wav .\sample.ahmc

# 4d. Decompress
.\build\Release\ahmc.exe audio-decompress .\sample.ahmc .\sample_recovered.wav

# 4e. Verify sample-exact lossless reconstruction
.\build\Release\ahmc.exe audio-verify .\sample.wav .\sample_recovered.wav

# 4f. Audio EABPS Block-wise compression
.\build\Release\ahmc.exe audio-eabps-compress .\sample.wav .\sample_audio_eabps.ahmc --block-size 1024 --json
.\build\Release\ahmc.exe audio-eabps-decompress .\sample_audio_eabps.ahmc .\sample_audio_rec.wav


# ============================================================
# SECTION 5 — VIDEO WORKFLOW (AVI & MP4)
# ============================================================

# 5a. Probe video container, streams, resolution, fps, audio
ffprobe -v quiet -print_format json -show_format -show_streams .\sample.avi
ffprobe -v quiet -print_format json -show_format -show_streams .\sample.mp4

# 5b. Video TEABPS multi-threaded block compression (AHLV container)
.\build\Release\ahmc.exe video-teabps-compress .\sample.avi .\sample.ahlv --block-size 16 --json
.\build\Release\ahmc.exe video-teabps-decompress .\sample.ahlv .\sample_rec.yuv

# 5c. Fast zero-disk streaming frame verification via framemd5
ffmpeg -v quiet -i .\sample.avi -pix_fmt bgr24 -f framemd5 -
ffmpeg -v quiet -i .\sample_lossless.avi -pix_fmt bgr24 -f framemd5 -


# ============================================================
# SECTION 6 — STANDALONE CODECS & ANALYZER
# ============================================================

# RLE PackBits on any binary file:
.\build\Release\ahmc.exe rle-compress   .\input.bin .\input.rle
.\build\Release\ahmc.exe rle-decompress .\input.rle .\input_rec.bin
.\build\Release\ahmc.exe rle-verify     .\input.bin .\input_rec.bin

# Canonical Huffman on any binary file:
.\build\Release\ahmc.exe huffman-compress   .\input.bin .\input.huff
.\build\Release\ahmc.exe huffman-decompress .\input.huff .\input_rec.bin
.\build\Release\ahmc.exe huffman-verify     .\input.bin .\input_rec.bin

# Generic Shannon Entropy Analyzer:
.\build\Release\ahmc.exe analyze .\sample.bin


# ============================================================
# SECTION 7 — FLASK DASHBOARD (Web Application)
# ============================================================

# Quick Launch:
#   .\run.bat  or  .\run.ps1

# Manual Launch:
# 1. Set up virtual environment
python -m venv .venv
.\.venv\Scripts\pip install -r requirements.txt

# 2. Run application
.\.venv\Scripts\python.exe flask_app\app.py
# URL: http://127.0.0.1:5000


# ============================================================
# SECTION 8 — FULL END-TO-END DEMO SEQUENCE
# ============================================================

# Step 1: Build engine
.\build.bat

# Step 2: Run all regression tests
.\test.bat

# Step 3: Launch web UI
.\run.bat
