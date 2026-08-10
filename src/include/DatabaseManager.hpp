#pragma once

#include <string>
#include <vector>

struct TrackRecord {
    int id = 0;
    std::string artist;
    std::string album;
    std::string title;
    std::string trackNo;
    std::string year;
    int durationSec = 0;
    std::string format;     // "FLAC", "MP3", "MISSING"
    int bitrateKbps = 0;
    int status = 0;          // 1 = [x] Downloaded, 0 = [ ] Missing
    std::string relPath;
};

class DatabaseManager {
public:
    static DatabaseManager& GetInstance();

    bool InitDatabase(const std::string& dbPath);
    void CloseDatabase();

    bool ImportFromTracklistMarkdown(const std::string& tracklistPath);
    void SyncCollectionWithDisk(const std::string& baseDir);

    std::vector<TrackRecord> QueryTracks(int filterStatus, int filterFormat, const std::string& searchQuery);
    bool ExportToCleanTracklistMarkdown(const std::string& tracklistPath);

    int GetTotalTracksCount();
    int GetDownloadedCount();
    int GetMissingCount();

private:
    DatabaseManager() = default;
    ~DatabaseManager();

    void* m_db = nullptr; // sqlite3* handle
};
