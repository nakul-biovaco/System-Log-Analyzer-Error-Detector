#include "parsers.hpp"
#include <regex>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include "rules.hpp"
#include "normalizer.hpp"
#include "classifier.hpp"

bool is_noise_line(const std::string& line) {
    for (const auto& item : get_noise_denylist()) {
        if (line.find(item) != std::string::npos) {
            return true;
        }
    }
    return false;
}

Level detect_level_from_text(const std::string& text) {
    std::string low = text;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return std::tolower(c); });
    if (low.find("fault") != std::string::npos || low.find("[fault]") != std::string::npos || low.find("critical") != std::string::npos) {
        return Level::CRITICAL;
    }
    if (low.find("error") != std::string::npos || low.find("[error]") != std::string::npos || low.find("fail") != std::string::npos) {
        return Level::ERROR;
    }
    if (low.find("warning") != std::string::npos || low.find("[warning]") != std::string::npos || low.find("notice") != std::string::npos) {
        return Level::WARNING;
    }
    return Level::INFO;
}

static std::string format_seq_id(const std::string& prefix, std::size_t seq) {
    std::ostringstream oss;
    oss << prefix << "-" << std::setw(5) << std::setfill('0') << seq;
    return oss.str();
}

LogRecord parse_simple_format(const std::string& line, std::size_t seq) {
    std::string raw = line;
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) raw.erase(raw.begin());
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.pop_back();

    std::string eid = format_seq_id("EVT", seq);

    static const std::regex simple_re(R"(^(\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:\.\d+)?)\s+(?:\[([A-Za-z]+)\]|([A-Za-z]+))\s+(.*)$)");
    std::smatch m;
    if (std::regex_match(raw, m, simple_re)) {
        std::string ts_str = m[1].str();
        std::string l1 = m[2].str();
        std::string l2 = m[3].str();
        std::string msg = m[4].str();
        std::string raw_lvl = l1.empty() ? l2 : l1;
        std::string cmsg = clean_message(msg);

        LogRecord rec;
        rec.event_id = eid;
        rec.timestamp = parse_timestamp(ts_str);
        rec.timestamp_str = ts_str;
        rec.level = normalize_level(raw_lvl);
        rec.message = cmsg;
        rec.category = classify_category(cmsg);
        rec.raw_message = raw;
        rec.parse_status = ParseStatus::PARSED;
        rec.source = "standard";
        return rec;
    }

    LogRecord rec;
    rec.event_id = eid;
    rec.timestamp = std::nullopt;
    rec.timestamp_str = "N/A";
    rec.level = Level::UNKNOWN;
    rec.message = clean_message(raw);
    rec.category = classify_category(raw);
    rec.raw_message = raw;
    rec.parse_status = ParseStatus::INVALID;
    rec.source = "standard";
    return rec;
}

std::optional<LogRecord> parse_syslog_format(const std::string& line, std::size_t seq) {
    std::string raw = line;
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) raw.erase(raw.begin());
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.pop_back();

    if (raw.empty()) return std::nullopt;
    if (raw.rfind("Timestamp", 0) == 0 && raw.find("process") != std::string::npos) return std::nullopt;
    if (is_noise_line(raw)) return std::nullopt;

    std::string eid = format_seq_id("SYS", seq);

    static const std::regex syslog_re(R"(^(\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:[+-]\d{4})?)\s+([^\s]+)\s+([^:]+):\s*(.*)$)");
    std::smatch m;
    if (std::regex_match(raw, m, syslog_re)) {
        std::string ts_str = m[1].str();
        std::string host = m[2].str();
        std::string sender = m[3].str();
        std::string remainder = m[4].str();
        std::string cmsg = clean_message("[" + sender + "] " + remainder);

        LogRecord rec;
        rec.event_id = eid;
        rec.timestamp = parse_timestamp(ts_str);
        rec.timestamp_str = ts_str;
        rec.level = detect_level_from_text(remainder);
        rec.message = cmsg;
        rec.category = classify_category(cmsg);
        rec.raw_message = raw;
        rec.parse_status = ParseStatus::PARSED;
        rec.source = "syslog (" + host + ")";
        return rec;
    }

    static const std::regex fallback_ts_re(R"(^(\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:[+-]\d{4})?)\s+(.*)$)");
    std::smatch fm;
    if (std::regex_match(raw, fm, fallback_ts_re)) {
        std::string ts_str = fm[1].str();
        std::string remainder = fm[2].str();
        std::string cmsg = clean_message(remainder);

        LogRecord rec;
        rec.event_id = eid;
        rec.timestamp = parse_timestamp(ts_str);
        rec.timestamp_str = ts_str;
        rec.level = detect_level_from_text(remainder);
        rec.message = cmsg;
        rec.category = classify_category(cmsg);
        rec.raw_message = raw;
        rec.parse_status = ParseStatus::PARTIAL;
        rec.source = "syslog";
        return rec;
    }

    std::string cmsg = clean_message(raw);
    if (!cmsg.empty() && cmsg != "<empty message>") {
        LogRecord rec;
        rec.event_id = eid;
        rec.timestamp = std::nullopt;
        rec.timestamp_str = "N/A";
        rec.level = detect_level_from_text(cmsg);
        rec.message = cmsg;
        rec.category = classify_category(cmsg);
        rec.raw_message = raw;
        rec.parse_status = ParseStatus::PARTIAL;
        rec.source = "syslog (continuation)";
        return rec;
    }

    LogRecord rec;
    rec.event_id = eid;
    rec.timestamp = std::nullopt;
    rec.timestamp_str = "N/A";
    rec.level = Level::UNKNOWN;
    rec.message = cmsg;
    rec.category = classify_category(raw);
    rec.raw_message = raw;
    rec.parse_status = ParseStatus::INVALID;
    rec.source = "syslog";
    return rec;
}

std::optional<LogRecord> parse_windows_event_block(const std::vector<std::string>& block, std::size_t seq) {
    if (block.empty()) return std::nullopt;

    std::string eid = format_seq_id("WIN", seq);
    std::string timestamp_str;
    std::string level_str;
    std::string source_str;
    std::string description;
    std::string event_id_str;

    for (const auto& line : block) {
        std::string trimmed = line;
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();

        auto colon_pos = trimmed.find(':');
        if (colon_pos == std::string::npos) continue;

        std::string key = trimmed.substr(0, colon_pos);
        std::string val = trimmed.substr(colon_pos + 1);
        while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
        while (!val.empty() && std::isspace(static_cast<unsigned char>(val.front()))) val.erase(val.begin());

        if (key == "Date" || key == "TimeCreated") timestamp_str = val;
        else if (key == "Level") level_str = val;
        else if (key == "Source" || key == "ProviderName") source_str = val;
        else if (key == "Description" || key == "Message") description = val;
        else if (key == "Event ID" || key == "Id") event_id_str = val;
    }

    if (timestamp_str.empty() && description.empty()) return std::nullopt;

    std::string msg_body = description;
    if (!source_str.empty() && !msg_body.empty()) {
        msg_body = "[" + source_str + "] " + msg_body;
    } else if (msg_body.empty() && !source_str.empty()) {
        msg_body = "[" + source_str + "] Event ID " + event_id_str;
    } else if (msg_body.empty()) {
        msg_body = "Event ID " + event_id_str;
    }

    Level lvl = Level::INFO;
    if (!level_str.empty()) {
        std::string low_lvl = level_str;
        std::transform(low_lvl.begin(), low_lvl.end(), low_lvl.begin(), [](unsigned char c) { return std::tolower(c); });
        if (low_lvl.find("critical") != std::string::npos) lvl = Level::CRITICAL;
        else if (low_lvl.find("error") != std::string::npos) lvl = Level::ERROR;
        else if (low_lvl.find("warning") != std::string::npos) lvl = Level::WARNING;
        else if (low_lvl.find("information") != std::string::npos) lvl = Level::INFO;
        else lvl = detect_level_from_text(description);
    } else {
        lvl = detect_level_from_text(msg_body);
    }

    std::string cmsg = clean_message(msg_body);

    LogRecord rec;
    rec.event_id = eid;
    rec.timestamp = parse_timestamp(timestamp_str);
    rec.timestamp_str = timestamp_str.empty() ? "N/A" : timestamp_str;
    rec.level = lvl;
    rec.message = cmsg;
    rec.category = classify_category(cmsg);
    rec.raw_message = "";
    for (const auto& l : block) rec.raw_message += l + " ";
    rec.parse_status = (rec.timestamp.has_value() && !description.empty()) ? ParseStatus::PARSED : ParseStatus::PARTIAL;
    rec.source = "windows-event";
    return rec;
}
