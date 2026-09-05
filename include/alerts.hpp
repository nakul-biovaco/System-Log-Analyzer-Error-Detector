#pragma once

#include <vector>
#include "models.hpp"

struct AlertRecord;
struct DetectionResult;

std::vector<AlertRecord> build_alerts(const std::vector<DetectionResult>& detections);
