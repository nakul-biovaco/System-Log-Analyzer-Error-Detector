#include "detection.hpp"
#include <iomanip>
#include <sstream>
#include "rules.hpp"

std::vector<DetectionResult> run_detection(const AnalysisStats& stats) {
    std::vector<DetectionResult> results;
    std::size_t total = stats.total_events;

    std::size_t error_cnt = 0;
    auto it_err = stats.severity_distribution.find(Level::ERROR);
    if (it_err != stats.severity_distribution.end()) error_cnt += it_err->second;
    auto it_crit = stats.severity_distribution.find(Level::CRITICAL);
    if (it_crit != stats.severity_distribution.end()) error_cnt += it_crit->second;

    for (const auto& pair : stats.top_errors) {
        if (pair.second >= REPEATED_ERROR_THRESHOLD) {
            std::string msg = pair.first;
            std::string snip = (msg.size() > 75) ? (msg.substr(0, 75) + "...") : msg;
            results.push_back({
                "R001",
                "Repeated Error Pattern",
                Level::ERROR,
                true,
                "Error '" + snip + "' occurred " + std::to_string(pair.second) + " times (threshold: >= " + std::to_string(REPEATED_ERROR_THRESHOLD) + ")"
            });
        }
    }

    std::size_t crit_count = (it_crit != stats.severity_distribution.end()) ? it_crit->second : 0;
    if (crit_count > 0) {
        results.push_back({
            "R002",
            "Critical System Event",
            Level::CRITICAL,
            true,
            "Found " + std::to_string(crit_count) + " CRITICAL/FAULT level event(s) requiring immediate intervention"
        });
    }

    if (error_cnt >= HIGH_ERROR_VOLUME) {
        results.push_back({
            "R003",
            "High Error Volume",
            Level::ERROR,
            true,
            "Total error events (" + std::to_string(error_cnt) + ") reached threshold of >= " + std::to_string(HIGH_ERROR_VOLUME)
        });
    }

    if (total > 0) {
        double ratio = static_cast<double>(error_cnt) / static_cast<double>(total);
        if (ratio > HIGH_ERROR_RATIO) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << (ratio * 100.0) << "% (" << error_cnt << "/" << total << ") exceeds " << static_cast<int>(HIGH_ERROR_RATIO * 100.0) << "% threshold";
            results.push_back({
                "R004",
                "High Error Ratio",
                Level::WARNING,
                true,
                "Error ratio " + oss.str()
            });
        }
    }

    if (!stats.time_buckets.empty()) {
        std::size_t total_bucket_errors = 0;
        for (const auto& b : stats.time_buckets) total_bucket_errors += b.error_count;
        double avg = static_cast<double>(total_bucket_errors) / static_cast<double>(stats.time_buckets.size());

        for (const auto& b : stats.time_buckets) {
            if (b.error_count >= 2 && static_cast<double>(b.error_count) > (avg * SPIKE_DENSITY_THRESHOLD)) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(1) << avg;
                results.push_back({
                    "R005",
                    "Time-Windowed Error Spike",
                    Level::ERROR,
                    true,
                    "Window [" + b.bucket_label + "] recorded " + std::to_string(b.error_count) + " errors (exceeds " + std::to_string(static_cast<int>(SPIKE_DENSITY_THRESHOLD)) + ".0x average of " + oss.str() + ")"
                });
            }
        }
    }

    return results;
}
