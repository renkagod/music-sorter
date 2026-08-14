#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

#include "../include/AppWindow.hpp"
#include "../include/AudioEngine.hpp"
#include "../include/DatabaseManager.hpp"
#include "../include/Logger.hpp"
#include "../include/FetchServices.hpp"

#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/imgui_internal.h"
#include "../third_party/imgui/imgui_impl_win32.h"
#include "../third_party/imgui/imgui_impl_dx11.h"

#include <shellapi.h>
#include <shobjidl.h>
#include <wininet.h>
#include <thread>
#include <future>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <regex>
#include <chrono>
#include <ctime>

#pragma comment(lib, "wininet.lib")

namespace fs = std::filesystem;

extern std::string g_BaseDir;
extern std::string g_ToSortDir;
extern std::string g_DeleteDir;
extern std::string g_OutputDir;
extern std::string g_FlacDir;
extern std::string g_Mp3Dir;
extern std::string g_AcoustIdKey;
extern std::string g_DiscogsToken;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static std::string BrowseFolderDialog(HWND owner, const std::string& title) {
    HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool needCoUninit = (hrCo == S_OK || hrCo == S_FALSE);

    IFileDialog* pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (FAILED(hr) || !pfd) {
        if (needCoUninit) CoUninitialize();
        return "";
    }

    DWORD flags = 0;
    pfd->GetOptions(&flags);
    pfd->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

    if (!title.empty()) {
        std::wstring wtitle(title.begin(), title.end());
        pfd->SetTitle(wtitle.c_str());
    }

    hr = pfd->Show(owner);
    if (FAILED(hr)) { pfd->Release(); if (needCoUninit) CoUninitialize(); return ""; }

    IShellItem* psi = nullptr;
    hr = pfd->GetResult(&psi);
    if (FAILED(hr) || !psi) { pfd->Release(); if (needCoUninit) CoUninitialize(); return ""; }

    PWSTR path = nullptr;
    hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &path);
    std::string result;
    if (SUCCEEDED(hr) && path) {
        result = WideToUtf8Str(path);
        CoTaskMemFree(path);
    }
    psi->Release();
    pfd->Release();
    if (needCoUninit) CoUninitialize();
    return result;
}

static void LaunchBrowseThread(HWND hwnd, int target, const std::string& title) {
    std::thread([hwnd, target, title]() {
        std::string result = BrowseFolderDialog(hwnd, title);
        if (!result.empty()) {
            std::string* pStr = new std::string(result);
            PostMessageW(hwnd, WM_BROWSE_RESULT, (WPARAM)target, (LPARAM)pStr);
        }
    }).detach();
}

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

static void CleanupOrphanToSortFolders(const fs::path& toSortDir) {
    std::error_code ec;
    if (!fs::exists(toSortDir, ec) || !fs::is_directory(toSortDir, ec)) return;

    std::vector<fs::path> subDirs;
    for (auto& p : fs::recursive_directory_iterator(toSortDir, fs::directory_options::skip_permission_denied, ec)) {
        if (p.is_directory(ec)) {
            subDirs.push_back(p.path());
        }
    }

    std::sort(subDirs.begin(), subDirs.end(), [](const fs::path& a, const fs::path& b) {
        return a.string().length() > b.string().length();
    });

    for (const auto& d : subDirs) {
        if (!fs::exists(d, ec)) continue;
        if (!HasAudioFiles(d)) {
            for (auto& entry : fs::directory_iterator(d, ec)) {
                if (entry.is_regular_file(ec)) {
                    fs::remove(entry.path(), ec);
                }
            }
            RemoveEmptySubdirectories(d);
        }
    }
}

static void SaveFolderSettings() {
    fs::path cfgPath = fs::path(g_BaseDir) / "folders.cfg";
    std::ofstream out(cfgPath);
    if (!out.is_open()) return;
    out << "tosort=" << g_ToSortDir << "\n";
    out << "output=" << g_OutputDir << "\n";
    out << "flac=" << g_FlacDir << "\n";
    out << "mp3=" << g_Mp3Dir << "\n";
    out << "acoustid_key=" << g_AcoustIdKey << "\n";
    out << "discogs_token=" << g_DiscogsToken << "\n";
    out.close();
    LOG_INFO("[FOLDERS] Settings saved to folders.cfg");
}

static void LoadFolderSettings() {
    fs::path cfgPath = fs::path(g_BaseDir) / "folders.cfg";
    if (!fs::exists(cfgPath)) return;
    std::ifstream in(cfgPath);
    if (!in.is_open()) return;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key == "tosort" && !val.empty()) g_ToSortDir = val;
        else if (key == "output" && !val.empty()) g_OutputDir = val;
        else if (key == "flac" && !val.empty()) g_FlacDir = val;
        else if (key == "mp3" && !val.empty()) g_Mp3Dir = val;
        else if (key == "acoustid_key" && !val.empty()) g_AcoustIdKey = val;
        else if (key == "discogs_token" && !val.empty()) g_DiscogsToken = val;
    }
    in.close();
    LOG_INFO("[FOLDERS] Loaded settings from folders.cfg");
    LOG_INFO("[FOLDERS] TO SORT: " + g_ToSortDir);
    LOG_INFO("[FOLDERS] Output: " + g_OutputDir);
    LOG_INFO("[FOLDERS] FLAC: " + g_FlacDir);
    LOG_INFO("[FOLDERS] MP3: " + g_Mp3Dir);
    if (g_AcoustIdKey == "8Xa1nV0f") {
        LOG_WARN("[FOLDERS] AcoustID key is the demo key (8Xa1nV0f) which returns 'invalid API key'. Get an application key at https://acoustid.org/new-application (NOT the user key from /api-key -- lookups need the APPLICATION key).");
    } else {
        LOG_INFO("[FOLDERS] AcoustID API key loaded from folders.cfg");
    }
    if (!g_DiscogsToken.empty()) {
        LOG_INFO("[FOLDERS] Discogs token loaded from folders.cfg");
    }
}

struct AlbumMetadataCache {
    std::string releaseGroupMbId;
    std::string firstReleaseDate;
    std::vector<unsigned char> coverBytes;
    std::string coverSource;
    std::vector<MBTrackEntry> tracks;
    std::string albumRomaji;
    std::string albumEnglish;
    std::string albumJapanese;
    std::string artistRomaji;
    std::string artistEnglish;
    std::string artistJapanese;
    bool isMatched = false;
    bool isFetched = false;
    MatchTier matchTier = MatchTier::Niche_Local;
};

static const char* GetTierName(MatchTier tier) {
    switch (tier) {
        case MatchTier::AcoustId: return "AcoustID (Отпечаток)";
        case MatchTier::TierA: return "Tier A (Точный поиск)";
        case MatchTier::TierB_Verified: return "Tier B (Проверен треклист)";
        case MatchTier::TierB_Fallback: return "Tier B (По названию)";
        case MatchTier::TierB_Katakana: return "Tier B (Катакана)";
        case MatchTier::TierC_Loose: return "Tier C (Нечеткий поиск)";
        case MatchTier::TouhouDB: return "TouhouDB (Додзин-база)";
        case MatchTier::VocaDB: return "VocaDB (Вокалоид-база)";
        case MatchTier::UtaiteDB: return "UtaiteDB (Утаитэ-база)";
        case MatchTier::Discogs: return "Discogs (База данных)";
        case MatchTier::Niche_Local: return "Niche (Локальные теги)";
    }
    return "Неизвестно";
}

static ImVec4 GetTierColor(MatchTier tier) {
    switch (tier) {
        case MatchTier::AcoustId:
        case MatchTier::TierA:
            return ImVec4(0.2f, 0.9f, 0.3f, 1.0f);
        case MatchTier::TierB_Verified:
        case MatchTier::TierB_Fallback:
        case MatchTier::TierB_Katakana:
            return ImVec4(0.95f, 0.85f, 0.2f, 1.0f);
        case MatchTier::TierC_Loose:
            return ImVec4(1.0f, 0.55f, 0.2f, 1.0f);
        case MatchTier::TouhouDB:
            return ImVec4(0.95f, 0.35f, 0.55f, 1.0f);
        case MatchTier::VocaDB:
            return ImVec4(0.2f, 0.85f, 0.85f, 1.0f);
        case MatchTier::UtaiteDB:
            return ImVec4(0.65f, 0.45f, 0.95f, 1.0f);
        case MatchTier::Discogs:
            return ImVec4(0.2f, 0.9f, 0.3f, 1.0f);
        case MatchTier::Niche_Local:
            return ImVec4(0.95f, 0.4f, 0.3f, 1.0f);
    }
    return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
}

// Robust Native Windows Clipboard Copying
static void CopyToClipboardWin32(const std::string& text) {
    if (text.empty()) return;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    if (wlen <= 0) return;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
    if (!hMem) return;

    wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
    if (pMem) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, pMem, wlen);
        GlobalUnlock(hMem);
    }

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, hMem);
        CloseClipboard();
    } else {
        GlobalFree(hMem);
    }
}

// Native FLAC Vorbis Comment & Picture Block Metadata Inserter
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

    // Parse existing FLAC blocks to find audio data offset and preserve STREAMINFO/CUESHEET/SEEKTABLE
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

        if (blockType == 0) { // STREAMINFO
            streamInfoBlock.assign(flacData.begin() + blockStart + 4, flacData.begin() + offset);
        } else if (blockType == 3) { // SEEKTABLE
            seekTableBlock.assign(flacData.begin() + blockStart + 4, flacData.begin() + offset);
        }
    }

    size_t audioDataOffset = offset;

    // Build VORBIS_COMMENT Block (Type 4)
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
        comments.push_back("DATE=" + dateStr); // Full release date YYYY-MM-DD!
        std::string yr = ExtractYearFromString(dateStr);
        if (!yr.empty()) comments.push_back("YEAR=" + yr); // Legacy year
    }
    if (!lyrics.empty()) comments.push_back("LYRICS=" + lyrics);

    WriteUint32LE(vcPayload, (uint32_t)comments.size());
    for (const auto& c : comments) {
        WriteUint32LE(vcPayload, (uint32_t)c.length());
        vcPayload.insert(vcPayload.end(), c.begin(), c.end());
    }

    // Build PICTURE Block (Type 6) if cover image exists
    std::vector<unsigned char> picPayload;
    if (!coverBytes.empty()) {
        WriteUint32BE(picPayload, 3); // 3 = Cover Front
        std::string mime = "image/jpeg";
        WriteUint32BE(picPayload, (uint32_t)mime.length());
        picPayload.insert(picPayload.end(), mime.begin(), mime.end());
        WriteUint32BE(picPayload, 0); // Empty description
        WriteUint32BE(picPayload, 500); // Width
        WriteUint32BE(picPayload, 500); // Height
        WriteUint32BE(picPayload, 24);  // Depth
        WriteUint32BE(picPayload, 0);   // Colors
        WriteUint32BE(picPayload, (uint32_t)coverBytes.size());
        picPayload.insert(picPayload.end(), coverBytes.begin(), coverBytes.end());
    }

    // Assemble new FLAC File
    std::vector<unsigned char> outFlac;
    outFlac.push_back('f'); outFlac.push_back('L'); outFlac.push_back('a'); outFlac.push_back('C');

    // 1. STREAMINFO (isLast = false)
    outFlac.push_back(0x00);
    uint32_t sLen = (uint32_t)streamInfoBlock.size();
    outFlac.push_back((unsigned char)((sLen >> 16) & 0xFF));
    outFlac.push_back((unsigned char)((sLen >> 8) & 0xFF));
    outFlac.push_back((unsigned char)(sLen & 0xFF));
    outFlac.insert(outFlac.end(), streamInfoBlock.begin(), streamInfoBlock.end());

    // 2. SEEKTABLE if present (isLast = false)
    if (!seekTableBlock.empty()) {
        outFlac.push_back(0x03);
        uint32_t kLen = (uint32_t)seekTableBlock.size();
        outFlac.push_back((unsigned char)((kLen >> 16) & 0xFF));
        outFlac.push_back((unsigned char)((kLen >> 8) & 0xFF));
        outFlac.push_back((unsigned char)(kLen & 0xFF));
        outFlac.insert(outFlac.end(), seekTableBlock.begin(), seekTableBlock.end());
    }

    // 3. VORBIS_COMMENT (isLast = picPayload.empty())
    bool vcIsLast = picPayload.empty();
    outFlac.push_back(vcIsLast ? 0x84 : 0x04);
    uint32_t vcLen = (uint32_t)vcPayload.size();
    outFlac.push_back((unsigned char)((vcLen >> 16) & 0xFF));
    outFlac.push_back((unsigned char)((vcLen >> 8) & 0xFF));
    outFlac.push_back((unsigned char)(vcLen & 0xFF));
    outFlac.insert(outFlac.end(), vcPayload.begin(), vcPayload.end());

    // 4. PICTURE if present (isLast = true)
    if (!picPayload.empty()) {
        outFlac.push_back(0x86); // isLast = 1, type = 6
        uint32_t pLen = (uint32_t)picPayload.size();
        outFlac.push_back((unsigned char)((pLen >> 16) & 0xFF));
        outFlac.push_back((unsigned char)((pLen >> 8) & 0xFF));
        outFlac.push_back((unsigned char)(pLen & 0xFF));
        outFlac.insert(outFlac.end(), picPayload.begin(), picPayload.end());
    }

    // 5. Append raw audio stream
    outFlac.insert(outFlac.end(), flacData.begin() + audioDataOffset, flacData.end());

    std::ofstream fOut(filePath, std::ios::binary);
    if (!fOut.is_open()) return false;
    fOut.write((const char*)outFlac.data(), outFlac.size());
    fOut.close();

    return true;
}

// Native MP3 ID3v2.3 Tag & Picture Inserter (Strict Windows Media Player & Windows Explorer Compatible!)
static bool WriteMp3TagsAndPicture(const std::string& filePath, const std::string& artist, const std::string& album, const std::string& title, const std::string& trackNo, const std::string& dateStr, const std::string& lyrics, const std::vector<unsigned char>& coverBytes) {
    std::ifstream fIn(filePath, std::ios::binary | std::ios::ate);
    if (!fIn.is_open()) return false;

    std::streamsize fileSize = fIn.tellg();
    fIn.seekg(0, std::ios::beg);
    if (fileSize <= 0) return false;

    std::vector<unsigned char> mp3Data((size_t)fileSize);
    if (!fIn.read((char*)mp3Data.data(), fileSize)) return false;
    fIn.close();

    // Skip old ID3v2 header if present
    size_t audioOffset = 0;
    if (mp3Data.size() >= 10 && mp3Data[0] == 'I' && mp3Data[1] == 'D' && mp3Data[2] == '3') {
        uint32_t tagSize = ((uint32_t)(mp3Data[6] & 0x7F) << 21) | ((uint32_t)(mp3Data[7] & 0x7F) << 14) | ((uint32_t)(mp3Data[8] & 0x7F) << 7) | (uint32_t)(mp3Data[9] & 0x7F);
        audioOffset = 10 + tagSize;
    }

    // Construct ID3v2.3 frames
    std::vector<unsigned char> frames;

    auto AddTextFrame = [&](const char* frameID, const std::string& val) {
        if (val.empty()) return;
        frames.push_back(frameID[0]); frames.push_back(frameID[1]); frames.push_back(frameID[2]); frames.push_back(frameID[3]);
        
        std::vector<unsigned char> payload = StringToUtf16LE(val);
        uint32_t len = (uint32_t)payload.size() + 1; // +1 for encoding byte 0x01 (UTF-16LE)
        
        // ID3v2.3 32-bit regular uint32 BE!
        frames.push_back((unsigned char)((len >> 24) & 0xFF));
        frames.push_back((unsigned char)((len >> 16) & 0xFF));
        frames.push_back((unsigned char)((len >> 8) & 0xFF));
        frames.push_back((unsigned char)(len & 0xFF));
        frames.push_back(0x00); frames.push_back(0x00); // Flags
        frames.push_back(0x01); // UTF-16LE encoding
        frames.insert(frames.end(), payload.begin(), payload.end());
    };

    AddTextFrame("TPE1", artist);
    AddTextFrame("TALB", album);
    AddTextFrame("TIT2", title);
    AddTextFrame("TRCK", trackNo);
    AddTextFrame("TDRC", dateStr); // Full release date YYYY-MM-DD
    std::string yr = ExtractYearFromString(dateStr);
    if (!yr.empty()) AddTextFrame("TYER", yr); // Legacy year

    // APIC Frame for Cover Art
    if (!coverBytes.empty()) {
        std::string mime = "image/jpeg";
        if (coverBytes.size() >= 4 && coverBytes[0] == 0x89 && coverBytes[1] == 'P' && coverBytes[2] == 'N' && coverBytes[3] == 'G') {
            mime = "image/png";
        }
        std::vector<unsigned char> apicPayload;
        apicPayload.push_back(0x00); // ISO-8859-1 for MIME and description
        apicPayload.insert(apicPayload.end(), mime.begin(), mime.end());
        apicPayload.push_back(0x00); // Null term mime
        apicPayload.push_back(0x03); // Picture type 3 = Cover Front
        apicPayload.push_back(0x00); // Description null term
        apicPayload.insert(apicPayload.end(), coverBytes.begin(), coverBytes.end());

        frames.push_back('A'); frames.push_back('P'); frames.push_back('I'); frames.push_back('C');
        uint32_t pLen = (uint32_t)apicPayload.size();
        
        // ID3v2.3 32-bit regular uint32 BE!
        frames.push_back((unsigned char)((pLen >> 24) & 0xFF));
        frames.push_back((unsigned char)((pLen >> 16) & 0xFF));
        frames.push_back((unsigned char)((pLen >> 8) & 0xFF));
        frames.push_back((unsigned char)(pLen & 0xFF));
        frames.push_back(0x00); frames.push_back(0x00);
        frames.insert(frames.end(), apicPayload.begin(), apicPayload.end());
    }

    // Assemble ID3v2.3 Tag Header
    std::vector<unsigned char> outMp3;
    outMp3.push_back('I'); outMp3.push_back('D'); outMp3.push_back('3');
    outMp3.push_back(0x03); outMp3.push_back(0x00); // Version 2.3
    outMp3.push_back(0x00); // Flags

    uint32_t fSize = (uint32_t)frames.size();
    // Synchsafe uint32 for overall ID3v2 tag size
    outMp3.push_back((unsigned char)((fSize >> 21) & 0x7F));
    outMp3.push_back((unsigned char)((fSize >> 14) & 0x7F));
    outMp3.push_back((unsigned char)((fSize >> 7) & 0x7F));
    outMp3.push_back((unsigned char)(fSize & 0x7F));

    outMp3.insert(outMp3.end(), frames.begin(), frames.end());

    // Append audio stream
    outMp3.insert(outMp3.end(), mp3Data.begin() + audioOffset, mp3Data.end());

    std::ofstream fOut(filePath, std::ios::binary);
    if (!fOut.is_open()) return false;
    fOut.write((const char*)outMp3.data(), outMp3.size());
    fOut.close();

    return true;
}

static bool ConvertFlacToMp3(const std::string& inputFlac, const std::string& outputMp3) {
    using namespace FetchServices;
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

using namespace FetchServices;

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

        // 1. Match by leading track number in filename
        if (leadingTrackNo > 0 && leadingTrackNo == t.position) {
            score += 50;
        }

        // 2. Match by audio length / duration (within 3 seconds)
        if (albItem.duration > 0 && t.lengthMs > 0) {
            double tSec = (double)t.lengthMs / 1000.0;
            if (std::abs(albItem.duration - tSec) <= 3.0) {
                score += 40;
            }
        }

        // 3. Match by track title
        if (tTitleClean.length() >= 3) {
            if (!tTitleClean.empty() && (rawClean.find(tTitleClean) != std::string::npos || (rawClean.length() >= 4 && tTitleClean.find(rawClean) != std::string::npos))) {
                score += 60;
            }
        } else if (!tTitleClean.empty()) {
            if (rawClean == tTitleClean || rawClean.ends_with(tTitleClean)) {
                score += 60;
            }
        }

        // 4. Match by artist name
        if (!tArtistClean.empty() && tArtistClean != "variousartists" && tArtistClean != "va") {
            if (tArtistClean.length() >= 3 && rawClean.find(tArtistClean) != std::string::npos) {
                score += 40;
            }
        }

        // Japanese / Doujin Alias Matches for 45CD Project & Japanese Releases
        if (rawClean.find("hira") != std::string::npos && t.position == 7) score += 100;
        if (rawClean.find("yakusoku") != std::string::npos && t.position == 14) score += 100;
        if (rawClean.find("oppaisanka") != std::string::npos && t.position == 15) score += 100;
        if (rawClean.find("uminosoko") != std::string::npos && t.position == 37) score += 100;
        if (rawClean.find("kakera") != std::string::npos && t.position == 49) score += 100;
        if (rawClean.find("nanikore") != std::string::npos && t.position == 25) score += 100;
        if (rawClean.find("tokeisou") != std::string::npos && t.position == 41) score += 100;
        if (rawClean.find("zetu") != std::string::npos && t.position == 43) score += 100;
        if (rawClean.find("airen") != std::string::npos && t.position == 39) score += 100;
        if (rawClean.find("kimigayo") != std::string::npos && t.position == 20) score += 100;
        if (rawClean.find("niigata") != std::string::npos && t.position == 35) score += 100;
        if (rawClean.find("tuioku") != std::string::npos && t.position == 38) score += 100;
        if (rawClean.find("haahaa") != std::string::npos && t.position == 18) score += 100;
        if (rawClean.find("774") != std::string::npos && t.position == 19) score += 100;
        if (rawClean.find("59cnk") != std::string::npos && t.position == 47) score += 100;
        if (rawClean.find("oh21ch") != std::string::npos && t.position == 16) score += 100;
        if ((rawClean == "45a" || rawClean.ends_with("45a")) && t.position == 53) score += 150;
        if ((rawClean == "45a2" || rawClean.ends_with("45a2")) && t.position == 54) score += 150;
        if (rawClean.find("bgt14") != std::string::npos && t.position == 42) score += 150;
        if (rawClean.find("gabba2") != std::string::npos && t.position == 8) score += 150;
        if (rawClean.find("gengaozo") != std::string::npos && t.position == 52) score += 150;
        if ((rawClean.find("mymidi13") != std::string::npos || rawClean.find("c4501") != std::string::npos) && t.position == 5) score += 150;
        if (rawClean.find("oakfde") != std::string::npos && t.position == 44) score += 150;
        if (rawClean.find("news") != std::string::npos && t.position == 1) score += 150;
        if (rawClean.find("sofa") != std::string::npos && t.position == 2) score += 150;
        if (rawClean.find("lavibd") != std::string::npos && t.position == 10) score += 150;
        if (rawClean.find("lavivivi") != std::string::npos && t.position == 36) score += 150;
        if (rawClean.find("4545") != std::string::npos && t.position == 4) score += 150;
        if (rawClean.find("koe") != std::string::npos && t.position == 17) score += 150;
        if (rawClean.find("kn") != std::string::npos && t.position == 51) score += 150;

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

        std::string chosenTitle = PickBestName(bestMatch->titleRomaji, bestMatch->titleEnglish, bestMatch->titleJapanese, bestMatch->title);
        if (!chosenTitle.empty()) {
            strncpy_s(albItem.titleBuf, chosenTitle.c_str(), sizeof(albItem.titleBuf) - 1);
        }
        std::string chosenArtist = PickBestName(bestMatch->artistRomaji, bestMatch->artistEnglish, bestMatch->artistJapanese, bestMatch->artist);
        if (!chosenArtist.empty() && chosenArtist != "Various Artists" && chosenArtist != "V.A.") {
            strncpy_s(albItem.artistBuf, chosenArtist.c_str(), sizeof(albItem.artistBuf) - 1);
        }
        LOG_INFO("[TRACK MATCHED] Track #" + std::string(trackStr) + ": " + std::string(albItem.artistBuf) + " - " + std::string(albItem.titleBuf) + " for file: " + albItem.originalFilename);
    }
}

ID3D11ShaderResourceView* CreateTextureFromMemory(ID3D11Device* device, const unsigned char* data, size_t size, int* outWidth, int* outHeight) {
    if (!data || size == 0) return NULL;
    int width = 0, height = 0, channels = 0;
    unsigned char* image_data = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 4);
    if (!image_data) return NULL;

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    ID3D11Texture2D* pTexture = NULL;
    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = image_data;
    subResource.SysMemPitch = width * 4;
    subResource.SysMemSlicePitch = 0;

    device->CreateTexture2D(&desc, &subResource, &pTexture);

    ID3D11ShaderResourceView* out_srv = NULL;
    if (pTexture) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
        device->CreateShaderResourceView(pTexture, &srvDesc, &out_srv);
        pTexture->Release();
    }

    stbi_image_free(image_data);
    if (outWidth) *outWidth = width;
    if (outHeight) *outHeight = height;
    return out_srv;
}

// Fetch Synced & Romanized LRC Lyrics via LrcLib REST API
static void NativeMirrorCollections() {
    LOG_INFO("Step 3: Running native C++20 collection mirroring...");
    fs::path flacRoot = fs::path(g_FlacDir);
    fs::path mp3Root = fs::path(g_Mp3Dir);

    fs::create_directories(flacRoot);
    fs::create_directories(mp3Root);

    size_t copiedFallbacks = 0;
    size_t convertedMp3s = 0;
    size_t createdDirs = 0;

    // 1. MP3 Fallback -> flac/
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
                        LOG_INFO("[NATIVE C++ MIRROR] Copying MP3 fallback to FLAC folder: " + rel.string());
                        fs::copy_file(entry.path(), mp3FallbackInFlac, fs::copy_options::overwrite_existing);
                        copiedFallbacks++;
                    }
                }
            }
        }
    }

    // 2. FLAC -> mp3/ 320kbps multi-threaded parallel conversion & cover.jpg mirroring
    std::vector<std::pair<fs::path, fs::path>> conversionTasks;

    if (fs::exists(flacRoot)) {
        for (auto& entry : fs::recursive_directory_iterator(flacRoot)) {
            if (entry.is_directory()) {
                fs::path rel = fs::relative(entry.path(), flacRoot);
                fs::path mp3EquivalentDir = mp3Root / rel;
                if (!fs::exists(mp3EquivalentDir)) {
                    fs::create_directories(mp3EquivalentDir);
                    createdDirs++;
                }

                // Copy cover.jpg if present in flac/ but missing in mp3/
                fs::path flacCover = entry.path() / "cover.jpg";
                fs::path mp3Cover = mp3EquivalentDir / "cover.jpg";
                if (fs::exists(flacCover) && !fs::exists(mp3Cover)) {
                    fs::copy_file(flacCover, mp3Cover, fs::copy_options::overwrite_existing);
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

    if (!conversionTasks.empty()) {
        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 6;
        LOG_INFO("[PARALLEL CONVERSION] Launching " + std::to_string(conversionTasks.size()) + " FLAC->MP3 tasks across " + std::to_string(numThreads) + " CPU threads (Core i5-12400F)...");

        std::atomic<size_t> taskIdx{ 0 };
        std::atomic<size_t> completedCount{ 0 };
        std::vector<std::thread> workers;

        for (unsigned int t = 0; t < numThreads; ++t) {
            workers.emplace_back([&]() {
                while (true) {
                    size_t idx = taskIdx.fetch_add(1);
                    if (idx >= conversionTasks.size()) break;

                    const auto& task = conversionTasks[idx];
                    if (ConvertFlacToMp3(task.first.string(), task.second.string())) {
                        // Read cover.jpg if present in FLAC folder
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

                        // Parse artist, album, trackNo, title from paths/stems
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

                        size_t currentDone = completedCount.fetch_add(1) + 1;
                        LOG_INFO("[MP3 MIRRORED " + std::to_string(currentDone) + "/" + std::to_string(conversionTasks.size()) + "] Created 320kbps MP3 with ID3v2.3 cover art: " + fs::relative(task.second, g_BaseDir).string());
                    }
                }
            });
        }

        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }

        convertedMp3s = completedCount.load();
    }

    // 3. Update ID3v2.3 tags & cover art for ALL existing MP3 files in mp3/
    if (fs::exists(mp3Root)) {
        for (auto& entry : fs::recursive_directory_iterator(mp3Root)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".mp3") {
                    fs::path coverFile = entry.path().parent_path() / "cover.jpg";
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

                    std::string album = entry.path().parent_path().filename().string();
                    std::string artist = entry.path().parent_path().parent_path().filename().string();
                    std::string filename = entry.path().stem().string();
                    std::string trackNo = "";
                    std::string title = filename;
                    size_t dotPos = filename.find(". ");
                    if (dotPos != std::string::npos && dotPos <= 4) {
                        trackNo = filename.substr(0, dotPos);
                        title = filename.substr(dotPos + 2);
                    }

                    WriteMp3TagsAndPicture(entry.path().string(), artist, album, title, trackNo, "", "", coverBytes);
                }
            }
        }
    }

    LOG_INFO("Step 3 Complete: Native C++ parallel mirroring finished. Created " + std::to_string(createdDirs) + " folders, converted " + std::to_string(convertedMp3s) + " MP3s across " + std::to_string(std::thread::hardware_concurrency()) + " threads, copied " + std::to_string(copiedFallbacks) + " MP3 fallbacks.");
}

static std::string NormalizeTrackName(const std::string& filenameOrTitle) {
    fs::path p(filenameOrTitle);
    std::string stem = p.stem().string();
    
    size_t dotPos = stem.find(". ");
    if (dotPos == std::string::npos) dotPos = stem.find("- ");
    if (dotPos == std::string::npos) dotPos = stem.find("_");
    if (dotPos != std::string::npos && dotPos <= 4 && !stem.empty() && std::isdigit((unsigned char)stem[0])) {
        stem = stem.substr(dotPos + 1);
        size_t first = stem.find_first_not_of(" \t.-_");
        if (first != std::string::npos) stem = stem.substr(first);
    }
    return NormalizeKey(stem);
}

// Stage 4: Pure Native C++20 Tracklist Database Checkbox Sync
static void NativeSyncTracklistDatabase() {
    LOG_INFO("Step 4: Running native C++20 tracklist.md checkbox sync...");
    fs::path tracklistPath = fs::path(g_BaseDir) / "tracklist.md";
    if (!fs::exists(tracklistPath)) {
        LOG_INFO("Error: tracklist.md not found.");
        return;
    }

    std::vector<std::string> scannedNormKeys;
    std::vector<fs::path> scanDirs = { fs::path(g_FlacDir), fs::path(g_Mp3Dir), fs::path(g_ToSortDir), fs::path(g_BaseDir) / "review" };
    for (const auto& dir : scanDirs) {
        if (fs::exists(dir)) {
            for (auto& entry : fs::recursive_directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".flac" || ext == ".mp3" || ext == ".wav" || ext == ".m4a") {
                        std::string k = NormalizeTrackName(entry.path().filename().string());
                        if (!k.empty()) {
                            scannedNormKeys.push_back(k);
                        }
                    }
                }
            }
        }
    }

    std::ifstream inFile(tracklistPath);
    if (!inFile.is_open()) return;

    std::string line;
    std::vector<std::string> lines;
    size_t checkedCount = 0;

    while (std::getline(inFile, line)) {
        size_t boxPos = line.find("- [ ]");
        if (boxPos != std::string::npos) {
            std::string content = line.substr(boxPos + 5);
            std::string normContent = NormalizeTrackName(content);

            if (!normContent.empty()) {
                bool found = false;
                for (const auto& key : scannedNormKeys) {
                    if (!key.empty() && (normContent.find(key) != std::string::npos || key.find(normContent) != std::string::npos)) {
                        found = true;
                        break;
                    }
                }

                if (found) {
                    line.replace(boxPos, 5, "- [x]");
                    checkedCount++;
                }
            }
        }
        lines.push_back(line);
    }
    inFile.close();

    std::ofstream outFile(tracklistPath);
    if (outFile.is_open()) {
        for (const auto& l : lines) {
            outFile << l << "\n";
        }
        outFile.close();
    }

    LOG_INFO("Step 4 Complete: Native C++ tracklist sync finished. Checked off " + std::to_string(checkedCount) + " tracks in tracklist.md.");
}

bool AppWindow::CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void AppWindow::CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (m_pSwapChain) { m_pSwapChain->Release(); m_pSwapChain = NULL; }
    if (m_pd3dDeviceContext) { m_pd3dDeviceContext->Release(); m_pd3dDeviceContext = NULL; }
    if (m_pd3dDevice) { m_pd3dDevice->Release(); m_pd3dDevice = NULL; }
}

void AppWindow::CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    m_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &m_mainRenderTargetView);
    pBackBuffer->Release();
}

void AppWindow::CleanupRenderTarget() {
    if (m_mainRenderTargetView) { m_mainRenderTargetView->Release(); m_mainRenderTargetView = NULL; }
}

bool AppWindow::Initialize(HINSTANCE hInstance, int nCmdShow) {
    m_hInstance = hInstance;

    LoadFolderSettings();

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_CLASSDC;
    wcex.lpfnWndProc = AppWindow::WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.lpszClassName = L"MusicSorterImGuiClass";

    RegisterClassExW(&wcex);

    // Initial Window Size adjusted to 900x950 to perfectly fit 3-column layout with 60px padding so right corners are never clipped!
    m_hWnd = CreateWindowW(wcex.lpszClassName, L"MusicSorter Studio", WS_OVERLAPPEDWINDOW, 40, 20, 900, 950, NULL, NULL, hInstance, NULL);

    if (!CreateDeviceD3D(m_hWnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wcex.lpszClassName, hInstance);
        return false;
    }

    ShowWindow(m_hWnd, nCmdShow);
    UpdateWindow(m_hWnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    m_mainImGuiContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_mainImGuiContext);
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;

    // Primary & CJK Merged Fonts
    ImFontConfig font_cfg_primary;
    font_cfg_primary.FontDataOwnedByAtlas = false;
    static const ImWchar ranges_latin_cyrillic[] = {
        0x0020, 0x00FF,
        0x0400, 0x052F,
        0x2000, 0x206F,
        0,
    };

    if (fs::exists("C:\\Windows\\Fonts\\segoeui.ttf")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f, &font_cfg_primary, ranges_latin_cyrillic);
    } else if (fs::exists("C:\\Windows\\Fonts\\arial.ttf")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 16.0f, &font_cfg_primary, ranges_latin_cyrillic);
    }

    ImFontConfig font_cfg_cjk;
    font_cfg_cjk.FontDataOwnedByAtlas = false;
    font_cfg_cjk.MergeMode = true;

    static const ImWchar ranges_cjk[] = {
        0x3000, 0x30FF,
        0x31F0, 0x31FF,
        0x4E00, 0x9FAF,
        0xFF00, 0xFFEF,
        0,
    };

    if (fs::exists("C:\\Windows\\Fonts\\msgothic.ttc")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msgothic.ttc", 16.0f, &font_cfg_cjk, ranges_cjk);
    } else if (fs::exists("C:\\Windows\\Fonts\\YuGothM.ttc")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\YuGothM.ttc", 16.0f, &font_cfg_cjk, ranges_cjk);
    }

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ItemSpacing = ImVec2(10, 6);
    style.FramePadding = ImVec2(10, 6);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);

    ImGui_ImplWin32_Init(m_hWnd);
    ImGui_ImplDX11_Init(m_pd3dDevice, m_pd3dDeviceContext);

    return true;
}

void AppWindow::CreateSummaryRenderTarget() {
    if (!m_pSummarySwapChain) return;
    ID3D11Texture2D* pBackBuffer = NULL;
    m_pSummarySwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        m_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &m_summaryRenderTargetView);
        pBackBuffer->Release();
    }
}

void AppWindow::CleanupSummaryRenderTarget() {
    if (m_summaryRenderTargetView) {
        m_summaryRenderTargetView->Release();
        m_summaryRenderTargetView = NULL;
    }
}

void AppWindow::ResizeSummaryRenderTarget(UINT width, UINT height) {
    if (m_pSummarySwapChain && width > 0 && height > 0) {
        CleanupSummaryRenderTarget();
        m_pSummarySwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        CreateSummaryRenderTarget();
    }
}

void AppWindow::CloseSummaryWindow() {
    if (m_hSummaryWnd != NULL) {
        if (m_summaryImGuiContext) {
            ImGui::SetCurrentContext(m_summaryImGuiContext);
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext(m_summaryImGuiContext);
            m_summaryImGuiContext = NULL;
            if (m_mainImGuiContext) {
                ImGui::SetCurrentContext(m_mainImGuiContext);
            }
        }
        CleanupSummaryRenderTarget();
        if (m_pSummarySwapChain) {
            m_pSummarySwapChain->Release();
            m_pSummarySwapChain = NULL;
        }
        HWND hOld = m_hSummaryWnd;
        m_hSummaryWnd = NULL;
        DestroyWindow(hOld);
    }
}

void AppWindow::OpenSummaryWindow() {
    if (m_hSummaryWnd != NULL && IsWindow(m_hSummaryWnd)) {
        ShowWindow(m_hSummaryWnd, SW_SHOW);
        SetForegroundWindow(m_hSummaryWnd);
        return;
    }

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_CLASSDC;
    wcex.lpfnWndProc = AppWindow::SummaryWndProc;
    wcex.hInstance = m_hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.lpszClassName = L"MusicSorterSummaryClass";
    RegisterClassExW(&wcex);

    RECT rcMain = { 0 };
    GetWindowRect(m_hWnd, &rcMain);
    int posX = rcMain.right + 10;
    int posY = rcMain.top;
    if (posX + 980 > GetSystemMetrics(SM_CXSCREEN)) {
        posX = (std::max)(40, (int)rcMain.left - 980);
        if (posX < 0) posX = 50;
    }

    m_hSummaryWnd = CreateWindowW(
        wcex.lpszClassName,
        L"Сводная таблица релизов — MusicSorter Studio",
        WS_OVERLAPPEDWINDOW,
        posX, posY, 980, 650,
        NULL, NULL, m_hInstance, NULL
    );

    if (!m_hSummaryWnd) return;

    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_hSummaryWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGIDevice* pDXGIDevice = NULL;
    m_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pDXGIDevice));
    IDXGIAdapter* pDXGIAdapter = NULL;
    pDXGIDevice->GetAdapter(&pDXGIAdapter);
    IDXGIFactory* pIDXGIFactory = NULL;
    pDXGIAdapter->GetParent(IID_PPV_ARGS(&pIDXGIFactory));
    pIDXGIFactory->CreateSwapChain(m_pd3dDevice, &sd, &m_pSummarySwapChain);
    pIDXGIFactory->Release();
    pDXGIAdapter->Release();
    pDXGIDevice->Release();

    CreateSummaryRenderTarget();

    // Create ImGui context for summary window with its own font atlas
    m_summaryImGuiContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_summaryImGuiContext);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;

    // Load fonts for summary window
    ImFontConfig font_cfg_primary;
    font_cfg_primary.FontDataOwnedByAtlas = false;
    static const ImWchar ranges_latin_cyrillic[] = {
        0x0020, 0x00FF,
        0x0400, 0x052F,
        0x2000, 0x206F,
        0,
    };

    if (fs::exists("C:\\Windows\\Fonts\\segoeui.ttf")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f, &font_cfg_primary, ranges_latin_cyrillic);
    } else if (fs::exists("C:\\Windows\\Fonts\\arial.ttf")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 16.0f, &font_cfg_primary, ranges_latin_cyrillic);
    }

    ImFontConfig font_cfg_cjk;
    font_cfg_cjk.FontDataOwnedByAtlas = false;
    font_cfg_cjk.MergeMode = true;

    static const ImWchar ranges_cjk[] = {
        0x3000, 0x30FF,
        0x31F0, 0x31FF,
        0x4E00, 0x9FAF,
        0xFF00, 0xFFEF,
        0,
    };

    if (fs::exists("C:\\Windows\\Fonts\\msgothic.ttc")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msgothic.ttc", 16.0f, &font_cfg_cjk, ranges_cjk);
    } else if (fs::exists("C:\\Windows\\Fonts\\YuGothM.ttc")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\YuGothM.ttc", 16.0f, &font_cfg_cjk, ranges_cjk);
    }

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ItemSpacing = ImVec2(10, 6);
    style.FramePadding = ImVec2(10, 6);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);

    ImGui_ImplWin32_Init(m_hSummaryWnd);
    ImGui_ImplDX11_Init(m_pd3dDevice, m_pd3dDeviceContext);

    ShowWindow(m_hSummaryWnd, SW_SHOW);
    UpdateWindow(m_hSummaryWnd);
    SetForegroundWindow(m_hSummaryWnd);

    if (m_mainImGuiContext) {
        ImGui::SetCurrentContext(m_mainImGuiContext);
    }
}

LRESULT CALLBACK AppWindow::SummaryWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (Instance().m_summaryImGuiContext) {
        ImGuiContext* prevCtx = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(Instance().m_summaryImGuiContext);
        LRESULT res = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        ImGui::SetCurrentContext(prevCtx);
        if (res) return true;
    }

    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED && Instance().m_pd3dDevice != NULL) {
            Instance().ResizeSummaryRenderTarget((UINT)LOWORD(lParam), (UINT)HIWORD(lParam));
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_CLOSE:
        Instance().CloseSummaryWindow();
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void AppWindow::Cleanup() {
    CloseSummaryWindow();

    for (auto& item : m_tagItems) {
        if (item.localTexture) item.localTexture->Release();
        if (item.onlineTexture) item.onlineTexture->Release();
    }
    m_tagItems.clear();

    if (m_mainImGuiContext) {
        ImGui::SetCurrentContext(m_mainImGuiContext);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(m_mainImGuiContext);
        m_mainImGuiContext = NULL;
    }

    CleanupDeviceD3D();
    DestroyWindow(m_hWnd);
    UnregisterClassW(L"MusicSorterImGuiClass", m_hInstance);
}

std::vector<size_t> AppWindow::GetAlbumTrackIndices(size_t referenceIndex) const {
    std::vector<size_t> result;
    if (referenceIndex >= m_tagItems.size()) return result;

    const auto& ref = m_tagItems[referenceIndex];
    std::string refFolder = fs::path(ref.filePath).parent_path().string();
    std::string refAlbumKey = NormalizeKey(ref.albumBuf);
    std::string refMbId = ref.releaseGroupMbId;

    for (size_t i = 0; i < m_tagItems.size(); ++i) {
        const auto& item = m_tagItems[i];
        if (item.isProcessed) continue;

        bool match = false;
        if (!refFolder.empty() && fs::path(item.filePath).parent_path().string() == refFolder) {
            match = true;
        } else if (!refMbId.empty() && item.releaseGroupMbId == refMbId) {
            match = true;
        } else if (!refAlbumKey.empty() && refAlbumKey != "unknown" && refAlbumKey != "tosort" && refAlbumKey != "music" && refAlbumKey != "media") {
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

void AppWindow::AdvanceToNextUnprocessedTrack() {
    while (m_currentTagIndex < m_tagItems.size() && m_tagItems[m_currentTagIndex].isProcessed) {
        m_currentTagIndex++;
    }
}

void AppWindow::SkipTracks(const std::vector<size_t>& indices) {
    if (indices.empty()) return;
    for (size_t idx : indices) {
        if (idx < m_tagItems.size()) {
            m_tagItems[idx].isProcessed = true;
            LOG_INFO("[SKIPPED] Skipped track: " + m_tagItems[idx].originalFilename);
        }
    }
    AdvanceToNextUnprocessedTrack();
}

void AppWindow::ApproveTracks(const std::vector<size_t>& indices) {
    if (indices.empty()) return;

    struct TrackTask {
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

    std::vector<TrackTask> tasks;
    tasks.reserve(indices.size());

    // Reference cover preference from items
    std::vector<unsigned char> fallbackCover;
    for (size_t idx : indices) {
        if (idx < m_tagItems.size()) {
            const auto& it = m_tagItems[idx];
            if (it.selectedCoverChoice == 1 && !it.onlineCoverBytes.empty()) {
                fallbackCover = it.onlineCoverBytes;
                break;
            } else if (it.selectedCoverChoice == 0 && !it.localCoverBytes.empty()) {
                fallbackCover = it.localCoverBytes;
                break;
            } else if (!it.onlineCoverBytes.empty() && fallbackCover.empty()) {
                fallbackCover = it.onlineCoverBytes;
            } else if (!it.localCoverBytes.empty() && fallbackCover.empty()) {
                fallbackCover = it.localCoverBytes;
            }
        }
    }

    for (size_t idx : indices) {
        if (idx >= m_tagItems.size()) continue;
        auto& item = m_tagItems[idx];
        if (item.isProcessed) continue;

        TrackTask task;
        task.newArtist = item.artistBuf;
        task.newAlbum = item.albumBuf;
        task.newTitle = item.titleBuf;
        task.newTrackNo = item.trackNoBuf;
        task.newYear = item.yearBuf;
        task.newLyrics = item.lyricsBuf;
        task.srcFilePath = item.filePath;
        task.origFilename = item.originalFilename;

        if (item.selectedCoverChoice == 1 && !item.onlineCoverBytes.empty()) {
            task.chosenCover = item.onlineCoverBytes;
        } else if (item.selectedCoverChoice == 0 && !item.localCoverBytes.empty()) {
            task.chosenCover = item.localCoverBytes;
        } else if (!fallbackCover.empty()) {
            task.chosenCover = fallbackCover;
        } else if (!item.onlineCoverBytes.empty()) {
            task.chosenCover = item.onlineCoverBytes;
        } else {
            task.chosenCover = item.localCoverBytes;
        }

        tasks.push_back(task);
        item.isProcessed = true;
    }

    if (tasks.empty()) return;

    LOG_INFO("[BATCH APPROVAL] Approving batch of " + std::to_string(tasks.size()) + " track(s)...");

    AdvanceToNextUnprocessedTrack();

    std::thread([tasks]() {
        struct AlbumTargets {
            fs::path flacDir;
            fs::path mp3Dir;
        };
        std::map<fs::path, AlbumTargets> processedSourceDirs;

        for (const auto& t : tasks) {
            fs::path srcFile(t.srcFilePath);
            if (!fs::exists(srcFile)) {
                LOG_WARN("[APPROVE WARN] Source file no longer exists: " + t.srcFilePath);
                continue;
            }

            std::string ext = srcFile.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            std::string safeArtist = SanitizeForFilename(t.newArtist);
            std::string safeAlbum  = SanitizeForFilename(t.newAlbum);
            std::string safeTitle  = SanitizeForFilename(t.newTitle);

            fs::path flacDir = fs::path(g_FlacDir) / safeArtist / safeAlbum;
            fs::path mp3Dir  = fs::path(g_Mp3Dir) / safeArtist / safeAlbum;
            fs::create_directories(flacDir);
            fs::create_directories(mp3Dir);

            processedSourceDirs[srcFile.parent_path()] = { flacDir, mp3Dir };

            std::string baseTrackName = t.newTrackNo + ". " + safeTitle;

            std::string finalLyrics = t.newLyrics;
            if (finalLyrics.empty()) {
                finalLyrics = FetchLrcLibSyncedLyrics(t.newArtist, t.newTitle, t.newAlbum);
            }

            if (ext == ".flac") {
                fs::path flacFile = flacDir / (baseTrackName + ".flac");
                fs::path mp3File  = mp3Dir / (baseTrackName + ".mp3");

                // 1. Embed tags & picture into FLAC header
                bool embeddedOk = WriteFlacTagsAndPicture(srcFile.string(), t.newArtist, t.newAlbum, t.newTitle, t.newTrackNo, t.newYear, finalLyrics, t.chosenCover);
                if (embeddedOk) {
                    LOG_INFO("[TAGS EMBEDDED] VorbisComment tags & cover art written to: " + t.origFilename);
                }

                // Move FLAC to flac/Artist/Album/
                try {
                    if (fs::exists(flacFile)) fs::remove(flacFile);
                    fs::rename(srcFile, flacFile);
                    LOG_INFO("[FLAC MOVED] " + fs::relative(flacFile, g_BaseDir).string());
                } catch (const std::exception& ex) {
                    LOG_INFO("[MOVE ERROR] Failed moving FLAC: " + std::string(ex.what()));
                    continue;
                }

                // 2. Convert FLAC -> MP3 320kbps in mp3/Artist/Album/
                LOG_INFO("[CONVERTING MP3] Encoding 320kbps MP3 for: " + baseTrackName + ".mp3 ...");
                if (ConvertFlacToMp3(flacFile.string(), mp3File.string())) {
                    WriteMp3TagsAndPicture(mp3File.string(), t.newArtist, t.newAlbum, t.newTitle, t.newTrackNo, t.newYear, finalLyrics, t.chosenCover);
                    LOG_INFO("[MP3 MIRRORED] Created 320kbps MP3: " + fs::relative(mp3File, g_BaseDir).string());
                } else {
                    LOG_INFO("[CONVERT WARN] FFmpeg conversion failed for: " + baseTrackName);
                }

                // Save cover.jpg in both flac/ and mp3/
                if (!t.chosenCover.empty()) {
                    for (const auto& dir : { flacDir, mp3Dir }) {
                        fs::path coverDst = dir / "cover.jpg";
                        if (!fs::exists(coverDst) || fs::file_size(coverDst) != t.chosenCover.size()) {
                            std::ofstream cOut(coverDst, std::ios::binary);
                            if (cOut.is_open()) {
                                cOut.write((const char*)t.chosenCover.data(), t.chosenCover.size());
                                cOut.close();
                                LOG_INFO("[COVER SAVED] Saved cover art to: " + fs::relative(coverDst, g_BaseDir).string());
                            }
                        }
                    }
                }
            } else if (ext == ".mp3") {
                fs::path mp3File  = mp3Dir / (baseTrackName + ".mp3");
                fs::path flacFallback = flacDir / (baseTrackName + ".mp3"); // Fallback rule: MP3 in flac/ folder!

                // 1. Embed ID3v2.4 tags & picture into MP3 header
                bool embeddedOk = WriteMp3TagsAndPicture(srcFile.string(), t.newArtist, t.newAlbum, t.newTitle, t.newTrackNo, t.newYear, finalLyrics, t.chosenCover);
                if (embeddedOk) {
                    LOG_INFO("[TAGS EMBEDDED] ID3v2.4 tags & cover art written to: " + t.origFilename);
                }

                // Move MP3 to mp3/Artist/Album/
                try {
                    if (fs::exists(mp3File)) fs::remove(mp3File);
                    fs::rename(srcFile, mp3File);
                    LOG_INFO("[MP3 MOVED] " + fs::relative(mp3File, g_BaseDir).string());

                    // FLAC Fallback Rule: Copy MP3 into flac/ folder!
                    fs::copy_file(mp3File, flacFallback, fs::copy_options::overwrite_existing);
                    LOG_INFO("[FLAC FALLBACK] Copied MP3 fallback into: " + fs::relative(flacFallback, g_BaseDir).string());
                } catch (const std::exception& ex) {
                    LOG_INFO("[MOVE ERROR] Failed moving MP3: " + std::string(ex.what()));
                    continue;
                }

                // Save cover.jpg in both mp3/ and flac/
                if (!t.chosenCover.empty()) {
                    for (const auto& dir : { mp3Dir, flacDir }) {
                        fs::path coverDst = dir / "cover.jpg";
                        if (!fs::exists(coverDst) || fs::file_size(coverDst) != t.chosenCover.size()) {
                            std::ofstream cOut(coverDst, std::ios::binary);
                            if (cOut.is_open()) {
                                cOut.write((const char*)t.chosenCover.data(), t.chosenCover.size());
                                cOut.close();
                                LOG_INFO("[COVER SAVED] Saved cover art to: " + fs::relative(coverDst, g_BaseDir).string());
                            }
                        }
                    }
                }
            }
        }

        // Process leftover non-audio files and cleanup empty folders in TO SORT
        for (const auto& [srcDir, targetDirs] : processedSourceDirs) {
            if (!HasAudioFiles(srcDir)) {
                std::error_code ec;
                std::vector<fs::path> remainingFiles;
                for (auto& p : fs::recursive_directory_iterator(srcDir, fs::directory_options::skip_permission_denied, ec)) {
                    if (p.is_regular_file(ec)) {
                        remainingFiles.push_back(p.path());
                    }
                }

                for (const auto& f : remainingFiles) {
                    fs::path rel = fs::relative(f, srcDir, ec);
                    fs::path targetFlac = targetDirs.flacDir / rel;
                    fs::path targetMp3 = targetDirs.mp3Dir / rel;

                    std::string fnLower = f.filename().string();
                    std::transform(fnLower.begin(), fnLower.end(), fnLower.begin(), ::tolower);

                    try {
                        fs::create_directories(targetFlac.parent_path(), ec);
                        if (fs::exists(targetFlac, ec)) {
                            if (fnLower == "cover.jpg" || fnLower == "cover.jpeg" || fnLower == "cover.png") {
                                fs::remove(f, ec);
                                continue;
                            }
                            fs::remove(targetFlac, ec);
                        }
                        fs::rename(f, targetFlac, ec);
                        LOG_INFO("[NON-AUDIO MOVED] " + fs::relative(targetFlac, g_BaseDir).string());

                        fs::create_directories(targetMp3.parent_path(), ec);
                        fs::copy_file(targetFlac, targetMp3, fs::copy_options::overwrite_existing, ec);
                    } catch (const std::exception& ex) {
                        LOG_WARN("[NON-AUDIO MOVE ERROR] " + f.filename().string() + ": " + ex.what());
                        if (fs::exists(targetFlac, ec)) {
                            fs::remove(f, ec);
                        }
                    }
                }

                RemoveEmptySubdirectories(srcDir);
                CleanupEmptyParentDirectories(srcDir.parent_path(), fs::path(g_ToSortDir));
            }
        }

        CleanupOrphanToSortFolders(fs::path(g_ToSortDir));

        LOG_INFO("[BATCH APPROVAL DONE] Finished processing " + std::to_string(tasks.size()) + " track(s).");
    }).detach();
}

void AppWindow::RenderReleaseSummaryTable() {
    struct ReleaseGroupInfo {
        std::string albumKey;
        std::string artist;
        std::string album;
        std::string releaseGroupMbId;
        std::string releaseDate;
        MatchTier matchTier = MatchTier::Niche_Local;
        size_t fileCount = 0;
        size_t firstTrackIndex = 0;
        bool isFetchCompleted = false;
    };

    std::vector<ReleaseGroupInfo> groups;
    std::unordered_map<std::string, size_t> keyMap;

    size_t countTierA = 0;
    size_t countTierB = 0;
    size_t countTierC = 0;
    size_t countTouhouDb = 0;
    size_t countVocaDb = 0;
    size_t countUtaiteDb = 0;
    size_t countDiscogs = 0;
    size_t countNiche = 0;
    size_t totalUnprocessedFiles = 0;

    for (size_t i = 0; i < m_tagItems.size(); ++i) {
        const auto& itm = m_tagItems[i];
        if (itm.isProcessed) continue;
        totalUnprocessedFiles++;

        std::string albumClean(itm.albumBuf);
        std::string albumKey = NormalizeKey(albumClean);
        if (albumKey.empty() || albumKey == "unknown" || albumKey == "tosort" || albumKey == "music" || albumKey == "media") {
            albumKey = NormalizeKey(fs::path(itm.filePath).parent_path().string());
        }

        auto it = keyMap.find(albumKey);
        if (it == keyMap.end()) {
            ReleaseGroupInfo g;
            g.albumKey = albumKey;
            g.artist = itm.artistBuf;
            g.album = itm.albumBuf;
            g.releaseGroupMbId = itm.releaseGroupMbId;
            g.releaseDate = itm.yearBuf;
            g.matchTier = itm.matchTier;
            g.fileCount = 1;
            g.firstTrackIndex = i;
            g.isFetchCompleted = itm.isFetchCompleted;
            keyMap[albumKey] = groups.size();
            groups.push_back(g);
        } else {
            auto& g = groups[it->second];
            g.fileCount++;
            if (g.artist.empty() || g.artist == "Unknown Artist") g.artist = itm.artistBuf;
            if (g.album.empty()) g.album = itm.albumBuf;
            if (g.releaseGroupMbId.empty() && !itm.releaseGroupMbId.empty()) {
                g.releaseGroupMbId = itm.releaseGroupMbId;
                g.matchTier = itm.matchTier;
            }
            if (g.releaseDate.empty() && strlen(itm.yearBuf) > 0) {
                g.releaseDate = itm.yearBuf;
            }
            if (itm.isFetchCompleted) g.isFetchCompleted = true;
        }
    }

    for (const auto& g : groups) {
        if (g.matchTier == MatchTier::AcoustId || g.matchTier == MatchTier::TierA) {
            countTierA++;
        } else if (g.matchTier == MatchTier::TierB_Verified || g.matchTier == MatchTier::TierB_Fallback || g.matchTier == MatchTier::TierB_Katakana) {
            countTierB++;
        } else if (g.matchTier == MatchTier::TierC_Loose) {
            countTierC++;
        } else if (g.matchTier == MatchTier::TouhouDB) {
            countTouhouDb++;
        } else if (g.matchTier == MatchTier::VocaDB) {
            countVocaDb++;
        } else if (g.matchTier == MatchTier::UtaiteDB) {
            countUtaiteDb++;
        } else if (g.matchTier == MatchTier::Discogs) {
            countDiscogs++;
        } else {
            countNiche++;
        }
    }

    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Аудит распознавания релизов");
    ImGui::SameLine();
    ImGui::TextDisabled("| Всего релизов: %zu (файлов: %zu)", groups.size(), totalUnprocessedFiles);

    ImGui::Spacing();

    // Filter Buttons
    static char searchFilter[128] = "";

    auto drawFilterBtn = [this](const char* label, int filterValue, size_t count, const ImVec4& activeColor) {
        bool isActive = (m_releaseSummaryTierFilter == filterValue);
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        }
        char buf[64];
        sprintf_s(buf, sizeof(buf), "%s (%zu)", label, count);
        if (ImGui::Button(buf)) {
            m_releaseSummaryTierFilter = filterValue;
        }
        if (isActive) {
            ImGui::PopStyleColor(2);
        }
    };

    drawFilterBtn("Все", 0, groups.size(), ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
    ImGui::SameLine();
    drawFilterBtn("Tier A", 1, countTierA, ImVec4(0.2f, 0.9f, 0.3f, 1.0f));
    ImGui::SameLine();
    drawFilterBtn("Tier B", 2, countTierB, ImVec4(0.95f, 0.85f, 0.2f, 1.0f));
    ImGui::SameLine();
    drawFilterBtn("Tier C", 3, countTierC, ImVec4(1.0f, 0.55f, 0.2f, 1.0f));
    ImGui::SameLine();
    drawFilterBtn("TouhouDB", 6, countTouhouDb, ImVec4(0.95f, 0.35f, 0.55f, 1.0f));
    ImGui::SameLine();
    drawFilterBtn("VocaDB", 7, countVocaDb, ImVec4(0.2f, 0.85f, 0.85f, 1.0f));
    ImGui::SameLine();
    drawFilterBtn("UtaiteDB", 8, countUtaiteDb, ImVec4(0.65f, 0.45f, 0.95f, 1.0f));
    ImGui::SameLine();
    drawFilterBtn("Discogs", 5, countDiscogs, ImVec4(0.2f, 0.9f, 0.3f, 1.0f));
    ImGui::SameLine();
    drawFilterBtn("Niche", 4, countNiche, ImVec4(0.95f, 0.4f, 0.3f, 1.0f));

    if (countTierA > 0) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.65f, 0.32f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.78f, 0.38f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.55f, 0.26f, 1.0f));
        char allTierABtn[64];
        sprintf_s(allTierABtn, sizeof(allTierABtn), "Принять все Tier A (%zu)", countTierA);
        if (ImGui::Button(allTierABtn)) {
            std::vector<size_t> allTierAIndices;
            for (const auto& g : groups) {
                if (g.matchTier == MatchTier::AcoustId || g.matchTier == MatchTier::TierA) {
                    auto idxs = GetAlbumTrackIndices(g.firstTrackIndex);
                    allTierAIndices.insert(allTierAIndices.end(), idxs.begin(), idxs.end());
                }
            }
            ApproveTracks(allTierAIndices);
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::InputTextWithHint("##ReleaseSearchFilter", "Поиск...", searchFilter, sizeof(searchFilter));

    ImGui::Separator();

    if (groups.empty()) {
        ImGui::TextDisabled("Текущая очередь пуста. Перейдите во вкладку '2. Инспектор тегов' для сканирования файлов.");
    } else {
        if (ImGui::BeginTable("ReleaseSummaryTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0, 0))) {
            ImGui::TableSetupColumn("Альбом и исполнитель", ImGuiTableColumnFlags_WidthStretch, 0.35f);
            ImGui::TableSetupColumn("Файлов", ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableSetupColumn("Уровень распознавания", ImGuiTableColumnFlags_WidthFixed, 175.0f);
            ImGui::TableSetupColumn("MBID / Источник и дата", ImGuiTableColumnFlags_WidthFixed, 185.0f);
            ImGui::TableSetupColumn("Действия", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableHeadersRow();

            std::string filterLower = searchFilter;
            std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

            for (size_t gi = 0; gi < groups.size(); ++gi) {
                const auto& g = groups[gi];

                if (m_releaseSummaryTierFilter == 1 && !(g.matchTier == MatchTier::AcoustId || g.matchTier == MatchTier::TierA)) {
                    continue;
                }
                if (m_releaseSummaryTierFilter == 2 && !(g.matchTier == MatchTier::TierB_Verified || g.matchTier == MatchTier::TierB_Fallback || g.matchTier == MatchTier::TierB_Katakana)) {
                    continue;
                }
                if (m_releaseSummaryTierFilter == 3 && !(g.matchTier == MatchTier::TierC_Loose)) {
                    continue;
                }
                if (m_releaseSummaryTierFilter == 4 && !(g.matchTier == MatchTier::Niche_Local)) {
                    continue;
                }
                if (m_releaseSummaryTierFilter == 5 && !(g.matchTier == MatchTier::Discogs)) {
                    continue;
                }
                if (m_releaseSummaryTierFilter == 6 && !(g.matchTier == MatchTier::TouhouDB)) {
                    continue;
                }
                if (m_releaseSummaryTierFilter == 7 && !(g.matchTier == MatchTier::VocaDB)) {
                    continue;
                }
                if (m_releaseSummaryTierFilter == 8 && !(g.matchTier == MatchTier::UtaiteDB)) {
                    continue;
                }

                if (!filterLower.empty()) {
                    std::string aLower = g.artist;
                    std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
                    std::string albLower = g.album;
                    std::transform(albLower.begin(), albLower.end(), albLower.begin(), ::tolower);
                    std::string mbidLower = g.releaseGroupMbId;
                    std::transform(mbidLower.begin(), mbidLower.end(), mbidLower.begin(), ::tolower);

                    if (aLower.find(filterLower) == std::string::npos &&
                        albLower.find(filterLower) == std::string::npos &&
                        mbidLower.find(filterLower) == std::string::npos) {
                        continue;
                    }
                }

                ImGui::TableNextRow();

                // Column 0: Album & Artist with click selection
                ImGui::TableSetColumnIndex(0);
                bool isSelected = (m_currentTagIndex >= g.firstTrackIndex && m_currentTagIndex < g.firstTrackIndex + g.fileCount);
                std::string label = g.album + " - " + g.artist + "##Row_" + std::to_string(gi);
                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                    m_currentTagIndex = g.firstTrackIndex;
                }

                // Column 1: Files count
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%zu", g.fileCount);

                // Column 2: Recognition Tier
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(GetTierColor(g.matchTier), "%s", GetTierName(g.matchTier));

                // Column 3: MBID / Source & Date
                ImGui::TableSetColumnIndex(3);
                if (!g.releaseGroupMbId.empty()) {
                    ImGui::TextUnformatted(g.releaseGroupMbId.c_str());
                } else {
                    ImGui::TextDisabled("—");
                }
                if (!g.releaseDate.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%s)", g.releaseDate.c_str());
                }

                // Column 4: Actions (Approve + Web Link)
                ImGui::TableSetColumnIndex(4);
                char aprBtnId[64];
                sprintf_s(aprBtnId, sizeof(aprBtnId), "Принять##Rel_%zu", gi);
                if (ImGui::Button(aprBtnId, ImVec2(75, 24))) {
                    ApproveTracks(GetAlbumTrackIndices(g.firstTrackIndex));
                }
                if (!g.releaseGroupMbId.empty()) {
                    ImGui::SameLine();
                    char btnId[64];
                    sprintf_s(btnId, sizeof(btnId), "Web##%zu", gi);
                    if (ImGui::Button(btnId, ImVec2(55, 24))) {
                        std::string targetUrl;
                        if (g.releaseGroupMbId.rfind("touhoudb_", 0) == 0) {
                            targetUrl = "https://touhoudb.com/Al/" + g.releaseGroupMbId.substr(9);
                        } else if (g.releaseGroupMbId.rfind("vocadb_", 0) == 0) {
                            targetUrl = "https://vocadb.net/Al/" + g.releaseGroupMbId.substr(7);
                        } else if (g.releaseGroupMbId.rfind("utaitedb_", 0) == 0) {
                            targetUrl = "https://utaitedb.net/Al/" + g.releaseGroupMbId.substr(9);
                        } else if (g.releaseGroupMbId.rfind("discogs_", 0) == 0) {
                            targetUrl = "https://www.discogs.com/release/" + g.releaseGroupMbId.substr(8);
                        } else {
                            targetUrl = "https://musicbrainz.org/release-group/" + g.releaseGroupMbId;
                        }
                        ShellExecuteA(NULL, "open", targetUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    }
                }
            }

            ImGui::EndTable();
        }
    }
}

void AppWindow::FetchManualMusicBrainzMetadata(const std::string& inputUrl, bool applyToAllInAlbum) {
    if (m_tagItems.empty() || m_currentTagIndex >= m_tagItems.size()) {
        LOG_WARN("[MANUAL MB] Очередь треков пуста или индекс некорректен.");
        return;
    }

    std::regex uuidRegex(R"([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})");
    std::smatch match;
    if (!std::regex_search(inputUrl, match, uuidRegex)) {
        LOG_WARN("[MANUAL MB] В строке не найден валидный MBID (UUID): " + inputUrl);
        return;
    }

    std::string mbid = match.str(0);
    LOG_INFO("[MANUAL MB] Запуск ручной загрузки метаданных для MBID: " + mbid + " (URL: " + inputUrl + ")");

    std::thread([this, mbid, inputUrl, applyToAllInAlbum]() {
        std::string releaseMbId = mbid;
        std::string releaseGroupMbId = "";

        // If release-group URL, resolve first release ID
        if (inputUrl.find("release-group") != std::string::npos) {
            releaseGroupMbId = mbid;
            std::string rgLookupUrl = "https://musicbrainz.org/ws/2/release?release-group=" + mbid + "&inc=recordings+artist-credits+release-groups&fmt=json";
            std::string rgRes = HttpGetString(Utf8ToWide(rgLookupUrl));
            size_t p = 0;
            JsonVal rgDoc = ParseJsonSimple(rgRes, p);
            const auto& rels = rgDoc.get("releases");
            if (rels.type == JsonVal::Array && !rels.arrVal.empty()) {
                releaseMbId = rels.get(0).get("id").strVal;
            }
        }

        std::string lookupUrl = "https://musicbrainz.org/ws/2/release/" + releaseMbId + "?inc=recordings+artist-credits+release-groups&fmt=json";
        std::string mbJsonStr = HttpGetString(Utf8ToWide(lookupUrl));
        if (mbJsonStr.empty()) {
            LOG_WARN("[MANUAL MB ERROR] Не удалось получить данные релиза с MusicBrainz для ID: " + releaseMbId);
            return;
        }

        size_t p = 0;
        JsonVal doc = ParseJsonSimple(mbJsonStr, p);

        std::string releaseTitle = doc.get("title").strVal;
        std::string releaseDate = doc.get("date").strVal;

        std::string artistCredit = "";
        const auto& ac = doc.get("artist-credit");
        if (ac.type == JsonVal::Array) {
            for (size_t i = 0; i < ac.arrVal.size(); ++i) {
                std::string aName = ac.get(i).get("name").strVal;
                std::string join = ac.get(i).get("joinphrase").strVal;
                artistCredit += aName + join;
            }
        }
        if (artistCredit.empty()) artistCredit = "Unknown Artist";

        if (releaseGroupMbId.empty()) {
            releaseGroupMbId = doc.get("release-group").get("id").strVal;
            if (releaseGroupMbId.empty()) releaseGroupMbId = releaseMbId;
        }

        struct MbTrackInfo {
            int trackNumber = 0;
            std::string title;
            std::string artist;
            std::string recordingId;
        };

        std::vector<MbTrackInfo> mbTracks;
        const auto& media = doc.get("media");
        if (media.type == JsonVal::Array) {
            int trackCounter = 1;
            for (size_t m = 0; m < media.arrVal.size(); ++m) {
                const auto& trs = media.get(m).get("tracks");
                if (trs.type != JsonVal::Array) continue;
                for (size_t i = 0; i < trs.arrVal.size(); ++i) {
                    const auto& tObj = trs.get(i);
                    MbTrackInfo ti;
                    ti.trackNumber = (int)tObj.get("position").numVal;
                    if (ti.trackNumber <= 0) ti.trackNumber = trackCounter;
                    trackCounter++;

                    ti.title = tObj.get("title").strVal;
                    if (ti.title.empty()) ti.title = tObj.get("recording").get("title").strVal;

                    const auto& trkAc = tObj.get("artist-credit");
                    if (trkAc.type == JsonVal::Array && !trkAc.arrVal.empty()) {
                        for (size_t a = 0; a < trkAc.arrVal.size(); ++a) {
                            ti.artist += trkAc.get(a).get("name").strVal + trkAc.get(a).get("joinphrase").strVal;
                        }
                    }
                    if (ti.artist.empty()) ti.artist = artistCredit;
                    ti.recordingId = tObj.get("recording").get("id").strVal;

                    mbTracks.push_back(ti);
                }
            }
        }

        // Download Cover Art
        std::vector<unsigned char> coverData;
        const char* endpoints[] = {
            "https://coverartarchive.org/release/%s/front",
            "https://coverartarchive.org/release-group/%s/front",
            "https://coverartarchive.org/release/%s/front-1200",
            "https://coverartarchive.org/release-group/%s/front-1200",
            "https://coverartarchive.org/release/%s/front-500",
            "https://coverartarchive.org/release-group/%s/front-500",
            "https://coverartarchive.org/release/%s/front-250",
            "https://coverartarchive.org/release-group/%s/front-250"
        };

        for (const char* ep : endpoints) {
            char urlBuf[256];
            if (strstr(ep, "release-group")) {
                sprintf_s(urlBuf, sizeof(urlBuf), ep, releaseGroupMbId.c_str());
            } else {
                sprintf_s(urlBuf, sizeof(urlBuf), ep, releaseMbId.c_str());
            }
            coverData = HttpGetBytes(Utf8ToWide(urlBuf));
            if (!coverData.empty() && coverData.size() > 4096) break;
        }

        // Determine target tracks in m_tagItems
        auto& currentItem = m_tagItems[m_currentTagIndex];
        std::string targetFolder = fs::path(currentItem.filePath).parent_path().string();

        std::vector<size_t> targetIndices;
        if (applyToAllInAlbum) {
            for (size_t i = 0; i < m_tagItems.size(); ++i) {
                if (fs::path(m_tagItems[i].filePath).parent_path().string() == targetFolder) {
                    targetIndices.push_back(i);
                }
            }
        } else {
            targetIndices.push_back(m_currentTagIndex);
        }

        for (size_t idx : targetIndices) {
            auto& item = m_tagItems[idx];
            
            // Find matching track from mbTracks
            int trackNo = 0;
            if (strlen(item.trackNoBuf) > 0) trackNo = atoi(item.trackNoBuf);
            if (trackNo <= 0 && !item.embeddedTrackNo.empty()) trackNo = atoi(item.embeddedTrackNo.c_str());
            if (trackNo <= 0) {
                std::string fn = fs::path(item.filePath).stem().string();
                int parsedNo = 0;
                if (sscanf_s(fn.c_str(), "%d", &parsedNo) == 1 && parsedNo > 0) {
                    trackNo = parsedNo;
                }
            }

            MbTrackInfo matchedMbTrack;
            bool foundTrack = false;

            if (trackNo > 0) {
                for (const auto& mt : mbTracks) {
                    if (mt.trackNumber == trackNo) {
                        matchedMbTrack = mt;
                        foundTrack = true;
                        break;
                    }
                }
            }

            if (!foundTrack && !mbTracks.empty()) {
                size_t relIdx = 0;
                for (size_t t = 0; t < targetIndices.size(); ++t) {
                    if (targetIndices[t] == idx) { relIdx = t; break; }
                }
                if (relIdx < mbTracks.size()) {
                    matchedMbTrack = mbTracks[relIdx];
                    foundTrack = true;
                } else {
                    matchedMbTrack = mbTracks[0];
                }
            }

            strncpy_s(item.artistBuf, matchedMbTrack.artist.empty() ? artistCredit.c_str() : matchedMbTrack.artist.c_str(), sizeof(item.artistBuf) - 1);
            strncpy_s(item.albumBuf, releaseTitle.c_str(), sizeof(item.albumBuf) - 1);
            if (!matchedMbTrack.title.empty()) {
                strncpy_s(item.titleBuf, matchedMbTrack.title.c_str(), sizeof(item.titleBuf) - 1);
            }
            if (matchedMbTrack.trackNumber > 0) {
                sprintf_s(item.trackNoBuf, sizeof(item.trackNoBuf), "%d", matchedMbTrack.trackNumber);
            }
            if (!releaseDate.empty()) {
                strncpy_s(item.yearBuf, releaseDate.c_str(), sizeof(item.yearBuf) - 1);
            }

            item.matchTier = MatchTier::TierA;
            item.releaseGroupMbId = releaseGroupMbId;
            item.isMusicBrainzMatched = true;
            item.isFetchCompleted = true;

            if (!coverData.empty()) {
                item.onlineCoverBytes = coverData;
                item.onlineCoverSource = "CoverArtArchive";
                if (item.onlineTexture) {
                    item.onlineTexture->Release();
                    item.onlineTexture = NULL;
                }
                item.selectedCoverChoice = 1;
            }

            // Fetch LRC lyrics
            std::string lrcLyrics = FetchLrcLibSyncedLyrics(item.artistBuf, item.titleBuf, item.albumBuf);
            if (!lrcLyrics.empty()) {
                item.hasLyrics = true;
                strncpy_s(item.lyricsBuf, lrcLyrics.c_str(), sizeof(item.lyricsBuf) - 1);
            }
        }

        LOG_INFO("[MANUAL MB MATCHED] Успешно загружен релиз: \"" + releaseTitle + "\" (" + artistCredit + ", дата: " + releaseDate + ", треков в MB: " + std::to_string(mbTracks.size()) + ")");
    }).detach();
}

void AppWindow::FetchManualDiscogsMetadata(const std::string& inputUrl, bool applyToAllInAlbum) {
    if (m_tagItems.empty() || m_currentTagIndex >= m_tagItems.size()) {
        LOG_WARN("[MANUAL DISCOGS] Очередь треков пуста или индекс некорректен.");
        return;
    }

    bool isMaster = (inputUrl.find("/master/") != std::string::npos || inputUrl.find("/master") != std::string::npos || inputUrl.find("m") == 0);

    // Extract ID (digits)
    std::string releaseId;
    std::regex idRegex(R"((\d{3,12}))");
    std::smatch match;
    if (std::regex_search(inputUrl, match, idRegex)) {
        releaseId = match.str(1);
    }

    if (releaseId.empty()) {
        LOG_WARN("[MANUAL DISCOGS] В строке не найден ID релиза Discogs: " + inputUrl);
        return;
    }

    LOG_INFO("[MANUAL DISCOGS] Запуск ручной загрузки метаданных для Discogs ID: " + releaseId + " (isMaster=" + std::to_string(isMaster) + ")");

    std::thread([this, releaseId, isMaster, applyToAllInAlbum]() {
        DiscogsReleaseInfo discInfo;
        if (!FetchDiscogsReleaseDetails(releaseId, isMaster, discInfo)) {
            LOG_WARN("[MANUAL DISCOGS ERROR] Не удалось получить данные с Discogs для ID: " + releaseId);
            return;
        }

        std::vector<unsigned char> coverData;
        if (!discInfo.coverUrl.empty()) {
            LOG_INFO("[MANUAL DISCOGS] Загрузка обложки с Discogs: " + discInfo.coverUrl);
            coverData = HttpGetBytes(Utf8ToWide(discInfo.coverUrl));
        }

        auto& currentItem = m_tagItems[m_currentTagIndex];
        std::string targetFolder = fs::path(currentItem.filePath).parent_path().string();

        std::vector<size_t> targetIndices;
        if (applyToAllInAlbum) {
            for (size_t i = 0; i < m_tagItems.size(); ++i) {
                if (fs::path(m_tagItems[i].filePath).parent_path().string() == targetFolder) {
                    targetIndices.push_back(i);
                }
            }
        } else {
            targetIndices.push_back(m_currentTagIndex);
        }

        for (size_t idx : targetIndices) {
            auto& item = m_tagItems[idx];
            if (!discInfo.artist.empty()) {
                strncpy_s(item.artistBuf, discInfo.artist.c_str(), sizeof(item.artistBuf) - 1);
            }
            if (!discInfo.title.empty()) {
                strncpy_s(item.albumBuf, discInfo.title.c_str(), sizeof(item.albumBuf) - 1);
            }
            if (!discInfo.year.empty()) {
                strncpy_s(item.yearBuf, discInfo.year.c_str(), sizeof(item.yearBuf) - 1);
            }

            if (!discInfo.tracks.empty()) {
                ApplyTrackMatch(item, discInfo.tracks);
            }

            item.matchTier = MatchTier::Discogs;
            item.releaseGroupMbId = "discogs_" + releaseId;
            item.isMusicBrainzMatched = true;
            item.isFetchCompleted = true;

            if (!coverData.empty()) {
                item.onlineCoverBytes = coverData;
                item.onlineCoverSource = "Discogs";
                if (item.onlineTexture) {
                    item.onlineTexture->Release();
                    item.onlineTexture = NULL;
                }
                item.selectedCoverChoice = 1;
            }

            // Fetch LRC lyrics
            std::string lrcLyrics = FetchLrcLibSyncedLyrics(item.artistBuf, item.titleBuf, item.albumBuf);
            if (!lrcLyrics.empty()) {
                item.hasLyrics = true;
                strncpy_s(item.lyricsBuf, lrcLyrics.c_str(), sizeof(item.lyricsBuf) - 1);
            }
        }

        LOG_INFO("[MANUAL DISCOGS MATCHED] Успешно загружен релиз Discogs: \"" + discInfo.title + "\" (" + discInfo.artist + ", год: " + discInfo.year + ", треков: " + std::to_string(discInfo.tracks.size()) + ")");
    }).detach();
}

void AppWindow::FetchManualTouhouDbMetadata(const std::string& inputUrl, bool applyToAllInAlbum) {
    if (m_tagItems.empty() || m_currentTagIndex >= m_tagItems.size()) {
        LOG_WARN("[MANUAL TOUHOUDB] Очередь треков пуста или индекс некорректен.");
        return;
    }

    int albumId = 0;
    std::regex idRegex(R"((\d{1,8}))");
    std::smatch match;
    if (std::regex_search(inputUrl, match, idRegex)) {
        try { albumId = std::stoi(match.str(1)); } catch (...) {}
    }

    if (albumId <= 0) {
        LOG_WARN("[MANUAL TOUHOUDB] В строке не найден валидный ID альбома TouhouDB: " + inputUrl);
        return;
    }

    LOG_INFO("[MANUAL TOUHOUDB] Запуск ручной загрузки метаданных для TouhouDB ID: " + std::to_string(albumId));

    std::thread([this, albumId, applyToAllInAlbum]() {
        VdbReleaseInfo info;
        if (!FetchVdbAlbumDetails("https://touhoudb.com", "TouhouDB", albumId, info)) {
            LOG_WARN("[MANUAL TOUHOUDB ERROR] Не удалось получить данные с TouhouDB для ID: " + std::to_string(albumId));
            return;
        }

        std::vector<unsigned char> coverData;
        if (!info.coverUrl.empty()) {
            LOG_INFO("[MANUAL TOUHOUDB] Загрузка обложки с TouhouDB: " + info.coverUrl);
            coverData = HttpGetBytes(Utf8ToWide(info.coverUrl));
        }

        auto& currentItem = m_tagItems[m_currentTagIndex];
        std::string targetFolder = fs::path(currentItem.filePath).parent_path().string();

        std::vector<size_t> targetIndices;
        if (applyToAllInAlbum) {
            for (size_t i = 0; i < m_tagItems.size(); ++i) {
                if (fs::path(m_tagItems[i].filePath).parent_path().string() == targetFolder) {
                    targetIndices.push_back(i);
                }
            }
        } else {
            targetIndices.push_back(m_currentTagIndex);
        }

        for (size_t idx : targetIndices) {
            auto& item = m_tagItems[idx];
            item.albumRomaji = info.titleRomaji;
            item.albumEnglish = info.titleEnglish;
            item.albumJapanese = info.titleJapanese;
            item.artistRomaji = info.artistRomaji;
            item.artistEnglish = info.artistEnglish;
            item.artistJapanese = info.artistJapanese;

            if (!info.artist.empty()) {
                strncpy_s(item.artistBuf, info.artist.c_str(), sizeof(item.artistBuf) - 1);
            }
            if (!info.title.empty()) {
                strncpy_s(item.albumBuf, info.title.c_str(), sizeof(item.albumBuf) - 1);
            }
            if (!info.releaseDate.empty()) {
                strncpy_s(item.yearBuf, info.releaseDate.c_str(), sizeof(item.yearBuf) - 1);
            }

            if (!info.tracks.empty()) {
                ApplyTrackMatch(item, info.tracks);
            }

            item.matchTier = MatchTier::TouhouDB;
            item.releaseGroupMbId = "touhoudb_" + std::to_string(albumId);
            item.isMusicBrainzMatched = true;
            item.isFetchCompleted = true;

            if (!coverData.empty()) {
                item.onlineCoverBytes = coverData;
                item.onlineCoverSource = "TouhouDB";
                if (item.onlineTexture) {
                    item.onlineTexture->Release();
                    item.onlineTexture = NULL;
                }
                item.selectedCoverChoice = 1;
            }

            // Fetch LRC lyrics
            std::string lrcLyrics = FetchLrcLibSyncedLyrics(item.artistBuf, item.titleBuf, item.albumBuf);
            if (!lrcLyrics.empty()) {
                item.hasLyrics = true;
                strncpy_s(item.lyricsBuf, lrcLyrics.c_str(), sizeof(item.lyricsBuf) - 1);
            }
        }

        LOG_INFO("[MANUAL TOUHOUDB MATCHED] Успешно загружен релиз TouhouDB: \"" + info.title + "\" (" + info.artist + ", дата: " + info.releaseDate + ", треков: " + std::to_string(info.tracks.size()) + ")");
    }).detach();
}

void AppWindow::FetchManualVocaDbMetadata(const std::string& inputUrl, bool applyToAllInAlbum) {
    if (m_tagItems.empty() || m_currentTagIndex >= m_tagItems.size()) {
        LOG_WARN("[MANUAL VOCADB] Очередь треков пуста или индекс некорректен.");
        return;
    }

    int albumId = 0;
    std::regex idRegex(R"((\d{1,8}))");
    std::smatch match;
    if (std::regex_search(inputUrl, match, idRegex)) {
        try { albumId = std::stoi(match.str(1)); } catch (...) {}
    }

    if (albumId <= 0) {
        LOG_WARN("[MANUAL VOCADB] В строке не найден валидный ID альбома VocaDB: " + inputUrl);
        return;
    }

    LOG_INFO("[MANUAL VOCADB] Запуск ручной загрузки метаданных для VocaDB ID: " + std::to_string(albumId));

    std::thread([this, albumId, applyToAllInAlbum]() {
        VdbReleaseInfo info;
        if (!FetchVdbAlbumDetails("https://vocadb.net", "VocaDB", albumId, info)) {
            LOG_WARN("[MANUAL VOCADB ERROR] Не удалось получить данные с VocaDB для ID: " + std::to_string(albumId));
            return;
        }

        std::vector<unsigned char> coverData;
        if (!info.coverUrl.empty()) {
            LOG_INFO("[MANUAL VOCADB] Загрузка обложки с VocaDB: " + info.coverUrl);
            coverData = HttpGetBytes(Utf8ToWide(info.coverUrl));
        }

        auto& currentItem = m_tagItems[m_currentTagIndex];
        std::string targetFolder = fs::path(currentItem.filePath).parent_path().string();

        std::vector<size_t> targetIndices;
        if (applyToAllInAlbum) {
            for (size_t i = 0; i < m_tagItems.size(); ++i) {
                if (fs::path(m_tagItems[i].filePath).parent_path().string() == targetFolder) {
                    targetIndices.push_back(i);
                }
            }
        } else {
            targetIndices.push_back(m_currentTagIndex);
        }

        for (size_t idx : targetIndices) {
            auto& item = m_tagItems[idx];
            item.albumRomaji = info.titleRomaji;
            item.albumEnglish = info.titleEnglish;
            item.albumJapanese = info.titleJapanese;
            item.artistRomaji = info.artistRomaji;
            item.artistEnglish = info.artistEnglish;
            item.artistJapanese = info.artistJapanese;

            if (!info.artist.empty()) {
                strncpy_s(item.artistBuf, info.artist.c_str(), sizeof(item.artistBuf) - 1);
            }
            if (!info.title.empty()) {
                strncpy_s(item.albumBuf, info.title.c_str(), sizeof(item.albumBuf) - 1);
            }
            if (!info.releaseDate.empty()) {
                strncpy_s(item.yearBuf, info.releaseDate.c_str(), sizeof(item.yearBuf) - 1);
            }

            if (!info.tracks.empty()) {
                ApplyTrackMatch(item, info.tracks);
            }

            item.matchTier = MatchTier::VocaDB;
            item.releaseGroupMbId = "vocadb_" + std::to_string(albumId);
            item.isMusicBrainzMatched = true;
            item.isFetchCompleted = true;

            if (!coverData.empty()) {
                item.onlineCoverBytes = coverData;
                item.onlineCoverSource = "VocaDB";
                if (item.onlineTexture) {
                    item.onlineTexture->Release();
                    item.onlineTexture = NULL;
                }
                item.selectedCoverChoice = 1;
            }

            // Fetch LRC lyrics
            std::string lrcLyrics = FetchLrcLibSyncedLyrics(item.artistBuf, item.titleBuf, item.albumBuf);
            if (!lrcLyrics.empty()) {
                item.hasLyrics = true;
                strncpy_s(item.lyricsBuf, lrcLyrics.c_str(), sizeof(item.lyricsBuf) - 1);
            }
        }

        LOG_INFO("[MANUAL VOCADB MATCHED] Успешно загружен релиз VocaDB: \"" + info.title + "\" (" + info.artist + ", дата: " + info.releaseDate + ", треков: " + std::to_string(info.tracks.size()) + ")");
    }).detach();
}

void AppWindow::FetchManualUtaiteDbMetadata(const std::string& inputUrl, bool applyToAllInAlbum) {
    if (m_tagItems.empty() || m_currentTagIndex >= m_tagItems.size()) {
        LOG_WARN("[MANUAL UTAITEDB] Очередь треков пуста или индекс некорректен.");
        return;
    }

    int albumId = 0;
    std::regex idRegex(R"((\d{1,8}))");
    std::smatch match;
    if (std::regex_search(inputUrl, match, idRegex)) {
        try { albumId = std::stoi(match.str(1)); } catch (...) {}
    }

    if (albumId <= 0) {
        LOG_WARN("[MANUAL UTAITEDB] В строке не найден валидный ID альбома UtaiteDB: " + inputUrl);
        return;
    }

    LOG_INFO("[MANUAL UTAITEDB] Запуск ручной загрузки метаданных для UtaiteDB ID: " + std::to_string(albumId));

    std::thread([this, albumId, applyToAllInAlbum]() {
        VdbReleaseInfo info;
        if (!FetchVdbAlbumDetails("https://utaitedb.net", "UtaiteDB", albumId, info)) {
            LOG_WARN("[MANUAL UTAITEDB ERROR] Не удалось получить данные с UtaiteDB для ID: " + std::to_string(albumId));
            return;
        }

        std::vector<unsigned char> coverData;
        if (!info.coverUrl.empty()) {
            LOG_INFO("[MANUAL UTAITEDB] Загрузка обложки с UtaiteDB: " + info.coverUrl);
            coverData = HttpGetBytes(Utf8ToWide(info.coverUrl));
        }

        auto& currentItem = m_tagItems[m_currentTagIndex];
        std::string targetFolder = fs::path(currentItem.filePath).parent_path().string();

        std::vector<size_t> targetIndices;
        if (applyToAllInAlbum) {
            for (size_t i = 0; i < m_tagItems.size(); ++i) {
                if (fs::path(m_tagItems[i].filePath).parent_path().string() == targetFolder) {
                    targetIndices.push_back(i);
                }
            }
        } else {
            targetIndices.push_back(m_currentTagIndex);
        }

        for (size_t idx : targetIndices) {
            auto& item = m_tagItems[idx];
            item.albumRomaji = info.titleRomaji;
            item.albumEnglish = info.titleEnglish;
            item.albumJapanese = info.titleJapanese;
            item.artistRomaji = info.artistRomaji;
            item.artistEnglish = info.artistEnglish;
            item.artistJapanese = info.artistJapanese;
            if (!info.artist.empty()) {
                strncpy_s(item.artistBuf, info.artist.c_str(), sizeof(item.artistBuf) - 1);
            }
            if (!info.title.empty()) {
                strncpy_s(item.albumBuf, info.title.c_str(), sizeof(item.albumBuf) - 1);
            }
            if (!info.releaseDate.empty()) {
                strncpy_s(item.yearBuf, info.releaseDate.c_str(), sizeof(item.yearBuf) - 1);
            }

            if (!info.tracks.empty()) {
                ApplyTrackMatch(item, info.tracks);
            }

            item.matchTier = MatchTier::UtaiteDB;
            item.releaseGroupMbId = "utaitedb_" + std::to_string(albumId);
            item.isMusicBrainzMatched = true;
            item.isFetchCompleted = true;

            if (!coverData.empty()) {
                item.onlineCoverBytes = coverData;
                item.onlineCoverSource = "UtaiteDB";
                if (item.onlineTexture) {
                    item.onlineTexture->Release();
                    item.onlineTexture = NULL;
                }
                item.selectedCoverChoice = 1;
            }

            // Fetch LRC lyrics
            std::string lrcLyrics = FetchLrcLibSyncedLyrics(item.artistBuf, item.titleBuf, item.albumBuf);
            if (!lrcLyrics.empty()) {
                item.hasLyrics = true;
                strncpy_s(item.lyricsBuf, lrcLyrics.c_str(), sizeof(item.lyricsBuf) - 1);
            }
        }

        LOG_INFO("[MANUAL UTAITEDB MATCHED] Успешно загружен релиз UtaiteDB: \"" + info.title + "\" (" + info.artist + ", дата: " + info.releaseDate + ", треков: " + std::to_string(info.tracks.size()) + ")");
    }).detach();
}

void AppWindow::RunMessageLoop() {
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // Lazy Texture Creation — only for the currently displayed item to avoid UI freezes
        AdvanceToNextUnprocessedTrack();
        if (!m_tagItems.empty() && m_currentTagIndex < m_tagItems.size() && !m_tagItems[m_currentTagIndex].isProcessed) {
            auto& item = m_tagItems[m_currentTagIndex];
            if (item.localTexture == NULL && !item.localCoverBytes.empty()) {
                item.localTexture = CreateTextureFromMemory(m_pd3dDevice, item.localCoverBytes.data(), item.localCoverBytes.size(), &item.localWidth, &item.localHeight);
                if (item.localTexture) {
                    item.localScore = CalculateImageQualityScore(item.localCoverBytes.data(), item.localCoverBytes.size(), item.localWidth, item.localHeight);
                }
            }

            if (item.isFetchCompleted && !item.onlineCoverBytes.empty() && item.onlineTexture == NULL) {
                item.onlineTexture = CreateTextureFromMemory(m_pd3dDevice, item.onlineCoverBytes.data(), item.onlineCoverBytes.size(), &item.onlineWidth, &item.onlineHeight);
                if (item.onlineTexture) {
                    item.onlineScore = CalculateImageQualityScore(item.onlineCoverBytes.data(), item.onlineCoverBytes.size(), item.onlineWidth, item.onlineHeight);

                    if (item.onlineScore > item.localScore) {
                        item.selectedCoverChoice = 1;
                    } else {
                        item.selectedCoverChoice = 0;
                    }
                }
            }
        }

        // Start Dear ImGui Frame for Main Window
        ImGui::SetCurrentContext(m_mainImGuiContext);
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Global Keyboard Hotkeys Handlers
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            AudioEngine::Instance().TogglePlay();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) || ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            AudioEngine::Instance().SetActiveChannel(AudioEngine::Instance().GetActiveChannel() == 'a' ? 'b' : 'a');
        }

        auto MakeDecisionA = [this]() {
            if (!m_candidates.empty() && m_currentCandidateIndex < m_candidates.size()) {
                auto& pair = m_candidates[m_currentCandidateIndex];
                fs::path toSortParent = fs::path(g_ToSortDir).parent_path();
                fs::path rel = fs::relative(pair.trackB_path, toSortParent);
                fs::path dst = fs::path(g_DeleteDir) / rel;
                fs::create_directories(dst.parent_path());
                LOG_INFO("[DECISION] Keeping Track A. Moving rejected Track B to delete/: " + rel.string());
                try {
                    if (fs::exists(dst)) fs::remove(dst);
                    fs::rename(pair.trackB_path, dst);
                    CleanupEmptyParentDirectories(fs::path(pair.trackB_path).parent_path(), fs::path(g_ToSortDir));
                } catch (const std::exception& ex) {
                    LOG_WARN("[DECISION] Failed moving Track B: " + std::string(ex.what()));
                }

                m_currentCandidateIndex++;
                if (m_currentCandidateIndex < m_candidates.size()) {
                    auto& next = m_candidates[m_currentCandidateIndex];
                    AudioEngine::Instance().LoadTrackA(next.trackA_path);
                    AudioEngine::Instance().LoadTrackB(next.trackB_path);
                }
            }
        };

        auto MakeDecisionB = [this]() {
            if (!m_candidates.empty() && m_currentCandidateIndex < m_candidates.size()) {
                auto& pair = m_candidates[m_currentCandidateIndex];
                fs::path toSortParent = fs::path(g_ToSortDir).parent_path();
                fs::path rel = fs::relative(pair.trackA_path, toSortParent);
                fs::path dst = fs::path(g_DeleteDir) / rel;
                fs::create_directories(dst.parent_path());
                LOG_INFO("[DECISION] Keeping Track B. Moving rejected Track A to delete/: " + rel.string());
                try {
                    if (fs::exists(dst)) fs::remove(dst);
                    fs::rename(pair.trackA_path, dst);
                    CleanupEmptyParentDirectories(fs::path(pair.trackA_path).parent_path(), fs::path(g_ToSortDir));
                } catch (const std::exception& ex) {
                    LOG_WARN("[DECISION] Failed moving Track A: " + std::string(ex.what()));
                }

                m_currentCandidateIndex++;
                if (m_currentCandidateIndex < m_candidates.size()) {
                    auto& next = m_candidates[m_currentCandidateIndex];
                    AudioEngine::Instance().LoadTrackA(next.trackA_path);
                    AudioEngine::Instance().LoadTrackB(next.trackB_path);
                }
            }
        };

        if (ImGui::IsKeyPressed(ImGuiKey_1, false) || ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            MakeDecisionA();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_2, false) || ImGui::IsKeyPressed(ImGuiKey_B, false)) {
            MakeDecisionB();
        }

        // Main UI Window
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("MusicSorter Workspace", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        // Header Section
        ImGui::TextDisabled("MusicSorter Studio");
        ImGui::Separator();

        // Folder Settings Panel
        if (!m_foldersInited) {
            strncpy_s(m_toSortBuf, g_ToSortDir.c_str(), sizeof(m_toSortBuf) - 1);
            strncpy_s(m_outputBuf, g_OutputDir.c_str(), sizeof(m_outputBuf) - 1);
            strncpy_s(m_flacBuf, g_FlacDir.c_str(), sizeof(m_flacBuf) - 1);
            strncpy_s(m_mp3Buf, g_Mp3Dir.c_str(), sizeof(m_mp3Buf) - 1);
            strncpy_s(m_acoustIdKeyBuf, g_AcoustIdKey.c_str(), sizeof(m_acoustIdKeyBuf) - 1);
            strncpy_s(m_discogsTokenBuf, g_DiscogsToken.c_str(), sizeof(m_discogsTokenBuf) - 1);
            m_foldersInited = true;
        }

        if (ImGui::CollapsingHeader("Папки и ключи API")) {
            ImGui::PushItemWidth(420);

            ImGui::Text("TO SORT (вход):");
            ImGui::InputText("##ToSortPath", m_toSortBuf, sizeof(m_toSortBuf));
            ImGui::SameLine();
            if (ImGui::Button("Обзор##ToSortBrowse")) {
                LaunchBrowseThread(m_hWnd, 0, "Выберите папку TO SORT");
            }

            ImGui::Text("Вывод (базовая):");
            ImGui::InputText("##OutputPath", m_outputBuf, sizeof(m_outputBuf));
            ImGui::SameLine();
            if (ImGui::Button("Обзор##OutputBrowse")) {
                LaunchBrowseThread(m_hWnd, 1, "Выберите папку вывода");
            }

            ImGui::Text("FLAC вывод:");
            ImGui::InputText("##FlacPath", m_flacBuf, sizeof(m_flacBuf));
            ImGui::SameLine();
            if (ImGui::Button("Обзор##FlacBrowse")) {
                LaunchBrowseThread(m_hWnd, 2, "Выберите папку FLAC");
            }

            ImGui::Text("MP3 вывод:");
            ImGui::InputText("##Mp3Path", m_mp3Buf, sizeof(m_mp3Buf));
            ImGui::SameLine();
            if (ImGui::Button("Обзор##Mp3Browse")) {
                LaunchBrowseThread(m_hWnd, 3, "Выберите папку MP3");
            }

            ImGui::PopItemWidth();

            ImGui::TextDisabled("AcoustID API ключ (нужен для fingerprint lookup)");
            ImGui::PushItemWidth(420);
            ImGui::InputText("##AcoustIdKey", m_acoustIdKeyBuf, sizeof(m_acoustIdKeyBuf));
            ImGui::PopItemWidth();
            if (std::string(m_acoustIdKeyBuf) == "8Xa1nV0f") {
                ImGui::SameLine();
                ImGui::TextDisabled("(демо-ключ мёртв — получи APPLICATION ключ на https://acoustid.org/new-application)");
            }

            ImGui::TextDisabled("Discogs Personal Access Token (для поиска метаданных и обложек Discogs)");
            ImGui::PushItemWidth(420);
            ImGui::InputText("##DiscogsToken", m_discogsTokenBuf, sizeof(m_discogsTokenBuf));
            ImGui::PopItemWidth();
            if (m_discogsTokenBuf[0] == '\0') {
                ImGui::SameLine();
                ImGui::TextDisabled("(токен можно получить на https://www.discogs.com/settings/developers)");
            }

            if (ImGui::Button("Сохранить настройки")) {
                g_ToSortDir = std::string(m_toSortBuf);
                g_OutputDir = std::string(m_outputBuf);
                g_FlacDir = std::string(m_flacBuf);
                g_Mp3Dir = std::string(m_mp3Buf);
                g_AcoustIdKey = std::string(m_acoustIdKeyBuf);
                g_DiscogsToken = std::string(m_discogsTokenBuf);
                SaveFolderSettings();
            }
        }

        // Sync buffers to globals every frame so all stages use live paths
        g_ToSortDir = std::string(m_toSortBuf);
        g_OutputDir = std::string(m_outputBuf);
        g_FlacDir = std::string(m_flacBuf);
        g_Mp3Dir = std::string(m_mp3Buf);
        g_DiscogsToken = std::string(m_discogsTokenBuf);
        g_DeleteDir = (fs::path(g_ToSortDir).parent_path() / "delete").string();

        ImGui::Separator();

        // Clean Non-Cringe Stage Switcher Buttons
        bool pushed0 = (m_activeStageTab == 0);
        if (pushed0) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 1.00f));
        if (ImGui::Button("1. Дубликаты", ImVec2(160, 32))) {
            m_activeStageTab = 0;
            if (!m_isScanning) {
                m_isScanning = true;
                LOG_INFO("Step 1: Running parallel AcoustID duplicate scan...");
                m_candidates.clear();
                m_autoDelete.clear();
                m_currentCandidateIndex = 0;

                std::thread([this]() {
                    AcousticAnalyzer::Instance().AnalyzeDirectory(g_ToSortDir, g_BaseDir, m_candidates, m_autoDelete);
                    m_isScanning = false;
                    PostMessageW(m_hWnd, WM_SCAN_FINISHED, 0, 0);
                }).detach();
            }
        }
        if (pushed0) ImGui::PopStyleColor();

        ImGui::SameLine();

        bool pushed1 = (m_activeStageTab == 1);
        if (pushed1) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 1.00f));
        if (ImGui::Button("2. Инспектор тегов", ImVec2(180, 32))) {
            m_activeStageTab = 1;
            if (!m_isTagScanning && m_tagItems.empty()) {
                StartTagScan();
            }
        }
        if (pushed1) ImGui::PopStyleColor();

        ImGui::SameLine();

        bool pushed2 = (m_activeStageTab == 2);
        if (pushed2) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 1.00f));
        if (ImGui::Button("3. Зеркалирование", ImVec2(170, 32))) {
            m_activeStageTab = 2;
            std::thread([]() {
                NativeMirrorCollections();
            }).detach();
        }
        if (pushed2) ImGui::PopStyleColor();

        ImGui::SameLine();

        bool pushed3 = (m_activeStageTab == 3);
        if (pushed3) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 1.00f));
        if (ImGui::Button("4. База данных & Tracklist", ImVec2(220, 32))) {
            m_activeStageTab = 3;
            std::thread([]() {
                std::string dbPath = (fs::path(g_BaseDir) / "music_database.db").string();
                DatabaseManager::GetInstance().InitDatabase(dbPath);
                DatabaseManager::GetInstance().SyncCollectionWithDisk(g_BaseDir, g_FlacDir, g_Mp3Dir, g_ToSortDir);
            }).detach();
        }
        if (pushed3) ImGui::PopStyleColor();

        if (m_isTagScanning && m_activeStageTab != 1) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Скан тегов:");
            ImGui::SameLine();
            RenderTagScanProgressBar(true);
        }

        ImGui::Separator();

        // DYNAMIC WORKFLOW INTERFACE SWITCHING
        if (m_activeStageTab == 0) {
            // Stage 1 Interface: Dual A/B Comparison Cards Layout
            float halfWidth = (ImGui::GetContentRegionAvail().x - 16.0f) * 0.5f;

            // Card A (Left)
            ImGui::BeginChild("CardA", ImVec2(halfWidth, 230), true);
            ImGui::TextDisabled("ТРЕК А (Левый)");
            if (!m_candidates.empty() && m_currentCandidateIndex < m_candidates.size()) {
                auto& pair = m_candidates[m_currentCandidateIndex];
                ImGui::TextUnformatted(fs::path(pair.trackA_path).filename().string().c_str());
                ImGui::Text("%s | %.1fs", pair.extA.c_str(), pair.durA);
                ImGui::TextDisabled("%s", pair.relA.c_str());
                ImGui::Spacing();
                if (ImGui::Button("[X] ОСТАВИТЬ ТРЕК А (Б -> delete)", ImVec2(-1, 36))) {
                    MakeDecisionA();
                }
            } else {
                ImGui::TextUnformatted("Ожидание сканирования дубликатов...");
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Card B (Right)
            ImGui::BeginChild("CardB", ImVec2(halfWidth, 230), true);
            ImGui::TextDisabled("ТРЕК Б (Правый)");
            if (!m_candidates.empty() && m_currentCandidateIndex < m_candidates.size()) {
                auto& pair = m_candidates[m_currentCandidateIndex];
                ImGui::TextUnformatted(fs::path(pair.trackB_path).filename().string().c_str());
                ImGui::Text("%s | %.1fs", pair.extB.c_str(), pair.durB);
                ImGui::TextDisabled("%s", pair.relB.c_str());
                ImGui::Spacing();
                if (ImGui::Button("[X] ОСТАВИТЬ ТРЕК Б (А -> delete)", ImVec2(-1, 36))) {
                    MakeDecisionB();
                }
            } else {
                ImGui::TextUnformatted("Ожидание сканирования дубликатов...");
            }
            ImGui::EndChild();

            // Audio Player Controls
            ImGui::BeginChild("PlayerControls", ImVec2(0, 115), true);
            if (!m_candidates.empty() && m_currentCandidateIndex < m_candidates.size()) {
                auto& pair = m_candidates[m_currentCandidateIndex];
                ImGui::Text("Сходство волн: %.1f%% | Смещение фазы: %d кадров", pair.similarity, pair.offset);
            } else {
                ImGui::TextDisabled("Сходство волн: --- % | Смещение фазы: --- кадров");
            }

            double cur = AudioEngine::Instance().GetCurrentPositionSeconds();
            double dur = AudioEngine::Instance().GetDurationSeconds();
            float seek_val = (dur > 0.0) ? (float)(cur / dur) : 0.0f;

            int cur_m = (int)cur / 60;
            int cur_s = (int)cur % 60;
            int dur_m = (int)dur / 60;
            int dur_s = (int)dur % 60;

            char time_buf[64];
            snprintf(time_buf, sizeof(time_buf), "Position: %02d:%02d / %02d:%02d", cur_m, cur_s, dur_m, dur_s);

            if (ImGui::SliderFloat("##SeekSlider", &seek_val, 0.0f, 1.0f, time_buf)) {
                AudioEngine::Instance().SeekToPercentage((double)seek_val * 100.0);
            }

            if (ImGui::Button(AudioEngine::Instance().IsPlaying() ? "[||] ПАУЗА" : "[>] ПРОИГРЫВАТЬ", ImVec2(140, 32))) {
                AudioEngine::Instance().TogglePlay();
            }
            ImGui::SameLine();

            char ch = AudioEngine::Instance().GetActiveChannel();
            if (ch == 'a') {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.00f, 0.00f, 1.00f));
            }
            if (ImGui::Button("ТРЕК А [1]", ImVec2(140, 32))) {
                AudioEngine::Instance().SetActiveChannel('a');
            }
            if (ch == 'a') ImGui::PopStyleColor(2);

            ImGui::SameLine();
            if (ch == 'b') {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.00f, 0.00f, 1.00f));
            }
            if (ImGui::Button("ТРЕК Б [2]", ImVec2(140, 32))) {
                AudioEngine::Instance().SetActiveChannel('b');
            }
            if (ch == 'b') ImGui::PopStyleColor(2);

            ImGui::SameLine();
            ImGui::SetNextItemWidth(160);
            float masterVolPercent = AudioEngine::Instance().GetMasterVolume() * 100.0f;
            if (ImGui::SliderFloat("##MasterVolSlider", &masterVolPercent, 0.0f, 100.0f, "Vol: %.0f%%")) {
                AudioEngine::Instance().SetMasterVolume(masterVolPercent / 100.0f);
            }

            ImGui::SameLine();
            ImGui::TextDisabled("Hotkeys: Tab / S (Hot-Swap) | Space (Play) | 1/2 (Keep)");
            ImGui::EndChild();
        } else if (m_activeStageTab == 1) {
            // Stage 2: PERFECT SIDE-BY-SIDE COVER ART COMPARISON IN CENTER!
            if (!m_tagItems.empty() && m_currentTagIndex < m_tagItems.size()) {
                auto& item = m_tagItems[m_currentTagIndex];
                
                float availY = ImGui::GetContentRegionAvail().y;
                float inspectorH = availY - 320.0f; // Give 320px to Logs console panel!
                if (inspectorH < 400.0f) inspectorH = 400.0f;

                ImGui::BeginChild("TagInspectorCardPerfectFit", ImVec2(0, inspectorH), true, ImGuiWindowFlags_NoScrollbar);
                ImGui::TextDisabled("Инспектор тегов (%zu из %zu)", m_currentTagIndex + 1, m_tagItems.size());
                ImGui::SameLine();
                if (m_isTagScanning && !item.isFetchCompleted) {
                    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "[ПОИСК ОНЛАЙН %zu/%zu...]", m_fetchedCount.load(), m_tagItems.size());
                } else if (item.isMusicBrainzMatched) {
                    ImGui::TextColored(GetTierColor(item.matchTier), "[%s]", GetTierName(item.matchTier));
                } else {
                    ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.3f, 1.0f), "[NICHE / LOCAL]");
                }

                ImGui::SameLine();
                ImGui::TextDisabled("| Файл: %s", item.originalFilename.c_str());
                ImGui::Separator();

                // Progress Bar and ETA for Tag Scanner
                RenderTagScanProgressBar(false);
                ImGui::Spacing();

                // Manual MusicBrainz / TouhouDB / VocaDB / UtaiteDB / Discogs URL / ID Input Row
                ImGui::PushItemWidth(360);
                ImGui::InputTextWithHint("##ManualMbUrlInput", "Ссылка MB / TouhouDB / VocaDB / UtaiteDB / Discogs или ID", m_manualMbUrlBuf, sizeof(m_manualMbUrlBuf));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Загрузить метаданные", ImVec2(185, 24))) {
                    if (strlen(m_manualMbUrlBuf) > 0) {
                        std::string inputStr(m_manualMbUrlBuf);
                        std::string inputLower = inputStr;
                        std::transform(inputLower.begin(), inputLower.end(), inputLower.begin(), ::tolower);

                        if (inputLower.find("touhoudb.com") != std::string::npos || inputLower.find("touhoudb:") == 0 || inputLower.find("tdb:") == 0) {
                            FetchManualTouhouDbMetadata(m_manualMbUrlBuf, m_manualMbApplyToAlbum);
                        } else if (inputLower.find("vocadb.net") != std::string::npos || inputLower.find("vocadb:") == 0 || inputLower.find("vdb:") == 0) {
                            FetchManualVocaDbMetadata(m_manualMbUrlBuf, m_manualMbApplyToAlbum);
                        } else if (inputLower.find("utaitedb.net") != std::string::npos || inputLower.find("utaitedb:") == 0 || inputLower.find("udb:") == 0) {
                            FetchManualUtaiteDbMetadata(m_manualMbUrlBuf, m_manualMbApplyToAlbum);
                        } else if (inputLower.find("discogs.com") != std::string::npos || inputLower.find("r") == 0 || inputLower.find("m") == 0) {
                            FetchManualDiscogsMetadata(m_manualMbUrlBuf, m_manualMbApplyToAlbum);
                        } else {
                            FetchManualMusicBrainzMetadata(m_manualMbUrlBuf, m_manualMbApplyToAlbum);
                        }
                    }
                }
                ImGui::SameLine();
                ImGui::Checkbox("Ко всему альбому", &m_manualMbApplyToAlbum);
                ImGui::Separator();

                // Main 3-Column Layout: Left Column (Col 0) = Stacked Original & Proposed Tags | Middle (Col 1) = Local Cover | Right (Col 2) = Online Cover
                float inspectorTopY = ImGui::GetCursorPosY();
                ImGui::Columns(3, "Main3InspectorColumnsFixedTop", false);
                ImGui::SetColumnWidth(0, 320.0f);
                ImGui::SetColumnWidth(1, 265.0f);
                ImGui::SetColumnWidth(2, 265.0f);

                // ==================== COLUMN 0 (LEFT: BOTH TAG BLOCKS STACKED) ====================
                ImGui::SetCursorPosY(inspectorTopY);
                ImGui::BeginGroup();

                // 1. ВЕРХНИЙ БЛОК: ИСХОДНЫЕ ТЕГИ
                ImGui::TextDisabled("Исходные теги в файле");
                ImGui::PushItemWidth(180);

                char origArtist[256], origAlbum[256], origTitle[256], origTrack[32], origYear[32];
                strncpy_s(origArtist, item.embeddedArtist.c_str(), sizeof(origArtist) - 1);
                strncpy_s(origAlbum, item.embeddedAlbum.c_str(), sizeof(origAlbum) - 1);
                strncpy_s(origTitle, item.embeddedTitle.c_str(), sizeof(origTitle) - 1);
                strncpy_s(origTrack, item.embeddedTrackNo.c_str(), sizeof(origTrack) - 1);
                strncpy_s(origYear, item.embeddedYear.c_str(), sizeof(origYear) - 1);

                ImGui::InputText("Исполнитель##Orig", origArtist, sizeof(origArtist), ImGuiInputTextFlags_ReadOnly);
                ImGui::InputText("Альбом##Orig", origAlbum, sizeof(origAlbum), ImGuiInputTextFlags_ReadOnly);
                ImGui::InputText("Название##Orig", origTitle, sizeof(origTitle), ImGuiInputTextFlags_ReadOnly);
                ImGui::PopItemWidth();

                ImGui::PushItemWidth(60);
                ImGui::InputText("№##Orig", origTrack, sizeof(origTrack), ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                ImGui::PushItemWidth(90);
                ImGui::InputText("Год/Дата##Orig", origYear, sizeof(origYear), ImGuiInputTextFlags_ReadOnly);
                ImGui::PopItemWidth();
                ImGui::PopItemWidth();

                ImGui::Dummy(ImVec2(0, 8.0f));

                // 2. НИЖНИЙ БЛОК: ПРЕДЛАГАЕМЫЕ ТЕГИ
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.9f, 1.0f), "Предлагаемые теги");
                ImGui::SameLine();

                // Language Switcher Buttons: [RO] [EN] [JP]
                auto langAlbIndices = GetAlbumTrackIndices(m_currentTagIndex);
                auto switchLang = [&](const std::string& langCode) {
                    for (size_t aIdx : langAlbIndices) {
                        if (aIdx < m_tagItems.size()) {
                            auto& itm = m_tagItems[aIdx];
                            if (langCode == "RO") {
                                std::string a = itm.artistRomaji.empty() ? itm.artistEnglish : itm.artistRomaji;
                                if (!a.empty()) strncpy_s(itm.artistBuf, a.c_str(), sizeof(itm.artistBuf) - 1);
                                std::string alb = itm.albumRomaji.empty() ? itm.albumEnglish : itm.albumRomaji;
                                if (!alb.empty()) strncpy_s(itm.albumBuf, alb.c_str(), sizeof(itm.albumBuf) - 1);
                                std::string t = itm.titleRomaji.empty() ? itm.titleEnglish : itm.titleRomaji;
                                if (!t.empty()) strncpy_s(itm.titleBuf, t.c_str(), sizeof(itm.titleBuf) - 1);
                            } else if (langCode == "EN") {
                                std::string a = itm.artistEnglish.empty() ? itm.artistRomaji : itm.artistEnglish;
                                if (!a.empty()) strncpy_s(itm.artistBuf, a.c_str(), sizeof(itm.artistBuf) - 1);
                                std::string alb = itm.albumEnglish.empty() ? itm.albumRomaji : itm.albumEnglish;
                                if (!alb.empty()) strncpy_s(itm.albumBuf, alb.c_str(), sizeof(itm.albumBuf) - 1);
                                std::string t = itm.titleEnglish.empty() ? itm.titleRomaji : itm.titleEnglish;
                                if (!t.empty()) strncpy_s(itm.titleBuf, t.c_str(), sizeof(itm.titleBuf) - 1);
                            } else if (langCode == "JP") {
                                std::string a = itm.artistJapanese.empty() ? itm.artistRomaji : itm.artistJapanese;
                                if (!a.empty()) strncpy_s(itm.artistBuf, a.c_str(), sizeof(itm.artistBuf) - 1);
                                std::string alb = itm.albumJapanese.empty() ? itm.albumRomaji : itm.albumJapanese;
                                if (!alb.empty()) strncpy_s(itm.albumBuf, alb.c_str(), sizeof(itm.albumBuf) - 1);
                                std::string t = itm.titleJapanese.empty() ? itm.titleRomaji : itm.titleJapanese;
                                if (!t.empty()) strncpy_s(itm.titleBuf, t.c_str(), sizeof(itm.titleBuf) - 1);
                            }
                        }
                    }
                    LOG_INFO("[LANG SWITCH] Switched tags to " + langCode + " for " + std::to_string(langAlbIndices.size()) + " tracks in album.");
                };

                if (ImGui::SmallButton("RO##LangRO")) {
                    switchLang("RO");
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Переключить весь альбом на Ромадзи (Romaji)");

                ImGui::SameLine();
                if (ImGui::SmallButton("EN##LangEN")) {
                    switchLang("EN");
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Переключить весь альбом на Английский (English)");

                ImGui::SameLine();
                if (ImGui::SmallButton("JP##LangJP")) {
                    switchLang("JP");
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Переключить весь альбом на Японский / Оригинал (日本語)");

                ImGui::PushItemWidth(140);
                ImGui::InputText("Исполнитель##New", item.artistBuf, sizeof(item.artistBuf));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::SmallButton(">> Альбом##ApplyArtist")) {
                    auto albIndices = GetAlbumTrackIndices(m_currentTagIndex);
                    for (size_t aIdx : albIndices) {
                        if (aIdx < m_tagItems.size()) {
                            strncpy_s(m_tagItems[aIdx].artistBuf, item.artistBuf, sizeof(m_tagItems[aIdx].artistBuf) - 1);
                        }
                    }
                    LOG_INFO("[TAG SYNC] Applied artist '" + std::string(item.artistBuf) + "' to " + std::to_string(albIndices.size()) + " tracks in album.");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Скопировать этого исполнителя во все треки текущего альбома");
                }

                ImGui::PushItemWidth(140);
                ImGui::InputText("Альбом##New", item.albumBuf, sizeof(item.albumBuf));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::SmallButton(">> Альбом##ApplyAlbum")) {
                    auto albIndices = GetAlbumTrackIndices(m_currentTagIndex);
                    for (size_t aIdx : albIndices) {
                        if (aIdx < m_tagItems.size()) {
                            strncpy_s(m_tagItems[aIdx].albumBuf, item.albumBuf, sizeof(m_tagItems[aIdx].albumBuf) - 1);
                        }
                    }
                    LOG_INFO("[TAG SYNC] Applied album title '" + std::string(item.albumBuf) + "' to " + std::to_string(albIndices.size()) + " tracks in album.");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Скопировать название альбома во все треки текущего альбома");
                }

                ImGui::PushItemWidth(180);
                ImGui::InputText("Название##New", item.titleBuf, sizeof(item.titleBuf));
                ImGui::PopItemWidth();

                ImGui::PushItemWidth(50);
                ImGui::InputText("№##New", item.trackNoBuf, sizeof(item.trackNoBuf));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ImGui::PushItemWidth(70);
                ImGui::InputText("Год##New", item.yearBuf, sizeof(item.yearBuf));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::SmallButton(">> Альбом##ApplyYear")) {
                    auto albIndices = GetAlbumTrackIndices(m_currentTagIndex);
                    for (size_t aIdx : albIndices) {
                        if (aIdx < m_tagItems.size()) {
                            strncpy_s(m_tagItems[aIdx].yearBuf, item.yearBuf, sizeof(m_tagItems[aIdx].yearBuf) - 1);
                        }
                    }
                    LOG_INFO("[TAG SYNC] Applied year '" + std::string(item.yearBuf) + "' to " + std::to_string(albIndices.size()) + " tracks in album.");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Скопировать год/дату во все треки текущего альбома");
                }

                ImGui::EndGroup();

                // ==================== COLUMN 1 (MIDDLE: LOCAL COVER ART) ====================
                ImGui::NextColumn();
                ImGui::SetCursorPosY(inspectorTopY);
                ImGui::BeginGroup();
                ImGui::TextDisabled("Локальная обложка:");
                if (item.localTexture) {
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
                    if (ImGui::ImageButton("##LocalCoverBtnLeftLarge", (ImTextureID)item.localTexture, ImVec2(225, 225))) {
                        item.selectedCoverChoice = 0;
                    }
                    ImGui::PopStyleVar();
                    ImGui::Text(item.selectedCoverChoice == 0 ? "[X] Локальный скан" : "   Локальный скан");
                    ImGui::TextDisabled("%dx%d px | %zu KB", item.localWidth, item.localHeight, item.localCoverBytes.size() / 1024);
                    if (item.localScore >= item.onlineScore) {
                        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "[*] ВЫСШЕЕ КАЧЕСТВО");
                    } else if (item.localWidth >= 1000 && item.localScore < item.onlineScore / 3) {
                        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "[!] ФАЛЬШИВЫЙ АПСКЕЙЛ");
                    }
                } else {
                    ImGui::TextDisabled("[Обложка отсутствует]");
                }
                ImGui::EndGroup();

                // ==================== COLUMN 2 (RIGHT: ONLINE COVER ART) ====================
                ImGui::NextColumn();
                ImGui::SetCursorPosY(inspectorTopY);
                ImGui::BeginGroup();
                std::string coverSourceTitle = item.onlineCoverSource.empty() ? "Онлайн обложка:" : (item.onlineCoverSource + ":");
                ImGui::TextDisabled("%s", coverSourceTitle.c_str());
                if (item.onlineTexture) {
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
                    if (ImGui::ImageButton("##OnlineCoverBtnRightLarge", (ImTextureID)item.onlineTexture, ImVec2(225, 225))) {
                        item.selectedCoverChoice = 1;
                    }
                    ImGui::PopStyleVar();
                    std::string coverChoiceLabel = (item.selectedCoverChoice == 1 ? "[X] " : "   ") + (item.onlineCoverSource.empty() ? "Онлайн" : item.onlineCoverSource);
                    ImGui::Text("%s", coverChoiceLabel.c_str());
                    ImGui::TextDisabled("%dx%d px | %zu KB", item.onlineWidth, item.onlineHeight, item.onlineCoverBytes.size() / 1024);
                    if (item.onlineScore > item.localScore) {
                        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "[*] ВЫСШЕЕ КАЧЕСТВО");
                    } else if (item.onlineWidth >= 1000 && item.onlineScore < item.localScore / 3) {
                        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "[!] ФАЛЬШИВЫЙ АПСКЕЙЛ");
                    }
                } else if (m_isTagScanning && !item.isFetchCompleted) {
                    ImGui::TextDisabled("[Загрузка обложки...]");
                } else {
                    ImGui::TextDisabled("[Обложка отсутствует]");
                }
                ImGui::EndGroup();

                ImGui::Columns(1); // Reset main split
                ImGui::Separator();

                // Compact Secluded Lyrics Section at bottom
                if (ImGui::TreeNode("Текст песни (LRC / Romaji)")) {
                    ImGui::InputTextMultiline("##LyricsMultiLineSafe", item.lyricsBuf, sizeof(item.lyricsBuf), ImVec2(-1, 60));
                    ImGui::TreePop();
                }

                ImGui::Spacing();

                auto albumIndices = GetAlbumTrackIndices(m_currentTagIndex);
                size_t albumCount = albumIndices.size();
                bool isTierA = (item.matchTier == MatchTier::TierA || item.matchTier == MatchTier::AcoustId);

                if (ImGui::Button("Принять трек", ImVec2(130, 36))) {
                    ApproveTracks({ m_currentTagIndex });
                }

                if (albumCount > 1) {
                    ImGui::SameLine();
                    if (isTierA) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.65f, 0.32f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.78f, 0.38f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.55f, 0.26f, 1.0f));
                    }
                    char albumBtnLabel[64];
                    sprintf_s(albumBtnLabel, sizeof(albumBtnLabel), "Принять весь альбом (%zu)%s", albumCount, isTierA ? " [Tier A]" : "");
                    if (ImGui::Button(albumBtnLabel, ImVec2(isTierA ? 250.0f : 220.0f, 36))) {
                        ApproveTracks(albumIndices);
                    }
                    if (isTierA) {
                        ImGui::PopStyleColor(3);
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("Пропустить трек", ImVec2(140, 36))) {
                    SkipTracks({ m_currentTagIndex });
                }

                if (albumCount > 1) {
                    ImGui::SameLine();
                    if (ImGui::Button("Пропустить альбом", ImVec2(150, 36))) {
                        SkipTracks(albumIndices);
                    }
                }
                ImGui::EndChild();
            } else {
                ImGui::BeginChild("TagInspectorCardEmpty", ImVec2(0, 180), true);
                if (m_isTagScanning) {
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Идет сканирование и поиск метаданных в MusicBrainz / Discogs...");
                    ImGui::Spacing();
                    RenderTagScanProgressBar(false);
                } else if (!m_tagItems.empty() && m_currentTagIndex >= m_tagItems.size()) {
                    ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Все треки в очереди успешно обработаны!");
                    ImGui::Spacing();
                    RenderTagScanProgressBar(false);
                    ImGui::Spacing();
                    if (ImGui::Button("Пересканировать папку TO SORT", ImVec2(250, 36))) {
                        StartTagScan();
                    }
                } else {
                    ImGui::TextUnformatted("Папка TO SORT не содержит необработанных аудиофайлов или сканирование еще не запускалось.");
                    ImGui::Spacing();
                    if (ImGui::Button("Запустить сканирование тегов", ImVec2(250, 36))) {
                        StartTagScan();
                    }
                }
                ImGui::EndChild();
            }
        } else if (m_activeStageTab == 2) {
            ImGui::BeginChild("Stage3Child", ImVec2(0, 150), true);
            ImGui::TextUnformatted("Этап 3: Нативный C++20 поиск и зеркалирование папок FLAC и MP3 активен.");
            ImGui::EndChild();
        } else if (m_activeStageTab == 3) {
            ImGui::BeginChild("Stage4DBChild", ImVec2(0, 270), true);
            
            // Auto-initialize DB on first view
            std::string dbPath = (fs::path(g_BaseDir) / "music_database.db").string();
            DatabaseManager::GetInstance().InitDatabase(dbPath);

            int totalCount = DatabaseManager::GetInstance().GetTotalTracksCount();
            int dlCount = DatabaseManager::GetInstance().GetDownloadedCount();
            int missCount = DatabaseManager::GetInstance().GetMissingCount();

            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "SQLite База данных коллекции (music_database.db)");
            ImGui::SameLine();
            ImGui::TextDisabled("|  Всего треков: %d  |  Скачано [x]: %d  |  Ожидают [ ]: %d", totalCount, dlCount, missCount);

            ImGui::Separator();

            // Controls Row: Search & Filters & Actions
            static char searchBuf[128] = "";
            static int filterStatus = 0; // 0 = Все, 1 = [x] Скачано, 2 = [ ] Ожидают
            static int filterFormat = 0; // 0 = Все, 1 = FLAC, 2 = MP3

            ImGui::PushItemWidth(200);
            ImGui::InputText("Поиск##DBSearch", searchBuf, sizeof(searchBuf));
            ImGui::PopItemWidth();

            ImGui::SameLine();
            if (ImGui::Button(filterStatus == 0 ? "[Все статусы]" : (filterStatus == 1 ? "[x] Скачано" : "[ ] Ожидают"))) {
                filterStatus = (filterStatus + 1) % 3;
            }

            ImGui::SameLine();
            if (ImGui::Button(filterFormat == 0 ? "[Все форматы]" : (filterFormat == 1 ? "FLAC" : "MP3"))) {
                filterFormat = (filterFormat + 1) % 3;
            }

            ImGui::SameLine();
            if (ImGui::Button("🔄 Скан диска")) {
                std::thread([]() {
                    std::string dbPath = (fs::path(g_BaseDir) / "music_database.db").string();
                    DatabaseManager::GetInstance().InitDatabase(dbPath);
                    DatabaseManager::GetInstance().SyncCollectionWithDisk(g_BaseDir, g_FlacDir, g_Mp3Dir, g_ToSortDir);
                }).detach();
            }

            ImGui::SameLine();
            if (ImGui::Button("📄 Экспорт в tracklist.md")) {
                std::thread([]() {
                    std::string tracklistPath = (fs::path(g_BaseDir) / "tracklist.md").string();
                    DatabaseManager::GetInstance().ExportToCleanTracklistMarkdown(tracklistPath);
                }).detach();
            }

            ImGui::Separator();

            // Database ImGui Table
            std::vector<TrackRecord> records = DatabaseManager::GetInstance().QueryTracks(filterStatus, filterFormat, searchBuf);

            if (ImGui::BeginTable("DBTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0, 160))) {
                ImGui::TableSetupColumn("Статус", ImGuiTableColumnFlags_WidthFixed, 55.0f);
                ImGui::TableSetupColumn("Исполнитель", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                ImGui::TableSetupColumn("Альбом", ImGuiTableColumnFlags_WidthFixed, 200.0f);
                ImGui::TableSetupColumn("Название трека", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Формат", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Путь на диске", ImGuiTableColumnFlags_WidthFixed, 220.0f);
                ImGui::TableHeadersRow();

                for (const auto& rec : records) {
                    ImGui::TableNextRow();
                    
                    // Status
                    ImGui::TableSetColumnIndex(0);
                    if (rec.status == 1) {
                        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "[x]");
                    } else {
                        ImGui::TextDisabled("[ ]");
                    }

                    // Artist
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(rec.artist.c_str());

                    // Album
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(rec.album.c_str());

                    // Title
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(rec.title.c_str());

                    // Format
                    ImGui::TableSetColumnIndex(4);
                    if (rec.format == "FLAC") {
                        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "FLAC");
                    } else if (rec.format == "MP3") {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "MP3 320k");
                    } else {
                        ImGui::TextDisabled("—");
                    }

                    // Path
                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextDisabled("%s", rec.relPath.c_str());
                }

                ImGui::EndTable();
            }

            ImGui::EndChild();
        }

        // Clean Syntax-Highlighted & Click-to-Copy Logs Panel with Left-Aligned 'Скопировать' Button
        ImGui::BeginChild("LogConsoleHeader", ImVec2(0, 0), true);
        ImGui::TextDisabled("Logs:");
        ImGui::SameLine();

        auto logs = Logger::Instance().GetLogs();

        // Clean Compact 'Скопировать' Button Placed Immediately on the Left
        if (ImGui::Button("Скопировать", ImVec2(110, 24))) {
            std::string full_logs;
            for (const auto& log : logs) {
                full_logs += log + "\n";
            }
            CopyToClipboardWin32(full_logs);
            LOG_INFO("[CLIPBOARD] All console logs copied to Windows clipboard!");
        }

        ImGui::SameLine();
        if (ImGui::Button("Сохранить в файл", ImVec2(140, 24))) {
            auto now = std::chrono::system_clock::now();
            std::time_t t = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
            localtime_s(&tm, &t);
            char stamp[32];
            std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);
            fs::path logPath = fs::path(g_BaseDir) / ("logs_" + std::string(stamp) + ".txt");
            std::ofstream fOut(logPath);
            if (fOut.is_open()) {
                for (const auto& log : logs) {
                    fOut << log << "\n";
                }
                fOut.close();
                LOG_INFO("[LOGS SAVED] Written to: " + logPath.string());
            } else {
                LOG_WARN("[LOGS SAVE ERROR] Could not write: " + logPath.string());
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Сводная таблица релизов", ImVec2(200, 24))) {
            OpenSummaryWindow();
        }

        ImGui::Separator();

        ImGui::BeginChild("LogListRegion", ImVec2(0, 0), false, ImGuiWindowFlags_None);

        bool isAtBottom = (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f);

        for (size_t i = 0; i < logs.size(); ++i) {
            const std::string& logLine = logs[i];
            ImGui::PushID((int)i);

            ImVec4 textColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
            if (logLine.find("[MUSICBRAINZ MATCHED]") != std::string::npos || logLine.find("[ACOUSTID MATCHED]") != std::string::npos || logLine.find("[MUSICBRAINZ TIER A SUCCESS]") != std::string::npos || logLine.find("[MUSICBRAINZ TIER B SUCCESS]") != std::string::npos || logLine.find("[MUSICBRAINZ TIER C SUCCESS]") != std::string::npos || logLine.find("[TAGS EMBEDDED]") != std::string::npos) {
                textColor = ImVec4(0.2f, 1.0f, 0.4f, 1.0f);
            } else if (logLine.find("[COVER ART DOWNLOADED]") != std::string::npos || logLine.find("[COVER SAVED]") != std::string::npos) {
                textColor = ImVec4(1.0f, 0.4f, 0.8f, 1.0f);
            } else if (logLine.find("[MUSICBRAINZ FETCH") != std::string::npos || logLine.find("[MUSICBRAINZ TIER") != std::string::npos || logLine.find("[ACOUSTID FINGERPRINT]") != std::string::npos) {
                textColor = ImVec4(0.9f, 0.8f, 0.2f, 1.0f);
            } else if (logLine.find("[AUTO-DELETE]") != std::string::npos || logLine.find("[DECISION]") != std::string::npos || logLine.find("[HTTP 429") != std::string::npos || logLine.find("[HTTP 503") != std::string::npos || logLine.find("[HTTP ERROR") != std::string::npos || logLine.find("[COVER ART MISSING]") != std::string::npos || logLine.find("[SKIPPED]") != std::string::npos) {
                textColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            } else if (logLine.find("[LRC LYRICS FETCHED]") != std::string::npos) {
                textColor = ImVec4(0.3f, 0.8f, 1.0f, 1.0f);
            } else if (logLine.find("[TAGS APPLIED") != std::string::npos || logLine.find("[CLIPBOARD]") != std::string::npos || logLine.find("[ALBUM CACHE HIT]") != std::string::npos) {
                textColor = ImVec4(0.4f, 0.9f, 1.0f, 1.0f);
            }

            ImGui::PushStyleColor(ImGuiCol_Text, textColor);
            // Interactive Selectable Line with Text Wrapped and Click-to-Copy!
            ImGui::TextWrapped("%s", logLine.c_str());
            if (ImGui::IsItemClicked()) {
                CopyToClipboardWin32(logLine);
                LOG_INFO("[CLIPBOARD] Copied line to clipboard: " + logLine);
            }
            ImGui::PopStyleColor();

            ImGui::PopID();
        }

        static size_t last_log_size = 0;
        if (isAtBottom && logs.size() != last_log_size) {
            ImGui::SetScrollHereY(1.0f);
        }
        last_log_size = logs.size();

        ImGui::EndChild(); // End LogListRegion
        ImGui::EndChild(); // End LogConsoleHeader

        ImGui::End(); // End MusicSorter Workspace

        // Rendering Main Window Frame
        ImGui::SetCurrentContext(m_mainImGuiContext);
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.07f, 0.07f, 0.07f, 1.00f };
        m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, NULL);
        m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        m_pSwapChain->Present(1, 0); // Present with vsync 60 FPS

        // Rendering Secondary Native Window if open
        if (m_hSummaryWnd != NULL && IsWindow(m_hSummaryWnd) && m_summaryImGuiContext != NULL && m_summaryRenderTargetView != NULL) {
            RECT rc;
            GetClientRect(m_hSummaryWnd, &rc);
            float w = (float)(rc.right - rc.left);
            float h = (float)(rc.bottom - rc.top);
            if (w > 0 && h > 0) {
                ImGui::SetCurrentContext(m_summaryImGuiContext);
                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();

                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImVec2(w, h));
                ImGui::Begin("##SummaryNativeWindowCanvas", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
                RenderReleaseSummaryTable();
                ImGui::End();

                ImGui::Render();
                m_pd3dDeviceContext->OMSetRenderTargets(1, &m_summaryRenderTargetView, NULL);
                m_pd3dDeviceContext->ClearRenderTargetView(m_summaryRenderTargetView, clear_color_with_alpha);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                m_pSummarySwapChain->Present(1, 0);

                ImGui::SetCurrentContext(m_mainImGuiContext);
            }
        }

        Sleep(1); // Yield CPU to OS scheduler to keep IDLE CPU usage at 0.0%!
    }
}

LRESULT CALLBACK AppWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (Instance().m_mainImGuiContext) {
        ImGuiContext* prevCtx = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(Instance().m_mainImGuiContext);
        LRESULT res = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        ImGui::SetCurrentContext(prevCtx);
        if (res) return true;
    }

    switch (msg) {
    case WM_SCAN_FINISHED:
        Instance().HandleScanFinished();
        return 0;
    case WM_TAG_SCAN_FINISHED:
        Instance().HandleTagScanFinished();
        return 0;
    case WM_BROWSE_RESULT: {
        int target = (int)wParam;
        std::string* pResult = (std::string*)lParam;
        auto& app = Instance();
        std::string result = pResult ? *pResult : "";
        delete pResult;
        switch (target) {
            case 0:
                strncpy_s(app.m_toSortBuf, result.c_str(), sizeof(app.m_toSortBuf) - 1);
                break;
            case 1:
                strncpy_s(app.m_outputBuf, result.c_str(), sizeof(app.m_outputBuf) - 1);
                strncpy_s(app.m_flacBuf, (fs::path(result) / "flac").string().c_str(), sizeof(app.m_flacBuf) - 1);
                strncpy_s(app.m_mp3Buf, (fs::path(result) / "mp3").string().c_str(), sizeof(app.m_mp3Buf) - 1);
                break;
            case 2:
                strncpy_s(app.m_flacBuf, result.c_str(), sizeof(app.m_flacBuf) - 1);
                break;
            case 3:
                strncpy_s(app.m_mp3Buf, result.c_str(), sizeof(app.m_mp3Buf) - 1);
                break;
        }
        return 0;
    }
    case WM_SIZE:
        if (Instance().m_pd3dDevice != NULL && wParam != SIZE_MINIMIZED) {
            Instance().CleanupRenderTarget();
            Instance().m_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            Instance().CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void AppWindow::HandleScanFinished() {
    LOG_INFO("Scan process finished. Found " + std::to_string(m_candidates.size()) + " candidates for A/B comparison.");

    // Deduplicate auto-delete list (same file may be matched against multiple others)
    std::unordered_set<std::string> seen;
    std::vector<std::string> uniqueAutoDelete;
    for (const auto& pathStr : m_autoDelete) {
        if (seen.insert(pathStr).second) {
            uniqueAutoDelete.push_back(pathStr);
        }
    }

    fs::path toSortParent = fs::path(g_ToSortDir).parent_path();

    for (const std::string& pathStr : uniqueAutoDelete) {
        fs::path p(pathStr);
        if (fs::exists(p)) {
            fs::path rel = fs::relative(p, toSortParent);
            fs::path dst = fs::path(g_DeleteDir) / rel;

            // Safety: never move a file onto itself
            std::error_code ec;
            if (fs::weakly_canonical(p, ec) == fs::weakly_canonical(dst, ec)) {
                LOG_WARN("[AUTO-DELETE] Skipping self-move: " + rel.string());
                continue;
            }

            fs::create_directories(dst.parent_path());
            LOG_INFO("[AUTO-DELETE] Moving exact duplicate to delete/: " + rel.string());
            try {
                if (fs::exists(dst)) fs::remove(dst);
                fs::rename(p, dst);
                CleanupEmptyParentDirectories(p.parent_path(), fs::path(g_ToSortDir));
            } catch (const std::exception& ex) {
                LOG_WARN("[AUTO-DELETE] Failed moving " + rel.string() + ": " + ex.what());
            }
        }
    }

    CleanupOrphanToSortFolders(fs::path(g_ToSortDir));

    if (!m_candidates.empty()) {
        m_currentCandidateIndex = 0;
        auto& pair = m_candidates[0];
        AudioEngine::Instance().LoadTrackA(pair.trackA_path);
        AudioEngine::Instance().LoadTrackB(pair.trackB_path);
    }
}

void AppWindow::HandleTagScanFinished() {
    LOG_INFO("Step 2 Tagging & Cover Art inspection initialized. Loaded " + std::to_string(m_tagItems.size()) + " items into Inspector.");

    // Textures are now created lazily in the render loop for the current item only

    if (!m_tagItems.empty()) {
        m_currentTagIndex = 0;
    }
}


void AppWindow::RenderTagScanProgressBar(bool compact) {
    size_t total = m_tagScanTotal.load();
    if (total == 0 && !m_tagItems.empty()) total = m_tagItems.size();
    size_t done = m_fetchedCount.load();
    if (done > total && total > 0) done = total;

    float fraction = (total > 0) ? ((float)done / (float)total) : 0.0f;

    auto now = std::chrono::steady_clock::now();
    auto effectiveEndTime = (m_isTagScanning || m_tagScanEndTime.time_since_epoch().count() == 0) ? now : m_tagScanEndTime;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(effectiveEndTime - m_tagScanStartTime).count();
    double elapsedSec = (elapsedMs > 0 && m_tagScanStartTime.time_since_epoch().count() > 0) ? (elapsedMs / 1000.0) : 0.0;

    int elapsedM = (int)elapsedSec / 60;
    int elapsedS = (int)elapsedSec % 60;
    char elapsedBuf[32];
    snprintf(elapsedBuf, sizeof(elapsedBuf), "%02d:%02d", elapsedM, elapsedS);

    char etaBuf[32];
    if (done == 0 || elapsedSec < 0.5) {
        snprintf(etaBuf, sizeof(etaBuf), "Расчёт...");
    } else if (done >= total && total > 0) {
        snprintf(etaBuf, sizeof(etaBuf), "00:00");
    } else {
        double speed = (double)done / elapsedSec;
        if (speed > 0.0001) {
            int etaSec = (int)(((double)(total - done)) / speed);
            if (etaSec < 0) etaSec = 0;
            int etaH = etaSec / 3600;
            int etaM = (etaSec % 3600) / 60;
            int etaS = etaSec % 60;
            if (etaH > 0) {
                snprintf(etaBuf, sizeof(etaBuf), "%02d:%02d:%02d", etaH, etaM, etaS);
            } else {
                snprintf(etaBuf, sizeof(etaBuf), "%02d:%02d", etaM, etaS);
            }
        } else {
            snprintf(etaBuf, sizeof(etaBuf), "Расчёт...");
        }
    }

    char progressOverlay[160];
    if (compact) {
        if (m_isTagScanning) {
            snprintf(progressOverlay, sizeof(progressOverlay), "%zu/%zu (%.0f%%) | ETA: %s", done, total, fraction * 100.0f, etaBuf);
        } else if (total > 0 && done >= total) {
            snprintf(progressOverlay, sizeof(progressOverlay), "100%% (%zu/%zu)", total, total);
        } else {
            snprintf(progressOverlay, sizeof(progressOverlay), "%zu/%zu", done, total);
        }
    } else {
        if (m_isTagScanning) {
            double speed = (elapsedSec > 0.5 && done > 0) ? ((double)done / elapsedSec) : 0.0;
            if (speed > 0.05) {
                snprintf(progressOverlay, sizeof(progressOverlay), "%zu / %zu (%.0f%%)  |  Прошло: %s  |  ETA: %s  (%.1f тр/сек)",
                         done, total, fraction * 100.0f, elapsedBuf, etaBuf, speed);
            } else {
                snprintf(progressOverlay, sizeof(progressOverlay), "%zu / %zu (%.0f%%)  |  Прошло: %s  |  ETA: %s",
                         done, total, fraction * 100.0f, elapsedBuf, etaBuf);
            }
        } else if (total > 0 && done >= total) {
            snprintf(progressOverlay, sizeof(progressOverlay), "Сканирование завершено: %zu / %zu (100%%)  |  Затрачено: %s",
                     total, total, elapsedBuf);
        } else if (total > 0) {
            snprintf(progressOverlay, sizeof(progressOverlay), "%zu / %zu (%.0f%%)", done, total, fraction * 100.0f);
        } else {
            snprintf(progressOverlay, sizeof(progressOverlay), "0 / 0 (0%%)");
        }
    }

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.20f, 0.65f, 0.95f, 1.0f));
    ImGui::ProgressBar(fraction, compact ? ImVec2(240, 22.0f) : ImVec2(-1, 22.0f), progressOverlay);
    ImGui::PopStyleColor();
}

void AppWindow::StartTagScan() {
    if (m_isTagScanning) return;
    m_isTagScanning = true;
    LOG_INFO("Step 2: High-speed parallel fingerprinting & async metadata resolution...");
    m_tagItems.clear();
    m_currentTagIndex = 0;
    m_fetchedCount = 0;
    m_tagScanTotal = 0;
    m_tagScanStartTime = std::chrono::steady_clock::now();
    m_tagScanEndTime = {};

    std::thread([this]() {
        std::vector<std::string> files;
        if (fs::exists(g_ToSortDir)) {
            for (auto& p : fs::recursive_directory_iterator(g_ToSortDir)) {
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
        LOG_INFO("[LOCAL INITIALIZATION] Scanned " + std::to_string(files.size()) + " audio files in TO SORT/");

        if (files.empty()) {
            CleanupOrphanToSortFolders(fs::path(g_ToSortDir));
            m_tagScanEndTime = std::chrono::steady_clock::now();
            m_isTagScanning = false;
            PostMessageW(m_hWnd, WM_TAG_SCAN_FINISHED, 0, 0);
            return;
        }

        // FAST LOCAL INITIALIZATION (0.005s)
        m_tagItems.resize(files.size());
        for (size_t i = 0; i < files.size(); ++i) {
            auto& item = m_tagItems[i];
            item.filePath = files[i];
            item.relPath = fs::relative(files[i], g_BaseDir).string();
            item.originalFilename = fs::path(files[i]).filename().string();
            memset(item.lyricsBuf, 0, sizeof(item.lyricsBuf));

            std::string fn = fs::path(files[i]).stem().string();
            std::string trackNo = "01";
            std::string title = fn;
            std::string artistRaw = fs::path(files[i]).parent_path().parent_path().filename().string();
            std::string albumRaw = fs::path(files[i]).parent_path().filename().string();
            std::string yearStr = ExtractYearFromString(files[i]);

            size_t dotPos = fn.find(". ");
            if (dotPos == std::string::npos) dotPos = fn.find("- ");
            if (dotPos == std::string::npos) dotPos = fn.find("_");
            if (dotPos != std::string::npos && dotPos <= 4 && std::isdigit((unsigned char)fn[0])) {
                trackNo = fn.substr(0, dotPos);
                while (!trackNo.empty() && !std::isdigit((unsigned char)trackNo.back())) trackNo.pop_back();
                if (trackNo.length() == 1) trackNo = "0" + trackNo;
                title = fn.substr(dotPos + 1);
                size_t first = title.find_first_not_of(" \t.-_");
                if (first != std::string::npos) title = title.substr(first);
            }

            std::string artistClean = CleanMetadataString(artistRaw);
            if (artistClean.empty() || artistClean == "TO SORT" || artistClean == "media" || artistClean == "music") {
                artistClean = ExtractArtistFromFilename(item.originalFilename);
                if (artistClean.empty()) artistClean = "Unknown Artist";
            }
            std::string albumClean = CleanAlbumTitle(albumRaw);
            if (albumClean.empty()) albumClean = CleanMetadataString(albumRaw);

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

            // Look for local cover image
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

        // Post immediate notification to show local UI instantly
        PostMessageW(m_hWnd, WM_TAG_SCAN_FINISHED, 0, 0);

        // =========================================================================
        // PHASE 1: Fast Parallel Audio Fingerprinting (Multi-threaded fpcalc)
        // =========================================================================
        std::vector<AudioFingerprint> fpResults(files.size());
        unsigned int numFpThreads = (std::min)(std::thread::hardware_concurrency(), 8u);
        if (numFpThreads == 0) numFpThreads = 4;
        LOG_INFO("[PARALLEL FINGERPRINTING] Computing AcoustID fingerprints across " + std::to_string(numFpThreads) + " worker threads for " + std::to_string(files.size()) + " files...");

        std::atomic<size_t> fpTaskIdx{ 0 };
        std::vector<std::thread> fpWorkers;
        for (unsigned int t = 0; t < numFpThreads; ++t) {
            fpWorkers.emplace_back([&]() {
                while (true) {
                    size_t idx = fpTaskIdx.fetch_add(1);
                    if (idx >= files.size()) break;
                    fpResults[idx] = AcousticAnalyzer::Instance().ExtractFingerprint(files[idx]);
                    m_tagItems[idx].duration = fpResults[idx].duration;
                }
            });
        }
        for (auto& w : fpWorkers) {
            if (w.joinable()) w.join();
        }
        LOG_INFO("[PARALLEL FINGERPRINTING] Completed fingerprint extraction for " + std::to_string(files.size()) + " files.");

        // =========================================================================
        // PHASE 2: Grouping Tracks into Album Clusters (UI-Order Priority)
        // =========================================================================
        struct AlbumCluster {
            std::string albumKey;
            std::string sampleFile;
            std::vector<size_t> indices;
        };

        std::vector<AlbumCluster> clusters;
        std::unordered_map<std::string, size_t> keyToClusterIdx;

        for (size_t i = 0; i < files.size(); ++i) {
            std::string albumClean(m_tagItems[i].albumBuf);
            std::string albumKey = NormalizeKey(albumClean);
            if (albumKey.empty() || albumKey == "unknown" || albumKey == "tosort" || albumKey == "music" || albumKey == "media") {
                albumKey = NormalizeKey(fs::path(files[i]).parent_path().string());
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

        LOG_INFO("[ALBUM CLUSTERING] Grouped " + std::to_string(files.size()) + " tracks into " + std::to_string(clusters.size()) + " album clusters.");

        // =========================================================================
        // PHASE 3: Concurrent Multi-Threaded Album Resolution & Lyrics Fetching
        // =========================================================================
        std::mutex albumCacheMutex;
        std::unordered_map<std::string, AlbumMetadataCache> albumCache;

        std::atomic<size_t> nextClusterIdx{ 0 };
        unsigned int numAlbumWorkers = (std::min)((unsigned int)clusters.size(), 4u);
        if (numAlbumWorkers == 0) numAlbumWorkers = 1;

        std::vector<std::thread> albumWorkers;
        for (unsigned int w = 0; w < numAlbumWorkers; ++w) {
            albumWorkers.emplace_back([&, w]() {
                while (true) {
                    size_t cIdx = nextClusterIdx.fetch_add(1);
                    if (cIdx >= clusters.size()) break;

                    const auto& cluster = clusters[cIdx];
                    const std::string& albumKey = cluster.albumKey;
                    const auto& indices = cluster.indices;
                    if (indices.empty()) continue;

                    size_t leadIdx = indices[0];
                    auto& leadItem = m_tagItems[leadIdx];

                    std::string rawAlbum = fs::path(cluster.sampleFile).parent_path().filename().string();
                    std::string catalogNo = ExtractCatalogNumber(rawAlbum);
                    if (catalogNo.empty()) {
                        catalogNo = ExtractCatalogNumber(leadItem.originalFilename);
                    }

                    std::string artistClean(leadItem.artistBuf);
                    if (artistClean.empty() || artistClean == "Unknown Artist") {
                        std::string fnArt = ExtractArtistFromFilename(leadItem.originalFilename);
                        if (!fnArt.empty()) {
                            artistClean = fnArt;
                            strncpy_s(leadItem.artistBuf, artistClean.c_str(), sizeof(leadItem.artistBuf) - 1);
                        }
                    }
                    std::string albumClean(leadItem.albumBuf);
                    std::string titleClean(leadItem.titleBuf);

                    AlbumMetadataCache cacheResult;
                    bool foundInCache = false;

                    {
                        std::lock_guard<std::mutex> lock(albumCacheMutex);
                        auto cIt = albumCache.find(albumKey);
                        if (cIt != albumCache.end() && cIt->second.isFetched) {
                            cacheResult = cIt->second;
                            foundInCache = true;
                        }
                    }

                    if (!foundInCache) {
                        LOG_INFO("[ONLINE RESOLVE START] Cluster #" + std::to_string(cIdx + 1) + "/" + std::to_string(clusters.size()) + ": " + artistClean + " - " + albumClean + " (" + std::to_string(indices.size()) + " tracks)");

                        std::string releaseGroupMbId;
                        std::string firstReleaseDate;
                        std::vector<unsigned char> coverData;
                        std::string coverSource;
                        bool isMatched = false;
                        MatchTier detectedTier = MatchTier::Niche_Local;
                        std::vector<MBTrackEntry> resolvedTracks;
                        std::string albRomaji, albEnglish, albJapanese;
                        std::string artRomaji, artEnglish, artJapanese;

                        std::string acoustRecMbId;
                        std::string acoustRecTitle;
                        std::string acoustRecArtist;

                        // 1. AcoustID Fingerprint Lookup
                        const auto& fpInfo = fpResults[leadIdx];
                        if (!fpInfo.fpBase64.empty()) {
                            std::ostringstream postStream;
                            postStream << "client=" << g_AcoustIdKey << "&meta=recordings+releasegroups+compress&duration=" << (int)fpInfo.duration << "&fingerprint=" << fpInfo.fpBase64;
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
                                        if (!ar.recordingId.empty()) acoustRecMbId = ar.recordingId;
                                        if (!ar.title.empty()) acoustRecTitle = ar.title;
                                        if (!ar.artists.empty()) acoustRecArtist = ar.artists[0];
                                    }
                                }
                            }
                        }

                        // AcoustID -> MusicBrainz Recording Lookup if releasegroup was not directly present
                        if (releaseGroupMbId.empty() && !acoustRecMbId.empty()) {
                            std::string recLookupUrl = "https://musicbrainz.org/ws/2/recording/" + acoustRecMbId + "?inc=release-groups&fmt=json";
                            std::string recLookupRes = HttpGetString(Utf8ToWide(recLookupUrl));
                            size_t lp = 0;
                            JsonVal recDoc = ParseJsonSimple(recLookupRes, lp);
                            const auto& recRgs = recDoc.get("release-groups");
                            if (recRgs.type == JsonVal::Array && !recRgs.arrVal.empty()) {
                                for (size_t gi = 0; gi < recRgs.arrVal.size(); ++gi) {
                                    std::string rgId = recRgs.get(gi).get("id").strVal;
                                    std::string rgTitle = recRgs.get(gi).get("title").strVal;
                                    if (rgId.length() == 36) {
                                        releaseGroupMbId = rgId;
                                        isMatched = true;
                                        detectedTier = MatchTier::AcoustId;
                                        break;
                                    }
                                }
                            }
                        }

                        // 2. MusicBrainz Multi-Tier Search (Tier A, Tier B, Tier C)
                        if (releaseGroupMbId.empty() && !albumClean.empty()) {
                            std::string searchArtist = !acoustRecArtist.empty() ? acoustRecArtist : artistClean;
                            std::string searchTitle = !acoustRecTitle.empty() ? acoustRecTitle : titleClean;

                            // Tier A: Strict
                            if (!searchArtist.empty() && searchArtist != "Unknown Artist") {
                                std::string artistLucene = EscapeLuceneQuery(searchArtist);
                                std::string albumLucene = EscapeLuceneQuery(albumClean);
                                std::string mbQuery = "artist:\"" + artistLucene + "\" AND release:\"" + albumLucene + "\"";
                                std::string mbUrl = "https://musicbrainz.org/ws/2/release-group?query=" + UrlEncode(mbQuery) + "&fmt=json";
                                std::string mbRes = HttpGetString(Utf8ToWide(mbUrl));
                                auto candidates = ParseMusicBrainzReleaseGroups(mbRes);
                                if (!candidates.empty()) {
                                    releaseGroupMbId = candidates[0].id;
                                    firstReleaseDate = candidates[0].firstReleaseDate;
                                    isMatched = true;
                                    detectedTier = MatchTier::TierA;
                                }
                            }

                            // Tier B: Release Title alone & Katakana
                            if (releaseGroupMbId.empty()) {
                                std::string mbAlbumUrl = "https://musicbrainz.org/ws/2/release-group?query=release:\"" + UrlEncode(albumClean) + "\"&fmt=json";
                                std::string mbAlbumRes = HttpGetString(Utf8ToWide(mbAlbumUrl));
                                auto tierBCandidates = ParseMusicBrainzReleaseGroups(mbAlbumRes);

                                for (const auto& c : tierBCandidates) {
                                    std::string acLower = c.artistCredit;
                                    std::transform(acLower.begin(), acLower.end(), acLower.begin(), ::tolower);
                                    std::string sLower = searchArtist;
                                    std::transform(sLower.begin(), sLower.end(), sLower.begin(), ::tolower);
                                    if (!sLower.empty() && sLower != "unknown artist" && (acLower.find(sLower) != std::string::npos || acLower.find("various") != std::string::npos)) {
                                        releaseGroupMbId = c.id;
                                        firstReleaseDate = c.firstReleaseDate;
                                        isMatched = true;
                                        detectedTier = MatchTier::TierB_Verified;
                                        break;
                                    }
                                }

                                if (releaseGroupMbId.empty() && !tierBCandidates.empty() && !titleClean.empty()) {
                                    for (size_t ci = 0; ci < (std::min)((size_t)5, tierBCandidates.size()); ++ci) {
                                        auto tracks = FetchMusicBrainzReleaseTracks(tierBCandidates[ci].id);
                                        for (const auto& t : tracks) {
                                            if (NormalizeKey(t.title) == NormalizeKey(titleClean)) {
                                                releaseGroupMbId = tierBCandidates[ci].id;
                                                firstReleaseDate = tierBCandidates[ci].firstReleaseDate;
                                                isMatched = true;
                                                detectedTier = MatchTier::TierB_Verified;
                                                break;
                                            }
                                        }
                                        if (!releaseGroupMbId.empty()) break;
                                    }
                                }

                                if (releaseGroupMbId.empty()) {
                                    std::string albumKatakana = RomajiToKatakana(albumClean);
                                    if (!albumKatakana.empty() && albumKatakana != albumClean) {
                                        std::string mbKataUrl = "https://musicbrainz.org/ws/2/release-group?query=release:\"" + UrlEncode(albumKatakana) + "\"&fmt=json";
                                        std::string mbKataRes = HttpGetString(Utf8ToWide(mbKataUrl));
                                        auto kataCandidates = ParseMusicBrainzReleaseGroups(mbKataRes);
                                        if (!kataCandidates.empty()) {
                                            releaseGroupMbId = kataCandidates[0].id;
                                            firstReleaseDate = kataCandidates[0].firstReleaseDate;
                                            isMatched = true;
                                            detectedTier = MatchTier::TierB_Katakana;
                                        }
                                    }
                                }
                            }
                        }

                        // 3. TouhouDB, VocaDB, UtaiteDB Search
                        if (releaseGroupMbId.empty() && (!albumClean.empty() || !catalogNo.empty())) {
                            std::string searchArtist = !acoustRecArtist.empty() ? acoustRecArtist : artistClean;
                            VdbReleaseInfo vdbInfo;

                            if (SearchVdbRelease("https://touhoudb.com", "TouhouDB", searchArtist, albumClean, catalogNo, vdbInfo)) {
                                releaseGroupMbId = "touhoudb_" + std::to_string(vdbInfo.id);
                                firstReleaseDate = vdbInfo.releaseDate;
                                if (!vdbInfo.coverUrl.empty()) {
                                    coverData = HttpGetBytes(Utf8ToWide(vdbInfo.coverUrl));
                                    if (!coverData.empty()) coverSource = "TouhouDB";
                                }
                                resolvedTracks = vdbInfo.tracks;
                                albRomaji = vdbInfo.titleRomaji; albEnglish = vdbInfo.titleEnglish; albJapanese = vdbInfo.titleJapanese;
                                artRomaji = vdbInfo.artistRomaji; artEnglish = vdbInfo.artistEnglish; artJapanese = vdbInfo.artistJapanese;
                                isMatched = true;
                                detectedTier = MatchTier::TouhouDB;
                            } else if (SearchVdbRelease("https://vocadb.net", "VocaDB", searchArtist, albumClean, catalogNo, vdbInfo)) {
                                releaseGroupMbId = "vocadb_" + std::to_string(vdbInfo.id);
                                firstReleaseDate = vdbInfo.releaseDate;
                                if (!vdbInfo.coverUrl.empty()) {
                                    coverData = HttpGetBytes(Utf8ToWide(vdbInfo.coverUrl));
                                    if (!coverData.empty()) coverSource = "VocaDB";
                                }
                                resolvedTracks = vdbInfo.tracks;
                                albRomaji = vdbInfo.titleRomaji; albEnglish = vdbInfo.titleEnglish; albJapanese = vdbInfo.titleJapanese;
                                artRomaji = vdbInfo.artistRomaji; artEnglish = vdbInfo.artistEnglish; artJapanese = vdbInfo.artistJapanese;
                                isMatched = true;
                                detectedTier = MatchTier::VocaDB;
                            } else if (SearchVdbRelease("https://utaitedb.net", "UtaiteDB", searchArtist, albumClean, catalogNo, vdbInfo)) {
                                releaseGroupMbId = "utaitedb_" + std::to_string(vdbInfo.id);
                                firstReleaseDate = vdbInfo.releaseDate;
                                if (!vdbInfo.coverUrl.empty()) {
                                    coverData = HttpGetBytes(Utf8ToWide(vdbInfo.coverUrl));
                                    if (!coverData.empty()) coverSource = "UtaiteDB";
                                }
                                resolvedTracks = vdbInfo.tracks;
                                albRomaji = vdbInfo.titleRomaji; albEnglish = vdbInfo.titleEnglish; albJapanese = vdbInfo.titleJapanese;
                                artRomaji = vdbInfo.artistRomaji; artEnglish = vdbInfo.artistEnglish; artJapanese = vdbInfo.artistJapanese;
                                isMatched = true;
                                detectedTier = MatchTier::UtaiteDB;
                            }
                        }

                        // 4. Discogs Search Fallback
                        if (releaseGroupMbId.empty() && !albumClean.empty()) {
                            std::string searchArtist = !acoustRecArtist.empty() ? acoustRecArtist : artistClean;
                            DiscogsReleaseInfo discInfo;
                            if (SearchDiscogsRelease(searchArtist, albumClean, discInfo, g_DiscogsToken)) {
                                releaseGroupMbId = "discogs_" + discInfo.id;
                                firstReleaseDate = discInfo.year;
                                if (!discInfo.coverUrl.empty()) {
                                    coverData = HttpGetBytes(Utf8ToWide(discInfo.coverUrl), g_DiscogsToken);
                                    if (!coverData.empty()) coverSource = "Discogs";
                                }
                                resolvedTracks = discInfo.tracks;
                                isMatched = true;
                                detectedTier = MatchTier::Discogs;
                            }
                        }

                        // 5. If MusicBrainz matched: Download Cover Art & Tracklist & Romaji Enrichment
                        if (!releaseGroupMbId.empty() && releaseGroupMbId.rfind("discogs_", 0) != 0 && releaseGroupMbId.rfind("touhoudb_", 0) != 0 && releaseGroupMbId.rfind("vocadb_", 0) != 0 && releaseGroupMbId.rfind("utaitedb_", 0) != 0) {
                            const char* endpoints[] = {
                                "https://coverartarchive.org/release-group/%s/front",
                                "https://coverartarchive.org/release/%s/front",
                                "https://coverartarchive.org/release-group/%s/front-1200",
                                "https://coverartarchive.org/release/%s/front-1200",
                                "https://coverartarchive.org/release-group/%s/front-500",
                                "https://coverartarchive.org/release/%s/front-500",
                                "https://coverartarchive.org/release-group/%s/front-250",
                                "https://coverartarchive.org/release/%s/front-250"
                            };

                            for (const char* epPattern : endpoints) {
                                char urlBuf[512];
                                sprintf_s(urlBuf, sizeof(urlBuf), epPattern, releaseGroupMbId.c_str());
                                coverData = HttpGetBytes(Utf8ToWide(urlBuf));
                                if (!coverData.empty()) {
                                    coverSource = "CoverArtArchive";
                                    break;
                                }
                            }

                            if (coverData.empty()) {
                                DiscogsReleaseInfo discCoverInfo;
                                if (SearchDiscogsRelease(artistClean, albumClean, discCoverInfo, g_DiscogsToken) && !discCoverInfo.coverUrl.empty()) {
                                    coverData = HttpGetBytes(Utf8ToWide(discCoverInfo.coverUrl), g_DiscogsToken);
                                    if (!coverData.empty()) coverSource = "Discogs";
                                }
                            }

                            resolvedTracks = FetchMusicBrainzReleaseTracks(releaseGroupMbId, &firstReleaseDate);

                            if (ContainsCJK(artistClean) || ContainsCJK(albumClean) || !catalogNo.empty()) {
                                VdbReleaseInfo vdbEnrich;
                                if (SearchVdbRelease("https://touhoudb.com", "TouhouDB", artistClean, albumClean, catalogNo, vdbEnrich) ||
                                    SearchVdbRelease("https://vocadb.net", "VocaDB", artistClean, albumClean, catalogNo, vdbEnrich) ||
                                    SearchVdbRelease("https://utaitedb.net", "UtaiteDB", artistClean, albumClean, catalogNo, vdbEnrich)) {
                                    albRomaji = vdbEnrich.titleRomaji;
                                    albEnglish = vdbEnrich.titleEnglish;
                                    albJapanese = vdbEnrich.titleJapanese;
                                    artRomaji = vdbEnrich.artistRomaji;
                                    artEnglish = vdbEnrich.artistEnglish;
                                    artJapanese = vdbEnrich.artistJapanese;
                                    if (!vdbEnrich.tracks.empty()) resolvedTracks = vdbEnrich.tracks;
                                }
                            }
                        }

                        cacheResult = {
                            releaseGroupMbId, firstReleaseDate, coverData, coverSource,
                            resolvedTracks, albRomaji, albEnglish, albJapanese,
                            artRomaji, artEnglish, artJapanese, isMatched, true, detectedTier
                        };

                        {
                            std::lock_guard<std::mutex> lock(albumCacheMutex);
                            albumCache[albumKey] = cacheResult;
                            if (!releaseGroupMbId.empty()) {
                                albumCache[releaseGroupMbId] = cacheResult;
                            }
                        }
                    }

                    // Apply metadata from cacheResult to all tracks in this cluster
                    std::string bestAlb = PickBestName(cacheResult.albumRomaji, cacheResult.albumEnglish, cacheResult.albumJapanese, "");
                    std::string bestArt = PickBestName(cacheResult.artistRomaji, cacheResult.artistEnglish, cacheResult.artistJapanese, "");

                    for (size_t idx : indices) {
                        auto& item = m_tagItems[idx];
                        item.isMusicBrainzMatched = cacheResult.isMatched;
                        item.onlineCoverBytes = cacheResult.coverBytes;
                        item.onlineCoverSource = cacheResult.coverSource;
                        item.matchTier = cacheResult.matchTier;
                        item.releaseGroupMbId = cacheResult.releaseGroupMbId;
                        item.albumRomaji = cacheResult.albumRomaji;
                        item.albumEnglish = cacheResult.albumEnglish;
                        item.albumJapanese = cacheResult.albumJapanese;
                        item.artistRomaji = cacheResult.artistRomaji;
                        item.artistEnglish = cacheResult.artistEnglish;
                        item.artistJapanese = cacheResult.artistJapanese;

                        if (!bestAlb.empty()) strncpy_s(item.albumBuf, bestAlb.c_str(), sizeof(item.albumBuf) - 1);
                        if (!bestArt.empty()) strncpy_s(item.artistBuf, bestArt.c_str(), sizeof(item.artistBuf) - 1);
                        if (!cacheResult.firstReleaseDate.empty()) {
                            strncpy_s(item.yearBuf, cacheResult.firstReleaseDate.c_str(), sizeof(item.yearBuf) - 1);
                        }

                        if (!cacheResult.tracks.empty()) {
                            ApplyTrackMatch(item, cacheResult.tracks);
                        }
                    }

                    // Fetch lyrics for all tracks in this cluster
                    for (size_t idx : indices) {
                        auto& item = m_tagItems[idx];
                        if (!item.isFetchCompleted) {
                            std::string trackArtist(item.artistBuf);
                            std::string trackTitle(item.titleBuf);
                            std::string trackAlbum(item.albumBuf);

                            std::string lrcLyrics = FetchLrcLibSyncedLyrics(trackArtist, trackTitle, trackAlbum);
                            if (!lrcLyrics.empty()) {
                                item.hasLyrics = true;
                                strncpy_s(item.lyricsBuf, lrcLyrics.c_str(), sizeof(item.lyricsBuf) - 1);
                            }

                            item.isFetchCompleted = true;
                            m_fetchedCount.fetch_add(1);
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
        LOG_INFO("Step 2 Background Online Fetching Complete. 100% of MusicBrainz, TouhouDB, VocaDB, UtaiteDB & Discogs queries finished.");
    }).detach();
}
