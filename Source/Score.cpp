#include "Score.h"

#include <eacp/Core/Utils/Format.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>

namespace CodeHealth
{
namespace
{
constexpr auto perfectScore = 10.0;
constexpr auto lowestScore = 1.0;
constexpr auto percent = 100.0;
constexpr auto minimumFunctionsForPrimitiveObsession = 7;
// CodeScene's Brain Method is long, complex and deeply nested at once; these
// cut-offs best match the functions it flags.
constexpr auto brainMethodMinimumLines = 70;
constexpr auto brainMethodMinimumNesting = 5;
constexpr auto brainMethodMinimumComplexity = 24;
constexpr auto brainClassMinimumLines = 1500;
constexpr auto lowCohesionMinimumMethods = 10;

using FunctionMeasure = std::function<double(const FunctionMetrics&)>;
using FileMeasure = std::function<double(const FileMetrics&)>;

double calculateMeanComplexity(const FileMetrics& file)
{
    if (file.functions.empty())
        return 0.0;

    return static_cast<double>(file.calculateTotalComplexity())
           / static_cast<double>(file.functions.size());
}

double calculatePrimitiveArgumentPercentage(const FileMetrics& file)
{
    if (file.functions.size() < minimumFunctionsForPrimitiveObsession)
        return 0.0;

    auto primitive = std::ranges::count_if(
        file.functions,
        [](auto& function) { return function.hasPrimitiveArgument; });
    return percent * static_cast<double>(primitive)
           / static_cast<double>(file.functions.size());
}

bool isLongAndDeeplyNested(const FunctionMetrics& function)
{
    return function.length >= brainMethodMinimumLines
           && function.maximumNestingDepth >= brainMethodMinimumNesting;
}

double measureBrainMethod(const FunctionMetrics& function)
{
    return isLongAndDeeplyNested(function) ? function.cyclomaticComplexity : 0;
}

// A large file with many functions, one of them a Brain Method.
double measureBrainClass(const FileMetrics& file)
{
    auto hasBrainMethod = std::ranges::any_of(
        file.functions,
        [](auto& function)
        { return measureBrainMethod(function) >= brainMethodMinimumComplexity; });

    if (!hasBrainMethod || file.linesOfCode < brainClassMinimumLines)
        return 0;

    return static_cast<double>(file.functions.size());
}

// The most unrelated method groups in any class with enough methods to judge.
double measureLowCohesion(const FileMetrics& file)
{
    auto groups = 0;

    for (auto& metrics: file.classes)
        if (metrics.methodCount >= lowCohesionMinimumMethods)
            groups = std::max(groups, metrics.cohesionGroups);

    return groups;
}

double calculateDuplicatedLinePercentage(const FileMetrics& file)
{
    if (file.linesOfCode == 0)
        return 0.0;

    return percent * file.duplicatedLines / file.linesOfCode;
}

const std::map<std::string, FunctionMeasure, std::less<>>& getFunctionMeasures()
{
    static const auto measures =
        std::map<std::string, FunctionMeasure, std::less<>> {
            {"Complex Method", [](auto& f) { return f.cyclomaticComplexity; }},
            {"Large Method", [](auto& f) { return f.length; }},
            {"Deep Nesting", [](auto& f) { return f.maximumNestingDepth; }},
            {"Bumpy Road", [](auto& f) { return f.bumpCount; }},
            {"Complex Conditional",
             [](auto& f) { return f.complexConditionalCount; }},
            {"Excess Arguments", [](auto& f) { return f.argumentCount; }},
            {"Brain Method", measureBrainMethod},
        };

    return measures;
}

const std::map<std::string, FileMeasure, std::less<>>& getFileMeasures()
{
    static const auto measures = std::map<std::string, FileMeasure, std::less<>> {
        {"Large File", [](auto& file) { return file.linesOfCode; }},
        {"Overall Code Complexity", calculateMeanComplexity},
        {"Primitive Obsession", calculatePrimitiveArgumentPercentage},
        {"Code Duplication", calculateDuplicatedLinePercentage},
        {"Brain Class", measureBrainClass},
        {"Low Cohesion", measureLowCohesion},
    };

    return measures;
}

template <typename Measure>
std::optional<Measure>
    findMeasure(const std::map<std::string, Measure, std::less<>>& measures,
                const std::string& name)
{
    auto found = measures.find(name);

    if (found == measures.end())
        return std::nullopt;

    return found->second;
}

double calculateFindingPenalty(const BiomarkerRule& rule,
                               double value,
                               double threshold)
{
    return rule.weight * (1.0 + std::log(value / threshold));
}

// Each biomarker saturates at its cap, so one kind of problem cannot sink a file alone.
double saturateAtCap(double rawPenalty, double cap)
{
    return cap * (1.0 - std::exp(-rawPenalty / cap));
}

class FindingCollector
{
public:
    void add(const BiomarkerRule& rule,
             double threshold,
             const std::string& functionName,
             int line,
             double value)
    {
        if (value < threshold)
            return;

        rawPenalty += calculateFindingPenalty(rule, value, threshold);
        findings.push_back({.biomarker = rule.name,
                            .functionName = functionName,
                            .line = line,
                            .value = value,
                            .threshold = threshold});
    }

    double takeSaturatedPenalty(double cap)
    {
        auto penalty = saturateAtCap(rawPenalty, cap);
        rawPenalty = 0.0;
        return penalty;
    }

    std::vector<Finding> findings;

private:
    double rawPenalty = 0.0;
};
} // namespace

double BiomarkerRule::getThresholdFor(const std::string& language) const
{
    auto found = thresholdByLanguage.find(language);
    return found == thresholdByLanguage.end() ? threshold : found->second;
}

const ScoringRules& getDefaultScoringRules()
{
    static const auto rules = ScoringRules {{
        {.name = "Complex Method",
         .threshold = 9,
         .weight = 0.3,
         .categoryCap = 4.2,
         .thresholdByLanguage = {{"TSX", 10}, {"JSX", 10}}},
        {.name = "Large Method",
         .threshold = 70,
         .weight = 0.24,
         .categoryCap = 1.559,
         .thresholdByLanguage = {{"TSX", 120}, {"JSX", 120}}},
        {.name = "Deep Nesting",
         .threshold = 4,
         .weight = 0.264,
         .categoryCap = 1.5},
        {.name = "Bumpy Road", .threshold = 2, .weight = 0.15, .categoryCap = 3.175},
        {.name = "Complex Conditional",
         .threshold = 1,
         .weight = 0.4,
         .categoryCap = 1.6},
        {.name = "Excess Arguments",
         .threshold = 5,
         .weight = 0.425,
         .categoryCap = 0.6},
        {.name = "Large File", .threshold = 1000, .weight = 0.1, .categoryCap = 0.6},
        {.name = "Overall Code Complexity",
         .threshold = 4,
         .weight = 0.001,
         .categoryCap = 0.012},
        {.name = "Code Duplication",
         .threshold = 5,
         .weight = 8.462,
         .categoryCap = 0.279},
        {.name = "Brain Method",
         .threshold = 24,
         .weight = 0.5,
         .categoryCap = 1.995},
        {.name = "Brain Class",
         .threshold = 40,
         .weight = 0.062,
         .categoryCap = 0.5},
        {.name = "Low Cohesion",
         .threshold = 25,
         .weight = 0.011,
         .categoryCap = 0.6},
        {.name = "Primitive Obsession",
         .threshold = 30,
         .weight = 0.02,
         .categoryCap = 0.3},
    }};

    return rules;
}

FileHealth assessFileHealth(const FileMetrics& metrics, const ScoringRules& rules)
{
    auto collector = FindingCollector {};
    auto totalPenalty = 0.0;

    for (auto& rule: rules.biomarkers)
    {
        auto threshold = rule.getThresholdFor(metrics.language);

        if (auto measure = findMeasure(getFunctionMeasures(), rule.name))
            for (auto& function: metrics.functions)
                collector.add(rule,
                              threshold,
                              function.name,
                              function.startLine,
                              (*measure)(function));

        if (auto measure = findMeasure(getFileMeasures(), rule.name))
            collector.add(rule, threshold, {}, 0, (*measure)(metrics));

        totalPenalty += collector.takeSaturatedPenalty(rule.categoryCap);
    }

    return {.path = metrics.path,
            .language = metrics.language,
            .score = std::max(lowestScore, perfectScore - totalPenalty),
            .linesOfCode = metrics.linesOfCode,
            .functionCount = static_cast<int>(metrics.functions.size()),
            .totalComplexity = metrics.calculateTotalComplexity(),
            .findings = std::move(collector.findings),
            .dependencies = metrics.dependencies};
}

std::string formatFinding(const Finding& finding)
{
    if (finding.functionName.empty())
        return eacp::Strings::format("{} ({:g} >= {:g})",
                                     finding.biomarker,
                                     finding.value,
                                     finding.threshold);

    return eacp::Strings::format("{} in {} at line {} ({:g} >= {:g})",
                                 finding.biomarker,
                                 finding.functionName,
                                 finding.line,
                                 finding.value,
                                 finding.threshold);
}
} // namespace CodeHealth
