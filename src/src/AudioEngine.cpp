#define MINIAUDIO_IMPLEMENTATION
#include "../third_party/miniaudio.h"
#include "../include/AudioEngine.hpp"
#include "../include/Logger.hpp"

AudioEngine::~AudioEngine() {
    Shutdown();
}

bool AudioEngine::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return true;

    ma_engine_config config = ma_engine_config_init();
    if (ma_engine_init(&config, &m_engine) != MA_SUCCESS) {
        LOG_ERROR("Failed to initialize miniaudio engine!");
        return false;
    }

    m_initialized = true;
    LOG_INFO("Miniaudio engine initialized successfully.");
    return true;
}

void AudioEngine::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return;

    if (m_soundALoaded) { ma_sound_uninit(&m_soundA); m_soundALoaded = false; }
    if (m_soundBLoaded) { ma_sound_uninit(&m_soundB); m_soundBLoaded = false; }
    ma_engine_uninit(&m_engine);
    m_initialized = false;
    LOG_INFO("Miniaudio engine shut down.");
}

bool AudioEngine::LoadTrackA(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return false;

    if (m_soundALoaded) { ma_sound_uninit(&m_soundA); m_soundALoaded = false; }

    if (ma_sound_init_from_file(&m_engine, path.c_str(), 0, NULL, NULL, &m_soundA) == MA_SUCCESS) {
        m_soundALoaded = true;
        LOG_INFO("Loaded Track A: " + path);
        ma_sound_set_volume(&m_soundA, (m_activeChannel == 'a') ? 1.0f : 0.0f);
        return true;
    }

    LOG_ERROR("Failed to load Track A: " + path);
    return false;
}

bool AudioEngine::LoadTrackB(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return false;

    if (m_soundBLoaded) { ma_sound_uninit(&m_soundB); m_soundBLoaded = false; }

    if (ma_sound_init_from_file(&m_engine, path.c_str(), 0, NULL, NULL, &m_soundB) == MA_SUCCESS) {
        m_soundBLoaded = true;
        LOG_INFO("Loaded Track B: " + path);
        ma_sound_set_volume(&m_soundB, (m_activeChannel == 'b') ? 1.0f : 0.0f);
        return true;
    }

    LOG_ERROR("Failed to load Track B: " + path);
    return false;
}

void AudioEngine::Play() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_soundALoaded) ma_sound_start(&m_soundA);
    if (m_soundBLoaded) ma_sound_start(&m_soundB);
    m_isPlaying = true;
}

void AudioEngine::Pause() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_soundALoaded) ma_sound_stop(&m_soundA);
    if (m_soundBLoaded) ma_sound_stop(&m_soundB);
    m_isPlaying = false;
}

void AudioEngine::TogglePlay() {
    if (m_isPlaying) Pause();
    else Play();
}

void AudioEngine::SetActiveChannel(char channel) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeChannel = channel;
    if (channel == 'a') {
        if (m_soundALoaded) ma_sound_set_volume(&m_soundA, 1.0f);
        if (m_soundBLoaded) ma_sound_set_volume(&m_soundB, 0.0f);
        LOG_INFO("Hot-Swap: Switched active audio output to TRACK A");
    } else {
        if (m_soundALoaded) ma_sound_set_volume(&m_soundA, 0.0f);
        if (m_soundBLoaded) ma_sound_set_volume(&m_soundB, 1.0f);
        LOG_INFO("Hot-Swap: Switched active audio output to TRACK B");
    }
}

void AudioEngine::SeekToPercentage(double percent) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_soundALoaded) {
        ma_uint64 length;
        ma_sound_get_length_in_pcm_frames(&m_soundA, &length);
        ma_uint64 target = (ma_uint64)(length * (percent / 100.0));
        ma_sound_seek_to_pcm_frame(&m_soundA, target);
        if (m_soundBLoaded) ma_sound_seek_to_pcm_frame(&m_soundB, target);
    }
}

double AudioEngine::GetCurrentPositionSeconds() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_soundALoaded) {
        ma_uint64 cursor;
        ma_sound_get_cursor_in_pcm_frames(&m_soundA, &cursor);
        ma_uint32 sampleRate = ma_engine_get_sample_rate(&m_engine);
        if (sampleRate > 0) return (double)cursor / (double)sampleRate;
    }
    return 0.0;
}

double AudioEngine::GetDurationSeconds() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_soundALoaded) {
        ma_uint64 length;
        ma_sound_get_length_in_pcm_frames(&m_soundA, &length);
        ma_uint32 sampleRate = ma_engine_get_sample_rate(&m_engine);
        if (sampleRate > 0) return (double)length / (double)sampleRate;
    }
    return 0.0;
}
