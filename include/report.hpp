#pragma once

#include <string>
#include <vector>
#include "models.hpp"

std::string generate_report(
    const AnalysisStats& stats,
    const std::vector<AlertRecord>& alerts,
    const RiskAssessment& risk,
    const std::vector<std::string>& recommendations
);
