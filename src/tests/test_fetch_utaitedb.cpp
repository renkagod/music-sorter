#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include "../include/MetadataUtils.hpp"

TEST_CASE("UtaiteDB Fetch", "Parse Album Details JSON with Utaite Artist") {
    std::string json = R"json({
        "id": 880,
        "name": "Sennzai 1st Solo Album",
        "defaultName": "Arrêter le temps",
        "catalogNumber": "SNZ-0001",
        "releaseDate": {
            "year": 2017,
            "month": 10,
            "day": 29,
            "isEmpty": false
        },
        "artistString": "Sennzai",
        "artists": [
            {
                "categories": "Vocalist",
                "name": "Sennzai",
                "roles": "Default"
            }
        ],
        "mainPicture": {
            "urlOriginal": "https://utaitedb.net/img/arreter_le_temps.jpg"
        },
        "tracks": [
            {
                "trackNumber": 1,
                "name": "Introduction",
                "song": {
                    "lengthSeconds": 92,
                    "artistString": "Sennzai"
                }
            },
            {
                "trackNumber": 2,
                "name": "Arrêter le temps",
                "song": {
                    "lengthSeconds": 284,
                    "artistString": "Sennzai"
                }
            }
        ]
    })json";

    VdbReleaseInfo info;
    bool ok = FetchServices::ParseVdbAlbumDetailsJson(json, "UtaiteDB", 880, info);

    ASSERT_TRUE(ok);
    ASSERT_EQ(info.id, 880);
    ASSERT_STR_EQ(info.service, "UtaiteDB");
    ASSERT_STR_EQ(info.title, "Sennzai 1st Solo Album");
    ASSERT_STR_EQ(info.catalogNumber, "SNZ-0001");
    ASSERT_STR_EQ(info.releaseDate, "2017.10.29");
    ASSERT_STR_EQ(info.artist, "Sennzai");
    ASSERT_STR_EQ(info.coverUrl, "https://utaitedb.net/img/arreter_le_temps.jpg");

    ASSERT_EQ(info.tracks.size(), 2);
    ASSERT_STR_EQ(info.tracks[0].title, "Introduction");
    ASSERT_EQ(info.tracks[0].lengthMs, 92000);
    ASSERT_STR_EQ(info.tracks[1].title, "Arrêter le temps");
    ASSERT_EQ(info.tracks[1].lengthMs, 284000);
}

TEST_CASE("UtaiteDB Fetch", "Extract Album ID from UtaiteDB URLs") {
    std::string url1 = "https://utaitedb.net/Al/880";
    ASSERT_EQ(FetchServices::ExtractVdbId(url1), 880);

    std::string url2 = "https://utaitedb.net/Al/880/arreter-le-temps";
    ASSERT_EQ(FetchServices::ExtractVdbId(url2), 880);
}
