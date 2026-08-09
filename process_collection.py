import os
import sys
import shutil
import logging
import subprocess
import re
import json
import urllib.parse
import urllib.request
from PIL import Image

try:
    import mutagen
    from mutagen.flac import FLAC, Picture
    from mutagen.id3 import ID3, APIC, TIT2, TPE1, TALB, TRCK, TDRC
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
FLAC_ROOT = os.path.join(BASE_DIR, 'flac')
MP3_ROOT = os.path.join(BASE_DIR, 'mp3')
REVIEW_DIR = os.path.join(BASE_DIR, 'review')
TRACKLIST_PATH = os.path.join(BASE_DIR, 'tracklist.md')

ACOUSTID_API_KEY = "8Xa1nV0f"

os.makedirs(FLAC_ROOT, exist_ok=True)
os.makedirs(MP3_ROOT, exist_ok=True)
os.makedirs(REVIEW_DIR, exist_ok=True)

def get_fpcalc_info(filepath):
    if not os.path.exists(FPCALC_BIN): return None
    try:
        res = subprocess.run([FPCALC_BIN, '-raw', filepath], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
        dur, fp = 0, ""
        for line in res.stdout.splitlines():
            if line.startswith('DURATION='): dur = float(line.split('=')[1])
            elif line.startswith('FINGERPRINT='): fp = line.split('=')[1]
        return {'duration': dur, 'fingerprint': fp}
    except: return None

def fetch_online_metadata(filepath):
    info = get_fpcalc_info(filepath)
    if not info or not info['fingerprint']: return None
    
    dur = int(info['duration'])
    fp = info['fingerprint']
    url = f"https://api.acoustid.org/v2/lookup?client={ACOUSTID_API_KEY}&meta=recordings+releasegroups+compress&duration={dur}&fingerprint={fp}"
    
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'MusicSorter/2.0'})
        with urllib.request.urlopen(req, timeout=4) as resp:
            data = json.loads(resp.read().decode('utf-8'))
            if data.get('status') == 'ok' and data.get('results'):
                for result in data['results']:
                    if result.get('score', 0) >= 0.75 and result.get('recordings'):
                        rec = result['recordings'][0]
                        title = rec.get('title')
                        artists = [a.get('name') for a in rec.get('artists', []) if a.get('name')]
                        artist = ", ".join(artists) if artists else None
                        
                        album, release_id, year = None, None, None
                        if rec.get('releasegroups'):
                            rg = rec['releasegroups'][0]
                            album = rg.get('title')
                            release_id = rg.get('id')
                            if rg.get('first-release-date'):
                                year = rg['first-release-date'][:4]
                                
                        return {
                            'title': title,
                            'artist': artist,
                            'album': album,
                            'year': year,
                            'release_id': release_id,
                            'score': result.get('score')
                        }
    except Exception as e:
        logging.debug(f"Online lookup failed for {filepath}: {e}")
    return None

def fetch_online_cover_art(release_id):
    if not release_id: return None
    url = f"https://coverartarchive.org/release-group/{release_id}/front-500"
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'MusicSorter/2.0'})
        with urllib.request.urlopen(req, timeout=4) as resp:
            return resp.read()
    except Exception:
        return None

def find_local_cover_image(folder_path):
    candidates = ['cover.jpg', 'cover.png', 'cover.jpeg', 'folder.jpg', 'folder.png', 'folder.jpeg', 'cover.JPG']
    for root, _, files in os.walk(folder_path):
        for file in files:
            if file in candidates or file.lower().endswith(('.jpg', '.png', '.jpeg')):
                return os.path.join(root, file)
    return None

def embed_cover_art_flac_data(audio, img_data, mime_type="image/jpeg"):
    if not img_data: return
    try:
        picture = Picture()
        picture.data = img_data
        picture.type = 3
        picture.mime = mime_type
        picture.depth = 24
        audio.clear_pictures()
        audio.add_picture(picture)
    except Exception: pass

def embed_cover_art_mp3_data(audio, img_data, mime_type="image/jpeg"):
    if not img_data: return
    try:
        if audio.tags is None: audio.add_tags()
        audio.tags.add(APIC(encoding=3, mime=mime_type, type=3, desc='Cover', data=img_data))
    except Exception: pass

def process_and_tag_collection():
    logging.info("--- Step 2: Multi-Artist Tagging, MusicBrainz & Cover Art Archive ---")

    for root, dirs, files in os.walk(TOSORT_DIR):
        audio_files = [f for f in files if f.lower().endswith(('.flac', '.mp3'))]
        if not audio_files: continue

        local_cover = find_local_cover_image(root)
        if not local_cover: local_cover = find_local_cover_image(os.path.dirname(root))

        for f in audio_files:
            filepath = os.path.join(root, f)
            ext = os.path.splitext(f)[1].lower()

            try:
                # 1. Try Online MusicBrainz Lookup
                online_meta = fetch_online_metadata(filepath)
                online_cover_data = None
                if online_meta and online_meta.get('release_id'):
                    online_cover_data = fetch_online_cover_art(online_meta['release_id'])

                # 2. Extract Existing Tags or Parse Filename
                audio = mutagen.File(filepath)
                filename_clean = os.path.splitext(f)[0]
                track_no = "01"

                num_m = re.match(r'^(\d{1,2})[\.\s_\-]+(.+)$', filename_clean)
                if num_m:
                    track_no = num_m.group(1).zfill(2)
                    filename_clean = num_m.group(2).strip()

                title = filename_clean
                artist = os.path.basename(os.path.dirname(root)) if os.path.basename(os.path.dirname(root)).lower() != 'to sort' else "Unknown Artist"
                album = os.path.basename(root) if os.path.basename(root).lower() != 'to sort' else "Unknown Album"

                if audio and audio.tags and hasattr(audio.tags, 'get'):
                    t_val = audio.tags.get('title', [title])
                    title = t_val[0] if isinstance(t_val, list) else t_val
                    art_val = audio.tags.get('artist', [artist])
                    artist = art_val[0] if isinstance(art_val, list) else art_val
                    alb_val = audio.tags.get('album', [album])
                    album = alb_val[0] if isinstance(alb_val, list) else alb_val

                # If MusicBrainz produced high confidence metadata, use it!
                if online_meta:
                    if online_meta.get('title'): title = online_meta['title']
                    if online_meta.get('artist'): artist = online_meta['artist']
                    if online_meta.get('album'): album = online_meta['album']

                artist_clean = str(artist).strip() if str(artist).strip() else "Unknown Artist"
                album_clean = str(album).strip() if str(album).strip() else "Unknown Album"
                title_clean = str(title).strip() if str(title).strip() else filename_clean

                if online_meta:
                    logging.info(f"[MUSICBRAINZ MATCH] {artist_clean} - {title_clean} ({album_clean})")
                else:
                    logging.info(f"[NICHE/LOCAL TRACK] {artist_clean} - {title_clean} ({album_clean})")

                # Prepare Cover Art Data (Online HQ > Local Scan)
                img_data = online_cover_data
                mime_type = "image/jpeg"

                if not img_data and local_cover and os.path.exists(local_cover):
                    with open(local_cover, 'rb') as c_file:
                        img_data = c_file.read()
                        if local_cover.lower().endswith('.png'): mime_type = "image/png"

                # Write Tags & Embed Cover Art
                if ext == '.flac':
                    flac_audio = FLAC(filepath)
                    flac_audio['TITLE'] = [title_clean]
                    flac_audio['ARTIST'] = [artist_clean]
                    flac_audio['ALBUM'] = [album_clean]
                    flac_audio['TRACKNUMBER'] = [track_no]
                    if img_data: embed_cover_art_flac_data(flac_audio, img_data, mime_type)
                    flac_audio.save()

                    dest_dir = os.path.join(FLAC_ROOT, artist_clean, album_clean)
                    os.makedirs(dest_dir, exist_ok=True)
                    dest_path = os.path.join(dest_dir, f"{track_no} - {title_clean}.flac")
                    shutil.copy2(filepath, dest_path)

                elif ext == '.mp3':
                    mp3_audio = MP3(filepath)
                    if mp3_audio.tags is None: mp3_audio.add_tags()
                    mp3_audio.tags['TIT2'] = TIT2(encoding=3, text=title_clean)
                    mp3_audio.tags['TPE1'] = TPE1(encoding=3, text=artist_clean)
                    mp3_audio.tags['TALB'] = TALB(encoding=3, text=album_clean)
                    mp3_audio.tags['TRCK'] = TRCK(encoding=3, text=track_no)
                    if img_data: embed_cover_art_mp3_data(mp3_audio, img_data, mime_type)
                    mp3_audio.save()

                    dest_dir = os.path.join(MP3_ROOT, artist_clean, album_clean)
                    os.makedirs(dest_dir, exist_ok=True)
                    dest_path = os.path.join(dest_dir, f"{track_no} - {title_clean}.mp3")
                    shutil.copy2(filepath, dest_path)

            except Exception as e:
                logging.error(f"Error processing {filepath}: {e}")

if __name__ == '__main__':
    process_and_tag_collection()
