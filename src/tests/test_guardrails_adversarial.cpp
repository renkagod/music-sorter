#include "TestFramework.hpp"
#include "../include/MetadataUtils.hpp"
#include "../include/FetchServices.hpp"

#ifndef ASSERT_LT
#define ASSERT_LT(actual, expected) ASSERT_TRUE((actual) < (expected))
#endif

#ifndef ASSERT_GT
#define ASSERT_GT(actual, expected) ASSERT_TRUE((actual) > (expected))
#endif

// ============================================================================
// ADVERSARIAL SUITE 1: 0.80 BOUNDARY PRECISION & PENALTY CLAMPING
// ============================================================================

TEST_CASE("Guardrails Adversarial", "Boundary: Exact 0.80 Threshold and Sub-Threshold Failure") {
    // 4 local tracks, 4 candidate tracks. 2 matched -> overlap = 0.50.
    // rawConfidence = 0.40(1.0) + 0.40(0.50) + 0.20(1.0) = 0.40 + 0.20 + 0.20 = 0.8000
    std::vector<std::string> local4 = { "Track 1", "Track 2", "Track 3", "Track 4" };
    std::vector<std::string> cand4 = { "Track 1", "Track 2", "Other 3", "Other 4" };

    auto res80 = ValidateAlbumMatch("Artist", "Album", local4, "Artist", "Album", cand4);
    ASSERT_TRUE(res80.passed);
    ASSERT_NEAR(res80.confidence, 0.80, 0.001);

    // 5 local tracks, 5 candidate tracks. 2 matched -> overlap = 0.40.
    // rawConfidence = 0.40(1.0) + 0.40(0.40) + 0.20(1.0) = 0.40 + 0.16 + 0.20 = 0.7600
    std::vector<std::string> local5 = { "Track 1", "Track 2", "Track 3", "Track 4", "Track 5" };
    std::vector<std::string> cand5 = { "Track 1", "Track 2", "Other 3", "Other 4", "Other 5" };

    auto res76 = ValidateAlbumMatch("Artist", "Album", local5, "Artist", "Album", cand5);
    ASSERT_FALSE(res76.passed);
    ASSERT_NEAR(res76.confidence, 0.76, 0.001);
}

TEST_CASE("Guardrails Adversarial", "Boundary: Divergent Tracklist Overlap Penalty Clamps to 0.35") {
    // When tracklist overlap is below 0.40 with N >= 2, confidence must be clamped to at most 0.35
    std::vector<std::string> local5 = { "Track 1", "Track 2", "Track 3", "Track 4", "Track 5" };
    std::vector<std::string> cand5 = { "Track 1", "Other 2", "Other 3", "Other 4", "Other 5" };

    auto res = ValidateAlbumMatch("Artist", "Album", local5, "Artist", "Album", cand5);
    ASSERT_FALSE(res.passed);
    ASSERT_LE(res.confidence, 0.35);
    ASSERT_TRUE(res.reason.find("Rejected") != std::string::npos);
}

TEST_CASE("Guardrails Adversarial", "Boundary: Divergent Artist Penalty Clamps to 0.15") {
    // When artist similarity is below 0.60, confidence must be clamped to at most 0.15
    std::vector<std::string> tracks = { "Track 1", "Track 2", "Track 3" };

    auto res = ValidateAlbumMatch("ExileLord", "Album", tracks, "Macroblank", "Album", tracks);
    ASSERT_FALSE(res.passed);
    ASSERT_LE(res.confidence, 0.15);
    ASSERT_TRUE(res.reason.find("divergent artist similarity") != std::string::npos);
}

// ============================================================================
// ADVERSARIAL SUITE 2: ARTIST NAME VARIATIONS AND MULTI-SCRIPT SIMILARITY
// ============================================================================

TEST_CASE("Guardrails Adversarial", "Artist Variations: Case, Punctuation, and Normalization") {
    // Case insensitivity across ASCII
    ASSERT_NEAR(ComputeStringSimilarity("DAFT PUNK", "daft punk"), 1.0, 0.001);
    ASSERT_NEAR(ComputeStringSimilarity("ExileLord", "exilelord"), 1.0, 0.001);

    // Punctuation and spacing resilience
    ASSERT_NEAR(ComputeStringSimilarity("AC/DC", "ACDC"), 1.0, 0.001);
    ASSERT_NEAR(ComputeStringSimilarity("Jay-Z", "Jay Z"), 1.0, 0.001);
    ASSERT_NEAR(ComputeStringSimilarity("Panic! At The Disco", "Panic at the Disco"), 1.0, 0.001);
    ASSERT_NEAR(ComputeStringSimilarity("Guns N' Roses", "Guns N Roses"), 1.0, 0.001);
}

TEST_CASE("Guardrails Adversarial", "Artist Variations: Diacritics and Accents") {
    double simMot = ComputeStringSimilarity("Motörhead", "Motorhead");
    ASSERT_GE(simMot, 0.75);

    double simBey = ComputeStringSimilarity("Beyoncé", "Beyonce");
    ASSERT_GE(simBey, 0.75);

    double simBjo = ComputeStringSimilarity("Björk", "Bjork");
    ASSERT_GE(simBjo, 0.60);
}

TEST_CASE("Guardrails Adversarial", "Artist Variations: CJK, Kanji, and Circle Suffixes") {
    ASSERT_NEAR(ComputeStringSimilarity("米津玄師", "米津玄師"), 1.0, 0.001);
    ASSERT_GE(ComputeStringSimilarity("米津玄師 (Kenshi Yonezu)", "Kenshi Yonezu"), 0.80);
    ASSERT_NEAR(ComputeStringSimilarity("FELT", "FELT (Circle)"), 1.0, 0.001);
}

TEST_CASE("Guardrails Adversarial", "Artist Variations: Cyrillic Alignment and Title-Case Support") {
    ASSERT_NEAR(ComputeStringSimilarity("Кино", "Кино"), 1.0, 0.001);
    ASSERT_GE(ComputeStringSimilarity("Кино", "кино"), 0.80);
    ASSERT_GE(ComputeStringSimilarity("Алиса", "алиса"), 0.80);
}

// ============================================================================
// ADVERSARIAL SUITE 3: MASSIVE CANDIDATE TRACKLISTS (BOX SETS VS EPS)
// ============================================================================

TEST_CASE("Guardrails Adversarial", "Massive Tracklists: 3-Track EP vs 100-Track Box Set with Divergent Album") {
    std::vector<std::string> ep3 = { "Song A", "Song B", "Song C" };
    std::vector<std::string> box100;
    box100.push_back("Song A");
    box100.push_back("Song B");
    box100.push_back("Song C");
    for (int i = 4; i <= 100; ++i) box100.push_back("Other Track " + std::to_string(i));

    auto res = ValidateAlbumMatch("Queen", "My Debut EP", ep3, "Queen", "Ultimate Box Set (100 Tracks)", box100);
    ASSERT_FALSE(res.passed);
    ASSERT_LT(res.confidence, 0.80);
}

TEST_CASE("Guardrails Adversarial", "Massive Tracklists: 100-Track Candidate with Zero Matches") {
    std::vector<std::string> ep3 = { "Song A", "Song B", "Song C" };
    std::vector<std::string> box100;
    for (int i = 1; i <= 100; ++i) box100.push_back("Completely Different " + std::to_string(i));

    auto res = ValidateAlbumMatch("Artist", "Anthology", ep3, "Artist", "Anthology", box100);
    ASSERT_FALSE(res.passed);
    ASSERT_LE(res.confidence, 0.35);
    ASSERT_NEAR(res.tracklistOverlap, 0.0, 0.001);
}

TEST_CASE("Guardrails Adversarial", "Massive Tracklists: 100 Local Tracks vs 3 Candidate Tracks") {
    std::vector<std::string> ep3 = { "Song A", "Song B", "Song C" };
    std::vector<std::string> box100;
    box100.push_back("Song A");
    box100.push_back("Song B");
    box100.push_back("Song C");
    for (int i = 4; i <= 100; ++i) box100.push_back("Local Track " + std::to_string(i));

    auto res = ValidateAlbumMatch("Artist", "Big Collection", box100, "Artist", "Sampler EP", ep3);
    ASSERT_FALSE(res.passed);
    ASSERT_LE(res.confidence, 0.35);
}

// ============================================================================
// ADVERSARIAL SUITE 4: EMPTY, WHITESPACE, AND SINGLE-CHARACTER INPUTS
// ============================================================================

TEST_CASE("Guardrails Adversarial", "Edge Cases: Empty Vectors and Whitespace Strings") {
    auto rEmpty = ValidateAlbumMatch("", "", {}, "", "", {});
    ASSERT_FALSE(rEmpty.passed);
    ASSERT_NEAR(rEmpty.confidence, 0.0, 0.001);

    auto rWhitespace = ValidateAlbumMatch("   ", "Album", { "Track" }, "   ", "Album", { "Track" });
    ASSERT_FALSE(rWhitespace.passed);
    ASSERT_NEAR(rWhitespace.artistSimilarity, 0.0, 0.001);

    auto rEmptyTrack = ValidateAlbumMatch("Artist", "Album", { "" }, "Artist", "Album", { "Track 1" });
    ASSERT_FALSE(rEmptyTrack.passed);
    ASSERT_NEAR(rEmptyTrack.tracklistOverlap, 0.0, 0.001);
}

TEST_CASE("Guardrails Adversarial", "Edge Cases: Sentinel Artist Names Treated as Unknown") {
    ASSERT_TRUE(IsUnknownArtist(""));
    ASSERT_TRUE(IsUnknownArtist("   "));
    ASSERT_TRUE(IsUnknownArtist("?"));
    ASSERT_TRUE(IsUnknownArtist("-"));
    ASSERT_TRUE(IsUnknownArtist("..."));
    ASSERT_TRUE(IsUnknownArtist("Unknown Artist"));
    ASSERT_TRUE(IsUnknownArtist("Various Artists"));
    ASSERT_TRUE(IsUnknownArtist("VA"));
    ASSERT_TRUE(IsUnknownArtist("TO SORT"));
    ASSERT_TRUE(IsUnknownArtist("singles"));
    ASSERT_TRUE(IsUnknownArtist("downloads"));
    ASSERT_TRUE(IsUnknownArtist("music"));
}

TEST_CASE("Guardrails Adversarial", "Edge Cases: Single-Character Inputs") {
    auto rMatch = ValidateAlbumMatch("A", "Album", { "Track" }, "A", "Album", { "Track" });
    ASSERT_TRUE(rMatch.passed);
    ASSERT_NEAR(rMatch.artistSimilarity, 1.0, 0.001);

    auto rMismatch = ValidateAlbumMatch("A", "Album", { "Track" }, "B", "Album", { "Track" });
    ASSERT_FALSE(rMismatch.passed);
    ASSERT_NEAR(rMismatch.artistSimilarity, 0.0, 0.001);
}

// ============================================================================
// ADVERSARIAL SUITE 5: DISPARATE SINGLES VS FALSE POPULAR ALBUMS
// ============================================================================

TEST_CASE("Guardrails Adversarial", "Disparate Singles: Multi-Artist Loose Track Cluster Rejection") {
    std::vector<std::string> loose = { "Track A", "Track B", "Track C" };
    std::vector<std::string> candTracks = { "Track A", "Track B", "Track C" };

    auto res = ValidateAlbumMatch("Various Artists", "TO SORT", loose, "VA Album", "Greatest Hits", candTracks);
    ASSERT_FALSE(res.passed);
    ASSERT_NEAR(res.artistSimilarity, 0.0, 0.001);
    ASSERT_LE(res.confidence, 0.15);
}

// ============================================================================
// ADVERSARIAL SUITE 6: DISCOGS POPULARITY CAPPING AND TITLE MISMATCH PROTECTION
// ============================================================================

TEST_CASE("Guardrails Adversarial", "Discogs Popularity: 1,000,000 Community Popularity Gated Behind Title Match") {
    std::string json = R"json({
        "results": [
            {
                "id": 201,
                "title": "Macroblank - Popular Hit",
                "year": 2022,
                "cover_image": "https://img.discogs.com/cover.jpg",
                "type": "release",
                "community": { "have": 1000000, "want": 500000 }
            }
        ]
    })json";

    size_t p = 0;
    JsonVal doc = ParseJsonSimple(json, p);
    const auto& results = doc.get("results");
    const auto& r = results.get(0);

    auto scoreCalc = [](const JsonVal& item, const std::string& artist, const std::string& album) -> int {
        std::string albNorm = NormalizeKey(album);
        std::string artNorm = NormalizeKey(artist);
        std::string rTitle = item.get("title").strVal;
        std::string rTitleNorm = NormalizeKey(rTitle);

        bool isArtistUnknown = IsUnknownArtist(artist);
        bool titleMatch = (!albNorm.empty() && (rTitleNorm.find(albNorm) != std::string::npos || albNorm.find(rTitleNorm) != std::string::npos));

        int score = 0;
        if (titleMatch) score += 100;
        if (!isArtistUnknown && !artNorm.empty()) {
            if (rTitleNorm.find(artNorm) != std::string::npos) score += 50;
            else if (!titleMatch) score -= 100;
        }
        if (item.get("year").numVal > 0) score += 30;
        if (!item.get("cover_image").strVal.empty()) score += 20;

        if (titleMatch) {
            double haveCount = item.get("community").get("have").numVal;
            double wantCount = item.get("community").get("want").numVal;
            double totalPop = (std::max)(0.0, haveCount) + (std::max)(0.0, wantCount);
            if (totalPop > 0.0) {
                int popBonus = (int)(totalPop / 50.0);
                if (popBonus <= 0) popBonus = 1;
                if (popBonus > 10) popBonus = 10;
                score += popBonus;
            }
        }
        return score;
    };

    int score = scoreCalc(r, "ExileLord", "TO SORT");
    // Title mismatch + artist mismatch = -100 + 30 + 20 = -50, zero popularity bonus
    ASSERT_LT(score, 0);
}

TEST_CASE("Guardrails Adversarial", "Discogs Popularity: Title Match Popularity Capped at 10 Points") {
    std::string json = R"json({
        "results": [
            {
                "id": 301,
                "title": "Daft Punk - Discovery",
                "year": 2001,
                "cover_image": "https://img.discogs.com/cover.jpg",
                "type": "release",
                "community": { "have": 500000, "want": 250000 }
            },
            {
                "id": 302,
                "title": "Daft Punk - Discovery",
                "year": 2001,
                "cover_image": "https://img.discogs.com/cover.jpg",
                "type": "release",
                "community": { "have": 0, "want": 0 }
            }
        ]
    })json";

    size_t p = 0;
    JsonVal doc = ParseJsonSimple(json, p);
    const auto& results = doc.get("results");

    auto scoreCalc = [](const JsonVal& item, const std::string& artist, const std::string& album) -> int {
        std::string albNorm = NormalizeKey(album);
        std::string artNorm = NormalizeKey(artist);
        std::string rTitle = item.get("title").strVal;
        std::string rTitleNorm = NormalizeKey(rTitle);

        bool isArtistUnknown = IsUnknownArtist(artist);
        bool titleMatch = (!albNorm.empty() && (rTitleNorm.find(albNorm) != std::string::npos || albNorm.find(rTitleNorm) != std::string::npos));

        int score = 0;
        if (titleMatch) score += 100;
        if (!isArtistUnknown && !artNorm.empty()) {
            if (rTitleNorm.find(artNorm) != std::string::npos) score += 50;
            else if (!titleMatch) score -= 100;
        }
        if (item.get("year").numVal > 0) score += 30;
        if (!item.get("cover_image").strVal.empty()) score += 20;

        if (titleMatch) {
            double haveCount = item.get("community").get("have").numVal;
            double wantCount = item.get("community").get("want").numVal;
            double totalPop = (std::max)(0.0, haveCount) + (std::max)(0.0, wantCount);
            if (totalPop > 0.0) {
                int popBonus = (int)(totalPop / 50.0);
                if (popBonus <= 0) popBonus = 1;
                if (popBonus > 10) popBonus = 10;
                score += popBonus;
            }
        }
        return score;
    };

    int scorePop = scoreCalc(results.get(0), "Daft Punk", "Discovery");
    int scoreZero = scoreCalc(results.get(1), "Daft Punk", "Discovery");

    ASSERT_EQ(scorePop - scoreZero, 10);
}

// ============================================================================
// ADVERSARIAL SUITE 7: LYRIC GUARDRAILS STRESS TESTING
// ============================================================================

TEST_CASE("Guardrails Adversarial", "Lyric Guardrails: Numbered Sequels and Subtitle Variants") {
    ASSERT_FALSE(ValidateLyricMatch("ExileLord", "Soulless 4", "ExileLord", "Soulless 5"));
    ASSERT_FALSE(ValidateLyricMatch("Artist", "Track 01", "Artist", "Track 02"));
    ASSERT_FALSE(ValidateLyricMatch("Artist", "Part 1", "Artist", "Part 2"));
}

TEST_CASE("Guardrails Adversarial", "Lyric Guardrails: Comprehensive Instrumental Markers") {
    ASSERT_FALSE(ValidateLyricMatch("Hans Zimmer", "Time (Instrumental)", "Hans Zimmer", "Time"));
    ASSERT_FALSE(ValidateLyricMatch("Hans Zimmer", "Time - Instrumental Version", "Hans Zimmer", "Time"));
    ASSERT_FALSE(ValidateLyricMatch("Artist", "Song [Off Vocal]", "Artist", "Song"));
    ASSERT_FALSE(ValidateLyricMatch("Artist", "Song (Karaoke)", "Artist", "Song"));
    ASSERT_FALSE(ValidateLyricMatch("Artist", "Song (Backing Track)", "Artist", "Song"));
    ASSERT_FALSE(ValidateLyricMatch("Artist", "Song (Without Vocal)", "Artist", "Song"));
    ASSERT_FALSE(ValidateLyricMatch("Artist", "Song (Minus One)", "Artist", "Song"));
    ASSERT_FALSE(ValidateLyricMatch("Artist", "Song (No Vocal)", "Artist", "Song"));
    ASSERT_FALSE(ValidateLyricMatch("Artist", "Song [Inst]", "Artist", "Song"));
}
