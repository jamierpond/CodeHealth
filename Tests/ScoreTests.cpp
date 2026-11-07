#include "Common.h"

#include <Score.h>

using namespace nano;
using namespace CodeHealth;

namespace
{
constexpr auto largeFileLineCount = 2000;
constexpr auto complexMethodComplexity = 30;
constexpr auto deepNestingDepth = 6;

FileMetrics makeFileWithOneFunction(FunctionMetrics function)
{
    return {
        .path = "a.cpp", .linesOfCode = function.length, .functions = {function}};
}
} // namespace

auto tScoreHealthyFile = test("Score/a file with no findings scores ten") = []
{
    auto health =
        assessFileHealth(makeFileWithOneFunction({.name = "f", .length = 5}));
    check(health.score == 10.0);
    check(health.findings.empty());
};

auto tScoreFindingsLower = test("Score/each biomarker lowers the score") = []
{
    auto complex = assessFileHealth(
        makeFileWithOneFunction({.name = "f",
                                 .length = 5,
                                 .cyclomaticComplexity = complexMethodComplexity}));
    auto complexAndNested = assessFileHealth(
        makeFileWithOneFunction({.name = "f",
                                 .length = 5,
                                 .cyclomaticComplexity = complexMethodComplexity,
                                 .maximumNestingDepth = deepNestingDepth}));

    check(complex.score < 10.0);
    check(complexAndNested.score < complex.score);
    check(complexAndNested.findings.size() == complex.findings.size() + 1);
};

auto tScoreLargeFile = test("Score/a large file is a file-level finding") = []
{
    auto health =
        assessFileHealth({.path = "big.cpp", .linesOfCode = largeFileLineCount});
    check(health.findings.size() == 1);
    check(health.findings.front().biomarker == "Large File");
};

auto tScoreFloor = test("Score/never drops below one") = []
{
    auto file = FileMetrics {.path = "a.cpp", .linesOfCode = largeFileLineCount};
    constexpr auto manyFunctions = 500;

    for (auto index = 0; index < manyFunctions; ++index)
        file.functions.push_back({.name = "f",
                                  .length = largeFileLineCount,
                                  .cyclomaticComplexity = complexMethodComplexity,
                                  .maximumNestingDepth = deepNestingDepth});

    check(assessFileHealth(file).score >= 1.0);
};
