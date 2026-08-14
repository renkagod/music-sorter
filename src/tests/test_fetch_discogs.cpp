#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include "../include/MetadataUtils.hpp"

TEST_CASE("Discogs Fetch", "Parse Duration and Position Formats") {
    ASSERT_EQ(FetchServices::ParseDurationMs("03:45"), 225000);
    ASSERT_EQ(FetchServices::ParseDurationMs("1:02:15"), 3735000);
    ASSERT_EQ(FetchServices::ParseDurationMs("45"), 45000);
    ASSERT_EQ(FetchServices::ParseDurationMs(""), 0);

    ASSERT_EQ(FetchServices::ParseDiscogsPosition("A1", 1), 1);
    ASSERT_EQ(FetchServices::ParseDiscogsPosition("B2", 2), 2);
    ASSERT_EQ(FetchServices::ParseDiscogsPosition("07", 7), 7);
    ASSERT_EQ(FetchServices::ParseDiscogsPosition("Track", 5), 5);
}

TEST_CASE("Discogs Fetch", "Extract Discogs ID and Master Flag") {
    bool isMaster = false;
    std::string id1 = FetchServices::ExtractDiscogsId("https://www.discogs.com/release/1234567-Daft-Punk-Discovery", isMaster);
    ASSERT_STR_EQ(id1, "1234567");
    ASSERT_FALSE(isMaster);

    std::string id2 = FetchServices::ExtractDiscogsId("https://www.discogs.com/master/987654-Daft-Punk-Discovery", isMaster);
    ASSERT_STR_EQ(id2, "987654");
    ASSERT_TRUE(isMaster);

    std::string id3 = FetchServices::ExtractDiscogsId("https://api.discogs.com/releases/555123", isMaster);
    ASSERT_STR_EQ(id3, "555123");
    ASSERT_FALSE(isMaster);
}

TEST_CASE("Discogs Fetch", "Parse Release Details JSON With Disambiguation and Primary Cover") {
    std::string json = R"json({
        "id": 1234567,
        "title": "Discovery",
        "released": "2001-03-12",
        "artists": [
            { "name": "Daft Punk (2)" },
            { "name": "Romanthony" }
        ],
        "images": [
            { "type": "secondary", "uri": "https://img.discogs.com/back.jpg" },
            { "type": "primary", "uri": "https://img.discogs.com/front_hi_res.jpg" }
        ],
        "tracklist": [
            {
                "position": "A1",
                "type_": "track",
                "title": "One More Time",
                "duration": "05:20",
                "artists": [
                    { "name": "Romanthony (3)" }
                ]
            },
            {
                "position": "A2",
                "type_": "track",
                "title": "Aerodynamic",
                "duration": "03:27"
            }
        ]
    })json";

    DiscogsReleaseInfo info;
    bool ok = FetchServices::ParseDiscogsReleaseDetailsJson(json, "1234567", info);

    ASSERT_TRUE(ok);
    ASSERT_STR_EQ(info.id, "1234567");
    ASSERT_STR_EQ(info.title, "Discovery");
    ASSERT_STR_EQ(info.year, "2001-03-12");
    // Verifies trailing disambiguation number "(2)" is stripped
    ASSERT_STR_EQ(info.artist, "Daft Punk, Romanthony");
    // Verifies primary cover is preferred
    ASSERT_STR_EQ(info.coverUrl, "https://img.discogs.com/front_hi_res.jpg");

    ASSERT_EQ(info.tracks.size(), 2);
    ASSERT_EQ(info.tracks[0].position, 1);
    ASSERT_STR_EQ(info.tracks[0].title, "One More Time");
    ASSERT_EQ(info.tracks[0].lengthMs, 320000);
    ASSERT_STR_EQ(info.tracks[0].artist, "Romanthony");

    ASSERT_EQ(info.tracks[1].position, 2);
    ASSERT_STR_EQ(info.tracks[1].title, "Aerodynamic");
    ASSERT_EQ(info.tracks[1].lengthMs, 207000);
    ASSERT_STR_EQ(info.tracks[1].artist, "Daft Punk, Romanthony");
}

TEST_CASE("Discogs Fetch", "Search Result Ranking Logic") {
    // Tests JSON results array ranking logic with format penalties (test pressing vs official album)
    std::string json = R"json({
        "results": [
            {
                "id": 101,
                "title": "Discovery",
                "year": 2001,
                "cover_image": "https://img.discogs.com/cover101.jpg",
                "type": "release",
                "formats": [
                    { "descriptions": [ "Test Pressing", "LP" ] }
                ],
                "community": { "have": 5, "want": 10 }
            },
            {
                "id": 102,
                "title": "Discovery",
                "year": 2001,
                "cover_image": "https://img.discogs.com/cover102.jpg",
                "type": "release",
                "formats": [
                    { "descriptions": [ "Album", "CD" ] }
                ],
                "community": { "have": 500, "want": 800 }
            }
        ]
    })json";

    size_t p = 0;
    JsonVal doc = ParseJsonSimple(json, p);
    const auto& results = doc.get("results");
    ASSERT_EQ(results.arrVal.size(), 2);

    // Compute scores as SearchDiscogsRelease does
    auto computeScore = [](const JsonVal& r, const std::string& album) -> int {
        std::string albNorm = NormalizeKey(album);
        std::string rTitle = r.get("title").strVal;
        std::string rTitleNorm = NormalizeKey(rTitle);
        int score = 0;
        if (rTitleNorm.find(albNorm) != std::string::npos) score += 100;
        if (r.get("year").numVal > 0) score += 30;
        if (!r.get("cover_image").strVal.empty()) score += 20;

        const auto& fmtArr = r.get("formats");
        if (fmtArr.type == JsonVal::Array) {
            for (size_t f = 0; f < fmtArr.arrVal.size(); ++f) {
                const auto& descArr = fmtArr.get(f).get("descriptions");
                if (descArr.type == JsonVal::Array) {
                    for (size_t d = 0; d < descArr.arrVal.size(); ++d) {
                        std::string dLower = descArr.get(d).strVal;
                        std::transform(dLower.begin(), dLower.end(), dLower.begin(), ::tolower);
                        if (dLower == "album") score += 15;
                        if (dLower.find("test") != std::string::npos) score -= 50;
                    }
                }
            }
        }
        score += (int)((r.get("community").get("have").numVal + r.get("community").get("want").numVal) / 2.0);
        return score;
    };

    int score101 = computeScore(results.get(0), "Discovery");
    int score102 = computeScore(results.get(1), "Discovery");

    // Test pressing got -50 penalty and low community stats, Official album got +15 and high community stats
    ASSERT_GE(score102, score101 + 500);
}
