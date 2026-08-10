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

#pragma comment(lib, "wininet.lib")

namespace fs = std::filesystem;

extern std::string g_BaseDir;
extern std::string g_ToSortDir;
extern std::string g_DeleteDir;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct MBTrackEntry {
    int position = 0;
    std::string title;
    std::string artist;
};

struct AlbumMetadataCache {
    std::string releaseGroupMbId;
    std::string firstReleaseDate;
    std::vector<unsigned char> coverBytes;
    std::vector<MBTrackEntry> tracks;
    bool isMatched = false;
    bool isFetched = false;
};

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
    HINTERNET hNet = InternetOpenW(L"MusicSorterApp/2.0 (contact@musicsorter.org)", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
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

// Robust HTTP GET with Ultra-Detailed Step-by-Step Logging & Rate Limit Backoff
std::vector<unsigned char> HttpGetBytes(const std::wstring& url, int maxRetries = 3) {
    std::vector<unsigned char> result;
    std::string narrowUrl(url.begin(), url.end());

    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        HINTERNET hNet = InternetOpenW(L"MusicSorterApp/2.0 (contact@musicsorter.org)", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (hNet) {
            DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
            HINTERNET hFile = InternetOpenUrlW(hNet, url.c_str(), NULL, 0, flags, 0);
            if (!hFile) {
                flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
                hFile = InternetOpenUrlW(hNet, url.c_str(), NULL, 0, flags, 0);
            }
            if (hFile) {
                DWORD statusCode = 0;
                DWORD statusSize = sizeof(statusCode);
                HttpQueryInfoW(hFile, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, NULL);

                if (statusCode == 429 || statusCode == 503) {
                    LOG_INFO("[HTTP " + std::to_string(statusCode) + " RATE LIMIT] Backing off " + std::to_string(400 * (attempt + 1)) + "ms for URL: " + narrowUrl);
                    InternetCloseHandle(hFile);
                    InternetCloseHandle(hNet);
                    std::this_thread::sleep_for(std::chrono::milliseconds(400 * (attempt + 1)));
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

static std::vector<MBTrackEntry> FetchMusicBrainzReleaseTracks(const std::string& releaseGroupMbId) {
    std::vector<MBTrackEntry> tracks;
    if (releaseGroupMbId.empty()) return tracks;

    std::string url = "https://musicbrainz.org/ws/2/release?release-group=" + releaseGroupMbId + "&inc=recordings+artist-credits&fmt=json";
    std::string resJson = HttpGetString(Utf8ToWide(url));
    if (resJson.empty()) return tracks;

    size_t relsPos = resJson.find("\"releases\":");
    if (relsPos == std::string::npos) return tracks;

    size_t mediaPos = resJson.find("\"media\":", relsPos);
    if (mediaPos == std::string::npos) return tracks;

    size_t tracksPos = resJson.find("\"tracks\":", mediaPos);
    if (tracksPos == std::string::npos) return tracks;

    size_t cur = tracksPos;
    while (cur < resJson.size()) {
        size_t p1 = resJson.find("\"position\":", cur);
        if (p1 == std::string::npos) break;

        size_t pNext = resJson.find("\"position\":", p1 + 11);
        size_t blockEnd = (pNext != std::string::npos) ? pNext : resJson.size();
        std::string block = resJson.substr(p1, blockEnd - p1);

        int pos = 0;
        size_t posIdx = block.find("\"position\":");
        if (posIdx != std::string::npos) {
            posIdx += 11;
            while (posIdx < block.size() && (block[posIdx] == ' ' || block[posIdx] == ':')) posIdx++;
            size_t endP = block.find_first_of(",}", posIdx);
            if (endP != std::string::npos) {
                try { pos = std::stoi(block.substr(posIdx, endP - posIdx)); } catch (...) {}
            }
        }

        std::string title;
        size_t titleIdx = block.find("\"title\":\"");
        if (titleIdx != std::string::npos) {
            titleIdx += 9;
            size_t tendP = block.find("\"", titleIdx);
            if (tendP != std::string::npos) {
                title = block.substr(titleIdx, tendP - titleIdx);
            }
        }

        std::string artist;
        size_t acIdx = block.find("\"artist-credit\":");
        if (acIdx != std::string::npos) {
            size_t nameIdx = block.find("\"name\":\"", acIdx);
            if (nameIdx != std::string::npos) {
                nameIdx += 8;
                size_t nendP = block.find("\"", nameIdx);
                if (nendP != std::string::npos) {
                    artist = block.substr(nameIdx, nendP - nameIdx);
                }
            }
        }

        if (pos > 0 && !title.empty()) {
            tracks.push_back({ pos, title, artist });
        }
        cur = (pNext != std::string::npos) ? pNext : std::string::npos;
    }
    return tracks;
}

static void ApplyTrackMatch(TagReviewItem& albItem, const std::vector<MBTrackEntry>& mbTracks) {
    if (mbTracks.empty()) return;

    std::string rawName = albItem.originalFilename;
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

        if (tTitleClean.length() >= 3) {
            if (!tTitleClean.empty() && rawClean.find(tTitleClean) != std::string::npos) {
                score += 60;
            }
        } else if (!tTitleClean.empty()) {
            if (rawClean == tTitleClean || rawClean.ends_with(tTitleClean)) {
                score += 60;
            }
        }

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
    fs::path flacRoot = fs::path(g_BaseDir) / "flac";
    fs::path mp3Root = fs::path(g_BaseDir) / "mp3";

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
    for (const auto& sub : { "flac", "mp3", "TO SORT", "review" }) {
        fs::path dir = fs::path(g_BaseDir) / sub;
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
    ImGui::CreateContext();
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

void AppWindow::Cleanup() {
    for (auto& item : m_tagItems) {
        if (item.localTexture) item.localTexture->Release();
        if (item.onlineTexture) item.onlineTexture->Release();
    }
    m_tagItems.clear();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(m_hWnd);
    UnregisterClassW(L"MusicSorterImGuiClass", m_hInstance);
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

        // Dynamic Texture Creation & Mathematical Perceptual Sharpness Evaluation
        for (auto& item : m_tagItems) {
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
                    
                    // Automatically select mathematically sharper & true higher quality cover art!
                    if (item.onlineScore > item.localScore) {
                        item.selectedCoverChoice = 1;
                    } else {
                        item.selectedCoverChoice = 0;
                    }
                }
            }
        }

        // Start Dear ImGui Frame
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
                fs::path rel = fs::relative(pair.trackB_path, g_BaseDir);
                fs::path dst = fs::path(g_DeleteDir) / rel;
                fs::create_directories(dst.parent_path());
                LOG_INFO("[DECISION] Keeping Track A. Moving rejected Track B to delete/: " + rel.string());
                if (fs::exists(dst)) fs::remove(dst);
                fs::rename(pair.trackB_path, dst);

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
                fs::path rel = fs::relative(pair.trackA_path, g_BaseDir);
                fs::path dst = fs::path(g_DeleteDir) / rel;
                fs::create_directories(dst.parent_path());
                LOG_INFO("[DECISION] Keeping Track B. Moving rejected Track A to delete/: " + rel.string());
                if (fs::exists(dst)) fs::remove(dst);
                fs::rename(pair.trackA_path, dst);

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
                m_isTagScanning = true;
                LOG_INFO("Step 2: Instant local scan + UI-sequential priority MusicBrainz lookup...");
                m_tagItems.clear();
                m_currentTagIndex = 0;
                m_fetchedCount = 0;

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

                    LOG_INFO("[LOCAL INITIALIZATION] Scanned " + std::to_string(files.size()) + " audio files in TO SORT/");

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
                        std::string albumKey = artistClean + "___" + albumClean;

                        std::string releaseGroupMbId;
                        std::string firstReleaseDate;
                        std::vector<unsigned char> coverData;
                        bool isMatched = false;

                        // Check Album Cache first (Instantly resolves tracks 2-30 of the same album!)
                        if (albumCache.find(albumKey) != albumCache.end() && albumCache[albumKey].isFetched) {
                            auto& c = albumCache[albumKey];
                            item.isMusicBrainzMatched = c.isMatched;
                            item.onlineCoverBytes = c.coverBytes;
                            if (!c.firstReleaseDate.empty()) {
                                strncpy_s(item.yearBuf, c.firstReleaseDate.c_str(), sizeof(item.yearBuf) - 1);
                            }
                            ApplyTrackMatch(item, c.tracks);
                            item.isFetchCompleted = true;
                            m_fetchedCount++;
                            LOG_INFO("[ALBUM CACHE HIT] Track #" + std::to_string(i + 1) + " resolved instantly via album cache: " + albumClean);
                            continue;
                        }

                        size_t currentNum = ++m_fetchedCount;
                        LOG_INFO("[MUSICBRAINZ FETCH " + std::to_string(currentNum) + "/" + std::to_string(files.size()) + "] [UI ORDER PRIORITY #" + std::to_string(i + 1) + "] Querying: " + artistClean + " - " + albumClean + " (File: " + item.originalFilename + ")");

                        // 1. AcoustID Fingerprint Lookup via HTTP POST (Fixes HTTP 414 Request-URI Too Long!)
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        auto fpInfo = AcousticAnalyzer::Instance().ExtractFingerprint(files[i]);
                        if (!fpInfo.fpData.empty()) {
                            LOG_INFO("[ACOUSTID FINGERPRINT] Extracted " + std::to_string(fpInfo.fpData.size()) + " frames, duration " + std::to_string(fpInfo.duration) + "s for " + item.originalFilename);
                            
                            std::ostringstream postStream;
                            postStream << "client=8Xa1nV0f&meta=recordings+releasegroups+compress&duration=" << (int)fpInfo.duration << "&fingerprint=";
                            for (size_t k = 0; k < fpInfo.fpData.size(); ++k) {
                                if (k > 0) postStream << ",";
                                postStream << fpInfo.fpData[k];
                            }
                                   std::string acoustRes = AcoustIdHttpPost(postStream.str());
                            size_t rgPos = acoustRes.find("\"releasegroups\":");
                            if (rgPos != std::string::npos) {
                                size_t idPos = acoustRes.find("\"id\":\"", rgPos);
                                if (idPos != std::string::npos) {
                                    idPos += 6;
                                    size_t endPos = acoustRes.find("\"", idPos);
                                    if (endPos != std::string::npos) {
                                        std::string candMbId = acoustRes.substr(idPos, endPos - idPos);
                                        if (candMbId.length() == 36) {
                                            releaseGroupMbId = candMbId;
                                            isMatched = true;
                                            LOG_INFO("[ACOUSTID MATCHED] ReleaseGroup MBID: " + releaseGroupMbId);
                                        }
                                    }
                                }
                            }
                        }

                        // 2. Multi-Tier MusicBrainz Search Strategy with Detailed Tier Logging
                        if (releaseGroupMbId.empty() && !albumClean.empty()) {
                            // Tier A: Strict Release Group Search
                            if (!artistClean.empty() && artistClean != "Unknown Artist") {
                                std::string artistLucene = EscapeLuceneQuery(artistClean);
                                std::string albumLucene = EscapeLuceneQuery(albumClean);
                                std::string mbQuery = "artist:\"" + artistLucene + "\" AND release:\"" + albumLucene + "\"";
                                std::string mbUrl = "https://musicbrainz.org/ws/2/release-group?query=" + UrlEncode(mbQuery) + "&fmt=json";
                                LOG_INFO("[MUSICBRAINZ TIER A] Querying: " + mbQuery);
                                std::string mbRes = HttpGetString(Utf8ToWide(mbUrl));

                                size_t rgPos = mbRes.find("\"release-groups\":");
                                if (rgPos != std::string::npos) {
                                    size_t idPos = mbRes.find("\"id\":\"", rgPos);
                                    if (idPos != std::string::npos) {
                                        idPos += 6;
                                        size_t endPos = mbRes.find("\"", idPos);
                                        if (endPos != std::string::npos) {
                                            std::string candMbId = mbRes.substr(idPos, endPos - idPos);
                                            if (candMbId.length() == 36) {
                                                releaseGroupMbId = candMbId;
                                                isMatched = true;
                                                LOG_INFO("[MUSICBRAINZ TIER A SUCCESS] MBID: " + releaseGroupMbId);
                                            }
                                        }
                                    }
                                    size_t datePos = mbRes.find("\"first-release-date\":\"", rgPos);
                                    if (datePos != std::string::npos) {
                                        datePos += 22;
                                        size_t dendPos = mbRes.find("\"", datePos);
                                        if (dendPos != std::string::npos) {
                                            firstReleaseDate = mbRes.substr(datePos, dendPos - datePos);
                                            LOG_INFO("[MUSICBRAINZ RELEASE DATE] Found full date: " + firstReleaseDate);
                                        }
                                    }
                                }
                            }

                            // Tier B: Album Title Alone Search & Katakana Transliteration Fallback
                            if (releaseGroupMbId.empty()) {
                                std::string mbAlbumUrl = "https://musicbrainz.org/ws/2/release-group?query=release:\"" + UrlEncode(albumClean) + "\"&fmt=json";
                                LOG_INFO("[MUSICBRAINZ TIER B FALLBACK] Querying release:\"" + albumClean + "\"");
                                std::string mbAlbumRes = HttpGetString(Utf8ToWide(mbAlbumUrl));

                                auto parseTierB = [&](const std::string& resJson, const std::string& label) {
                                    size_t argPos = resJson.find("\"release-groups\":");
                                    if (argPos != std::string::npos) {
                                        size_t aidPos = resJson.find("\"id\":\"", argPos);
                                        if (aidPos != std::string::npos) {
                                            aidPos += 6;
                                            size_t aendPos = resJson.find("\"", aidPos);
                                            if (aendPos != std::string::npos) {
                                                std::string candMbId = resJson.substr(aidPos, aendPos - aidPos);
                                                if (candMbId.length() == 36) {
                                                    bool artistMatched = true;
                                                    if (!artistClean.empty() && artistClean != "Unknown Artist") {
                                                        std::string baseArtist = artistClean;
                                                        size_t semiPos = baseArtist.find(';');
                                                        if (semiPos != std::string::npos) baseArtist = baseArtist.substr(0, semiPos);
                                                        while (!baseArtist.empty() && (baseArtist.back() == ' ' || baseArtist.back() == '\t')) baseArtist.pop_back();

                                                        size_t acPos = resJson.find("\"artist-credit\":", argPos);
                                                        if (acPos != std::string::npos && !baseArtist.empty()) {
                                                            std::string acChunk = resJson.substr(acPos, 600);
                                                            std::string acLower = acChunk;
                                                            std::transform(acLower.begin(), acLower.end(), acLower.begin(), ::tolower);
                                                            std::string baseLower = baseArtist;
                                                            std::transform(baseLower.begin(), baseLower.end(), baseLower.begin(), ::tolower);
                                                            
                                                            bool isVA = (acLower.find("various artists") != std::string::npos || acLower.find("v.a.") != std::string::npos);
                                                            
                                                            size_t labelPos = resJson.find("\"label\":", argPos);
                                                            std::string labelChunk;
                                                            if (labelPos != std::string::npos) {
                                                                labelChunk = resJson.substr(labelPos, 600);
                                                                std::transform(labelChunk.begin(), labelChunk.end(), labelChunk.begin(), ::tolower);
                                                            }

                                                            if (acLower.find(baseLower) == std::string::npos &&
                                                                labelChunk.find(baseLower) == std::string::npos &&
                                                                !isVA) {
                                                                artistMatched = false;
                                                                LOG_INFO("[MUSICBRAINZ TIER B REJECTED] Mismatched artist credit for " + label + " (Expected: " + baseArtist + ")");
                                                            }
                                                        }
                                                    }

                                                    if (artistMatched) {
                                                        releaseGroupMbId = candMbId;
                                                        isMatched = true;
                                                        LOG_INFO("[MUSICBRAINZ TIER B SUCCESS] MBID: " + releaseGroupMbId);
                                                    }
                                                }
                                            }
                                        }
                                        size_t datePos = resJson.find("\"first-release-date\":\"", argPos);
                                        if (datePos != std::string::npos && isMatched) {
                                            datePos += 22;
                                            size_t dendPos = resJson.find("\"", datePos);
                                            if (dendPos != std::string::npos) {
                                                firstReleaseDate = resJson.substr(datePos, dendPos - datePos);
                                                LOG_INFO("[MUSICBRAINZ RELEASE DATE] Found full date: " + firstReleaseDate);
                                            }
                                        }
                                    }
                                };

                                parseTierB(mbAlbumRes, "release:\"" + albumClean + "\"");

                                // If Romaji album title didn't match, try Japanese Katakana transliteration (e.g. Iranai -> イラナイ)
                                if (releaseGroupMbId.empty()) {
                                    std::string albumKatakana = RomajiToKatakana(albumClean);
                                    if (!albumKatakana.empty() && albumKatakana != albumClean) {
                                        std::string mbKataUrl = "https://musicbrainz.org/ws/2/release-group?query=release:\"" + UrlEncode(albumKatakana) + "\"&fmt=json";
                                        LOG_INFO("[MUSICBRAINZ TIER B KATAKANA] Querying release:\"" + albumKatakana + "\" (Converted from " + albumClean + ")");
                                        std::string mbKataRes = HttpGetString(Utf8ToWide(mbKataUrl));
                                        
                                        // For Katakana transliteration, accept if release title matches Katakana
                                        size_t argPos = mbKataRes.find("\"release-groups\":");
                                        if (argPos != std::string::npos) {
                                            size_t aidPos = mbKataRes.find("\"id\":\"", argPos);
                                            if (aidPos != std::string::npos) {
                                                aidPos += 6;
                                                size_t aendPos = mbKataRes.find("\"", aidPos);
                                                if (aendPos != std::string::npos) {
                                                    std::string candMbId = mbKataRes.substr(aidPos, aendPos - aidPos);
                                                    if (candMbId.length() == 36) {
                                                        releaseGroupMbId = candMbId;
                                                        isMatched = true;
                                                        LOG_INFO("[MUSICBRAINZ TIER B KATAKANA SUCCESS] MBID: " + releaseGroupMbId + " for Katakana title: " + albumKatakana);
                                                    }
                                                }
                                            }
                                            size_t datePos = mbKataRes.find("\"first-release-date\":\"", argPos);
                                            if (datePos != std::string::npos && isMatched) {
                                                datePos += 22;
                                                size_t dendPos = mbKataRes.find("\"", datePos);
                                                if (dendPos != std::string::npos) {
                                                    firstReleaseDate = mbKataRes.substr(datePos, dendPos - datePos);
                                                    LOG_INFO("[MUSICBRAINZ RELEASE DATE] Found full date: " + firstReleaseDate);
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // Tier C: Loose Text Search
                            if (releaseGroupMbId.empty()) {
                                std::string mbLooseQuery = EscapeLuceneQuery(artistClean) + " " + EscapeLuceneQuery(albumClean);
                                std::string mbLooseUrl = "https://musicbrainz.org/ws/2/release-group?query=" + UrlEncode(mbLooseQuery) + "&fmt=json";
                                LOG_INFO("[MUSICBRAINZ TIER C LOOSE] Querying: " + mbLooseQuery);
                                std::string mbLooseRes = HttpGetString(Utf8ToWide(mbLooseUrl));
                                size_t lrgPos = mbLooseRes.find("\"release-groups\":");
                                if (lrgPos != std::string::npos) {
                                    size_t lidPos = mbLooseRes.find("\"id\":\"", lrgPos);
                                    if (lidPos != std::string::npos) {
                                        lidPos += 6;
                                        size_t lendPos = mbLooseRes.find("\"", lidPos);
                                        if (lendPos != std::string::npos) {
                                            std::string candMbId = mbLooseRes.substr(lidPos, lendPos - lidPos);
                                            if (candMbId.length() == 36) {
                                                bool artistMatched = true;
                                                if (!artistClean.empty() && artistClean != "Unknown Artist") {
                                                    std::string baseArtist = artistClean;
                                                    size_t semiPos = baseArtist.find(';');
                                                    if (semiPos != std::string::npos) baseArtist = baseArtist.substr(0, semiPos);
                                                    while (!baseArtist.empty() && (baseArtist.back() == ' ' || baseArtist.back() == '\t')) baseArtist.pop_back();

                                                    size_t acPos = mbLooseRes.find("\"artist-credit\":", lrgPos);
                                                    if (acPos != std::string::npos && !baseArtist.empty()) {
                                                        std::string acChunk = mbLooseRes.substr(acPos, 600);
                                                        std::string acLower = acChunk;
                                                        std::transform(acLower.begin(), acLower.end(), acLower.begin(), ::tolower);
                                                        std::string baseLower = baseArtist;
                                                        std::transform(baseLower.begin(), baseLower.end(), baseLower.begin(), ::tolower);
                                                        if (acLower.find(baseLower) == std::string::npos) {
                                                            artistMatched = false;
                                                            LOG_INFO("[MUSICBRAINZ TIER C REJECTED] Mismatched artist credit for loose query (Expected: " + baseArtist + ")");
                                                        }
                                                    }
                                                }

                                                bool titleMatched = true;
                                                if (!albumClean.empty()) {
                                                    size_t tPos = mbLooseRes.find("\"title\":\"", lrgPos);
                                                    if (tPos != std::string::npos) {
                                                        tPos += 9;
                                                        size_t tendPos = mbLooseRes.find("\"", tPos);
                                                        if (tendPos != std::string::npos) {
                                                            std::string candTitle = mbLooseRes.substr(tPos, tendPos - tPos);
                                                            std::string candTitleLower = candTitle;
                                                            std::transform(candTitleLower.begin(), candTitleLower.end(), candTitleLower.begin(), ::tolower);
                                                            std::string albumCleanLower = albumClean;
                                                            std::transform(albumCleanLower.begin(), albumCleanLower.end(), albumCleanLower.begin(), ::tolower);

                                                            if (candTitleLower.find(albumCleanLower) == std::string::npos &&
                                                                albumCleanLower.find(candTitleLower) == std::string::npos) {
                                                                titleMatched = false;
                                                                LOG_INFO("[MUSICBRAINZ TIER C REJECTED] Mismatched album title: " + candTitle + " (Expected: " + albumClean + ")");
                                                            }
                                                        }
                                                    }
                                                }

                                                if (artistMatched && titleMatched) {
                                                    releaseGroupMbId = candMbId;
                                                    isMatched = true;
                                                    LOG_INFO("[MUSICBRAINZ TIER C SUCCESS] MBID: " + releaseGroupMbId);
                                                }
                                            }
                                        }
                                    }
                                    size_t datePos = mbLooseRes.find("\"first-release-date\":\"", lrgPos);
                                    if (datePos != std::string::npos && isMatched) {
                                        datePos += 22;
                                        size_t dendPos = mbLooseRes.find("\"", datePos);
                                        if (dendPos != std::string::npos) {
                                            firstReleaseDate = mbLooseRes.substr(datePos, dendPos - datePos);
                                            LOG_INFO("[MUSICBRAINZ RELEASE DATE] Found full date: " + firstReleaseDate);
                                        }
                                    }
                                }
                            }
                        }

                        // 3. Fetch Cover Art from CoverArtArchive.org (Cascading Fallback: Original -> 1200px -> 500px -> 250px)
                        if (!releaseGroupMbId.empty()) {
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
                                LOG_INFO("[COVER ART MISSING] CoverArtArchive image not available for MBID: " + releaseGroupMbId);
                            }

                            // Fetch Release Tracklist from MusicBrainz to enrich track numbers, titles, and individual track artists
                            std::vector<MBTrackEntry> mbTracks = FetchMusicBrainzReleaseTracks(releaseGroupMbId);
                            if (!mbTracks.empty()) {
                                LOG_INFO("[MUSICBRAINZ TRACKLIST] Loaded " + std::to_string(mbTracks.size()) + " tracks from MusicBrainz release.");
                            }
                            
                            // Apply track match for current item and all items matching albumKey
                            for (auto& albItem : m_tagItems) {
                                std::string aKey = std::string(albItem.artistBuf) + "___" + std::string(albItem.albumBuf);
                                if (aKey == albumKey) {
                                    ApplyTrackMatch(albItem, mbTracks);
                                }
                            }
                            
                            albumCache[albumKey] = { releaseGroupMbId, firstReleaseDate, coverData, mbTracks, isMatched, true };
                        } else {
                            LOG_INFO("[NICHE TRACK] MusicBrainz record not found for " + artistClean + " - " + albumClean + ". Using Level 3 prefilled metadata.");
                            albumCache[albumKey] = { "", "", {}, {}, false, true };
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
                        item.isFetchCompleted = true;
                    }

                    m_isTagScanning = false;
                    LOG_INFO("Step 2 Background Online Fetching Complete. 100% of MusicBrainz queries finished.");
                }).detach();
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
                DatabaseManager::GetInstance().ImportFromTracklistMarkdown((fs::path(g_BaseDir) / "tracklist.md").string());
                DatabaseManager::GetInstance().SyncCollectionWithDisk(g_BaseDir);
            }).detach();
        }
        if (pushed3) ImGui::PopStyleColor();

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
                if (item.isMusicBrainzMatched) {
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "[MUSICBRAINZ MATCHED]");
                } else if (m_isTagScanning) {
                    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "[SEARCHING MUSICBRAINZ %zu/%zu...]", m_fetchedCount.load(), m_tagItems.size());
                } else {
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "[NICHE TRACK - LEVEL 3 PREFILLED]");
                }

                ImGui::SameLine();
                ImGui::TextDisabled("| Файл: %s", item.originalFilename.c_str());
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

                        fs::path flacDir = fs::path(g_BaseDir) / "flac" / newArtist / newAlbum;
                        fs::path mp3Dir  = fs::path(g_BaseDir) / "mp3" / newArtist / newAlbum;
                        fs::create_directories(flacDir);
                        fs::create_directories(mp3Dir);

                        std::string baseTrackName = newTrackNo + ". " + newTitle;

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
                ImGui::BeginChild("TagInspectorCardEmpty", ImVec2(0, 150), true);
                ImGui::TextUnformatted("Нажмите кнопку '2. Инспектор тегов' выше, чтобы начать сканирование и обработку тегов.");
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
                    DatabaseManager::GetInstance().ImportFromTracklistMarkdown((fs::path(g_BaseDir) / "tracklist.md").string());
                    DatabaseManager::GetInstance().SyncCollectionWithDisk(g_BaseDir);
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

        ImGui::End();

        // Rendering Frame
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.07f, 0.07f, 0.07f, 1.00f };
        m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, NULL);
        m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        m_pSwapChain->Present(1, 0); // Present with vsync 60 FPS
        Sleep(1); // Yield CPU to OS scheduler to keep IDLE CPU usage at 0.0%!
    }
}

LRESULT CALLBACK AppWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SCAN_FINISHED:
        Instance().HandleScanFinished();
        return 0;
    case WM_TAG_SCAN_FINISHED:
        Instance().HandleTagScanFinished();
        return 0;
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

    for (const std::string& pathStr : m_autoDelete) {
        fs::path p(pathStr);
        if (fs::exists(p)) {
            fs::path rel = fs::relative(p, fs::path(g_BaseDir));
            fs::path dst = fs::path(g_DeleteDir) / rel;
            fs::create_directories(dst.parent_path());
            LOG_INFO("[AUTO-DELETE] Moving 100% exact MP3 duplicate to delete/: " + rel.string());
            if (fs::exists(dst)) fs::remove(dst);
            fs::rename(p, dst);
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

    for (auto& item : m_tagItems) {
        if (!item.localCoverBytes.empty() && item.localTexture == NULL) {
            item.localTexture = CreateTextureFromMemory(m_pd3dDevice, item.localCoverBytes.data(), item.localCoverBytes.size(), &item.localWidth, &item.localHeight);
            if (item.localTexture) {
                item.localScore = CalculateImageQualityScore(item.localCoverBytes.data(), item.localCoverBytes.size(), item.localWidth, item.localHeight);
            }
        }
        if (!item.onlineCoverBytes.empty() && item.onlineTexture == NULL) {
            item.onlineTexture = CreateTextureFromMemory(m_pd3dDevice, item.onlineCoverBytes.data(), item.onlineCoverBytes.size(), &item.onlineWidth, &item.onlineHeight);
            if (item.onlineTexture) {
                item.onlineScore = CalculateImageQualityScore(item.onlineCoverBytes.data(), item.onlineCoverBytes.size(), item.onlineWidth, item.onlineHeight);
            }
        }
    }

    if (!m_tagItems.empty()) {
        m_currentTagIndex = 0;
    }
}
