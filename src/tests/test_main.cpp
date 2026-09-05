#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

#include "TestFramework.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <map>

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    std::cout << "=======================================================\n";
    std::cout << "  MusicSorter Fetch Services & API Test Suite (C++20)\n";
    std::cout << "=======================================================\n\n";

    const auto& tests = TestFramework::TestRegistry::Instance().GetTests();
    int passedCount = 0;
    int failedCount = 0;
    auto totalStart = std::chrono::high_resolution_clock::now();

    std::string currentSuite = "";

    for (const auto& t : tests) {
        if (t.suite != currentSuite) {
            currentSuite = t.suite;
            std::cout << "\n--- [SUITE: " << currentSuite << "] ---\n";
        }

        std::cout << "  -> Starting: " << t.name << std::endl;

        TestFramework::g_ctx.assertionsRun = 0;
        TestFramework::g_ctx.assertionsFailed = 0;
        TestFramework::g_ctx.failureMessages.clear();

        auto start = std::chrono::high_resolution_clock::now();
        try {
            t.func();
        } catch (const std::exception& ex) {
            TestFramework::g_ctx.assertionsFailed++;
            TestFramework::g_ctx.failureMessages.push_back(std::string("  [EXCEPTION] Unhandled exception: ") + ex.what());
        } catch (...) {
            TestFramework::g_ctx.assertionsFailed++;
            TestFramework::g_ctx.failureMessages.push_back("  [EXCEPTION] Unknown unhandled exception");
        }
        auto end = std::chrono::high_resolution_clock::now();
        double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

        if (TestFramework::g_ctx.assertionsFailed == 0) {
            std::cout << "  [PASS] " << t.name << " (" << std::fixed << std::setprecision(2) << elapsedMs << " ms, "
                      << TestFramework::g_ctx.assertionsRun << " assertions)\n";
            passedCount++;
        } else {
            std::cout << "  [FAIL] " << t.name << " (" << std::fixed << std::setprecision(2) << elapsedMs << " ms, "
                      << TestFramework::g_ctx.assertionsFailed << " failed of "
                      << TestFramework::g_ctx.assertionsRun << " assertions)\n";
            for (const auto& msg : TestFramework::g_ctx.failureMessages) {
                std::cout << msg << "\n";
            }
            failedCount++;
        }
    }

    auto totalEnd = std::chrono::high_resolution_clock::now();
    double totalElapsedSec = std::chrono::duration<double>(totalEnd - totalStart).count();

    std::cout << "\n=======================================================\n";
    std::cout << "  Test Summary: " << passedCount << " passed, " << failedCount << " failed, "
              << (passedCount + failedCount) << " total (" << std::fixed << std::setprecision(2) << totalElapsedSec << "s)\n";
    std::cout << "=======================================================\n";

    return (failedCount == 0) ? 0 : 1;
}
