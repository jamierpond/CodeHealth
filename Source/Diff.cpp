#include "Diff.h"
#include "Coupling.h"
#include "History.h"
#include "Languages.h"
#include "Repository.h"

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Format.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <map>
#include <set>

namespace CodeHealth
{
namespace
{
constexpr auto nearbyCouplingLimit = size_t {5};
constexpr auto nearbyDependentLimit = size_t {5};
constexpr auto scoreTolerance = 0.005;

using FindingKey = std::pair<std::string, std::string>;

struct ChangedPath
{
    std::string status;
    std::string path;
    std::string previousPath;
};

bool isAnalysedPath(const std::string& path)
{
    return findLanguageForPath(path) && !isExcludedPath(path);
}

std::string resolveBase(const DiffOptions& options)
{
    if (!options.base.empty())
        return options.base;

    try
    {
        return eacp::Strings::trim(runGit(
            options.repository, {"rev-parse", "--abbrev-ref", "origin/HEAD"}));
    }
    catch (const std::exception&)
    {
        return "HEAD";
    }
}

std::vector<std::string> keepAnalysedPaths(std::string_view nulSeparatedPaths)
{
    auto paths = std::vector<std::string> {};

    for (auto& path: eacp::Strings::split(nulSeparatedPaths, '\0'))
        if (isAnalysedPath(path))
            paths.push_back(path);

    return paths;
}

bool isRenameOrCopy(const std::string& status)
{
    return status == "R" || status == "C";
}

// `git diff --name-status -z` gives a status, then one path, or two for a
// rename or copy.
std::vector<ChangedPath> parseNameStatus(const eacp::Vector<std::string>& tokens)
{
    auto changes = std::vector<ChangedPath> {};

    for (auto index = size_t {0}; index + 1 < tokens.size(); index += 2)
    {
        auto status = tokens[index].substr(0, 1);
        auto change = ChangedPath {.status = status, .path = tokens[index + 1]};

        if (status != "A")
            change.previousPath = change.path;

        if (isRenameOrCopy(status))
        {
            ++index;
            change.path = tokens[index + 1];
        }

        if (isAnalysedPath(change.path) || isAnalysedPath(change.previousPath))
            changes.push_back(change);
    }

    return changes;
}

std::vector<ChangedPath> listChangedPaths(const eacp::FilePath& repository,
                                          const std::string& mergeBase)
{
    auto changes = parseNameStatus(eacp::Strings::split(
        runGit(repository, {"diff", "--name-status", "-z", "-M", mergeBase}), '\0'));
    auto untracked =
        runGit(repository, {"ls-files", "--others", "--exclude-standard", "-z"});

    for (auto& path: keepAnalysedPaths(untracked))
        changes.push_back({.status = "?", .path = path});

    return changes;
}

// What another file would write to include or import this one: a header by its
// file name, a module by its stem; a .c or .cpp is not included.
std::string chooseMentionSearchTerm(const std::string& changedPath)
{
    auto file = eacp::FilePath {changedPath};

    if (file.hasExtension(".c") || file.hasExtension(".cpp"))
        return {};

    if (file.hasExtension(".h") || file.hasExtension(".hpp"))
        return file.fileName();

    return file.stem();
}

FileHealth measureFile(SourceParser& parser,
                       const std::string& path,
                       const std::string& content,
                       const ScoringRules& rules)
{
    auto& language = findLanguageForPath(path)->get();
    return assessFileHealth(parser.measure(content, path, language), rules);
}

std::map<FindingKey, Finding> indexFindings(const std::optional<FileHealth>& file)
{
    auto findings = std::map<FindingKey, Finding> {};

    if (file)
        for (auto& finding: file->findings)
            findings.emplace(FindingKey {finding.biomarker, finding.functionName},
                             finding);

    return findings;
}

FindingChange describeFinding(const Finding& after, double valueBefore)
{
    return {.biomarker = after.biomarker,
            .functionName = after.functionName,
            .line = after.line,
            .valueBefore = valueBefore,
            .valueAfter = after.value};
}

void collectIntroducedAndWorsened(
    const std::map<FindingKey, Finding>& findingsBefore,
    const std::map<FindingKey, Finding>& findingsAfter,
    FileChange& change)
{
    for (auto& [key, finding]: findingsAfter)
    {
        auto previous = findingsBefore.find(key);

        if (previous == findingsBefore.end())
            change.introducedFindings.push_back(describeFinding(finding, 0.0));
        else if (finding.value > previous->second.value)
            change.worsenedFindings.push_back(
                describeFinding(finding, previous->second.value));
    }
}

void collectFixed(const std::map<FindingKey, Finding>& findingsBefore,
                  const std::map<FindingKey, Finding>& findingsAfter,
                  FileChange& change)
{
    for (auto& [key, finding]: findingsBefore)
        if (!findingsAfter.contains(key))
            change.fixedFindings.push_back({.biomarker = finding.biomarker,
                                            .functionName = finding.functionName,
                                            .line = finding.line,
                                            .valueBefore = finding.value});
}

void compareFindings(const std::optional<FileHealth>& before,
                     const std::optional<FileHealth>& after,
                     FileChange& change)
{
    auto findingsBefore = indexFindings(before);
    auto findingsAfter = indexFindings(after);
    collectIntroducedAndWorsened(findingsBefore, findingsAfter, change);
    collectFixed(findingsBefore, findingsAfter, change);
}

std::optional<double> getScore(const std::optional<FileHealth>& file)
{
    return file ? std::optional {file->score} : std::nullopt;
}

class ChangeAnalyser
{
public:
    ChangeAnalyser(const DiffOptions& optionsToUse, const ScoringRules& rulesToUse)
        : options(optionsToUse)
        , rules(rulesToUse)
    {
    }

    DiffReport analyse()
    {
        auto report = DiffReport {.base = resolveBase(options)};
        report.mergeBase = eacp::Strings::trim(
            runGit(options.repository, {"merge-base", report.base, "HEAD"}));

        auto changedPaths = listChangedPaths(options.repository, report.mergeBase);
        auto contentsBefore = readContentsBefore(report.mergeBase, changedPaths);

        for (auto index = size_t {0}; index < changedPaths.size(); ++index)
            report.changes.push_back(
                describeChange(changedPaths[index], contentsBefore[index]));

        std::ranges::stable_sort(
            report.changes, std::ranges::less {}, &FileChange::scoreChange);
        report.nearbyFiles = findNearbyFiles(report.changes);
        return report;
    }

private:
    std::vector<std::string>
        readContentsBefore(const std::string& mergeBase,
                           const std::vector<ChangedPath>& changedPaths)
    {
        auto previousPaths = std::vector<std::string> {};

        for (auto& change: changedPaths)
            previousPaths.push_back(
                change.previousPath.empty() ? change.path : change.previousPath);

        return readFilesAtRef(options.repository, mergeBase, previousPaths);
    }

    std::optional<FileHealth> measureWorkingTreeFile(const std::string& path)
    {
        auto onDisk = options.repository / path;

        if (!isAnalysedPath(path) || !eacp::Files::exists(onDisk))
            return std::nullopt;

        return measureFile(parser, path, eacp::Files::readFile(onDisk), rules);
    }

    FileChange describeChange(const ChangedPath& changed,
                              const std::string& contentBefore)
    {
        auto before = std::optional<FileHealth> {};

        if (!changed.previousPath.empty() && isAnalysedPath(changed.previousPath))
            before = measureFile(parser, changed.previousPath, contentBefore, rules);

        auto after = measureWorkingTreeFile(changed.path);
        auto change = FileChange {.path = changed.path,
                                  .previousPath = changed.previousPath,
                                  .status = changed.status,
                                  .scoreBefore = getScore(before),
                                  .scoreAfter = getScore(after)};

        if (before && after)
            change.scoreChange = after->score - before->score;

        compareFindings(before, after, change);
        return change;
    }

    std::vector<NearbyFile> findNearbyFiles(const std::vector<FileChange>& changes)
    {
        auto changed = std::set<std::string> {};

        for (auto& change: changes)
            changed.insert(change.path);

        auto nearby = findCoupledFiles(changed);
        auto dependents = findDependents(changed);
        nearby.insert(nearby.end(), dependents.begin(), dependents.end());
        return nearby;
    }

    std::vector<NearbyFile> findCoupledFiles(const std::set<std::string>& changed)
    {
        auto trackedPaths =
            keepAnalysedPaths(runGit(options.repository, {"ls-files", "-z"}));
        auto commits = readCommits(options.repository, "HEAD", options.since);
        auto nearby = std::vector<NearbyFile> {};
        auto listed = std::set<std::string> {};

        for (auto& coupling: findChangeCoupling(commits, trackedPaths))
        {
            if (nearby.size() == nearbyCouplingLimit)
                break;

            auto partner = describeCouplingPartner(coupling, changed);

            if (partner && listed.insert(partner->path).second)
                nearby.push_back(*partner);
        }

        return nearby;
    }

    std::optional<NearbyFile>
        describeCouplingPartner(const Coupling& coupling,
                                const std::set<std::string>& changed)
    {
        auto isFirstChanged = changed.contains(coupling.firstPath);

        if (isFirstChanged == changed.contains(coupling.secondPath))
            return std::nullopt;

        auto& partner = isFirstChanged ? coupling.secondPath : coupling.firstPath;
        auto health = measureWorkingTreeFile(partner);
        return NearbyFile {
            .path = partner,
            .changedPath = isFirstChanged ? coupling.firstPath : coupling.secondPath,
            .reason = "changes with",
            .sharedCommits = coupling.sharedCommits,
            .commitCount = isFirstChanged ? coupling.firstCommitCount
                                          : coupling.secondCommitCount,
            .score = health ? health->score : 10.0};
    }

    std::vector<NearbyFile> findDependents(const std::set<std::string>& changed)
    {
        auto dependents = std::vector<NearbyFile> {};

        for (auto& changedPath: changed)
            for (auto& candidate: findFilesMentioning(changedPath))
            {
                if (dependents.size() == nearbyDependentLimit)
                    return dependents;

                auto isListed = std::ranges::any_of(
                    dependents,
                    [&](auto& dependent) { return dependent.path == candidate; });

                if (changed.contains(candidate) || isListed)
                    continue;

                auto health = measureWorkingTreeFile(candidate);

                if (health && dependsOn(*health, changedPath))
                    dependents.push_back({.path = candidate,
                                          .changedPath = changedPath,
                                          .reason = "depends on",
                                          .score = health->score});
            }

        return dependents;
    }

    // `git grep` exits 1 when nothing matches, which reads as no files here.
    std::vector<std::string> findFilesMentioning(const std::string& changedPath)
    {
        auto searchTerm = chooseMentionSearchTerm(changedPath);

        if (searchTerm.empty())
            return {};

        try
        {
            return keepAnalysedPaths(runGit(
                options.repository, {"grep", "-l", "-z", "-F", "-e", searchTerm}));
        }
        catch (const std::exception&)
        {
            return {};
        }
    }

    // An include names a path ending in the changed file; a relative import
    // names it from the importer's directory, without its extension.
    static bool dependsOn(const FileHealth& file, const std::string& changedPath)
    {
        auto withoutExtension = eacp::FilePath {changedPath}.withExtension("").str();
        auto directory = eacp::FilePath {file.path}.parentDirectory();

        return std::ranges::any_of(
            file.dependencies,
            [&](const std::string& target)
            {
                if (!target.starts_with("."))
                    return changedPath == target
                           || changedPath.ends_with("/" + target);

                auto resolved = (directory / target).lexicallyNormal().str();
                return resolved == withoutExtension || resolved == changedPath
                       || resolved + "/index" == withoutExtension;
            });
    }

    const DiffOptions& options;
    const ScoringRules& rules;
    SourceParser parser;
};

std::string formatScore(const std::optional<double>& score)
{
    return score ? eacp::Strings::format("{:5.2f}", *score) : std::string {"    -"};
}

std::string formatFindingChange(std::string_view label, const FindingChange& finding)
{
    auto where = finding.functionName.empty()
                     ? std::string {"file"}
                     : eacp::Strings::format(
                           "{} line {}", finding.functionName, finding.line);
    return eacp::Strings::format("      {} {}: {} ({:g} -> {:g})\n",
                                 label,
                                 finding.biomarker,
                                 where,
                                 finding.valueBefore,
                                 finding.valueAfter);
}

void appendChange(std::string& text, const FileChange& change)
{
    text += eacp::Strings::format("  {} -> {}  {:+.2f}  {} {}\n",
                                  formatScore(change.scoreBefore),
                                  formatScore(change.scoreAfter),
                                  change.scoreChange,
                                  change.status,
                                  change.path);

    for (auto& finding: change.introducedFindings)
        text += formatFindingChange("new", finding);

    for (auto& finding: change.worsenedFindings)
        text += formatFindingChange("worse", finding);

    for (auto& finding: change.fixedFindings)
        text += formatFindingChange("fixed", finding);
}

void appendChanges(std::string& text,
                   std::string_view heading,
                   const DiffReport& report,
                   bool (FileChange::*isWanted)() const)
{
    auto section = std::string {};

    for (auto& change: report.changes)
        if ((change.*isWanted)())
            appendChange(section, change);

    if (!section.empty())
        text += std::string {heading} + "\n" + section + "\n";
}
} // namespace

bool FileChange::isDegraded() const
{
    return scoreChange < -scoreTolerance || !introducedFindings.empty()
           || !worsenedFindings.empty();
}

bool FileChange::isImproved() const
{
    return !isDegraded() && (scoreChange > scoreTolerance || !fixedFindings.empty());
}

bool DiffReport::hasRegression() const
{
    return std::ranges::any_of(changes,
                               [](auto& change)
                               {
                                   return change.scoreChange < -scoreTolerance
                                          || !change.introducedFindings.empty();
                               });
}

DiffReport analyseChanges(const DiffOptions& options, const ScoringRules& rules)
{
    return ChangeAnalyser {options, rules}.analyse();
}

std::string formatDiffSummary(const DiffReport& report)
{
    auto text = eacp::Strings::format("{} changed source files since {} ({})\n\n",
                                      report.changes.size(),
                                      report.base,
                                      report.mergeBase.substr(0, 10));
    appendChanges(text, "Degraded", report, &FileChange::isDegraded);
    appendChanges(text, "Improved", report, &FileChange::isImproved);

    auto unchanged = std::ranges::count_if(
        report.changes,
        [](auto& change) { return !change.isDegraded() && !change.isImproved(); });

    if (unchanged > 0)
        text += eacp::Strings::format(
            "{} changed files kept their score and findings\n\n", unchanged);

    if (report.nearbyFiles.empty())
        return text;

    text += "Near your changes\n";

    for (auto& nearby: report.nearbyFiles)
    {
        auto why =
            nearby.reason == "changes with"
                ? eacp::Strings::format("usually changes with {} ({} of {} commits)",
                                        nearby.changedPath,
                                        nearby.sharedCommits,
                                        nearby.commitCount)
                : eacp::Strings::format("depends on {}", nearby.changedPath);
        text += eacp::Strings::format(
            "  {:5.2f}  {}  {}\n", nearby.score, nearby.path, why);
    }

    return text;
}
} // namespace CodeHealth
