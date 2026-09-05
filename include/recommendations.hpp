#pragma once

#include <vector>
#include <string>
#include "models.hpp"

std::vector<std::string> generate_recommendations(
    const AnalysisStats& stats,
    const std::vector<DetectionResult>& detections
);
