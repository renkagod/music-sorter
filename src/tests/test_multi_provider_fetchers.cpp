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
// TIER 1: FEATURE COVERAGE (Core Functional Requirements)
// ============================================================================

TEST_CASE("Multi-Provider Fetchers", "LastFm: Parse track.search Standard Response") {
    std::string mockJson = R"json({
      "results": {
        "opensearch:totalResults": "2",
        "trackmatches": {
          "track": [
            {
              "name": "One More Time",
              "artist": "Daft Punk",
              "url": "https://www.last.fm/music/Daft+Punk/_/One+More+Time",
              "listeners": "2345678",
              "image": [
                { "#text": "https://lastfm.freetls.fastly.net/i/u/34s/small.png", "size": "small" },
                { "#text": "https://lastfm.freetls.fastly.net/i/u/64s/medium.png", "size": "medium" },
                { "#text": "https://lastfm.freetls.fastly.net/i/u/174s/large.png", "size": "large" },
                { "#text": "https://lastfm.freetls.fastly.net/i/u/300x300/extralarge.png", "size": "extralarge" }
              ],
              "mbid": "6c45eb0a-8671-460d-a7ab-b78809ec0a38"
            },
            {
              "name": "One More Time (Short Radio Edit)",
              "artist": "Daft Punk",
              "url": "https://www.last.fm/music/Daft+Punk/_/One+More+Time+(Short+Radio+Edit)",
              "listeners": "45678",
              "image": [
                { "#text": "https://lastfm.freetls.fastly.net/i/u/34s/small2.png", "size": "small" },
                { "#text": "https://lastfm.freetls.fastly.net/i/u/300x300/extralarge2.png", "size": "extralarge" }
              ]
            }
          ]
        }
      }
    })json";

    auto tracks = FetchServices::ParseLastFmTrackSearchJson(mockJson);
    ASSERT_EQ(tracks.size(), 2);

    ASSERT_STR_EQ(tracks[0].title, "One More Time");
    ASSERT_STR_EQ(tracks[0].artist, "Daft Punk");
    ASSERT_STR_EQ(tracks[0].coverUrl, "https://lastfm.freetls.fastly.net/i/u/300x300/extralarge.png");
    ASSERT_STR_EQ(tracks[0].url, "https://www.last.fm/music/Daft+Punk/_/One+More+Time");

    ASSERT_STR_EQ(tracks[1].title, "One More Time (Short Radio Edit)");
    ASSERT_STR_EQ(tracks[1].artist, "Daft Punk");
    ASSERT_STR_EQ(tracks[1].coverUrl, "https://lastfm.freetls.fastly.net/i/u/300x300/extralarge2.png");
}

TEST_CASE("Multi-Provider Fetchers", "LastFm: Parse track.search Single Object (Non-Array)") {
    std::string mockJson = R"json({
      "results": {
        "opensearch:totalResults": "1",
        "trackmatches": {
          "track": {
            "name": "Harder, Better, Faster, Stronger",
            "artist": "Daft Punk",
            "url": "https://www.last.fm/music/Daft+Punk/_/Harder,+Better,+Faster,+Stronger",
            "listeners": "1890000",
            "image": [
              { "#text": "https://lastfm.freetls.fastly.net/i/u/300x300/hbfs.png", "size": "extralarge" }
            ]
          }
        }
      }
    })json";

    auto tracks = FetchServices::ParseLastFmTrackSearchJson(mockJson);
    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(tracks[0].title, "Harder, Better, Faster, Stronger");
    ASSERT_STR_EQ(tracks[0].artist, "Daft Punk");
    ASSERT_STR_EQ(tracks[0].coverUrl, "https://lastfm.freetls.fastly.net/i/u/300x300/hbfs.png");
}

TEST_CASE("Multi-Provider Fetchers", "LastFm: Parse album.getInfo Standard Response") {
    std::string mockJson = R"json({
      "album": {
        "name": "Discovery",
        "artist": "Daft Punk",
        "url": "https://www.last.fm/music/Daft+Punk/Discovery",
        "image": [
          { "#text": "https://lastfm.freetls.fastly.net/i/u/34s/disc_s.png", "size": "small" },
          { "#text": "https://lastfm.freetls.fastly.net/i/u/174s/disc_l.png", "size": "large" },
          { "#text": "https://lastfm.freetls.fastly.net/i/u/300x300/discovery_cover.png", "size": "extralarge" }
        ],
        "tracks": {
          "track": [
            {
              "name": "One More Time",
              "duration": "320",
              "@attr": { "rank": "1" },
              "artist": { "name": "Daft Punk" }
            },
            {
              "name": "Aerodynamic",
              "duration": "207",
              "@attr": { "rank": "2" },
              "artist": { "name": "Daft Punk" }
            },
            {
              "name": "Digital Love",
              "duration": "301",
              "@attr": { "rank": "3" },
              "artist": { "name": "Daft Punk" }
            }
          ]
        },
        "wiki": {
          "published": "12 Mar 2001, 00:00",
          "summary": "Discovery is Daft Punk second studio album."
        }
      }
    })json";

    LastFmAlbumInfo info;
    bool ok = FetchServices::ParseLastFmAlbumInfoJson(mockJson, info);
    ASSERT_TRUE(ok);
    ASSERT_STR_EQ(info.album, "Discovery");
    ASSERT_STR_EQ(info.artist, "Daft Punk");
    ASSERT_STR_EQ(info.releaseDate, "12 Mar 2001, 00:00");
    ASSERT_STR_EQ(info.coverUrl, "https://lastfm.freetls.fastly.net/i/u/300x300/discovery_cover.png");
    ASSERT_EQ(info.tracklist.size(), 3);
    ASSERT_STR_EQ(info.tracklist[0], "One More Time");
    ASSERT_STR_EQ(info.tracklist[1], "Aerodynamic");
    ASSERT_STR_EQ(info.tracklist[2], "Digital Love");
}

TEST_CASE("Multi-Provider Fetchers", "LastFm: Parse album.getInfo Single Track Album") {
    std::string mockJson = R"json({
      "album": {
        "name": "Single Release",
        "artist": "Solo Artist",
        "tracks": {
          "track": {
            "name": "Lone Track",
            "duration": "180"
          }
        }
      }
    })json";

    LastFmAlbumInfo info;
    bool ok = FetchServices::ParseLastFmAlbumInfoJson(mockJson, info);
    ASSERT_TRUE(ok);
    ASSERT_STR_EQ(info.album, "Single Release");
    ASSERT_STR_EQ(info.artist, "Solo Artist");
    ASSERT_EQ(info.tracklist.size(), 1);
    ASSERT_STR_EQ(info.tracklist[0], "Lone Track");
}

TEST_CASE("Multi-Provider Fetchers", "LastFm: Image Size Selection Priority") {
    std::string mockJson = R"json({
      "results": {
        "trackmatches": {
          "track": [
            {
              "name": "Image Priority Test",
              "artist": "Test Artist",
              "image": [
                { "#text": "https://example.com/medium.png", "size": "medium" },
                { "#text": "https://example.com/small.png", "size": "small" },
                { "#text": "https://example.com/mega.png", "size": "mega" },
                { "#text": "https://example.com/large.png", "size": "large" }
              ]
            }
          ]
        }
      }
    })json";

    auto tracks = FetchServices::ParseLastFmTrackSearchJson(mockJson);
    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(tracks[0].coverUrl, "https://example.com/mega.png");
}

TEST_CASE("Multi-Provider Fetchers", "YouTubeMusic: Parse WEB_REMIX Standard Song Result") {
    std::string mockJson = R"json({
      "contents": {
        "tabbedSearchResultsRenderer": {
          "tabs": [
            {
              "tabRenderer": {
                "content": {
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
                                      "text": {
                                        "runs": [
                                          {
                                            "text": "Soulless 4",
                                            "navigationEndpoint": {
                                              "watchEndpoint": { "videoId": "SLS4_dQw4w9W" }
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
                                          { "text": "Song" },
                                          { "text": " • " },
                                          { "text": "ExileLord" },
                                          { "text": " • " },
                                          { "text": "Soulless Collection" },
                                          { "text": " • " },
                                          { "text": "12:34" }
                                        ]
                                      }
                                    }
                                  }
                                ],
                                "thumbnail": {
                                  "musicThumbnailRenderer": {
                                    "thumbnail": {
                                      "thumbnails": [
                                        { "url": "https://lh3.googleusercontent.com/small.jpg", "width": 60, "height": 60 },
                                        { "url": "https://lh3.googleusercontent.com/large.jpg", "width": 544, "height": 544 }
                                      ]
                                    }
                                  }
                                },
                                "playlistItemData": { "videoId": "SLS4_dQw4w9W" }
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

    auto tracks = FetchServices::ParseYouTubeMusicSearchJson(mockJson);
    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(tracks[0].title, "Soulless 4");
    ASSERT_STR_EQ(tracks[0].artist, "ExileLord");
    ASSERT_STR_EQ(tracks[0].album, "Soulless Collection");
    ASSERT_STR_EQ(tracks[0].videoId, "SLS4_dQw4w9W");
    ASSERT_EQ(tracks[0].durationSec, 754);
    ASSERT_STR_EQ(tracks[0].coverUrl, "https://lh3.googleusercontent.com/large.jpg");
}

TEST_CASE("Multi-Provider Fetchers", "YouTubeMusic: Parse Single Without Album") {
    std::string mockJson = R"json({
      "contents": {
        "tabbedSearchResultsRenderer": {
          "tabs": [
            {
              "tabRenderer": {
                "content": {
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
                                      "text": {
                                        "runs": [
                                          { "text": "Soulless 5" }
                                        ]
                                      }
                                    }
                                  },
                                  {
                                    "musicResponsiveListItemFlexColumnRenderer": {
                                      "text": {
                                        "runs": [
                                          { "text": "Song" },
                                          { "text": " • " },
                                          { "text": "ExileLord" },
                                          { "text": " • " },
                                          { "text": "17:10" }
                                        ]
                                      }
                                    }
                                  }
                                ],
                                "playlistItemData": { "videoId": "SLS5_abc" }
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

    auto tracks = FetchServices::ParseYouTubeMusicSearchJson(mockJson);
    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(tracks[0].title, "Soulless 5");
    ASSERT_STR_EQ(tracks[0].artist, "ExileLord");
    ASSERT_STR_EQ(tracks[0].album, "");
    ASSERT_EQ(tracks[0].durationSec, 1030);
}

TEST_CASE("Multi-Provider Fetchers", "YouTubeMusic: Thumbnail Selection Highest Resolution") {
    std::string mockJson = R"json({
      "contents": {
        "tabbedSearchResultsRenderer": {
          "tabs": [
            {
              "tabRenderer": {
                "content": {
                  "sectionListRenderer": {
                    "contents": [
                      {
                        "musicShelfRenderer": {
                          "contents": [
                            {
                              "musicResponsiveListItemRenderer": {
                                "flexColumns": [
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Test" } ] } } },
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Song" }, { "text": " • " }, { "text": "Artist" }, { "text": " • " }, { "text": "3:00" } ] } } }
                                ],
                                "thumbnail": {
                                  "musicThumbnailRenderer": {
                                    "thumbnail": {
                                      "thumbnails": [
                                        { "url": "https://example.com/s.jpg", "width": 60, "height": 60 },
                                        { "url": "https://example.com/hq.jpg", "width": 544, "height": 544 },
                                        { "url": "https://example.com/m.jpg", "width": 120, "height": 120 }
                                      ]
                                    }
                                  }
                                }
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

    auto tracks = FetchServices::ParseYouTubeMusicSearchJson(mockJson);
    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(tracks[0].coverUrl, "https://example.com/hq.jpg");
}

TEST_CASE("Multi-Provider Fetchers", "YouTubeMusic: VideoId Extraction Sources") {
    std::string mockJson = R"json({
      "contents": {
        "tabbedSearchResultsRenderer": {
          "tabs": [
            {
              "tabRenderer": {
                "content": {
                  "sectionListRenderer": {
                    "contents": [
                      {
                        "musicShelfRenderer": {
                          "contents": [
                            {
                              "musicResponsiveListItemRenderer": {
                                "flexColumns": [
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Track 1" } ] } } },
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Song" }, { "text": " • " }, { "text": "Art" }, { "text": " • " }, { "text": "1:00" } ] } } }
                                ],
                                "playlistItemData": { "videoId": "ID_FROM_PLAYLIST" }
                              }
                            },
                            {
                              "musicResponsiveListItemRenderer": {
                                "flexColumns": [
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Track 2", "navigationEndpoint": { "watchEndpoint": { "videoId": "ID_FROM_WATCH" } } } ] } } },
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Song" }, { "text": " • " }, { "text": "Art" }, { "text": " • " }, { "text": "2:00" } ] } } }
                                ]
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

    auto tracks = FetchServices::ParseYouTubeMusicSearchJson(mockJson);
    ASSERT_EQ(tracks.size(), 2);
    ASSERT_STR_EQ(tracks[0].videoId, "ID_FROM_PLAYLIST");
    ASSERT_STR_EQ(tracks[1].videoId, "ID_FROM_WATCH");
}

TEST_CASE("Multi-Provider Fetchers", "YouTubeMusic: Multiple Artists Parsing") {
    std::string mockJson = R"json({
      "contents": {
        "tabbedSearchResultsRenderer": {
          "tabs": [
            {
              "tabRenderer": {
                "content": {
                  "sectionListRenderer": {
                    "contents": [
                      {
                        "musicShelfRenderer": {
                          "contents": [
                            {
                              "musicResponsiveListItemRenderer": {
                                "flexColumns": [
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Get Lucky" } ] } } },
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Song" }, { "text": " • " }, { "text": "Daft Punk, Pharrell Williams" }, { "text": " • " }, { "text": "Random Access Memories" }, { "text": " • " }, { "text": "4:08" } ] } } }
                                ],
                                "playlistItemData": { "videoId": "5NV6Rdv1a3I" }
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

    auto tracks = FetchServices::ParseYouTubeMusicSearchJson(mockJson);
    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(tracks[0].title, "Get Lucky");
    ASSERT_STR_EQ(tracks[0].artist, "Daft Punk, Pharrell Williams");
    ASSERT_STR_EQ(tracks[0].album, "Random Access Memories");
    ASSERT_EQ(tracks[0].durationSec, 248);
}

// ============================================================================
// TIER 2: BOUNDARY & CORNER CASES (Error Handling & Edge Cases)
// ============================================================================

TEST_CASE("Multi-Provider Fetchers", "LastFm: Error Response - Track Not Found (Error 6)") {
    std::string notFoundJson = R"json({
      "error": 6,
      "message": "The track you supplied cannot be found",
      "links": []
    })json";

    auto tracks = FetchServices::ParseLastFmTrackSearchJson(notFoundJson);
    ASSERT_TRUE(tracks.empty());
}

TEST_CASE("Multi-Provider Fetchers", "LastFm: Error Response - Invalid API Key (Error 10)") {
    std::string invalidKeyJson = R"json({
      "error": 10,
      "message": "Invalid API key - You must be granted a valid key to use this service"
    })json";

    auto tracks = FetchServices::ParseLastFmTrackSearchJson(invalidKeyJson);
    ASSERT_TRUE(tracks.empty());

    LastFmAlbumInfo info;
    bool ok = FetchServices::ParseLastFmAlbumInfoJson(invalidKeyJson, info);
    ASSERT_FALSE(ok);
}

TEST_CASE("Multi-Provider Fetchers", "LastFm: Malformed and Truncated JSON") {
    std::string malformed = R"json({ "results": { "trackmatches": { "track": [ { "name": "Broken )json";
    auto tracks = FetchServices::ParseLastFmTrackSearchJson(malformed);
    ASSERT_TRUE(tracks.empty());

    LastFmAlbumInfo info;
    bool ok = FetchServices::ParseLastFmAlbumInfoJson(malformed, info);
    ASSERT_FALSE(ok);
}

TEST_CASE("Multi-Provider Fetchers", "LastFm: Empty and Whitespace JSON") {
    ASSERT_TRUE(FetchServices::ParseLastFmTrackSearchJson("").empty());
    ASSERT_TRUE(FetchServices::ParseLastFmTrackSearchJson("   \t\n  ").empty());
    ASSERT_TRUE(FetchServices::ParseLastFmTrackSearchJson("{}").empty());

    LastFmAlbumInfo info;
    ASSERT_FALSE(FetchServices::ParseLastFmAlbumInfoJson("", info));
    ASSERT_FALSE(FetchServices::ParseLastFmAlbumInfoJson("{}", info));
}

TEST_CASE("Multi-Provider Fetchers", "LastFm: Missing Fields and Null Elements") {
    std::string mockJson = R"json({
      "results": {
        "trackmatches": {
          "track": [
            {
              "name": "Sparse Track",
              "artist": "",
              "url": null,
              "image": []
            }
          ]
        }
      }
    })json";

    auto tracks = FetchServices::ParseLastFmTrackSearchJson(mockJson);
    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(tracks[0].title, "Sparse Track");
    ASSERT_STR_EQ(tracks[0].artist, "");
    ASSERT_STR_EQ(tracks[0].coverUrl, "");
}

TEST_CASE("Multi-Provider Fetchers", "LastFm: Unicode Japanese and Special Characters") {
    std::string mockJson = R"json({
      "results": {
        "trackmatches": {
          "track": [
            {
              "name": "Bad Apple!! feat. nomico",
              "artist": "Alstroemeria Records",
              "url": "https://www.last.fm/music/Alstroemeria+Records/_/Bad+Apple!!+feat.+nomico"
            },
            {
              "name": "ナイト・オブ・ナイツ",
              "artist": "ビートまりお",
              "url": "https://www.last.fm/music/Beat+Mario"
            }
          ]
        }
      }
    })json";

    auto tracks = FetchServices::ParseLastFmTrackSearchJson(mockJson);
    ASSERT_EQ(tracks.size(), 2);
    ASSERT_STR_EQ(tracks[0].title, "Bad Apple!! feat. nomico");
    ASSERT_STR_EQ(tracks[1].title, "ナイト・オブ・ナイツ");
    ASSERT_STR_EQ(tracks[1].artist, "ビートまりお");
}

TEST_CASE("Multi-Provider Fetchers", "YouTubeMusic: Empty Search Results Array") {
    std::string mockJson = R"json({
      "contents": {
        "tabbedSearchResultsRenderer": {
          "tabs": [
            {
              "tabRenderer": {
                "content": {
                  "sectionListRenderer": {
                    "contents": []
                  }
                }
              }
            }
          ]
        }
      }
    })json";

    auto tracks = FetchServices::ParseYouTubeMusicSearchJson(mockJson);
    ASSERT_TRUE(tracks.empty());
}

TEST_CASE("Multi-Provider Fetchers", "YouTubeMusic: Malformed or Incomplete JSON") {
    std::string malformed = R"json({ "contents": { "tabbedSearchResultsRenderer": { )json";
    auto tracks = FetchServices::ParseYouTubeMusicSearchJson(malformed);
    ASSERT_TRUE(tracks.empty());
}

TEST_CASE("Multi-Provider Fetchers", "YouTubeMusic: Missing Flex Columns") {
    std::string mockJson = R"json({
      "contents": {
        "tabbedSearchResultsRenderer": {
          "tabs": [
            {
              "tabRenderer": {
                "content": {
                  "sectionListRenderer": {
                    "contents": [
                      {
                        "musicShelfRenderer": {
                          "contents": [
                            {
                              "musicResponsiveListItemRenderer": {
                                "flexColumns": []
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

    auto tracks = FetchServices::ParseYouTubeMusicSearchJson(mockJson);
    ASSERT_TRUE(tracks.empty());
}

TEST_CASE("Multi-Provider Fetchers", "YouTubeMusic: Non-Song Result Filtering") {
    std::string mockJson = R"json({
      "contents": {
        "tabbedSearchResultsRenderer": {
          "tabs": [
            {
              "tabRenderer": {
                "content": {
                  "sectionListRenderer": {
                    "contents": [
                      {
                        "musicShelfRenderer": {
                          "contents": [
                            {
                              "musicResponsiveListItemRenderer": {
                                "flexColumns": [
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Random Video" } ] } } },
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Video" }, { "text": " • " }, { "text": "1.2M views" } ] } } }
                                ]
                              }
                            },
                            {
                              "musicResponsiveListItemRenderer": {
                                "flexColumns": [
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Actual Song" } ] } } },
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Song" }, { "text": " • " }, { "text": "Real Artist" }, { "text": " • " }, { "text": "Album" }, { "text": " • " }, { "text": "3:20" } ] } } }
                                ],
                                "playlistItemData": { "videoId": "ACTUAL_ID" }
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

    auto tracks = FetchServices::ParseYouTubeMusicSearchJson(mockJson);
    ASSERT_GE(tracks.size(), 1);
    ASSERT_STR_EQ(tracks.back().title, "Actual Song");
    ASSERT_STR_EQ(tracks.back().artist, "Real Artist");
    ASSERT_EQ(tracks.back().durationSec, 200);
}

TEST_CASE("Multi-Provider Fetchers", "YouTubeMusic: Duration Formatting Edge Cases") {
    ASSERT_EQ(MetadataUtils::ParseDurationMs("1:02:15") / 1000, 3735);
    ASSERT_EQ(MetadataUtils::ParseDurationMs("45") / 1000, 45);
    ASSERT_EQ(MetadataUtils::ParseDurationMs("0:00") / 1000, 0);
    ASSERT_EQ(MetadataUtils::ParseDurationMs("LIVE") / 1000, 0);
    ASSERT_EQ(MetadataUtils::ParseDurationMs("") / 1000, 0);
}

TEST_CASE("Multi-Provider Fetchers", "YouTubeMusic: Empty and Whitespace JSON") {
    ASSERT_TRUE(FetchServices::ParseYouTubeMusicSearchJson("").empty());
    ASSERT_TRUE(FetchServices::ParseYouTubeMusicSearchJson("   ").empty());
    ASSERT_TRUE(FetchServices::ParseYouTubeMusicSearchJson("{}").empty());
}

// ============================================================================
// TIER 3: CROSS-FEATURE COMBINATIONS (Integration Pipeline)
// ============================================================================

TEST_CASE("Multi-Provider Fetchers", "Integration: LastFm Album Info to ValidateAlbumMatch (Pass)") {
    std::string mockJson = R"json({
      "album": {
        "name": "Discovery",
        "artist": "Daft Punk",
        "tracks": {
          "track": [
            { "name": "One More Time" },
            { "name": "Aerodynamic" },
            { "name": "Digital Love" },
            { "name": "Harder, Better, Faster, Stronger" },
            { "name": "Crescendolls" },
            { "name": "Nightvision" },
            { "name": "Superheroes" },
            { "name": "High Life" },
            { "name": "Something About Us" },
            { "name": "Voyager" },
            { "name": "Veridis Quo" },
            { "name": "Short Circuit" },
            { "name": "Face to Face" },
            { "name": "Too Long" }
          ]
        }
      }
    })json";

    LastFmAlbumInfo info;
    bool ok = FetchServices::ParseLastFmAlbumInfoJson(mockJson, info);
    ASSERT_TRUE(ok);

    std::vector<std::string> localTitles = {
        "One More Time", "Aerodynamic", "Digital Love", "Harder, Better, Faster, Stronger",
        "Crescendolls", "Nightvision", "Superheroes", "High Life",
        "Something About Us", "Voyager", "Veridis Quo", "Short Circuit",
        "Face to Face", "Too Long"
    };

    auto guardResult = ValidateAlbumMatch("Daft Punk", "Discovery", localTitles, info.artist, info.album, info.tracklist);
    ASSERT_TRUE(guardResult.passed);
    ASSERT_GE(guardResult.confidence, 0.85);
    ASSERT_NEAR(guardResult.artistSimilarity, 1.0, 0.01);
    ASSERT_NEAR(guardResult.tracklistOverlap, 1.0, 0.01);
}

TEST_CASE("Multi-Provider Fetchers", "Integration: LastFm Album Info to ValidateAlbumMatch (Mismatched Artist Rejected)") {
    std::string mockJson = R"json({
      "album": {
        "name": "TO SORT",
        "artist": "Macroblank",
        "tracks": {
          "track": [
            { "name": "Track 1" },
            { "name": "Track 2" }
          ]
        }
      }
    })json";

    LastFmAlbumInfo info;
    bool ok = FetchServices::ParseLastFmAlbumInfoJson(mockJson, info);
    ASSERT_TRUE(ok);

    std::vector<std::string> localTitles = { "Soulless 4", "Soulless 5" };
    auto guardResult = ValidateAlbumMatch("ExileLord", "TO SORT", localTitles, info.artist, info.album, info.tracklist);
    ASSERT_FALSE(guardResult.passed);
    ASSERT_LT(guardResult.confidence, 0.80);
    ASSERT_LT(guardResult.artistSimilarity, 0.35);
}

TEST_CASE("Multi-Provider Fetchers", "Integration: LastFm Album Info to ValidateAlbumMatch (Divergent Tracklist Rejected)") {
    std::string mockJson = R"json({
      "album": {
        "name": "Singles",
        "artist": "ExileLord",
        "tracks": {
          "track": [
            { "name": "Intro" },
            { "name": "Interlude" },
            { "name": "Outro" }
          ]
        }
      }
    })json";

    LastFmAlbumInfo info;
    bool ok = FetchServices::ParseLastFmAlbumInfoJson(mockJson, info);
    ASSERT_TRUE(ok);

    std::vector<std::string> localTitles = { "Soulless 4", "Megalodon", "Arm Breaker" };
    auto guardResult = ValidateAlbumMatch("ExileLord", "Singles", localTitles, info.artist, info.album, info.tracklist);
    ASSERT_FALSE(guardResult.passed);
    ASSERT_LT(guardResult.confidence, 0.80);
    ASSERT_NEAR(guardResult.tracklistOverlap, 0.0, 0.01);
}

TEST_CASE("Multi-Provider Fetchers", "Integration: LastFm Track to ValidateLyricMatch (Pass)") {
    std::string mockJson = R"json({
      "results": {
        "trackmatches": {
          "track": [
            { "name": "Bad Apple!!", "artist": "nomico" }
          ]
        }
      }
    })json";

    auto tracks = FetchServices::ParseLastFmTrackSearchJson(mockJson);
    ASSERT_EQ(tracks.size(), 1);

    bool lyricValid = ValidateLyricMatch(tracks[0].artist, tracks[0].title, "nomico", "Bad Apple!!");
    ASSERT_TRUE(lyricValid);
}

TEST_CASE("Multi-Provider Fetchers", "Integration: LastFm Track to ValidateLyricMatch (Unknown Artist Strictly Blocked)") {
    std::string mockJson = R"json({
      "results": {
        "trackmatches": {
          "track": [
            { "name": "Mysterious Track", "artist": "Unknown Artist" }
          ]
        }
      }
    })json";

    auto tracks = FetchServices::ParseLastFmTrackSearchJson(mockJson);
    ASSERT_EQ(tracks.size(), 1);

    bool lyricValid = ValidateLyricMatch(tracks[0].artist, tracks[0].title, "Random Vocalist", "Mysterious Track");
    ASSERT_FALSE(lyricValid);
}

TEST_CASE("Multi-Provider Fetchers", "Integration: YouTubeMusic Track to ValidateLyricMatch (Instrumental Rejected)") {
    std::string mockJson = R"json({
      "contents": {
        "tabbedSearchResultsRenderer": {
          "tabs": [
            {
              "tabRenderer": {
                "content": {
                  "sectionListRenderer": {
                    "contents": [
                      {
                        "musicShelfRenderer": {
                          "contents": [
                            {
                              "musicResponsiveListItemRenderer": {
                                "flexColumns": [
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Soulless 4 (Instrumental)" } ] } } },
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Song" }, { "text": " • " }, { "text": "ExileLord" }, { "text": " • " }, { "text": "12:34" } ] } } }
                                ]
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

    auto tracks = FetchServices::ParseYouTubeMusicSearchJson(mockJson);
    ASSERT_EQ(tracks.size(), 1);

    bool lyricValid = ValidateLyricMatch(tracks[0].artist, tracks[0].title, "ExileLord", "Soulless 4");
    ASSERT_FALSE(lyricValid);
}

TEST_CASE("Multi-Provider Fetchers", "Integration: Heuristic Filename to LastFm Track Match") {
    auto parsed = ParseFilenameHeuristic("01 - Daft Punk - One More Time");
    ASSERT_STR_EQ(parsed.artist, "Daft Punk");
    ASSERT_STR_EQ(parsed.title, "One More Time");
    ASSERT_EQ(parsed.trackNumber, 1);

    std::string mockJson = R"json({
      "results": {
        "trackmatches": {
          "track": [
            { "name": "One More Time", "artist": "Daft Punk", "url": "https://www.last.fm/music/Daft+Punk/_/One+More+Time" }
          ]
        }
      }
    })json";

    auto tracks = FetchServices::ParseLastFmTrackSearchJson(mockJson);
    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(tracks[0].title, parsed.title);
    ASSERT_STR_EQ(tracks[0].artist, parsed.artist);
}

// ============================================================================
// TIER 4: REAL-WORLD APPLICATION SCENARIOS
// ============================================================================

TEST_CASE("Multi-Provider Fetchers", "RealWorld: ExileLord Soulless 4 YouTube Music & Guardrail Pipeline") {
    auto parsed = ParseFilenameHeuristic("ExileLord - Soulless 4");
    ASSERT_STR_EQ(parsed.artist, "ExileLord");
    ASSERT_STR_EQ(parsed.title, "Soulless 4");

    std::string ytMockJson = R"json({
      "contents": {
        "tabbedSearchResultsRenderer": {
          "tabs": [
            {
              "tabRenderer": {
                "content": {
                  "sectionListRenderer": {
                    "contents": [
                      {
                        "musicShelfRenderer": {
                          "contents": [
                            {
                              "musicResponsiveListItemRenderer": {
                                "flexColumns": [
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Soulless 4" } ] } } },
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Song" }, { "text": " • " }, { "text": "ExileLord" }, { "text": " • " }, { "text": "12:34" } ] } } }
                                ],
                                "playlistItemData": { "videoId": "SLS4_NATIVE_ID" }
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

    auto ytTracks = FetchServices::ParseYouTubeMusicSearchJson(ytMockJson);
    ASSERT_EQ(ytTracks.size(), 1);
    ASSERT_STR_EQ(ytTracks[0].title, "Soulless 4");
    ASSERT_STR_EQ(ytTracks[0].artist, "ExileLord");
    ASSERT_STR_EQ(ytTracks[0].videoId, "SLS4_NATIVE_ID");

    std::vector<std::string> singleTrack = { "Soulless 4" };
    std::vector<std::string> falseAlbumTracks = { "Intro", "Soulless 4", "Outro", "Bonus" };
    auto guard = ValidateAlbumMatch("ExileLord", "TO SORT", singleTrack, "Macroblank", "Some Album", falseAlbumTracks);
    ASSERT_FALSE(guard.passed);

    bool lyricAllowed = ValidateLyricMatch("ExileLord", "Soulless 4", "Unknown Singer", "Soulless 4");
    ASSERT_FALSE(lyricAllowed);
}

TEST_CASE("Multi-Provider Fetchers", "RealWorld: Daft Punk Discovery 14 Tracks Complete Album Match") {
    std::string mockJson = R"json({
      "album": {
        "name": "Discovery",
        "artist": "Daft Punk",
        "wiki": { "published": "12 Mar 2001, 00:00" },
        "image": [
          { "#text": "https://lastfm.freetls.fastly.net/i/u/300x300/discovery.jpg", "size": "extralarge" }
        ],
        "tracks": {
          "track": [
            { "name": "One More Time" },
            { "name": "Aerodynamic" },
            { "name": "Digital Love" },
            { "name": "Harder, Better, Faster, Stronger" },
            { "name": "Crescendolls" },
            { "name": "Nightvision" },
            { "name": "Superheroes" },
            { "name": "High Life" },
            { "name": "Something About Us" },
            { "name": "Voyager" },
            { "name": "Veridis Quo" },
            { "name": "Short Circuit" },
            { "name": "Face to Face" },
            { "name": "Too Long" }
          ]
        }
      }
    })json";

    LastFmAlbumInfo info;
    bool ok = FetchServices::ParseLastFmAlbumInfoJson(mockJson, info);
    ASSERT_TRUE(ok);
    ASSERT_EQ(info.tracklist.size(), 14);

    std::vector<std::string> localTitles = {
        "One More Time", "Aerodynamic", "Digital Love", "Harder, Better, Faster, Stronger",
        "Crescendolls", "Nightvision", "Superheroes", "High Life",
        "Something About Us", "Voyager", "Veridis Quo", "Short Circuit",
        "Face to Face", "Too Long"
    };

    auto matchResult = ValidateAlbumMatch("Daft Punk", "Discovery", localTitles, info.artist, info.album, info.tracklist);
    ASSERT_TRUE(matchResult.passed);
    ASSERT_GE(matchResult.confidence, 0.90);
    ASSERT_NEAR(matchResult.artistSimilarity, 1.0, 0.001);
    ASSERT_NEAR(matchResult.tracklistOverlap, 1.0, 0.001);
}

TEST_CASE("Multi-Provider Fetchers", "RealWorld: Touhou Bad Apple Multi-Provider Consensus Input Consistency") {
    std::string lastFmJson = R"json({
      "results": {
        "trackmatches": {
          "track": [
            {
              "name": "Bad Apple!!",
              "artist": "nomico",
              "url": "https://www.last.fm/music/nomico/_/Bad+Apple!!"
            }
          ]
        }
      }
    })json";

    std::string ytMusicJson = R"json({
      "contents": {
        "tabbedSearchResultsRenderer": {
          "tabs": [
            {
              "tabRenderer": {
                "content": {
                  "sectionListRenderer": {
                    "contents": [
                      {
                        "musicShelfRenderer": {
                          "contents": [
                            {
                              "musicResponsiveListItemRenderer": {
                                "flexColumns": [
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Bad Apple!!" } ] } } },
                                  { "musicResponsiveListItemFlexColumnRenderer": { "text": { "runs": [ { "text": "Song" }, { "text": " • " }, { "text": "nomico" }, { "text": " • " }, { "text": "Lovelight" }, { "text": " • " }, { "text": "5:18" } ] } } }
                                ],
                                "playlistItemData": { "videoId": "FtutLA63Cp8" }
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

    auto lastFmTracks = FetchServices::ParseLastFmTrackSearchJson(lastFmJson);
    auto ytTracks = FetchServices::ParseYouTubeMusicSearchJson(ytMusicJson);

    ASSERT_EQ(lastFmTracks.size(), 1);
    ASSERT_EQ(ytTracks.size(), 1);

    ASSERT_STR_EQ(lastFmTracks[0].title, ytTracks[0].title);
    ASSERT_STR_EQ(lastFmTracks[0].artist, ytTracks[0].artist);
    ASSERT_GE(ComputeStringSimilarity(lastFmTracks[0].artist, ytTracks[0].artist), 0.90);
}
