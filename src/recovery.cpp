#include "include/recovery.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <regex>
#include <chrono>
#include "include/normalizer.hpp"
#include "include/classifier.hpp"
#include "include/parsers.hpp"

std::string sanitize_raw_log_line(const std::string& line) {
    std::string sanitized;
    sanitized.reserve(line.size());
    for (char c : line) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc == '\t' || uc == '\n' || uc == '\r' || (uc >= 32 && uc < 127)) {
            sanitized.push_back(c);
        } else if (uc >= 128) {
            sanitized.push_back(c);
        } else {
            sanitized.push_back(' ');
        }
    }
    while (!sanitized.empty() && std::isspace(static_cast<unsigned char>(sanitized.front()))) {
        sanitized.erase(sanitized.begin());
    }
    while (!sanitized.empty() && std::isspace(static_cast<unsigned char>(sanitized.back()))) {
        sanitized.pop_back();
    }
    return sanitized;
}

bool is_stack_trace_continuation(const std::string& line) {
    if (line.empty()) return false;
    if (std::isspace(static_cast<unsigned char>(line.front()))) return true;
    if (line.rfind("at ", 0) == 0) return true;
    if (line.rfind("File \"", 0) == 0) return true;
    if (line.rfind("Caused by:", 0) == 0) return true;
    if (line.rfind("Traceback", 0) == 0) return true;
    if (line.rfind("Exception in", 0) == 0) return true;
    if (line.rfind("...", 0) == 0) return true;
    if (line.rfind("0x", 0) == 0) return true;
    return false;
}

std::vector<std::string> stitch_multiline_tracebacks(const std::vector<std::string>& lines, std::size_t& stitched_count) {
    std::vector<std::string> result;
    result.reserve(lines.size());
    stitched_count = 0;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string cur = sanitize_raw_log_line(lines[i]);
        if (cur.empty()) continue;

        if (is_stack_trace_continuation(cur) && !result.empty()) {
            result.back() += " | " + cur;
            stitched_count++;
        } else {
            result.push_back(cur);
        }
    }

    result.shrink_to_fit();
    return result;
}

LogRecord auto_repair_log_record(
    const std::string& line,
    std::size_t seq,
    const std::optional<std::string>& last_valid_ts,
    RecoveryMetrics& metrics
) {
    std::string sanitized = sanitize_raw_log_line(line);
    auto strict_opt = parse_syslog_format(sanitized, seq);
    if (strict_opt.has_value() && strict_opt->parse_status == ParseStatus::PARSED) {
        return *strict_opt;
    }

    auto simple_rec = parse_simple_format(sanitized, seq);
    if (simple_rec.parse_status == ParseStatus::PARSED) {
        return simple_rec;
    }

    std::ostringstream oss;
    oss << "REP-" << std::setw(5) << std::setfill('0') << seq;

    LogRecord repaired;
    repaired.event_id = oss.str();
    repaired.raw_message = sanitized;
    repaired.source = "auto-repair-engine";

    std::vector<std::string> notes;

    static const std::regex ts_regex(R"((\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:\.\d+)?))");
    std::smatch ts_match;
    if (std::regex_search(sanitized, ts_match, ts_regex)) {
        repaired.timestamp_str = ts_match[1].str();
        repaired.timestamp = parse_timestamp(repaired.timestamp_str);
    } else if (last_valid_ts.has_value() && !last_valid_ts->empty()) {
        repaired.timestamp_str = *last_valid_ts;
        repaired.timestamp = parse_timestamp(repaired.timestamp_str);
        metrics.timestamps_imputed++;
        notes.push_back("Timestamp imputed from chronological sequence");
    } else {
        auto now = std::chrono::system_clock::now();
        repaired.timestamp_str = format_timestamp(now);
        repaired.timestamp = now;
        metrics.timestamps_imputed++;
        notes.push_back("Timestamp synthesized from system baseline");
    }

    Level detected_lvl = detect_level_from_text(sanitized);
    if (detected_lvl != Level::INFO && detected_lvl != Level::UNKNOWN) {
        repaired.level = detected_lvl;
        metrics.levels_inferred++;
        notes.push_back("Severity restored from semantic message tokens");
    } else {
        std::string low = sanitized;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return std::tolower(c); });
        if (low.find("fail") != std::string::npos || low.find("refused") != std::string::npos || low.find("timeout") != std::string::npos || low.find("timed out") != std::string::npos || low.find("error") != std::string::npos) {
            repaired.level = Level::ERROR;
            metrics.levels_inferred++;
            notes.push_back("Severity inferred as ERROR from failure keywords");
        } else if (low.find("warn") != std::string::npos || low.find("threshold") != std::string::npos || low.find("slow") != std::string::npos) {
            repaired.level = Level::WARNING;
            metrics.levels_inferred++;
            notes.push_back("Severity inferred as WARNING from degradation keywords");
        } else {
            repaired.level = Level::INFO;
        }
    }

    std::string clean = clean_message(sanitized);
    repaired.message = clean.empty() ? sanitized : clean;
    repaired.category = classify_category(repaired.message);
    repaired.parse_status = ParseStatus::REPAIRED;

    std::ostringstream note_oss;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        note_oss << notes[i];
        if (i + 1 < notes.size()) note_oss << "; ";
    }
    repaired.repair_note = note_oss.str().empty() ? "Malformed record normalized" : note_oss.str();
    metrics.malformed_sanitized++;

    return repaired;
}

std::vector<LogRecord> apply_sliding_ring_buffer(std::vector<LogRecord> records, std::size_t max_capacity) {
    if (records.size() <= max_capacity) {
        records.shrink_to_fit();
        return records;
    }

    std::size_t offset = records.size() - max_capacity;
    std::vector<LogRecord> capped(
        std::make_move_iterator(records.begin() + offset),
        std::make_move_iterator(records.end())
    );

    capped.shrink_to_fit();
    return capped;
}
