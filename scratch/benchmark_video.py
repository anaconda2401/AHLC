"""
benchmark_video.py
Measures execution time, input/output file sizes, and verification status
for AHLC video compression (AVI and MP4).
"""
import sys
import time
from pathlib import Path

PROJECT_ROOT = Path(r"d:\projects\compression\AHLC").resolve()
sys.path.insert(0, str(PROJECT_ROOT / "flask_app"))

import ahmc_runner as runner

def run_benchmark(label=""):
    test_files = [
        ("Uncompressed AVI", PROJECT_ROOT / "scratch" / "video_test_artifacts" / "uncompressed_test.avi", "avi"),
        ("Compressed H.264 MP4", PROJECT_ROOT / "scratch" / "video_test_artifacts" / "h264_test.mp4", "mp4"),
        ("AVI with Audio", PROJECT_ROOT / "scratch" / "video_test_artifacts" / "with_audio.avi", "avi"),
        ("MP4 with Audio", PROJECT_ROOT / "scratch" / "video_test_artifacts" / "with_audio.mp4", "mp4"),
    ]

    out_dir = PROJECT_ROOT / "scratch" / "bench_outputs"
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"\n======================================================================")
    print(f"   AHLC VIDEO BENCHMARK: {label}")
    print(f"======================================================================")
    print(f"{'Test File':<24} | {'Elapsed (ms)':<12} | {'Input Size':<12} | {'Output Size':<12} | {'Ratio':<8} | {'Verified':<8}")
    print("-" * 88)

    results = []
    total_time = 0.0

    for name, path, fmt in test_files:
        if not path.exists():
            print(f"File not found: {path}")
            continue

        out_path = out_dir / f"bench_{path.name}"
        t0 = time.perf_counter()
        if fmt == "avi":
            res = runner.compress_avi(path, out_path)
        else:
            res = runner.compress_mp4(path, out_path)
        elapsed_ms = (time.perf_counter() - t0) * 1000
        total_time += elapsed_ms

        in_sz = res.get("original_size", 0)
        out_sz = res.get("compressed_size", 0)
        ratio = res.get("compression_ratio", 1.0)
        ver = "PASS" if res.get("verification") else "FAIL"

        print(f"{name:<24} | {elapsed_ms:>10.1f} ms | {in_sz:>10} B | {out_sz:>10} B | {ratio:>6.2f}x | {ver:<8}")
        results.append({
            "name": name,
            "elapsed_ms": elapsed_ms,
            "in_sz": in_sz,
            "out_sz": out_sz,
            "ratio": ratio,
            "verified": ver,
            "method": res.get("method", ""),
        })

    print("-" * 88)
    print(f"{'TOTAL':<24} | {total_time:>10.1f} ms")
    print(f"======================================================================\n")
    return results

if __name__ == "__main__":
    lbl = sys.argv[1] if len(sys.argv) > 1 else "CURRENT IMPLEMENTATION"
    run_benchmark(lbl)
