#include "normalizer.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <unordered_map>

Level normalize_level(const std::string& raw) {
    std::string s = raw;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });

    static const std::unordered_map<std::string, Level> map = {
        {"INFO", Level::INFO}, {"INFORMATIONAL", Level::INFO}, {"DEFAULT", Level::INFO}, {"NOTICE", Level::INFO},
        {"WARN", Level::WARNING}, {"WARNING", Level::WARNING},
        {"ERR", Level::ERROR}, {"ERROR", Level::ERROR},
        {"CRIT", Level::CRITICAL}, {"CRITICAL", Level::CRITICAL}, {"FATAL", Level::CRITICAL}, {"FAULT", Level::CRITICAL}, {"EMERGENCY", Level::CRITICAL}
    };
    auto it = map.find(s);
    return (it != map.end()) ? it->second : Level::UNKNOWN;
}

std::string clean_message(const std::string& msg) {
    std::string res;
    bool in_space = false;
    for (char c : msg) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!in_space && !res.empty()) {
                res.push_back(' ');
                in_space = true;
            }
        } else {
            res.push_back(c);
            in_space = false;
        }
    }
    while (!res.empty() && res.back() == ' ') res.pop_back();
    return res.empty() ? "<empty message>" : res;
}

std::optional<std::chrono::system_clock::time_point> parse_timestamp(const std::string& ts_str) {
    std::string s = ts_str;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    if (s.empty()) return std::nullopt;

    auto plus_pos = s.find('+');
    if (plus_pos != std::string::npos) s = s.substr(0, plus_pos);

    int hyphen_count = 0;
    for (char c : s) if (c == '-') hyphen_count++;
    if (hyphen_count > 2) {
        auto last_hyphen = s.rfind('-');
        if (last_hyphen != std::string::npos) s = s.substr(0, last_hyphen);
    }

    auto dot_pos = s.find('.');
    if (dot_pos != std::string::npos) s = s.substr(0, dot_pos);

    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();

    std::tm tm_buf{};
    std::string formats[] = {"%Y-%m-%d %H:%M:%S", "%Y-%m-%dT%H:%M:%S"};
    for (const auto& fmt : formats) {
        std::istringstream ss(s);
        ss >> std::get_time(&tm_buf, fmt.c_str());
        if (!ss.fail()) {
            std::time_t t = std::mktime(&tm_buf);
            if (t != static_cast<std::time_t>(-1)) {
                return std::chrono::system_clock::from_time_t(t);
            }
        }
    }
    return std::nullopt;
}

std::string format_timestamp(std::chrono::system_clock::time_point tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buf);
}

std::string format_time_only(std::chrono::system_clock::time_point tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_buf);
    return std::string(buf);
}
