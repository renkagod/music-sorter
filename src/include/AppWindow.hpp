#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include "AcousticAnalyzer.hpp"

#define WM_SCAN_PROGRESS (WM_USER + 101)
#define WM_SCAN_FINISHED (WM_USER + 102)

class AppWindow {
public:
    static AppWindow& Instance() {
        static AppWindow instance;
        return instance;
    }

    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    void RunMessageLoop();

    HWND GetHWND() const { return m_hWnd; }

    void HandleScanProgress(int progress, const std::string& msg);
    void HandleScanFinished();

private:
    AppWindow() = default;
    ~AppWindow() = default;

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_hInstance = NULL;
    HWND m_hWnd = NULL;

    HWND m_hBtnScan = NULL;
    HWND m_hBtnPlay = NULL;
    HWND m_hBtnToggleA = NULL;
    HWND m_hBtnToggleB = NULL;
    HWND m_hBtnKeepA = NULL;
    HWND m_hBtnKeepB = NULL;
    HWND m_hSeekSlider = NULL;
    HWND m_hProgressBar = NULL;
    HWND m_hLblStatus = NULL;
    HWND g_hLblCardA = NULL;
    HWND g_hLblCardB = NULL;
    HWND g_hLblSim = NULL;
    HWND m_hLogBox = NULL;

    std::vector<ABCandidatePair> m_candidates;
    std::vector<std::string> m_autoDelete;
    size_t m_currentCandidateIndex = 0;
};
