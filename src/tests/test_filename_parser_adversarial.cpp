#include "TestFramework.hpp"
#include "../include/MetadataUtils.hpp"
#include <iostream>
#include <vector>
#include <string>

// ============================================================================
// ADVERSARIAL VERIFICATION SUITE 1: WEIRD DELIMITERS, SPACES, MIXED DASHES
// ============================================================================

TEST_CASE("Filename Heuristic Parser Adversarial", "Adv 1: Weird Delimiters, Spaces, and Mixed Dashes") {
    // Em dash (—) with standard spaces
    auto res1 = ParseFilenameHeuristic("Queen \xE2\x80\x94 Bohemian Rhapsody.flac");
    ASSERT_TRUE(res1.hasArtist);
    ASSERT_STR_EQ(res1.artist, "Queen");
    ASSERT_STR_EQ(res1.title, "Bohemian Rhapsody");

    // En dash (–) with standard spaces
    auto res2 = ParseFilenameHeuristic("Pink Floyd \xE2\x80\x93 Comfortably Numb.mp3");
    ASSERT_TRUE(res2.hasArtist);
    ASSERT_STR_EQ(res2.artist, "Pink Floyd");
    ASSERT_STR_EQ(res2.title, "Comfortably Numb");

    // Fullwidth hyphen-minus (－)
    auto res3 = ParseFilenameHeuristic("ExileLord \xEF\xBC\x8D Soulless 4.mp3");
    ASSERT_TRUE(res3.hasArtist);
    ASSERT_STR_EQ(res3.artist, "ExileLord");
    ASSERT_STR_EQ(res3.title, "Soulless 4");

    // Em dash without spaces between artist and title
    auto res4 = ParseFilenameHeuristic("Queen\xE2\x80\x94" "Bohemian Rhapsody.flac");
    ASSERT_TRUE(res4.hasArtist);
    ASSERT_STR_EQ(res4.artist, "Queen");
    ASSERT_STR_EQ(res4.title, "Bohemian Rhapsody");

    // En dash without spaces between artist and title
    auto res5 = ParseFilenameHeuristic("Pink Floyd\xE2\x80\x93" "Comfortably Numb.mp3");
    ASSERT_TRUE(res5.hasArtist);
    ASSERT_STR_EQ(res5.artist, "Pink Floyd");
    ASSERT_STR_EQ(res5.title, "Comfortably Numb");

    // Multiple consecutive spaces around delimiter
    auto res6 = ParseFilenameHeuristic("ExileLord       -       Soulless 4.mp3");
    ASSERT_TRUE(res6.hasArtist);
    ASSERT_STR_EQ(res6.artist, "ExileLord");
    ASSERT_STR_EQ(res6.title, "Soulless 4");

    // Double hyphen delimiter
    auto res7 = ParseFilenameHeuristic("ExileLord -- Soulless 4.mp3");
    ASSERT_TRUE(res7.hasArtist);
    ASSERT_STR_EQ(res7.artist, "ExileLord");
    ASSERT_STR_EQ(res7.title, "Soulless 4");

    // Underscore delimiter: Artist_-_Title
    auto res8 = ParseFilenameHeuristic("ExileLord_-_Soulless_4.mp3");
    ASSERT_TRUE(res8.hasArtist);
    ASSERT_STR_EQ(res8.artist, "ExileLord");
    ASSERT_STR_EQ(res8.title, "Soulless 4");

    // Fullwidth ideographic spaces (U+3000) around delimiter
    auto res9 = ParseFilenameHeuristic("\xE3\x80\x80" "ExileLord\xE3\x80\x80-\xE3\x80\x80Soulless 4\xE3\x80\x80.mp3");
    ASSERT_TRUE(res9.hasArtist);
    ASSERT_STR_EQ(res9.artist, "ExileLord");
    ASSERT_STR_EQ(res9.title, "Soulless 4");

    // Consecutive dashes with space: Artist - - Title
    auto res10 = ParseFilenameHeuristic("ExileLord - - Soulless 4.mp3");
    ASSERT_TRUE(res10.hasArtist);
    ASSERT_STR_EQ(res10.artist, "ExileLord");
    ASSERT_STR_EQ(res10.title, "Soulless 4");
}

// ============================================================================
// ADVERSARIAL VERIFICATION SUITE 2: ARTIST NAMES WITH NUMBERS OR DASHES
// ============================================================================

TEST_CASE("Filename Heuristic Parser Adversarial", "Adv 2: Artist Names With Numbers or Dashes") {
    // Artist with embedded hyphen (Blink-182)
    auto res1 = ParseFilenameHeuristic("Blink-182 - What's My Age Again?.mp3");
    ASSERT_TRUE(res1.hasArtist);
    ASSERT_STR_EQ(res1.artist, "Blink-182");
    ASSERT_STR_EQ(res1.title, "What's My Age Again?");
    ASSERT_FALSE(res1.hasTrackNumber);
    ASSERT_EQ(res1.trackNumber, 0);

    // Artist starting with digits (50 Cent)
    auto res2 = ParseFilenameHeuristic("50 Cent - In Da Club.mp3");
    ASSERT_TRUE(res2.hasArtist);
    ASSERT_STR_EQ(res2.artist, "50 Cent");
    ASSERT_STR_EQ(res2.title, "In Da Club");
    ASSERT_FALSE(res2.hasTrackNumber);
    ASSERT_EQ(res2.trackNumber, 0);

    // Artist starting with digits (3 Doors Down)
    auto res3 = ParseFilenameHeuristic("3 Doors Down - Kryptonite.mp3");
    ASSERT_TRUE(res3.hasArtist);
    ASSERT_STR_EQ(res3.artist, "3 Doors Down");
    ASSERT_STR_EQ(res3.title, "Kryptonite");
    ASSERT_FALSE(res3.hasTrackNumber);

    // Artist containing number (Sum 41)
    auto res4 = ParseFilenameHeuristic("Sum 41 - Fat Lip.mp3");
    ASSERT_TRUE(res4.hasArtist);
    ASSERT_STR_EQ(res4.artist, "Sum 41");
    ASSERT_STR_EQ(res4.title, "Fat Lip");
    ASSERT_FALSE(res4.hasTrackNumber);

    // Artist name is year-like (The 1975)
    auto res5 = ParseFilenameHeuristic("The 1975 - Somebody Else.mp3");
    ASSERT_TRUE(res5.hasArtist);
    ASSERT_STR_EQ(res5.artist, "The 1975");
    ASSERT_STR_EQ(res5.title, "Somebody Else");
    ASSERT_FALSE(res5.hasTrackNumber);

    // Artist starting with digit (2Pac)
    auto res6 = ParseFilenameHeuristic("2Pac - Changes.mp3");
    ASSERT_TRUE(res6.hasArtist);
    ASSERT_STR_EQ(res6.artist, "2Pac");
    ASSERT_STR_EQ(res6.title, "Changes");
    ASSERT_FALSE(res6.hasTrackNumber);

    // Artist with digits (100 gecs)
    auto res7 = ParseFilenameHeuristic("100 gecs - Money Machine.mp3");
    ASSERT_TRUE(res7.hasArtist);
    ASSERT_STR_EQ(res7.artist, "100 gecs");
    ASSERT_STR_EQ(res7.title, "Money Machine");
    ASSERT_FALSE(res7.hasTrackNumber);

    // Artist with digits (65daysofstatic)
    auto res8 = ParseFilenameHeuristic("65daysofstatic - Retreat! Retreat!.mp3");
    ASSERT_TRUE(res8.hasArtist);
    ASSERT_STR_EQ(res8.artist, "65daysofstatic");
    ASSERT_STR_EQ(res8.title, "Retreat! Retreat!");
    ASSERT_FALSE(res8.hasTrackNumber);

    // Artist with hyphen and em-dash delimiter
    auto res9 = ParseFilenameHeuristic("Blink-182 \xE2\x80\x94 All The Small Things.mp3");
    ASSERT_TRUE(res9.hasArtist);
    ASSERT_STR_EQ(res9.artist, "Blink-182");
    ASSERT_STR_EQ(res9.title, "All The Small Things");
    ASSERT_FALSE(res9.hasTrackNumber);

    // Other artists with hyphens
    auto res10 = ParseFilenameHeuristic("Jay-Z - Empire State of Mind.mp3");
    ASSERT_TRUE(res10.hasArtist);
    ASSERT_STR_EQ(res10.artist, "Jay-Z");
    ASSERT_STR_EQ(res10.title, "Empire State of Mind");

    auto res11 = ParseFilenameHeuristic("T-Pain - Buy U a Drank.mp3");
    ASSERT_TRUE(res11.hasArtist);
    ASSERT_STR_EQ(res11.artist, "T-Pain");
    ASSERT_STR_EQ(res11.title, "Buy U a Drank");

    // Combination: leading track number + artist with hyphen
    auto res12 = ParseFilenameHeuristic("01 - Blink-182 - What's My Age Again?.mp3");
    ASSERT_TRUE(res12.hasTrackNumber);
    ASSERT_EQ(res12.trackNumber, 1);
    ASSERT_TRUE(res12.hasArtist);
    ASSERT_STR_EQ(res12.artist, "Blink-182");
    ASSERT_STR_EQ(res12.title, "What's My Age Again?");

    // Combination: leading track number with dot + artist starting with digits
    auto res13 = ParseFilenameHeuristic("01. 50 Cent - In Da Club.mp3");
    ASSERT_TRUE(res13.hasTrackNumber);
    ASSERT_EQ(res13.trackNumber, 1);
    ASSERT_TRUE(res13.hasArtist);
    ASSERT_STR_EQ(res13.artist, "50 Cent");
    ASSERT_STR_EQ(res13.title, "In Da Club");

    // Combination: disc-track prefix + artist with hyphen
    auto res14 = ParseFilenameHeuristic("1-02 - Blink-182 - All The Small Things.flac");
    ASSERT_TRUE(res14.hasTrackNumber);
    ASSERT_EQ(res14.trackNumber, 2);
    ASSERT_TRUE(res14.hasArtist);
    ASSERT_STR_EQ(res14.artist, "Blink-182");
    ASSERT_STR_EQ(res14.title, "All The Small Things");
}

// ============================================================================
// ADVERSARIAL VERIFICATION SUITE 3: TITLES WITH DASHES AND SUBTITLES
// ============================================================================

TEST_CASE("Filename Heuristic Parser Adversarial", "Adv 3: Titles With Dashes and Subtitles") {
    // 3 tokens: Artist - Title - Subtitle
    auto res1 = ParseFilenameHeuristic("Linkin Park - In the End - Live in Texas.mp3");
    ASSERT_TRUE(res1.hasArtist);
    ASSERT_STR_EQ(res1.artist, "Linkin Park");
    ASSERT_STR_EQ(res1.title, "In the End - Live in Texas");
    ASSERT_FALSE(res1.hasAlbum);
    ASSERT_FALSE(res1.hasTrackNumber);

    auto res2 = ParseFilenameHeuristic("Hans Zimmer - Time - Instrumental Version.flac");
    ASSERT_TRUE(res2.hasArtist);
    ASSERT_STR_EQ(res2.artist, "Hans Zimmer");
    ASSERT_STR_EQ(res2.title, "Time - Instrumental Version");

    auto res3 = ParseFilenameHeuristic("Daft Punk - Aerodynamic - Daft Crew Remix.mp3");
    ASSERT_TRUE(res3.hasArtist);
    ASSERT_STR_EQ(res3.artist, "Daft Punk");
    ASSERT_STR_EQ(res3.title, "Aerodynamic - Daft Crew Remix");

    auto res4 = ParseFilenameHeuristic("ExileLord - Soulless 4 - The Final Chapter.mp3");
    ASSERT_TRUE(res4.hasArtist);
    ASSERT_STR_EQ(res4.artist, "ExileLord");
    ASSERT_STR_EQ(res4.title, "Soulless 4 - The Final Chapter");

    // Leading track number with dash-containing title
    auto res5 = ParseFilenameHeuristic("01 - Linkin Park - In the End - Live in Texas.mp3");
    ASSERT_TRUE(res5.hasTrackNumber);
    ASSERT_EQ(res5.trackNumber, 1);
    ASSERT_TRUE(res5.hasArtist);
    ASSERT_STR_EQ(res5.artist, "Linkin Park");
    ASSERT_STR_EQ(res5.title, "In the End - Live in Texas");

    auto res6 = ParseFilenameHeuristic("03. Linkin Park - In the End - Live in Texas.mp3");
    ASSERT_TRUE(res6.hasTrackNumber);
    ASSERT_EQ(res6.trackNumber, 3);
    ASSERT_TRUE(res6.hasArtist);
    ASSERT_STR_EQ(res6.artist, "Linkin Park");
    ASSERT_STR_EQ(res6.title, "In the End - Live in Texas");

    // 4-token with album: Artist - Album - Track - Title
    auto res7 = ParseFilenameHeuristic("Linkin Park - Live in Texas - 03 - In the End.mp3");
    ASSERT_TRUE(res7.hasArtist);
    ASSERT_STR_EQ(res7.artist, "Linkin Park");
    ASSERT_TRUE(res7.hasAlbum);
    ASSERT_STR_EQ(res7.album, "Live in Texas");
    ASSERT_TRUE(res7.hasTrackNumber);
    ASSERT_EQ(res7.trackNumber, 3);
    ASSERT_STR_EQ(res7.title, "In the End");

    // 5-token with album: Artist - Album - Track - Title - Subtitle
    auto res8 = ParseFilenameHeuristic("Linkin Park - Live in Texas - 03 - In the End - Live.mp3");
    ASSERT_TRUE(res8.hasArtist);
    ASSERT_STR_EQ(res8.artist, "Linkin Park");
    ASSERT_TRUE(res8.hasAlbum);
    ASSERT_STR_EQ(res8.album, "Live in Texas");
    ASSERT_TRUE(res8.hasTrackNumber);
    ASSERT_EQ(res8.trackNumber, 3);
    ASSERT_STR_EQ(res8.title, "In the End - Live");

    // 4-token with track number at token 1: Artist - Track - Title - Subtitle
    auto res9 = ParseFilenameHeuristic("ExileLord - 04 - Soulless 4 - The Final Chapter.mp3");
    ASSERT_TRUE(res9.hasArtist);
    ASSERT_STR_EQ(res9.artist, "ExileLord");
    ASSERT_TRUE(res9.hasTrackNumber);
    ASSERT_EQ(res9.trackNumber, 4);
    ASSERT_STR_EQ(res9.title, "Soulless 4 - The Final Chapter");
}

// ============================================================================
// ADVERSARIAL VERIFICATION SUITE 4: EMPTY, WHITESPACE, SINGLE-LETTER, EXTENSION-ONLY
// ============================================================================

TEST_CASE("Filename Heuristic Parser Adversarial", "Adv 4: Empty, Whitespace, Single-Letter, and Extension-Only") {
    // Empty input
    auto res1 = ParseFilenameHeuristic("");
    ASSERT_FALSE(res1.hasArtist);
    ASSERT_FALSE(res1.hasAlbum);
    ASSERT_FALSE(res1.hasTrackNumber);
    ASSERT_STR_EQ(res1.artist, "");
    ASSERT_STR_EQ(res1.title, "");
    ASSERT_EQ(res1.trackNumber, 0);

    // Whitespace only
    auto res2 = ParseFilenameHeuristic("     ");
    ASSERT_FALSE(res2.hasArtist);
    ASSERT_FALSE(res2.hasAlbum);
    ASSERT_FALSE(res2.hasTrackNumber);
    ASSERT_STR_EQ(res2.title, "");

    auto res3 = ParseFilenameHeuristic("\t\r\n  \v\f");
    ASSERT_FALSE(res3.hasArtist);
    ASSERT_FALSE(res3.hasAlbum);
    ASSERT_FALSE(res3.hasTrackNumber);
    ASSERT_STR_EQ(res3.title, "");

    // Ideographic spaces only
    auto res4 = ParseFilenameHeuristic("\xE3\x80\x80\xE3\x80\x80");
    ASSERT_FALSE(res4.hasArtist);
    ASSERT_FALSE(res4.hasTrackNumber);
    ASSERT_STR_EQ(res4.title, "");

    // Single letter title
    auto res5 = ParseFilenameHeuristic("A.mp3");
    ASSERT_FALSE(res5.hasArtist);
    ASSERT_STR_EQ(res5.title, "A");
    ASSERT_FALSE(res5.hasTrackNumber);

    auto res6 = ParseFilenameHeuristic("Z");
    ASSERT_FALSE(res6.hasArtist);
    ASSERT_STR_EQ(res6.title, "Z");
    ASSERT_FALSE(res6.hasTrackNumber);

    // Single letter artist and title
    auto res7 = ParseFilenameHeuristic("A - B.mp3");
    ASSERT_TRUE(res7.hasArtist);
    ASSERT_STR_EQ(res7.artist, "A");
    ASSERT_STR_EQ(res7.title, "B");
    ASSERT_FALSE(res7.hasTrackNumber);

    // Track number prefix with single letter title
    auto res8 = ParseFilenameHeuristic("01 - A.mp3");
    ASSERT_TRUE(res8.hasTrackNumber);
    ASSERT_EQ(res8.trackNumber, 1);
    ASSERT_FALSE(res8.hasArtist);
    ASSERT_STR_EQ(res8.title, "A");

    // Track number prefix with single letter artist and title
    auto res9 = ParseFilenameHeuristic("01. A - B.mp3");
    ASSERT_TRUE(res9.hasTrackNumber);
    ASSERT_EQ(res9.trackNumber, 1);
    ASSERT_TRUE(res9.hasArtist);
    ASSERT_STR_EQ(res9.artist, "A");
    ASSERT_STR_EQ(res9.title, "B");

    // Extension-only filenames: must not crash, throw, or fabricate false artist
    auto res10 = ParseFilenameHeuristic(".mp3");
    ASSERT_FALSE(res10.hasArtist);
    ASSERT_FALSE(res10.hasAlbum);
    ASSERT_FALSE(res10.hasTrackNumber);
    ASSERT_STR_EQ(res10.artist, "");

    auto res11 = ParseFilenameHeuristic(".flac");
    ASSERT_FALSE(res11.hasArtist);
    ASSERT_FALSE(res11.hasAlbum);
    ASSERT_FALSE(res11.hasTrackNumber);
    ASSERT_STR_EQ(res11.artist, "");

    auto res12 = ParseFilenameHeuristic("  .mp3  ");
    ASSERT_FALSE(res12.hasArtist);
    ASSERT_FALSE(res12.hasAlbum);
    ASSERT_FALSE(res12.hasTrackNumber);

    auto res13 = ParseFilenameHeuristic("D:\\music\\.flac");
    ASSERT_FALSE(res13.hasArtist);
    ASSERT_FALSE(res13.hasAlbum);
    ASSERT_FALSE(res13.hasTrackNumber);

    // Filenames with trailing whitespace around extensions
    auto res14 = ParseFilenameHeuristic("ExileLord - Soulless 4.mp3 ");
    ASSERT_TRUE(res14.hasArtist);
    ASSERT_STR_EQ(res14.artist, "ExileLord");
    ASSERT_STR_EQ(res14.title, "Soulless 4");

    auto res15 = ParseFilenameHeuristic("  ExileLord - Soulless 4.mp3  ");
    ASSERT_TRUE(res15.hasArtist);
    ASSERT_STR_EQ(res15.artist, "ExileLord");
    ASSERT_STR_EQ(res15.title, "Soulless 4");
}

// ============================================================================
// ADVERSARIAL VERIFICATION SUITE 5: CLEAN ARTIST/TITLE ISOLATION INVARIANTS
// ============================================================================

TEST_CASE("Filename Heuristic Parser Adversarial", "Adv 5: Clean Artist/Title Isolation Invariants") {
    std::vector<std::string> sampleInputs = {
        "ExileLord - Soulless 4.mp3",
        "Queen - Bohemian Rhapsody.flac",
        "Pink Floyd - Comfortably Numb.mp3",
        "01 - Blink-182 - What's My Age Again?.mp3",
        "05. 50 Cent - In Da Club.mp3",
        "Linkin Park - In the End - Live in Texas.mp3",
        "Hans Zimmer - Time - Instrumental Version.flac",
        "Daft Punk - Aerodynamic - Daft Crew Remix.mp3",
        "Led Zeppelin - Mothership - 01 - Good Times Bad Times.flac",
        "1-02 Queen - Killer Queen.mp3",
        "ExileLord \xE2\x80\x94 Soulless 5.mp3",
        "ExileLord \xE2\x80\x93 Soulless 6.mp3",
        "ExileLord_-_SLSVSX_hybrid.mp3",
        "ExileLord -- Broken Circus.mp3"
    };

    for (const auto& input : sampleInputs) {
        auto res = ParseFilenameHeuristic(input);
        if (res.hasArtist) {
            // Artist must be non-empty and stripped of whitespace
            ASSERT_FALSE(res.artist.empty());
            ASSERT_EQ(res.artist.front(), detail::TrimWhitespace(res.artist).front());
            ASSERT_EQ(res.artist.back(), detail::TrimWhitespace(res.artist).back());

            // Title must be non-empty and stripped of whitespace
            ASSERT_FALSE(res.title.empty());
            ASSERT_EQ(res.title.front(), detail::TrimWhitespace(res.title).front());
            ASSERT_EQ(res.title.back(), detail::TrimWhitespace(res.title).back());

            // CRITICAL INVARIANT: Title must NEVER start with artist name
            ASSERT_FALSE(res.title.rfind(res.artist, 0) == 0);

            // CRITICAL INVARIANT: Title must NEVER start with delimiter artifacts
            ASSERT_FALSE(res.title.rfind(" - ", 0) == 0);
            ASSERT_FALSE(res.title.rfind("- ", 0) == 0);
            ASSERT_FALSE(res.title.rfind("-", 0) == 0);
            ASSERT_FALSE(res.title.rfind(". ", 0) == 0);
            ASSERT_FALSE(res.title.rfind(".", 0) == 0);

            // Title must not contain "Artist - " prefix anywhere at the beginning
            std::string artistPrefix = res.artist + " - ";
            ASSERT_FALSE(res.title.rfind(artistPrefix, 0) == 0);
        }

        // Flag consistency invariants
        ASSERT_EQ(res.hasArtist, !res.artist.empty());
        ASSERT_EQ(res.hasAlbum, !res.album.empty());
        ASSERT_EQ(res.hasTrackNumber, (res.trackNumber > 0));
    }
}

// ============================================================================
// ADVERSARIAL VERIFICATION SUITE 6: ALL 43 FILES FROM D:\media\music\TO SORT\ExileLord
// ============================================================================

TEST_CASE("Filename Heuristic Parser Adversarial", "Adv 6: All 43 Real-World Files from ExileLord Directory") {
    struct ExpectedTrack {
        std::string filename;
        std::string expectedTitle;
    };

    const std::vector<ExpectedTrack> all43ExileLordFiles = {
        { "ExileLord - Amalgamation.mp3", "Amalgamation" },
        { "ExileLord - Arm Breaker (400 BPM).mp3", "Arm Breaker (400 BPM)" },
        { "ExileLord - Arm Breaker.mp3", "Arm Breaker" },
        { "ExileLord - Broken Circus.mp3", "Broken Circus" },
        { "ExileLord - Crash Test 5.mp3", "Crash Test 5" },
        { "ExileLord - Dream 2.mp3", "Dream 2" },
        { "ExileLord - Dreamsequence.mp3", "Dreamsequence" },
        { "ExileLord - Entropy.mp3", "Entropy" },
        { "ExileLord - Epidox.mp3", "Epidox" },
        { "ExileLord - Exile's Minute of Madness (hardsongforme).mp3", "Exile's Minute of Madness (hardsongforme)" },
        { "ExileLord - Hellensoerensen.mp3", "Hellensoerensen" },
        { "ExileLord - Hollow.mp3", "Hollow" },
        { "ExileLord - Horror.mp3", "Horror" },
        { "ExileLord - Madbeatsdawg.mp3", "Madbeatsdawg" },
        { "ExileLord - Magic 3.mp3", "Magic 3" },
        { "ExileLord - Mashing 3.mp3", "Mashing 3" },
        { "ExileLord - Minds of the Mad (newsong32).mp3", "Minds of the Mad (newsong32)" },
        { "ExileLord - Misadventure (jshot2).mp3", "Misadventure (jshot2)" },
        { "ExileLord - Monochrome (The Gray Boy).mp3", "Monochrome (The Gray Boy)" },
        { "ExileLord - Nameless Song (newsongthing).mp3", "Nameless Song (newsongthing)" },
        { "ExileLord - Nightlight II.mp3", "Nightlight II" },
        { "ExileLord - Nightlight.mp3", "Nightlight" },
        { "ExileLord - Nightmare Loop.mp3", "Nightmare Loop" },
        { "ExileLord - Obsidian.mp3", "Obsidian" },
        { "ExileLord - Quartz River (happy240bpm).mp3", "Quartz River (happy240bpm)" },
        { "ExileLord - SLSVSX_hybrid.mp3", "SLSVSX_hybrid" },
        { "ExileLord - Shamblefrost.mp3", "Shamblefrost" },
        { "ExileLord - Soulless 2 (Mechanical Machine).mp3", "Soulless 2 (Mechanical Machine)" },
        { "ExileLord - Soulless 3 (380 BPM).mp3", "Soulless 3 (380 BPM)" },
        { "ExileLord - Soulless 3.mp3", "Soulless 3" },
        { "ExileLord - Soulless 4.mp3", "Soulless 4" },
        { "ExileLord - Soulless 5.mp3", "Soulless 5" },
        { "ExileLord - Soulless 6.mp3", "Soulless 6" },
        { "ExileLord - Soulless.mp3", "Soulless" },
        { "ExileLord - Speedtest.mp3", "Speedtest" },
        { "ExileLord - Strumming Practice.mp3", "Strumming Practice" },
        { "ExileLord - Temple of the Temple.mp3", "Temple of the Temple" },
        { "ExileLord - Tower Loop.mp3", "Tower Loop" },
        { "ExileLord - Tree of Wat.mp3", "Tree of Wat" },
        { "ExileLord - Two Hour Testament.mp3", "Two Hour Testament" },
        { "ExileLord - Zigzagtest.mp3", "Zigzagtest" },
        { "ExileLord - i made something.mp3", "i made something" },
        { "ExileLord - test.mp3", "test" }
    };

    ASSERT_EQ(all43ExileLordFiles.size(), 43);

    for (const auto& item : all43ExileLordFiles) {
        // Test with filename only
        auto res = ParseFilenameHeuristic(item.filename);
        ASSERT_TRUE(res.hasArtist);
        ASSERT_STR_EQ(res.artist, "ExileLord");
        ASSERT_STR_EQ(res.title, item.expectedTitle);
        ASSERT_FALSE(res.hasAlbum);
        ASSERT_FALSE(res.hasTrackNumber);
        ASSERT_EQ(res.trackNumber, 0);

        // Title must NEVER contain artist prefix
        ASSERT_FALSE(res.title.rfind("ExileLord", 0) == 0);
        ASSERT_FALSE(res.title.rfind("-", 0) == 0);

        // Test with full Windows path
        std::string fullPath = "D:\\media\\music\\TO SORT\\ExileLord\\" + item.filename;
        auto resPath = ParseFilenameHeuristic(fullPath);
        ASSERT_TRUE(resPath.hasArtist);
        ASSERT_STR_EQ(resPath.artist, "ExileLord");
        ASSERT_STR_EQ(resPath.title, item.expectedTitle);
        ASSERT_FALSE(resPath.hasAlbum);
        ASSERT_FALSE(resPath.hasTrackNumber);
    }
}

#ifdef STANDALONE_TEST_RUNNER
int main() {
    std::cout << "=======================================================\n";
    std::cout << "  Adversarial Filename Parser Test Runner (C++20)\n";
    std::cout << "=======================================================\n\n";

    const auto& tests = TestFramework::TestRegistry::Instance().GetTests();
    int passedCount = 0;
    int failedCount = 0;

    for (const auto& t : tests) {
        if (t.suite.find("Adversarial") == std::string::npos) continue;

        TestFramework::g_ctx.assertionsRun = 0;
        TestFramework::g_ctx.assertionsFailed = 0;
        TestFramework::g_ctx.failureMessages.clear();

        auto start = std::chrono::high_resolution_clock::now();
        try {
            t.func();
        } catch (const std::exception& ex) {
            TestFramework::g_ctx.assertionsFailed++;
            TestFramework::g_ctx.failureMessages.push_back(std::string("  [EXCEPTION] ") + ex.what());
        } catch (...) {
            TestFramework::g_ctx.assertionsFailed++;
            TestFramework::g_ctx.failureMessages.push_back("  [EXCEPTION] Unknown exception");
        }
        auto end = std::chrono::high_resolution_clock::now();
        double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

        if (TestFramework::g_ctx.assertionsFailed == 0) {
            std::cout << "  [PASS] " << t.name << " (" << elapsedMs << " ms, "
                      << TestFramework::g_ctx.assertionsRun << " assertions)\n";
            passedCount++;
        } else {
            std::cout << "  [FAIL] " << t.name << " (" << elapsedMs << " ms, "
                      << TestFramework::g_ctx.assertionsFailed << " failed of "
                      << TestFramework::g_ctx.assertionsRun << " assertions)\n";
            for (const auto& msg : TestFramework::g_ctx.failureMessages) {
                std::cout << msg << "\n";
            }
            failedCount++;
        }
    }

    std::cout << "\n=======================================================\n";
    std::cout << "  Adversarial Summary: " << passedCount << " passed, " << failedCount << " failed\n";
    std::cout << "=======================================================\n";

    return (failedCount == 0) ? 0 : 1;
}
#endif
