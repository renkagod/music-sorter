# Project: MusicSorter Metadata Enhancement Pipeline

## Architecture
MusicSorter is a native Windows C++20 desktop application built with DirectX 11 and Dear ImGui. It automates music library organization, audio file tagging, and metadata retrieval.

### Key Components & Data Flow
1. **Audio Scanning & Tag Initialization (`AppWindow.cpp`)**:
   - Discovers audio files (`.flac`, `.mp3`) recursively.
   - Parses filename patterns heuristically (`MetadataUtils.hpp`) to cleanly isolate artist, title, album, and track number when tags are missing.
   - Groups files into `AlbumCluster` structures. Single tracks or disparate tracks in folder collections are isolated into independent loose track clusters.
2. **Metadata Providers (`FetchServices.hpp`)**:
   - Native HTTPS client using Windows WinINet API (`<wininet.h>`) with TLS Schannel.
   - Built-in recursive JSON parser (`JsonVal`).
   - Existing providers: AcoustID, MusicBrainz, TouhouDB, THBWiki, VocaDB, UtaiteDB, Discogs, CoverArtArchive, LRCLIB, QQ Music.
   - New native C++ providers:
     - **Last.fm API**: Track and album queries (`track.search`, `track.getInfo`, `album.getInfo`) with dedicated throttling (~200 ms).
     - **YouTube Music Innertube API**: Direct HTTPS POST queries to `music.youtube.com/youtubei/v1/search` with desktop client context (`WEB_REMIX`) and parsing of `musicResponsiveListItemRenderer` with throttling (~600 ms).
3. **Guardrails & Anti-False-Positive Engine (`MetadataUtils.hpp`)**:
   - `ValidateAlbumMatch`: Strict candidate validation requiring >= 80% confidence based on artist similarity and tracklist overlap (minimum track count and title overlap). Rejects mismatched albums (e.g. Macroblank vs ExileLord).
   - Discogs popularity scoring capped to a minimal tie-breaker.
   - `ValidateLyricMatch`: Blocks lyric queries and attachments from LRCLIB/QQ Music if the artist is unknown or does not match the lyric source artist.
4. **Consensus Aggregator (`ConsensusAggregator.hpp` / `MetadataUtils.hpp`)**:
   - Concurrently or aggregatedly queries enabled providers.
   - Scores candidates by text similarity and multi-provider agreement.
   - Marks tracks with `hasConflict = true` when confidence < 80% or providers disagree.
5. **Interactive UI (`AppWindow.cpp`)**:
   - ImGui Inspector displays conflict badges for low-confidence or conflicting tracks.
   - Interactive candidate picker (dropdown/modal) to preview and select from all returned provider metadata with single-click assignment.
   - "Fetch from all providers" button in the inspector to trigger comprehensive multi-source queries.
6. **Testing & Verification**:
   - C++20 test suite in `src/tests/` using `TestFramework.hpp` compiled into `MusicSorterTests.exe` and executed via CTest and `run_tests.bat`.

---

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| F1 | Heuristic Filename Parsing | Parse `Artist - Title`, `Track - Artist - Title`, `Artist - Album - Track - Title`, bracketed formats, and numbered prefixes; cleanly remove artist from title. | M1 | ORIGINAL_REQUEST §R1 |
| F2 | Loose Tracks Support | Disparate tracks or tracks without a shared album tag in folders (like `TO SORT`) remain independent single-track clusters instead of collapsing into a false multi-track album. | M1 | ORIGINAL_REQUEST §R1 |
| F3 | Album Match Tracklist & Artist Guardrails | Validate album candidates comparing tracklist overlap and artist similarity. Reject match if confidence < 80%. | M2 | ORIGINAL_REQUEST §R2 |
| F4 | Anti-False-Positive Discogs Scoring | Cap or eliminate unverified `(have + want)` community popularity score overrides to prevent popular false matches. | M2 | ORIGINAL_REQUEST §R2 |
| F5 | Strict Lyric Guardrails | Never fetch or attach lyrics from QQ Music or LRCLIB if track artist is unknown or does not match lyric source artist. | M2 | ORIGINAL_REQUEST §R2 |
| F6 | Native C++ Last.fm Fetcher | Direct HTTPS WinINet requests to Last.fm API for `track.search`, `track.getInfo`, and `album.getInfo` with rate-limiting and JSON parsing. | M3 | ORIGINAL_REQUEST §R3 |
| F7 | Native C++ YouTube Music Innertube Fetcher | Direct HTTPS POST WinINet requests to YouTube Music Innertube search endpoint with WEB_REMIX client context, rate-limiting, and JSON parsing. | M3 | ORIGINAL_REQUEST §R3 |
| F8 | Multi-Provider Aggregator & Consensus Engine | Aggregate results across all enabled providers, compute confidence scores, and determine agreement/conflict status. | M4 | ORIGINAL_REQUEST §R4 |
| F9 | ImGui Conflict Indicator & Candidate Picker | Display conflict indicator on ambiguous tracks/albums; provide candidate selection dropdown/modal with single-click assignment. | M4 | ORIGINAL_REQUEST §R4 |
| F10 | "Fetch from all providers" Inspector Button | UI button in Track Inspector triggering full multi-provider search and populating candidates list. | M4 | ORIGINAL_REQUEST §R4 |
| F11 | Automated Unit & Integration Tests | Comprehensive unit tests for filename parsing, guardrails, fetchers, and consensus aggregation in `src/tests/`. | M5 | ORIGINAL_REQUEST §Verification |
| F12 | Manual Verification & ExileLord Test Set | Verification in DirectX 11/ImGui application against `D:\media\music\TO SORT` ExileLord singles with zero false albums/lyrics. | M5 | ORIGINAL_REQUEST §Verification |

---

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1 | Heuristic Filename Parsing & Loose Tracks Support | Implement `ParseFilenameHeuristic`, update `StartTagScan`, isolate loose single tracks in `AppWindow.cpp`. | None | DONE |
| M2 | Strict Match Guardrails & Anti-False-Positive Filtering | Implement `ValidateAlbumMatch`, `ValidateLyricMatch`, cap Discogs popularity, enforce 80% confidence threshold. | M1 | PLANNED |
| M3 | Standalone C++ Multi-Provider Fetchers | Implement native WinINet Last.fm API and YouTube Music Innertube fetchers in `FetchServices.hpp`. | None | PLANNED |
| M4 | Aggregator, Consensus Logic & ImGui Inspection UI | Implement `MetadataCandidate`, consensus scoring, conflict badge, candidate dropdown/modal, and "Fetch from all" button. | M2, M3 | PLANNED |
| M5 | E2E Testing Suite & Full Verification | Automated unit tests covering Tiers 1-4, CTest verification, and live scan verification on `D:\media\music\TO SORT`. | M1, M2, M3, M4 | PLANNED |

---

## Interface Contracts

### M1: Heuristic Filename Parsing
```cpp
struct ParsedFilenameInfo {
    std::string artist;
    std::string album;
    std::string title;
    int trackNumber{0};
    bool hasArtist{false};
    bool hasAlbum{false};
    bool hasTrackNumber{false};
};

ParsedFilenameInfo ParseFilenameHeuristic(const std::string& filenameStem);
```

### M2: Match Guardrails
```cpp
struct GuardrailValidationResult {
    bool passed{false};
    double confidence{0.0};       // 0.0 to 1.0
    double artistSimilarity{0.0};
    double tracklistOverlap{0.0};
    std::string reason;
};

GuardrailValidationResult ValidateAlbumMatch(
    const std::string& queryArtist,
    const std::string& queryAlbum,
    const std::vector<std::string>& localTitles,
    const std::string& candidateArtist,
    const std::string& candidateAlbum,
    const std::vector<std::string>& candidateTracklist
);

bool ValidateLyricMatch(
    const std::string& trackArtist,
    const std::string& trackTitle,
    const std::string& lyricArtist,
    const std::string& lyricTitle
);
```

### M3: Native C++ Multi-Provider Fetchers
```cpp
struct LastFmTrackInfo {
    std::string artist;
    std::string title;
    std::string album;
    std::string url;
    std::string coverUrl;
    int durationSec{0};
};

struct LastFmAlbumInfo {
    std::string artist;
    std::string album;
    std::string releaseDate;
    std::string coverUrl;
    std::vector<std::string> tracklist;
};

struct YtMusicTrackInfo {
    std::string artist;
    std::string title;
    std::string album;
    std::string videoId;
    std::string coverUrl;
    int durationSec{0};
};

// FetchServices namespace functions
std::vector<LastFmTrackInfo> SearchLastFmTrack(const std::string& artist, const std::string& title, const std::string& apiKey = "");
LastFmAlbumInfo GetLastFmAlbumInfo(const std::string& artist, const std::string& album, const std::string& apiKey = "");
std::vector<YtMusicTrackInfo> SearchYouTubeMusic(const std::string& query);
```

### M4: Metadata Candidate & Aggregator
```cpp
struct MetadataCandidate {
    std::string providerName;    // "MusicBrainz", "Discogs", "Last.fm", "YouTube Music", etc.
    std::string artist;
    std::string album;
    std::string title;
    std::string year;
    int trackNumber{0};
    std::string coverUrl;
    double confidence{0.0};
    std::vector<std::string> tracklist;
};

struct AggregationResult {
    MetadataCandidate bestCandidate;
    std::vector<MetadataCandidate> allCandidates;
    bool hasConflict{false};
    double confidence{0.0};
};

AggregationResult AggregateTrackCandidates(
    const std::string& currentArtist,
    const std::string& currentTitle,
    const std::vector<MetadataCandidate>& rawCandidates
);
```

---

## Code Layout
- `src/include/MetadataUtils.hpp`: Heuristic filename parser, string normalization, Levenshtein distance, token similarity, guardrail validators, and `JsonVal`.
- `src/include/FetchServices.hpp`: WinINet HTTP engine, rate limiters, provider implementations (MusicBrainz, Discogs, TouhouDB, Last.fm, YouTube Music, LRCLIB, QQ Music).
- `src/include/AppWindow.hpp`: `TagReviewItem` structure (enhanced with `candidates`, `hasConflict`, `confidenceScore`), GUI state definitions.
- `src/src/AppWindow.cpp`: Tag scan pipeline (`StartTagScan`), album clustering logic, track matching, ImGui rendering (`TagInspectorCardPerfectFit`, candidate picker, buttons).
- `src/tests/`: Unit test suite:
  - `TestFramework.hpp`: Header-only test assertions and test registry.
  - `test_main.cpp`: Test runner entry point.
  - `test_filename_parser.cpp`: Unit tests for R1 filename parsing.
  - `test_guardrails_scoring.cpp`: Unit tests for R2 guardrails and anti-false-positive filtering.
  - `test_multi_provider_fetchers.cpp`: Unit tests for R3 Last.fm and YouTube Music parsing.
  - `test_aggregator_consensus.cpp`: Unit tests for R4 multi-source consensus and conflict detection.
