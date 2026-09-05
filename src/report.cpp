#include "report.hpp"
#include <iomanip>
#include <sstream>
#include "normalizer.hpp"

std::string generate_report(
    const AnalysisStats& stats,
    const std::vector<AlertRecord>& alerts,
    const RiskAssessment& risk,
    const std::vector<std::string>& recommendations
) {
    std::string div(54, '=');
    std::string subdiv(54, '-');
    std::ostringstream ss;

    ss << div << "\n";
    ss << "             SYSTEM LOG ANALYZER & ERROR DETECTOR\n";
    ss << div << "\n";
    ss << "ANALYSIS SUMMARY\n";
    ss << subdiv << "\n";

    std::size_t tot = stats.total_events;
    if (stats.telemetry.has_value()) {
        const auto& tel = *stats.telemetry;
        if (tel.total_raw_lines > tot) {
            double noise_pct = tel.total_raw_lines > 0 ? (static_cast<double>(tel.noise_lines_removed) / tel.total_raw_lines * 100.0) : 0.0;
            ss << std::fixed << std::setprecision(1);
            ss << "Raw Input    : " << tel.total_raw_lines << " lines (Noise Filter Discarded: " << tel.noise_lines_removed << " [" << noise_pct << "%])\n";
        }
    }

    double p_pct = tot > 0 ? (static_cast<double>(stats.fully_parsed_events) / tot * 100.0) : 0.0;
    double f_pct = tot > 0 ? (static_cast<double>(stats.partial_events) / tot * 100.0) : 0.0;
    double i_pct = tot > 0 ? (static_cast<double>(stats.invalid_events) / tot * 100.0) : 0.0;
    ss << std::fixed << std::setprecision(1);
    ss << "Total Events : " << tot << " (Parsed: " << stats.fully_parsed_events << " [" << p_pct << "%] | Fallback: " << stats.partial_events << " [" << f_pct << "%] | Invalid: " << stats.invalid_events << " [" << i_pct << "%])\n";

    if (stats.earliest_timestamp.has_value() && stats.latest_timestamp.has_value()) {
        ss << "Timeline     : " << format_timestamp(*stats.earliest_timestamp) << "  to  " << format_timestamp(*stats.latest_timestamp) << "\n";
    }

    ss << "\nSEVERITY DISTRIBUTION\n";
    ss << subdiv << "\n";
    Level all_levels[] = {Level::INFO, Level::WARNING, Level::ERROR, Level::CRITICAL, Level::UNKNOWN};
    for (Level lvl : all_levels) {
        std::size_t cnt = 0;
        auto it = stats.severity_distribution.find(lvl);
        if (it != stats.severity_distribution.end()) cnt = it->second;
        double pct = tot > 0 ? (static_cast<double>(cnt) / tot * 100.0) : 0.0;
        ss << std::left << std::setw(10) << level_to_string(lvl) << ": " << std::right << std::setw(5) << cnt << " (" << std::fixed << std::setprecision(1) << std::setw(5) << pct << "%)\n";
    }

    ss << "\nTOP ERRORS\n";
    ss << subdiv << "\n";
    if (!stats.top_errors.empty()) {
        std::size_t idx = 1;
        for (const auto& pair : stats.top_errors) {
            std::string msg = pair.first;
            std::string snip = (msg.size() > 68) ? (msg.substr(0, 65) + "...") : msg;
            ss << idx++ << ". [Count: " << std::right << std::setw(3) << pair.second << "] " << snip << "\n";
        }
    } else {
        ss << "No error-level events identified.\n";
    }

    ss << "\nCATEGORY SUMMARY\n";
    ss << subdiv << "\n";
    Category all_cats[] = {Category::FILE, Category::NETWORK, Category::SECURITY, Category::RESOURCE, Category::PROCESS, Category::SYSTEM};
    for (Category cat : all_cats) {
        std::size_t cnt = 0;
        auto it = stats.category_summary.find(cat);
        if (it != stats.category_summary.end()) cnt = it->second;
        double pct = tot > 0 ? (static_cast<double>(cnt) / tot * 100.0) : 0.0;
        ss << std::left << std::setw(10) << category_to_string(cat) << ": " << std::right << std::setw(5) << cnt << " (" << std::fixed << std::setprecision(1) << std::setw(5) << pct << "%)\n";
    }

    if (!stats.time_buckets.empty()) {
        ss << "\nTIME-BUCKETED ERROR DENSITY\n";
        ss << subdiv << "\n";
        for (const auto& b : stats.time_buckets) {
            ss << "[" << b.bucket_label << "] Total: " << std::right << std::setw(5) << b.count << " | Errors: " << std::right << std::setw(4) << b.error_count << "\n";
        }
    }

    ss << "\nDETECTION RESULTS\n";
    ss << subdiv << "\n";
    if (!alerts.empty()) {
        for (const auto& a : alerts) {
            ss << "[" << level_to_string(a.severity) << "] " << a.rule_id << " " << a.name << "\n";
            ss << "  Evidence: " << a.evidence << "\n";
        }
    } else {
        ss << "No detection rule thresholds triggered.\n";
    }

    ss << "\nRISK ASSESSMENT\n";
    ss << subdiv << "\n";
    ss << "Risk Score : " << risk.score << "/100\n";
    ss << "Status     : " << risk.band << "\n";
    ss << risk.disclaimer << "\n";

    ss << "\nRECOMMENDATIONS\n";
    ss << subdiv << "\n";
    std::size_t r_idx = 1;
    for (const auto& rec : recommendations) {
        ss << r_idx++ << ". " << rec << "\n";
    }

    if (stats.telemetry.has_value()) {
        const auto& tel = *stats.telemetry;
        ss << "\nPERFORMANCE & RESOURCE TELEMETRY\n";
        ss << subdiv << "\n";
        ss << "Processing Time : " << std::fixed << std::setprecision(4) << tel.elapsed_seconds << " seconds\n";
        ss << "Throughput      : " << std::fixed << std::setprecision(0) << tel.throughput_eps << " events/sec\n";
        ss << "Peak Memory     : " << std::fixed << std::setprecision(1) << tel.peak_memory_kb << " KB (" << tel.memory_metric_label << ")\n";
        ss << "Complexity      : O(N) single-pass stream + O(B+A) evaluation\n";
    }

    ss << div;
    return ss.str();
}
