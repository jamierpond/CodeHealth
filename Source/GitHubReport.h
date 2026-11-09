#pragma once

#include "Diff.h"

#include <optional>
#include <string>

namespace CodeHealth
{
// What makes `codehealth diff` fail a CI job.
enum class FailureRule
{
    scoreDrop,
    newFinding,
    never
};

// "score-drop" (a changed file's score fell or it gained a finding),
// "new-finding" or "never"; nothing for any other name.
std::optional<FailureRule> findFailureRule(std::string_view name);

// One line saying why the job fails, or nothing when it passes.
std::optional<std::string> findGateFailure(const DiffReport& report,
                                           FailureRule rule);

// GitHub Actions workflow commands: an ::error for each new finding and a
// ::warning for each worsened one, on its file and line, with a fix hint.
std::string formatGitHubAnnotations(const DiffReport& report);

// The gate's failure as an ::error, so it shows on the run's summary page.
std::string formatGitHubFailure(std::string_view reason);

// A Markdown table of the changed files' scores, before and after.
std::string formatGitHubStepSummary(const DiffReport& report);
} // namespace CodeHealth
