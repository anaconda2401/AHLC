"""
app.py  —  AHLC Multimedia Compression Dashboard (BMP, PNG, WAV, FLAC, AVI, MP4)
Run with:  python flask_app/app.py
"""
import os
import uuid
from pathlib import Path
from flask import (Flask, render_template, request, redirect,
                   url_for, flash, send_file, jsonify, abort)
from werkzeug.utils import secure_filename

import ahmc_runner as runner
import database as db

# ----------------------------------------------------------------
# Config
# ----------------------------------------------------------------
PROJECT_ROOT = Path(__file__).parent.parent.resolve()
UPLOAD_DIR   = PROJECT_ROOT / "uploads"
OUTPUT_DIR   = PROJECT_ROOT / "outputs"
ALLOWED_EXT  = {"bmp", "png", "wav", "flac", "avi", "mp4"}
MAX_UPLOAD   = 100 * 1024 * 1024  # 100 MB

UPLOAD_DIR.mkdir(exist_ok=True)
OUTPUT_DIR.mkdir(exist_ok=True)

app = Flask(__name__)
app.secret_key = os.environ.get("AHLC_SECRET", "ahlc-dev-secret-2025")
app.config["MAX_CONTENT_LENGTH"] = MAX_UPLOAD

db.init_db()


def _allowed(filename: str) -> bool:
    return "." in filename and filename.rsplit(".", 1)[1].lower() in ALLOWED_EXT


def _fmt_bytes(n) -> str:
    if n is None:
        return "N/A"
    n = int(n)
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024:
            return f"{n:.1f} {unit}" if unit != "B" else f"{n} B"
        n /= 1024
    return f"{n:.1f} GB"


# Inject helpers into all templates
@app.context_processor
def _inject():
    return {"fmt_bytes": _fmt_bytes}


# ================================================================
# Dashboard  GET /
# ================================================================
@app.route("/")
def dashboard():
    stats = db.get_dashboard_stats()
    return render_template("dashboard.html", stats=stats)


# ================================================================
# Analyze  GET /analyze
# ================================================================
@app.route("/analyze")
def analyze_page():
    return render_template("analyze.html")


# ================================================================
# Analyze  POST /analyze
# ================================================================
@app.route("/analyze", methods=["POST"])
def do_analyze():
    f = request.files.get("file")
    if not f or not f.filename:
        flash("No file selected.", "error")
        return redirect(url_for("analyze_page"))
    if not _allowed(f.filename):
        flash("Only .bmp, .png, .wav, .flac, .avi, and .mp4 files are supported.", "error")
        return redirect(url_for("analyze_page"))

    sid = uuid.uuid4().hex
    safe = secure_filename(f.filename)
    upload_path = UPLOAD_DIR / f"{sid}_{safe}"
    f.save(str(upload_path))

    data = runner.analyze_media(upload_path)
    upload_path.unlink(missing_ok=True)

    if not data.get("ok"):
        flash(f"Analysis failed: {data.get('error', 'Unknown error')}", "error")
        return redirect(url_for("analyze_page"))

    return render_template("analyze.html", result=data, filename=safe)


# ================================================================
# Compress  POST /compress
# ================================================================
@app.route("/compress", methods=["POST"])
def do_compress():
    f = request.files.get("file")
    if not f or not f.filename:
        flash("No file selected.", "error")
        return redirect(url_for("dashboard"))
    if not _allowed(f.filename):
        flash("Only .bmp, .png, .wav, .flac, .avi, and .mp4 files are supported.", "error")
        return redirect(url_for("dashboard"))

    sid  = uuid.uuid4().hex
    safe = secure_filename(f.filename)
    ext  = safe.rsplit(".", 1)[1].lower()

    upload_path = UPLOAD_DIR / f"{sid}_{safe}"
    output_path = OUTPUT_DIR / f"{sid}_compressed.{ext}"
    f.save(str(upload_path))

    data = runner.compress_media(upload_path, output_path)

    if not data.get("ok"):
        upload_path.unlink(missing_ok=True)
        flash(f"Compression failed: {data.get('error', 'Unknown error')}", "error")
        return redirect(url_for("dashboard"))

    exp_id = db.save_experiment(
        session_id=sid,
        filename=safe,
        data=data,
        original_path=str(upload_path),
        compressed_path=data.get("compressed_path", str(output_path)),
        recovered_path=data.get("recovered_path", str(output_path)),
    )
    return redirect(url_for("result", exp_id=exp_id))


# ================================================================
# Result  GET /result/<id>
# ================================================================
@app.route("/result/<int:exp_id>")
def result(exp_id):
    exp = db.get_experiment(exp_id)
    if not exp:
        abort(404)
    return render_template("result.html", exp=exp)


# ================================================================
# History  GET /history
# ================================================================
@app.route("/history")
def history():
    experiments = db.get_all_experiments()
    return render_template("history.html", experiments=experiments)


# ================================================================
# Report  GET /report/<id>
# ================================================================
@app.route("/report/<int:exp_id>")
def report(exp_id):
    exp = db.get_experiment(exp_id)
    if not exp:
        abort(404)
    return render_template("report.html", exp=exp)


# ================================================================
# Download file (BMP, PNG, WAV, or FLAC)
# ================================================================
@app.route("/download/<int:exp_id>")
@app.route("/download/<int:exp_id>/file")
@app.route("/download/<int:exp_id>/image")
@app.route("/download/<int:exp_id>/audio")
@app.route("/download/<int:exp_id>/video")
@app.route("/download/<int:exp_id>/compressed")
@app.route("/download/<int:exp_id>/recovered")
def download_image(exp_id):
    exp = db.get_experiment(exp_id)
    if not exp:
        abort(404)
    p_str = exp.get("compressed_path") or exp.get("recovered_path")
    if not p_str:
        abort(404)
    p = Path(p_str)
    if not p.exists():
        abort(404)

    orig_name = exp["filename"]
    ext = orig_name.rsplit(".", 1)[-1].lower() if "." in orig_name else ""
    mime_map = {
        "bmp": "image/bmp",
        "png": "image/png",
        "wav": "audio/wav",
        "flac": "audio/flac",
        "avi": "video/x-msvideo",
        "mp4": "video/mp4",
    }
    mimetype = mime_map.get(ext, "application/octet-stream")
    return send_file(
        str(p),
        as_attachment=True,
        download_name=orig_name,
        mimetype=mimetype,
    )


# ================================================================
# API: dashboard stats JSON  GET /api/stats
# ================================================================
@app.route("/api/stats")
def api_stats():
    return jsonify(db.get_dashboard_stats())


# ================================================================
# Error handlers
# ================================================================
@app.errorhandler(404)
def not_found(_):
    return render_template("base.html"), 404


@app.errorhandler(413)
def too_large(_):
    flash("File too large (max 100 MB).", "error")
    return redirect(url_for("dashboard"))


# ================================================================
# Entry point
# ================================================================
if __name__ == "__main__":
    print("\n" + "=" * 50)
    print("  AHLC Multimedia Compression Dashboard (BMP, PNG, WAV, FLAC, AVI, MP4)")
    print("  Running at http://127.0.0.1:5000")
    print("=" * 50 + "\n")
    app.run(debug=True, host="127.0.0.1", port=5000)
