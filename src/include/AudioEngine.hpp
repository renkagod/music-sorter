#pragma once
#include <string>
#include <mutex>
#include "../third_party/miniaudio.h"

class AudioEngine {
public:
    static AudioEngine& Instance() {
        static AudioEngine instance;
        return instance;
    }

    bool Initialize();
    void Shutdown();

    bool LoadTrackA(const std::string& path);
    bool LoadTrackB(const std::string& path);

    void Play();
    void Pause();
    void TogglePlay();
    bool IsPlaying() const { return m_isPlaying; }

    void SetActiveChannel(char channel); // 'a' or 'b'
    char GetActiveChannel() const { return m_activeChannel; }

    void SetMasterVolume(float volume); // 0.0f to 1.0f
    float GetMasterVolume() const { return m_masterVolume; }

    void SeekToPercentage(double percent);
    double GetCurrentPositionSeconds();
    double GetDurationSeconds();

private:
    AudioEngine() = default;
    ~AudioEngine();

    ma_engine m_engine;
    ma_sound m_soundA;
    ma_sound m_soundB;
    bool m_initialized = false;
    bool m_soundALoaded = false;
    bool m_soundBLoaded = false;
    bool m_isPlaying = false;
    char m_activeChannel = 'a';
    float m_masterVolume = 0.25f; // Default 25% volume so user doesn't go deaf!
    mutable std::mutex m_mutex;
};
