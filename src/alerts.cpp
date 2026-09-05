#include "alerts.hpp"
#include <iomanip>
#include <sstream>

std::vector<AlertRecord> build_alerts(const std::vector<DetectionResult>& detections) {
    std::vector<AlertRecord> alerts;
    std::size_t idx = 1;
    for (const auto& d : detections) {
        if (d.triggered) {
            std::ostringstream oss;
            oss << "ALT-" << std::setw(3) << std::setfill('0') << idx++;
            alerts.push_back({
                oss.str(),
                d.rule_id,
                d.name,
                d.severity,
                d.evidence
            });
        }
    }
    return alerts;
}
