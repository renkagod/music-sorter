#pragma once
#include <windows.h>
#include <d3d11.h>
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
    void Cleanup();

    HWND GetHWND() const { return m_hWnd; }
    void HandleScanFinished();

private:
    AppWindow() = default;
    ~AppWindow() = default;

    bool CreateDeviceD3D(HWND hWnd);
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_hInstance = NULL;
    HWND m_hWnd = NULL;

    // DirectX 11 Data
    ID3D11Device* m_pd3dDevice = NULL;
    ID3D11DeviceContext* m_pd3dDeviceContext = NULL;
    IDXGISwapChain* m_pSwapChain = NULL;
    ID3D11RenderTargetView* m_mainRenderTargetView = NULL;

    std::vector<ABCandidatePair> m_candidates;
    std::vector<std::string> m_autoDelete;
    size_t m_currentCandidateIndex = 0;
    bool m_isScanning = false;
};
