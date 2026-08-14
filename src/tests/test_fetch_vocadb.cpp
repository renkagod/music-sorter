#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include "../include/MetadataUtils.hpp"

TEST_CASE("VocaDB Fetch", "Parse Album Details JSON with Producer Priority") {
    std::string json = R"json({
        "id": 1520,
        "name": "Unhappy Refrain",
        "defaultName": "アンハッピーリフレイン",
        "catalogNumber": "DGSA-10008",
        "releaseDate": {
            "year": 2011,
            "month": 5,
            "day": 18,
            "isEmpty": false
        },
        "artistString": "wowaka feat. 初音ミク",
        "artists": [
            {
                "categories": "Producer",
                "name": "wowaka",
                "artist": { "artistType": "Producer" }
            },
            {
                "categories": "Vocalist",
                "name": "初音ミク",
                "artist": { "artistType": "Vocaloid" }
            }
        ],
        "mainPicture": {
            "urlOriginal": "https://vocadb.net/img/unhappy_refrain.jpg"
        },
        "tracks": [
            {
                "trackNumber": 1,
                "name": "アンハッピーリフレイン",
                "song": {
                    "lengthSeconds": 198,
                    "artistString": "wowaka feat. 初音ミク"
                }
            },
            {
                "trackNumber": 2,
                "name": "ローリンガール",
                "song": {
                    "lengthSeconds": 196,
                    "artistString": "wowaka feat. 初音ミク"
                }
            }
        ]
    })json";

    VdbReleaseInfo info;
    bool ok = FetchServices::ParseVdbAlbumDetailsJson(json, "VocaDB", 1520, info);

    ASSERT_TRUE(ok);
    ASSERT_EQ(info.id, 1520);
    ASSERT_STR_EQ(info.service, "VocaDB");
    ASSERT_STR_EQ(info.title, "Unhappy Refrain");
    ASSERT_STR_EQ(info.catalogNumber, "DGSA-10008");
    ASSERT_STR_EQ(info.releaseDate, "2011.05.18");
    ASSERT_STR_EQ(info.artist, "wowaka feat. 初音ミク");
    ASSERT_STR_EQ(info.coverUrl, "https://vocadb.net/img/unhappy_refrain.jpg");

    ASSERT_EQ(info.tracks.size(), 2);
    ASSERT_STR_EQ(info.tracks[0].title, "アンハッピーリフレイン");
    ASSERT_EQ(info.tracks[0].lengthMs, 198000);
    ASSERT_STR_EQ(info.tracks[1].title, "ローリンガール");
    ASSERT_EQ(info.tracks[1].lengthMs, 196000);
}

TEST_CASE("VocaDB Fetch", "Parse Album Details with Various Artists Fallback to Producer") {
    std::string json = R"json({
        "id": 1521,
        "name": "Compilation Album",
        "artistString": "Various artists",
        "artists": [
            {
                "categories": "Producer",
                "name": "kz(livetune)",
                "artist": { "artistType": "Producer" }
            }
        ],
        "tracks": [
            {
                "trackNumber": 1,
                "name": "Packaged",
                "song": { "lengthSeconds": 290, "artistString": "kz(livetune)" }
            }
        ]
    })json";

    VdbReleaseInfo info;
    bool ok = FetchServices::ParseVdbAlbumDetailsJson(json, "VocaDB", 1521, info);
    ASSERT_TRUE(ok);
    // When artistString is "Various artists", producer name is used
    ASSERT_STR_EQ(info.artist, "kz(livetune)");
}

TEST_CASE("VocaDB Fetch", "Extract Album ID from VocaDB URLs") {
    std::string url1 = "https://vocadb.net/Al/1520";
    ASSERT_EQ(FetchServices::ExtractVdbId(url1), 1520);

    std::string url2 = "https://vocadb.net/Al/1520/unhappy-refrain";
    ASSERT_EQ(FetchServices::ExtractVdbId(url2), 1520);
}
