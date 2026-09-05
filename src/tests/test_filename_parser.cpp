#include "TestFramework.hpp"
#include "../include/MetadataUtils.hpp"

// ============================================================================
// TIER 1: FEATURE COVERAGE (Core Pattern Recognition)
// ============================================================================

TEST_CASE("Filename Heuristic Parser", "Tier 1: Basic Artist - Title Pattern") {
    // Standard Artist - Title with extension
    auto res1 = ParseFilenameHeuristic("Pink Floyd - Comfortably Numb.mp3");
    ASSERT_TRUE(res1.hasArtist);
    ASSERT_STR_EQ(res1.artist, "Pink Floyd");
    ASSERT_STR_EQ(res1.title, "Comfortably Numb");
    ASSERT_FALSE(res1.hasAlbum);
    ASSERT_FALSE(res1.hasTrackNumber);
    ASSERT_EQ(res1.trackNumber, 0);

    // FLAC file with multiple words in artist and title
    auto res2 = ParseFilenameHeuristic("Queen - Bohemian Rhapsody.flac");
    ASSERT_TRUE(res2.hasArtist);
    ASSERT_STR_EQ(res2.artist, "Queen");
    ASSERT_STR_EQ(res2.title, "Bohemian Rhapsody");
    ASSERT_FALSE(res2.hasAlbum);
    ASSERT_FALSE(res2.hasTrackNumber);

    // Input without extension (filename stem)
    auto res3 = ParseFilenameHeuristic("ExileLord - Soulless 4");
    ASSERT_TRUE(res3.hasArtist);
    ASSERT_STR_EQ(res3.artist, "ExileLord");
    ASSERT_STR_EQ(res3.title, "Soulless 4");
    ASSERT_FALSE(res3.hasAlbum);
    ASSERT_FALSE(res3.hasTrackNumber);
}

TEST_CASE("Filename Heuristic Parser", "Tier 1: Track - Artist - Title Pattern") {
    // 02 - Artist - Title format
    auto res1 = ParseFilenameHeuristic("02 - ExileLord - Soulless 5.flac");
    ASSERT_TRUE(res1.hasTrackNumber);
    ASSERT_EQ(res1.trackNumber, 2);
    ASSERT_TRUE(res1.hasArtist);
    ASSERT_STR_EQ(res1.artist, "ExileLord");
    ASSERT_STR_EQ(res1.title, "Soulless 5");
    ASSERT_FALSE(res1.hasAlbum);

    // 01 - Artist - Title format
    auto res2 = ParseFilenameHeuristic("01 - Daft Punk - One More Time.mp3");
    ASSERT_TRUE(res2.hasTrackNumber);
    ASSERT_EQ(res2.trackNumber, 1);
    ASSERT_TRUE(res2.hasArtist);
    ASSERT_STR_EQ(res2.artist, "Daft Punk");
    ASSERT_STR_EQ(res2.title, "One More Time");

    // Double-digit track number
    auto res3 = ParseFilenameHeuristic("12 - Led Zeppelin - Stairway to Heaven.mp3");
    ASSERT_TRUE(res3.hasTrackNumber);
    ASSERT_EQ(res3.trackNumber, 12);
    ASSERT_TRUE(res3.hasArtist);
    ASSERT_STR_EQ(res3.artist, "Led Zeppelin");
    ASSERT_STR_EQ(res3.title, "Stairway to Heaven");
}

TEST_CASE("Filename Heuristic Parser", "Tier 1: Artist - Album - Track - Title Pattern") {
    // 4-token standard album track pattern
    auto res1 = ParseFilenameHeuristic("Pink Floyd - The Dark Side of the Moon - 04 - Time.mp3");
    ASSERT_TRUE(res1.hasArtist);
    ASSERT_STR_EQ(res1.artist, "Pink Floyd");
    ASSERT_TRUE(res1.hasAlbum);
    ASSERT_STR_EQ(res1.album, "The Dark Side of the Moon");
    ASSERT_TRUE(res1.hasTrackNumber);
    ASSERT_EQ(res1.trackNumber, 4);
    ASSERT_STR_EQ(res1.title, "Time");

    auto res2 = ParseFilenameHeuristic("Led Zeppelin - Mothership - 01 - Good Times Bad Times.flac");
    ASSERT_TRUE(res2.hasArtist);
    ASSERT_STR_EQ(res2.artist, "Led Zeppelin");
    ASSERT_TRUE(res2.hasAlbum);
    ASSERT_STR_EQ(res2.album, "Mothership");
    ASSERT_TRUE(res2.hasTrackNumber);
    ASSERT_EQ(res2.trackNumber, 1);
    ASSERT_STR_EQ(res2.title, "Good Times Bad Times");
}

TEST_CASE("Filename Heuristic Parser", "Tier 1: Numbered Prefixes (01. and 01 formats)") {
    // Dot prefix with Artist - Title: "01. Queen - Bohemian Rhapsody.mp3"
    auto res1 = ParseFilenameHeuristic("01. Queen - Bohemian Rhapsody.mp3");
    ASSERT_TRUE(res1.hasTrackNumber);
    ASSERT_EQ(res1.trackNumber, 1);
    ASSERT_TRUE(res1.hasArtist);
    ASSERT_STR_EQ(res1.artist, "Queen");
    ASSERT_STR_EQ(res1.title, "Bohemian Rhapsody");

    // Numbered track without artist: "01 Title.mp3"
    auto res2 = ParseFilenameHeuristic("01 Title Only.mp3");
    ASSERT_TRUE(res2.hasTrackNumber);
    ASSERT_EQ(res2.trackNumber, 1);
    ASSERT_FALSE(res2.hasArtist);
    ASSERT_STR_EQ(res2.title, "Title Only");

    // Dot prefix without artist: "04. Track Without Artist.flac"
    auto res3 = ParseFilenameHeuristic("04. Track Without Artist.flac");
    ASSERT_TRUE(res3.hasTrackNumber);
    ASSERT_EQ(res3.trackNumber, 4);
    ASSERT_FALSE(res3.hasArtist);
    ASSERT_STR_EQ(res3.title, "Track Without Artist");

    // Disc-track prefix: "1-02 Artist - Title.mp3"
    auto res4 = ParseFilenameHeuristic("1-02 Queen - Killer Queen.mp3");
    ASSERT_TRUE(res4.hasTrackNumber);
    ASSERT_EQ(res4.trackNumber, 2);
    ASSERT_TRUE(res4.hasArtist);
    ASSERT_STR_EQ(res4.artist, "Queen");
    ASSERT_STR_EQ(res4.title, "Killer Queen");
}

TEST_CASE("Filename Heuristic Parser", "Tier 1: Bracketed Circle and Artist Formats") {
    // Bracketed circle: [FELT] Rendezvous.mp3
    auto res1 = ParseFilenameHeuristic("[FELT] Rendezvous.mp3");
    ASSERT_TRUE(res1.hasArtist);
    ASSERT_STR_EQ(res1.artist, "FELT");
    ASSERT_STR_EQ(res1.title, "Rendezvous");

    // Bracketed circle with Japanese title
    auto res2 = ParseFilenameHeuristic("[EastNewSound] 緋色月下、狂咲ノ絶.flac");
    ASSERT_TRUE(res2.hasArtist);
    ASSERT_STR_EQ(res2.artist, "EastNewSound");
    ASSERT_STR_EQ(res2.title, "緋色月下、狂咲ノ絶");

    // Bracketed circle followed by Artist - Title
    auto res3 = ParseFilenameHeuristic("[Alstroemeria Records] nomico - Bad Apple!!.mp3");
    ASSERT_TRUE(res3.hasArtist);
    ASSERT_STR_EQ(res3.artist, "nomico");
    ASSERT_STR_EQ(res3.title, "Bad Apple!!");

    // Year bracket followed by bracketed circle
    auto res4 = ParseFilenameHeuristic("(2018) [IOSYS] Cirno's Perfect Math Class.mp3");
    ASSERT_TRUE(res4.hasArtist);
    ASSERT_STR_EQ(res4.artist, "IOSYS");
    ASSERT_STR_EQ(res4.title, "Cirno's Perfect Math Class");
}

// ============================================================================
// TIER 2: BOUNDARY & CORNER CASES (Robustness & Sanitization)
// ============================================================================

TEST_CASE("Filename Heuristic Parser", "Tier 2: Empty and Whitespace Inputs") {
    auto res1 = ParseFilenameHeuristic("");
    ASSERT_FALSE(res1.hasArtist);
    ASSERT_FALSE(res1.hasAlbum);
    ASSERT_FALSE(res1.hasTrackNumber);
    ASSERT_STR_EQ(res1.artist, "");
    ASSERT_STR_EQ(res1.title, "");
    ASSERT_EQ(res1.trackNumber, 0);

    auto res2 = ParseFilenameHeuristic("   ");
    ASSERT_FALSE(res2.hasArtist);
    ASSERT_FALSE(res2.hasAlbum);
    ASSERT_FALSE(res2.hasTrackNumber);
    ASSERT_STR_EQ(res2.title, "");

    auto res3 = ParseFilenameHeuristic("\t  \r\n");
    ASSERT_FALSE(res3.hasArtist);
    ASSERT_FALSE(res3.hasTrackNumber);
}

TEST_CASE("Filename Heuristic Parser", "Tier 2: Delimiter-Free Filenames") {
    auto res1 = ParseFilenameHeuristic("Megalodon.mp3");
    ASSERT_FALSE(res1.hasArtist);
    ASSERT_FALSE(res1.hasAlbum);
    ASSERT_FALSE(res1.hasTrackNumber);
    ASSERT_STR_EQ(res1.title, "Megalodon");

    auto res2 = ParseFilenameHeuristic("Amalgamation");
    ASSERT_FALSE(res2.hasArtist);
    ASSERT_STR_EQ(res2.title, "Amalgamation");

    auto res3 = ParseFilenameHeuristic("Untitled Instrumental Track.wav");
    ASSERT_FALSE(res3.hasArtist);
    ASSERT_STR_EQ(res3.title, "Untitled Instrumental Track");
}

TEST_CASE("Filename Heuristic Parser", "Tier 2: Dashes Inside Titles and Subtitles") {
    // 3 tokens where tokens 1 and 2 are non-numeric: title retains subtitle
    auto res1 = ParseFilenameHeuristic("ExileLord - Soulless 4 - The Final Chapter.mp3");
    ASSERT_TRUE(res1.hasArtist);
    ASSERT_STR_EQ(res1.artist, "ExileLord");
    ASSERT_STR_EQ(res1.title, "Soulless 4 - The Final Chapter");
    ASSERT_FALSE(res1.hasAlbum);

    auto res2 = ParseFilenameHeuristic("Hans Zimmer - Time - Instrumental Version.flac");
    ASSERT_TRUE(res2.hasArtist);
    ASSERT_STR_EQ(res2.artist, "Hans Zimmer");
    ASSERT_STR_EQ(res2.title, "Time - Instrumental Version");

    auto res3 = ParseFilenameHeuristic("Daft Punk - Aerodynamic - Daft Crew Remix.mp3");
    ASSERT_TRUE(res3.hasArtist);
    ASSERT_STR_EQ(res3.artist, "Daft Punk");
    ASSERT_STR_EQ(res3.title, "Aerodynamic - Daft Crew Remix");
}

TEST_CASE("Filename Heuristic Parser", "Tier 2: Numbers in Artist and Title Names") {
    // Blink-182: hyphen without spaces; "182" must NOT be extracted as track number
    auto res1 = ParseFilenameHeuristic("Blink-182 - All The Small Things.mp3");
    ASSERT_TRUE(res1.hasArtist);
    ASSERT_STR_EQ(res1.artist, "Blink-182");
    ASSERT_STR_EQ(res1.title, "All The Small Things");
    ASSERT_FALSE(res1.hasTrackNumber);
    ASSERT_EQ(res1.trackNumber, 0);

    // Sum 41: number in band name
    auto res2 = ParseFilenameHeuristic("Sum 41 - Fat Lip.mp3");
    ASSERT_TRUE(res2.hasArtist);
    ASSERT_STR_EQ(res2.artist, "Sum 41");
    ASSERT_STR_EQ(res2.title, "Fat Lip");
    ASSERT_FALSE(res2.hasTrackNumber);

    // 50 Cent: band name begins with digits without leading zero or delimiter
    auto res3 = ParseFilenameHeuristic("50 Cent - In Da Club.mp3");
    ASSERT_TRUE(res3.hasArtist);
    ASSERT_STR_EQ(res3.artist, "50 Cent");
    ASSERT_STR_EQ(res3.title, "In Da Club");
    ASSERT_FALSE(res3.hasTrackNumber);

    // 3 Doors Down
    auto res4 = ParseFilenameHeuristic("3 Doors Down - Kryptonite.mp3");
    ASSERT_TRUE(res4.hasArtist);
    ASSERT_STR_EQ(res4.artist, "3 Doors Down");
    ASSERT_STR_EQ(res4.title, "Kryptonite");

    // The 1975
    auto res5 = ParseFilenameHeuristic("The 1975 - Somebody Else.mp3");
    ASSERT_TRUE(res5.hasArtist);
    ASSERT_STR_EQ(res5.artist, "The 1975");
    ASSERT_STR_EQ(res5.title, "Somebody Else");

    // Numbered track in title: "Crash Test 5"
    auto res6 = ParseFilenameHeuristic("ExileLord - Crash Test 5.mp3");
    ASSERT_TRUE(res6.hasArtist);
    ASSERT_STR_EQ(res6.artist, "ExileLord");
    ASSERT_STR_EQ(res6.title, "Crash Test 5");
    ASSERT_FALSE(res6.hasTrackNumber);
}

TEST_CASE("Filename Heuristic Parser", "Tier 2: Multiple Dots and Special Punctuation") {
    // Dot in artist name ("Mr. Big") with leading track number
    auto res1 = ParseFilenameHeuristic("01. Mr. Big - To Be With You.mp3");
    ASSERT_TRUE(res1.hasTrackNumber);
    ASSERT_EQ(res1.trackNumber, 1);
    ASSERT_TRUE(res1.hasArtist);
    ASSERT_STR_EQ(res1.artist, "Mr. Big");
    ASSERT_STR_EQ(res1.title, "To Be With You");

    // Dots in artist and title ("Dr. Dre - Still D.R.E.")
    auto res2 = ParseFilenameHeuristic("Dr. Dre - Still D.R.E..flac");
    ASSERT_TRUE(res2.hasArtist);
    ASSERT_STR_EQ(res2.artist, "Dr. Dre");
    ASSERT_STR_EQ(res2.title, "Still D.R.E.");

    // Decimal number in song title: "Soulless 4.3"
    auto res3 = ParseFilenameHeuristic("ExileLord - Soulless 4.3.mp3");
    ASSERT_TRUE(res3.hasArtist);
    ASSERT_STR_EQ(res3.artist, "ExileLord");
    ASSERT_STR_EQ(res3.title, "Soulless 4.3");

    // Multiple dots inside title words
    auto res4 = ParseFilenameHeuristic("Artist - Track.Name.With.Dots.mp3");
    ASSERT_TRUE(res4.hasArtist);
    ASSERT_STR_EQ(res4.artist, "Artist");
    ASSERT_STR_EQ(res4.title, "Track.Name.With.Dots");
}

TEST_CASE("Filename Heuristic Parser", "Tier 2: Whitespace and Alternate Delimiters") {
    // Excessive spaces around separator
    auto res1 = ParseFilenameHeuristic("  ExileLord   -   Soulless 4  .mp3");
    ASSERT_TRUE(res1.hasArtist);
    ASSERT_STR_EQ(res1.artist, "ExileLord");
    ASSERT_STR_EQ(res1.title, "Soulless 4");

    // Underscore delimiter: ExileLord_-_Soulless_4.mp3
    auto res2 = ParseFilenameHeuristic("ExileLord_-_Soulless_4.mp3");
    ASSERT_TRUE(res2.hasArtist);
    ASSERT_STR_EQ(res2.artist, "ExileLord");
    ASSERT_STR_EQ(res2.title, "Soulless 4");

    // Double hyphen separator: ExileLord -- Soulless 4.mp3
    auto res3 = ParseFilenameHeuristic("ExileLord -- Soulless 4.mp3");
    ASSERT_TRUE(res3.hasArtist);
    ASSERT_STR_EQ(res3.artist, "ExileLord");
    ASSERT_STR_EQ(res3.title, "Soulless 4");

    // Unicode en-dash delimiter (U+2013)
    auto res4 = ParseFilenameHeuristic("ExileLord \xE2\x80\x93 Soulless 4.mp3");
    ASSERT_TRUE(res4.hasArtist);
    ASSERT_STR_EQ(res4.artist, "ExileLord");
    ASSERT_STR_EQ(res4.title, "Soulless 4");

    // Unicode em-dash delimiter (U+2014)
    auto res5 = ParseFilenameHeuristic("ExileLord \xE2\x80\x94 Soulless 4.mp3");
    ASSERT_TRUE(res5.hasArtist);
    ASSERT_STR_EQ(res5.artist, "ExileLord");
    ASSERT_STR_EQ(res5.title, "Soulless 4");
}

TEST_CASE("Filename Heuristic Parser", "Tier 2: Unicode, Kanji and Kana Characters") {
    // UTF-8 Kanji / Kana: 東方幻想 - 砕月.flac
    auto res1 = ParseFilenameHeuristic("東方幻想 - 砕月.flac");
    ASSERT_TRUE(res1.hasArtist);
    ASSERT_STR_EQ(res1.artist, "東方幻想");
    ASSERT_STR_EQ(res1.title, "砕月");

    // Numbered track with Japanese artist: 01. 米津玄師 - Lemon.mp3
    auto res2 = ParseFilenameHeuristic("01. 米津玄師 - Lemon.mp3");
    ASSERT_TRUE(res2.hasTrackNumber);
    ASSERT_EQ(res2.trackNumber, 1);
    ASSERT_TRUE(res2.hasArtist);
    ASSERT_STR_EQ(res2.artist, "米津玄師");
    ASSERT_STR_EQ(res2.title, "Lemon");

    // Bracketed circle with Japanese title: [凋叶棕] 騙 - Katari.mp3
    auto res3 = ParseFilenameHeuristic("[凋叶棕] 騙 - Katari.mp3");
    ASSERT_TRUE(res3.hasArtist);
    ASSERT_STR_EQ(res3.artist, "凋叶棕");
    ASSERT_STR_EQ(res3.title, "騙 - Katari");
}

// ============================================================================
// TIER 3: CROSS-FEATURE INTERACTION (Clean Field Isolation & Invariants)
// ============================================================================

TEST_CASE("Filename Heuristic Parser", "Tier 3: Clean Prefix Stripping and Field Isolation") {
    // Acceptance criteria check: title must never retain artist prefix or delimiter
    auto info1 = ParseFilenameHeuristic("ExileLord - Soulless 4.mp3");
    ASSERT_STR_EQ(info1.artist, "ExileLord");
    ASSERT_STR_EQ(info1.title, "Soulless 4");
    ASSERT_TRUE(info1.title.find("ExileLord") == std::string::npos);
    ASSERT_TRUE(info1.title.find(" - ") == std::string::npos);
    ASSERT_FALSE(info1.title.rfind("ExileLord", 0) == 0);
    ASSERT_FALSE(info1.title.rfind("-", 0) == 0);
    ASSERT_FALSE(info1.title.rfind(".", 0) == 0);

    // Track prefix and artist prefix stripped cleanly from title
    auto info2 = ParseFilenameHeuristic("01 - Pink Floyd - Time.mp3");
    ASSERT_EQ(info2.trackNumber, 1);
    ASSERT_STR_EQ(info2.artist, "Pink Floyd");
    ASSERT_STR_EQ(info2.title, "Time");
    ASSERT_TRUE(info2.title.find("01") == std::string::npos);
    ASSERT_TRUE(info2.title.find("Pink Floyd") == std::string::npos);
    ASSERT_FALSE(info2.title.rfind("-", 0) == 0);

    // Numbered dot prefix stripped cleanly
    auto info3 = ParseFilenameHeuristic("05. Queen - Killer Queen.mp3");
    ASSERT_EQ(info3.trackNumber, 5);
    ASSERT_STR_EQ(info3.artist, "Queen");
    ASSERT_STR_EQ(info3.title, "Killer Queen");
    ASSERT_TRUE(info3.title.find("05") == std::string::npos);
    ASSERT_FALSE(info3.title.rfind(".", 0) == 0);
}

TEST_CASE("Filename Heuristic Parser", "Tier 3: Flag Consistency and Track Number Disambiguation") {
    // Year bracket must NOT be interpreted as track number
    auto res1 = ParseFilenameHeuristic("(2020) Artist - Title.mp3");
    ASSERT_TRUE(res1.hasArtist);
    ASSERT_STR_EQ(res1.artist, "Artist");
    ASSERT_STR_EQ(res1.title, "Title");
    ASSERT_FALSE(res1.hasTrackNumber);
    ASSERT_EQ(res1.trackNumber, 0);

    // Boolean invariant validation across distinct inputs
    std::vector<std::string> testInputs = {
        "",
        "Megalodon.mp3",
        "ExileLord - Soulless 4.mp3",
        "01 - ExileLord - Soulless 5.flac",
        "Pink Floyd - The Wall - 03 - Hey You.mp3",
        "[FELT] Rendezvous.mp3"
    };

    for (const auto& input : testInputs) {
        auto res = ParseFilenameHeuristic(input);
        ASSERT_EQ(res.hasArtist, !res.artist.empty());
        ASSERT_EQ(res.hasAlbum, !res.album.empty());
        ASSERT_EQ(res.hasTrackNumber, (res.trackNumber > 0));
    }
}

// ============================================================================
// TIER 4: REAL-WORLD DATASET VERIFICATION (D:\media\music\TO SORT\ExileLord)
// ============================================================================

TEST_CASE("Filename Heuristic Parser", "Tier 4: ExileLord Real-World Singles from TO SORT") {
    // Exact file list from D:\media\music\TO SORT\ExileLord
    struct ExpectedSingle {
        std::string filename;
        std::string expectedTitle;
    };

    const std::vector<ExpectedSingle> exileLordFiles = {
        { "ExileLord - Soulless 4.mp3", "Soulless 4" },
        { "ExileLord - Megalodon.mp3", "Megalodon" },
        { "ExileLord - Arm Breaker (400 BPM).mp3", "Arm Breaker (400 BPM)" },
        { "ExileLord - Soulless 2 (Mechanical Machine).mp3", "Soulless 2 (Mechanical Machine)" },
        { "ExileLord - Soulless 3 (380 BPM).mp3", "Soulless 3 (380 BPM)" },
        { "ExileLord - Soulless 5.mp3", "Soulless 5" },
        { "ExileLord - Crash Test 5.mp3", "Crash Test 5" },
        { "ExileLord - Exile's Minute of Madness (hardsongforme).mp3", "Exile's Minute of Madness (hardsongforme)" },
        { "ExileLord - SLSVSX_hybrid.mp3", "SLSVSX_hybrid" },
        { "ExileLord - i made something.mp3", "i made something" },
        { "ExileLord - Two Hour Testament.mp3", "Two Hour Testament" },
        { "ExileLord - test.mp3", "test" }
    };

    for (const auto& item : exileLordFiles) {
        auto info = ParseFilenameHeuristic(item.filename);
        ASSERT_TRUE(info.hasArtist);
        ASSERT_STR_EQ(info.artist, "ExileLord");
        ASSERT_STR_EQ(info.title, item.expectedTitle);
        ASSERT_FALSE(info.hasAlbum);
        ASSERT_FALSE(info.hasTrackNumber);
        ASSERT_EQ(info.trackNumber, 0);

        // Ensure title does not retain leading "ExileLord - "
        ASSERT_FALSE(info.title.rfind("ExileLord - ", 0) == 0);
    }
}

TEST_CASE("Filename Heuristic Parser", "Tier 4: Full Path and Extension Resilience") {
    // Windows absolute path with backslashes
    auto res1 = ParseFilenameHeuristic("D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Soulless 4.mp3");
    ASSERT_TRUE(res1.hasArtist);
    ASSERT_STR_EQ(res1.artist, "ExileLord");
    ASSERT_STR_EQ(res1.title, "Soulless 4");

    // Path with forward slashes
    auto res2 = ParseFilenameHeuristic("D:/media/music/TO SORT/ExileLord/ExileLord - Soulless 4.mp3");
    ASSERT_TRUE(res2.hasArtist);
    ASSERT_STR_EQ(res2.artist, "ExileLord");
    ASSERT_STR_EQ(res2.title, "Soulless 4");

    // Various audio extensions produce identical parsed values
    std::vector<std::string> extList = { ".mp3", ".flac", ".wav", ".ogg", ".m4a", ".opus", "" };
    for (const auto& ext : extList) {
        auto res = ParseFilenameHeuristic("ExileLord - Soulless 4" + ext);
        ASSERT_TRUE(res.hasArtist);
        ASSERT_STR_EQ(res.artist, "ExileLord");
        ASSERT_STR_EQ(res.title, "Soulless 4");
    }
}
