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
FLAC_DIR = os.path.join(BASE_DIR, 'flac')
MP3_DIR = os.path.join(BASE_DIR, 'mp3')

def sync_collections():
    logging.info("Starting music collection synchronization...")
    logging.info(f"FLAC directory: {FLAC_DIR}")
    logging.info(f"MP3 directory: {MP3_DIR}")

    if not os.path.exists(FLAC_DIR):
        os.makedirs(FLAC_DIR, exist_ok=True)
        logging.info("Created FLAC directory.")

    if not os.path.exists(MP3_DIR):
        os.makedirs(MP3_DIR, exist_ok=True)
        logging.info("Created MP3 directory.")

    # 1. Fallback sync: If MP3 exists in mp3/ but no FLAC in flac/, copy MP3 to flac/
    logging.info("Step 1: Checking for MP3 fallbacks for FLAC directory...")
    for root, dirs, files in os.walk(MP3_DIR):
        rel_path = os.path.relpath(root, MP3_DIR)
        flac_target_dir = os.path.join(FLAC_DIR, rel_path) if rel_path != '.' else FLAC_DIR

        for file in files:
            if file.lower().endswith('.mp3'):
                base_name = os.path.splitext(file)[0]
                mp3_source = os.path.join(root, file)
                flac_expected = os.path.join(flac_target_dir, base_name + '.flac')
                mp3_fallback_in_flac = os.path.join(flac_target_dir, file)

                # Check if FLAC version exists
                if not os.path.exists(flac_expected) and not os.path.exists(mp3_fallback_in_flac):
                    os.makedirs(flac_target_dir, exist_ok=True)
                    logging.info(f"[Fallback Copy] Copying MP3 fallback to FLAC folder: {os.path.join(rel_path, file)}")
                    shutil.copy2(mp3_source, mp3_fallback_in_flac)
                elif os.path.exists(flac_expected) and os.path.exists(mp3_fallback_in_flac):
                    logging.warning(f"[Notice] Both FLAC and MP3 fallback exist in FLAC folder for: {base_name}. No deletion performed (deletion prohibited).")

    # 2. Mirror structure sync: Ensure directory structures match
    logging.info("Step 2: Mirroring folder structures between FLAC and MP3...")
    for root, dirs, files in os.walk(FLAC_DIR):
        rel_path = os.path.relpath(root, FLAC_DIR)
        if rel_path == '.':
            continue
        mp3_equivalent_dir = os.path.join(MP3_DIR, rel_path)
        if not os.path.exists(mp3_equivalent_dir):
            os.makedirs(mp3_equivalent_dir, exist_ok=True)
            logging.info(f"[Directory Sync] Created MP3 subfolder: {rel_path}")

    logging.info("Synchronization check completed successfully.")

if __name__ == '__main__':
    sync_collections()
