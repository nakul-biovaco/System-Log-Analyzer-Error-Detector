#include "risk.hpp"
#include <algorithm>
#include <cmath>
#include "rules.hpp"

RiskAssessment calculate_risk_score(const AnalysisStats& stats) {
    if (stats.total_events == 0) {
        return {0, "HEALTHY", RISK_DISCLAIMER};
    }

    std::size_t total = stats.total_events;
    std::size_t warn_cnt = 0;
    std::size_t err_cnt = 0;
    std::size_t crit_cnt = 0;

    for (const auto& pair : stats.severity_distribution) {
        switch (pair.first) {
            case Level::WARNING: warn_cnt += pair.second; break;
            case Level::ERROR: err_cnt += pair.second; break;
            case Level::CRITICAL: crit_cnt += pair.second; break;
            default: break;
        }
    }

    double error_rate = static_cast<double>(err_cnt) / static_cast<double>(total);
    double crit_rate = static_cast<double>(crit_cnt) / static_cast<double>(total);
    double warn_rate = static_cast<double>(warn_cnt) / static_cast<double>(total);

    double density_score = std::min(40.0, (error_rate * 55.0) + (crit_rate * 75.0) + (warn_rate * 10.0));

    double weighted_sum = (warn_cnt * 1.0) + (err_cnt * 3.0) + (crit_cnt * 5.0);
    double max_weight = static_cast<double>(total) * 5.0;
    double severity_score = (weighted_sum / max_weight) * 35.0;

    double anomaly_score = 0.0;
    for (const auto& pair : stats.top_errors) {
        if (pair.second >= 3) {
            double freq_ratio = static_cast<double>(pair.second) / static_cast<double>(total);
            anomaly_score += std::min(5.0, 2.0 + freq_ratio * 15.0);
        }
    }
    anomaly_score = std::min(15.0, anomaly_score);

    if (!stats.time_buckets.empty()) {
        std::size_t max_bucket_err = 0;
        std::size_t total_bucket_err = 0;
        for (const auto& b : stats.time_buckets) {
            if (b.error_count > max_bucket_err) max_bucket_err = b.error_count;
            total_bucket_err += b.error_count;
        }
        double avg_bucket_err = static_cast<double>(total_bucket_err) / static_cast<double>(stats.time_buckets.size());
        if (avg_bucket_err > 0.0 && static_cast<double>(max_bucket_err) > (avg_bucket_err * 1.8)) {
            double spike_factor = static_cast<double>(max_bucket_err) / avg_bucket_err;
            anomaly_score += std::min(10.0, spike_factor * 2.5);
        }
    }

    double crit_floor_bonus = 0.0;
    if (crit_cnt > 0) {
        crit_floor_bonus = std::min(15.0, 3.0 + (crit_rate * 40.0));
        if (crit_rate >= 0.50) {
            crit_floor_bonus = std::max(crit_floor_bonus, 25.0);
        }
    }

    int raw_score = static_cast<int>(std::round(density_score + severity_score + anomaly_score + crit_floor_bonus));
    int score = std::clamp(raw_score, 0, 100);

    if (crit_cnt > 0 && total == crit_cnt) {
        score = std::max(score, CRITICAL_RISK_FLOOR);
    }

    std::string band = get_risk_band(score);
    return {score, band, RISK_DISCLAIMER};
}
