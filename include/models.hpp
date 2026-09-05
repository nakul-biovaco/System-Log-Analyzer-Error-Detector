#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <chrono>
#include <utility>
#include <cstddef>

enum class Level {
    INFO,
    WARNING,
    ERROR,
    CRITICAL,
    UNKNOWN
};

enum class Category {
    FILE,
    NETWORK,
    SECURITY,
    RESOURCE,
    PROCESS,
    SYSTEM
};

enum class ParseStatus {
    PARSED,
    PARTIAL,
    INVALID
};

inline std::string level_to_string(Level lvl) {
    switch (lvl) {
        case Level::INFO: return "INFO";
        case Level::WARNING: return "WARNING";
        case Level::ERROR: return "ERROR";
        case Level::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

inline std::string category_to_string(Category cat) {
    switch (cat) {
        case Category::FILE: return "FILE";
        case Category::NETWORK: return "NETWORK";
        case Category::SECURITY: return "SECURITY";
        case Category::RESOURCE: return "RESOURCE";
        case Category::PROCESS: return "PROCESS";
        default: return "SYSTEM";
    }
}

inline std::string parse_status_to_string(ParseStatus st) {
    switch (st) {
        case ParseStatus::PARSED: return "PARSED";
        case ParseStatus::PARTIAL: return "PARTIAL";
        default: return "INVALID";
    }
}

struct LogRecord {
    std::string event_id;
    std::optional<std::chrono::system_clock::time_point> timestamp;
    std::string timestamp_str;
    Level level{Level::UNKNOWN};
    std::string message;
    Category category{Category::SYSTEM};
    std::string raw_message;
    ParseStatus parse_status{ParseStatus::INVALID};
    std::string source{"general"};
};

struct DetectionResult {
    std::string rule_id;
    std::string name;
    Level severity{Level::ERROR};
    bool triggered{false};
    std::string evidence;
};

struct AlertRecord {
    std::string alert_id;
    std::string rule_id;
    std::string name;
    Level severity{Level::ERROR};
    std::string evidence;
};

struct TimeBucket {
    std::string bucket_label;
    std::size_t count{0};
    std::size_t error_count{0};
};

struct BenchmarkTelemetry {
    double elapsed_seconds{0.0};
    double throughput_eps{0.0};
    double peak_memory_kb{0.0};
    std::string memory_metric_label{"Peak Process RSS"};
    std::size_t total_raw_lines{0};
    std::size_t noise_lines_removed{0};
};

struct AnalysisStats {
    std::size_t total_events{0};
    std::size_t valid_events{0};
    std::size_t invalid_events{0};
    std::size_t fully_parsed_events{0};
    std::size_t partial_events{0};
    std::unordered_map<Level, std::size_t> severity_distribution;
    std::unordered_map<Category, std::size_t> category_summary;
    std::vector<std::pair<std::string, std::size_t>> top_errors;
    std::optional<std::chrono::system_clock::time_point> earliest_timestamp;
    std::optional<std::chrono::system_clock::time_point> latest_timestamp;
    std::vector<TimeBucket> time_buckets;
    std::optional<BenchmarkTelemetry> telemetry;
};

struct RiskAssessment {
    int score{0};
    std::string band;
    std::string disclaimer;
};
