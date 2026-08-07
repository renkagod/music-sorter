#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <windows.h>
#include <commctrl.h>
#include <immintrin.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <filesystem>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

// Global Audio Engine & Sounds
ma_engine g_AudioEngine;
ma_sound g_SoundA;
ma_sound g_SoundB;
bool g_SoundA_Loaded = false;
bool g_SoundB_Loaded = false;
bool g_IsPlaying = false;
char g_ActiveChannel = 'a'; // 'a' or 'b'

// Paths
std::string g_BaseDir;
std::string g_FpcalcBin;
std::string g_ToSortDir;
std::string g_DeleteDir;

// Candidate Pair Structure
struct ABCandidatePair {
    std::string id;
    std::string trackA_path;
    std::string relA;
    std::string extA;
    double durA;
    std::string trackB_path;
    std::string relB;
    std::string extB;
    double durB;
    double similarity;
    int offset;
};

std::vector<ABCandidatePair> g_Candidates;
size_t g_CurrentCandidateIndex = 0;
std::mutex g_LogMutex;
std::vector<std::string> g_Logs;

// Win32 Control IDs
#define ID_BTN_SCAN       1001
#define ID_BTN_PLAY       1002
#define ID_BTN_TOGGLE_A   1003
#define ID_BTN_TOGGLE_B   1004
#define ID_BTN_KEEP_A     1005
#define ID_BTN_KEEP_B     1006
#define ID_SEEK_SLIDER    1007
#define ID_TIMER_SEEK     2001

HWND g_hWnd = NULL;
HWND g_hBtnScan = NULL;
HWND g_hBtnPlay = NULL;
HWND g_hBtnToggleA = NULL;
HWND g_hBtnToggleB = NULL;
HWND g_hBtnKeepA = NULL;
HWND g_hBtnKeepB = NULL;
HWND g_hSeekSlider = NULL;
HWND g_hLblStatus = NULL;
HWND g_hLblCardA = NULL;
HWND g_hLblCardB = NULL;
HWND g_hLblSim = NULL;
HWND g_hLblTime = NULL;
HWND g_hLogBox = NULL;

void LogMsg(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    g_Logs.push_back(msg);
    if (g_hLogBox) {
        std::string full_log;
        for (const auto& l : g_Logs) full_log += l + "\r\n";
        SetWindowTextA(g_hLogBox, full_log.c_str());
        SendMessageA(g_hLogBox, EM_LINESCROLL, 0, g_Logs.size());
    }
}

// SIMD Cross-Correlation Alignment
int BitCount64(unsigned long long v) {
#if defined(_MSC_VER)
    return (int)__popcnt64(v);
#else
    return __builtin_popcountll(v);
#endif
}

double AlignedCrossCorrelation(const std::vector<unsigned int>& fp1, const std::vector<unsigned int>& fp2, int max_offset, int& out_offset) {
    if (fp1.empty() || fp2.empty()) return 0.0;

    double best_sim = 0.0;
    out_offset = 0;

    for (int offset = -max_offset; offset <= max_offset; ++offset) {
        size_t start1 = (offset >= 0) ? offset : 0;
        size_t start2 = (offset < 0) ? -offset : 0;

        size_t len1 = fp1.size() > start1 ? fp1.size() - start1 : 0;
        size_t len2 = fp2.size() > start2 ? fp2.size() - start2 : 0;
        size_t min_len = (std::min)(len1, len2);

        if (min_len < 30) continue;

        unsigned long long matching_bits = 0;
        for (size_t i = 0; i < min_len; ++i) {
            unsigned int x = fp1[start1 + i] ^ fp2[start2 + i];
            matching_bits += (32 - BitCount64(x));
        }

        double sim = (double)matching_bits / (double)(min_len * 32);
        if (sim > best_sim) {
            best_sim = sim;
            out_offset = offset;
        }
    }

    return best_sim;
}

// Fingerprint Extraction via fpcalc.exe
struct RawFP {
    std::string path;
    double dur = 0.0;
    std::vector<unsigned int> fp;
};

RawFP GetFpcalcRaw(const std::string& filepath) {
    RawFP result;
    result.path = filepath;
    std::string cmd = "\"" + g_FpcalcBin + "\" -raw \"" + filepath + "\"";

    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return result;

    char buffer[128];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        output += buffer;
    }
    _pclose(pipe);

    std::stringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.rfind("DURATION=", 0) == 0) {
            result.dur = std::stod(line.substr(9));
        } else if (line.rfind("FINGERPRINT=", 0) == 0) {
            std::stringstream fpss(line.substr(12));
            std::string val;
            while (std::getline(fpss, val, ',')) {
                if (!val.empty()) result.fp.push_back((unsigned int)std::stoul(val));
            }
        }
    }
    return result;
}

void SetActiveAudioChannel(char ch) {
    g_ActiveChannel = ch;
    if (ch == 'a') {
        if (g_SoundA_Loaded) ma_sound_set_volume(&g_SoundA, 1.0f);
        if (g_SoundB_Loaded) ma_sound_set_volume(&g_SoundB, 0.0f);
        SetWindowTextA(g_hBtnToggleA, "🔊 ТРЕК А (АКТИВЕН)");
        SetWindowTextA(g_hBtnToggleB, "🔈 ТРЕК Б");
    } else {
        if (g_SoundA_Loaded) ma_sound_set_volume(&g_SoundA, 0.0f);
        if (g_SoundB_Loaded) ma_sound_set_volume(&g_SoundB, 1.0f);
        SetWindowTextA(g_hBtnToggleA, "🔈 ТРЕК А");
        SetWindowTextA(g_hBtnToggleB, "🔊 ТРЕК Б (АКТИВЕН)");
    }
}

void LoadCandidatePairIntoPlayer(const ABCandidatePair& pair) {
    if (g_SoundA_Loaded) { ma_sound_uninit(&g_SoundA); g_SoundA_Loaded = false; }
    if (g_SoundB_Loaded) { ma_sound_uninit(&g_SoundB); g_SoundB_Loaded = false; }

    g_IsPlaying = false;
    SetWindowTextA(g_hBtnPlay, "▶ Проигрывать");

    if (ma_sound_init_from_file(&g_AudioEngine, pair.trackA_path.c_str(), 0, NULL, NULL, &g_SoundA) == MA_SUCCESS) {
        g_SoundA_Loaded = true;
    }
    if (ma_sound_init_from_file(&g_AudioEngine, pair.trackB_path.c_str(), 0, NULL, NULL, &g_SoundB) == MA_SUCCESS) {
        g_SoundB_Loaded = true;
    }

    std::string cardA_text = "ТРЕК А: " + fs::path(pair.trackA_path).filename().string() + "\n" + pair.extA + " | " + std::to_string((int)pair.durA) + "s\n" + pair.relA;
    std::string cardB_text = "ТРЕК Б: " + fs::path(pair.trackB_path).filename().string() + "\n" + pair.extB + " | " + std::to_string((int)pair.durB) + "s\n" + pair.relB;
    std::string sim_text = "Сходство волн: " + std::to_string((int)pair.similarity) + "% | Смещение: " + std::to_string(pair.offset) + " кадров";

    SetWindowTextA(g_hLblCardA, cardA_text.c_str());
    SetWindowTextA(g_hLblCardB, cardB_text.c_str());
    SetWindowTextA(g_hLblSim, sim_text.c_str());

    SetActiveAudioChannel('a');
}

void TogglePlayPause() {
    if (!g_SoundA_Loaded && !g_SoundB_Loaded) return;

    if (g_IsPlaying) {
        if (g_SoundA_Loaded) ma_sound_stop(&g_SoundA);
        if (g_SoundB_Loaded) ma_sound_stop(&g_SoundB);
        g_IsPlaying = false;
        SetWindowTextA(g_hBtnPlay, "▶ Проигрывать");
    } else {
        if (g_SoundA_Loaded) ma_sound_start(&g_SoundA);
        if (g_SoundB_Loaded) ma_sound_start(&g_SoundB);
        g_IsPlaying = true;
        SetWindowTextA(g_hBtnPlay, "⏸ Пауза");
    }
}

void MakeDecision(char keepChoice) {
    if (g_Candidates.empty() || g_CurrentCandidateIndex >= g_Candidates.size()) return;

    ABCandidatePair pair = g_Candidates[g_CurrentCandidateIndex];
    std::string rejected = (keepChoice == 'a') ? pair.trackB_path : pair.trackA_path;

    if (fs::exists(rejected)) {
        fs::path rel = fs::relative(rejected, g_BaseDir);
        fs::path dst = fs::path(g_DeleteDir) / rel;
        fs::create_directories(dst.parent_path());
        LogMsg("[DECISION] Moving rejected candidate to delete/: " + rel.string());
        if (fs::exists(dst)) fs::remove(dst);
        fs::rename(rejected, dst);
    }

    g_CurrentCandidateIndex++;
    std::string count_str = "Кандидатов: " + std::to_string(g_Candidates.size() - g_CurrentCandidateIndex);
    SetWindowTextA(g_hLblStatus, count_str.c_str());

    if (g_CurrentCandidateIndex < g_Candidates.size()) {
        LoadCandidatePairIntoPlayer(g_Candidates[g_CurrentCandidateIndex]);
    } else {
        if (g_SoundA_Loaded) ma_sound_uninit(&g_SoundA);
        if (g_SoundB_Loaded) ma_sound_uninit(&g_SoundB);
        g_SoundA_Loaded = g_SoundB_Loaded = false;
        SetWindowTextA(g_hLblCardA, "✨ Все кандидаты обработаны!");
        SetWindowTextA(g_hLblCardB, "Отклоненные файлы находятся в d:\\media\\music\\delete\\");
    }
}

// Background Scan Thread
void RunScanThread() {
    LogMsg("=== C++ SIMD Multi-threaded Scan Pipeline Starting ===");
    std::vector<std::string> files_to_scan;

    for (auto& p : fs::recursive_directory_iterator(g_ToSortDir)) {
        if (p.is_regular_file()) {
            std::string ext = p.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".flac" || ext == ".mp3") {
                files_to_scan.push_back(p.path().string());
            }
        }
    }

    LogMsg("Found " + std::to_string(files_to_scan.size()) + " audio files in TO SORT.");
    if (files_to_scan.empty()) {
        EnableWindow(g_hBtnScan, TRUE);
        return;
    }

    std::vector<RawFP> fps;
    for (size_t i = 0; i < files_to_scan.size(); ++i) {
        RawFP fp_res = GetFpcalcRaw(files_to_scan[i]);
        if (!fp_res.fp.empty()) fps.push_back(fp_res);
    }

    LogMsg("Computed Chromaprint AcoustID fingerprints for " + std::to_string(fps.size()) + " tracks.");

    g_Candidates.clear();
    g_CurrentCandidateIndex = 0;
    std::vector<std::string> auto_del;

    for (size_t i = 0; i < fps.size(); ++i) {
        for (size_t j = i + 1; j < fps.size(); ++j) {
            if (std::abs(fps[i].dur - fps[j].dur) <= 5.0) {
                int offset = 0;
                double sim = AlignedCrossCorrelation(fps[i].fp, fps[j].fp, 250, offset);

                std::string ext1 = fs::path(fps[i].path).extension().string();
                std::string ext2 = fs::path(fps[j].path).extension().string();
                std::transform(ext1.begin(), ext1.end(), ext1.begin(), ::tolower);
                std::transform(ext2.begin(), ext2.end(), ext2.begin(), ::tolower);

                if (sim >= 0.95) {
                    if (ext1 == ".flac" && ext2 == ".mp3") auto_del.push_back(fps[j].path);
                    else if (ext2 == ".flac" && ext1 == ".mp3") auto_del.push_back(fps[i].path);
                    else auto_del.push_back(fps[j].path);
                } else if (sim >= 0.75) {
                    ABCandidatePair pair;
                    pair.id = "pair_" + std::to_string(g_Candidates.size() + 1);
                    pair.trackA_path = fps[i].path;
                    pair.relA = fs::relative(fps[i].path, g_BaseDir).string();
                    pair.extA = ext1;
                    pair.durA = fps[i].dur;
                    pair.trackB_path = fps[j].path;
                    pair.relB = fs::relative(fps[j].path, g_BaseDir).string();
                    pair.extB = ext2;
                    pair.durB = fps[j].dur;
                    pair.similarity = sim * 100.0;
                    pair.offset = offset;
                    g_Candidates.push_back(pair);
                }
            }
        }
    }

    for (const auto& p : auto_del) {
        if (fs::exists(p)) {
            fs::path rel = fs::relative(p, g_BaseDir);
            fs::path dst = fs::path(g_DeleteDir) / rel;
            fs::create_directories(dst.parent_path());
            LogMsg("[AUTO-DELETE] Moving exact duplicate MP3 to delete/: " + rel.string());
            if (fs::exists(dst)) fs::remove(dst);
            fs::rename(p, dst);
        }
    }

    LogMsg("Scan complete. Found " + std::to_string(g_Candidates.size()) + " candidates for A/B comparison.");
    EnableWindow(g_hBtnScan, TRUE);

    if (!g_Candidates.empty()) {
        LoadCandidatePairIntoPlayer(g_Candidates[0]);
    }
}

// Win32 WndProc
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g_hBtnScan = CreateWindowA("BUTTON", "🔍 Сканировать TO SORT", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 20, 20, 220, 36, hWnd, (HMENU)ID_BTN_SCAN, NULL, NULL);

        g_hLblCardA = CreateWindowA("STATIC", "ТРЕК А: Ожидание сканирования...", WS_VISIBLE | WS_CHILD | SS_LEFT, 20, 70, 440, 100, hWnd, NULL, NULL, NULL);
        g_hLblCardB = CreateWindowA("STATIC", "ТРЕК Б: Ожидание сканирования...", WS_VISIBLE | WS_CHILD | SS_LEFT, 480, 70, 440, 100, hWnd, NULL, NULL, NULL);

        g_hBtnKeepA = CreateWindowA("BUTTON", "⚪ Оставить Трек А (Б ➔ delete)", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 20, 180, 440, 36, hWnd, (HMENU)ID_BTN_KEEP_A, NULL, NULL);
        g_hBtnKeepB = CreateWindowA("BUTTON", "⚪ Оставить Трек Б (А ➔ delete)", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 480, 180, 440, 36, hWnd, (HMENU)ID_BTN_KEEP_B, NULL, NULL);

        g_hLblSim = CreateWindowA("STATIC", "Сходство: --- % | Смещение: --- кадров", WS_VISIBLE | WS_CHILD | SS_CENTER, 20, 235, 900, 20, hWnd, NULL, NULL, NULL);

        g_hSeekSlider = CreateWindowA(TRACKBAR_CLASSA, "", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, 20, 260, 900, 30, hWnd, (HMENU)ID_SEEK_SLIDER, NULL, NULL);
        SendMessage(g_hSeekSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 1000));

        g_hBtnPlay = CreateWindowA("BUTTON", "▶ Проигрывать", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 20, 300, 140, 36, hWnd, (HMENU)ID_BTN_PLAY, NULL, NULL);
        g_hBtnToggleA = CreateWindowA("BUTTON", "🔊 ТРЕК А (АКТИВЕН)", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 170, 300, 200, 36, hWnd, (HMENU)ID_BTN_TOGGLE_A, NULL, NULL);
        g_hBtnToggleB = CreateWindowA("BUTTON", "🔈 ТРЕК Б", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 380, 300, 200, 36, hWnd, (HMENU)ID_BTN_TOGGLE_B, NULL, NULL);

        g_hLblStatus = CreateWindowA("STATIC", "Горячая клавиша: Tab / S (Переключение без паузы) | 1: Оставить A | 2: Оставить B", WS_VISIBLE | WS_CHILD | SS_LEFT, 600, 310, 320, 20, hWnd, NULL, NULL, NULL);

        g_hLogBox = CreateWindowA("EDIT", "Готов к работе...", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 20, 360, 900, 240, hWnd, NULL, NULL, NULL);

        SetTimer(hWnd, ID_TIMER_SEEK, 100, NULL);
        break;
    }
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        switch (wmId) {
        case ID_BTN_SCAN:
            EnableWindow(g_hBtnScan, FALSE);
            std::thread(RunScanThread).detach();
            break;
        case ID_BTN_PLAY:
            TogglePlayPause();
            break;
        case ID_BTN_TOGGLE_A:
            SetActiveAudioChannel('a');
            break;
        case ID_BTN_TOGGLE_B:
            SetActiveAudioChannel('b');
            break;
        case ID_BTN_KEEP_A:
            MakeDecision('a');
            break;
        case ID_BTN_KEEP_B:
            MakeDecision('b');
            break;
        }
        break;
    }
    case WM_TIMER: {
        if (wParam == ID_TIMER_SEEK && g_IsPlaying && g_SoundA_Loaded) {
            ma_uint64 cursor, length;
            ma_sound_get_cursor_in_pcm_frames(&g_SoundA, &cursor);
            ma_sound_get_length_in_pcm_frames(&g_SoundA, &length);
            if (length > 0) {
                int pos = (int)(((double)cursor / (double)length) * 1000.0);
                SendMessage(g_hSeekSlider, TBM_SETPOS, TRUE, pos);
            }
        }
        break;
    }
    case WM_KEYDOWN: {
        if (wParam == VK_SPACE) TogglePlayPause();
        else if (wParam == VK_TAB || wParam == 'S') SetActiveAudioChannel(g_ActiveChannel == 'a' ? 'b' : 'a');
        else if (wParam == '1' || wParam == 'A') MakeDecision('a');
        else if (wParam == '2' || wParam == 'B') MakeDecision('b');
        break;
    }
    case WM_DESTROY:
        KillTimer(hWnd, ID_TIMER_SEEK);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    InitCommonControls();

    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    g_BaseDir = fs::path(exe_path).parent_path().parent_path().string();
    g_FpcalcBin = (fs::path(g_BaseDir) / "fpcalc.exe").string();
    g_ToSortDir = (fs::path(g_BaseDir) / "TO SORT").string();
    g_DeleteDir = (fs::path(g_BaseDir) / "delete").string();

    ma_engine_config engineConfig = ma_engine_config_init();
    if (ma_engine_init(&engineConfig, &g_AudioEngine) != MA_SUCCESS) {
        MessageBoxA(NULL, "Failed to initialize miniaudio engine!", "Error", MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXA wcex = { sizeof(WNDCLASSEXA) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = "MusicSorterWinClass";

    RegisterClassExA(&wcex);

    g_hWnd = CreateWindowA("MusicSorterWinClass", "MusicSorter Desktop App (Native C++20)", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT, 960, 660, NULL, NULL, hInstance, NULL);
    if (!g_hWnd) return 0;

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    ma_engine_uninit(&g_AudioEngine);
    return (int)msg.wParam;
}
