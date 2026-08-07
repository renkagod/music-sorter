# Rules for Music Workspace

## Workspace Structure
- `flac/`: Main lossy/lossless collection.
- `mp3/`: MP3 version of collection.
- `flac/` and `mp3/` must mirror each other in structure.
- **FLAC Fallback Rule**: If a track has no FLAC version but an MP3 version exists, place/keep the MP3 file in the `flac/` folder as a fallback.

## Workflow Order
1. **Duplicate Search**: Before sorting or moving, scan for duplicates (by audio hash, title/artist, or filename).
2. **Metadata & Cover Art**: Ensure all metadata (title, artist, album, track number, year) is filled and cover art image is embedded directly inside the audio files.
3. **Organize & Mirror**: Place tracks into `flac/` and `mp3/` according to mirroring & fallback rules.
4. **Update Database**: Check off `[x]` downloaded tracks in `tracklist.md`.

## Safety & Deletion Rules
- ⚠️ **AUTOMATIC DELETION IS STRICTLY FORBIDDEN!** Never delete any user files automatically. If duplicates or extra files are found, log them in detail and ask the user for explicit confirmation.
- 📦 **PROTECTED ZIP ARCHIVES**: ZIP archives (`*.zip`) containing music backups in this directory are strictly protected and MUST NEVER be deleted under any circumstances!

## Script & Automation Requirements
- All scripts working with music files or database sync must output **detailed step-by-step console logs**.
- The main track list database is maintained in `tracklist.md`.
- **Git Commit Rule**: Make a Git commit after any changes to workspace files.
