import sys
import os
import shutil
import subprocess
import json
from pathlib import Path

# Setup paths
repo_root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(repo_root))
sys.path.insert(0, str(repo_root / "flask_app"))

from flask_app import ahmc_runner
from flask_app import database
from PIL import Image

def run_cmd(cmd_list):
    res = subprocess.run(cmd_list, capture_output=True, text=True, cwd=str(repo_root))
    return res

def main():
    print("=================================================================")
    print(" AHLC Comprehensive EABPS Integration & Verification Suite")
    print("=================================================================")
    
    ahmc_exe = repo_root / "build" / "Release" / "ahmc.exe"
    if not ahmc_exe.exists():
        print(f"[FAIL] Engine executable not found at {ahmc_exe}")
        sys.exit(1)
        
    all_passed = True
    
    # 1. Run C++ Internal Unit & Layout Test Suite
    print("\n--- Test Phase 1: C++ Automated EABPS Test Suite ---")
    res1 = run_cmd([str(ahmc_exe), "image-eabps-test"])
    if res1.returncode == 0 and "ALL PASS" in res1.stdout:
        print("[PASS] C++ EABPS Test Suite (Sections 8.A - 8.F)")
    else:
        print("[FAIL] C++ EABPS Test Suite")
        print(res1.stdout)
        print(res1.stderr)
        all_passed = False

    # 2. Run C++ Benchmark against Baseline
    print("\n--- Test Phase 2: C++ Benchmark (EABPS vs Baseline) ---")
    res2 = run_cmd([str(ahmc_exe), "image-eabps-benchmark"])
    if res2.returncode == 0 and "Checkerboard Pattern" in res2.stdout:
        print("[PASS] C++ EABPS Benchmark execution and verification")
    else:
        print("[FAIL] C++ EABPS Benchmark")
        print(res2.stdout)
        all_passed = False

    # 3. Test CLI json mode on synthetic test BMP
    print("\n--- Test Phase 3: CLI JSON Output & Reversibility ---")
    tmp_dir = repo_root / "scratch" / "test_run"
    tmp_dir.mkdir(parents=True, exist_ok=True)
    
    test_bmp = tmp_dir / "test_img.bmp"
    im = Image.new("RGB", (48, 36), color=(120, 45, 200))
    # Draw some pattern
    for y in range(36):
        for x in range(48):
            im.putpixel((x, y), (x * 5 % 256, y * 7 % 256, (x + y) * 3 % 256))
    im.save(test_bmp, format="BMP")
    
    test_ahmc = tmp_dir / "test_img.ahmc"
    res3 = run_cmd([str(ahmc_exe), "image-eabps-compress", str(test_bmp), str(test_ahmc), "--json", "--block-size", "16"])
    if res3.returncode == 0:
        try:
            j = json.loads(res3.stdout.strip())
            if j.get("ok") and j.get("algorithm") == "EABPS" and j.get("verification") is True:
                print(f"[PASS] JSON output valid: blocks={j['total_blocks']}, ratio={j['compression_ratio']:.2f}x, verified=True")
            else:
                print(f"[FAIL] Invalid JSON response: {j}")
                all_passed = False
        except Exception as e:
            print(f"[FAIL] JSON parsing error: {e}, stdout={res3.stdout}")
            all_passed = False
    else:
        print(f"[FAIL] image-eabps-compress failed: {res3.stderr}")
        all_passed = False

    # 4. Decompress using CLI and check exact equality
    rec_bmp = tmp_dir / "test_img_rec.bmp"
    res4 = run_cmd([str(ahmc_exe), "image-eabps-decompress", str(test_ahmc), str(rec_bmp)])
    if res4.returncode == 0 and rec_bmp.exists():
        im_orig = Image.open(test_bmp)
        im_rec = Image.open(rec_bmp)
        if list(im_orig.getdata()) == list(im_rec.getdata()):
            print("[PASS] Decompressed BMP pixel-identical to original")
        else:
            print("[FAIL] Decompressed BMP pixels mismatch")
            all_passed = False
    else:
        print(f"[FAIL] image-eabps-decompress failed: {res4.stderr}")
        all_passed = False

    # 5. Test Python Flask ahmc_runner compress_bmp Integration
    print("\n--- Test Phase 4: Flask ahmc_runner Integration ---")
    out_bmp = tmp_dir / "runner_out.bmp"
    result_dict = ahmc_runner.compress_bmp(test_bmp, out_bmp)
    if result_dict.get("ok"):
        print(f"[PASS] compress_bmp returned ok=True")
        print(f"       Method: {result_dict.get('method')}")
        print(f"       EABPS size: {result_dict.get('eabps_compressed_size')} B (ratio {result_dict.get('eabps_ratio')}x)")
        print(f"       Baseline size: {result_dict.get('baseline_compressed_size')} B (ratio {result_dict.get('baseline_ratio')}x)")
        print(f"       Predictor counts: NONE={result_dict.get('eabps_none_count')} LEFT={result_dict.get('eabps_left_count')} UP={result_dict.get('eabps_up_count')}")
        print(f"       Lossless Verification: {result_dict.get('verification')}")
        if not result_dict.get("verification"):
            all_passed = False
    else:
        print(f"[FAIL] compress_bmp failed: {result_dict}")
        all_passed = False

    # 6. Database save and retrieve
    print("\n--- Test Phase 5: SQLite Database Persistence ---")
    database.init_db()
    exp_id = database.save_experiment(
        session_id="test_sess",
        filename="test_img.bmp",
        data=result_dict,
        original_path=str(test_bmp),
        compressed_path=str(out_bmp),
        recovered_path=str(out_bmp)
    )
    exp_row = database.get_experiment(exp_id)
    if exp_row and exp_row["eabps_total_blocks"] > 0:
        print(f"[PASS] Experiment #{exp_id} saved & retrieved with EABPS total_blocks={exp_row['eabps_total_blocks']}")
    else:
        print(f"[FAIL] Experiment #{exp_id} failed to record EABPS metrics in database: {exp_row}")
        all_passed = False

    # 7. Regression check for existing engine tests
    print("\n--- Test Phase 6: Existing Regression Tests ---")
    existing_tests = [
        "image-test",
        "image-adaptive-test",
        "rle-test",
        "huffman-test",
        "bitio-test",
        "audio-test",
        "audio-adaptive-test"
    ]
    for t in existing_tests:
        r = run_cmd([str(ahmc_exe), t])
        if r.returncode == 0:
            print(f" [PASS] Regression: {t}")
        else:
            print(f" [FAIL] Regression: {t} failed")
            all_passed = False

    # Cleanup test dir
    shutil.rmtree(tmp_dir, ignore_errors=True)

    print("\n=================================================================")
    if all_passed:
        print(" ALL EABPS TESTS AND REGRESSIONS PASSED!")
    else:
        print(" SOME TESTS FAILED!")
    print("=================================================================")
    return 0 if all_passed else 1

if __name__ == "__main__":
    sys.exit(main())
