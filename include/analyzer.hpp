#pragma once

#include <vector>
#include <chrono>
#include <cstddef>
#include "models.hpp"

std::vector<TimeBucket> compute_time_buckets(
    const std::vector<LogRecord>& dated,
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end,
    std::size_t n = 4
);

AnalysisStats compute_statistics(
    const std::vector<LogRecord>& records,
    std::size_t raw_line_count = 0,
    std::size_t noise_count = 0
);

double get_peak_rss_kb();
