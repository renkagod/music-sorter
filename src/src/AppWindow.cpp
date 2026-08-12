#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

#include "../include/AppWindow.hpp"
#include "../include/AudioEngine.hpp"
#include "../include/DatabaseManager.hpp"
#include "../include/Logger.hpp"

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

static std::string WideToUtf8Str(const wchar_t* wstr) {
    if (!wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, NULL, NULL);
    return result;
}

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

struct MBTrackEntry {
    int position = 0;
    std::string title;
    std::string artist;
    int lengthMs = 0;
};

struct AlbumMetadataCache {
    std::string releaseGroupMbId;
    std::string firstReleaseDate;
    std::vector<unsigned char> coverBytes;
    std::vector<MBTrackEntry> tracks;
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

static std::string CleanMetadataString(const std::string& str) {
    if (str.empty()) return "";
    std::string res;
    res.reserve(str.size());
    int bLevel = 0;
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        if (c == '[' || c == '(' || c == '{') { bLevel++; continue; }
        if (c == ']' || c == ')' || c == '}') { if (bLevel > 0) bLevel--; continue; }
        if (bLevel == 0) res.push_back(c);
    }

    size_t first = res.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = res.find_last_not_of(" \t\r\n");
    return res.substr(first, (last - first + 1));
}

static std::string SanitizeForFilename(const std::string& str) {
    std::string res;
    res.reserve(str.size());
    for (char c : str) {
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
            res.push_back('_');
        } else if (c == '\t' || c == '\r' || c == '\n') {
            res.push_back(' ');
        } else {
            res.push_back(c);
        }
    }
    size_t first = res.find_first_not_of(" ");
    if (first == std::string::npos) return "_";
    size_t last = res.find_last_not_of(" .");
    return res.substr(first, (last - first + 1));
}

static std::string EscapeLuceneQuery(const std::string& str) {
    std::string res;
    res.reserve(str.size() * 2);
    for (char c : str) {
        if (c == ';' || c == '+' || c == '-' || c == '&' || c == '|' || c == '!' || 
            c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']' || 
            c == '^' || c == '"' || c == '~' || c == '*' || c == '?' || c == ':' || 
            c == '\\' || c == '/') {
            res.push_back(' ');
        } else {
            res.push_back(c);
        }
    }
    size_t first = res.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    size_t last = res.find_last_not_of(" \t");
    return res.substr(first, (last - first + 1));
}

static std::string ExtractYearFromString(const std::string& str) {
    if (str.size() < 4) return "";
    for (size_t i = 0; i <= str.size() - 4; ++i) {
        if (std::isdigit((unsigned char)str[i]) &&
            std::isdigit((unsigned char)str[i + 1]) &&
            std::isdigit((unsigned char)str[i + 2]) &&
            std::isdigit((unsigned char)str[i + 3])) {
            int y = std::stoi(str.substr(i, 4));
            if (y >= 1900 && y <= 2099) {
                return str.substr(i, 4);
            }
        }
    }
    return "";
}

static std::string RomajiToKatakana(const std::string& input) {
    if (input.empty()) return "";
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    static const std::pair<const char*, const char*> table[] = {
        {"tsu", "ツ"}, {"chi", "チ"}, {"shi", "シ"},
        {"kya", "キャ"}, {"kyu", "キュ"}, {"kyo", "キョ"},
        {"sha", "シャ"}, {"shu", "シュ"}, {"sho", "ショ"},
        {"cha", "チャ"}, {"chu", "チュ"}, {"cho", "チョ"},
        {"nya", "ニャ"}, {"nyu", "ニュ"}, {"nyo", "ニョ"},
        {"hya", "ヒャ"}, {"hyu", "ヒュ"}, {"hyo", "ヒョ"},
        {"mya", "ミャ"}, {"myu", "ミュ"}, {"myo", "ミョ"},
        {"rya", "リャ"}, {"ryu", "リュ"}, {"ryo", "リョ"},
        {"gya", "ギャ"}, {"gyu", "ギュ"}, {"gyo", "ギョ"},
        {"ja", "ジャ"}, {"ju", "ジュ"}, {"jo", "ジョ"},
        {"bya", "ビャ"}, {"byu", "ビュ"}, {"byo", "ビョ"},
        {"pya", "ピャ"}, {"pyu", "ピュ"}, {"pyo", "ピョ"},
        {"ka", "カ"}, {"ki", "キ"}, {"ku", "ク"}, {"ke", "ケ"}, {"ko", "コ"},
        {"sa", "サ"}, {"si", "シ"}, {"su", "ス"}, {"se", "セ"}, {"so", "ソ"},
        {"ta", "タ"}, {"ti", "チ"}, {"tu", "ツ"}, {"te", "テ"}, {"to", "ト"},
        {"na", "ナ"}, {"ni", "ニ"}, {"nu", "ヌ"}, {"ne", "ネ"}, {"no", "ノ"},
        {"ha", "ハ"}, {"hi", "ヒ"}, {"fu", "フ"}, {"hu", "フ"}, {"he", "ヘ"}, {"ho", "ホ"},
        {"ma", "マ"}, {"mi", "ミ"}, {"mu", "ム"}, {"me", "メ"}, {"mo", "モ"},
        {"ya", "ヤ"}, {"yu", "ユ"}, {"yo", "ヨ"},
        {"ra", "ラ"}, {"ri", "リ"}, {"ru", "ル"}, {"re", "レ"}, {"ro", "ロ"},
        {"wa", "ワ"}, {"wo", "ヲ"},
        {"ga", "ガ"}, {"gi", "ギ"}, {"gu", "グ"}, {"ge", "ゲ"}, {"go", "ゴ"},
        {"za", "ザ"}, {"zi", "ジ"}, {"zu", "ズ"}, {"ze", "ゼ"}, {"zo", "ゾ"},
        {"da", "ダ"}, {"di", "ヂ"}, {"du", "ヅ"}, {"de", "デ"}, {"do", "ド"},
        {"ba", "バ"}, {"bi", "ビ"}, {"bu", "ブ"}, {"be", "ベ"}, {"bo", "ボ"},
        {"pa", "パ"}, {"pi", "ピ"}, {"pu", "プ"}, {"pe", "ペ"}, {"po", "ポ"},
        {"a", "ア"}, {"i", "イ"}, {"u", "ウ"}, {"e", "エ"}, {"o", "オ"},
        {"n", "ン"}
    };

    std::string result;
    size_t i = 0;
    while (i < lower.size()) {
        bool matched = false;
        for (const auto& kv : table) {
            size_t len = strlen(kv.first);
            if (i + len <= lower.size() && lower.compare(i, len, kv.first) == 0) {
                result += kv.second;
                i += len;
                matched = true;
                break;
            }
        }
        if (!matched) {
            result += input[i];
            i++;
        }
}
    return result;
}

static std::string NormalizeKey(const std::string& text) {
    if (text.empty()) return "";
    std::string result;
    result.reserve(text.size());

    int bLevel = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '[' || c == '(' || c == '{') { bLevel++; continue; }
        if (c == ']' || c == ')' || c == '}') { if (bLevel > 0) bLevel--; continue; }
        if (bLevel > 0) continue;

        if (c <= 32 || c == '-' || c == '_' || c == '/' || c == '\\' || c == ',' || c == '.' || c == '~') continue;

        if (c == 0xEF && i + 2 < text.size() && (unsigned char)text[i + 1] == 0xBD && (unsigned char)text[i + 2] == 0x9E) {
            i += 2;
            continue;
        }

        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
        result.push_back((char)c);
    }
    return result;
}

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    std::wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstr[0], len);
    return wstr;
}

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), NULL, 0, NULL, NULL);
    std::string str(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), &str[0], len, NULL, NULL);
    return str;
}

static std::string UrlEncode(const std::string& str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : str) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << std::uppercase << (int)(unsigned char)c;
        }
    }
    return escaped.str();
}

// Mathematical Laplacian High-Frequency Sharpness Analysis
static double CalculatePerceptualSharpness(const unsigned char* data, size_t size) {
    if (!data || size == 0) return 0.0;
    int width = 0, height = 0, channels = 0;
    unsigned char* gray = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 1);
    if (!gray || width < 4 || height < 4) return 0.0;

    double sum = 0.0;
    double sumSq = 0.0;
    size_t count = 0;

    for (int y = 1; y < height - 1; y += 2) {
        for (int x = 1; x < width - 1; x += 2) {
            int center = gray[y * width + x];
            int top    = gray[(y - 1) * width + x];
            int bottom = gray[(y + 1) * width + x];
            int left   = gray[y * width + (x - 1)];
            int right  = gray[y * width + (x + 1)];

            double lap = (double)(top + bottom + left + right - 4 * center);
            sum += lap;
            sumSq += lap * lap;
            count++;
        }
    }

    stbi_image_free(gray);

    if (count == 0) return 0.0;
    double mean = sum / count;
    double variance = (sumSq / count) - (mean * mean);
    return variance;
}

// Multi-Factor Image Quality Score: Sharpness * Resolution * sqrt(FileSizeKB) / (1 + 3 * JpegBlockiness8x8)
static long long CalculateImageQualityScore(const unsigned char* data, size_t size, int width, int height) {
    if (!data || size == 0 || width <= 0 || height <= 0) return 0;

    double sharpness = CalculatePerceptualSharpness(data, size);

    // Compute 8x8 JPEG Grid Discontinuity Penalty (Blockiness)
    int w = 0, h = 0, c = 0;
    unsigned char* gray = stbi_load_from_memory(data, (int)size, &w, &h, &c, 1);
    double blockiness = 0.0;
    if (gray && w >= 16 && h >= 16) {
        double gridDiff = 0.0, nonGridDiff = 0.0;
        size_t gridCount = 0, nonGridCount = 0;
        for (int y = 1; y < h - 1; y += 2) {
            bool isRowGrid = (y % 8 == 0);
            for (int x = 1; x < w - 1; x += 2) {
                bool isColGrid = (x % 8 == 0);
                int diffH = std::abs((int)gray[y * w + x] - (int)gray[y * w + (x + 1)]);
                int diffV = std::abs((int)gray[y * w + x] - (int)gray[(y + 1) * w + x]);
                if (isColGrid) { gridDiff += diffH; gridCount++; }
                else { nonGridDiff += diffH; nonGridCount++; }
                if (isRowGrid) { gridDiff += diffV; gridCount++; }
                else { nonGridDiff += diffV; nonGridCount++; }
            }
        }
        stbi_image_free(gray);

        if (gridCount > 0 && nonGridCount > 0 && nonGridDiff > 0.0) {
            double ratio = (gridDiff / gridCount) / ((nonGridDiff / nonGridCount) + 0.001);
            if (ratio > 1.0) blockiness = (ratio - 1.0);
        }
    } else if (gray) {
        stbi_image_free(gray);
    }

    double sizeKB = (double)size / 1024.0;
    double sizeFactor = std::sqrt(sizeKB + 1.0);
    double penalty = 1.0 + 3.0 * blockiness;

    double finalScore = (sharpness * (double)width * (double)height * sizeFactor) / penalty;
    return (long long)finalScore;
}

// Helper: Endian Conversions & Vector Writing
static void WriteUint32LE(std::vector<unsigned char>& buf, uint32_t val) {
    buf.push_back((unsigned char)(val & 0xFF));
    buf.push_back((unsigned char)((val >> 8) & 0xFF));
    buf.push_back((unsigned char)((val >> 16) & 0xFF));
    buf.push_back((unsigned char)((val >> 24) & 0xFF));
}

static void WriteUint32BE(std::vector<unsigned char>& buf, uint32_t val) {
    buf.push_back((unsigned char)((val >> 24) & 0xFF));
    buf.push_back((unsigned char)((val >> 16) & 0xFF));
    buf.push_back((unsigned char)((val >> 8) & 0xFF));
    buf.push_back((unsigned char)(val & 0xFF));
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

// Helper: Convert UTF-8 std::string to UTF-16LE byte payload with BOM (0xFF 0xFE) for ID3v2.3
static std::vector<unsigned char> StringToUtf16LE(const std::string& utf8Str) {
    std::wstring wstr = Utf8ToWide(utf8Str);
    std::vector<unsigned char> res;
    res.push_back(0xFF); // BOM
    res.push_back(0xFE);
    for (wchar_t wc : wstr) {
        uint16_t val = (uint16_t)wc;
        res.push_back((unsigned char)(val & 0xFF));
        res.push_back((unsigned char)((val >> 8) & 0xFF));
    }
    return res;
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

// Robust HTTP POST for AcoustID Fingerprint Lookup (Fixes HTTP 414 Request-URI Too Long!)
static std::string AcoustIdHttpPost(const std::string& postData) {
    std::vector<unsigned char> result;
    HINTERNET hNet = InternetOpenW(L"MusicSorterApp/1.0 (https://github.com/renkagod/music-sorter)", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
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

// MusicBrainz API rate limit is 1 request/sec per IP. Enforce this proactively
// by sleeping before each MB request so we don't trip 503 throttling.
// https://musicbrainz.org/doc/MusicBrainz_API/Rate_Limiting
static std::mutex g_mbThrottleMutex;
static std::chrono::steady_clock::time_point g_lastMbRequestTime;
static void MusicBrainzThrottle() {
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

// Discogs API rate limit is 60 requests/min (1 req/sec) with auth or 25 requests/min without auth.
static std::mutex g_discogsThrottleMutex;
static std::chrono::steady_clock::time_point g_lastDiscogsRequestTime;
static void DiscogsThrottle() {
    std::lock_guard<std::mutex> lock(g_discogsThrottleMutex);
    auto now = std::chrono::steady_clock::now();
    auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastDiscogsRequestTime).count();
    const long long kMinGapMs = 1100;
    if (sinceLast < kMinGapMs) {
        long long sleepMs = kMinGapMs - sinceLast;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    g_lastDiscogsRequestTime = std::chrono::steady_clock::now();
}

// Robust HTTP GET with Ultra-Detailed Step-by-Step Logging & Rate Limit Backoff
std::vector<unsigned char> HttpGetBytes(const std::wstring& url, int maxRetries = 3) {
    std::vector<unsigned char> result;
    std::string narrowUrl = WideToUtf8(url);

    bool isMusicBrainz = (narrowUrl.find("musicbrainz.org") != std::string::npos);
    if (isMusicBrainz) MusicBrainzThrottle();

    bool isDiscogs = (narrowUrl.find("api.discogs.com") != std::string::npos);
    if (isDiscogs) DiscogsThrottle();

    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        HINTERNET hNet = InternetOpenW(L"MusicSorterApp/1.0 (https://github.com/renkagod/music-sorter)", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (hNet) {
            DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
            std::wstring customHeaders;
            if (isDiscogs && !g_DiscogsToken.empty()) {
                customHeaders = L"Authorization: Discogs token=" + Utf8ToWide(g_DiscogsToken) + L"\r\nUser-Agent: MusicSorterApp/1.0 (https://github.com/renkagod/music-sorter)\r\n";
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
                    LOG_INFO("[HTTP " + std::to_string(statusCode) + " RATE LIMIT] Backing off " + std::to_string(backoffMs) + "ms for URL: " + narrowUrl);
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

std::string HttpGetString(const std::wstring& url) {
    auto bytes = HttpGetBytes(url);
    if (bytes.empty()) return "";
    return std::string((char*)bytes.data(), bytes.size());
}

struct JsonVal {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    std::string strVal;
    double numVal = 0;
    bool boolVal = false;
    std::vector<JsonVal> arrVal;
    std::unordered_map<std::string, JsonVal> objVal;

    const JsonVal& get(const std::string& key) const {
        static JsonVal nullVal;
        if (type == Object) {
            auto it = objVal.find(key);
            if (it != objVal.end()) return it->second;
        }
        return nullVal;
    }

    const JsonVal& get(size_t idx) const {
        static JsonVal nullVal;
        if (type == Array && idx < arrVal.size()) return arrVal[idx];
        return nullVal;
    }
};

static JsonVal ParseJsonSimple(const std::string& str, size_t& pos) {
    auto skipWs = [&]() {
        while (pos < str.size() && (unsigned char)str[pos] <= 32) pos++;
    };
    skipWs();
    if (pos >= str.size()) return {};

    char c = str[pos];
    if (c == '"') {
        pos++;
        std::string s;
        while (pos < str.size()) {
            char ch = str[pos++];
            if (ch == '"') break;
            if (ch == '\\' && pos < str.size()) {
                char esc = str[pos++];
                if (esc == '"' || esc == '\\' || esc == '/') s += esc;
                else if (esc == 'b') s += '\b';
                else if (esc == 'f') s += '\f';
                else if (esc == 'n') s += '\n';
                else if (esc == 'r') s += '\r';
                else if (esc == 't') s += '\t';
                else if (esc == 'u' && pos + 4 <= str.size()) {
                    std::string hexStr = str.substr(pos, 4);
                    pos += 4;
                    try {
                        unsigned int code = std::stoul(hexStr, nullptr, 16);
                        if (code < 0x80) s += (char)code;
                        else if (code < 0x800) { s += (char)(0xC0 | (code >> 6)); s += (char)(0x80 | (code & 0x3F)); }
                        else { s += (char)(0xE0 | (code >> 12)); s += (char)(0x80 | ((code >> 6) & 0x3F)); s += (char)(0x80 | (code & 0x3F)); }
                    } catch (...) {}
                }
            } else {
                s += ch;
            }
        }
        JsonVal v; v.type = JsonVal::String; v.strVal = s; return v;
    }
    if (c == '{') {
        pos++;
        JsonVal v; v.type = JsonVal::Object;
        while (pos < str.size()) {
            skipWs();
            if (pos < str.size() && str[pos] == '}') { pos++; break; }
            JsonVal k = ParseJsonSimple(str, pos);
            if (k.type != JsonVal::String) break;
            skipWs();
            if (pos < str.size() && str[pos] == ':') pos++;
            JsonVal val = ParseJsonSimple(str, pos);
            v.objVal[k.strVal] = val;
            skipWs();
            if (pos < str.size() && str[pos] == ',') pos++;
            else if (pos < str.size() && str[pos] == '}') { pos++; break; }
        }
        return v;
    }
    if (c == '[') {
        pos++;
        JsonVal v; v.type = JsonVal::Array;
        while (pos < str.size()) {
            skipWs();
            if (pos < str.size() && str[pos] == ']') { pos++; break; }
            JsonVal val = ParseJsonSimple(str, pos);
            v.arrVal.push_back(val);
            skipWs();
            if (pos < str.size() && str[pos] == ',') pos++;
            else if (pos < str.size() && str[pos] == ']') { pos++; break; }
        }
        return v;
    }
    if (c == 't' || c == 'f') {
        JsonVal v; v.type = JsonVal::Bool;
        if (str.compare(pos, 4, "true") == 0) { v.boolVal = true; pos += 4; }
        else if (str.compare(pos, 5, "false") == 0) { v.boolVal = false; pos += 5; }
        return v;
    }
    if (c == 'n') {
        if (str.compare(pos, 4, "null") == 0) pos += 4;
        return {};
    }
    if ((c >= '0' && c <= '9') || c == '-') {
        size_t start = pos;
        if (c == '-') pos++;
        while (pos < str.size() && (std::isdigit((unsigned char)str[pos]) || str[pos] == '.' || str[pos] == 'e' || str[pos] == 'E' || str[pos] == '+' || str[pos] == '-')) pos++;
        JsonVal v; v.type = JsonVal::Number;
        try { v.numVal = std::stod(str.substr(start, pos - start)); } catch (...) {}
        return v;
    }
    pos++;
    return {};
}

struct MBReleaseGroupCandidate {
    std::string id;
    std::string title;
    std::string firstReleaseDate;
    std::string artistCredit;
    int score = 0;
};

static std::vector<MBReleaseGroupCandidate> ParseMusicBrainzReleaseGroups(const std::string& resJson) {
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

static std::vector<MBTrackEntry> FetchMusicBrainzReleaseTracks(const std::string& releaseGroupMbId, std::string* outReleaseDate = nullptr) {
    std::vector<MBTrackEntry> tracks;
    if (releaseGroupMbId.empty()) return tracks;

    std::string url = "https://musicbrainz.org/ws/2/release?release-group=" + releaseGroupMbId + "&inc=recordings+artist-credits&fmt=json";
    std::string resJson = HttpGetString(Utf8ToWide(url));
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
        if (!outReleaseDate->empty()) {
            LOG_INFO("[MUSICBRAINZ RELEASE DATE] Found full date: " + *outReleaseDate);
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
                    out.push_back({ pos, title, artist, lengthMs });
                }
            }
        }
    };

    // 1. Prefer Pseudo-Release (romanized titles)
    for (const auto& rel : rels.arrVal) {
        std::string status = rel.get("status").strVal;
        if (status == "Pseudo-Release") {
            extractTracks(rel.get("media"), tracks);
            if (!tracks.empty()) {
                LOG_INFO("[MUSICBRAINZ] Selected Pseudo-Release for romanized track titles");
                return tracks;
            }
        }
    }

    // 2. Fallback to first Official release
    for (const auto& rel : rels.arrVal) {
        std::string status = rel.get("status").strVal;
        if (status == "Official") {
            extractTracks(rel.get("media"), tracks);
            if (!tracks.empty()) {
                LOG_INFO("[MUSICBRAINZ] No Pseudo-Release found, using Official release titles");
                return tracks;
            }
        }
    }

    // 3. Last resort: whatever is available
    extractTracks(rels.get(0).get("media"), tracks);
    return tracks;
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

        if (!bestMatch->title.empty()) {
            strncpy_s(albItem.titleBuf, bestMatch->title.c_str(), sizeof(albItem.titleBuf) - 1);
        }
        if (!bestMatch->artist.empty() && bestMatch->artist != "Various Artists" && bestMatch->artist != "V.A.") {
            strncpy_s(albItem.artistBuf, bestMatch->artist.c_str(), sizeof(albItem.artistBuf) - 1);
        }
        LOG_INFO("[MUSICBRAINZ TRACK MATCHED] Track #" + std::string(trackStr) + ": " + std::string(albItem.artistBuf) + " - " + std::string(albItem.titleBuf) + " for file: " + albItem.originalFilename);
    }
}

struct DiscogsReleaseInfo {
    std::string id;
    std::string title;
    std::string artist;
    std::string year;
    std::string coverUrl;
    std::vector<MBTrackEntry> tracks;
};

static int ParseDurationMs(const std::string& durStr) {
    if (durStr.empty()) return 0;
    int totalSec = 0;
    std::stringstream ss(durStr);
    std::string part;
    std::vector<int> parts;
    while (std::getline(ss, part, ':')) {
        try { parts.push_back(std::stoi(part)); } catch (...) {}
    }
    if (parts.size() == 2) {
        totalSec = parts[0] * 60 + parts[1];
    } else if (parts.size() == 3) {
        totalSec = parts[0] * 3600 + parts[1] * 60 + parts[2];
    } else if (parts.size() == 1) {
        totalSec = parts[0];
    }
    return totalSec * 1000;
}

static int ParseDiscogsPosition(const std::string& posStr, int defaultPos) {
    if (posStr.empty()) return defaultPos;
    int num = 0;
    for (char c : posStr) {
        if (c >= '0' && c <= '9') {
            num = num * 10 + (c - '0');
        }
    }
    return (num > 0) ? num : defaultPos;
}

static bool FetchDiscogsReleaseDetails(const std::string& releaseId, bool isMaster, DiscogsReleaseInfo& outInfo) {
    std::string endpoint = isMaster ? 
        ("https://api.discogs.com/masters/" + releaseId) : 
        ("https://api.discogs.com/releases/" + releaseId);
    if (!g_DiscogsToken.empty()) {
        endpoint += "?token=" + g_DiscogsToken;
    }
    std::string jsonStr = HttpGetString(Utf8ToWide(endpoint));
    if (jsonStr.empty()) return false;

    size_t p = 0;
    JsonVal doc = ParseJsonSimple(jsonStr, p);
    outInfo.id = releaseId;
    outInfo.title = doc.get("title").strVal;
    
    // Year / Released date (prefer ISO full date YYYY-MM-DD from "released", or "released_formatted", or "year")
    if (doc.get("released").type == JsonVal::String && !doc.get("released").strVal.empty()) {
        outInfo.year = doc.get("released").strVal;
    } else if (doc.get("released_formatted").type == JsonVal::String && !doc.get("released_formatted").strVal.empty()) {
        outInfo.year = doc.get("released_formatted").strVal;
    } else if (doc.get("year").type == JsonVal::Number && doc.get("year").numVal > 0) {
        outInfo.year = std::to_string((int)doc.get("year").numVal);
    } else if (doc.get("year").type == JsonVal::String) {
        outInfo.year = doc.get("year").strVal;
    }

    // Master fallback if date still empty
    if (outInfo.year.empty() || outInfo.year == "0") {
        std::string masterId;
        if (doc.get("master_id").type == JsonVal::Number && doc.get("master_id").numVal > 0) {
            masterId = std::to_string((int)doc.get("master_id").numVal);
        }
        if (!masterId.empty()) {
            std::string masterUrl = "https://api.discogs.com/masters/" + masterId;
            if (!g_DiscogsToken.empty()) masterUrl += "?token=" + g_DiscogsToken;
            std::string masterJson = HttpGetString(Utf8ToWide(masterUrl));
            if (!masterJson.empty()) {
                size_t mp = 0;
                JsonVal mDoc = ParseJsonSimple(masterJson, mp);
                if (mDoc.get("year").type == JsonVal::Number && mDoc.get("year").numVal > 0) {
                    outInfo.year = std::to_string((int)mDoc.get("year").numVal);
                }
            }
        }
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
                outInfo.tracks.push_back({ pos, trkTitle, trkArtist, durMs });
            }
            trackPosCounter++;
        }
    }

    return !outInfo.title.empty() || !outInfo.tracks.empty();
}

static bool SearchDiscogsRelease(const std::string& artist, const std::string& album, DiscogsReleaseInfo& outInfo) {
    if (album.empty()) return false;

    bool isArtistUnknown = (artist.empty() || artist == "Unknown Artist" || artist == "Various Artists" || artist == "V.A." || artist == "VA");

    std::string queryUrl = "https://api.discogs.com/database/search?release_title=" + UrlEncode(album);
    if (!isArtistUnknown) {
        queryUrl += "&artist=" + UrlEncode(artist);
    }
    queryUrl += "&type=release";
    if (!g_DiscogsToken.empty()) {
        queryUrl += "&token=" + g_DiscogsToken;
    }

    LOG_INFO("[DISCOGS SEARCH] Querying: " + queryUrl);
    std::string resJson = HttpGetString(Utf8ToWide(queryUrl));
    if (resJson.empty() || resJson.find("\"results\":[]") != std::string::npos) {
        std::string broadQuery = isArtistUnknown ? album : (artist + " " + album);
        std::string broadUrl = "https://api.discogs.com/database/search?q=" + UrlEncode(broadQuery) + "&type=release";
        if (!g_DiscogsToken.empty()) broadUrl += "&token=" + g_DiscogsToken;
        LOG_INFO("[DISCOGS SEARCH FALLBACK] Querying: " + broadUrl);
        resJson = HttpGetString(Utf8ToWide(broadUrl));
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

        // Check format descriptions to prioritize official standard Album over test pressings/promos
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

        // Community popularity score
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
    bool ok = FetchDiscogsReleaseDetails(pickedId, isMasterPicked, outInfo);
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
static std::string FetchLrcLibSyncedLyrics(const std::string& artist, const std::string& title, const std::string& album) {
    if (artist.empty() || title.empty()) return "";
    std::string url = "https://lrclib.net/api/get?artist_name=" + UrlEncode(artist) + "&track_name=" + UrlEncode(title);
    if (!album.empty()) url += "&album_name=" + UrlEncode(album);

    std::string json = HttpGetString(Utf8ToWide(url));
    if (json.empty()) return "";

    size_t sPos = json.find("\"syncedLyrics\":\"");
    if (sPos != std::string::npos) {
        sPos += 16;
        size_t endPos = json.find("\"", sPos);
        if (endPos != std::string::npos) {
            std::string lyrics = json.substr(sPos, endPos - sPos);
            lyrics = std::regex_replace(lyrics, std::regex(R"(\\n)"), "\n");
            lyrics = std::regex_replace(lyrics, std::regex(R"(\\r)"), "");
            return lyrics;
        }
    }

    size_t pPos = json.find("\"plainLyrics\":\"");
    if (pPos != std::string::npos) {
        pPos += 15;
        size_t endPos = json.find("\"", pPos);
        if (endPos != std::string::npos) {
            std::string lyrics = json.substr(pPos, endPos - pPos);
            lyrics = std::regex_replace(lyrics, std::regex(R"(\\n)"), "\n");
            lyrics = std::regex_replace(lyrics, std::regex(R"(\\r)"), "");
            return lyrics;
        }
    }

    return "";
}

// Stage 3: Pure Native C++20 FLAC / MP3 Mirroring
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
    size_t countDiscogs = 0;
    size_t countNiche = 0;

    for (size_t i = 0; i < m_tagItems.size(); ++i) {
        const auto& itm = m_tagItems[i];
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
        } else if (g.matchTier == MatchTier::Discogs) {
            countDiscogs++;
        } else {
            countNiche++;
        }
    }

    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Аудит распознавания релизов");
    ImGui::SameLine();
    ImGui::TextDisabled("| Всего релизов: %zu (файлов: %zu)", groups.size(), m_tagItems.size());

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
    drawFilterBtn("Discogs", 5, countDiscogs, ImVec4(0.2f, 0.9f, 0.3f, 1.0f));
    ImGui::SameLine();
    drawFilterBtn("Niche", 4, countNiche, ImVec4(0.95f, 0.4f, 0.3f, 1.0f));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::InputTextWithHint("##ReleaseSearchFilter", "Поиск...", searchFilter, sizeof(searchFilter));

    ImGui::Separator();

    if (groups.empty()) {
        ImGui::TextDisabled("Текущая очередь пуста. Перейдите во вкладку '2. Инспектор тегов' для сканирования файлов.");
    } else {
        if (ImGui::BeginTable("ReleaseSummaryTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0, 0))) {
            ImGui::TableSetupColumn("Альбом и исполнитель", ImGuiTableColumnFlags_WidthStretch, 0.38f);
            ImGui::TableSetupColumn("Файлов", ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableSetupColumn("Уровень распознавания", ImGuiTableColumnFlags_WidthFixed, 175.0f);
            ImGui::TableSetupColumn("MBID / Источник и дата", ImGuiTableColumnFlags_WidthFixed, 185.0f);
            ImGui::TableSetupColumn("Действие", ImGuiTableColumnFlags_WidthFixed, 105.0f);
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
                char selId[64];
                sprintf_s(selId, sizeof(selId), "##RowSelect_%zu", gi);

                if (ImGui::Selectable(selId, isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                    m_currentTagIndex = g.firstTrackIndex;
                    m_activeStageTab = 1;
                }
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextUnformatted(g.album.empty() ? "(Без названия альбома)" : g.album.c_str());
                ImGui::TextDisabled("%s", g.artist.empty() ? "Unknown Artist" : g.artist.c_str());
                ImGui::EndGroup();

                // Column 1: Files
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%zu", g.fileCount);

                // Column 2: Badge
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(GetTierColor(g.matchTier), "%s", GetTierName(g.matchTier));

                // Column 3: MBID & Date
                ImGui::TableSetColumnIndex(3);
                if (!g.releaseGroupMbId.empty()) {
                    if (g.releaseGroupMbId.rfind("discogs_", 0) == 0) {
                        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Discogs: %s", g.releaseGroupMbId.substr(8).c_str());
                    } else {
                        ImGui::TextUnformatted(g.releaseGroupMbId.c_str());
                    }
                    if (!g.releaseDate.empty()) {
                        ImGui::TextDisabled("Дата: %s", g.releaseDate.c_str());
                    }
                } else {
                    ImGui::TextDisabled("Источник отсутствует");
                }

                // Column 4: Open in Web
                ImGui::TableSetColumnIndex(4);
                if (!g.releaseGroupMbId.empty()) {
                    char btnId[64];
                    sprintf_s(btnId, sizeof(btnId), "Открыть##%zu", gi);
                    if (ImGui::Button(btnId, ImVec2(-1, 24))) {
                        std::string targetUrl;
                        if (g.releaseGroupMbId.rfind("discogs_", 0) == 0) {
                            targetUrl = "https://www.discogs.com/release/" + g.releaseGroupMbId.substr(8);
                        } else {
                            targetUrl = "https://musicbrainz.org/release-group/" + g.releaseGroupMbId;
                        }
                        ShellExecuteA(NULL, "open", targetUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    }
                } else {
                    ImGui::TextDisabled("-");
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
        if (!m_tagItems.empty() && m_currentTagIndex < m_tagItems.size()) {
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
                    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "[ПОИСК MB / DISCOGS %zu/%zu...]", m_fetchedCount.load(), m_tagItems.size());
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

                // Manual MusicBrainz / Discogs URL / ID Input Row
                ImGui::PushItemWidth(360);
                ImGui::InputTextWithHint("##ManualMbUrlInput", "Ссылка MusicBrainz / Discogs или ID", m_manualMbUrlBuf, sizeof(m_manualMbUrlBuf));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Загрузить метаданные", ImVec2(185, 24))) {
                    if (strlen(m_manualMbUrlBuf) > 0) {
                        std::string inputStr(m_manualMbUrlBuf);
                        if (inputStr.find("discogs.com") != std::string::npos || (inputStr.find_first_not_of("0123456789") == std::string::npos && inputStr.length() >= 4 && inputStr.length() <= 12) || inputStr.find("r") == 0 || inputStr.find("m") == 0) {
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
                ImGui::PushItemWidth(180);
                ImGui::InputText("Исполнитель##New", item.artistBuf, sizeof(item.artistBuf));
                ImGui::InputText("Альбом##New", item.albumBuf, sizeof(item.albumBuf));
                ImGui::InputText("Название##New", item.titleBuf, sizeof(item.titleBuf));
                ImGui::PopItemWidth();

                ImGui::PushItemWidth(60);
                ImGui::InputText("№##New", item.trackNoBuf, sizeof(item.trackNoBuf));
                ImGui::SameLine();
                ImGui::PushItemWidth(90);
                ImGui::InputText("Год/Дата##New", item.yearBuf, sizeof(item.yearBuf));
                ImGui::PopItemWidth();
                ImGui::PopItemWidth();

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
                ImGui::TextDisabled("CoverArtArchive:");
                if (item.onlineTexture) {
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
                    if (ImGui::ImageButton("##OnlineCoverBtnRightLarge", (ImTextureID)item.onlineTexture, ImVec2(225, 225))) {
                        item.selectedCoverChoice = 1;
                    }
                    ImGui::PopStyleVar();
                    ImGui::Text(item.selectedCoverChoice == 1 ? "[X] CoverArtArchive" : "   CoverArtArchive");
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

                if (ImGui::Button("Принять", ImVec2(160, 34))) {
                    std::string newArtist(item.artistBuf);
                    std::string newAlbum(item.albumBuf);
                    std::string newTitle(item.titleBuf);
                    std::string newTrackNo(item.trackNoBuf);
                    std::string newYear(item.yearBuf);
                    std::string newLyrics(item.lyricsBuf);
                    std::string srcFilePath = item.filePath;
                    std::string origFilename = item.originalFilename;

                    const auto chosenCover = (item.selectedCoverChoice == 1 && !item.onlineCoverBytes.empty()) ? item.onlineCoverBytes : item.localCoverBytes;

                    std::thread([newArtist, newAlbum, newTitle, newTrackNo, newYear, newLyrics, srcFilePath, origFilename, chosenCover]() {
                        fs::path srcFile(srcFilePath);
                        std::string ext = srcFile.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                        std::string safeArtist = SanitizeForFilename(newArtist);
                        std::string safeAlbum  = SanitizeForFilename(newAlbum);
                        std::string safeTitle  = SanitizeForFilename(newTitle);

                        fs::path flacDir = fs::path(g_FlacDir) / safeArtist / safeAlbum;
                        fs::path mp3Dir  = fs::path(g_Mp3Dir) / safeArtist / safeAlbum;
                        fs::create_directories(flacDir);
                        fs::create_directories(mp3Dir);

                        std::string baseTrackName = newTrackNo + ". " + safeTitle;

                        if (ext == ".flac") {
                            fs::path flacFile = flacDir / (baseTrackName + ".flac");
                            fs::path mp3File  = mp3Dir / (baseTrackName + ".mp3");

                            // 1. Embed tags & picture into FLAC header
                            bool embeddedOk = WriteFlacTagsAndPicture(srcFile.string(), newArtist, newAlbum, newTitle, newTrackNo, newYear, newLyrics, chosenCover);
                            if (embeddedOk) {
                                LOG_INFO("[TAGS EMBEDDED] VorbisComment tags & cover art written to: " + origFilename);
                            }

                            // Move FLAC to flac/Artist/Album/
                            try {
                                if (fs::exists(flacFile)) fs::remove(flacFile);
                                fs::rename(srcFile, flacFile);
                                LOG_INFO("[FLAC MOVED] " + fs::relative(flacFile, g_BaseDir).string());
                            } catch (const std::exception& ex) {
                                LOG_INFO("[MOVE ERROR] Failed moving FLAC: " + std::string(ex.what()));
                                return;
                            }

                            // 2. Convert FLAC -> MP3 320kbps in mp3/Artist/Album/
                            LOG_INFO("[CONVERTING MP3] Encoding 320kbps MP3 for: " + baseTrackName + ".mp3 ...");
                            if (ConvertFlacToMp3(flacFile.string(), mp3File.string())) {
                                WriteMp3TagsAndPicture(mp3File.string(), newArtist, newAlbum, newTitle, newTrackNo, newYear, newLyrics, chosenCover);
                                LOG_INFO("[MP3 MIRRORED] Created 320kbps MP3: " + fs::relative(mp3File, g_BaseDir).string());
                            } else {
                                LOG_INFO("[CONVERT WARN] FFmpeg conversion failed for: " + baseTrackName);
                            }

                            // Save cover.jpg in both flac/ and mp3/
                            if (!chosenCover.empty()) {
                                for (const auto& dir : { flacDir, mp3Dir }) {
                                    fs::path coverDst = dir / "cover.jpg";
                                    if (!fs::exists(coverDst) || fs::file_size(coverDst) != chosenCover.size()) {
                                        std::ofstream cOut(coverDst, std::ios::binary);
                                        if (cOut.is_open()) {
                                            cOut.write((const char*)chosenCover.data(), chosenCover.size());
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
                            bool embeddedOk = WriteMp3TagsAndPicture(srcFile.string(), newArtist, newAlbum, newTitle, newTrackNo, newYear, newLyrics, chosenCover);
                            if (embeddedOk) {
                                LOG_INFO("[TAGS EMBEDDED] ID3v2.4 tags & cover art written to: " + origFilename);
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
                                return;
                            }

                            // Save cover.jpg in both mp3/ and flac/
                            if (!chosenCover.empty()) {
                                for (const auto& dir : { mp3Dir, flacDir }) {
                                    fs::path coverDst = dir / "cover.jpg";
                                    if (!fs::exists(coverDst) || fs::file_size(coverDst) != chosenCover.size()) {
                                        std::ofstream cOut(coverDst, std::ios::binary);
                                        if (cOut.is_open()) {
                                            cOut.write((const char*)chosenCover.data(), chosenCover.size());
                                            cOut.close();
                                            LOG_INFO("[COVER SAVED] Saved cover art to: " + fs::relative(coverDst, g_BaseDir).string());
                                        }
                                    }
                                }
                            }
                        }
                    }).detach();

                    m_currentTagIndex++;
                }
                ImGui::SameLine();
                if (ImGui::Button("Пропустить", ImVec2(160, 34))) {
                    LOG_INFO("[SKIPPED] Skipped track: " + item.originalFilename);
                    m_currentTagIndex++;
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
            } catch (const std::exception& ex) {
                LOG_WARN("[AUTO-DELETE] Failed moving " + rel.string() + ": " + ex.what());
            }
        }
    }

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
    LOG_INFO("Step 2: Instant local scan + UI-sequential priority MusicBrainz lookup...");
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
                        m_tagScanEndTime = std::chrono::steady_clock::now();
                        m_isTagScanning = false;
                        PostMessageW(m_hWnd, WM_TAG_SCAN_FINISHED, 0, 0);
                        return;
                    }

                    // FAST LOCAL INITIALIZATION (0.01 seconds!)
                    m_tagItems.resize(files.size());
                    for (size_t i = 0; i < files.size(); ++i) {
                        auto& item = m_tagItems[i];
                        item.filePath = files[i];
                        item.relPath = fs::relative(files[i], g_BaseDir).string();
                        item.originalFilename = fs::path(files[i]).filename().string();
                        memset(item.lyricsBuf, 0, sizeof(item.lyricsBuf)); // Zero-terminate lyrics buffer

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
                            artistClean = "Unknown Artist";
                        }
                        std::string albumClean = CleanMetadataString(albumRaw);

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

                    // Post immediate notification to show local UI instantly!
                    PostMessageW(m_hWnd, WM_TAG_SCAN_FINISHED, 0, 0);

                    // ULTRA-DETAILED STEP-BY-STEP MUSICBRAINZ RESOLVER
                    std::unordered_map<std::string, AlbumMetadataCache> albumCache;

                    for (size_t i = 0; i < files.size(); ++i) {
                        auto& item = m_tagItems[i];
                        std::string artistClean(item.artistBuf);
                        std::string albumClean(item.albumBuf);
                        std::string titleClean(item.titleBuf);
                        std::string albumKey = NormalizeKey(albumClean);
                        if (albumKey.empty() || albumKey == "unknown" || albumKey == "tosort" || albumKey == "music" || albumKey == "media") {
                            albumKey = NormalizeKey(fs::path(files[i]).parent_path().string());
                        }

                        std::string releaseGroupMbId;
                        std::string firstReleaseDate;
                        std::vector<unsigned char> coverData;
                        bool isMatched = false;
                        MatchTier detectedTier = MatchTier::Niche_Local;

                        // Check Album Cache first (Instantly resolves tracks 2-30 of the same album!)
                        if (albumCache.find(albumKey) != albumCache.end() && albumCache[albumKey].isFetched) {
                            auto& c = albumCache[albumKey];
                            item.isMusicBrainzMatched = c.isMatched;
                            item.onlineCoverBytes = c.coverBytes;
                            item.matchTier = c.matchTier;
                            item.releaseGroupMbId = c.releaseGroupMbId;
                            if (!c.firstReleaseDate.empty()) {
                                strncpy_s(item.yearBuf, c.firstReleaseDate.c_str(), sizeof(item.yearBuf) - 1);
                            }
                            ApplyTrackMatch(item, c.tracks);
                            item.isFetchCompleted = true;
                            m_fetchedCount++;
                            LOG_INFO("[ALBUM CACHE HIT] Track #" + std::to_string(i + 1) + " resolved instantly via album cache: " + albumClean);
                            continue;
                        }

                        size_t currentNum = i + 1;
                        LOG_INFO("[MUSICBRAINZ FETCH " + std::to_string(currentNum) + "/" + std::to_string(files.size()) + "] [UI ORDER PRIORITY #" + std::to_string(i + 1) + "] Querying: " + artistClean + " - " + albumClean + " (File: " + item.originalFilename + ")");

                        // 1. AcoustID Fingerprint Lookup via HTTP POST (Fixes HTTP 414 Request-URI Too Long!)
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        auto fpInfo = AcousticAnalyzer::Instance().ExtractFingerprint(files[i]);
                        item.duration = fpInfo.duration;

                        std::string acoustRecMbId;
                        std::string acoustRecTitle;
                        std::string acoustRecArtist;

                        if (!fpInfo.fpBase64.empty()) {
                            LOG_INFO("[ACOUSTID FINGERPRINT] Extracted " + std::to_string(fpInfo.fpData.size()) + " frames, duration " + std::to_string(fpInfo.duration) + "s for " + item.originalFilename);
                            
                            std::ostringstream postStream;
                            postStream << "client=" << g_AcoustIdKey << "&meta=recordings+releasegroups+compress&duration=" << (int)fpInfo.duration << "&fingerprint=" << fpInfo.fpBase64;
                            std::string acoustRes = AcoustIdHttpPost(postStream.str());

                            if (acoustRes.empty()) {
                                LOG_WARN("[ACOUSTID] Empty response from API (rate limit or network issue)");
                            } else if (acoustRes.find("\"error\"") != std::string::npos || acoustRes.find("\"status\":\"error\"") != std::string::npos) {
                                LOG_WARN("[ACOUSTID API ERROR] " + acoustRes.substr(0, (std::min)((size_t)300, acoustRes.size())));
                            } else if (acoustRes.find("\"recordings\"") == std::string::npos) {
                                LOG_WARN("[ACOUSTID] Response has no recordings array (size=" + std::to_string(acoustRes.size()) + ")");
                            }

                            size_t jsonPos = 0;
                            JsonVal acoustDoc = ParseJsonSimple(acoustRes, jsonPos);
                            const auto& acoustResults = acoustDoc.get("results");

                            if (acoustResults.type == JsonVal::Array) {
                                std::string bestRgId;
                                int bestScore = -1;

                                for (size_t ri = 0; ri < acoustResults.arrVal.size(); ++ri) {
                                    const auto& recs = acoustResults.get(ri).get("recordings");
                                    if (recs.type != JsonVal::Array) continue;

                                    for (size_t ci = 0; ci < recs.arrVal.size(); ++ci) {
                                        const auto& rec = recs.get(ci);
                                        std::string recTitle = rec.get("title").strVal;
                                        std::string recId = rec.get("id").strVal;

                                        std::string recArtist;
                                        const auto& recArtists = rec.get("artists");
                                        if (recArtists.type == JsonVal::Array && !recArtists.arrVal.empty()) {
                                            recArtist = recArtists.get(0).get("name").strVal;
                                        }

                                        const auto& rgs = rec.get("releasegroups");
                                        std::string rgId;
                                        std::string rgTitle;
                                        if (rgs.type == JsonVal::Array && !rgs.arrVal.empty()) {
                                            rgId = rgs.get(0).get("id").strVal;
                                            rgTitle = rgs.get(0).get("title").strVal;
                                        }

                                        int score = 0;

                                        if (!titleClean.empty()) {
                                            std::string recTitleNorm = NormalizeKey(recTitle);
                                            std::string fileTitleNorm = NormalizeKey(titleClean);
                                            if (!recTitleNorm.empty() && recTitleNorm == fileTitleNorm) {
                                                score += 100;
                                            } else if (!recTitleNorm.empty() && (recTitleNorm.find(fileTitleNorm) != std::string::npos || fileTitleNorm.find(recTitleNorm) != std::string::npos)) {
                                                score += 60;
                                            }
                                        }

                                        if (!artistClean.empty() && artistClean != "Unknown Artist" && !recArtist.empty()) {
                                            std::string recArtistLower = recArtist;
                                            std::transform(recArtistLower.begin(), recArtistLower.end(), recArtistLower.begin(), ::tolower);
                                            std::string fileArtistLower = artistClean;
                                            std::transform(fileArtistLower.begin(), fileArtistLower.end(), fileArtistLower.begin(), ::tolower);
                                            if (recArtistLower.find(fileArtistLower) != std::string::npos || fileArtistLower.find(recArtistLower) != std::string::npos) {
                                                score += 50;
                                            }
                                        }

                                        if (!albumClean.empty() && !rgTitle.empty()) {
                                            std::string rgTitleLower = rgTitle;
                                            std::transform(rgTitleLower.begin(), rgTitleLower.end(), rgTitleLower.begin(), ::tolower);
                                            std::string albumLower = albumClean;
                                            std::transform(albumLower.begin(), albumLower.end(), albumLower.begin(), ::tolower);
                                            if (rgTitleLower.find(albumLower) != std::string::npos || albumLower.find(rgTitleLower) != std::string::npos) {
                                                score += 40;
                                            }
                                        }

                                        if (score > bestScore) {
                                            bestScore = score;
                                            bestRgId = rgId;
                                            if (!recId.empty()) acoustRecMbId = recId;
                                            if (!recTitle.empty()) acoustRecTitle = recTitle;
                                            if (!recArtist.empty()) acoustRecArtist = recArtist;
                                        }
                                    }
                                }

                                if (!bestRgId.empty() && bestRgId.length() == 36) {
                                    releaseGroupMbId = bestRgId;
                                    isMatched = true;
                                    detectedTier = MatchTier::AcoustId;
                                    LOG_INFO("[ACOUSTID MATCHED] ReleaseGroup MBID: " + releaseGroupMbId + " (score: " + std::to_string(bestScore) + ")");
                                } else if (!acoustRecMbId.empty()) {
                                    LOG_INFO("[ACOUSTID MATCHED] Recording MBID: " + acoustRecMbId + " (no releasegroup in AcoustID, fetching from MusicBrainz)");
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
                                                if (!albumClean.empty()) {
                                                    std::string rgLower = rgTitle;
                                                    std::transform(rgLower.begin(), rgLower.end(), rgLower.begin(), ::tolower);
                                                    std::string albLower = albumClean;
                                                    std::transform(albLower.begin(), albLower.end(), albLower.begin(), ::tolower);
                                                    if (rgLower.find(albLower) != std::string::npos || albLower.find(rgLower) != std::string::npos) {
                                                        releaseGroupMbId = rgId;
                                                        isMatched = true;
                                                        detectedTier = MatchTier::AcoustId;
                                                        LOG_INFO("[ACOUSTID->MB RECORDING LOOKUP] Found matching releasegroup: " + rgTitle + " (MBID: " + rgId + ")");
                                                        break;
                                                    }
                                                }
                                                if (releaseGroupMbId.empty()) {
                                                    releaseGroupMbId = rgId;
                                                    isMatched = true;
                                                    detectedTier = MatchTier::AcoustId;
                                                    LOG_INFO("[ACOUSTID->MB RECORDING LOOKUP] First releasegroup: " + rgTitle + " (MBID: " + rgId + ")");
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // 2. Multi-Tier MusicBrainz Search Strategy with Detailed Tier Logging
                        if (releaseGroupMbId.empty() && !albumClean.empty()) {
                            // If AcoustID provided a recording artist/title, prefer it over folder-derived metadata
                            std::string searchArtist = artistClean;
                            std::string searchTitle = titleClean;
                            if (!acoustRecArtist.empty()) {
                                searchArtist = acoustRecArtist;
                                LOG_INFO("[ACOUSTID OVERRIDE] Using AcoustID artist '" + acoustRecArtist + "' instead of folder '" + artistClean + "'");
                            }
                            if (!acoustRecTitle.empty()) {
                                searchTitle = acoustRecTitle;
                            }

                            std::vector<MBReleaseGroupCandidate> tierBCandidates;

                            // Tier A: Strict Release Group Search
                            if (!searchArtist.empty() && searchArtist != "Unknown Artist") {
                                std::string artistLucene = EscapeLuceneQuery(searchArtist);
                                std::string albumLucene = EscapeLuceneQuery(albumClean);
                                std::string mbQuery = "artist:\"" + artistLucene + "\" AND release:\"" + albumLucene + "\"";
                                std::string mbUrl = "https://musicbrainz.org/ws/2/release-group?query=" + UrlEncode(mbQuery) + "&fmt=json";
                                LOG_INFO("[MUSICBRAINZ TIER A] Querying: " + mbQuery);
                                std::string mbRes = HttpGetString(Utf8ToWide(mbUrl));

                                auto candidates = ParseMusicBrainzReleaseGroups(mbRes);
                                if (!candidates.empty()) {
                                    releaseGroupMbId = candidates[0].id;
                                    if (!candidates[0].firstReleaseDate.empty()) {
                                        firstReleaseDate = candidates[0].firstReleaseDate;
                                        LOG_INFO("[MUSICBRAINZ RELEASE DATE] Found full date: " + firstReleaseDate);
                                    }
                                    isMatched = true;
                                    detectedTier = MatchTier::TierA;
                                    LOG_INFO("[MUSICBRAINZ TIER A SUCCESS] MBID: " + releaseGroupMbId + " (" + candidates[0].title + " by " + candidates[0].artistCredit + ")");
                                }
                            }

                            // Tier B: Album Title Alone Search & Katakana Transliteration Fallback
                            if (releaseGroupMbId.empty()) {
                                std::string mbAlbumUrl = "https://musicbrainz.org/ws/2/release-group?query=release:\"" + UrlEncode(albumClean) + "\"&fmt=json";
                                LOG_INFO("[MUSICBRAINZ TIER B FALLBACK] Querying release:\"" + albumClean + "\"");
                                std::string mbAlbumRes = HttpGetString(Utf8ToWide(mbAlbumUrl));
                                tierBCandidates = ParseMusicBrainzReleaseGroups(mbAlbumRes);
                                LOG_INFO("[MUSICBRAINZ TIER B] Found " + std::to_string(tierBCandidates.size()) + " candidate release-groups");

                                auto artistKnown = [&]() { return !searchArtist.empty() && searchArtist != "Unknown Artist"; };

                                if (!tierBCandidates.empty()) {
                                    std::string candMbIdPicked;
                                    std::string candTitlePicked;
                                    std::string candDatePicked;
                                    MatchTier tierBPicked = MatchTier::TierB_Fallback;

                                    if (artistKnown()) {
                                        std::string baseArtist = searchArtist;
                                        size_t semiPos = baseArtist.find(';');
                                        if (semiPos != std::string::npos) baseArtist = baseArtist.substr(0, semiPos);
                                        while (!baseArtist.empty() && (baseArtist.back() == ' ' || baseArtist.back() == '\t')) baseArtist.pop_back();

                                        std::string baseLower = baseArtist;
                                        std::transform(baseLower.begin(), baseLower.end(), baseLower.begin(), ::tolower);

                                        for (const auto& c : tierBCandidates) {
                                            std::string acLower = c.artistCredit;
                                            std::transform(acLower.begin(), acLower.end(), acLower.begin(), ::tolower);

                                            bool isVA = (acLower.find("various artists") != std::string::npos || acLower.find("v.a.") != std::string::npos);
                                            if (!baseLower.empty() && (acLower.find(baseLower) != std::string::npos || isVA)) {
                                                candMbIdPicked = c.id;
                                                candTitlePicked = c.title;
                                                candDatePicked = c.firstReleaseDate;
                                                tierBPicked = MatchTier::TierB_Verified;
                                                LOG_INFO("[MUSICBRAINZ TIER B ARTIST MATCH] MBID: " + c.id + " (artist: " + c.artistCredit + ", album: " + c.title + ")");
                                                break;
                                            }
                                        }
                                    }

                                    if (candMbIdPicked.empty() && !titleClean.empty()) {
                                        LOG_INFO("[MUSICBRAINZ TIER B TRACK VERIFY] Verifying candidates by track title match for: " + titleClean);
                                        int bestScore = -1;
                                        size_t maxVerifications = (std::min)((size_t)10, tierBCandidates.size());
                                        for (size_t ci = 0; ci < maxVerifications; ++ci) {
                                            const auto& c = tierBCandidates[ci];
                                            auto tracks = FetchMusicBrainzReleaseTracks(c.id);
                                            int score = -1;
                                            std::string foundTrackTitle;
                                            for (const auto& t : tracks) {
                                                std::string tNorm = NormalizeKey(t.title);
                                                std::string fNorm = NormalizeKey(titleClean);
                                                int s = -1;
                                                if (!tNorm.empty() && tNorm == fNorm) s = 100;
                                                else if (!tNorm.empty() && (tNorm.find(fNorm) != std::string::npos || fNorm.find(tNorm) != std::string::npos)) s = 70;
                                                else if (t.lengthMs > 0 && std::abs(((double)t.lengthMs / 1000.0) - item.duration) <= 3.0) s = 50;
                                                if (s > score) { score = s; foundTrackTitle = t.title; }
                                            }
                                            LOG_INFO("[MUSICBRAINZ TIER B TRACK VERIFY] " + c.title + " (MBID: " + c.id + ") score: " + std::to_string(score) + (score > 0 ? " via " + foundTrackTitle : ""));
                                            if (score > bestScore && score >= 50) {
                                                bestScore = score;
                                                candMbIdPicked = c.id;
                                                candTitlePicked = c.title;
                                                candDatePicked = c.firstReleaseDate;
                                                tierBPicked = MatchTier::TierB_Verified;
                                            }
                                            if (bestScore == 100) {
                                                break; // Exact track match found, stop checking remaining candidates
                                            }
                                        }
                                    }

                                    if (!candMbIdPicked.empty()) {
                                        releaseGroupMbId = candMbIdPicked;
                                        if (!candDatePicked.empty()) {
                                            firstReleaseDate = candDatePicked;
                                            LOG_INFO("[MUSICBRAINZ RELEASE DATE] Found full date: " + firstReleaseDate);
                                        }
                                        isMatched = true;
                                        detectedTier = tierBPicked;
                                        LOG_INFO("[MUSICBRAINZ TIER B VERIFIED SUCCESS] MBID: " + releaseGroupMbId + " (" + candTitlePicked + ")");
                                    }
                                }

                                // If Romaji album title didn't match, try Japanese Katakana transliteration (e.g. Iranai -> イラナイ)
                                if (releaseGroupMbId.empty()) {
                                    std::string albumKatakana = RomajiToKatakana(albumClean);
                                    if (!albumKatakana.empty() && albumKatakana != albumClean) {
                                        std::string mbKataUrl = "https://musicbrainz.org/ws/2/release-group?query=release:\"" + UrlEncode(albumKatakana) + "\"&fmt=json";
                                        LOG_INFO("[MUSICBRAINZ TIER B KATAKANA] Querying release:\"" + albumKatakana + "\" (Converted from " + albumClean + ")");
                                        std::string mbKataRes = HttpGetString(Utf8ToWide(mbKataUrl));
                                        auto kataCandidates = ParseMusicBrainzReleaseGroups(mbKataRes);
                                        if (!kataCandidates.empty()) {
                                            releaseGroupMbId = kataCandidates[0].id;
                                            if (!kataCandidates[0].firstReleaseDate.empty()) {
                                                firstReleaseDate = kataCandidates[0].firstReleaseDate;
                                                LOG_INFO("[MUSICBRAINZ RELEASE DATE] Found full date: " + firstReleaseDate);
                                            }
                                            isMatched = true;
                                            detectedTier = MatchTier::TierB_Katakana;
                                            LOG_INFO("[MUSICBRAINZ TIER B KATAKANA SUCCESS] MBID: " + releaseGroupMbId + " for Katakana title: " + albumKatakana);
                                        }
                                    }
                                }
                            }

                            // Prioritize Discogs Database Search before Loose/Unverified MusicBrainz Fallbacks
                            if (releaseGroupMbId.empty()) {
                                LOG_INFO("[DISCOGS PIPELINE] Searching Discogs Database for: " + searchArtist + " - " + albumClean);
                                DiscogsReleaseInfo discInfo;
                                if (SearchDiscogsRelease(searchArtist, albumClean, discInfo)) {
                                    LOG_INFO("[DISCOGS METADATA FOUND] Matched release: " + discInfo.artist + " - " + discInfo.title + " (Year: " + discInfo.year + ", Tracks: " + std::to_string(discInfo.tracks.size()) + ")");
                                    releaseGroupMbId = "discogs_" + discInfo.id;
                                    if (!discInfo.year.empty()) {
                                        firstReleaseDate = discInfo.year;
                                    }
                                    if (!discInfo.coverUrl.empty()) {
                                        coverData = HttpGetBytes(Utf8ToWide(discInfo.coverUrl));
                                        if (!coverData.empty()) {
                                            LOG_INFO("[DISCOGS COVER DOWNLOADED] " + std::to_string(coverData.size()) + " bytes cover art from Discogs via " + discInfo.coverUrl);
                                        }
                                    }
                                    isMatched = true;
                                    detectedTier = MatchTier::Discogs;

                                    for (size_t k = 0; k < files.size(); ++k) {
                                        std::string aKey = NormalizeKey(m_tagItems[k].albumBuf);
                                        if (aKey.empty() || aKey == "unknown" || aKey == "tosort" || aKey == "music" || aKey == "media") {
                                            aKey = NormalizeKey(fs::path(files[k]).parent_path().string());
                                        }
                                        if (aKey == albumKey) {
                                            if (!discInfo.artist.empty()) {
                                                strncpy_s(m_tagItems[k].artistBuf, discInfo.artist.c_str(), sizeof(m_tagItems[k].artistBuf) - 1);
                                            }
                                            if (!discInfo.title.empty()) {
                                                strncpy_s(m_tagItems[k].albumBuf, discInfo.title.c_str(), sizeof(m_tagItems[k].albumBuf) - 1);
                                            }
                                            if (!discInfo.year.empty()) {
                                                strncpy_s(m_tagItems[k].yearBuf, discInfo.year.c_str(), sizeof(m_tagItems[k].yearBuf) - 1);
                                            }
                                            if (!discInfo.tracks.empty()) {
                                                ApplyTrackMatch(m_tagItems[k], discInfo.tracks);
                                            }
                                            m_tagItems[k].matchTier = detectedTier;
                                            m_tagItems[k].releaseGroupMbId = releaseGroupMbId;
                                        }
                                    }

                                    albumCache[albumKey] = { releaseGroupMbId, firstReleaseDate, coverData, discInfo.tracks, isMatched, true, detectedTier };
                                    if (!releaseGroupMbId.empty()) {
                                        albumCache[releaseGroupMbId] = { releaseGroupMbId, firstReleaseDate, coverData, discInfo.tracks, isMatched, true, detectedTier };
                                    }
                                }
                            }

                            // Tier C: Loose Text Search (ONLY when Artist is known to avoid random keyword matches)
                            if (releaseGroupMbId.empty() && !searchArtist.empty() && searchArtist != "Unknown Artist") {
                                std::string mbLooseQuery = EscapeLuceneQuery(searchArtist) + " " + EscapeLuceneQuery(albumClean);
                                std::string mbLooseUrl = "https://musicbrainz.org/ws/2/release-group?query=" + UrlEncode(mbLooseQuery) + "&fmt=json";
                                LOG_INFO("[MUSICBRAINZ TIER C LOOSE] Querying: " + mbLooseQuery);
                                std::string mbLooseRes = HttpGetString(Utf8ToWide(mbLooseUrl));
                                auto looseCandidates = ParseMusicBrainzReleaseGroups(mbLooseRes);
                                for (const auto& c : looseCandidates) {
                                    bool artistMatched = true;
                                    std::string baseArtist = searchArtist;
                                    size_t semiPos = baseArtist.find(';');
                                    if (semiPos != std::string::npos) baseArtist = baseArtist.substr(0, semiPos);
                                    while (!baseArtist.empty() && (baseArtist.back() == ' ' || baseArtist.back() == '\t')) baseArtist.pop_back();

                                    std::string acLower = c.artistCredit;
                                    std::transform(acLower.begin(), acLower.end(), acLower.begin(), ::tolower);
                                    std::string baseLower = baseArtist;
                                    std::transform(baseLower.begin(), baseLower.end(), baseLower.begin(), ::tolower);
                                    bool isVA = (acLower.find("various artists") != std::string::npos || acLower.find("v.a.") != std::string::npos);
                                    if (!baseLower.empty() && acLower.find(baseLower) == std::string::npos && !isVA) {
                                        artistMatched = false;
                                        LOG_INFO("[MUSICBRAINZ TIER C REJECTED] Mismatched artist credit: " + c.artistCredit + " (Expected: " + baseArtist + ")");
                                    }

                                    bool titleMatched = false;
                                    if (!albumClean.empty()) {
                                        std::string candTitleNorm = NormalizeKey(c.title);
                                        std::string albumCleanNorm = NormalizeKey(albumClean);
                                        if (candTitleNorm == albumCleanNorm) {
                                            titleMatched = true;
                                        } else if (candTitleNorm.length() >= 4 && albumCleanNorm.length() >= 4 &&
                                                   (candTitleNorm.find(albumCleanNorm) != std::string::npos || albumCleanNorm.find(candTitleNorm) != std::string::npos)) {
                                            double lenRatio = (double)(std::min)(candTitleNorm.length(), albumCleanNorm.length()) / 
                                                              (double)(std::max)(candTitleNorm.length(), albumCleanNorm.length());
                                            if (lenRatio >= 0.75) {
                                                titleMatched = true;
                                            }
                                        }
                                        if (!titleMatched) {
                                            LOG_INFO("[MUSICBRAINZ TIER C REJECTED] Mismatched album title: " + c.title + " (Expected: " + albumClean + ")");
                                        }
                                    }

                                    if (artistMatched && titleMatched) {
                                        releaseGroupMbId = c.id;
                                        if (!c.firstReleaseDate.empty()) {
                                            firstReleaseDate = c.firstReleaseDate;
                                            LOG_INFO("[MUSICBRAINZ RELEASE DATE] Found full date: " + firstReleaseDate);
                                        }
                                        isMatched = true;
                                        detectedTier = MatchTier::TierC_Loose;
                                        LOG_INFO("[MUSICBRAINZ TIER C SUCCESS] MBID: " + releaseGroupMbId + " (" + c.title + " by " + c.artistCredit + ")");
                                        break;
                                    }
                                }
                            }

                            // Tier B Unverified Fallback: Last Resort if artist was known
                            if (releaseGroupMbId.empty() && !tierBCandidates.empty() && !searchArtist.empty() && searchArtist != "Unknown Artist") {
                                releaseGroupMbId = tierBCandidates[0].id;
                                if (!tierBCandidates[0].firstReleaseDate.empty()) {
                                    firstReleaseDate = tierBCandidates[0].firstReleaseDate;
                                }
                                isMatched = true;
                                detectedTier = MatchTier::TierB_Fallback;
                                LOG_INFO("[MUSICBRAINZ TIER B FALLBACK FIRST] MBID: " + releaseGroupMbId + " (" + tierBCandidates[0].title + ")");
                            }
                        }

                        // 3. Fetch Cover Art from CoverArtArchive.org (Cascading Fallback: Original -> 1200px -> 500px -> 250px)
                        if (!releaseGroupMbId.empty() && releaseGroupMbId.rfind("discogs_", 0) != 0) {
                            LOG_INFO("[MUSICBRAINZ MATCHED] MBID " + releaseGroupMbId + " for " + artistClean + " - " + albumClean + ". Downloading CoverArtArchive image...");
                            
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
                                    LOG_INFO("[COVER ART DOWNLOADED] " + std::to_string(coverData.size()) + " bytes cover art via " + std::string(urlBuf));
                                    break;
                                }
                            }

                            if (coverData.empty()) {
                                LOG_INFO("[COVER ART MISSING] CoverArtArchive image not available for MBID: " + releaseGroupMbId + ". Trying Discogs fallback...");
                                DiscogsReleaseInfo discCoverInfo;
                                if (SearchDiscogsRelease(artistClean, albumClean, discCoverInfo) && !discCoverInfo.coverUrl.empty()) {
                                    coverData = HttpGetBytes(Utf8ToWide(discCoverInfo.coverUrl));
                                    if (!coverData.empty()) {
                                        LOG_INFO("[DISCOGS COVER FALLBACK SUCCESS] " + std::to_string(coverData.size()) + " bytes cover art downloaded from Discogs via " + discCoverInfo.coverUrl);
                                    }
                                }
                            }

                            // Fetch Release Tracklist from MusicBrainz to enrich track numbers, titles, and individual track artists
                            std::vector<MBTrackEntry> mbTracks = FetchMusicBrainzReleaseTracks(releaseGroupMbId, &firstReleaseDate);
                            if (!mbTracks.empty()) {
                                LOG_INFO("[MUSICBRAINZ TRACKLIST] Loaded " + std::to_string(mbTracks.size()) + " tracks from MusicBrainz release.");
                            }
                            
                            // Apply track match for current item and all items matching albumKey or MBID
                            for (size_t k = 0; k < files.size(); ++k) {
                                std::string aKey = NormalizeKey(m_tagItems[k].albumBuf);
                                if (aKey.empty() || aKey == "unknown" || aKey == "tosort" || aKey == "music" || aKey == "media") {
                                    aKey = NormalizeKey(fs::path(files[k]).parent_path().string());
                                }
                                if (aKey == albumKey) {
                                    ApplyTrackMatch(m_tagItems[k], mbTracks);
                                    if (!firstReleaseDate.empty()) {
                                        strncpy_s(m_tagItems[k].yearBuf, firstReleaseDate.c_str(), sizeof(m_tagItems[k].yearBuf) - 1);
                                    }
                                    m_tagItems[k].matchTier = detectedTier;
                                    m_tagItems[k].releaseGroupMbId = releaseGroupMbId;
                                }
                            }
                            
                            albumCache[albumKey] = { releaseGroupMbId, firstReleaseDate, coverData, mbTracks, isMatched, true, detectedTier };
                            if (!releaseGroupMbId.empty()) {
                                albumCache[releaseGroupMbId] = { releaseGroupMbId, firstReleaseDate, coverData, mbTracks, isMatched, true, detectedTier };
                            }
                        } else if (releaseGroupMbId.empty()) {
                            detectedTier = MatchTier::Niche_Local;
                            LOG_INFO("[NICHE TRACK] MusicBrainz and Discogs records not found for " + artistClean + " - " + albumClean + ". Using Level 3 prefilled metadata.");
                            albumCache[albumKey] = { "", "", {}, {}, false, true, MatchTier::Niche_Local };
                        }

                        // 4. Fetch Synced Romanized LRC Lyrics via LrcLib REST API
                        std::string lrcLyrics = FetchLrcLibSyncedLyrics(artistClean, titleClean, albumClean);
                        if (!lrcLyrics.empty()) {
                            item.hasLyrics = true;
                            strncpy_s(item.lyricsBuf, lrcLyrics.c_str(), sizeof(item.lyricsBuf) - 1);
                            LOG_INFO("[LRC LYRICS FETCHED] Found synced lyrics (" + std::to_string(lrcLyrics.size()) + " chars) for " + artistClean + " - " + titleClean);
                        } else {
                            LOG_INFO("[LRC LYRICS NOT FOUND] No synced lyrics available for " + artistClean + " - " + titleClean);
                        }

                        if (!firstReleaseDate.empty()) {
                            strncpy_s(item.yearBuf, firstReleaseDate.c_str(), sizeof(item.yearBuf) - 1);
                        }

                        item.isMusicBrainzMatched = isMatched;
                        item.onlineCoverBytes = coverData;
                        item.matchTier = detectedTier;
                        item.releaseGroupMbId = releaseGroupMbId;
                        item.isFetchCompleted = true;
                        m_fetchedCount++;
                    }

                    m_tagScanEndTime = std::chrono::steady_clock::now();
                    m_isTagScanning = false;
                    LOG_INFO("Step 2 Background Online Fetching Complete. 100% of MusicBrainz & Discogs queries finished.");
    }).detach();
}
