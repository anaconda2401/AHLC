"""
database.py
SQLite persistence for AHLC experiments using only stdlib (sqlite3).
"""
import sqlite3
from pathlib import Path
from datetime import datetime
from typing import Optional

DB_PATH = Path(__file__).parent / "ahlc.db"


def _conn():
    con = sqlite3.connect(str(DB_PATH))
    con.row_factory = sqlite3.Row
    return con


def init_db():
    with _conn() as con:
        con.execute("""
        CREATE TABLE IF NOT EXISTS experiments (
            id                   INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id           TEXT NOT NULL,
            filename             TEXT NOT NULL,
            media_type           TEXT NOT NULL,
            original_size        INTEGER,
            compressed_size      INTEGER,
            compression_ratio    REAL,
            space_saved_percent  REAL,
            predictor            TEXT,
            selected_codec       TEXT,
            rle_size             INTEGER,
            huffman_size         INTEGER,
            original_entropy     REAL,
            residual_entropy     REAL,
            compression_time_ms  REAL,
            decompression_time_ms REAL,
            verification         INTEGER,
            -- Image-specific
            width                INTEGER,
            height               INTEGER,
            bpp                  INTEGER,
            -- Audio-specific
            channels             INTEGER,
            sample_rate          INTEGER,
            bits_per_sample      INTEGER,
            duration_seconds     REAL,
            -- Video-specific
            fps                  REAL,
            frame_count          INTEGER,
            video_codec          TEXT,
            has_audio            INTEGER,
            -- Fallback & Container metrics
            is_fallback          INTEGER DEFAULT 0,
            candidate_codec      TEXT,
            fallback_reason      TEXT,
            ahmc_container_size  INTEGER DEFAULT 0,
            container_overhead   INTEGER DEFAULT 0,
            -- File paths
            original_path        TEXT,
            compressed_path      TEXT,
            recovered_path       TEXT,
            created_at           TEXT DEFAULT CURRENT_TIMESTAMP
        )
        """)
        # Ensure new columns exist if upgrading an existing DB
        cols = {row["name"] for row in con.execute("PRAGMA table_info(experiments)").fetchall()}
        for col, col_type in [
            ("fps", "REAL"), ("frame_count", "INTEGER"),
            ("video_codec", "TEXT"), ("has_audio", "INTEGER"),
            ("is_fallback", "INTEGER DEFAULT 0"),
            ("candidate_codec", "TEXT"),
            ("fallback_reason", "TEXT"),
            ("ahmc_container_size", "INTEGER DEFAULT 0"),
            ("container_overhead", "INTEGER DEFAULT 0"),
        ]:
            if col not in cols:
                con.execute(f"ALTER TABLE experiments ADD COLUMN {col} {col_type}")


def save_experiment(session_id: str, filename: str, data: dict,
                    original_path: str, compressed_path: str,
                    recovered_path: str) -> int:
    """Insert a new experiment row and return its id."""
    media = data.get("media_type", "unknown")
    with _conn() as con:
        cur = con.execute("""
        INSERT INTO experiments (
            session_id, filename, media_type,
            original_size, compressed_size, compression_ratio, space_saved_percent,
            predictor, selected_codec, rle_size, huffman_size,
            original_entropy, residual_entropy,
            compression_time_ms, decompression_time_ms, verification,
            width, height, bpp,
            channels, sample_rate, bits_per_sample, duration_seconds,
            fps, frame_count, video_codec, has_audio,
            is_fallback, candidate_codec, fallback_reason,
            ahmc_container_size, container_overhead,
            original_path, compressed_path, recovered_path
        ) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
        """, (
            session_id, filename, media,
            data.get("original_size"), data.get("compressed_size"),
            data.get("compression_ratio"), data.get("space_saved_percent"),
            data.get("predictor"), data.get("selected_codec"),
            data.get("rle_size"), data.get("huffman_size"),
            data.get("original_entropy"), data.get("residual_entropy"),
            data.get("compression_time_ms"), data.get("decompression_time_ms"),
            1 if data.get("verification") else 0,
            data.get("width"), data.get("height"), data.get("bpp"),
            data.get("channels"), data.get("sample_rate"),
            data.get("bits_per_sample"), data.get("duration_seconds"),
            data.get("fps"), data.get("frame_count"),
            data.get("video_codec"), 1 if data.get("has_audio") else 0,
            1 if data.get("is_fallback") else 0,
            data.get("candidate_codec"),
            data.get("fallback_reason"),
            data.get("ahmc_container_size", 0),
            max(0, data.get("container_overhead", 0)),
            original_path, compressed_path, recovered_path,
        ))
        return cur.lastrowid


def update_experiment(exp_id: int, fields: dict) -> bool:
    """Update specific fields of an experiment row."""
    if not fields:
        return False
    keys = list(fields.keys())
    set_clause = ", ".join(f"{k} = ?" for k in keys)
    values = [fields[k] for k in keys] + [exp_id]
    with _conn() as con:
        con.execute(f"UPDATE experiments SET {set_clause} WHERE id = ?", values)
    return True


def get_experiment(exp_id: int) -> Optional[dict]:
    with _conn() as con:
        row = con.execute("SELECT * FROM experiments WHERE id=?", (exp_id,)).fetchone()
        return dict(row) if row else None


def get_all_experiments(limit: int = 200) -> list:
    with _conn() as con:
        rows = con.execute(
            "SELECT * FROM experiments ORDER BY id DESC LIMIT ?", (limit,)
        ).fetchall()
        return [dict(r) for r in rows]


def get_dashboard_stats() -> dict:
    with _conn() as con:
        total = con.execute("SELECT COUNT(*) FROM experiments").fetchone()[0]
        bmps = con.execute("SELECT COUNT(*) FROM experiments WHERE filename LIKE '%.bmp' OR media_type='bmp'").fetchone()[0]
        pngs = con.execute("SELECT COUNT(*) FROM experiments WHERE filename LIKE '%.png' OR media_type='png'").fetchone()[0]
        wavs = con.execute("SELECT COUNT(*) FROM experiments WHERE filename LIKE '%.wav' OR media_type='wav'").fetchone()[0]
        flacs = con.execute("SELECT COUNT(*) FROM experiments WHERE filename LIKE '%.flac' OR media_type='flac'").fetchone()[0]
        avis = con.execute("SELECT COUNT(*) FROM experiments WHERE filename LIKE '%.avi' OR media_type='avi'").fetchone()[0]
        mp4s = con.execute("SELECT COUNT(*) FROM experiments WHERE filename LIKE '%.mp4' OR media_type='mp4'").fetchone()[0]
        images = bmps + pngs
        audio = wavs + flacs
        videos = avis + mp4s
        avg_r = con.execute("SELECT AVG(compression_ratio) FROM experiments WHERE compression_ratio IS NOT NULL").fetchone()[0]
        saved = con.execute("SELECT SUM(original_size - compressed_size) FROM experiments WHERE original_size >= compressed_size").fetchone()[0]
        ver_ok = con.execute("SELECT COUNT(*) FROM experiments WHERE verification=1").fetchone()[0]
        ver_rt = (ver_ok / total * 100) if total > 0 else 0.0
        recent = con.execute(
            "SELECT * FROM experiments ORDER BY id DESC LIMIT 5"
        ).fetchall()
    return {
        "total": total,
        "bmps": bmps,
        "pngs": pngs,
        "wavs": wavs,
        "flacs": flacs,
        "avis": avis,
        "mp4s": mp4s,
        "images": images,
        "audio": audio,
        "videos": videos,
        "avg_ratio": round(avg_r, 3) if avg_r else 0.0,
        "total_saved_bytes": saved or 0,
        "verification_rate": round(ver_rt, 1),
        "recent": [dict(r) for r in recent],
    }


def delete_experiment(exp_id: int):
    with _conn() as con:
        con.execute("DELETE FROM experiments WHERE id=?", (exp_id,))
