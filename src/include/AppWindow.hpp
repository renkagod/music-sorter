#pragma once

#include "CoreEngine.hpp"

// AppWindow compatibility layer for tests and existing modules
class AppWindow {
public:
    static AppWindow& Instance() {
        static AppWindow instance;
        return instance;
    }

    std::vector<size_t> GetAlbumTrackIndices(size_t referenceIndex) const {
        return CoreEngine::Instance().GetAlbumTrackIndices(referenceIndex);
    }

    void ApproveTracks(const std::vector<size_t>& indices) {
        for (size_t idx : indices) {
            CoreEngine::Instance().ApproveTrack(idx);
        }
    }

    void SkipTracks(const std::vector<size_t>& indices) {
        for (size_t idx : indices) {
            CoreEngine::Instance().SkipTrack(idx);
        }
    }

    void StartTagScan() {
        CoreEngine::Instance().StartTagScan();
    }
};
