"""
run_all_tests.py
Master test runner for AHLC Multimedia Lossless Compression (BMP, PNG, WAV, FLAC).
Executes:
1. C++ Engine Regression Suites (BitIO, RLE, Huffman, Image, Image-Adaptive, Audio, Audio-Adaptive, FFmpeg, Format-Detection)
2. Image Compression Test Suite (BMP & PNG)
3. Audio Compression Test Suite (WAV & FLAC: all 10 requirements)
"""
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(r"d:\projects\compression\AHLC").resolve()
PYTHON_EXE = sys.executable
AHMC_EXE = PROJECT_ROOT / "build" / "Release" / "ahmc.exe"


def run_cmd(args, desc):
    print(f"\n>> Running: {desc}")
    res = subprocess.run(args, cwd=str(PROJECT_ROOT), capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[FAILED] {desc}")
        print("STDOUT:\n", res.stdout)
        print("STDERR:\n", res.stderr)
        return False
    print(f"[PASSED] {desc}")
    return True


def main():
    print("======================================================================")
    print("   AHLC MASTER TEST RUNNER — COMPLETE SYSTEM VERIFICATION")
    print("======================================================================")

    cpp_tests = [
        ("bitio-test", "C++ Bit I/O Test"),
        ("rle-test", "C++ RLE Compression Test"),
        ("huffman-test", "C++ Huffman Coding Test"),
        ("image-test", "C++ Image Round-Trip Test"),
        ("image-adaptive-test", "C++ Adaptive Image Test"),
        ("image-eabps-test", "C++ Image EABPS Suite"),
        ("audio-test", "C++ Audio Round-Trip Test"),
        ("audio-adaptive-test", "C++ Adaptive Audio Test"),
        ("audio-eabps-test", "C++ Audio EABPS Suite"),
        ("video-teabps-test", "C++ Video TEABPS Suite"),
        ("ffmpeg-test", "C++ FFmpeg Integration Test"),
        ("format-test", "C++ Format Abstraction Test"),
    ]

    all_passed = True

    # 1. C++ Regression Tests
    print("\n--- Phase 1: C++ Engine Regression Tests ---")
    for cmd, name in cpp_tests:
        ok = run_cmd([str(AHMC_EXE), cmd], name)
        if not ok:
            all_passed = False

    # 2. Python Image Tests (BMP & PNG)
    print("\n--- Phase 2: Python Image Tests (BMP & PNG) ---")
    ok_img = run_cmd([PYTHON_EXE, str(PROJECT_ROOT / "scratch" / "test_suite.py")], "Image Test Suite (BMP & PNG)")
    if not ok_img:
        all_passed = False

    # 3. Python Audio Tests (WAV & FLAC)
    print("\n--- Phase 3: Python Audio Tests (WAV & FLAC - 10 Requirements) ---")
    ok_aud = run_cmd([PYTHON_EXE, str(PROJECT_ROOT / "scratch" / "test_audio_suite.py")], "Audio Test Suite (WAV & FLAC)")
    if not ok_aud:
        all_passed = False

    # 4. Python Video Tests (AVI & MP4)
    print("\n--- Phase 4: Python Video Tests (AVI & MP4 - 10 Requirements) ---")
    ok_vid = run_cmd([PYTHON_EXE, str(PROJECT_ROOT / "scratch" / "test_video_suite.py")], "Video Test Suite (AVI & MP4)")
    if not ok_vid:
        all_passed = False

    # 5. Python Video TEABPS Test Suite
    print("\n--- Phase 5: Python Video TEABPS Test Suite ---")
    ok_teabps = run_cmd([PYTHON_EXE, str(PROJECT_ROOT / "scratch" / "test_video_teabps_full.py")], "Video TEABPS Full Verification Suite")
    if not ok_teabps:
        all_passed = False

    # 6. Python Video Playback, FFV1 Integrity & Download Suite
    print("\n--- Phase 6: Python Video Playback, FFV1 Integrity & Download Suite ---")
    ok_pb = run_cmd([PYTHON_EXE, str(PROJECT_ROOT / "scratch" / "test_video_playback_pipeline.py")], "Video Playback & Download Verification Suite")
    if not ok_pb:
        all_passed = False

    print("\n======================================================================")
    if all_passed:
        print("   ALL TESTS PASSED! FULL SYSTEM COMPLIANT & VERIFIED.")
    else:
        print("   SOME TESTS FAILED! Check output above.")
    print("======================================================================")

    sys.exit(0 if all_passed else 1)


if __name__ == "__main__":
    main()
