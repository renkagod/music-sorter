#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include "../include/MetadataUtils.hpp"

TEST_CASE("Live Network Fetch", "Live LRCLIB Lyrics Fetch") {
    std::string lyrics = FetchServices::FetchLrcLibSyncedLyrics("nomico", "Bad Apple!!", "Lovelight");
    if (!lyrics.empty()) {
        ASSERT_TRUE(lyrics.find("Nagare yuku") != std::string::npos || lyrics.find("[00:") != std::string::npos);
    } else {
        std::cout << "    [SKIP/NOTE] LRCLIB live fetch returned empty (network offline or endpoint unreachable)\n";
    }
}

TEST_CASE("Live Network Fetch", "Live TouhouDB API Search and Fetch") {
    VdbReleaseInfo info;
    bool ok = FetchServices::SearchVdbRelease("https://touhoudb.com", "TouhouDB", "Diabolic Phantasma", "Daydream In the Dead of Night", "DBPS-001", info);
    if (ok) {
        ASSERT_FALSE(info.title.empty());
        ASSERT_STR_EQ(info.catalogNumber, "DBPS-001");
        ASSERT_FALSE(info.tracks.empty());
    } else {
        std::cout << "    [SKIP/NOTE] TouhouDB live fetch returned empty (network offline or rate limited)\n";
    }
}

TEST_CASE("Live Network Fetch", "Live VocaDB API Search and Fetch") {
    VdbReleaseInfo info;
    bool ok = FetchServices::SearchVdbRelease("https://vocadb.net", "VocaDB", "wowaka", "Unhappy Refrain", "DGSA-10008", info);
    if (ok) {
        ASSERT_FALSE(info.title.empty());
        ASSERT_FALSE(info.tracks.empty());
    } else {
        std::cout << "    [SKIP/NOTE] VocaDB live fetch returned empty (network offline or rate limited)\n";
    }
}

TEST_CASE("Live Network Fetch", "Live MusicBrainz Release Group Search") {
    std::string url = "https://musicbrainz.org/ws/2/release-group?query=releasegroup:Lovelight%20AND%20artist:%22Alstroemeria%20Records%22&fmt=json";
    std::string res = FetchServices::HttpGetString(Utf8ToWide(url));
    if (!res.empty()) {
        auto candidates = FetchServices::ParseMusicBrainzReleaseGroups(res);
        if (!candidates.empty()) {
            ASSERT_STR_EQ(candidates[0].title, "Lovelight");
            ASSERT_EQ(candidates[0].id.length(), 36);
        }
    } else {
        std::cout << "    [SKIP/NOTE] MusicBrainz live search returned empty (network offline or throttled)\n";
    }
}
