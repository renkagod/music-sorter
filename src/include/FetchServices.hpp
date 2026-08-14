#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <thread>
#include <future>
#include <atomic>
#include <regex>
#include <filesystem>
#include <windows.h>
#include <wininet.h>

#include "MetadataUtils.hpp"
#include "Logger.hpp"

#pragma comment(lib, "wininet.lib")

struct MBTrackEntry {
    int position = 0;
    std::string title;
    std::string titleRomaji;
    std::string titleEnglish;
    std::string titleJapanese;
    std::string artist;
    std::string artistRomaji;
    std::string artistEnglish;
    std::string artistJapanese;
    int lengthMs = 0;
};

struct MBReleaseGroupCandidate {
    std::string id;
    std::string title;
    std::string firstReleaseDate;
    std::string artistCredit;
    int score = 0;
};

struct DiscogsReleaseInfo {
    std::string id;
    std::string title;
    std::string artist;
    std::string year;
    std::string coverUrl;
    std::vector<MBTrackEntry> tracks;
};

struct VdbReleaseInfo {
    int id = 0;
    std::string service; // "TouhouDB", "VocaDB", "UtaiteDB"
    std::string title;
    std::string titleRomaji;
    std::string titleEnglish;
    std::string titleJapanese;
    std::string artist;
    std::string artistRomaji;
    std::string artistEnglish;
    std::string artistJapanese;
    std::string catalogNumber;
    std::string releaseDate;
    std::string coverUrl;
    std::vector<MBTrackEntry> tracks;
};

inline std::string PickBestName(const std::string& romaji, const std::string& english, const std::string& japanese, const std::string& def) {
    if (!romaji.empty()) return romaji;
    if (!english.empty()) return english;
    if (!japanese.empty()) return japanese;
    return def;
}

inline bool ContainsCJK(const std::string& str) {
    for (size_t i = 0; i < str.length(); ) {
        unsigned char c = (unsigned char)str[i];
        if (c < 0x80) {
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 < str.length()) {
                unsigned char c2 = (unsigned char)str[i+1];
                unsigned char c3 = (unsigned char)str[i+2];
                uint32_t cp = ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
                if ((cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF)) {
                    return true;
                }
            }
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            i += 4;
        } else {
            i++;
        }
    }
    return false;
}

struct AcoustIdResult {
    std::string recordingId;
    std::string title;
    std::vector<std::string> artists;
    std::vector<std::string> releaseGroupIds;
    double score = 0.0;
};

namespace FetchServices {

using ::UrlEncode;
using ::ParseDurationMs;
using ::ParseDiscogsPosition;
using ::CleanMetadataString;
using ::CleanAlbumTitle;
using ::EscapeLuceneQuery;
using ::RomajiToKatakana;
using ::NormalizeKey;
using ::Utf8ToWide;
using ::WideToUtf8;
using ::ExtractYearFromString;
using ::ExtractCatalogNumber;
using ::ExtractArtistFromFilename;

inline std::mutex g_mbThrottleMutex;
inline std::chrono::steady_clock::time_point g_lastMbRequestTime;

inline void MusicBrainzThrottle() {
    std::lock_guard<std::mutex> lock(g_mbThrottleMutex);
    auto now = std::chrono::steady_clock::now();
    auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastMbRequestTime).count();
    const long long kMinGapMs = 1100;
    if (sinceLast < kMinGapMs) {
        long long sleepMs = kMinGapMs - sinceLast;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    g_lastMbRequestTime = std::chrono::steady_clock::now();
}

inline std::mutex g_discogsThrottleMutex;
inline std::chrono::steady_clock::time_point g_lastDiscogsRequestTime;

inline void DiscogsThrottle(bool isAuthenticated = true) {
    std::lock_guard<std::mutex> lock(g_discogsThrottleMutex);
    auto now = std::chrono::steady_clock::now();
    auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastDiscogsRequestTime).count();
    // 60 requests/min (1100ms) with token, 25 requests/min (2500ms) without token
    const long long kMinGapMs = isAuthenticated ? 1100 : 2500;
    if (sinceLast < kMinGapMs) {
        long long sleepMs = kMinGapMs - sinceLast;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    g_lastDiscogsRequestTime = std::chrono::steady_clock::now();
}

inline std::mutex g_lrclibThrottleMutex;
inline std::chrono::steady_clock::time_point g_lastLrclibRequestTime;

inline void LrcLibThrottle() {
    std::lock_guard<std::mutex> lock(g_lrclibThrottleMutex);
    auto now = std::chrono::steady_clock::now();
    auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastLrclibRequestTime).count();
    const long long kMinGapMs = 300; // 300 ms delay between sequential LRCLIB requests (recommended 200-500ms range)
    if (sinceLast < kMinGapMs) {
        long long sleepMs = kMinGapMs - sinceLast;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    g_lastLrclibRequestTime = std::chrono::steady_clock::now();
}

inline std::mutex g_acoustIdThrottleMutex;
inline std::chrono::steady_clock::time_point g_lastAcoustIdRequestTime;

inline void AcoustIdThrottle() {
    std::lock_guard<std::mutex> lock(g_acoustIdThrottleMutex);
    auto now = std::chrono::steady_clock::now();
    auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastAcoustIdRequestTime).count();
    const long long kMinGapMs = 340; // Max 3 requests/sec per official AcoustID guidelines (340ms spacing)
    if (sinceLast < kMinGapMs) {
        long long sleepMs = kMinGapMs - sinceLast;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    g_lastAcoustIdRequestTime = std::chrono::steady_clock::now();
}


inline std::vector<unsigned char> HttpGetBytes(const std::wstring& url, const std::string& discogsToken = "", int maxRetries = 3) {
    std::vector<unsigned char> result;
    std::string narrowUrl = WideToUtf8(url);

    bool isMusicBrainz = (narrowUrl.find("musicbrainz.org") != std::string::npos);
    if (isMusicBrainz) MusicBrainzThrottle();

    bool isDiscogs = (narrowUrl.find("api.discogs.com") != std::string::npos);
    if (isDiscogs) DiscogsThrottle(!discogsToken.empty());

    bool isLrcLib = (narrowUrl.find("lrclib.net") != std::string::npos);
    if (isLrcLib) LrcLibThrottle();

    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        HINTERNET hNet = InternetOpenW(L"MusicSorter/2.0 (https://github.com/renkagod/music-sorter)", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (hNet) {
            DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
            std::wstring customHeaders;
            if (isDiscogs && !discogsToken.empty()) {
                customHeaders = L"Authorization: Discogs token=" + Utf8ToWide(discogsToken) + L"\r\nUser-Agent: MusicSorter/2.0 (https://github.com/renkagod/music-sorter)\r\n";
            } else if (isLrcLib) {
                customHeaders = L"User-Agent: MusicSorter v2.0 (https://github.com/renkagod/music-sorter)\r\nLrclib-Client: MusicSorter v2.0 (https://github.com/renkagod/music-sorter)\r\n";
            }
            HINTERNET hFile = InternetOpenUrlW(hNet, url.c_str(), customHeaders.empty() ? NULL : customHeaders.c_str(), (DWORD)customHeaders.length(), flags, 0);
            if (!hFile) {
                flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
                hFile = InternetOpenUrlW(hNet, url.c_str(), customHeaders.empty() ? NULL : customHeaders.c_str(), (DWORD)customHeaders.length(), flags, 0);
            }
            if (hFile) {
                DWORD statusCode = 0;
                DWORD statusSize = sizeof(statusCode);
                HttpQueryInfoW(hFile, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, NULL);

                if (statusCode == 429 || statusCode == 503) {
                    long long backoffMs = (isMusicBrainz || isDiscogs) ? (1500 * (attempt + 1)) : (400 * (attempt + 1));
                    
                    wchar_t retryAfterBuf[64] = { 0 };
                    DWORD retryAfterSize = sizeof(retryAfterBuf);
                    if (HttpQueryInfoW(hFile, HTTP_QUERY_RETRY_AFTER, retryAfterBuf, &retryAfterSize, NULL)) {
                        try {
                            int sec = std::stoi(WideToUtf8(retryAfterBuf));
                            if (sec > 0) {
                                backoffMs = (long long)sec * 1000;
                                LOG_INFO("[HTTP 429 RETRY-AFTER] Honoring Retry-After header: " + std::to_string(sec) + "s for URL: " + narrowUrl);
                            }
                        } catch (...) {}
                    } else {
                        LOG_INFO("[HTTP " + std::to_string(statusCode) + " RATE LIMIT] Backing off " + std::to_string(backoffMs) + "ms for URL: " + narrowUrl);
                    }

                    InternetCloseHandle(hFile);
                    InternetCloseHandle(hNet);
                    std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
                    if (isMusicBrainz) {
                        std::lock_guard<std::mutex> lock(g_mbThrottleMutex);
                        g_lastMbRequestTime = std::chrono::steady_clock::now();
                    }
                    if (isDiscogs) {
                        std::lock_guard<std::mutex> lock(g_discogsThrottleMutex);
                        g_lastDiscogsRequestTime = std::chrono::steady_clock::now();
                    }
                    if (isLrcLib) {
                        std::lock_guard<std::mutex> lock(g_lrclibThrottleMutex);
                        g_lastLrclibRequestTime = std::chrono::steady_clock::now();
                    }
                    continue;
                }

                unsigned char buffer[16384];
                DWORD bytesRead = 0;
                while (InternetReadFile(hFile, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                    result.insert(result.end(), buffer, buffer + bytesRead);
                }
                InternetCloseHandle(hFile);
                
                if (statusCode != 200 && statusCode != 0) {
                    LOG_INFO("[HTTP RESP " + std::to_string(statusCode) + "] Received " + std::to_string(result.size()) + " bytes from " + narrowUrl);
                }
            } else {
                DWORD err = GetLastError();
                LOG_INFO("[HTTP ERROR " + std::to_string(err) + "] Failed to open URL (Attempt " + std::to_string(attempt + 1) + "/" + std::to_string(maxRetries) + "): " + narrowUrl);
            }
            InternetCloseHandle(hNet);
        }
        if (!result.empty()) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(250 * (attempt + 1)));
    }
    return result;
}

inline std::string HttpGetString(const std::wstring& url, const std::string& discogsToken = "") {
    auto bytes = HttpGetBytes(url, discogsToken);
    if (bytes.empty()) return "";
    return std::string((char*)bytes.data(), bytes.size());
}

inline std::string AcoustIdHttpPost(const std::string& postData) {
    AcoustIdThrottle();
    std::vector<unsigned char> result;
    HINTERNET hNet = InternetOpenW(L"MusicSorter/2.0 (https://github.com/renkagod/music-sorter)", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hNet) return "";

    HINTERNET hConnect = InternetConnectW(hNet, L"api.acoustid.org", INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (hConnect) {
        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
        HINTERNET hRequest = HttpOpenRequestW(hConnect, L"POST", L"/v2/lookup", NULL, NULL, NULL, flags, 0);
        if (hRequest) {
            std::wstring headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
            BOOL sent = HttpSendRequestW(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)postData.c_str(), (DWORD)postData.length());
            if (sent) {
                unsigned char buffer[16384];
                DWORD bytesRead = 0;
                while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                    result.insert(result.end(), buffer, buffer + bytesRead);
                }
            } else {
                LOG_INFO("[ACOUSTID POST ERROR] HttpSendRequest failed with error: " + std::to_string(GetLastError()));
            }
            InternetCloseHandle(hRequest);
        }
        InternetCloseHandle(hConnect);
    }
    InternetCloseHandle(hNet);
    return std::string((char*)result.data(), result.size());
}

inline std::vector<AcoustIdResult> ParseAcoustIdResponse(const std::string& resJson) {
    std::vector<AcoustIdResult> out;
    if (resJson.empty()) return out;
    size_t p = 0;
    JsonVal doc = ParseJsonSimple(resJson, p);
    if (doc.get("status").strVal != "ok") return out;

    const auto& results = doc.get("results");
    if (results.type != JsonVal::Array) return out;

    for (size_t i = 0; i < results.arrVal.size(); ++i) {
        const auto& r = results.get(i);
        double score = r.get("score").numVal;
        const auto& recordings = r.get("recordings");
        if (recordings.type == JsonVal::Array) {
            for (size_t recIdx = 0; recIdx < recordings.arrVal.size(); ++recIdx) {
                const auto& rec = recordings.get(recIdx);
                AcoustIdResult item;
                item.recordingId = rec.get("id").strVal;
                item.title = rec.get("title").strVal;
                item.score = score;

                const auto& artists = rec.get("artists");
                if (artists.type == JsonVal::Array) {
                    for (size_t a = 0; a < artists.arrVal.size(); ++a) {
                        std::string aName = artists.get(a).get("name").strVal;
                        if (!aName.empty()) item.artists.push_back(aName);
                    }
                }

                const auto& rgs = rec.get("releasegroups");
                if (rgs.type == JsonVal::Array) {
                    for (size_t rg = 0; rg < rgs.arrVal.size(); ++rg) {
                        std::string rgId = rgs.get(rg).get("id").strVal;
                        if (!rgId.empty()) item.releaseGroupIds.push_back(rgId);
                    }
                }
                out.push_back(item);
            }
        }
    }
    return out;
}

inline std::vector<MBReleaseGroupCandidate> ParseMusicBrainzReleaseGroups(const std::string& resJson) {
    std::vector<MBReleaseGroupCandidate> candidates;
    if (resJson.empty()) return candidates;
    size_t p = 0;
    JsonVal doc = ParseJsonSimple(resJson, p);
    const auto& rgs = doc.get("release-groups");
    if (rgs.type != JsonVal::Array) return candidates;

    for (size_t i = 0; i < rgs.arrVal.size(); ++i) {
        const auto& rg = rgs.get(i);
        std::string id = rg.get("id").strVal;
        if (id.length() != 36) continue;

        std::string title = rg.get("title").strVal;
        std::string date = rg.get("first-release-date").strVal;
        int score = (int)rg.get("score").numVal;
        std::string artistCredit;

        const auto& ac = rg.get("artist-credit");
        if (ac.type == JsonVal::Array) {
            for (size_t a = 0; a < ac.arrVal.size(); ++a) {
                std::string aName = ac.get(a).get("name").strVal;
                if (!aName.empty()) {
                    if (!artistCredit.empty()) artistCredit += ", ";
                    artistCredit += aName;
                }
            }
        }

        candidates.push_back({ id, title, date, artistCredit, score });
    }
    return candidates;
}

inline std::vector<MBTrackEntry> ParseMusicBrainzReleaseTracksJson(const std::string& resJson, std::string* outReleaseDate = nullptr) {
    std::vector<MBTrackEntry> tracks;
    if (resJson.empty()) return tracks;

    size_t p = 0;
    JsonVal doc = ParseJsonSimple(resJson, p);

    const auto& rels = doc.get("releases");
    if (rels.type != JsonVal::Array || rels.arrVal.empty()) return tracks;

    if (outReleaseDate && outReleaseDate->empty()) {
        for (size_t ri = 0; ri < rels.arrVal.size(); ++ri) {
            std::string d = rels.get(ri).get("date").strVal;
            if (!d.empty()) {
                if (outReleaseDate->empty() || d < *outReleaseDate) {
                    *outReleaseDate = d;
                }
            }
        }
    }

    auto extractTracks = [](const JsonVal& mediaVal, std::vector<MBTrackEntry>& out) {
        if (mediaVal.type != JsonVal::Array || mediaVal.arrVal.empty()) return;
        for (size_t m = 0; m < mediaVal.arrVal.size(); ++m) {
            const auto& trs = mediaVal.get(m).get("tracks");
            if (trs.type != JsonVal::Array) continue;
            for (size_t i = 0; i < trs.arrVal.size(); ++i) {
                const auto& tObj = trs.get(i);
                int pos = (int)tObj.get("position").numVal;
                std::string title = tObj.get("title").strVal;
                if (title.empty()) title = tObj.get("recording").get("title").strVal;
                int lengthMs = (int)tObj.get("length").numVal;
                if (lengthMs == 0) lengthMs = (int)tObj.get("recording").get("length").numVal;
                std::string artist;
                const auto& ac = tObj.get("artist-credit");
                if (ac.type == JsonVal::Array && !ac.arrVal.empty()) {
                    artist = ac.get(0).get("name").strVal;
                } else {
                    const auto& recAc = tObj.get("recording").get("artist-credit");
                    if (recAc.type == JsonVal::Array && !recAc.arrVal.empty()) {
                        artist = recAc.get(0).get("name").strVal;
                    }
                }
                if (pos > 0 && !title.empty()) {
                    out.push_back({ pos, title, "", "", "", artist, "", "", "", lengthMs });
                }
            }
        }
    };

    // 1. Prefer Pseudo-Release (romanized titles)
    for (const auto& rel : rels.arrVal) {
        std::string status = rel.get("status").strVal;
        if (status == "Pseudo-Release") {
            extractTracks(rel.get("media"), tracks);
            if (!tracks.empty()) return tracks;
        }
    }

    // 2. Fallback to first Official release
    for (const auto& rel : rels.arrVal) {
        std::string status = rel.get("status").strVal;
        if (status == "Official") {
            extractTracks(rel.get("media"), tracks);
            if (!tracks.empty()) return tracks;
        }
    }

    // 3. Last resort
    extractTracks(rels.get(0).get("media"), tracks);
    return tracks;
}

inline std::vector<MBTrackEntry> FetchMusicBrainzReleaseTracks(const std::string& releaseGroupMbId, std::string* outReleaseDate = nullptr) {
    if (releaseGroupMbId.empty()) return {};
    std::string url = "https://musicbrainz.org/ws/2/release?release-group=" + releaseGroupMbId + "&inc=recordings+artist-credits&fmt=json";
    std::string resJson = HttpGetString(Utf8ToWide(url));
    return ParseMusicBrainzReleaseTracksJson(resJson, outReleaseDate);
}

inline bool ParseDiscogsReleaseDetailsJson(const std::string& jsonStr, const std::string& releaseId, DiscogsReleaseInfo& outInfo) {
    if (jsonStr.empty()) return false;
    size_t p = 0;
    JsonVal doc = ParseJsonSimple(jsonStr, p);
    outInfo.id = releaseId;
    outInfo.title = doc.get("title").strVal;
    
    // Year / Released date
    if (doc.get("released").type == JsonVal::String && !doc.get("released").strVal.empty()) {
        outInfo.year = doc.get("released").strVal;
    } else if (doc.get("released_formatted").type == JsonVal::String && !doc.get("released_formatted").strVal.empty()) {
        outInfo.year = doc.get("released_formatted").strVal;
    } else if (doc.get("year").type == JsonVal::Number && doc.get("year").numVal > 0) {
        outInfo.year = std::to_string((int)doc.get("year").numVal);
    } else if (doc.get("year").type == JsonVal::String) {
        outInfo.year = doc.get("year").strVal;
    }

    // Artist
    const auto& artists = doc.get("artists");
    if (artists.type == JsonVal::Array && !artists.arrVal.empty()) {
        std::string artName;
        for (size_t a = 0; a < artists.arrVal.size(); ++a) {
            std::string name = artists.get(a).get("name").strVal;
            size_t paren = name.rfind(" (");
            if (paren != std::string::npos && paren + 3 < name.size() && name.back() == ')') {
                bool isNum = true;
                for (size_t k = paren + 2; k < name.size() - 1; ++k) {
                    if (!std::isdigit((unsigned char)name[k])) { isNum = false; break; }
                }
                if (isNum) name = name.substr(0, paren);
            }
            if (!artName.empty()) artName += ", ";
            artName += name;
        }
        outInfo.artist = artName;
    }

    // Images
    const auto& images = doc.get("images");
    if (images.type == JsonVal::Array && !images.arrVal.empty()) {
        for (size_t imgIdx = 0; imgIdx < images.arrVal.size(); ++imgIdx) {
            const auto& img = images.get(imgIdx);
            std::string imgType = img.get("type").strVal;
            std::string uri = img.get("uri").strVal;
            if (uri.empty()) uri = img.get("resource_url").strVal;
            if (!uri.empty()) {
                if (imgType == "primary" || outInfo.coverUrl.empty()) {
                    outInfo.coverUrl = uri;
                    if (imgType == "primary") break;
                }
            }
        }
    }

    // Tracklist
    const auto& trkList = doc.get("tracklist");
    if (trkList.type == JsonVal::Array) {
        int trackPosCounter = 1;
        for (size_t t = 0; t < trkList.arrVal.size(); ++t) {
            const auto& trk = trkList.get(t);
            std::string trkType = trk.get("type_").strVal;
            if (!trkType.empty() && trkType != "track") continue;

            std::string posStr = trk.get("position").strVal;
            std::string trkTitle = trk.get("title").strVal;
            std::string durStr = trk.get("duration").strVal;
            int durMs = ParseDurationMs(durStr);
            int pos = ParseDiscogsPosition(posStr, trackPosCounter);

            std::string trkArtist = outInfo.artist;
            const auto& trkArtists = trk.get("artists");
            if (trkArtists.type == JsonVal::Array && !trkArtists.arrVal.empty()) {
                std::string tArt = trkArtists.get(0).get("name").strVal;
                size_t paren = tArt.rfind(" (");
                if (paren != std::string::npos && paren + 3 < tArt.size() && tArt.back() == ')') {
                    tArt = tArt.substr(0, paren);
                }
                if (!tArt.empty()) trkArtist = tArt;
            }

            if (!trkTitle.empty()) {
                outInfo.tracks.push_back({ pos, trkTitle, "", "", "", trkArtist, "", "", "", durMs });
            }
            trackPosCounter++;
        }
    }

    return !outInfo.title.empty() || !outInfo.tracks.empty();
}

inline bool FetchDiscogsReleaseDetails(const std::string& releaseId, bool isMaster, DiscogsReleaseInfo& outInfo, const std::string& discogsToken = "") {
    std::string endpoint = isMaster ? 
        ("https://api.discogs.com/masters/" + releaseId) : 
        ("https://api.discogs.com/releases/" + releaseId);
    if (!discogsToken.empty()) {
        endpoint += "?token=" + discogsToken;
    }
    std::string jsonStr = HttpGetString(Utf8ToWide(endpoint), discogsToken);
    if (jsonStr.empty()) return false;

    bool ok = ParseDiscogsReleaseDetailsJson(jsonStr, releaseId, outInfo);
    if (!ok) return false;

    // Master fallback if date still empty
    if (outInfo.year.empty() || outInfo.year == "0") {
        size_t p = 0;
        JsonVal doc = ParseJsonSimple(jsonStr, p);
        std::string masterId;
        if (doc.get("master_id").type == JsonVal::Number && doc.get("master_id").numVal > 0) {
            masterId = std::to_string((int)doc.get("master_id").numVal);
        }
        if (!masterId.empty()) {
            std::string masterUrl = "https://api.discogs.com/masters/" + masterId;
            if (!discogsToken.empty()) masterUrl += "?token=" + discogsToken;
            std::string masterJson = HttpGetString(Utf8ToWide(masterUrl), discogsToken);
            if (!masterJson.empty()) {
                size_t mp = 0;
                JsonVal mDoc = ParseJsonSimple(masterJson, mp);
                if (mDoc.get("year").type == JsonVal::Number && mDoc.get("year").numVal > 0) {
                    outInfo.year = std::to_string((int)mDoc.get("year").numVal);
                }
            }
        }
    }

    return true;
}

inline bool SearchDiscogsRelease(const std::string& artist, const std::string& album, DiscogsReleaseInfo& outInfo, const std::string& discogsToken = "") {
    if (album.empty()) return false;

    bool isArtistUnknown = (artist.empty() || artist == "Unknown Artist" || artist == "Various Artists" || artist == "V.A." || artist == "VA");

    std::string queryUrl = "https://api.discogs.com/database/search?release_title=" + UrlEncode(album);
    if (!isArtistUnknown) {
        queryUrl += "&artist=" + UrlEncode(artist);
    }
    queryUrl += "&type=release";
    if (!discogsToken.empty()) {
        queryUrl += "&token=" + discogsToken;
    }

    LOG_INFO("[DISCOGS SEARCH] Querying: " + queryUrl);
    std::string resJson = HttpGetString(Utf8ToWide(queryUrl), discogsToken);
    if (resJson.empty() || resJson.find("\"results\":[]") != std::string::npos) {
        std::string broadQuery = isArtistUnknown ? album : (artist + " " + album);
        std::string broadUrl = "https://api.discogs.com/database/search?q=" + UrlEncode(broadQuery) + "&type=release";
        if (!discogsToken.empty()) broadUrl += "&token=" + discogsToken;
        LOG_INFO("[DISCOGS SEARCH FALLBACK] Querying: " + broadUrl);
        resJson = HttpGetString(Utf8ToWide(broadUrl), discogsToken);
    }

    if (resJson.empty()) return false;

    size_t p = 0;
    JsonVal doc = ParseJsonSimple(resJson, p);
    const auto& results = doc.get("results");
    if (results.type != JsonVal::Array || results.arrVal.empty()) {
        LOG_INFO("[DISCOGS SEARCH] No results found for: " + artist + " - " + album);
        return false;
    }

    int bestScore = -10000;
    std::string pickedId;
    std::string pickedCover;
    std::string pickedYear;
    std::string pickedTitle;
    bool isMasterPicked = false;

    std::string albNorm = NormalizeKey(album);

    for (size_t i = 0; i < results.arrVal.size(); ++i) {
        const auto& r = results.get(i);
        std::string rId;
        if (r.get("id").type == JsonVal::Number) rId = std::to_string((int)r.get("id").numVal);
        else rId = r.get("id").strVal;
        if (rId.empty()) continue;

        std::string rTitle = r.get("title").strVal;
        std::string rYear;
        if (r.get("year").type == JsonVal::Number) rYear = std::to_string((int)r.get("year").numVal);
        else rYear = r.get("year").strVal;
        std::string rCover = r.get("cover_image").strVal;
        bool isMaster = (r.get("type").strVal == "master");

        std::string rTitleNorm = NormalizeKey(rTitle);
        bool titleMatch = (!albNorm.empty() && (rTitleNorm.find(albNorm) != std::string::npos || albNorm.find(rTitleNorm) != std::string::npos));

        int score = 0;
        if (titleMatch) score += 100;
        if (!rYear.empty() && rYear != "0") score += 30;
        if (!rCover.empty()) score += 20;

        const auto& fmtArr = r.get("formats");
        if (fmtArr.type == JsonVal::Array) {
            for (size_t f = 0; f < fmtArr.arrVal.size(); ++f) {
                const auto& fObj = fmtArr.get(f);
                const auto& descArr = fObj.get("descriptions");
                if (descArr.type == JsonVal::Array) {
                    for (size_t d = 0; d < descArr.arrVal.size(); ++d) {
                        std::string dLower = descArr.get(d).strVal;
                        std::transform(dLower.begin(), dLower.end(), dLower.begin(), ::tolower);
                        if (dLower == "album") score += 15;
                        if (dLower.find("test") != std::string::npos) score -= 50;
                        if (dLower.find("promo") != std::string::npos) score -= 30;
                        if (dLower.find("unofficial") != std::string::npos) score -= 60;
                    }
                }
            }
        }

        double haveCount = r.get("community").get("have").numVal;
        double wantCount = r.get("community").get("want").numVal;
        score += (int)((haveCount + wantCount) / 2.0);

        if (score > bestScore) {
            bestScore = score;
            pickedId = rId;
            pickedCover = rCover;
            pickedYear = rYear;
            pickedTitle = rTitle;
            isMasterPicked = isMaster;
        }
    }

    if (pickedId.empty()) return false;

    LOG_INFO("[DISCOGS MATCHED] Selected release ID: " + pickedId + " (" + pickedTitle + ", Score: " + std::to_string(bestScore) + "). Fetching details...");
    bool ok = FetchDiscogsReleaseDetails(pickedId, isMasterPicked, outInfo, discogsToken);
    if (!ok && !pickedTitle.empty()) {
        outInfo.id = pickedId;
        outInfo.title = pickedTitle;
        outInfo.year = pickedYear;
        outInfo.coverUrl = pickedCover;
        return true;
    }
    if (outInfo.coverUrl.empty() && !pickedCover.empty()) {
        outInfo.coverUrl = pickedCover;
    }
    return ok;
}

inline bool ParseVdbAlbumDetailsJson(const std::string& jsonStr, const std::string& service, int albumId, VdbReleaseInfo& outInfo) {
    if (jsonStr.empty()) return false;

    size_t p = 0;
    JsonVal doc = ParseJsonSimple(jsonStr, p);
    if (doc.type != JsonVal::Object) return false;

    outInfo.id = albumId;
    outInfo.service = service;
    outInfo.catalogNumber = doc.get("catalogNumber").strVal;

    // Album Names (Romaji / English / Japanese)
    const auto& names = doc.get("names");
    if (names.type == JsonVal::Array) {
        for (size_t n = 0; n < names.arrVal.size(); ++n) {
            const auto& nObj = names.get(n);
            std::string lang = nObj.get("language").strVal;
            std::string val = nObj.get("value").strVal;
            if (lang == "Romaji" && outInfo.titleRomaji.empty()) outInfo.titleRomaji = val;
            else if (lang == "English" && outInfo.titleEnglish.empty()) outInfo.titleEnglish = val;
            else if (lang == "Japanese" && outInfo.titleJapanese.empty()) outInfo.titleJapanese = val;
        }
    }
    std::string rawName = doc.get("name").strVal;
    if (rawName.empty()) rawName = doc.get("defaultName").strVal;
    if (outInfo.titleRomaji.empty()) outInfo.titleRomaji = rawName;
    if (outInfo.titleJapanese.empty()) outInfo.titleJapanese = doc.get("defaultName").strVal;
    outInfo.title = PickBestName(outInfo.titleRomaji, outInfo.titleEnglish, outInfo.titleJapanese, rawName);

    // Release Date
    const auto& rd = doc.get("releaseDate");
    if (rd.type == JsonVal::Object && !rd.get("isEmpty").boolVal) {
        int y = (int)rd.get("year").numVal;
        int m = (int)rd.get("month").numVal;
        int d = (int)rd.get("day").numVal;
        if (y > 0 && m > 0 && d > 0) {
            char dateBuf[32];
            sprintf_s(dateBuf, sizeof(dateBuf), "%04d.%02d.%02d", y, m, d);
            outInfo.releaseDate = dateBuf;
        } else if (y > 0) {
            outInfo.releaseDate = std::to_string(y);
        }
    }

    // Artist / Circle determination & multi-language
    std::string circleName;
    std::string producerName;
    const auto& artists = doc.get("artists");
    if (artists.type == JsonVal::Array) {
        for (size_t a = 0; a < artists.arrVal.size(); ++a) {
            const auto& aObj = artists.get(a);
            std::string cat = aObj.get("categories").strVal;
            std::string roles = aObj.get("roles").strVal;
            std::string name = aObj.get("name").strVal;
            if (name.empty()) name = aObj.get("artist").get("name").strVal;

            const auto& aNames = aObj.get("artist").get("names");
            if (aNames.type == JsonVal::Array) {
                for (size_t n = 0; n < aNames.arrVal.size(); ++n) {
                    const auto& nObj = aNames.get(n);
                    std::string lang = nObj.get("language").strVal;
                    std::string val = nObj.get("value").strVal;
                    if (lang == "Romaji" && outInfo.artistRomaji.empty()) outInfo.artistRomaji = val;
                    else if (lang == "English" && outInfo.artistEnglish.empty()) outInfo.artistEnglish = val;
                    else if (lang == "Japanese" && outInfo.artistJapanese.empty()) outInfo.artistJapanese = val;
                }
            }

            if (cat.find("Circle") != std::string::npos || roles.find("Circle") != std::string::npos || 
                aObj.get("artist").get("artistType").strVal == "Circle") {
                if (circleName.empty()) circleName = name;
            } else if (cat.find("Producer") != std::string::npos || aObj.get("artist").get("artistType").strVal == "Producer") {
                if (producerName.empty()) producerName = name;
            }
        }
    }

    std::string artStr = doc.get("artistString").strVal;
    std::string rawArtist = circleName;
    if (rawArtist.empty()) {
        if (!artStr.empty() && artStr != "Various artists" && artStr != "Various Artists" && artStr != "V.A.") {
            rawArtist = artStr;
        } else if (!producerName.empty()) {
            rawArtist = producerName;
        } else if (!artStr.empty()) {
            rawArtist = artStr;
        }
    }
    if (outInfo.artistRomaji.empty()) outInfo.artistRomaji = rawArtist;
    outInfo.artist = PickBestName(outInfo.artistRomaji, outInfo.artistEnglish, outInfo.artistJapanese, rawArtist);

    // Cover Art
    const auto& mp = doc.get("mainPicture");
    if (mp.type == JsonVal::Object) {
        outInfo.coverUrl = mp.get("urlOriginal").strVal;
        if (outInfo.coverUrl.empty()) outInfo.coverUrl = mp.get("urlThumb").strVal;
        if (outInfo.coverUrl.empty()) outInfo.coverUrl = mp.get("urlSmallThumb").strVal;
    }

    // Tracklist with multi-language
    const auto& trks = doc.get("tracks");
    if (trks.type == JsonVal::Array) {
        for (size_t t = 0; t < trks.arrVal.size(); ++t) {
            const auto& trk = trks.get(t);
            int pos = (int)trk.get("trackNumber").numVal;
            if (pos <= 0) pos = (int)(t + 1);

            std::string tTitle = trk.get("name").strVal;
            const auto& song = trk.get("song");
            if (tTitle.empty() && song.type == JsonVal::Object) {
                tTitle = song.get("name").strVal;
            }

            std::string tRomaji, tEnglish, tJapanese;
            if (song.type == JsonVal::Object) {
                const auto& songNames = song.get("names");
                if (songNames.type == JsonVal::Array) {
                    for (size_t n = 0; n < songNames.arrVal.size(); ++n) {
                        const auto& nObj = songNames.get(n);
                        std::string lang = nObj.get("language").strVal;
                        std::string val = nObj.get("value").strVal;
                        if (lang == "Romaji" && tRomaji.empty()) tRomaji = val;
                        else if (lang == "English" && tEnglish.empty()) tEnglish = val;
                        else if (lang == "Japanese" && tJapanese.empty()) tJapanese = val;
                    }
                }
                if (tJapanese.empty()) tJapanese = song.get("defaultName").strVal;
            }
            if (tRomaji.empty()) tRomaji = tTitle;
            std::string bestTrackTitle = PickBestName(tRomaji, tEnglish, tJapanese, tTitle);

            int durMs = 0;
            std::string tArtist = outInfo.artist;
            if (song.type == JsonVal::Object) {
                int lenSec = (int)song.get("lengthSeconds").numVal;
                durMs = lenSec * 1000;
                std::string sArt = song.get("artistString").strVal;
                if (!sArt.empty()) tArtist = sArt;
            }

            if (!bestTrackTitle.empty()) {
                outInfo.tracks.push_back({ pos, bestTrackTitle, tRomaji, tEnglish, tJapanese, tArtist, outInfo.artistRomaji, outInfo.artistEnglish, outInfo.artistJapanese, durMs });
            }
        }
    }

    return !outInfo.title.empty() || !outInfo.tracks.empty();
}

inline bool FetchVdbAlbumDetails(const std::string& baseUrl, const std::string& service, int albumId, VdbReleaseInfo& outInfo) {
    std::string endpoint = baseUrl + "/api/albums/" + std::to_string(albumId) + "?lang=Romaji&fields=Tracks,MainPicture,Artists,Names,Identifiers&songFields=Lyrics,Names";
    std::string jsonStr = HttpGetString(Utf8ToWide(endpoint));
    return ParseVdbAlbumDetailsJson(jsonStr, service, albumId, outInfo);
}

inline bool SearchVdbRelease(const std::string& baseUrl, const std::string& service, const std::string& artist, const std::string& album, const std::string& catalogNo, VdbReleaseInfo& outInfo) {
    if (album.empty() && catalogNo.empty()) return false;

    auto executeQuery = [&](const std::string& q) -> std::string {
        if (q.empty()) return "";
        std::string url = baseUrl + "/api/albums?query=" + UrlEncode(q) + "&lang=Romaji&fields=Tracks,MainPicture,Artists,Names,Identifiers&songFields=Lyrics,Names&maxResults=5";
        return HttpGetString(Utf8ToWide(url));
    };

    std::string resJson;
    if (!catalogNo.empty()) {
        LOG_INFO("[" + service + " SEARCH] Querying catalog number: " + catalogNo);
        resJson = executeQuery(catalogNo);
    }

    if ((resJson.empty() || resJson.find("\"items\":[]") != std::string::npos) && !album.empty()) {
        LOG_INFO("[" + service + " SEARCH] Querying album: " + album);
        resJson = executeQuery(album);
    }

    if ((resJson.empty() || resJson.find("\"items\":[]") != std::string::npos) && !artist.empty() && artist != "Unknown Artist" && !album.empty()) {
        LOG_INFO("[" + service + " SEARCH] Querying artist + album: " + artist + " " + album);
        resJson = executeQuery(artist + " " + album);
    }

    if (resJson.empty()) return false;

    size_t p = 0;
    JsonVal doc = ParseJsonSimple(resJson, p);
    const auto& items = doc.get("items");
    if (items.type != JsonVal::Array || items.arrVal.empty()) return false;

    int bestScore = -100;
    int pickedId = 0;
    std::string albNorm = NormalizeKey(album);
    std::string catNorm = NormalizeKey(catalogNo);

    for (size_t i = 0; i < items.arrVal.size(); ++i) {
        const auto& it = items.get(i);
        int id = (int)it.get("id").numVal;
        if (id <= 0) continue;

        std::string candName = it.get("name").strVal;
        std::string candCat = it.get("catalogNumber").strVal;
        std::string candArt = it.get("artistString").strVal;

        int score = 0;
        if (!catNorm.empty() && !candCat.empty() && NormalizeKey(candCat) == catNorm) {
            score += 150;
        }

        std::string candNorm = NormalizeKey(candName);
        if (!albNorm.empty() && !candNorm.empty()) {
            if (albNorm == candNorm) score += 100;
            else if (candNorm.find(albNorm) != std::string::npos || albNorm.find(candNorm) != std::string::npos) score += 70;
        }

        if (!artist.empty() && artist != "Unknown Artist" && !candArt.empty()) {
            std::string aNorm = NormalizeKey(artist);
            std::string caNorm = NormalizeKey(candArt);
            if (caNorm.find(aNorm) != std::string::npos || aNorm.find(caNorm) != std::string::npos) score += 40;
        }

        if (score > bestScore) {
            bestScore = score;
            pickedId = id;
        }
    }

    if (pickedId > 0 && bestScore >= 40) {
        LOG_INFO("[" + service + " MATCHED] Found album ID " + std::to_string(pickedId) + " (Score: " + std::to_string(bestScore) + "). Fetching full details...");
        return FetchVdbAlbumDetails(baseUrl, service, pickedId, outInfo);
    }

    return false;
}

inline std::string ParseLrcLibLyricsJson(const std::string& json) {
    if (json.empty()) return "";

    size_t p = 0;
    JsonVal doc = ParseJsonSimple(json, p);
    if (doc.type != JsonVal::Object) return "";

    std::string synced = doc.get("syncedLyrics").strVal;
    if (!synced.empty() && synced != "null") {
        return synced;
    }

    std::string plain = doc.get("plainLyrics").strVal;
    if (!plain.empty() && plain != "null") {
        return plain;
    }

    return "";
}

inline std::string FetchLrcLibSyncedLyrics(const std::string& artist, const std::string& title, const std::string& album) {
    if (artist.empty() || title.empty()) return "";
    std::string url = "https://lrclib.net/api/get?artist_name=" + UrlEncode(artist) + "&track_name=" + UrlEncode(title);
    if (!album.empty()) url += "&album_name=" + UrlEncode(album);

    std::string json = HttpGetString(Utf8ToWide(url));
    return ParseLrcLibLyricsJson(json);
}

struct LyricsQuery {
    std::string artist;
    std::string title;
    std::string album;
};

inline std::future<std::string> FetchLrcLibSyncedLyricsAsync(const std::string& artist, const std::string& title, const std::string& album) {
    return std::async(std::launch::async, [artist, title, album]() {
        return FetchLrcLibSyncedLyrics(artist, title, album);
    });
}

inline std::vector<std::string> BatchFetchLrcLibLyrics(const std::vector<LyricsQuery>& queries, unsigned int maxConcurrency = 6) {
    std::vector<std::string> results(queries.size());
    if (queries.empty()) return results;

    unsigned int numWorkers = (std::min)((unsigned int)queries.size(), maxConcurrency);
    if (numWorkers == 0) numWorkers = 1;

    std::atomic<size_t> nextIdx{ 0 };
    std::vector<std::thread> workers;

    for (unsigned int w = 0; w < numWorkers; ++w) {
        workers.emplace_back([&]() {
            while (true) {
                size_t idx = nextIdx.fetch_add(1);
                if (idx >= queries.size()) break;
                results[idx] = FetchLrcLibSyncedLyrics(queries[idx].artist, queries[idx].title, queries[idx].album);
            }
        });
    }

    for (auto& worker : workers) {
        if (worker.joinable()) worker.join();
    }

    return results;
}

inline std::string ExtractMusicBrainzUuid(const std::string& inputUrl) {
    std::regex uuidRegex(R"([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})");
    std::smatch match;
    if (std::regex_search(inputUrl, match, uuidRegex)) {
        return match.str(0);
    }
    return "";
}

inline std::string ExtractDiscogsId(const std::string& inputUrl, bool& isMaster) {
    isMaster = (inputUrl.find("/master/") != std::string::npos || inputUrl.find("/masters/") != std::string::npos);
    std::regex idRegex(R"((\d{4,10}))");
    std::smatch match;
    if (std::regex_search(inputUrl, match, idRegex)) {
        return match.str(1);
    }
    return "";
}

inline int ExtractVdbId(const std::string& inputUrl) {
    std::regex idRegex(R"((\d{1,8}))");
    std::smatch match;
    if (std::regex_search(inputUrl, match, idRegex)) {
        try {
            return std::stoi(match.str(1));
        } catch (...) {}
    }
    return 0;
}

} // namespace FetchServices
