#include "TestFramework.hpp"
#include "../include/FetchServices.hpp"
#include <future>
#include <chrono>
#include <thread>
#include <vector>

using namespace FetchServices;

TEST_CASE("Async Fetch", "Async Lyrics Fetch via FetchLrcLibSyncedLyricsAsync") {
    // Asynchronous invocation with standard future
    auto fut = FetchLrcLibSyncedLyricsAsync("nomico", "Bad Apple!!", "Lovelight");
    
    // Future should be valid and getable
    ASSERT_TRUE(fut.valid());
    
    // Wait for the async result with timeout
    auto status = fut.wait_for(std::chrono::seconds(10));
    ASSERT_TRUE(status == std::future_status::ready);
    
    std::string lyrics = fut.get();
    ASSERT_TRUE(!lyrics.empty());
    ASSERT_TRUE(lyrics.find("[00:") != std::string::npos || lyrics.find("Nagare") != std::string::npos || lyrics.find("Bad Apple") != std::string::npos);
}

TEST_CASE("Async Fetch", "Batch Parallel Lyrics Fetch via BatchFetchLrcLibLyrics") {
    std::vector<LyricsQuery> queries = {
        { "nomico", "Bad Apple!!", "Lovelight" },
        { "nomico", "Bad Apple!!", "" },
        { "Unknown Artist XYZ 12345", "Nonexistent Track Title 99999", "Fake Album" }
    };

    auto results = BatchFetchLrcLibLyrics(queries, 4);
    
    ASSERT_EQ(results.size(), queries.size());
    // At least one valid query should return non-empty lyrics
    ASSERT_TRUE(!results[0].empty() || !results[1].empty());
    // The nonexistent track query should return empty string cleanly without crashing
    ASSERT_EQ(results[2], std::string(""));
}

TEST_CASE("Async Fetch", "Multi-Threaded Throttle Race Safety") {
    // Spawn 8 threads simultaneously requesting throttled operations
    const int numThreads = 8;
    std::vector<std::thread> workers;
    std::atomic<int> completed{ 0 };

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < numThreads; ++i) {
        workers.emplace_back([&completed, i]() {
            if (i % 2 == 0) {
                MusicBrainzThrottle();
            } else {
                DiscogsThrottle();
            }
            completed.fetch_add(1);
        });
    }

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    ASSERT_EQ(completed.load(), numThreads);
    // Because 4 threads called MB and 4 called Discogs with 1.1s intervals, elapsed time should be > 2000ms
    ASSERT_TRUE(elapsed >= 2000);
}

TEST_CASE("Async Fetch", "Album Clustering & Partitioning Logic") {
    struct MockTrack {
        std::string filename;
        std::string album;
    };

    std::vector<MockTrack> mockFiles = {
        { "01. Intro.flac", "Bad Apple Collection" },
        { "02. Bad Apple.flac", "Bad Apple Collection" },
        { "03. Outro.flac", "Bad Apple Collection" },
        { "01. Track A.flac", "Another Album" },
        { "02. Track B.flac", "Another Album" },
        { "01. Standalone.mp3", "Single Release" }
    };

    std::unordered_map<std::string, std::vector<size_t>> clusters;
    for (size_t i = 0; i < mockFiles.size(); ++i) {
        std::string key = NormalizeKey(mockFiles[i].album);
        clusters[key].push_back(i);
    }

    ASSERT_EQ(clusters.size(), (size_t)3);
    ASSERT_EQ(clusters[NormalizeKey("Bad Apple Collection")].size(), (size_t)3);
    ASSERT_EQ(clusters[NormalizeKey("Another Album")].size(), (size_t)2);
    ASSERT_EQ(clusters[NormalizeKey("Single Release")].size(), (size_t)1);
}
