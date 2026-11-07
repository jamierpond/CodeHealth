#pragma once

#include "Metrics.h"

#include <map>

namespace CodeHealth
{
struct Finding
{
    std::string biomarker;
    std::string functionName;
    int line = 0;
    double value = 0.0;
    double threshold = 0.0;

    MIRO_REFLECT(biomarker, functionName, line, value, threshold)
};

struct FileHealth
{
    std::string path;
    std::string language;
    double score = 10.0;
    int linesOfCode = 0;
    int functionCount = 0;
    int totalComplexity = 0;
    std::vector<Finding> findings;
    std::vector<std::string> dependencies;

    MIRO_REFLECT(path,
                 language,
                 score,
                 linesOfCode,
                 functionCount,
                 totalComplexity,
                 findings,
                 dependencies)
};

// One biomarker's tuning: a finding at or above threshold costs
// weight * (1 + ln(value / threshold)), and a biomarker's findings together
// saturate towards categoryCap.
struct BiomarkerRule
{
    std::string name;
    double threshold = 0.0;
    double weight = 0.0;
    double categoryCap = 0.0;
    std::map<std::string, double> thresholdByLanguage {};

    double getThresholdFor(const std::string& language) const;

    MIRO_REFLECT(name, threshold, weight, categoryCap, thresholdByLanguage)
};

struct ScoringRules
{
    std::vector<BiomarkerRule> biomarkers;

    MIRO_REFLECT(biomarkers)
};

const ScoringRules& getDefaultScoringRules();

FileHealth assessFileHealth(const FileMetrics& metrics,
                            const ScoringRules& rules = getDefaultScoringRules());
} // namespace CodeHealth
