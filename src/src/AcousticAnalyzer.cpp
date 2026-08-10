#include "../include/AcousticAnalyzer.hpp"
#include "../include/Logger.hpp"

#include <windows.h>
#include <immintrin.h>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <thread>
#include <future>

static int BitCount64(unsigned long long v) {
#if defined(_MSC_VER)
    return (int)__popcnt64(v);
#else
    return __builtin_popcountll(v);
#endif
}

static std::wstring Utf8ToWideStr(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    std::wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstr[0], len);
    return wstr;
}

AudioFingerprint AcousticAnalyzer::ExtractFingerprint(const std::string& filepath) {
    AudioFingerprint res;
    res.path = filepath;

    std::wstring wBin = Utf8ToWideStr(m_fpcalcBin);
    std::wstring wFile = Utf8ToWideStr(filepath);
    std::wstring wCmd = L"\"" + wBin + L"\" -raw \"" + wFile + L"\"";

    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return res;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };
    std::vector<wchar_t> cmdBuf(wCmd.begin(), wCmd.end());
    cmdBuf.push_back(L'\0');

    if (!CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return res;
    }

    CloseHandle(hWrite);

    char buffer[4096];
    DWORD bytesRead = 0;
    std::string output;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }

    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::stringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.rfind("DURATION=", 0) == 0) {
            try { res.duration = std::stod(line.substr(9)); } catch (...) {}
        } else if (line.rfind("FINGERPRINT=", 0) == 0) {
            std::stringstream fpss(line.substr(12));
            std::string val;
            while (std::getline(fpss, val, ',')) {
                if (!val.empty()) {
                    try { res.fpData.push_back((unsigned int)std::stoul(val)); } catch (...) {}
                }
            }
        }
    }

    if (res.fpData.empty()) {
        LOG_WARN("fpcalc failed to produce fingerprint for: " + fs::path(filepath).filename().string());
    }

    return res;
}

double AcousticAnalyzer::CalculateSlidingCrossCorrelation(
    const std::vector<unsigned int>& fp1,
    const std::vector<unsigned int>& fp2,
    int maxOffsetFrames,
    int& outOffset
) {
    if (fp1.empty() || fp2.empty()) return 0.0;

    double best_sim = 0.0;
    outOffset = 0;

    for (int offset = -maxOffsetFrames; offset <= maxOffsetFrames; ++offset) {
        size_t start1 = (offset >= 0) ? offset : 0;
        size_t start2 = (offset < 0) ? -offset : 0;

        size_t len1 = fp1.size() > start1 ? fp1.size() - start1 : 0;
        size_t len2 = fp2.size() > start2 ? fp2.size() - start2 : 0;
        size_t min_len = (std::min)(len1, len2);

        if (min_len < 20) continue;

        unsigned long long matching_bits = 0;
        for (size_t i = 0; i < min_len; ++i) {
            unsigned int x = fp1[start1 + i] ^ fp2[start2 + i];
            matching_bits += (32 - BitCount64(x));
        }

        double sim = (double)matching_bits / (double)(min_len * 32);
        if (sim > best_sim) {
            best_sim = sim;
            outOffset = offset;
        }
    }

    return round(best_sim * 10000.0) / 10000.0;
}

void AcousticAnalyzer::AnalyzeDirectory(
    const std::string& directoryPath,
    const std::string& baseDirectory,
    std::vector<ABCandidatePair>& outCandidates,
    std::vector<std::string>& outAutoDelete
) {
    LOG_INFO("Scanning directory: " + directoryPath);
    std::vector<std::string> files;

    if (!fs::exists(directoryPath)) {
        LOG_WARN("Target directory does not exist: " + directoryPath);
        return;
    }

    for (auto& p : fs::recursive_directory_iterator(directoryPath)) {
        if (p.is_regular_file()) {
            std::string ext = p.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".flac" || ext == ".mp3") {
                files.push_back(p.path().string());
            }
        }
    }

    LOG_INFO("Found " + std::to_string(files.size()) + " audio files to analyze.");
    if (files.empty()) return;

    // Parallel Fingerprint Calculation
    std::vector<AudioFingerprint> fpResults(files.size());
    std::vector<std::future<void>> futures;

    for (size_t i = 0; i < files.size(); ++i) {
        futures.push_back(std::async(std::launch::async, [this, i, &files, &fpResults]() {
            fpResults[i] = ExtractFingerprint(files[i]);
        }));
    }

    for (auto& f : futures) f.wait();
    LOG_INFO("Computed AcoustID fingerprints for " + std::to_string(fpResults.size()) + " files.");

    // Pairwise Sliding Cross-Correlation Analysis
    for (size_t i = 0; i < fpResults.size(); ++i) {
        for (size_t j = i + 1; j < fpResults.size(); ++j) {
            auto& f1 = fpResults[i];
            auto& f2 = fpResults[j];

            if (f1.fpData.empty() || f2.fpData.empty()) continue;

            if (std::abs(f1.duration - f2.duration) <= 5.0) {
                int offset = 0;
                double sim = CalculateSlidingCrossCorrelation(f1.fpData, f2.fpData, 250, offset);

                std::string ext1 = fs::path(f1.path).extension().string();
                std::string ext2 = fs::path(f2.path).extension().string();
                std::transform(ext1.begin(), ext1.end(), ext1.begin(), ::tolower);
                std::transform(ext2.begin(), ext2.end(), ext2.begin(), ::tolower);

                std::string rel1 = fs::relative(f1.path, baseDirectory).string();
                std::string rel2 = fs::relative(f2.path, baseDirectory).string();

                bool isExplicitCopyFolder = (f1.path.find("- Copy") != std::string::npos || f1.path.find(" (1)") != std::string::npos ||
                                             f2.path.find("- Copy") != std::string::npos || f2.path.find(" (1)") != std::string::npos);
                bool isFlacMp3Pair = (ext1 != ext2 && (ext1 == ".mp3" || ext2 == ".mp3"));

                // Auto-delete applies ONLY to:
                // 1. Exact 96%+ identical wave matches
                // 2. FLAC vs MP3 pairs that are 85%+ similar (MP3 auto-deleted) — previously deleted at ANY similarity!
                // 3. Explicit "- Copy" folders that are 85%+ similar (copy auto-deleted)
                if (sim >= 0.96 || (isFlacMp3Pair && sim >= 0.85) || (sim >= 0.85 && isExplicitCopyFolder)) {
                    std::string autoDelPath;
                    if (ext1 == ".flac" && ext2 == ".mp3") autoDelPath = f2.path;
                    else if (ext2 == ".flac" && ext1 == ".mp3") autoDelPath = f1.path;
                    else if (f2.path.find("- Copy") != std::string::npos || f2.path.find(" (1)") != std::string::npos) autoDelPath = f2.path;
                    else if (f1.path.find("- Copy") != std::string::npos || f1.path.find(" (1)") != std::string::npos) autoDelPath = f1.path;
                    else autoDelPath = f2.path;

                    LOG_INFO("[AUTO-DELETE MATCH " + std::to_string((int)(sim * 100)) + "%] Moving duplicate to delete/: " + fs::relative(autoDelPath, baseDirectory).string());
                    outAutoDelete.push_back(autoDelPath);
                } else if (sim >= 0.70) {
                    // Alternate album mixes, reissues, demos (70% - 95% similarity) are sent to A/B Comparison Screen for user decision!
                    LOG_INFO("[A/B CANDIDATE MATCH " + std::to_string((int)(sim * 100)) + "%] " + rel1 + " <==> " + rel2);
                    ABCandidatePair pair;
                    pair.id = "pair_" + std::to_string(outCandidates.size() + 1);
                    pair.trackA_path = f1.path;
                    pair.relA = rel1;
                    pair.extA = ext1;
                    pair.durA = f1.duration;
                    pair.trackB_path = f2.path;
                    pair.relB = rel2;
                    pair.extB = ext2;
                    pair.durB = f2.duration;
                    pair.similarity = round(sim * 100.0 * 10.0) / 10.0;
                    pair.offset = offset;
                    outCandidates.push_back(pair);
                }
            }
        }
    }
}
