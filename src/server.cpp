#include "include/server.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "include/models.hpp"
#include "include/ingestion.hpp"
#include "include/parsers.hpp"
#include "include/analyzer.hpp"
#include "include/detection.hpp"
#include "include/alerts.hpp"
#include "include/risk.hpp"
#include "include/recommendations.hpp"
#include "include/recovery.hpp"
#include "include/tests.hpp"

static std::atomic<bool> g_running{false};
static std::thread g_server_thread;
static int g_listen_fd = -1;

static std::string escape_json_str(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) <= 0x1f) {
                    o << "\\u00";
                    char hex[] = "0123456789abcdef";
                    o << hex[(c >> 4) & 0xf] << hex[c & 0xf];
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

static std::string read_file_content(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::string url_decode_str(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int val = 0;
            std::istringstream is(in.substr(i + 1, 2));
            if (is >> std::hex >> val) {
                out.push_back(static_cast<char>(val));
                i += 2;
            } else {
                out.push_back(in[i]);
            }
        } else if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

static std::pair<int, std::string> execute_shell_command(const std::string& cmd) {
    std::string full_cmd = cmd + " 2>&1";
    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) {
        return {1, "Error: Failed to spawn command process\n"};
    }
    std::vector<char> buf(4096);
    std::string result;
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        result += buf.data();
        if (result.size() > 262144) {
            result += "\n[Output truncated at 256KB]\n";
            break;
        }
    }
    int status = pclose(pipe);
    int exit_code = 0;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
    }
    return {exit_code, result};
}

static std::string build_analysis_json(const std::vector<std::string>& raw_lines) {
    std::size_t stitched_count = 0;
    std::vector<std::string> sanitized_lines = stitch_multiline_tracebacks(raw_lines, stitched_count);
    std::vector<LogRecord> records;
    records.reserve(sanitized_lines.size());
    std::size_t noise = 0;
    RecoveryMetrics recovery_metrics;
    recovery_metrics.tracebacks_stitched = stitched_count;

    std::optional<std::string> previous_valid_timestamp;

    for (std::size_t i = 0; i < sanitized_lines.size(); ++i) {
        auto rec = parse_syslog_format(sanitized_lines[i], i + 1);
        if (rec.has_value()) {
            if (!rec->timestamp_str.empty() && rec->timestamp_str != "N/A") {
                previous_valid_timestamp = rec->timestamp_str;
            }
            records.push_back(std::move(*rec));
        } else {
            auto fallback = parse_simple_format(sanitized_lines[i], i + 1);
            if (fallback.parse_status != ParseStatus::INVALID) {
                if (!fallback.timestamp_str.empty() && fallback.timestamp_str != "N/A") {
                    previous_valid_timestamp = fallback.timestamp_str;
                }
                records.push_back(std::move(fallback));
            } else {
                auto repaired = auto_repair_log_record(sanitized_lines[i], i + 1, previous_valid_timestamp, recovery_metrics);
                if (!repaired.timestamp_str.empty() && repaired.timestamp_str != "N/A") {
                    previous_valid_timestamp = repaired.timestamp_str;
                }
                records.push_back(std::move(repaired));
            }
        }
    }

    AnalysisStats stats = compute_statistics(records, raw_lines.size(), noise);
    stats.repaired_events = recovery_metrics.malformed_sanitized;
    stats.timestamps_imputed = recovery_metrics.timestamps_imputed;
    stats.levels_inferred = recovery_metrics.levels_inferred;
    stats.tracebacks_stitched = recovery_metrics.tracebacks_stitched;

    auto detections = run_detection(stats);
    auto alerts = build_alerts(detections);
    auto risk = calculate_risk_score(stats);
    auto recs = generate_recommendations(stats, detections);

    records = apply_sliding_ring_buffer(records, 5000);
    stats.active_working_set_capped = records.size();

    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"total_records\": " << stats.total_events << ",\n";
    ss << "  \"parsed_records\": " << stats.fully_parsed_events << ",\n";
    ss << "  \"fallback_records\": " << stats.partial_events << ",\n";
    ss << "  \"repaired_records\": " << stats.repaired_events << ",\n";
    ss << "  \"timestamps_imputed\": " << stats.timestamps_imputed << ",\n";
    ss << "  \"levels_inferred\": " << stats.levels_inferred << ",\n";
    ss << "  \"tracebacks_stitched\": " << stats.tracebacks_stitched << ",\n";
    ss << "  \"active_working_set_capped\": " << stats.active_working_set_capped << ",\n";
    ss << "  \"noise_records_discarded\": " << noise << ",\n";
    ss << "  \"risk_score\": " << risk.score << ",\n";
    ss << "  \"risk_band\": \"" << escape_json_str(risk.band) << "\",\n";
    ss << "  \"risk_desc\": \"" << escape_json_str(risk.disclaimer) << "\",\n";

    ss << "  \"severity_breakdown\": {\n";
    ss << "    \"CRITICAL\": " << stats.severity_distribution[Level::CRITICAL] << ",\n";
    ss << "    \"ERROR\": " << stats.severity_distribution[Level::ERROR] << ",\n";
    ss << "    \"WARNING\": " << stats.severity_distribution[Level::WARNING] << ",\n";
    ss << "    \"INFO\": " << stats.severity_distribution[Level::INFO] << ",\n";
    ss << "    \"DEBUG\": " << stats.severity_distribution[Level::UNKNOWN] << "\n";
    ss << "  },\n";

    ss << "  \"subsystem_breakdown\": {\n";
    ss << "    \"ALL\": " << stats.total_events << ",\n";
    ss << "    \"SYSTEM\": " << stats.category_summary[Category::SYSTEM] << ",\n";
    ss << "    \"NETWORK\": " << stats.category_summary[Category::NETWORK] << ",\n";
    ss << "    \"SECURITY\": " << stats.category_summary[Category::SECURITY] << ",\n";
    ss << "    \"RESOURCE\": " << stats.category_summary[Category::RESOURCE] << ",\n";
    ss << "    \"FILE\": " << stats.category_summary[Category::FILE] << ",\n";
    ss << "    \"PROCESS\": " << stats.category_summary[Category::PROCESS] << "\n";
    ss << "  },\n";

    ss << "  \"top_errors\": [\n";
    for (std::size_t i = 0; i < stats.top_errors.size(); ++i) {
        ss << "    {\"count\": " << stats.top_errors[i].second
           << ", \"msg\": \"" << escape_json_str(stats.top_errors[i].first) << "\"}";
        if (i + 1 < stats.top_errors.size()) ss << ",";
        ss << "\n";
    }
    ss << "  ],\n";

    ss << "  \"rules\": [\n";
    for (std::size_t i = 0; i < detections.size(); ++i) {
        ss << "    {\"id\": \"" << escape_json_str(detections[i].rule_id) << "\""
           << ", \"name\": \"" << escape_json_str(detections[i].name) << "\""
           << ", \"severity\": \"" << level_to_string(detections[i].severity) << "\""
           << ", \"active\": " << (detections[i].triggered ? "true" : "false")
           << ", \"detail\": \"" << escape_json_str(detections[i].evidence) << "\"}";
        if (i + 1 < detections.size()) ss << ",";
        ss << "\n";
    }
    ss << "  ],\n";

    ss << "  \"recommendations\": [\n";
    for (std::size_t i = 0; i < recs.size(); ++i) {
        ss << "    \"" << escape_json_str(recs[i]) << "\"";
        if (i + 1 < recs.size()) ss << ",";
        ss << "\n";
    }
    ss << "  ],\n";

    ss << "  \"records\": [\n";
    std::size_t max_out = std::min(records.size(), static_cast<std::size_t>(5000));
    for (std::size_t i = 0; i < max_out; ++i) {
        const auto& r = records[i];
        ss << "    {\"seq\": " << (i + 1)
           << ", \"timestamp\": \"" << escape_json_str(r.timestamp_str) << "\""
           << ", \"severity\": \"" << level_to_string(r.level) << "\""
           << ", \"category\": \"" << category_to_string(r.category) << "\""
           << ", \"message\": \"" << escape_json_str(r.message) << "\""
           << ", \"raw_line\": \"" << escape_json_str(r.raw_message) << "\""
           << ", \"is_fallback\": " << (r.parse_status == ParseStatus::PARTIAL ? "true" : "false")
           << ", \"is_repaired\": " << (r.parse_status == ParseStatus::REPAIRED ? "true" : "false")
           << ", \"repair_note\": \"" << escape_json_str(r.repair_note) << "\""
           << "}";
        if (i + 1 < max_out) ss << ",";
        ss << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";

    return ss.str();
}

static void handle_client(int client_fd) {
    std::vector<char> buffer(65536, 0);
    ssize_t bytes_read = recv(client_fd, buffer.data(), buffer.size() - 1, 0);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }

    std::string request(buffer.data(), static_cast<std::size_t>(bytes_read));
    std::istringstream req_stream(request);
    std::string method, path, version;
    req_stream >> method >> path >> version;

    std::string response_body;
    std::string content_type = "text/plain";
    int status_code = 200;

    if (path == "/" || path == "/index.html") {
        response_body = read_file_content("gui/index.html");
        content_type = "text/html; charset=UTF-8";
    } else if (path == "/style.css") {
        response_body = read_file_content("gui/style.css");
        content_type = "text/css; charset=UTF-8";
    } else if (path == "/app.js") {
        response_body = read_file_content("gui/app.js");
        content_type = "application/javascript; charset=UTF-8";
    } else if (path.rfind("/api/status", 0) == 0) {
        std::string os = detect_platform();
        response_body = "{\"status\":\"ok\",\"platform\":\"" + os + "\",\"version\":\"2.4.0\",\"engine\":\"Standard C++17 Core Engine\"}";
        content_type = "application/json; charset=UTF-8";
    } else if (path.rfind("/api/historical", 0) == 0) {
        int mins = 5;
        auto pos = path.find("mins=");
        if (pos != std::string::npos) {
            try {
                mins = std::stoi(path.substr(pos + 5));
                if (mins <= 0) mins = 5;
            } catch (...) {}
        }
        std::string os = detect_platform();
        std::vector<std::string> lines;
        if (os == "macos") {
            lines = get_macos_historical(mins);
        } else if (os == "linux") {
            lines = get_linux_logs(500);
        } else if (os == "windows") {
            lines = get_windows_event_logs("System", 500);
        } else {
            lines = get_sample_logs();
        }
        response_body = build_analysis_json(lines);
        content_type = "application/json; charset=UTF-8";
    } else if (path.rfind("/api/stream", 0) == 0) {
        int secs = 5;
        auto pos = path.find("secs=");
        if (pos != std::string::npos) {
            try {
                secs = std::stoi(path.substr(pos + 5));
                if (secs <= 0) secs = 5;
            } catch (...) {}
        }
        std::string os = detect_platform();
        std::vector<std::string> lines;
        if (os == "macos") {
            lines = get_macos_live_stream(secs);
        } else if (os == "linux") {
            lines = get_linux_logs(300);
        } else {
            lines = get_sample_logs();
        }
        response_body = build_analysis_json(lines);
        content_type = "application/json; charset=UTF-8";
    } else if (path.rfind("/api/tests", 0) == 0) {
        bool ok = run_all_tests();
        response_body = "{\"status\":\"" + std::string(ok ? "passed" : "failed") + "\",\"total_tests\":16,\"passed_tests\":" + std::string(ok ? "16" : "0") + "}";
        content_type = "application/json; charset=UTF-8";
    } else if (path.rfind("/api/terminal/info", 0) == 0) {
        const char* u = std::getenv("USER");
        std::string user = u ? u : "user";
        char host[256];
        gethostname(host, sizeof(host));
        char cwd_buf[1024];
        std::string cwd = "System Log Analyzer & Error Detector";
        if (getcwd(cwd_buf, sizeof(cwd_buf))) {
            std::string full_cwd = cwd_buf;
            auto last_slash = full_cwd.find_last_of("/\\");
            if (last_slash != std::string::npos && last_slash + 1 < full_cwd.size()) {
                cwd = full_cwd.substr(last_slash + 1);
            }
        }
        std::ostringstream ss;
        ss << "{\n"
           << "  \"user\": \"" << escape_json_str(user) << "\",\n"
           << "  \"host\": \"" << escape_json_str(host) << "\",\n"
           << "  \"shell\": \"/bin/zsh\",\n"
           << "  \"cwd\": \"" << escape_json_str(cwd) << "\"\n"
           << "}";
        response_body = ss.str();
        content_type = "application/json; charset=UTF-8";
    } else if (path.rfind("/api/terminal/exec", 0) == 0) {
        std::string cmd;
        auto pos = path.find("cmd=");
        if (pos != std::string::npos) {
            cmd = url_decode_str(path.substr(pos + 4));
        } else if (method == "POST") {
            auto body_pos = request.find("\r\n\r\n");
            if (body_pos != std::string::npos) {
                std::string body = request.substr(body_pos + 4);
                auto cmd_tag = body.find("\"cmd\":");
                if (cmd_tag != std::string::npos) {
                    auto start_q = body.find("\"", cmd_tag + 6);
                    auto end_q = body.find("\"", start_q + 1);
                    if (start_q != std::string::npos && end_q != std::string::npos) {
                        cmd = body.substr(start_q + 1, end_q - start_q - 1);
                    }
                } else {
                    cmd = body;
                }
            }
        }
        if (cmd.empty()) {
            cmd = "echo No command specified";
        }
        auto [exit_code, output] = execute_shell_command(cmd);
        std::ostringstream ss;
        ss << "{\n"
           << "  \"command\": \"" << escape_json_str(cmd) << "\",\n"
           << "  \"exit_code\": " << exit_code << ",\n"
           << "  \"output\": \"" << escape_json_str(output) << "\"\n"
           << "}";
        response_body = ss.str();
        content_type = "application/json; charset=UTF-8";
    } else {
        status_code = 404;
        response_body = "Not Found";
    }

    std::ostringstream resp;
    resp << "HTTP/1.1 " << status_code << " " << (status_code == 200 ? "OK" : "Not Found") << "\r\n";
    resp << "Content-Type: " << content_type << "\r\n";
    resp << "Content-Length: " << response_body.size() << "\r\n";
    resp << "Access-Control-Allow-Origin: *\r\n";
    resp << "Connection: close\r\n\r\n";
    resp << response_body;

    std::string full_response = resp.str();
    send(client_fd, full_response.c_str(), full_response.size(), 0);
    close(client_fd);
}

static void server_loop(int port) {
    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) return;

    int opt = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(g_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
        return;
    }

    if (listen(g_listen_fd, 10) < 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
        return;
    }

    g_running = true;

    while (g_running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(g_listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (!g_running) break;
            continue;
        }
        std::thread(handle_client, client_fd).detach();
    }

    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
}

void start_gui_server(int port) {
    if (g_running) return;
    g_server_thread = std::thread(server_loop, port);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

void stop_gui_server() {
    if (!g_running) return;
    g_running = false;
    if (g_listen_fd >= 0) {
        shutdown(g_listen_fd, SHUT_RDWR);
        close(g_listen_fd);
        g_listen_fd = -1;
    }
    if (g_server_thread.joinable()) {
        g_server_thread.join();
    }
}

void launch_desktop_gui() {
    start_gui_server(8765);
    std::string os = detect_platform();

    if (os == "macos") {
        struct stat st;
        if (stat("bin/desktop_app", &st) != 0) {
            std::system("mkdir -p bin && clang++ -std=c++17 -O2 -framework Cocoa -framework WebKit src/desktop_window.mm -o bin/desktop_app 2>/dev/null");
        }
        std::cout << "Launching native macOS desktop application window...\n";
        int res = std::system("./bin/desktop_app http://127.0.0.1:8765");
        (void)res;
    } else if (os == "linux") {
        int res = std::system("google-chrome --app=http://127.0.0.1:8765 2>/dev/null || chromium --app=http://127.0.0.1:8765 2>/dev/null");
        (void)res;
    } else if (os == "windows") {
        int res = std::system("start msedge --app=http://127.0.0.1:8765");
        (void)res;
    }

    stop_gui_server();
}
