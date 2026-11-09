#include <Duplication.h>
#include <Languages.h>
#include <Metrics.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace CodeHealth;

namespace
{
constexpr auto sequenceLength = 150u;
constexpr auto firstAlphabet = 7u;
constexpr auto secondAlphabet = 5u;
constexpr auto secondStride = 3u;
constexpr auto testJobCount = 2;
constexpr auto linesPerFixtureFunction = 13;

// Varied statements, so the function does not repeat itself.
std::string makeLongFunction(const std::string& name, const std::string& v)
{
    return "int " + name + "(int " + v + ", int limit)\n{\n" + "    if (" + v
           + " < 0) return -1;\n" + "    int total = 0;\n"
           + "    for (int i = 0; i < limit; ++i) total += i * " + v + ";\n"
           + "    while (total > 1000) total /= 2;\n" + "    switch (" + v
           + " % 3) { case 0: total += 7; break; case 1: total -= 3; break; }\n"
           + "    char buffer[16] = {};\n"
           + "    buffer[0] = static_cast<char>(total & 0xff);\n"
           + "    if (buffer[0] == 'x' && limit > 2) return total + limit;\n"
           + "    do { total ^= limit; } while (total < limit);\n"
           + "    return total - " + v + ";\n}\n";
}

struct FoundDuplication
{
    std::vector<FunctionClone> functions;
    std::vector<int> duplicatedLineCounts;
};

FoundDuplication
    findClonesIn(const std::vector<std::pair<std::string, std::string>>& files)
{
    auto functions = std::vector<std::vector<FunctionTokens>> {};
    auto paths = std::vector<std::string> {};

    for (auto& [path, source]: files)
    {
        auto measured =
            SourceParser {}.measure(source, path, findLanguageForPath(path)->get());
        functions.push_back(measured.functionTokens);
        paths.push_back(path);
    }

    auto found = FoundDuplication {
        .functions = findFunctionClones(functions, paths, testJobCount)};
    found.duplicatedLineCounts = countDuplicatedLines(found.functions, paths);
    return found;
}

size_t
    measureLongestCommonSubsequenceByTable(const std::vector<std::uint32_t>& first,
                                           const std::vector<std::uint32_t>& second)
{
    auto table = std::vector<std::vector<size_t>>(
        first.size() + 1, std::vector<size_t>(second.size() + 1, 0));

    for (auto row = size_t {1}; row <= first.size(); ++row)
        for (auto column = size_t {1}; column <= second.size(); ++column)
        {
            if (first[row - 1] == second[column - 1])
                table[row][column] = table[row - 1][column - 1] + 1;
            else
                table[row][column] =
                    std::max(table[row - 1][column], table[row][column - 1]);
        }

    return table.back().back();
}
} // namespace

auto tRenamedCopyAcrossFiles = test(
    "Duplication/a copy with renamed identifiers in another file is a function clone") =
    []
{
    auto found = findClonesIn({{"a.cpp", makeLongFunction("first", "value")},
                               {"b.cpp", makeLongFunction("second", "amount")}});

    check(found.functions.size() == 1);
    check(found.functions.front().firstFunction == "first");
    check(found.functions.front().secondFunction == "second");
    check(found.functions.front().similarity == 1.0);
    check(found.duplicatedLineCounts == std::vector<int> {0, 0});
};

auto tEditedCopy = test(
    "Duplication/a copy with a few statements changed is still a function clone") =
    []
{
    auto edited = makeLongFunction("second", "amount");
    edited.replace(edited.find("total /= 2"),
                   std::string {"total /= 2"}.size(),
                   "total = total / 4 + 1");

    auto found = findClonesIn(
        {{"a.cpp", makeLongFunction("first", "value")}, {"b.cpp", edited}});
    check(found.functions.size() == 1);
    check(found.functions.front().similarity < 1.0);
};

auto tCopyWithinFile = test(
    "Duplication/a copy within one file is a clone and its lines count as duplicated") =
    []
{
    auto found = findClonesIn({{"a.cpp",
                                makeLongFunction("first", "value")
                                    + makeLongFunction("second", "other")}});

    check(found.functions.size() == 1);
    check(found.functions.front().firstStartLine
          < found.functions.front().secondStartLine);
    check(found.duplicatedLineCounts.front() >= 2 * linesPerFixtureFunction);
};

auto tDuplicatedFunctionCount =
    test("Duplication/each function cloned within its file counts once") = []
{
    auto source = makeLongFunction("first", "value")
                  + makeLongFunction("second", "other")
                  + makeLongFunction("third", "count");
    auto found = findClonesIn({{"a.cpp", source}, {"b.cpp", source}});
    auto counts = countDuplicatedFunctions(found.functions, {"a.cpp", "b.cpp"});

    check(counts == std::vector<int> {3, 3});
};

auto tCopyWithinFileInLargeRepository = test(
    "Duplication/a copy within one file is found even when its code is common across the repository") =
    []
{
    constexpr auto filesSharingTheCode = 300;
    auto files = std::vector<std::pair<std::string, std::string>> {
        {"a.cpp",
         makeLongFunction("first", "value") + makeLongFunction("second", "value")}};

    for (auto index = 0; index < filesSharingTheCode; ++index)
        files.emplace_back("other" + std::to_string(index) + ".cpp",
                           makeLongFunction("copy", "value"));

    auto found = findClonesIn(files);
    auto isWithinFirstFile = [](auto& clone)
    { return clone.firstPath == "a.cpp" && clone.secondPath == "a.cpp"; };

    check(std::ranges::any_of(found.functions, isWithinFirstFile));
};

auto tDifferentCodeIsNotAClone =
    test("Duplication/unrelated code shares no clone") = []
{
    auto found = findClonesIn(
        {{"a.cpp", makeLongFunction("first", "value")},
         {"b.cpp", "struct S { int a; int b; };\nint g() { return 0; }\n"}});

    check(found.functions.empty());
    check(found.duplicatedLineCounts == std::vector<int> {0, 0});
};

auto tLongestCommonSubsequence = test(
    "Duplication/the bit-parallel longest common subsequence matches the textbook one") =
    []
{
    auto first = std::vector<std::uint32_t> {};
    auto second = std::vector<std::uint32_t> {};

    for (auto index = 0u; index < sequenceLength; ++index)
    {
        first.push_back(index % firstAlphabet);
        second.push_back((index * secondStride) % secondAlphabet);
    }

    check(measureLongestCommonSubsequence(first, second)
          == measureLongestCommonSubsequenceByTable(first, second));
};
