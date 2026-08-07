import os
import sys
import shutil
import logging
import subprocess
import re
from PIL import Image

try:
    import mutagen
    from mutagen.flac import FLAC, Picture
    from mutagen.id3 import ID3, APIC, TIT2, TPE1, TALB, TRCK
    from mutagen.mp3 import MP3
except ImportError:
    print("Mutagen is required.")
    sys.exit(1)

logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(levelname)s: %(message)s',
    datefmt='%H:%M:%S',
    handlers=[logging.StreamHandler(sys.stdout)]
)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
FPCALC_BIN = os.path.join(BASE_DIR, 'fpcalc.exe')
TOSORT_DIR = os.path.join(BASE_DIR, 'TO SORT')
FLAC_DIR = os.path.join(BASE_DIR, 'flac', "D'va;;;;;;;;5")
MP3_DIR = os.path.join(BASE_DIR, 'mp3', "D'va;;;;;;;;5")
REVIEW_DIR = os.path.join(BASE_DIR, 'review')
TRACKLIST_PATH = os.path.join(BASE_DIR, 'tracklist.md')

os.makedirs(FLAC_DIR, exist_ok=True)
os.makedirs(MP3_DIR, exist_ok=True)
os.makedirs(REVIEW_DIR, exist_ok=True)

DISCOGS_MAP = [
    ('45CD Project (2003)', ['45cd project', '45cd']),
    ('紅花翁草 (2004)', ['紅花翁草', 'anemone']),
    ('Candybug (2005)', ['candybug']),
    ('クロユリ (2006)', ['クロユリ']),
    ('ざくろ (2007)', ['ざくろ']),
    ('アムリタ (2009)', ['アムリタ']),
    ('タイタニア (2010)', ['タイタニア']),
    ('Sun Fleur 1-2 (2012)', ['sun fleur 1', 'sun fleur 1/2', 'sun fleur 1／2']),
    ('AAAAAA (2013)', ['aaaaaa']),
    ('Sun Fleur (2014)', ['sun fleur']),
    ('マリスコール (2014)', ['マリスコール']),
    ('メイリリィ (2015)', ['メイリリィ']),
    ('BlackLotus (2015)', ['blacklotus']),
    ('空木ノ花試作型 (2017)', ['空木ノ花試作型']),
    ('空木ノ花 (2017)', ['空木ノ花']),
    ('モノクロ (2007)', ['monokuro', 'モノクロ']),
    ('Ankoku Douwa (2008)', ['ankoku douwa']),
    ('畸茎樹 (2008)', ['kikeiju', '畸茎樹']),
    ('Kakurika (2008)', ['kakurika']),
    ('厭離穢土 (2009)', ['enri edo', '厭離穢土']),
    ('Kukuri (2009)', ['kukuri']),
    ('Midori (2010)', ['midori']),
    ('Iranai Ver 0.5 (2010)', ['iranai ver 0.5', 'iranai 0.5']),
    ('Iranai (2011)', ['iranai']),
    ('Haiiro (2011)', ['haiiro']),
    ('Carnival (2012)', ['carnival']),
    ('Border (2012)', ['border']),
    ('Wakuraba (2013)', ['wakuraba']),
]

def normalize_key(name):
    if not name: return ""
    s = re.sub(r'\[[^\]]*\]|\{[^\}]*\}|\([^\)]*\)', '', name)
    s = re.sub(r'[\s\-_/\\,.\u2044\u2215\u3013\uFF5E]+', '', s)
    return s.lower().strip()

def match_canonical_album(full_path):
    parts = full_path.replace(TOSORT_DIR, '').split(os.sep)
    full_str = " ".join(parts)
    norm_full = normalize_key(full_str)

    for canonical, keys in DISCOGS_MAP:
        for k in keys:
            norm_k = normalize_key(k)
            if norm_k in norm_full:
                return canonical

    if 'archive' in norm_full: return 'Archives'
    return 'Unsorted'

def get_fpcalc_raw(filepath):
    if not os.path.exists(FPCALC_BIN): return None
    try:
        res = subprocess.run([FPCALC_BIN, '-raw', filepath], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
        dur, fp = 0, []
        for line in res.stdout.splitlines():
            if line.startswith('DURATION='): dur = float(line.split('=')[1])
            elif line.startswith('FINGERPRINT='): fp = [int(x) for x in line.split('=')[1].split(',') if x]
        return {'duration': dur, 'fingerprint': fp}
    except: return None

def bit_count(int_type):
    return bin(int_type).count('1')

def fingerprint_similarity(fp1_list, fp2_list):
    if not fp1_list or not fp2_list: return 0.0
    min_len = min(len(fp1_list), len(fp2_list))
    if min_len == 0: return 0.0
    matching_bits = sum(32 - bit_count(fp1_list[i] ^ fp2_list[i]) for i in range(min_len))
    return round(matching_bits / (min_len * 32), 4)

def run_acoustid_quarantine():
    logging.info("--- Step 1: AcoustID Chromaprint Duplicate Quarantine Scan ---")
    flacs, mp3s = [], []
    for root, _, files in os.walk(TOSORT_DIR):
        for f in files:
            p = os.path.join(root, f)
            if f.lower().endswith('.flac'): flacs.append(p)
            elif f.lower().endswith('.mp3'): mp3s.append(p)

    flac_fps = []
    for p in flacs:
        info = get_fpcalc_raw(p)
        if info and info['fingerprint']:
            flac_fps.append({'path': p, 'file': os.path.basename(p), 'dur': info['duration'], 'fp': info['fingerprint']})

    moved_count = 0
    for p in mp3s:
        if not os.path.exists(p): continue
        info = get_fpcalc_raw(p)
        if info and info['fingerprint']:
            m_dur, m_fp, m_file = info['duration'], info['fingerprint'], os.path.basename(p)
            best_sim, best_flac = 0.0, None

            for f in flac_fps:
                if abs(m_dur - f['dur']) <= 3.0:
                    sim = fingerprint_similarity(m_fp, f['fp'])
                    if sim > best_sim:
                        best_sim, best_flac = sim, f

            if best_sim >= 0.85:
                rel = os.path.relpath(p, TOSORT_DIR)
                target_path = os.path.join(REVIEW_DIR, rel)
                os.makedirs(os.path.dirname(target_path), exist_ok=True)
                logging.info(f"[QUARANTINE] Moving redundant MP3 to review/: {rel} (Match: {best_sim:.1%})")
                shutil.move(p, target_path)
                moved_count += 1
    logging.info(f"Step 1 Complete. Redundant MP3s moved to review/: {moved_count}")

def find_cover_image(folder_path):
    candidates = ['cover.jpg', 'cover.png', 'cover.jpeg', 'folder.jpg', 'folder.png', 'folder.jpeg', 'cover.JPG']
    for root, _, files in os.walk(folder_path):
        for file in files:
            if file in candidates or file.lower().endswith(('.jpg', '.png', '.jpeg')):
                return os.path.join(root, file)
    return None

def embed_cover_art_flac(audio, image_path):
    if not image_path or not os.path.exists(image_path): return
    try:
        picture = Picture()
        with open(image_path, 'rb') as f: picture.data = f.read()
        im = Image.open(image_path)
        picture.type = 3
        picture.mime = f"image/{im.format.lower()}" if im.format else "image/jpeg"
        picture.width, picture.height = im.width, im.height
        picture.depth = 24
        audio.clear_pictures()
        audio.add_picture(picture)
    except Exception: pass

def embed_cover_art_mp3(audio, image_path):
    if not image_path or not os.path.exists(image_path): return
    try:
        if audio.tags is None: audio.add_tags()
        im = Image.open(image_path)
        mime = f"image/{im.format.lower()}" if im.format else "image/jpeg"
        with open(image_path, 'rb') as f:
            audio.tags.add(APIC(encoding=3, mime=mime, type=3, desc='Cover', data=f.read()))
    except Exception: pass

def run_organize_and_tag():
    logging.info("--- Step 2: Tagging Metadata, Embedding Cover Art & Standardizing ---")

    for root, dirs, files in os.walk(TOSORT_DIR):
        audio_files = [f for f in files if f.lower().endswith(('.flac', '.mp3'))]
        if not audio_files: continue

        canonical_album = match_canonical_album(root)
        cover_image = find_cover_image(root)
        if not cover_image: cover_image = find_cover_image(os.path.dirname(root))

        for f in audio_files:
            filepath = os.path.join(root, f)
            ext = os.path.splitext(f)[1].lower()

            try:
                audio = mutagen.File(filepath)
                title = os.path.splitext(f)[0]
                artist = "-45"
                track_no = "01"

                num_m = re.match(r'^(\d{1,2})[\.\s_\-]', f)
                if num_m: track_no = num_m.group(1).zfill(2)

                if audio and audio.tags and hasattr(audio.tags, 'get'):
                    t_val = audio.tags.get('title', [title])
                    title = t_val[0] if isinstance(t_val, list) else t_val
                    art_val = audio.tags.get('artist', [artist])
                    artist = art_val[0] if isinstance(art_val, list) else art_val

                artist_clean = str(artist).strip()
                if not artist_clean or artist_clean == "Unknown Artist": artist_clean = "-45"

                title_clean = str(title).strip()
                title_clean = re.sub(r'^\d{1,2}[\.\s_\-]+', '', title_clean).strip()

                if ext == '.flac':
                    flac_audio = FLAC(filepath)
                    flac_audio['TITLE'] = [title_clean]
                    flac_audio['ARTIST'] = [artist_clean]
                    flac_audio['ALBUM'] = [canonical_album]
                    flac_audio['TRACKNUMBER'] = [track_no]
                    if cover_image: embed_cover_art_flac(flac_audio, cover_image)
                    flac_audio.save()

                    dest_dir = os.path.join(FLAC_DIR, canonical_album)
                    os.makedirs(dest_dir, exist_ok=True)
                    dest_filename = f"{track_no} - {title_clean}.flac"
                    dest_path = os.path.join(dest_dir, dest_filename)

                    logging.info(f"[FLAC] D'va;;;;;;;;5 / {canonical_album} / {dest_filename}")
                    shutil.copy2(filepath, dest_path)

                elif ext == '.mp3':
                    mp3_audio = MP3(filepath)
                    if mp3_audio.tags is None: mp3_audio.add_tags()
                    mp3_audio.tags['TIT2'] = TIT2(encoding=3, text=title_clean)
                    mp3_audio.tags['TPE1'] = TPE1(encoding=3, text=artist_clean)
                    mp3_audio.tags['TALB'] = TALB(encoding=3, text=canonical_album)
                    mp3_audio.tags['TRCK'] = TRCK(encoding=3, text=track_no)
                    if cover_image: embed_cover_art_mp3(mp3_audio, cover_image)
                    mp3_audio.save()

                    dest_dir = os.path.join(MP3_DIR, canonical_album)
                    os.makedirs(dest_dir, exist_ok=True)
                    dest_filename = f"{track_no} - {title_clean}.mp3"
                    dest_path = os.path.join(dest_dir, dest_filename)

                    logging.info(f"[MP3] D'va;;;;;;;;5 / {canonical_album} / {dest_filename}")
                    shutil.copy2(filepath, dest_path)

            except Exception as e:
                logging.error(f"Error processing {filepath}: {e}")

    # FLAC Fallback Rule
    logging.info("--- Step 3: Applying FLAC Fallback Rule ---")
    for root, _, files in os.walk(MP3_DIR):
        for f in files:
            if f.lower().endswith('.mp3'):
                rel_dir = os.path.relpath(root, MP3_DIR)
                flac_target_dir = os.path.join(FLAC_DIR, rel_dir)
                flac_equivalent_file = os.path.splitext(f)[0] + '.flac'

                if not os.path.exists(os.path.join(flac_target_dir, flac_equivalent_file)):
                    os.makedirs(flac_target_dir, exist_ok=True)
                    mp3_path = os.path.join(root, f)
                    fallback_dest = os.path.join(flac_target_dir, f)
                    if not os.path.exists(fallback_dest):
                        logging.info(f"[FLAC FALLBACK] Copying MP3 fallback into flac/: {rel_dir} / {f}")
                        shutil.copy2(mp3_path, fallback_dest)

def sync_tracklist_checkboxes():
    logging.info("--- Step 4: Updating tracklist.md Checkboxes ---")
    if not os.path.exists(TRACKLIST_PATH): return

    flac_titles = set()
    for root, _, files in os.walk(FLAC_DIR):
        for f in files:
            if f.lower().endswith(('.flac', '.mp3')):
                t = os.path.splitext(f)[0]
                clean_t = re.sub(r'^\d{1,2}[\.\s_\-]+', '', t).strip().lower()
                flac_titles.add(clean_t)

    with open(TRACKLIST_PATH, "r", encoding="utf-8") as f:
        lines = f.readlines()

    new_lines, checked_count = [], 0
    for line in lines:
        if line.strip().startswith("- [ ]") or line.strip().startswith("- [x]"):
            m = re.search(r'\*\*([^*]+)\*\*', line)
            if m:
                t_name = m.group(1).strip().lower()
                if any(t_name in ft or ft in t_name for ft in flac_titles):
                    line = line.replace("- [ ]", "- [x]").replace("- [X]", "- [x]")
                    checked_count += 1
        new_lines.append(line)

    with open(TRACKLIST_PATH, "w", encoding="utf-8") as f:
        f.writelines(new_lines)

    logging.info(f"Updated tracklist.md: Marked {checked_count} tracks as [x]")

def run_full_pipeline():
    logging.info("==================================================")
    logging.info("   MUSIC COLLECTION AUTOMATION PIPELINE START     ")
    logging.info("==================================================")

    run_acoustid_quarantine()
    run_organize_and_tag()
    sync_tracklist_checkboxes()

    logging.info("==================================================")
    logging.info("   MUSIC COLLECTION PIPELINE COMPLETE SUCCESSFULLY")
    logging.info("==================================================")

if __name__ == '__main__':
    run_full_pipeline()
