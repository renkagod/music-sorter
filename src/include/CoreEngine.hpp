#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <filesystem>

#include "AcousticAnalyzer.hpp"
#include "ConsensusAggregator.hpp"
#include "DatabaseManager.hpp"
#include "FetchServices.hpp"
#include "MetadataUtils.hpp"

namespace fs = std::filesystem;

enum class MatchTier {
    AcoustId,
    TierA,
    TierB_Verified,
    TierB_Fallback,
    TierB_Katakana,
    TierC_Loose,
    TouhouDB,
    THBWiki,
    VocaDB,
    UtaiteDB,
    Discogs,
    Niche_Local
};

struct TagReviewItem {
    std::string filePath;
    std::string relPath;
    std::string originalFilename;

    std::string embeddedArtist;
    std::string embeddedAlbum;
    std::string embeddedTitle;
    std::string embeddedTrackNo;
    std::string embeddedYear;

    char artistBuf[256] = {0};
    char albumBuf[256] = {0};
    char titleBuf[256] = {0};
    char trackNoBuf[32] = {0};
    char yearBuf[32] = {0};
    char lyricsBuf[16384] = {0};

    // Multi-language metadata variants (Romaji, English, Japanese)
    std::string titleRomaji;
    std::string titleEnglish;
    std::string titleJapanese;

    std::string artistRomaji;
    std::string artistEnglish;
    std::string artistJapanese;

    std::string albumRomaji;
    std::string albumEnglish;
    std::string albumJapanese;

    // Unsynced lyrics fallback variants (Original, Romaji, English)
    std::string lyricsOriginal;
    std::string lyricsRomaji;
    std::string lyricsEnglish;
    bool hasSyncedLyrics = false;

    bool isMusicBrainzMatched = false;
    bool isFetchCompleted = false;
    bool isProcessed = false;
    bool hasLyrics = false;
    bool isSingleTrack{false};
    double duration = 0.0;
    MatchTier matchTier = MatchTier::Niche_Local;
    std::string releaseGroupMbId;

    std::string localCoverPath;
    std::string onlineCoverUrl;
    std::string onlineCoverSource;
    std::vector<unsigned char> localCoverBytes;
    std::vector<unsigned char> onlineCoverBytes;

    void* localTexture = nullptr;
    void* onlineTexture = nullptr;
    int localWidth = 0, localHeight = 0;
    int onlineWidth = 0, onlineHeight = 0;

    long long localScore = 0;
    long long onlineScore = 0;

    int selectedCoverChoice = 0; // 0 = Local, 1 = Online

    // Multi-Provider Candidates & Consensus
    std::vector<ConsensusAggregator::MetadataCandidate> candidates;
    bool hasConflict{false};
    double confidenceScore{0.0};
    bool isFetchingAll{false};
    int selectedCandidateIndex{-1};
    int selectedCandidateIdx{-1};
};

struct AlbumGroup {
    std::string albumKey;
    std::string album;
    std::string artist;
    std::string year;
    size_t trackCount = 0;
    double confidenceScore = 0.0;
    std::string matchTierName;
    bool hasConflict = false;
    int selectedCoverChoice = 0;
    bool hasLocalCover = false;
    bool hasOnlineCover = false;
    std::string onlineCoverSource;
    std::vector<ConsensusAggregator::MetadataCandidate> candidates;
    std::vector<size_t> trackIndices;
};

struct MirrorProgress {
    std::atomic<bool> isRunning{false};
    std::atomic<size_t> totalTasks{0};
    std::atomic<size_t> completedTasks{0};
    std::atomic<size_t> createdFolders{0};
    std::atomic<size_t> copiedFallbacks{0};
    std::string lastMessage;
};

class CoreEngine {
public:
    static CoreEngine& Instance() {
        static CoreEngine instance;
        return instance;
    }

    void Initialize(const std::string& baseDir);
    void Shutdown();

    // Folder & Configuration Management
    void LoadFolderSettings();
    void SaveFolderSettings();

    std::string GetBaseDir() const { return m_baseDir; }
    std::string GetToSortDir() const { return m_toSortDir; }
    std::string GetOutputDir() const { return m_outputDir; }
    std::string GetFlacDir() const { return m_flacDir; }
    std::string GetMp3Dir() const { return m_mp3Dir; }
    std::string GetAcoustIdKey() const { return m_acoustIdKey; }
    std::string GetDiscogsToken() const { return m_discogsToken; }

    void SetFolders(const std::string& tosort, const std::string& output,
                    const std::string& flac, const std::string& mp3,
                    const std::string& acoustid, const std::string& discogs);

    // Duplicate Detection & Resolution
    void StartDuplicateScan();
    bool IsDuplicateScanning() const { return m_isDuplicateScanning; }
    std::vector<ABCandidatePair> GetDuplicateCandidates();
    bool ResolveDuplicate(size_t index, const std::string& action);

    // Tag & Release Inspection
    void StartTagScan();
    bool IsTagScanning() const { return m_isTagScanning; }
    void GetTagScanProgress(size_t& done, size_t& total, double& fraction, double& speed,
                            std::string& eta, std::string& elapsed);

    std::vector<AlbumGroup> GetAlbumGroups();
    std::vector<TagReviewItem> GetTagItems();
    TagReviewItem GetTagItem(size_t index);

    void ApproveTrack(size_t trackIndex);
    void ApproveAlbum(size_t referenceTrackIndex);
    void SkipTrack(size_t trackIndex);
    void SkipAlbum(size_t referenceTrackIndex);

    void ApplyCandidateToTrack(size_t trackIndex, int candidateIndex);
    void ApplyCandidateToAlbum(size_t referenceTrackIndex, int candidateIndex);

    void UpdateTrackDetails(size_t trackIndex, const std::string& title,
                            const std::string& artist, const std::string& trackNo,
                            const std::string& lyrics);
    void SetCoverChoice(size_t trackIndex, int choice, bool applyToAlbum);

    void FetchManualMusicBrainz(const std::string& inputUrl, size_t referenceTrackIndex, bool applyToAlbum);
    void FetchManualDiscogs(const std::string& inputUrl, size_t referenceTrackIndex, bool applyToAlbum);
    void FetchManualTouhouDb(const std::string& inputUrl, size_t referenceTrackIndex, bool applyToAlbum);
    void FetchManualThwiki(const std::string& inputUrl, size_t referenceTrackIndex, bool applyToAlbum);
    void FetchManualVocaDb(const std::string& inputUrl, size_t referenceTrackIndex, bool applyToAlbum);
    void FetchManualUtaiteDb(const std::string& inputUrl, size_t referenceTrackIndex, bool applyToAlbum);

    std::vector<unsigned char> GetCoverImageBytes(size_t trackIndex, const std::string& type);

    // Collection Mirroring
    void StartMirroring();
    bool IsMirroring() const { return m_mirrorProgress.isRunning; }
    void GetMirrorProgress(size_t& completed, size_t& total, size_t& createdDirs,
                           size_t& copiedFallbacks, bool& isRunning, std::string& msg);

    // Database Sync & Queries
    void SyncTracklistDatabase();
    bool ExportDatabase(const std::string& path);
    std::vector<TrackRecord> QueryDatabase(int filterStatus, int filterFormat, const std::string& searchQuery);
    void GetDatabaseStats(int& total, int& downloaded, int& missing);

    // Waveform Extraction
    std::vector<float> GetWaveformPeaks(const std::string& filePath, int peakCount = 100);

    // Global Stats for Sidebar
    void GetSidebarStats(size_t& duplicatesCount, size_t& unresolvedTagsCount,
                         int& totalTracks, int& downloadedTracks, int& missingTracks);

    std::vector<size_t> GetAlbumTrackIndices(size_t referenceIndex) const;

private:
    CoreEngine() = default;
    ~CoreEngine() = default;

    void ExecuteTrackApprovalBatch(const std::vector<size_t>& indices);

    std::string m_baseDir;
    std::string m_toSortDir;
    std::string m_deleteDir;
    std::string m_outputDir;
    std::string m_flacDir;
    std::string m_mp3Dir;
    std::string m_acoustIdKey;
    std::string m_discogsToken;

    // Duplicates State
    mutable std::mutex m_duplicatesMutex;
    std::vector<ABCandidatePair> m_candidates;
    std::vector<std::string> m_autoDelete;
    std::atomic<bool> m_isDuplicateScanning{false};

    // Tag State
    mutable std::mutex m_tagMutex;
    std::vector<TagReviewItem> m_tagItems;
    std::atomic<bool> m_isTagScanning{false};
    std::atomic<size_t> m_fetchedCount{0};
    std::atomic<size_t> m_tagScanTotal{0};
    std::chrono::steady_clock::time_point m_tagScanStartTime{};
    std::chrono::steady_clock::time_point m_tagScanEndTime{};

    // Mirror State
    MirrorProgress m_mirrorProgress;
};
