#include "Common.h"

#include <Score.h>

#include <cmath>

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

auto tScoreBrainMethod =
    test("Score/a long, complex and deeply nested function is a Brain Method") = []
{
    constexpr auto brainLength = 120;
    constexpr auto brainComplexity = 30;
    constexpr auto brainNesting = 6;
    constexpr auto shallowNesting = 2;

    auto brain = assessFileHealth(
        makeFileWithOneFunction({.name = "f",
                                 .length = brainLength,
                                 .cyclomaticComplexity = brainComplexity,
                                 .maximumNestingDepth = brainNesting}));
    auto shallow = assessFileHealth(
        makeFileWithOneFunction({.name = "f",
                                 .length = brainLength,
                                 .cyclomaticComplexity = brainComplexity,
                                 .maximumNestingDepth = shallowNesting}));
    auto isBrainMethod = [](auto& finding)
    { return finding.biomarker == "Brain Method"; };

    check(std::ranges::any_of(brain.findings, isBrainMethod));
    check(std::ranges::none_of(shallow.findings, isBrainMethod));
};

namespace
{
constexpr auto largeModuleLines = 600;
constexpr auto smallModuleLines = 500;
constexpr auto clonedFunctions = 10;
constexpr auto belowDuplicationLines = 60;
constexpr auto unrelatedGroups = 16;
constexpr auto moduleFunctionCount = 76;

bool hasFinding(const FileHealth& health, std::string_view biomarker)
{
    return std::ranges::any_of(health.findings,
                               [&](auto& finding)
                               { return finding.biomarker == biomarker; });
}

FileMetrics makeModule(std::string path, int linesOfCode)
{
    auto file = FileMetrics {.linesOfCode = linesOfCode};
    file.path = std::move(path);
    file.language = findLanguageForPath(file.path)->get().name;
    return file;
}

FileMetrics makeModuleWithFunctions(std::string path, int count, std::string prefix)
{
    auto file = makeModule(std::move(path), largeModuleLines);

    for (auto index = 0; index < count; ++index)
        file.functions.push_back(
            {.name = prefix + std::to_string(index), .length = 2});

    return file;
}
} // namespace

auto tScorePythonDuplication = test(
    "Score/Python duplication counts cloned functions in files of 70 lines or more") =
    []
{
    auto python = makeModule("a.py", largeModuleLines);
    python.duplicatedFunctionCount = clonedFunctions;
    auto smallPython = makeModule("b.py", belowDuplicationLines);
    smallPython.duplicatedFunctionCount = clonedFunctions;
    auto cpp = makeModule("a.cpp", largeModuleLines);
    cpp.duplicatedFunctionCount = clonedFunctions;

    auto health = assessFileHealth(python);
    check(hasFinding(health, "Code Duplication"));
    check(health.score < 9.0);
    check(!hasFinding(assessFileHealth(smallPython), "Code Duplication"));
    check(!hasFinding(assessFileHealth(cpp), "Code Duplication"));
};

auto tScorePythonModuleCohesion = test(
    "Score/Python Low Cohesion judges the whole module, from 550 lines of code") = []
{
    auto large = makeModule("a.py", largeModuleLines);
    large.moduleCohesionGroups = unrelatedGroups;
    auto small = makeModule("b.py", smallModuleLines);
    small.moduleCohesionGroups = unrelatedGroups;
    auto cpp = makeModule("a.cpp", largeModuleLines);
    cpp.moduleCohesionGroups = unrelatedGroups;

    check(hasFinding(assessFileHealth(large), "Low Cohesion"));
    check(!hasFinding(assessFileHealth(small), "Low Cohesion"));
    check(!hasFinding(assessFileHealth(cpp), "Low Cohesion"));
};

auto tScorePythonFunctionCount = test(
    "Score/a large Python module with 76 functions is flagged; nested functions and other languages are not") =
    []
{
    auto python = makeModuleWithFunctions("a.py", moduleFunctionCount, "f");
    auto nested =
        makeModuleWithFunctions("b.py", moduleFunctionCount, "outer.inner");
    auto cpp = makeModuleWithFunctions("a.cpp", moduleFunctionCount, "f");
    auto rule = "Number of Functions in a Single Module";

    check(hasFinding(assessFileHealth(python), rule));
    check(!hasFinding(assessFileHealth(nested), rule));
    check(!hasFinding(assessFileHealth(cpp), rule));
};

auto tScorePythonLongMethodCostGrowsLinearly = test(
    "Score/a Python method's length costs in proportion, so 584 lines scores like CodeScene's 7.21") =
    []
{
    constexpr auto longMethodLines = 584;
    constexpr auto codesceneScore = 7.21;
    constexpr auto tolerance = 0.05;

    auto file = makeModule("a.py", longMethodLines + 2);
    file.functions.push_back({.name = "probe", .length = longMethodLines});

    check(std::abs(assessFileHealth(file).score - codesceneScore) < tolerance);
};
