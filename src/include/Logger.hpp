#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

enum class LogLevel {
    Info,
    Warning,
    Error,
    Debug
};

class Logger {
public:
    using LogCallback = std::function<void(const std::string&)>;

    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    void SetCallback(LogCallback cb) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_callback = cb;
    }

    void Log(LogLevel level, const std::string& message) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");

        std::string lvlStr;
        switch (level) {
            case LogLevel::Info: lvlStr = "[INFO]"; break;
            case LogLevel::Warning: lvlStr = "[WARN]"; break;
            case LogLevel::Error: lvlStr = "[ERROR]"; break;
            case LogLevel::Debug: lvlStr = "[DEBUG]"; break;
        }

        std::string formatted = "[" + ss.str() + "] " + lvlStr + " " + message;
        m_logs.push_back(formatted);

        std::cout << formatted << std::endl;

        if (m_callback) {
            m_callback(formatted);
        }
    }

    std::vector<std::string> GetLogs() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_logs;
    }

private:
    Logger() = default;
    mutable std::mutex m_mutex;
    std::vector<std::string> m_logs;
    LogCallback m_callback;
};

#define LOG_INFO(msg) Logger::Instance().Log(LogLevel::Info, msg)
#define LOG_WARN(msg) Logger::Instance().Log(LogLevel::Warning, msg)
#define LOG_ERROR(msg) Logger::Instance().Log(LogLevel::Error, msg)
#define LOG_DEBUG(msg) Logger::Instance().Log(LogLevel::Debug, msg)
