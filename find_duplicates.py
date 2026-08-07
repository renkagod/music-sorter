import os
import sys
import hashlib
import logging
import difflib
from collections import defaultdict

try:
    import mutagen
except ImportError:
    mutagen = None

try:
    import acoustid
except ImportError:
    acoustid = None

logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(levelname)s: %(message)s',
    datefmt='%H:%M:%S',
    handlers=[logging.StreamHandler(sys.stdout)]
)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

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
        "duration": duration,
        "size": os.path.getsize(filepath),
        "ext": os.path.splitext(filename)[1].lower()
    }

def fuzzy_title_match(t1, t2):
    s1 = t1.lower()
    s2 = t2.lower()
    ratio = difflib.SequenceMatcher(None, s1, s2).ratio()
    # Check substring inclusion or high similarity ratio
    if ratio > 0.7 or (len(s1) > 4 and s1 in s2) or (len(s2) > 4 and s2 in s1):
        return True, ratio
    return False, ratio

def scan_for_duplicates(search_dirs=None):
    if search_dirs is None:
        search_dirs = [
            os.path.join(BASE_DIR, 'TO SORT'),
            os.path.join(BASE_DIR, 'flac'),
            os.path.join(BASE_DIR, 'mp3')
        ]

    logging.info("==================================================")
    logging.info("     MULTI-LEVEL DUPLICATE DETECTOR ACTIVE       ")
    logging.info("==================================================")

    audio_extensions = ('.flac', '.mp3', '.m4a', '.wav', '.ogg')
    hashes = defaultdict(list)
    tracks = []

    total_scanned = 0

    for d in search_dirs:
        if not os.path.exists(d):
            logging.info(f"Directory '{os.path.relpath(d, BASE_DIR)}' does not exist yet. Skipping.")
            continue

        logging.info(f"Scanning directory: {os.path.relpath(d, BASE_DIR)}")
        for root, _, files in os.walk(d):
            for file in files:
                if file.lower().endswith(audio_extensions):
                    total_scanned += 1
                    filepath = os.path.join(root, file)
                    rel_p = os.path.relpath(filepath, BASE_DIR)

                    logging.info(f"  [#{total_scanned}] Scanning: {rel_p}")
                    
                    # 1. Byte hash
                    f_hash = get_file_hash(filepath)
                    hashes[f_hash].append(filepath)

                    # 2. Audio info
                    info = get_audio_info(filepath)
                    tracks.append(info)

    logging.info("==================================================")
    logging.info(f"Scan complete. Total files analyzed: {total_scanned}")
    logging.info("==================================================")

    # 1. Exact MD5 duplicates
    exact_duplicates = {h: paths for h, paths in hashes.items() if len(paths) > 1}
    if exact_duplicates:
        logging.warning(f"⚠️ LEVEL 1: EXACT FILE DUPLICATES (Identical MD5 Hash): {len(exact_duplicates)} groups")
        for h, paths in exact_duplicates.items():
            logging.warning(f"  [Hash {h[:8]}...]:")
            for p in paths:
                logging.warning(f"    -> {os.path.relpath(p, BASE_DIR)}")
    else:
        logging.info("Level 1 (Exact Hash): No duplicates found.")

    # 2. Duration & Fuzzy Title / Localized Duplicate Detection
    possible_dups = []
    n = len(tracks)
    for i in range(n):
        for j in range(i + 1, n):
            t1 = tracks[i]
            t2 = tracks[j]
            
            # Same file path comparison skip
            if t1['filepath'] == t2['filepath']:
                continue

            dur_diff = abs(t1['duration'] - t2['duration'])
            match, ratio = fuzzy_title_match(t1['title'], t2['title'])
            same_artist = (t1['artist'].lower() == t2['artist'].lower() and t1['artist'] != "Unknown Artist")

            # Condition for duplicate suspicion:
            # - Exact same duration (+/- 1.5s) AND (same artist OR fuzzy title match)
            is_suspicious = False
            reason = ""

            if dur_diff <= 1.5 and (same_artist or match):
                is_suspicious = True
                reason = f"Duration match (~{dur_diff:.1f}s diff) + Title similarity ({ratio:.0%})"
            elif match and ratio > 0.85:
                is_suspicious = True
                reason = f"High title similarity ({ratio:.0%})"

            if is_suspicious:
                possible_dups.append((reason, t1, t2))

    if possible_dups:
        logging.warning(f"⚠️ LEVEL 2: POTENTIAL AUDIO DUPLICATES (Different Bitrate / Translated Title / Formats): {len(possible_dups)} pairs")
        for reason, t1, t2 in possible_dups:
            p1 = os.path.relpath(t1['filepath'], BASE_DIR)
            p2 = os.path.relpath(t2['filepath'], BASE_DIR)
            logging.warning(f"  [{reason}]")
            logging.warning(f"    File A: {p1} ({t1['ext'].upper()}, {t1['duration']}s)")
            logging.warning(f"    File B: {p2} ({t2['ext'].upper()}, {t2['duration']}s)")
    else:
        logging.info("Level 2 (Duration + Fuzzy Title): No potential duplicates found.")

    logging.info("--------------------------------------------------")
    logging.info("SAFETY REMINDER: Automatic deletion is FORBIDDEN.")
    logging.info("If duplicates were found above, please confirm with the user before deleting.")
    logging.info("--------------------------------------------------")

if __name__ == '__main__':
    scan_for_duplicates()
