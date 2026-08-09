#pragma once
#include <windows.h>
#include <d3d11.h>
#include <string>
#include <vector>
#include <atomic>
#include "AcousticAnalyzer.hpp"

#define WM_SCAN_PROGRESS     (WM_USER + 101)
#define WM_SCAN_FINISHED     (WM_USER + 102)
#define WM_TAG_SCAN_FINISHED (WM_USER + 103)

struct TagReviewItem {
    std::string filePath;
    std::string relPath;
    std::string originalFilename;

    std::string embeddedArtist;
    std::string embeddedAlbum;
    std::string embeddedTitle;
    std::string embeddedTrackNo;

    char artistBuf[256] = {0};
    char albumBuf[256] = {0};
    char titleBuf[256] = {0};
    char trackNoBuf[32] = {0};

    bool isMusicBrainzMatched = false;
    bool isFetchCompleted = false;

    std::string localCoverPath;
    std::string onlineCoverUrl;
    std::vector<unsigned char> localCoverBytes;
    std::vector<unsigned char> onlineCoverBytes;

    ID3D11ShaderResourceView* localTexture = NULL;
    ID3D11ShaderResourceView* onlineTexture = NULL;
    int localWidth = 0, localHeight = 0;
    int onlineWidth = 0, onlineHeight = 0;

    long long localScore = 0;
    long long onlineScore = 0;

    int selectedCoverChoice = 0; // 0 = Local, 1 = Online
};

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
    void HandleTagScanFinished();

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

    // Step 2 Tag & Cover Inspection
    std::vector<TagReviewItem> m_tagItems;
    size_t m_currentTagIndex = 0;
    bool m_isTagScanning = false;
    std::atomic<size_t> m_fetchedCount{0};

    // Console Log Auto-Scroll State
    bool m_logAutoScroll = true;
};
