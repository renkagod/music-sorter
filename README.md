# Music Sorter

Desktop application for acoustic analysis, duplicate detection, and music collection tagging.

## TODO

### Metadata & Lyrics Fetch Services
- [x] **TouhouDB / VocaDB / UtaiteDB API** (`https://touhoudb.com/api`, `https://vocadb.net/api`, `https://utaitedb.net/api`)
  - REST API (JSON), tracks, durations in seconds, artists, original high-res covers
- [ ] **TouhouDB / VocaDB / UtaiteDB Lyrics Fallback**
  - Unsynced lyrics (original Japanese, Romaji, English translation) as fallback when synced LRC is not found in LRCLIB
- [x] **TouhouDB / VocaDB / UtaiteDB Language Preference & Romaji Fallback**
  - Romaji track, album, and artist title support (`lang=Romaji`) with fallback chain: Romaji -> English -> Japanese, interactive `[RO] [EN] [JP]` language switcher buttons
- [ ] **THBWiki API** (`https://thwiki.cc/api.php`)
  - MediaWiki API, wikitext templates `{{同人专辑信息}}` and `{{同人曲目信息}}`, staff and original ZUN themes
- [ ] **Genius API** (`https://genius.com/api`, `https://api.genius.com`)
  - Track lyrics, track/album artwork
- [ ] **RateYourMusic / Sonemic API** (`https://rateyourmusic.com/data-access/register-interest/`)
  - Upcoming official Sonemic API and datasets (pending API access)

### Rate Limits / Проверка ограничений частоты запросов
- [ ] Проверить и протестировать соблюдение рейт-лимитов для внешних сервисов:
  - MusicBrainz: базовый лимит 1 запрос в секунду на IP
  - Discogs: 60 запросов в минуту с персональным токеном (25 запросов в минуту без токена)
  - AcoustID: лимиты на частоту вызовов /v2/lookup
  - TouhouDB / VocaDB / UtaiteDB: допустимая частота пакетных поисковых запросов
  - CoverArtArchive: допустимая нагрузка при загрузке полноразмерных обложек

### Albums to Download
- [ ] **Diabolic Phantasma - Daydream In the Dead of Night** (DBPS-001)

