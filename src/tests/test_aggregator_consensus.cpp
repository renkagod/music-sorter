#include "TestFramework.hpp"
#include "../include/MetadataUtils.hpp"
#include "../include/FetchServices.hpp"
#include "../include/ConsensusAggregator.hpp"

#ifndef ASSERT_LT
#define ASSERT_LT(actual, expected) ASSERT_TRUE((actual) < (expected))
#endif

#ifndef ASSERT_GT
#define ASSERT_GT(actual, expected) ASSERT_TRUE((actual) > (expected))
#endif

using namespace ConsensusAggregator;

// ============================================================================
// TIER 1: FEATURE COVERAGE (Core Consensus Logic)
// ============================================================================

TEST_CASE("Consensus Aggregator Tier 1", "Unanimous Three-Provider Agreement") {
    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "Daft Punk", "Discovery", "One More Time", "2001", 1, "https://cover/mb.jpg", 0.95, {} },
        { "Last.fm", "Daft Punk", "Discovery", "One More Time", "2001", 1, "https://cover/lastfm.jpg", 0.90, {} },
        { "Discogs", "Daft Punk", "Discovery", "One More Time", "2001", 1, "https://cover/discogs.jpg", 0.92, {} }
    };

    auto res = AggregateTrackCandidates("Daft Punk", "One More Time", raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.95);
    ASSERT_STR_EQ(res.bestCandidate.artist, "Daft Punk");
    ASSERT_STR_EQ(res.bestCandidate.title, "One More Time");
    ASSERT_EQ(res.allCandidates.size(), 3);
}

TEST_CASE("Consensus Aggregator Tier 1", "Two-Provider Agreement: Last.fm and YouTube Music") {
    std::vector<MetadataCandidate> raw = {
        { "Last.fm", "ExileLord", "", "Soulless 4", "2015", 0, "", 0.85, {} },
        { "YouTube Music", "ExileLord", "", "Soulless 4", "2015", 0, "https://ytimg/1.jpg", 0.85, {} }
    };

    auto res = AggregateTrackCandidates("ExileLord", "Soulless 4", raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.90);
    ASSERT_STR_EQ(res.bestCandidate.artist, "ExileLord");
    ASSERT_STR_EQ(res.bestCandidate.title, "Soulless 4");
}

TEST_CASE("Consensus Aggregator Tier 1", "Single Provider High-Confidence Match") {
    std::vector<MetadataCandidate> raw = {
        { "TouhouDB", "ZUN", "Embodiment of Scarlet Devil", "Septette for the Dead Princess", "2002", 6, "", 0.94, {} }
    };

    auto res = AggregateTrackCandidates("ZUN", "Septette for the Dead Princess", raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.80);
    ASSERT_STR_EQ(res.bestCandidate.providerName, "TouhouDB");
}

TEST_CASE("Consensus Aggregator Tier 1", "Conflicting Providers: Divergent Artists") {
    std::vector<MetadataCandidate> raw = {
        { "Last.fm", "ExileLord", "", "Soulless 4", "2015", 0, "", 0.82, {} },
        { "MusicBrainz", "Macroblank", "痛みの永遠", "Meat Grinder", "2021", 1, "", 0.81, {} }
    };

    auto res = AggregateTrackCandidates("Unknown", "Track 01", raw);

    ASSERT_TRUE(res.hasConflict);
    ASSERT_EQ(res.allCandidates.size(), 2);
}

TEST_CASE("Consensus Aggregator Tier 1", "Low Confidence Threshold Rejection (< 80%)") {
    std::vector<MetadataCandidate> raw = {
        { "YouTube Music", "Indie Artist", "", "Acoustic Demo", "", 0, "", 0.65, {} }
    };

    auto res = AggregateTrackCandidates("Unknown", "Untitled", raw);

    ASSERT_TRUE(res.hasConflict);
    ASSERT_LT(res.confidence, 0.80);
}

TEST_CASE("Consensus Aggregator Tier 1", "Tie-Breaking Rule: Metadata Completeness Preference") {
    std::vector<MetadataCandidate> raw = {
        { "ProviderA", "The Beatles", "", "Yesterday", "", 0, "", 0.85, {} },
        { "ProviderB", "The Beatles", "Help!", "Yesterday", "1965", 13, "https://cover/help.jpg", 0.85, {} }
    };

    auto res = AggregateTrackCandidates("The Beatles", "Yesterday", raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_STR_EQ(res.bestCandidate.album, "Help!");
    ASSERT_STR_EQ(res.bestCandidate.year, "1965");
}

TEST_CASE("Consensus Aggregator Tier 1", "Tie-Breaking Rule: Authoritative Provider Precedence") {
    std::vector<MetadataCandidate> raw = {
        { "Last.fm", "Pink Floyd", "The Wall", "Comfortably Numb", "1979", 6, "", 0.88, {} },
        { "MusicBrainz", "Pink Floyd", "The Wall", "Comfortably Numb", "1979", 6, "", 0.88, {} }
    };

    auto res = AggregateTrackCandidates("Pink Floyd", "Comfortably Numb", raw);

    ASSERT_STR_EQ(res.bestCandidate.providerName, "MusicBrainz");
}

TEST_CASE("Consensus Aggregator Tier 1", "Candidate List Sorted Descending by Confidence") {
    std::vector<MetadataCandidate> raw = {
        { "ProviderA", "Artist", "Album", "Title", "", 0, "", 0.50, {} },
        { "ProviderB", "Artist", "Album", "Title", "", 0, "", 0.92, {} },
        { "ProviderC", "Artist", "Album", "Title", "", 0, "", 0.73, {} }
    };

    auto res = AggregateTrackCandidates("Artist", "Title", raw);

    ASSERT_EQ(res.allCandidates.size(), 3);
    ASSERT_GE(res.allCandidates[0].confidence, res.allCandidates[1].confidence);
    ASSERT_GE(res.allCandidates[1].confidence, res.allCandidates[2].confidence);
}

// ============================================================================
// TIER 2: BOUNDARY & CORNER CASES
// ============================================================================

TEST_CASE("Consensus Aggregator Tier 2", "Empty Raw Candidates Vector") {
    std::vector<MetadataCandidate> raw;

    auto res = AggregateTrackCandidates("Some Artist", "Some Title", raw);

    ASSERT_EQ(res.confidence, 0.0);
    ASSERT_FALSE(res.hasConflict);
    ASSERT_TRUE(res.allCandidates.empty());
    ASSERT_TRUE(res.bestCandidate.artist.empty());
}

TEST_CASE("Consensus Aggregator Tier 2", "Candidate with Empty Title or Artist") {
    std::vector<MetadataCandidate> raw = {
        { "ProviderA", "", "", "", "", 0, "", 0.90, {} },
        { "ProviderB", "Valid Artist", "Valid Album", "Valid Title", "2020", 1, "", 0.82, {} }
    };

    auto res = AggregateTrackCandidates("Valid Artist", "Valid Title", raw);

    ASSERT_STR_EQ(res.bestCandidate.artist, "Valid Artist");
    ASSERT_STR_EQ(res.bestCandidate.title, "Valid Title");
}

TEST_CASE("Consensus Aggregator Tier 2", "Case and Whitespace Invariance") {
    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "  DAFT PUNK  ", "DISCOVERY", "ONE MORE TIME", "2001", 1, "", 0.90, {} },
        { "Last.fm", "daft punk", "discovery", "one more time", "2001", 1, "", 0.90, {} }
    };

    auto res = AggregateTrackCandidates("Daft Punk", "One More Time", raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.90);
}

TEST_CASE("Consensus Aggregator Tier 2", "Punctuation Variations") {
    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "Guns N' Roses", "Appetite for Destruction", "Sweet Child O' Mine", "1987", 9, "", 0.88, {} },
        { "Last.fm", "Guns N Roses", "Appetite for Destruction", "Sweet Child O Mine", "1987", 9, "", 0.88, {} }
    };

    auto res = AggregateTrackCandidates("Guns N' Roses", "Sweet Child O' Mine", raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.90);
}

TEST_CASE("Consensus Aggregator Tier 2", "Extreme Candidate Count (15 Candidates)") {
    std::vector<MetadataCandidate> raw;
    for (int i = 0; i < 15; ++i) {
        raw.push_back({ "Prov_" + std::to_string(i), "Artist", "Album", "Track " + std::to_string(i), "", 0, "", 0.60 + (i * 0.02), {} });
    }

    auto res = AggregateTrackCandidates("Artist", "Track", raw);

    ASSERT_EQ(res.allCandidates.size(), 15);
    ASSERT_STR_EQ(res.allCandidates[0].providerName, "Prov_14");
}

TEST_CASE("Consensus Aggregator Tier 2", "Unicode and CJK Text Match") {
    std::vector<MetadataCandidate> raw = {
        { "VocaDB", "ryo", "supercell", "メルト", "2007", 1, "", 0.95, {} },
        { "YouTube Music", "ryo", "supercell", "メルト", "2007", 1, "", 0.90, {} }
    };

    auto res = AggregateTrackCandidates("ryo", "メルト", raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.95);
    ASSERT_STR_EQ(res.bestCandidate.title, "メルト");
}

TEST_CASE("Consensus Aggregator Tier 2", "Confidence Range Clamping") {
    std::vector<MetadataCandidate> raw = {
        { "ProviderA", "Artist", "Album", "Title", "", 0, "", 1.80, {} },
        { "ProviderB", "Artist", "Album", "Title", "", 0, "", -0.50, {} }
    };

    auto res = AggregateTrackCandidates("Artist", "Title", raw);

    ASSERT_LE(res.confidence, 1.0);
    ASSERT_GE(res.confidence, 0.0);
}

TEST_CASE("Consensus Aggregator Tier 2", "Album Candidate with Empty Tracklist") {
    std::vector<std::string> localTitles = { "Track 1", "Track 2", "Track 3" };
    std::vector<MetadataCandidate> raw = {
        { "Discogs", "Artist", "Album", "", "2020", 0, "", 0.85, {} }
    };

    auto res = AggregateAlbumCandidates("Artist", "Album", localTitles, raw);

    ASSERT_TRUE(res.hasConflict);
}

TEST_CASE("Consensus Aggregator Tier 2", "Album Candidate Caller Confidence Preserved") {
    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "Daft Punk", "Discovery", "", "2001", 0, "", 0.93, {} }
    };
    std::vector<std::string> localTitles;
    auto res = AggregateAlbumCandidates("Daft Punk", "Discovery", localTitles, raw);
    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.90);
}

TEST_CASE("Consensus Aggregator Tier 2", "Album Candidate Sequel Mismatch Penalized") {
    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "Final Fantasy", "Final Fantasy VIII OST", "", "1999", 0, "", 0.90, {} }
    };
    std::vector<std::string> localTitles;
    auto res = AggregateAlbumCandidates("Final Fantasy", "Final Fantasy VII OST", localTitles, raw);
    ASSERT_TRUE(res.hasConflict);
    ASSERT_LE(res.confidence, 0.20);
}

// ============================================================================
// TIER 3: CROSS-FEATURE COMBINATIONS
// ============================================================================

TEST_CASE("Consensus Aggregator Tier 3", "Pipeline: Heuristic Parsing Feeds Consensus Query") {
    auto parsed = ParseFilenameHeuristic("01. Daft Punk - One More Time");
    ASSERT_TRUE(parsed.hasArtist);
    ASSERT_STR_EQ(parsed.artist, "Daft Punk");
    ASSERT_STR_EQ(parsed.title, "One More Time");

    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "Daft Punk", "Discovery", "One More Time", "2001", 1, "", 0.92, {} },
        { "Last.fm", "Daft Punk", "Discovery", "One More Time", "2001", 1, "", 0.89, {} }
    };

    auto res = AggregateTrackCandidates(parsed.artist, parsed.title, raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.90);
    ASSERT_STR_EQ(res.bestCandidate.artist, parsed.artist);
}

TEST_CASE("Consensus Aggregator Tier 3", "Multi-Provider Pipeline: Last.fm and YouTube Music Ingestion") {
    std::string mockLastFm = R"json({
      "results": {
        "trackmatches": {
          "track": [
            { "name": "Soulless 4", "artist": "ExileLord", "url": "https://last.fm/..." }
          ]
        }
      }
    })json";

    auto lastFmTracks = FetchServices::ParseLastFmTrackSearchJson(mockLastFm);
    ASSERT_EQ(lastFmTracks.size(), 1);

    std::vector<MetadataCandidate> candidates;
    for (const auto& t : lastFmTracks) {
        candidates.push_back({ "Last.fm", t.artist, t.album, t.title, "", 0, t.coverUrl, 0.88, {} });
    }
    candidates.push_back({ "YouTube Music", "ExileLord", "", "Soulless 4", "2015", 0, "", 0.86, {} });

    auto res = AggregateTrackCandidates("ExileLord", "Soulless 4", candidates);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.90);
    ASSERT_STR_EQ(res.bestCandidate.artist, "ExileLord");
}

TEST_CASE("Consensus Aggregator Tier 3", "Loose Singles Folder Isolation") {
    std::vector<std::string> looseFiles = {
        "ExileLord - Soulless 4",
        "Macroblank - Meat Grinder",
        "ZUN - Bad Apple"
    };

    for (const auto& fileStem : looseFiles) {
        auto parsed = ParseFilenameHeuristic(fileStem);
        std::vector<MetadataCandidate> raw = {
            { "Provider", parsed.artist, "", parsed.title, "", 0, "", 0.85, {} }
        };
        auto res = AggregateTrackCandidates(parsed.artist, parsed.title, raw);
        ASSERT_FALSE(res.hasConflict);
        ASSERT_STR_EQ(res.bestCandidate.artist, parsed.artist);
    }
}

TEST_CASE("Consensus Aggregator Tier 3", "Consensus with Guardrail Lyric Validation Integration") {
    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "ExileLord", "", "Soulless 4", "2015", 0, "", 0.88, {} }
    };

    auto res = AggregateTrackCandidates("ExileLord", "Soulless 4", raw);
    ASSERT_FALSE(res.hasConflict);

    bool lyricValid = ValidateLyricMatch(res.bestCandidate.artist, res.bestCandidate.title, "Various Artists", "Soulless 4");
    ASSERT_FALSE(lyricValid);
}

TEST_CASE("Consensus Aggregator Tier 3", "Album Consensus with Guardrail Overlap Validation") {
    std::vector<std::string> localTracks = { "Track A", "Track B", "Track C" };
    std::vector<std::string> candidateTracks = { "Track A", "Track B", "Track C" };

    std::vector<MetadataCandidate> raw = {
        { "Discogs", "Artist X", "Album Y", "", "2020", 0, "", 0.90, candidateTracks }
    };

    auto res = AggregateAlbumCandidates("Artist X", "Album Y", localTracks, raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.85);
}

// ============================================================================
// TIER 4: REAL-WORLD APPLICATION SCENARIOS
// ============================================================================

TEST_CASE("Consensus Aggregator Tier 4", "ExileLord TO SORT Single: Soulless 4") {
    std::vector<MetadataCandidate> raw = {
        { "YouTube Music", "ExileLord", "", "Soulless 4", "2015", 0, "https://ytimg.com/s4.jpg", 0.88, {} },
        { "Last.fm", "ExileLord", "", "Soulless 4", "", 0, "", 0.86, {} }
    };

    auto res = AggregateTrackCandidates("ExileLord", "Soulless 4", raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.90);
    ASSERT_STR_EQ(res.bestCandidate.artist, "ExileLord");
    ASSERT_TRUE(res.bestCandidate.album.empty());
}

TEST_CASE("Consensus Aggregator Tier 4", "Discogs Popularity False Match Rejected") {
    std::vector<std::string> exileLordSingles = { "Soulless 4", "Soulless 5" };
    std::vector<std::string> macroblankTracks = { "Track 1", "Track 2", "Track 3", "Track 4" };

    std::vector<MetadataCandidate> raw = {
        { "Discogs", "Macroblank", "痛みの永遠", "", "2021", 0, "", 0.95, macroblankTracks }
    };

    auto res = AggregateAlbumCandidates("ExileLord", "TO SORT", exileLordSingles, raw);

    ASSERT_TRUE(res.hasConflict);
    ASSERT_LT(res.confidence, 0.80);
}

TEST_CASE("Consensus Aggregator Tier 4", "Touhou Doujin Release: TouhouDB vs MusicBrainz Arrangement") {
    std::vector<MetadataCandidate> raw = {
        { "TouhouDB", "COOL&CREATE", "Flowering Night", "Help me, ERINNNNNN!!", "2004", 1, "", 0.93, {} },
        { "MusicBrainz", "ビートまりお", "Flowering Night", "Help me, ERINNNNNN!!", "2004", 1, "", 0.91, {} }
    };

    auto res = AggregateTrackCandidates("COOL&CREATE", "Help me, ERINNNNNN!!", raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.90);
    ASSERT_EQ(res.allCandidates.size(), 2);
}

TEST_CASE("Consensus Aggregator Tier 4", "Live vs Studio Album Candidate Disambiguation") {
    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "Pink Floyd", "The Wall", "Comfortably Numb", "1979", 6, "", 0.90, {} },
        { "Discogs", "Pink Floyd", "Pulse (Live)", "Comfortably Numb", "1995", 14, "", 0.88, {} }
    };

    auto res = AggregateTrackCandidates("Pink Floyd", "Comfortably Numb", raw);

    ASSERT_EQ(res.allCandidates.size(), 2);
    ASSERT_STR_EQ(res.allCandidates[0].album, "The Wall");
    ASSERT_STR_EQ(res.allCandidates[1].album, "Pulse (Live)");
}

TEST_CASE("Consensus Aggregator Tier 4", "ExileLord 43 Singles Batch Simulation") {
    for (int i = 1; i <= 43; ++i) {
        std::string title = "ExileTrack " + std::to_string(i);
        std::vector<MetadataCandidate> raw = {
            { "YouTube Music", "ExileLord", "", title, "2015", 0, "", 0.85, {} }
        };

        auto res = AggregateTrackCandidates("ExileLord", title, raw);
        ASSERT_FALSE(res.hasConflict);
        ASSERT_STR_EQ(res.bestCandidate.artist, "ExileLord");
        ASSERT_TRUE(res.bestCandidate.album.empty());
    }
}
