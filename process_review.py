import os
import sys
import shutil
import logging

logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(levelname)s: %(message)s',
    datefmt='%H:%M:%S',
    handlers=[logging.StreamHandler(sys.stdout)]
)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
TOSORT_DIR = os.path.join(BASE_DIR, 'TO SORT')
REVIEW_DIR = os.path.join(BASE_DIR, 'review')

os.makedirs(REVIEW_DIR, exist_ok=True)

def move_to_review(filepath, reason):
    rel = os.path.relpath(filepath, TOSORT_DIR)
    target_path = os.path.join(REVIEW_DIR, rel)
    os.makedirs(os.path.dirname(target_path), exist_ok=True)
    
    logging.info(f"[QUARANTINE TO REVIEW] Moving: {rel}")
    logging.info(f"  Reason: {reason}")
    logging.info(f"  Destination: {os.path.relpath(target_path, BASE_DIR)}")
    
    shutil.move(filepath, target_path)

def process_quarantine():
    logging.info("Starting duplicate review quarantine process...")

    # Specific candidate rules based on scan analysis:
    # 1. Move 16-bit versions when 24-bit Hi-Res version exists
    for root, dirs, files in os.walk(TOSORT_DIR):
        for f in files:
            full_p = os.path.join(root, f)
            
            # Rule A: 16bit vs 24bit in Archives
            if '16bit' in root and f in ['patchwork_chimera.flac', 'sinkai.flac']:
                move_to_review(full_p, "Lower quality version (16-bit vs 24-bit Hi-Res available)")
            
            # Rule B: MP3 in クロユリ vs FLAC in アムリタ Disc 2
            elif 'クロユリ' in root and f.endswith('.mp3') and '少女たちの魔女狩り' in f:
                move_to_review(full_p, "Lower quality MP3 version (FLAC version available in アムリタ Disc 2)")
                
            # Rule C: Demo MP3 in Sun Fleur 1/2 vs Full album Sun Fleur
            elif 'Sun Fleur 1' in root and f.endswith('.mp3'):
                move_to_review(full_p, "Demo / Preview version (Full album Sun Fleur version available)")

    logging.info("Quarantine process finished.")

if __name__ == '__main__':
    process_quarantine()
