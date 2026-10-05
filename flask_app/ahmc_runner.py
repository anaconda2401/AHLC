"""
ahmc_runner.py
Subprocess wrapper around ahmc.exe. All compression/decompression is
performed by the C++ engine; Python only orchestrates and parses output.
"""
import json
import subprocess
import time
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional

PROJECT_ROOT = Path(__file__).parent.parent.resolve()
AHMC_EXE = PROJECT_ROOT / "build" / "Release" / "ahmc.exe"


@dataclass
class RunResult:
    success: bool
    stdout: str = ""
    stderr: str = ""
    elapsed_ms: float = 0.0
    error: str = ""


def _run(*args: str, timeout: int = 120) -> RunResult:
    """Execute ahmc.exe with the given arguments. Never uses shell=True."""
    cmd = [str(AHMC_EXE)] + list(args)
    t0 = time.perf_counter()
    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=str(PROJECT_ROOT),
        )
        elapsed = (time.perf_counter() - t0) * 1000
        return RunResult(
            success=proc.returncode == 0,
            stdout=proc.stdout,
            stderr=proc.stderr,
            elapsed_ms=elapsed,
        )
    except FileNotFoundError:
        return RunResult(success=False, error="ahmc.exe not found. Build the C++ project first.")
    except subprocess.TimeoutExpired:
        return RunResult(success=False, error="Operation timed out.")
    except Exception as exc:
        return RunResult(success=False, error=str(exc))


def _parse_json(result: RunResult) -> Optional[dict]:
    """Try to parse stdout as JSON."""
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        return None


# ----------------------------------------------------------------
# Analyze
# ----------------------------------------------------------------
def analyze_image(path: Path) -> dict:
    result = _run("image-analyze", str(path), "--json")
    data = _parse_json(result)
    if data is None or not data.get("ok"):
        return {"ok": False, "error": result.stderr or result.error or "Analysis failed"}
    data["elapsed_ms"] = result.elapsed_ms
    return data


def analyze_audio(path: Path) -> dict:
    result = _run("audio-analyze", str(path), "--json")
    data = _parse_json(result)
    if data is None or not data.get("ok"):
        return {"ok": False, "error": result.stderr or result.error or "Analysis failed"}
    data["elapsed_ms"] = result.elapsed_ms
    return data


# ----------------------------------------------------------------
# Compress (includes inline verification via C++)
# ----------------------------------------------------------------
def compress_image(input_path: Path, output_path: Path) -> dict:
    t0 = time.perf_counter()
    result = _run("image-compress", str(input_path), str(output_path), "--json")
    comp_ms = (time.perf_counter() - t0) * 1000

    data = _parse_json(result)
    if data is None or not data.get("ok"):
        return {
            "ok": False,
            "error": result.stderr or result.error or "Compression failed",
        }
    data["compression_time_ms"] = comp_ms

    # Decompress to permanent recovered file (for download)
    recovered_path = output_path.parent / (output_path.stem + "_recovered.bmp")
    t1 = time.perf_counter()
    dec = _run("image-decompress", str(output_path), str(recovered_path))
    decomp_ms = (time.perf_counter() - t1) * 1000
    data["decompression_time_ms"] = decomp_ms

    if dec.success and recovered_path.exists():
        # Verify pixel-perfect reconstruction
        ver = _run("image-verify", str(input_path), str(recovered_path))
        data["verification"] = ver.success and "PASS" in ver.stdout
    else:
        data["verification"] = False

    data["recovered_path"] = str(recovered_path)
    data["compressed_path"] = str(output_path)
    return data


def compress_audio(input_path: Path, output_path: Path) -> dict:
    t0 = time.perf_counter()
    result = _run("audio-compress", str(input_path), str(output_path), "--json")
    comp_ms = (time.perf_counter() - t0) * 1000

    data = _parse_json(result)
    if data is None or not data.get("ok"):
        return {
            "ok": False,
            "error": result.stderr or result.error or "Compression failed",
        }
    data["compression_time_ms"] = comp_ms

    recovered_path = output_path.parent / (output_path.stem + "_recovered.wav")
    t1 = time.perf_counter()
    dec = _run("audio-decompress", str(output_path), str(recovered_path))
    decomp_ms = (time.perf_counter() - t1) * 1000
    data["decompression_time_ms"] = decomp_ms

    if dec.success and recovered_path.exists():
        ver = _run("audio-verify", str(input_path), str(recovered_path))
        data["verification"] = ver.success and "PASS" in ver.stdout
    else:
        data["verification"] = False

    data["recovered_path"] = str(recovered_path)
    data["compressed_path"] = str(output_path)
    return data


# ----------------------------------------------------------------
# Decompress (for /decompress/<id> route)
# ----------------------------------------------------------------
def decompress_image(ahmc_path: Path, out_bmp: Path) -> RunResult:
    return _run("image-decompress", str(ahmc_path), str(out_bmp))


def decompress_audio(ahmc_path: Path, out_wav: Path) -> RunResult:
    return _run("audio-decompress", str(ahmc_path), str(out_wav))


# ----------------------------------------------------------------
# Run all C++ tests (for health-check endpoint)
# ----------------------------------------------------------------
def run_all_tests() -> dict:
    results = {}
    for test in ("bitio-test", "rle-test", "huffman-test", "image-test", "audio-test"):
        r = _run(test, timeout=60)
        results[test] = {"ok": r.success, "output": r.stdout[-500:]}
    return results
