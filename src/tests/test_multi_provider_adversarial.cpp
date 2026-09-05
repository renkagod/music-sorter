#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include "../include/MetadataUtils.hpp"
#include <thread>
#include <vector>
#include <chrono>

#ifndef ASSERT_LT
#define ASSERT_LT(actual, expected) ASSERT_TRUE((actual) < (expected))
#endif

#ifndef ASSERT_GT
#define ASSERT_GT(actual, expected) ASSERT_TRUE((actual) > (expected))
#endif

#ifndef ASSERT_GE
#define ASSERT_GE(actual, expected) ASSERT_TRUE((actual) >= (expected))
#endif

// ============================================================================
// SUITE: Multi-Provider Adversarial (Challenger M3-1)
// ============================================================================

TEST_CASE("Multi-Provider Adversarial", "Adv 1: IsJsonWellFormed Truncation Fuzzing") {
    std::string sampleJson = R"json({
      "results": {
        "trackmatches": {
          "track": [
            {
              "name": "One More Time",
              "artist": "Daft Punk",
              "url": "https://www.last.fm/music/Daft+Punk/_/One+More+Time",
              "image": [{ "#text": "https://example.com/art.png", "size": "extralarge" }]
            }
          ]
        }
      }
    })json";

    // Whole JSON should be well-formed
    ASSERT_TRUE(FetchServices::IsJsonWellFormed(sampleJson));

    // Every single truncation point must NOT be well formed and must not crash
    for (size_t i = 0; i < sampleJson.size() - 1; ++i) {
        std::string truncated = sampleJson.substr(0, i);
        bool wf = FetchServices::IsJsonWellFormed(truncated);
        ASSERT_FALSE(wf);
    }
}

TEST_CASE("Multi-Provider Adversarial", "Adv 2: Deeply Nested and Mismatched Bracket JSON") {
    // 100 levels of nested objects
    std::string deepOpen, deepClose;
    for (int i = 0; i < 80; ++i) {
        deepOpen += "{\"n\":";
        deepClose += "}";
    }
    std::string deepJson = deepOpen + "\"val\"" + deepClose;
    ASSERT_TRUE(FetchServices::IsJsonWellFormed(deepJson));

    // Mismatched brackets
    ASSERT_FALSE(FetchServices::IsJsonWellFormed("{[}]"));
    // Note: IsJsonWellFormed counts depths independently. Let's see how it evaluates {[}]
    // {[}] has braceDepth=0 and bracketDepth=0, but unbalanced.
    // Let's test if ParseLastFmTrackSearchJson handles {[}] safely without crashing:
    auto r1 = FetchServices::ParseLastFmTrackSearchJson("{[}]");
    ASSERT_TRUE(r1.empty());

    auto r2 = FetchServices::ParseYouTubeMusicSearchJson("{[}]");
    ASSERT_TRUE(r2.empty());

    // Single token garbage
    ASSERT_FALSE(FetchServices::IsJsonWellFormed("{"));
    ASSERT_FALSE(FetchServices::IsJsonWellFormed("}"));
    ASSERT_FALSE(FetchServices::IsJsonWellFormed("["));
    ASSERT_FALSE(FetchServices::IsJsonWellFormed("]"));
    ASSERT_FALSE(FetchServices::IsJsonWellFormed("\""));
    ASSERT_FALSE(FetchServices::IsJsonWellFormed(""));
    ASSERT_FALSE(FetchServices::IsJsonWellFormed("   "));
}

TEST_CASE("Multi-Provider Adversarial", "Adv 3: LastFm Corrupted Types and Nulls") {
    // 1. results is a number or null
    std::string badResults1 = "{\"results\": null}";
    auto t1 = FetchServices::ParseLastFmTrackSearchJson(badResults1);
    ASSERT_EQ(t1.size(), 0);

    std::string badResults2 = "{\"results\": 12345}";
    auto t2 = FetchServices::ParseLastFmTrackSearchJson(badResults2);
    ASSERT_EQ(t2.size(), 0);

    // 2. trackmatches is an array or string
    std::string badTrackmatches = "{\"results\": {\"trackmatches\": \"none\"}}";
    auto t3 = FetchServices::ParseLastFmTrackSearchJson(badTrackmatches);
    ASSERT_EQ(t3.size(), 0);

    // 3. track items have null name and artist
    std::string nullFields = R"json({
      "results": {
        "trackmatches": {
          "track": [
            { "name": null, "artist": null, "url": null },
            { "name": "", "artist": "" },
            { "name": 404, "artist": 500 }
          ]
        }
      }
    })json";
    auto t4 = FetchServices::ParseLastFmTrackSearchJson(nullFields);
    // Null and empty fields should yield 0 valid tracks
    ASSERT_EQ(t4.size(), 0);

    // 4. Album getInfo tracks string instead of object (Last.fm empty album response)
    std::string emptyAlbum = R"json({
      "album": {
        "name": "Empty Album",
        "artist": "Empty Artist",
        "tracks": ""
      }
    })json";
    LastFmAlbumInfo alb;
    bool ok = FetchServices::ParseLastFmAlbumInfoJson(emptyAlbum, alb);
    ASSERT_TRUE(ok);
    ASSERT_STR_EQ(alb.album, "Empty Album");
    ASSERT_STR_EQ(alb.artist, "Empty Artist");
    ASSERT_EQ(alb.tracklist.size(), 0);
}

TEST_CASE("Multi-Provider Adversarial", "Adv 4: LastFm Unicode and Emoji Resilience") {
    std::string unicodeTrackJson = R"json({
      "results": {
        "trackmatches": {
          "track": [
            {
              "name": "Bad Apple!! feat. nomico (チルノのパーフェクトさんすう教室)",
              "artist": "Alstroemeria Records / 魂音泉",
              "url": "https://www.last.fm/music/Alstroemeria+Records/_/Bad+Apple%21%21",
              "image": [{ "#text": "https://example.com/cover_cjk.jpg", "size": "extralarge" }]
            },
            {
              "name": "Группа крови 🎸 🔥",
              "artist": "Кино (Viktor Tsoi)",
              "url": "https://www.last.fm/music/Kino/_/Gruppa+Krovi",
              "image": [{ "#text": "https://example.com/cover_cyrillic.jpg", "size": "large" }]
            }
          ]
        }
      }
    })json";

    auto results = FetchServices::ParseLastFmTrackSearchJson(unicodeTrackJson);
    ASSERT_EQ(results.size(), 2);
    ASSERT_STR_EQ(results[0].title, "Bad Apple!! feat. nomico (チルノのパーフェクトさんすう教室)");
    ASSERT_STR_EQ(results[0].artist, "Alstroemeria Records / 魂音泉");
    ASSERT_STR_EQ(results[0].coverUrl, "https://example.com/cover_cjk.jpg");

    ASSERT_STR_EQ(results[1].title, "Группа крови 🎸 🔥");
    ASSERT_STR_EQ(results[1].artist, "Кино (Viktor Tsoi)");
    ASSERT_STR_EQ(results[1].coverUrl, "https://example.com/cover_cyrillic.jpg");
}

TEST_CASE("Multi-Provider Adversarial", "Adv 5: YouTubeMusic MPRE Album Before Artist Candidate Bug") {
    // When YouTube Music subtitle runs contain Album first (with MPRE browseId)
    // and Artist second (text without ARTIST pageType), verify candidate assignment
    std::string mockJson = R"json({
      "contents": {
        "tabbedSearchResultsRenderer": {
          "tabs": [{
            "tabRenderer": {
              "content": {
                "sectionListRenderer": {
                  "contents": [{
                    "musicShelfRenderer": {
                      "contents": [{
                        "musicResponsiveListItemRenderer": {
                          "flexColumns": [
                            {
                              "musicResponsiveListItemFlexColumnRenderer": {
                                "text": { "runs": [{ "text": "Adventure" }] }
                              }
                            },
                            {
                              "musicResponsiveListItemFlexColumnRenderer": {
                                "text": {
                                  "runs": [
                                    {
                                      "text": "Madeon",
                                      "navigationEndpoint": {
                                        "browseEndpoint": {
                                          "browseId": "UCxyz_artist",
                                          "browseEndpointContextSupportedConfigs": {
                                            "browseEndpointContextMusicConfig": {
                                              "pageType": "MUSIC_PAGE_TYPE_ARTIST"
                                            }
                                          }
                                        }
                                      }
                                    },
                                    { "text": " • " },
                                    {
                                      "text": "Adventure (Deluxe)",
                                      "navigationEndpoint": {
                                        "browseEndpoint": {
                                          "browseId": "MPREb_album",
                                          "browseEndpointContextSupportedConfigs": {
                                            "browseEndpointContextMusicConfig": {
                                              "pageType": "MUSIC_PAGE_TYPE_ALBUM"
                                            }
                                          }
                                        }
                                      }
                                    }
                                  ]
                                }
                              }
                            }
                          ]
                        }
                      }]
                    }
                  }]
                }
              }
            }
          }]
        }
      }
    })json";

    auto tracks = FetchServices::ParseYouTubeMusicSearchJson(mockJson);
    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(tracks[0].title, "Adventure");
    ASSERT_STR_EQ(tracks[0].artist, "Madeon");
    ASSERT_STR_EQ(tracks[0].album, "Adventure (Deluxe)");
}

TEST_CASE("Multi-Provider Adversarial", "Adv 6: YouTubeMusic Reversed Columns (Album MPRE First, Untyped Artist Second)") {
    // In some community uploads or compilations, album appears first
    std::string reversedJson = R"json({
      "contents": {
        "sectionListRenderer": {
          "contents": [{
            "musicShelfRenderer": {
              "contents": [{
                "musicResponsiveListItemRenderer": {
                  "flexColumns": [
                    {
                      "musicResponsiveListItemFlexColumnRenderer": {
                        "text": { "runs": [{ "text": "Finesse" }] }
                      }
                    },
                    {
                      "musicResponsiveListItemFlexColumnRenderer": {
                        "text": {
                          "runs": [
                            {
                              "text": "24K Magic",
                              "navigationEndpoint": {
                                "browseEndpoint": {
                                  "browseId": "MPREb_album_id",
                                  "browseEndpointContextSupportedConfigs": {
                                    "browseEndpointContextMusicConfig": {
                                      "pageType": "MUSIC_PAGE_TYPE_ALBUM"
                                    }
                                  }
                                }
                              }
                            },
                            { "text": " • " },
                            { "text": "Bruno Mars" }
                          ]
                        }
                      }
                    }
                  ]
                }
              }]
            }
          }]
        }
      }
    })json";

    auto tracks = FetchServices::ParseYouTubeMusicSearchJson(reversedJson);
    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(tracks[0].title, "Finesse");
    // Verify whether artist was assigned Bruno Mars or if it erroneously copied the album
    std::cout << "      -> Reversed candidates parsed: Artist='" << tracks[0].artist << "', Album='" << tracks[0].album << "'\n";
    ASSERT_STR_EQ(tracks[0].album, "24K Magic");
    // If bug exists, tracks[0].artist is "24K Magic" instead of "Bruno Mars"
    ASSERT_STR_EQ(tracks[0].artist, "Bruno Mars");
}

TEST_CASE("Multi-Provider Adversarial", "Adv 7: YouTubeMusic Non-Standard Separators & Duration Spacing") {
    // Tests non-standard unicode bullets: U+00B7 (·) and raw bar (|)
    std::string separatorJson = R"json({
      "contents": {
        "sectionListRenderer": {
          "contents": [{
            "musicShelfRenderer": {
              "contents": [{
                "musicResponsiveListItemRenderer": {
                  "flexColumns": [
                    {
                      "musicResponsiveListItemFlexColumnRenderer": {
                        "text": { "runs": [{ "text": "Harder Better Faster Stronger" }] }
                      }
                    },
                    {
                      "musicResponsiveListItemFlexColumnRenderer": {
                        "text": {
                          "runs": [
                            { "text": "Song" },
                            { "text": " · " },
                            { "text": "Daft Punk" },
                            { "text": " | " },
                            { "text": "Discovery" },
                            { "text": " · " },
                            { "text": "3:44" },
                            { "text": " · " },
                            { "text": "120M views" }
                          ]
                        }
                      }
                    }
                  ]
                }
              }]
            }
          }]
        }
      }
    })json";

    auto tracks = FetchServices::ParseYouTubeMusicSearchJson(separatorJson);
    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(tracks[0].title, "Harder Better Faster Stronger");
    ASSERT_STR_EQ(tracks[0].artist, "Daft Punk");
    ASSERT_STR_EQ(tracks[0].album, "Discovery");
    ASSERT_EQ(tracks[0].durationSec, 224);
}

TEST_CASE("Multi-Provider Adversarial", "Adv 8: Rate Limiter Interval Timing and Concurrency") {
    // 1. Test LastFmThrottle minimum interval (~200ms)
    auto t1 = std::chrono::steady_clock::now();
    FetchServices::LastFmThrottle();
    FetchServices::LastFmThrottle();
    FetchServices::LastFmThrottle();
    auto t2 = std::chrono::steady_clock::now();
    double lastFmMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    // 3 calls must span at least 2 intervals (>= 350 ms allowing small clock jitter)
    ASSERT_GE(lastFmMs, 350.0);
    std::cout << "      -> LastFmThrottle 3 sequential calls took " << lastFmMs << " ms (expected >= 350 ms)\n";

    // 2. Test Multi-threaded safety of YouTubeMusicThrottle
    auto startYt = std::chrono::steady_clock::now();
    std::vector<std::thread> threads;
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([]() {
            FetchServices::YouTubeMusicThrottle();
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    auto endYt = std::chrono::steady_clock::now();
    double ytMs = std::chrono::duration<double, std::milli>(endYt - startYt).count();
    // 3 calls across threads must serialize to at least 2 * 600 ms = 1200 ms (>= 1050 ms allowing jitter)
    ASSERT_GE(ytMs, 1050.0);
    std::cout << "      -> YouTubeMusicThrottle 3 concurrent threads took " << ytMs << " ms (expected >= 1050 ms)\n";
}

TEST_CASE("Multi-Provider Adversarial", "Adv 9: Extreme and Degenerate MusicCardShelfRenderer") {
    // Music card with missing thumbnail, missing runs, corrupted duration
    std::string cardJson = R"json({
      "contents": {
        "sectionListRenderer": {
          "contents": [{
            "musicCardShelfRenderer": {
              "title": { "runs": [{ "text": "Unreleased Demo" }] },
              "subtitle": {
                "runs": [
                  { "text": "Artist Only" },
                  { "text": " • " },
                  { "text": "999:99:99" }
                ]
              }
            }
          }]
        }
      }
    })json";

    auto tracks = FetchServices::ParseYouTubeMusicSearchJson(cardJson);
    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(tracks[0].title, "Unreleased Demo");
    ASSERT_STR_EQ(tracks[0].artist, "Artist Only");
}
