# music-sorter

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Desktop app for organizing a music library: it fingerprints tracks to find duplicates, fetches metadata and lyrics from multiple online databases, and lets you review and approve the results before they are written to your files. A headless C++ core does the analysis and serves a local REST API; a Tauri (Rust) shell hosts a React/TypeScript UI on top of it.

## Features

- **Duplicate detection** — audio fingerprinting with Chromaprint (`fpcalc`) and AcoustID, clustered into candidate pairs you resolve one by one (`DuplicateDeck`).
- **Multi-source tagging** — fetches candidate metadata from MusicBrainz, Discogs, VocaDB, TouhouDB, UtaiteDB and THBWiki, and synced lyrics from LRCLIB, then scores the candidates with a consensus aggregator before you approve a track or album (`TagInspector`).
- **Audio preview** — built-in player (miniaudio) with waveform display, seek and volume control.
- **Library mirroring** — sync a sorted output folder from the source library (`MirroringView`).
- **Local database** — every track is tracked in a SQLite database (artist, album, title, track number, year, duration, format, bitrate, status) with stats and export (`DatabaseView`).
- **Live logs** — core engine activity streamed to the UI (`LogDrawer`).

## Quick start

The app has two parts that both need to be built: the C++ core (`music-sorter-core.exe`) and the Tauri/React shell. The shell looks for the core executable next to itself or in the current/parent working directory, so build the core first and run the shell from the repository root.

1. **Build the C++ core** (Windows, MSVC Build Tools + CMake + Ninja — edit the tool paths at the top of the script if they differ from yours):

   ```bat
   build_native_cpp.bat
   ```

   This configures CMake in `build/` and compiles `music-sorter-core.exe` into the repository root, next to the bundled `fpcalc.exe`.

2. **Install the frontend dependencies**:

   ```bash
   npm install
   ```

3. **Run in development mode** from the repository root, so the Tauri shell can find `music-sorter-core.exe`:

   ```bash
   npm run tauri dev
   ```

   Or build a release binary with `npm run tauri build` (produced under `src-tauri/target/release`); copy `music-sorter-core.exe` and `fpcalc.exe` next to it before running.

## Usage

1. Open **Settings** and point the app at the folder to sort, the output folder, and (optionally) an AcoustID API key and a Discogs personal access token for higher-quality matches.
2. Run a **duplicate scan** and resolve the candidate pairs it finds in the **Duplicates** view.
3. Run a **tag scan**, review the metadata candidates per album/track in the **Tag Inspector**, pick a source or cover art, then approve or skip.
4. Use **Mirroring** to sync the sorted library to its output location, and **Database** to browse the indexed tracks and export the catalog.

## Architecture

- `src/` — the headless C++20 core (`music-sorter-core`, built with CMake + Ninja). Serves HTTP+JSON on `127.0.0.1:8765` (`HttpServer`), does fingerprinting (`AcousticAnalyzer`), playback (`AudioEngine`, miniaudio), metadata fetching and consensus scoring (`FetchServices`, `ConsensusAggregator`), and owns the SQLite database (`DatabaseManager`, bundled `sqlite3.c`).
- `src-tauri/` — the Tauri 2 (Rust) desktop shell. Spawns `music-sorter-core.exe` as a child process on startup and stops it (`POST /api/shutdown`, then a hard kill) when the window closes.
- `ui/` — the React 19 + TypeScript + Tailwind frontend (Vite), talking to the core over the local REST API (`ui/store.ts`).
- `src/tests/` — a CTest suite (`MusicSorterTests`) covering the metadata fetchers, consensus aggregator, filename parser and duplicate clustering, including adversarial cases; run it with `run_tests.bat`.

## Roadmap

### Metadata & lyrics fetch services
- [x] TouhouDB / VocaDB / UtaiteDB API (`https://touhoudb.com/api`, `https://vocadb.net/api`, `https://utaitedb.net/api`) — REST API (JSON), tracks, durations in seconds, artists, original high-res covers
- [x] TouhouDB / VocaDB / UtaiteDB lyrics fallback — unsynced lyrics (original Japanese, Romaji, English translation) as fallback when synced LRC is not found in LRCLIB
- [x] TouhouDB / VocaDB / UtaiteDB language preference & Romaji fallback — Romaji track, album, and artist title support (`lang=Romaji`) with fallback chain: Romaji -> English -> Japanese, interactive `[RO] [EN] [JP]` language switcher buttons
- [x] THBWiki API (`https://thwiki.cc/album.php`) — lightweight JSON album queries (`m=sa`, `m=ga`), tracklists, circle/staff info, original ZUN theme references (`ogmusic`), high-resolution cover art (up to 800px), and synced `.lrc` lyrics integration via `https://lyrics.thwiki.cc/`
- [ ] RateYourMusic / Sonemic API (`https://rateyourmusic.com/data-access/register-interest/`) — upcoming official Sonemic API and datasets (pending API access)

### Albums to download
- [ ] Diabolic Phantasma - Daydream In the Dead of Night (DBPS-001)

## License

[MIT](LICENSE) — Copyright (c) 2026 [renkagod](https://github.com/renkagod).
