#pragma once

#include <string>
#include <vector>

std::string detect_platform();
std::vector<std::string> get_sample_logs();
std::vector<std::string> get_manual_logs();
std::vector<std::string> get_file_logs(const std::string& filepath);
std::vector<std::string> get_platform_logs(int count_or_minutes = 5);
std::vector<std::string> get_macos_historical(int minutes = 5);
std::vector<std::string> get_macos_live_stream(int seconds = 5);
std::vector<std::string> get_linux_logs(int lines = 500);
std::vector<std::string> get_windows_event_logs(const std::string& channel = "System", int count = 500);
