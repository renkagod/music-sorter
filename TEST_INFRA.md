# E2E Test Infra: MusicSorter

## Test Philosophy
- Opaque-box and requirement-driven, derived from ORIGINAL_REQUEST.md.
- Methodology: Category-Partition, Boundary Value Analysis (BVA), Pairwise Combinatorial Testing, Real-World Workload Testing.

## Feature Inventory
| # | Feature | Source | Tier 1 | Tier 2 | Tier 3 | Tier 4 |
|---|---------|--------|:------:|:------:|:------:|:------:|
| 1 | Filename Heuristic Parsing | ORIGINAL_REQUEST §R1 | 5 | 5 | ✓ | ✓ |
| 2 | Loose Tracks Support | ORIGINAL_REQUEST §R1 | 5 | 5 | ✓ | ✓ |
| 3 | Match Guardrails & Anti-False-Positive | ORIGINAL_REQUEST §R2 | 5 | 5 | ✓ | ✓ |
| 4 | Lyric Guardrails | ORIGINAL_REQUEST §R2 | 5 | 5 | ✓ | ✓ |
| 5 | Native C++ Last.fm Fetcher | ORIGINAL_REQUEST §R3 | 5 | 5 | ✓ | ✓ |
| 6 | Native C++ YouTube Music Fetcher | ORIGINAL_REQUEST §R3 | 5 | 5 | ✓ | ✓ |
| 7 | Consensus Aggregator & Conflict UI | ORIGINAL_REQUEST §R4 | 5 | 5 | ✓ | ✓ |

## Test Architecture
- Test runner: `MusicSorterTests.exe` compiled by CMake/Ninja into `build/MusicSorterTests.exe` or run via `run_tests.bat`.
- Test framework: `src/tests/TestFramework.hpp` with assertions (`ASSERT_TRUE`, `ASSERT_FALSE`, `ASSERT_EQ`, `ASSERT_STR_EQ`, `ASSERT_NEAR`).
- Automatic inclusion: `file(GLOB TEST_SOURCES "tests/*.cpp")` in `src/CMakeLists.txt`.
- Mock fixture directory / embedded mock JSON responses for network-isolated deterministic execution.

## Coverage Thresholds
- Tier 1 (Feature Coverage): >= 5 test cases per feature (happy path, isolations)
- Tier 2 (Boundary & Corner Cases): >= 5 test cases per feature (empty strings, special characters, zero tracks, unicode, malformed inputs)
- Tier 3 (Cross-Feature Combinations): Pairwise feature interactions (e.g. parsed filename feeding into Last.fm search, loose tracks feeding into consensus aggregator)
- Tier 4 (Real-World Application Scenarios): Real-world workload tests, including the ExileLord `TO SORT` test set (43 singles, no false album, no false lyrics).
