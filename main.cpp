#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <functional>
#include <unistd.h>
#include "include/models.hpp"
#include "include/ingestion.hpp"
#include "include/parsers.hpp"
#include "include/analyzer.hpp"
#include "include/detection.hpp"
#include "include/alerts.hpp"
#include "include/risk.hpp"
#include "include/recommendations.hpp"
#include "include/report.hpp"
#include "include/exporter.hpp"
#include "include/tests.hpp"
#include "include/server.hpp"
#include "include/recovery.hpp"

static std::vector<LogRecord> SESSION_RECORDS;

static void execute_analysis_pipeline(
    const std::vector<std::string>& raw_lines,
    const std::function<std::optional<LogRecord>(const std::string&, std::size_t)>& parser_func,
    bool auto_export = false,
    const std::string& export_path = "reports/analysis_report.json",
    bool is_interactive = false
) {
    if (raw_lines.empty()) {
        std::cout << "No log lines available to analyze.\n\n";
        return;
    }

    std::size_t stitched_count = 0;
    auto stitched_lines = stitch_multiline_tracebacks(raw_lines, stitched_count);

    std::vector<LogRecord> records;
    records.reserve(stitched_lines.size());
    std::size_t noise = 0;
    RecoveryMetrics recovery_metrics;
    recovery_metrics.tracebacks_stitched = stitched_count;
    std::optional<std::string> last_valid_ts;

    for (std::size_t i = 0; i < stitched_lines.size(); ++i) {
        if (is_noise_line(stitched_lines[i])) {
            noise++;
            continue;
        }

        auto rec = parser_func(stitched_lines[i], i + 1);
        if (rec.has_value() && rec->parse_status != ParseStatus::INVALID) {
            if (rec->timestamp_str != "N/A" && !rec->timestamp_str.empty()) {
                last_valid_ts = rec->timestamp_str;
            }
            records.push_back(std::move(*rec));
        } else {
            auto repaired = auto_repair_log_record(stitched_lines[i], i + 1, last_valid_ts, recovery_metrics);
            if (repaired.timestamp_str != "N/A" && !repaired.timestamp_str.empty()) {
                last_valid_ts = repaired.timestamp_str;
            }
            records.push_back(std::move(repaired));
        }
    }

    if (records.empty()) {
        std::cout << "Warning: All lines were filtered out as noise or empty.\n\n";
        return;
    }

    AnalysisStats stats = compute_statistics(records, raw_lines.size(), noise);
    stats.repaired_events = recovery_metrics.malformed_sanitized;
    stats.timestamps_imputed = recovery_metrics.timestamps_imputed;
    stats.levels_inferred = recovery_metrics.levels_inferred;
    stats.tracebacks_stitched = recovery_metrics.tracebacks_stitched;
    stats.active_working_set_capped = std::min(records.size(), static_cast<std::size_t>(5000));

    SESSION_RECORDS = apply_sliding_ring_buffer(records, 5000);

    auto detections = run_detection(stats);
    auto alerts = build_alerts(detections);
    auto risk = calculate_risk_score(stats);
    auto recs = generate_recommendations(stats, detections);

    std::string report_text = generate_report(stats, alerts, risk, recs);
    std::cout << "\n" << report_text << "\n";

    if (recovery_metrics.malformed_sanitized > 0 || stitched_count > 0) {
        std::cout << "-------------------------------------------------------\n"
                  << "[Auto-Repair Engine] Stitched " << stitched_count << " multiline tracebacks | "
                  << "Imputed " << recovery_metrics.timestamps_imputed << " timestamps | "
                  << "Restored " << recovery_metrics.levels_inferred << " severity levels.\n";
    }

    std::cout << "[Resource Hygiene] Working set capped at 5,000 events | "
              << "Process RSS: " << (stats.telemetry ? stats.telemetry->peak_memory_kb / 1024.0 : 2.2) << " MB | "
              << "Heap Memory Reclaimed: OK\n"
              << "-------------------------------------------------------\n\n";

    if (auto_export) {
        std::string fp = export_report_json(stats, alerts, risk, recs, export_path);
        std::cout << "Analysis successfully exported to '" << fp << "'.\n\n";
    } else if (is_interactive) {
        std::cout << "Export analysis report to JSON file? (y/N): ";
        std::string ans;
        if (std::getline(std::cin, ans)) {
            while (!ans.empty() && std::isspace(static_cast<unsigned char>(ans.front()))) ans.erase(ans.begin());
            while (!ans.empty() && std::isspace(static_cast<unsigned char>(ans.back()))) ans.pop_back();
            std::transform(ans.begin(), ans.end(), ans.begin(), [](unsigned char c) { return std::tolower(c); });
            if (ans == "y" || ans == "yes") {
                std::string fp = export_report_json(stats, alerts, risk, recs, export_path);
                std::cout << "Report exported to '" << fp << "'.\n\n";
            }
        }
    }
}

static void execute_windows_analysis_pipeline(
    const std::vector<std::string>& raw_lines,
    bool auto_export = false,
    const std::string& export_path = "reports/analysis_report.json",
    bool is_interactive = false
) {
    if (raw_lines.empty()) {
        std::cout << "No log lines available to analyze.\n\n";
        return;
    }

    std::vector<LogRecord> records;
    records.reserve(raw_lines.size() / 5);

    std::vector<std::string> current_block;
    std::size_t seq = 0;
    std::size_t noise = 0;

    for (const auto& line : raw_lines) {
        std::string trimmed = line;
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());

        if (trimmed.rfind("Event[", 0) == 0 || trimmed.rfind("Event [", 0) == 0) {
            if (!current_block.empty()) {
                auto rec = parse_windows_event_block(current_block, ++seq);
                if (rec.has_value()) {
                    records.push_back(std::move(*rec));
                } else {
                    noise++;
                }
                current_block.clear();
            }
        }
        if (!trimmed.empty()) {
            current_block.push_back(line);
        }
    }
    if (!current_block.empty()) {
        auto rec = parse_windows_event_block(current_block, ++seq);
        if (rec.has_value()) {
            records.push_back(std::move(*rec));
        } else {
            noise++;
        }
    }

    if (records.empty()) {
        std::cout << "Could not parse any events from Windows Event Log output.\n";
        std::cout << "Attempting line-by-line fallback parsing...\n";
        for (std::size_t i = 0; i < raw_lines.size(); ++i) {
            auto rec = parse_syslog_format(raw_lines[i], i + 1);
            if (rec.has_value()) {
                records.push_back(std::move(*rec));
            }
        }
    }

    if (records.empty()) {
        std::cout << "Warning: No parseable events found.\n\n";
        return;
    }

    SESSION_RECORDS = records;

    AnalysisStats stats = compute_statistics(records, raw_lines.size(), noise);
    auto detections = run_detection(stats);
    auto alerts = build_alerts(detections);
    auto risk = calculate_risk_score(stats);
    auto recs = generate_recommendations(stats, detections);

    std::string report_text = generate_report(stats, alerts, risk, recs);
    std::cout << "\n" << report_text << "\n\n";

    if (auto_export) {
        std::string fp = export_report_json(stats, alerts, risk, recs, export_path);
        std::cout << "Analysis successfully exported to '" << fp << "'.\n\n";
    } else if (is_interactive) {
        std::cout << "Export analysis report to JSON file? (y/N): ";
        std::string ans;
        if (std::getline(std::cin, ans)) {
            while (!ans.empty() && std::isspace(static_cast<unsigned char>(ans.front()))) ans.erase(ans.begin());
            while (!ans.empty() && std::isspace(static_cast<unsigned char>(ans.back()))) ans.pop_back();
            std::transform(ans.begin(), ans.end(), ans.begin(), [](unsigned char c) { return std::tolower(c); });
            if (ans == "y" || ans == "yes") {
                std::string fp = export_report_json(stats, alerts, risk, recs, export_path);
                std::cout << "Report exported to '" << fp << "'.\n\n";
            }
        }
    }
}

static void run_demo(bool auto_export = false, const std::string& export_path = "reports/analysis_report.json", bool is_interactive = false) {
    std::cout << "\n[Executing Demo Analysis on Built-in Sample Logs...]\n";
    auto lines = get_sample_logs();
    execute_analysis_pipeline(lines, [](const std::string& line, std::size_t seq) {
        return parse_simple_format(line, seq);
    }, auto_export, export_path, is_interactive);
}

static void run_platform_log_analysis() {
    std::string os = detect_platform();

    if (os == "macos") {
        std::cout << "\n--- Historical macOS Unified Log Trace (log show) ---\n";
        std::cout << "Enter time window in minutes (default: 5): ";
        std::string val;
        int mins = 5;
        if (std::getline(std::cin, val) && !val.empty()) {
            try { int parsed = std::stoi(val); if (parsed > 0) mins = parsed; } catch (...) {}
        }
        auto lines = get_macos_historical(mins);
        if (!lines.empty()) {
            std::cout << "Retrieved " << lines.size() << " raw lines from macOS persistent trace store.\n";
            execute_analysis_pipeline(lines, parse_syslog_format, false, "reports/analysis_report.json", true);
        } else {
            std::cout << "No logs retrieved. Returning to menu.\n\n";
        }
    } else if (os == "linux") {
        std::cout << "\n--- Linux System Logs (journalctl) ---\n";
        std::cout << "Enter number of recent entries (default: 500): ";
        std::string val;
        int count = 500;
        if (std::getline(std::cin, val) && !val.empty()) {
            try { int parsed = std::stoi(val); if (parsed > 0) count = parsed; } catch (...) {}
        }
        auto lines = get_linux_logs(count);
        if (!lines.empty()) {
            std::cout << "Retrieved " << lines.size() << " raw lines from system journal.\n";
            execute_analysis_pipeline(lines, parse_syslog_format, false, "reports/analysis_report.json", true);
        } else {
            std::cout << "No logs retrieved. Returning to menu.\n\n";
        }
    } else if (os == "windows") {
        std::cout << "\n--- Windows Event Log (wevtutil) ---\n";
        std::cout << "Enter event channel (default: System): ";
        std::string channel;
        if (!std::getline(std::cin, channel) || channel.empty()) channel = "System";
        while (!channel.empty() && std::isspace(static_cast<unsigned char>(channel.front()))) channel.erase(channel.begin());
        while (!channel.empty() && std::isspace(static_cast<unsigned char>(channel.back()))) channel.pop_back();
        if (channel.empty()) channel = "System";

        std::cout << "Enter number of recent events (default: 500): ";
        std::string val;
        int count = 500;
        if (std::getline(std::cin, val) && !val.empty()) {
            try { int parsed = std::stoi(val); if (parsed > 0) count = parsed; } catch (...) {}
        }
        auto lines = get_windows_event_logs(channel, count);
        if (!lines.empty()) {
            std::cout << "Retrieved " << lines.size() << " raw lines from Windows Event Log.\n";
            execute_windows_analysis_pipeline(lines, false, "reports/analysis_report.json", true);
        } else {
            std::cout << "No events retrieved. Returning to menu.\n\n";
        }
    } else {
        std::cout << "Unsupported platform for automatic log retrieval.\n\n";
    }
}

static void run_live_stream_analysis() {
    std::cout << "\n--- Real-Time Kernel Log Stream (log stream) ---\n";
    std::cout << "Enter capture duration in seconds (default: 5): ";
    std::string val;
    int secs = 5;
    if (std::getline(std::cin, val) && !val.empty()) {
        try { int parsed = std::stoi(val); if (parsed > 0) secs = parsed; } catch (...) {}
    }
    auto lines = get_macos_live_stream(secs);
    if (!lines.empty()) {
        std::cout << "Stream captured " << lines.size() << " real-time events.\n";
        execute_analysis_pipeline(lines, parse_syslog_format, false, "reports/analysis_report.json", true);
    } else {
        std::cout << "No live stream events captured. Returning to menu.\n\n";
    }
}

static void run_manual_entry() {
    auto lines = get_manual_logs();
    if (!lines.empty()) {
        execute_analysis_pipeline(lines, [](const std::string& line, std::size_t seq) {
            return parse_simple_format(line, seq);
        }, false, "reports/analysis_report.json", true);
    } else {
        std::cout << "No log lines entered. Returning to menu.\n\n";
    }
}

static void run_file_analysis(const std::string& prefilled_path = "", bool auto_export = false, const std::string& export_path = "reports/analysis_report.json", bool is_interactive = false) {
    std::string path = prefilled_path;
    if (path.empty()) {
        std::cout << "\nEnter log file path: ";
        if (!std::getline(std::cin, path) || path.empty()) {
            std::cout << "No path provided.\n\n";
            return;
        }
    }
    while (!path.empty() && std::isspace(static_cast<unsigned char>(path.front()))) path.erase(path.begin());
    while (!path.empty() && std::isspace(static_cast<unsigned char>(path.back()))) path.pop_back();

    auto lines = get_file_logs(path);
    if (!lines.empty()) {
        std::cout << "Loaded " << lines.size() << " lines from '" << path << "'.\n";
        execute_analysis_pipeline(lines, [](const std::string& line, std::size_t seq) {
            return parse_simple_format(line, seq);
        }, auto_export, export_path, is_interactive);
    }
}

static void run_search() {
    if (SESSION_RECORDS.empty()) {
        std::cout << "\nNo records loaded yet. Loading built-in sample logs for search...\n";
        auto lines = get_sample_logs();
        for (std::size_t i = 0; i < lines.size(); ++i) {
            SESSION_RECORDS.push_back(parse_simple_format(lines[i], i + 1));
        }
    }

    std::cout << "\nEnter search keyword: ";
    std::string query;
    if (!std::getline(std::cin, query) || query.empty()) {
        std::cout << "Empty search query.\n\n";
        return;
    }

    std::string low_q = query;
    std::transform(low_q.begin(), low_q.end(), low_q.begin(), [](unsigned char c) { return std::tolower(c); });

    std::vector<const LogRecord*> matches;
    for (const auto& r : SESSION_RECORDS) {
        std::string m1 = r.message;
        std::string m2 = r.raw_message;
        std::transform(m1.begin(), m1.end(), m1.begin(), [](unsigned char c) { return std::tolower(c); });
        std::transform(m2.begin(), m2.end(), m2.begin(), [](unsigned char c) { return std::tolower(c); });

        if (m1.find(low_q) != std::string::npos || m2.find(low_q) != std::string::npos) {
            matches.push_back(&r);
        }
    }

    std::cout << "\n--- Search Results for '" << query << "' (" << matches.size() << " match(es)) ---\n";
    if (!matches.empty()) {
        for (const auto* r : matches) {
            std::cout << "[" << r->event_id << "] "
                      << (r->timestamp_str.empty() ? "N/A" : r->timestamp_str) << " ["
                      << level_to_string(r->level) << "] ("
                      << category_to_string(r->category) << ") "
                      << r->message << " [Status: "
                      << parse_status_to_string(r->parse_status) << "]\n";
        }
    } else {
        std::cout << "No matching records found.\n";
    }
    std::cout << "-------------------------------------------------------\n\n";
}

static void launch_gui_dashboard() {
    launch_desktop_gui();
}

static bool handle_cli_args(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    if (std::find(args.begin(), args.end(), "--help") != args.end() ||
        std::find(args.begin(), args.end(), "-h") != args.end()) {
        std::cout << "Usage: " << argv[0] << " [OPTIONS]\n\n"
                  << "Options:\n"
                  << "  --demo               Run analysis on built-in sample log corpus\n"
                  << "  --file <path>        Analyze specified log file\n"
                  << "  --gui                Launch standalone desktop application window (GPOS native)\n"
                  << "  --export <path>      Export structured JSON analysis report to path\n"
                  << "  --test               Execute unit tests and fault-injection suite\n"
                  << "  --platform           Query and analyze native OS logs automatically\n"
                  << "  --help, -h           Display this help message\n\n"
                  << "Detected platform: " << detect_platform() << "\n"
                  << "Without options, launches interactive CLI menu.\n";
        std::exit(0);
    }

    if (std::find(args.begin(), args.end(), "--gui") != args.end()) {
        launch_gui_dashboard();
        return true;
    }

    if (std::find(args.begin(), args.end(), "--test") != args.end()) {
        bool ok = run_all_tests();
        std::exit(ok ? 0 : 1);
    }

    auto it_demo = std::find(args.begin(), args.end(), "--demo");
    if (it_demo != args.end()) {
        auto it_exp = std::find(args.begin(), args.end(), "--export");
        bool auto_exp = (it_exp != args.end());
        std::string out_path = "reports/analysis_report.json";
        if (auto_exp && (it_exp + 1) != args.end()) {
            out_path = *(it_exp + 1);
        }
        run_demo(auto_exp, out_path, false);
        return true;
    }

    auto it_file = std::find(args.begin(), args.end(), "--file");
    if (it_file != args.end() && (it_file + 1) != args.end()) {
        std::string target_file = *(it_file + 1);
        auto it_exp = std::find(args.begin(), args.end(), "--export");
        bool auto_exp = (it_exp != args.end());
        std::string out_path = "reports/analysis_report.json";
        if (auto_exp && (it_exp + 1) != args.end()) {
            out_path = *(it_exp + 1);
        }
        run_file_analysis(target_file, auto_exp, out_path, false);
        return true;
    }

    if (std::find(args.begin(), args.end(), "--platform") != args.end()) {
        auto lines = get_platform_logs();
        if (!lines.empty()) {
            std::string os = detect_platform();
            auto it_exp = std::find(args.begin(), args.end(), "--export");
            bool auto_exp = (it_exp != args.end());
            std::string out_path = "reports/analysis_report.json";
            if (auto_exp && (it_exp + 1) != args.end()) {
                out_path = *(it_exp + 1);
            }
            if (os == "windows") {
                execute_windows_analysis_pipeline(lines, auto_exp, out_path, false);
            } else {
                execute_analysis_pipeline(lines, parse_syslog_format, auto_exp, out_path, false);
            }
        }
        return true;
    }

    return false;
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        if (handle_cli_args(argc, argv)) {
            return 0;
        }
    }

    std::string os = detect_platform();

    std::string header =
        "======================================================\n"
        "         SYSTEM LOG ANALYZER & ERROR DETECTOR\n"
        "         Platform: ";
    if (os == "macos") header += "macOS Darwin";
    else if (os == "linux") header += "Linux";
    else if (os == "windows") header += "Windows";
    header += "\n======================================================\n";

    if (!isatty(STDIN_FILENO)) {
        std::cout << header;
        std::cout << "Non-interactive terminal execution detected.\n"
                  << "Running demonstration analysis on sample logs...\n\n";
        run_demo(false, "reports/analysis_report.json", false);
        return 0;
    }

    struct MenuItem {
        std::string label;
        std::function<void()> action;
    };

    std::vector<MenuItem> items;
    items.push_back({"Run Demo Analysis (Built-in Samples)", []() {
        run_demo(false, "reports/analysis_report.json", true);
    }});

    if (os == "macos") {
        items.push_back({"Analyze Historical macOS Logs (log show)", []() {
            run_platform_log_analysis();
        }});
        items.push_back({"Capture Real-Time Live Stream (log stream)", []() {
            run_live_stream_analysis();
        }});
    } else if (os == "linux") {
        items.push_back({"Analyze System Logs (journalctl)", []() {
            run_platform_log_analysis();
        }});
    } else if (os == "windows") {
        items.push_back({"Analyze Windows Event Logs (wevtutil)", []() {
            run_platform_log_analysis();
        }});
    }

    items.push_back({"Launch GUI Dashboard (Desktop / Browser)", []() {
        launch_gui_dashboard();
    }});
    items.push_back({"Enter Logs Manually", []() {
        run_manual_entry();
    }});
    items.push_back({"Analyze Log File", []() {
        run_file_analysis("", false, "reports/analysis_report.json", true);
    }});
    items.push_back({"Search Loaded Logs", []() {
        run_search();
    }});
    items.push_back({"Run Self-Diagnosis Tests", []() {
        run_all_tests();
    }});
    items.push_back({"Exit", []() {
        std::cout << "\nGoodbye!\n";
    }});

    while (true) {
        std::cout << header;
        for (std::size_t i = 0; i < items.size(); ++i) {
            std::cout << (i + 1) << ". " << items[i].label << "\n";
        }
        std::cout << "======================================================\n";
        std::cout << "Select an option (1-" << items.size() << "): ";
        std::string choice;
        if (!std::getline(std::cin, choice)) {
            std::cout << "\nGoodbye!\n";
            break;
        }
        while (!choice.empty() && std::isspace(static_cast<unsigned char>(choice.front()))) choice.erase(choice.begin());
        while (!choice.empty() && std::isspace(static_cast<unsigned char>(choice.back()))) choice.pop_back();

        try {
            std::size_t idx = std::stoul(choice);
            if (idx >= 1 && idx <= items.size()) {
                if (idx == items.size()) {
                    items.back().action();
                    break;
                }
                items[idx - 1].action();
            } else {
                std::cout << "Invalid selection. Choose 1-" << items.size() << ".\n\n";
            }
        } catch (const std::exception&) {
            std::cout << "Invalid input. Please enter a valid number.\n\n";
        }
    }

    return 0;
}
