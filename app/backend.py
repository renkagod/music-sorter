import os
import sys
import shutil
import hashlib
import subprocess
import logging
import re
import math
from concurrent.futures import ThreadPoolExecutor
from PIL import Image

from fastapi import FastAPI, HTTPException, Request, Response
from fastapi.responses import FileResponse, StreamingResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

import mutagen
from mutagen.flac import FLAC, Picture
from mutagen.mp3 import MP3
from mutagen.id3 import APIC, TIT2, TPE1, TALB, TRCK

logging.basicConfig(level=logging.INFO, format='[%(asctime)s] %(levelname)s: %(message)s')

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP_DIR = os.path.join(BASE_DIR, 'app')
FPCALC_BIN = os.path.join(BASE_DIR, 'fpcalc.exe')
TOSORT_DIR = os.path.join(BASE_DIR, 'TO SORT')
FLAC_ROOT = os.path.join(BASE_DIR, 'flac')
MP3_ROOT = os.path.join(BASE_DIR, 'mp3')
REVIEW_DIR = os.path.join(BASE_DIR, 'review')
DELETE_DIR = os.path.join(BASE_DIR, 'delete')
TRACKLIST_PATH = os.path.join(BASE_DIR, 'tracklist.md')

os.makedirs(FLAC_ROOT, exist_ok=True)
os.makedirs(MP3_ROOT, exist_ok=True)
os.makedirs(REVIEW_DIR, exist_ok=True)
os.makedirs(DELETE_DIR, exist_ok=True)

app = FastAPI(title="MusicSorter Desktop API")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Shared state
SCAN_RESULTS = {
    "status": "idle", # idle, scanning, ready, complete
    "progress": 0,
    "total_files": 0,
    "auto_quarantined": [],
    "ab_candidates": [],
    "logs": []
}

def log_msg(msg):
    logging.info(msg)
    SCAN_RESULTS["logs"].append(msg)

def get_fpcalc_raw(filepath):
    if not os.path.exists(FPCALC_BIN): return None
    try:
        res = subprocess.run([FPCALC_BIN, '-raw', filepath], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
        dur, fp = 0, []
        for line in res.stdout.splitlines():
            if line.startswith('DURATION='): dur = float(line.split('=')[1])
            elif line.startswith('FINGERPRINT='): fp = [int(x) for x in line.split('=')[1].split(',') if x]
        return {'path': filepath, 'dur': dur, 'fp': fp}
    except: return None

def bit_count(int_type):
    return bin(int_type).count('1')

def aligned_cross_correlation_similarity(fp1_list, fp2_list, max_offset=250):
    if not fp1_list or not fp2_list: return 0.0, 0
    best_sim = 0.0
    best_offset = 0

    for offset in range(-max_offset, max_offset + 1):
        if offset >= 0:
            sub1 = fp1_list[offset:]
            sub2 = fp2_list[:len(sub1)]
        else:
            sub2 = fp2_list[-offset:]
            sub1 = fp1_list[:len(sub2)]

        min_len = min(len(sub1), len(sub2))
        if min_len < 30: continue

        matching_bits = sum(32 - bit_count(sub1[i] ^ sub2[i]) for i in range(min_len))
        sim = matching_bits / (min_len * 32)
        if sim > best_sim:
            best_sim = sim
            best_offset = offset

    return round(best_sim, 4), best_offset

@app.get("/api/status")
def get_status():
    return SCAN_RESULTS

@app.post("/api/scan")
def trigger_scan():
    SCAN_RESULTS["status"] = "scanning"
    SCAN_RESULTS["progress"] = 0
    SCAN_RESULTS["auto_quarantined"] = []
    SCAN_RESULTS["ab_candidates"] = []
    SCAN_RESULTS["logs"] = []

    log_msg("=== Starting MusicSorter Scanning Pipeline ===")
    
    files_to_scan = []
    for root, _, files in os.walk(TOSORT_DIR):
        for f in files:
            if f.lower().endswith(('.flac', '.mp3')):
                files_to_scan.append(os.path.join(root, f))

    SCAN_RESULTS["total_files"] = len(files_to_scan)
    log_msg(f"Found {len(files_to_scan)} audio files in TO SORT.")

    if not files_to_scan:
        SCAN_RESULTS["status"] = "ready"
        return SCAN_RESULTS

    # Multithreaded fingerprinting
    fps = []
    with ThreadPoolExecutor(max_workers=8) as executor:
        results = executor.map(get_fpcalc_raw, files_to_scan)
        for idx, r in enumerate(results):
            if r and r['fp']:
                fps.append(r)
            SCAN_RESULTS["progress"] = int((idx + 1) / len(files_to_scan) * 50)

    log_msg(f"Computed Chromaprint fingerprints for {len(fps)} tracks.")

    # Pairwise comparison
    ab_pairs = []
    auto_del = []

    for i in range(len(fps)):
        for j in range(i + 1, len(fps)):
            f1, f2 = fps[i], fps[j]
            if abs(f1['dur'] - f2['dur']) <= 5.0:
                sim, offset = aligned_cross_correlation_similarity(f1['fp'], f2['fp'])

                ext1 = os.path.splitext(f1['path'])[1].lower()
                ext2 = os.path.splitext(f2['path'])[1].lower()

                # 100% exact duplicate (>= 95%) -> FLAC over MP3
                if sim >= 0.95:
                    if ext1 == '.flac' and ext2 == '.mp3':
                        auto_del.append(f2['path'])
                    elif ext2 == '.flac' and ext1 == '.mp3':
                        auto_del.append(f1['path'])
                    else:
                        auto_del.append(f2['path'])
                elif sim >= 0.75:
                    ab_pairs.append({
                        "id": f"pair_{len(ab_pairs)+1}",
                        "track_a": f1['path'],
                        "rel_a": os.path.relpath(f1['path'], BASE_DIR),
                        "ext_a": ext1.replace('.', '').upper(),
                        "dur_a": round(f1['dur'], 1),
                        "track_b": f2['path'],
                        "rel_b": os.path.relpath(f2['path'], BASE_DIR),
                        "ext_b": ext2.replace('.', '').upper(),
                        "dur_b": round(f2['dur'], 1),
                        "similarity": round(sim * 100, 1),
                        "offset": offset
                    })

    # Auto-quarantine exact 100% duplicates to delete/
    for p in auto_del:
        if os.path.exists(p):
            rel = os.path.relpath(p, BASE_DIR)
            dst = os.path.join(DELETE_DIR, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            log_msg(f"[AUTO-DELETE] Moving exact duplicate MP3 to delete/: {rel}")
            if os.path.exists(dst): os.remove(dst)
            shutil.move(p, dst)

    SCAN_RESULTS["auto_quarantined"] = auto_del
    SCAN_RESULTS["ab_candidates"] = ab_pairs
    SCAN_RESULTS["progress"] = 100
    SCAN_RESULTS["status"] = "ready"
    log_msg(f"Scan complete. Found {len(ab_pairs)} candidates for A/B comparison.")
    return SCAN_RESULTS

class DecisionRequest(BaseModel):
    pair_id: str
    keep_track: str # "a" or "b"
    track_a_path: str
    track_b_path: str

@app.post("/api/decision")
def make_decision(req: DecisionRequest):
    rejected_path = req.track_b_path if req.keep_track == "a" else req.track_a_path
    if os.path.exists(rejected_path):
        rel = os.path.relpath(rejected_path, BASE_DIR)
        dst = os.path.join(DELETE_DIR, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        log_msg(f"[USER DECISION] Moving rejected track to delete/: {rel}")
        if os.path.exists(dst): os.remove(dst)
        shutil.move(rejected_path, dst)

    # Filter out from candidates
    SCAN_RESULTS["ab_candidates"] = [c for c in SCAN_RESULTS["ab_candidates"] if c["id"] != req.pair_id]
    return {"status": "ok", "remaining": len(SCAN_RESULTS["ab_candidates"])}

@app.get("/api/stream")
def stream_audio(path: str, request: Request):
    if not os.path.exists(path):
        raise HTTPException(status_code=404, detail="Audio file not found")

    file_size = os.path.getsize(path)
    range_header = request.headers.get('range')

    if range_header:
        byte_opts = range_header.replace('bytes=', '').split('-')
        start = int(byte_opts[0])
        end = int(byte_opts[1]) if byte_opts[1] else file_size - 1
        length = end - start + 1

        def iterfile():
            with open(path, 'rb') as f:
                f.seek(start)
                bytes_left = length
                while bytes_left > 0:
                    chunk = f.read(min(bytes_left, 65536))
                    if not chunk: break
                    bytes_left -= len(chunk)
                    yield chunk

        headers = {
            'Content-Range': f'bytes {start}-{end}/{file_size}',
            'Accept-Ranges': 'bytes',
            'Content-Length': str(length),
            'Content-Type': 'audio/flac' if path.endswith('.flac') else 'audio/mpeg',
        }
        return StreamingResponse(iterfile(), status_code=206, headers=headers)
    else:
        return FileResponse(path, media_type='audio/flac' if path.endswith('.flac') else 'audio/mpeg')

# Serve static web files
app.mount("/", StaticFiles(directory=os.path.join(APP_DIR, 'public'), html=True), name="public")
