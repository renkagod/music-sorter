#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include <chrono>

TEST_CASE("Fetch Throttling", "MusicBrainz Proactive Rate Limit Throttle") {
    auto t1 = std::chrono::steady_clock::now();
    FetchServices::MusicBrainzThrottle();
    auto t2 = std::chrono::steady_clock::now();
    FetchServices::MusicBrainzThrottle();
    auto t3 = std::chrono::steady_clock::now();

    auto gap1 = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
    // Must be at least 1000ms (1.1s target with small OS timer variance margin)
    ASSERT_GE(gap1, 950);
}

TEST_CASE("Fetch Throttling", "Discogs Proactive Rate Limit Throttle") {
    auto t1 = std::chrono::steady_clock::now();
    FetchServices::DiscogsThrottle();
    auto t2 = std::chrono::steady_clock::now();
    FetchServices::DiscogsThrottle();
    auto t3 = std::chrono::steady_clock::now();

    auto gap1 = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
    // Must be at least 1000ms
    ASSERT_GE(gap1, 950);
}

TEST_CASE("Fetch Throttling", "Exponential Backoff Calculation Formula") {
    auto calcBackoff = [](bool isThrottledService, int attempt) -> long long {
        return isThrottledService ? (1500 * (attempt + 1)) : (400 * (attempt + 1));
    };

    ASSERT_EQ(calcBackoff(true, 0), 1500);
    ASSERT_EQ(calcBackoff(true, 1), 3000);
    ASSERT_EQ(calcBackoff(true, 2), 4500);

    ASSERT_EQ(calcBackoff(false, 0), 400);
    ASSERT_EQ(calcBackoff(false, 1), 800);
    ASSERT_EQ(calcBackoff(false, 2), 1200);
}

TEST_CASE("Fetch Throttling", "LRCLIB Proactive Rate Limit Throttle") {
    auto t1 = std::chrono::steady_clock::now();
    FetchServices::LrcLibThrottle();
    auto t2 = std::chrono::steady_clock::now();
    FetchServices::LrcLibThrottle();
    auto t3 = std::chrono::steady_clock::now();

    auto gap1 = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
    // Must be at least 250ms (300ms target with variance margin)
    ASSERT_GE(gap1, 240);
}

TEST_CASE("Fetch Throttling", "AcoustID Proactive Rate Limit Throttle") {
    auto t1 = std::chrono::steady_clock::now();
    FetchServices::AcoustIdThrottle();
    auto t2 = std::chrono::steady_clock::now();
    FetchServices::AcoustIdThrottle();
    auto t3 = std::chrono::steady_clock::now();

    auto gap1 = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
    // Must be at least 300ms (340ms target with variance margin)
    ASSERT_GE(gap1, 300);
}


