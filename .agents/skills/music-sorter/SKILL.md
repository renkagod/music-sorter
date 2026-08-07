---
name: music-sorter
description: Unified 5-stage automated music sorting workflow (highest quality selection FLAC>MP3, AcoustID fpcalc.exe quarantine to review/, Discogs/MusicBrainz tagging, embedded cover art auto-fetch, final folder placement, tracklist.md checkbox sync, and Git commits).
---

# Master Music Sorting Workflow

Follow this strict 5-stage pipeline whenever new audio files or albums are placed into `TO SORT`.

## 🚀 Execution Command
```bash
python process_collection.py
```

---

## 📋 The 5-Stage Pipeline Architecture

```
[ TO SORT (Raw Downloads) ]
            │
            ▼
┌─────────────────────────────────────────────────────────────┐
│ STAGE 1: AcoustID Duplicate & Quality Quarantine            │
│  - Compute Chromaprint fingerprints via `fpcalc.exe -raw`   │
│  - FLAC > MP3 priority: lossy MP3 duplicates (>=85% sim)    │
│    moved to `review/` quarantine                           │
│  - Alternate mixes / similar tracks logged for user review  │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ STAGE 2: Tagging & Cover Art Enrichment                      │
│  - Discogs / MusicBrainz canonical metadata matching        │
│  - Exact 1-in-1 track titles (Japanese/special chars preserved)│
│  - Embed cover art (local scan/file or fetch from trusted   │
│    sources: MusicBrainz / Discogs / VGMdb)                  │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ STAGE 3: Final Placement & Mirroring                        │
│  - ONLY when tracks are in pristine tagged form with covers:│
│  - Move into `flac/{Artist}/{Album (Year)}/01 - Title.flac` │
│  - Mirror into `mp3/{Artist}/{Album (Year)}/01 - Title.mp3` │
│  - Apply FLAC Fallback: keep MP3s in `flac/` if FLAC missing│
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ STAGE 4: Database Checkbox Sync (`tracklist.md`)             │
│  - Auto-update `tracklist.md` checkboxes to `[x]` with format│
│    tag `[FLAC]` / `[MP3]` and track durations               │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ STAGE 5: Git Commit & Clean Output Logs                     │
│  - Output detailed step-by-step console logs                │
│  - Make a Git commit documenting all changes                │
└─────────────────────────────────────────────────────────────┘
```

---

## ⚠️ Core Rules & Constraints
1. 🛑 **NO AUTOMATIC DELETIONS**: Never delete user audio files automatically. Move secondary candidates to `review/`.
2. 🚫 **NO AI GENERATED IMAGES**: Always use authentic original CD booklet scans or trusted database cover art (Discogs / VGMdb / MusicBrainz).
3. 📦 **PROTECTED ZIP ARCHIVES**: `*.zip` backup archives in the workspace are protected and must never be deleted.
