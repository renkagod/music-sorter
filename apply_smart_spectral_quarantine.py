import os
import sys
import shutil
import logging
import subprocess

logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(levelname)s: %(message)s',
    datefmt='%H:%M:%S',
    handlers=[logging.StreamHandler(sys.stdout)]
)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
FPCALC_BIN = os.path.join(BASE_DIR, 'fpcalc.exe')
TOSORT_DIR = os.path.join(BASE_DIR, 'TO SORT')
REVIEW_DIR = os.path.join(BASE_DIR, 'review')

os.makedirs(REVIEW_DIR, exist_ok=True)

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

def move_to_review(filepath, reason):
    rel = os.path.relpath(filepath, TOSORT_DIR)
    target_path = os.path.join(REVIEW_DIR, rel)
    os.makedirs(os.path.dirname(target_path), exist_ok=True)
    logging.info(f"[FPCALC ACOUSTID QUARANTINE] Moving redundant MP3: {rel}")
    logging.info(f"  Reason: {reason}")
    shutil.move(filepath, target_path)

def run_fpcalc_quarantine():
    logging.info("Starting fpcalc Chromaprint AcoustID quarantine scan...")

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
                move_to_review(p, f"Chromaprint AcoustID Match ({best_sim:.1%} similarity) with FLAC '{best_flac['file']}'")
                moved_count += 1

    logging.info(f"fpcalc AcoustID Quarantine Complete. Total redundant MP3s moved: {moved_count}")

if __name__ == '__main__':
    run_fpcalc_quarantine()
