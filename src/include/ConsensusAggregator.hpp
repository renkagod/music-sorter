#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include "MetadataUtils.hpp"

namespace ConsensusAggregator {

struct MetadataCandidate {
    std::string providerName;
    std::string artist;
    std::string album;
    std::string title;
    std::string year;
    int trackNumber{0};
    std::string coverUrl;
    double confidence{0.0};
    std::vector<std::string> tracklist;
    std::string releaseId;
    int durationSec{0};
};

struct AggregationResult {
    MetadataCandidate bestCandidate;
    std::vector<MetadataCandidate> allCandidates;
    bool hasConflict{false};
    double confidence{0.0};
    std::string conflictReason;
};

inline double GetProviderTrustWeight(const std::string& providerName) {
    if (providerName == "MusicBrainz" || providerName == "TouhouDB" ||
        providerName == "VocaDB" || providerName == "UtaiteDB" || providerName == "THBWiki") {
        return 1.00;
    }
    if (providerName == "Discogs") return 0.96;
    if (providerName == "Last.fm") return 0.94;
    if (providerName == "YouTube Music") return 0.92;
    return 0.90;
}

inline int GetMetadataCompletenessScore(const MetadataCandidate& c) {
    int score = 0;
    if (!c.artist.empty()) score++;
    if (!c.album.empty()) score += 2;
    if (!c.title.empty()) score++;
    if (!c.year.empty()) score += 2;
    if (c.trackNumber > 0) score += 2;
    if (!c.coverUrl.empty()) score++;
    if (!c.tracklist.empty()) score += 3;
    return score;
}

inline bool AreTrackCandidatesInAgreement(const MetadataCandidate& a, const MetadataCandidate& b) {
    int numA = ExtractTrailingOrEmbeddedNumber(a.title);
    int numB = ExtractTrailingOrEmbeddedNumber(b.title);
    if (numA >= 0 && numB >= 0 && numA != numB) {
        return false;
    }

    if (a.trackNumber > 0 && b.trackNumber > 0 && a.trackNumber != b.trackNumber) {
        return false;
    }

    double titleSim = ComputeStringSimilarity(a.title, b.title);
    if (titleSim < 0.85 && NormalizeKey(a.title) != NormalizeKey(b.title)) {
        return false;
    }

    bool aArtKnown = !IsUnknownArtist(a.artist);
    bool bArtKnown = !IsUnknownArtist(b.artist);
    if (aArtKnown && bArtKnown) {
        double artSim = ComputeStringSimilarity(a.artist, b.artist);
        if (artSim < 0.85 && NormalizeKey(a.artist) != NormalizeKey(b.artist)) {
            bool sameAlbum = (!a.album.empty() && !b.album.empty() &&
                (ComputeStringSimilarity(a.album, b.album) >= 0.85 || NormalizeKey(a.album) == NormalizeKey(b.album)));
            if (!sameAlbum) {
                return false;
            }
        }
    }

    return true;
}

inline bool AreAlbumCandidatesInAgreement(const MetadataCandidate& a, const MetadataCandidate& b) {
    double albSim = ComputeStringSimilarity(a.album, b.album);
    if (albSim < 0.85 && NormalizeKey(a.album) != NormalizeKey(b.album)) {
        return false;
    }

    bool aArtKnown = !IsUnknownArtist(a.artist);
    bool bArtKnown = !IsUnknownArtist(b.artist);
    if (aArtKnown && bArtKnown) {
        double artSim = ComputeStringSimilarity(a.artist, b.artist);
        if (artSim < 0.85 && NormalizeKey(a.artist) != NormalizeKey(b.artist)) {
            return false;
        }
    }

    if (!a.tracklist.empty() && !b.tracklist.empty()) {
        size_t minTracks = (std::min)(a.tracklist.size(), b.tracklist.size());
        size_t maxTracks = (std::max)(a.tracklist.size(), b.tracklist.size());
        if (maxTracks > 0 && (double)minTracks / (double)maxTracks < 0.60) {
            return false;
        }
    }

    return true;
}

inline int CountDistinctAgreeingProviders(
    const MetadataCandidate& target,
    const std::vector<MetadataCandidate>& candidates,
    bool isAlbum
) {
    std::unordered_set<std::string> providers;
    if (!target.providerName.empty()) {
        providers.insert(target.providerName);
    }

    for (const auto& c : candidates) {
        if (c.providerName.empty()) continue;
        bool agrees = isAlbum ? AreAlbumCandidatesInAgreement(target, c)
                              : AreTrackCandidatesInAgreement(target, c);
        if (agrees) {
            providers.insert(c.providerName);
        }
    }
    return static_cast<int>(providers.size());
}

inline AggregationResult AggregateTrackCandidates(
    const std::string& currentArtist,
    const std::string& currentTitle,
    const std::vector<MetadataCandidate>& rawCandidates
) {
    AggregationResult result;
    if (rawCandidates.empty()) {
        result.hasConflict = false;
        result.confidence = 0.0;
        result.conflictReason = "No candidates found";
        return result;
    }

    std::vector<MetadataCandidate> scoredCandidates = rawCandidates;
    int localNum = ExtractTrailingOrEmbeddedNumber(currentTitle);
    bool hasLocalArtist = !IsUnknownArtist(currentArtist);

    // Phase 1: Compute base local similarity score or preserve caller-provided candidate confidence
    for (auto& cand : scoredCandidates) {
        int candNum = ExtractTrailingOrEmbeddedNumber(cand.title);
        bool numberMismatch = (localNum >= 0 && candNum >= 0 && localNum != candNum);

        if (cand.title.empty() && cand.artist.empty()) {
            cand.confidence = 0.0;
            continue;
        }

        if (cand.confidence != 0.0) {
            cand.confidence = (std::max)(0.0, (std::min)(1.0, cand.confidence));

            if (hasLocalArtist && !IsUnknownArtist(cand.artist)) {
                double artSim = ComputeStringSimilarity(currentArtist, cand.artist);
                if (artSim < 0.60) {
                    cand.confidence = (std::min)(cand.confidence * 0.25, 0.15);
                }
            }
            if (numberMismatch) {
                cand.confidence = (std::min)(cand.confidence * 0.10, 0.10);
            }
        } else {
            double titleSim = ComputeStringSimilarity(currentTitle, cand.title);
            if (numberMismatch) {
                titleSim = (std::min)(titleSim * 0.10, 0.10);
            }

            double rawScore = 0.0;
            if (hasLocalArtist) {
                bool candArtistKnown = !IsUnknownArtist(cand.artist);
                double artSim = candArtistKnown ? ComputeStringSimilarity(currentArtist, cand.artist) : 0.0;

                if (artSim < 0.60) {
                    rawScore = (std::min)((0.50 * artSim + 0.50 * titleSim) * 0.25, 0.15);
                } else {
                    rawScore = 0.45 * artSim + 0.55 * titleSim;
                }
            } else {
                rawScore = titleSim * 0.85;
            }

            double trust = GetProviderTrustWeight(cand.providerName);
            cand.confidence = (std::min)(rawScore * trust, 1.0);
            if (cand.confidence < 0.0) cand.confidence = 0.0;
        }
    }

    // Phase 2: Multi-provider consensus agreement boost
    for (auto& cand : scoredCandidates) {
        if (cand.confidence < 0.25) continue;

        int distinctProviders = CountDistinctAgreeingProviders(cand, scoredCandidates, false);
        double boost = 0.0;
        if (distinctProviders >= 4) {
            boost = 0.25;
        } else if (distinctProviders == 3) {
            boost = 0.18;
        } else if (distinctProviders == 2) {
            boost = 0.10;
        }

        if (boost > 0.0) {
            cand.confidence = (std::min)(1.0, cand.confidence + boost);
        }
    }

    // Phase 3: Sort descending by confidence, tie-breaking by completeness and provider trust
    std::sort(scoredCandidates.begin(), scoredCandidates.end(), [](const MetadataCandidate& a, const MetadataCandidate& b) {
        if (std::abs(a.confidence - b.confidence) > 0.0001) {
            return a.confidence > b.confidence;
        }
        int compA = GetMetadataCompletenessScore(a);
        int compB = GetMetadataCompletenessScore(b);
        if (compA != compB) {
            return compA > compB;
        }
        return GetProviderTrustWeight(a.providerName) > GetProviderTrustWeight(b.providerName);
    });

    result.allCandidates = scoredCandidates;
    result.bestCandidate = scoredCandidates[0];
    result.confidence = result.bestCandidate.confidence;

    // Phase 4: Conflict detection
    if (result.confidence < 0.80) {
        result.hasConflict = true;
        int confPct = static_cast<int>(std::round(result.confidence * 100.0));
        result.conflictReason = "Confidence " + std::to_string(confPct) + "% is below the 80% threshold";
        return result;
    }

    if (scoredCandidates.size() >= 2) {
        for (size_t i = 1; i < scoredCandidates.size(); ++i) {
            const auto& rival = scoredCandidates[i];
            if (rival.providerName == result.bestCandidate.providerName) {
                continue;
            }

            bool agrees = AreTrackCandidatesInAgreement(result.bestCandidate, rival);
            if (!agrees) {
                double diff = result.bestCandidate.confidence - rival.confidence;
                if (rival.confidence >= 0.70 && diff < 0.15) {
                    result.hasConflict = true;
                    result.conflictReason = "Conflict between " + result.bestCandidate.providerName +
                        " ('" + result.bestCandidate.title + "') and " + rival.providerName +
                        " ('" + rival.title + "')";
                    return result;
                }
            }
        }
    }

    if (hasLocalArtist && !IsUnknownArtist(result.bestCandidate.artist)) {
        double artSim = ComputeStringSimilarity(currentArtist, result.bestCandidate.artist);
        if (artSim < 0.60) {
            result.hasConflict = true;
            result.conflictReason = "Candidate artist ('" + result.bestCandidate.artist +
                "') diverges from local artist ('" + currentArtist + "')";
            return result;
        }
    }

    result.hasConflict = false;
    result.conflictReason = "Approved: confidence >= 80% with clear consensus";
    return result;
}

inline AggregationResult AggregateAlbumCandidates(
    const std::string& currentArtist,
    const std::string& currentAlbum,
    const std::vector<std::string>& localTitles,
    const std::vector<MetadataCandidate>& rawCandidates
) {
    AggregationResult result;
    if (rawCandidates.empty()) {
        result.hasConflict = false;
        result.confidence = 0.0;
        result.conflictReason = "No candidates found";
        return result;
    }

    std::vector<MetadataCandidate> scoredCandidates = rawCandidates;
    bool hasLocalArtist = !IsUnknownArtist(currentArtist);

    // Phase 1: Base guardrail evaluation
    for (auto& cand : scoredCandidates) {
        if (!cand.tracklist.empty()) {
            auto guard = ValidateAlbumMatch(
                currentArtist, currentAlbum, localTitles,
                cand.artist, cand.album, cand.tracklist
            );
            cand.confidence = guard.confidence * GetProviderTrustWeight(cand.providerName);
        } else {
            double artSim = hasLocalArtist ? ComputeStringSimilarity(currentArtist, cand.artist) : 0.80;
            double albSim = (!currentAlbum.empty()) ? ComputeStringSimilarity(currentAlbum, cand.album) : 0.50;

            if (hasLocalArtist && artSim < 0.60) {
                cand.confidence = (std::min)(0.25 * artSim, 0.15);
            } else {
                double base = (0.50 * artSim + 0.50 * albSim) * GetProviderTrustWeight(cand.providerName);
                if (!localTitles.empty()) {
                    cand.confidence = (std::min)(base, 0.75);
                } else {
                    cand.confidence = base;
                }
            }
        }
        if (cand.confidence > 1.0) cand.confidence = 1.0;
        if (cand.confidence < 0.0) cand.confidence = 0.0;
    }

    // Phase 2: Multi-provider consensus agreement boost
    for (auto& cand : scoredCandidates) {
        if (cand.confidence < 0.30) continue;

        int distinctProviders = CountDistinctAgreeingProviders(cand, scoredCandidates, true);
        double boost = 0.0;
        if (distinctProviders >= 4) {
            boost = 0.25;
        } else if (distinctProviders == 3) {
            boost = 0.18;
        } else if (distinctProviders == 2) {
            boost = 0.10;
        }

        if (boost > 0.0) {
            cand.confidence = (std::min)(1.0, cand.confidence + boost);
        }
    }

    // Phase 3: Sort descending by confidence
    std::sort(scoredCandidates.begin(), scoredCandidates.end(), [](const MetadataCandidate& a, const MetadataCandidate& b) {
        if (std::abs(a.confidence - b.confidence) > 0.0001) {
            return a.confidence > b.confidence;
        }
        int compA = GetMetadataCompletenessScore(a);
        int compB = GetMetadataCompletenessScore(b);
        if (compA != compB) {
            return compA > compB;
        }
        return GetProviderTrustWeight(a.providerName) > GetProviderTrustWeight(b.providerName);
    });

    result.allCandidates = scoredCandidates;
    result.bestCandidate = scoredCandidates[0];
    result.confidence = result.bestCandidate.confidence;

    // Phase 4: Conflict detection
    if (result.confidence < 0.80) {
        result.hasConflict = true;
        int confPct = static_cast<int>(std::round(result.confidence * 100.0));
        result.conflictReason = "Album confidence " + std::to_string(confPct) + "% is below 80% threshold";
        return result;
    }

    if (scoredCandidates.size() >= 2) {
        for (size_t i = 1; i < scoredCandidates.size(); ++i) {
            const auto& rival = scoredCandidates[i];
            if (rival.providerName == result.bestCandidate.providerName) {
                continue;
            }

            bool agrees = AreAlbumCandidatesInAgreement(result.bestCandidate, rival);
            if (!agrees) {
                double diff = result.bestCandidate.confidence - rival.confidence;
                if (rival.confidence >= 0.70 && diff < 0.15) {
                    result.hasConflict = true;
                    result.conflictReason = "Conflict between " + result.bestCandidate.providerName +
                        " ('" + result.bestCandidate.album + "') and " + rival.providerName +
                        " ('" + rival.album + "')";
                    return result;
                }
            }
        }
    }

    result.hasConflict = false;
    result.conflictReason = "Approved: album confidence >= 80% with clear consensus";
    return result;
}

} // namespace ConsensusAggregator
