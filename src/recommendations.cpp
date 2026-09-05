#include "recommendations.hpp"
#include <algorithm>
#include <cctype>

struct ActionMapping {
    std::vector<std::string> keys;
    std::string template_text;
};

std::vector<std::string> generate_recommendations(
    const AnalysisStats& stats,
    const std::vector<DetectionResult>& detections
) {
    std::vector<std::string> recs;
    bool r001_triggered = false;
    bool r002_triggered = false;
    bool r005_triggered = false;

    for (const auto& d : detections) {
        if (d.rule_id == "R001") r001_triggered = true;
        if (d.rule_id == "R002") r002_triggered = true;
        if (d.rule_id == "R005") r005_triggered = true;
    }

    static const std::vector<ActionMapping> mappings = {
        {{"cfprefsd", "defaults", "plist", "preference"}, "Repair user preference permissions and check for corrupted plists for '"},
        {{"runningboard", "jetsam", "appnap"}, "Adjust RunningBoard jetsam memory thresholds causing recurring termination in '"},
        {{"rapportd", "mediaremote", "xpc", "bluetooth"}, "Inspect companion device discovery and XPC daemon connection health for '"},
        {{"connection", "database", "socket", "timeout", "refused", "port"}, "Verify database listener status, network connectivity, and socket timeouts for '"},
        {{"auth", "password", "unauthorized", "login", "ssh", "credential"}, "Audit repeated authentication failures for potential brute-force attempts in '"},
        {{"file", "not found", "path", "directory"}, "Verify file path resolution and check filesystem read/write permissions for '"},
        {{"memory", "oom", "disk", "capacity", "exhaust", "quota"}, "Resolve resource exhaustion by freeing disk storage or increasing memory limits for '"}
    };

    if (r001_triggered) {
        for (const auto& pair : stats.top_errors) {
            if (pair.second >= 3) {
                std::string msg = pair.first;
                std::string low = msg;
                std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return std::tolower(c); });

                std::string snip = (msg.size() > 63) ? (msg.substr(0, 60) + "...") : msg;
                bool matched = false;

                for (const auto& map_item : mappings) {
                    for (const auto& kw : map_item.keys) {
                        if (low.find(kw) != std::string::npos) {
                            recs.push_back("Possible action (R001): " + map_item.template_text + snip + "'.");
                            matched = true;
                            break;
                        }
                    }
                    if (matched) break;
                }

                if (!matched) {
                    recs.push_back("Possible action (R001): Investigate recurring failure pattern in '" + snip + "' (observed " + std::to_string(pair.second) + " times).");
                }
            }
        }
    }

    if (r002_triggered) {
        std::size_t crit_count = 0;
        auto it_crit = stats.severity_distribution.find(Level::CRITICAL);
        if (it_crit != stats.severity_distribution.end()) crit_count = it_crit->second;
        recs.push_back("Possible action (R002): Immediately review system crash logs and service states for the " + std::to_string(crit_count) + " CRITICAL event(s).");
    }

    if (r005_triggered) {
        recs.push_back("Possible action (R005): Correlate error spike window with scheduled cron jobs, deployments, or network events.");
    }

    if (recs.empty()) {
        std::string corpus;
        for (const auto& pair : stats.top_errors) {
            std::string low = pair.first;
            std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return std::tolower(c); });
            corpus += " " + low;
        }

        if (corpus.find("connection") != std::string::npos || corpus.find("database") != std::string::npos || corpus.find("socket") != std::string::npos || corpus.find("port") != std::string::npos) {
            recs.push_back("Possible action: Check database server availability and network ports.");
        } else if (corpus.find("memory") != std::string::npos || corpus.find("disk") != std::string::npos || corpus.find("space") != std::string::npos) {
            recs.push_back("Possible action: Monitor process memory consumption and filesystem storage.");
        } else {
            recs.push_back("Possible action: System operating within normal thresholds; maintain scheduled log monitoring.");
        }
    }

    return recs;
}
