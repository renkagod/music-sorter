#include "TestFramework.hpp"
#include "MetadataUtils.hpp"
#include "AppWindow.hpp"
#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>

namespace fs = std::filesystem;

namespace {

struct SimulatedCluster {
    std::string albumKey;
    std::string sampleFile;
    std::vector<size_t> indices;
};

struct ScanResult {
    std::vector<TagReviewItem> items;
    std::vector<SimulatedCluster> clusters;
};

// Exact simulation of AppWindow.cpp lines 3939-4112
ScanResult SimulateScanAndCluster(const std::vector<std::string>& files) {
    ScanResult result;
    result.items.resize(files.size());

    for (size_t i = 0; i < files.size(); ++i) {
        auto& item = result.items[i];
        item.filePath = files[i];
        item.originalFilename = fs::path(files[i]).filename().string();
        memset(item.lyricsBuf, 0, sizeof(item.lyricsBuf));

        std::string fn = fs::path(files[i]).stem().string();
        std::string trackNo = "01";
        std::string title = fn;
        std::string artistRaw = fs::path(files[i]).parent_path().parent_path().filename().string();
        std::string albumRaw = fs::path(files[i]).parent_path().filename().string();
        std::string yearStr = ExtractYearFromString(files[i]);

        ParsedFilenameInfo parsed = ParseFilenameHeuristic(files[i]);

        if (parsed.hasTrackNumber && parsed.trackNumber > 0) {
            trackNo = (parsed.trackNumber < 10) ? ("0" + std::to_string(parsed.trackNumber)) : std::to_string(parsed.trackNumber);
        }
        if (!parsed.title.empty()) {
            title = parsed.title;
        }

        std::string artistClean = CleanMetadataString(artistRaw);
        std::string artistCleanKey = NormalizeKey(artistClean);
        if (artistClean.empty() || artistCleanKey == "tosort" || artistCleanKey == "media" || artistCleanKey == "music" || artistCleanKey == "singles" || artistCleanKey == "downloads") {
            if (parsed.hasArtist && !parsed.artist.empty()) {
                artistClean = parsed.artist;
            } else {
                artistClean = ExtractArtistFromFilename(item.originalFilename);
                if (artistClean.empty()) artistClean = "Unknown Artist";
            }
        } else if (parsed.hasArtist && !parsed.artist.empty()) {
            artistClean = parsed.artist;
        }

        std::string albumClean = CleanAlbumTitle(albumRaw);
        if (albumClean.empty()) albumClean = CleanMetadataString(albumRaw);
        if (parsed.hasAlbum && !parsed.album.empty()) {
            albumClean = parsed.album;
        }

        // Sanitize generic/staging folder names
        std::string albumCleanKey = NormalizeKey(albumClean);
        if (albumCleanKey == "tosort" || albumCleanKey == "music" || albumCleanKey == "media" || albumCleanKey == "singles" || albumCleanKey == "downloads") {
            albumClean = "";
        }

        // If folder name equals the artist name and filename had no album, this is an artist folder of singles
        if (!parsed.hasAlbum && NormalizeKey(albumClean) == NormalizeKey(artistClean)) {
            albumClean = "";
        }

        // If in a staging folder hierarchy and the filename did not specify an album, clear albumClean
        std::string parentName = fs::path(files[i]).parent_path().filename().string();
        std::string grandParentName = fs::path(files[i]).parent_path().parent_path().filename().string();
        std::string parentKey = NormalizeKey(parentName);
        std::string grandParentKey = NormalizeKey(grandParentName);
        bool inStaging = (parentKey == "tosort" || parentKey == "music" || parentKey == "media" || parentKey == "singles" || parentKey == "downloads" ||
                          grandParentKey == "tosort" || grandParentKey == "music" || grandParentKey == "media" || grandParentKey == "singles" || grandParentKey == "downloads");
        if (inStaging && !parsed.hasAlbum) {
            albumClean = "";
        }

        item.embeddedArtist = artistClean;
        item.embeddedAlbum = albumClean;
        item.embeddedTitle = title;
        item.embeddedTrackNo = trackNo;
        item.embeddedYear = yearStr;

        strncpy_s(item.artistBuf, artistClean.c_str(), sizeof(item.artistBuf) - 1);
        strncpy_s(item.albumBuf, albumClean.c_str(), sizeof(item.albumBuf) - 1);
        strncpy_s(item.titleBuf, title.c_str(), sizeof(item.titleBuf) - 1);
        strncpy_s(item.trackNoBuf, trackNo.c_str(), sizeof(item.trackNoBuf) - 1);
        strncpy_s(item.yearBuf, yearStr.c_str(), sizeof(item.yearBuf) - 1);
    }

    // Phase 2: Album Clustering
    std::unordered_map<std::string, size_t> keyToClusterIdx;

    for (size_t i = 0; i < files.size(); ++i) {
        auto& item = result.items[i];
        std::string albumClean(item.albumBuf);
        std::string albumKey = NormalizeKey(albumClean);

        std::string parentName = fs::path(files[i]).parent_path().filename().string();
        std::string grandParentName = fs::path(files[i]).parent_path().parent_path().filename().string();
        std::string parentKey = NormalizeKey(parentName);
        std::string grandParentKey = NormalizeKey(grandParentName);
        bool inStaging = (parentKey.empty() || parentKey == "tosort" || parentKey == "music" || parentKey == "media" || parentKey == "singles" || parentKey == "downloads" ||
                          grandParentKey == "tosort" || grandParentKey == "music" || grandParentKey == "media" || grandParentKey == "singles" || grandParentKey == "downloads");

        bool isLoose = false;
        if (inStaging) {
            if (albumKey.empty() || albumKey == "unknown" || albumKey == "tosort" || albumKey == "music" || albumKey == "media" || albumKey == "singles" || albumKey == "downloads" || item.embeddedAlbum.empty()) {
                isLoose = true;
            }
        } else if (albumKey.empty() || albumKey == "unknown") {
            isLoose = true;
        }

        if (isLoose) {
            item.isSingleTrack = true;
            std::string singleKey = "__single_track_" + std::to_string(i);
            result.clusters.push_back({ singleKey, files[i], { i } });
            continue;
        }

        auto it = keyToClusterIdx.find(albumKey);
        if (it == keyToClusterIdx.end()) {
            size_t newIdx = result.clusters.size();
            keyToClusterIdx[albumKey] = newIdx;
            result.clusters.push_back({ albumKey, files[i], { i } });
        } else {
            result.clusters[it->second].indices.push_back(i);
        }
    }

    return result;
}

// Exact simulation of AppWindow.cpp lines 1418-1470
std::vector<size_t> SimulateGetAlbumTrackIndices(const std::vector<TagReviewItem>& items, size_t referenceIndex) {
    std::vector<size_t> result;
    if (referenceIndex >= items.size()) return result;

    const auto& ref = items[referenceIndex];
    if (ref.isSingleTrack) {
        return { referenceIndex };
    }

    std::string refFolder = fs::path(ref.filePath).parent_path().string();
    std::string refParentName = fs::path(ref.filePath).parent_path().filename().string();
    std::string refParentKey = NormalizeKey(refParentName);
    std::string refGrandParentName = fs::path(ref.filePath).parent_path().parent_path().filename().string();
    std::string refGrandParentKey = NormalizeKey(refGrandParentName);

    bool isStagingFolder = (refParentKey.empty() || refParentKey == "tosort" || refParentKey == "music" || refParentKey == "media" || refParentKey == "singles" || refParentKey == "downloads" ||
                            refGrandParentKey == "tosort" || refGrandParentKey == "music" || refGrandParentKey == "media" || refGrandParentKey == "singles" || refGrandParentKey == "downloads");

    std::string refAlbumKey = NormalizeKey(ref.albumBuf);
    std::string refMbId = ref.releaseGroupMbId;

    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        if (item.isProcessed) continue;
        if (item.isSingleTrack) {
            if (i == referenceIndex) {
                result.push_back(i);
            }
            continue;
        }

        bool match = false;
        if (!isStagingFolder && !refFolder.empty() && fs::path(item.filePath).parent_path().string() == refFolder) {
            match = true;
        } else if (!refMbId.empty() && item.releaseGroupMbId == refMbId) {
            match = true;
        } else if (!refAlbumKey.empty() && refAlbumKey != "unknown" && refAlbumKey != "tosort" && refAlbumKey != "music" && refAlbumKey != "media" && refAlbumKey != "singles" && refAlbumKey != "downloads") {
            std::string itemAlbumKey = NormalizeKey(item.albumBuf);
            if (itemAlbumKey == refAlbumKey) {
                match = true;
            }
        }

        if (match) {
            result.push_back(i);
        }
    }

    if (result.empty() && !ref.isProcessed) {
        result.push_back(referenceIndex);
    }

    return result;
}

} // anonymous namespace

// ============================================================================
// TEST 1: 43 Loose Singles from D:\media\music\TO SORT\ExileLord
// ============================================================================
TEST_CASE("Clustering Adversarial", "ExileLord 43 Loose Singles Isolation") {
    const std::vector<std::string> exileLordFiles = {
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Soulless 4.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Megalodon.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Arm Breaker (400 BPM).mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Soulless 2 (Mechanical Machine).mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Soulless 3 (380 BPM).mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Soulless 5.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Crash Test 5.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Exile's Minute of Madness (hardsongforme).mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - SLSVSX_hybrid.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - i made something.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Two Hour Testament.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - test.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Astral Overdrive.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Dark Matter.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Cosmic Radiation.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Hypernova.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Singularity.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Quasar.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Event Horizon.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Nebula Pulse.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Wormhole.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Supercluster.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Gravitational Collapse.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Tachyon Beam.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Antimatter Engine.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Void Walker.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Starfall.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Neutron Burst.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Solar Flare.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Pulsar Echo.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Void Resonance.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Quantum Rift.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Gamma Ray.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Red Giant.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - White Dwarf.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Black Hole Sun.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Relativistic Jet.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Magnetar.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Accretion Disk.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Cosmic Horizon.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Dark Energy.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Entropy 0.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Final Singularity.mp3"
    };

    ASSERT_EQ(exileLordFiles.size(), (size_t)43);

    ScanResult scan = SimulateScanAndCluster(exileLordFiles);

    // 1. Must produce exactly 43 distinct clusters (1 per track)
    ASSERT_EQ(scan.clusters.size(), (size_t)43);

    // 2. Each item must have isSingleTrack = true, correct artist, empty albumBuf
    for (size_t i = 0; i < scan.items.size(); ++i) {
        const auto& itm = scan.items[i];
        ASSERT_TRUE(itm.isSingleTrack);
        ASSERT_STR_EQ(itm.artistBuf, "ExileLord");
        ASSERT_STR_EQ(itm.albumBuf, "");
        ASSERT_FALSE(itm.titleBuf[0] == '\0');

        // Verify cluster has size 1 and contains index i
        ASSERT_EQ(scan.clusters[i].indices.size(), (size_t)1);
        ASSERT_EQ(scan.clusters[i].indices[0], i);

        // 3. GetAlbumTrackIndices must return ONLY { i }
        auto indices = SimulateGetAlbumTrackIndices(scan.items, i);
        ASSERT_EQ(indices.size(), (size_t)1);
        ASSERT_EQ(indices[0], i);
    }
}

// ============================================================================
// TEST 2: GetAlbumTrackIndices Contract and Invariants
// ============================================================================
TEST_CASE("Clustering Adversarial", "GetAlbumTrackIndices Contract and Boundary Conditions") {
    std::vector<std::string> mockFiles = {
        "D:\\music\\Queen\\A Night at the Opera\\01 - Death on Two Legs.mp3",
        "D:\\music\\Queen\\A Night at the Opera\\02 - Lazing on a Sunday Afternoon.mp3",
        "D:\\music\\Queen\\A Night at the Opera\\03 - I'm in Love with My Car.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Soulless 4.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Megalodon.mp3"
    };

    ScanResult scan = SimulateScanAndCluster(mockFiles);

    // Out of range index should return empty vector
    auto outOfRange = SimulateGetAlbumTrackIndices(scan.items, 999);
    ASSERT_EQ(outOfRange.size(), (size_t)0);

    // Tracks 0, 1, 2 are from Queen - A Night at the Opera (3-level path, not in staging)
    // They should cluster together into an album
    for (size_t i = 0; i < 3; ++i) {
        ASSERT_FALSE(scan.items[i].isSingleTrack);
        ASSERT_STR_EQ(scan.items[i].albumBuf, "A Night at the Opera");
        ASSERT_STR_EQ(scan.items[i].artistBuf, "Queen");

        auto albIndices = SimulateGetAlbumTrackIndices(scan.items, i);
        ASSERT_EQ(albIndices.size(), (size_t)3);
        ASSERT_EQ(albIndices[0], (size_t)0);
        ASSERT_EQ(albIndices[1], (size_t)1);
        ASSERT_EQ(albIndices[2], (size_t)2);
    }

    // Tracks 3 and 4 are loose singles from ExileLord
    for (size_t i = 3; i < 5; ++i) {
        ASSERT_TRUE(scan.items[i].isSingleTrack);
        auto singleIndices = SimulateGetAlbumTrackIndices(scan.items, i);
        ASSERT_EQ(singleIndices.size(), (size_t)1);
        ASSERT_EQ(singleIndices[0], i);
    }
}

// ============================================================================
// TEST 3: Mixed Folders (Album Subfolder Alongside Loose Singles in Same Parent)
// ============================================================================
TEST_CASE("Clustering Adversarial", "Mixed Folders: Album Subfolder Alongside Loose Singles") {
    std::vector<std::string> mixedFiles = {
        // Loose singles inside ExileLord folder
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Soulless 1.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Soulless 2.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\ExileLord - Soulless 3.mp3",
        // Legitimate album inside its own subfolder inside ExileLord folder
        "D:\\media\\music\\TO SORT\\ExileLord\\Overcome EP\\01 - Intro.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\Overcome EP\\02 - Overcome.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord\\Overcome EP\\03 - Climax.mp3"
    };

    ScanResult scan = SimulateScanAndCluster(mixedFiles);

    // Indices 0, 1, 2 must be loose tracks
    for (size_t i = 0; i < 3; ++i) {
        ASSERT_TRUE(scan.items[i].isSingleTrack);
        ASSERT_STR_EQ(scan.items[i].albumBuf, "");
        auto idxs = SimulateGetAlbumTrackIndices(scan.items, i);
        ASSERT_EQ(idxs.size(), (size_t)1);
        ASSERT_EQ(idxs[0], i);
    }

    // Indices 3, 4, 5 belong to "Overcome EP" (parent is "Overcome EP", grandparent is "ExileLord")
    // Grandparent is "ExileLord", parent is "Overcome EP", neither is in staging folder list!
    // They must form a proper album cluster!
    for (size_t i = 3; i < 6; ++i) {
        ASSERT_FALSE(scan.items[i].isSingleTrack);
        ASSERT_STR_EQ(scan.items[i].albumBuf, "Overcome EP");
        auto idxs = SimulateGetAlbumTrackIndices(scan.items, i);
        ASSERT_EQ(idxs.size(), (size_t)3);
        ASSERT_EQ(idxs[0], (size_t)3);
        ASSERT_EQ(idxs[1], (size_t)4);
        ASSERT_EQ(idxs[2], (size_t)5);
    }

    // Total clusters: 3 loose singles + 1 album cluster = 4 clusters
    ASSERT_EQ(scan.clusters.size(), (size_t)4);
}

// ============================================================================
// TEST 4: Normal Album Folders in Dedicated Library Paths
// ============================================================================
TEST_CASE("Clustering Adversarial", "Normal Legitimate Albums In Standard Folders") {
    std::vector<std::string> albumFiles = {
        "D:\\Library\\Pink Floyd\\The Wall\\01 - In the Flesh.mp3",
        "D:\\Library\\Pink Floyd\\The Wall\\02 - The Thin Ice.mp3",
        "D:\\Library\\Pink Floyd\\The Wall\\03 - Another Brick in the Wall.mp3",
        "D:\\Library\\Pink Floyd\\The Wall\\04 - Mother.mp3"
    };

    ScanResult scan = SimulateScanAndCluster(albumFiles);

    ASSERT_EQ(scan.clusters.size(), (size_t)1);
    ASSERT_EQ(scan.clusters[0].indices.size(), (size_t)4);

    for (size_t i = 0; i < 4; ++i) {
        ASSERT_FALSE(scan.items[i].isSingleTrack);
        ASSERT_STR_EQ(scan.items[i].albumBuf, "The Wall");
        ASSERT_STR_EQ(scan.items[i].artistBuf, "Pink Floyd");

        auto indices = SimulateGetAlbumTrackIndices(scan.items, i);
        ASSERT_EQ(indices.size(), (size_t)4);
    }
}

// ============================================================================
// TEST 5: Filenames with Explicit Album Tags Group Even in Staging
// ============================================================================
TEST_CASE("Clustering Adversarial", "Explicit Album in Filename Groups in Staging") {
    std::vector<std::string> taggedInStaging = {
        "D:\\media\\music\\TO SORT\\Daft Punk - Discovery - 01 - One More Time.mp3",
        "D:\\media\\music\\TO SORT\\Daft Punk - Discovery - 02 - Aerodynamic.mp3",
        "D:\\media\\music\\TO SORT\\Daft Punk - Discovery - 03 - Digital Love.mp3"
    };

    ScanResult scan = SimulateScanAndCluster(taggedInStaging);

    // Because filenames have "Artist - Album - Track - Title", parsed.hasAlbum is true
    // They should group into 1 cluster with 3 tracks
    ASSERT_EQ(scan.clusters.size(), (size_t)1);
    ASSERT_EQ(scan.clusters[0].indices.size(), (size_t)3);

    for (size_t i = 0; i < 3; ++i) {
        ASSERT_FALSE(scan.items[i].isSingleTrack);
        ASSERT_STR_EQ(scan.items[i].albumBuf, "Discovery");
        ASSERT_STR_EQ(scan.items[i].artistBuf, "Daft Punk");

        auto indices = SimulateGetAlbumTrackIndices(scan.items, i);
        ASSERT_EQ(indices.size(), (size_t)3);
    }
}

// ============================================================================
// TEST 6: Compilation Album with Diverse Artists Under Non-Staging Parent
// ============================================================================
TEST_CASE("Clustering Adversarial", "Compilation Album with Diverse Artists") {
    std::vector<std::string> compilationFiles = {
        "D:\\media\\music\\TO SORT\\Compilations\\Summer Hits 2024\\01 - Dua Lipa - Levitating.mp3",
        "D:\\media\\music\\TO SORT\\Compilations\\Summer Hits 2024\\02 - The Weeknd - Blinding Lights.mp3",
        "D:\\media\\music\\TO SORT\\Compilations\\Summer Hits 2024\\03 - Doja Cat - Say So.mp3"
    };

    ScanResult scan = SimulateScanAndCluster(compilationFiles);

    // Parent is "Summer Hits 2024", grandparent is "Compilations" (not in staging list)
    // All 3 tracks have different artists, but same album folder
    // They must form 1 album cluster
    ASSERT_EQ(scan.clusters.size(), (size_t)1);
    ASSERT_EQ(scan.clusters[0].indices.size(), (size_t)3);

    for (size_t i = 0; i < 3; ++i) {
        ASSERT_FALSE(scan.items[i].isSingleTrack);
        ASSERT_STR_EQ(scan.items[i].albumBuf, "Summer Hits 2024");
        auto idxs = SimulateGetAlbumTrackIndices(scan.items, i);
        ASSERT_EQ(idxs.size(), (size_t)3);
    }

    ASSERT_STR_EQ(scan.items[0].artistBuf, "Dua Lipa");
    ASSERT_STR_EQ(scan.items[1].artistBuf, "The Weeknd");
    ASSERT_STR_EQ(scan.items[2].artistBuf, "Doja Cat");
}

// ============================================================================
// TEST 7: Direct Files in TO SORT Root Without Subfolders
// ============================================================================
TEST_CASE("Clustering Adversarial", "Direct Files in TO SORT Root") {
    std::vector<std::string> rootFiles = {
        "D:\\media\\music\\TO SORT\\Track A.mp3",
        "D:\\media\\music\\TO SORT\\Track B.mp3",
        "D:\\media\\music\\TO SORT\\ExileLord - Megalodon.mp3"
    };

    ScanResult scan = SimulateScanAndCluster(rootFiles);

    // All must be isolated loose tracks
    ASSERT_EQ(scan.clusters.size(), (size_t)3);
    for (size_t i = 0; i < 3; ++i) {
        ASSERT_TRUE(scan.items[i].isSingleTrack);
        ASSERT_STR_EQ(scan.items[i].albumBuf, "");
        auto idxs = SimulateGetAlbumTrackIndices(scan.items, i);
        ASSERT_EQ(idxs.size(), (size_t)1);
        ASSERT_EQ(idxs[0], i);
    }
}

// ============================================================================
// TEST 8: 2-Level Album Directly in TO SORT (Behavioral Characterization)
// ============================================================================
TEST_CASE("Clustering Adversarial", "Direct 2-Level Album in TO SORT Root Analysis") {
    // When an album folder is placed directly inside TO SORT/ without artist wrapper:
    // D:\media\music\TO SORT\The Dark Side of the Moon\01 - Speak to Me.mp3
    // parent is "The Dark Side of the Moon", grandparent is "TO SORT"
    // Because grandParentKey == "tosort", inStaging is true.
    // If filename has no album name, it treats them as loose singles.
    std::vector<std::string> twoLevelFiles = {
        "D:\\media\\music\\TO SORT\\The Dark Side of the Moon\\01 - Speak to Me.mp3",
        "D:\\media\\music\\TO SORT\\The Dark Side of the Moon\\02 - Breathe.mp3"
    };

    ScanResult scan = SimulateScanAndCluster(twoLevelFiles);

    // Documented behavior: in staging folder root, without explicit album tag in filename,
    // tracks are treated as loose tracks to protect against false album consolidation.
    ASSERT_EQ(scan.clusters.size(), (size_t)2);
    ASSERT_TRUE(scan.items[0].isSingleTrack);
    ASSERT_TRUE(scan.items[1].isSingleTrack);

    auto idxs0 = SimulateGetAlbumTrackIndices(scan.items, 0);
    ASSERT_EQ(idxs0.size(), (size_t)1);
    ASSERT_EQ(idxs0[0], (size_t)0);

    auto idxs1 = SimulateGetAlbumTrackIndices(scan.items, 1);
    ASSERT_EQ(idxs1.size(), (size_t)1);
    ASSERT_EQ(idxs1[0], (size_t)1);
}

// ============================================================================
// TEST 9: Dirty Folder Names with Brackets, Discs, and Years
// ============================================================================
TEST_CASE("Clustering Adversarial", "Dirty Folder Names with Brackets and Years") {
    std::vector<std::string> dirtyFolderFiles = {
        "D:\\Library\\Pink Floyd\\(1973) The Dark Side of the Moon [FLAC]\\01 - Speak to Me.mp3",
        "D:\\Library\\Pink Floyd\\(1973) The Dark Side of the Moon [FLAC]\\02 - Breathe.mp3"
    };

    ScanResult scan = SimulateScanAndCluster(dirtyFolderFiles);

    // CleanAlbumTitle strips "(1973) ", leaves "The Dark Side of the Moon [FLAC]" or clean title
    ASSERT_EQ(scan.clusters.size(), (size_t)1);
    ASSERT_EQ(scan.clusters[0].indices.size(), (size_t)2);
    ASSERT_FALSE(scan.items[0].isSingleTrack);
    ASSERT_FALSE(scan.items[1].isSingleTrack);

    auto idxs = SimulateGetAlbumTrackIndices(scan.items, 0);
    ASSERT_EQ(idxs.size(), (size_t)2);
}

