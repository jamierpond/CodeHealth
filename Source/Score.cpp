#include "Score.h"

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
        };

    return measures;
}

const std::map<std::string, FileMeasure, std::less<>>& getFileMeasures()
{
    static const auto measures = std::map<std::string, FileMeasure, std::less<>> {
        {"Large File", [](auto& file) { return file.linesOfCode; }},
        {"Overall Code Complexity", calculateMeanComplexity},
        {"Primitive Obsession", calculatePrimitiveArgumentPercentage},
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
         .categoryCap = 5.6,
         .thresholdByLanguage = {{"TSX", 10}, {"JSX", 10}}},
        {.name = "Large Method",
         .threshold = 70,
         .weight = 0.24,
         .categoryCap = 2.65,
         .thresholdByLanguage = {{"TSX", 120}, {"JSX", 120}}},
        {.name = "Deep Nesting",
         .threshold = 4,
         .weight = 0.265,
         .categoryCap = 8.0},
        {.name = "Bumpy Road", .threshold = 2, .weight = 0.15, .categoryCap = 12.7},
        {.name = "Complex Conditional",
         .threshold = 1,
         .weight = 0.4,
         .categoryCap = 1.6},
        {.name = "Excess Arguments",
         .threshold = 5,
         .weight = 0.425,
         .categoryCap = 0.6},
        {.name = "Large File",
         .threshold = 1000,
         .weight = 0.075,
         .categoryCap = 0.6},
        {.name = "Overall Code Complexity",
         .threshold = 4,
         .weight = 0.006,
         .categoryCap = 0.012},
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
} // namespace CodeHealth
