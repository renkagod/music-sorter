#include "../include/AppWindow.hpp"
#include "../include/AudioEngine.hpp"
#include "../include/Logger.hpp"

#include <shellapi.h>
#include <thread>
#include <sstream>

#define ID_BTN_SCAN       1001
#define ID_BTN_PLAY       1002
#define ID_BTN_TOGGLE_A   1003
#define ID_BTN_TOGGLE_B   1004
#define ID_BTN_KEEP_A     1005
#define ID_BTN_KEEP_B     1006
#define ID_SEEK_SLIDER    1007
#define ID_TIMER_SEEK     2001

extern std::string g_BaseDir;
extern std::string g_ToSortDir;
extern std::string g_DeleteDir;

struct ProgressData {
    int percent;
    std::string text;
};

bool AppWindow::Initialize(HINSTANCE hInstance, int nCmdShow) {
    m_hInstance = hInstance;

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_PROGRESS_CLASS | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXA wcex = { sizeof(WNDCLASSEXA) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = AppWindow::WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = "MusicSorterNativeClass";

    if (!RegisterClassExA(&wcex)) {
        LOG_ERROR("Failed to register Win32 Window Class!");
        return false;
    }

    m_hWnd = CreateWindowA("MusicSorterNativeClass", "MusicSorter Desktop App (Native C++20 MSVC)", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT, 960, 680, NULL, NULL, hInstance, NULL);
    if (!m_hWnd) return false;

    ShowWindow(m_hWnd, nCmdShow);
    UpdateWindow(m_hWnd);
    return true;
}

void AppWindow::RunMessageLoop() {
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

LRESULT CALLBACK AppWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return Instance().HandleMessage(hWnd, msg, wParam, lParam);
}

LRESULT AppWindow::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        m_hBtnScan = CreateWindowA("BUTTON", "🔍 Сканировать TO SORT", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 20, 20, 220, 36, hWnd, (HMENU)ID_BTN_SCAN, NULL, NULL);

        g_hLblCardA = CreateWindowA("STATIC", "ТРЕК А: Ожидание сканирования...", WS_VISIBLE | WS_CHILD | SS_LEFT, 20, 70, 440, 100, hWnd, NULL, NULL, NULL);
        g_hLblCardB = CreateWindowA("STATIC", "ТРЕК Б: Ожидание сканирования...", WS_VISIBLE | WS_CHILD | SS_LEFT, 480, 70, 440, 100, hWnd, NULL, NULL, NULL);

        m_hBtnKeepA = CreateWindowA("BUTTON", "⚪ Оставить Трек А (Б ➔ delete)", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 20, 180, 440, 36, hWnd, (HMENU)ID_BTN_KEEP_A, NULL, NULL);
        m_hBtnKeepB = CreateWindowA("BUTTON", "⚪ Оставить Трек Б (А ➔ delete)", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 480, 180, 440, 36, hWnd, (HMENU)ID_BTN_KEEP_B, NULL, NULL);

        g_hLblSim = CreateWindowA("STATIC", "Сходство: --- % | Смещение: --- кадров", WS_VISIBLE | WS_CHILD | SS_CENTER, 20, 235, 900, 20, hWnd, NULL, NULL, NULL);

        m_hSeekSlider = CreateWindowA(TRACKBAR_CLASSA, "", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, 20, 260, 900, 30, hWnd, (HMENU)ID_SEEK_SLIDER, NULL, NULL);
        SendMessage(m_hSeekSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 1000));

        m_hBtnPlay = CreateWindowA("BUTTON", "▶ Проигрывать", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 20, 300, 140, 36, hWnd, (HMENU)ID_BTN_PLAY, NULL, NULL);
        m_hBtnToggleA = CreateWindowA("BUTTON", "🔊 ТРЕК А (АКТИВЕН)", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 170, 300, 200, 36, hWnd, (HMENU)ID_BTN_TOGGLE_A, NULL, NULL);
        m_hBtnToggleB = CreateWindowA("BUTTON", "🔈 ТРЕК Б", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 380, 300, 200, 36, hWnd, (HMENU)ID_BTN_TOGGLE_B, NULL, NULL);

        m_hLblStatus = CreateWindowA("STATIC", "Горячие клавиши: Tab/S (A/B Hot-Swap) | 1: Оставить A | 2: Оставить B", WS_VISIBLE | WS_CHILD | SS_LEFT, 600, 310, 320, 20, hWnd, NULL, NULL, NULL);

        m_hProgressBar = CreateWindowA(PROGRESS_CLASSA, "", WS_CHILD | WS_VISIBLE, 20, 345, 900, 10, hWnd, NULL, NULL, NULL);
        SendMessage(m_hProgressBar, PBM_SETRANGE, 0, MAKELONG(0, 100));

        m_hLogBox = CreateWindowA("EDIT", "Готов к работе...", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 20, 365, 900, 240, hWnd, NULL, NULL, NULL);

        Logger::Instance().SetCallback([hWnd](const std::string& msg) {
            std::string* pMsg = new std::string(msg);
            PostMessageA(hWnd, WM_SCAN_PROGRESS, 0, (LPARAM)pMsg);
        });

        SetTimer(hWnd, ID_TIMER_SEEK, 100, NULL);
        break;
    }
    case WM_SCAN_PROGRESS: {
        std::string* pMsg = (std::string*)lParam;
        if (pMsg) {
            std::string curr;
            int len = GetWindowTextLengthA(m_hLogBox);
            if (len < 65536) {
                std::vector<char> buf(len + 1);
                GetWindowTextA(m_hLogBox, buf.data(), len + 1);
                curr = std::string(buf.data());
            }
            curr += *pMsg + "\r\n";
            SetWindowTextA(m_hLogBox, curr.c_str());
            SendMessageA(m_hLogBox, EM_LINESCROLL, 0, 1000);
            delete pMsg;
        }
        break;
    }
    case WM_SCAN_FINISHED: {
        HandleScanFinished();
        break;
    }
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        switch (wmId) {
        case ID_BTN_SCAN: {
            EnableWindow(m_hBtnScan, FALSE);
            m_candidates.clear();
            m_autoDelete.clear();
            m_currentCandidateIndex = 0;

            std::thread([this, hWnd]() {
                AcousticAnalyzer::Instance().AnalyzeDirectory(g_ToSortDir, g_BaseDir, m_candidates, m_autoDelete);
                PostMessageA(hWnd, WM_SCAN_FINISHED, 0, 0);
            }).detach();
            break;
        }
        case ID_BTN_PLAY:
            AudioEngine::Instance().TogglePlay();
            SetWindowTextA(m_hBtnPlay, AudioEngine::Instance().IsPlaying() ? "⏸ Пауза" : "▶ Проигрывать");
            break;
        case ID_BTN_TOGGLE_A:
            AudioEngine::Instance().SetActiveChannel('a');
            SetWindowTextA(m_hBtnToggleA, "🔊 ТРЕК А (АКТИВЕН)");
            SetWindowTextA(m_hBtnToggleB, "🔈 ТРЕК Б");
            break;
        case ID_BTN_TOGGLE_B:
            AudioEngine::Instance().SetActiveChannel('b');
            SetWindowTextA(m_hBtnToggleA, "🔈 ТРЕК А");
            SetWindowTextA(m_hBtnToggleB, "🔊 ТРЕК Б (АКТИВЕН)");
            break;
        case ID_BTN_KEEP_A: {
            if (m_currentCandidateIndex < m_candidates.size()) {
                auto pair = m_candidates[m_currentCandidateIndex];
                fs::path rel = fs::relative(pair.trackB_path, g_BaseDir);
                fs::path dst = fs::path(g_DeleteDir) / rel;
                fs::create_directories(dst.parent_path());
                LOG_INFO("[DECISION] Moving rejected Track B to delete/: " + rel.string());
                if (fs::exists(dst)) fs::remove(dst);
                fs::rename(pair.trackB_path, dst);

                m_currentCandidateIndex++;
                if (m_currentCandidateIndex < m_candidates.size()) {
                    auto next_p = m_candidates[m_currentCandidateIndex];
                    AudioEngine::Instance().LoadTrackA(next_p.trackA_path);
                    AudioEngine::Instance().LoadTrackB(next_p.trackB_path);
                    SetWindowTextA(g_hLblCardA, ("ТРЕК А: " + fs::path(next_p.trackA_path).filename().string() + "\n" + next_p.relA).c_str());
                    SetWindowTextA(g_hLblCardB, ("ТРЕК Б: " + fs::path(next_p.trackB_path).filename().string() + "\n" + next_p.relB).c_str());
                } else {
                    SetWindowTextA(g_hLblCardA, "✨ Все кандидаты обработаны!");
                    SetWindowTextA(g_hLblCardB, "Файлы перемещены в delete/");
                }
            }
            break;
        }
        case ID_BTN_KEEP_B: {
            if (m_currentCandidateIndex < m_candidates.size()) {
                auto pair = m_candidates[m_currentCandidateIndex];
                fs::path rel = fs::relative(pair.trackA_path, g_BaseDir);
                fs::path dst = fs::path(g_DeleteDir) / rel;
                fs::create_directories(dst.parent_path());
                LOG_INFO("[DECISION] Moving rejected Track A to delete/: " + rel.string());
                if (fs::exists(dst)) fs::remove(dst);
                fs::rename(pair.trackA_path, dst);

                m_currentCandidateIndex++;
                if (m_currentCandidateIndex < m_candidates.size()) {
                    auto next_p = m_candidates[m_currentCandidateIndex];
                    AudioEngine::Instance().LoadTrackA(next_p.trackA_path);
                    AudioEngine::Instance().LoadTrackB(next_p.trackB_path);
                    SetWindowTextA(g_hLblCardA, ("ТРЕК А: " + fs::path(next_p.trackA_path).filename().string() + "\n" + next_p.relA).c_str());
                    SetWindowTextA(g_hLblCardB, ("ТРЕК Б: " + fs::path(next_p.trackB_path).filename().string() + "\n" + next_p.relB).c_str());
                } else {
                    SetWindowTextA(g_hLblCardA, "✨ Все кандидаты обработаны!");
                    SetWindowTextA(g_hLblCardB, "Файлы перемещены в delete/");
                }
            }
            break;
        }
        }
        break;
    }
    case WM_TIMER: {
        if (wParam == ID_TIMER_SEEK && AudioEngine::Instance().IsPlaying()) {
            double dur = AudioEngine::Instance().GetDurationSeconds();
            double cur = AudioEngine::Instance().GetCurrentPositionSeconds();
            if (dur > 0.0) {
                int pos = (int)((cur / dur) * 1000.0);
                SendMessage(m_hSeekSlider, TBM_SETPOS, TRUE, pos);
            }
        }
        break;
    }
    case WM_DESTROY:
        KillTimer(hWnd, ID_TIMER_SEEK);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

void AppWindow::HandleScanFinished() {
    EnableWindow(m_hBtnScan, TRUE);
    LOG_INFO("Scan process complete. Found " + std::to_string(m_candidates.size()) + " candidate pairs.");

    for (const auto& p : m_autoDelete) {
        if (fs::exists(p)) {
            fs::path rel = fs::relative(p, g_BaseDir);
            fs::path dst = fs::path(g_DeleteDir) / rel;
            fs::create_directories(dst.parent_path());
            LOG_INFO("[AUTO-DELETE] Moving exact MP3 duplicate to delete/: " + rel.string());
            if (fs::exists(dst)) fs::remove(dst);
            fs::rename(p, dst);
        }
    }

    if (!m_candidates.empty()) {
        m_currentCandidateIndex = 0;
        auto pair = m_candidates[0];
        AudioEngine::Instance().LoadTrackA(pair.trackA_path);
        AudioEngine::Instance().LoadTrackB(pair.trackB_path);

        SetWindowTextA(g_hLblCardA, ("ТРЕК А: " + fs::path(pair.trackA_path).filename().string() + "\n" + pair.relA).c_str());
        SetWindowTextA(g_hLblCardB, ("ТРЕК Б: " + fs::path(pair.trackB_path).filename().string() + "\n" + pair.relB).c_str());
        SetWindowTextA(g_hLblSim, ("Сходство: " + std::to_string((int)pair.similarity) + "% | Смещение: " + std::to_string(pair.offset) + " кадров").c_str());
    } else {
        SetWindowTextA(g_hLblCardA, "✨ Дубликаты и кандидаты не найдены!");
        SetWindowTextA(g_hLblCardB, "Папка TO SORT полностью чиста.");
    }
}
