#include "Report.h"

#include <eacp/Core/Utils/Format.h>

#include <algorithm>

namespace CodeHealth
{
namespace
{
constexpr auto jsonIndent = 2;
constexpr auto findingsShownPerFile = 5;

struct RankedFunctionFinding
{
    std::string path;
    Finding finding;
};

std::string formatFinding(const Finding& finding)
{
    if (finding.functionName.empty())
        return eacp::Strings::format("{} ({:g} >= {:g})",
                                     finding.biomarker,
                                     finding.value,
                                     finding.threshold);

    return eacp::Strings::format("{} in {} at line {} ({:g} >= {:g})",
                                 finding.biomarker,
                                 finding.functionName,
                                 finding.line,
                                 finding.value,
                                 finding.threshold);
}

void appendWorstFiles(std::string& text, const Analysis& analysis, int entryCount)
{
    text += "Worst files by Code Health\n";

    auto shown = std::min<size_t>(analysis.files.size(), entryCount);

    for (auto& file: std::span {analysis.files}.first(shown))
    {
        text += eacp::Strings::format(
            "  {:5.2f}  {}  ({} lines of code, {} functions)\n",
            file.score,
            file.path,
            file.linesOfCode,
            file.functionCount);

        auto findingCount =
            std::min<size_t>(file.findings.size(), findingsShownPerFile);

        for (auto& finding: std::span {file.findings}.first(findingCount))
            text += "         " + formatFinding(finding) + "\n";
    }
}

void appendHotspots(std::string& text, const Analysis& analysis, int entryCount)
{
    text += "\nHotspots (commits x complexity)\n";

    auto shown = std::min<size_t>(analysis.hotspots.size(), entryCount);

    for (auto& hotspot: std::span {analysis.hotspots}.first(shown))
        text +=
            eacp::Strings::format("  {:7}  {:4} commits  complexity {:5}  health "
                                  "{:5.2f}  {}\n",
                                  hotspot.hotspotScore,
                                  hotspot.commitCount,
                                  hotspot.totalComplexity,
                                  hotspot.healthScore,
                                  hotspot.path);
}

void appendCouplings(std::string& text, const Analysis& analysis, int entryCount)
{
    text += "\nChange coupling (shared commits, share of the less-changed file)\n";

    auto shown = std::min<size_t>(analysis.couplings.size(), entryCount);

    for (auto& coupling: std::span {analysis.couplings}.first(shown))
        text += eacp::Strings::format("  {:4}  {:5.1f}%  {}  <->  {}\n",
                                      coupling.sharedCommits,
                                      coupling.couplingPercentage,
                                      coupling.firstPath,
                                      coupling.secondPath);
}
} // namespace

std::string formatHumanSummary(const Analysis& analysis, int entryCount)
{
    auto text = eacp::Strings::format(
        "{} files analysed at {}\n\n", analysis.files.size(), analysis.revision);
    appendWorstFiles(text, analysis, entryCount);
    appendHotspots(text, analysis, entryCount);
    appendCouplings(text, analysis, entryCount);
    return text;
}

std::string formatJson(const Analysis& analysis)
{
    return Miro::toJSONString(analysis, jsonIndent);
}
} // namespace CodeHealth
