#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <thread>

class HttpServer {
public:
    HttpServer(int port = 8765);
    ~HttpServer();

    bool Start();
    void Stop();
    bool IsRunning() const { return m_running; }
    int GetPort() const { return m_port; }

private:
    void RunLoop();
    void HandleClient(uintptr_t clientSocket);

    int m_port;
    std::atomic<bool> m_running{false};
    uintptr_t m_serverSocket{0};
    std::thread m_serverThread;
};
