#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include "../include/MetadataUtils.hpp"

TEST_CASE("AcoustID Fetch", "POST Payload Building") {
    std::string client = "testClientKey123";
    double duration = 214.5;
    std::string fingerprint = "AQABz0mSZEmSRImi";

    std::string postData = "client=" + FetchServices::UrlEncode(client)
                         + "&meta=recordings+releasegroups+compress"
                         + "&duration=" + std::to_string((int)duration)
                         + "&fingerprint=" + FetchServices::UrlEncode(fingerprint);

    ASSERT_TRUE(postData.find("client=testClientKey123") != std::string::npos);
    ASSERT_TRUE(postData.find("meta=recordings+releasegroups+compress") != std::string::npos);
    ASSERT_TRUE(postData.find("duration=214") != std::string::npos);
    ASSERT_TRUE(postData.find("fingerprint=AQABz0mSZEmSRImi") != std::string::npos);
}

TEST_CASE("AcoustID Fetch", "Parse Valid AcoustID Multi-Result JSON") {
    std::string json = R"json({
        "status": "ok",
        "results": [
            {
                "id": "result-1-uuid",
                "score": 0.95,
                "recordings": [
                    {
                        "id": "b3e34b3e-108b-4ef9-bfef-c081e7d9564c",
                        "title": "Bad Apple!! feat. nomico",
                        "artists": [
                            { "id": "artist-1", "name": "Alstroemeria Records" },
                            { "id": "artist-2", "name": "nomico" }
                        ],
                        "releasegroups": [
                            { "id": "41530960-93bb-40cb-a5e2-e1c6fa620f4c", "title": "Lovelight" },
                            { "id": "52640a71-84bb-40cb-b5e2-f2c6fa620f5d", "title": "Exserens" }
                        ]
                    }
                ]
            },
            {
                "id": "result-2-uuid",
                "score": 0.88,
                "recordings": [
                    {
                        "id": "c4f45c4f-219c-5fa0-c0f0-d192f8e0675d",
                        "title": "Bad Apple!! (Instrumental)",
                        "artists": [
                            { "id": "artist-1", "name": "Alstroemeria Records" }
                        ],
                        "releasegroups": [
                            { "id": "41530960-93bb-40cb-a5e2-e1c6fa620f4c" }
                        ]
                    }
                ]
            }
        ]
    })json";

    auto results = FetchServices::ParseAcoustIdResponse(json);
    ASSERT_EQ(results.size(), 2);

    // First item assertions
    ASSERT_STR_EQ(results[0].recordingId, "b3e34b3e-108b-4ef9-bfef-c081e7d9564c");
    ASSERT_STR_EQ(results[0].title, "Bad Apple!! feat. nomico");
    ASSERT_NEAR(results[0].score, 0.95, 0.001);
    ASSERT_EQ(results[0].artists.size(), 2);
    ASSERT_STR_EQ(results[0].artists[0], "Alstroemeria Records");
    ASSERT_STR_EQ(results[0].artists[1], "nomico");
    ASSERT_EQ(results[0].releaseGroupIds.size(), 2);
    ASSERT_STR_EQ(results[0].releaseGroupIds[0], "41530960-93bb-40cb-a5e2-e1c6fa620f4c");
    ASSERT_STR_EQ(results[0].releaseGroupIds[1], "52640a71-84bb-40cb-b5e2-f2c6fa620f5d");

    // Second item assertions
    ASSERT_STR_EQ(results[1].recordingId, "c4f45c4f-219c-5fa0-c0f0-d192f8e0675d");
    ASSERT_STR_EQ(results[1].title, "Bad Apple!! (Instrumental)");
    ASSERT_NEAR(results[1].score, 0.88, 0.001);
    ASSERT_EQ(results[1].artists.size(), 1);
    ASSERT_STR_EQ(results[1].artists[0], "Alstroemeria Records");
}

TEST_CASE("AcoustID Fetch", "Parse AcoustID Empty and Error JSON") {
    std::string emptyResultsJson = R"json({"status": "ok", "results": []})json";
    auto emptyRes = FetchServices::ParseAcoustIdResponse(emptyResultsJson);
    ASSERT_EQ(emptyRes.size(), 0);

    std::string errorJson = R"json({
        "status": "error",
        "error": {
            "code": 4,
            "message": "invalid fingerprint"
        }
    })json";
    auto errRes = FetchServices::ParseAcoustIdResponse(errorJson);
    ASSERT_EQ(errRes.size(), 0);

    std::string invalidJson = "<html><head><title>502 Bad Gateway</title></head><body>502</body></html>";
    auto invalidRes = FetchServices::ParseAcoustIdResponse(invalidJson);
    ASSERT_EQ(invalidRes.size(), 0);
}
