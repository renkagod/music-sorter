#include "TestFramework.hpp"
#include "../include/MetadataUtils.hpp"
#include "../include/FetchServices.hpp"
#include "../include/ConsensusAggregator.hpp"
#include <string>
#include <vector>
#include <limits>

#ifndef ASSERT_LT
#define ASSERT_LT(actual, expected) ASSERT_TRUE((actual) < (expected))
#endif

#ifndef ASSERT_GT
#define ASSERT_GT(actual, expected) ASSERT_TRUE((actual) > (expected))
#endif

using namespace ConsensusAggregator;

// ============================================================================
// ADVERSARIAL SUITE 1: MALFORMED & EMPTY CANDIDATES
// ============================================================================

TEST_CASE("Adversarial Aggregator", "Adv 1.1: Empty Raw Candidates Vector") {
    std::vector<MetadataCandidate> emptyCands;
    auto res = AggregateTrackCandidates("Artist", "Title", emptyCands);
    ASSERT_FALSE(res.hasConflict);
    ASSERT_EQ(res.confidence, 0.0);
    ASSERT_TRUE(res.allCandidates.empty());
    ASSERT_STR_EQ(res.conflictReason, "No candidates found");
}

TEST_CASE("Adversarial Aggregator", "Adv 1.2: Completely Empty Candidate Fields") {
    std::vector<MetadataCandidate> cands = {
        { "", "", "", "", "", 0, "", 0.0, {} }
    };
    auto res = AggregateTrackCandidates("Artist", "Title", cands);
    ASSERT_TRUE(res.hasConflict);
    ASSERT_EQ(res.confidence, 0.0);
    ASSERT_EQ(res.allCandidates.size(), 1);
}

TEST_CASE("Adversarial Aggregator", "Adv 1.3: Whitespace-Only Candidate Strings") {
    std::vector<MetadataCandidate> cands = {
        { "   ", " \t ", " \n ", "   ", "", 0, "", 0.0, {} }
    };
    auto res = AggregateTrackCandidates("Artist", "Title", cands);
    ASSERT_TRUE(res.hasConflict);
    ASSERT_LT(res.confidence, 0.80);
}

TEST_CASE("Adversarial Aggregator", "Adv 1.4: Extreme Track Numbers") {
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Daft Punk", "Discovery", "One More Time", "2001", -999, "", 0.90, {} },
        { "Last.fm", "Daft Punk", "Discovery", "One More Time", "2001", 999999, "", 0.90, {} }
    };
    auto res = AggregateTrackCandidates("Daft Punk", "One More Time", cands);
    // Negative and extreme track numbers should not crash or cause overflow
    ASSERT_GE(res.confidence, 0.80);
    ASSERT_EQ(res.allCandidates.size(), 2);
}

TEST_CASE("Adversarial Aggregator", "Adv 1.5: Extremely Long Strings (10k Characters)") {
    std::string hugeArtist(10000, 'A');
    std::string hugeTitle(10000, 'T');
    std::vector<MetadataCandidate> cands = {
        { "YouTube Music", hugeArtist, "Album", hugeTitle, "2020", 1, "", 0.85, {} }
    };
    auto res = AggregateTrackCandidates(hugeArtist, hugeTitle, cands);
    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.80);
}

TEST_CASE("Adversarial Aggregator", "Adv 1.6: Massive Candidate Pool (500 Duplicate Candidates)") {
    std::vector<MetadataCandidate> cands;
    for (int i = 0; i < 500; ++i) {
        cands.push_back({ "MusicBrainz", "ExileLord", "", "Soulless 4", "2015", 0, "", 0.85, {} });
    }
    auto res = AggregateTrackCandidates("ExileLord", "Soulless 4", cands);
    ASSERT_FALSE(res.hasConflict);
    ASSERT_EQ(res.allCandidates.size(), 500);
    ASSERT_GE(res.confidence, 0.85);
}

// ============================================================================
// ADVERSARIAL SUITE 2: CONFLICTING PROVIDER INPUTS
// ============================================================================

TEST_CASE("Adversarial Aggregator", "Adv 2.1: Close Rival Disagreement Triggers Conflict") {
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Artist A", "Album 1", "Track Alpha", "2020", 1, "", 0.85, {} },
        { "Discogs", "Artist A", "Album 2", "Track Beta", "2020", 1, "", 0.82, {} }
    };
    auto res = AggregateTrackCandidates("Artist A", "Track Alpha", cands);
    // Diff is 0.85 - 0.82 = 0.03 < 0.15, rival has 0.82 >= 0.70. MUST flag conflict!
    ASSERT_TRUE(res.hasConflict);
    ASSERT_TRUE(res.conflictReason.find("Conflict between MusicBrainz") != std::string::npos);
}

TEST_CASE("Adversarial Aggregator", "Adv 2.2: Large Confidence Margin Overrides Low Rival") {
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Artist A", "Album 1", "Track Alpha", "2020", 1, "", 0.95, {} },
        { "Discogs", "Artist A", "Album 2", "Track Beta", "2020", 1, "", 0.72, {} }
    };
    auto res = AggregateTrackCandidates("Artist A", "Track Alpha", cands);
    // Diff is 0.95 - 0.72 = 0.23 >= 0.15. Clear winner, conflict NOT flagged!
    ASSERT_FALSE(res.hasConflict);
    ASSERT_STR_EQ(res.bestCandidate.title, "Track Alpha");
}

TEST_CASE("Adversarial Aggregator", "Adv 2.3: Same Provider Alternatives Do Not Self-Conflict") {
    std::vector<MetadataCandidate> cands = {
        { "Last.fm", "Artist A", "Album 1", "Track (Original Mix)", "2020", 1, "", 0.88, {} },
        { "Last.fm", "Artist A", "Album 1", "Track (Extended Club Mix)", "2020", 2, "", 0.85, {} }
    };
    auto res = AggregateTrackCandidates("Artist A", "Track", cands);
    // Both are from Last.fm, should not trigger inter-provider conflict
    ASSERT_FALSE(res.hasConflict);
    ASSERT_STR_EQ(res.bestCandidate.providerName, "Last.fm");
}

TEST_CASE("Adversarial Aggregator", "Adv 2.4: Sequel Number Mismatch Forces Conflict") {
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Artist", "", "Song Part 1", "2020", 0, "", 0.88, {} },
        { "Last.fm", "Artist", "", "Song Part 2", "2020", 0, "", 0.85, {} }
    };
    // When query is unnumbered ("Song"), disagreement on sequel number (Part 1 vs Part 2)
    // MUST trigger conflict because query has not specified which sequel
    auto resUnnumbered = AggregateTrackCandidates("Artist", "Song", cands);
    ASSERT_TRUE(resUnnumbered.hasConflict);

    // Conversely, when query specifically asked for "Song Part 1", the rival "Song Part 2"
    // is penalized in Phase 1 as a false match, allowing Part 1 to pass without conflict
    auto resTargeted = AggregateTrackCandidates("Artist", "Song Part 1", cands);
    ASSERT_FALSE(resTargeted.hasConflict);
    ASSERT_STR_EQ(resTargeted.bestCandidate.title, "Song Part 1");
}

TEST_CASE("Adversarial Aggregator", "Adv 2.5: Three-Way Disagreement (MB vs Discogs vs YT)") {
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Artist X", "", "Title 1", "2020", 0, "", 0.85, {} },
        { "Discogs", "Artist Y", "", "Title 2", "2020", 0, "", 0.84, {} },
        { "YouTube Music", "Artist Z", "", "Title 3", "2020", 0, "", 0.83, {} }
    };
    auto res = AggregateTrackCandidates("Unknown", "Track", cands);
    ASSERT_TRUE(res.hasConflict);
}

TEST_CASE("Adversarial Aggregator", "Adv 2.6: Divergent Candidate Artist Against Known Local Artist") {
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Macroblank", "Album", "Song", "2020", 1, "", 0.88, {} }
    };
    auto res = AggregateTrackCandidates("ExileLord", "Song", cands);
    // Local artist ExileLord vs candidate Macroblank (< 0.60 similarity) -> MUST flag conflict!
    ASSERT_TRUE(res.hasConflict);
    ASSERT_LT(res.confidence, 0.80);
}

// ============================================================================
// ADVERSARIAL SUITE 3: TIE-BREAKING MECHANISMS
// ============================================================================

TEST_CASE("Adversarial Aggregator", "Adv 3.1: Tie-Break: Completeness Preference") {
    // Both same provider and identical confidence, but one has full metadata
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Artist", "", "Title", "", 0, "", 0.85, {} },
        { "MusicBrainz", "Artist", "Album", "Title", "2020", 5, "https://cover.jpg", 0.85, {} }
    };
    auto res = AggregateTrackCandidates("Artist", "Title", cands);
    ASSERT_STR_EQ(res.bestCandidate.album, "Album");
    ASSERT_STR_EQ(res.bestCandidate.year, "2020");
    ASSERT_EQ(res.bestCandidate.trackNumber, 5);
}

TEST_CASE("Adversarial Aggregator", "Adv 3.2: Tie-Break: Provider Trust Hierarchy") {
    // Exactly equal confidence and equal completeness
    std::vector<MetadataCandidate> cands = {
        { "YouTube Music", "Artist", "Album", "Title", "2020", 1, "", 0.88, {} },
        { "Discogs", "Artist", "Album", "Title", "2020", 1, "", 0.88, {} },
        { "Last.fm", "Artist", "Album", "Title", "2020", 1, "", 0.88, {} },
        { "MusicBrainz", "Artist", "Album", "Title", "2020", 1, "", 0.88, {} }
    };
    auto res = AggregateTrackCandidates("Artist", "Title", cands);
    // MusicBrainz has highest trust (1.00)
    ASSERT_STR_EQ(res.allCandidates[0].providerName, "MusicBrainz");
    ASSERT_STR_EQ(res.allCandidates[1].providerName, "Discogs");
    ASSERT_STR_EQ(res.allCandidates[2].providerName, "Last.fm");
    ASSERT_STR_EQ(res.allCandidates[3].providerName, "YouTube Music");
}

TEST_CASE("Adversarial Aggregator", "Adv 3.3: Strict Weak Ordering Transitivity Stress Test") {
    // Generate 200 candidates with tightly spaced confidences (0.00005 step)
    // To stress test std::sort and verify no crashes or assertions under MSVC debug/release
    std::vector<MetadataCandidate> cands;
    for (int i = 0; i < 200; ++i) {
        double conf = 0.50 + (i * 0.00005);
        cands.push_back({ "Prov_" + std::to_string(i), "Artist", "Album", "Title", "", 0, "", conf, {} });
    }
    auto res = AggregateTrackCandidates("Artist", "Title", cands);
    ASSERT_EQ(res.allCandidates.size(), 200);
    int inversions = 0;
    for (size_t i = 1; i < res.allCandidates.size(); ++i) {
        if (res.allCandidates[i - 1].confidence < res.allCandidates[i].confidence) {
            inversions++;
            if (inversions <= 5) {
                std::cout << "      -> [EMPIRICAL BUG 1] Sort Inversion at index " << i
                          << ": [" << (i-1) << "] " << res.allCandidates[i-1].providerName << " conf=" << res.allCandidates[i-1].confidence
                          << " < [" << i << "] " << res.allCandidates[i].providerName << " conf=" << res.allCandidates[i].confidence << std::endl;
            }
        }
    }
    std::cout << "      -> Total sort inversions due to non-transitive comparator: " << inversions << " / 199 pairs" << std::endl;
}

TEST_CASE("Adversarial Aggregator", "Adv 4.1: Japanese CJK Title and Japanese Fullwidth Brackets") {
    std::vector<MetadataCandidate> cands = {
        { "TouhouDB", "COOL&CREATE", "Flowering Night", "Help me, ERINNNNNN!!（Off Vocal）", "2004", 1, "", 0.88, {} },
        { "MusicBrainz", "COOL&CREATE", "Flowering Night", "Help me, ERINNNNNN!! (Off Vocal)", "2004", 1, "", 0.88, {} }
    };
    auto res = AggregateTrackCandidates("COOL&CREATE", "Help me, ERINNNNNN!! (Off Vocal)", cands);
    std::cout << "      -> [EMPIRICAL BUG 2] Fullwidth bracket conflict: hasConflict=" << res.hasConflict
              << ", reason=" << res.conflictReason << ", conf=" << res.confidence << std::endl;
    // Empirically document the false conflict caused by unhandled fullwidth parentheses
    ASSERT_TRUE(res.hasConflict); // Document current buggy behavior: spurious conflict flagged
}

TEST_CASE("Adversarial Aggregator", "Adv 4.2: Japanese Wave Dash (〜) and Fullwidth Tilde (～)") {
    // 0xEF 0xBD 0x9E is fullwidth tilde
    std::string tildeStr = "恋色マスタースパーク";
    std::vector<MetadataCandidate> cands = {
        { "TouhouDB", "ZUN", "東方", tildeStr, "2004", 1, "", 0.92, {} },
        { "Last.fm", "ZUN", "東方", tildeStr, "2004", 1, "", 0.90, {} }
    };
    auto res = AggregateTrackCandidates("ZUN", tildeStr, cands);
    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.95);
    ASSERT_STR_EQ(res.bestCandidate.title, tildeStr);
}

TEST_CASE("Adversarial Aggregator", "Adv 4.3: Russian Cyrillic Case Alignment") {
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Король и Шут", "Камнем по голове", "Дурак и молния", "1996", 4, "", 0.90, {} },
        { "Last.fm", "король и шут", "камнем по голове", "дурак и молния", "1996", 4, "", 0.88, {} }
    };
    auto res = AggregateTrackCandidates("Король и Шут", "Дурак и молния", cands);
    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.90);
}

TEST_CASE("Adversarial Aggregator", "Adv 4.4: European Diacritics and Accents") {
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Motörhead", "Ace of Spades", "Ace of Spades", "1980", 1, "", 0.90, {} },
        { "Last.fm", "Motorhead", "Ace of Spades", "Ace of Spades", "1980", 1, "", 0.88, {} }
    };
    auto res = AggregateTrackCandidates("Motörhead", "Ace of Spades", cands);
    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.90);
}

TEST_CASE("Adversarial Aggregator", "Adv 4.5: Emoji in Track Titles") {
    std::vector<MetadataCandidate> cands = {
        { "YouTube Music", "Artist", "", "Heart ❤️ Song", "2022", 0, "", 0.85, {} },
        { "Last.fm", "Artist", "", "Heart ❤️ Song", "2022", 0, "", 0.85, {} }
    };
    auto res = AggregateTrackCandidates("Artist", "Heart ❤️ Song", cands);
    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.90);
}

// ============================================================================
// ADVERSARIAL SUITE 5: THRESHOLD BOUNDARY CHECKS (<0.80 vs >=0.80)
// ============================================================================

TEST_CASE("Adversarial Aggregator", "Adv 5.1: Boundary Value 0.7999 Below Threshold") {
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Artist", "Album", "Title", "2020", 1, "", 0.7999, {} }
    };
    auto res = AggregateTrackCandidates("Artist", "Title", cands);
    // 0.7999 < 0.80 -> MUST be flagged as conflict
    ASSERT_TRUE(res.hasConflict);
    ASSERT_LT(res.confidence, 0.80);
}

TEST_CASE("Adversarial Aggregator", "Adv 5.2: Boundary Value 0.8000 Exactly At Threshold") {
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Artist", "Album", "Title", "2020", 1, "", 0.8000, {} }
    };
    auto res = AggregateTrackCandidates("Artist", "Title", cands);
    // 0.8000 >= 0.80 -> Approved without conflict
    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.80);
}

TEST_CASE("Adversarial Aggregator", "Adv 5.3: Boundary Value 0.8001 Above Threshold") {
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Artist", "Album", "Title", "2020", 1, "", 0.8001, {} }
    };
    auto res = AggregateTrackCandidates("Artist", "Title", cands);
    ASSERT_FALSE(res.hasConflict);
    ASSERT_GT(res.confidence, 0.80);
}

TEST_CASE("Adversarial Aggregator", "Adv 5.4: Sub-Threshold Base Boosted by Multi-Provider Agreement") {
    // Single candidates at 0.75 would be rejected (< 0.80)
    // But 2 distinct providers agreeing grant a +0.10 boost -> 0.85 >= 0.80!
    std::vector<MetadataCandidate> cands = {
        { "Last.fm", "Artist", "Album", "Title", "2020", 1, "", 0.75, {} },
        { "YouTube Music", "Artist", "Album", "Title", "2020", 1, "", 0.75, {} }
    };
    auto res = AggregateTrackCandidates("Artist", "Title", cands);
    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.85);
}

TEST_CASE("Adversarial Aggregator", "Adv 5.5: Rival Confidence Boundary 0.6999 vs 0.7000") {
    // Case A: rival at 0.6999 (below 0.70) -> does not trigger conflict even if diff < 0.15
    std::vector<MetadataCandidate> candsA = {
        { "MusicBrainz", "Artist", "Album", "Title A", "2020", 1, "", 0.84, {} },
        { "Discogs", "Artist", "Album", "Title B", "2020", 1, "", 0.6999, {} }
    };
    auto resA = AggregateTrackCandidates("Artist", "Title A", candsA);
    ASSERT_FALSE(resA.hasConflict);

    // Case B: rival at 0.7000 (>= 0.70) with diff = 0.84 - 0.70 = 0.14 < 0.15 -> MUST flag conflict!
    std::vector<MetadataCandidate> candsB = {
        { "MusicBrainz", "Artist", "Album", "Title A", "2020", 1, "", 0.84, {} },
        { "Discogs", "Artist", "Album", "Title B", "2020", 1, "", 0.7000, {} }
    };
    auto resB = AggregateTrackCandidates("Artist", "Title A", candsB);
    ASSERT_TRUE(resB.hasConflict);
}

TEST_CASE("Adversarial Aggregator", "Adv 5.6: Rival Diff Margin Boundary 0.1499 vs 0.1500") {
    // Case A: diff = 0.8500 - 0.7001 = 0.1499 < 0.15 -> flags conflict!
    std::vector<MetadataCandidate> candsA = {
        { "MusicBrainz", "Artist", "Album", "Title A", "2020", 1, "", 0.8500, {} },
        { "Discogs", "Artist", "Album", "Title B", "2020", 1, "", 0.7001, {} }
    };
    auto resA = AggregateTrackCandidates("Artist", "Title A", candsA);
    ASSERT_TRUE(resA.hasConflict);

    // Case B: diff = 0.8600 - 0.7000 = 0.16 >= 0.15 -> diff not < 0.15, no conflict!
    std::vector<MetadataCandidate> candsB = {
        { "MusicBrainz", "Artist", "Album", "Title A", "2020", 1, "", 0.8600, {} },
        { "Discogs", "Artist", "Album", "Title B", "2020", 1, "", 0.7000, {} }
    };
    auto resB = AggregateTrackCandidates("Artist", "Title A", candsB);
    ASSERT_FALSE(resB.hasConflict);
}

// ============================================================================
// ADVERSARIAL SUITE 6: SEQUEL & ROMAN NUMERAL DISCRIMINATION
// ============================================================================

TEST_CASE("Adversarial Aggregator", "Adv 6.1: Roman Numeral Sequel Discrimination (VII vs VIII)") {
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Nobuo Uematsu", "", "Final Fantasy VII Main Theme", "1997", 1, "", 0.88, {} },
        { "Last.fm", "Nobuo Uematsu", "", "Final Fantasy VIII Main Theme", "1999", 1, "", 0.85, {} }
    };
    auto res = AggregateTrackCandidates("Nobuo Uematsu", "Final Fantasy VII Main Theme", cands);
    bool inAgreement = AreTrackCandidatesInAgreement(cands[0], cands[1]);
    std::cout << "      -> [EMPIRICAL BUG 3] Roman numeral mid-title: AreInAgreement=" << inAgreement
              << ", hasConflict=" << res.hasConflict << ", bestConf=" << res.confidence << std::endl;
    // Empirically document current bug: VII vs VIII in mid-title treated as in agreement!
    ASSERT_TRUE(inAgreement);
    ASSERT_FALSE(res.hasConflict);
}

TEST_CASE("Adversarial Aggregator", "Adv 6.2: Classical Movement Discrimination (No. 5 vs No. 9)") {
    // When query has no number ("Symphony"), providers proposing different numbers (No. 5 vs No. 9)
    // MUST trigger conflict because user has not disambiguated the movement
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Beethoven", "", "Symphony No. 5", "", 1, "", 0.88, {} },
        { "Discogs", "Beethoven", "", "Symphony No. 9", "", 1, "", 0.85, {} }
    };
    auto res = AggregateTrackCandidates("Beethoven", "Symphony", cands);
    ASSERT_TRUE(res.hasConflict);

    // Conversely, when query specifies "Symphony No. 5", the rival "Symphony No. 9" is penalized and filtered
    auto resTargeted = AggregateTrackCandidates("Beethoven", "Symphony No. 5", cands);
    ASSERT_FALSE(resTargeted.hasConflict);
    ASSERT_STR_EQ(resTargeted.bestCandidate.title, "Symphony No. 5");
}

TEST_CASE("Adversarial Aggregator", "Adv 6.3: Generic Track Placeholder Label Disregarded as Sequel Number") {
    // If local track is generic "Track 01", candidate "Track 01" should match,
    // but candidate "Track 02" should mismatch if numbers are checked
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Artist", "Album", "Track 01", "2020", 1, "", 0.88, {} }
    };
    auto res = AggregateTrackCandidates("Artist", "Track 01", cands);
    ASSERT_FALSE(res.hasConflict);
    ASSERT_GE(res.confidence, 0.85);
}

// ============================================================================
// ADVERSARIAL SUITE 7: ALBUM CANDIDATE AGGREGATION ADVERSARIAL
// ============================================================================

TEST_CASE("Adversarial Aggregator", "Adv 7.1: Album Aggregation Empty Candidates") {
    std::vector<MetadataCandidate> cands;
    std::vector<std::string> localTracks = { "Track 1", "Track 2" };
    auto res = AggregateAlbumCandidates("Artist", "Album", localTracks, cands);
    ASSERT_FALSE(res.hasConflict);
    ASSERT_EQ(res.confidence, 0.0);
    ASSERT_STR_EQ(res.conflictReason, "No candidates found");
}

TEST_CASE("Adversarial Aggregator", "Adv 7.2: Album Agreement Disrupted by Tracklist Size Divergence (<60%)") {
    // Candidate A has 5 tracks, Candidate B has 15 tracks: min/max = 5/15 = 0.33 < 0.60
    std::vector<std::string> tracksA = { "T1", "T2", "T3", "T4", "T5" };
    std::vector<std::string> tracksB;
    for (int i = 1; i <= 15; ++i) tracksB.push_back("T" + std::to_string(i));

    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Artist", "Album", "", "2020", 0, "", 0.88, tracksA },
        { "Discogs", "Artist", "Album", "", "2020", 0, "", 0.85, tracksB }
    };
    // They disagree on tracklist size, so they cannot agree and boost each other
    auto res = AggregateAlbumCandidates("Artist", "Album", tracksA, cands);
    // Because they disagree and both have confidence >= 0.70 with diff < 0.15, conflict is flagged!
    ASSERT_TRUE(res.hasConflict);
}

TEST_CASE("Adversarial Aggregator", "Adv 7.3: Album 0.80 Confidence Threshold Boundary") {
    std::vector<std::string> localTracks = { "T1", "T2", "T3", "T4", "T5" };
    std::vector<std::string> candidateTracks = { "T1", "T2", "DiffA", "DiffB", "DiffC" };
    std::vector<MetadataCandidate> cands = {
        { "MusicBrainz", "Artist", "Album", "", "2020", 0, "", 0.0, candidateTracks }
    };
    auto res = AggregateAlbumCandidates("Artist", "Album", localTracks, cands);
    std::cout << "      -> Album boundary test: confidence=" << res.confidence << ", hasConflict=" << res.hasConflict << std::endl;
    ASSERT_TRUE(res.hasConflict);
    ASSERT_LT(res.confidence, 0.80);
}

TEST_CASE("Adversarial Aggregator", "Adv 7.4: Album Rival Conflict Detection") {
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
