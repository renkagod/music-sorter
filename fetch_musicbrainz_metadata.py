import os
import sys
import re
import json
import urllib.parse
import urllib.request
import logging
import subprocess

try:
    import mutagen
    from mutagen.flac import FLAC, Picture
    from mutagen.id3 import ID3, APIC, TIT2, TPE1, TALB, TRCK, TDRC
    from mutagen.mp3 import MP3
    from PIL import Image
except ImportError:
    print("Mutagen and Pillow are required.")
    sys.exit(1)

logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(levelname)s: %(message)s',
    datefmt='%H:%M:%S'
)

ACOUSTID_API_KEY = "8Xa1nV0f" // Client API Key for AcoustID / MusicBrainz lookup
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
FPCALC_BIN = os.path.join(BASE_DIR, 'fpcalc.exe')

def get_fpcalc_info(filepath):
    if not os.path.exists(FPCALC_BIN): return None
    try:
        res = subprocess.run([FPCALC_BIN, '-raw', filepath], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
        dur, fp = 0, ""
        for line in res.stdout.splitlines():
            if line.startswith('DURATION='): dur = float(line.split('=')[1])
            elif line.startswith('FINGERPRINT='): fp = line.split('=')[1]
        return {'duration': dur, 'fingerprint': fp}
    except Exception as e:
        return None

def fetch_acoustid_metadata(fingerprint, duration):
    if not fingerprint or not duration: return None
    url = f"https://api.acoustid.org/v2/lookup?client={ACOUSTID_API_KEY}&meta=recordings+releasegroups+compress&duration={int(duration)}&fingerprint={fingerprint}"
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'MusicSorter/2.0 (music@sorter.local)'})
        with urllib.request.urlopen(req, timeout=5) as response:
            data = json.loads(response.read().decode('utf-8'))
            if data.get('status') == 'ok' and data.get('results'):
                for result in data['results']:
                    if result.get('score', 0) >= 0.70 and result.get('recordings'):
                        rec = result['recordings'][0]
                        title = rec.get('title')
                        artists = [a.get('name') for a in rec.get('artists', []) if a.get('name')]
                        artist = ", ".join(artists) if artists else None
                        
                        release_id = None
                        album = None
                        year = None
                        
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
        logging.debug(f"AcoustID API lookup failed: {e}")
    return None

def fetch_cover_art_bytes(release_id):
    if not release_id: return None
    url = f"https://coverartarchive.org/release-group/{release_id}/front-500"
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'MusicSorter/2.0 (music@sorter.local)'})
        with urllib.request.urlopen(req, timeout=5) as response:
            return response.read()
    except Exception:
        return None

def parse_niche_fallback(filepath):
    filename = os.path.basename(filepath)
    title_raw = os.path.splitext(filename)[0]
    
    parent_folder = os.path.basename(os.path.dirname(filepath))
    grandparent_folder = os.path.basename(os.path.dirname(os.path.dirname(filepath)))
    
    artist = "Unknown Artist"
    album = parent_folder if parent_folder and parent_folder.lower() != 'to sort' else "Unknown Album"
    title = title_raw
    track_no = "01"
    
    # Check if filename starts with track number e.g., "05 - Title" or "05. Title"
    num_m = re.match(r'^(\d{1,2})[\.\s_\-]+(.+)$', title_raw)
    if num_m:
        track_no = num_m.group(1).zfill(2)
        title = num_m.group(2).strip()
        
    # Check if artist is in grandparent folder e.g., TO SORT / Artist / Album / Track.flac
    if grandparent_folder and grandparent_folder.lower() != 'to sort':
        artist = grandparent_folder
        
    return {
        'title': title,
        'artist': artist,
        'album': album,
        'track_no': track_no
    }
