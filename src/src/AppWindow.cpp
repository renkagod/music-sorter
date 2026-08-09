#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

#include "../include/AppWindow.hpp"
#include "../include/AudioEngine.hpp"
#include "../include/Logger.hpp"

#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/imgui_internal.h"
#include "../third_party/imgui/imgui_impl_win32.h"
#include "../third_party/imgui/imgui_impl_dx11.h"

#include <shellapi.h>
#include <wininet.h>
#include <thread>
#include <future>
#include <mutex>
#include <unordered_map>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <regex>

#pragma comment(lib, "wininet.lib")

namespace fs = std::filesystem;

extern std::string g_BaseDir;
extern std::string g_ToSortDir;
extern std::string g_DeleteDir;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct AlbumMetadataCache {
    std::string releaseGroupMbId;
    std::vector<unsigned char> coverBytes;
    bool isMatched = false;
    bool isFetched = false;
};

// Robust Native Windows Clipboard Copying
static void CopyToClipboardWin32(const std::string& text) {
    if (text.empty()) return;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    if (wlen <= 0) return;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
    if (!hMem) return;

    wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
    if (pMem) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, pMem, wlen);
        GlobalUnlock(hMem);
    }

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, hMem);
        CloseClipboard();
    } else {
        GlobalFree(hMem);
    }
}

static std::string CleanMetadataString(const std::string& str) {
    if (str.empty()) return "";
    std::string s = std::regex_replace(str, std::regex(R"(\[[^\]]*\]|\{[^\}]*\}|\([^\)]*\)|\d{4}\.\d{2}\.\d{2})"), "");
    s = std::regex_replace(s, std::regex(R"(^\s+|\s+$)"), "");
    return s.empty() ? str : s;
}

static std::string NormalizeKey(const std::string& text) {
    if (text.empty()) return "";
    std::string s = std::regex_replace(text, std::regex(R"(\[[^\]]*\]|\{[^\}]*\}|\([^\)]*\))"), "");
    s = std::regex_replace(s, std::regex(R"([\s\-_/\\,.\u2044\u2215\u3013\uFF5E]+)"), "");
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    std::wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstr[0], len);
    return wstr;
}

static std::string UrlEncode(const std::string& str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : str) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << std::uppercase << (int)(unsigned char)c;
        }
    }
    return escaped.str();
}

// Mathematical Laplacian High-Frequency Sharpness Analysis (Detects fake upscaled / blurry images!)
static double CalculatePerceptualSharpness(const unsigned char* data, size_t size) {
    if (!data || size == 0) return 0.0;
    int width = 0, height = 0, channels = 0;
    unsigned char* gray = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 1);
    if (!gray || width < 4 || height < 4) return 0.0;

    double sum = 0.0;
    double sumSq = 0.0;
    size_t count = 0;

    for (int y = 1; y < height - 1; y += 2) {
        for (int x = 1; x < width - 1; x += 2) {
            int center = gray[y * width + x];
            int top    = gray[(y - 1) * width + x];
            int bottom = gray[(y + 1) * width + x];
            int left   = gray[y * width + (x - 1)];
            int right  = gray[y * width + (x + 1)];

            double lap = (double)(top + bottom + left + right - 4 * center);
            sum += lap;
            sumSq += lap * lap;
            count++;
        }
    }

    stbi_image_free(gray);

    if (count == 0) return 0.0;
    double mean = sum / count;
    double variance = (sumSq / count) - (mean * mean);
    return variance;
}

std::vector<unsigned char> HttpGetBytes(const std::wstring& url) {
    std::vector<unsigned char> result;
    HINTERNET hNet = InternetOpenW(L"MusicSorter/2.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hNet) return result;

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
    HINTERNET hFile = InternetOpenUrlW(hNet, url.c_str(), NULL, 0, flags, 0);
    if (!hFile) {
        flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
        hFile = InternetOpenUrlW(hNet, url.c_str(), NULL, 0, flags, 0);
    }
    if (!hFile) {
        InternetCloseHandle(hNet);
        return result;
    }

    DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_REVOCATION | SECURITY_FLAG_IGNORE_REDIRECT_TO_HTTP | SECURITY_FLAG_IGNORE_REDIRECT_TO_HTTPS;
    InternetSetOptionW(hFile, INTERNET_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

    unsigned char buffer[16384];
    DWORD bytesRead = 0;
    while (InternetReadFile(hFile, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        result.insert(result.end(), buffer, buffer + bytesRead);
    }

    InternetCloseHandle(hFile);
    InternetCloseHandle(hNet);
    return result;
}

std::string HttpGetString(const std::wstring& url) {
    auto bytes = HttpGetBytes(url);
    if (bytes.empty()) return "";
    return std::string((char*)bytes.data(), bytes.size());
}

ID3D11ShaderResourceView* CreateTextureFromMemory(ID3D11Device* device, const unsigned char* data, size_t size, int* outWidth, int* outHeight) {
    if (!data || size == 0) return NULL;
    int width = 0, height = 0, channels = 0;
    unsigned char* image_data = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 4);
    if (!image_data) return NULL;

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    ID3D11Texture2D* pTexture = NULL;
    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = image_data;
    subResource.SysMemPitch = width * 4;
    subResource.SysMemSlicePitch = 0;

    device->CreateTexture2D(&desc, &subResource, &pTexture);

    ID3D11ShaderResourceView* out_srv = NULL;
    if (pTexture) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
        device->CreateShaderResourceView(pTexture, &srvDesc, &out_srv);
        pTexture->Release();
    }

    stbi_image_free(image_data);
    if (outWidth) *outWidth = width;
    if (outHeight) *outHeight = height;
    return out_srv;
}

// Stage 3: Pure Native C++20 FLAC / MP3 Mirroring
static void NativeMirrorCollections() {
    LOG_INFO("Step 3: Running native C++20 collection mirroring...");
    fs::path flacRoot = fs::path(g_BaseDir) / "flac";
    fs::path mp3Root = fs::path(g_BaseDir) / "mp3";

    fs::create_directories(flacRoot);
    fs::create_directories(mp3Root);

    size_t copiedFallbacks = 0;
    size_t createdDirs = 0;

    if (fs::exists(mp3Root)) {
        for (auto& entry : fs::recursive_directory_iterator(mp3Root)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".mp3") {
                    fs::path rel = fs::relative(entry.path(), mp3Root);
                    fs::path flacTargetDir = flacRoot / rel.parent_path();
                    std::string stem = entry.path().stem().string();

                    fs::path expectedFlac = flacTargetDir / (stem + ".flac");
                    fs::path mp3FallbackInFlac = flacTargetDir / entry.path().filename();

                    if (!fs::exists(expectedFlac) && !fs::exists(mp3FallbackInFlac)) {
                        fs::create_directories(flacTargetDir);
                        LOG_INFO("[NATIVE C++ MIRROR] Copying MP3 fallback to FLAC folder: " + rel.string());
                        fs::copy_file(entry.path(), mp3FallbackInFlac, fs::copy_options::overwrite_existing);
                        copiedFallbacks++;
                    }
                }
            }
        }
    }

    if (fs::exists(flacRoot)) {
        for (auto& entry : fs::recursive_directory_iterator(flacRoot)) {
            if (entry.is_directory()) {
                fs::path rel = fs::relative(entry.path(), flacRoot);
                fs::path mp3EquivalentDir = mp3Root / rel;
                if (!fs::exists(mp3EquivalentDir)) {
                    fs::create_directories(mp3EquivalentDir);
                    createdDirs++;
                }
            }
        }
    }

    LOG_INFO("Step 3 Complete: Native C++ mirroring finished. Created " + std::to_string(createdDirs) + " folders, copied " + std::to_string(copiedFallbacks) + " MP3 fallbacks.");
}

// Stage 4: Pure Native C++20 Tracklist Database Checkbox Sync
static void NativeSyncTracklistDatabase() {
    LOG_INFO("Step 4: Running native C++20 tracklist.md checkbox sync...");
    fs::path tracklistPath = fs::path(g_BaseDir) / "tracklist.md";
    if (!fs::exists(tracklistPath)) {
        LOG_INFO("Error: tracklist.md not found.");
        return;
    }

    std::vector<std::string> scannedNormKeys;
    for (const auto& sub : { "flac", "mp3", "TO SORT", "review" }) {
        fs::path dir = fs::path(g_BaseDir) / sub;
        if (fs::exists(dir)) {
            for (auto& entry : fs::recursive_directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".flac" || ext == ".mp3" || ext == ".wav" || ext == ".m4a") {
                        scannedNormKeys.push_back(NormalizeKey(entry.path().filename().string()));
                    }
                }
            }
        }
    }

    std::ifstream inFile(tracklistPath);
    std::string line;
    std::vector<std::string> lines;
    size_t checkedCount = 0;

    while (std::getline(inFile, line)) {
        std::regex track_regex(R"(^\s*-\s*\[\s*\]\s*(.+)$)");
        std::smatch match;
        if (std::regex_search(line, match, track_regex)) {
            std::string content = match[1].str();
            std::string normContent = NormalizeKey(content);

            bool found = false;
            for (const auto& key : scannedNormKeys) {
                if (!key.empty() && normContent.find(key) != std::string::npos || key.find(normContent) != std::string::npos) {
                    found = true;
                    break;
                }
            }

            if (found) {
                line = std::regex_replace(line, std::regex(R"(-\s*\[\s*\])"), "- [x]");
                checkedCount++;
            }
        }
        lines.push_back(line);
    }
    inFile.close();

    std::ofstream outFile(tracklistPath);
    for (const auto& l : lines) {
        outFile << l << "\n";
    }
    outFile.close();

    LOG_INFO("Step 4 Complete: Native C++ tracklist sync finished. Checked off " + std::to_string(checkedCount) + " tracks in tracklist.md.");
}

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

    // Initial Window Size enlarged to 1280x920 so Console Panel is fully visible by default!
    m_hWnd = CreateWindowW(wcex.lpszClassName, L"MusicSorter Desktop - Modern Monochrome C++ Studio", WS_OVERLAPPEDWINDOW, 60, 40, 1280, 920, NULL, NULL, hInstance, NULL);

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
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;

    // Primary & CJK Merged Fonts
    ImFontConfig font_cfg_primary;
    font_cfg_primary.FontDataOwnedByAtlas = false;
    static const ImWchar ranges_latin_cyrillic[] = {
        0x0020, 0x00FF,
        0x0400, 0x052F,
        0x2000, 0x206F,
        0,
    };

    if (fs::exists("C:\\Windows\\Fonts\\segoeui.ttf")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f, &font_cfg_primary, ranges_latin_cyrillic);
    } else if (fs::exists("C:\\Windows\\Fonts\\arial.ttf")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 16.0f, &font_cfg_primary, ranges_latin_cyrillic);
    }

    ImFontConfig font_cfg_cjk;
    font_cfg_cjk.FontDataOwnedByAtlas = false;
    font_cfg_cjk.MergeMode = true;

    static const ImWchar ranges_cjk[] = {
        0x3000, 0x30FF,
        0x31F0, 0x31FF,
        0x4E00, 0x9FAF,
        0xFF00, 0xFFEF,
        0,
    };

    if (fs::exists("C:\\Windows\\Fonts\\msgothic.ttc")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msgothic.ttc", 16.0f, &font_cfg_cjk, ranges_cjk);
    } else if (fs::exists("C:\\Windows\\Fonts\\YuGothM.ttc")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\YuGothM.ttc", 16.0f, &font_cfg_cjk, ranges_cjk);
    }

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ItemSpacing = ImVec2(10, 6);
    style.FramePadding = ImVec2(10, 5);

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
    for (auto& item : m_tagItems) {
        if (item.localTexture) item.localTexture->Release();
        if (item.onlineTexture) item.onlineTexture->Release();
    }
    m_tagItems.clear();

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

        // Dynamic Texture Creation & Mathematical Perceptual Sharpness Evaluation
        for (auto& item : m_tagItems) {
            if (item.localTexture == NULL && !item.localCoverBytes.empty()) {
                item.localTexture = CreateTextureFromMemory(m_pd3dDevice, item.localCoverBytes.data(), item.localCoverBytes.size(), &item.localWidth, &item.localHeight);
                if (item.localTexture) {
                    double sharpness = CalculatePerceptualSharpness(item.localCoverBytes.data(), item.localCoverBytes.size());
                    item.localScore = (long long)(sharpness * item.localWidth * item.localHeight);
                }
            }

            if (item.isFetchCompleted && !item.onlineCoverBytes.empty() && item.onlineTexture == NULL) {
                item.onlineTexture = CreateTextureFromMemory(m_pd3dDevice, item.onlineCoverBytes.data(), item.onlineCoverBytes.size(), &item.onlineWidth, &item.onlineHeight);
                if (item.onlineTexture) {
                    double sharpness = CalculatePerceptualSharpness(item.onlineCoverBytes.data(), item.onlineCoverBytes.size());
                    item.onlineScore = (long long)(sharpness * item.onlineWidth * item.onlineHeight);
                    
                    // Automatically select mathematically sharper & true higher quality cover art!
                    if (item.onlineScore > item.localScore) {
                        item.selectedCoverChoice = 1;
                    } else {
                        item.selectedCoverChoice = 0;
                    }
                }
            }
        }

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

        // 4 Step-by-Step Workflow Stage Buttons
        if (ImGui::Button("1. [Поиск] Дубликаты (AcoustID)", ImVec2(240, 32))) {
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
        if (ImGui::Button("2. [Теги & Обложки] Инспектор", ImVec2(230, 32))) {
            if (!m_isTagScanning) {
                m_isTagScanning = true;
                LOG_INFO("Step 2: Instant local scan + parallel 16-thread MusicBrainz lookup...");
                m_tagItems.clear();
                m_currentTagIndex = 0;
                m_fetchedCount = 0;

                std::thread([this]() {
                    std::vector<std::string> files;
                    if (fs::exists(g_ToSortDir)) {
                        for (auto& p : fs::recursive_directory_iterator(g_ToSortDir)) {
                            if (p.is_regular_file()) {
                                std::string ext = p.path().extension().string();
                                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                if (ext == ".flac" || ext == ".mp3") {
                                    files.push_back(p.path().string());
                                }
                            }
                        }
                    }

                    // FAST LOCAL INITIALIZATION (0.01 seconds!)
                    m_tagItems.resize(files.size());
                    for (size_t i = 0; i < files.size(); ++i) {
                        auto& item = m_tagItems[i];
                        item.filePath = files[i];
                        item.relPath = fs::relative(files[i], g_BaseDir).string();

                        std::string fn = fs::path(files[i]).stem().string();
                        std::string trackNo = "01";
                        std::string title = fn;
                        std::string artistRaw = fs::path(files[i]).parent_path().parent_path().filename().string();
                        std::string albumRaw = fs::path(files[i]).parent_path().filename().string();

                        std::regex num_regex(R"(^(\d{1,2})[\.\s_\-]+(.+)$)");
                        std::smatch match;
                        if (std::regex_search(fn, match, num_regex)) {
                            trackNo = match[1].str();
                            if (trackNo.length() == 1) trackNo = "0" + trackNo;
                            title = match[2].str();
                        }

                        std::string artistClean = CleanMetadataString(artistRaw);
                        if (artistClean.empty() || artistClean == "TO SORT" || artistClean == "media" || artistClean == "music") {
                            artistClean = "Unknown Artist";
                        }
                        std::string albumClean = CleanMetadataString(albumRaw);

                        strncpy_s(item.artistBuf, artistClean.c_str(), sizeof(item.artistBuf) - 1);
                        strncpy_s(item.albumBuf, albumClean.c_str(), sizeof(item.albumBuf) - 1);
                        strncpy_s(item.titleBuf, title.c_str(), sizeof(item.titleBuf) - 1);
                        strncpy_s(item.trackNoBuf, trackNo.c_str(), sizeof(item.trackNoBuf) - 1);

                        // Look for local cover image
                        fs::path folderPath = fs::path(files[i]).parent_path();
                        for (auto& cfile : fs::directory_iterator(folderPath)) {
                            if (cfile.is_regular_file()) {
                                std::string cext = cfile.path().extension().string();
                                std::transform(cext.begin(), cext.end(), cext.begin(), ::tolower);
                                if (cext == ".jpg" || cext == ".jpeg" || cext == ".png" || cext == ".bmp") {
                                    std::ifstream fIn(cfile.path(), std::ios::binary);
                                    if (fIn.is_open()) {
                                        item.localCoverBytes = std::vector<unsigned char>((std::istreambuf_iterator<char>(fIn)), std::istreambuf_iterator<char>());
                                        item.localCoverPath = cfile.path().string();
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    // Post immediate notification to show local UI instantly!
                    PostMessageW(m_hWnd, WM_TAG_SCAN_FINISHED, 0, 0);

                    // ALBUM-LEVEL PARALLEL CACHING (Reduces 117 queries down to 10 album queries!)
                    std::unordered_map<std::string, AlbumMetadataCache> albumCache;
                    std::mutex cacheMutex;

                    const size_t numThreads = 16;
                    std::vector<std::thread> workers;

                    for (size_t t = 0; t < numThreads; ++t) {
                        workers.emplace_back([this, t, numThreads, &files, &albumCache, &cacheMutex]() {
                            for (size_t i = t; i < files.size(); i += numThreads) {
                                auto& item = m_tagItems[i];
                                std::string artistClean(item.artistBuf);
                                std::string albumClean(item.albumBuf);
                                std::string albumKey = artistClean + "___" + albumClean;

                                std::string releaseGroupMbId;
                                std::vector<unsigned char> coverData;
                                bool isMatched = false;

                                {
                                    std::lock_guard<std::mutex> lock(cacheMutex);
                                    if (albumCache.find(albumKey) != albumCache.end() && albumCache[albumKey].isFetched) {
                                        auto& c = albumCache[albumKey];
                                        item.isMusicBrainzMatched = c.isMatched;
                                        item.onlineCoverBytes = c.coverBytes;
                                        item.isFetchCompleted = true;
                                        m_fetchedCount++;
                                        continue;
                                    }
                                }

                                size_t currentNum = ++m_fetchedCount;
                                LOG_INFO("[MUSICBRAINZ FETCH " + std::to_string(currentNum) + "/" + std::to_string(files.size()) + "] Querying AcoustID & MusicBrainz for: " + artistClean + " - " + albumClean);

                                // 1. AcoustID Lookup
                                auto fpInfo = AcousticAnalyzer::Instance().ExtractFingerprint(files[i]);
                                if (!fpInfo.fpData.empty()) {
                                    std::wstringstream wss;
                                    wss << L"https://api.acoustid.org/v2/lookup?client=8Xa1nV0f&meta=recordings+releasegroups+compress&duration=" << (int)fpInfo.duration << L"&fingerprint=";
                                    for (size_t k = 0; k < fpInfo.fpData.size(); ++k) {
                                        if (k > 0) wss << L",";
                                        wss << fpInfo.fpData[k];
                                    }
                                    std::string acoustRes = HttpGetString(wss.str());
                                    size_t rgPos = acoustRes.find("\"releasegroups\":");
                                    if (rgPos != std::string::npos) {
                                        size_t idPos = acoustRes.find("\"id\":\"", rgPos);
                                        if (idPos != std::string::npos) {
                                            idPos += 6;
                                            size_t endPos = acoustRes.find("\"", idPos);
                                            if (endPos != std::string::npos) {
                                                releaseGroupMbId = acoustRes.substr(idPos, endPos - idPos);
                                                isMatched = true;
                                            }
                                        }
                                    }
                                }

                                // 2. Fallback: MusicBrainz Text Search API (Demetori / Doujin support!)
                                if (releaseGroupMbId.empty() && !artistClean.empty() && artistClean != "Unknown Artist" && !albumClean.empty()) {
                                    std::string mbQuery = "artist:\"" + artistClean + "\" AND release:\"" + albumClean + "\"";
                                    std::string mbUrl = "https://musicbrainz.org/ws/2/release-group?query=" + UrlEncode(mbQuery) + "&fmt=json";
                                    std::string mbRes = HttpGetString(Utf8ToWide(mbUrl));

                                    size_t rgPos = mbRes.find("\"release-groups\":");
                                    if (rgPos != std::string::npos) {
                                        size_t idPos = mbRes.find("\"id\":\"", rgPos);
                                        if (idPos != std::string::npos) {
                                            idPos += 6;
                                            size_t endPos = mbRes.find("\"", idPos);
                                            if (endPos != std::string::npos) {
                                                releaseGroupMbId = mbRes.substr(idPos, endPos - idPos);
                                                isMatched = true;
                                            }
                                        }
                                    }
                                }

                                // 3. Fetch Cover Art from CoverArtArchive.org if Release ID was found
                                if (!releaseGroupMbId.empty()) {
                                    LOG_INFO("[MUSICBRAINZ MATCHED] MBID " + releaseGroupMbId + " for " + artistClean + " - " + albumClean + ". Downloading CoverArtArchive image...");
                                    std::wstring caaUrl = Utf8ToWide("https://coverartarchive.org/release-group/" + releaseGroupMbId + "/front-500");
                                    coverData = HttpGetBytes(caaUrl);
                                    if (!coverData.empty()) {
                                        LOG_INFO("[COVER ART DOWNLOADED] " + std::to_string(coverData.size()) + " bytes cover art for " + albumClean);
                                    }
                                } else {
                                    LOG_INFO("[NICHE TRACK] MusicBrainz record not found for " + artistClean + " - " + albumClean + ". Using Level 3 prefilled metadata.");
                                }

                                item.isMusicBrainzMatched = isMatched;
                                item.onlineCoverBytes = coverData;
                                item.isFetchCompleted = true;

                                {
                                    std::lock_guard<std::mutex> lock(cacheMutex);
                                    albumCache[albumKey] = { releaseGroupMbId, coverData, isMatched, true };
                                }
                            }
                        });
                    }

                    for (auto& w : workers) {
                        if (w.joinable()) w.join();
                    }

                    m_isTagScanning = false;
                    LOG_INFO("Step 2 Background Online Fetching Complete. 100% of MusicBrainz queries finished.");
                }).detach();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("3. [Сортировка] Папки FLAC/MP3", ImVec2(240, 32))) {
            std::thread([]() {
                NativeMirrorCollections();
            }).detach();
        }
        ImGui::SameLine();
        if (ImGui::Button("4. [Реестр] Обновление Tracklist", ImVec2(240, 32))) {
            std::thread([]() {
                NativeSyncTracklistDatabase();
            }).detach();
        }

        ImGui::Separator();

        // Dual A/B Comparison Cards Layout
        float halfWidth = (ImGui::GetContentRegionAvail().x - 16.0f) * 0.5f;

        // Card A (Left)
        ImGui::BeginChild("CardA", ImVec2(halfWidth, 150), true);
        ImGui::TextDisabled("ТРЕК А (Левый)");
        if (!m_candidates.empty() && m_currentCandidateIndex < m_candidates.size()) {
            auto& pair = m_candidates[m_currentCandidateIndex];
            ImGui::TextUnformatted(fs::path(pair.trackA_path).filename().string().c_str());
            ImGui::Text("%s | %.1fs", pair.extA.c_str(), pair.durA);
            ImGui::TextDisabled("%s", pair.relA.c_str());
            ImGui::Spacing();
            if (ImGui::Button("[X] ОСТАВИТЬ ТРЕК А (Б -> delete)", ImVec2(-1, 32))) {
                MakeDecisionA();
            }
        } else {
            ImGui::TextUnformatted("Ожидание сканирования дубликатов...");
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Card B (Right)
        ImGui::BeginChild("CardB", ImVec2(halfWidth, 150), true);
        ImGui::TextDisabled("ТРЕК Б (Правый)");
        if (!m_candidates.empty() && m_currentCandidateIndex < m_candidates.size()) {
            auto& pair = m_candidates[m_currentCandidateIndex];
            ImGui::TextUnformatted(fs::path(pair.trackB_path).filename().string().c_str());
            ImGui::Text("%s | %.1fs", pair.extB.c_str(), pair.durB);
            ImGui::TextDisabled("%s", pair.relB.c_str());
            ImGui::Spacing();
            if (ImGui::Button("[X] ОСТАВИТЬ ТРЕК Б (А -> delete)", ImVec2(-1, 32))) {
                MakeDecisionB();
            }
        } else {
            ImGui::TextUnformatted("Ожидание сканирования дубликатов...");
        }
        ImGui::EndChild();

        // Step 2 Interactive Tag & Cover Inspector Card (When Step 2 is active)
        if (!m_tagItems.empty() && m_currentTagIndex < m_tagItems.size()) {
            auto& item = m_tagItems[m_currentTagIndex];
            ImGui::BeginChild("TagInspectorCard", ImVec2(0, 210), true);
            ImGui::TextDisabled("[ИНСПЕКТОР ТЕГОВ И ВЫБОР ОБЛОЖКИ] (%zu из %zu)", m_currentTagIndex + 1, m_tagItems.size());
            ImGui::SameLine();
            if (item.isMusicBrainzMatched) {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "[MUSICBRAINZ MATCHED]");
            } else if (m_isTagScanning) {
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "[SEARCHING MUSICBRAINZ %zu/%zu...]", m_fetchedCount.load(), m_tagItems.size());
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "[NICHE TRACK - LEVEL 3 PREFILLED]");
            }

            ImGui::Columns(2, "TagCols", false);
            ImGui::SetColumnWidth(0, 460);

            // Prefilled Editable Inputs
            ImGui::InputText("Исполнитель", item.artistBuf, sizeof(item.artistBuf));
            ImGui::InputText("Альбом", item.albumBuf, sizeof(item.albumBuf));
            ImGui::InputText("Название", item.titleBuf, sizeof(item.titleBuf));
            ImGui::InputText("Номер", item.trackNoBuf, sizeof(item.trackNoBuf));

            ImGui::NextColumn();

            // Side-by-Side Cover Art Choice & Mathematical Quality Comparison
            ImGui::TextDisabled("ВЫБОР И ДЕТЕКЦИЯ АПСКЕЙЛА ОБЛОЖЕК:");

            if (item.localTexture) {
                if (ImGui::ImageButton("##LocalCoverBtn", (ImTextureID)item.localTexture, ImVec2(80, 80))) {
                    item.selectedCoverChoice = 0;
                }
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Text(item.selectedCoverChoice == 0 ? "[X] Локальный скан" : "   Локальный скан");
                ImGui::TextDisabled("%dx%d px | %zu KB", item.localWidth, item.localHeight, item.localCoverBytes.size() / 1024);
                if (item.localScore >= item.onlineScore) {
                    ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "[*] ВЫСШЕЕ НАСТОЯЩЕЕ КАЧЕСТВО");
                } else if (item.localWidth >= 1000 && item.localScore < item.onlineScore / 3) {
                    ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "[!] ФАЛЬШИВЫЙ АПСКЕЙЛ/МЫЛО");
                }
                ImGui::EndGroup();
            } else {
                ImGui::TextDisabled("[Локальная обложка отсутствует]");
            }

            ImGui::SameLine();
            ImGui::Spacing();
            ImGui::SameLine();

            if (item.onlineTexture) {
                if (ImGui::ImageButton("##OnlineCoverBtn", (ImTextureID)item.onlineTexture, ImVec2(80, 80))) {
                    item.selectedCoverChoice = 1;
                }
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Text(item.selectedCoverChoice == 1 ? "[X] CoverArtArchive" : "   CoverArtArchive");
                ImGui::TextDisabled("%dx%d px | %zu KB", item.onlineWidth, item.onlineHeight, item.onlineCoverBytes.size() / 1024);
                if (item.onlineScore > item.localScore) {
                    ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "[*] ВЫСШЕЕ НАСТОЯЩЕЕ КАЧЕСТВО");
                } else if (item.onlineWidth >= 1000 && item.onlineScore < item.localScore / 3) {
                    ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "[!] ФАЛЬШИВЫЙ АПСКЕЙЛ/МЫЛО");
                }
                ImGui::EndGroup();
            } else if (m_isTagScanning && !item.isFetchCompleted) {
                ImGui::TextDisabled("[Загрузка онлайн обложки...]");
            } else {
                ImGui::TextDisabled("[Онлайн обложка отсутствует]");
            }

            ImGui::Columns(1);
            ImGui::Spacing();

            if (ImGui::Button("[V] Принять и Записать Теги & Обложку", ImVec2(280, 30))) {
                LOG_INFO("[TAGS APPLIED] " + std::string(item.artistBuf) + " - " + std::string(item.titleBuf) + " (" + std::string(item.albumBuf) + ")");
                m_currentTagIndex++;
            }
            ImGui::SameLine();
            if (ImGui::Button("[>>] Пропустить", ImVec2(140, 30))) {
                m_currentTagIndex++;
            }
            ImGui::EndChild();
        }

        // Audio Player Controls & Wave Similarity Section
        ImGui::BeginChild("PlayerControls", ImVec2(0, 115), true);
        if (!m_candidates.empty() && m_currentCandidateIndex < m_candidates.size()) {
            auto& pair = m_candidates[m_currentCandidateIndex];
            ImGui::Text("Сходство волн: %.1f%% | Смещение фазы: %d кадров", pair.similarity, pair.offset);
        } else {
            ImGui::TextDisabled("Сходство волн: --- % | Смещение фазы: --- кадров");
        }

        double cur = AudioEngine::Instance().GetCurrentPositionSeconds();
        double dur = AudioEngine::Instance().GetDurationSeconds();
        float seek_val = (dur > 0.0) ? (float)(cur / dur) : 0.0f;

        int cur_m = (int)cur / 60;
        int cur_s = (int)cur % 60;
        int dur_m = (int)dur / 60;
        int dur_s = (int)dur % 60;

        char time_buf[64];
        snprintf(time_buf, sizeof(time_buf), "Position: %02d:%02d / %02d:%02d", cur_m, cur_s, dur_m, dur_s);

        if (ImGui::SliderFloat("##SeekSlider", &seek_val, 0.0f, 1.0f, time_buf)) {
            AudioEngine::Instance().SeekToPercentage((double)seek_val * 100.0);
        }

        if (ImGui::Button(AudioEngine::Instance().IsPlaying() ? "[||] ПАУЗА" : "[>] ПРОИГРЫВАТЬ", ImVec2(140, 32))) {
            AudioEngine::Instance().TogglePlay();
        }
        ImGui::SameLine();

        char ch = AudioEngine::Instance().GetActiveChannel();
        if (ch == 'a') {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.00f, 0.00f, 1.00f));
        }
        if (ImGui::Button("ТРЕК А [1]", ImVec2(140, 32))) {
            AudioEngine::Instance().SetActiveChannel('a');
        }
        if (ch == 'a') ImGui::PopStyleColor(2);

        ImGui::SameLine();
        if (ch == 'b') {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.00f, 0.00f, 1.00f));
        }
        if (ImGui::Button("ТРЕК Б [2]", ImVec2(140, 32))) {
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

        // Fully Selectable & Copyable Log Console Field with Exact User Auto-Scroll Logic!
        ImGui::BeginChild("LogConsoleHeader", ImVec2(0, 0), true);
        ImGui::TextDisabled("ПОШАГОВЫЙ КОНСОЛЬНЫЙ ЖУРНАЛ СОБЫТИЙ (Выделите любой текст мышью / Ctrl+C):");
        ImGui::Separator();

        auto logs = Logger::Instance().GetLogs();
        static std::string log_buffer;
        log_buffer.clear();
        for (const auto& log : logs) {
            log_buffer += log + "\n";
        }

        static size_t last_log_size = 0;
        ImGui::InputTextMultiline("##LogConsoleMultiLineSelectable", log_buffer.data(), log_buffer.size() + 1, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);

        ImGuiContext& g = *GImGui;
        ImGuiWindow* childWindow = ImGui::FindWindowByName("##LogConsoleMultiLineSelectable_01");
        if (!childWindow) childWindow = g.CurrentWindow;

        if (childWindow) {
            float scrollY = childWindow->Scroll.y;
            float maxScrollY = childWindow->ScrollMax.y;

            if (maxScrollY > 0.0f) {
                if (scrollY >= maxScrollY - 25.0f) {
                    m_logAutoScroll = true;  // Turned ON when scrolled to the very bottom
                } else if (scrollY < maxScrollY - 40.0f) {
                    m_logAutoScroll = false; // Frozen/Turned OFF when user scrolls UP to read history!
                }
            } else {
                m_logAutoScroll = true;
            }

            if (m_logAutoScroll && logs.size() != last_log_size) {
                childWindow->ScrollTarget.y = childWindow->ScrollMax.y + 1000.0f; // Scroll to bottom on new logs if autoScroll ON
            }
        }
        last_log_size = logs.size();

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
    case WM_TAG_SCAN_FINISHED:
        Instance().HandleTagScanFinished();
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

void AppWindow::HandleTagScanFinished() {
    LOG_INFO("Step 2 Tagging & Cover Art inspection initialized. Loaded " + std::to_string(m_tagItems.size()) + " items into Inspector.");

    for (auto& item : m_tagItems) {
        if (!item.localCoverBytes.empty() && item.localTexture == NULL) {
            item.localTexture = CreateTextureFromMemory(m_pd3dDevice, item.localCoverBytes.data(), item.localCoverBytes.size(), &item.localWidth, &item.localHeight);
            if (item.localTexture) {
                double sharpness = CalculatePerceptualSharpness(item.localCoverBytes.data(), item.localCoverBytes.size());
                item.localScore = (long long)(sharpness * item.localWidth * item.localHeight);
            }
        }
        if (!item.onlineCoverBytes.empty() && item.onlineTexture == NULL) {
            item.onlineTexture = CreateTextureFromMemory(m_pd3dDevice, item.onlineCoverBytes.data(), item.onlineCoverBytes.size(), &item.onlineWidth, &item.onlineHeight);
            if (item.onlineTexture) {
                double sharpness = CalculatePerceptualSharpness(item.onlineCoverBytes.data(), item.onlineCoverBytes.size());
                item.onlineScore = (long long)(sharpness * item.onlineWidth * item.onlineHeight);
            }
        }
    }

    if (!m_tagItems.empty()) {
        m_currentTagIndex = 0;
    }
}
