#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <regex>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <windows.h>
#include <cstdint>

// Forward declaration for stbi_load_from_memory if stb_image is included elsewhere
extern "C" unsigned char* stbi_load_from_memory(unsigned char const* buffer, int len, int* x, int* y, int* comp, int req_comp);
extern "C" void stbi_image_free(void* retval_from_stbi_load);

inline std::wstring Utf8ToWideStr(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    std::wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstr[0], len);
    return wstr;
}

inline std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    std::wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstr[0], len);
    return wstr;
}

inline std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), NULL, 0, NULL, NULL);
    std::string str(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), &str[0], len, NULL, NULL);
    return str;
}

inline std::string WideToUtf8Str(const wchar_t* wstr) {
    if (!wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, NULL, NULL);
    return result;
}

inline std::string CleanMetadataString(const std::string& str) {
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

inline std::string SanitizeForFilename(const std::string& str) {
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

inline std::string EscapeLuceneQuery(const std::string& str) {
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

inline std::string ExtractYearFromString(const std::string& str) {
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

inline std::string ExtractCatalogNumber(const std::string& str) {
    if (str.empty()) return "";
    std::regex catRegex(R"([\[\(]([A-Za-z0-9]{2,12}[-_][A-Za-z0-9~_\-\.]{1,16})[\]\)])");
    std::smatch match;
    if (std::regex_search(str, match, catRegex)) {
        return match.str(1);
    }
    return "";
}

inline std::string CleanAlbumTitle(const std::string& str) {
    if (str.empty()) return "";
    std::string res = CleanMetadataString(str);
    std::regex datePrefixRegex(R"(^\s*(\d{4}[.-]\d{2}[.-]\d{2}|\d{4}[.-]\d{2}|\d{4})\s*)");
    res = std::regex_replace(res, datePrefixRegex, "");
    size_t first = res.find_first_not_of(" \t\r\n.-_");
    if (first == std::string::npos) return "";
    size_t last = res.find_last_not_of(" \t\r\n.-_");
    return res.substr(first, (last - first + 1));
}

inline std::string ExtractArtistFromFilename(const std::string& fn) {
    if (fn.empty()) return "";
    std::regex artBracketRegex(R"(\[[^\]]+\])");
    auto words_begin = std::sregex_iterator(fn.begin(), fn.end(), artBracketRegex);
    auto words_end = std::sregex_iterator();
    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch m = *i;
        std::string val = m.str();
        if (val.size() > 2) {
            val = val.substr(1, val.size() - 2);
            if (val.find('-') != std::string::npos && val.size() <= 10 && std::isdigit((unsigned char)val.back())) {
                continue;
            }
            std::string valLower = val;
            std::transform(valLower.begin(), valLower.end(), valLower.begin(), ::tolower);
            if (valLower.find("reitaisai") != std::string::npos || valLower.find("comic market") != std::string::npos || 
                valLower.find("m3") != std::string::npos || valLower.find("c7") == 0 || valLower.find("c8") == 0 || valLower.find("c9") == 0 || valLower.find("c10") == 0) {
                continue;
            }
            return val;
        }
    }
    return "";
}

inline std::string RomajiToKatakana(const std::string& input) {
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

inline std::string NormalizeKey(const std::string& text) {
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

inline std::string UrlEncode(const std::string& str) {
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

inline int ParseDurationMs(const std::string& durStr) {
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

inline int ParseDiscogsPosition(const std::string& posStr, int defaultPos) {
    if (posStr.empty()) return defaultPos;
    int num = 0;
    for (char c : posStr) {
        if (c >= '0' && c <= '9') {
            num = num * 10 + (c - '0');
        }
    }
    return (num > 0) ? num : defaultPos;
}

inline void WriteUint32LE(std::vector<unsigned char>& buf, uint32_t val) {
    buf.push_back((unsigned char)(val & 0xFF));
    buf.push_back((unsigned char)((val >> 8) & 0xFF));
    buf.push_back((unsigned char)((val >> 16) & 0xFF));
    buf.push_back((unsigned char)((val >> 24) & 0xFF));
}

inline void WriteUint32BE(std::vector<unsigned char>& buf, uint32_t val) {
    buf.push_back((unsigned char)((val >> 24) & 0xFF));
    buf.push_back((unsigned char)((val >> 16) & 0xFF));
    buf.push_back((unsigned char)((val >> 8) & 0xFF));
    buf.push_back((unsigned char)(val & 0xFF));
}

inline std::vector<unsigned char> StringToUtf16LE(const std::string& utf8Str) {
    std::vector<unsigned char> result;
    if (utf8Str.empty()) return result;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, NULL, 0);
    if (wlen <= 0) return result;
    std::vector<wchar_t> wbuf(wlen);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, wbuf.data(), wlen);
    for (int i = 0; i < wlen - 1; ++i) {
        wchar_t ch = wbuf[i];
        result.push_back((unsigned char)(ch & 0xFF));
        result.push_back((unsigned char)((ch >> 8) & 0xFF));
    }
    return result;
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

inline JsonVal ParseJsonSimple(const std::string& str, size_t& pos) {
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

inline double CalculatePerceptualSharpness(const unsigned char* data, size_t size) {
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
            int lap = -4 * center
                    + gray[(y - 1) * width + x]
                    + gray[(y + 1) * width + x]
                    + gray[y * width + (x - 1)]
                    + gray[y * width + (x + 1)];
            double val = (double)std::abs(lap);
            sum += val;
            sumSq += val * val;
            count++;
        }
    }

    stbi_image_free(gray);
    if (count == 0) return 0.0;

    double mean = sum / (double)count;
    double variance = (sumSq / (double)count) - (mean * mean);
    if (variance < 0.0) variance = 0.0;
    return variance;
}

inline long long CalculateImageQualityScore(const unsigned char* data, size_t size, int width, int height) {
    if (!data || size == 0 || width <= 0 || height <= 0) return 0;

    long long score = 0;
    long long maxDim = (std::max)(width, height);
    long long minDim = (std::min)(width, height);

    if (maxDim >= 1400) score += 500000000LL;
    else if (maxDim >= 900) score += 400000000LL;
    else if (maxDim >= 600) score += 300000000LL;
    else if (maxDim >= 400) score += 200000000LL;
    else score += (maxDim * 300000LL);

    long long megapixels = (long long)width * (long long)height;
    score += (megapixels * 50LL);

    double aspect = (double)minDim / (double)maxDim;
    if (aspect >= 0.95) score += 50000000LL;
    else if (aspect >= 0.85) score += 20000000LL;

    double sharpness = CalculatePerceptualSharpness(data, size);
    long long sharpBonus = (long long)(sharpness * 1000.0);
    if (sharpBonus > 100000000LL) sharpBonus = 100000000LL;
    score += sharpBonus;

    return score;
}
