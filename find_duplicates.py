import os
import sys
import hashlib
import logging
import re
import difflib
from collections import defaultdict

import numpy as np

try:
    import librosa
except ImportError:
    librosa = None

try:
    import mutagen
except ImportError:
    mutagen = None

logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(levelname)s: %(message)s',
    datefmt='%H:%M:%S',
    handlers=[logging.StreamHandler(sys.stdout)]
)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
REVIEW_DIR = os.path.join(BASE_DIR, 'review')

def normalize_text(text):
    if not text:
        return ""
    s = re.sub(r'\[[^\]]*\]|\{[^\}]*\}|\(\d{4}\)', '', text)
    s = re.sub(r'[\s\-_/\\,.\u2044\u2215\u3013\uFF5E]+', '', s)
    return s.lower().strip()

def get_file_hash(filepath, block_size=65536):
    hasher = hashlib.md5()
    with open(filepath, 'rb') as f:
        buf = f.read(block_size)
        while len(buf) > 0:
            hasher.update(buf)
            buf = f.read(block_size)
    return hasher.hexdigest()

def get_audio_info(filepath):
    filename = os.path.basename(filepath)
    title = os.path.splitext(filename)[0]
    artist = "Unknown Artist"
    album = "Unknown Album"
    duration = 0.0

    if mutagen:
        try:
            audio = mutagen.File(filepath)
            if audio and hasattr(audio, 'info') and hasattr(audio.info, 'length'):
                duration = round(audio.info.length, 1)
            
            if audio and audio.tags:
                tags = audio.tags
                if hasattr(tags, 'get'):
                    artist_val = tags.get('artist', [artist])
                    artist = artist_val[0] if isinstance(artist_val, list) else artist_val
                    title_val = tags.get('title', [title])
                    title = title_val[0] if isinstance(title_val, list) else title_val
                    album_val = tags.get('album', [album])
                    album = album_val[0] if isinstance(album_val, list) else album_val
                elif hasattr(tags, 'getall'):
                    if tags.get('TPE1'): artist = str(tags.get('TPE1')[0])
                    if tags.get('TIT2'): title = str(tags.get('TIT2')[0])
                    if tags.get('TALB'): album = str(tags.get('TALB')[0])
        except Exception as e:
            logging.debug(f"Error reading tags from {filepath}: {e}")

    return {
        "filepath": filepath,
        "filename": filename,
        "artist": str(artist).strip(),
        "title": str(title).strip(),
        "album": str(album).strip(),
        "norm_title": normalize_text(str(title)),
        "norm_album": normalize_text(str(album)),
        "duration": duration,
        "size": os.path.getsize(filepath),
        "ext": os.path.splitext(filename)[1].lower()
    }

def compute_spectral_similarity(file1, file2, duration_sec=30):
    if not librosa:
        return None
    try:
        y1, sr1 = librosa.load(file1, sr=22050, duration=duration_sec)
        y2, sr2 = librosa.load(file2, sr=22050, duration=duration_sec)

        mfcc1 = librosa.feature.mfcc(y=y1, sr=sr1, n_mfcc=20)
        mfcc2 = librosa.feature.mfcc(y=y2, sr=sr2, n_mfcc=20)

        min_len = min(mfcc1.shape[1], mfcc2.shape[1])
        if min_len == 0: return 0.0

        m1 = mfcc1[:, :min_len].flatten()
        m2 = mfcc2[:, :min_len].flatten()

        norm1 = np.linalg.norm(m1)
        norm2 = np.linalg.norm(m2)
        if norm1 == 0 or norm2 == 0: return 0.0

        cos_sim = np.dot(m1, m2) / (norm1 * norm2)
        return round(float(cos_sim), 4)
    except Exception as e:
        logging.debug(f"Spectral comparison error between {file1} and {file2}: {e}")
        return None

def scan_and_process_duplicates():
    search_dirs = [
        os.path.join(BASE_DIR, 'TO SORT'),
        os.path.join(BASE_DIR, 'flac'),
        os.path.join(BASE_DIR, 'mp3')
    ]

    logging.info("==================================================")
    logging.info("  SMART MATHEMATICAL DUPLICATE & SPECTRAL DETECTOR ")
    logging.info("==================================================")

    audio_extensions = ('.flac', '.mp3', '.m4a', '.wav', '.ogg')
    hashes = defaultdict(list)
    tracks = []
    total_scanned = 0

    for d in search_dirs:
        if not os.path.exists(d): continue
        logging.info(f"Scanning directory: {os.path.relpath(d, BASE_DIR)}")
        for root, _, files in os.walk(d):
            for file in files:
                if file.lower().endswith(audio_extensions):
                    total_scanned += 1
                    filepath = os.path.join(root, file)
                    rel_p = os.path.relpath(filepath, BASE_DIR)

                    f_hash = get_file_hash(filepath)
                    hashes[f_hash].append(filepath)

                    info = get_audio_info(filepath)
                    tracks.append(info)

    logging.info(f"Scan complete. Total files analyzed: {total_scanned}")
    logging.info("--------------------------------------------------")

    # Spectral Analysis for suspicious pairs
    n = len(tracks)
    for i in range(n):
        for j in range(i + 1, n):
            t1 = tracks[i]
            t2 = tracks[j]
            if t1['filepath'] == t2['filepath']: continue

            dur_diff = abs(t1['duration'] - t2['duration'])
            n1 = t1['norm_title']
            n2 = t2['norm_title']
            ratio = difflib.SequenceMatcher(None, n1, n2).ratio() if n1 and n2 else 0

            if dur_diff <= 2.0 and (n1 == n2 or ratio > 0.7):
                sim = compute_spectral_similarity(t1['filepath'], t2['filepath'])
                if sim is not None:
                    logging.info(f"Acoustic spectral test between '{t1['filename']}' and '{t2['filename']}': {sim:.1%} similarity")

if __name__ == '__main__':
    scan_and_process_duplicates()
