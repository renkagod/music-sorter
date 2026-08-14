# Music Sorter

Desktop application for acoustic analysis, duplicate detection, and music collection tagging.

## TODO

### Metadata & Lyrics Fetch Services
- [x] **TouhouDB / VocaDB / UtaiteDB API** (`https://touhoudb.com/api`, `https://vocadb.net/api`, `https://utaitedb.net/api`)
  - REST API (JSON), tracks, durations in seconds, artists, original high-res covers
- [x] **TouhouDB / VocaDB / UtaiteDB Lyrics Fallback**
  - Unsynced lyrics (original Japanese, Romaji, English translation) as fallback when synced LRC is not found in LRCLIB
- [x] **TouhouDB / VocaDB / UtaiteDB Language Preference & Romaji Fallback**
  - Romaji track, album, and artist title support (`lang=Romaji`) with fallback chain: Romaji -> English -> Japanese, interactive `[RO] [EN] [JP]` language switcher buttons
- [x] **THBWiki API** (`https://thwiki.cc/album.php`)
  - Lightweight JSON album queries (`m=sa`, `m=ga`), tracklists, circle/staff info, original ZUN theme references (`ogmusic`), high-resolution cover art (up to 800px), and synced `.lrc` lyrics integration via `https://lyrics.thwiki.cc/`
- [ ] **RateYourMusic / Sonemic API** (`https://rateyourmusic.com/data-access/register-interest/`)
  - Upcoming official Sonemic API and datasets (pending API access)

### Albums to Download
- [ ] **Diabolic Phantasma - Daydream In the Dead of Night** (DBPS-001)
