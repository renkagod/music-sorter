#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

#include "../include/AppWindow.hpp"
#include "../include/AudioEngine.hpp"
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

struct AlbumMetadataCache {
    std::string releaseGroupMbId;
    std::string firstReleaseDate;
    std::vector<unsigned char> coverBytes;
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
    std::string s = std::regex_replace(str, std::regex(R"(\[[^\]]*\]|\{[^\}]*\}|\([^\)]*\)|\d{4}\.\d{2}\.\d{2})"), "");
    s = std::regex_replace(s, std::regex(R"(^\s+|\s+$)"), "");
    return s.empty() ? str : s;
}

static std::string ExtractYearFromString(const std::string& str) {
    std::regex year_regex(R"((19\d\d|20\d\d))");
    std::smatch match;
    if (std::regex_search(str, match, year_regex)) {
        return match[1].str();
    }
    return "";
}

static std::string NormalizeKey(const std::string& text) {
    if (text.empty()) return "";
    std::string s = std::regex_replace(text, std::regex(R"(\[[^\]]*\]|\{[^\}]*\}|\([^\)]*\))"), "");
    s = std::regex_replace(s, std::regex(R"([\s\-_/\\,.\u2044\u2215\u3013\uFF5E]+)"), "");
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
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
    std::ifstream fIn(filePath, std::ios::binary);
    if (!fIn.is_open()) return false;

    std::vector<unsigned char> flacData((std::istreambuf_iterator<char>(fIn)), std::istreambuf_iterator<char>());
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

// Native MP3 ID3v2.4 Tag & Picture Inserter
static bool WriteMp3TagsAndPicture(const std::string& filePath, const std::string& artist, const std::string& album, const std::string& title, const std::string& trackNo, const std::string& dateStr, const std::string& lyrics, const std::vector<unsigned char>& coverBytes) {
    std::ifstream fIn(filePath, std::ios::binary);
    if (!fIn.is_open()) return false;

    std::vector<unsigned char> mp3Data((std::istreambuf_iterator<char>(fIn)), std::istreambuf_iterator<char>());
    fIn.close();

    // Skip old ID3v2 header if present
    size_t audioOffset = 0;
    if (mp3Data.size() >= 10 && mp3Data[0] == 'I' && mp3Data[1] == 'D' && mp3Data[2] == '3') {
        uint32_t tagSize = ((uint32_t)(mp3Data[6] & 0x7F) << 21) | ((uint32_t)(mp3Data[7] & 0x7F) << 14) | ((uint32_t)(mp3Data[8] & 0x7F) << 7) | (uint32_t)(mp3Data[9] & 0x7F);
        audioOffset = 10 + tagSize;
    }

    // Construct ID3v2.4 frames
    std::vector<unsigned char> frames;

    auto AddTextFrame = [&](const char* frameID, const std::string& val) {
        if (val.empty()) return;
        frames.push_back(frameID[0]); frames.push_back(frameID[1]); frames.push_back(frameID[2]); frames.push_back(frameID[3]);
        uint32_t len = (uint32_t)val.length() + 1; // +1 for encoding byte 0x03 (UTF-8)
        frames.push_back((unsigned char)((len >> 21) & 0x7F));
        frames.push_back((unsigned char)((len >> 14) & 0x7F));
        frames.push_back((unsigned char)((len >> 7) & 0x7F));
        frames.push_back((unsigned char)(len & 0x7F));
        frames.push_back(0x00); frames.push_back(0x00); // Flags
        frames.push_back(0x03); // UTF-8 encoding
        frames.insert(frames.end(), val.begin(), val.end());
    };

    AddTextFrame("TPE1", artist);
    AddTextFrame("TALB", album);
    AddTextFrame("TIT2", title);
    AddTextFrame("TRCK", trackNo);
    AddTextFrame("TDRC", dateStr); // Full release date YYYY-MM-DD!
    std::string yr = ExtractYearFromString(dateStr);
    if (!yr.empty()) AddTextFrame("TYER", yr); // Legacy year

    // APIC Frame for Cover Art
    if (!coverBytes.empty()) {
        std::vector<unsigned char> apicPayload;
        apicPayload.push_back(0x03); // UTF-8
        std::string mime = "image/jpeg";
        apicPayload.insert(apicPayload.end(), mime.begin(), mime.end());
        apicPayload.push_back(0x00); // Null term mime
        apicPayload.push_back(0x03); // Picture type 3 = Cover Front
        apicPayload.push_back(0x00); // Description null term
        apicPayload.insert(apicPayload.end(), coverBytes.begin(), coverBytes.end());

        frames.push_back('A'); frames.push_back('P'); frames.push_back('I'); frames.push_back('C');
        uint32_t pLen = (uint32_t)apicPayload.size();
        frames.push_back((unsigned char)((pLen >> 21) & 0x7F));
        frames.push_back((unsigned char)((pLen >> 14) & 0x7F));
        frames.push_back((unsigned char)((pLen >> 7) & 0x7F));
        frames.push_back((unsigned char)(pLen & 0x7F));
        frames.push_back(0x00); frames.push_back(0x00);
        frames.insert(frames.end(), apicPayload.begin(), apicPayload.end());
    }

    // Assemble ID3v2.4 Tag
    std::vector<unsigned char> outMp3;
    outMp3.push_back('I'); outMp3.push_back('D'); outMp3.push_back('3');
    outMp3.push_back(0x04); outMp3.push_back(0x00); // Version 2.4
    outMp3.push_back(0x00); // Flags

    uint32_t fSize = (uint32_t)frames.size();
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
    size_t createdDirs = 0;

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

    if (fs::exists(flacRoot)) {
        for (auto& entry : fs::recursive_directory_iterator(flacRoot)) {
            if (entry.is_directory()) {
                fs::path rel = fs::relative(entry.path(), flacRoot);
                fs::path mp3EquivalentDir = mp3Root / rel;
                if (!fs::exists(mp3EquivalentDir)) {
                    fs::create_directories(mp3EquivalentDir);
                    createdDirs++;
                }
            }
        }
    }

    LOG_INFO("Step 3 Complete: Native C++ mirroring finished. Created " + std::to_string(createdDirs) + " folders, copied " + std::to_string(copiedFallbacks) + " MP3 fallbacks.");
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
                        scannedNormKeys.push_back(NormalizeKey(entry.path().filename().string()));
                    }
                }
            }
        }
    }

    std::ifstream inFile(tracklistPath);
    std::string line;
    std::vector<std::string> lines;
    size_t checkedCount = 0;

    while (std::getline(inFile, line)) {
        std::regex track_regex(R"(^\s*-\s*\[\s*\]\s*(.+)$)");
        std::smatch match;
        if (std::regex_search(line, match, track_regex)) {
            std::string content = match[1].str();
            std::string normContent = NormalizeKey(content);

            bool found = false;
            for (const auto& key : scannedNormKeys) {
                if (!key.empty() && normContent.find(key) != std::string::npos || key.find(normContent) != std::string::npos) {
                    found = true;
                    break;
                }
            }

            if (found) {
                line = std::regex_replace(line, std::regex(R"(-\s*\[\s*\])"), "- [x]");
                checkedCount++;
            }
        }
        lines.push_back(line);
    }
    inFile.close();

    std::ofstream outFile(tracklistPath);
    for (const auto& l : lines) {
        outFile << l << "\n";
    }
    outFile.close();

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

    // Initial Window Size adjusted to 940x950 to perfectly fit 3-column layout and end right after CoverArtArchive!
    m_hWnd = CreateWindowW(wcex.lpszClassName, L"MusicSorter Studio", WS_OVERLAPPEDWINDOW, 40, 20, 940, 950, NULL, NULL, hInstance, NULL);

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
                    double sharpness = CalculatePerceptualSharpness(item.localCoverBytes.data(), item.localCoverBytes.size());
                    item.localScore = (long long)(sharpness * item.localWidth * item.localHeight);
                }
            }

            if (item.isFetchCompleted && !item.onlineCoverBytes.empty() && item.onlineTexture == NULL) {
                item.onlineTexture = CreateTextureFromMemory(m_pd3dDevice, item.onlineCoverBytes.data(), item.onlineCoverBytes.size(), &item.onlineWidth, &item.onlineHeight);
                if (item.onlineTexture) {
                    double sharpness = CalculatePerceptualSharpness(item.onlineCoverBytes.data(), item.onlineCoverBytes.size());
                    item.onlineScore = (long long)(sharpness * item.onlineWidth * item.onlineHeight);
                    
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

                        std::regex num_regex(R"(^(\d{1,2})[\.\s_\-]+(.+)$)");
                        std::smatch match;
                        if (std::regex_search(fn, match, num_regex)) {
                            trackNo = match[1].str();
                            if (trackNo.length() == 1) trackNo = "0" + trackNo;
                            title = match[2].str();
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
                                        releaseGroupMbId = acoustRes.substr(idPos, endPos - idPos);
                                        isMatched = true;
                                        LOG_INFO("[ACOUSTID MATCHED] ReleaseGroup MBID: " + releaseGroupMbId);
                                    }
                                }
                            }
                        }

                        // 2. Multi-Tier MusicBrainz Search Strategy with Detailed Tier Logging
                        if (releaseGroupMbId.empty() && !albumClean.empty()) {
                            // Tier A: Strict Release Group Search
                            if (!artistClean.empty() && artistClean != "Unknown Artist") {
                                std::string mbQuery = "artist:\"" + artistClean + "\" AND release:\"" + albumClean + "\"";
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
                                            releaseGroupMbId = mbRes.substr(idPos, endPos - idPos);
                                            isMatched = true;
                                            LOG_INFO("[MUSICBRAINZ TIER A SUCCESS] MBID: " + releaseGroupMbId);
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

                            // Tier B: Album Title Alone Search (100% matches Doujin albums!)
                            if (releaseGroupMbId.empty()) {
                                std::string mbAlbumUrl = "https://musicbrainz.org/ws/2/release-group?query=release:\"" + UrlEncode(albumClean) + "\"&fmt=json";
                                LOG_INFO("[MUSICBRAINZ TIER B FALLBACK] Querying release:\"" + albumClean + "\"");
                                std::string mbAlbumRes = HttpGetString(Utf8ToWide(mbAlbumUrl));
                                size_t argPos = mbAlbumRes.find("\"release-groups\":");
                                if (argPos != std::string::npos) {
                                    size_t aidPos = mbAlbumRes.find("\"id\":\"", argPos);
                                    if (aidPos != std::string::npos) {
                                        aidPos += 6;
                                        size_t aendPos = mbAlbumRes.find("\"", argPos);
                                        if (aendPos != std::string::npos) {
                                            releaseGroupMbId = mbAlbumRes.substr(aidPos, aendPos - aidPos);
                                            isMatched = true;
                                            LOG_INFO("[MUSICBRAINZ TIER B SUCCESS] MBID: " + releaseGroupMbId);
                                        }
                                    }
                                    size_t datePos = mbAlbumRes.find("\"first-release-date\":\"", argPos);
                                    if (datePos != std::string::npos) {
                                        datePos += 22;
                                        size_t dendPos = mbAlbumRes.find("\"", datePos);
                                        if (dendPos != std::string::npos) {
                                            firstReleaseDate = mbAlbumRes.substr(datePos, dendPos - datePos);
                                            LOG_INFO("[MUSICBRAINZ RELEASE DATE] Found full date: " + firstReleaseDate);
                                        }
                                    }
                                }
                            }

                            // Tier C: Loose Text Search
                            if (releaseGroupMbId.empty()) {
                                std::string mbLooseQuery = artistClean + " " + albumClean;
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
                                            releaseGroupMbId = mbLooseRes.substr(lidPos, lendPos - lidPos);
                                            isMatched = true;
                                            LOG_INFO("[MUSICBRAINZ TIER C SUCCESS] MBID: " + releaseGroupMbId);
                                        }
                                    }
                                    size_t datePos = mbLooseRes.find("\"first-release-date\":\"", lrgPos);
                                    if (datePos != std::string::npos) {
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

                        // 3. Fetch Cover Art from CoverArtArchive.org if Release ID was found
                        if (!releaseGroupMbId.empty()) {
                            LOG_INFO("[MUSICBRAINZ MATCHED] MBID " + releaseGroupMbId + " for " + artistClean + " - " + albumClean + ". Downloading CoverArtArchive image...");
                            std::wstring caaUrl = Utf8ToWide("https://coverartarchive.org/release-group/" + releaseGroupMbId + "/front-500");
                            coverData = HttpGetBytes(caaUrl);
                            if (!coverData.empty()) {
                                LOG_INFO("[COVER ART DOWNLOADED] " + std::to_string(coverData.size()) + " bytes cover art for " + albumClean);
                            } else {
                                LOG_INFO("[COVER ART MISSING] CoverArtArchive image not available for MBID: " + releaseGroupMbId);
                            }
                        } else {
                            LOG_INFO("[NICHE TRACK] MusicBrainz record not found for " + artistClean + " - " + albumClean + ". Using Level 3 prefilled metadata.");
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

                        albumCache[albumKey] = { releaseGroupMbId, firstReleaseDate, coverData, isMatched, true };
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
        if (ImGui::Button("4. Tracklist.md", ImVec2(160, 32))) {
            m_activeStageTab = 3;
            std::thread([]() {
                NativeSyncTracklistDatabase();
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

                // ==================== ROW 1 (TOP LEFT): ORIGINAL TAGS ====================
                ImGui::TextDisabled("Исходные теги в файле");
                ImGui::PushItemWidth(190);

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

                ImGui::PushItemWidth(65);
                ImGui::InputText("№##Orig", origTrack, sizeof(origTrack), ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                ImGui::PushItemWidth(95);
                ImGui::InputText("Год/Дата##Orig", origYear, sizeof(origYear), ImGuiInputTextFlags_ReadOnly);
                ImGui::PopItemWidth();
                ImGui::PopItemWidth();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // ==================== ROW 2 (BOTTOM): 3 COLUMNS (PROPOSED TAGS | LOCAL COVER | ONLINE COVER) ====================
                ImGui::Columns(3, "InspectorBottomRow3Columns", false);
                ImGui::SetColumnWidth(0, 340.0f);
                ImGui::SetColumnWidth(1, 280.0f);
                ImGui::SetColumnWidth(2, 280.0f);

                // 1. Bottom-Left: Proposed Fetched Tags (directly under Original Tags!)
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.9f, 1.0f), "Предлагаемые теги");
                ImGui::PushItemWidth(190);
                ImGui::InputText("Исполнитель##New", item.artistBuf, sizeof(item.artistBuf));
                ImGui::InputText("Альбом##New", item.albumBuf, sizeof(item.albumBuf));
                ImGui::InputText("Название##New", item.titleBuf, sizeof(item.titleBuf));
                ImGui::PopItemWidth();

                ImGui::PushItemWidth(65);
                ImGui::InputText("№##New", item.trackNoBuf, sizeof(item.trackNoBuf));
                ImGui::SameLine();
                ImGui::PushItemWidth(95);
                ImGui::InputText("Год/Дата##New", item.yearBuf, sizeof(item.yearBuf));
                ImGui::PopItemWidth();
                ImGui::PopItemWidth();

                // 2. Bottom-Center: Local Cover Art
                ImGui::NextColumn();
                ImGui::TextDisabled("Локальная обложка:");
                if (item.localTexture) {
                    if (ImGui::ImageButton("##LocalCoverBtnLeftLarge", (ImTextureID)item.localTexture, ImVec2(260, 260))) {
                        item.selectedCoverChoice = 0;
                    }
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

                // 3. Bottom-Right: CoverArtArchive Cover Art
                ImGui::NextColumn();
                ImGui::TextDisabled("CoverArtArchive:");
                if (item.onlineTexture) {
                    if (ImGui::ImageButton("##OnlineCoverBtnRightLarge", (ImTextureID)item.onlineTexture, ImVec2(260, 260))) {
                        item.selectedCoverChoice = 1;
                    }
                    ImGui::Text(item.selectedCoverChoice == 1 ? "[X] CoverArtArchive" : "   CoverArtArchive");
                    ImGui::TextDisabled("%dx%d px | %zu KB", item.onlineWidth, item.onlineHeight, item.onlineCoverBytes.size() / 1024);
                    if (item.onlineScore > item.localScore) {
                        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "[*] ВЫСШЕЕ КАЧЕСТВО");
                    } else if (item.onlineWidth >= 1000 && item.onlineScore < item.onlineScore / 3) {
                        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "[!] ФАЛЬШИВЫЙ АПСКЕЙЛ");
                    }
                } else if (m_isTagScanning && !item.isFetchCompleted) {
                    ImGui::TextDisabled("[Загрузка обложки...]");
                } else {
                    ImGui::TextDisabled("[Обложка отсутствует]");
                }

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

                        fs::path targetFolder = (ext == ".flac") ? (fs::path(g_BaseDir) / "flac" / newArtist / newAlbum) : (fs::path(g_BaseDir) / "mp3" / newArtist / newAlbum);
                        fs::create_directories(targetFolder);

                        std::string newFileName = newTrackNo + ". " + newTitle + ext;
                        fs::path dstFile = targetFolder / newFileName;

                        // EMBED METADATA TAGS & COVER ART DIRECTLY INTO FLAC / MP3 FILE HEADER ASYNCHRONOUSLY!
                        bool embeddedOk = false;
                        if (ext == ".flac") {
                            embeddedOk = WriteFlacTagsAndPicture(srcFile.string(), newArtist, newAlbum, newTitle, newTrackNo, newYear, newLyrics, chosenCover);
                        } else if (ext == ".mp3") {
                            embeddedOk = WriteMp3TagsAndPicture(srcFile.string(), newArtist, newAlbum, newTitle, newTrackNo, newYear, newLyrics, chosenCover);
                        }

                        if (embeddedOk) {
                            LOG_INFO("[TAGS EMBEDDED] Embedded full VorbisComment/ID3v2 tags (Date: " + newYear + ") & cover art into file header!");
                        } else {
                            LOG_INFO("[TAG EMBED WARN] Direct tag header embedding returned false for: " + origFilename);
                        }

                        // Move audio file to sorted target folder
                        try {
                            if (fs::exists(dstFile)) fs::remove(dstFile);
                            fs::rename(srcFile, dstFile);
                            LOG_INFO("[TAGS APPLIED & FILE MOVED] Written tags & moved to: " + fs::relative(dstFile, g_BaseDir).string());

                            // Save chosen cover art to folder once per album
                            if (!chosenCover.empty()) {
                                fs::path coverDst = targetFolder / "cover.jpg";
                                if (!fs::exists(coverDst) || fs::file_size(coverDst) != chosenCover.size()) {
                                    std::ofstream cOut(coverDst, std::ios::binary);
                                    if (cOut.is_open()) {
                                        cOut.write((const char*)chosenCover.data(), chosenCover.size());
                                        cOut.close();
                                        LOG_INFO("[COVER SAVED] Saved cover art to: " + fs::relative(coverDst, g_BaseDir).string());
                                    }
                                }
                            }
                        } catch (const std::exception& ex) {
                            LOG_INFO("[WRITE ERROR] Failed to move/write file: " + std::string(ex.what()));
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
            ImGui::BeginChild("Stage4Child", ImVec2(0, 150), true);
            ImGui::TextUnformatted("Этап 4: Нативное C++20 сканирование и обновление галочек [x] в tracklist.md активно.");
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
                double sharpness = CalculatePerceptualSharpness(item.localCoverBytes.data(), item.localCoverBytes.size());
                item.localScore = (long long)(sharpness * item.localWidth * item.localHeight);
            }
        }
        if (!item.onlineCoverBytes.empty() && item.onlineTexture == NULL) {
            item.onlineTexture = CreateTextureFromMemory(m_pd3dDevice, item.onlineCoverBytes.data(), item.onlineCoverBytes.size(), &item.onlineWidth, &item.onlineHeight);
            if (item.onlineTexture) {
                double sharpness = CalculatePerceptualSharpness(item.onlineCoverBytes.data(), item.onlineCoverBytes.size());
                item.onlineScore = (long long)(sharpness * item.onlineWidth * item.onlineHeight);
            }
        }
    }

    if (!m_tagItems.empty()) {
        m_currentTagIndex = 0;
    }
}
