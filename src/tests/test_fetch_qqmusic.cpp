#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include "../include/MetadataUtils.hpp"

TEST_CASE("QQ Music", "ParseQQMusicSearchJson extracts songmid with best match") {
    std::string mockSearchJson = R"json({
        "code": 0,
        "data": {
            "song": {
                "list": [
                    {
                        "songmid": "0013uHHh4DlmEL",
                        "songname": "Daydream In the Dead of Night (Explicit)",
                        "singer": [
                            { "name": "Diabolic Phantasma" }
                        ]
                    },
                    {
                        "songmid": "0099abcdefghij",
                        "songname": "Unrelated Song",
                        "singer": [
                            { "name": "Other Artist" }
                        ]
                    }
                ]
            }
        }
    })json";

    std::string mid = FetchServices::ParseQQMusicSearchJson(mockSearchJson, "Diabolic Phantasma", "Daydream In the Dead of Night");
    ASSERT_STR_EQ(mid, "0013uHHh4DlmEL");
}

TEST_CASE("QQ Music", "ParseQQMusicLyricJson decodes Base64 and unescapes entities") {
    // Base64 of: "[00:10.00]Hello &amp; world&#58; test"
    // "WzAwOjEwLjAwXUhlbGxvICZhbXA7IHdvcmxkJiM1ODsgdGVzdA=="
    std::string mockLyricJson = R"json({
        "code": 0,
        "lyric": "WzAwOjEwLjAwXUhlbGxvICZhbXA7IHdvcmxkJiM1ODsgdGVzdA=="
    })json";

    std::string lrc = FetchServices::ParseQQMusicLyricJson(mockLyricJson);
    ASSERT_STR_EQ(lrc, "[00:10.00]Hello & world: test");
}
