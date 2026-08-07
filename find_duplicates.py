import os
import sys
import hashlib
import logging
import re
import difflib

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

def normalize_text(text):
    if not text:
        return ""
    # Strip release metadata tags like [C87], {DVAC-0004}, (2014)
    s = re.sub(r'\[[^\]]*\]|\{[^\}]*\}|\(\d{4}\)', '', text)
    # Remove unicode slashes, punctuation, spaces
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

def scan_for_duplicates(search_dirs=None):
    if search_dirs is None:
        search_dirs = [
            os.path.join(BASE_DIR, 'TO SORT'),
            os.path.join(BASE_DIR, 'flac'),
            os.path.join(BASE_DIR, 'mp3')
        ]

    logging.info("==================================================")
    logging.info("    ROBUST MULTI-LEVEL DUPLICATE SCANNER          ")
    logging.info("==================================================")

    audio_extensions = ('.flac', '.mp3', '.m4a', '.wav', '.ogg')
    hashes = defaultdict(list)
    tracks = []

    total_scanned = 0

    for d in search_dirs:
        if not os.path.exists(d):
            logging.info(f"Directory '{os.path.relpath(d, BASE_DIR)}' does not exist. Skipping.")
            continue

        logging.info(f"Scanning directory: {os.path.relpath(d, BASE_DIR)}")
        for root, _, files in os.walk(d):
            for file in files:
                if file.lower().endswith(audio_extensions):
                    total_scanned += 1
                    filepath = os.path.join(root, file)
                    rel_p = os.path.relpath(filepath, BASE_DIR)

                    logging.info(f"  [#{total_scanned}] Analyzed: {rel_p}")
                    
                    f_hash = get_file_hash(filepath)
                    hashes[f_hash].append(filepath)

                    info = get_audio_info(filepath)
                    tracks.append(info)

    logging.info("==================================================")
    logging.info(f"Scan complete. Total files analyzed: {total_scanned}")
    logging.info("==================================================")

    # 1. Exact MD5
    exact_duplicates = {h: paths for h, paths in hashes.items() if len(paths) > 1}
    if exact_duplicates:
        logging.warning(f"⚠️ LEVEL 1: EXACT FILE DUPLICATES (MD5): {len(exact_duplicates)} groups")
        for h, paths in exact_duplicates.items():
            logging.warning(f"  [Hash {h[:8]}...]:")
            for p in paths:
                logging.warning(f"    -> {os.path.relpath(p, BASE_DIR)}")
    else:
        logging.info("Level 1 (Exact Hash): No duplicates found.")

    # 2. Robust Normalized Title + Duration Match
    possible_dups = []
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

            # Match criteria: same normalized title OR (duration diff <= 1.0s and high title similarity)
            is_dup = False
            reason = ""

            if n1 and n2 and n1 == n2 and dur_diff <= 2.0:
                is_dup = True
                reason = f"Identical title '{n1}' + Duration match ({dur_diff:.1f}s diff)"
            elif dur_diff <= 1.0 and ratio > 0.7:
                is_dup = True
                reason = f"Duration match ({dur_diff:.1f}s diff) + Title similarity ({ratio:.0%})"

            if is_dup:
                possible_dups.append((reason, t1, t2))

    if possible_dups:
        logging.warning(f"⚠️ LEVEL 2: POTENTIAL DUPLICATES: {len(possible_dups)} pairs")
        for reason, t1, t2 in possible_dups:
            p1 = os.path.relpath(t1['filepath'], BASE_DIR)
            p2 = os.path.relpath(t2['filepath'], BASE_DIR)
            logging.warning(f"  [{reason}]")
            logging.warning(f"    File A: {p1} ({t1['ext'].upper()}, {t1['duration']}s)")
            logging.warning(f"    File B: {p2} ({t2['ext'].upper()}, {t2['duration']}s)")
    else:
        logging.info("Level 2 (Normalized Title + Duration): No potential duplicates found.")

if __name__ == '__main__':
    scan_for_duplicates()
