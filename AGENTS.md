# Rules for Music Workspace

## Workspace Structure
- `flac/`: Main lossy/lossless collection.
- `mp3/`: MP3 version of collection.
- `flac/` and `mp3/` must mirror each other in structure.
- **FLAC Fallback Rule**: If a track has no FLAC version but an MP3 version exists, place/keep the MP3 file in the `flac/` folder as a fallback.

## Safety & Deletion Rules
- ⚠️ **AUTOMATIC DELETION IS STRICTLY FORBIDDEN!** Never delete any user files automatically. If files need to be deleted, ask the user for explicit confirmation.

## Script & Automation Requirements
- All scripts working with music files or database sync must output **detailed step-by-step console logs**.
- The main track list database is maintained in `tracklist.md`.
