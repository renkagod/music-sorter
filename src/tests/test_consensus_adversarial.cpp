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
// ADVERSARIAL SUITE 1: DIFFERING TRACK COUNT SCENARIOS
// ============================================================================

TEST_CASE("Consensus Adversarial", "Diagnostic: Track Matching Investigation") {
    std::vector<std::string> localTracks = { "T1", "T2", "T3", "T4", "T5" };
    std::vector<std::string> candidateTracks = { "T1", "T2", "DiffA", "DiffB", "DiffC" };

    for (const auto& loc : localTracks) {
        int numLoc = ExtractTrailingOrEmbeddedNumber(loc);
        for (const auto& cand : candidateTracks) {
            int numCand = ExtractTrailingOrEmbeddedNumber(cand);
            double sim = ComputeStringSimilarity(loc, cand);
            if (sim >= 0.70) {
                std::cout << "      [MATCH] loc='" << loc << "' (num=" << numLoc << ") cand='" << cand 
                          << "' (num=" << numCand << ") sim=" << sim << "\n";
            }
        }
    }
    auto guard = ValidateAlbumMatch("Artist", "Album", localTracks, "Artist", "Album", candidateTracks);
    std::cout << "      [GUARD] confidence=" << guard.confidence << " overlap=" << guard.tracklistOverlap 
              << " passed=" << guard.passed << "\n";
}

TEST_CASE("Consensus Adversarial", "Differing Track Count: Deluxe Edition with Bonus Tracks Passes") {
    // 10 local standard tracks matching a 14-track Deluxe Edition candidate.
    // Overlap should be high enough (10/10 title match, 10/14 count ratio) to pass >= 80%.
    std::vector<std::string> localTracks;
    for (int i = 1; i <= 10; ++i) {
        localTracks.push_back("Track " + std::to_string(i));
    }

    std::vector<std::string> candidateTracks = localTracks;
    candidateTracks.push_back("Bonus Track 11");
    candidateTracks.push_back("Bonus Track 12");
    candidateTracks.push_back("Bonus Track 13");
    candidateTracks.push_back("Bonus Track 14");

    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "Daft Punk", "Discovery", "", "2001", 0, "https://cover.jpg", 0.0, candidateTracks }
    };

    auto res = AggregateAlbumCandidates("Daft Punk", "Discovery", localTracks, raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.85);
    ASSERT_STR_EQ(res.bestCandidate.album, "Discovery");
}

TEST_CASE("Consensus Adversarial", "Differing Track Count: Severe Truncation Sampler Rejected") {
    // 10 local album tracks compared against a 3-track sampler EP.
    // Title overlap is only 3/10 (0.30), which is < 0.40 and must be clamped to <= 0.35.
    std::vector<std::string> localTracks = {
        "Hotel California", "New Kid in Town", "Life in the Fast Lane", "Wasted Time",
        "Victim of Love", "Pretty Maids All in a Row", "Try and Love Again", "The Last Resort",
        "Bonus Track 9", "Bonus Track 10"
    };

    std::vector<std::string> samplerTracks = {
        "Hotel California", "New Kid in Town", "Life in the Fast Lane"
    };

    std::vector<MetadataCandidate> raw = {
        { "Discogs", "Eagles", "Sampler EP", "", "2020", 0, "", 0.0, samplerTracks }
    };

    auto res = AggregateAlbumCandidates("Eagles", "Hotel California", localTracks, raw);

    ASSERT_TRUE(res.hasConflict);
    ASSERT_LE(res.confidence, 0.35);
}

TEST_CASE("Consensus Adversarial", "Differing Track Count: 12-Track Album vs 120-Track Box Set with Divergent Title") {
    // Local album has 12 tracks. Candidate is a 120-track complete anthology box set with different title.
    // Even though 12 titles exist in the box set, count ratio is 0.10 and album title differs.
    std::vector<std::string> localTracks;
    for (int i = 1; i <= 12; ++i) {
        localTracks.push_back("Hit Single " + std::to_string(i));
    }

    std::vector<std::string> boxSetTracks;
    for (int i = 1; i <= 120; ++i) {
        if (i <= 12) {
            boxSetTracks.push_back("Hit Single " + std::to_string(i));
        } else {
            boxSetTracks.push_back("Rarity Track " + std::to_string(i));
        }
    }

    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "Queen", "The Complete Studio Works (120 Tracks)", "", "1995", 0, "", 0.0, boxSetTracks }
    };

    auto res = AggregateAlbumCandidates("Queen", "A Night at the Opera", localTracks, raw);

    ASSERT_TRUE(res.hasConflict);
    ASSERT_LT(res.confidence, 0.80);
}

TEST_CASE("Consensus Adversarial", "Differing Track Count: Multi-Provider Tracklist Size Divergence Blocks Consensus") {
    // Provider A has 10 tracks, Provider B has 26 tracks.
    // minTracks / maxTracks = 10 / 26 = 0.3846 < 0.60.
    // AreAlbumCandidatesInAgreement must return false, preventing mutual confidence boosting.
    MetadataCandidate candA;
    candA.providerName = "MusicBrainz";
    candA.artist = "Pink Floyd";
    candA.album = "The Wall";
    for (int i = 1; i <= 10; ++i) candA.tracklist.push_back("Track " + std::to_string(i));

    MetadataCandidate candB;
    candB.providerName = "Discogs";
    candB.artist = "Pink Floyd";
    candB.album = "The Wall";
    for (int i = 1; i <= 26; ++i) candB.tracklist.push_back("Track " + std::to_string(i));

    bool agree = AreAlbumCandidatesInAgreement(candA, candB);
    ASSERT_FALSE(agree);
}

TEST_CASE("Consensus Adversarial", "Differing Track Count: 1-Track Candidate vs 8-Track Local Album") {
    // A single-track release matching one song of an 8-track album must fail album validation.
    // Distinct titles ensure no accidental 1-character Levenshtein collision.
    std::vector<std::string> localTracks = {
        "Hotel California", "New Kid in Town", "Life in the Fast Lane", "Wasted Time",
        "Victim of Love", "Pretty Maids All in a Row", "Try and Love Again", "The Last Resort"
    };
    std::vector<std::string> singleTrack = { "Hotel California" };

    std::vector<MetadataCandidate> raw = {
        { "Last.fm", "Eagles", "Hotel California - Single", "", "1976", 0, "", 0.0, singleTrack }
    };

    auto res = AggregateAlbumCandidates("Eagles", "Hotel California", localTracks, raw);

    ASSERT_TRUE(res.hasConflict);
    ASSERT_LE(res.confidence, 0.35);
}

// ============================================================================
// ADVERSARIAL SUITE 2: INVERTED TRACK NUMBERS & PERMUTATIONS
// ============================================================================

TEST_CASE("Consensus Adversarial", "Inverted Track Numbers: Explicit Number Prefixes Reject Inverted Order") {
    // When titles contain explicit track prefixes ("01.", "02.", "03."),
    // inverted candidate ("01.", "02.", "03.") causes number mismatch on tracks 1 and 3.
    // Only track 2 matches (1/3 = 33% < 40%), resulting in penalty clamp to <= 0.35.
    std::vector<std::string> localTracks = {
        "01. Introduction",
        "02. Main Theme",
        "03. Conclusion"
    };

    std::vector<std::string> invertedTracks = {
        "01. Conclusion",
        "02. Main Theme",
        "03. Introduction"
    };

    std::vector<MetadataCandidate> raw = {
        { "Discogs", "Artist", "Symphony", "", "2020", 0, "", 0.0, invertedTracks }
    };

    auto res = AggregateAlbumCandidates("Artist", "Symphony", localTracks, raw);

    ASSERT_TRUE(res.hasConflict);
    ASSERT_LE(res.confidence, 0.35);
}

TEST_CASE("Consensus Adversarial", "Order Permutations: Unnumbered Titles Match Order-Independently") {
    // When titles have no embedded track numbers, permuted order should match all 3 titles.
    std::vector<std::string> localTracks = { "Alpha Song", "Beta Song", "Gamma Song" };
    std::vector<std::string> permutedTracks = { "Gamma Song", "Alpha Song", "Beta Song" };

    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "Artist", "Greek Trilogy", "", "2021", 0, "", 0.0, permutedTracks }
    };

    auto res = AggregateAlbumCandidates("Artist", "Greek Trilogy", localTracks, raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.90);
}

TEST_CASE("Consensus Adversarial", "Sequel Number Mismatch: Disjoint Sequel Movement Rejection") {
    // Local has Part I, Part II, Part III. Candidate has Part IV, Part V, Part VI.
    // Number mismatch prevents any cross-matching, resulting in 0 matches and confidence <= 0.35.
    std::vector<std::string> localTracks = { "Symphony Part I", "Symphony Part II", "Symphony Part III" };
    std::vector<std::string> distantTracks = { "Symphony Part IV", "Symphony Part V", "Symphony Part VI" };

    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "Composer", "Grand Symphony", "", "1990", 0, "", 0.0, distantTracks }
    };

    auto res = AggregateAlbumCandidates("Composer", "Grand Symphony", localTracks, raw);

    ASSERT_TRUE(res.hasConflict);
    ASSERT_LE(res.confidence, 0.35);
}

// ============================================================================
// ADVERSARIAL SUITE 3: EMPTY AND DEGENERATE TRACKLISTS
// ============================================================================

TEST_CASE("Consensus Adversarial", "Empty Inputs: Empty Local Titles and Empty Candidate List") {
    std::vector<std::string> localTracks;
    std::vector<MetadataCandidate> raw;

    auto res = AggregateAlbumCandidates("Artist", "Album", localTracks, raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_EQ(res.confidence, 0.0);
    ASSERT_STR_EQ(res.conflictReason, "No candidates found");
}

TEST_CASE("Consensus Adversarial", "Degenerate Candidate Tracklist: Only Whitespace and Empty Strings") {
    // Candidate tracklist has 3 empty or whitespace-only elements.
    // Matched count must be 0, leading to tracklistOverlap = 0.0 and confidence <= 0.35.
    std::vector<std::string> localTracks = { "Song 1", "Song 2", "Song 3" };
    std::vector<std::string> blankTracks = { "", "   ", "\t" };

    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "Artist", "Album", "", "2020", 0, "", 0.0, blankTracks }
    };

    auto res = AggregateAlbumCandidates("Artist", "Album", localTracks, raw);

    ASSERT_TRUE(res.hasConflict);
    ASSERT_LE(res.confidence, 0.35);
}

TEST_CASE("Consensus Adversarial", "Empty Candidate Tracklist: Single Provider Capped at 75 Percent") {
    // Single provider returns an album match with empty tracklist.
    // Confidence must be capped to at most 0.75, triggering conflict (< 80%).
    std::vector<std::string> localTracks = { "Track 1", "Track 2", "Track 3" };
    std::vector<MetadataCandidate> raw = {
        { "Discogs", "Artist", "Album", "", "2020", 0, "", 0.0, {} }
    };

    auto res = AggregateAlbumCandidates("Artist", "Album", localTracks, raw);

    ASSERT_TRUE(res.hasConflict);
    ASSERT_LE(res.confidence, 0.75);
    ASSERT_TRUE(res.conflictReason.find("below 80% threshold") != std::string::npos);
}

TEST_CASE("Consensus Adversarial", "Empty Candidate Tracklist: Multi-Provider Consensus Boost Above Threshold") {
    // When two distinct providers agree on artist and album even without tracklists,
    // they boost each other from 0.75 by +0.10 to 0.85, clearing the 80% threshold.
    std::vector<std::string> localTracks = { "Track 1", "Track 2", "Track 3" };
    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "Daft Punk", "Discovery", "", "2001", 0, "", 0.0, {} },
        { "Last.fm", "Daft Punk", "Discovery", "", "2001", 0, "", 0.0, {} }
    };

    auto res = AggregateAlbumCandidates("Daft Punk", "Discovery", localTracks, raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.80);
    ASSERT_STR_EQ(res.bestCandidate.providerName, "MusicBrainz");
}

TEST_CASE("Consensus Adversarial", "Pure Album Search: Empty Local Titles Preserves Base Confidence") {
    // When localTitles is empty (pure album search without files),
    // candidates without tracklists preserve base confidence without the 0.75 cap.
    std::vector<std::string> localTracks;
    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "Daft Punk", "Discovery", "", "2001", 0, "https://cover.jpg", 0.0, {} }
    };

    auto res = AggregateAlbumCandidates("Daft Punk", "Discovery", localTracks, raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.95);
}

// ============================================================================
// ADVERSARIAL SUITE 4: ARTIST SIMILARITY GUARDRAIL EDGE CASES
// ============================================================================

TEST_CASE("Consensus Adversarial", "Artist Divergence: Macroblank vs ExileLord with Identical Album and Tracklist") {
    // Hostile scenario: Candidate has exact same album name and exact same tracklist,
    // but the artist diverges completely ("Macroblank" vs "ExileLord").
    // Confidence must be clamped to <= 0.15, strictly rejecting the match.
    std::vector<std::string> tracks = { "Meat Grinder", "Pain", "Vapor" };

    std::vector<MetadataCandidate> raw = {
        { "Discogs", "Macroblank", "Pain Forever", "", "2021", 0, "", 0.0, tracks },
        { "Last.fm", "Macroblank", "Pain Forever", "", "2021", 0, "", 0.0, tracks }
    };

    auto res = AggregateAlbumCandidates("ExileLord", "Pain Forever", tracks, raw);

    ASSERT_TRUE(res.hasConflict);
    ASSERT_LE(res.confidence, 0.15);
}

TEST_CASE("Consensus Adversarial", "Artist Typo: Minor Edit Distance Passes with Matching Tracklist") {
    // Typo in artist name ("Daft Punk" vs "Daft Punkk").
    // High similarity (> 0.85) combined with matching tracklist passes >= 80%.
    std::vector<std::string> tracks = { "One More Time", "Aerodynamic", "Digital Love" };

    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "Daft Punkk", "Discovery", "", "2001", 0, "", 0.0, tracks }
    };

    auto res = AggregateAlbumCandidates("Daft Punk", "Discovery", tracks, raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.80);
}

TEST_CASE("Consensus Adversarial", "Sentinel Artist: TO SORT as Local Artist Strictly Blocked") {
    // When local artist is a directory sentinel like "TO SORT",
    // IsUnknownArtist is true, artistSimilarity is 0.0, and confidence is clamped <= 0.15.
    std::vector<std::string> tracks = { "Track 1", "Track 2", "Track 3" };

    std::vector<MetadataCandidate> raw = {
        { "Discogs", "ExileLord", "Compilation", "", "2020", 0, "", 0.0, tracks }
    };

    auto res = AggregateAlbumCandidates("TO SORT", "Compilation", tracks, raw);

    ASSERT_TRUE(res.hasConflict);
    ASSERT_LE(res.confidence, 0.15);
}

TEST_CASE("Consensus Adversarial", "Sentinel Artist: Various Artists Candidate Without Match Strictly Blocked") {
    // When candidate artist is "Various Artists" and query artist is "ExileLord",
    // IsUnknownArtist("Various Artists") returns true -> artistSimilarity = 0.0 -> confidence <= 0.15.
    std::vector<std::string> tracks = { "Soulless 4", "Soulless 5" };

    std::vector<MetadataCandidate> raw = {
        { "Discogs", "Various Artists", "Guitar Hero Customs", "", "2018", 0, "", 0.0, tracks }
    };

    auto res = AggregateAlbumCandidates("ExileLord", "Guitar Hero Customs", tracks, raw);

    ASSERT_TRUE(res.hasConflict);
    ASSERT_LE(res.confidence, 0.15);
}

TEST_CASE("Consensus Adversarial", "Collaborative Artist: Featuring Divergence Capped") {
    // Local artist is "ZUN". Candidate artist is "COOL&CREATE feat. beatMARIO".
    // Low string similarity (< 0.60) forces clamp to <= 0.15.
    std::vector<std::string> tracks = { "Help me, ERINNNNNN!!" };

    std::vector<MetadataCandidate> raw = {
        { "TouhouDB", "COOL&CREATE feat. beatMARIO", "Flowering Night", "", "2004", 0, "", 0.0, tracks }
    };

    auto res = AggregateAlbumCandidates("ZUN", "Flowering Night", tracks, raw);

    ASSERT_TRUE(res.hasConflict);
    ASSERT_LE(res.confidence, 0.15);
}

TEST_CASE("Consensus Adversarial", "Rival Conflict: Two High-Confidence Conflicting Album Candidates") {
    // When local album tag is empty and local tracks match two distinct albums:
    // MusicBrainz proposes "The Wall" (confidence ~0.95),
    // Discogs proposes "Pulse (Live)" (confidence ~0.91).
    // Both match local tracks with high confidence, but album titles diverge.
    // Difference is 0.04 < 0.15 with rival >= 0.70 -> conflict triggered!
    std::vector<std::string> tracks = { "Comfortably Numb", "Hey You", "Mother" };

    std::vector<MetadataCandidate> raw = {
        { "MusicBrainz", "Pink Floyd", "The Wall", "", "1979", 0, "", 0.95, tracks },
        { "Discogs", "Pink Floyd", "Pulse (Live)", "", "1995", 0, "", 0.91, tracks }
    };

    auto res = AggregateAlbumCandidates("Pink Floyd", "", tracks, raw);

    ASSERT_TRUE(res.hasConflict);
    ASSERT_TRUE(res.conflictReason.find("Conflict between MusicBrainz") != std::string::npos);
    ASSERT_TRUE(res.conflictReason.find("Discogs") != std::string::npos);
}

TEST_CASE("Consensus Adversarial", "Metadata Completeness Tie-Breaking for Equal Confidence") {
    // When two candidates have the exact same confidence score,
    // the one with more complete metadata (year, cover, tracklist) must win.
    std::vector<std::string> tracks = { "Song 1", "Song 2" };

    MetadataCandidate basicCand;
    basicCand.providerName = "ProviderA";
    basicCand.artist = "Artist";
    basicCand.album = "Album";
    basicCand.tracklist = tracks;

    MetadataCandidate completeCand;
    completeCand.providerName = "ProviderA";
    completeCand.artist = "Artist";
    completeCand.album = "Album";
    completeCand.year = "2020";
    completeCand.coverUrl = "https://cover.jpg/art.png";
    completeCand.tracklist = tracks;

    std::vector<MetadataCandidate> raw = { basicCand, completeCand };

    auto res = AggregateAlbumCandidates("Artist", "Album", tracks, raw);

    ASSERT_FALSE(res.hasConflict);
    ASSERT_STR_EQ(res.bestCandidate.year, "2020");
    ASSERT_STR_EQ(res.bestCandidate.coverUrl, "https://cover.jpg/art.png");
}

TEST_CASE("Consensus Adversarial", "Confidence Clamping Invariant") {
    // Extreme confidence inputs must be strictly bounded in [0.0, 1.0].
    std::vector<std::string> tracks = { "Track 1" };

    std::vector<MetadataCandidate> raw = {
        { "ProviderA", "Artist", "Album", "", "", 0, "", 2.50, tracks },
        { "ProviderB", "Artist", "Album", "", "", 0, "", -1.50, tracks }
    };

    auto res = AggregateAlbumCandidates("Artist", "Album", tracks, raw);

    ASSERT_LE(res.confidence, 1.0);
    ASSERT_GE(res.confidence, 0.0);
}
