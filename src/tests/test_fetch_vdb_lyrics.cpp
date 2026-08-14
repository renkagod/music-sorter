#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include "../include/MetadataUtils.hpp"

TEST_CASE("VDB Lyrics Fetch", "PickBestLyrics Language Preference Chain") {
    std::string orig = "散弾銃とテレキャスター";
    std::string romaji = "Sandanjuu to telecaster";
    std::string english = "A shotgun and a telecaster";

    // Romaji preference
    ASSERT_STR_EQ(FetchServices::PickBestLyrics(romaji, english, orig, "Romaji"), romaji);
    ASSERT_STR_EQ(FetchServices::PickBestLyrics("", english, orig, "Romaji"), orig);
    ASSERT_STR_EQ(FetchServices::PickBestLyrics("", english, "", "Romaji"), english);

    // English preference
    ASSERT_STR_EQ(FetchServices::PickBestLyrics(romaji, english, orig, "English"), english);
    ASSERT_STR_EQ(FetchServices::PickBestLyrics(romaji, "", orig, "English"), romaji);
    ASSERT_STR_EQ(FetchServices::PickBestLyrics("", "", orig, "English"), orig);

    // Japanese preference
    ASSERT_STR_EQ(FetchServices::PickBestLyrics(romaji, english, orig, "Japanese"), orig);
    ASSERT_STR_EQ(FetchServices::PickBestLyrics(romaji, english, "", "Japanese"), romaji);
    ASSERT_STR_EQ(FetchServices::PickBestLyrics("", english, "", "Japanese"), english);
}

TEST_CASE("VDB Lyrics Fetch", "Parse Song Lyrics JSON with Multiple Translation Types") {
    std::string sampleJson = R"json({
        "id": 1500,
        "name": "アンハッピーリフレイン",
        "lyrics": [
            {
                "id": 93,
                "translationType": "Original",
                "cultureCode": "ja",
                "source": "Official",
                "value": "散弾銃とテレキャスター\n言葉の整列、アンハッピー"
            },
            {
                "id": 4013,
                "translationType": "Romanized",
                "cultureCode": "",
                "source": "mylifemyword",
                "value": "Sandanjuu to telecaster\nKotoba no seiretsu, unhappy"
            },
            {
                "id": 4014,
                "translationType": "Translation",
                "cultureCodes": ["en"],
                "source": "Vocaloid Lyrics Wiki",
                "value": "A shotgun and a telecaster\nwords lined up in rows, unhappy"
            }
        ]
    })json";

    std::string orig, romaji, eng;
    bool ok = FetchServices::ParseVdbSongLyricsJson(sampleJson, orig, romaji, eng);
    ASSERT_TRUE(ok);
    ASSERT_STR_EQ(orig, "散弾銃とテレキャスター\n言葉の整列、アンハッピー");
    ASSERT_STR_EQ(romaji, "Sandanjuu to telecaster\nKotoba no seiretsu, unhappy");
    ASSERT_STR_EQ(eng, "A shotgun and a telecaster\nwords lined up in rows, unhappy");
}

TEST_CASE("VDB Lyrics Fetch", "Parse Album Details JSON with Embedded Song Lyrics") {
    std::string albumJson = R"json({
        "id": 79,
        "name": "Unhappy Refrain",
        "defaultName": "アンハッピーリフレイン",
        "artistString": "wowaka",
        "tracks": [
            {
                "trackNumber": 1,
                "name": "Unhappy Refrain",
                "song": {
                    "id": 1500,
                    "name": "アンハッピーリフレイン",
                    "lengthSeconds": 198,
                    "lyrics": [
                        {
                            "id": 93,
                            "translationType": "Original",
                            "value": "Japanese Lyrics Line 1"
                        },
                        {
                            "id": 4013,
                            "translationType": "Romanized",
                            "value": "Romaji Lyrics Line 1"
                        },
                        {
                            "id": 4014,
                            "translationType": "Translation",
                            "cultureCode": "en",
                            "value": "English Translation Line 1"
                        }
                    ]
                }
            }
        ]
    })json";

    VdbReleaseInfo info;
    bool ok = FetchServices::ParseVdbAlbumDetailsJson(albumJson, "VocaDB", 79, info);
    ASSERT_TRUE(ok);
    ASSERT_EQ(info.tracks.size(), 1);
    ASSERT_STR_EQ(info.tracks[0].lyricsOriginal, "Japanese Lyrics Line 1");
    ASSERT_STR_EQ(info.tracks[0].lyricsRomaji, "Romaji Lyrics Line 1");
    ASSERT_STR_EQ(info.tracks[0].lyricsEnglish, "English Translation Line 1");
}
