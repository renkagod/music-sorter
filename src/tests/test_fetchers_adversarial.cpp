#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include "../include/MetadataUtils.hpp"

#ifndef ASSERT_LT
#define ASSERT_LT(actual, expected) ASSERT_TRUE((actual) < (expected))
#endif

#ifndef ASSERT_GT
#define ASSERT_GT(actual, expected) ASSERT_TRUE((actual) > (expected))
#endif

// ============================================================================
// ADVERSARIAL SUITE 1: COMPLEX NESTED YOUTUBE MUSIC STRUCTURES
// ============================================================================

TEST_CASE("Fetchers Adversarial", "YouTubeMusic: musicCardShelfRenderer Top Result with Nested Contents") {
    std::string json = R"json({
      "contents": {
        "tabbedSearchResultsRenderer": {
          "tabs": [
            {
              "tabRenderer": {
                "content": {
                  "sectionListRenderer": {
                    "contents": [
                      {
                        "musicCardShelfRenderer": {
                          "title": {
                            "runs": [
                              {
                                "text": "Master of Puppets",
                                "navigationEndpoint": {
                                  "watchEndpoint": { "videoId": "MOP_CARD_ID" }
                                }
                              }
                            ]
                          },
                          "subtitle": {
                            "runs": [
                              { "text": "Song" },
                              { "text": " • " },
                              { "text": "Metallica" },
                              { "text": " • " },
                              { "text": "Master of Puppets" },
                              { "text": " • " },
                              { "text": "8:35" }
                            ]
                          },
                          "thumbnail": {
                            "musicThumbnailRenderer": {
                              "thumbnail": {
                                "thumbnails": [
                                  { "url": "https://lh3.googleusercontent.com/card_small.jpg", "width": 120 },
                                  { "url": "https://lh3.googleusercontent.com/card_large.jpg", "width": 1080 }
                                ]
                              }
                            }
                          },
                          "contents": [
                            {
                              "musicResponsiveListItemRenderer": {
                                "flexColumns": [
                                  {
                                    "musicResponsiveListItemFlexColumnRenderer": {
                                      "text": {
                                        "runs": [ { "text": "Master of Puppets (Live)" } ]
                                      }
                                    }
                                  },
                                  {
                                    "musicResponsiveListItemFlexColumnRenderer": {
                                      "text": {
                                        "runs": [
                                          { "text": "Song" },
                                          { "text": " • " },
                                          { "text": "Metallica" },
                                          { "text": " • " },
                                          { "text": "Live in Seattle '89" },
                                          { "text": " • " },
                                          { "text": "9:12" }
                                        ]
                                      }
                                    }
                                  }
                                ],
                                "playlistItemData": { "videoId": "MOP_LIVE_ID" }
                              }
                            }
                          ]
                        }
                      }
                    ]
                  }
                }
              }
            }
          ]
        }
      }
    })json";

    auto results = FetchServices::ParseYouTubeMusicSearchJson(json);
    ASSERT_EQ(results.size(), 2);

    // First item from card shelf
    ASSERT_STR_EQ(results[0].title, "Master of Puppets");
    ASSERT_STR_EQ(results[0].artist, "Metallica");
    ASSERT_STR_EQ(results[0].album, "Master of Puppets");
    ASSERT_STR_EQ(results[0].videoId, "MOP_CARD_ID");
    ASSERT_EQ(results[0].durationSec, 515);
    ASSERT_STR_EQ(results[0].coverUrl, "https://lh3.googleusercontent.com/card_large.jpg");

    // Second item from card's nested contents
    ASSERT_STR_EQ(results[1].title, "Master of Puppets (Live)");
    ASSERT_STR_EQ(results[1].artist, "Metallica");
    ASSERT_STR_EQ(results[1].album, "Live in Seattle '89");
    ASSERT_STR_EQ(results[1].videoId, "MOP_LIVE_ID");
    ASSERT_EQ(results[1].durationSec, 552);
}

TEST_CASE("Fetchers Adversarial", "YouTubeMusic: Direct sectionListRenderer Without tabbedSearchResultsRenderer") {
    // Unfiltered queries or alternate Innertube clients omit tabbedSearchResultsRenderer
    std::string json = R"json({
      "contents": {
        "sectionListRenderer": {
          "contents": [
            {
              "musicShelfRenderer": {
                "contents": [
                  {
                    "musicResponsiveListItemRenderer": {
                      "flexColumns": [
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": { "runs": [ { "text": "Direct Section Track" } ] }
                          }
                        },
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": {
                              "runs": [
                                { "text": "Song" },
                                { "text": " • " },
                                { "text": "Direct Artist" },
                                { "text": " • " },
                                { "text": "Direct Album" },
                                { "text": " • " },
                                { "text": "4:15" }
                              ]
                            }
                          }
                        }
                      ],
                      "playlistItemData": { "videoId": "DIR_SEC_01" }
                    }
                  }
                ]
              }
            }
          ]
        }
      }
    })json";

    auto results = FetchServices::ParseYouTubeMusicSearchJson(json);
    ASSERT_EQ(results.size(), 1);
    ASSERT_STR_EQ(results[0].title, "Direct Section Track");
    ASSERT_STR_EQ(results[0].artist, "Direct Artist");
    ASSERT_STR_EQ(results[0].album, "Direct Album");
    ASSERT_STR_EQ(results[0].videoId, "DIR_SEC_01");
    ASSERT_EQ(results[0].durationSec, 255);
}

TEST_CASE("Fetchers Adversarial", "YouTubeMusic: Empty tabs Array Graceful Fallback") {
    std::string json = R"json({
      "contents": {
        "tabbedSearchResultsRenderer": {
          "tabs": []
        }
      }
    })json";

    auto results = FetchServices::ParseYouTubeMusicSearchJson(json);
    ASSERT_TRUE(results.empty());
}

TEST_CASE("Fetchers Adversarial", "YouTubeMusic: Missing itemSectionRenderer and Heterogeneous Shelves") {
    // A response with a mixture of musicShelfRenderer, non-object entries, and missing itemSectionRenderer
    std::string json = R"json({
      "contents": {
        "sectionListRenderer": {
          "contents": [
            null,
            42,
            { "dummyUnknownRenderer": { "id": 1 } },
            {
              "musicShelfRenderer": {
                "contents": [
                  {
                    "musicResponsiveListItemRenderer": {
                      "flexColumns": [
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": { "runs": [ { "text": "Hetero Track" } ] }
                          }
                        },
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": {
                              "runs": [
                                { "text": "Song" },
                                { "text": " • " },
                                { "text": "Hetero Artist" },
                                { "text": " • " },
                                { "text": "3:30" }
                              ]
                            }
                          }
                        }
                      ],
                      "playlistItemData": { "videoId": "HETERO_01" }
                    }
                  }
                ]
              }
            }
          ]
        }
      }
    })json";

    auto results = FetchServices::ParseYouTubeMusicSearchJson(json);
    ASSERT_EQ(results.size(), 1);
    ASSERT_STR_EQ(results[0].title, "Hetero Track");
    ASSERT_STR_EQ(results[0].artist, "Hetero Artist");
    ASSERT_EQ(results[0].durationSec, 210);
}

TEST_CASE("Fetchers Adversarial", "YouTubeMusic: itemSectionRenderer Wrapping List Items") {
    std::string json = R"json({
      "contents": {
        "sectionListRenderer": {
          "contents": [
            {
              "itemSectionRenderer": {
                "contents": [
                  {
                    "musicResponsiveListItemRenderer": {
                      "flexColumns": [
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": { "runs": [ { "text": "Item Section Track" } ] }
                          }
                        },
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": {
                              "runs": [
                                { "text": "Song" },
                                { "text": " • " },
                                { "text": "Item Section Artist" },
                                { "text": " • " },
                                { "text": "2:45" }
                              ]
                            }
                          }
                        }
                      ],
                      "playlistItemData": { "videoId": "ITEM_SEC_01" }
                    }
                  }
                ]
              }
            }
          ]
        }
      }
    })json";

    auto results = FetchServices::ParseYouTubeMusicSearchJson(json);
    ASSERT_EQ(results.size(), 1);
    ASSERT_STR_EQ(results[0].title, "Item Section Track");
    ASSERT_STR_EQ(results[0].artist, "Item Section Artist");
    ASSERT_EQ(results[0].durationSec, 165);
}

// ============================================================================
// ADVERSARIAL SUITE 2: DURATION FORMATS STRESS TESTING
// ============================================================================

TEST_CASE("Fetchers Adversarial", "Duration: Over-1-Hour Long Formats (1:02:15 and 2:10:05)") {
    std::string json = R"json({
      "contents": {
        "sectionListRenderer": {
          "contents": [
            {
              "musicShelfRenderer": {
                "contents": [
                  {
                    "musicResponsiveListItemRenderer": {
                      "flexColumns": [
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": { "runs": [ { "text": "Long Symphony" } ] }
                          }
                        },
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": {
                              "runs": [
                                { "text": "Song" },
                                { "text": " • " },
                                { "text": "Orchestra" },
                                { "text": " • " },
                                { "text": "1:02:15" }
                              ]
                            }
                          }
                        }
                      ],
                      "playlistItemData": { "videoId": "LONG_01" }
                    }
                  },
                  {
                    "musicResponsiveListItemRenderer": {
                      "flexColumns": [
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": { "runs": [ { "text": "Opera Part 2" } ] }
                          }
                        },
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": {
                              "runs": [
                                { "text": "Song" },
                                { "text": " • " },
                                { "text": "Opera Co" },
                                { "text": " • " },
                                { "text": "2:10:05" }
                              ]
                            }
                          }
                        }
                      ],
                      "playlistItemData": { "videoId": "LONG_02" }
                    }
                  }
                ]
              }
            }
          ]
        }
      }
    })json";

    auto results = FetchServices::ParseYouTubeMusicSearchJson(json);
    ASSERT_EQ(results.size(), 2);
    ASSERT_EQ(results[0].durationSec, 3735); // 1*3600 + 2*60 + 15
    ASSERT_EQ(results[1].durationSec, 7805); // 2*3600 + 10*60 + 5
}

TEST_CASE("Fetchers Adversarial", "Duration: Short Duration Under 1 Minute (0:45 and 0:08)") {
    std::string json = R"json({
      "contents": {
        "sectionListRenderer": {
          "contents": [
            {
              "musicShelfRenderer": {
                "contents": [
                  {
                    "musicResponsiveListItemRenderer": {
                      "flexColumns": [
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": { "runs": [ { "text": "Short Jingle" } ] }
                          }
                        },
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": {
                              "runs": [
                                { "text": "Song" },
                                { "text": " • " },
                                { "text": "Jingle Creator" },
                                { "text": " • " },
                                { "text": "0:45" }
                              ]
                            }
                          }
                        }
                      ],
                      "playlistItemData": { "videoId": "JINGLE_01" }
                    }
                  },
                  {
                    "musicResponsiveListItemRenderer": {
                      "flexColumns": [
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": { "runs": [ { "text": "Micro Stinger" } ] }
                          }
                        },
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": {
                              "runs": [
                                { "text": "Song" },
                                { "text": " • " },
                                { "text": "Stinger Artist" },
                                { "text": " • " },
                                { "text": "0:08" }
                              ]
                            }
                          }
                        }
                      ],
                      "playlistItemData": { "videoId": "STINGER_01" }
                    }
                  }
                ]
              }
            }
          ]
        }
      }
    })json";

    auto results = FetchServices::ParseYouTubeMusicSearchJson(json);
    ASSERT_EQ(results.size(), 2);
    ASSERT_EQ(results[0].durationSec, 45);
    ASSERT_EQ(results[1].durationSec, 8);
}

TEST_CASE("Fetchers Adversarial", "Duration: Raw Seconds Token 45 vs ParseDurationMs Behavior") {
    // ParseDurationMs handles standalone single-part integer "45" as 45000 ms
    ASSERT_EQ(MetadataUtils::ParseDurationMs("45"), 45000);

    // In YouTube Music, UI always displays mm:ss or hh:mm:ss.
    // If a standalone number "45" appeared in metadata runs without colon,
    // verify it is not erroneously treated as duration (which would corrupt numbered artist or album names).
    std::string json = R"json({
      "contents": {
        "sectionListRenderer": {
          "contents": [
            {
              "musicShelfRenderer": {
                "contents": [
                  {
                    "musicResponsiveListItemRenderer": {
                      "flexColumns": [
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": { "runs": [ { "text": "Track by Number Artist" } ] }
                          }
                        },
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": {
                              "runs": [
                                { "text": "Song" },
                                { "text": " • " },
                                { "text": "45" },
                                { "text": " • " },
                                { "text": "Album 45" },
                                { "text": " • " },
                                { "text": "3:45" }
                              ]
                            }
                          }
                        }
                      ],
                      "playlistItemData": { "videoId": "NUM_01" }
                    }
                  }
                ]
              }
            }
          ]
        }
      }
    })json";

    auto results = FetchServices::ParseYouTubeMusicSearchJson(json);
    ASSERT_EQ(results.size(), 1);
    ASSERT_STR_EQ(results[0].artist, "45");
    ASSERT_STR_EQ(results[0].album, "Album 45");
    ASSERT_EQ(results[0].durationSec, 225); // "3:45", not 45
}

TEST_CASE("Fetchers Adversarial", "Duration: Empty, Non-Numeric, and LIVE Badge Resilience") {
    std::string json = R"json({
      "contents": {
        "sectionListRenderer": {
          "contents": [
            {
              "musicShelfRenderer": {
                "contents": [
                  {
                    "musicResponsiveListItemRenderer": {
                      "flexColumns": [
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": { "runs": [ { "text": "Live Stream Track" } ] }
                          }
                        },
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": {
                              "runs": [
                                { "text": "Song" },
                                { "text": " • " },
                                { "text": "Streamer" },
                                { "text": " • " },
                                { "text": "LIVE" }
                              ]
                            }
                          }
                        }
                      ],
                      "playlistItemData": { "videoId": "LIVE_01" }
                    }
                  },
                  {
                    "musicResponsiveListItemRenderer": {
                      "flexColumns": [
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": { "runs": [ { "text": "Empty Duration Track" } ] }
                          }
                        },
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": {
                              "runs": [
                                { "text": "Song" },
                                { "text": " • " },
                                { "text": "Ghost Artist" },
                                { "text": " • " },
                                { "text": "" }
                              ]
                            }
                          }
                        }
                      ],
                      "playlistItemData": { "videoId": "EMPTY_DUR_01" }
                    }
                  }
                ]
              }
            }
          ]
        }
      }
    })json";

    auto results = FetchServices::ParseYouTubeMusicSearchJson(json);
    ASSERT_EQ(results.size(), 2);
    ASSERT_STR_EQ(results[0].title, "Live Stream Track");
    ASSERT_STR_EQ(results[0].artist, "Streamer");
    ASSERT_EQ(results[0].durationSec, 0); // Non-numeric LIVE does not crash or corrupt durationSec

    ASSERT_STR_EQ(results[1].title, "Empty Duration Track");
    ASSERT_STR_EQ(results[1].artist, "Ghost Artist");
    ASSERT_EQ(results[1].durationSec, 0);
}

// ============================================================================
// ADVERSARIAL SUITE 3: COVER ART EXTRACTION & PLACEHOLDER HASHING
// ============================================================================

TEST_CASE("Fetchers Adversarial", "CoverArt: LastFm Star Placeholder Hash Rejection (2a96cbd8b46e442fc41c2b86b821562f)") {
    // When Last.fm returns the generic star placeholder image across all resolutions,
    // coverUrl must be rejected and set to empty string.
    std::string json = R"json({
      "results": {
        "trackmatches": {
          "track": [
            {
              "name": "Obscure Single",
              "artist": "Unknown Underground",
              "url": "https://www.last.fm/music/test",
              "image": [
                { "#text": "https://lastfm.freetls.fastly.net/i/u/34s/2a96cbd8b46e442fc41c2b86b821562f.png", "size": "small" },
                { "#text": "https://lastfm.freetls.fastly.net/i/u/64s/2a96cbd8b46e442fc41c2b86b821562f.png", "size": "medium" },
                { "#text": "https://lastfm.freetls.fastly.net/i/u/174s/2a96cbd8b46e442fc41c2b86b821562f.png", "size": "large" },
                { "#text": "https://lastfm.freetls.fastly.net/i/u/300x300/2a96cbd8b46e442fc41c2b86b821562f.png", "size": "extralarge" }
              ]
            }
          ]
        }
      }
    })json";

    auto tracks = FetchServices::ParseLastFmTrackSearchJson(json);
    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(tracks[0].title, "Obscure Single");
    ASSERT_STR_EQ(tracks[0].coverUrl, ""); // Star placeholder hash rejected!
}

TEST_CASE("Fetchers Adversarial", "CoverArt: LastFm Fallback to Real Large Cover When ExtraLarge is Placeholder") {
    // If extralarge is placeholder but large has a valid artwork URL
    std::string json = R"json({
      "results": {
        "trackmatches": {
          "track": [
            {
              "name": "Partial Artwork Track",
              "artist": "Indie Band",
              "image": [
                { "#text": "https://lastfm.freetls.fastly.net/i/u/174s/real_large_artwork.png", "size": "large" },
                { "#text": "https://lastfm.freetls.fastly.net/i/u/300x300/2a96cbd8b46e442fc41c2b86b821562f.png", "size": "extralarge" }
              ]
            }
          ]
        }
      }
    })json";

    auto tracks = FetchServices::ParseLastFmTrackSearchJson(json);
    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(tracks[0].coverUrl, "https://lastfm.freetls.fastly.net/i/u/174s/real_large_artwork.png");
}

TEST_CASE("Fetchers Adversarial", "CoverArt: YouTubeMusic Highest Resolution Thumbnail Selection") {
    // Shuffled resolutions with widths: 60, 1080, 226, 544
    std::string json = R"json({
      "contents": {
        "sectionListRenderer": {
          "contents": [
            {
              "musicShelfRenderer": {
                "contents": [
                  {
                    "musicResponsiveListItemRenderer": {
                      "flexColumns": [
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": { "runs": [ { "text": "Resolution Track" } ] }
                          }
                        },
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": {
                              "runs": [
                                { "text": "Song" },
                                { "text": " • " },
                                { "text": "HD Artist" },
                                { "text": " • " },
                                { "text": "3:00" }
                              ]
                            }
                          }
                        }
                      ],
                      "thumbnail": {
                        "musicThumbnailRenderer": {
                          "thumbnail": {
                            "thumbnails": [
                              { "url": "https://lh3.googleusercontent.com/w60.jpg", "width": 60, "height": 60 },
                              { "url": "https://lh3.googleusercontent.com/w1080.jpg", "width": 1080, "height": 1080 },
                              { "url": "https://lh3.googleusercontent.com/w226.jpg", "width": 226, "height": 226 },
                              { "url": "https://lh3.googleusercontent.com/w544.jpg", "width": 544, "height": 544 }
                            ]
                          }
                        }
                      },
                      "playlistItemData": { "videoId": "HD_01" }
                    }
                  }
                ]
              }
            }
          ]
        }
      }
    })json";

    auto results = FetchServices::ParseYouTubeMusicSearchJson(json);
    ASSERT_EQ(results.size(), 1);
    ASSERT_STR_EQ(results[0].coverUrl, "https://lh3.googleusercontent.com/w1080.jpg");
}

// ============================================================================
// ADVERSARIAL SUITE 4: FALLBACK & UNFILTERED SEARCH MECHANISM
// ============================================================================

TEST_CASE("Fetchers Adversarial", "Fallback: Unfiltered Search Mixed Categories (Songs, Videos, Community Uploads)") {
    // In an unfiltered search, YouTube Music returns multiple shelves.
    // Songs and community uploads with artists and durations must be parsed.
    std::string json = R"json({
      "contents": {
        "tabbedSearchResultsRenderer": {
          "tabs": [
            {
              "tabRenderer": {
                "content": {
                  "sectionListRenderer": {
                    "contents": [
                      {
                        "musicCardShelfRenderer": {
                          "title": {
                            "runs": [ { "text": "Niche Underground Track" } ]
                          },
                          "subtitle": {
                            "runs": [
                              { "text": "Song" },
                              { "text": " • " },
                              { "text": "Niche Artist" },
                              { "text": " • " },
                              { "text": "4:20" }
                            ]
                          },
                          "thumbnail": {
                            "musicThumbnailRenderer": {
                              "thumbnail": {
                                "thumbnails": [
                                  { "url": "https://lh3.googleusercontent.com/card.jpg", "width": 544 }
                                ]
                              }
                            }
                          }
                        }
                      },
                      {
                        "musicShelfRenderer": {
                          "contents": [
                            {
                              "musicResponsiveListItemRenderer": {
                                "flexColumns": [
                                  {
                                    "musicResponsiveListItemFlexColumnRenderer": {
                                      "text": { "runs": [ { "text": "Community Remix Upload" } ] }
                                    }
                                  },
                                  {
                                    "musicResponsiveListItemFlexColumnRenderer": {
                                      "text": {
                                        "runs": [
                                          { "text": "Video" },
                                          { "text": " • " },
                                          { "text": "Remixer" },
                                          { "text": " • " },
                                          { "text": "5:30" }
                                        ]
                                      }
                                    }
                                  }
                                ],
                                "playlistItemData": { "videoId": "REMIX_COMMUNITY_01" }
                              }
                            }
                          ]
                        }
                      }
                    ]
                  }
                }
              }
            }
          ]
        }
      }
    })json";

    auto results = FetchServices::ParseYouTubeMusicSearchJson(json);
    ASSERT_EQ(results.size(), 2);

    ASSERT_STR_EQ(results[0].title, "Niche Underground Track");
    ASSERT_STR_EQ(results[0].artist, "Niche Artist");
    ASSERT_EQ(results[0].durationSec, 260);

    ASSERT_STR_EQ(results[1].title, "Community Remix Upload");
    ASSERT_STR_EQ(results[1].artist, "Remixer");
    ASSERT_EQ(results[1].durationSec, 330);
    ASSERT_STR_EQ(results[1].videoId, "REMIX_COMMUNITY_01");
}

TEST_CASE("Fetchers Adversarial", "Fallback: EscapeJsonString for Complex Query Strings") {
    // Tests that EscapeJsonString prevents payload breakage when quotes, slashes, and control chars are in query
    std::string tricky = "Track \"With Quotes\" & \\Backslashes\\ and \tTabs\n";
    std::string escaped = FetchServices::EscapeJsonString(tricky);

    // Verify raw quotes and unescaped backslashes are sanitized
    ASSERT_TRUE(escaped.find("\\\"With Quotes\\\"") != std::string::npos);
    ASSERT_TRUE(escaped.find("\\\\Backslashes\\\\") != std::string::npos);
    ASSERT_TRUE(escaped.find("\\tTabs\\n") != std::string::npos);

    // Verify constructed payload parses with IsJsonWellFormed
    std::string payload = "{\"query\":\"" + escaped + "\"}";
    ASSERT_TRUE(FetchServices::IsJsonWellFormed(payload));
}

TEST_CASE("Fetchers Adversarial", "YouTubeMusic: Four Separate Flex Columns (Title, Artist, Album, Duration)") {
    // Some desktop search layouts split metadata across separate flex columns
    std::string json = R"json({
      "contents": {
        "sectionListRenderer": {
          "contents": [
            {
              "musicShelfRenderer": {
                "contents": [
                  {
                    "musicResponsiveListItemRenderer": {
                      "flexColumns": [
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": { "runs": [ { "text": "Separate Column Track" } ] }
                          }
                        },
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": {
                              "runs": [
                                {
                                  "text": "Multi Column Artist",
                                  "navigationEndpoint": {
                                    "browseEndpoint": {
                                      "browseId": "UC_ARTIST_CHANNEL",
                                      "browseEndpointContextSupportedConfigs": {
                                        "browseEndpointContextMusicConfig": {
                                          "pageType": "MUSIC_PAGE_TYPE_ARTIST"
                                        }
                                      }
                                    }
                                  }
                                }
                              ]
                            }
                          }
                        },
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": {
                              "runs": [
                                {
                                  "text": "Multi Column Album",
                                  "navigationEndpoint": {
                                    "browseEndpoint": {
                                      "browseId": "MPRE_ALBUM_ID",
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
                        },
                        {
                          "musicResponsiveListItemFlexColumnRenderer": {
                            "text": { "runs": [ { "text": "3:45" } ] }
                          }
                        }
                      ],
                      "playlistItemData": { "videoId": "MULTI_COL_01" }
                    }
                  }
                ]
              }
            }
          ]
        }
      }
    })json";

    auto results = FetchServices::ParseYouTubeMusicSearchJson(json);
    ASSERT_EQ(results.size(), 1);
    ASSERT_STR_EQ(results[0].title, "Separate Column Track");
    ASSERT_STR_EQ(results[0].artist, "Multi Column Artist");
    ASSERT_STR_EQ(results[0].album, "Multi Column Album");
    ASSERT_STR_EQ(results[0].videoId, "MULTI_COL_01");
    ASSERT_EQ(results[0].durationSec, 225);
}

TEST_CASE("Fetchers Adversarial", "YouTubeMusic: Standalone musicCardShelfRenderer Without Contents") {
    std::string json = R"json({
      "contents": {
        "sectionListRenderer": {
          "contents": [
            {
              "musicCardShelfRenderer": {
                "title": {
                  "runs": [ { "text": "Solo Card Song" } ]
                },
                "subtitle": {
                  "runs": [
                    { "text": "Song" },
                    { "text": " • " },
                    { "text": "Solo Artist" },
                    { "text": " • " },
                    { "text": "Single EP" },
                    { "text": " • " },
                    { "text": "1:20" }
                  ]
                }
              }
            }
          ]
        }
      }
    })json";

    auto results = FetchServices::ParseYouTubeMusicSearchJson(json);
    ASSERT_EQ(results.size(), 1);
    ASSERT_STR_EQ(results[0].title, "Solo Card Song");
    ASSERT_STR_EQ(results[0].artist, "Solo Artist");
    ASSERT_STR_EQ(results[0].album, "Single EP");
    ASSERT_EQ(results[0].durationSec, 80);
}

TEST_CASE("Fetchers Adversarial", "CoverArt: LastFm Mega Placeholder Fallback to ExtraLarge") {
    // Mega image has placeholder hash, extralarge has legitimate artwork
    std::string json = R"json({
      "results": {
        "trackmatches": {
          "track": [
            {
              "name": "Mega Fallback",
              "artist": "Artist X",
              "image": [
                { "#text": "https://example.com/xl_valid.png", "size": "extralarge" },
                { "#text": "https://lastfm.freetls.fastly.net/i/u/300x300/2a96cbd8b46e442fc41c2b86b821562f.png", "size": "mega" }
              ]
            }
          ]
        }
      }
    })json";

    auto tracks = FetchServices::ParseLastFmTrackSearchJson(json);
    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(tracks[0].coverUrl, "https://example.com/xl_valid.png");
}

TEST_CASE("Fetchers Adversarial", "Duration: ParseDurationMs Extended Delimiters and Malformed Strings") {
    // Exact standard durations
    ASSERT_EQ(MetadataUtils::ParseDurationMs("3:45"), 225000);
    ASSERT_EQ(MetadataUtils::ParseDurationMs("03:45"), 225000);
    ASSERT_EQ(MetadataUtils::ParseDurationMs("1:02:15"), 3735000);

    // Malformed non-numeric or extra segments
    ASSERT_EQ(MetadataUtils::ParseDurationMs("1:02:15:30"), 0); // 4 parts -> 0
    ASSERT_EQ(MetadataUtils::ParseDurationMs("3:XX"), 3000);    // partial parse "3" -> 3 sec
    ASSERT_EQ(MetadataUtils::ParseDurationMs("LIVE STREAM"), 0);
    ASSERT_EQ(MetadataUtils::ParseDurationMs("   "), 0);
}

