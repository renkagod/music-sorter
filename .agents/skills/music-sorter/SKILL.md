---
name: music-sorter
description: Multi-stage music collection workflow including duplicate detection (MD5, duration, fuzzy text, acoustic fingerprinting), metadata tagging, cover art embedding, FLAC/MP3 mirroring with fallbacks, and tracklist.md sync.
---

# Music Collection Sorting Workflow

Follow this multi-stage pipeline when processing downloaded music files.

## Stage 1: Duplicate Search
Run `python find_duplicates.py` to scan for:
- Exact byte duplicates (MD5).
- Duration & fuzzy title matches (e.g. Japanese vs English translated titles).
- Acoustic fingerprints (using Chromaprint / AcoustID where available).

⚠️ **NEVER DELETE AUTOMATICALLY**. Present all detected duplicates to the user for explicit confirmation before taking any action.

## Stage 2: Metadata Tagging & Cover Art
Run `python tagger.py` to:
- Fill audio tags: Artist, Title, Album, Track Number, Year.
- Embed cover art directly inside `.flac` and `.mp3` files.

## Stage 3: Organize & Mirror FLAC / MP3
Run `python organize.py` to:
- Place FLACs into `flac/{Artist}/{Album}/{Track}.flac`.
- Place MP3s into `mp3/{Artist}/{Album}/{Track}.mp3`.
- Apply **FLAC Fallback Rule**: If FLAC version is missing, copy MP3 into `flac/` as fallback.

## Stage 4: Database Sync & Git Commit
- Update `tracklist.md` by marking downloaded tracks `[x]` and adding new tracks if discovered.
- Execute a Git commit with a detailed commit message describing changes.
- 📦 **PROTECTED ZIP ARCHIVES**: Never touch or delete `*.zip` backup archives in the workspace.
