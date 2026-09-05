#include "exporter.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include "normalizer.hpp"

static std::string escape_json(const std::string& s) {
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

std::string export_report_json(
    const AnalysisStats& stats,
    const std::vector<AlertRecord>& alerts,
    const RiskAssessment& risk,
    const std::vector<std::string>& recommendations,
    const std::string& filepath
) {
    std::filesystem::path p(filepath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"summary\": {\n";
    ss << "    \"total_events\": " << stats.total_events << ",\n";
    ss << "    \"fully_parsed\": " << stats.fully_parsed_events << ",\n";
    ss << "    \"partial_fallback\": " << stats.partial_events << ",\n";
    ss << "    \"invalid\": " << stats.invalid_events << ",\n";
    ss << "    \"timeline\": {\n";
    ss << "      \"start\": " << (stats.earliest_timestamp.has_value() ? ("\"" + format_timestamp(*stats.earliest_timestamp) + "\"") : "null") << ",\n";
    ss << "      \"end\": " << (stats.latest_timestamp.has_value() ? ("\"" + format_timestamp(*stats.latest_timestamp) + "\"") : "null") << "\n";
    ss << "    }\n";
    ss << "  },\n";

    ss << "  \"severity_distribution\": {\n";
    Level all_levels[] = {Level::INFO, Level::WARNING, Level::ERROR, Level::CRITICAL, Level::UNKNOWN};
    for (std::size_t i = 0; i < 5; ++i) {
        Level lvl = all_levels[i];
        std::size_t cnt = 0;
        auto it = stats.severity_distribution.find(lvl);
        if (it != stats.severity_distribution.end()) cnt = it->second;
        ss << "    \"" << level_to_string(lvl) << "\": " << cnt << (i < 4 ? ",\n" : "\n");
    }
    ss << "  },\n";

    ss << "  \"category_summary\": {\n";
    Category all_cats[] = {Category::FILE, Category::NETWORK, Category::SECURITY, Category::RESOURCE, Category::PROCESS, Category::SYSTEM};
    for (std::size_t i = 0; i < 6; ++i) {
        Category cat = all_cats[i];
        std::size_t cnt = 0;
        auto it = stats.category_summary.find(cat);
        if (it != stats.category_summary.end()) cnt = it->second;
        ss << "    \"" << category_to_string(cat) << "\": " << cnt << (i < 5 ? ",\n" : "\n");
    }
    ss << "  },\n";

    ss << "  \"top_errors\": [\n";
    for (std::size_t i = 0; i < stats.top_errors.size(); ++i) {
        ss << "    {\n";
        ss << "      \"message\": \"" << escape_json(stats.top_errors[i].first) << "\",\n";
        ss << "      \"count\": " << stats.top_errors[i].second << "\n";
        ss << "    }" << (i + 1 < stats.top_errors.size() ? ",\n" : "\n");
    }
    ss << "  ],\n";

    ss << "  \"time_buckets\": [\n";
    for (std::size_t i = 0; i < stats.time_buckets.size(); ++i) {
        const auto& b = stats.time_buckets[i];
        ss << "    {\n";
        ss << "      \"label\": \"" << escape_json(b.bucket_label) << "\",\n";
        ss << "      \"count\": " << b.count << ",\n";
        ss << "      \"errors\": " << b.error_count << "\n";
        ss << "    }" << (i + 1 < stats.time_buckets.size() ? ",\n" : "\n");
    }
    ss << "  ],\n";

    ss << "  \"alerts\": [\n";
    for (std::size_t i = 0; i < alerts.size(); ++i) {
        const auto& a = alerts[i];
        ss << "    {\n";
        ss << "      \"rule_id\": \"" << escape_json(a.rule_id) << "\",\n";
        ss << "      \"name\": \"" << escape_json(a.name) << "\",\n";
        ss << "      \"severity\": \"" << level_to_string(a.severity) << "\",\n";
        ss << "      \"evidence\": \"" << escape_json(a.evidence) << "\"\n";
        ss << "    }" << (i + 1 < alerts.size() ? ",\n" : "\n");
    }
    ss << "  ],\n";

    ss << "  \"risk_assessment\": {\n";
    ss << "    \"score\": " << risk.score << ",\n";
    ss << "    \"band\": \"" << escape_json(risk.band) << "\",\n";
    ss << "    \"disclaimer\": \"" << escape_json(risk.disclaimer) << "\"\n";
    ss << "  },\n";

    ss << "  \"recommendations\": [\n";
    for (std::size_t i = 0; i < recommendations.size(); ++i) {
        ss << "    \"" << escape_json(recommendations[i]) << "\"" << (i + 1 < recommendations.size() ? ",\n" : "\n");
    }
    ss << "  ],\n";

    ss << "  \"telemetry\": {\n";
    if (stats.telemetry.has_value()) {
        const auto& tel = *stats.telemetry;
        ss << "    \"elapsed_seconds\": " << tel.elapsed_seconds << ",\n";
        ss << "    \"throughput_eps\": " << tel.throughput_eps << ",\n";
        ss << "    \"peak_memory_kb\": " << tel.peak_memory_kb << ",\n";
        ss << "    \"memory_metric_label\": \"" << escape_json(tel.memory_metric_label) << "\"\n";
    } else {
        ss << "    \"elapsed_seconds\": 0.0,\n";
        ss << "    \"throughput_eps\": 0.0,\n";
        ss << "    \"peak_memory_kb\": 0.0,\n";
        ss << "    \"memory_metric_label\": \"N/A\"\n";
    }
    ss << "  }\n";
    ss << "}\n";

    std::ofstream out(filepath);
    out << ss.str();
    return filepath;
}

std::string export_report_txt(const std::string& report_text, const std::string& filepath) {
    std::filesystem::path p(filepath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(filepath);
    out << report_text << "\n";
    return filepath;
}
