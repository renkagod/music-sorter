# Rules for Music Workspace

## Workspace Structure
- `flac/`: Main lossy/lossless collection.
- `mp3/`: MP3 version of collection.
- `review/`: Quarantine folder for potential duplicates, lower bitrates, or alternative mixes needing manual user inspection.
- `flac/` and `mp3/` must mirror each other in structure.
- **FLAC Fallback Rule**: If a track has no FLAC version but an MP3 version exists, place/keep the MP3 file in the `flac/` folder as a fallback.

## Workflow Order
1. **Duplicate Search**: Before sorting or moving, scan for duplicates (by audio hash, duration + title similarity, or acoustic fingerprint).
2. **Review Quarantine Rule**: If potential duplicates or alternate versions (e.g. 16-bit vs 24-bit, FLAC vs MP3 duplicates, demo vs full album) are found, keep the highest quality version (24-bit > 16-bit > MP3) in the main pipeline and move lower/alternative versions to `review/` for manual user decision.
3. **Metadata & Cover Art**: Ensure all metadata (title, artist, album, track number, year) is filled and cover art image is embedded directly inside the audio files.
4. **Organize & Mirror**: Place tracks into `flac/` and `mp3/` according to mirroring & fallback rules.
5. **Update Database**: Check off `[x]` downloaded tracks in `tracklist.md`.

## Safety & Deletion Rules
- ⚠️ **AUTOMATIC DELETION IS STRICTLY FORBIDDEN!** Never delete any user files automatically. Move candidate duplicates to `review/` instead of deleting.
- 📦 **PROTECTED ZIP ARCHIVES**: ZIP archives (`*.zip`) containing music backups in this directory are strictly protected and MUST NEVER be deleted under any circumstances!

## Script & Automation Requirements
- All scripts working with music files or database sync must output **detailed step-by-step console logs**.
- The main track list database is maintained in `tracklist.md`.
- **Git Commit Rule**: Make a Git commit after any changes to workspace files.
