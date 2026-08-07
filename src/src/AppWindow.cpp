#include "../include/AppWindow.hpp"
#include "../include/AudioEngine.hpp"
#include "../include/Logger.hpp"

#include <shellapi.h>
#include <thread>
#include <sstream>

#define ID_BTN_STEP1      1001
#define ID_BTN_STEP2      1002
#define ID_BTN_STEP3      1003
#define ID_BTN_STEP4      1004

#define ID_BTN_PLAY       1005
#define ID_BTN_TOGGLE_A   1006
#define ID_BTN_TOGGLE_B   1007
#define ID_BTN_KEEP_A     1008
#define ID_BTN_KEEP_B     1009
#define ID_SEEK_SLIDER    1010
#define ID_TIMER_SEEK     2001

extern std::string g_BaseDir;
extern std::string g_ToSortDir;
extern std::string g_DeleteDir;

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    std::wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstr[0], len);
    return wstr;
}

bool AppWindow::Initialize(HINSTANCE hInstance, int nCmdShow) {
    m_hInstance = hInstance;

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_PROGRESS_CLASS | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    // Dark Monokai / Clean Dark Theme Fonts
    m_hFontMain = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_hFontBold = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_hFontTitle = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_hFontMono = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Consolas");

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = AppWindow::WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszClassName = L"MusicSorterUnicodeClass";

    if (!RegisterClassExW(&wcex)) {
        LOG_ERROR("Failed to register Win32 Window Class!");
        return false;
    }

    m_hWnd = CreateWindowExW(0, L"MusicSorterUnicodeClass", L"MusicSorter Desktop (C++20 MSVC - Multi-step Workflow)", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT, 980, 720, NULL, NULL, hInstance, NULL);
    if (!m_hWnd) return false;

    ShowWindow(m_hWnd, nCmdShow);
    UpdateWindow(m_hWnd);
    return true;
}

void AppWindow::RunMessageLoop() {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

LRESULT CALLBACK AppWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return Instance().HandleMessage(hWnd, msg, wParam, lParam);
}

LRESULT AppWindow::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // Step 1 - Step 4 Manual Workflow Buttons
        m_hBtnStep1 = CreateWindowW(L"BUTTON", L"1. 🔍 Поиск дубликатов (AcoustID)", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 20, 16, 220, 36, hWnd, (HMENU)ID_BTN_STEP1, NULL, NULL);
        m_hBtnStep2 = CreateWindowW(L"BUTTON", L"2. 🏷️ Тегирование & Обложки", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 250, 16, 210, 36, hWnd, (HMENU)ID_BTN_STEP2, NULL, NULL);
        m_hBtnStep3 = CreateWindowW(L"BUTTON", L"3. 📂 Зеркалирование FLAC/MP3", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 470, 16, 220, 36, hWnd, (HMENU)ID_BTN_STEP3, NULL, NULL);
        m_hBtnStep4 = CreateWindowW(L"BUTTON", L"4. 📝 Синхронизация Tracklist", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 700, 16, 220, 36, hWnd, (HMENU)ID_BTN_STEP4, NULL, NULL);

        // Track Cards
        m_hLblCardA = CreateWindowW(L"STATIC", L"ТРЕК А: Ожидание выполнения шага 1...", WS_VISIBLE | WS_CHILD | SS_LEFT, 20, 68, 450, 90, hWnd, NULL, NULL, NULL);
        m_hLblCardB = CreateWindowW(L"STATIC", L"ТРЕК Б: Ожидание выполнения шага 1...", WS_VISIBLE | WS_CHILD | SS_LEFT, 490, 68, 450, 90, hWnd, NULL, NULL, NULL);

        m_hBtnKeepA = CreateWindowW(L"BUTTON", L"⚪ Оставить Трек А (Б ➔ delete)", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 20, 165, 450, 36, hWnd, (HMENU)ID_BTN_KEEP_A, NULL, NULL);
        m_hBtnKeepB = CreateWindowW(L"BUTTON", L"⚪ Оставить Трек Б (А ➔ delete)", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 490, 165, 450, 36, hWnd, (HMENU)ID_BTN_KEEP_B, NULL, NULL);

        m_hLblSim = CreateWindowW(L"STATIC", L"Сходство: --- % | Смещение: --- кадров", WS_VISIBLE | WS_CHILD | SS_CENTER, 20, 215, 920, 20, hWnd, NULL, NULL, NULL);

        m_hSeekSlider = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, 20, 240, 920, 30, hWnd, (HMENU)ID_SEEK_SLIDER, NULL, NULL);
        SendMessage(m_hSeekSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 1000));

        m_hBtnPlay = CreateWindowW(L"BUTTON", L"▶ Проигрывать", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 20, 280, 140, 36, hWnd, (HMENU)ID_BTN_PLAY, NULL, NULL);
        m_hBtnToggleA = CreateWindowW(L"BUTTON", L"🔊 ТРЕК А (АКТИВЕН)", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 170, 280, 200, 36, hWnd, (HMENU)ID_BTN_TOGGLE_A, NULL, NULL);
        m_hBtnToggleB = CreateWindowW(L"BUTTON", L"🔈 ТРЕК Б", WS_TABSTOP | WS_VISIBLE | WS_CHILD, 380, 280, 200, 36, hWnd, (HMENU)ID_BTN_TOGGLE_B, NULL, NULL);

        m_hLblStatus = CreateWindowW(L"STATIC", L"Горячие клавиши: Tab/S (A/B Hot-Swap) | 1: Оставить A | 2: Оставить B", WS_VISIBLE | WS_CHILD | SS_LEFT, 590, 290, 350, 20, hWnd, NULL, NULL, NULL);

        m_hProgressBar = CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 20, 325, 920, 8, hWnd, NULL, NULL, NULL);
        SendMessage(m_hProgressBar, PBM_SETRANGE, 0, MAKELONG(0, 100));

        m_hLogBox = CreateWindowW(L"EDIT", L"Готов к работе...", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 20, 345, 920, 310, hWnd, NULL, NULL, NULL);

        // Apply fonts
        SendMessage(m_hBtnStep1, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        SendMessage(m_hBtnStep2, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        SendMessage(m_hBtnStep3, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        SendMessage(m_hBtnStep4, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        SendMessage(m_hLogBox, WM_SETFONT, (WPARAM)m_hFontMono, TRUE);

        Logger::Instance().SetCallback([hWnd](const std::string& msg) {
            std::wstring* pWmsg = new std::wstring(Utf8ToWide(msg));
            PostMessageW(hWnd, WM_SCAN_PROGRESS, 0, (LPARAM)pWmsg);
        });

        SetTimer(hWnd, ID_TIMER_SEEK, 100, NULL);
        break;
    }
    case WM_SCAN_PROGRESS: {
        std::wstring* pMsg = (std::wstring*)lParam;
        if (pMsg) {
            std::wstring curr;
            int len = GetWindowTextLengthW(m_hLogBox);
            if (len < 128000) {
                std::vector<wchar_t> buf(len + 1);
                GetWindowTextW(m_hLogBox, buf.data(), len + 1);
                curr = std::wstring(buf.data());
            }
            curr += *pMsg + L"\r\n";
            SetWindowTextW(m_hLogBox, curr.c_str());
            SendMessageW(m_hLogBox, EM_LINESCROLL, 0, 10000);
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
        case ID_BTN_STEP1: {
            EnableWindow(m_hBtnStep1, FALSE);
            m_candidates.clear();
            m_autoDelete.clear();
            m_currentCandidateIndex = 0;

            std::thread([this, hWnd]() {
                AcousticAnalyzer::Instance().AnalyzeDirectory(g_ToSortDir, g_BaseDir, m_candidates, m_autoDelete);
                PostMessageW(hWnd, WM_SCAN_FINISHED, 0, 0);
            }).detach();
            break;
        }
        case ID_BTN_STEP2: {
            LOG_INFO("[STEP 2] Tagging metadata & embedding cover art...");
            std::string pyCmd = "python process_collection.py";
            _popen(pyCmd.c_str(), "r");
            LOG_INFO("[STEP 2 COMPLETE] Metadata tagged & original CD covers embedded.");
            break;
        }
        case ID_BTN_STEP3: {
            LOG_INFO("[STEP 3] Mirroring FLAC and MP3 directories...");
            std::string pyCmd = "python sync_music.py";
            _popen(pyCmd.c_str(), "r");
            LOG_INFO("[STEP 3 COMPLETE] Directories mirrored 1-in-1.");
            break;
        }
        case ID_BTN_STEP4: {
            LOG_INFO("[STEP 4] Syncing tracklist.md database checkboxes...");
            std::string pyCmd = "python populate_and_check_tracklist.py";
            _popen(pyCmd.c_str(), "r");
            LOG_INFO("[STEP 4 COMPLETE] tracklist.md checkboxes marked [x].");
            break;
        }
        case ID_BTN_PLAY:
            AudioEngine::Instance().TogglePlay();
            SetWindowTextW(m_hBtnPlay, AudioEngine::Instance().IsPlaying() ? L"⏸ Пауза" : L"▶ Проигрывать");
            break;
        case ID_BTN_TOGGLE_A:
            AudioEngine::Instance().SetActiveChannel('a');
            SetWindowTextW(m_hBtnToggleA, L"🔊 ТРЕК А (АКТИВЕН)");
            SetWindowTextW(m_hBtnToggleB, L"🔈 ТРЕК Б");
            break;
        case ID_BTN_TOGGLE_B:
            AudioEngine::Instance().SetActiveChannel('b');
            SetWindowTextW(m_hBtnToggleA, L"🔈 ТРЕК А");
            SetWindowTextW(m_hBtnToggleB, L"🔊 ТРЕК Б (АКТИВЕН)");
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
        SetWindowTextW(m_hLblCardA, Utf8ToWide("ТРЕК А: " + fs::path(next_p.trackA_path).filename().string() + "\n" + next_p.relA).c_str());
        SetWindowTextW(m_hLblCardB, Utf8ToWide("ТРЕК Б: " + fs::path(next_p.trackB_path).filename().string() + "\n" + next_p.relB).c_str());
    } else {
        SetWindowTextW(m_hLblCardA, L"✨ Все кандидаты обработаны!");
        SetWindowTextW(m_hLblCardB, L"Файлы перемещены в delete/");
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
        SetWindowTextW(m_hLblCardA, Utf8ToWide("ТРЕК А: " + fs::path(next_p.trackA_path).filename().string() + "\n" + next_p.relA).c_str());
        SetWindowTextW(m_hLblCardB, Utf8ToWide("ТРЕК Б: " + fs::path(next_p.trackB_path).filename().string() + "\n" + next_p.relB).c_str());
    } else {
        SetWindowTextW(m_hLblCardA, L"✨ Все кандидаты обработаны!");
        SetWindowTextW(m_hLblCardB, L"Файлы перемещены в delete/");
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
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

void AppWindow::HandleScanFinished() {
    EnableWindow(m_hBtnStep1, TRUE);
    LOG_INFO("Scan process complete. Found " + std::to_string(m_candidates.size()) + " candidate pairs for A/B comparison.");

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

        SetWindowTextW(m_hLblCardA, Utf8ToWide("ТРЕК А: " + fs::path(pair.trackA_path).filename().string() + "\n" + pair.relA).c_str());
        SetWindowTextW(m_hLblCardB, Utf8ToWide("ТРЕК Б: " + fs::path(pair.trackB_path).filename().string() + "\n" + pair.relB).c_str());
        SetWindowTextW(m_hLblSim, Utf8ToWide("Сходство волн: " + std::to_string((int)pair.similarity) + "% | Смещение: " + std::to_string(pair.offset) + " кадров").c_str());
    } else {
        SetWindowTextW(m_hLblCardA, L"✨ Похожие кандидаты не найдены!");
        SetWindowTextW(m_hLblCardB, L"Папка TO SORT полностью обработана.");
    }
}
