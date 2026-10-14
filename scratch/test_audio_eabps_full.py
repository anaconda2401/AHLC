#!/usr/bin/env python3
"""
scratch/test_audio_eabps_full.py
Comprehensive Audio EABPS automated validation script:
- CLI command invocation and JSON output verification
- Multi-format WAV round-trips (8-bit, 16-bit, 24-bit, 32-bit, Mono, Stereo)
- Sample-exact verification via soundfile and numpy
- Flask runner (ahmc_runner.compress_wav) integration
- Database persistence (save_experiment and get_experiment)
- C++ regression suites (audio-eabps-test, audio-eabps-benchmark, image-eabps-test, etc.)
"""
import sys
import os
import shutil
import tempfile
import numpy as np
import soundfile as sf
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent.resolve()
sys.path.insert(0, str(PROJECT_ROOT / "flask_app"))

import ahmc_runner as runner
import database as db

def log_test(name: str, passed: bool, extra: str = ""):
    status = "PASS" if passed else "FAIL"
    print(f"[{status}] {name}{' - ' + extra if extra else ''}")
    if not passed:
        raise AssertionError(f"Test failed: {name}")

def create_synthetic_wav(path: Path, channels: int = 1, samplerate: int = 44100,
                         subtype: str = "PCM_16", frames: int = 8192, pattern: str = "sine"):
    dtype = "int16"
    if subtype == "PCM_U8":
        dtype = "uint8"
    elif subtype in ("PCM_24", "PCM_32"):
        dtype = "int32"

    t = np.linspace(0, frames / samplerate, frames, endpoint=False)
    if pattern == "sine":
        if dtype == "int16":
            base = (np.sin(2 * np.pi * 440.0 * t) * 20000).astype(np.int16)
        elif dtype == "uint8":
            base = ((np.sin(2 * np.pi * 440.0 * t) + 1.0) * 120).astype(np.uint8)
        else: # int32
            base = (np.sin(2 * np.pi * 440.0 * t) * 1000000000).astype(np.int32)
    elif pattern == "ramp":
        if dtype == "int16":
            base = (np.linspace(-15000, 15000, frames)).astype(np.int16)
        elif dtype == "uint8":
            base = (np.linspace(10, 240, frames)).astype(np.uint8)
        else:
            base = (np.linspace(-1000000000, 1000000000, frames)).astype(np.int32)
    elif pattern == "silence":
        base = np.zeros(frames, dtype=dtype)
    elif pattern == "noise":
        np.random.seed(42)
        if dtype == "int16":
            base = np.random.randint(-30000, 30000, frames, dtype=np.int16)
        elif dtype == "uint8":
            base = np.random.randint(0, 256, frames, dtype=np.uint8)
        else:
            base = np.random.randint(-1000000000, 1000000000, frames, dtype=np.int32)
    else:
        raise ValueError(f"Unknown pattern {pattern}")

    if channels == 1:
        data = base
    elif channels == 2:
        # 2 distinct channels
        data = np.column_stack([base, np.roll(base, frames // 4)])
    else:
        cols = [np.roll(base, (frames // channels) * c) for c in range(channels)]
        data = np.column_stack(cols)

    if subtype == "PCM_U8":
        import wave
        with wave.open(str(path), 'wb') as wf:
            wf.setnchannels(channels)
            wf.setsampwidth(1)
            wf.setframerate(samplerate)
            wf.writeframes(data.astype(np.uint8).tobytes())
        return

    sf.write(str(path), data, samplerate, subtype=subtype)

def main():
    print("=" * 65)
    print(" AHLC Audio EABPS Full Test & Verification Suite")
    print("=" * 65)

    tmpdir = Path(tempfile.mkdtemp(prefix="ahlc_audio_eabps_test_"))
    try:
        # -------------------------------------------------------------
        # 1. C++ Audio EABPS Built-in Test Suite
        # -------------------------------------------------------------
        print("\n--- 1. C++ Audio EABPS Built-in Test Suite ---")
        res_test = runner._run("audio-eabps-test")
        log_test("audio-eabps-test CLI execution", res_test.success, "All unit tests pass")

        # -------------------------------------------------------------
        # 2. C++ Audio EABPS Benchmark
        # -------------------------------------------------------------
        print("\n--- 2. C++ Audio EABPS Benchmark ---")
        res_bench = runner._run("audio-eabps-benchmark")
        log_test("audio-eabps-benchmark CLI execution", res_bench.success, "Benchmark completed")

        # -------------------------------------------------------------
        # 3. CLI JSON Compression & Decompression Round-Trip
        # -------------------------------------------------------------
        print("\n--- 3. CLI JSON Mode & Decompress Round-Trips ---")
        wav16 = tmpdir / "tone_16bit.wav"
        ahmc16 = tmpdir / "tone_16bit.ahmc"
        rec16 = tmpdir / "tone_16bit_rec.wav"
        create_synthetic_wav(wav16, channels=2, subtype="PCM_16", frames=16384, pattern="sine")

        res_c = runner._run("audio-eabps-compress", str(wav16), str(ahmc16), "--json")
        log_test("audio-eabps-compress --json", res_c.success)
        json_data = runner._parse_json(res_c)
        log_test("CLI JSON schema valid", json_data is not None and json_data.get("ok") is True)
        assert json_data["algorithm"] == "EABPS Audio"
        assert json_data["total_blocks"] > 0
        assert json_data["channels"] == 2
        assert json_data["bits_per_sample"] == 16
        print(f"      Blocks: {json_data['total_blocks']} | D2: {json_data['count_delta2']} | Huffman: {json_data['count_huffman']}")

        res_d = runner._run("audio-eabps-decompress", str(ahmc16), str(rec16))
        log_test("audio-eabps-decompress", res_d.success)
        exact_match = runner.verify_audio_identical(wav16, rec16)
        log_test("16-bit Stereo PCM Sample-Exact Identity", exact_match)

        # -------------------------------------------------------------
        # 4. Multi-format PCM Tests (8-bit, 24-bit, 32-bit, Mono, Stereo)
        # -------------------------------------------------------------
        print("\n--- 4. Multi-Format PCM Support ---")
        formats = [
            ("8-bit Unsigned Mono", 1, "PCM_U8", "ramp"),
            ("16-bit Signed Mono", 1, "PCM_16", "sine"),
            ("16-bit Signed Stereo", 2, "PCM_16", "ramp"),
            ("24-bit Signed Stereo", 2, "PCM_24", "sine"),
            ("32-bit Signed Mono", 1, "PCM_32", "sine"),
        ]

        for desc, ch, sub, pat in formats:
            w_in = tmpdir / f"test_{sub}_{ch}ch.wav"
            w_out = tmpdir / f"out_{sub}_{ch}ch.wav"
            create_synthetic_wav(w_in, channels=ch, subtype=sub, frames=8192, pattern=pat)

            ret = runner.compress_wav(w_in, w_out)
            log_test(f"{desc} via compress_wav", ret["ok"] and ret["verification"],
                     f"ratio={ret['compression_ratio']:.2f}x method={ret['method']}")

            # Verify samples match input
            match = runner.verify_audio_identical(w_in, w_out)
            log_test(f"{desc} Soundfile Exact Match", match)

        # -------------------------------------------------------------
        # 5. Database Integration & Persistence
        # -------------------------------------------------------------
        print("\n--- 5. Database Integration ---")
        db.init_db()
        test_data = ret.copy()
        exp_id = db.save_experiment(
            session_id="test_session_audio_eabps",
            filename="test_audio.wav",
            data=test_data,
            original_path=str(w_in),
            compressed_path=str(w_out),
            recovered_path=str(w_out),
        )
        log_test("Save experiment with Audio EABPS metrics", exp_id > 0)

        exp_row = db.get_experiment(exp_id)
        log_test("Retrieve experiment from DB", exp_row is not None)
        assert exp_row["media_type"] == "audio"
        assert exp_row["eabps_total_blocks"] > 0
        assert exp_row["eabps_delta2_count"] >= 0
        print(f"      DB Record #{exp_id}: blocks={exp_row['eabps_total_blocks']}, delta1={exp_row['eabps_delta1_count']}, delta2={exp_row['eabps_delta2_count']}")

        # -------------------------------------------------------------
        # 6. Full Regression Suite (Image EABPS & Baseline Audio)
        # -------------------------------------------------------------
        print("\n--- 6. C++ Full Regression Suite ---")
        reg_tests = [
            "image-eabps-test",
            "image-test",
            "image-adaptive-test",
            "audio-test",
            "audio-adaptive-test",
            "huffman-test",
            "format-test"
        ]
        for tname in reg_tests:
            r = runner._run(tname)
            log_test(f"Regression {tname}", r.success)

        print("\n" + "=" * 65)
        print(" ALL AUDIO EABPS TESTS AND REGRESSIONS PASSED SUCCESSFULLY!")
        print("=" * 65)

    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)

if __name__ == "__main__":
    main()
