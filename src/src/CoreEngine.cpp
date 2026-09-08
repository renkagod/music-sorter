#include "../include/CoreEngine.hpp"
#include "../include/AudioEngine.hpp"
#include "../include/Logger.hpp"

#include <fstream>
#include <sstream>
#include <thread>
#include <future>
#include <algorithm>
#include <unordered_set>
#include <regex>
#include <cmath>

namespace fs = std::filesystem;
using namespace FetchServices;

static bool HasAudioFiles(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return false;
    for (auto& p : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (p.is_regular_file(ec)) {
            std::string ext = p.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".flac" || ext == ".mp3" || ext == ".wav" || ext == ".m4a" ||
                ext == ".aac" || ext == ".ogg" || ext == ".opus" || ext == ".wma" ||
                ext == ".alac" || ext == ".ape" || ext == ".wv") {
                return true;
            }
        }
    }
    return false;
}

static bool RemoveEmptySubdirectories(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return false;

    bool allChildrenRemoved = true;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_directory(ec)) {
            if (!RemoveEmptySubdirectories(entry.path())) {
                allChildrenRemoved = false;
            }
        } else {
            allChildrenRemoved = false;
        }
    }

    if (allChildrenRemoved) {
        LOG_INFO("[CLEANUP] Removing empty folder: " + dir.string());
        fs::remove(dir, ec);
        return true;
    }
    return false;
}

static void CleanupEmptyParentDirectories(fs::path curDir, const fs::path& stopDir) {
    std::error_code ec;
    fs::path canonicalStop = fs::weakly_canonical(stopDir, ec);
    while (!curDir.empty()) {
        fs::path canonicalCur = fs::weakly_canonical(curDir, ec);
        if (canonicalCur == canonicalStop) break;

        if (!fs::exists(curDir, ec) || !fs::is_directory(curDir, ec)) {
            curDir = curDir.parent_path();
            continue;
        }

        if (fs::is_empty(curDir, ec)) {
            LOG_INFO("[CLEANUP] Removing empty parent folder: " + curDir.string());
            fs::remove(curDir, ec);
            curDir = curDir.parent_path();
        } else {
            break;
        }
    }
}

static void CleanupEmptyDirectories(const fs::path& rootDir) {
    std::error_code ec;
    if (!fs::exists(rootDir, ec) || !fs::is_directory(rootDir, ec)) return;

    for (const auto& entry : fs::directory_iterator(rootDir, ec)) {
        if (entry.is_directory(ec)) {
            RemoveEmptySubdirectories(entry.path());
        }
    }
}

static bool WriteFlacTagsAndPicture(const std::string& filePath, const std::string& artist, const std::string& album, const std::string& title, const std::string& trackNo, const std::string& dateStr, const std::string& lyrics, const std::vector<unsigned char>& coverBytes) {
    std::ifstream fIn(filePath, std::ios::binary | std::ios::ate);
    if (!fIn.is_open()) return false;

    std::streamsize fileSize = fIn.tellg();
    fIn.seekg(0, std::ios::beg);
    if (fileSize <= 0) return false;

    std::vector<unsigned char> flacData((size_t)fileSize);
    if (!fIn.read((char*)flacData.data(), fileSize)) return false;
    fIn.close();

    if (flacData.size() < 4 || flacData[0] != 'f' || flacData[1] != 'L' || flacData[2] != 'a' || flacData[3] != 'C') {
        return false;
    }

    size_t offset = 4;
    bool isLast = false;
    std::vector<unsigned char> streamInfoBlock;
    std::vector<unsigned char> seekTableBlock;

    while (offset < flacData.size() && !isLast) {
        unsigned char bHeader = flacData[offset];
        isLast = (bHeader & 0x80) != 0;
        unsigned char blockType = bHeader & 0x7F;

        uint32_t blockLen = ((uint32_t)flacData[offset + 1] << 16) | ((uint32_t)flacData[offset + 2] << 8) | (uint32_t)flacData[offset + 3];
        size_t blockStart = offset;
        offset += 4 + blockLen;

        if (blockType == 0) {
            streamInfoBlock.assign(flacData.begin() + blockStart + 4, flacData.begin() + offset);
        } else if (blockType == 3) {
            seekTableBlock.assign(flacData.begin() + blockStart + 4, flacData.begin() + offset);
        }
    }

    size_t audioDataOffset = offset;

    std::vector<unsigned char> vcPayload;
    std::string vendor = "MusicSorter Studio 2.0";
    WriteUint32LE(vcPayload, (uint32_t)vendor.length());
    vcPayload.insert(vcPayload.end(), vendor.begin(), vendor.end());

    std::vector<std::string> comments;
    if (!artist.empty()) comments.push_back("ARTIST=" + artist);
    if (!album.empty()) comments.push_back("ALBUM=" + album);
    if (!title.empty()) comments.push_back("TITLE=" + title);
    if (!trackNo.empty()) comments.push_back("TRACKNUMBER=" + trackNo);
    if (!dateStr.empty()) {
        comments.push_back("DATE=" + dateStr);
        std::string yr = ExtractYearFromString(dateStr);
        if (!yr.empty()) comments.push_back("YEAR=" + yr);
    }
    if (!lyrics.empty()) comments.push_back("LYRICS=" + lyrics);

    WriteUint32LE(vcPayload, (uint32_t)comments.size());
    for (const auto& c : comments) {
        WriteUint32LE(vcPayload, (uint32_t)c.length());
        vcPayload.insert(vcPayload.end(), c.begin(), c.end());
    }

    std::vector<unsigned char> picPayload;
    if (!coverBytes.empty()) {
        WriteUint32BE(picPayload, 3);
        std::string mime = "image/jpeg";
        WriteUint32BE(picPayload, (uint32_t)mime.length());
        picPayload.insert(picPayload.end(), mime.begin(), mime.end());
        WriteUint32BE(picPayload, 0);
        WriteUint32BE(picPayload, 500);
        WriteUint32BE(picPayload, 500);
        WriteUint32BE(picPayload, 24);
        WriteUint32BE(picPayload, 0);
        WriteUint32BE(picPayload, (uint32_t)coverBytes.size());
        picPayload.insert(picPayload.end(), coverBytes.begin(), coverBytes.end());
    }

    std::vector<unsigned char> outFlac;
    outFlac.push_back('f'); outFlac.push_back('L'); outFlac.push_back('a'); outFlac.push_back('C');

    outFlac.push_back(0x00);
    uint32_t sLen = (uint32_t)streamInfoBlock.size();
    outFlac.push_back((unsigned char)((sLen >> 16) & 0xFF));
    outFlac.push_back((unsigned char)((sLen >> 8) & 0xFF));
    outFlac.push_back((unsigned char)(sLen & 0xFF));
    outFlac.insert(outFlac.end(), streamInfoBlock.begin(), streamInfoBlock.end());

    if (!seekTableBlock.empty()) {
        outFlac.push_back(0x03);
        uint32_t kLen = (uint32_t)seekTableBlock.size();
        outFlac.push_back((unsigned char)((kLen >> 16) & 0xFF));
        outFlac.push_back((unsigned char)((kLen >> 8) & 0xFF));
        outFlac.push_back((unsigned char)(kLen & 0xFF));
        outFlac.insert(outFlac.end(), seekTableBlock.begin(), seekTableBlock.end());
    }

    bool vcIsLast = picPayload.empty();
    outFlac.push_back(vcIsLast ? 0x84 : 0x04);
    uint32_t vcLen = (uint32_t)vcPayload.size();
    outFlac.push_back((unsigned char)((vcLen >> 16) & 0xFF));
    outFlac.push_back((unsigned char)((vcLen >> 8) & 0xFF));
    outFlac.push_back((unsigned char)(vcLen & 0xFF));
    outFlac.insert(outFlac.end(), vcPayload.begin(), vcPayload.end());

    if (!picPayload.empty()) {
        outFlac.push_back(0x86);
        uint32_t pLen = (uint32_t)picPayload.size();
        outFlac.push_back((unsigned char)((pLen >> 16) & 0xFF));
        outFlac.push_back((unsigned char)((pLen >> 8) & 0xFF));
        outFlac.push_back((unsigned char)(pLen & 0xFF));
        outFlac.insert(outFlac.end(), picPayload.begin(), picPayload.end());
    }

    outFlac.insert(outFlac.end(), flacData.begin() + audioDataOffset, flacData.end());

    std::ofstream fOut(filePath, std::ios::binary);
    if (!fOut.is_open()) return false;
    fOut.write((const char*)outFlac.data(), outFlac.size());
    fOut.close();

    return true;
}

static bool WriteMp3TagsAndPicture(const std::string& filePath, const std::string& artist, const std::string& album, const std::string& title, const std::string& trackNo, const std::string& dateStr, const std::string& lyrics, const std::vector<unsigned char>& coverBytes) {
    std::ifstream fIn(filePath, std::ios::binary | std::ios::ate);
    if (!fIn.is_open()) return false;

    std::streamsize fileSize = fIn.tellg();
    fIn.seekg(0, std::ios::beg);
    if (fileSize <= 0) return false;

    std::vector<unsigned char> mp3Data((size_t)fileSize);
    if (!fIn.read((char*)mp3Data.data(), fileSize)) return false;
    fIn.close();

    size_t audioOffset = 0;
    if (mp3Data.size() >= 10 && mp3Data[0] == 'I' && mp3Data[1] == 'D' && mp3Data[2] == '3') {
        uint32_t tagSize = ((uint32_t)(mp3Data[6] & 0x7F) << 21) |
                           ((uint32_t)(mp3Data[7] & 0x7F) << 14) |
                           ((uint32_t)(mp3Data[8] & 0x7F) << 7)  |
                           ((uint32_t)(mp3Data[9] & 0x7F));
        audioOffset = 10 + tagSize;
        if (audioOffset > mp3Data.size()) audioOffset = mp3Data.size();
    }

    std::vector<unsigned char> frames;

    auto AppendTextFrame = [&](const char* frameId, const std::string& text) {
        if (text.empty()) return;
        frames.push_back(frameId[0]);
        frames.push_back(frameId[1]);
        frames.push_back(frameId[2]);
        frames.push_back(frameId[3]);

        uint32_t contentSize = 1 + (uint32_t)text.length();
        frames.push_back((unsigned char)((contentSize >> 24) & 0xFF));
        frames.push_back((unsigned char)((contentSize >> 16) & 0xFF));
        frames.push_back((unsigned char)((contentSize >> 8) & 0xFF));
        frames.push_back((unsigned char)(contentSize & 0xFF));

        frames.push_back(0x00);
        frames.push_back(0x00);

        frames.push_back(0x03);
        frames.insert(frames.end(), text.begin(), text.end());
    };

    AppendTextFrame("TPE1", artist);
    AppendTextFrame("TALB", album);
    AppendTextFrame("TIT2", title);
    AppendTextFrame("TRCK", trackNo);
    if (!dateStr.empty()) {
        std::string yr = ExtractYearFromString(dateStr);
        AppendTextFrame("TYER", yr);
        AppendTextFrame("TDRC", dateStr);
    }

    if (!lyrics.empty()) {
        frames.push_back('U'); frames.push_back('S'); frames.push_back('L'); frames.push_back('T');
        std::string lang = "eng";
        std::string desc = "";
        uint32_t contentSize = 1 + 3 + 1 + (uint32_t)lyrics.length();
        frames.push_back((unsigned char)((contentSize >> 24) & 0xFF));
        frames.push_back((unsigned char)((contentSize >> 16) & 0xFF));
        frames.push_back((unsigned char)((contentSize >> 8) & 0xFF));
        frames.push_back((unsigned char)(contentSize & 0xFF));

        frames.push_back(0x00); frames.push_back(0x00);
        frames.push_back(0x03);
        frames.insert(frames.end(), lang.begin(), lang.end());
        frames.push_back(0x00);
        frames.insert(frames.end(), lyrics.begin(), lyrics.end());
    }

    if (!coverBytes.empty()) {
        frames.push_back('A'); frames.push_back('P'); frames.push_back('I'); frames.push_back('C');
        std::string mime = "image/jpeg";
        uint32_t contentSize = 1 + (uint32_t)mime.length() + 1 + 1 + 1 + (uint32_t)coverBytes.size();
        frames.push_back((unsigned char)((contentSize >> 24) & 0xFF));
        frames.push_back((unsigned char)((contentSize >> 16) & 0xFF));
        frames.push_back((unsigned char)((contentSize >> 8) & 0xFF));
        frames.push_back((unsigned char)(contentSize & 0xFF));

        frames.push_back(0x00); frames.push_back(0x00);
        frames.push_back(0x00);
        frames.insert(frames.end(), mime.begin(), mime.end());
        frames.push_back(0x00);
        frames.push_back(0x03);
        frames.push_back(0x00);
        frames.insert(frames.end(), coverBytes.begin(), coverBytes.end());
    }

    std::vector<unsigned char> outMp3;
    outMp3.push_back('I'); outMp3.push_back('D'); outMp3.push_back('3');
    outMp3.push_back(0x03); outMp3.push_back(0x00);
    outMp3.push_back(0x00);

    uint32_t fSize = (uint32_t)frames.size();
    outMp3.push_back((unsigned char)((fSize >> 21) & 0x7F));
    outMp3.push_back((unsigned char)((fSize >> 14) & 0x7F));
    outMp3.push_back((unsigned char)((fSize >> 7) & 0x7F));
    outMp3.push_back((unsigned char)(fSize & 0x7F));

    outMp3.insert(outMp3.end(), frames.begin(), frames.end());
    outMp3.insert(outMp3.end(), mp3Data.begin() + audioOffset, mp3Data.end());

    std::ofstream fOut(filePath, std::ios::binary);
    if (!fOut.is_open()) return false;
    fOut.write((const char*)outMp3.data(), outMp3.size());
    fOut.close();

    return true;
}

static bool ConvertFlacToMp3(const std::string& inputFlac, const std::string& outputMp3) {
    std::string cmd = "ffmpeg -v quiet -y -i \"" + inputFlac + "\" -ab 320k -c:v mjpeg -id3v2_version 3 \"" + outputMp3 + "\"";
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcessA(NULL, (char*)cmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return fs::exists(outputMp3) && fs::file_size(outputMp3) > 0;
    }
    return false;
}

static void ApplyTrackMatch(TagReviewItem& albItem, const std::vector<MBTrackEntry>& mbTracks) {
    if (mbTracks.empty()) return;

    std::string rawName = albItem.originalFilename;
    int leadingTrackNo = 0;
    try {
        size_t d = rawName.find_first_of("._ -");
        if (d != std::string::npos && d > 0 && d <= 3) {
            leadingTrackNo = std::stoi(rawName.substr(0, d));
        }
    } catch (...) {}

    size_t dotPos = rawName.find_first_not_of("0123456789. -_");
    if (dotPos != std::string::npos && dotPos > 0 && dotPos < 6) {
        rawName = rawName.substr(dotPos);
    }
    size_t extPos = rawName.rfind('.');
    if (extPos != std::string::npos) rawName = rawName.substr(0, extPos);

    std::string rawClean = NormalizeKey(rawName);

    const MBTrackEntry* bestMatch = nullptr;
    int bestScore = -1;

    for (const auto& t : mbTracks) {
        int score = 0;
        std::string tTitleClean = NormalizeKey(t.title);
        std::string tArtistClean = NormalizeKey(t.artist);

        if (leadingTrackNo > 0 && leadingTrackNo == t.position) score += 50;

        if (albItem.duration > 0 && t.lengthMs > 0) {
            double tSec = (double)t.lengthMs / 1000.0;
            if (std::abs(albItem.duration - tSec) <= 3.0) score += 40;
        }

        if (tTitleClean.length() >= 3) {
            if (!tTitleClean.empty() && (rawClean.find(tTitleClean) != std::string::npos || (rawClean.length() >= 4 && tTitleClean.find(rawClean) != std::string::npos))) {
                score += 60;
            }
        } else if (!tTitleClean.empty()) {
            if (rawClean == tTitleClean || rawClean.ends_with(tTitleClean)) score += 60;
        }

        if (!tArtistClean.empty() && tArtistClean != "variousartists" && tArtistClean != "va") {
            if (tArtistClean.length() >= 3 && rawClean.find(tArtistClean) != std::string::npos) score += 40;
        }

        if (score > bestScore) {
            bestScore = score;
            bestMatch = &t;
        }
    }

    if (bestMatch && bestScore > 0) {
        char trackStr[16];
        sprintf_s(trackStr, sizeof(trackStr), "%02d", bestMatch->position);
        strncpy_s(albItem.trackNoBuf, trackStr, sizeof(albItem.trackNoBuf) - 1);

        albItem.titleRomaji = bestMatch->titleRomaji;
        albItem.titleEnglish = bestMatch->titleEnglish;
        albItem.titleJapanese = bestMatch->titleJapanese;
        if (!bestMatch->artistRomaji.empty()) albItem.artistRomaji = bestMatch->artistRomaji;
        if (!bestMatch->artistEnglish.empty()) albItem.artistEnglish = bestMatch->artistEnglish;
        if (!bestMatch->artistJapanese.empty()) albItem.artistJapanese = bestMatch->artistJapanese;

        albItem.lyricsOriginal = bestMatch->lyricsOriginal;
        albItem.lyricsRomaji = bestMatch->lyricsRomaji;
        albItem.lyricsEnglish = bestMatch->lyricsEnglish;

        std::string chosenTitle = PickBestName(bestMatch->titleRomaji, bestMatch->titleEnglish, bestMatch->titleJapanese, bestMatch->title);
        if (!chosenTitle.empty()) {
            strncpy_s(albItem.titleBuf, chosenTitle.c_str(), sizeof(albItem.titleBuf) - 1);
        }
        std::string chosenArtist = PickBestName(bestMatch->artistRomaji, bestMatch->artistEnglish, bestMatch->artistJapanese, bestMatch->artist);
        if (!chosenArtist.empty() && chosenArtist != "Various Artists" && chosenArtist != "V.A.") {
            strncpy_s(albItem.artistBuf, chosenArtist.c_str(), sizeof(albItem.artistBuf) - 1);
        }

        if (albItem.lyricsBuf[0] == '\0' && (!albItem.lyricsRomaji.empty() || !albItem.lyricsEnglish.empty() || !albItem.lyricsOriginal.empty())) {
            std::string bestLyrics = PickBestLyrics(albItem.lyricsRomaji, albItem.lyricsEnglish, albItem.lyricsOriginal);
            if (!bestLyrics.empty()) {
                strncpy_s(albItem.lyricsBuf, bestLyrics.c_str(), sizeof(albItem.lyricsBuf) - 1);
                albItem.hasLyrics = true;
                albItem.hasSyncedLyrics = false;
            }
        }
    }
}

// -------------------------------------------------------------------------------------------------
// CoreEngine Implementation
// -------------------------------------------------------------------------------------------------

void CoreEngine::Initialize(const std::string& baseDir) {
    m_baseDir = baseDir;
    m_outputDir = baseDir;
    m_toSortDir = (fs::path(baseDir) / "TO SORT").string();
    m_deleteDir = (fs::path(baseDir) / "delete").string();
    m_flacDir = (fs::path(baseDir) / "flac").string();
    m_mp3Dir = (fs::path(baseDir) / "mp3").string();

    LoadFolderSettings();

    std::string fpcalcBin = (fs::path(m_baseDir) / "fpcalc.exe").string();
    if (!fs::exists(fpcalcBin)) {
        fpcalcBin = (fs::path(m_baseDir).parent_path() / "fpcalc.exe").string();
    }
    if (fs::exists(fpcalcBin)) {
        AcousticAnalyzer::Instance().SetFpcalcPath(fpcalcBin);
    }

    std::string dbPath = (fs::path(m_baseDir) / "music_database.db").string();
    DatabaseManager::GetInstance().InitDatabase(dbPath);

    std::string tracklistPath = (fs::path(m_baseDir) / "tracklist.md").string();
    if (fs::exists(tracklistPath)) {
        DatabaseManager::GetInstance().ImportFromTracklistMarkdown(tracklistPath);
    }
}

void CoreEngine::Shutdown() {
    DatabaseManager::GetInstance().CloseDatabase();
}

void CoreEngine::LoadFolderSettings() {
    fs::path cfgPath = fs::path(m_baseDir) / "folders.cfg";
    std::ifstream in(cfgPath);
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' ')) val.pop_back();

        if (key == "tosort" && !val.empty()) m_toSortDir = val;
        else if (key == "output" && !val.empty()) m_outputDir = val;
        else if (key == "flac" && !val.empty()) m_flacDir = val;
        else if (key == "mp3" && !val.empty()) m_mp3Dir = val;
        else if (key == "acoustid_key" && !val.empty()) m_acoustIdKey = val;
        else if (key == "discogs_token" && !val.empty()) m_discogsToken = val;
    }
    in.close();
}

void CoreEngine::SaveFolderSettings() {
    fs::path cfgPath = fs::path(m_baseDir) / "folders.cfg";
    std::ofstream out(cfgPath);
    if (!out.is_open()) return;
    out << "tosort=" << m_toSortDir << "\n";
    out << "output=" << m_outputDir << "\n";
    out << "flac=" << m_flacDir << "\n";
    out << "mp3=" << m_mp3Dir << "\n";
    out << "acoustid_key=" << m_acoustIdKey << "\n";
    out << "discogs_token=" << m_discogsToken << "\n";
    out.close();
}

void CoreEngine::SetFolders(const std::string& tosort, const std::string& output,
                           const std::string& flac, const std::string& mp3,
                           const std::string& acoustid, const std::string& discogs) {
    if (!tosort.empty()) m_toSortDir = tosort;
    if (!output.empty()) m_outputDir = output;
    if (!flac.empty()) m_flacDir = flac;
    if (!mp3.empty()) m_mp3Dir = mp3;
    if (!acoustid.empty()) m_acoustIdKey = acoustid;
    if (!discogs.empty()) m_discogsToken = discogs;
    SaveFolderSettings();
}

// -------------------------------------------------------------------------------------------------
// Duplicates Scan & Resolution
// -------------------------------------------------------------------------------------------------

void CoreEngine::StartDuplicateScan() {
    if (m_isDuplicateScanning) return;
    m_isDuplicateScanning = true;

    std::thread([this]() {
        LOG_INFO("[DUPLICATES] Starting acoustic duplicate scan in: " + m_toSortDir);
        std::vector<ABCandidatePair> cands;
        std::vector<std::string> autoDel;

        AcousticAnalyzer::Instance().AnalyzeDirectory(m_toSortDir, m_baseDir, cands, autoDel);

        std::unordered_set<std::string> seen;
        std::vector<std::string> uniqueAutoDel;
        for (const auto& p : autoDel) {
            if (seen.insert(p).second) uniqueAutoDel.push_back(p);
        }

        fs::path toSortParent = fs::path(m_toSortDir).parent_path();
        for (const auto& pStr : uniqueAutoDel) {
            fs::path p(pStr);
            if (fs::exists(p)) {
                fs::path rel = fs::relative(p, toSortParent);
                fs::path dst = fs::path(m_deleteDir) / rel;
                std::error_code ec;
                if (fs::weakly_canonical(p, ec) == fs::weakly_canonical(dst, ec)) continue;

                fs::create_directories(dst.parent_path());
                try {
                    if (fs::exists(dst)) fs::remove(dst);
                    fs::rename(p, dst);
                    CleanupEmptyParentDirectories(p.parent_path(), fs::path(m_toSortDir));
                } catch (const std::exception& ex) {
                    LOG_WARN("[AUTO-DELETE FAIL] " + std::string(ex.what()));
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_duplicatesMutex);
            m_candidates = std::move(cands);
            m_autoDelete = std::move(uniqueAutoDel);
        }

        if (!m_candidates.empty()) {
            AudioEngine::Instance().LoadTrackA(m_candidates[0].trackA_path);
            AudioEngine::Instance().LoadTrackB(m_candidates[0].trackB_path);
        }

        m_isDuplicateScanning = false;
        LOG_INFO("[DUPLICATES] Scan completed. Found " + std::to_string(m_candidates.size()) + " candidate pairs.");
    }).detach();
}

std::vector<ABCandidatePair> CoreEngine::GetDuplicateCandidates() {
    std::lock_guard<std::mutex> lock(m_duplicatesMutex);
    return m_candidates;
}

bool CoreEngine::ResolveDuplicate(size_t index, const std::string& action) {
    std::lock_guard<std::mutex> lock(m_duplicatesMutex);
    if (index >= m_candidates.size()) return false;

    const auto& pair = m_candidates[index];
    fs::path toSortParent = fs::path(m_toSortDir).parent_path();

    if (action == "keep_a") {
        fs::path pB(pair.trackB_path);
        if (fs::exists(pB)) {
            fs::path rel = fs::relative(pB, toSortParent);
            fs::path dst = fs::path(m_deleteDir) / rel;
            fs::create_directories(dst.parent_path());
            std::error_code ec;
            if (fs::exists(dst)) fs::remove(dst, ec);
            fs::rename(pB, dst, ec);
            CleanupEmptyParentDirectories(pB.parent_path(), fs::path(m_toSortDir));
            LOG_INFO("[RESOLVE A/B] Kept A, moved B to delete/: " + rel.string());
        }
    } else if (action == "keep_b") {
        fs::path pA(pair.trackA_path);
        if (fs::exists(pA)) {
            fs::path rel = fs::relative(pA, toSortParent);
            fs::path dst = fs::path(m_deleteDir) / rel;
            fs::create_directories(dst.parent_path());
            std::error_code ec;
            if (fs::exists(dst)) fs::remove(dst, ec);
            fs::rename(pA, dst, ec);
            CleanupEmptyParentDirectories(pA.parent_path(), fs::path(m_toSortDir));
            LOG_INFO("[RESOLVE A/B] Kept B, moved A to delete/: " + rel.string());
        }
    } else if (action == "skip") {
        LOG_INFO("[RESOLVE A/B] Skipped pair: " + pair.relA);
    }

    m_candidates.erase(m_candidates.begin() + index);

    if (!m_candidates.empty()) {
        size_t nextIdx = (index < m_candidates.size()) ? index : 0;
        AudioEngine::Instance().LoadTrackA(m_candidates[nextIdx].trackA_path);
        AudioEngine::Instance().LoadTrackB(m_candidates[nextIdx].trackB_path);
    } else {
        AudioEngine::Instance().Pause();
    }

    return true;
}

// -------------------------------------------------------------------------------------------------
// Tag & Release Inspection
// -------------------------------------------------------------------------------------------------

void CoreEngine::StartTagScan() {
    if (m_isTagScanning) return;
    m_isTagScanning = true;

    {
        std::lock_guard<std::mutex> lock(m_tagMutex);
        m_tagItems.clear();
        m_fetchedCount = 0;
        m_tagScanTotal = 0;
        m_tagScanStartTime = std::chrono::steady_clock::now();
        m_tagScanEndTime = {};
    }

    std::thread([this]() {
        std::vector<std::string> files;
        if (fs::exists(m_toSortDir)) {
            for (auto& p : fs::recursive_directory_iterator(m_toSortDir)) {
                if (p.is_regular_file()) {
                    std::string ext = p.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".flac" || ext == ".mp3") {
                        files.push_back(p.path().string());
                    }
                }
            }
        }

        m_tagScanTotal = files.size();
        LOG_INFO("[TAG SCAN] Found " + std::to_string(files.size()) + " audio files in TO SORT/");

        if (files.empty()) {
            CleanupEmptyDirectories(fs::path(m_toSortDir));
            m_tagScanEndTime = std::chrono::steady_clock::now();
            m_isTagScanning = false;
            return;
        }

        std::vector<TagReviewItem> items(files.size());
        for (size_t i = 0; i < files.size(); ++i) {
            auto& item = items[i];
            item.filePath = files[i];
            item.relPath = fs::relative(files[i], m_baseDir).string();
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
            if (!parsed.title.empty()) title = parsed.title;

            std::string artistClean = CleanMetadataString(artistRaw);
            std::regex trackPrefixRegex(R"(^\s*([A-Za-z]?\d{1,2}[.\-_]\s*|\d{1,2}\s+-\s+))");
            artistClean = std::regex_replace(artistClean, trackPrefixRegex, "");
            std::string artistCleanKey = NormalizeKey(artistClean);
            if (artistClean.empty() || artistCleanKey == "tosort" || artistCleanKey == "media" || artistCleanKey == "music" || artistCleanKey == "singles" || artistCleanKey == "downloads") {
                if (parsed.hasArtist && !parsed.artist.empty()) {
                    artistClean = std::regex_replace(parsed.artist, trackPrefixRegex, "");
                } else {
                    artistClean = ExtractArtistFromFilename(item.originalFilename);
                    if (artistClean.empty()) artistClean = "Unknown Artist";
                    else artistClean = std::regex_replace(artistClean, trackPrefixRegex, "");
                }
            } else if (parsed.hasArtist && !parsed.artist.empty()) {
                artistClean = std::regex_replace(parsed.artist, trackPrefixRegex, "");
            }

            std::string albumClean = CleanAlbumTitle(albumRaw);
            if (albumClean.empty()) albumClean = CleanMetadataString(albumRaw);
            if (parsed.hasAlbum && !parsed.album.empty()) albumClean = parsed.album;

            std::string albumCleanKey = NormalizeKey(albumClean);
            if (albumCleanKey == "tosort" || albumCleanKey == "music" || albumCleanKey == "media" || albumCleanKey == "singles" || albumCleanKey == "downloads") {
                albumClean = "";
            }

            if (!parsed.hasAlbum && NormalizeKey(albumClean) == NormalizeKey(artistClean)) {
                albumClean = "";
            }

            std::string parentName = fs::path(files[i]).parent_path().filename().string();
            std::string parentKey = NormalizeKey(parentName);
            bool inStaging = (parentKey == "tosort" || parentKey == "music" || parentKey == "media" || parentKey == "singles" || parentKey == "downloads");
            if (inStaging && !parsed.hasAlbum) albumClean = "";

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

            fs::path folderPath = fs::path(files[i]).parent_path();
            for (auto& cfile : fs::directory_iterator(folderPath)) {
                if (cfile.is_regular_file()) {
                    std::string cext = cfile.path().extension().string();
                    std::transform(cext.begin(), cext.end(), cext.begin(), ::tolower);
                    if (cext == ".jpg" || cext == ".jpeg" || cext == ".png" || cext == ".bmp") {
                        std::ifstream fIn(cfile.path(), std::ios::binary);
                        if (fIn.is_open()) {
                            item.localCoverBytes = std::vector<unsigned char>((std::istreambuf_iterator<char>(fIn)), std::istreambuf_iterator<char>());
                            item.localCoverPath = cfile.path().string();
                            break;
                        }
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_tagMutex);
            m_tagItems = items;
        }

        // Parallel Fingerprinting
        std::vector<AudioFingerprint> fpResults(files.size());
        unsigned int numFpThreads = (std::min)(std::thread::hardware_concurrency(), 8u);
        if (numFpThreads == 0) numFpThreads = 4;
        std::atomic<size_t> fpTaskIdx{0};
        std::vector<std::thread> fpWorkers;
        for (unsigned int t = 0; t < numFpThreads; ++t) {
            fpWorkers.emplace_back([&]() {
                while (true) {
                    size_t idx = fpTaskIdx.fetch_add(1);
                    if (idx >= files.size()) break;
                    fpResults[idx] = AcousticAnalyzer::Instance().ExtractFingerprint(files[idx]);
                    std::lock_guard<std::mutex> lock(m_tagMutex);
                    m_tagItems[idx].duration = fpResults[idx].duration;
                }
            });
        }
        for (auto& w : fpWorkers) {
            if (w.joinable()) w.join();
        }

        // Album Clustering
        struct InternalCluster {
            std::string albumKey;
            std::string sampleFile;
            std::vector<size_t> indices;
        };
        std::vector<InternalCluster> clusters;
        std::unordered_map<std::string, size_t> keyToClusterIdx;

        for (size_t i = 0; i < files.size(); ++i) {
            std::string albumClean;
            {
                std::lock_guard<std::mutex> lock(m_tagMutex);
                albumClean = m_tagItems[i].albumBuf;
            }
            std::string albumKey = NormalizeKey(albumClean);
            std::string parentName = fs::path(files[i]).parent_path().filename().string();
            std::string parentKey = NormalizeKey(parentName);
            bool inStaging = (parentKey.empty() || parentKey == "tosort" || parentKey == "music" || parentKey == "media" || parentKey == "singles" || parentKey == "downloads");

            bool isLoose = false;
            if (inStaging) {
                if (albumKey.empty() || albumKey == "unknown" || albumKey == "tosort" || albumKey == "music" || albumKey == "media" || albumKey == "singles" || albumKey == "downloads") {
                    isLoose = true;
                }
            } else if (albumKey.empty() || albumKey == "unknown") {
                isLoose = true;
            }

            if (isLoose) {
                std::lock_guard<std::mutex> lock(m_tagMutex);
                m_tagItems[i].isSingleTrack = true;
                std::string singleKey = "__single_track_" + std::to_string(i);
                clusters.push_back({ singleKey, files[i], { i } });
                continue;
            }

            auto it = keyToClusterIdx.find(albumKey);
            if (it == keyToClusterIdx.end()) {
                size_t newIdx = clusters.size();
                keyToClusterIdx[albumKey] = newIdx;
                clusters.push_back({ albumKey, files[i], { i } });
            } else {
                clusters[it->second].indices.push_back(i);
            }
        }

        // Concurrent Album Resolution
        std::atomic<size_t> nextClusterIdx{0};
        unsigned int numAlbumWorkers = (std::min)((unsigned int)clusters.size(), 4u);
        if (numAlbumWorkers == 0) numAlbumWorkers = 1;

        std::vector<std::thread> albumWorkers;
        for (unsigned int w = 0; w < numAlbumWorkers; ++w) {
            albumWorkers.emplace_back([&]() {
                while (true) {
                    size_t cIdx = nextClusterIdx.fetch_add(1);
                    if (cIdx >= clusters.size()) break;

                    const auto& cluster = clusters[cIdx];
                    const auto& indices = cluster.indices;
                    if (indices.empty()) continue;

                    size_t leadIdx = indices[0];
                    std::string artistClean, albumClean, titleClean;
                    {
                        std::lock_guard<std::mutex> lock(m_tagMutex);
                        artistClean = m_tagItems[leadIdx].artistBuf;
                        albumClean = m_tagItems[leadIdx].albumBuf;
                        titleClean = m_tagItems[leadIdx].titleBuf;
                    }

                    std::string releaseGroupMbId;
                    std::string firstReleaseDate;
                    std::vector<unsigned char> coverData;
                    std::string coverSource;
                    bool isMatched = false;
                    MatchTier detectedTier = MatchTier::Niche_Local;
                    std::vector<MBTrackEntry> resolvedTracks;

                    // AcoustID lookup
                    const auto& fpInfo = fpResults[leadIdx];
                    if (!fpInfo.fpBase64.empty() && !m_acoustIdKey.empty()) {
                        std::ostringstream postStream;
                        postStream << "client=" << m_acoustIdKey << "&meta=recordings+releasegroups+compress&duration=" << (int)fpInfo.duration << "&fingerprint=" << fpInfo.fpBase64;
                        std::string acoustRes = AcoustIdHttpPost(postStream.str());
                        auto acoustResults = ParseAcoustIdResponse(acoustRes);
                        if (!acoustResults.empty()) {
                            double bestScore = -1.0;
                            for (const auto& ar : acoustResults) {
                                if (ar.score > bestScore) {
                                    bestScore = ar.score;
                                    if (!ar.releaseGroupIds.empty()) {
                                        releaseGroupMbId = ar.releaseGroupIds[0];
                                        isMatched = true;
                                        detectedTier = MatchTier::AcoustId;
                                    }
                                }
                            }
                        }
                    }

                    // MusicBrainz release-group search if not matched yet
                    if (releaseGroupMbId.empty() && !albumClean.empty() && !artistClean.empty()) {
                        std::string searchUrl = "https://musicbrainz.org/ws/2/release-group/?query=releasegroup:" + UrlEncode(albumClean) + "%20AND%20artist:" + UrlEncode(artistClean) + "&fmt=json";
                        std::string searchRes = HttpGetString(Utf8ToWide(searchUrl));
                        size_t p = 0;
                        JsonVal searchDoc = ParseJsonSimple(searchRes, p);
                        const auto& rgs = searchDoc.get("release-groups");
                        if (rgs.type == JsonVal::Array && !rgs.arrVal.empty()) {
                            releaseGroupMbId = rgs.get(0).get("id").strVal;
                            firstReleaseDate = rgs.get(0).get("first-release-date").strVal;
                            isMatched = true;
                            detectedTier = MatchTier::TierA;
                        }
                    }

                    // Fetch release details and tracks from MusicBrainz
                    if (!releaseGroupMbId.empty()) {
                        std::string relLookupUrl = "https://musicbrainz.org/ws/2/release?release-group=" + releaseGroupMbId + "&inc=recordings+artist-credits&fmt=json";
                        std::string relLookupRes = HttpGetString(Utf8ToWide(relLookupUrl));
                        size_t p = 0;
                        JsonVal relDoc = ParseJsonSimple(relLookupRes, p);
                        const auto& rels = relDoc.get("releases");
                        if (rels.type == JsonVal::Array && !rels.arrVal.empty()) {
                            std::string relId = rels.get(0).get("id").strVal;
                            if (firstReleaseDate.empty()) firstReleaseDate = rels.get(0).get("date").strVal;

                            std::string detailUrl = "https://musicbrainz.org/ws/2/release/" + relId + "?inc=recordings+artist-credits&fmt=json";
                            std::string detailRes = HttpGetString(Utf8ToWide(detailUrl));
                            size_t dp = 0;
                            JsonVal dDoc = ParseJsonSimple(detailRes, dp);
                            const auto& media = dDoc.get("media");
                            if (media.type == JsonVal::Array) {
                                int trkNum = 1;
                                for (size_t m = 0; m < media.arrVal.size(); ++m) {
                                    const auto& trks = media.get(m).get("tracks");
                                    if (trks.type != JsonVal::Array) continue;
                                    for (size_t ti = 0; ti < trks.arrVal.size(); ++ti) {
                                        MBTrackEntry entry;
                                        entry.position = (int)trks.get(ti).get("position").numVal;
                                        if (entry.position <= 0) entry.position = trkNum;
                                        trkNum++;
                                        entry.title = trks.get(ti).get("title").strVal;
                                        if (entry.title.empty()) entry.title = trks.get(ti).get("recording").get("title").strVal;
                                        entry.lengthMs = (int)trks.get(ti).get("length").numVal;
                                        resolvedTracks.push_back(entry);
                                    }
                                }
                            }

                            // Fetch cover art archive (release first, then release-group fallback)
                            std::string caUrl = "https://coverartarchive.org/release/" + relId + "/front";
                            coverData = HttpGetBytes(Utf8ToWide(caUrl));
                            if (coverData.empty() && !releaseGroupMbId.empty()) {
                                coverData = HttpGetBytes(Utf8ToWide("https://coverartarchive.org/release-group/" + releaseGroupMbId + "/front"));
                            }
                            if (coverData.empty()) {
                                coverData = HttpGetBytes(Utf8ToWide("https://coverartarchive.org/release/" + relId + "/front-500"));
                            }
                            if (coverData.empty() && !releaseGroupMbId.empty()) {
                                coverData = HttpGetBytes(Utf8ToWide("https://coverartarchive.org/release-group/" + releaseGroupMbId + "/front-500"));
                            }
                            if (coverData.empty()) {
                                coverData = HttpGetBytes(Utf8ToWide("https://coverartarchive.org/release/" + relId + "/front-250"));
                            }
                            if (coverData.empty() && !releaseGroupMbId.empty()) {
                                coverData = HttpGetBytes(Utf8ToWide("https://coverartarchive.org/release-group/" + releaseGroupMbId + "/front-250"));
                            }
                            if (!coverData.empty() && IsValidImageData(coverData)) {
                                coverSource = "CoverArtArchive";
                            } else {
                                coverData.clear();
                            }
                        }
                    }

                    // Apply metadata and multi-provider candidates to items in cluster
                    {
                        std::lock_guard<std::mutex> lock(m_tagMutex);
                        for (size_t idx : indices) {
                            auto& it = m_tagItems[idx];
                            it.isMusicBrainzMatched = isMatched;
                            it.matchTier = detectedTier;
                            it.releaseGroupMbId = releaseGroupMbId;

                            if (!firstReleaseDate.empty()) {
                                std::string yr = ExtractYearFromString(firstReleaseDate);
                                strncpy_s(it.yearBuf, yr.c_str(), sizeof(it.yearBuf) - 1);
                            }

                            if (!resolvedTracks.empty()) {
                                ApplyTrackMatch(it, resolvedTracks);
                            }

                            if (!coverData.empty()) {
                                it.onlineCoverBytes = coverData;
                                it.onlineCoverSource = coverSource;
                                it.selectedCoverChoice = 1;
                            } else if (!it.localCoverBytes.empty()) {
                                it.selectedCoverChoice = 0;
                            }

                            // Generate default candidate
                            ConsensusAggregator::MetadataCandidate cand;
                            cand.providerName = (detectedTier == MatchTier::AcoustId) ? "AcoustID" : (isMatched ? "MusicBrainz" : "Local");
                            cand.title = it.titleBuf;
                            cand.artist = it.artistBuf;
                            cand.album = it.albumBuf;
                            cand.year = it.yearBuf;
                            cand.confidence = isMatched ? 0.95 : 0.60;
                            it.candidates.push_back(cand);
                            it.confidenceScore = cand.confidence;
                            it.isFetchCompleted = true;
                            m_fetchedCount++;
                        }
                    }
                }
            });
        }
        for (auto& w : albumWorkers) {
            if (w.joinable()) w.join();
        }

        m_tagScanEndTime = std::chrono::steady_clock::now();
        m_isTagScanning = false;
        LOG_INFO("[TAG SCAN] Completed tag scanning and resolution for all items.");
    }).detach();
}

void CoreEngine::GetTagScanProgress(size_t& done, size_t& total, double& fraction, double& speed,
                                   std::string& eta, std::string& elapsed) {
    total = m_tagScanTotal.load();
    done = m_fetchedCount.load();
    if (done > total && total > 0) done = total;

    fraction = (total > 0) ? ((double)done / (double)total) : 0.0;

    auto now = std::chrono::steady_clock::now();
    auto effectiveEnd = (m_isTagScanning || m_tagScanEndTime.time_since_epoch().count() == 0) ? now : m_tagScanEndTime;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(effectiveEnd - m_tagScanStartTime).count();
    double elapsedSec = (elapsedMs > 0 && m_tagScanStartTime.time_since_epoch().count() > 0) ? (elapsedMs / 1000.0) : 0.0;

    int em = (int)elapsedSec / 60;
    int es = (int)elapsedSec % 60;
    char elBuf[32];
    snprintf(elBuf, sizeof(elBuf), "%02d:%02d", em, es);
    elapsed = elBuf;

    speed = (elapsedSec > 0.5 && done > 0) ? ((double)done / elapsedSec) : 0.0;

    char etaBuf[32];
    if (done == 0 || elapsedSec < 0.5) {
        snprintf(etaBuf, sizeof(etaBuf), "Расчёт...");
    } else if (done >= total && total > 0) {
        snprintf(etaBuf, sizeof(etaBuf), "00:00");
    } else if (speed > 0.0001) {
        int remSec = (int)(((double)(total - done)) / speed);
        if (remSec < 0) remSec = 0;
        int rm = (remSec % 3600) / 60;
        int rs = remSec % 60;
        snprintf(etaBuf, sizeof(etaBuf), "%02d:%02d", rm, rs);
    } else {
        snprintf(etaBuf, sizeof(etaBuf), "Расчёт...");
    }
    eta = etaBuf;
}

std::vector<AlbumGroup> CoreEngine::GetAlbumGroups() {
    std::lock_guard<std::mutex> lock(m_tagMutex);
    std::vector<AlbumGroup> groups;
    std::unordered_map<std::string, size_t> keyToGroupIdx;

    for (size_t i = 0; i < m_tagItems.size(); ++i) {
        const auto& it = m_tagItems[i];
        std::string alb(it.albumBuf);
        std::string key = NormalizeKey(alb);
        if (key.empty()) key = "__single_" + std::to_string(i);

        auto gIt = keyToGroupIdx.find(key);
        if (gIt == keyToGroupIdx.end()) {
            size_t newIdx = groups.size();
            keyToGroupIdx[key] = newIdx;

            AlbumGroup g;
            g.albumKey = key;
            g.album = alb.empty() ? (it.titleBuf) : alb;
            g.artist = it.artistBuf;
            g.year = it.yearBuf;
            g.trackCount = 1;
            g.confidenceScore = it.confidenceScore;
            g.hasConflict = it.hasConflict;
            g.selectedCoverChoice = it.selectedCoverChoice;
            g.hasLocalCover = !it.localCoverBytes.empty();
            g.hasOnlineCover = !it.onlineCoverBytes.empty();
            g.onlineCoverSource = it.onlineCoverSource;
            g.candidates = it.candidates;
            g.trackIndices.push_back(i);

            switch (it.matchTier) {
                case MatchTier::AcoustId: g.matchTierName = "AcoustID"; break;
                case MatchTier::TierA: g.matchTierName = "MusicBrainz (Tier A)"; break;
                case MatchTier::TierB_Verified: g.matchTierName = "Tier B (Verified)"; break;
                case MatchTier::Discogs: g.matchTierName = "Discogs"; break;
                case MatchTier::TouhouDB: g.matchTierName = "TouhouDB"; break;
                case MatchTier::THBWiki: g.matchTierName = "THBWiki"; break;
                case MatchTier::VocaDB: g.matchTierName = "VocaDB"; break;
                case MatchTier::UtaiteDB: g.matchTierName = "UtaiteDB"; break;
                default: g.matchTierName = "Niche / Local"; break;
            }

            groups.push_back(g);
        } else {
            auto& g = groups[gIt->second];
            g.trackCount++;
            g.trackIndices.push_back(i);
            if (it.hasConflict) g.hasConflict = true;
            if (!it.localCoverBytes.empty()) g.hasLocalCover = true;
            if (!it.onlineCoverBytes.empty()) g.hasOnlineCover = true;
        }
    }

    return groups;
}

std::vector<TagReviewItem> CoreEngine::GetTagItems() {
    std::lock_guard<std::mutex> lock(m_tagMutex);
    return m_tagItems;
}

TagReviewItem CoreEngine::GetTagItem(size_t index) {
    std::lock_guard<std::mutex> lock(m_tagMutex);
    if (index < m_tagItems.size()) return m_tagItems[index];
    return TagReviewItem{};
}

std::vector<size_t> CoreEngine::GetAlbumTrackIndices(size_t referenceIndex) const {
    std::lock_guard<std::mutex> lock(m_tagMutex);
    std::vector<size_t> indices;
    if (referenceIndex >= m_tagItems.size()) return indices;

    std::string refAlbum(m_tagItems[referenceIndex].albumBuf);
    std::string refKey = NormalizeKey(refAlbum);

    if (refKey.empty() || m_tagItems[referenceIndex].isSingleTrack) {
        indices.push_back(referenceIndex);
        return indices;
    }

    for (size_t i = 0; i < m_tagItems.size(); ++i) {
        if (m_tagItems[i].isProcessed) continue;
        std::string alb(m_tagItems[i].albumBuf);
        if (NormalizeKey(alb) == refKey) {
            indices.push_back(i);
        }
    }
    return indices;
}

void CoreEngine::ApproveTrack(size_t trackIndex) {
    ExecuteTrackApprovalBatch({ trackIndex });
}

void CoreEngine::ApproveAlbum(size_t referenceTrackIndex) {
    std::vector<size_t> indices = GetAlbumTrackIndices(referenceTrackIndex);
    ExecuteTrackApprovalBatch(indices);
}

void CoreEngine::SkipTrack(size_t trackIndex) {
    std::lock_guard<std::mutex> lock(m_tagMutex);
    if (trackIndex < m_tagItems.size()) {
        m_tagItems[trackIndex].isProcessed = true;
    }
}

void CoreEngine::SkipAlbum(size_t referenceTrackIndex) {
    std::vector<size_t> indices = GetAlbumTrackIndices(referenceTrackIndex);
    std::lock_guard<std::mutex> lock(m_tagMutex);
    for (size_t idx : indices) {
        if (idx < m_tagItems.size()) {
            m_tagItems[idx].isProcessed = true;
        }
    }
}

void CoreEngine::ExecuteTrackApprovalBatch(const std::vector<size_t>& indices) {
    if (indices.empty()) return;

    struct ApprovalTask {
        std::string newArtist;
        std::string newAlbum;
        std::string newTitle;
        std::string newTrackNo;
        std::string newYear;
        std::string newLyrics;
        std::string srcFilePath;
        std::string origFilename;
        std::vector<unsigned char> chosenCover;
    };

    std::vector<ApprovalTask> tasks;
    tasks.reserve(indices.size());

    std::vector<unsigned char> fallbackCover;
    {
        std::lock_guard<std::mutex> lock(m_tagMutex);
        for (size_t idx : indices) {
            if (idx < m_tagItems.size()) {
                const auto& it = m_tagItems[idx];
                if (it.selectedCoverChoice == 1 && !it.onlineCoverBytes.empty()) {
                    fallbackCover = it.onlineCoverBytes;
                    break;
                } else if (it.selectedCoverChoice == 0 && !it.localCoverBytes.empty()) {
                    fallbackCover = it.localCoverBytes;
                    break;
                }
            }
        }

        for (size_t idx : indices) {
            if (idx >= m_tagItems.size()) continue;
            auto& it = m_tagItems[idx];
            if (it.isProcessed) continue;

            ApprovalTask t;
            t.newArtist = it.artistBuf;
            t.newAlbum = it.albumBuf;
            t.newTitle = it.titleBuf;
            t.newTrackNo = it.trackNoBuf;
            t.newYear = it.yearBuf;
            t.newLyrics = it.lyricsBuf;
            t.srcFilePath = it.filePath;
            t.origFilename = it.originalFilename;

            if (it.selectedCoverChoice == 1 && !it.onlineCoverBytes.empty()) t.chosenCover = it.onlineCoverBytes;
            else if (it.selectedCoverChoice == 0 && !it.localCoverBytes.empty()) t.chosenCover = it.localCoverBytes;
            else if (!fallbackCover.empty()) t.chosenCover = fallbackCover;

            tasks.push_back(t);
            it.isProcessed = true;
        }
    }

    if (tasks.empty()) return;

    std::thread([this, tasks]() {
        for (const auto& t : tasks) {
            fs::path src(t.srcFilePath);
            if (!fs::exists(src)) continue;

            std::string ext = src.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            std::string safeArtist = SanitizeForFilename(t.newArtist);
            std::string safeAlbum = SanitizeForFilename(t.newAlbum);
            std::string safeTitle = SanitizeForFilename(t.newTitle);

            fs::path flacDir = fs::path(m_flacDir) / safeArtist / safeAlbum;
            fs::path mp3Dir = fs::path(m_mp3Dir) / safeArtist / safeAlbum;
            fs::create_directories(flacDir);
            fs::create_directories(mp3Dir);

            std::string baseTrackName = t.newTrackNo + ". " + safeTitle;

            std::string finalLyrics = t.newLyrics;
            if (finalLyrics.empty()) {
                finalLyrics = FetchLrcLibSyncedLyrics(t.newArtist, t.newTitle, t.newAlbum);
            }

            if (ext == ".flac") {
                fs::path flacFile = flacDir / (baseTrackName + ".flac");
                fs::path mp3File = mp3Dir / (baseTrackName + ".mp3");

                WriteFlacTagsAndPicture(src.string(), t.newArtist, t.newAlbum, t.newTitle, t.newTrackNo, t.newYear, finalLyrics, t.chosenCover);
                try {
                    std::error_code ec;
                    if (fs::exists(flacFile)) fs::remove(flacFile, ec);
                    fs::rename(src, flacFile, ec);
                    ConvertFlacToMp3(flacFile.string(), mp3File.string());
                    WriteMp3TagsAndPicture(mp3File.string(), t.newArtist, t.newAlbum, t.newTitle, t.newTrackNo, t.newYear, finalLyrics, t.chosenCover);
                } catch (...) {}
            } else if (ext == ".mp3") {
                fs::path mp3File = mp3Dir / (baseTrackName + ".mp3");
                WriteMp3TagsAndPicture(src.string(), t.newArtist, t.newAlbum, t.newTitle, t.newTrackNo, t.newYear, finalLyrics, t.chosenCover);
                try {
                    std::error_code ec;
                    if (fs::exists(mp3File)) fs::remove(mp3File, ec);
                    fs::rename(src, mp3File, ec);
                } catch (...) {}
            }

            if (!t.chosenCover.empty()) {
                fs::path flacCover = flacDir / "cover.jpg";
                fs::path mp3Cover = mp3Dir / "cover.jpg";
                if (!fs::exists(flacCover)) {
                    std::ofstream cOut(flacCover, std::ios::binary);
                    if (cOut.is_open()) {
                        cOut.write((const char*)t.chosenCover.data(), t.chosenCover.size());
                        cOut.close();
                    }
                }
                if (!fs::exists(mp3Cover)) {
                    std::ofstream cOut(mp3Cover, std::ios::binary);
                    if (cOut.is_open()) {
                        cOut.write((const char*)t.chosenCover.data(), t.chosenCover.size());
                        cOut.close();
                    }
                }
            }

            CleanupEmptyParentDirectories(src.parent_path(), fs::path(m_toSortDir));
            LOG_INFO("[PROCESSED] Successfully tagged & moved: " + baseTrackName);
        }
    }).detach();
}

void CoreEngine::ApplyCandidateToTrack(size_t trackIndex, int candidateIndex) {
    std::lock_guard<std::mutex> lock(m_tagMutex);
    if (trackIndex >= m_tagItems.size()) return;
    auto& it = m_tagItems[trackIndex];
    if (candidateIndex < 0 || (size_t)candidateIndex >= it.candidates.size()) return;

    const auto& c = it.candidates[candidateIndex];
    if (!c.title.empty()) strncpy_s(it.titleBuf, c.title.c_str(), sizeof(it.titleBuf) - 1);
    if (!c.artist.empty()) strncpy_s(it.artistBuf, c.artist.c_str(), sizeof(it.artistBuf) - 1);
    if (!c.album.empty()) strncpy_s(it.albumBuf, c.album.c_str(), sizeof(it.albumBuf) - 1);
    if (!c.year.empty()) strncpy_s(it.yearBuf, c.year.c_str(), sizeof(it.yearBuf) - 1);
    it.selectedCandidateIndex = candidateIndex;
}

void CoreEngine::ApplyCandidateToAlbum(size_t referenceTrackIndex, int candidateIndex) {
    std::lock_guard<std::mutex> lock(m_tagMutex);
    if (referenceTrackIndex >= m_tagItems.size()) return;
    const auto& refItem = m_tagItems[referenceTrackIndex];
    if (candidateIndex < 0 || (size_t)candidateIndex >= refItem.candidates.size()) return;
    const auto& c = refItem.candidates[candidateIndex];

    std::string refAlbum(refItem.albumBuf);
    std::string refKey = NormalizeKey(refAlbum);
    std::vector<size_t> indices;
    for (size_t i = 0; i < m_tagItems.size(); ++i) {
        if (NormalizeKey(m_tagItems[i].albumBuf) == refKey) {
            indices.push_back(i);
        }
    }

    for (size_t t = 0; t < indices.size(); ++t) {
        size_t idx = indices[t];
        auto& it = m_tagItems[idx];
        if (!c.artist.empty()) strncpy_s(it.artistBuf, c.artist.c_str(), sizeof(it.artistBuf) - 1);
        if (!c.album.empty()) strncpy_s(it.albumBuf, c.album.c_str(), sizeof(it.albumBuf) - 1);
        if (!c.year.empty()) strncpy_s(it.yearBuf, c.year.c_str(), sizeof(it.yearBuf) - 1);
        if (t < c.tracklist.size() && !c.tracklist[t].empty()) {
            strncpy_s(it.titleBuf, c.tracklist[t].c_str(), sizeof(it.titleBuf) - 1);
        } else if (indices.size() == 1 && !c.title.empty()) {
            strncpy_s(it.titleBuf, c.title.c_str(), sizeof(it.titleBuf) - 1);
        }
        it.selectedCandidateIndex = candidateIndex;
    }
}

void CoreEngine::UpdateTrackDetails(size_t trackIndex, const std::string& title,
                                   const std::string& artist, const std::string& trackNo,
                                   const std::string& lyrics) {
    std::lock_guard<std::mutex> lock(m_tagMutex);
    if (trackIndex >= m_tagItems.size()) return;
    auto& it = m_tagItems[trackIndex];
    if (!title.empty()) strncpy_s(it.titleBuf, title.c_str(), sizeof(it.titleBuf) - 1);
    if (!artist.empty()) strncpy_s(it.artistBuf, artist.c_str(), sizeof(it.artistBuf) - 1);
    if (!trackNo.empty()) strncpy_s(it.trackNoBuf, trackNo.c_str(), sizeof(it.trackNoBuf) - 1);
    if (!lyrics.empty()) {
        strncpy_s(it.lyricsBuf, lyrics.c_str(), sizeof(it.lyricsBuf) - 1);
        it.hasLyrics = true;
    }
}

void CoreEngine::SetCoverChoice(size_t trackIndex, int choice, bool applyToAlbum) {
    std::lock_guard<std::mutex> lock(m_tagMutex);
    if (trackIndex >= m_tagItems.size()) return;
    if (applyToAlbum) {
        std::string alb(m_tagItems[trackIndex].albumBuf);
        std::string key = NormalizeKey(alb);
        for (auto& it : m_tagItems) {
            if (NormalizeKey(it.albumBuf) == key) {
                it.selectedCoverChoice = choice;
            }
        }
    } else {
        m_tagItems[trackIndex].selectedCoverChoice = choice;
    }
}

std::vector<unsigned char> CoreEngine::GetCoverImageBytes(size_t trackIndex, const std::string& type) {
    std::lock_guard<std::mutex> lock(m_tagMutex);
    if (trackIndex < m_tagItems.size()) {
        const auto& it = m_tagItems[trackIndex];
        if (type == "online") return it.onlineCoverBytes;
        return it.localCoverBytes;
    }
    return {};
}

void CoreEngine::FetchManualMusicBrainz(const std::string& inputUrl, size_t referenceTrackIndex, bool applyToAlbum) {
    std::regex uuidRegex(R"([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})");
    std::smatch match;
    if (!std::regex_search(inputUrl, match, uuidRegex)) return;

    std::string mbid = match.str(0);
    std::thread([this, mbid, inputUrl, referenceTrackIndex, applyToAlbum]() {
        std::string releaseMbId = mbid;
        if (inputUrl.find("release-group") != std::string::npos) {
            std::string rgLookupUrl = "https://musicbrainz.org/ws/2/release?release-group=" + mbid + "&inc=recordings+artist-credits&fmt=json";
            std::string rgRes = HttpGetString(Utf8ToWide(rgLookupUrl));
            size_t p = 0;
            JsonVal rgDoc = ParseJsonSimple(rgRes, p);
            const auto& rels = rgDoc.get("releases");
            if (rels.type == JsonVal::Array && !rels.arrVal.empty()) {
                releaseMbId = rels.get(0).get("id").strVal;
            }
        }

        std::string lookupUrl = "https://musicbrainz.org/ws/2/release/" + releaseMbId + "?inc=recordings+artist-credits&fmt=json";
        std::string mbJsonStr = HttpGetString(Utf8ToWide(lookupUrl));
        if (mbJsonStr.empty()) return;

        size_t p = 0;
        JsonVal doc = ParseJsonSimple(mbJsonStr, p);
        std::string releaseTitle = doc.get("title").strVal;
        std::string releaseDate = doc.get("date").strVal;
        std::string artistCredit = "";
        const auto& ac = doc.get("artist-credit");
        if (ac.type == JsonVal::Array && !ac.arrVal.empty()) {
            artistCredit = ac.get(0).get("name").strVal;
        }

        std::vector<MBTrackEntry> mbTracks;
        const auto& media = doc.get("media");
        if (media.type == JsonVal::Array) {
            int trkCnt = 1;
            for (size_t m = 0; m < media.arrVal.size(); ++m) {
                const auto& trs = media.get(m).get("tracks");
                if (trs.type != JsonVal::Array) continue;
                for (size_t i = 0; i < trs.arrVal.size(); ++i) {
                    MBTrackEntry ti;
                    ti.position = (int)trs.get(i).get("position").numVal;
                    if (ti.position <= 0) ti.position = trkCnt;
                    trkCnt++;
                    ti.title = trs.get(i).get("title").strVal;
                    mbTracks.push_back(ti);
                }
            }
        }

        std::string caUrl = "https://coverartarchive.org/release/" + releaseMbId + "/front";
        std::vector<unsigned char> coverBytes = HttpGetBytes(Utf8ToWide(caUrl));
        if (coverBytes.empty() && !mbid.empty()) {
            coverBytes = HttpGetBytes(Utf8ToWide("https://coverartarchive.org/release-group/" + mbid + "/front"));
        }
        if (coverBytes.empty()) {
            coverBytes = HttpGetBytes(Utf8ToWide("https://coverartarchive.org/release/" + releaseMbId + "/front-500"));
        }
        if (coverBytes.empty() && !mbid.empty()) {
            coverBytes = HttpGetBytes(Utf8ToWide("https://coverartarchive.org/release-group/" + mbid + "/front-500"));
        }
        if (coverBytes.empty()) {
            coverBytes = HttpGetBytes(Utf8ToWide("https://coverartarchive.org/release/" + releaseMbId + "/front-250"));
        }
        if (coverBytes.empty() && !mbid.empty()) {
            coverBytes = HttpGetBytes(Utf8ToWide("https://coverartarchive.org/release-group/" + mbid + "/front-250"));
        }
        if (!coverBytes.empty() && !IsValidImageData(coverBytes)) {
            coverBytes.clear();
        }

        std::vector<size_t> targetIndices = applyToAlbum ? GetAlbumTrackIndices(referenceTrackIndex) : std::vector<size_t>{ referenceTrackIndex };

        std::lock_guard<std::mutex> lock(m_tagMutex);
        for (size_t idx : targetIndices) {
            if (idx >= m_tagItems.size()) continue;
            auto& it = m_tagItems[idx];
            if (!releaseTitle.empty()) strncpy_s(it.albumBuf, releaseTitle.c_str(), sizeof(it.albumBuf) - 1);
            if (!artistCredit.empty()) strncpy_s(it.artistBuf, artistCredit.c_str(), sizeof(it.artistBuf) - 1);
            if (!releaseDate.empty()) {
                std::string yr = ExtractYearFromString(releaseDate);
                strncpy_s(it.yearBuf, yr.c_str(), sizeof(it.yearBuf) - 1);
            }
            if (!mbTracks.empty()) ApplyTrackMatch(it, mbTracks);
            if (!coverBytes.empty()) {
                it.onlineCoverBytes = coverBytes;
                it.onlineCoverSource = "CoverArtArchive";
                it.selectedCoverChoice = 1;
            }
        }
    }).detach();
}

void CoreEngine::FetchManualDiscogs(const std::string& inputUrl, size_t referenceTrackIndex, bool applyToAlbum) {
    std::regex idRegex(R"(release[s]?/([0-9]+))");
    std::smatch match;
    std::string relId = inputUrl;
    if (std::regex_search(inputUrl, match, idRegex)) {
        relId = match.str(1);
    }
    std::thread([this, relId, referenceTrackIndex, applyToAlbum]() {
        bool isMaster = false;
        DiscogsReleaseInfo discInfo;
        if (!FetchDiscogsReleaseDetails(relId, isMaster, discInfo, m_discogsToken)) return;

        std::vector<unsigned char> coverData;
        if (!discInfo.coverUrl.empty()) {
            coverData = HttpGetBytes(Utf8ToWide(discInfo.coverUrl), m_discogsToken);
        }

        std::vector<size_t> targetIndices = applyToAlbum ? GetAlbumTrackIndices(referenceTrackIndex) : std::vector<size_t>{ referenceTrackIndex };

        std::lock_guard<std::mutex> lock(m_tagMutex);
        for (size_t idx : targetIndices) {
            if (idx >= m_tagItems.size()) continue;
            auto& it = m_tagItems[idx];
            if (!discInfo.title.empty()) strncpy_s(it.albumBuf, discInfo.title.c_str(), sizeof(it.albumBuf) - 1);
            if (!discInfo.artist.empty()) strncpy_s(it.artistBuf, discInfo.artist.c_str(), sizeof(it.artistBuf) - 1);
            if (!discInfo.year.empty() && discInfo.year != "0") strncpy_s(it.yearBuf, discInfo.year.c_str(), sizeof(it.yearBuf) - 1);
            if (!discInfo.tracks.empty()) ApplyTrackMatch(it, discInfo.tracks);
            if (!coverData.empty()) {
                it.onlineCoverBytes = coverData;
                it.onlineCoverSource = "Discogs";
                it.selectedCoverChoice = 1;
            }
            it.matchTier = MatchTier::Discogs;
            it.isFetchCompleted = true;
        }
        LOG_INFO("[MANUAL DISCOGS MATCHED] Loaded release: " + discInfo.title);
    }).detach();
}

void CoreEngine::FetchManualTouhouDb(const std::string& inputUrl, size_t referenceTrackIndex, bool applyToAlbum) {
    int albumId = 0;
    std::regex idRegex(R"((\d{1,8}))");
    std::smatch match;
    if (std::regex_search(inputUrl, match, idRegex)) {
        try { albumId = std::stoi(match.str(1)); } catch (...) {}
    }
    if (albumId <= 0) return;

    std::thread([this, albumId, referenceTrackIndex, applyToAlbum]() {
        VdbReleaseInfo info;
        if (!FetchVdbAlbumDetails("https://touhoudb.com", "TouhouDB", albumId, info)) return;

        std::vector<unsigned char> coverData;
        if (!info.coverUrl.empty()) {
            coverData = HttpGetBytes(Utf8ToWide(info.coverUrl));
        }

        std::vector<size_t> targetIndices = applyToAlbum ? GetAlbumTrackIndices(referenceTrackIndex) : std::vector<size_t>{ referenceTrackIndex };

        std::lock_guard<std::mutex> lock(m_tagMutex);
        for (size_t idx : targetIndices) {
            if (idx >= m_tagItems.size()) continue;
            auto& it = m_tagItems[idx];
            if (!info.artist.empty()) strncpy_s(it.artistBuf, info.artist.c_str(), sizeof(it.artistBuf) - 1);
            if (!info.title.empty()) strncpy_s(it.albumBuf, info.title.c_str(), sizeof(it.albumBuf) - 1);
            if (!info.releaseDate.empty()) {
                std::string yr = ExtractYearFromString(info.releaseDate);
                strncpy_s(it.yearBuf, yr.c_str(), sizeof(it.yearBuf) - 1);
            }
            if (!info.tracks.empty()) ApplyTrackMatch(it, info.tracks);
            if (!coverData.empty()) {
                it.onlineCoverBytes = coverData;
                it.onlineCoverSource = "TouhouDB";
                it.selectedCoverChoice = 1;
            }
            it.matchTier = MatchTier::TouhouDB;
            it.isFetchCompleted = true;
        }
        LOG_INFO("[MANUAL TOUHOUDB MATCHED] Loaded release: " + info.title);
    }).detach();
}

void CoreEngine::FetchManualThwiki(const std::string& inputUrl, size_t referenceTrackIndex, bool applyToAlbum) {
    int albumId = ExtractThwikiId(inputUrl);
    std::string customTitle;
    if (albumId <= 0) {
        std::string cleaned = CleanMetadataString(inputUrl);
        if (!cleaned.empty() && cleaned.find("http") == std::string::npos) {
            customTitle = cleaned;
        }
    }
    if (albumId <= 0 && customTitle.empty()) return;

    std::thread([this, albumId, customTitle, referenceTrackIndex, applyToAlbum]() {
        ThwikiReleaseInfo info;
        bool ok = (albumId > 0) ? FetchThwikiAlbumDetails(albumId, info) : FetchThwikiAlbumDetailsByTitle(customTitle, info);
        if (!ok) return;

        std::vector<unsigned char> coverData;
        if (!info.coverUrl.empty()) {
            coverData = HttpGetBytes(Utf8ToWide(info.coverUrl));
        }

        std::vector<size_t> targetIndices = applyToAlbum ? GetAlbumTrackIndices(referenceTrackIndex) : std::vector<size_t>{ referenceTrackIndex };

        std::lock_guard<std::mutex> lock(m_tagMutex);
        for (size_t idx : targetIndices) {
            if (idx >= m_tagItems.size()) continue;
            auto& it = m_tagItems[idx];
            if (!info.circle.empty()) strncpy_s(it.artistBuf, info.circle.c_str(), sizeof(it.artistBuf) - 1);
            if (!info.title.empty()) strncpy_s(it.albumBuf, info.title.c_str(), sizeof(it.albumBuf) - 1);
            if (!info.releaseDate.empty()) {
                std::string yr = ExtractYearFromString(info.releaseDate);
                strncpy_s(it.yearBuf, yr.c_str(), sizeof(it.yearBuf) - 1);
            }
            if (!info.tracks.empty()) ApplyTrackMatch(it, info.tracks);
            if (!coverData.empty()) {
                it.onlineCoverBytes = coverData;
                it.onlineCoverSource = "THBWiki";
                it.selectedCoverChoice = 1;
            }
            it.matchTier = MatchTier::THBWiki;
            it.isFetchCompleted = true;
        }
        LOG_INFO("[MANUAL THBWIKI MATCHED] Loaded release: " + info.title);
    }).detach();
}

void CoreEngine::FetchManualVocaDb(const std::string& inputUrl, size_t referenceTrackIndex, bool applyToAlbum) {
    int albumId = 0;
    std::regex idRegex(R"((\d{1,8}))");
    std::smatch match;
    if (std::regex_search(inputUrl, match, idRegex)) {
        try { albumId = std::stoi(match.str(1)); } catch (...) {}
    }
    if (albumId <= 0) return;

    std::thread([this, albumId, referenceTrackIndex, applyToAlbum]() {
        VdbReleaseInfo info;
        if (!FetchVdbAlbumDetails("https://vocadb.net", "VocaDB", albumId, info)) return;

        std::vector<unsigned char> coverData;
        if (!info.coverUrl.empty()) {
            coverData = HttpGetBytes(Utf8ToWide(info.coverUrl));
        }

        std::vector<size_t> targetIndices = applyToAlbum ? GetAlbumTrackIndices(referenceTrackIndex) : std::vector<size_t>{ referenceTrackIndex };

        std::lock_guard<std::mutex> lock(m_tagMutex);
        for (size_t idx : targetIndices) {
            if (idx >= m_tagItems.size()) continue;
            auto& it = m_tagItems[idx];
            if (!info.artist.empty()) strncpy_s(it.artistBuf, info.artist.c_str(), sizeof(it.artistBuf) - 1);
            if (!info.title.empty()) strncpy_s(it.albumBuf, info.title.c_str(), sizeof(it.albumBuf) - 1);
            if (!info.releaseDate.empty()) {
                std::string yr = ExtractYearFromString(info.releaseDate);
                strncpy_s(it.yearBuf, yr.c_str(), sizeof(it.yearBuf) - 1);
            }
            if (!info.tracks.empty()) ApplyTrackMatch(it, info.tracks);
            if (!coverData.empty()) {
                it.onlineCoverBytes = coverData;
                it.onlineCoverSource = "VocaDB";
                it.selectedCoverChoice = 1;
            }
            it.matchTier = MatchTier::VocaDB;
            it.isFetchCompleted = true;
        }
        LOG_INFO("[MANUAL VOCADB MATCHED] Loaded release: " + info.title);
    }).detach();
}

void CoreEngine::FetchManualUtaiteDb(const std::string& inputUrl, size_t referenceTrackIndex, bool applyToAlbum) {
    int albumId = 0;
    std::regex idRegex(R"((\d{1,8}))");
    std::smatch match;
    if (std::regex_search(inputUrl, match, idRegex)) {
        try { albumId = std::stoi(match.str(1)); } catch (...) {}
    }
    if (albumId <= 0) return;

    std::thread([this, albumId, referenceTrackIndex, applyToAlbum]() {
        VdbReleaseInfo info;
        if (!FetchVdbAlbumDetails("https://utaitedb.net", "UtaiteDB", albumId, info)) return;

        std::vector<unsigned char> coverData;
        if (!info.coverUrl.empty()) {
            coverData = HttpGetBytes(Utf8ToWide(info.coverUrl));
        }

        std::vector<size_t> targetIndices = applyToAlbum ? GetAlbumTrackIndices(referenceTrackIndex) : std::vector<size_t>{ referenceTrackIndex };

        std::lock_guard<std::mutex> lock(m_tagMutex);
        for (size_t idx : targetIndices) {
            if (idx >= m_tagItems.size()) continue;
            auto& it = m_tagItems[idx];
            if (!info.artist.empty()) strncpy_s(it.artistBuf, info.artist.c_str(), sizeof(it.artistBuf) - 1);
            if (!info.title.empty()) strncpy_s(it.albumBuf, info.title.c_str(), sizeof(it.albumBuf) - 1);
            if (!info.releaseDate.empty()) {
                std::string yr = ExtractYearFromString(info.releaseDate);
                strncpy_s(it.yearBuf, yr.c_str(), sizeof(it.yearBuf) - 1);
            }
            if (!info.tracks.empty()) ApplyTrackMatch(it, info.tracks);
            if (!coverData.empty()) {
                it.onlineCoverBytes = coverData;
                it.onlineCoverSource = "UtaiteDB";
                it.selectedCoverChoice = 1;
            }
            it.matchTier = MatchTier::UtaiteDB;
            it.isFetchCompleted = true;
        }
        LOG_INFO("[MANUAL UTAITEDB MATCHED] Loaded release: " + info.title);
    }).detach();
}

// -------------------------------------------------------------------------------------------------
// Collection Mirroring
// -------------------------------------------------------------------------------------------------

void CoreEngine::StartMirroring() {
    if (m_mirrorProgress.isRunning) return;
    m_mirrorProgress.isRunning = true;
    m_mirrorProgress.totalTasks = 0;
    m_mirrorProgress.completedTasks = 0;
    m_mirrorProgress.createdFolders = 0;
    m_mirrorProgress.copiedFallbacks = 0;

    std::thread([this]() {
        LOG_INFO("[MIRROR] Starting native collection mirroring...");
        fs::path flacRoot = fs::path(m_flacDir);
        fs::path mp3Root = fs::path(m_mp3Dir);
        fs::create_directories(flacRoot);
        fs::create_directories(mp3Root);

        // 1. MP3 Fallbacks -> flac/
        if (fs::exists(mp3Root)) {
            for (auto& entry : fs::recursive_directory_iterator(mp3Root)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".mp3") {
                        fs::path rel = fs::relative(entry.path(), mp3Root);
                        fs::path flacTargetDir = flacRoot / rel.parent_path();
                        std::string stem = entry.path().stem().string();

                        fs::path expectedFlac = flacTargetDir / (stem + ".flac");
                        fs::path mp3FallbackInFlac = flacTargetDir / entry.path().filename();

                        if (!fs::exists(expectedFlac) && !fs::exists(mp3FallbackInFlac)) {
                            fs::create_directories(flacTargetDir);
                            std::error_code ec;
                            fs::copy_file(entry.path(), mp3FallbackInFlac, fs::copy_options::overwrite_existing, ec);
                            m_mirrorProgress.copiedFallbacks++;
                        }
                    }
                }
            }
        }

        // 2. FLAC -> mp3/ 320kbps parallel conversion & cover.jpg mirroring
        std::vector<std::pair<fs::path, fs::path>> conversionTasks;
        if (fs::exists(flacRoot)) {
            for (auto& entry : fs::recursive_directory_iterator(flacRoot)) {
                if (entry.is_directory()) {
                    fs::path rel = fs::relative(entry.path(), flacRoot);
                    fs::path mp3EquivalentDir = mp3Root / rel;
                    if (!fs::exists(mp3EquivalentDir)) {
                        fs::create_directories(mp3EquivalentDir);
                        m_mirrorProgress.createdFolders++;
                    }
                    fs::path flacCover = entry.path() / "cover.jpg";
                    fs::path mp3Cover = mp3EquivalentDir / "cover.jpg";
                    if (fs::exists(flacCover) && !fs::exists(mp3Cover)) {
                        std::error_code ec;
                        fs::copy_file(flacCover, mp3Cover, fs::copy_options::overwrite_existing, ec);
                    }
                } else if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".flac") {
                        fs::path rel = fs::relative(entry.path(), flacRoot);
                        fs::path mp3TargetFile = mp3Root / rel.parent_path() / (entry.path().stem().string() + ".mp3");
                        if (!fs::exists(mp3TargetFile) || fs::file_size(mp3TargetFile) == 0) {
                            fs::create_directories(mp3TargetFile.parent_path());
                            conversionTasks.push_back({ entry.path(), mp3TargetFile });
                        }
                    }
                }
            }
        }

        m_mirrorProgress.totalTasks = conversionTasks.size();

        if (!conversionTasks.empty()) {
            unsigned int numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0) numThreads = 4;
            std::atomic<size_t> taskIdx{0};
            std::vector<std::thread> workers;

            for (unsigned int t = 0; t < numThreads; ++t) {
                workers.emplace_back([&]() {
                    while (true) {
                        size_t idx = taskIdx.fetch_add(1);
                        if (idx >= conversionTasks.size()) break;
                        const auto& task = conversionTasks[idx];
                        if (ConvertFlacToMp3(task.first.string(), task.second.string())) {
                            fs::path coverFile = task.first.parent_path() / "cover.jpg";
                            std::vector<unsigned char> coverBytes;
                            if (fs::exists(coverFile)) {
                                std::ifstream cIn(coverFile, std::ios::binary | std::ios::ate);
                                if (cIn.is_open()) {
                                    std::streamsize cLen = cIn.tellg();
                                    cIn.seekg(0, std::ios::beg);
                                    if (cLen > 0) {
                                        coverBytes.resize((size_t)cLen);
                                        cIn.read((char*)coverBytes.data(), cLen);
                                    }
                                    cIn.close();
                                }
                            }

                            std::string album = task.first.parent_path().filename().string();
                            std::string artist = task.first.parent_path().parent_path().filename().string();
                            std::string filename = task.first.stem().string();
                            std::string trackNo = "";
                            std::string title = filename;
                            size_t dotPos = filename.find(". ");
                            if (dotPos != std::string::npos && dotPos <= 4) {
                                trackNo = filename.substr(0, dotPos);
                                title = filename.substr(dotPos + 2);
                            }

                            WriteMp3TagsAndPicture(task.second.string(), artist, album, title, trackNo, "", "", coverBytes);
                            m_mirrorProgress.completedTasks++;
                        }
                    }
                });
            }
            for (auto& w : workers) {
                if (w.joinable()) w.join();
            }
        }

        m_mirrorProgress.isRunning = false;
        LOG_INFO("[MIRROR] Mirroring complete. Converted " + std::to_string(m_mirrorProgress.completedTasks.load()) + " MP3s.");
    }).detach();
}

void CoreEngine::GetMirrorProgress(size_t& completed, size_t& total, size_t& createdDirs,
                                  size_t& copiedFallbacks, bool& isRunning, std::string& msg) {
    completed = m_mirrorProgress.completedTasks.load();
    total = m_mirrorProgress.totalTasks.load();
    createdDirs = m_mirrorProgress.createdFolders.load();
    copiedFallbacks = m_mirrorProgress.copiedFallbacks.load();
    isRunning = m_mirrorProgress.isRunning.load();
    msg = m_mirrorProgress.lastMessage;
}

// -------------------------------------------------------------------------------------------------
// Database & Tracklist Operations
// -------------------------------------------------------------------------------------------------

void CoreEngine::SyncTracklistDatabase() {
    DatabaseManager::GetInstance().SyncCollectionWithDisk(m_baseDir, m_flacDir, m_mp3Dir, m_toSortDir);
}

bool CoreEngine::ExportDatabase(const std::string& path) {
    std::string exportPath = path.empty() ? (fs::path(m_baseDir) / "tracklist_exported.md").string() : path;
    return DatabaseManager::GetInstance().ExportToCleanTracklistMarkdown(exportPath);
}

std::vector<TrackRecord> CoreEngine::QueryDatabase(int filterStatus, int filterFormat, const std::string& searchQuery) {
    return DatabaseManager::GetInstance().QueryTracks(filterStatus, filterFormat, searchQuery);
}

void CoreEngine::GetDatabaseStats(int& total, int& downloaded, int& missing) {
    total = DatabaseManager::GetInstance().GetTotalTracksCount();
    downloaded = DatabaseManager::GetInstance().GetDownloadedCount();
    missing = DatabaseManager::GetInstance().GetMissingCount();
}

// -------------------------------------------------------------------------------------------------
// Waveform Peak Extraction
// -------------------------------------------------------------------------------------------------

std::vector<float> CoreEngine::GetWaveformPeaks(const std::string& filePath, int peakCount) {
    if (peakCount <= 0) peakCount = 100;
    std::vector<float> peaks(peakCount, 0.2f);

    std::ifstream file(fs::path(Utf8ToWide(filePath)), std::ios::binary | std::ios::ate);
    if (!file.is_open()) return peaks;

    std::streamsize size = file.tellg();
    if (size < 1000) return peaks;

    size_t samplePoints = (size_t)peakCount;
    size_t step = (size_t)size / samplePoints;
    std::vector<char> buffer(256);

    for (size_t i = 0; i < samplePoints; ++i) {
        file.seekg(i * step);
        file.read(buffer.data(), buffer.size());
        std::streamsize bytesRead = file.gcount();
        if (bytesRead > 0) {
            float sum = 0.0f;
            for (std::streamsize b = 0; b < bytesRead; ++b) {
                sum += std::abs((int)(signed char)buffer[b]);
            }
            float avg = sum / (bytesRead * 128.0f);
            peaks[i] = (std::min)(1.0f, (std::max)(0.05f, avg * 1.8f));
        }
    }
    return peaks;
}

// -------------------------------------------------------------------------------------------------
// Global Sidebar Stats
// -------------------------------------------------------------------------------------------------

void CoreEngine::GetSidebarStats(size_t& duplicatesCount, size_t& unresolvedTagsCount,
                                 int& totalTracks, int& downloadedTracks, int& missingTracks) {
    {
        std::lock_guard<std::mutex> lock(m_duplicatesMutex);
        duplicatesCount = m_candidates.size();
    }
    {
        std::lock_guard<std::mutex> lock(m_tagMutex);
        size_t unresolved = 0;
        for (const auto& it : m_tagItems) {
            if (!it.isProcessed) unresolved++;
        }
        unresolvedTagsCount = unresolved;
    }
    GetDatabaseStats(totalTracks, downloadedTracks, missingTracks);
}
