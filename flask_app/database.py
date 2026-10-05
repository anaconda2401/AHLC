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
            -- File paths
            original_path        TEXT,
            compressed_path      TEXT,
            recovered_path       TEXT,
            created_at           TEXT DEFAULT CURRENT_TIMESTAMP
        )
        """)


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
            original_path, compressed_path, recovered_path
        ) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
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
            original_path, compressed_path, recovered_path,
        ))
        return cur.lastrowid


def get_experiment(exp_id: int) -> Optional[dict]:
    with _conn() as con:
        row = con.execute("SELECT * FROM experiments WHERE id=?", (exp_id,)).fetchone()
        return dict(row) if row else None


def get_all_experiments(limit: int = 200) -> list:
    with _conn() as con:
        rows = con.execute(
            "SELECT * FROM experiments ORDER BY created_at DESC LIMIT ?", (limit,)
        ).fetchall()
        return [dict(r) for r in rows]


def get_dashboard_stats() -> dict:
    with _conn() as con:
        total  = con.execute("SELECT COUNT(*) FROM experiments").fetchone()[0]
        images = con.execute("SELECT COUNT(*) FROM experiments WHERE media_type='image'").fetchone()[0]
        audio  = con.execute("SELECT COUNT(*) FROM experiments WHERE media_type='audio'").fetchone()[0]
        avg_r  = con.execute("SELECT AVG(compression_ratio) FROM experiments WHERE compression_ratio IS NOT NULL").fetchone()[0]
        saved  = con.execute("SELECT SUM(original_size - compressed_size) FROM experiments").fetchone()[0]
        ver_ok = con.execute("SELECT COUNT(*) FROM experiments WHERE verification=1").fetchone()[0]
        ver_rt = (ver_ok / total * 100) if total > 0 else 0.0
        recent = con.execute(
            "SELECT * FROM experiments ORDER BY created_at DESC LIMIT 5"
        ).fetchall()
    return {
        "total": total,
        "images": images,
        "audio": audio,
        "avg_ratio": round(avg_r, 3) if avg_r else 0.0,
        "total_saved_bytes": saved or 0,
        "verification_rate": round(ver_rt, 1),
        "recent": [dict(r) for r in recent],
    }


def delete_experiment(exp_id: int):
    with _conn() as con:
        con.execute("DELETE FROM experiments WHERE id=?", (exp_id,))
