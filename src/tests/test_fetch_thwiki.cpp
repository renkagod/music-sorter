#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include "../include/MetadataUtils.hpp"

TEST_CASE("THBWiki Fetch", "Parse Album Details JSON with Circle, Tracks, Cover and LRC") {
    std::string json = R"json([
        [
            ["alname", ["Stand Up"]],
            ["circle", ["FELT"]],
            ["date", ["2012-5-27"]],
            ["coverurl", ["https://upload.thbwiki.cc/thumb/c/c7/Stand_Up.jpg/800px-Stand_Up.jpg"]],
            ["event", ["第九回 博麗神社例大祭"]]
        ],
        [
            [
                ["id", 1266162],
                ["name", ["Reset (Introduction)"]],
                ["trackno", [1]],
                ["artist", ["NAGI☆"]],
                ["arrange", ["NAGI☆"]],
                ["vocal", ""],
                ["ogmusic", ""],
                ["lrc", ""],
                ["time", ["42"]]
            ],
            [
                ["id", 1266163],
                ["name", ["Innocent Eyes"]],
                ["trackno", [2]],
                ["artist", ["舞花"]],
                ["arrange", ["NAGI☆"]],
                ["vocal", ["舞花"]],
                ["ogmusic", ["少女さとり　～ 3rd eye"]],
                ["lrc", ["https://lyrics.thwiki.cc/Innocent_Eyes.lrc"]],
                ["time", ["258"]]
            ],
            [
                ["id", 1266164],
                ["name", ["World Around Us"]],
                ["trackno", [3]],
                ["artist", ["Renko", "Vivienne", "W.nova"]],
                ["arrange", ["Maurits\"禅\"Cornelis"]],
                ["vocal", ["W.nova", "Vivienne", "Renko"]],
                ["ogmusic", ["クリスタライズシルバー"]],
                ["lrc", ["https://lyrics.thwiki.cc/World_Around_Us.lrc"]],
                ["time", ["294"]]
            ]
        ]
    ])json";

    ThwikiReleaseInfo info;
    bool ok = FetchServices::ParseThwikiAlbumDetailsJson(json, 43822, info);

    ASSERT_TRUE(ok);
    ASSERT_EQ(info.id, 43822);
    ASSERT_STR_EQ(info.title, "Stand Up");
    ASSERT_STR_EQ(info.circle, "FELT");
    ASSERT_STR_EQ(info.releaseDate, "2012-5-27");
    ASSERT_STR_EQ(info.event, "第九回 博麗神社例大祭");
    ASSERT_STR_EQ(info.coverUrl, "https://upload.thbwiki.cc/thumb/c/c7/Stand_Up.jpg/800px-Stand_Up.jpg");

    ASSERT_EQ(info.tracks.size(), 3);
    ASSERT_EQ(info.tracks[0].position, 1);
    ASSERT_STR_EQ(info.tracks[0].title, "Reset (Introduction)");
    ASSERT_STR_EQ(info.tracks[0].artist, "NAGI☆");
    ASSERT_EQ(info.tracks[0].lengthMs, 42000);

    ASSERT_EQ(info.tracks[1].position, 2);
    ASSERT_STR_EQ(info.tracks[1].title, "Innocent Eyes");
    ASSERT_STR_EQ(info.tracks[1].artist, "舞花");
    ASSERT_EQ(info.tracks[1].lengthMs, 258000);
    ASSERT_STR_EQ(info.tracks[1].lyricsOriginal, "https://lyrics.thwiki.cc/Innocent_Eyes.lrc");

    ASSERT_EQ(info.tracks[2].position, 3);
    ASSERT_STR_EQ(info.tracks[2].title, "World Around Us");
    ASSERT_STR_EQ(info.tracks[2].artist, "Renko, Vivienne, W.nova");
    ASSERT_EQ(info.tracks[2].lengthMs, 294000);
}

TEST_CASE("THBWiki Fetch", "Parse Search Results JSON (m=sa)") {
    std::string json = R"json([
        [114151, "POP｜CULTURE 5", "Alstroemeria Records"],
        [55354, "POP｜CULTURE", "Alstroemeria Records"],
        [90104, "POP｜CULTURE 2", "Alstroemeria Records"]
    ])json";

    auto results = FetchServices::ParseThwikiSearchResultsJson(json);

    ASSERT_EQ(results.size(), 3);
    ASSERT_EQ(results[0].id, 114151);
    ASSERT_STR_EQ(results[0].title, "POP｜CULTURE 5");
    ASSERT_STR_EQ(results[0].circle, "Alstroemeria Records");

    ASSERT_EQ(results[1].id, 55354);
    ASSERT_STR_EQ(results[1].title, "POP｜CULTURE");
}

TEST_CASE("THBWiki Fetch", "Extract Album ID from Various THBWiki URLs and Formats") {
    ASSERT_EQ(FetchServices::ExtractThwikiId("https://thwiki.cc/album.php?m=ga&a=114151"), 114151);
    ASSERT_EQ(FetchServices::ExtractThwikiId("https://thwiki.cc/album.php?a=43822"), 43822);
    ASSERT_EQ(FetchServices::ExtractThwikiId("https://thwiki.cc/album.php?m=gt&i=1266162"), 1266162);
    ASSERT_EQ(FetchServices::ExtractThwikiId("thwiki_114151"), 114151);
    ASSERT_EQ(FetchServices::ExtractThwikiId("THWIKI_43822"), 43822);
    ASSERT_EQ(FetchServices::ExtractThwikiId("114151"), 114151);
}

TEST_CASE("THBWiki Fetch", "Empty / Malformed JSON Handling") {
    ThwikiReleaseInfo info;
    ASSERT_FALSE(FetchServices::ParseThwikiAlbumDetailsJson("", 1, info));
    ASSERT_FALSE(FetchServices::ParseThwikiAlbumDetailsJson("{}", 1, info));
    ASSERT_FALSE(FetchServices::ParseThwikiAlbumDetailsJson("[[]]", 1, info));

    auto results = FetchServices::ParseThwikiSearchResultsJson("");
    ASSERT_TRUE(results.empty());
    results = FetchServices::ParseThwikiSearchResultsJson("null");
    ASSERT_TRUE(results.empty());
}
