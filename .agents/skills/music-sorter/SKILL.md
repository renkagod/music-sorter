---
name: music-sorter
description: Complete automated music collection sorting workflow including AcoustID (fpcalc.exe) duplicate quarantine, metadata tagging, cover art embedding, Discogs canonical folder standardization, FLAC/MP3 fallbacks, tracklist.md checkbox sync, and Git commits.
---

# Complete Music Collection Workflow Guide

This document describes the exact automated pipeline for processing any newly downloaded music files into the collection.

## 🚀 One-Command Execution
Whenever you download new music tracks/albums into `TO SORT`, simply run:
```bash
python process_collection.py
```

---

## 🛠️ Step-by-Step Pipeline Architecture

### Step 1: AcoustID Chromaprint Duplicate Quarantine (`fpcalc.exe`)
- Runs `fpcalc.exe -raw` on all audio files to calculate Chromaprint AcoustID fingerprints.
- Compares acoustic waveforms across Japanese/English titles and encoder silence padding.
- Automatically quarantines duplicate lossy MP3s (>=85% AcoustID similarity) to `review/` without deleting any user files.

### Step 2: Metadata Tagging & Embedded Cover Art
- Fills audio tags: `TITLE` (1-in-1 exact original title with Japanese/special characters), `ARTIST`, `ALBUM`, `TRACKNUMBER`.
- Automatically finds `cover.jpg` / `folder.jpg` in the album directory and embeds front cover art directly inside FLAC (`Picture`) and MP3 (`APIC`).

### Step 3: Canonical Discogs Album Standardization
- Matches album names against official Discogs release titles (e.g. `Candybug (2005)`, `クロユリ (2006)`).
- Organizes tracks into clean single-folder structures under `flac/D'va;;;;;;;;5/{Album}/` and `mp3/D'va;;;;;;;;5/{Album}/`.

### Step 4: FLAC Fallback Rule
- If a track exists only in MP3, copies the MP3 file into `flac/D'va;;;;;;;;5/{Album}/` as a fallback, ensuring the `flac/` collection remains 100% complete.

### Step 5: Database Checkbox Sync & Git Commit
- Updates `tracklist.md` checkboxes to `[x]` for all downloaded tracks.
- Executes Git commit documenting all changes.

---

## ⚠️ Important Safety & Collection Rules
1. 🛑 **AUTOMATIC DELETION IS FORBIDDEN**: Never delete audio files. Move secondary candidates to `review/`.
2. 📦 **PROTECTED ZIP ARCHIVES**: `*.zip` backup archives in the workspace are protected and must never be deleted.
3. 📜 **LOGGING REQUIREMENT**: All scripts output detailed step-by-step console logs during execution.
