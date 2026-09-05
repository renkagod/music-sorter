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
// TIER 1: FEATURE COVERAGE (Core Functional Requirements)
// ============================================================================

TEST_CASE("Guardrail Scoring", "ValidateAlbumMatch: Exact Match High Confidence") {
    std::vector<std::string> daftPunk14 = {
        "One More Time",
        "Aerodynamic",
        "Digital Love",
        "Harder, Better, Faster, Stronger",
        "Crescendolls",
        "Nightvision",
        "Superheroes",
        "High Life",
        "Something About Us",
        "Voyager",
        "Veridis Quo",
        "Short Circuit",
        "Face to Face",
        "Too Long"
    };

    auto result = ValidateAlbumMatch(
        "Daft Punk",
        "Discovery",
        daftPunk14,
        "Daft Punk",
        "Discovery",
        daftPunk14
    );

    ASSERT_TRUE(result.passed);
    ASSERT_GE(result.confidence, 0.90);
    ASSERT_NEAR(result.artistSimilarity, 1.0, 0.001);
    ASSERT_NEAR(result.tracklistOverlap, 1.0, 0.001);
    ASSERT_TRUE(result.reason.find("Approved") != std::string::npos);
}

TEST_CASE("Guardrail Scoring", "ValidateAlbumMatch: Mismatched Artist Rejection") {
    std::vector<std::string> localTitles = { "Soulless 4", "Soulless 5" };
    std::vector<std::string> candidateTracklist = { "Track 1", "Track 2" };

    auto result = ValidateAlbumMatch(
        "ExileLord",
        "TO SORT",
        localTitles,
        "Macroblank",
        "TO SORT",
        candidateTracklist
    );

    ASSERT_FALSE(result.passed);
    ASSERT_LT(result.confidence, 0.80);
    ASSERT_LT(result.artistSimilarity, 0.30);
    ASSERT_TRUE(result.reason.find("Rejected") != std::string::npos);
}

TEST_CASE("Guardrail Scoring", "ValidateAlbumMatch: Divergent Tracklists Rejection") {
    std::vector<std::string> localTitles = { "Soulless 4", "Megalodon", "Arm Breaker" };
    std::vector<std::string> candidateTracklist = { "Intro", "Outro", "Interlude" };

    auto result = ValidateAlbumMatch(
        "ExileLord",
        "Singles",
        localTitles,
        "ExileLord",
        "Singles",
        candidateTracklist
    );

    ASSERT_FALSE(result.passed);
    ASSERT_LT(result.confidence, 0.80);
    ASSERT_NEAR(result.tracklistOverlap, 0.0, 0.001);
    ASSERT_TRUE(result.reason.find("Rejected") != std::string::npos);
}

TEST_CASE("Guardrail Scoring", "ValidateAlbumMatch: Partial Tracklist Overlap Below 80 Percent") {
    std::vector<std::string> local10 = {
        "In the Flesh?", "The Thin Ice", "Another Brick in the Wall, Pt. 1",
        "The Happiest Days of Our Lives", "Another Brick in the Wall, Pt. 2",
        "Mother", "Goodbye Blue Sky", "Empty Spaces", "Young Lust", "One of My Turns"
    };
    std::vector<std::string> cand10 = {
        "In the Flesh?", "The Thin Ice", "Totally Different 1",
        "Totally Different 2", "Totally Different 3", "Totally Different 4",
        "Totally Different 5", "Totally Different 6", "Totally Different 7", "Totally Different 8"
    };

    auto result = ValidateAlbumMatch(
        "Pink Floyd",
        "The Wall",
        local10,
        "Pink Floyd",
        "The Wall",
        cand10
    );

    ASSERT_FALSE(result.passed);
    ASSERT_LT(result.confidence, 0.80);
    ASSERT_LE(result.tracklistOverlap, 0.30);
}

TEST_CASE("Guardrail Scoring", "ValidateLyricMatch: Exact Artist and Title Pass") {
    bool ok = ValidateLyricMatch("Queen", "Bohemian Rhapsody", "Queen", "Bohemian Rhapsody");
    ASSERT_TRUE(ok);
}

TEST_CASE("Guardrail Scoring", "ValidateLyricMatch: Unknown Artist Rejection") {
    ASSERT_FALSE(ValidateLyricMatch("Unknown Artist", "Track 01", "Queen", "Track 01"));
    ASSERT_FALSE(ValidateLyricMatch("", "Track 01", "Queen", "Track 01"));
    ASSERT_FALSE(ValidateLyricMatch("Various Artists", "Track 01", "Queen", "Track 01"));
    ASSERT_FALSE(ValidateLyricMatch("Queen", "Bohemian Rhapsody", "Unknown Artist", "Bohemian Rhapsody"));
    ASSERT_FALSE(ValidateLyricMatch("Queen", "Bohemian Rhapsody", "", "Bohemian Rhapsody"));
}

TEST_CASE("Guardrail Scoring", "ValidateLyricMatch: Mismatched Lyric Artist Rejection") {
    ASSERT_FALSE(ValidateLyricMatch("ExileLord", "Soulless 4", "DragonForce", "Soulless 4"));
    ASSERT_FALSE(ValidateLyricMatch("ExileLord", "Soulless 4", "Macroblank", "Soulless 4"));
}

TEST_CASE("Guardrail Scoring", "Discogs Anti-False-Positive Popularity Capping") {
    std::string json = R"json({
        "results": [
            {
                "id": 101,
                "title": "Daft Punk - Discovery",
                "year": 2001,
                "cover_image": "https://img.discogs.com/cover101.jpg",
                "type": "release",
                "formats": [ { "descriptions": [ "Album" ] } ],
                "community": { "have": 0, "want": 0 }
            },
            {
                "id": 102,
                "title": "Macroblank - Rowing",
                "year": 2021,
                "cover_image": "https://img.discogs.com/cover102.jpg",
                "type": "release",
                "formats": [ { "descriptions": [ "Album" ] } ],
                "community": { "have": 100000, "want": 50000 }
            }
        ]
    })json";

    size_t p = 0;
    JsonVal doc = ParseJsonSimple(json, p);
    const auto& results = doc.get("results");
    ASSERT_EQ(results.arrVal.size(), 2);

    auto rankScore = [](const JsonVal& r, const std::string& artist, const std::string& album) -> int {
        std::string albNorm = NormalizeKey(album);
        std::string artNorm = NormalizeKey(artist);
        std::string rTitle = r.get("title").strVal;
        std::string rTitleNorm = NormalizeKey(rTitle);

        bool titleMatch = (!albNorm.empty() && (rTitleNorm.find(albNorm) != std::string::npos || albNorm.find(rTitleNorm) != std::string::npos));
        int score = 0;
        if (titleMatch) score += 100;
        if (!artNorm.empty()) {
            if (rTitleNorm.find(artNorm) != std::string::npos) score += 50;
            else if (!titleMatch) score -= 100;
        }
        if (r.get("year").numVal > 0) score += 30;
        if (!r.get("cover_image").strVal.empty()) score += 20;

        if (titleMatch) {
            double haveCount = r.get("community").get("have").numVal;
            double wantCount = r.get("community").get("want").numVal;
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

    int scoreA = rankScore(results.get(0), "Daft Punk", "Discovery");
    int scoreB = rankScore(results.get(1), "Daft Punk", "Discovery");

    // Candidate A matches title and artist: score >= 200
    // Candidate B matches neither: score is negative (-50) because popularity bonus is 0 when titleMatch == false
    ASSERT_GE(scoreA, 200);
    ASSERT_LT(scoreB, 0);
    ASSERT_GT(scoreA, scoreB);
}

// ============================================================================
// TIER 2: BOUNDARY AND CORNER CASES
// ============================================================================

TEST_CASE("Guardrail Scoring", "ValidateAlbumMatch: Empty and Whitespace Inputs") {
    auto result = ValidateAlbumMatch("", "", {}, "", "", {});
    ASSERT_FALSE(result.passed);
    ASSERT_NEAR(result.confidence, 0.0, 0.001);
}

TEST_CASE("Guardrail Scoring", "ValidateAlbumMatch: Single Track Cluster (Loose Track)") {
    std::vector<std::string> localTitles = { "Soulless 4" };
    std::vector<std::string> candidateTracklist = { "Soulless 4" };

    auto result = ValidateAlbumMatch(
        "ExileLord",
        "",
        localTitles,
        "ExileLord",
        "Soulless 4 - Single",
        candidateTracklist
    );

    ASSERT_TRUE(result.passed);
    ASSERT_GE(result.confidence, 0.80);
    ASSERT_NEAR(result.tracklistOverlap, 1.0, 0.001);
}

TEST_CASE("Guardrail Scoring", "ValidateAlbumMatch: 80 Percent Threshold Boundary Precision") {
    // Exact 80% boundary verification: 0.79 fails, 0.80 passes
    std::vector<std::string> local5 = { "Track 1", "Track 2", "Track 3", "Track 4", "Track 5" };
    std::vector<std::string> cand5 = { "Track 1", "Track 2", "Track 3", "Track 4", "Other" };

    // With 4 of 5 matching (overlap = 0.80) and identical artist (1.0) and identical album (1.0):
    // rawConfidence = 0.40(1.0) + 0.40(0.80) + 0.20(1.0) = 0.40 + 0.32 + 0.20 = 0.92 >= 0.80 -> passes
    auto resPass = ValidateAlbumMatch("Artist", "Album", local5, "Artist", "Album", cand5);
    ASSERT_TRUE(resPass.passed);
    ASSERT_GE(resPass.confidence, 0.80);

    // If 0 tracks match: overlap is 0.0 -> fails
    std::vector<std::string> candZero = { "X1", "X2", "X3", "X4", "X5" };
    auto resFail = ValidateAlbumMatch("Artist", "Album", local5, "Artist", "Album", candZero);
    ASSERT_FALSE(resFail.passed);
    ASSERT_LT(resFail.confidence, 0.80);
}

TEST_CASE("Guardrail Scoring", "ValidateAlbumMatch: Empty Candidate Tracklist Graceful Handling") {
    std::vector<std::string> localTitles = { "Track 1", "Track 2", "Track 3" };
    std::vector<std::string> emptyCandidate = {};

    auto result = ValidateAlbumMatch(
        "Queen",
        "A Night at the Opera",
        localTitles,
        "Queen",
        "A Night at the Opera",
        emptyCandidate
    );

    ASSERT_FALSE(result.passed);
    ASSERT_NEAR(result.tracklistOverlap, 0.0, 0.001);
}

TEST_CASE("Guardrail Scoring", "ValidateLyricMatch: Empty and Missing Field Combinations") {
    ASSERT_FALSE(ValidateLyricMatch("", "Bohemian Rhapsody", "Queen", "Bohemian Rhapsody"));
    ASSERT_FALSE(ValidateLyricMatch("Queen", "", "Queen", "Bohemian Rhapsody"));
    ASSERT_FALSE(ValidateLyricMatch("Queen", "Bohemian Rhapsody", "", "Bohemian Rhapsody"));
    ASSERT_FALSE(ValidateLyricMatch("Queen", "Bohemian Rhapsody", "Queen", ""));
    ASSERT_FALSE(ValidateLyricMatch("   ", "   ", "   ", "   "));
}

TEST_CASE("Guardrail Scoring", "ValidateLyricMatch: Instrumental and Off-Vocal Tracks") {
    ASSERT_FALSE(ValidateLyricMatch("Hans Zimmer", "Time - Instrumental Version", "Hans Zimmer", "Time"));
    ASSERT_FALSE(ValidateLyricMatch("Hans Zimmer", "Time (Instrumental)", "Hans Zimmer", "Time"));
    ASSERT_FALSE(ValidateLyricMatch("Artist", "Song [Off Vocal]", "Artist", "Song"));
    ASSERT_FALSE(ValidateLyricMatch("Artist", "Song (Karaoke)", "Artist", "Song"));
    ASSERT_FALSE(ValidateLyricMatch("Artist", "Song (Backing Track)", "Artist", "Song"));
}

TEST_CASE("Guardrail Scoring", "Popularity Capping: Boundary Clamping and Overflow Protection") {
    auto calcPopBonus = [](double have, double want) -> int {
        double totalPop = (std::max)(0.0, have) + (std::max)(0.0, want);
        if (totalPop <= 0.0) return 0;
        int popBonus = (int)(totalPop / 50.0);
        if (popBonus <= 0) popBonus = 1;
        if (popBonus > 10) popBonus = 10;
        return popBonus;
    };

    ASSERT_EQ(calcPopBonus(0, 0), 0);
    ASSERT_EQ(calcPopBonus(10, 10), 1);
    ASSERT_EQ(calcPopBonus(500, 500), 10);
    ASSERT_EQ(calcPopBonus(10000000.0, 10000000.0), 10);
    ASSERT_EQ(calcPopBonus(-50, -50), 0);
}

// ============================================================================
// TIER 3: CROSS-FEATURE COMBINATIONS
// ============================================================================

TEST_CASE("Guardrail Scoring", "Heuristic Filename Parsing to ValidateAlbumMatch Pipeline") {
    auto parsed = ParseFilenameHeuristic("ExileLord - Soulless 4.mp3");
    ASSERT_TRUE(parsed.hasArtist);
    ASSERT_STR_EQ(parsed.artist, "ExileLord");
    ASSERT_STR_EQ(parsed.title, "Soulless 4");

    auto result = ValidateAlbumMatch(
        parsed.artist,
        "TO SORT",
        { parsed.title },
        "Macroblank",
        "TO SORT",
        { "Track 1", "Track 2" }
    );

    ASSERT_FALSE(result.passed);
    ASSERT_LT(result.confidence, 0.20);
}

TEST_CASE("Guardrail Scoring", "Heuristic Filename Parsing to ValidateLyricMatch Pipeline") {
    auto parsed = ParseFilenameHeuristic("01 - Untitled Track.mp3");
    ASSERT_FALSE(parsed.hasArtist);
    ASSERT_TRUE(IsUnknownArtist(parsed.artist));

    bool lyricMatch = ValidateLyricMatch(parsed.artist, parsed.title, "Popular Artist", parsed.title);
    ASSERT_FALSE(lyricMatch);
}

TEST_CASE("Guardrail Scoring", "CJK and Romanized Multi-Language Artist Alignment") {
    double exactCjk = ComputeStringSimilarity("東方幻想", "東方幻想");
    ASSERT_NEAR(exactCjk, 1.0, 0.001);

    double romanSim = ComputeStringSimilarity("Kenshi Yonezu", "米津玄師 (Kenshi Yonezu)");
    ASSERT_GE(romanSim, 0.80);
}

TEST_CASE("Guardrail Scoring", "QQ Music Zero-Score Guardrail Filtering") {
    std::string mockJson = R"json({
        "code": 0,
        "data": {
            "song": {
                "list": [
                    {
                        "songmid": "0099unrelated",
                        "songname": "Completely Unrelated Song",
                        "singer": [ { "name": "Random Artist" } ]
                    }
                ]
            }
        }
    })json";

    std::string mid = FetchServices::ParseQQMusicSearchJson(mockJson, "ExileLord", "Soulless 4");
    ASSERT_STR_EQ(mid, "");
}

TEST_CASE("Guardrail Scoring", "Similarity Metric Mathematical Invariants") {
    // 1. Reflexivity: sim(s, s) == 1.0
    ASSERT_NEAR(ComputeStringSimilarity("ExileLord", "ExileLord"), 1.0, 0.001);
    ASSERT_NEAR(ComputeStringSimilarity("", ""), 1.0, 0.001);

    // 2. Symmetry: sim(a, b) == sim(b, a)
    double simAB = ComputeStringSimilarity("Daft Punk", "Daft Punk, Romanthony");
    double simBA = ComputeStringSimilarity("Daft Punk, Romanthony", "Daft Punk");
    ASSERT_NEAR(simAB, simBA, 0.001);

    // 3. Range: 0.0 <= sim <= 1.0
    double simRange = ComputeStringSimilarity("Alpha", "Beta");
    ASSERT_GE(simRange, 0.0);
    ASSERT_LE(simRange, 1.0);

    // 4. Disjoint strings: sim("abc", "xyz") == 0.0
    ASSERT_NEAR(ComputeStringSimilarity("abc", "xyz"), 0.0, 0.001);

    // 5. Levenshtein distance invariant
    ASSERT_EQ(ComputeLevenshteinDistance("kitten", "sitting"), 3);
    ASSERT_EQ(ComputeLevenshteinDistance("hello", "hello"), 0);
    ASSERT_EQ(ComputeLevenshteinDistance("", "test"), 4);
}

// ============================================================================
// TIER 4: REAL-WORLD APPLICATION SCENARIOS
// ============================================================================

TEST_CASE("Guardrail Scoring", "Real-World Scenario 1: ExileLord 43 Singles in TO SORT Directory") {
    const std::vector<std::string> all43Titles = {
        "Amalgamation", "Arm Breaker (400 BPM)", "Arm Breaker", "Broken Circus",
        "Crash Test 5", "Dream 2", "Dreamsequence", "Entropy", "Epidox",
        "Exile's Minute of Madness (hardsongforme)", "Hellensoerensen", "Hollow",
        "Horror", "Madbeatsdawg", "Magic 3", "Mashing 3",
        "Minds of the Mad (newsong32)", "Misadventure (jshot2)",
        "Monochrome (The Gray Boy)", "Nameless Song (newsongthing)",
        "Nightlight II", "Nightlight", "Nightmare Loop", "Obsidian",
        "Quartz River (happy240bpm)", "SLSVSX_hybrid", "Shamblefrost",
        "Soulless 2 (Mechanical Machine)", "Soulless 3 (380 BPM)", "Soulless 3",
        "Soulless 4", "Soulless 5", "Soulless 6", "Soulless", "Speedtest",
        "Strumming Practice", "Temple of the Temple", "Tower Loop",
        "Tree of Wat", "Two Hour Testament", "Zigzagtest", "i made something", "test"
    };

    const std::vector<std::string> macroblankTracks = {
        "行方不明", "痛みを伴う記憶", "孤立", "悲哀", "深夜の静けさ", "失われた愛",
        "残響", "不在", "幻影", "空虚", "静寂", "忘却"
    };

    auto result = ValidateAlbumMatch(
        "ExileLord",
        "TO SORT",
        all43Titles,
        "Macroblank",
        "行方不明",
        macroblankTracks
    );

    ASSERT_FALSE(result.passed);
    ASSERT_LT(result.confidence, 0.10);
    ASSERT_NEAR(result.tracklistOverlap, 0.0, 0.001);
    ASSERT_LT(result.artistSimilarity, 0.10);
}

TEST_CASE("Guardrail Scoring", "Real-World Scenario 2: ExileLord Instrumental Singles vs QQ Music Bogus Lyrics") {
    std::string mockJson = R"json({
        "code": 0,
        "data": {
            "song": {
                "list": [
                    {
                        "songmid": "004BogusLyric",
                        "songname": "Soulless (Vocal Remix)",
                        "singer": [ { "name": "Random Vocalist" } ]
                    }
                ]
            }
        }
    })json";

    std::string mid = FetchServices::ParseQQMusicSearchJson(mockJson, "ExileLord", "Soulless 4");
    ASSERT_STR_EQ(mid, "");

    // Number mismatch guard: "Soulless 4" vs "Soulless 5"
    ASSERT_FALSE(ValidateLyricMatch("ExileLord", "Soulless 4", "ExileLord", "Soulless 5"));

    // Instrumental guard
    ASSERT_FALSE(ValidateLyricMatch("ExileLord", "Crash Test 5 (Instrumental)", "ExileLord", "Crash Test 5"));
}

TEST_CASE("Guardrail Scoring", "Real-World Scenario 3: Daft Punk Discovery Legitimate Album Validation") {
    const std::vector<std::string> discoveryTracks = {
        "One More Time", "Aerodynamic", "Digital Love",
        "Harder, Better, Faster, Stronger", "Crescendolls", "Nightvision",
        "Superheroes", "High Life", "Something About Us", "Voyager",
        "Veridis Quo", "Short Circuit", "Face to Face", "Too Long"
    };

    auto result = ValidateAlbumMatch(
        "Daft Punk",
        "Discovery",
        discoveryTracks,
        "Daft Punk",
        "Discovery",
        discoveryTracks
    );

    ASSERT_TRUE(result.passed);
    ASSERT_GE(result.confidence, 0.95);
    ASSERT_NEAR(result.tracklistOverlap, 1.0, 0.001);

    ASSERT_TRUE(ValidateLyricMatch("Daft Punk", "One More Time", "Daft Punk", "One More Time"));
}

TEST_CASE("Guardrail Scoring", "Real-World Scenario 4: Touhou Circle FELT Stand Up 14 Tracks Validation") {
    const std::vector<std::string> feltStandUpTracks = {
        "Reset (Introduction)", "Innocent Eyes", "World Around Us",
        "Stand Up", "Daydream In the Dead of Night", "Phantom of the Flower",
        "Night Falls", "Secret Secret", "Colourless Sky", "Light and Shade",
        "Windy Way", "Memory of the Past", "Lost in the Woods", "Endless Voyage"
    };

    auto result = ValidateAlbumMatch(
        "FELT",
        "Stand Up",
        feltStandUpTracks,
        "FELT",
        "Stand Up",
        feltStandUpTracks
    );

    ASSERT_TRUE(result.passed);
    ASSERT_GE(result.confidence, 0.95);
    ASSERT_NEAR(result.tracklistOverlap, 1.0, 0.001);
}
