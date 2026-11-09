#include "Duplication.h"

#include <algorithm>
#include <map>
#include <set>

namespace CodeHealth
{
std::vector<int>
    countDuplicatedLines(const std::vector<FunctionClone>& functionClones,
                         const std::vector<std::string>& paths)
{
    auto rangesByPath = std::map<std::string, std::vector<std::pair<int, int>>> {};

    for (auto& clone: functionClones)
    {
        if (clone.firstPath != clone.secondPath)
            continue;

        rangesByPath[clone.firstPath].emplace_back(clone.firstStartLine,
                                                   clone.firstEndLine);
        rangesByPath[clone.secondPath].emplace_back(clone.secondStartLine,
                                                    clone.secondEndLine);
    }

    auto counts = std::vector<int> {};

    for (auto& path: paths)
    {
        auto& ranges = rangesByPath[path];
        std::ranges::sort(ranges);
        auto covered = 0;
        auto coveredUpTo = 0;

        for (auto [start, end]: ranges)
        {
            covered += std::max(0, end - std::max(start - 1, coveredUpTo));
            coveredUpTo = std::max(coveredUpTo, end);
        }

        counts.push_back(covered);
    }

    return counts;
}
std::vector<int>
    countDuplicatedFunctions(const std::vector<FunctionClone>& functionClones,
                             const std::vector<std::string>& paths)
{
    auto startLinesByPath = std::map<std::string, std::set<int>> {};

    for (auto& clone: functionClones)
    {
        if (clone.firstPath != clone.secondPath)
            continue;

        startLinesByPath[clone.firstPath].insert(clone.firstStartLine);
        startLinesByPath[clone.firstPath].insert(clone.secondStartLine);
    }

    auto counts = std::vector<int> {};

    for (auto& path: paths)
        counts.push_back(static_cast<int>(startLinesByPath[path].size()));

    return counts;
}
} // namespace CodeHealth
