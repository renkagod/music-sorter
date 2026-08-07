import os
import sys
import re
import mutagen

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
TRACKLIST_PATH = os.path.join(BASE_DIR, 'tracklist.md')

def normalize_text(text):
    if not text:
        return ""
    # Strip release metadata tags in brackets [], braces {}, or parentheses ()
    s = re.sub(r'\[[^\]]*\]|\{[^\}]*\}|\([^\)]*\)', '', text)
    # Remove unicode slashes (⁄), spaces, hyphens, punctuation
    s = re.sub(r'[\s\-_/\\,.\u2044\u2215\u3013\uFF5E]+', '', s)
    return s.lower().strip()

def check_coverage(artist_filter=None):
    if not os.path.exists(TRACKLIST_PATH):
        print(f"Error: {TRACKLIST_PATH} not found.")
        return

    with open(TRACKLIST_PATH, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    db = {}
    current_artist = None
    current_album = None

    for line in lines:
        l = line.strip()
        if l.startswith("## 👤 "):
            current_artist = l.replace("## 👤 ", "").strip()
            db[current_artist] = {}
            current_album = None
        elif l.startswith("### 💿 ") and current_artist:
            current_album = l.replace("### 💿 ", "").strip()
            db[current_artist][current_album] = []
        elif l.startswith("- [") and current_artist and current_album:
            m = re.search(r'\*\*([^*]+)\*\*', l)
            if m:
                db[current_artist][current_album].append(m.group(1).strip())

    scanned_files = []
    for sub in ['TO SORT', 'flac', 'mp3', 'review']:
        p = os.path.join(BASE_DIR, sub)
        if not os.path.exists(p): continue
        for root, _, files in os.walk(p):
            for f in files:
                if f.lower().endswith(('.flac', '.mp3', '.wav', '.m4a')):
                    fp = os.path.join(root, f)
                    rel = os.path.relpath(fp, BASE_DIR)
                    ext = os.path.splitext(f)[1].lower()
                    dur = 0.0
                    title = os.path.splitext(f)[0]
                    try:
                        audio = mutagen.File(fp)
                        if audio and hasattr(audio, 'info') and hasattr(audio.info, 'length'):
                            dur = round(audio.info.length, 1)
                        if audio and audio.tags and hasattr(audio.tags, 'get'):
                            t_val = audio.tags.get('title', [title])
                            title = t_val[0] if isinstance(t_val, list) else t_val
                    except: pass

                    scanned_files.append({
                        'filename': f,
                        'filepath': fp,
                        'rel': rel,
                        'ext': ext,
                        'dur': dur,
                        'title': str(title).strip(),
                        'norm_title': normalize_text(str(title)),
                        'norm_rel': normalize_text(rel)
                    })

    print("==================================================")
    print("      ACCURATE TRACKLIST COVERAGE REPORT          ")
    print("==================================================")

    for artist_name, albums in sorted(db.items()):
        if artist_filter and artist_filter.lower() not in artist_name.lower():
            continue

        print(f"\n👤 Artist: {artist_name}")
        print("=" * (len(artist_name) + 12))

        for album_name, db_tracks in sorted(albums.items()):
            norm_alb = normalize_text(album_name)

            matched = [s for s in scanned_files if norm_alb in s['norm_rel']]
            
            flac_files = [f for f in matched if f['ext'] == '.flac']
            mp3_files = [f for f in matched if f['ext'] == '.mp3']

            if flac_files and mp3_files:
                status = f"🟢 BOTH FLAC ({len(flac_files)}) & MP3 ({len(mp3_files)})"
            elif flac_files:
                status = f"🟢 FLAC ONLY ({len(flac_files)} tracks)"
            elif mp3_files:
                status = f"🟡 MP3 ONLY ({len(mp3_files)} tracks) — Ready for FLAC upgrade"
            else:
                status = "🔴 NOT DOWNLOADED YET"

            print(f"\n  💿 {album_name}")
            print(f"     Status: {status}")
            if matched:
                print(f"     Downloaded files ({len(matched)} total):")
                for m in matched[:5]:
                    print(f"       - {m['filename']} [{m['ext'].upper()}] ({m['dur']}s)")
                if len(matched) > 5:
                    print(f"       ... and {len(matched) - 5} more files")
            else:
                print("     (No files found locally for this album)")

if __name__ == '__main__':
    arg = sys.argv[1] if len(sys.argv) > 1 else None
    check_coverage(arg)
