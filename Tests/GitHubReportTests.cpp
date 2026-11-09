#include <GitHubReport.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace CodeHealth;

namespace
{
constexpr auto newFindingLine = 12;
constexpr auto branchCount = 14.0;
constexpr auto branchLimit = 9.0;
constexpr auto scoreBefore = 9.8;
constexpr auto scoreAfter = 9.1;

DiffReport makeReportWithNewComplexMethod(std::string path)
{
    auto change = FileChange {.path = std::move(path),
                              .status = "M",
                              .scoreBefore = scoreBefore,
                              .scoreAfter = scoreAfter,
                              .scoreChange = scoreAfter - scoreBefore};
    change.introducedFindings.push_back({.biomarker = "Complex Method",
                                         .functionName = "processOrder",
                                         .line = newFindingLine,
                                         .valueAfter = branchCount,
                                         .threshold = branchLimit});
    return {.base = "origin/main", .changes = {change}};
}
} // namespace

auto tGitHubAnnotation = test(
    "GitHubReport/a new finding is an error on its file and line, with the number, the limit and a fix") =
    []
{
    auto annotations =
        formatGitHubAnnotations(makeReportWithNewComplexMethod("src/orders.py"));

    check(
        annotations
        == "::error file=src/orders.py,line=12,title=codehealth%3A Complex Method::"
           "processOrder: 14 branches, limit 9: split the decisions into named "
           "functions\n");
};

auto tGitHubAnnotationEscapes =
    test("GitHubReport/commas and colons in a path are escaped") = []
{
    auto annotations =
        formatGitHubAnnotations(makeReportWithNewComplexMethod("a,b:c.py"));
    check(annotations.find("file=a%2Cb%3Ac.py,") != std::string::npos);
};

auto tGitHubGate =
    test("GitHubReport/each failure rule fails for its reason only") = []
{
    auto report = makeReportWithNewComplexMethod("a.py");
    auto onlyDropped = report;
    onlyDropped.changes.front().introducedFindings.clear();

    check(findGateFailure(report, FailureRule::newFinding).has_value());
    check(!findGateFailure(report, FailureRule::never).has_value());
    check(!findGateFailure(onlyDropped, FailureRule::newFinding).has_value());
    check(findGateFailure(onlyDropped, FailureRule::scoreDrop)
          == "codehealth: a changed file's score dropped: a.py 9.80 -> 9.10");
    check(findFailureRule("new-finding") == FailureRule::newFinding);
    check(!findFailureRule("sometimes").has_value());
};

auto tGitHubSummary =
    test("GitHubReport/the step summary has a row per changed file") = []
{
    auto summary = formatGitHubStepSummary(makeReportWithNewComplexMethod("a.py"));
    check(summary.find("| `a.py` | 9.80 | 9.10 | -0.70 | 1 |") != std::string::npos);
};
