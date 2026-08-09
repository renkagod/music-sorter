#include "../include/DatabaseManager.hpp"
#include "../include/Logger.hpp"
#include "../third_party/sqlite3.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

static std::string TrimString(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n*#-_");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n*#-_");
    return str.substr(first, (last - first + 1));
}

static std::string ToLowerString(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return str;
}

DatabaseManager& DatabaseManager::GetInstance() {
    static DatabaseManager instance;
    return instance;
}

DatabaseManager::~DatabaseManager() {
    CloseDatabase();
}

void DatabaseManager::CloseDatabase() {
    if (m_db) {
        sqlite3_close((sqlite3*)m_db);
        m_db = nullptr;
    }
}

bool DatabaseManager::InitDatabase(const std::string& dbPath) {
    if (m_db) return true;

    int rc = sqlite3_open(dbPath.c_str(), (sqlite3**)&m_db);
    if (rc != SQLITE_OK) {
        LOG_INFO("Error opening SQLite database: " + std::string(sqlite3_errmsg((sqlite3*)m_db)));
        return false;
    }

    const char* createSql = 
        "CREATE TABLE IF NOT EXISTS tracks ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "artist TEXT NOT NULL,"
        "album TEXT NOT NULL,"
        "title TEXT NOT NULL,"
        "track_no TEXT DEFAULT '',"
        "year TEXT DEFAULT '',"
        "duration_sec INTEGER DEFAULT 0,"
        "format TEXT DEFAULT 'MISSING',"
        "bitrate_kbps INTEGER DEFAULT 0,"
        "status INTEGER DEFAULT 0,"
        "rel_path TEXT DEFAULT ''"
        ");"
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_artist_album_title ON tracks(artist, album, title);";

    char* errMsgs = nullptr;
    rc = sqlite3_exec((sqlite3*)m_db, createSql, nullptr, nullptr, &errMsgs);
    if (rc != SQLITE_OK) {
        std::string err = errMsgs ? errMsgs : "Unknown error";
        sqlite3_free(errMsgs);
        LOG_INFO("Error creating SQLite schema: " + err);
        return false;
    }

    LOG_INFO("[SQLITE DB] Initialized database successfully at: " + dbPath);
    return true;
}

bool DatabaseManager::ImportFromTracklistMarkdown(const std::string& tracklistPath) {
    if (!m_db || !fs::exists(tracklistPath)) return false;

    std::ifstream inFile(tracklistPath);
    if (!inFile.is_open()) return false;

    std::string line;
    std::string currentArtist = "Unknown Artist";
    std::string currentAlbum = "Unknown Album";

    sqlite3_exec((sqlite3*)m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* insertSql = 
        "INSERT INTO tracks (artist, album, title, track_no, year, duration_sec, format, bitrate_kbps, status, rel_path) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(artist, album, title) DO UPDATE SET "
        "status=excluded.status, format=excluded.format;";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)m_db, insertSql, -1, &stmt, nullptr);

    int importedCount = 0;

    while (std::getline(inFile, line)) {
        std::string trimmed = TrimString(line);
        if (trimmed.empty()) continue;

        if (line.rfind("## 👤 ", 0) == 0) {
            currentArtist = line.substr(7);
            // Strip trailing metadata like (-45) or ;;;;5
            size_t semi = currentArtist.find(';');
            if (semi != std::string::npos) currentArtist = currentArtist.substr(0, semi);
            currentArtist = TrimString(currentArtist);
            if (currentArtist.empty()) currentArtist = "Unknown Artist";
            continue;
        }

        if (line.rfind("### 💿 ", 0) == 0) {
            currentAlbum = line.substr(8);
            currentAlbum = TrimString(currentAlbum);
            if (currentAlbum.empty()) currentAlbum = "Unknown Album";
            continue;
        }

        size_t boxUnchecked = line.find("- [ ]");
        size_t boxChecked = line.find("- [x]");

        if (boxUnchecked != std::string::npos || boxChecked != std::string::npos) {
            int status = (boxChecked != std::string::npos) ? 1 : 0;
            size_t boxPos = (status == 1) ? boxChecked : boxUnchecked;
            std::string content = line.substr(boxPos + 5);

            std::string title = content;
            std::string fmt = "MISSING";
            if (title.find("[FLAC]") != std::string::npos) fmt = "FLAC";
            else if (title.find("[MP3]") != std::string::npos) fmt = "MP3";

            // Strip **bold**, (duration), [FLAC]
            size_t boldStart = title.find("**");
            if (boldStart != std::string::npos) {
                size_t boldEnd = title.find("**", boldStart + 2);
                if (boldEnd != std::string::npos) {
                    title = title.substr(boldStart + 2, boldEnd - (boldStart + 2));
                }
            }

            title = TrimString(title);
            if (title.empty()) continue;

            sqlite3_bind_text(stmt, 1, currentArtist.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, currentAlbum.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, "", -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, "", -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 6, 0);
            sqlite3_bind_text(stmt, 7, fmt.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 8, status == 1 ? 320 : 0);
            sqlite3_bind_int(stmt, 9, status);
            sqlite3_bind_text(stmt, 10, "", -1, SQLITE_TRANSIENT);

            sqlite3_step(stmt);
            sqlite3_reset(stmt);
            importedCount++;
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_exec((sqlite3*)m_db, "COMMIT;", nullptr, nullptr, nullptr);
    inFile.close();

    LOG_INFO("[SQLITE DB] Imported " + std::to_string(importedCount) + " tracks from tracklist.md into SQLite database.");
    return true;
}

void DatabaseManager::SyncCollectionWithDisk(const std::string& baseDir) {
    if (!m_db) return;

    sqlite3_exec((sqlite3*)m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* updateSql = 
        "INSERT INTO tracks (artist, album, title, track_no, year, duration_sec, format, bitrate_kbps, status, rel_path) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 1, ?) "
        "ON CONFLICT(artist, album, title) DO UPDATE SET "
        "status=1, format=excluded.format, bitrate_kbps=excluded.bitrate_kbps, rel_path=excluded.rel_path;";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)m_db, updateSql, -1, &stmt, nullptr);

    int diskSyncCount = 0;

    for (const auto& sub : { "flac", "mp3", "TO SORT", "review" }) {
        fs::path dir = fs::path(baseDir) / sub;
        if (fs::exists(dir)) {
            for (auto& entry : fs::recursive_directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".flac" || ext == ".mp3") {
                        std::string album = entry.path().parent_path().filename().string();
                        std::string artist = entry.path().parent_path().parent_path().filename().string();
                        std::string filename = entry.path().stem().string();
                        std::string trackNo = "";
                        std::string title = filename;

                        size_t dotPos = filename.find(". ");
                        if (dotPos == std::string::npos) dotPos = filename.find("- ");
                        if (dotPos != std::string::npos && dotPos <= 4 && !filename.empty() && std::isdigit((unsigned char)filename[0])) {
                            trackNo = filename.substr(0, dotPos);
                            title = filename.substr(dotPos + 2);
                        }

                        if (artist.empty() || artist == "TO SORT" || artist == "media" || artist == "music") {
                            artist = "Unknown Artist";
                        }
                        if (album.empty() || album == "TO SORT") {
                            album = "Unknown Album";
                        }

                        std::string fmt = (ext == ".flac") ? "FLAC" : "MP3";
                        int bitrate = (ext == ".flac") ? 1411 : 320;
                        std::string relP = fs::relative(entry.path(), baseDir).string();

                        sqlite3_bind_text(stmt, 1, artist.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 2, album.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 4, trackNo.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 5, "", -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int(stmt, 6, 0);
                        sqlite3_bind_text(stmt, 7, fmt.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int(stmt, 8, bitrate);
                        sqlite3_bind_text(stmt, 9, relP.c_str(), -1, SQLITE_TRANSIENT);

                        sqlite3_step(stmt);
                        sqlite3_reset(stmt);
                        diskSyncCount++;
                    }
                }
            }
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_exec((sqlite3*)m_db, "COMMIT;", nullptr, nullptr, nullptr);

    LOG_INFO("[SQLITE DB] Synced " + std::to_string(diskSyncCount) + " audio files on disk with SQLite database.");
}

std::vector<TrackRecord> DatabaseManager::QueryTracks(int filterStatus, int filterFormat, const std::string& searchQuery) {
    std::vector<TrackRecord> results;
    if (!m_db) return results;

    std::string sql = "SELECT id, artist, album, title, track_no, year, duration_sec, format, bitrate_kbps, status, rel_path FROM tracks WHERE 1=1";

    if (filterStatus == 1) sql += " AND status=1";
    else if (filterStatus == 2) sql += " AND status=0";

    if (filterFormat == 1) sql += " AND format='FLAC'";
    else if (filterFormat == 2) sql += " AND format='MP3'";

    if (!searchQuery.empty()) {
        sql += " AND (artist LIKE ? OR album LIKE ? OR title LIKE ?)";
    }

    sql += " ORDER BY artist ASC, album ASC, track_no ASC, title ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    if (!searchQuery.empty()) {
        std::string q = "%" + searchQuery + "%";
        sqlite3_bind_text(stmt, 1, q.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, q.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, q.c_str(), -1, SQLITE_TRANSIENT);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TrackRecord rec;
        rec.id = sqlite3_column_int(stmt, 0);
        rec.artist = (const char*)sqlite3_column_text(stmt, 1);
        rec.album = (const char*)sqlite3_column_text(stmt, 2);
        rec.title = (const char*)sqlite3_column_text(stmt, 3);
        rec.trackNo = (const char*)sqlite3_column_text(stmt, 4);
        rec.year = (const char*)sqlite3_column_text(stmt, 5);
        rec.durationSec = sqlite3_column_int(stmt, 6);
        rec.format = (const char*)sqlite3_column_text(stmt, 7);
        rec.bitrateKbps = sqlite3_column_int(stmt, 8);
        rec.status = sqlite3_column_int(stmt, 9);
        rec.relPath = (const char*)sqlite3_column_text(stmt, 10);
        results.push_back(rec);
    }

    sqlite3_finalize(stmt);
    return results;
}

bool DatabaseManager::ExportToCleanTracklistMarkdown(const std::string& tracklistPath) {
    if (!m_db) return false;

    std::vector<TrackRecord> allTracks = QueryTracks(0, 0, "");
    if (allTracks.empty()) return false;

    std::ofstream outFile(tracklistPath);
    if (!outFile.is_open()) return false;

    outFile << "# Список исполнителей, альбомов и треков\n\n";

    std::string lastArtist = "";
    std::string lastAlbum = "";

    for (const auto& tr : allTracks) {
        if (tr.artist != lastArtist) {
            lastArtist = tr.artist;
            lastAlbum = "";
            outFile << "## 👤 " << lastArtist << "\n\n";
        }
        if (tr.album != lastAlbum) {
            lastAlbum = tr.album;
            outFile << "### 💿 " << lastAlbum << "\n\n";
        }

        std::string mark = (tr.status == 1) ? "- [x]" : "- [ ]";
        outFile << mark << " **" << tr.title << "**";
        if (tr.status == 1 && !tr.format.empty() && tr.format != "MISSING") {
            outFile << " [" << tr.format << "]";
        }
        outFile << "\n";
    }

    outFile.close();
    LOG_INFO("[SQLITE DB] Exported 100% clean tracklist.md to: " + tracklistPath);
    return true;
}

int DatabaseManager::GetTotalTracksCount() {
    if (!m_db) return 0;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)m_db, "SELECT COUNT(*) FROM tracks;", -1, &stmt, nullptr);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

int DatabaseManager::GetDownloadedCount() {
    if (!m_db) return 0;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)m_db, "SELECT COUNT(*) FROM tracks WHERE status=1;", -1, &stmt, nullptr);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

int DatabaseManager::GetMissingCount() {
    if (!m_db) return 0;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)m_db, "SELECT COUNT(*) FROM tracks WHERE status=0;", -1, &stmt, nullptr);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}
