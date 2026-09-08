#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "../include/HttpServer.hpp"
#include "../include/CoreEngine.hpp"
#include "../include/AudioEngine.hpp"
#include "../include/Logger.hpp"
#include "../include/MetadataUtils.hpp"

#include <sstream>
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <thread>
#include <chrono>

using namespace FetchServices;

static std::string JsonEscape(const std::string& s) {
    return EscapeJsonString(s);
}

static std::map<std::string, std::string> ParseQueryString(const std::string& query) {
    std::map<std::string, std::string> result;
    std::istringstream stream(query);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string k = pair.substr(0, eq);
            std::string v = pair.substr(eq + 1);
            // simple url decode
            std::string decoded;
            for (size_t i = 0; i < v.length(); ++i) {
                if (v[i] == '+') decoded += ' ';
                else if (v[i] == '%' && i + 2 < v.length()) {
                    std::string hex = v.substr(i + 1, 2);
                    try {
                        char ch = (char)std::stoi(hex, nullptr, 16);
                        decoded += ch;
                        i += 2;
                    } catch (...) {
                        decoded += v[i];
                    }
                } else {
                    decoded += v[i];
                }
            }
            result[k] = decoded;
        }
    }
    return result;
}

HttpServer::HttpServer(int port) : m_port(port) {}

HttpServer::~HttpServer() {
    Stop();
}

bool HttpServer::Start() {
    if (m_running) return true;

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("[HTTP] WSAStartup failed");
        return false;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        LOG_ERROR("[HTTP] socket() failed");
        WSACleanup();
        return false;
    }

    BOOL opt = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons((u_short)m_port);

    if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("[HTTP] bind() to port " + std::to_string(m_port) + " failed");
        closesocket(s);
        WSACleanup();
        return false;
    }

    if (listen(s, SOMAXCONN) == SOCKET_ERROR) {
        LOG_ERROR("[HTTP] listen() failed");
        closesocket(s);
        WSACleanup();
        return false;
    }

    m_serverSocket = (uintptr_t)s;
    m_running = true;

    LOG_INFO("[HTTP] Music Sorter Headless Server listening on http://127.0.0.1:" + std::to_string(m_port));

    m_serverThread = std::thread(&HttpServer::RunLoop, this);
    return true;
}

void HttpServer::Stop() {
    if (!m_running) return;
    m_running = false;

    if (m_serverSocket != 0) {
        closesocket((SOCKET)m_serverSocket);
        m_serverSocket = 0;
    }

    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }

    WSACleanup();
    LOG_INFO("[HTTP] Music Sorter Headless Server stopped");
}

void HttpServer::RunLoop() {
    while (m_running) {
        sockaddr_in clientAddr;
        int clientLen = sizeof(clientAddr);
        SOCKET client = accept((SOCKET)m_serverSocket, (sockaddr*)&clientAddr, &clientLen);
        if (client == INVALID_SOCKET) {
            if (!m_running) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::thread(&HttpServer::HandleClient, this, (uintptr_t)client).detach();
    }
}

void HttpServer::HandleClient(uintptr_t clientSocket) {
    SOCKET s = (SOCKET)clientSocket;

    std::vector<char> rawReq;
    char buffer[4096];
    int bytesRead = 0;
    size_t headerEnd = std::string::npos;

    // Read headers
    while ((bytesRead = recv(s, buffer, sizeof(buffer), 0)) > 0) {
        rawReq.insert(rawReq.end(), buffer, buffer + bytesRead);
        std::string cur(rawReq.begin(), rawReq.end());
        headerEnd = cur.find("\r\n\r\n");
        if (headerEnd != std::string::npos) break;
    }

    if (headerEnd == std::string::npos) {
        closesocket(s);
        return;
    }

    std::string headerStr(rawReq.begin(), rawReq.begin() + headerEnd);
    std::istringstream hStream(headerStr);
    std::string reqLine;
    std::getline(hStream, reqLine);
    if (!reqLine.empty() && reqLine.back() == '\r') reqLine.pop_back();

    std::istringstream rStream(reqLine);
    std::string method, fullPath, version;
    rStream >> method >> fullPath >> version;

    std::string path = fullPath;
    std::string queryString = "";
    size_t qPos = fullPath.find('?');
    if (qPos != std::string::npos) {
        path = fullPath.substr(0, qPos);
        queryString = fullPath.substr(qPos + 1);
    }
    auto queryParams = ParseQueryString(queryString);

    // Parse Content-Length
    size_t contentLength = 0;
    std::string hLine;
    while (std::getline(hStream, hLine)) {
        if (!hLine.empty() && hLine.back() == '\r') hLine.pop_back();
        size_t cPos = hLine.find(':');
        if (cPos != std::string::npos) {
            std::string k = hLine.substr(0, cPos);
            std::string v = hLine.substr(cPos + 1);
            while (!v.empty() && v[0] == ' ') v.erase(0, 1);
            std::string kLower = k;
            std::transform(kLower.begin(), kLower.end(), kLower.begin(), ::tolower);
            if (kLower == "content-length") {
                try { contentLength = std::stoul(v); } catch (...) {}
            }
        }
    }

    // Read remaining body if any
    std::vector<char> bodyData(rawReq.begin() + headerEnd + 4, rawReq.end());
    while (bodyData.size() < contentLength) {
        int r = recv(s, buffer, (int)(std::min)((size_t)sizeof(buffer), contentLength - bodyData.size()), 0);
        if (r <= 0) break;
        bodyData.insert(bodyData.end(), buffer, buffer + r);
    }
    std::string bodyStr(bodyData.begin(), bodyData.end());

    // CORS Preflight Handler
    if (method == "OPTIONS") {
        std::string resp = "HTTP/1.1 204 No Content\r\n"
                           "Access-Control-Allow-Origin: *\r\n"
                           "Access-Control-Allow-Methods: GET, POST, OPTIONS, PUT, DELETE\r\n"
                           "Access-Control-Allow-Headers: Content-Type, Authorization, X-Requested-With\r\n"
                           "Access-Control-Max-Age: 86400\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n\r\n";
        send(s, resp.c_str(), (int)resp.length(), 0);
        closesocket(s);
        return;
    }

    std::string contentType = "application/json; charset=utf-8";
    std::string respBody = "{}";
    int statusCode = 200;
    std::vector<unsigned char> binaryBody;

    // ---------------------------------------------------------------------------------------------
    // REST Endpoints Routing
    // ---------------------------------------------------------------------------------------------

    if (path == "/api/status" && method == "GET") {
        size_t dups = 0, tags = 0;
        int total = 0, downloaded = 0, missing = 0;
        CoreEngine::Instance().GetSidebarStats(dups, tags, total, downloaded, missing);

        std::ostringstream ss;
        ss << "{"
           << "\"status\":\"ready\","
           << "\"version\":\"2.0.0\","
           << "\"baseDir\":\"" << JsonEscape(CoreEngine::Instance().GetBaseDir()) << "\","
           << "\"toSortDir\":\"" << JsonEscape(CoreEngine::Instance().GetToSortDir()) << "\","
           << "\"outputDir\":\"" << JsonEscape(CoreEngine::Instance().GetOutputDir()) << "\","
           << "\"flacDir\":\"" << JsonEscape(CoreEngine::Instance().GetFlacDir()) << "\","
           << "\"mp3Dir\":\"" << JsonEscape(CoreEngine::Instance().GetMp3Dir()) << "\","
           << "\"stats\":{"
           << "\"duplicatesCount\":" << dups << ","
           << "\"unresolvedTagsCount\":" << tags << ","
           << "\"totalTracks\":" << total << ","
           << "\"downloadedTracks\":" << downloaded << ","
           << "\"missingTracks\":" << missing << ","
           << "\"isTagScanning\":" << (CoreEngine::Instance().IsTagScanning() ? "true" : "false") << ","
           << "\"isDuplicateScanning\":" << (CoreEngine::Instance().IsDuplicateScanning() ? "true" : "false") << ","
           << "\"isMirroring\":" << (CoreEngine::Instance().IsMirroring() ? "true" : "false")
           << "}"
           << "}";
        respBody = ss.str();
    }
    else if (path == "/api/settings" && method == "GET") {
        std::ostringstream ss;
        ss << "{"
           << "\"toSortDir\":\"" << JsonEscape(CoreEngine::Instance().GetToSortDir()) << "\","
           << "\"outputDir\":\"" << JsonEscape(CoreEngine::Instance().GetOutputDir()) << "\","
           << "\"flacDir\":\"" << JsonEscape(CoreEngine::Instance().GetFlacDir()) << "\","
           << "\"mp3Dir\":\"" << JsonEscape(CoreEngine::Instance().GetMp3Dir()) << "\","
           << "\"acoustIdKey\":\"" << JsonEscape(CoreEngine::Instance().GetAcoustIdKey()) << "\","
           << "\"discogsToken\":\"" << JsonEscape(CoreEngine::Instance().GetDiscogsToken()) << "\""
           << "}";
        respBody = ss.str();
    }
    else if (path == "/api/settings" && method == "POST") {
        size_t p = 0;
        JsonVal doc = ParseJsonSimple(bodyStr, p);
        std::string tosort = doc.get("toSortDir").strVal;
        std::string output = doc.get("outputDir").strVal;
        std::string flac = doc.get("flacDir").strVal;
        std::string mp3 = doc.get("mp3Dir").strVal;
        std::string acoustid = doc.get("acoustIdKey").strVal;
        std::string discogs = doc.get("discogsToken").strVal;

        CoreEngine::Instance().SetFolders(tosort, output, flac, mp3, acoustid, discogs);
        respBody = "{\"success\":true}";
    }
    else if (path == "/api/duplicates/scan" && method == "POST") {
        CoreEngine::Instance().StartDuplicateScan();
        respBody = "{\"started\":true}";
    }
    else if (path == "/api/duplicates" && method == "GET") {
        auto pairs = CoreEngine::Instance().GetDuplicateCandidates();
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < pairs.size(); ++i) {
            const auto& p = pairs[i];
            if (i > 0) ss << ",";
            ss << "{"
               << "\"id\":\"pair_" << i << "\","
               << "\"trackA_path\":\"" << JsonEscape(p.trackA_path) << "\","
               << "\"relA\":\"" << JsonEscape(p.relA) << "\","
               << "\"extA\":\"" << JsonEscape(p.extA) << "\","
               << "\"durA\":" << p.durA << ","
               << "\"trackB_path\":\"" << JsonEscape(p.trackB_path) << "\","
               << "\"relB\":\"" << JsonEscape(p.relB) << "\","
               << "\"extB\":\"" << JsonEscape(p.extB) << "\","
               << "\"durB\":" << p.durB << ","
               << "\"similarity\":" << p.similarity << ","
               << "\"offset\":" << p.offset
               << "}";
        }
        ss << "]";
        respBody = ss.str();
    }
    else if (path == "/api/duplicates/resolve" && method == "POST") {
        size_t p = 0;
        JsonVal doc = ParseJsonSimple(bodyStr, p);
        size_t index = (size_t)doc.get("index").numVal;
        std::string action = doc.get("action").strVal;

        bool ok = CoreEngine::Instance().ResolveDuplicate(index, action);
        respBody = ok ? "{\"success\":true}" : "{\"error\":\"Invalid index\"}";
    }
    else if (path == "/api/audio/status" && method == "GET") {
        std::ostringstream ss;
        ss << "{"
           << "\"isPlaying\":" << (AudioEngine::Instance().IsPlaying() ? "true" : "false") << ","
           << "\"activeChannel\":\"" << AudioEngine::Instance().GetActiveChannel() << "\","
           << "\"currentTime\":" << AudioEngine::Instance().GetCurrentPositionSeconds() << ","
           << "\"duration\":" << AudioEngine::Instance().GetDurationSeconds() << ","
           << "\"volume\":" << AudioEngine::Instance().GetMasterVolume()
           << "}";
        respBody = ss.str();
    }
    else if (path == "/api/audio/load" && method == "POST") {
        size_t p = 0;
        JsonVal doc = ParseJsonSimple(bodyStr, p);
        std::string pathA = doc.get("pathA").strVal;
        std::string pathB = doc.get("pathB").strVal;

        bool okA = pathA.empty() || AudioEngine::Instance().LoadTrackA(pathA);
        bool okB = pathB.empty() || AudioEngine::Instance().LoadTrackB(pathB);
        respBody = (okA && okB) ? "{\"success\":true}" : "{\"error\":\"Failed to load audio files\"}";
    }
    else if (path == "/api/audio/play" && method == "POST") {
        AudioEngine::Instance().Play();
        respBody = "{\"success\":true}";
    }
    else if (path == "/api/audio/pause" && method == "POST") {
        AudioEngine::Instance().Pause();
        respBody = "{\"success\":true}";
    }
    else if (path == "/api/audio/toggle" && method == "POST") {
        AudioEngine::Instance().TogglePlay();
        respBody = "{\"success\":true,\"isPlaying\":" + std::string(AudioEngine::Instance().IsPlaying() ? "true" : "false") + "}";
    }
    else if (path == "/api/audio/switch-channel" && method == "POST") {
        size_t p = 0;
        JsonVal doc = ParseJsonSimple(bodyStr, p);
        std::string chStr = doc.get("channel").strVal;
        char ch = (!chStr.empty() && (chStr[0] == 'b' || chStr[0] == 'B')) ? 'b' : 'a';
        AudioEngine::Instance().SetActiveChannel(ch);
        respBody = "{\"success\":true,\"activeChannel\":\"" + std::string(1, ch) + "\"}";
    }
    else if (path == "/api/audio/seek" && method == "POST") {
        size_t p = 0;
        JsonVal doc = ParseJsonSimple(bodyStr, p);
        double pct = doc.get("percent").numVal;
        AudioEngine::Instance().SeekToPercentage(pct);
        respBody = "{\"success\":true}";
    }
    else if (path == "/api/audio/volume" && method == "POST") {
        size_t p = 0;
        JsonVal doc = ParseJsonSimple(bodyStr, p);
        float vol = (float)doc.get("volume").numVal;
        AudioEngine::Instance().SetMasterVolume(vol);
        respBody = "{\"success\":true}";
    }
    else if (path == "/api/audio/waveform" && method == "GET") {
        std::string fp = queryParams["path"];
        auto peaks = CoreEngine::Instance().GetWaveformPeaks(fp, 100);
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < peaks.size(); ++i) {
            if (i > 0) ss << ",";
            ss << peaks[i];
        }
        ss << "]";
        respBody = ss.str();
    }
    else if (path == "/api/tags/scan" && method == "POST") {
        CoreEngine::Instance().StartTagScan();
        respBody = "{\"started\":true}";
    }
    else if (path == "/api/tags/progress" && method == "GET") {
        size_t done = 0, total = 0;
        double fraction = 0.0, speed = 0.0;
        std::string eta, elapsed;
        CoreEngine::Instance().GetTagScanProgress(done, total, fraction, speed, eta, elapsed);

        std::ostringstream ss;
        ss << "{"
           << "\"isScanning\":" << (CoreEngine::Instance().IsTagScanning() ? "true" : "false") << ","
           << "\"done\":" << done << ","
           << "\"total\":" << total << ","
           << "\"fraction\":" << fraction << ","
           << "\"speed\":" << speed << ","
           << "\"eta\":\"" << JsonEscape(eta) << "\","
           << "\"elapsed\":\"" << JsonEscape(elapsed) << "\""
           << "}";
        respBody = ss.str();
    }
    else if (path == "/api/tags/albums" && method == "GET") {
        auto albums = CoreEngine::Instance().GetAlbumGroups();
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < albums.size(); ++i) {
            const auto& a = albums[i];
            if (i > 0) ss << ",";
            ss << "{"
               << "\"albumKey\":\"" << JsonEscape(a.albumKey) << "\","
               << "\"album\":\"" << JsonEscape(a.album) << "\","
               << "\"artist\":\"" << JsonEscape(a.artist) << "\","
               << "\"year\":\"" << JsonEscape(a.year) << "\","
               << "\"trackCount\":" << a.trackCount << ","
               << "\"confidenceScore\":" << a.confidenceScore << ","
               << "\"matchTierName\":\"" << JsonEscape(a.matchTierName) << "\","
               << "\"hasConflict\":" << (a.hasConflict ? "true" : "false") << ","
               << "\"selectedCoverChoice\":" << a.selectedCoverChoice << ","
               << "\"hasLocalCover\":" << (a.hasLocalCover ? "true" : "false") << ","
               << "\"hasOnlineCover\":" << (a.hasOnlineCover ? "true" : "false") << ","
               << "\"onlineCoverSource\":\"" << JsonEscape(a.onlineCoverSource) << "\","
               << "\"referenceIndex\":" << (a.trackIndices.empty() ? 0 : a.trackIndices[0]) << ","
               << "\"candidates\":[";
            for (size_t c = 0; c < a.candidates.size(); ++c) {
                if (c > 0) ss << ",";
                const auto& cand = a.candidates[c];
                ss << "{"
                   << "\"source\":\"" << JsonEscape(cand.providerName) << "\","
                   << "\"title\":\"" << JsonEscape(cand.title) << "\","
                   << "\"artist\":\"" << JsonEscape(cand.artist) << "\","
                   << "\"album\":\"" << JsonEscape(cand.album) << "\","
                   << "\"year\":\"" << JsonEscape(cand.year) << "\","
                   << "\"confidenceScore\":" << cand.confidence
                   << "}";
            }
            ss << "],\"tracks\":[";
            for (size_t t = 0; t < a.trackIndices.size(); ++t) {
                if (t > 0) ss << ",";
                size_t trkIdx = a.trackIndices[t];
                TagReviewItem item = CoreEngine::Instance().GetTagItem(trkIdx);
                ss << "{"
                   << "\"index\":" << trkIdx << ","
                   << "\"trackNo\":\"" << JsonEscape(item.trackNoBuf) << "\","
                   << "\"title\":\"" << JsonEscape(item.titleBuf) << "\","
                   << "\"artist\":\"" << JsonEscape(item.artistBuf) << "\","
                   << "\"album\":\"" << JsonEscape(item.albumBuf) << "\","
                   << "\"duration\":" << item.duration << ","
                   << "\"hasLyrics\":" << (item.hasLyrics ? "true" : "false") << ","
                   << "\"hasSyncedLyrics\":" << (item.hasSyncedLyrics ? "true" : "false") << ","
                   << "\"lyricsOriginal\":\"" << JsonEscape(item.lyricsOriginal) << "\","
                   << "\"lyricsRomaji\":\"" << JsonEscape(item.lyricsRomaji) << "\","
                   << "\"lyricsEnglish\":\"" << JsonEscape(item.lyricsEnglish) << "\","
                   << "\"currentLyrics\":\"" << JsonEscape(item.lyricsBuf) << "\","
                   << "\"filePath\":\"" << JsonEscape(item.filePath) << "\","
                   << "\"isProcessed\":" << (item.isProcessed ? "true" : "false")
                   << "}";
            }
            ss << "]}";
        }
        ss << "]";
        respBody = ss.str();
    }
    else if (path == "/api/tags/cover" && method == "GET") {
        size_t trkIdx = 0;
        try { trkIdx = std::stoul(queryParams["trackIndex"]); } catch (...) {}
        std::string type = queryParams["type"];
        binaryBody = CoreEngine::Instance().GetCoverImageBytes(trkIdx, type);
        if (binaryBody.empty()) {
            statusCode = 404;
            respBody = "{\"error\":\"Cover image not found\"}";
        } else {
            if (binaryBody.size() >= 4 && binaryBody[0] == 0x89 && binaryBody[1] == 'P' && binaryBody[2] == 'N' && binaryBody[3] == 'G') {
                contentType = "image/png";
            } else {
                contentType = "image/jpeg";
            }
        }
    }
    else if (path == "/api/tags/approve-track" && method == "POST") {
        size_t p = 0;
        JsonVal doc = ParseJsonSimple(bodyStr, p);
        size_t idx = (size_t)doc.get("trackIndex").numVal;
        CoreEngine::Instance().ApproveTrack(idx);
        respBody = "{\"success\":true}";
    }
    else if (path == "/api/tags/approve-album" && method == "POST") {
        size_t p = 0;
        JsonVal doc = ParseJsonSimple(bodyStr, p);
        size_t idx = (size_t)doc.get("referenceIndex").numVal;
        CoreEngine::Instance().ApproveAlbum(idx);
        respBody = "{\"success\":true}";
    }
    else if (path == "/api/tags/skip" && method == "POST") {
        size_t p = 0;
        JsonVal doc = ParseJsonSimple(bodyStr, p);
        size_t idx = (size_t)doc.get("trackIndex").numVal;
        bool isAlbum = doc.get("isAlbum").boolVal;
        if (isAlbum) CoreEngine::Instance().SkipAlbum(idx);
        else CoreEngine::Instance().SkipTrack(idx);
        respBody = "{\"success\":true}";
    }
    else if (path == "/api/tags/select-candidate" && method == "POST") {
        size_t p = 0;
        JsonVal doc = ParseJsonSimple(bodyStr, p);
        size_t trackIdx = (size_t)doc.get("trackIndex").numVal;
        int candIdx = (int)doc.get("candidateIndex").numVal;
        bool applyToAlbum = doc.get("applyToAlbum").boolVal;

        if (applyToAlbum) CoreEngine::Instance().ApplyCandidateToAlbum(trackIdx, candIdx);
        else CoreEngine::Instance().ApplyCandidateToTrack(trackIdx, candIdx);
        respBody = "{\"success\":true}";
    }
    else if (path == "/api/tags/update-track" && method == "POST") {
        size_t p = 0;
        JsonVal doc = ParseJsonSimple(bodyStr, p);
        size_t trackIdx = (size_t)doc.get("trackIndex").numVal;
        std::string title = doc.get("title").strVal;
        std::string artist = doc.get("artist").strVal;
        std::string trackNo = doc.get("trackNo").strVal;
        std::string lyrics = doc.get("lyrics").strVal;

        CoreEngine::Instance().UpdateTrackDetails(trackIdx, title, artist, trackNo, lyrics);
        respBody = "{\"success\":true}";
    }
    else if (path == "/api/tags/cover-choice" && method == "POST") {
        size_t p = 0;
        JsonVal doc = ParseJsonSimple(bodyStr, p);
        size_t trackIdx = (size_t)doc.get("trackIndex").numVal;
        int choice = (int)doc.get("choice").numVal;
        bool applyToAlbum = doc.get("applyToAlbum").boolVal;

        CoreEngine::Instance().SetCoverChoice(trackIdx, choice, applyToAlbum);
        respBody = "{\"success\":true}";
    }
    else if (path == "/api/tags/manual-fetch" && method == "POST") {
        size_t p = 0;
        JsonVal doc = ParseJsonSimple(bodyStr, p);
        std::string src = doc.get("source").strVal;
        std::string q = doc.get("query").strVal;
        size_t refIdx = (size_t)doc.get("referenceIndex").numVal;
        bool applyToAlbum = doc.get("applyToAlbum").boolVal;

        if (src == "mb") CoreEngine::Instance().FetchManualMusicBrainz(q, refIdx, applyToAlbum);
        else if (src == "discogs") CoreEngine::Instance().FetchManualDiscogs(q, refIdx, applyToAlbum);
        else if (src == "touhoudb") CoreEngine::Instance().FetchManualTouhouDb(q, refIdx, applyToAlbum);
        else if (src == "thwiki") CoreEngine::Instance().FetchManualThwiki(q, refIdx, applyToAlbum);
        else if (src == "vocadb") CoreEngine::Instance().FetchManualVocaDb(q, refIdx, applyToAlbum);
        else if (src == "utaitedb") CoreEngine::Instance().FetchManualUtaiteDb(q, refIdx, applyToAlbum);

        respBody = "{\"started\":true}";
    }
    else if (path == "/api/mirror/start" && method == "POST") {
        CoreEngine::Instance().StartMirroring();
        respBody = "{\"started\":true}";
    }
    else if (path == "/api/mirror/status" && method == "GET") {
        size_t completed = 0, total = 0, dirs = 0, fallbacks = 0;
        bool running = false;
        std::string msg;
        CoreEngine::Instance().GetMirrorProgress(completed, total, dirs, fallbacks, running, msg);

        std::ostringstream ss;
        ss << "{"
           << "\"isRunning\":" << (running ? "true" : "false") << ","
           << "\"completedTasks\":" << completed << ","
           << "\"totalTasks\":" << total << ","
           << "\"createdFolders\":" << dirs << ","
           << "\"copiedFallbacks\":" << fallbacks << ","
           << "\"message\":\"" << JsonEscape(msg) << "\""
           << "}";
        respBody = ss.str();
    }
    else if (path == "/api/database/tracks" && method == "GET") {
        int filterStatus = -1;
        int filterFormat = -1;
        std::string q = queryParams["query"];
        try { if (!queryParams["status"].empty()) filterStatus = std::stoi(queryParams["status"]); } catch (...) {}
        try { if (!queryParams["format"].empty()) filterFormat = std::stoi(queryParams["format"]); } catch (...) {}

        auto tracks = CoreEngine::Instance().QueryDatabase(filterStatus, filterFormat, q);
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < tracks.size(); ++i) {
            if (i > 0) ss << ",";
            const auto& t = tracks[i];
            ss << "{"
               << "\"id\":" << t.id << ","
               << "\"artist\":\"" << JsonEscape(t.artist) << "\","
               << "\"album\":\"" << JsonEscape(t.album) << "\","
               << "\"title\":\"" << JsonEscape(t.title) << "\","
               << "\"trackNo\":\"" << JsonEscape(t.trackNo) << "\","
               << "\"year\":\"" << JsonEscape(t.year) << "\","
               << "\"durationSec\":" << t.durationSec << ","
               << "\"format\":\"" << JsonEscape(t.format) << "\","
               << "\"bitrateKbps\":" << t.bitrateKbps << ","
               << "\"status\":" << t.status << ","
               << "\"relPath\":\"" << JsonEscape(t.relPath) << "\""
               << "}";
        }
        ss << "]";
        respBody = ss.str();
    }
    else if (path == "/api/database/stats" && method == "GET") {
        int total = 0, dl = 0, miss = 0;
        CoreEngine::Instance().GetDatabaseStats(total, dl, miss);
        std::ostringstream ss;
        ss << "{"
           << "\"totalTracks\":" << total << ","
           << "\"downloadedTracks\":" << dl << ","
           << "\"missingTracks\":" << miss
           << "}";
        respBody = ss.str();
    }
    else if (path == "/api/database/sync" && method == "POST") {
        CoreEngine::Instance().SyncTracklistDatabase();
        respBody = "{\"success\":true}";
    }
    else if (path == "/api/database/export" && method == "POST") {
        size_t p = 0;
        JsonVal doc = ParseJsonSimple(bodyStr, p);
        std::string expPath = doc.get("path").strVal;
        bool ok = CoreEngine::Instance().ExportDatabase(expPath);
        respBody = ok ? "{\"success\":true}" : "{\"error\":\"Failed to export database\"}";
    }
    else if (path == "/api/logs" && method == "GET") {
        auto logs = Logger::Instance().GetLogs();
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < logs.size(); ++i) {
            if (i > 0) ss << ",";
            ss << "\"" << JsonEscape(logs[i]) << "\"";
        }
        ss << "]";
        respBody = ss.str();
    }
    else if (path == "/api/shutdown" && method == "POST") {
        respBody = "{\"success\":true}";
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            Stop();
        }).detach();
    }
    else {
        statusCode = 404;
        respBody = "{\"error\":\"Not Found\"}";
    }

    // Send HTTP Response
    std::string statusText = (statusCode == 200) ? "OK" : ((statusCode == 404) ? "Not Found" : "Error");
    size_t payloadSize = binaryBody.empty() ? respBody.length() : binaryBody.size();

    std::ostringstream headerStream;
    headerStream << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n"
                 << "Access-Control-Allow-Origin: *\r\n"
                 << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                 << "Access-Control-Allow-Headers: *\r\n"
                 << "Content-Type: " << contentType << "\r\n"
                 << "Content-Length: " << payloadSize << "\r\n"
                 << "Connection: close\r\n\r\n";

    std::string responseHeader = headerStream.str();

    auto sendAll = [s](const char* data, size_t total) {
        size_t sent = 0;
        while (sent < total) {
            int chunk = (int)std::min<size_t>(total - sent, 65536);
            int res = send(s, data + sent, chunk, 0);
            if (res <= 0) break;
            sent += res;
        }
    };

    sendAll(responseHeader.c_str(), responseHeader.length());

    if (binaryBody.empty()) {
        sendAll(respBody.c_str(), respBody.length());
    } else {
        sendAll((const char*)binaryBody.data(), binaryBody.size());
    }

    closesocket(s);
}
