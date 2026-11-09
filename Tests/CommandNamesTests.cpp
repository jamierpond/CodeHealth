#include <CommandNames.h>
#include <Report.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace CodeHealth;

namespace
{
const auto commandNames =
    std::vector<std::string_view> {"diff", "health", "hotspots", "duplicates"};

using Names = std::vector<std::string_view>;
} // namespace

auto tUnambiguousPrefixPicksOneName =
    test("CommandNames/any unambiguous prefix picks its one name") = []
{
    check(findNamesMatchingPrefix(commandNames, "dup") == Names {"duplicates"});
    check(findNamesMatchingPrefix(commandNames, "dupl") == Names {"duplicates"});
    check(findNamesMatchingPrefix(commandNames, "duplicates")
          == Names {"duplicates"});
};

auto tAmbiguousPrefixListsEveryMatch =
    test("CommandNames/an ambiguous prefix lists every name it could mean") = []
{
    check(findNamesMatchingPrefix(commandNames, "d")
          == Names {"diff", "duplicates"});
    check(findNamesMatchingPrefix(commandNames, "h")
          == Names {"health", "hotspots"});
};

auto tUnknownWordMatchesNothing =
    test("CommandNames/a word that starts no name, or no word, matches nothing") = []
{
    check(findNamesMatchingPrefix(commandNames, "repo").empty());
    check(findNamesMatchingPrefix(commandNames, "").empty());
};

auto tSectionsMakeUpTheSummary =
    test("CommandNames/the report's sections printed alone make up its summary") = []
{
    constexpr auto summaryEntryCount = 5;
    auto analysis = Analysis {.revision = "HEAD"};
    auto summary = formatHumanSummary(analysis, summaryEntryCount);

    for (auto& section: getReportSections())
    {
        auto alone = formatReportSection(section, analysis, summaryEntryCount);
        check(alone.front() != '\n');
        check(summary.find(alone) != std::string::npos);
    }
};
