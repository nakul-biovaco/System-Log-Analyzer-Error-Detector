#pragma once

#include <string>
#include <optional>
#include <cstddef>
#include "models.hpp"

bool is_noise_line(const std::string& line);
Level detect_level_from_text(const std::string& text);
LogRecord parse_simple_format(const std::string& line, std::size_t seq);
std::optional<LogRecord> parse_syslog_format(const std::string& line, std::size_t seq);
std::optional<LogRecord> parse_windows_event_block(const std::vector<std::string>& block, std::size_t seq);
