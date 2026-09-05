#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstddef>
#include "models.hpp"

constexpr std::size_t REPEATED_ERROR_THRESHOLD = 3;
constexpr std::size_t HIGH_ERROR_VOLUME = 5;
constexpr double HIGH_ERROR_RATIO = 0.20;
constexpr double SPIKE_DENSITY_THRESHOLD = 2.0;
constexpr std::size_t TIME_BUCKETS_COUNT = 4;
constexpr int CRITICAL_RISK_FLOOR = 60;

inline int get_severity_weight(Level lvl) {
    switch (lvl) {
        case Level::INFO: return 0;
        case Level::WARNING: return 1;
        case Level::ERROR: return 3;
        case Level::CRITICAL: return 5;
        default: return 1;
    }
}

inline std::string get_risk_band(int score) {
    if (score < 20) return "HEALTHY";
    if (score < 40) return "NORMAL";
    if (score < 60) return "WARNING";
    if (score < 80) return "HIGH RISK";
    return "CRITICAL";
}

inline const std::vector<std::string>& get_noise_denylist() {
    static const std::vector<std::string> denylist = {
        "AppleBCMWLAN",
        "clocksyncd",
        "SCAN_INFO",
        "Clock Statistics",
        "CoreAnalytics",
        "IOPlatformPluginFamily: thermal"
    };
    return denylist;
}

constexpr const char* RISK_DISCLAIMER = "Risk score computed from severity weights, volume thresholds, and rule activations.";
