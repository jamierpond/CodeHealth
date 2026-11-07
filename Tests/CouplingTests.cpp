#include <Coupling.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace CodeHealth;

namespace
{
constexpr auto togetherCommitCount = 12;
constexpr auto aloneCommitCount = 20;
constexpr auto filesInSweep = 60;

std::vector<Commit> makeHistory()
{
    auto commits = std::vector<Commit> {};

    for (auto index = 0; index < togetherCommitCount; ++index)
        commits.push_back({.paths = {"a.cpp", "a.h", "unrelated.cpp"}});

    for (auto index = 0; index < aloneCommitCount; ++index)
        commits.push_back({.paths = {"unrelated.cpp"}});

    auto sweep = Commit {};

    for (auto index = 0; index < filesInSweep; ++index)
        sweep.paths.push_back("sweep" + std::to_string(index) + ".cpp");

    commits.push_back(sweep);
    return commits;
}
} // namespace

auto tCouplingFindsPairs = test(
    "Coupling/files changed together are coupled, by share of the less-changed one") =
    []
{
    auto couplings =
        findChangeCoupling(makeHistory(), {"a.cpp", "a.h", "unrelated.cpp"});

    check(couplings.size() == 3);
    check(couplings.front().sharedCommits == togetherCommitCount);
    check(couplings.front().couplingPercentage == 100.0);
};

auto tCouplingIgnoresSweeps =
    test("Coupling/a commit touching many files couples nothing") = []
{
    auto paths = std::vector<std::string> {};

    for (auto index = 0; index < filesInSweep; ++index)
        paths.push_back("sweep" + std::to_string(index) + ".cpp");

    check(findChangeCoupling(makeHistory(), paths).empty());
};

auto tCouplingOnlyAnalysedFiles =
    test("Coupling/only files being analysed are paired") = []
{
    auto couplings = findChangeCoupling(makeHistory(), {"a.cpp", "unrelated.cpp"});
    check(couplings.size() == 1);
    check(couplings.front().firstPath == "a.cpp");
};
