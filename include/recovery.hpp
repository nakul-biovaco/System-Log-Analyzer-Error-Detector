#pragma once

#include <string>
#include <vector>
#include <optional>
#include "models.hpp"

struct RecoveryMetrics {
    std::size_t timestamps_imputed{0};
    std::size_t levels_inferred{0};
    std::size_t tracebacks_stitched{0};
    std::size_t malformed_sanitized{0};
};

std::string sanitize_raw_log_line(const std::string& line);
bool is_stack_trace_continuation(const std::string& line);
std::vector<std::string> stitch_multiline_tracebacks(const std::vector<std::string>& lines, std::size_t& stitched_count);
LogRecord auto_repair_log_record(const std::string& line, std::size_t seq, const std::optional<std::string>& last_valid_ts, RecoveryMetrics& metrics);
std::vector<LogRecord> apply_sliding_ring_buffer(std::vector<LogRecord> records, std::size_t max_capacity = 5000);
