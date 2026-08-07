import os
import sys
import shutil
import logging
import re
from PIL import Image

try:
    import mutagen
    from mutagen.flac import FLAC, Picture
    from mutagen.id3 import ID3, APIC, TIT2, TPE1, TALB, TRCK, TDRC
    from mutagen.mp3 import MP3
except ImportError:
    print("Mutagen package is required.")
    sys.exit(1)

logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(levelname)s: %(message)s',
    datefmt='%H:%M:%S',
    handlers=[logging.StreamHandler(sys.stdout)]
)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
TOSORT_DIR = os.path.join(BASE_DIR, 'TO SORT')
FLAC_DIR = os.path.join(BASE_DIR, 'flac')
MP3_DIR = os.path.join(BASE_DIR, 'mp3')

os.makedirs(FLAC_DIR, exist_ok=True)
os.makedirs(MP3_DIR, exist_ok=True)

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
        with open(image_path, 'rb') as f:
            picture.data = f.read()
        im = Image.open(image_path)
        picture.type = 3 # Front Cover
        picture.mime = f"image/{im.format.lower()}" if im.format else "image/jpeg"
        picture.width = im.width
        picture.height = im.height
        picture.depth = 24
        audio.clear_pictures()
        audio.add_picture(picture)
    except Exception as e:
        logging.debug(f"Cover embed FLAC error: {e}")

def embed_cover_art_mp3(audio, image_path):
    if not image_path or not os.path.exists(image_path): return
    try:
        if audio.tags is None:
            audio.add_tags()
        im = Image.open(image_path)
        mime = f"image/{im.format.lower()}" if im.format else "image/jpeg"
        with open(image_path, 'rb') as f:
            audio.tags.add(
                APIC(
                    encoding=3,
                    mime=mime,
                    type=3,
                    desc='Cover',
                    data=f.read()
                )
            )
    except Exception as e:
        logging.debug(f"Cover embed MP3 error: {e}")

def process_album_folder(root_path, files):
    audio_files = [f for f in files if f.lower().endswith(('.flac', '.mp3'))]
    if not audio_files: return

    cover_image = find_cover_image(root_path)

    for f in audio_files:
        filepath = os.path.join(root_path, f)
        ext = os.path.splitext(f)[1].lower()

        # Read current tags
        try:
            audio = mutagen.File(filepath)
            title = os.path.splitext(f)[0]
            artist = "-45"
            album = os.path.basename(root_path)
            track_no = "01"

            # Parse track number from filename if available (e.g. 01. -45 - Title -> 01)
            num_m = re.match(r'^(\d{1,2})[\.\s_\-]', f)
            if num_m:
                track_no = num_m.group(1).zfill(2)

            if audio and audio.tags:
                if hasattr(audio.tags, 'get'):
                    t_val = audio.tags.get('title', [title])
                    title = t_val[0] if isinstance(t_val, list) else t_val
                    art_val = audio.tags.get('artist', [artist])
                    artist = art_val[0] if isinstance(art_val, list) else art_val
                    alb_val = audio.tags.get('album', [album])
                    album = alb_val[0] if isinstance(alb_val, list) else alb_val

            # Clean artist and album names
            artist_clean = str(artist).strip()
            if not artist_clean or artist_clean == "Unknown Artist":
                artist_clean = "-45"

            album_clean = str(album).strip()
            album_clean = re.sub(r'\[[^\]]*\]|\{[^\}]*\}', '', album_clean).strip()
            if not album_clean: album_clean = "Album"

            title_clean = str(title).strip()
            # Clean title leading track numbers if duplicated
            title_clean = re.sub(r'^\d{1,2}[\.\s_\-]+', '', title_clean).strip()

            # Save tags and embed cover
            if ext == '.flac':
                flac_audio = FLAC(filepath)
                flac_audio['TITLE'] = [title_clean]
                flac_audio['ARTIST'] = [artist_clean]
                flac_audio['ALBUM'] = [album_clean]
                flac_audio['TRACKNUMBER'] = [track_no]
                if cover_image:
                    embed_cover_art_flac(flac_audio, cover_image)
                flac_audio.save()

                # Destination path
                dest_dir = os.path.join(FLAC_DIR, artist_clean, album_clean)
                os.makedirs(dest_dir, exist_ok=True)
                dest_filename = f"{track_no} - {title_clean}.flac"
                dest_path = os.path.join(dest_dir, dest_filename)

                logging.info(f"Organizing FLAC: {artist_clean} / {album_clean} / {dest_filename}")
                shutil.copy2(filepath, dest_path)

            elif ext == '.mp3':
                mp3_audio = MP3(filepath)
                if mp3_audio.tags is None: mp3_audio.add_tags()
                mp3_audio.tags['TIT2'] = TIT2(encoding=3, text=title_clean)
                mp3_audio.tags['TPE1'] = TPE1(encoding=3, text=artist_clean)
                mp3_audio.tags['TALB'] = TALB(encoding=3, text=album_clean)
                mp3_audio.tags['TRCK'] = TRCK(encoding=3, text=track_no)
                if cover_image:
                    embed_cover_art_mp3(mp3_audio, cover_image)
                mp3_audio.save()

                # Destination path MP3
                dest_dir = os.path.join(MP3_DIR, artist_clean, album_clean)
                os.makedirs(dest_dir, exist_ok=True)
                dest_filename = f"{track_no} - {title_clean}.mp3"
                dest_path = os.path.join(dest_dir, dest_filename)

                logging.info(f"Organizing MP3: {artist_clean} / {album_clean} / {dest_filename}")
                shutil.copy2(filepath, dest_path)

        except Exception as e:
            logging.error(f"Error processing {filepath}: {e}")

def run_organization():
    logging.info("==================================================")
    logging.info("  STARTING METADATA TAGGING & FILE ORGANIZATION  ")
    logging.info("==================================================")

    for root, _, files in os.walk(TOSORT_DIR):
        process_album_folder(root, files)

    logging.info("==================================================")
    logging.info("  ORGANIZATION COMPLETE. APPLYING FLAC FALLBACK  ")
    logging.info("==================================================")

    # Apply FLAC Fallback Rule: if track is in mp3 but not in flac, copy to flac
    for root, _, files in os.walk(MP3_DIR):
        for f in files:
            if f.lower().endswith('.mp3'):
                rel_dir = os.path.relpath(root, MP3_DIR)
                flac_target_dir = os.path.join(FLAC_DIR, rel_dir)
                flac_equivalent_file = os.path.splitext(f)[0] + '.flac'

                has_flac = os.path.exists(os.path.join(flac_target_dir, flac_equivalent_file))
                if not has_flac:
                    os.makedirs(flac_target_dir, exist_ok=True)
                    mp3_path = os.path.join(root, f)
                    fallback_dest = os.path.join(flac_target_dir, f)
                    if not os.path.exists(fallback_dest):
                        logging.info(f"[FLAC FALLBACK] Copying MP3 fallback into flac/: {rel_dir} / {f}")
                        shutil.copy2(mp3_path, fallback_dest)

if __name__ == '__main__':
    run_organization()
