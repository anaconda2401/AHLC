"""
test_video_playback_pipeline.py
Comprehensive Verification & Regression Suite for Video Playback, FFV1 Integrity,
AHLV Container Decode, and Flask Download Endpoints.
"""

import sys
import unittest
import subprocess
import shutil
from pathlib import Path

PROJECT_ROOT = Path(r"d:\projects\compression\AHLC").resolve()
sys.path.insert(0, str(PROJECT_ROOT / "flask_app"))

import app as flask_app_module
from ahmc_runner import (
    get_ffmpeg_path,
    get_ffprobe_path,
    run_ffmpeg,
    probe_video,
    compress_avi,
    compress_mp4,
    verify_video_frames_identical,
    run_video_teabps,
)
import database as db

TEST_DIR = PROJECT_ROOT / "scratch" / "video_playback_test_artifacts"
TEST_DIR.mkdir(exist_ok=True)


class TestVideoPlaybackPipeline(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ffmpeg = get_ffmpeg_path()
        cls.ffprobe = get_ffprobe_path()
        cls.app = flask_app_module.app
        cls.client = cls.app.test_client()

        # 1. Create a 1-second synthetic rawvideo AVI (640x360, 25fps = 25 frames, YUV420p, no audio)
        cls.raw_avi = TEST_DIR / "test_synth_raw.avi"
        subprocess.run([
            cls.ffmpeg, "-y",
            "-f", "lavfi", "-i", "testsrc=size=640x360:rate=25:duration=1",
            "-pix_fmt", "yuv420p",
            "-c:v", "rawvideo",
            str(cls.raw_avi)
        ], capture_output=True, check=True)

        # 2. Create a 1-second synthetic rawvideo AVI with audio (sine 440Hz PCM)
        cls.raw_avi_with_audio = TEST_DIR / "test_synth_raw_audio.avi"
        subprocess.run([
            cls.ffmpeg, "-y",
            "-f", "lavfi", "-i", "testsrc=size=640x360:rate=25:duration=1",
            "-f", "lavfi", "-i", "sine=frequency=440:duration=1",
            "-pix_fmt", "yuv420p",
            "-c:v", "rawvideo",
            "-c:a", "pcm_s16le",
            str(cls.raw_avi_with_audio)
        ], capture_output=True, check=True)

        # 3. Create a 1-second H.264 MP4 with audio
        cls.synth_mp4 = TEST_DIR / "test_synth_h264.mp4"
        subprocess.run([
            cls.ffmpeg, "-y",
            "-f", "lavfi", "-i", "testsrc=size=640x360:rate=25:duration=1",
            "-f", "lavfi", "-i", "sine=frequency=880:duration=1",
            "-pix_fmt", "yuv420p",
            "-c:v", "libx264", "-crf", "23", "-preset", "ultrafast",
            "-c:a", "aac",
            str(cls.synth_mp4)
        ], capture_output=True, check=True)

    def test_01_raw_uncompressed_avi_probe_and_decode(self):
        """Verify raw uncompressed AVI probe and full FFmpeg decode."""
        probe = probe_video(self.raw_avi)
        self.assertTrue(probe["ok"])
        self.assertEqual(probe["codec_name"], "rawvideo")
        self.assertEqual(probe["width"], 640)
        self.assertEqual(probe["height"], 360)
        self.assertEqual(probe["frame_count"], 25)
        self.assertFalse(probe["has_audio"])

        # Decode entire file to null
        res = subprocess.run([self.ffmpeg, "-v", "error", "-i", str(self.raw_avi), "-f", "null", "-"],
                             capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        self.assertEqual(res.stderr.strip(), "")

    def test_02_compress_avi_ffv1_and_playback_mp4(self):
        """Verify compress_avi produces valid lossless FFV1 master and playable H.264 MP4."""
        out_avi = TEST_DIR / "out_compress.avi"
        res = compress_avi(self.raw_avi, out_avi)
        self.assertTrue(res["ok"])
        self.assertEqual(res["selected_codec"], "ffv1")
        self.assertTrue(res["verification"])
        self.assertTrue(res["has_playback_mp4"])

        # Check FFV1 file validity
        final_avi = Path(res["compressed_path"])
        self.assertTrue(final_avi.exists())
        probe_ffv1 = probe_video(final_avi)
        self.assertEqual(probe_ffv1["codec_name"], "ffv1")
        self.assertEqual(probe_ffv1["frame_count"], 25)

        dec_ffv1 = subprocess.run([self.ffmpeg, "-v", "error", "-i", str(final_avi), "-f", "null", "-"],
                                  capture_output=True, text=True)
        self.assertEqual(dec_ffv1.returncode, 0, f"FFV1 decode error: {dec_ffv1.stderr}")

        # Check Playback MP4 validity
        playback_mp4 = Path(res["playback_path"])
        self.assertTrue(playback_mp4.exists())
        self.assertEqual(playback_mp4.suffix.lower(), ".mp4")
        probe_mp4 = probe_video(playback_mp4)
        self.assertEqual(probe_mp4["codec_name"], "h264")
        self.assertEqual(probe_mp4["frame_count"], 25)

        dec_mp4 = subprocess.run([self.ffmpeg, "-v", "error", "-i", str(playback_mp4), "-f", "null", "-"],
                                 capture_output=True, text=True)
        self.assertEqual(dec_mp4.returncode, 0, f"MP4 playback decode error: {dec_mp4.stderr}")

    def test_03_audio_preservation_in_video(self):
        """Verify audio track is preserved in both FFV1 and playback MP4."""
        out_avi = TEST_DIR / "out_audio_compress.avi"
        res = compress_avi(self.raw_avi_with_audio, out_avi)
        self.assertTrue(res["ok"])
        self.assertTrue(res["has_audio"])
        self.assertTrue(res["verification"])

        # Check FFV1 audio
        probe_ffv1 = probe_video(Path(res["compressed_path"]))
        self.assertTrue(probe_ffv1["has_audio"])
        self.assertEqual(probe_ffv1["audio_codec"], "pcm_s16le")

        # Check Playback MP4 audio
        probe_mp4 = probe_video(Path(res["playback_path"]))
        self.assertTrue(probe_mp4["has_audio"])
        self.assertEqual(probe_mp4["audio_codec"], "aac")

    def test_04_teabps_ahlv_exact_canonical_reconstruction(self):
        """Verify TEABPS AHLV container compresses and decompresses with 100% exact sample identity."""
        ahlv_path = TEST_DIR / "test_teabps.ahlv"
        t_info = run_video_teabps(self.raw_avi, TEST_DIR, block_size=16, container_path=ahlv_path)
        self.assertTrue(t_info["success"])
        self.assertTrue(ahlv_path.exists())
        self.assertGreater(t_info["container_size"], 0)
        self.assertTrue(t_info["data"].get("verification"))

        # Decompress AHLV container via C++ engine
        decomp_yuv = TEST_DIR / "decompressed.yuv"
        ahmc_exe = PROJECT_ROOT / "build" / "Release" / "ahmc.exe"
        d_res = subprocess.run([str(ahmc_exe), "video-teabps-decompress", str(ahlv_path), str(decomp_yuv)],
                               capture_output=True, text=True)
        self.assertEqual(d_res.returncode, 0)
        self.assertTrue(decomp_yuv.exists())

        # Extract canonical frames directly from original video
        orig_yuv = TEST_DIR / "canonical_orig.yuv"
        subprocess.run([self.ffmpeg, "-y", "-i", str(self.raw_avi), "-f", "rawvideo", "-pix_fmt", "yuv420p", str(orig_yuv)],
                       capture_output=True, check=True)

        # Byte-for-byte comparison
        orig_bytes = orig_yuv.read_bytes()
        rec_bytes = decomp_yuv.read_bytes()
        self.assertEqual(orig_bytes, rec_bytes, "Canonical YUV byte comparison mismatch!")

    def test_05_failed_ffmpeg_handling(self):
        """Verify run_ffmpeg captures returncode != 0 and stderr on failure."""
        res = run_ffmpeg(["-nonexistent_option", "invalid_input.mp4"])
        self.assertFalse(res.success)
        self.assertIn("error", res.error.lower() if res.error else "")

    def test_06_corrupt_video_rejection(self):
        """Verify corrupt input video is rejected cleanly without unhandled exception."""
        corrupt_file = TEST_DIR / "corrupt.avi"
        corrupt_file.write_bytes(b"RIFF\x00\x00\x00\x00NOTAVI12345678")
        probe = probe_video(corrupt_file)
        self.assertFalse(probe["ok"])

    def test_07_flask_download_routes(self):
        """Verify Flask download endpoints for playback MP4, lossless master, and AHLV container."""
        # Run a real compression and save experiment in DB
        out_avi = TEST_DIR / "flask_test.avi"
        data = compress_avi(self.raw_avi, out_avi)
        exp_id = db.save_experiment(
            session_id="test_session_video",
            filename="test_synth_raw.avi",
            data=data,
            original_path=str(self.raw_avi),
            compressed_path=data["compressed_path"],
            recovered_path=data["recovered_path"],
        )

        # 1. Download Playable Video (MP4)
        resp_pb = self.client.get(f"/download/{exp_id}/playback")
        self.assertEqual(resp_pb.status_code, 200)
        self.assertIn("_playback.mp4", resp_pb.headers["Content-Disposition"])
        dl_mp4 = TEST_DIR / "downloaded_test.mp4"
        dl_mp4.write_bytes(resp_pb.data)
        dec_test = subprocess.run([self.ffmpeg, "-v", "error", "-i", str(dl_mp4), "-f", "null", "-"],
                                  capture_output=True, text=True)
        self.assertEqual(dec_test.returncode, 0, f"Downloaded MP4 decode error: {dec_test.stderr}")

        # 2. Download Lossless Master (AVI)
        resp_lossless = self.client.get(f"/download/{exp_id}/lossless")
        self.assertEqual(resp_lossless.status_code, 200)
        self.assertEqual(resp_lossless.mimetype, "video/x-msvideo")
        dl_avi = TEST_DIR / "downloaded_lossless.avi"
        dl_avi.write_bytes(resp_lossless.data)
        dec_lossless = subprocess.run([self.ffmpeg, "-v", "error", "-i", str(dl_avi), "-f", "null", "-"],
                                      capture_output=True, text=True)
        self.assertEqual(dec_lossless.returncode, 0, f"Downloaded FFV1 decode error: {dec_lossless.stderr}")

        # 3. Download AHLV Container
        resp_ahlv = self.client.get(f"/download/{exp_id}/ahlv")
        self.assertEqual(resp_ahlv.status_code, 200)
        self.assertEqual(resp_ahlv.data[:4], b"AHLV")

        # 4. Non-existent experiment 404
        resp_404 = self.client.get("/download/999999/playback")
        self.assertEqual(resp_404.status_code, 404)


if __name__ == "__main__":
    unittest.main()
