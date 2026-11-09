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
// saturate towards categoryCap. A rule with onlyForLanguages applies to those alone.
struct BiomarkerRule
{
    std::string name;
    double threshold = 0.0;
    double weight = 0.0;
    double categoryCap = 0.0;
    std::map<std::string, double> thresholdByLanguage {};
    std::map<std::string, double> categoryCapByLanguage {};
    std::map<std::string, double> weightByLanguage {};
    // Another measure under this rule's name, for a language that counts it differently.
    std::map<std::string, std::string> measureByLanguage {};
    // Languages whose findings cost weight * value / threshold, growing with the
    // value instead of with its logarithm.
    std::vector<std::string> linearPenaltyLanguages {};
    std::vector<std::string> onlyForLanguages {};

    bool isAppliedTo(const std::string& language) const;

    MIRO_REFLECT(name,
                 threshold,
                 weight,
                 categoryCap,
                 thresholdByLanguage,
                 categoryCapByLanguage,
                 weightByLanguage,
                 measureByLanguage,
                 linearPenaltyLanguages,
                 onlyForLanguages)
};

struct ScoringRules
{
    std::vector<BiomarkerRule> biomarkers;

    MIRO_REFLECT(biomarkers)
};

// A rule with one language's overrides applied.
struct LanguageRule
{
    std::string name;
    std::string measureName;
    double threshold = 0.0;
    double weight = 0.0;
    double categoryCap = 0.0;
    bool hasLinearPenalty = false;
};

LanguageRule resolveRuleForLanguage(const BiomarkerRule& rule,
                                    const std::string& language);

const ScoringRules& getDefaultScoringRules();

std::string formatFinding(const Finding& finding);

FileHealth assessFileHealth(const FileMetrics& metrics,
                            const ScoringRules& rules = getDefaultScoringRules());
} // namespace CodeHealth
