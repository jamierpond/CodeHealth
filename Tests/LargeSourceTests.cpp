#include "Common.h"

#include <eacp/Core/Threads/Thread.h>

#include <chrono>
#include <string>

using namespace nano;
using namespace CodeHealth;
using namespace CodeHealth::Testing;

namespace
{
constexpr auto initializerElementCount = 200000;
constexpr auto topLevelFunctionCount = 20000;
constexpr auto generousSecondsForLinearWork = 5.0;
constexpr auto deepNestingLevels = 20000;
constexpr auto longConditionOperands = 50000;

std::string makeLargeInitializer()
{
    auto source = std::string {"static const int table[] = {\n"};

    for (auto index = 0; index < initializerElementCount; ++index)
        source += std::to_string(index) + ",\n";

    return source + "};\nint first(void) { return table[0]; }\n";
}

std::string makeManyFunctions()
{
    auto source = std::string {};

    for (auto index = 0; index < topLevelFunctionCount; ++index)
        source +=
            "int f" + std::to_string(index) + "(int a) { return a ? 1 : 0; }\n";

    return source;
}

std::string makeDeeplyNestedFunction()
{
    auto source = std::string {"int deep(int a) {\n"};
    source += std::string(deepNestingLevels, '{');
    source += "if (a";

    for (auto index = 0; index < longConditionOperands; ++index)
        source += " || a";

    source += ") return 1;";
    source += std::string(deepNestingLevels, '}');
    return source + "\nreturn 0; }\n";
}

double measureSeconds(const std::function<void()>& work)
{
    auto start = std::chrono::steady_clock::now();
    work();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
        .count();
}
} // namespace

auto tLargeInitializer =
    test("LargeSource/a node with many children is measured in linear time") = []
{
    auto source = makeLargeInitializer();
    auto file = FileMetrics {};
    auto seconds = measureSeconds([&] { file = measureFixture(source, "table.c"); });

    check(file.functions.size() == 1);
    check(file.linesOfCode == initializerElementCount + 3);
    check(seconds < generousSecondsForLinearWork);
};

auto tManyFunctions =
    test("LargeSource/a file with many functions is measured in linear time") = []
{
    auto source = makeManyFunctions();
    auto file = FileMetrics {};
    auto seconds = measureSeconds([&] { file = measureFixture(source, "many.c"); });

    check(file.functions.size() == topLevelFunctionCount);
    check(file.functions.back().cyclomaticComplexity == 2);
    check(seconds < generousSecondsForLinearWork);
};

auto tDeepNestingOnWorkerThread = test(
    "LargeSource/deeply nested source is measured on a worker thread's small stack") =
    []
{
    auto source = makeDeeplyNestedFunction();
    auto file = FileMetrics {};
    auto worker =
        eacp::Threads::Thread {[&] { file = measureFixture(source, "deep.c"); }};
    worker.join();

    check(file.functions.size() == 1);
    check(file.functions.front().cyclomaticComplexity == longConditionOperands + 2);
    check(file.functions.front().complexConditionalCount == 1);
};
