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
#define WM_BROWSE_RESULT     (WM_USER + 104)

enum class MatchTier {
    AcoustId,
    TierA,
    TierB_Verified,
    TierB_Fallback,
    TierB_Katakana,
    TierC_Loose,
    Niche_Local
};

struct TagReviewItem {
    std::string filePath;
    std::string relPath;
    std::string originalFilename;

    std::string embeddedArtist;
    std::string embeddedAlbum;
    std::string embeddedTitle;
    std::string embeddedTrackNo;
    std::string embeddedYear;

    char artistBuf[256] = {0};
    char albumBuf[256] = {0};
    char titleBuf[256] = {0};
    char trackNoBuf[32] = {0};
    char yearBuf[32] = {0};
    char lyricsBuf[2048] = {0};

    bool isMusicBrainzMatched = false;
    bool isFetchCompleted = false;
    bool hasLyrics = false;
    double duration = 0.0;
    MatchTier matchTier = MatchTier::Niche_Local;
    std::string releaseGroupMbId;

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
    static LRESULT CALLBACK SummaryWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void OpenSummaryWindow();
    void CloseSummaryWindow();
    void CreateSummaryRenderTarget();
    void CleanupSummaryRenderTarget();
    void ResizeSummaryRenderTarget(UINT width, UINT height);
    void RenderReleaseSummaryTable();

    HINSTANCE m_hInstance = NULL;
    HWND m_hWnd = NULL;
    HWND m_hSummaryWnd = NULL;

    // ImGui Contexts
    struct ImGuiContext* m_mainImGuiContext = NULL;
    struct ImGuiContext* m_summaryImGuiContext = NULL;

    // DirectX 11 Data
    ID3D11Device* m_pd3dDevice = NULL;
    ID3D11DeviceContext* m_pd3dDeviceContext = NULL;
    IDXGISwapChain* m_pSwapChain = NULL;
    ID3D11RenderTargetView* m_mainRenderTargetView = NULL;

    // Secondary Window DirectX 11 Data
    IDXGISwapChain* m_pSummarySwapChain = NULL;
    ID3D11RenderTargetView* m_summaryRenderTargetView = NULL;

    std::vector<ABCandidatePair> m_candidates;
    std::vector<std::string> m_autoDelete;
    size_t m_currentCandidateIndex = 0;
    bool m_isScanning = false;

    // Step 2 Tag & Cover Inspection
    std::vector<TagReviewItem> m_tagItems;
    size_t m_currentTagIndex = 0;
    bool m_isTagScanning = false;
    std::atomic<size_t> m_fetchedCount{0};

    // Active Stage Tab (0 = Step 1 Duplicates, 1 = Step 2 Inspector, 2 = Step 3 Mirror, 3 = Step 4 Tracklist)
    int m_activeStageTab = 0;

    // Release Summary Filter State
    int m_releaseSummaryTierFilter = 0;

    // Console Log Auto-Scroll State
    bool m_logAutoScroll = true;

    // Folder Settings Buffers
    char m_toSortBuf[512] = {0};
    char m_outputBuf[512] = {0};
    char m_flacBuf[512] = {0};
    char m_mp3Buf[512] = {0};
    char m_acoustIdKeyBuf[64] = {0};
    bool m_foldersInited = false;

    // Manual MusicBrainz Release URL / MBID Input
    char m_manualMbUrlBuf[512] = {0};
    bool m_manualMbApplyToAlbum = true;
    void FetchManualMusicBrainzMetadata(const std::string& inputUrl, bool applyToAllInAlbum);
};
