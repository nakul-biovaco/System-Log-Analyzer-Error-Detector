#pragma once

#include <string>
#include <optional>
#include <chrono>
#include "models.hpp"

Level normalize_level(const std::string& raw);
std::string clean_message(const std::string& msg);
std::optional<std::chrono::system_clock::time_point> parse_timestamp(const std::string& ts_str);
std::string format_timestamp(std::chrono::system_clock::time_point tp);
std::string format_time_only(std::chrono::system_clock::time_point tp);
