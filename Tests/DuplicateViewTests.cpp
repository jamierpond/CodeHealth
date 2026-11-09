#include <DuplicateView.h>
#include <Report.h>
#include <Repository.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace CodeHealth;

namespace
{
constexpr auto fullSimilarity = 1.0;
constexpr auto fixtureTokenCount = 12;

FunctionClone makeClone()
{
    return {.firstPath = "a.cpp",
            .firstFunction = "addOne",
            .firstStartLine = 2,
            .firstEndLine = 5,
            .secondPath = "b.cpp",
            .secondFunction = "addTwo",
            .secondStartLine = 1,
            .secondEndLine = 4,
            .similarity = fullSimilarity,
            .tokenCount = fixtureTokenCount};
}

const auto firstSource = std::string {"// header\n"
                                      "int addOne(int value)\n"
                                      "{\n"
                                      "    return value + 1;\n"
                                      "}\n"};
const auto secondSource = std::string {"int addTwo(int value)\n"
                                       "{\n"
                                       "        return value + 2;\n"
                                       "}\n"};
} // namespace

auto tDiffMarksOnlyDifferingLines =
    test("DuplicateView/the diff marks differing lines and ignores indentation") = []
{
    auto diff = formatFunctionCloneDiff(
        makeClone(), {firstSource, secondSource}, OutputColouring::plain);

    check(diff.find("A int addOne(int value)\n") != std::string::npos);
    check(diff.find("B int addTwo(int value)\n") != std::string::npos);
    check(diff.find("A     return value + 1;\n") != std::string::npos);
    check(diff.find("B         return value + 2;\n") != std::string::npos);
    check(diff.find("  {\n") != std::string::npos);
    check(diff.find("// header") == std::string::npos);
    check(diff.find('\033') == std::string::npos);
};

auto tDiffColoursWhenAsked =
    test("DuplicateView/the diff is coloured only when asked") = []
{
    auto diff = formatFunctionCloneDiff(
        makeClone(), {firstSource, secondSource}, OutputColouring::terminalColours);

    check(diff.find("\033[34mA int addOne") != std::string::npos);
    check(diff.find("\033[38;5;208mB int addTwo") != std::string::npos);
};

auto tSummaryNumbersDuplicatedPairs = test(
    "DuplicateView/the summary numbers duplicated pairs from 1 in list order") = []
{
    auto analysis = Analysis {.functionClones = {makeClone(), makeClone()}};
    constexpr auto summaryEntryCount = 5;
    auto summary = formatHumanSummary(analysis, summaryEntryCount);

    check(summary.find("\n    1  100%") != std::string::npos);
    check(summary.find("\n    2  100%") != std::string::npos);
};

auto tTiedPairsKeepOneOrder = test(
    "DuplicateView/pairs of equal size are listed by path then line of A, then B") =
    []
{
    auto laterLineOfB = makeClone();
    laterLineOfB.secondStartLine += 1;
    auto laterPathOfA = makeClone();
    laterPathOfA.firstPath = "c.cpp";
    auto laterLineOfA = makeClone();
    laterLineOfA.firstStartLine += 1;

    auto listed =
        keepLargestClones({laterPathOfA, laterLineOfA, laterLineOfB, makeClone()});

    check(listed[0].secondStartLine == makeClone().secondStartLine);
    check(listed[0].firstStartLine == makeClone().firstStartLine);
    check(listed[1].secondStartLine == laterLineOfB.secondStartLine);
    check(listed[2].firstStartLine == laterLineOfA.firstStartLine);
    check(listed[3].firstPath == "c.cpp");
};
