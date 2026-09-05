#include "analyzer.hpp"
#include <algorithm>
#include <unordered_map>
#include <chrono>
#include <cmath>
#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif
#include "normalizer.hpp"
#include "rules.hpp"

double get_peak_rss_kb() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<double>(pmc.PeakWorkingSetSize) / 1024.0;
    }
#elif defined(__APPLE__)
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return static_cast<double>(usage.ru_maxrss) / 1024.0;
    }
#elif defined(__linux__)
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return static_cast<double>(usage.ru_maxrss);
    }
#endif
    return 0.0;
}

std::vector<TimeBucket> compute_time_buckets(
    const std::vector<LogRecord>& dated,
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end,
    std::size_t n
) {
    if (dated.empty()) return {};
    double dur = std::chrono::duration<double>(end - start).count();
    if (dur <= 0.0 || dated.size() == 1) {
        std::size_t errs = 0;
        for (const auto& r : dated) {
            if (r.level == Level::ERROR || r.level == Level::CRITICAL) errs++;
        }
        return {{format_time_only(start), dated.size(), errs}};
    }

    double step = dur / static_cast<double>(n);
    std::vector<std::size_t> counts(n, 0);
    std::vector<std::size_t> err_counts(n, 0);

    for (const auto& r : dated) {
        if (!r.timestamp.has_value()) continue;
        double offset = std::chrono::duration<double>(*r.timestamp - start).count();
        std::size_t idx = std::min(static_cast<std::size_t>(std::max(0.0, offset / step)), n - 1);
        counts[idx]++;
        if (r.level == Level::ERROR || r.level == Level::CRITICAL) {
            err_counts[idx]++;
        }
    }

    std::vector<TimeBucket> buckets;
    buckets.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        auto b_start = start + std::chrono::milliseconds(static_cast<long long>(i * step * 1000.0));
        auto b_end = start + std::chrono::milliseconds(static_cast<long long>((i + 1) * step * 1000.0));
        std::string label = format_time_only(b_start) + " - " + format_time_only(b_end);
        buckets.push_back({label, counts[i], err_counts[i]});
    }
    return buckets;
}

AnalysisStats compute_statistics(
    const std::vector<LogRecord>& records,
    std::size_t raw_line_count,
    std::size_t noise_count
) {
    auto t_start = std::chrono::steady_clock::now();

    AnalysisStats stats;
    stats.total_events = records.size();

    stats.severity_distribution[Level::INFO] = 0;
    stats.severity_distribution[Level::WARNING] = 0;
    stats.severity_distribution[Level::ERROR] = 0;
    stats.severity_distribution[Level::CRITICAL] = 0;
    stats.severity_distribution[Level::UNKNOWN] = 0;

    stats.category_summary[Category::FILE] = 0;
    stats.category_summary[Category::NETWORK] = 0;
    stats.category_summary[Category::SECURITY] = 0;
    stats.category_summary[Category::RESOURCE] = 0;
    stats.category_summary[Category::PROCESS] = 0;
    stats.category_summary[Category::SYSTEM] = 0;

    std::unordered_map<std::string, std::size_t> err_freq;
    std::vector<LogRecord> dated;

    for (const auto& r : records) {
        if (r.parse_status == ParseStatus::PARSED) stats.fully_parsed_events++;
        else if (r.parse_status == ParseStatus::PARTIAL) stats.partial_events++;
        else stats.invalid_events++;

        stats.severity_distribution[r.level]++;
        stats.category_summary[r.category]++;

        if (r.level == Level::ERROR || r.level == Level::CRITICAL) {
            err_freq[r.message]++;
        }

        if (r.timestamp.has_value()) {
            dated.push_back(r);
            if (!stats.earliest_timestamp.has_value() || *r.timestamp < *stats.earliest_timestamp) {
                stats.earliest_timestamp = r.timestamp;
            }
            if (!stats.latest_timestamp.has_value() || *r.timestamp > *stats.latest_timestamp) {
                stats.latest_timestamp = r.timestamp;
            }
        }
    }

    stats.valid_events = stats.fully_parsed_events + stats.partial_events;

    std::vector<std::pair<std::string, std::size_t>> error_pairs(err_freq.begin(), err_freq.end());
    std::sort(error_pairs.begin(), error_pairs.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    if (error_pairs.size() > 5) error_pairs.resize(5);
    stats.top_errors = std::move(error_pairs);

    if (stats.earliest_timestamp.has_value() && stats.latest_timestamp.has_value()) {
        stats.time_buckets = compute_time_buckets(dated, *stats.earliest_timestamp, *stats.latest_timestamp, TIME_BUCKETS_COUNT);
    }

    auto t_end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(t_end - t_start).count();
    if (elapsed <= 0.0) elapsed = 1e-6;

    BenchmarkTelemetry tel;
    tel.elapsed_seconds = elapsed;
    tel.throughput_eps = static_cast<double>(stats.total_events) / elapsed;
    tel.peak_memory_kb = get_peak_rss_kb();
    tel.total_raw_lines = raw_line_count ? raw_line_count : stats.total_events;
    tel.noise_lines_removed = noise_count;

    stats.telemetry = tel;
    return stats;
}
