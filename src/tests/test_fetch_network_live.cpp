#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include "../include/MetadataUtils.hpp"
#include <fstream>

TEST_CASE("Live Network Fetch", "Live LRCLIB Lyrics Fetch") {
    std::string lyrics = FetchServices::FetchLrcLibSyncedLyrics("nomico", "Bad Apple!!", "Lovelight");
    ASSERT_FALSE(lyrics.empty());
    ASSERT_TRUE(lyrics.find("[00:") != std::string::npos || lyrics.find("Nagare yuku") != std::string::npos);
    std::cout << "      -> Fetched " << lyrics.size() << " bytes of lyrics. Sample: " 
              << lyrics.substr(0, lyrics.find('\n')) << "\n";
}

TEST_CASE("Live Network Fetch", "Live TouhouDB API Search and Fetch") {
    VdbReleaseInfo info;
    bool ok = FetchServices::SearchVdbRelease("https://touhoudb.com", "TouhouDB", "Diabolic Phantasma", "Daydream In the Dead of Night", "DBPS-001", info);
    ASSERT_TRUE(ok);
    ASSERT_STR_EQ(info.title, "Daydream In the Dead of Night");
    ASSERT_STR_EQ(info.catalogNumber, "DBPS-001");
    ASSERT_FALSE(info.tracks.empty());
    ASSERT_FALSE(info.coverUrl.empty());
    std::cout << "      -> TouhouDB matched album ID " << info.id << ", " << info.tracks.size() 
              << " tracks, Cover: " << info.coverUrl << "\n";
}

TEST_CASE("Live Network Fetch", "Live VocaDB API Search and Fetch") {
    VdbReleaseInfo info;
    bool ok = FetchServices::SearchVdbRelease("https://vocadb.net", "VocaDB", "wowaka", "Unhappy Refrain", "DGSA-10008", info);
    ASSERT_TRUE(ok);
    ASSERT_FALSE(info.title.empty());
    ASSERT_FALSE(info.tracks.empty());
    ASSERT_FALSE(info.coverUrl.empty());
    std::cout << "      -> VocaDB matched album ID " << info.id << " (" << info.title << "), " 
              << info.tracks.size() << " tracks\n";
}

TEST_CASE("Live Network Fetch", "Live UtaiteDB API Fetch Album Details") {
    VdbReleaseInfo info;
    bool ok = FetchServices::FetchVdbAlbumDetails("https://utaitedb.net", "UtaiteDB", 1, info);
    ASSERT_TRUE(ok);
    ASSERT_FALSE(info.title.empty());
    ASSERT_FALSE(info.tracks.empty());
    std::cout << "      -> UtaiteDB fetched album ID " << info.id << " (" << info.title << "), " 
              << info.tracks.size() << " tracks\n";
}

TEST_CASE("Live Network Fetch", "Live MusicBrainz Release Group Search and Tracks Fetch") {
    std::string url = "https://musicbrainz.org/ws/2/release-group?query=releasegroup:Lovelight%20AND%20artist:%22Alstroemeria%20Records%22&fmt=json";
    std::string res = FetchServices::HttpGetString(Utf8ToWide(url));
    ASSERT_FALSE(res.empty());
    auto candidates = FetchServices::ParseMusicBrainzReleaseGroups(res);
    ASSERT_FALSE(candidates.empty());
    ASSERT_STR_EQ(candidates[0].title, "Lovelight");
    ASSERT_EQ(candidates[0].id.length(), 36);

    std::string relDate;
    auto tracks = FetchServices::FetchMusicBrainzReleaseTracks(candidates[0].id, &relDate);
    ASSERT_FALSE(tracks.empty());
    std::cout << "      -> MusicBrainz MBID: " << candidates[0].id << ", Date: " << relDate 
              << ", Tracks: " << tracks.size() << " (Track #1: " << tracks[0].title << ")\n";
}

TEST_CASE("Live Network Fetch", "Live Cover Art Archive Download") {
    // Release Group MBID for Lovelight (actual MusicBrainz UUID)
    std::string rgMbId = "8ef0427c-bd47-360b-b2f7-63f88eff7960";
    std::string coverUrl = "https://coverartarchive.org/release-group/" + rgMbId + "/front";
    auto bytes = FetchServices::HttpGetBytes(Utf8ToWide(coverUrl));
    ASSERT_FALSE(bytes.empty());
    ASSERT_GE(bytes.size(), 1000); // Image file should be at least a few KB
    std::cout << "      -> Cover Art Archive downloaded " << bytes.size() << " bytes of image data\n";
}

TEST_CASE("Live Network Fetch", "Live Discogs Search and Details Fetch") {
    std::string token;
    std::ifstream in("folders.cfg");
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                if (k == "discogs_token") {
                    token = v;
                    break;
                }
            }
        }
    }

    DiscogsReleaseInfo info;
    bool ok = FetchServices::SearchDiscogsRelease("Daft Punk", "Discovery", info, token);
    ASSERT_TRUE(ok);
    ASSERT_FALSE(info.id.empty());
    ASSERT_STR_EQ(info.title, "Discovery");
    ASSERT_FALSE(info.tracks.empty());
    std::cout << "      -> Discogs matched ID " << info.id << " (" << info.title << "), " 
              << info.tracks.size() << " tracks, Year: " << info.year 
              << ", Token: " << (!token.empty() ? "Present in folders.cfg" : "Anonymous") << "\n";
}


