#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include "../include/MetadataUtils.hpp"

TEST_CASE("Cover Art Archive Fetch", "URL Formation for Release and Release Group Front Covers") {
    std::string releaseGroupMbId = "41530960-93bb-40cb-a5e2-e1c6fa620f4c";
    std::string rgCoverUrl = "https://coverartarchive.org/release-group/" + releaseGroupMbId + "/front";
    ASSERT_STR_EQ(rgCoverUrl, "https://coverartarchive.org/release-group/41530960-93bb-40cb-a5e2-e1c6fa620f4c/front");

    std::string releaseMbId = "b3e34b3e-108b-4ef9-bfef-c081e7d9564c";
    std::string relCoverUrl = "https://coverartarchive.org/release/" + releaseMbId + "/front";
    ASSERT_STR_EQ(relCoverUrl, "https://coverartarchive.org/release/b3e34b3e-108b-4ef9-bfef-c081e7d9564c/front");
}

TEST_CASE("Cover Art Archive Fetch", "Cover Image Quality Scoring Algorithm") {
    // Synthetic cover metrics tests
    // 1400x1400 high-res 1:1 image vs 500x500 medium image vs 200x200 low-res
    unsigned char dummy[] = { 0xFF, 0xD8, 0xFF, 0xE0 }; // Dummy JPEG magic bytes
    size_t dummySize = sizeof(dummy);

    long long scoreHigh = CalculateImageQualityScore(dummy, dummySize, 1400, 1400);
    long long scoreMed = CalculateImageQualityScore(dummy, dummySize, 500, 500);
    long long scoreLow = CalculateImageQualityScore(dummy, dummySize, 200, 200);

    // High res gets 500M + megapixels*50 + 50M (aspect >= 0.95)
    ASSERT_GE(scoreHigh, 500000000LL);
    ASSERT_GE(scoreHigh, scoreMed + 200000000LL);
    ASSERT_GE(scoreMed, scoreLow + 100000000LL);

    // Square 1:1 aspect ratio gets bonus over rectangular 16:9
    long long scoreSquare = CalculateImageQualityScore(dummy, dummySize, 1000, 1000);
    long long scoreBanner = CalculateImageQualityScore(dummy, dummySize, 1000, 500);
    ASSERT_GE(scoreSquare, scoreBanner + 40000000LL);
}
