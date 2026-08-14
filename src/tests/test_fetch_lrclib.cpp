#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include "../include/MetadataUtils.hpp"

TEST_CASE("LRCLIB Lyrics Fetch", "URL Construction and Parameter Encoding") {
    std::string artist = "Alstroemeria Records feat. nomico";
    std::string title = "Bad Apple!!";
    std::string album = "Lovelight";

    std::string url = "https://lrclib.net/api/get?artist_name=" + FetchServices::UrlEncode(artist) 
                    + "&track_name=" + FetchServices::UrlEncode(title)
                    + "&album_name=" + FetchServices::UrlEncode(album);

    ASSERT_TRUE(url.find("artist_name=Alstroemeria%20Records%20feat.%20nomico") != std::string::npos);
    ASSERT_TRUE(url.find("track_name=Bad%20Apple%21%21") != std::string::npos);
    ASSERT_TRUE(url.find("album_name=Lovelight") != std::string::npos);
}

TEST_CASE("LRCLIB Lyrics Fetch", "Parse Synced LRC Lyrics with Timestamp Tags") {
    std::string json = R"json({
        "id": 12345,
        "name": "Bad Apple!!",
        "trackName": "Bad Apple!!",
        "artistName": "nomico",
        "albumName": "Lovelight",
        "duration": 318,
        "instrumental": false,
        "plainLyrics": "Nagare yuku toki no naka de demo\nKedarusa ga hora guruguru mawatte",
        "syncedLyrics": "[00:15.30] Nagare yuku toki no naka de demo\n[00:22.45] Kedarusa ga hora guruguru mawatte\n[00:29.80] Watashi kara hanareru kokoro mo"
    })json";

    std::string lyrics = FetchServices::ParseLrcLibLyricsJson(json);
    ASSERT_FALSE(lyrics.empty());
    ASSERT_TRUE(lyrics.find("[00:15.30] Nagare yuku toki no naka de demo") != std::string::npos);
    ASSERT_TRUE(lyrics.find("[00:22.45] Kedarusa ga hora guruguru mawatte") != std::string::npos);
    ASSERT_TRUE(lyrics.find("[00:29.80] Watashi kara hanareru kokoro mo") != std::string::npos);
}

TEST_CASE("LRCLIB Lyrics Fetch", "Parse Plain Lyrics Fallback") {
    std::string json = R"json({
        "id": 67890,
        "name": "Instrumental Intro",
        "plainLyrics": "Line one of lyrics\nLine two of lyrics\nLine three of lyrics",
        "syncedLyrics": null
    })json";

    std::string lyrics = FetchServices::ParseLrcLibLyricsJson(json);
    ASSERT_STR_EQ(lyrics, "Line one of lyrics\nLine two of lyrics\nLine three of lyrics");
}

TEST_CASE("LRCLIB Lyrics Fetch", "Parse Not Found and Error Response") {
    std::string notFoundJson = R"json({
        "statusCode": 404,
        "error": "Not Found",
        "message": "Track not found"
    })json";

    std::string lyrics = FetchServices::ParseLrcLibLyricsJson(notFoundJson);
    ASSERT_STR_EQ(lyrics, "");

    std::string emptyJson = "";
    ASSERT_STR_EQ(FetchServices::ParseLrcLibLyricsJson(emptyJson), "");
}
