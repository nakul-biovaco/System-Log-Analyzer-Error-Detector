#include "classifier.hpp"
#include <algorithm>
#include <cctype>
#include <vector>

struct CategoryRule {
    std::vector<std::string> keywords;
    Category category;
};

Category classify_category(const std::string& message) {
    std::string text = message;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });

    static const std::vector<CategoryRule> rules = {
        {{"login", "auth", "password", "unauthorized", "permission denied", "ssh", "sudo", "token", "credential"}, Category::SECURITY},
        {{"connection", "timeout", "socket", "port", "gateway", "dns", "wifi", "wlan", "tcp", "udp", "http", "disconnect"}, Category::NETWORK},
        {{"memory", "cpu", "ram", "capacity", "exhaust", "oom", "disk space", "storage full", "quota"}, Category::RESOURCE},
        {{"file", "directory", "folder", "path", "read error", "write error", "not found", "filesystem"}, Category::FILE},
        {{"process", "thread", "worker", "service", "daemon", "killed", "terminated", "spawn"}, Category::PROCESS},
        {{"kernel", "boot", "driver", "hardware", "thermal", "clock", "shutdown", "reboot", "panic"}, Category::SYSTEM}
    };

    for (const auto& rule : rules) {
        for (const auto& kw : rule.keywords) {
            if (text.find(kw) != std::string::npos) {
                return rule.category;
            }
        }
    }
    return Category::SYSTEM;
}
