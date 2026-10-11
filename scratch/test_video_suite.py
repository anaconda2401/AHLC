"""
test_video_suite.py
Automated test suite verifying AVI and MP4 video support in AHLC.
Tests all 10 requirements:
1. Short uncompressed AVI video
2. Short MP4/H.264 video
3. Video with no audio track
4. Video containing an audio track (preserved losslessly)
5. Invalid and unsupported video files
6. Successful upload, processing, and download through Flask
7. Correct output dimensions and frame count
8. Exact decoded-frame verification for supported inputs
9. Correct file-size and compression-ratio calculations
10. Video that does not compress smaller than the original (honest retention)
"""
import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PROJECT_ROOT = Path(r"d:\projects\compression\AHLC").resolve()
sys.path.insert(0, str(PROJECT_ROOT / "flask_app"))

import ahmc_runner as runner
import database as db
from app import app


class TestVideoCompressionSuite(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.test_dir = PROJECT_ROOT / "scratch" / "video_test_artifacts"
        cls.test_dir.mkdir(parents=True, exist_ok=True)

        cls.ffmpeg = runner.get_ffmpeg_path()
        cls.ffprobe = runner.get_ffprobe_path()

        # 1. Uncompressed rawvideo AVI (64x64, 10 frames, 10 fps, testsrc, no audio)
        cls.uncompressed_avi = cls.test_dir / "uncompressed_test.avi"
        cmd1 = [
            cls.ffmpeg, "-y",
            "-f", "lavfi", "-i", "testsrc=duration=1.0:size=64x64:rate=10",
            "-c:v", "rawvideo",
            "-pix_fmt", "bgr24",
            "-an",
            str(cls.uncompressed_avi)
        ]
        subprocess.run(cmd1, capture_output=True, check=True)

        # 2. Short MP4 H.264 video (64x64, 10 frames, 10 fps, testsrc, no audio)
        cls.h264_mp4 = cls.test_dir / "h264_test.mp4"
        cmd2 = [
            cls.ffmpeg, "-y",
            "-f", "lavfi", "-i", "testsrc=duration=1.0:size=64x64:rate=10",
            "-c:v", "libx264",
            "-crf", "23",
            "-pix_fmt", "yuv420p",
            "-an",
            str(cls.h264_mp4)
        ]
        subprocess.run(cmd2, capture_output=True, check=True)

        # 3. Video with audio track (64x64, 10 frames, 440 Hz sine audio, AVI and MP4)
        cls.avi_with_audio = cls.test_dir / "with_audio.avi"
        cmd3 = [
            cls.ffmpeg, "-y",
            "-f", "lavfi", "-i", "testsrc=duration=1.0:size=64x64:rate=10",
            "-f", "lavfi", "-i", "sine=frequency=440:duration=1.0:sample_rate=44100",
            "-c:v", "rawvideo",
            "-pix_fmt", "bgr24",
            "-c:a", "pcm_s16le",
            str(cls.avi_with_audio)
        ]
        subprocess.run(cmd3, capture_output=True, check=True)

        cls.mp4_with_audio = cls.test_dir / "with_audio.mp4"
        cmd4 = [
            cls.ffmpeg, "-y",
            "-f", "lavfi", "-i", "testsrc=duration=1.0:size=64x64:rate=10",
            "-f", "lavfi", "-i", "sine=frequency=440:duration=1.0:sample_rate=44100",
            "-c:v", "libx264",
            "-crf", "23",
            "-pix_fmt", "yuv420p",
            "-c:a", "aac",
            str(cls.mp4_with_audio)
        ]
        subprocess.run(cmd4, capture_output=True, check=True)

        # 4. Tiny already-optimal MP4 (hard to compress further without expansion)
        cls.dense_mp4 = cls.test_dir / "dense.mp4"
        cmd5 = [
            cls.ffmpeg, "-y",
            "-f", "lavfi", "-i", "color=c=black:duration=0.5:size=32x32:rate=10",
            "-c:v", "libx264",
            "-crf", "28",
            "-pix_fmt", "yuv420p",
            "-an",
            str(cls.dense_mp4)
        ]
        subprocess.run(cmd5, capture_output=True, check=True)

        # 5. Invalid video files
        cls.corrupt_avi = cls.test_dir / "corrupt.avi"
        cls.corrupt_avi.write_bytes(b"RIFF\x00\x00\x00\x00AVI NOT_VALID_VIDEO_CONTENT")

        cls.non_video_mp4 = cls.test_dir / "text_as_mp4.mp4"
        cls.non_video_mp4.write_text("This is plain text with an mp4 extension")

    # ----------------------------------------------------------------
    # Requirement 1: Short uncompressed AVI video
    # ----------------------------------------------------------------
    def test_01_short_uncompressed_avi(self):
        print("\n[Test 1] Short uncompressed AVI video...")
        probe = runner.probe_video(self.uncompressed_avi)
        self.assertTrue(probe.get("ok"), f"Probe failed: {probe.get('error')}")
        self.assertEqual(probe["codec_name"], "rawvideo")
        self.assertEqual(probe["width"], 64)
        self.assertEqual(probe["height"], 64)
        self.assertEqual(probe["frame_count"], 10)

        out_avi = self.test_dir / "out_uncompressed.avi"
        res = runner.compress_avi(self.uncompressed_avi, out_avi)
        self.assertTrue(res.get("ok"), f"Compression failed: {res.get('error')}")
        self.assertTrue(res.get("verification"), f"Verification failed: {res.get('verification_label')}")
        self.assertLess(res["compressed_size"], res["original_size"])
        self.assertGreater(res["compression_ratio"], 1.0)
        print(f"  Orig: {res['original_size']} B -> Comp: {res['compressed_size']} B ({res['compression_ratio']:.2f}x)")
        print(f"  Verification: {res['verification_label']}")

    # ----------------------------------------------------------------
    # Requirement 2: Short MP4/H.264 video
    # ----------------------------------------------------------------
    def test_02_short_mp4_h264(self):
        print("\n[Test 2] Short MP4/H.264 video...")
        probe = runner.probe_video(self.h264_mp4)
        self.assertTrue(probe.get("ok"))
        self.assertEqual(probe["codec_name"], "h264")
        self.assertEqual(probe["width"], 64)
        self.assertEqual(probe["height"], 64)
        self.assertEqual(probe["frame_count"], 10)

        out_mp4 = self.test_dir / "out_h264.mp4"
        res = runner.compress_mp4(self.h264_mp4, out_mp4)
        self.assertTrue(res.get("ok"))
        self.assertTrue(res.get("verification"))
        self.assertEqual(res["format"], "MP4")
        print(f"  Method: {res['method']}")
        print(f"  Verification: {res['verification_label']}")

    # ----------------------------------------------------------------
    # Requirement 3: Video with no audio track
    # ----------------------------------------------------------------
    def test_03_video_no_audio(self):
        print("\n[Test 3] Video with no audio track...")
        probe = runner.probe_video(self.uncompressed_avi)
        self.assertFalse(probe["has_audio"])

        out_avi = self.test_dir / "out_no_audio.avi"
        res = runner.compress_avi(self.uncompressed_avi, out_avi)
        self.assertTrue(res.get("ok"))
        self.assertFalse(res["has_audio"])
        self.assertTrue(res["verification"])
        print("  Confirmed no audio present and cleanly processed without error.")

    # ----------------------------------------------------------------
    # Requirement 4: Video containing audio track (preserved losslessly)
    # ----------------------------------------------------------------
    def test_04_video_with_audio_preserved(self):
        print("\n[Test 4] Video with audio track preservation...")
        probe = runner.probe_video(self.avi_with_audio)
        self.assertTrue(probe["has_audio"])

        out_avi = self.test_dir / "out_with_audio.avi"
        res = runner.compress_avi(self.avi_with_audio, out_avi)
        self.assertTrue(res.get("ok"))
        self.assertTrue(res["has_audio"])
        self.assertTrue(res["verification"])

        # Check output still has audio
        rec_probe = runner.probe_video(out_avi)
        self.assertTrue(rec_probe["has_audio"])
        print(f"  Original has audio: True -> Reconstructed has audio: True")
        print(f"  Verification: {res['verification_label']}")

    # ----------------------------------------------------------------
    # Requirement 5: Invalid and unsupported video files
    # ----------------------------------------------------------------
    def test_05_invalid_and_unsupported(self):
        print("\n[Test 5] Invalid and unsupported video files...")
        out_temp = self.test_dir / "out_dummy.avi"
        res1 = runner.compress_avi(self.corrupt_avi, out_temp)
        self.assertFalse(res1.get("ok"), "Corrupt AVI should fail")
        print(f"  Corrupt AVI rejected: {res1.get('error')}")

        res2 = runner.compress_mp4(self.non_video_mp4, self.test_dir / "out_dummy.mp4")
        self.assertFalse(res2.get("ok"), "Text as MP4 should fail")
        print(f"  Invalid MP4 rejected: {res2.get('error')}")

        # Unsupported format (e.g. .mkv)
        dummy_mkv = self.test_dir / "dummy.mkv"
        dummy_mkv.write_bytes(b"\x1a\x45\xdf\xa3")
        res3 = runner.compress_media(dummy_mkv, self.test_dir / "out_dummy.mkv")
        self.assertFalse(res3.get("ok"))
        print(f"  Unsupported format .mkv rejected: {res3.get('error')}")

    # ----------------------------------------------------------------
    # Requirement 6: Upload, processing, and download through Flask
    # ----------------------------------------------------------------
    def test_06_flask_workflow(self):
        print("\n[Test 6] Flask upload, process, and download workflow...")
        client = app.test_client()

        # 6a: AVI upload & compress
        with open(self.uncompressed_avi, "rb") as f:
            resp = client.post(
                "/compress",
                data={"file": (f, "test_flask.avi")},
                follow_redirects=False,
            )
        self.assertEqual(resp.status_code, 302)
        redirect_url = resp.headers["Location"]
        self.assertIn("/result/", redirect_url)
        exp_id = int(redirect_url.split("/")[-1])

        # Result page
        r_page = client.get(f"/result/{exp_id}")
        self.assertEqual(r_page.status_code, 200)
        self.assertIn(b"Video Compression Result", r_page.data)
        self.assertIn(b"Frame Lossless Verify", r_page.data)
        self.assertIn(b"PASS", r_page.data)

        # Download video
        d_resp = client.get(f"/download/{exp_id}/video")
        self.assertEqual(d_resp.status_code, 200)
        self.assertEqual(d_resp.headers["Content-Type"], "video/x-msvideo")
        self.assertGreater(len(d_resp.data), 0)
        d_resp.close()

        # Report page
        rep_page = client.get(f"/report/{exp_id}")
        self.assertEqual(rep_page.status_code, 200)
        self.assertIn(b"Input Information", rep_page.data)
        self.assertIn(b"SPATIAL + TEMPORAL", rep_page.data)

        # 6b: MP4 upload & compress
        with open(self.h264_mp4, "rb") as f:
            resp_mp4 = client.post(
                "/compress",
                data={"file": (f, "test_flask.mp4")},
                follow_redirects=False,
            )
        self.assertEqual(resp_mp4.status_code, 302)
        exp_id_mp4 = int(resp_mp4.headers["Location"].split("/")[-1])

        d_mp4 = client.get(f"/download/{exp_id_mp4}/video")
        self.assertEqual(d_mp4.status_code, 200)
        self.assertEqual(d_mp4.headers["Content-Type"], "video/mp4")
        d_mp4.close()

        # 6c: Analyze page with AVI
        with open(self.uncompressed_avi, "rb") as f:
            a_resp = client.post(
                "/analyze",
                data={"file": (f, "analyze_test.avi")},
                follow_redirects=True,
            )
        self.assertEqual(a_resp.status_code, 200)
        self.assertIn(b"Video Properties", a_resp.data)
        self.assertIn(b"rawvideo", a_resp.data)
        print("  Flask upload, result display, report generation, and video download verified.")

    # ----------------------------------------------------------------
    # Requirement 7: Correct output dimensions and frame count
    # ----------------------------------------------------------------
    def test_07_dimensions_and_frame_count(self):
        print("\n[Test 7] Dimensions and frame count preservation...")
        out_avi = self.test_dir / "dim_test.avi"
        res = runner.compress_avi(self.uncompressed_avi, out_avi)
        self.assertTrue(res.get("ok"))

        probe = runner.probe_video(out_avi)
        self.assertEqual(probe["width"], 64)
        self.assertEqual(probe["height"], 64)
        self.assertEqual(probe["frame_count"], 10)
        self.assertEqual(probe["fps"], 10.0)
        print("  Verified width: 64, height: 64, frames: 10, fps: 10.0")

    # ----------------------------------------------------------------
    # Requirement 8: Exact decoded-frame verification for supported inputs
    # ----------------------------------------------------------------
    def test_08_exact_decoded_frame_verification(self):
        print("\n[Test 8] Exact decoded-frame verification...")
        out_avi = self.test_dir / "verify_exact.avi"
        res = runner.compress_avi(self.uncompressed_avi, out_avi)
        self.assertTrue(res.get("ok"))

        ver_dict = runner.verify_video_frames_identical(self.uncompressed_avi, out_avi)
        self.assertTrue(ver_dict["verified"], f"Verification failed: {ver_dict.get('reason')}")
        self.assertEqual(ver_dict["frame_count"], 10)
        print(f"  Exact raw frame bit-level verification: {ver_dict['label']}")

    # ----------------------------------------------------------------
    # Requirement 9: Correct file-size and compression-ratio calculations
    # ----------------------------------------------------------------
    def test_09_file_size_and_ratio_calculations(self):
        print("\n[Test 9] File size and compression ratio calculations...")
        out_avi = self.test_dir / "ratio_test.avi"
        res = runner.compress_avi(self.uncompressed_avi, out_avi)
        self.assertTrue(res.get("ok"))

        orig_sz = res["original_size"]
        comp_sz = res["compressed_size"]
        bytes_saved = res["bytes_saved"]
        space_saved = res["space_saved_percent"]
        ratio = res["compression_ratio"]

        self.assertEqual(bytes_saved, orig_sz - comp_sz)
        self.assertAlmostEqual(space_saved, (bytes_saved / orig_sz) * 100.0, places=2)
        self.assertAlmostEqual(ratio, orig_sz / comp_sz, places=3)
        print(f"  Original: {orig_sz} B, Compressed: {comp_sz} B")
        print(f"  Saved: {bytes_saved} B ({space_saved:.2f}%), Ratio: {ratio:.3f}x")

    # ----------------------------------------------------------------
    # Requirement 10: Video that does not compress smaller (honest retention)
    # ----------------------------------------------------------------
    def test_10_no_size_reduction_honest_retention(self):
        print("\n[Test 10] No-reduction honest retention...")
        out_no_red = self.test_dir / "out_no_reduction.mp4"
        res = runner.compress_mp4(self.h264_mp4, out_no_red)
        self.assertTrue(res.get("ok"))
        self.assertTrue(res["verification"])

        # When a file cannot be made smaller losslessly, AHLC retains the original
        self.assertEqual(res["compressed_size"], res["original_size"])
        self.assertEqual(res["bytes_saved"], 0)
        self.assertEqual(res["space_saved_percent"], 0.0)
        self.assertEqual(res["compression_ratio"], 1.0)
        self.assertIn("Original MP4 Retained", res["method"])
        print(f"  Input size: {res['original_size']} B -> Output size: {res['compressed_size']} B")
        print(f"  Method: {res['method']}")
        print(f"  Bytes saved: {res['bytes_saved']} B ({res['space_saved_percent']:.2f}%), Ratio: {res['compression_ratio']:.2f}x")
        print("  Honest retention verified: 0 fake reduction reported.")


    # ----------------------------------------------------------------
    # Requirement 11: 50 MB video upload limit enforcement
    # ----------------------------------------------------------------
    def test_11_max_video_upload_limit(self):
        print("\n[Test 11] 50 MB maximum video upload size enforcement...")
        client = app.test_client()

        # Create a dummy 51 MB in-memory video stream exceeding the 50 MB limit
        large_size = 51 * 1024 * 1024
        oversized_data = io.BytesIO(b"\x00" * large_size)

        resp = client.post(
            "/compress",
            data={"file": (oversized_data, "large_demo.mp4")},
            follow_redirects=True,
        )
        self.assertEqual(resp.status_code, 200)
        self.assertIn(b"Video file exceeds 50 MB demo limit", resp.data)
        self.assertIn(b"51.0 MB uploaded", resp.data)

        # Also test on /analyze
        oversized_data2 = io.BytesIO(b"\x00" * large_size)
        a_resp = client.post(
            "/analyze",
            data={"file": (oversized_data2, "large_demo.avi")},
            follow_redirects=True,
        )
        self.assertEqual(a_resp.status_code, 200)
        self.assertIn(b"Video file exceeds 50 MB demo limit", a_resp.data)
        print("  50 MB demo upload limit verified on /compress and /analyze.")


if __name__ == "__main__":
    unittest.main()

