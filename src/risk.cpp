#include "risk.hpp"
#include <algorithm>
#include "rules.hpp"

RiskAssessment calculate_risk_score(const AnalysisStats& stats) {
    if (stats.total_events == 0) {
        return {0, "HEALTHY", RISK_DISCLAIMER};
    }

    std::size_t crit_count = 0;
    auto it_crit = stats.severity_distribution.find(Level::CRITICAL);
    if (it_crit != stats.severity_distribution.end()) crit_count = it_crit->second;

    int points = 0;
    for (const auto& pair : stats.severity_distribution) {
        points += get_severity_weight(pair.first) * static_cast<int>(pair.second);
    }

    double max_possible = static_cast<double>(stats.total_events * get_severity_weight(Level::CRITICAL));
    int score = static_cast<int>((static_cast<double>(points) / max_possible) * 100.0 * 2.2);

    if (crit_count > 0) {
        int boost = 5;
        if (crit_count >= 50) boost = 35;
        else if (crit_count >= 10) boost = 25;
        else if (crit_count >= 3) boost = 15;

        score = std::max(score, std::min(100, CRITICAL_RISK_FLOOR + boost));
    }

    score = std::min(100, score);
    std::string band = get_risk_band(score);

    return {score, band, RISK_DISCLAIMER};
}
