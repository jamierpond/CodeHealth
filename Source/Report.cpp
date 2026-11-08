#include "Report.h"

#include <eacp/Core/Utils/Format.h>

#include <algorithm>
#include <span>
#include <string_view>
#include <vector>

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

template <typename Entry, typename FormatEntry>
void appendSection(std::string& text,
                   std::string_view heading,
                   const std::vector<Entry>& entries,
                   int entryCount,
                   FormatEntry formatEntry)
{
    text += heading;

    auto shown = std::min<size_t>(entries.size(), entryCount);

    for (auto& entry: std::span {entries}.first(shown))
        text += formatEntry(entry);
}

std::string formatHotspot(const Hotspot& hotspot)
{
    return eacp::Strings::format(
        "  {:7}  {:4} commits  complexity {:5}  health {:5.2f}  {}\n",
        hotspot.hotspotScore,
        hotspot.commitCount,
        hotspot.totalComplexity,
        hotspot.healthScore,
        hotspot.path);
}

std::string formatCoupling(const Coupling& coupling)
{
    return eacp::Strings::format("  {:4}  {:5.1f}%  {}  <->  {}\n",
                                 coupling.sharedCommits,
                                 coupling.couplingPercentage,
                                 coupling.firstPath,
                                 coupling.secondPath);
}

std::string formatFunctionClone(const FunctionClone& clone)
{
    return eacp::Strings::format("  {:3.0f}%  {:5}  {} in {}:{}  =  {} in {}:{}\n",
                                 clone.similarity * 100.0,
                                 clone.tokenCount,
                                 clone.firstFunction,
                                 clone.firstPath,
                                 clone.firstStartLine,
                                 clone.secondFunction,
                                 clone.secondPath,
                                 clone.secondStartLine);
}

} // namespace

std::string formatHumanSummary(const Analysis& analysis, int entryCount)
{
    auto text = eacp::Strings::format(
        "{} files analysed at {}\n\n", analysis.files.size(), analysis.revision);
    appendWorstFiles(text, analysis, entryCount);
    appendSection(text,
                  "\nHotspots (commits x complexity)\n",
                  analysis.hotspots,
                  entryCount,
                  formatHotspot);
    appendSection(
        text,
        "\nChange coupling (shared commits, share of the less-changed file)\n",
        analysis.couplings,
        entryCount,
        formatCoupling);
    appendSection(text,
                  "\nDuplicated functions (similarity, tokens; only same-file "
                  "pairs count toward the score)\n",
                  analysis.functionClones,
                  entryCount,
                  formatFunctionClone);
    return text;
}

std::string formatJson(const Analysis& analysis)
{
    return Miro::toJSONString(analysis, jsonIndent);
}
} // namespace CodeHealth
