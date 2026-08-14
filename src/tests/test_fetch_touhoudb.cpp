#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include "../include/MetadataUtils.hpp"

TEST_CASE("TouhouDB Fetch", "Parse Album Details JSON with Circle and High-Res Cover") {
    std::string json = R"json({
        "id": 4501,
        "name": "Daydream In the Dead of Night",
        "defaultName": "Daydream In the Dead of Night",
        "catalogNumber": "DBPS-001",
        "releaseDate": {
            "year": 2024,
            "month": 5,
            "day": 12,
            "isEmpty": false
        },
        "artistString": "Diabolic Phantasma feat. various",
        "artists": [
            {
                "categories": "Circle",
                "name": "Diabolic Phantasma",
                "artist": { "artistType": "Circle" }
            },
            {
                "categories": "Producer",
                "name": "Renka",
                "artist": { "artistType": "Producer" }
            }
        ],
        "mainPicture": {
            "urlOriginal": "https://touhoudb.com/img/Daydream_Original.jpg",
            "urlThumb": "https://touhoudb.com/img/Daydream_Thumb.jpg"
        },
        "tracks": [
            {
                "trackNumber": 1,
                "name": "Dead of Night",
                "song": {
                    "lengthSeconds": 248,
                    "artistString": "Diabolic Phantasma"
                }
            },
            {
                "trackNumber": 2,
                "name": "Phantasmagoria",
                "song": {
                    "lengthSeconds": 312,
                    "artistString": "Renka feat. Sennzai"
                }
            }
        ]
    })json";

    VdbReleaseInfo info;
    bool ok = FetchServices::ParseVdbAlbumDetailsJson(json, "TouhouDB", 4501, info);

    ASSERT_TRUE(ok);
    ASSERT_EQ(info.id, 4501);
    ASSERT_STR_EQ(info.service, "TouhouDB");
    ASSERT_STR_EQ(info.title, "Daydream In the Dead of Night");
    ASSERT_STR_EQ(info.catalogNumber, "DBPS-001");
    ASSERT_STR_EQ(info.releaseDate, "2024.05.12");
    // Circle name preferred over producer name and generic artistString
    ASSERT_STR_EQ(info.artist, "Diabolic Phantasma");
    // urlOriginal preferred over urlThumb
    ASSERT_STR_EQ(info.coverUrl, "https://touhoudb.com/img/Daydream_Original.jpg");

    ASSERT_EQ(info.tracks.size(), 2);
    ASSERT_EQ(info.tracks[0].position, 1);
    ASSERT_STR_EQ(info.tracks[0].title, "Dead of Night");
    ASSERT_EQ(info.tracks[0].lengthMs, 248000);
    ASSERT_STR_EQ(info.tracks[0].artist, "Diabolic Phantasma");

    ASSERT_EQ(info.tracks[1].position, 2);
    ASSERT_STR_EQ(info.tracks[1].title, "Phantasmagoria");
    ASSERT_EQ(info.tracks[1].lengthMs, 312000);
    ASSERT_STR_EQ(info.tracks[1].artist, "Renka feat. Sennzai");
}

TEST_CASE("TouhouDB Fetch", "Extract Album ID from TouhouDB URLs") {
    std::string url1 = "https://touhoudb.com/Al/4501";
    ASSERT_EQ(FetchServices::ExtractVdbId(url1), 4501);

    std::string url2 = "https://touhoudb.com/Al/4501/daydream-in-the-dead-of-night";
    ASSERT_EQ(FetchServices::ExtractVdbId(url2), 4501);

    std::string rawId = "9876";
    ASSERT_EQ(FetchServices::ExtractVdbId(rawId), 9876);
}

TEST_CASE("TouhouDB Fetch", "Search Candidate Scoring with Catalog Number Priority") {
    std::string searchJson = R"json({
        "items": [
            {
                "id": 10,
                "name": "Daydream",
                "catalogNumber": "OTHER-001",
                "artistString": "Other Circle"
            },
            {
                "id": 20,
                "name": "Daydream In the Dead of Night",
                "catalogNumber": "DBPS-001",
                "artistString": "Diabolic Phantasma"
            }
        ]
    })json";

    size_t p = 0;
    JsonVal doc = ParseJsonSimple(searchJson, p);
    const auto& items = doc.get("items");
    ASSERT_EQ(items.arrVal.size(), 2);

    std::string targetAlbum = "Daydream In the Dead of Night";
    std::string targetCatalog = "DBPS-001";
    std::string targetArtist = "Diabolic Phantasma";

    std::string albNorm = NormalizeKey(targetAlbum);
    std::string catNorm = NormalizeKey(targetCatalog);
    std::string artNorm = NormalizeKey(targetArtist);

    int score10 = 0;
    int score20 = 0;

    // Item 10 score
    if (!catNorm.empty() && NormalizeKey(items.get(0).get("catalogNumber").strVal) == catNorm) score10 += 150;
    if (NormalizeKey(items.get(0).get("name").strVal) == albNorm) score10 += 100;

    // Item 20 score
    if (!catNorm.empty() && NormalizeKey(items.get(1).get("catalogNumber").strVal) == catNorm) score20 += 150;
    if (NormalizeKey(items.get(1).get("name").strVal) == albNorm) score20 += 100;
    if (NormalizeKey(items.get(1).get("artistString").strVal) == artNorm) score20 += 40;

    ASSERT_EQ(score10, 0);
    ASSERT_EQ(score20, 290);
}
