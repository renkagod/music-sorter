import os
import sys
import shutil
import logging
import re
import difflib
from collections import defaultdict
import numpy as np

try:
    import librosa
    import mutagen
except ImportError:
    print("Error importing audio libraries.")
    sys.exit(1)

logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(levelname)s: %(message)s',
    datefmt='%H:%M:%S',
    handlers=[logging.StreamHandler(sys.stdout)]
)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
TOSORT_DIR = os.path.join(BASE_DIR, 'TO SORT')
REVIEW_DIR = os.path.join(BASE_DIR, 'review')

os.makedirs(REVIEW_DIR, exist_ok=True)

def normalize_text(text):
    if not text: return ""
    s = re.sub(r'\[[^\]]*\]|\{[^\}]*\}|\(\d{4}\)', '', text)
    s = re.sub(r'[\s\-_/\\,.\u2044\u2215\u3013\uFF5E]+', '', s)
    return s.lower().strip()

def compute_dtw_chroma_similarity(file1, file2, duration_sec=40):
    try:
        y1, sr1 = librosa.load(file1, sr=22050, duration=duration_sec)
        y2, sr2 = librosa.load(file2, sr=22050, duration=duration_sec)
        c1 = librosa.feature.chroma_cens(y=y1, sr=sr1)
        c2 = librosa.feature.chroma_cens(y=y2, sr=sr2)
        D, wp = librosa.sequence.dtw(c1, c2, metric='cosine')
        dist = D[-1, -1] / len(wp)
        return float(1.0 - dist)
    except Exception as e:
        logging.debug(f"DTW calculation error: {e}")
        return 0.0

def move_to_review(filepath, reason):
    rel = os.path.relpath(filepath, TOSORT_DIR)
    target_path = os.path.join(REVIEW_DIR, rel)
    os.makedirs(os.path.dirname(target_path), exist_ok=True)
    logging.info(f"[DTW QUARANTINE] Moving redundant MP3 duplicate: {rel}")
    logging.info(f"  Reason: {reason}")
    shutil.move(filepath, target_path)

def run_dtw_quarantine():
    logging.info("Starting DTW Chroma Acoustic Quarantine analysis...")
    audio_extensions = ('.flac', '.mp3')
    tracks = []

    for root, _, files in os.walk(TOSORT_DIR):
        for f in files:
            if f.lower().endswith(audio_extensions):
                fp = os.path.join(root, f)
                ext = os.path.splitext(f)[1].lower()
                dur = 0.0
                title = os.path.splitext(f)[0]
                try:
                    m = mutagen.File(fp)
                    if m and hasattr(m, 'info') and hasattr(m.info, 'length'):
                        dur = round(m.info.length, 1)
                    if m and m.tags and hasattr(m.tags, 'get'):
                        t_val = m.tags.get('title', [title])
                        title = t_val[0] if isinstance(t_val, list) else t_val
                except: pass

                tracks.append({
                    'filepath': fp,
                    'filename': f,
                    'ext': ext,
                    'dur': dur,
                    'title': str(title),
                    'norm_title': normalize_text(str(title))
                })

    n = len(tracks)
    moved_count = 0

    for i in range(n):
        for j in range(i + 1, n):
            t1, t2 = tracks[i], tracks[j]
            if not os.path.exists(t1['filepath']) or not os.path.exists(t2['filepath']):
                continue

            if (t1['ext'] == '.flac' and t2['ext'] == '.mp3') or (t1['ext'] == '.mp3' and t2['ext'] == '.flac'):
                dur_diff = abs(t1['dur'] - t2['dur'])
                n1, n2 = t1['norm_title'], t2['norm_title']
                ratio = difflib.SequenceMatcher(None, n1, n2).ratio() if n1 and n2 else 0

                if dur_diff <= 3.0 and (n1 == n2 or ratio > 0.70):
                    sim = compute_dtw_chroma_similarity(t1['filepath'], t2['filepath'])
                    flac_track = t1 if t1['ext'] == '.flac' else t2
                    mp3_track = t2 if t1['ext'] == '.flac' else t1

                    if sim >= 0.94:
                        move_to_review(mp3_track['filepath'], f"DTW Chroma Acoustic Match ({sim:.1%} similarity) with FLAC '{flac_track['filename']}'")
                        moved_count += 1
                    else:
                        logging.info(f"DISTINCT AUDIO MIXES ({sim:.1%} similarity): Keeping both '{t1['filename']}' and '{t2['filename']}'")

    logging.info(f"DTW Quarantine Complete. Total redundant MP3 duplicates moved: {moved_count}")

if __name__ == '__main__':
    run_dtw_quarantine()
