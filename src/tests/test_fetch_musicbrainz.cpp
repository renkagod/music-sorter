#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include "../include/MetadataUtils.hpp"

TEST_CASE("MusicBrainz Fetch", "Parse Release Group Search Response") {
    std::string json = R"json({
        "created": "2026-08-14T06:00:00.000Z",
        "count": 2,
        "offset": 0,
        "release-groups": [
            {
                "id": "41530960-93bb-40cb-a5e2-e1c6fa620f4c",
                "type-id": "f529b476-6e62-324f-b0aa-1f3e33d313fc",
                "score": 100,
                "title": "Lovelight",
                "first-release-date": "2007-05-20",
                "primary-type": "Album",
                "artist-credit": [
                    { "name": "Alstroemeria Records" }
                ]
            },
            {
                "id": "52640a71-84bb-40cb-b5e2-f2c6fa620f5d",
                "score": 85,
                "title": "For Your Pieces",
                "first-release-date": "2008-12-29",
                "artist-credit": [
                    { "name": "Alstroemeria Records" },
                    { "name": "Masayoshi Minoshima" }
                ]
            },
            {
                "id": "invalid-uuid-too-short",
                "score": 50,
                "title": "Invalid"
            }
        ]
    })json";

    auto candidates = FetchServices::ParseMusicBrainzReleaseGroups(json);
    ASSERT_EQ(candidates.size(), 2);

    ASSERT_STR_EQ(candidates[0].id, "41530960-93bb-40cb-a5e2-e1c6fa620f4c");
    ASSERT_STR_EQ(candidates[0].title, "Lovelight");
    ASSERT_STR_EQ(candidates[0].firstReleaseDate, "2007-05-20");
    ASSERT_STR_EQ(candidates[0].artistCredit, "Alstroemeria Records");
    ASSERT_EQ(candidates[0].score, 100);

    ASSERT_STR_EQ(candidates[1].id, "52640a71-84bb-40cb-b5e2-f2c6fa620f5d");
    ASSERT_STR_EQ(candidates[1].title, "For Your Pieces");
    ASSERT_STR_EQ(candidates[1].firstReleaseDate, "2008-12-29");
    ASSERT_STR_EQ(candidates[1].artistCredit, "Alstroemeria Records, Masayoshi Minoshima");
    ASSERT_EQ(candidates[1].score, 85);
}

TEST_CASE("MusicBrainz Fetch", "Parse Release Tracks With Pseudo-Release Preference") {
    // JSON containing both an Official release (Japanese titles) and a Pseudo-Release (Romanized titles)
    std::string json = R"json({
        "releases": [
            {
                "id": "rel-official-1",
                "status": "Official",
                "date": "2007-05-20",
                "media": [
                    {
                        "position": 1,
                        "tracks": [
                            {
                                "position": 1,
                                "title": "Bad Apple!! feat. nomico (Official)",
                                "length": 318000,
                                "artist-credit": [ { "name": "nomico" } ]
                            }
                        ]
                    }
                ]
            },
            {
                "id": "rel-pseudo-2",
                "status": "Pseudo-Release",
                "date": "2007-05-20",
                "media": [
                    {
                        "position": 1,
                        "tracks": [
                            {
                                "position": 1,
                                "title": "Bad Apple!! feat. nomico (Romanized)",
                                "length": 318000,
                                "artist-credit": [ { "name": "nomico" } ]
                            },
                            {
                                "position": 2,
                                "title": "Dreaming (Romanized)",
                                "length": 245000,
                                "artist-credit": [ { "name": "Alstroemeria Records" } ]
                            }
                        ]
                    }
                ]
            }
        ]
    })json";

    std::string releaseDate;
    auto tracks = FetchServices::ParseMusicBrainzReleaseTracksJson(json, &releaseDate);

    ASSERT_EQ(tracks.size(), 2);
    ASSERT_STR_EQ(releaseDate, "2007-05-20");
    // Verifies Pseudo-Release was prioritized over Official
    ASSERT_STR_EQ(tracks[0].title, "Bad Apple!! feat. nomico (Romanized)");
    ASSERT_EQ(tracks[0].position, 1);
    ASSERT_EQ(tracks[0].lengthMs, 318000);
    ASSERT_STR_EQ(tracks[0].artist, "nomico");

    ASSERT_STR_EQ(tracks[1].title, "Dreaming (Romanized)");
    ASSERT_EQ(tracks[1].position, 2);
    ASSERT_EQ(tracks[1].lengthMs, 245000);
    ASSERT_STR_EQ(tracks[1].artist, "Alstroemeria Records");
}

TEST_CASE("MusicBrainz Fetch", "Parse Release Tracks Official Fallback") {
    std::string json = R"json({
        "releases": [
            {
                "id": "rel-official-only",
                "status": "Official",
                "date": "2010-08-14",
                "media": [
                    {
                        "position": 1,
                        "tracks": [
                            {
                                "position": 1,
                                "recording": { "title": "Necro-Fantasia", "length": 290000, "artist-credit": [ { "name": "ZUN" } ] }
                            }
                        ]
                    }
                ]
            }
        ]
    })json";

    std::string releaseDate;
    auto tracks = FetchServices::ParseMusicBrainzReleaseTracksJson(json, &releaseDate);

    ASSERT_EQ(tracks.size(), 1);
    ASSERT_STR_EQ(releaseDate, "2010-08-14");
    ASSERT_STR_EQ(tracks[0].title, "Necro-Fantasia");
    ASSERT_EQ(tracks[0].position, 1);
    ASSERT_EQ(tracks[0].lengthMs, 290000);
    ASSERT_STR_EQ(tracks[0].artist, "ZUN");
}

TEST_CASE("MusicBrainz Fetch", "Extract UUID from Various URL Formats") {
    std::string url1 = "https://musicbrainz.org/release/41530960-93bb-40cb-a5e2-e1c6fa620f4c";
    ASSERT_STR_EQ(FetchServices::ExtractMusicBrainzUuid(url1), "41530960-93bb-40cb-a5e2-e1c6fa620f4c");

    std::string url2 = "https://musicbrainz.org/release-group/52640A71-84BB-40CB-B5E2-F2C6FA620F5D?inc=recordings";
    ASSERT_STR_EQ(FetchServices::ExtractMusicBrainzUuid(url2), "52640A71-84BB-40CB-B5E2-F2C6FA620F5D");

    std::string rawUuid = "b3e34b3e-108b-4ef9-bfef-c081e7d9564c";
    ASSERT_STR_EQ(FetchServices::ExtractMusicBrainzUuid(rawUuid), "b3e34b3e-108b-4ef9-bfef-c081e7d9564c");

    std::string invalid = "https://example.com/albums/12345";
    ASSERT_STR_EQ(FetchServices::ExtractMusicBrainzUuid(invalid), "");
}

TEST_CASE("MusicBrainz Fetch", "Search Tier Lucene Query Formatting") {
    std::string artist = "dBu Music";
    std::string album = "Shisou [Historical Disc]";

    std::string cleanArtist = CleanMetadataString(artist);
    std::string cleanAlbum = CleanAlbumTitle(album);

    ASSERT_STR_EQ(cleanArtist, "dBu Music");
    ASSERT_STR_EQ(cleanAlbum, "Shisou");

    std::string escArtist = EscapeLuceneQuery(cleanArtist);
    std::string escAlbum = EscapeLuceneQuery(cleanAlbum);

    std::string tierAQuery = "artist:\"" + escArtist + "\" AND releasegroup:\"" + escAlbum + "\"";
    ASSERT_STR_EQ(tierAQuery, "artist:\"dBu Music\" AND releasegroup:\"Shisou\"");

    std::string romaji = "Shisou";
    std::string kata = RomajiToKatakana(romaji);
    ASSERT_STR_EQ(kata, "シソウ");
}
