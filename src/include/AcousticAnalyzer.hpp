#pragma once
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

struct AudioFingerprint {
    std::string path;
    double duration = 0.0;
    std::vector<unsigned int> fpData;
};

struct ABCandidatePair {
    std::string id;
    std::string trackA_path;
    std::string relA;
    std::string extA;
    double durA = 0.0;
    std::string trackB_path;
    std::string relB;
    std::string extB;
    double durB = 0.0;
    double similarity = 0.0;
    int offset = 0;
};

class AcousticAnalyzer {
public:
    static AcousticAnalyzer& Instance() {
        static AcousticAnalyzer instance;
        return instance;
    }

    void SetFpcalcPath(const std::string& path) { m_fpcalcBin = path; }

    AudioFingerprint ExtractFingerprint(const std::string& filepath);
    
    double CalculateSlidingCrossCorrelation(
        const std::vector<unsigned int>& fp1,
        const std::vector<unsigned int>& fp2,
        int maxOffsetFrames,
        int& outOffset
    );

    void AnalyzeDirectory(
        const std::string& directoryPath,
        const std::string& baseDirectory,
        std::vector<ABCandidatePair>& outCandidates,
        std::vector<std::string>& outAutoDelete
    );

private:
    AcousticAnalyzer() = default;
    std::string m_fpcalcBin;
};
