#include "ingestion.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <memory>
#include <array>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <cctype>

#if defined(_WIN32)
#define PLATFORM_POPEN _popen
#define PLATFORM_PCLOSE _pclose
#else
#define PLATFORM_POPEN popen
#define PLATFORM_PCLOSE pclose
#endif

#if defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#endif

static const std::vector<std::string> BUILTIN_SAMPLES = {
    "2026-09-05 10:00:01 [INFO] System kernel initialization completed on host-alpha",
    "2026-09-05 10:00:05 [INFO] User alice authenticated successfully via ssh publickey",
    "2026-09-05 10:01:12 [WARNING] Storage space warning: volume /var at 87% utilization",
    "2026-09-05 10:02:15 [ERROR] Connection refused to database primary on port 5432",
    "2026-09-05 10:02:18 [ERROR] Connection refused to database primary on port 5432",
    "2026-09-05 10:02:22 [ERROR] Connection refused to database primary on port 5432",
    "2026-09-05 10:03:40 [WARNING] Resource threshold exceeded: process worker-3 memory at 92%",
    "2026-09-05 10:04:10 [ERROR] Authentication failure: invalid credentials for user admin",
    "2026-09-05 10:04:55 [ERROR] File read error: configuration path /etc/app/config.json not found",
    "2026-09-05 10:05:01 [CRITICAL] Kernel out of memory: terminated process 4102 (worker)",
    "2026-09-05 10:06:14 [INFO] Network interface en0 reconnected to default gateway",
    "2026-09-05 10:07:00 [ERROR] Network timeout after 10000ms while polling payment gateway",
    "CORRUPT_RECORD_MISSING_HEADER_AND_TIMESTAMP",
    "2026-09-05 10:08:30 INCOMPLETE_RECORD_NO_LEVEL_DELIMITERS"
};

std::string detect_platform() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

std::vector<std::string> get_sample_logs() {
    return BUILTIN_SAMPLES;
}

std::vector<std::string> get_manual_logs() {
    std::cout << "\nEnter log lines below (press Enter on an empty line to finish):\n";
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) break;
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> get_file_logs(const std::string& filepath) {
    std::string p = filepath;
    while (!p.empty() && (p.front() == '\'' || p.front() == '"')) p.erase(p.begin());
    while (!p.empty() && (p.back() == '\'' || p.back() == '"')) p.pop_back();

    std::vector<std::string> lines;
    std::ifstream file(p);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open log file at '" << p << "'\n";
        return lines;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) lines.push_back(line);
        }
    }
    return lines;
}

static std::vector<std::string> execute_command_lines(const std::string& cmd) {
    std::vector<std::string> lines;
    struct PipeCloser {
        void operator()(FILE* fp) const { if (fp) PLATFORM_PCLOSE(fp); }
    };
    std::unique_ptr<FILE, PipeCloser> pipe_handle(PLATFORM_POPEN(cmd.c_str(), "r"));
    if (!pipe_handle) return lines;

    std::array<char, 4096> buffer;
    std::string accumulator;
    while (std::fgets(buffer.data(), buffer.size(), pipe_handle.get()) != nullptr) {
        accumulator.append(buffer.data());
        std::size_t pos;
        while ((pos = accumulator.find('\n')) != std::string::npos) {
            std::string line = accumulator.substr(0, pos);
            accumulator.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) lines.push_back(line);
        }
    }
    if (!accumulator.empty()) {
        if (!accumulator.empty() && accumulator.back() == '\r') accumulator.pop_back();
        if (!accumulator.empty()) lines.push_back(accumulator);
    }
    return lines;
}

std::vector<std::string> get_macos_historical(int minutes) {
    std::string cmd = "/usr/bin/log show --last " + std::to_string(minutes) + "m --style syslog 2>/dev/null";
    std::cout << "Querying persistent unified log trace: " << cmd << "...\n";
    return execute_command_lines(cmd);
}

std::vector<std::string> get_macos_live_stream(int seconds) {
    std::cout << "Attaching to real-time kernel logging stream for " << seconds << "s...\n";
    std::vector<std::string> lines;
#if defined(__APPLE__)
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        std::cerr << "Error: pipe creation failed.\n";
        return lines;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        std::cerr << "Error: fork failed.\n";
        return lines;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        close(pipefd[1]);
        execlp("/usr/bin/log", "log", "stream", "--style", "syslog", nullptr);
        _exit(1);
    }

    close(pipefd[1]);

    auto start_time = std::chrono::steady_clock::now();
    int timeout_ms = seconds * 1000;
    std::array<char, 4096> buffer;
    std::string accumulator;

    while (true) {
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time
        ).count();
        if (elapsed_ms >= timeout_ms) break;
        int remaining_ms = static_cast<int>(timeout_ms - elapsed_ms);

        struct pollfd pfd;
        pfd.fd = pipefd[0];
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret = poll(&pfd, 1, remaining_ms > 100 ? 100 : remaining_ms);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t bytes_read = read(pipefd[0], buffer.data(), buffer.size());
            if (bytes_read <= 0) break;
            accumulator.append(buffer.data(), static_cast<std::size_t>(bytes_read));
            std::size_t pos;
            while ((pos = accumulator.find('\n')) != std::string::npos) {
                std::string line = accumulator.substr(0, pos);
                accumulator.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) lines.push_back(line);
            }
        } else if (ret == 0) {
            continue;
        } else {
            if (errno == EINTR) continue;
            break;
        }
    }

    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);

    while (true) {
        ssize_t bytes_read = read(pipefd[0], buffer.data(), buffer.size());
        if (bytes_read <= 0) break;
        accumulator.append(buffer.data(), static_cast<std::size_t>(bytes_read));
        std::size_t pos;
        while ((pos = accumulator.find('\n')) != std::string::npos) {
            std::string line = accumulator.substr(0, pos);
            accumulator.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) lines.push_back(line);
        }
    }
    if (!accumulator.empty()) {
        if (!accumulator.empty() && accumulator.back() == '\r') accumulator.pop_back();
        if (!accumulator.empty()) lines.push_back(accumulator);
    }
    close(pipefd[0]);
#else
    std::cerr << "Error: Live kernel log stream is only available on macOS.\n";
#endif
    return lines;
}

std::vector<std::string> get_linux_logs(int count) {
#if defined(__linux__)
    std::string cmd = "journalctl -n " + std::to_string(count) + " --no-pager -o short-iso 2>/dev/null";
    std::cout << "Querying systemd journal: " << cmd << "...\n";
    auto res = execute_command_lines(cmd);
    if (!res.empty()) return res;

    for (const auto& fallback : {"/var/log/syslog", "/var/log/messages"}) {
        if (std::filesystem::exists(fallback)) {
            std::cout << "Falling back to '" << fallback << "'...\n";
            return get_file_logs(fallback);
        }
    }
    std::cerr << "Error: Neither journalctl nor standard syslog files were accessible.\n";
#else
    std::cerr << "Error: journalctl is only available on Linux.\n";
    (void)count;
#endif
    return {};
}

std::vector<std::string> get_windows_event_logs(const std::string& channel, int count) {
#if defined(_WIN32)
    std::string cmd = "wevtutil qe " + channel + " /c:" + std::to_string(count) + " /f:text /rd:true 2>NUL";
    std::cout << "Querying Windows Event Log: " << cmd << "...\n";
    auto lines = execute_command_lines(cmd);
    if (lines.empty()) {
        cmd = "powershell -NoProfile -Command \"Get-EventLog -LogName " + channel + " -Newest " + std::to_string(count) + " | Format-List\" 2>NUL";
        std::cout << "Falling back to PowerShell: " << cmd << "...\n";
        lines = execute_command_lines(cmd);
    }
    return lines;
#else
    std::cerr << "Error: Windows Event Log is only available on Windows.\n";
    (void)channel;
    (void)count;
    return {};
#endif
}

std::vector<std::string> get_platform_logs(int count_or_minutes) {
    std::string os = detect_platform();
    if (os == "macos") return get_macos_historical(count_or_minutes);
    if (os == "linux") return get_linux_logs(count_or_minutes > 100 ? count_or_minutes : 500);
    if (os == "windows") return get_windows_event_logs("System", count_or_minutes > 100 ? count_or_minutes : 500);
    std::cerr << "Error: Unsupported platform for automatic log retrieval.\n";
    return {};
}
