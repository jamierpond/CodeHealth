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
// CodeScene's Python duplication costs about 0.6 points for 2 cloned functions,
// 1.5 for 10 and 2 for 20 or more, and only in files of 70 or more lines of code
// (scripts/calibration/python-probes.py); weight and cap fit those penalties.
constexpr auto duplicatedFunctionsMinimumLines = 70;
constexpr auto pythonDuplicatedFunctionsThreshold = 2;
constexpr auto pythonDuplicationWeight = 0.765;
constexpr auto pythonDuplicationCap = 2.98;
// CodeScene flags a Python module's cohesion and function count only from about
// 550 lines of code: 500 never fired in the probes, 550 did.
constexpr auto largeModuleMinimumLines = 550;
// Fitted on the pytorch, tensorflow and django sample: modules CodeScene flags
// have 16 or more unrelated groups, and it costs them 1.2 to 2.5 points.
constexpr auto pythonModuleCohesionThreshold = 16;
constexpr auto pythonModuleCohesionWeight = 1.6;
constexpr auto pythonModuleCohesionCap = 2.5;
// CodeScene's cost for one long Python method grows about linearly with its
// length: 0.45 at 79 lines, 1.57 at 300, 2.79 at 584 (python-probes.py).
constexpr auto pythonLargeMethodWeight = 0.36;
constexpr auto pythonLargeMethodCap = 20.0;
// Flagged at 76 functions and methods, not at 75; costs about 0.2 points.
constexpr auto pythonFunctionCountThreshold = 76;

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

double countDuplicatedFunctionsInLargeEnoughFile(const FileMetrics& file)
{
    if (file.linesOfCode < duplicatedFunctionsMinimumLines)
        return 0.0;

    return file.duplicatedFunctionCount;
}

double measureModuleCohesionInLargeModule(const FileMetrics& file)
{
    if (file.linesOfCode < largeModuleMinimumLines)
        return 0.0;

    return file.moduleCohesionGroups;
}

// Top-level functions and methods; a function nested in another is named
// after it ("outer.inner") and does not count.
double countModuleFunctionsInLargeModule(const FileMetrics& file)
{
    if (file.linesOfCode < largeModuleMinimumLines)
        return 0.0;

    return static_cast<double>(std::ranges::count_if(
        file.functions,
        [](auto& function)
        { return function.name.find('.') == std::string::npos; }));
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
        {"Duplicated Functions", countDuplicatedFunctionsInLargeEnoughFile},
        {"Module Cohesion", measureModuleCohesionInLargeModule},
        {"Number of Functions in a Single Module",
         countModuleFunctionsInLargeModule},
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

double calculateFindingPenalty(const LanguageRule& rule, double value)
{
    if (rule.hasLinearPenalty)
        return rule.weight * value / rule.threshold;

    return rule.weight * (1.0 + std::log(value / rule.threshold));
}

// Each biomarker saturates at its cap, so one kind of problem cannot sink a file alone.
double saturateAtCap(double rawPenalty, double cap)
{
    return cap * (1.0 - std::exp(-rawPenalty / cap));
}

class FindingCollector
{
public:
    void add(const LanguageRule& rule,
             const std::string& functionName,
             int line,
             double value)
    {
        if (value < rule.threshold)
            return;

        rawPenalty += calculateFindingPenalty(rule, value);
        findings.push_back({.biomarker = rule.name,
                            .functionName = functionName,
                            .line = line,
                            .value = value,
                            .threshold = rule.threshold});
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

template <typename Value>
Value findForLanguage(const std::map<std::string, Value>& overrides,
                      const std::string& language,
                      const Value& fallback)
{
    auto found = overrides.find(language);
    return found == overrides.end() ? fallback : found->second;
}

LanguageRule resolveRuleForLanguage(const BiomarkerRule& rule,
                                    const std::string& language)
{
    return {
        .name = rule.name,
        .measureName = findForLanguage(rule.measureByLanguage, language, rule.name),
        .threshold =
            findForLanguage(rule.thresholdByLanguage, language, rule.threshold),
        .weight = findForLanguage(rule.weightByLanguage, language, rule.weight),
        .categoryCap =
            findForLanguage(rule.categoryCapByLanguage, language, rule.categoryCap),
        .hasLinearPenalty = std::ranges::find(rule.linearPenaltyLanguages, language)
                            != rule.linearPenaltyLanguages.end()};
}

bool BiomarkerRule::isAppliedTo(const std::string& language) const
{
    return onlyForLanguages.empty()
           || std::ranges::find(onlyForLanguages, language)
                  != onlyForLanguages.end();
}

namespace
{
std::vector<BiomarkerRule> makeFunctionRules()
{
    return {

        {.name = "Complex Method",
         .threshold = 9,
         .weight = 0.3,
         .categoryCap = 4.2,
         .thresholdByLanguage = {{"TSX", 10}, {"JSX", 10}}},
        {.name = "Large Method",
         .threshold = 70,
         .weight = 0.24,
         .categoryCap = 1.559,
         .thresholdByLanguage = {{"TSX", 120}, {"JSX", 120}},
         .categoryCapByLanguage = {{"Python", pythonLargeMethodCap}},
         .weightByLanguage = {{"Python", pythonLargeMethodWeight}},
         .linearPenaltyLanguages = {"Python"}},
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
    };
}

std::vector<BiomarkerRule> makeFileRules()
{
    return {
        {.name = "Large File", .threshold = 1000, .weight = 0.1, .categoryCap = 0.6},
        {.name = "Overall Code Complexity",
         .threshold = 4,
         .weight = 0.001,
         .categoryCap = 0.012},
        {.name = "Code Duplication",
         .threshold = 5,
         .weight = 8.462,
         .categoryCap = 0.279,
         .thresholdByLanguage = {{"Python", pythonDuplicatedFunctionsThreshold}},
         .categoryCapByLanguage = {{"Python", pythonDuplicationCap}},
         .weightByLanguage = {{"Python", pythonDuplicationWeight}},
         .measureByLanguage = {{"Python", "Duplicated Functions"}}},
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
         .categoryCap = 0.6,
         .thresholdByLanguage = {{"Python", pythonModuleCohesionThreshold}},
         .categoryCapByLanguage = {{"Python", pythonModuleCohesionCap}},
         .weightByLanguage = {{"Python", pythonModuleCohesionWeight}},
         .measureByLanguage = {{"Python", "Module Cohesion"}}},
        {.name = "Number of Functions in a Single Module",
         .threshold = pythonFunctionCountThreshold,
         .weight = 0.2,
         .categoryCap = 0.3,
         .onlyForLanguages = {"Python"}},
        {.name = "Primitive Obsession",
         .threshold = 30,
         .weight = 0.02,
         .categoryCap = 0.3},
    };
}

ScoringRules makeDefaultScoringRules()
{
    auto biomarkers = makeFunctionRules();
    auto fileRules = makeFileRules();
    biomarkers.insert(biomarkers.end(), fileRules.begin(), fileRules.end());
    return {biomarkers};
}
} // namespace

const ScoringRules& getDefaultScoringRules()
{
    static const auto rules = makeDefaultScoringRules();
    return rules;
}

FileHealth assessFileHealth(const FileMetrics& metrics, const ScoringRules& rules)
{
    auto collector = FindingCollector {};
    auto totalPenalty = 0.0;

    for (auto& generalRule: rules.biomarkers)
    {
        if (!generalRule.isAppliedTo(metrics.language))
            continue;

        auto rule = resolveRuleForLanguage(generalRule, metrics.language);

        if (auto measure = findMeasure(getFunctionMeasures(), rule.measureName))
            for (auto& function: metrics.functions)
                collector.add(
                    rule, function.name, function.startLine, (*measure)(function));

        if (auto measure = findMeasure(getFileMeasures(), rule.measureName))
            collector.add(rule, {}, 0, (*measure)(metrics));

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
