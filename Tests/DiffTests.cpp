#include "FixtureRepository.h"

#include <Diff.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>

using namespace nano;
using namespace CodeHealth;
using namespace CodeHealth::Testing;

namespace
{
constexpr auto baseTime = 1700000000LL;
constexpr auto branchTime = 1700000600LL;
constexpr auto simpleBranches = 1;
constexpr auto complexBranches = 12;
constexpr auto worseBranches = 16;

std::string makeFunction(const std::string& name, int branches)
{
    auto body = std::string {};

    for (auto branch = 0; branch < branches; ++branch)
        body += "    if (a == " + std::to_string(branch) + ") return "
                + std::to_string(branch) + ";\n";

    return "int " + name + "(int a)\n{\n" + body + "    return -1;\n}\n";
}

const auto renamedContent = makeFunction("moved", simpleBranches);

FixtureRepository makeChangedRepository()
{
    auto repository = FixtureRepository {};
    repository.stageFile("degrade.cpp", makeFunction("work", simpleBranches));
    repository.stageFile("fix.cpp", makeFunction("tidy", complexBranches));
    repository.stageFile("dirty.cpp", makeFunction("edit", complexBranches));
    repository.stageFile("old_name.cpp", renamedContent);
    repository.stageFile("gone.cpp", makeFunction("bye", simpleBranches));
    repository.stageFile("util.h", "int helper();\n");
    repository.stageFile("user.cpp",
                         "#include \"util.h\"\nint use() { return helper(); }\n");
    repository.commit("Base", baseTime);
    repository.createBranch("base");

    repository.stageFile("degrade.cpp", makeFunction("work", complexBranches));
    repository.stageFile("fix.cpp", makeFunction("tidy", simpleBranches));
    repository.unstageFile("old_name.cpp");
    repository.stageFile("new_name.cpp", renamedContent);
    repository.unstageFile("gone.cpp");
    repository.commit("Branch", branchTime);
    repository.checkOutHead();

    repository.writeWorkingFile("dirty.cpp", makeFunction("edit", worseBranches));
    repository.writeWorkingFile("util.h", "int helper();\nint other();\n");
    repository.writeWorkingFile("lib/App.h", "int lib();\nint more();\n");
    repository.writeWorkingFile("untracked.cpp",
                                makeFunction("fresh", complexBranches));
    return repository;
}

// Built once: each test reads it and nothing writes to it.
const FixtureRepository& getChangedRepository()
{
    static const auto repository = makeChangedRepository();
    return repository;
}

const FileChange& findChange(const DiffReport& report, const std::string& path)
{
    auto found = std::ranges::find(report.changes, path, &FileChange::path);
    check(found != report.changes.end());
    return *found;
}

bool hasFinding(const std::vector<FindingChange>& findings,
                const std::string& function)
{
    return std::ranges::any_of(findings,
                               [&](auto& finding)
                               {
                                   return finding.biomarker == "Complex Method"
                                          && finding.functionName == function;
                               });
}
} // namespace

auto tDiffChanges =
    test("Diff/committed, uncommitted, untracked, renamed and deleted changes") = []
{
    auto& repository = getChangedRepository();
    auto report =
        analyseChanges({.repository = repository.getPath(), .base = "base"},
                       getDefaultScoringRules());

    auto& degraded = findChange(report, "degrade.cpp");
    check(degraded.scoreChange < 0.0);
    check(hasFinding(degraded.introducedFindings, "work"));

    auto& fixed = findChange(report, "fix.cpp");
    check(fixed.scoreChange > 0.0);
    check(hasFinding(fixed.fixedFindings, "tidy"));

    auto& dirty = findChange(report, "dirty.cpp");
    check(hasFinding(dirty.worsenedFindings, "edit"));
    check(dirty.worsenedFindings.front().valueAfter
          > dirty.worsenedFindings.front().valueBefore);

    auto& untracked = findChange(report, "untracked.cpp");
    check(!untracked.scoreBefore.has_value());
    check(hasFinding(untracked.introducedFindings, "fresh"));

    auto& renamed = findChange(report, "new_name.cpp");
    check(renamed.status == "R");
    check(renamed.previousPath == "old_name.cpp");
    check(renamed.introducedFindings.empty() && renamed.fixedFindings.empty());

    auto& deleted = findChange(report, "gone.cpp");
    check(deleted.status == "D");
    check(!deleted.scoreAfter.has_value());

    check(report.hasRegression());
};

auto tDiffNearbyDependents = test(
    "Diff/a file that includes a changed header is listed near the changes") = []
{
    auto& repository = getChangedRepository();
    auto report =
        analyseChanges({.repository = repository.getPath(), .base = "base"},
                       getDefaultScoringRules());

    check(std::ranges::any_of(
        report.nearbyFiles,
        [](auto& nearby)
        { return nearby.path == "user.cpp" && nearby.changedPath == "util.h"; }));
};

auto tDiffIncludeBesideFile =
    test("Diff/an include is resolved beside the including file first") = []
{
    auto& repository = getChangedRepository();
    auto report =
        analyseChanges({.repository = repository.getPath(), .base = "base"},
                       getDefaultScoringRules());

    check(std::ranges::none_of(report.nearbyFiles,
                               [](auto& nearby)
                               { return nearby.path == "hub/Main.cpp"; }));
};

auto tDiffSummaryOrder =
    test("Diff/the summary lists degraded files before improved ones") = []
{
    auto& repository = getChangedRepository();
    auto summary = formatDiffSummary(
        analyseChanges({.repository = repository.getPath(), .base = "base"},
                       getDefaultScoringRules()));

    check(summary.find("Degraded") < summary.find("Improved"));
    check(summary.find("Improved") != std::string::npos);
};

auto tDiffCleanTree =
    test("Diff/with HEAD as the base only working-tree changes show") = []
{
    auto& repository = getChangedRepository();
    auto report =
        analyseChanges({.repository = repository.getPath(), .base = "HEAD"},
                       getDefaultScoringRules());
    auto onlyWorkingTree = std::ranges::all_of(
        report.changes,
        [](auto& change) { return change.status == "M" || change.status == "?"; });
    check(onlyWorkingTree);
};
