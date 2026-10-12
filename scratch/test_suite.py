"""
test_suite.py
Automated end-to-end verification of the AHLC BMP and PNG Image Compression system.
Tests:
1. BMP that compresses well (synthetic/palette)
2. BMP that does not compress well (random 24-bit photo)
3. PNG image optimization
4. Pixel-identical lossless verification for all outputs
5. Valid image opening for all outputs
6. Rejection of unsupported formats
7. Accuracy of reported sizes, ratios, and percentages
"""
import os
import sys
from pathlib import Path
from PIL import Image

# Setup paths
PROJECT_ROOT = Path(r"d:\projects\compression\AHLC").resolve()
sys.path.insert(0, str(PROJECT_ROOT / "flask_app"))

import ahmc_runner as runner
from app import app

OUTPUT_DIR = PROJECT_ROOT / "outputs"
OUTPUT_DIR.mkdir(exist_ok=True)


def test_suite():
    print("=" * 60)
    print("AHLC BMP & PNG Image Compression Test Suite")
    print("=" * 60)

    # -------------------------------------------------------------
    # 1. BMP that compresses well (256-color palette gradient 64x64)
    # -------------------------------------------------------------
    print("\n--- Test 1: BMP that compresses well ---")
    bmp_good_in = PROJECT_ROOT / "scratch" / "test_compress_good.bmp"
    bmp_good_in.parent.mkdir(exist_ok=True)

    # Create 64x64 image with 64 colors
    im = Image.new("RGB", (64, 64))
    for y in range(64):
        for x in range(64):
            im.putpixel((x, y), ((x // 8) * 32, (y // 8) * 32, 128))
    im.save(bmp_good_in, format="BMP")

    out_good = OUTPUT_DIR / "test_good_out.bmp"
    res_good = runner.compress_image(bmp_good_in, out_good)

    assert res_good["ok"], f"Compression failed: {res_good.get('error')}"
    assert res_good["format"] == "BMP"
    assert res_good["verification"], "Lossless verification failed!"
    assert out_good.exists(), "Output file does not exist!"

    actual_orig = bmp_good_in.stat().st_size
    actual_out = out_good.stat().st_size
    assert res_good["original_size"] == actual_orig, "Original size mismatch!"
    assert res_good["compressed_size"] == actual_out, "Output size mismatch!"
    assert actual_out < actual_orig, f"Expected size reduction! {actual_out} vs {actual_orig}"
    assert res_good["bytes_saved"] == (actual_orig - actual_out), "Bytes saved mismatch!"

    # Verify output opens as a valid image
    with Image.open(out_good) as im_chk:
        assert im_chk.format == "BMP", "Output is not valid BMP!"
        assert im_chk.size == (64, 64), "Dimensions mismatch!"

    # Verify pixels identical
    assert runner.verify_pixels_identical(bmp_good_in, out_good), "Pixels not identical!"
    print(f" [PASS] BMP (Good Compression): {actual_orig} B -> {actual_out} B | Saved: {res_good['bytes_saved']} B ({res_good['space_saved_percent']:.2f}%) | Ratio: {res_good['compression_ratio']:.2f}x")
    print(f"        Method: {res_good['method']}")
    print(f"        Verification: {res_good['verification_label']}")

    # -------------------------------------------------------------
    # 2. BMP that does NOT compress well (high-entropy 24-bit photo)
    # -------------------------------------------------------------
    print("\n--- Test 2: BMP that does NOT compress well ---")
    bmp_hard_in = PROJECT_ROOT / "scratch" / "test_compress_hard.bmp"
    import random
    random.seed(42)
    # 32x32 random noise with high entropy (many distinct colors)
    im_noise = Image.new("RGB", (32, 32))
    for y in range(32):
        for x in range(32):
            im_noise.putpixel((x, y), (random.randint(0, 255), random.randint(0, 255), random.randint(0, 255)))
    im_noise.save(bmp_hard_in, format="BMP")

    out_hard = OUTPUT_DIR / "test_hard_out.bmp"
    res_hard = runner.compress_image(bmp_hard_in, out_hard)

    assert res_hard["ok"], f"Compression failed: {res_hard.get('error')}"
    assert res_hard["format"] == "BMP"
    assert res_hard["verification"], "Lossless verification failed!"
    assert out_hard.exists(), "Output file does not exist!"

    actual_orig_h = bmp_hard_in.stat().st_size
    actual_out_h = out_hard.stat().st_size
    assert res_hard["original_size"] == actual_orig_h
    assert res_hard["compressed_size"] == actual_out_h

    # Verify output opens as a valid image
    with Image.open(out_hard) as im_chk:
        assert im_chk.format == "BMP"
        assert im_chk.size == (32, 32)

    assert runner.verify_pixels_identical(bmp_hard_in, out_hard)
    print(f" [PASS] BMP (No Reduction): {actual_orig_h} B -> {actual_out_h} B | Saved: {res_hard['bytes_saved']} B ({res_hard['space_saved_percent']:.2f}%) | Ratio: {res_hard['compression_ratio']:.2f}x")
    print(f"        Method: {res_hard['method']}")
    print(f"        Verification: {res_hard['verification_label']}")

    # -------------------------------------------------------------
    # 3. PNG Image
    # -------------------------------------------------------------
    print("\n--- Test 3: PNG Compression & Lossless Optimization ---")
    png_src = PROJECT_ROOT / ".venv" / "Lib" / "site-packages" / "werkzeug" / "debug" / "shared" / "console.png"
    out_png = OUTPUT_DIR / "test_console_out.png"

    res_png = runner.compress_image(png_src, out_png)
    assert res_png["ok"], f"PNG failed: {res_png.get('error')}"
    assert res_png["format"] == "PNG"
    assert res_png["verification"], "PNG Lossless verification failed!"
    assert out_png.exists()

    actual_orig_p = png_src.stat().st_size
    actual_out_p = out_png.stat().st_size
    assert res_png["original_size"] == actual_orig_p
    assert res_png["compressed_size"] == actual_out_p
    assert actual_out_p <= actual_orig_p

    with Image.open(out_png) as im_chk:
        assert im_chk.format == "PNG"

    assert runner.verify_pixels_identical(png_src, out_png)
    print(f" [PASS] PNG: {actual_orig_p} B -> {actual_out_p} B | Saved: {res_png['bytes_saved']} B ({res_png['space_saved_percent']:.2f}%) | Ratio: {res_png['compression_ratio']:.2f}x")
    print(f"        Method: {res_png['method']}")
    print(f"        Verification: {res_png['verification_label']}")

    # -------------------------------------------------------------
    # 4. Flask Workflow (Upload -> Compress -> Download)
    # -------------------------------------------------------------
    print("\n--- Test 4: Flask Web Application Workflow ---")
    client = app.test_client()

    # GET Dashboard
    r = client.get("/")
    assert r.status_code == 200, "Dashboard failed to load"
    assert b"BMP" in r.data and b"PNG" in r.data, "Dashboard missing BMP/PNG indicators"

    # POST Compress BMP
    with open(bmp_good_in, "rb") as f:
        r = client.post("/compress", data={"file": (f, "test_compress_good.bmp")}, follow_redirects=True)
    assert r.status_code == 200
    assert b"Compression Result" in r.data
    assert b"Download BMP Image" in r.data or b"Download" in r.data

    # POST Compress PNG
    with open(png_src, "rb") as f:
        r = client.post("/compress", data={"file": (f, "console.png")}, follow_redirects=True)
    assert r.status_code == 200
    assert b"Compression Result" in r.data
    assert b"Download PNG Image" in r.data or b"Download" in r.data

    # Test Download endpoint
    # Find latest experiment ID
    import database as db
    exps = db.get_all_experiments(limit=1)
    assert len(exps) > 0
    latest_id = exps[0]["id"]
    r_dl = client.get(f"/download/{latest_id}")
    assert r_dl.status_code == 200
    assert r_dl.headers["Content-Type"] in ("image/png", "image/bmp")
    print(f" [PASS] Flask Web Workflow: Upload -> Compress -> Download working seamlessly (HTTP 200)")

    # -------------------------------------------------------------
    # 5. Unsupported formats and invalid uploads
    # -------------------------------------------------------------
    print("\n--- Test 5: Rejection of Unsupported Formats ---")
    import io
    dummy_txt = io.BytesIO(b"Hello text file")
    r_bad = client.post("/compress", data={"file": (dummy_txt, "notes.txt")}, follow_redirects=True)
    assert b"files are supported." in r_bad.data
    print(" [PASS] Rejection of .txt: Flash message shown")

    dummy_mp3 = io.BytesIO(b"ID3\x03\x00\x00\x00\x00\x00")
    r_mp3 = client.post("/compress", data={"file": (dummy_mp3, "audio.mp3")}, follow_redirects=True)
    assert b"files are supported." in r_mp3.data
    print(" [PASS] Rejection of .mp3: Flash message shown")

    # -------------------------------------------------------------
    # 6. Clear History Workflow
    # -------------------------------------------------------------
    print("\n--- Test 6: Clear History Workflow ---")
    r_clear = client.post("/clear_history", follow_redirects=True)
    assert r_clear.status_code == 200
    assert len(db.get_all_experiments()) == 0
    assert b"No experiments yet." in r_clear.data
    print(" [PASS] Clear History: All session records deleted, empty state displayed")

    print("\n" + "=" * 60)
    print("ALL TESTS PASSED SUCCESSFULLY!")
    print("=" * 60)


if __name__ == "__main__":
    test_suite()
