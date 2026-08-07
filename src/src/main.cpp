#include "../include/AppWindow.hpp"
#include "../include/AudioEngine.hpp"
#include "../include/AcousticAnalyzer.hpp"
#include "../include/Logger.hpp"

#include <windows.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

std::string g_BaseDir;
std::string g_ToSortDir;
std::string g_DeleteDir;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);

    fs::path p(exe_path);
    g_BaseDir = p.parent_path().string();

    std::string fpcalcBin = (p.parent_path() / "fpcalc.exe").string();
    if (!fs::exists(fpcalcBin)) {
        fpcalcBin = (p.parent_path().parent_path() / "fpcalc.exe").string();
        if (fs::exists(fpcalcBin)) g_BaseDir = p.parent_path().parent_path().string();
    }

    g_ToSortDir = (fs::path(g_BaseDir) / "TO SORT").string();
    g_DeleteDir = (fs::path(g_BaseDir) / "delete").string();

    LOG_INFO("=== Starting MusicSorter Native ImGui C++ Studio ===");
    LOG_INFO("Base Directory: " + g_BaseDir);
    LOG_INFO("TO SORT Directory: " + g_ToSortDir);
    LOG_INFO("fpcalc Binary: " + fpcalcBin);

    AcousticAnalyzer::Instance().SetFpcalcPath(fpcalcBin);

    if (!AudioEngine::Instance().Initialize()) {
        MessageBoxA(NULL, "Failed to initialize Miniaudio engine!", "MusicSorter Error", MB_ICONERROR);
        return 1;
    }

    if (!AppWindow::Instance().Initialize(hInstance, nCmdShow)) {
        MessageBoxA(NULL, "Failed to initialize ImGui DirectX 11 AppWindow!", "MusicSorter Error", MB_ICONERROR);
        return 1;
    }

    AppWindow::Instance().RunMessageLoop();

    AppWindow::Instance().Cleanup();
    AudioEngine::Instance().Shutdown();
    LOG_INFO("=== MusicSorter Native C++ Application Exited Cleanly ===");
    return 0;
}
