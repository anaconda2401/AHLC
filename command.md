# ============================================================
# AHLC — Build, Test & Deployment Command Reference
# Project root: D:\projects\compression\AHLC
# ============================================================


# ============================================================
# SECTION 1 — BUILD (C++ Engine)
# ============================================================

# Working directory for all C++ commands:
#   cd D:\projects\compression\AHLC

# 1a. Generate CMake build system with vcpkg toolchain (run once, or after CMakeLists.txt changes)
E:\Programs\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build -DCMAKE_TOOLCHAIN_FILE="E:\Programs\vcpkg-ahcl\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows

# 1b. Compile the Release binary
E:\Programs\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build build --config Release

# Output: D:\projects\compression\AHLC\build\Release\ahmc.exe


# ============================================================
# SECTION 2 — REGRESSION TEST SUITE
# Run all suites after every build to confirm nothing is broken.
# Expected result: ALL PASS on every suite.
# ============================================================

# 2a. Bit-level I/O (BitWriter / BitReader round-trip)
.\build\Release\ahmc.exe bitio-test

# 2b. RLE compression / decompression (7 test cases)
.\build\Release\ahmc.exe rle-test

# 2c. Canonical Huffman encoder / decoder (10 test cases)
.\build\Release\ahmc.exe huffman-test

# 2d. BMP image round-trip (7 synthetic images)
.\build\Release\ahmc.exe image-test

# 2e. BMP adaptive codec selection + verification
.\build\Release\ahmc.exe image-adaptive-test

# 2f. WAV audio round-trip (7 synthetic signals)
.\build\Release\ahmc.exe audio-test

# 2g. WAV adaptive codec selection + verification
.\build\Release\ahmc.exe audio-adaptive-test

# 2h. FFmpeg library integration test
.\build\Release\ahmc.exe ffmpeg-test

# 2i. Format abstraction layer test
.\build\Release\ahmc.exe format-test

# 2j. Python Automated Regression Suites (BMP, PNG, WAV, FLAC, AVI, MP4)
.\.venv\Scripts\python.exe scratch\test_suite.py
.\.venv\Scripts\python.exe scratch\test_audio_suite.py
.\.venv\Scripts\python.exe scratch\test_video_suite.py
.\.venv\Scripts\python.exe scratch\run_all_tests.py


# ============================================================
# SECTION 3 — IMAGE WORKFLOW (BMP)
# Replace sample.bmp with your actual file path.
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
# Expected output: Lossless verification: PASS

# 3f. Compare RLE vs Huffman on any file
.\build\Release\ahmc.exe entropy-compare .\sample.bmp

# 3g. Machine-readable JSON output (used by Flask dashboard)
.\build\Release\ahmc.exe image-analyze  .\sample.bmp  --json
.\build\Release\ahmc.exe image-compress .\sample.bmp .\sample.ahmc --json


# ============================================================
# SECTION 4 — AUDIO WORKFLOW (WAV PCM)
# Replace sample.wav with your actual file path.
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
# Expected output: Lossless verification: PASS

# 4f. Machine-readable JSON output
.\build\Release\ahmc.exe audio-analyze  .\sample.wav --json
.\build\Release\ahmc.exe audio-compress .\sample.wav .\sample.ahmc --json


# ============================================================
# SECTION 5 — RLE STANDALONE (any binary file)
# ============================================================

.\build\Release\ahmc.exe rle-compress   .\input.bin .\input.rle
.\build\Release\ahmc.exe rle-decompress .\input.rle .\input_recovered.bin
.\build\Release\ahmc.exe rle-verify     .\input.bin .\input_recovered.bin


# ============================================================
# SECTION 6 — HUFFMAN STANDALONE (any binary file)
# ============================================================

.\build\Release\ahmc.exe huffman-compress   .\input.bin .\input.huff
.\build\Release\ahmc.exe huffman-decompress .\input.huff .\input_recovered.bin
.\build\Release\ahmc.exe huffman-verify     .\input.bin .\input_recovered.bin


# ============================================================
# SECTION 7 — GENERIC BINARY ANALYZER
# ============================================================

# Analyze any file (byte frequency, Shannon entropy, most-frequent byte)
.\build\Release\ahmc.exe analyze .\build\Release\ahmc.exe
.\build\Release\ahmc.exe analyze .\sample.bmp
.\build\Release\ahmc.exe analyze .\sample.wav


# ============================================================
# SECTION 8 — FLASK DASHBOARD (Python)
# Working directory: D:\projects\compression\AHLC
# ============================================================

# 8a. Create virtual environment (first time only)
python -m venv .venv

# 8b. Install Python dependencies (first time only)
.\.venv\Scripts\pip install -r requirements.txt

# 8c. Start the dashboard
.\.venv\Scripts\python.exe flask_app\app.py
# Dashboard URL: http://127.0.0.1:5000

# 8d. Alternative: set environment variable and run
$env:FLASK_APP = "flask_app/app.py"; .\.venv\Scripts\python.exe -m flask run


# ============================================================
# SECTION 9 — FLASK DASHBOARD PAGES
# ============================================================

# Main dashboard (stats + upload)
# http://127.0.0.1:5000/

# Analyze a file without compressing
# http://127.0.0.1:5000/analyze

# View all past experiments
# http://127.0.0.1:5000/history

# View result for experiment ID 1
# http://127.0.0.1:5000/result/1

# View full 9-section research report for experiment ID 1
# http://127.0.0.1:5000/report/1

# Download valid compressed file (.bmp, .png, .wav, .flac, .avi, or .mp4) for experiment ID 1
# http://127.0.0.1:5000/download/1


# ============================================================
# SECTION 10 — VIDEO WORKFLOW (AVI & MP4)
# Demonstrates FFmpeg integration, candidate codecs, and frame verification.
# ============================================================

# 10a. Probe video container, streams, resolution, fps, and audio tracks
ffprobe -v quiet -print_format json -show_format -show_streams .\sample.avi
ffprobe -v quiet -print_format json -show_format -show_streams .\sample.mp4

# 10b. Compress uncompressed AVI to lossless FFV1 with audio copy
ffmpeg -y -i .\sample.avi -c:v ffv1 -level 3 -c:a copy .\sample_lossless.avi

# 10c. Extract decoded raw pixel frames for bit-level verification
ffmpeg -y -i .\sample.avi -f rawvideo -pix_fmt bgr24 .\raw_orig.bin
ffmpeg -y -i .\sample_lossless.avi -f rawvideo -pix_fmt bgr24 .\raw_rec.bin

# 10d. Verify exact byte-level identity of decoded frames
fc /b .\raw_orig.bin .\raw_rec.bin
# Expected output: FC: no differences encountered

# 10e. Run full 10-requirement automated video test suite
.\.venv\Scripts\python.exe scratch\test_video_suite.py


# ============================================================
# SECTION 11 — FULL END-TO-END DEMO SEQUENCE
# Run this sequence for a live project demonstration.
# ============================================================

# Step 1: Build
E:\Programs\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build build --config Release

# Step 2: Run all regression tests (C++, Image, Audio, Video)
.\.venv\Scripts\python.exe scratch\run_all_tests.py

# Step 3: Demonstrate image compression
.\build\Release\ahmc.exe image-analyze  .\sample.bmp
.\build\Release\ahmc.exe image-compress .\sample.bmp .\sample.ahmc
.\build\Release\ahmc.exe image-decompress .\sample.ahmc .\sample_recovered.bmp
.\build\Release\ahmc.exe image-verify   .\sample.bmp .\sample_recovered.bmp

# Step 4: Demonstrate audio compression
.\build\Release\ahmc.exe audio-analyze  .\sample.wav
.\build\Release\ahmc.exe audio-compress .\sample.wav .\sample_audio.ahmc
.\build\Release\ahmc.exe audio-decompress .\sample_audio.ahmc .\sample_recovered.wav
.\build\Release\ahmc.exe audio-verify   .\sample.wav .\sample_recovered.wav

# Step 5: Start Flask dashboard for visual demonstration (BMP, PNG, WAV, FLAC, AVI, MP4)
.\.venv\Scripts\python.exe flask_app\app.py
# Then open: http://127.0.0.1:5000
