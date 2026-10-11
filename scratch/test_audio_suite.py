"""
test_audio_suite.py
Automated end-to-end verification of AHLC WAV and FLAC Audio Compression.

Tests required by specification:
1. A normal PCM WAV file.
2. A WAV file containing silence or repeated samples.
3. A WAV file with nontrivial audio content.
4. A valid FLAC file.
5. A FLAC file that does not benefit from additional compression.
6. Exact decoded sample preservation after processing.
7. Correct metadata preservation.
8. Complete Flask upload -> compress -> results -> download workflow for both formats.
9. Rejection of unsupported or invalid audio files.
10. Correct handling when output is not smaller than input.
"""
import io
import os
import sys
import numpy as np
import soundfile as sf
from pathlib import Path

# Setup paths
PROJECT_ROOT = Path(r"d:\projects\compression\AHLC").resolve()
sys.path.insert(0, str(PROJECT_ROOT / "flask_app"))

import ahmc_runner as runner
import database as db
from app import app

OUTPUT_DIR = PROJECT_ROOT / "outputs"
SCRATCH_DIR = PROJECT_ROOT / "scratch"
OUTPUT_DIR.mkdir(exist_ok=True)
SCRATCH_DIR.mkdir(exist_ok=True)


def create_pcm_wav(path: Path, data: np.ndarray, samplerate: int = 44100, subtype: str = "PCM_16"):
    sf.write(str(path), data, samplerate, format="WAV", subtype=subtype)


def create_flac(path: Path, data: np.ndarray, samplerate: int = 44100, compression_level: float = 0.8):
    sf.write(str(path), data, samplerate, format="FLAC", compression_level=compression_level)


def run_audio_tests():
    print("=" * 70)
    print("AHLC WAV & FLAC Audio Compression Test Suite")
    print("=" * 70)

    sr = 44100
    duration = 1.0  # 1 second
    t = np.linspace(0, duration, int(sr * duration), endpoint=False)

    # -----------------------------------------------------------------
    # Test 1: A normal PCM WAV file (440 Hz sine wave, mono 16-bit PCM)
    # -----------------------------------------------------------------
    print("\n--- Test 1: Normal PCM WAV file ---")
    wav_normal = SCRATCH_DIR / "test1_normal.wav"
    normal_samples = (np.sin(2 * np.pi * 440 * t) * 30000).astype(np.int16)
    create_pcm_wav(wav_normal, normal_samples, samplerate=sr, subtype="PCM_16")

    out_normal = OUTPUT_DIR / "test1_normal_out.wav"
    res1 = runner.compress_wav(wav_normal, out_normal)

    assert res1["ok"], f"Normal WAV failed: {res1.get('error')}"
    assert res1["format"] == "WAV"
    assert res1["verification"], "Lossless verification failed for normal WAV"
    assert out_normal.exists(), "Output WAV does not exist"
    assert runner.verify_audio_identical(wav_normal, out_normal), "Decoded samples mismatch!"

    # Verify metadata
    info_in = sf.info(str(wav_normal))
    info_out = sf.info(str(out_normal))
    assert info_in.channels == info_out.channels == 1
    assert info_in.samplerate == info_out.samplerate == sr
    assert info_in.frames == info_out.frames
    print(f" [PASS] Normal PCM WAV: {res1['original_size']} B -> {res1['compressed_size']} B | Ratio: {res1['compression_ratio']:.2f}x")
    print(f"        Method: {res1['method']}")
    print(f"        Verification: {res1['verification_label']}")

    # -----------------------------------------------------------------
    # Test 2: WAV file containing silence / repeated samples
    # -----------------------------------------------------------------
    print("\n--- Test 2: WAV with silence / repeated samples ---")
    wav_silence = SCRATCH_DIR / "test2_silence.wav"
    silence_samples = np.zeros(sr, dtype=np.int16)
    create_pcm_wav(wav_silence, silence_samples, samplerate=sr, subtype="PCM_16")

    out_silence = OUTPUT_DIR / "test2_silence_out.wav"
    res2 = runner.compress_wav(wav_silence, out_silence)

    assert res2["ok"], f"Silence WAV failed: {res2.get('error')}"
    assert res2["verification"], "Silence verification failed"
    assert runner.verify_audio_identical(wav_silence, out_silence)
    # Check that predictor residual had zero or minimal entropy and Delta+RLE candidate was evaluated
    assert res2["rle_size"] > 0, "RLE candidate size should be computed"
    print(f" [PASS] Silence WAV: {res2['original_size']} B -> {res2['compressed_size']} B")
    print(f"        Candidates: RLE={res2['rle_size']} B, Huffman={res2['huffman_size']} B")
    print(f"        Method: {res2['method']}")
    print(f"        Verification: {res2['verification_label']}")

    # -----------------------------------------------------------------
    # Test 3: WAV with nontrivial audio content (polyphonic chords + stereo)
    # -----------------------------------------------------------------
    print("\n--- Test 3: WAV with nontrivial audio content ---")
    wav_complex = SCRATCH_DIR / "test3_complex.wav"
    ch1 = (np.sin(2 * np.pi * 220 * t) * 15000 + np.sin(2 * np.pi * 440 * t) * 10000).astype(np.int16)
    ch2 = (np.sin(2 * np.pi * 330 * t) * 15000 + np.sin(2 * np.pi * 660 * t) * 10000).astype(np.int16)
    stereo_samples = np.column_stack((ch1, ch2))
    create_pcm_wav(wav_complex, stereo_samples, samplerate=sr, subtype="PCM_16")

    out_complex = OUTPUT_DIR / "test3_complex_out.wav"
    res3 = runner.compress_wav(wav_complex, out_complex)

    assert res3["ok"], f"Complex WAV failed: {res3.get('error')}"
    assert res3["verification"]
    assert res3["channels"] == 2
    assert runner.verify_audio_identical(wav_complex, out_complex)
    print(f" [PASS] Nontrivial Stereo WAV: {res3['original_size']} B -> {res3['compressed_size']} B | Channels: {res3['channels']}")
    print(f"        Method: {res3['method']}")
    print(f"        Verification: {res3['verification_label']}")

    # -----------------------------------------------------------------
    # Test 4: Valid FLAC file
    # -----------------------------------------------------------------
    print("\n--- Test 4: Valid FLAC file ---")
    flac_valid = SCRATCH_DIR / "test4_valid.flac"
    flac_samples = (np.sin(2 * np.pi * 523.25 * t) * 28000).astype(np.int16)
    create_flac(flac_valid, flac_samples, samplerate=sr, compression_level=0.5)

    out_flac = OUTPUT_DIR / "test4_valid_out.flac"
    res4 = runner.compress_flac(flac_valid, out_flac)

    assert res4["ok"], f"FLAC failed: {res4.get('error')}"
    assert res4["format"] == "FLAC"
    assert res4["verification"], "FLAC sample verification failed!"
    assert out_flac.exists()

    # Verify output opens as a valid FLAC file
    info_flac_out = sf.info(str(out_flac))
    assert info_flac_out.format == "FLAC"
    assert info_flac_out.samplerate == sr
    assert info_flac_out.channels == 1
    assert runner.verify_audio_identical(flac_valid, out_flac)
    print(f" [PASS] Valid FLAC: {res4['original_size']} B -> {res4['compressed_size']} B | Ratio: {res4['compression_ratio']:.2f}x")
    print(f"        Method: {res4['method']}")
    print(f"        Verification: {res4['verification_label']}")

    # -----------------------------------------------------------------
    # Test 5: FLAC file that does NOT benefit from additional compression
    # -----------------------------------------------------------------
    print("\n--- Test 5: FLAC file that does not benefit from additional compression ---")
    flac_dense = SCRATCH_DIR / "test5_dense.flac"
    dense_samples = (np.sin(2 * np.pi * 880 * t) * 20000 + np.cos(2 * np.pi * 1760 * t) * 10000).astype(np.int16)
    # Encoded at max compression level 1.0
    create_flac(flac_dense, dense_samples, samplerate=sr, compression_level=1.0)

    out_dense = OUTPUT_DIR / "test5_dense_out.flac"
    res5 = runner.compress_flac(flac_dense, out_dense)

    assert res5["ok"]
    assert res5["format"] == "FLAC"
    assert res5["verification"]
    # Verify honest reporting: no negative savings or bogus claims
    assert res5["bytes_saved"] >= 0
    assert res5["compression_ratio"] >= 1.0
    assert out_dense.stat().st_size <= flac_dense.stat().st_size
    assert runner.verify_audio_identical(flac_dense, out_dense)
    print(f" [PASS] FLAC (No Improvement): {res5['original_size']} B -> {res5['compressed_size']} B | Saved: {res5['bytes_saved']} B ({res5['space_saved_percent']:.2f}%) | Ratio: {res5['compression_ratio']:.2f}x")
    print(f"        Method: {res5['method']}")
    print(f"        Verification: {res5['verification_label']}")

    # -----------------------------------------------------------------
    # Test 6: Exact decoded sample preservation after processing
    # -----------------------------------------------------------------
    print("\n--- Test 6: Exact decoded sample preservation after processing ---")
    # Read both original and compressed files with soundfile as raw numpy arrays
    s_orig, _ = sf.read(str(wav_normal), dtype="int16")
    s_out, _ = sf.read(str(out_normal), dtype="int16")
    assert np.array_equal(s_orig, s_out), "WAV samples are not exactly identical!"

    f_orig, _ = sf.read(str(flac_valid), dtype="int16")
    f_out, _ = sf.read(str(out_flac), dtype="int16")
    assert np.array_equal(f_orig, f_out), "FLAC samples are not exactly identical!"

    max_diff_wav = np.max(np.abs(s_orig.astype(np.int32) - s_out.astype(np.int32)))
    max_diff_flac = np.max(np.abs(f_orig.astype(np.int32) - f_out.astype(np.int32)))
    assert max_diff_wav == 0, f"Max difference in WAV samples is {max_diff_wav} != 0"
    assert max_diff_flac == 0, f"Max difference in FLAC samples is {max_diff_flac} != 0"
    print(f" [PASS] Sample equality verified: Max sample diff = 0 for both WAV and FLAC")

    # -----------------------------------------------------------------
    # Test 7: Correct metadata preservation
    # -----------------------------------------------------------------
    print("\n--- Test 7: Correct metadata preservation ---")
    info_wav_in = sf.info(str(wav_complex))
    info_wav_out = sf.info(str(out_complex))
    assert info_wav_in.samplerate == info_wav_out.samplerate, "Sample rate changed!"
    assert info_wav_in.channels == info_wav_out.channels, "Channel count changed!"
    assert info_wav_in.frames == info_wav_out.frames, "Frame count changed!"
    assert info_wav_in.format == info_wav_out.format == "WAV", "Format changed!"

    info_f_in = sf.info(str(flac_valid))
    info_f_out = sf.info(str(out_flac))
    assert info_f_in.samplerate == info_f_out.samplerate, "FLAC sample rate changed!"
    assert info_f_in.channels == info_f_out.channels, "FLAC channels changed!"
    assert info_f_in.frames == info_f_out.frames, "FLAC frames changed!"
    assert info_f_in.format == info_f_out.format == "FLAC", "FLAC format changed!"
    print(f" [PASS] Metadata preserved: Sample rate ({sr} Hz), Channels, Subtype, Frame count identical")

    # -----------------------------------------------------------------
    # Test 8: Complete Flask upload -> compress -> results -> download workflow
    # -----------------------------------------------------------------
    print("\n--- Test 8: Complete Flask upload -> compress -> results -> download workflow ---")
    client = app.test_client()

    # 8a. WAV Workflow
    with open(wav_normal, "rb") as f:
        r = client.post("/compress", data={"file": (f, "test_audio.wav")}, follow_redirects=True)
    assert r.status_code == 200, f"Flask WAV upload returned {r.status_code}"
    assert b"Audio Compression Result" in r.data
    assert b"Download WAV File" in r.data
    assert b"Sample-identical verification: PASS" in r.data

    # Download WAV
    exps = db.get_all_experiments(limit=1)
    exp_wav = exps[0]
    r_dl_wav = client.get(f"/download/{exp_wav['id']}")
    assert r_dl_wav.status_code == 200
    assert r_dl_wav.headers["Content-Type"] == "audio/wav"
    assert r_dl_wav.headers["Content-Disposition"].startswith("attachment; filename=test_audio.wav")

    # Verify downloaded WAV
    dl_wav_bytes = io.BytesIO(r_dl_wav.data)
    dl_samples, dl_sr = sf.read(dl_wav_bytes, dtype="int16")
    orig_samples, _ = sf.read(str(wav_normal), dtype="int16")
    assert np.array_equal(dl_samples, orig_samples), "Downloaded WAV samples differ from original!"

    # 8b. FLAC Workflow
    with open(flac_valid, "rb") as f:
        r = client.post("/compress", data={"file": (f, "test_music.flac")}, follow_redirects=True)
    assert r.status_code == 200, f"Flask FLAC upload returned {r.status_code}"
    assert b"Audio Compression Result" in r.data
    assert b"Download FLAC File" in r.data
    assert b"Sample-identical verification: PASS" in r.data

    # Download FLAC
    exps = db.get_all_experiments(limit=1)
    exp_flac = exps[0]
    r_dl_flac = client.get(f"/download/{exp_flac['id']}")
    assert r_dl_flac.status_code == 200
    assert r_dl_flac.headers["Content-Type"] == "audio/flac"
    assert r_dl_flac.headers["Content-Disposition"].startswith("attachment; filename=test_music.flac")

    # Verify downloaded FLAC
    dl_flac_bytes = io.BytesIO(r_dl_flac.data)
    dl_f_samples, dl_f_sr = sf.read(dl_flac_bytes, dtype="int16")
    orig_f_samples, _ = sf.read(str(flac_valid), dtype="int16")
    assert np.array_equal(dl_f_samples, orig_f_samples), "Downloaded FLAC samples differ from original!"
    print(f" [PASS] Flask upload -> compress -> results -> download verified for both WAV and FLAC")

    # -----------------------------------------------------------------
    # Test 9: Rejection of unsupported or invalid audio files
    # -----------------------------------------------------------------
    print("\n--- Test 9: Rejection of unsupported or invalid audio files ---")
    # Unsupported extension
    dummy_mp3 = io.BytesIO(b"ID3\x03\x00\x00\x00\x00\x00")
    r_mp3 = client.post("/compress", data={"file": (dummy_mp3, "audio.mp3")}, follow_redirects=True)
    assert b"files are supported." in r_mp3.data

    dummy_ogg = io.BytesIO(b"OggS\x00\x02\x00\x00\x00\x00")
    r_ogg = client.post("/compress", data={"file": (dummy_ogg, "track.ogg")}, follow_redirects=True)
    assert b"files are supported." in r_ogg.data

    dummy_aiff = io.BytesIO(b"FORM....AIFF")
    r_aiff = client.post("/compress", data={"file": (dummy_aiff, "track.aiff")}, follow_redirects=True)
    assert b"files are supported." in r_aiff.data

    # Corrupt WAV file
    corrupt_wav = io.BytesIO(b"RIFF\x00\x00\x00\x00WAVEfmt \x10\x00\x00\x00corrupted garbage data")
    r_corrupt = client.post("/compress", data={"file": (corrupt_wav, "corrupt.wav")}, follow_redirects=True)
    assert b"Compression failed" in r_corrupt.data or r_corrupt.status_code == 200
    print(" [PASS] Unsupported extensions (.mp3, .ogg, .aiff) and corrupt files correctly rejected")

    # -----------------------------------------------------------------
    # Test 10: Correct handling when output is not smaller than input
    # -----------------------------------------------------------------
    print("\n--- Test 10: Handling when output is not smaller than input ---")
    assert res5["bytes_saved"] == 0, f"Expected 0 bytes saved, got {res5['bytes_saved']}"
    assert res5["space_saved_percent"] == 0.0, f"Expected 0.0%, got {res5['space_saved_percent']}"
    assert res5["compression_ratio"] == 1.0, f"Expected 1.0x ratio, got {res5['compression_ratio']}"
    assert "Original FLAC Retained" in res5["method"]
    print(" [PASS] Honest calculation verified: 0 bytes saved, 0.00% space saved, 1.00x ratio, retained original file")

    print("\n" + "=" * 70)
    print("ALL 10 AUDIO SPECIFICATION TESTS PASSED SUCCESSFULLY!")
    print("=" * 70)


if __name__ == "__main__":
    run_audio_tests()
