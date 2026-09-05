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

struct ParsedFilenameInfo {
    std::string artist;
    std::string album;
    std::string title;
    int trackNumber{0};
    bool hasArtist{false};
    bool hasAlbum{false};
    bool hasTrackNumber{false};
};

namespace detail {

inline std::string TrimWhitespace(const std::string& str) {
    if (str.empty()) return "";
    size_t first = 0;
    while (first < str.size()) {
        unsigned char c = (unsigned char)str[first];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
            first++;
            continue;
        }
        if (c == 0xE3 && first + 2 < str.size() &&
            (unsigned char)str[first + 1] == 0x80 &&
            (unsigned char)str[first + 2] == 0x80) {
            first += 3;
            continue;
        }
        break;
    }
    if (first >= str.size()) return "";

    size_t last = str.size() - 1;
    while (last >= first) {
        unsigned char c = (unsigned char)str[last];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
            if (last == 0) break;
            last--;
            continue;
        }
        if (last >= 2 && (unsigned char)str[last - 2] == 0xE3 &&
            (unsigned char)str[last - 1] == 0x80 && c == 0x80) {
            last -= 3;
            continue;
        }
        break;
    }
    return str.substr(first, last - first + 1);
}

inline bool IsAllDigits(const std::string& str) {
    if (str.empty()) return false;
    for (char c : str) {
        if (!std::isdigit((unsigned char)c)) return false;
    }
    return true;
}

inline size_t Utf8CharCount(const std::string& str) {
    size_t count = 0;
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = (unsigned char)str[i];
        if (c < 0x80) i += 1;
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
        else i += 1;
        count++;
    }
    return count;
}

inline std::string StripPathAndExtension(const std::string& filepath) {
    std::string s = filepath;
    size_t lastSlash = s.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        s = s.substr(lastSlash + 1);
    }
    s = TrimWhitespace(s);

    size_t dot = s.rfind('.');
    if (dot != std::string::npos && dot > 0) {
        std::string ext = s.substr(dot);
        std::string extLower = ext;
        std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
        if (extLower == ".mp3" || extLower == ".flac" || extLower == ".wav" ||
            extLower == ".m4a" || extLower == ".aac" || extLower == ".ogg" ||
            extLower == ".opus" || extLower == ".wma" || extLower == ".aiff" ||
            extLower == ".ape") {
            s = s.substr(0, dot);
        }
    }
    return s;
}

inline std::string NormalizeDelimiters(const std::string& input) {
    std::string s = input;
    if (s.find("_-_") != std::string::npos) {
        size_t pos = 0;
        while ((pos = s.find("_-_", pos)) != std::string::npos) {
            s.replace(pos, 3, " - ");
            pos += 3;
        }
        for (char& c : s) {
            if (c == '_') c = ' ';
        }
    }

    size_t pos = 0;
    while ((pos = s.find(" -- ", pos)) != std::string::npos) {
        s.replace(pos, 4, " - ");
        pos += 3;
    }

    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        if (i + 2 < s.size() && (unsigned char)s[i] == 0xE3 && (unsigned char)s[i+1] == 0x80 && (unsigned char)s[i+2] == 0x80) {
            out += ' ';
            i += 3;
        } else if (i + 2 < s.size() && (unsigned char)s[i] == 0xE2 && (unsigned char)s[i+1] == 0x80 &&
            ((unsigned char)s[i+2] == 0x93 || (unsigned char)s[i+2] == 0x94)) {
            out += " - ";
            i += 3;
        } else if (i + 2 < s.size() && (unsigned char)s[i] == 0xEF && (unsigned char)s[i+1] == 0xBC && (unsigned char)s[i+2] == 0x8D) {
            out += " - ";
            i += 3;
        } else {
            out += s[i];
            i++;
        }
    }

    while ((pos = out.find(" -- ")) != std::string::npos) {
        out.replace(pos, 4, " - ");
    }
    while ((pos = out.find(" - - ")) != std::string::npos) {
        out.replace(pos, 5, " - ");
    }

    return out;
}

inline std::vector<std::string> SplitByDash(const std::string& str) {
    std::vector<std::string> tokens;
    std::string delimiter = " - ";
    size_t start = 0;
    size_t end = str.find(delimiter);
    while (end != std::string::npos) {
        std::string token = TrimWhitespace(str.substr(start, end - start));
        if (!token.empty()) tokens.push_back(token);
        start = end + delimiter.length();
        end = str.find(delimiter, start);
    }
    std::string lastToken = TrimWhitespace(str.substr(start));
    if (!lastToken.empty()) tokens.push_back(lastToken);
    return tokens;
}

} // namespace detail

inline ParsedFilenameInfo ParseFilenameHeuristic(const std::string& input) {
    ParsedFilenameInfo res;
    if (input.empty()) return res;

    std::string s = detail::StripPathAndExtension(input);
    s = detail::NormalizeDelimiters(s);
    s = detail::TrimWhitespace(s);
    if (s.empty()) return res;

    // Step 1: Extract leading brackets (e.g. circle, artist, year, event tags)
    std::string bracketArtist;
    while (!s.empty() && (s.front() == '[' || s.front() == '(')) {
        char closeChar = (s.front() == '[') ? ']' : ')';
        size_t closePos = s.find(closeChar);
        if (closePos == std::string::npos || closePos <= 1) break;

        std::string content = detail::TrimWhitespace(s.substr(1, closePos - 1));
        std::string contentLower = content;
        std::transform(contentLower.begin(), contentLower.end(), contentLower.begin(), ::tolower);

        bool isYear = (content.size() == 4 && detail::IsAllDigits(content));
        bool isTech = (contentLower == "flac" || contentLower == "320k" || contentLower == "mp3" ||
                       contentLower == "official video" || contentLower == "official audio" ||
                       contentLower == "mv" || contentLower == "audio");
        bool isEvent = (contentLower.find("c7") == 0 || contentLower.find("c8") == 0 ||
                        contentLower.find("c9") == 0 || contentLower.find("c10") == 0 ||
                        contentLower.find("m3") != std::string::npos ||
                        contentLower.find("reitaisai") != std::string::npos ||
                        contentLower.find("comic market") != std::string::npos);
        bool isCatalog = (content.size() <= 12 && content.find('-') != std::string::npos &&
                          !content.empty() && std::isdigit((unsigned char)content.back()));

        if (!isYear && !isTech && !isEvent && !isCatalog && !content.empty()) {
            if (bracketArtist.empty()) {
                bracketArtist = content;
            }
        }
        s = detail::TrimWhitespace(s.substr(closePos + 1));
    }

    if (s.empty()) {
        if (!bracketArtist.empty()) {
            res.title = bracketArtist;
        }
        res.hasArtist = !res.artist.empty();
        res.hasAlbum = !res.album.empty();
        res.hasTrackNumber = (res.trackNumber > 0);
        return res;
    }

    // Step 2: Extract leading track number
    std::regex discTrackRegex(R"(^(\d{1,2})[-.](\d{1,3})\s*[-._\s]\s*(.*)$)");
    std::smatch match;
    if (std::regex_match(s, match, discTrackRegex)) {
        try {
            res.trackNumber = std::stoi(match.str(2));
            res.hasTrackNumber = true;
            s = detail::TrimWhitespace(match.str(3));
        } catch (...) {}
    } else {
        std::regex numPrefixDelimRegex(R"(^(\d{1,3})\s*[\.\-_]\s*(.*)$)");
        if (std::regex_match(s, match, numPrefixDelimRegex)) {
            try {
                res.trackNumber = std::stoi(match.str(1));
                res.hasTrackNumber = true;
                s = detail::TrimWhitespace(match.str(2));
            } catch (...) {}
        } else {
            std::regex numSpaceRegex(R"(^(\d{1,3})\s+(.*)$)");
            if (std::regex_match(s, match, numSpaceRegex)) {
                std::string numStr = match.str(1);
                std::string rest = match.str(2);
                bool hasDash = (rest.find(" - ") != std::string::npos);
                if (!hasDash || (numStr.size() >= 2 && numStr[0] == '0')) {
                    try {
                        res.trackNumber = std::stoi(numStr);
                        res.hasTrackNumber = true;
                        s = detail::TrimWhitespace(rest);
                    } catch (...) {}
                }
            }
        }
    }

    while (!s.empty() && (s.front() == '-' || s.front() == '.' || s.front() == '_') && s.size() > 1 && s[1] == ' ') {
        s = detail::TrimWhitespace(s.substr(2));
    }

    // Step 3: Check trailing track numbers
    if (!res.hasTrackNumber) {
        std::regex trailDashTrackRegex(R"(^(.*?)\s*-\s*(\d{1,3})$)");
        if (std::regex_match(s, match, trailDashTrackRegex)) {
            try {
                res.trackNumber = std::stoi(match.str(2));
                res.hasTrackNumber = true;
                s = detail::TrimWhitespace(match.str(1));
            } catch (...) {}
        } else {
            std::regex trailParenTrackRegex(R"(^(.*?)\s*[\(\[](\d{1,3})[\)\]]$)");
            if (std::regex_match(s, match, trailParenTrackRegex)) {
                try {
                    res.trackNumber = std::stoi(match.str(2));
                    res.hasTrackNumber = true;
                    s = detail::TrimWhitespace(match.str(1));
                } catch (...) {}
            }
        }
    }

    // Step 4: Tokenize by " - "
    auto tokens = detail::SplitByDash(s);

    if (tokens.empty()) {
        if (!bracketArtist.empty()) {
            res.artist = bracketArtist;
        }
        res.title = s;
    } else if (tokens.size() == 1) {
        if (!bracketArtist.empty()) {
            res.artist = bracketArtist;
        }
        res.title = tokens[0];
    } else if (tokens.size() == 2) {
        if (!res.hasTrackNumber && detail::IsAllDigits(tokens[0])) {
            try {
                res.trackNumber = std::stoi(tokens[0]);
                res.hasTrackNumber = true;
                res.title = tokens[1];
                if (!bracketArtist.empty()) {
                    res.artist = bracketArtist;
                }
            } catch (...) {}
        } else if (!bracketArtist.empty()) {
            std::string bracketLower = bracketArtist;
            std::transform(bracketLower.begin(), bracketLower.end(), bracketLower.begin(), ::tolower);
            bool isLabel = (bracketLower.find("records") != std::string::npos ||
                            bracketLower.find("record") != std::string::npos ||
                            bracketLower.find("circle") != std::string::npos ||
                            bracketLower.find("label") != std::string::npos ||
                            bracketLower.find("sound") != std::string::npos ||
                            bracketLower.find("studio") != std::string::npos);
            if (isLabel) {
                res.artist = tokens[0];
                res.title = tokens[1];
            } else if (detail::Utf8CharCount(tokens[0]) <= 2 && ContainsCJK(tokens[0])) {
                res.artist = bracketArtist;
                res.title = tokens[0] + " - " + tokens[1];
            } else {
                res.artist = tokens[0];
                res.title = tokens[1];
            }
        } else {
            res.artist = tokens[0];
            res.title = tokens[1];
        }
    } else if (tokens.size() == 3) {
        if (!res.hasTrackNumber && detail::IsAllDigits(tokens[0])) {
            try {
                res.trackNumber = std::stoi(tokens[0]);
                res.artist = tokens[1];
                res.title = tokens[2];
            } catch (...) {}
        } else if (detail::IsAllDigits(tokens[1])) {
            try {
                res.trackNumber = std::stoi(tokens[1]);
                res.artist = tokens[0];
                res.title = tokens[2];
            } catch (...) {}
        } else if (!res.hasTrackNumber && detail::IsAllDigits(tokens[2]) && tokens[2].size() <= 3) {
            try {
                int num = std::stoi(tokens[2]);
                if (num < 1000) {
                    res.trackNumber = num;
                    res.artist = tokens[0];
                    res.title = tokens[1];
                }
            } catch (...) {}
        } else {
            res.artist = tokens[0];
            res.title = tokens[1] + " - " + tokens[2];
        }
    } else if (tokens.size() == 4) {
        if (detail::IsAllDigits(tokens[2])) {
            try {
                res.artist = tokens[0];
                res.album = tokens[1];
                res.trackNumber = std::stoi(tokens[2]);
                res.title = tokens[3];
            } catch (...) {}
        } else if (detail::IsAllDigits(tokens[1])) {
            res.artist = tokens[0];
            res.hasArtist = !res.artist.empty();
            try {
                res.trackNumber = std::stoi(tokens[1]);
                res.hasTrackNumber = true;
            } catch (...) {}
            res.title = tokens[2] + " - " + tokens[3];
        } else if (!res.hasTrackNumber && detail::IsAllDigits(tokens[0])) {
            try {
                res.trackNumber = std::stoi(tokens[0]);
                res.artist = tokens[1];
                res.album = tokens[2];
                res.title = tokens[3];
            } catch (...) {}
        } else {
            res.artist = tokens[0];
            res.title = tokens[1] + " - " + tokens[2] + " - " + tokens[3];
        }
    } else {
        if (detail::IsAllDigits(tokens[2])) {
            try {
                res.artist = tokens[0];
                res.album = tokens[1];
                res.trackNumber = std::stoi(tokens[2]);
                std::string remTitle = tokens[3];
                for (size_t i = 4; i < tokens.size(); ++i) {
                    remTitle += " - " + tokens[i];
                }
                res.title = remTitle;
            } catch (...) {}
        } else {
            res.artist = tokens[0];
            std::string remTitle = tokens[1];
            for (size_t i = 2; i < tokens.size(); ++i) {
                remTitle += " - " + tokens[i];
            }
            res.title = remTitle;
        }
    }

    while (res.title.size() >= 2 && res.title[0] == '-' && res.title[1] == ' ') {
        res.title = detail::TrimWhitespace(res.title.substr(2));
    }

    res.hasArtist = !res.artist.empty();
    res.hasAlbum = !res.album.empty();
    res.hasTrackNumber = (res.trackNumber > 0);
    return res;
}

inline std::string Base64Decode(const std::string& in) {
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) {
        T["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i]] = i;
    }
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

inline std::string UnescapeHtmlEntities(const std::string& str) {
    std::string res;
    res.reserve(str.size());
    for (size_t i = 0; i < str.size(); ) {
        if (str[i] == '&') {
            size_t semi = str.find(';', i);
            if (semi != std::string::npos && semi - i <= 10) {
                std::string entity = str.substr(i, semi - i + 1);
                if (entity == "&amp;") { res += '&'; i = semi + 1; continue; }
                if (entity == "&lt;") { res += '<'; i = semi + 1; continue; }
                if (entity == "&gt;") { res += '>'; i = semi + 1; continue; }
                if (entity == "&quot;") { res += '"'; i = semi + 1; continue; }
                if (entity == "&apos;") { res += '\''; i = semi + 1; continue; }
                if (entity == "&#58;") { res += ':'; i = semi + 1; continue; }
                if (entity == "&#40;") { res += '('; i = semi + 1; continue; }
                if (entity == "&#41;") { res += ')'; i = semi + 1; continue; }
                if (entity == "&#10;") { res += '\n'; i = semi + 1; continue; }
                if (entity == "&#13;") { res += '\r'; i = semi + 1; continue; }
                if (entity == "&#32;") { res += ' '; i = semi + 1; continue; }
                if (entity.size() > 3 && entity[1] == '#') {
                    try {
                        int code = 0;
                        if (entity[2] == 'x' || entity[2] == 'X') {
                            code = std::stoi(entity.substr(3, entity.size() - 4), nullptr, 16);
                        } else {
                            code = std::stoi(entity.substr(2, entity.size() - 3));
                        }
                        if (code > 0 && code < 128) {
                            res += (char)code;
                            i = semi + 1;
                            continue;
                        }
                    } catch (...) {}
                }
            }
        }
        res += str[i];
        i++;
    }
    return res;
}

inline std::string KanaToRomaji(const std::string& input) {
    if (input.empty()) return "";

    static const std::pair<const char*, const char*> digraphs[] = {
        {"きゃ", "kya"}, {"きゅ", "kyu"}, {"きょ", "kyo"},
        {"しゃ", "sha"}, {"しゅ", "shu"}, {"しょ", "sho"},
        {"ちゃ", "cha"}, {"ちゅ", "chu"}, {"ちょ", "cho"},
        {"にゃ", "nya"}, {"にゅ", "nyu"}, {"にょ", "nyo"},
        {"ひゃ", "hya"}, {"ひゅ", "hyu"}, {"ひょ", "hyo"},
        {"みゃ", "mya"}, {"みゅ", "myu"}, {"みょ", "myo"},
        {"りゃ", "rya"}, {"りゅ", "ryu"}, {"りょ", "ryo"},
        {"ぎゃ", "gya"}, {"ぎゅ", "gyu"}, {"ぎょ", "gyo"},
        {"じゃ", "ja"}, {"じゅ", "ju"}, {"じょ", "jo"},
        {"ぢゃ", "ja"}, {"ぢゅ", "ju"}, {"ぢょ", "jo"},
        {"びゃ", "bya"}, {"びゅ", "byu"}, {"びょ", "byo"},
        {"ぴゃ", "pya"}, {"ぴゅ", "pyu"}, {"ぴょ", "pyo"},
        {"ふぁ", "fa"}, {"ふぃ", "fi"}, {"ふぇ", "fe"}, {"ふぉ", "fo"},
        {"てぃ", "ti"}, {"でぃ", "di"}, {"どぅ", "du"},
        {"うぃ", "wi"}, {"うぇ", "we"}, {"うぉ", "wo"},
        {"ゔぁ", "va"}, {"ゔぃ", "vi"}, {"ゔ", "vu"}, {"ゔぇ", "ve"}, {"ゔぉ", "vo"},
        {"じぇ", "je"}, {"ちぇ", "che"}, {"しぇ", "she"},
        {"キャ", "kya"}, {"キュ", "kyu"}, {"キョ", "kyo"},
        {"シャ", "sha"}, {"シュ", "shu"}, {"ショ", "sho"},
        {"チャ", "cha"}, {"チュ", "chu"}, {"チョ", "cho"},
        {"ニャ", "nya"}, {"ニュ", "nyu"}, {"ニョ", "nyo"},
        {"ヒャ", "hya"}, {"ヒュ", "hyu"}, {"ヒョ", "hyo"},
        {"ミャ", "mya"}, {"ミュ", "myu"}, {"ミョ", "myo"},
        {"リャ", "rya"}, {"リュ", "ryu"}, {"リョ", "ryo"},
        {"ギャ", "gya"}, {"ギュ", "gyu"}, {"ギョ", "gyo"},
        {"ジャ", "ja"}, {"ジュ", "ju"}, {"ジョ", "jo"},
        {"ヂャ", "ja"}, {"ヂュ", "ju"}, {"ヂョ", "jo"},
        {"ビャ", "bya"}, {"ビュ", "byu"}, {"ビョ", "byo"},
        {"ピャ", "pya"}, {"ピュ", "pyu"}, {"ピョ", "pyo"},
        {"ファ", "fa"}, {"フィ", "fi"}, {"フェ", "fe"}, {"フォ", "fo"},
        {"ティ", "ti"}, {"ディ", "di"}, {"ドゥ", "du"},
        {"ウィ", "wi"}, {"ウェ", "we"}, {"ウォ", "wo"},
        {"ヴァ", "va"}, {"ヴィ", "vi"}, {"ヴ", "vu"}, {"ヴェ", "ve"}, {"ヴォ", "vo"},
        {"ジェ", "je"}, {"チェ", "che"}, {"シェ", "she"}
    };

    static const std::pair<const char*, const char*> monographs[] = {
        {"あ", "a"}, {"い", "i"}, {"う", "u"}, {"え", "e"}, {"お", "o"},
        {"か", "ka"}, {"き", "ki"}, {"く", "ku"}, {"け", "ke"}, {"こ", "ko"},
        {"さ", "sa"}, {"し", "shi"}, {"す", "su"}, {"せ", "se"}, {"そ", "so"},
        {"た", "ta"}, {"ち", "chi"}, {"つ", "tsu"}, {"て", "te"}, {"と", "to"},
        {"な", "na"}, {"に", "ni"}, {"ぬ", "nu"}, {"ね", "ne"}, {"の", "no"},
        {"は", "ha"}, {"ひ", "hi"}, {"ふ", "fu"}, {"へ", "he"}, {"ほ", "ho"},
        {"ま", "ma"}, {"み", "mi"}, {"む", "mu"}, {"め", "me"}, {"も", "mo"},
        {"や", "ya"}, {"ゆ", "yu"}, {"よ", "yo"},
        {"ら", "ra"}, {"り", "ri"}, {"る", "ru"}, {"れ", "re"}, {"ろ", "ro"},
        {"わ", "wa"}, {"ゐ", "wi"}, {"ゑ", "we"}, {"を", "wo"},
        {"ん", "n"},
        {"が", "ga"}, {"ぎ", "gi"}, {"ぐ", "gu"}, {"げ", "ge"}, {"ご", "go"},
        {"ざ", "za"}, {"じ", "ji"}, {"ず", "zu"}, {"ぜ", "ze"}, {"ぞ", "zo"},
        {"だ", "da"}, {"ぢ", "ji"}, {"づ", "zu"}, {"で", "de"}, {"ど", "do"},
        {"ば", "ba"}, {"び", "bi"}, {"ぶ", "bu"}, {"べ", "be"}, {"ぼ", "bo"},
        {"ぱ", "pa"}, {"ぴ", "pi"}, {"ぷ", "pu"}, {"ぺ", "pe"}, {"ぽ", "po"},
        {"ぁ", "a"}, {"ぃ", "i"}, {"ぅ", "u"}, {"ぇ", "e"}, {"ぉ", "o"},
        {"ゎ", "wa"},
        {"ア", "a"}, {"イ", "i"}, {"ウ", "u"}, {"エ", "e"}, {"オ", "o"},
        {"カ", "ka"}, {"キ", "ki"}, {"ク", "ku"}, {"ケ", "ke"}, {"コ", "ko"},
        {"サ", "sa"}, {"シ", "shi"}, {"ス", "su"}, {"セ", "se"}, {"ソ", "so"},
        {"タ", "ta"}, {"チ", "chi"}, {"ツ", "tsu"}, {"テ", "te"}, {"ト", "to"},
        {"ナ", "na"}, {"ニ", "ni"}, {"ヌ", "nu"}, {"ネ", "ne"}, {"ノ", "no"},
        {"ハ", "ha"}, {"ヒ", "hi"}, {"フ", "fu"}, {"ヘ", "he"}, {"ホ", "ho"},
        {"マ", "ma"}, {"ミ", "mi"}, {"ム", "mu"}, {"メ", "me"}, {"モ", "mo"},
        {"ヤ", "ya"}, {"ユ", "yu"}, {"ヨ", "yo"},
        {"ラ", "ra"}, {"リ", "ri"}, {"ル", "ru"}, {"レ", "re"}, {"ロ", "ro"},
        {"ワ", "wa"}, {"ヰ", "wi"}, {"ヱ", "we"}, {"ヲ", "wo"},
        {"ン", "n"},
        {"ガ", "ga"}, {"ギ", "gi"}, {"グ", "gu"}, {"ゲ", "ge"}, {"ゴ", "go"},
        {"ザ", "za"}, {"ジ", "ji"}, {"ズ", "zu"}, {"ゼ", "ze"}, {"ゾ", "zo"},
        {"ダ", "da"}, {"ヂ", "ji"}, {"ヅ", "zu"}, {"デ", "de"}, {"ド", "do"},
        {"バ", "ba"}, {"ビ", "bi"}, {"ブ", "bu"}, {"ベ", "be"}, {"ボ", "bo"},
        {"パ", "pa"}, {"ピ", "pi"}, {"プ", "pu"}, {"ペ", "pe"}, {"ポ", "po"},
        {"ァ", "a"}, {"ィ", "i"}, {"ゥ", "u"}, {"ェ", "e"}, {"ォ", "o"},
        {"ヮ", "wa"}, {"ー", "-"}
    };

    std::string result;
    size_t i = 0;
    while (i < input.size()) {
        if ((i + 3 <= input.size() && input.compare(i, 3, "っ") == 0) ||
            (i + 3 <= input.size() && input.compare(i, 3, "ッ") == 0)) {
            i += 3;
            bool nextMatched = false;
            for (const auto& kv : digraphs) {
                size_t klen = strlen(kv.first);
                if (i + klen <= input.size() && input.compare(i, klen, kv.first) == 0) {
                    char cons = kv.second[0];
                    if (cons != 'a' && cons != 'i' && cons != 'u' && cons != 'e' && cons != 'o') {
                        result += (cons == 'c' ? 't' : cons);
                    }
                    result += kv.second;
                    i += klen;
                    nextMatched = true;
                    break;
                }
            }
            if (!nextMatched) {
                for (const auto& kv : monographs) {
                    size_t klen = strlen(kv.first);
                    if (i + klen <= input.size() && input.compare(i, klen, kv.first) == 0) {
                        char cons = kv.second[0];
                        if (cons != 'a' && cons != 'i' && cons != 'u' && cons != 'e' && cons != 'o') {
                            result += (cons == 'c' ? 't' : cons);
                        }
                        result += kv.second;
                        i += klen;
                        nextMatched = true;
                        break;
                    }
                }
            }
            if (!nextMatched) {
                result += "tsu";
            }
            continue;
        }

        bool matched = false;
        for (const auto& kv : digraphs) {
            size_t klen = strlen(kv.first);
            if (i + klen <= input.size() && input.compare(i, klen, kv.first) == 0) {
                result += kv.second;
                i += klen;
                matched = true;
                break;
            }
        }
        if (matched) continue;

        for (const auto& kv : monographs) {
            size_t klen = strlen(kv.first);
            if (i + klen <= input.size() && input.compare(i, klen, kv.first) == 0) {
                result += kv.second;
                i += klen;
                matched = true;
                break;
            }
        }
        if (matched) continue;

        result += input[i];
        i++;
    }

    return result;
}

inline bool IsKanjiUtf8(unsigned char c0, unsigned char c1, unsigned char c2) {
    uint32_t cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
    return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) || cp == 0x3005;
}

inline std::string RomanizeJapaneseLyrics(const std::string& text) {
    if (text.empty()) return "";

    std::istringstream stream(text);
    std::string line;
    std::string result;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string tagPrefix;
        std::string lineContent = line;
        if (!line.empty() && line.front() == '[') {
            size_t closeB = line.find(']');
            if (closeB != std::string::npos && closeB < 15) {
                tagPrefix = line.substr(0, closeB + 1);
                lineContent = line.substr(closeB + 1);
            }
        }

        // Replace furigana in parentheses with the reading inside parentheses
        // e.g. 私の運命(さだめ) -> 私の + さだめ
        std::string processedLine;
        size_t p = 0;
        while (p < lineContent.size()) {
            size_t openP = lineContent.find("(", p);
            size_t openFullP = lineContent.find("（", p);
            size_t nextOpen = (std::min)(openP, openFullP);

            if (nextOpen == std::string::npos) {
                processedLine += lineContent.substr(p);
                break;
            }

            size_t openLen = (nextOpen == openFullP) ? 3 : 1;
            size_t closeP = lineContent.find(openLen == 3 ? "）" : ")", nextOpen + openLen);
            if (closeP != std::string::npos && closeP > nextOpen + openLen) {
                std::string inside = lineContent.substr(nextOpen + openLen, closeP - (nextOpen + openLen));
                if (ContainsCJK(inside)) {
                    size_t kStart = nextOpen;
                    while (kStart >= p + 3) {
                        unsigned char c0 = (unsigned char)lineContent[kStart - 3];
                        unsigned char c1 = (unsigned char)lineContent[kStart - 2];
                        unsigned char c2 = (unsigned char)lineContent[kStart - 1];
                        if ((c0 & 0xF0) == 0xE0 && IsKanjiUtf8(c0, c1, c2)) {
                            kStart -= 3;
                        } else {
                            break;
                        }
                    }
                    processedLine += lineContent.substr(p, kStart - p);
                    processedLine += inside;
                    p = closeP + (openLen == 3 ? 3 : 1);
                    continue;
                }
            }

            processedLine += lineContent.substr(p, nextOpen + openLen - p);
            p = nextOpen + openLen;
        }

        std::string romLine = KanaToRomaji(processedLine);
        if (!result.empty()) result += "\n";
        result += tagPrefix + romLine;
    }

    return result;
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
