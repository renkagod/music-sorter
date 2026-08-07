---
name: music-sorter
description: Multi-stage music collection workflow including duplicate detection (MD5, duration, fuzzy text), metadata tagging, cover art embedding, FLAC quality validation, FLAC/MP3 mirroring with fallbacks, and raw directory verification against tracklist.md.
---

# Music Collection Sorting Workflow

Follow this multi-stage pipeline when processing downloaded music files.

## Stage 1: Raw Directory Inspection & Duplicate Search
1. **Raw Directory Tree Inspection**: Use raw OS directory commands (`Get-ChildItem -Recurse` / `dir`) to inspect all folders and files on disk. Do not rely solely on automated script regexes.
2. **Duplicate Search**: Run `python find_duplicates.py` to scan for:
   - Exact byte duplicates (MD5).
   - Duration & fuzzy title matches (e.g. Japanese vs English translated titles).

⚠️ **QUARANTINE RULE**: Keep the highest quality version (24-bit > 16-bit > MP3) in the main workflow (`TO SORT` -> `flac/` / `mp3/`). Move secondary/lower quality duplicate candidates to `review/` for manual user inspection. **NEVER DELETE FILES AUTOMATICALLY**.

## Stage 2: FLAC Audio Quality Validation
Check FLAC files for bit depth (16-bit / 24-bit), sample rate (44.1kHz / 48kHz / 96kHz), and bitrate integrity. Ensure no corrupted files exist.

## Stage 3: Metadata Tagging & Cover Art
Run `python tagger.py` to:
- Fill audio tags: Artist, Title, Album, Track Number, Year.
- Embed cover art directly inside `.flac` and `.mp3` files.

## Stage 4: Organize & Mirror FLAC / MP3
Run `python organize.py` to:
- Place FLACs into `flac/{Artist}/{Album}/{Track}.flac`.
- Place MP3s into `mp3/{Artist}/{Album}/{Track}.mp3`.
- Apply **FLAC Fallback Rule**: If FLAC version is missing, copy MP3 into `flac/` as fallback.

## Stage 5: Database Sync (`tracklist.md`) & Git Commit
- Update `tracklist.md`: Check off downloaded tracks with `[x]` and format tag (e.g. `[FLAC]` or `[MP3]`).
- Execute a Git commit with a detailed commit message describing changes.
- 📦 **PROTECTED ZIP ARCHIVES**: Never touch or delete `*.zip` backup archives in the workspace.
