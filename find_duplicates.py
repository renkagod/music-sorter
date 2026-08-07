import os
import sys
import hashlib
import logging
from collections import defaultdict

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

def get_file_hash(filepath, block_size=65536):
    hasher = hashlib.md5()
    with open(filepath, 'rb') as f:
        buf = f.read(block_size)
        while len(buf) > 0:
            hasher.update(buf)
            buf = f.read(block_size)
    return hasher.hexdigest()

def get_audio_info(filepath):
    title = os.path.basename(filepath)
    artist = "Unknown Artist"
    album = "Unknown Album"
    duration = 0

    if mutagen:
        try:
            audio = mutagen.File(filepath)
            if audio and hasattr(audio, 'info') and hasattr(audio.info, 'length'):
                duration = round(audio.info.length, 1)
            
            # Read tags
            if audio and audio.tags:
                tags = audio.tags
                # FLAC Vorbis comment
                if hasattr(tags, 'get'):
                    artist = tags.get('artist', [artist])[0] if isinstance(tags.get('artist'), list) else tags.get('artist', artist)
                    title = tags.get('title', [title])[0] if isinstance(tags.get('title'), list) else tags.get('title', title)
                    album = tags.get('album', [album])[0] if isinstance(tags.get('album'), list) else tags.get('album', album)
                # MP3 ID3
                elif hasattr(tags, 'getall'):
                    if tags.get('TPE1'): artist = str(tags.get('TPE1')[0])
                    if tags.get('TIT2'): title = str(tags.get('TIT2')[0])
                    if tags.get('TALB'): album = str(tags.get('TALB')[0])
        except Exception as e:
            logging.debug(f"Could not read tags for {filepath}: {e}")

    return {
        "filepath": filepath,
        "artist": str(artist).strip(),
        "title": str(title).strip(),
        "album": str(album).strip(),
        "duration": duration,
        "size": os.path.getsize(filepath)
    }

def scan_for_duplicates(search_dirs=None):
    if search_dirs is None:
        search_dirs = [
            os.path.join(BASE_DIR, 'TO SORT'),
            os.path.join(BASE_DIR, 'flac'),
            os.path.join(BASE_DIR, 'mp3')
        ]

    logging.info("==================================================")
    logging.info("         STARTING DUPLICATE TRACK SEARCH          ")
    logging.info("==================================================")

    audio_extensions = ('.flac', '.mp3', '.m4a', '.wav', '.ogg')
    hashes = defaultdict(list)
    meta_tracks = defaultdict(list)

    total_scanned = 0

    for d in search_dirs:
        if not os.path.exists(d):
            logging.info(f"Directory {d} does not exist yet. Skipping.")
            continue

        logging.info(f"Scanning directory: {d}")
        for root, _, files in os.walk(d):
            for file in files:
                if file.lower().endswith(audio_extensions):
                    total_scanned += 1
                    filepath = os.path.join(root, file)
                    rel_p = os.path.relpath(filepath, BASE_DIR)

                    logging.info(f"Analyzing file #{total_scanned}: {rel_p}")
                    
                    # 1. Exact hash
                    file_hash = get_file_hash(filepath)
                    hashes[file_hash].append(filepath)

                    # 2. Metadata / Name key
                    info = get_audio_info(filepath)
                    key = (info['artist'].lower(), info['title'].lower(), info['duration'])
                    meta_tracks[key].append(info)

    logging.info("==================================================")
    logging.info(f"Scan complete. Total audio files scanned: {total_scanned}")
    logging.info("==================================================")

    exact_duplicates = {h: paths for h, paths in hashes.items() if len(paths) > 1}
    meta_duplicates = {k: items for k, items in meta_tracks.items() if len(items) > 1}

    if exact_duplicates:
        logging.warning(f"FOUND {len(exact_duplicates)} EXACT FILE DUPLICATES (Identical Hash):")
        for h, paths in exact_duplicates.items():
            logging.warning(f"  Hash {h}:")
            for p in paths:
                logging.warning(f"    - {os.path.relpath(p, BASE_DIR)}")
    else:
        logging.info("No exact file duplicates found.")

    if meta_duplicates:
        logging.warning(f"FOUND {len(meta_duplicates)} POTENTIAL METADATA DUPLICATES (Same Artist + Title + Duration):")
        for (artist, title, dur), items in meta_duplicates.items():
            logging.warning(f"  Track: '{title}' by '{artist}' (~{dur}s):")
            for it in items:
                logging.warning(f"    - {os.path.relpath(it['filepath'], BASE_DIR)} ({it['size']} bytes)")
    else:
        logging.info("No metadata duplicates found.")

    logging.info("--------------------------------------------------")
    logging.info("NOTE: Automatic deletion is prohibited.")
    logging.info("If duplicates were found, please review the log above.")
    logging.info("--------------------------------------------------")

if __name__ == '__main__':
    dirs_to_check = sys.argv[1:] if len(sys.argv) > 1 else None
    scan_for_duplicates(dirs_to_check)
