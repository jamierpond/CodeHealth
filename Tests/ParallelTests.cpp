#include "FixtureRepository.h"

#include <Report.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace CodeHealth;
using namespace CodeHealth::Testing;

namespace
{
constexpr auto fixtureFileCount = 40;
constexpr auto fixtureCommitCount = 12;
constexpr auto fixtureStartTime = 1700000000LL;
constexpr auto manyJobs = 8;

std::string makeFixtureSource(int fileIndex, int commitIndex)
{
    auto source = std::string {};

    for (auto function = 0; function <= fileIndex % 7 + commitIndex % 3; ++function)
        source += "int f" + std::to_string(function)
                  + "(int a, int b) { if (a && b || a) "
                    "{ for (;;) { if (b) { return 1; } } } return a ? b : 0; }\n";

    return source;
}

std::string analyseAsJson(const eacp::FilePath& repository, int jobCount)
{
    auto options = AnalysisOptions {.repository = repository, .jobCount = jobCount};
    return formatJson(analyseRepository(options, getDefaultScoringRules()));
}
} // namespace

auto tParallelFixture = test(
    "Parallel/output is identical with one job and with many, on a fixture") = []
{
    auto repository = FixtureRepository {};

    for (auto commitIndex = 0; commitIndex < fixtureCommitCount; ++commitIndex)
    {
        for (auto file = 0; file < fixtureFileCount; ++file)
            if ((file + commitIndex) % 3 != 0)
                repository.stageFile("file" + std::to_string(file) + ".cpp",
                                     makeFixtureSource(file, commitIndex));

        repository.commit("Fixture", fixtureStartTime + commitIndex);
    }

    check(analyseAsJson(repository.getPath(), 1)
          == analyseAsJson(repository.getPath(), manyJobs));
};

auto tParallelEacp =
    test("Parallel/output is identical with one job and with many, on eacp") = []
{
    auto repository = eacp::FilePath {CODEHEALTH_EACP_SOURCE_DIR};
    check(analyseAsJson(repository, 1) == analyseAsJson(repository, manyJobs));
};
