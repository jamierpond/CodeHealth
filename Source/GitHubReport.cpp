#include "GitHubReport.h"

#include <eacp/Core/Utils/Format.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace CodeHealth
{
namespace
{
struct FindingAdvice
{
    std::string_view biomarker;
    std::string_view measured;
    std::string_view hint;
};

constexpr auto findingAdvice = std::array {
    FindingAdvice {
        "Complex Method", "branches", "split the decisions into named functions"},
    FindingAdvice {
        "Large Method", "lines", "extract its steps into named functions"},
    FindingAdvice {"Deep Nesting",
                   "levels of nesting",
                   "return early or extract the inner block"},
    FindingAdvice {"Bumpy Road",
                   "nested blocks",
                   "extract each nested block into its own function"},
    FindingAdvice {"Complex Conditional",
                   "conditions with two or more and/or operators",
                   "give each condition a name: a boolean or a small function"},
    FindingAdvice {
        "Excess Arguments", "arguments", "group related arguments into one type"},
    FindingAdvice {"Brain Method",
                   "branches in a long, deeply nested function",
                   "split it into smaller functions, each with one job"},
    FindingAdvice {
        "Large File", "lines of code", "split the file by responsibility"},
    FindingAdvice {"Overall Code Complexity",
                   "branches per function on average",
                   "simplify the most complex functions"},
    FindingAdvice {"Code Duplication",
                   "duplicated",
                   "extract the shared code into one function"},
    FindingAdvice {"Brain Class",
                   "functions around a Brain Method",
                   "split the file by responsibility"},
    FindingAdvice {"Low Cohesion",
                   "unrelated groups of functions",
                   "move each group into its own class or module"},
    FindingAdvice {"Primitive Obsession",
                   "percent of functions taking primitive arguments",
                   "introduce small types for the values"},
    FindingAdvice {"Number of Functions in a Single Module",
                   "functions",
                   "split the module by responsibility"},
};

constexpr auto fallbackAdvice = FindingAdvice {"", "", "simplify it"};

struct FailureRuleName
{
    std::string_view name;
    FailureRule rule;
};

constexpr auto failureRuleNames = std::array {
    FailureRuleName {"score-drop", FailureRule::scoreDrop},
    FailureRuleName {"new-finding", FailureRule::newFinding},
    FailureRuleName {"never", FailureRule::never},
};

const FindingAdvice& findAdvice(std::string_view biomarker)
{
    auto found =
        std::ranges::find(findingAdvice, biomarker, &FindingAdvice::biomarker);
    return found == findingAdvice.end() ? fallbackAdvice : *found;
}

struct Escape
{
    char character;
    std::string_view replacement;
};

// Workflow command data escapes %, CR and LF; property values also : and ,.
constexpr auto messageEscapes =
    std::array {Escape {'%', "%25"}, Escape {'\r', "%0D"}, Escape {'\n', "%0A"}};
constexpr auto propertyEscapes = std::array {Escape {'%', "%25"},
                                             Escape {'\r', "%0D"},
                                             Escape {'\n', "%0A"},
                                             Escape {':', "%3A"},
                                             Escape {',', "%2C"}};

template <size_t escapeCount>
std::string escapeText(std::string_view text,
                       const std::array<Escape, escapeCount>& escapes)
{
    auto escaped = std::string {};

    for (auto character: text)
    {
        auto found = std::ranges::find(escapes, character, &Escape::character);
        escaped += found == escapes.end() ? std::string(1, character)
                                          : std::string {found->replacement};
    }

    return escaped;
}

struct AnnotationLevel
{
    std::string_view command;
    bool showsPreviousValue = false;
};

constexpr auto newFindingLevel = AnnotationLevel {"error", false};
constexpr auto worsenedFindingLevel = AnnotationLevel {"warning", true};

std::string describeSubject(const FindingChange& finding)
{
    return finding.functionName.empty() ? std::string {"This file"}
                                        : finding.functionName;
}

std::string describePreviousValue(const FindingChange& finding,
                                  const AnnotationLevel& level)
{
    if (!level.showsPreviousValue)
        return {};

    return eacp::Strings::format(" (was {:g})", finding.valueBefore);
}

std::string describeFinding(const FindingChange& finding,
                            const AnnotationLevel& level)
{
    auto& advice = findAdvice(finding.biomarker);
    return eacp::Strings::format("{}: {:g} {}{}, limit {:g}: {}",
                                 describeSubject(finding),
                                 finding.valueAfter,
                                 advice.measured,
                                 describePreviousValue(finding, level),
                                 finding.threshold,
                                 advice.hint);
}

std::string formatAnnotation(const AnnotationLevel& level,
                             const FileChange& change,
                             const FindingChange& finding)
{
    return eacp::Strings::format(
        "::{} file={},line={},title={}::{}\n",
        level.command,
        escapeText(change.path, propertyEscapes),
        std::max(1, finding.line),
        escapeText("codehealth: " + finding.biomarker, propertyEscapes),
        escapeText(describeFinding(finding, level), messageEscapes));
}

std::string formatScoreCell(const std::optional<double>& score)
{
    return score ? eacp::Strings::format("{:.2f}", *score) : std::string {"new"};
}

int countNewFindings(const DiffReport& report)
{
    auto count = 0;

    for (auto& change: report.changes)
        count += static_cast<int>(change.introducedFindings.size());

    return count;
}

std::string describeFirstNewFinding(const DiffReport& report)
{
    for (auto& change: report.changes)
        for (auto& finding: change.introducedFindings)
            return eacp::Strings::format("{} in {} at {}:{}",
                                         finding.biomarker,
                                         describeSubject(finding),
                                         change.path,
                                         std::max(1, finding.line));

    return {};
}

bool isScoreDropOfExistingFile(const FileChange& change)
{
    if (!change.scoreBefore || !change.scoreAfter)
        return false;

    return *change.scoreAfter < *change.scoreBefore;
}

std::string describeFirstScoreDrop(const DiffReport& report)
{
    for (auto& change: report.changes)
        if (isScoreDropOfExistingFile(change))
            return eacp::Strings::format("{} {:.2f} -> {:.2f}",
                                         change.path,
                                         *change.scoreBefore,
                                         *change.scoreAfter);

    return {};
}

std::optional<std::string> describeNewFindings(const DiffReport& report)
{
    if (!report.hasNewFinding())
        return std::nullopt;

    return eacp::Strings::format("codehealth: {} new finding(s) in changed files, "
                                 "first: {}",
                                 countNewFindings(report),
                                 describeFirstNewFinding(report));
}
} // namespace

std::optional<FailureRule> findFailureRule(std::string_view name)
{
    auto found = std::ranges::find(failureRuleNames, name, &FailureRuleName::name);

    if (found == failureRuleNames.end())
        return std::nullopt;

    return found->rule;
}

std::optional<std::string> findGateFailure(const DiffReport& report,
                                           FailureRule rule)
{
    if (rule == FailureRule::never)
        return std::nullopt;

    if (auto newFindings = describeNewFindings(report))
        return newFindings;

    if (rule == FailureRule::scoreDrop && report.hasScoreDrop())
        return "codehealth: a changed file's score dropped: "
               + describeFirstScoreDrop(report);

    return std::nullopt;
}

std::string formatGitHubAnnotations(const DiffReport& report)
{
    auto text = std::string {};

    for (auto& change: report.changes)
    {
        for (auto& finding: change.introducedFindings)
            text += formatAnnotation(newFindingLevel, change, finding);

        for (auto& finding: change.worsenedFindings)
            text += formatAnnotation(worsenedFindingLevel, change, finding);
    }

    return text;
}

std::string formatGitHubStepSummary(const DiffReport& report)
{
    auto text =
        eacp::Strings::format("### codehealth: {} changed source files since {}\n\n"
                              "| File | Before | After | Change | New findings |\n"
                              "|---|---|---|---|---|\n",
                              report.changes.size(),
                              report.base);

    for (auto& change: report.changes)
        text += eacp::Strings::format("| `{}` | {} | {} | {:+.2f} | {} |\n",
                                      change.path,
                                      formatScoreCell(change.scoreBefore),
                                      formatScoreCell(change.scoreAfter),
                                      change.scoreChange,
                                      change.introducedFindings.size());

    return text;
}
std::string formatGitHubFailure(std::string_view reason)
{
    return "::error title=codehealth::" + escapeText(reason, messageEscapes) + "\n";
}
} // namespace CodeHealth
