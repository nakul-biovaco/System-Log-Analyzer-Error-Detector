#pragma once

#include <string>
#include <vector>
#include "models.hpp"

std::string export_report_json(
    const AnalysisStats& stats,
    const std::vector<AlertRecord>& alerts,
    const RiskAssessment& risk,
    const std::vector<std::string>& recommendations,
    const std::string& filepath = "reports/analysis_report.json"
);

std::string export_report_txt(
    const std::string& report_text,
    const std::string& filepath = "reports/analysis_report.txt"
);
