#include "../include/HttpServer.hpp"
#include "../include/CoreEngine.hpp"
#include "../include/AudioEngine.hpp"
#include "../include/Logger.hpp"

#include <windows.h>
#include <filesystem>
#include <iostream>
#include <csignal>
#include <atomic>

namespace fs = std::filesystem;

static std::atomic<bool> g_keepRunning{true};
static HttpServer* g_httpServer = nullptr;

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            LOG_INFO("[SYSTEM] Received termination signal, shutting down gracefully...");
            g_keepRunning = false;
            if (g_httpServer) {
                g_httpServer->Stop();
            }
            return TRUE;
        default:
            return FALSE;
    }
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    fs::path p(exePath);
    std::string baseDir = p.parent_path().string();

    int port = 8765;
    DWORD parentPid = 0;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            try { port = std::stoi(argv[++i]); } catch (...) {}
        } else if (arg == "--dir" && i + 1 < argc) {
            baseDir = argv[++i];
        } else if (arg == "--parent-pid" && i + 1 < argc) {
            try { parentPid = (DWORD)std::stoul(argv[++i]); } catch (...) {}
        }
    }

    std::string fpcalcBin = (fs::path(baseDir) / "fpcalc.exe").string();
    if (!fs::exists(fpcalcBin)) {
        fpcalcBin = (fs::path(baseDir).parent_path() / "fpcalc.exe").string();
        if (fs::exists(fpcalcBin)) {
            baseDir = fs::path(baseDir).parent_path().string();
        }
    }

    LOG_INFO("=== Starting MusicSorter Headless C++ Core ===");
    LOG_INFO("Base Directory: " + baseDir);
    LOG_INFO("HTTP REST Port: " + std::to_string(port));
    if (parentPid > 0) {
        LOG_INFO("Parent Watchdog attached to PID: " + std::to_string(parentPid));
    }

    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    if (!AudioEngine::Instance().Initialize()) {
        LOG_WARN("[WARN] Failed to initialize Miniaudio engine (running in audio-disabled mode)");
    }

    CoreEngine::Instance().Initialize(baseDir);

    HttpServer server(port);
    g_httpServer = &server;

    if (!server.Start()) {
        LOG_ERROR("[ERROR] Failed to start HTTP server on port " + std::to_string(port));
        AudioEngine::Instance().Shutdown();
        CoreEngine::Instance().Shutdown();
        return 1;
    }

    LOG_INFO("[READY] MusicSorter Core Engine ready for Tauri frontend.");

    if (parentPid > 0) {
        std::thread([parentPid, &server]() {
            HANDLE hParent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
            if (hParent != NULL) {
                WaitForSingleObject(hParent, INFINITE);
                CloseHandle(hParent);
                LOG_INFO("[SYSTEM] Parent process exited. Initiating headless core shutdown...");
                g_keepRunning = false;
                server.Stop();
            }
        }).detach();
    }

    while (g_keepRunning && server.IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server.Stop();
    AudioEngine::Instance().Shutdown();
    CoreEngine::Instance().Shutdown();

    LOG_INFO("=== MusicSorter Headless Core Exited Cleanly ===");
    return 0;
}
