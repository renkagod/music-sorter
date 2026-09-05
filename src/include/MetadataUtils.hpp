#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
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
        if ((unsigned char)c == 0xEF && i + 2 < str.size() && (unsigned char)str[i + 1] == 0xBC) {
            unsigned char c3 = (unsigned char)str[i + 2];
            if (c3 == 0x88 || c3 == 0xBB) { bLevel++; i += 2; continue; }
            if (c3 == 0x89 || c3 == 0xBD) { if (bLevel > 0) bLevel--; i += 2; continue; }
        }
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

        // UTF-8 fullwidth Japanese parentheses: （ (0xEF 0xBC 0x88) / ） (0xEF 0xBC 0x89)
        // and fullwidth square brackets: ［ (0xEF 0xBC 0xBB) / ］ (0xEF 0xBC 0xBD)
        if (c == 0xEF && i + 2 < text.size() && (unsigned char)text[i + 1] == 0xBC) {
            unsigned char c3 = (unsigned char)text[i + 2];
            if (c3 == 0x88 || c3 == 0xBB) { bLevel++; i += 2; continue; }
            if (c3 == 0x89 || c3 == 0xBD) { if (bLevel > 0) bLevel--; i += 2; continue; }
        }

        if (bLevel > 0) continue;

        if (c <= 32 || c == '-' || c == '_' || c == '/' || c == '\\' || c == ',' || c == '.' || c == '~') continue;

        if (c == 0xEF && i + 2 < text.size() && (unsigned char)text[i + 1] == 0xBD && (unsigned char)text[i + 2] == 0x9E) {
            i += 2;
            continue;
        }

        // UTF-8 Cyrillic uppercase to lowercase
        if (c == 0xD0 && i + 1 < text.size()) {
            unsigned char c2 = (unsigned char)text[i + 1];
            if (c2 >= 0x90 && c2 <= 0x9F) {
                // А-П -> а-п (0xD0 0xB0 .. 0xBF)
                result.push_back((char)0xD0);
                result.push_back((char)(c2 + 0x20));
                i++;
                continue;
            } else if (c2 >= 0xA0 && c2 <= 0xAF) {
                // Р-Я -> р-я (0xD1 0x80 .. 0x8F)
                result.push_back((char)0xD1);
                result.push_back((char)(c2 - 0x20));
                i++;
                continue;
            } else if (c2 == 0x81) {
                // Ё -> ё (0xD1 0x91)
                result.push_back((char)0xD1);
                result.push_back((char)0x91);
                i++;
                continue;
            }
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
            size_t keyStart = pos;
            JsonVal k = ParseJsonSimple(str, pos);
            if (k.type != JsonVal::String || pos <= keyStart) break;
            skipWs();
            if (pos < str.size() && str[pos] == ':') {
                pos++;
            } else {
                break;
            }
            size_t valStart = pos;
            JsonVal val = ParseJsonSimple(str, pos);
            v.objVal[k.strVal] = std::move(val);
            if (pos <= valStart) {
                pos++;
                break;
            }
            if (v.objVal.size() > 10000) break;
            skipWs();
            if (pos < str.size() && str[pos] == ',') {
                pos++;
            } else if (pos < str.size() && str[pos] == '}') {
                pos++;
                break;
            } else {
                break;
            }
        }
        return v;
    }
    if (c == '[') {
        pos++;
        JsonVal v; v.type = JsonVal::Array;
        while (pos < str.size()) {
            skipWs();
            if (pos < str.size() && str[pos] == ']') { pos++; break; }
            size_t valStart = pos;
            JsonVal val = ParseJsonSimple(str, pos);
            v.arrVal.push_back(std::move(val));
            if (pos <= valStart) {
                pos++;
                break;
            }
            if (v.arrVal.size() > 10000) break;
            skipWs();
            if (pos < str.size() && str[pos] == ',') {
                pos++;
            } else if (pos < str.size() && str[pos] == ']') {
                pos++;
                break;
            } else {
                break;
            }
        }
        return v;
    }
    if (c == 't') {
        if (str.compare(pos, 4, "true") == 0) { pos += 4; JsonVal v; v.type = JsonVal::Bool; v.boolVal = true; return v; }
        pos++;
        return {};
    }
    if (c == 'f') {
        if (str.compare(pos, 5, "false") == 0) { pos += 5; JsonVal v; v.type = JsonVal::Bool; v.boolVal = false; return v; }
        pos++;
        return {};
    }
    if (c == 'n') {
        if (str.compare(pos, 4, "null") == 0) { pos += 4; return {}; }
        pos++;
        return {};
    }
    if ((c >= '0' && c <= '9') || c == '-') {
        size_t start = pos;
        if (c == '-') pos++;
        while (pos < str.size() && (std::isdigit((unsigned char)str[pos]) || str[pos] == '.' || str[pos] == 'e' || str[pos] == 'E' || str[pos] == '+' || str[pos] == '-')) pos++;
        if (pos <= start) { pos++; return {}; }
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
    if (!gray) return 0.0;
    if (width < 4 || height < 4) {
        stbi_image_free(gray);
        return 0.0;
    }

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

// ============================================================================
// GUARDRAILS & STRING SIMILARITY ENGINE (Milestone 2)
// ============================================================================

inline size_t ComputeLevenshteinDistance(const std::string& s1, const std::string& s2) {
    const size_t m = s1.size();
    const size_t n = s2.size();
    if (m == 0) return n;
    if (n == 0) return m;

    if (m > 512 || n > 512) {
        size_t capM = (std::min)(m, (size_t)512);
        size_t capN = (std::min)(n, (size_t)512);
        return ComputeLevenshteinDistance(s1.substr(0, capM), s2.substr(0, capN)) + (m > capM ? m - capM : 0) + (n > capN ? n - capN : 0);
    }

    std::vector<size_t> prev(n + 1);
    std::vector<size_t> curr(n + 1);

    for (size_t j = 0; j <= n; ++j) prev[j] = j;

    for (size_t i = 1; i <= m; ++i) {
        curr[0] = i;
        for (size_t j = 1; j <= n; ++j) {
            size_t cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            curr[j] = (std::min)({
                prev[j] + 1,
                curr[j - 1] + 1,
                prev[j - 1] + cost
            });
        }
        prev = curr;
    }
    return prev[n];
}

inline size_t LevenshteinDistance(const std::string& s1, const std::string& s2) {
    return ComputeLevenshteinDistance(s1, s2);
}

inline int ParseRomanNumeralWord(const std::string& word) {
    if (word.empty()) return -1;
    std::string up = word;
    for (char& c : up) c = (char)::toupper((unsigned char)c);

    while (!up.empty() && (up.back() == '.' || up.back() == ',' || up.back() == ':' || up.back() == ';')) {
        up.pop_back();
    }
    if (up.empty()) return -1;

    static const std::unordered_map<std::string, int> romanMap = {
        {"I", 1}, {"II", 2}, {"III", 3}, {"IV", 4}, {"V", 5},
        {"VI", 6}, {"VII", 7}, {"VIII", 8}, {"IX", 9}, {"X", 10},
        {"XI", 11}, {"XII", 12}, {"XIII", 13}, {"XIV", 14}, {"XV", 15},
        {"XVI", 16}, {"XVII", 17}, {"XVIII", 18}, {"XIX", 19}, {"XX", 20},
        {"XXI", 21}, {"XXII", 22}, {"XXIII", 23}, {"XXIV", 24}, {"XXV", 25},
        {"XXVI", 26}, {"XXVII", 27}, {"XXVIII", 28}, {"XXIX", 29}, {"XXX", 30}
    };
    auto it = romanMap.find(up);
    if (it != romanMap.end()) return it->second;
    return -1;
}

inline bool IsTrailingDescriptorWord(const std::string& word) {
    if (word.empty()) return false;
    std::string w = word;
    for (char& c : w) c = (char)::tolower((unsigned char)c);
    while (!w.empty() && (w.back() == '.' || w.back() == ',' || w.back() == ':' || w.back() == ';' || w.back() == '-')) {
        w.pop_back();
    }
    if (w.empty()) return false;
    static const std::unordered_set<std::string> descriptors = {
        "theme", "themes", "main", "title", "opening", "ending", "op", "ed", "intro", "outro",
        "prologue", "epilogue", "overture", "prelude", "interlude", "finale",
        "ost", "soundtrack", "soundtracks", "score", "scores", "bgm", "music", "audio", "song", "track", "tracks",
        "original", "official", "mix", "mixes", "remix", "remixes", "edit", "edits",
        "version", "versions", "ver", "instrumental", "acoustic", "orchestral", "orchestra", "piano", "vocal", "vocals",
        "arrangement", "arrange", "arrangements", "medley", "suite", "remaster", "remastered", "re-recorded", "recording",
        "live", "session", "sessions", "take", "battle", "boss", "stage", "character", "credits", "trailer", "teaser",
        "field", "dungeon", "level", "area", "town", "map", "menu", "cutscene", "cinematic", "event",
        "turbo", "deluxe", "edition", "extended", "club", "radio", "dub", "bonus", "special", "anniversary",
        "collection", "collections", "complete", "definitive", "super",
        "part", "pt", "vol", "volume", "act", "chapter", "movement", "no"
    };
    return descriptors.find(w) != descriptors.end();
}

inline std::string StripMetadataAnnotations(const std::string& str) {
    if (str.empty()) return "";
    std::string result;
    result.reserve(str.size());

    for (size_t i = 0; i < str.size(); ++i) {
        char openBracket = str[i];
        if (openBracket == '(' || openBracket == '[' || openBracket == '{') {
            char closeBracket = (openBracket == '(') ? ')' : (openBracket == '[') ? ']' : '}';
            size_t closePos = str.find(closeBracket, i + 1);
            if (closePos != std::string::npos) {
                std::string inner = str.substr(i + 1, closePos - (i + 1));
                std::string lowerInner = inner;
                for (char& c : lowerInner) c = (char)::tolower((unsigned char)c);

                bool hasSequelKeyword = (lowerInner.find("part") != std::string::npos ||
                                         lowerInner.find("pt") != std::string::npos ||
                                         lowerInner.find("act") != std::string::npos ||
                                         lowerInner.find("vol") != std::string::npos ||
                                         lowerInner.find("suite") != std::string::npos ||
                                         lowerInner.find("movement") != std::string::npos ||
                                         lowerInner.find("chapter") != std::string::npos ||
                                         lowerInner.find("no.") != std::string::npos ||
                                         lowerInner.find("no ") != std::string::npos ||
                                         lowerInner.find("#") != std::string::npos);

                bool isMetadata = (lowerInner.find("remaster") != std::string::npos ||
                                   lowerInner.find("mix") != std::string::npos ||
                                   lowerInner.find("edition") != std::string::npos ||
                                   lowerInner.find("version") != std::string::npos ||
                                   lowerInner.find("ver.") != std::string::npos ||
                                   lowerInner.find("bpm") != std::string::npos ||
                                   lowerInner.find("kbps") != std::string::npos ||
                                   lowerInner.find("anniversary") != std::string::npos ||
                                   lowerInner.find("deluxe") != std::string::npos ||
                                   lowerInner.find("re-recorded") != std::string::npos ||
                                   lowerInner.find("live") != std::string::npos ||
                                   lowerInner.find("bonus") != std::string::npos ||
                                   lowerInner.find("mono") != std::string::npos ||
                                   lowerInner.find("stereo") != std::string::npos ||
                                   lowerInner.find("instrumental") != std::string::npos ||
                                   lowerInner.find("off vocal") != std::string::npos ||
                                   lowerInner.find("karaoke") != std::string::npos ||
                                   lowerInner.find("backing track") != std::string::npos);

                if (!isMetadata) {
                    std::string trimmedInner;
                    for (char c : lowerInner) if ((unsigned char)c > 32) trimmedInner.push_back(c);
                    if (trimmedInner.size() == 4 && std::isdigit((unsigned char)trimmedInner[0])) {
                        try {
                            int y = std::stoi(trimmedInner);
                            if (y >= 1900 && y <= 2099) isMetadata = true;
                        } catch (...) {}
                    }
                }

                if (isMetadata && !hasSequelKeyword) {
                    i = closePos;
                    continue;
                }
            }
        }
        result.push_back(str[i]);
    }
    return result;
}

inline int ExtractTrailingOrEmbeddedNumber(const std::string& str) {
    if (str.empty()) return -1;
    std::string clean = StripMetadataAnnotations(str);

    struct WordToken {
        std::string text;
        size_t start{0};
        size_t end{0};
    };

    std::vector<WordToken> tokens;
    std::string curWord;
    size_t curStart = 0;
    for (size_t i = 0; i < clean.size(); ++i) {
        unsigned char c = (unsigned char)clean[i];
        if (std::isalnum(c) || c == '#' || c >= 0x80) {
            if (curWord.empty()) curStart = i;
            curWord.push_back((char)c);
        } else {
            if (!curWord.empty()) {
                tokens.push_back({curWord, curStart, i});
                curWord.clear();
            }
        }
    }
    if (!curWord.empty()) {
        tokens.push_back({curWord, curStart, clean.size()});
    }

    std::vector<std::string> words;
    words.reserve(tokens.size());
    for (const auto& t : tokens) words.push_back(t.text);

    // 1. Check for sequel keywords followed by number or Roman numeral
    for (size_t i = 0; i < words.size(); ++i) {
        std::string w = words[i];
        for (char& c : w) c = (char)::tolower((unsigned char)c);
        bool isKeyword = (w == "part" || w == "pt" || w == "act" || w == "vol" ||
                          w == "volume" || w == "suite" || w == "movement" ||
                          w == "chapter" || w == "no" || w == "#" || w == "track" ||
                          w == "episode" || w == "ep");
        if (isKeyword && i + 1 < words.size()) {
            const std::string& next = words[i + 1];
            int rn = ParseRomanNumeralWord(next);
            if (rn > 0) return rn;
            bool isAllDigits = true;
            for (char c : next) {
                if (!std::isdigit((unsigned char)c)) { isAllDigits = false; break; }
            }
            if (isAllDigits && !next.empty()) {
                try {
                    int val = std::stoi(next);
                    if (!(val >= 1900 && val <= 2099 && next.size() == 4)) {
                        return val;
                    }
                } catch (...) {}
            }
        }
    }

    // 2. Check for Roman numerals (trailing, followed by descriptors, or preceding title delimiters)
    for (size_t i = 0; i < tokens.size(); ++i) {
        int rn = ParseRomanNumeralWord(tokens[i].text);
        if (rn <= 0) continue;

        bool allSubsequentAreDescriptors = true;
        for (size_t j = i + 1; j < tokens.size(); ++j) {
            if (!IsTrailingDescriptorWord(tokens[j].text)) {
                allSubsequentAreDescriptors = false;
                break;
            }
        }

        bool followedByTitleDelimiter = false;
        size_t afterWord = clean.find_first_not_of(" \t", tokens[i].end);
        if (afterWord != std::string::npos) {
            char delim = clean[afterWord];
            if (delim == ':' || delim == '-' || delim == '~' || delim == '/' || delim == '|') {
                followedByTitleDelimiter = true;
            }
        }

        if (allSubsequentAreDescriptors || (followedByTitleDelimiter && rn > 1)) {
            if (rn > 1) {
                return rn;
            }
            // Safe handling for Roman numeral 1 ("I") to prevent false positives from English pronoun "I"
            if (rn == 1) {
                if (tokens.size() == 1) {
                    return 1;
                }
                if (i > 0 && i + 1 < tokens.size() && allSubsequentAreDescriptors) {
                    return 1;
                }
                if (i > 0) {
                    std::string prev = tokens[i - 1].text;
                    for (char& c : prev) c = (char)::tolower((unsigned char)c);
                    if (prev == "part" || prev == "pt" || prev == "act" || prev == "vol" ||
                        prev == "volume" || prev == "suite" || prev == "no" || prev == "#" ||
                        prev == "episode" || prev == "ep") {
                        return 1;
                    }
                }
            }
        }
    }

    // 3. Scan for trailing or embedded Arabic numbers, ignoring 4-digit years (1900-2099)
    int lastNum = -1;
    for (size_t i = 0; i < clean.size(); ++i) {
        if (std::isdigit((unsigned char)clean[i])) {
            size_t start = i;
            while (i < clean.size() && std::isdigit((unsigned char)clean[i])) {
                ++i;
            }
            std::string numStr = clean.substr(start, i - start);
            try {
                int val = std::stoi(numStr);
                if (val >= 1900 && val <= 2099 && numStr.size() == 4) {
                    continue;
                }
                size_t after = clean.find_first_not_of(" \t", i);
                if (after != std::string::npos) {
                    std::string rest = clean.substr(after);
                    for (char& c : rest) c = (char)::tolower((unsigned char)c);
                    if (rest.rfind("bpm", 0) == 0 || rest.rfind("kbps", 0) == 0 ||
                        rest.rfind("khz", 0) == 0 || rest.rfind("hz", 0) == 0) {
                        continue;
                    }
                }
                lastNum = val;
            } catch (...) {}
        }
    }
    return lastNum;
}

inline bool IsUnknownArtist(const std::string& artist) {
    if (artist.empty()) return true;
    std::string trimmed;
    for (char c : artist) {
        if ((unsigned char)c > 32) trimmed.push_back(c);
    }
    if (trimmed.empty() || trimmed == "?" || trimmed == "-" || trimmed == "..." || trimmed == "/") return true;

    std::string norm = NormalizeKey(artist);
    if (norm.empty()) return true;
    if (norm == "unknownartist" || norm == "unknown" ||
        norm == "variousartists" || norm == "various" ||
        norm == "va" || norm == "none" || norm == "na" ||
        norm == "untitled" || norm == "tosort" ||
        norm == "singles" || norm == "downloads" ||
        norm == "music" || norm == "media") {
        return true;
    }

    // Generic track labels: "Track", "Track 01", "Track 02", "Track 1", "AudioTrack 01", etc.
    if (norm.rfind("track", 0) == 0) {
        bool allDigits = true;
        for (size_t i = 5; i < norm.size(); ++i) {
            if (!std::isdigit((unsigned char)norm[i])) {
                allDigits = false;
                break;
            }
        }
        if (allDigits) return true;
    }
    if (norm.rfind("audiotrack", 0) == 0) {
        bool allDigits = true;
        for (size_t i = 10; i < norm.size(); ++i) {
            if (!std::isdigit((unsigned char)norm[i])) {
                allDigits = false;
                break;
            }
        }
        if (allDigits) return true;
    }

    return false;
}

inline bool IsInstrumentalTitle(const std::string& title) {
    if (title.empty()) return false;
    std::string lower = title;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)::tolower(c); });

    return (lower.find("instrumental") != std::string::npos ||
            lower.find("off vocal") != std::string::npos ||
            lower.find("karaoke") != std::string::npos ||
            lower.find("backing track") != std::string::npos ||
            lower.find("without vocal") != std::string::npos ||
            lower.find("minus one") != std::string::npos ||
            lower.find("no vocal") != std::string::npos ||
            lower.find("(inst)") != std::string::npos ||
            lower.find("[inst]") != std::string::npos ||
            lower.find("(inst.)") != std::string::npos ||
            lower.find("[inst.]") != std::string::npos);
}

inline std::vector<std::string> TokenizeWords(const std::string& str) {
    std::vector<std::string> tokens;
    std::string current;
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = (unsigned char)str[i];
        if (std::isalnum(c) || c >= 0x80) {
            if (std::isupper(c)) c = (unsigned char)std::tolower(c);
            current.push_back((char)c);
        } else {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        }
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

inline double ComputeStringSimilarity(const std::string& s1, const std::string& s2) {
    if (s1.empty() && s2.empty()) return 1.0;
    if (s1.empty() || s2.empty()) return 0.0;
    if (s1 == s2) return 1.0;

    std::string n1 = NormalizeKey(s1);
    std::string n2 = NormalizeKey(s2);

    if (n1 == n2 && !n1.empty()) return 1.0;

    std::string low1 = s1;
    std::transform(low1.begin(), low1.end(), low1.begin(), [](unsigned char c) { return (char)::tolower(c); });
    std::string low2 = s2;
    std::transform(low2.begin(), low2.end(), low2.begin(), [](unsigned char c) { return (char)::tolower(c); });

    size_t maxLen = (std::max)(n1.size(), n2.size());
    double levSim = 0.0;
    if (maxLen > 0) {
        size_t dist = ComputeLevenshteinDistance(n1, n2);
        levSim = 1.0 - (double)dist / (double)maxLen;
        if (levSim < 0.0) levSim = 0.0;
    }

    auto tok1 = TokenizeWords(s1);
    auto tok2 = TokenizeWords(s2);
    double tokenSim = 0.0;
    double containmentSim = 0.0;
    int commonCount = 0;

    if (!tok1.empty() && !tok2.empty()) {
        std::unordered_map<std::string, int> countMap;
        for (const auto& t : tok1) countMap[t]++;
        for (const auto& t : tok2) {
            auto it = countMap.find(t);
            if (it != countMap.end() && it->second > 0) {
                commonCount++;
                it->second--;
            }
        }

        int totalTokens = (int)(tok1.size() + tok2.size() - commonCount);
        if (totalTokens > 0) {
            tokenSim = (double)commonCount / (double)totalTokens;
        }

        int minTokens = (int)(std::min)(tok1.size(), tok2.size());
        int maxTokens = (int)(std::max)(tok1.size(), tok2.size());
        // Only provide containment bonus if there are at least 2 tokens fully contained
        // This prevents 1-word stop words/generic words ("The", "One", "Daft", "Run") from inflating similarity
        if (minTokens >= 2 && commonCount == minTokens) {
            containmentSim = 0.80 + 0.15 * ((double)minTokens / (double)maxTokens);
        }
    }

    // Substring containment for unspaced / CJK strings:
    // Only apply if the shorter string constitutes at least 70% of the longer string,
    // and use the raw ratio without an artificial +0.80 baseline.
    double subSim = 0.0;
    if (!low1.empty() && !low2.empty()) {
        if (low1.find(low2) != std::string::npos || low2.find(low1) != std::string::npos) {
            double rawSubRatio = (double)(std::min)(low1.size(), low2.size()) / (double)(std::max)(low1.size(), low2.size());
            if (rawSubRatio >= 0.70) {
                subSim = rawSubRatio;
            }
        }
    }

    // When there are NO shared word tokens (commonCount == 0), check word boundaries for Levenshtein:
    // If a much shorter string (length ratio < 0.60) does NOT align with either prefix or suffix boundary,
    // scale levSim so incidental internal character matches (e.g. "Cat" in "Sophisticated", "War" in "Software") are discounted.
    if (commonCount == 0 && levSim > 0.0 && !n1.empty() && !n2.empty()) {
        const std::string& shorter = (n1.size() <= n2.size()) ? n1 : n2;
        const std::string& longer = (n1.size() <= n2.size()) ? n2 : n1;
        double lenRatio = (double)shorter.size() / (double)longer.size();
        if (lenRatio < 0.60) {
            bool isPrefix = (longer.rfind(shorter, 0) == 0);
            bool isSuffix = (longer.size() >= shorter.size() &&
                             longer.compare(longer.size() - shorter.size(), shorter.size(), shorter) == 0);
            if (!isPrefix && !isSuffix) {
                levSim *= 0.85;
            }
        }
    }

    double best = (std::max)({levSim, subSim, tokenSim, containmentSim});
    if (best > 1.0) best = 1.0;
    if (best < 0.0) best = 0.0;
    return best;
}

struct GuardrailValidationResult {
    bool passed{false};
    double confidence{0.0};       // 0.0 to 1.0
    double artistSimilarity{0.0};
    double tracklistOverlap{0.0};
    std::string reason;
};

inline GuardrailValidationResult ValidateAlbumMatch(
    const std::string& queryArtist,
    const std::string& queryAlbum,
    const std::vector<std::string>& localTitles,
    const std::string& candidateArtist,
    const std::string& candidateAlbum,
    const std::vector<std::string>& candidateTracklist
) {
    GuardrailValidationResult res;

    if (localTitles.empty() && candidateTracklist.empty()) {
        res.passed = false;
        res.confidence = 0.0;
        res.artistSimilarity = 0.0;
        res.tracklistOverlap = 0.0;
        res.reason = "Rejected: empty inputs";
        return res;
    }

    if (IsUnknownArtist(queryArtist) || IsUnknownArtist(candidateArtist)) {
        res.artistSimilarity = 0.0;
    } else {
        res.artistSimilarity = ComputeStringSimilarity(queryArtist, candidateArtist);
    }

    size_t N = localTitles.size();
    size_t M = candidateTracklist.size();

    size_t matchedCount = 0;
    if (N == 0 || M == 0) {
        res.tracklistOverlap = 0.0;
    } else {
        for (const auto& loc : localTitles) {
            int numLoc = ExtractTrailingOrEmbeddedNumber(loc);
            double bestSim = 0.0;
            for (const auto& cand : candidateTracklist) {
                int numCand = ExtractTrailingOrEmbeddedNumber(cand);
                if (numLoc >= 0 && numCand >= 0 && numLoc != numCand) {
                    continue;
                }
                double sim = ComputeStringSimilarity(loc, cand);
                if (sim > bestSim) bestSim = sim;
            }
            if (bestSim >= 0.70) {
                matchedCount++;
            }
        }

        double R_title = (double)matchedCount / (double)N;
        double R_count = 0.0;
        if (N == 1) {
            R_count = (matchedCount == 1) ? 1.0 : 0.0;
        } else {
            R_count = (double)(std::min)(N, M) / (double)(std::max)(N, M);
        }

        if (matchedCount == 0) {
            res.tracklistOverlap = 0.0;
        } else {
            res.tracklistOverlap = R_title * (0.70 + 0.30 * R_count);
        }
    }

    bool hasValidAlbum = !IsUnknownArtist(queryAlbum) && !queryAlbum.empty();
    double rawConfidence = 0.0;
    if (hasValidAlbum) {
        double albumSim = ComputeStringSimilarity(queryAlbum, candidateAlbum);
        rawConfidence = 0.40 * res.artistSimilarity + 0.40 * res.tracklistOverlap + 0.20 * albumSim;
    } else {
        rawConfidence = 0.50 * res.artistSimilarity + 0.50 * res.tracklistOverlap;
    }

    if (res.artistSimilarity < 0.60) {
        res.confidence = (std::min)(rawConfidence * 0.25, 0.15);
    } else if (N >= 2 && res.tracklistOverlap < 0.40) {
        res.confidence = (std::min)(rawConfidence, 0.35);
    } else {
        res.confidence = rawConfidence;
    }

    if (res.confidence > 1.0) res.confidence = 1.0;
    if (res.confidence < 0.0) res.confidence = 0.0;

    res.passed = (res.confidence >= 0.80);

    if (res.passed) {
        res.reason = "Approved: confidence " + std::to_string((int)(res.confidence * 100)) +
                     "% >= 80% (artist: " + std::to_string((int)(res.artistSimilarity * 100)) +
                     "%, tracklist: " + std::to_string((int)(res.tracklistOverlap * 100)) + "%)";
    } else {
        if (res.artistSimilarity < 0.60) {
            res.reason = "Rejected: divergent artist similarity (" +
                         std::to_string((int)(res.artistSimilarity * 100)) + "% < 80%) ['" +
                         queryArtist + "' vs '" + candidateArtist + "']";
        } else if (N >= 2 && res.tracklistOverlap < 0.40) {
            res.reason = "Rejected: tracklist overlap too low (" +
                         std::to_string((int)(res.tracklistOverlap * 100)) + "% < 80%) [" +
                         std::to_string(matchedCount) + "/" + std::to_string(N) + " tracks matched]";
        } else {
            res.reason = "Rejected: combined confidence " +
                         std::to_string((int)(res.confidence * 100)) + "% < 80%";
        }
    }

    return res;
}

inline bool ValidateLyricMatch(
    const std::string& trackArtist,
    const std::string& trackTitle,
    const std::string& lyricArtist,
    const std::string& lyricTitle
) {
    if (IsUnknownArtist(trackArtist) || IsUnknownArtist(lyricArtist)) {
        return false;
    }

    if (NormalizeKey(trackTitle).empty() || NormalizeKey(lyricTitle).empty()) {
        return false;
    }

    if (IsInstrumentalTitle(trackTitle) || IsInstrumentalTitle(lyricTitle)) {
        return false;
    }

    int trackNum = ExtractTrailingOrEmbeddedNumber(trackTitle);
    int lyricNum = ExtractTrailingOrEmbeddedNumber(lyricTitle);
    if (trackNum >= 0 && lyricNum >= 0 && trackNum != lyricNum) {
        return false;
    }

    double artistSim = ComputeStringSimilarity(trackArtist, lyricArtist);
    if (artistSim < 0.75) {
        return false;
    }

    double titleSim = ComputeStringSimilarity(trackTitle, lyricTitle);
    if (titleSim < 0.70) {
        return false;
    }

    double confidence = 0.60 * artistSim + 0.40 * titleSim;
    return confidence >= 0.75;
}

