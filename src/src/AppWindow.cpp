#include "../include/AppWindow.hpp"
#include "../include/AudioEngine.hpp"
#include "../include/Logger.hpp"

#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/imgui_impl_win32.h"
#include "../third_party/imgui/imgui_impl_dx11.h"

#include <shellapi.h>
#include <thread>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

extern std::string g_BaseDir;
extern std::string g_ToSortDir;
extern std::string g_DeleteDir;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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

    m_hWnd = CreateWindowW(wcex.lpszClassName, L"MusicSorter Desktop - Modern Monochrome C++ Studio", WS_OVERLAPPEDWINDOW, 100, 100, 1080, 760, NULL, NULL, hInstance, NULL);

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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // 1. Primary Font: Segoe UI for crisp English, Russian, and Latin
    ImFontConfig font_cfg_primary;
    font_cfg_primary.FontDataOwnedByAtlas = false;
    static const ImWchar ranges_latin_cyrillic[] = {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x0400, 0x052F, // Cyrillic + Cyrillic Supplement
        0x2000, 0x206F, // General Punctuation
        0,
    };

    if (fs::exists("C:\\Windows\\Fonts\\segoeui.ttf")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f, &font_cfg_primary, ranges_latin_cyrillic);
    } else if (fs::exists("C:\\Windows\\Fonts\\arial.ttf")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 16.0f, &font_cfg_primary, ranges_latin_cyrillic);
    }

    // 2. Merged CJK Font: MS Gothic / YuGothM for Japanese Hiragana, Katakana, and Kanji
    ImFontConfig font_cfg_cjk;
    font_cfg_cjk.FontDataOwnedByAtlas = false;
    font_cfg_cjk.MergeMode = true; // Merge Japanese glyphs seamlessly into Segoe UI primary font!

    static const ImWchar ranges_cjk[] = {
        0x3000, 0x30FF, // CJK Symbols and Punctuation + Hiragana + Katakana
        0x31F0, 0x31FF, // Katakana Phonetic Extensions
        0x4E00, 0x9FAF, // CJK Unified Ideographs
        0xFF00, 0xFFEF, // Halfwidth and Fullwidth Forms
        0,
    };

    if (fs::exists("C:\\Windows\\Fonts\\msgothic.ttc")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msgothic.ttc", 16.0f, &font_cfg_cjk, ranges_cjk);
    } else if (fs::exists("C:\\Windows\\Fonts\\YuGothM.ttc")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\YuGothM.ttc", 16.0f, &font_cfg_cjk, ranges_cjk);
    }

    // Apply Sleek Dark Monochrome Styling
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ItemSpacing = ImVec2(10, 8);
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
        ImGui::TextDisabled("MUSIC SORTER DESKTOP (C++20 NATIVE STUDIO)");
        ImGui::Separator();
        ImGui::Spacing();

        // 4 Step-by-Step Workflow Stage Buttons
        if (ImGui::Button("1. [Поиск] Дубликаты (AcoustID)", ImVec2(240, 36))) {
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
        ImGui::SameLine();
        if (ImGui::Button("2. [Теги] Метаданные & Обложки", ImVec2(230, 36))) {
            LOG_INFO("Step 2: Tagging canonical metadata & embedding cover art...");
            std::thread([]() {
                _popen("python process_collection.py", "r");
                LOG_INFO("Step 2 Complete: Metadata tagged & covers embedded.");
            }).detach();
        }
        ImGui::SameLine();
        if (ImGui::Button("3. [Сортировка] Папки FLAC/MP3", ImVec2(240, 36))) {
            LOG_INFO("Step 3: Mirroring FLAC and MP3 collections...");
            std::thread([]() {
                _popen("python sync_music.py", "r");
                LOG_INFO("Step 3 Complete: Collections 100% mirrored.");
            }).detach();
        }
        ImGui::SameLine();
        if (ImGui::Button("4. [Реестр] Обновление Tracklist", ImVec2(240, 36))) {
            LOG_INFO("Step 4: Syncing tracklist.md checkboxes...");
            std::thread([]() {
                _popen("python populate_and_check_tracklist.py", "r");
                LOG_INFO("Step 4 Complete: tracklist.md updated.");
            }).detach();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Dual A/B Comparison Cards Layout
        float halfWidth = (ImGui::GetContentRegionAvail().x - 16.0f) * 0.5f;

        // Card A (Left)
        ImGui::BeginChild("CardA", ImVec2(halfWidth, 180), true);
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
        ImGui::BeginChild("CardB", ImVec2(halfWidth, 180), true);
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

        ImGui::Spacing();

        // Audio Player Controls & Wave Similarity Section
        ImGui::BeginChild("PlayerControls", ImVec2(0, 140), true);
        if (!m_candidates.empty() && m_currentCandidateIndex < m_candidates.size()) {
            auto& pair = m_candidates[m_currentCandidateIndex];
            ImGui::Text("Сходство волн: %.1f%% | Смещение фазы: %d кадров", pair.similarity, pair.offset);
        } else {
            ImGui::TextDisabled("Сходство волн: --- % | Смещение фазы: --- кадров");
        }

        double cur = AudioEngine::Instance().GetCurrentPositionSeconds();
        double dur = AudioEngine::Instance().GetDurationSeconds();
        float seek_val = (dur > 0.0) ? (float)(cur / dur) : 0.0f;

        if (ImGui::SliderFloat("##SeekSlider", &seek_val, 0.0f, 1.0f, "Position: %.1f sec")) {
            AudioEngine::Instance().SeekToPercentage((double)seek_val * 100.0);
        }

        if (ImGui::Button(AudioEngine::Instance().IsPlaying() ? "[||] ПАУЗА" : "[>] ПРОИГРЫВАТЬ", ImVec2(140, 36))) {
            AudioEngine::Instance().TogglePlay();
        }
        ImGui::SameLine();

        char ch = AudioEngine::Instance().GetActiveChannel();
        if (ch == 'a') {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.00f, 0.00f, 1.00f));
        }
        if (ImGui::Button("ТРЕК А [1]", ImVec2(140, 36))) {
            AudioEngine::Instance().SetActiveChannel('a');
        }
        if (ch == 'a') ImGui::PopStyleColor(2);

        ImGui::SameLine();
        if (ch == 'b') {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.00f, 0.00f, 1.00f));
        }
        if (ImGui::Button("ТРЕК Б [2]", ImVec2(140, 36))) {
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

        ImGui::Spacing();

        // Log Console Panel
        ImGui::BeginChild("LogConsole", ImVec2(0, 0), true);
        ImGui::TextDisabled("ПОШАГОВЫЙ КОНСОЛЬНЫЙ ЖУРНАЛ СОБЫТИЙ:");
        ImGui::SameLine();
        if (ImGui::Button("Копировать весь лог в буфер обмена")) {
            auto logs = Logger::Instance().GetLogs();
            std::string full_log;
            for (const auto& log : logs) full_log += log + "\n";
            ImGui::SetClipboardText(full_log.c_str());
            LOG_INFO("Logs copied to clipboard.");
        }
        ImGui::Separator();

        auto logs = Logger::Instance().GetLogs();
        static std::string log_buffer;
        log_buffer.clear();
        for (const auto& log : logs) {
            log_buffer += log + "\n";
        }

        ImGui::InputTextMultiline("##LogConsoleMultiLine", log_buffer.data(), log_buffer.size() + 1, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndChild();

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
