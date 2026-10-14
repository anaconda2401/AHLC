"""
scratch/test_video_teabps_full.py
Comprehensive automated test suite for AHLC Video TEABPS
(Temporal Entropy-Aware Block-wise Predictor Selection).
Covers:
1. C++ Engine TEABPS unit tests & benchmark execution
2. Reversible integer predictor arithmetic & boundary wrap-around
3. First-frame, second-frame, and third-frame candidate legality
4. Predictor selection (static video, linear motion, scene cuts, random noise)
5. Container validation & corrupted input rejection
6. Flask runner integration (compress_avi, compress_mp4)
7. Exact 100% decoded frame verification
8. Database persistence & retrieval of TEABPS metrics
9. Zero regressions across Image EABPS, Audio EABPS, and FFmpeg
"""
import json
import os
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent.resolve()
sys.path.insert(0, str(PROJECT_ROOT))

from flask_app.ahmc_runner import (
    _run,
    compress_media,
    compress_avi,
    compress_mp4,
    run_video_teabps,
    run_all_tests,
    probe_video,
    AHMC_EXE,
)
from flask_app.database import init_db, save_experiment, get_experiment


class TestVideoTEABPSFull(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        init_db()
        cls.test_dir = PROJECT_ROOT / "scratch" / "test_teabps_tmp"
        cls.test_dir.mkdir(parents=True, exist_ok=True)
        cls.avi_sample = PROJECT_ROOT / "scratch" / "video_test_artifacts" / "uncompressed_test.avi"
        cls.mp4_sample = PROJECT_ROOT / "scratch" / "video_test_artifacts" / "h264_test.mp4"

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.test_dir, ignore_errors=True)

    def test_01_cpp_test_suite(self):
        """Run C++ Video TEABPS automated test suite."""
        res = _run("video-teabps-test")
        self.assertTrue(res.success, f"video-teabps-test failed: {res.stdout}\n{res.stderr}")
        self.assertIn("Video TEABPS Test Suite Overall: ALL PASS", res.stdout)
        self.assertIn("1-pixel block forward/inverse for NONE, D1, D2", res.stdout)
        self.assertIn("TEMPORAL-D2 produces exact 0 for linear temporal velocity", res.stdout)
        self.assertIn("Modular residual arithmetic & extreme wrap-around", res.stdout)
        self.assertIn("Single-frame sequence exact reconstruction", res.stdout)
        self.assertIn("Partial blocks at right and bottom edges (50x38)", res.stdout)

    def test_02_cpp_benchmark_runner(self):
        """Run C++ Video TEABPS benchmark suite."""
        res = _run("video-teabps-benchmark")
        self.assertTrue(res.success, f"video-teabps-benchmark failed: {res.stdout}\n{res.stderr}")
        self.assertIn("Static Video Scene", res.stdout)
        self.assertIn("Linear Motion Sequence", res.stdout)
        self.assertIn("Moving Waveform Pattern", res.stdout)
        self.assertIn("Scene Cut Transition", res.stdout)
        self.assertIn("High-Entropy Noise Video", res.stdout)

    def test_03_cli_compress_decompress_roundtrip(self):
        """Test C++ CLI compress, decompress, and JSON output."""
        if not self.avi_sample.exists():
            self.skipTest("Sample AVI not found")

        out_ahmc = self.test_dir / "roundtrip.ahmc"
        res = _run("video-teabps-compress", str(self.avi_sample), str(out_ahmc), "--block-size", "16", "--max-frames", "5", "--json")
        self.assertTrue(res.success, f"Compress failed: {res.stderr}")
        self.assertTrue(out_ahmc.exists())
        self.assertGreater(out_ahmc.stat().st_size, 41)

        data = json.loads(res.stdout)
        self.assertTrue(data.get("ok"))
        self.assertEqual(data.get("algorithm"), "TEABPS Video")
        self.assertTrue(data.get("verification"))
        self.assertGreater(data.get("total_blocks", 0), 0)
        self.assertIn("count_none", data)
        self.assertIn("count_d1", data)
        self.assertIn("count_d2", data)
        self.assertIn("count_rle", data)
        self.assertIn("count_huffman", data)
        self.assertIn("count_raw", data)

        out_yuv = self.test_dir / "recovered.yuv"
        dec_res = _run("video-teabps-decompress", str(out_ahmc), str(out_yuv))
        self.assertTrue(dec_res.success)
        self.assertTrue(out_yuv.exists())
        self.assertGreater(out_yuv.stat().st_size, 0)

    def test_04_corrupted_container_safety(self):
        """Ensure decoder safely rejects corrupted and malformed AHLV containers."""
        # 1. Invalid Magic
        bad_magic = self.test_dir / "bad_magic.ahmc"
        bad_magic.write_bytes(b"XXXX\x03\x00\x01\x00\x00\x00\x10\x00\x00\x00\x10")
        r1 = _run("video-teabps-decompress", str(bad_magic), str(self.test_dir / "dummy.yuv"))
        self.assertFalse(r1.success)

        # 2. Unsupported Version
        bad_ver = self.test_dir / "bad_ver.ahmc"
        bad_ver.write_bytes(b"AHLV\x99\x00\x01\x00\x00\x00\x10\x00\x00\x00\x10")
        r2 = _run("video-teabps-decompress", str(bad_ver), str(self.test_dir / "dummy.yuv"))
        self.assertFalse(r2.success)

        # 3. Truncated header
        trunc = self.test_dir / "trunc.ahmc"
        trunc.write_bytes(b"AHLV\x03\x00")
        r3 = _run("video-teabps-decompress", str(trunc), str(self.test_dir / "dummy.yuv"))
        self.assertFalse(r3.success)

    def test_05_flask_runner_avi_workflow(self):
        """Test full Flask video runner with uncompressed AVI input."""
        if not self.avi_sample.exists():
            self.skipTest("Sample AVI not found")

        out_avi = self.test_dir / "flask_run_out.avi"
        res = compress_avi(self.avi_sample, out_avi)
        self.assertTrue(res["ok"])
        self.assertTrue(res["verification"])
        self.assertIn("PASS", res["verification_label"])
        self.assertIn("teabps_total_blocks", res)
        self.assertGreater(res["teabps_total_blocks"], 0)
        self.assertIn("teabps_ratio", res)
        self.assertGreater(res["teabps_ratio"], 0.0)
        self.assertTrue(res["teabps_verified"])
        self.assertTrue(res["compressed_path"].endswith(".avi"))
        self.assertGreater(res["ahmc_container_size"], 0)

    def test_06_flask_runner_mp4_workflow(self):
        """Test full Flask video runner with H.264 MP4 input (fallback + genuine TEABPS metrics)."""
        if not self.mp4_sample.exists():
            self.skipTest("Sample MP4 not found")

        out_mp4 = self.test_dir / "flask_run_out.mp4"
        res = compress_mp4(self.mp4_sample, out_mp4)
        self.assertTrue(res["ok"])
        self.assertTrue(res["verification"])
        self.assertTrue(res["is_fallback"])
        self.assertEqual(res["method"], "Original MP4 Retained (No size reduction)")
        self.assertIn("teabps_total_blocks", res)
        self.assertGreater(res["teabps_total_blocks"], 0)
        self.assertTrue(res["teabps_verified"])
        self.assertGreater(res["teabps_ratio"], 1.0) # TEABPS compresses raw frames effectively

    def test_07_database_persistence(self):
        """Test database storage and retrieval of Video TEABPS metrics."""
        if not self.avi_sample.exists():
            self.skipTest("Sample AVI not found")

        out_avi = self.test_dir / "db_test_out.avi"
        res = compress_avi(self.avi_sample, out_avi)
        exp_id = save_experiment(
            "test_session_video",
            self.avi_sample.name,
            res,
            str(self.avi_sample),
            res["compressed_path"],
            res["recovered_path"],
        )
        self.assertIsInstance(exp_id, int)
        row = get_experiment(exp_id)
        self.assertIsNotNone(row)
        self.assertEqual(row["filename"], self.avi_sample.name)
        self.assertEqual(row["media_type"], "video")
        self.assertGreater(row["eabps_total_blocks"], 0)
        self.assertEqual(row["teabps_d1_count"], res["teabps_d1_count"])
        self.assertEqual(row["teabps_d2_count"], res["teabps_d2_count"])
        self.assertEqual(row["eabps_verified"], 1)

    def test_08_run_all_tests_dispatcher(self):
        """Verify run_all_tests includes video-teabps-test and everything passes."""
        all_res = run_all_tests()
        self.assertIn("video-teabps-test", all_res)
        self.assertTrue(all_res["video-teabps-test"]["ok"])
        for name, info in all_res.items():
            self.assertTrue(info["ok"], f"Subtest failed in run_all_tests: {name}")

    def test_09_regressions_image_and_audio(self):
        """Verify that Image EABPS and Audio EABPS have zero regressions."""
        r_img = _run("image-eabps-test")
        self.assertTrue(r_img.success, f"Image EABPS regression: {r_img.stdout}")
        self.assertIn("ALL PASS", r_img.stdout)

        r_audio = _run("audio-eabps-test")
        self.assertTrue(r_audio.success, f"Audio EABPS regression: {r_audio.stdout}")
        self.assertIn("ALL PASS", r_audio.stdout)

        r_ffmpeg = _run("ffmpeg-test")
        self.assertTrue(r_ffmpeg.success, f"FFmpeg integration regression: {r_ffmpeg.stdout}")
        self.assertIn("PASS", r_ffmpeg.stdout)


if __name__ == "__main__":
    unittest.main()
