#include "Coupling.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace CodeHealth
{
namespace
{
// Mass renames and formatting sweeps would couple everything to everything.
constexpr auto maximumFilesPerCommit = size_t {50};
constexpr auto minimumSharedCommits = 10;
constexpr auto minimumCouplingPercentage = 30.0;
constexpr auto percent = 100.0;
constexpr auto pairKeyShift = 32;

using FileIndex = std::uint32_t;
using PairKey = std::uint64_t;

constexpr auto lowerIndexMask = PairKey {0xFFFFFFFF};

PairKey makePairKey(FileIndex first, FileIndex second)
{
    return (static_cast<PairKey>(first) << pairKeyShift) | second;
}

FileIndex getFirstIndex(PairKey key)
{
    return static_cast<FileIndex>(key >> pairKeyShift);
}

FileIndex getSecondIndex(PairKey key)
{
    return static_cast<FileIndex>(key & lowerIndexMask);
}

class CouplingCounter
{
public:
    explicit CouplingCounter(const std::vector<std::string>& analysedPaths)
        : paths(analysedPaths)
        , commitCounts(analysedPaths.size(), 0)
    {
        for (auto index = FileIndex {0}; index < paths.size(); ++index)
            indexByPath.emplace(paths[index], index);
    }

    void addCommit(const Commit& commit)
    {
        if (commit.paths.size() > maximumFilesPerCommit)
            return;

        auto indices = findAnalysedIndices(commit);

        for (auto first = indices.begin(); first != indices.end(); ++first)
        {
            ++commitCounts[*first];

            for (auto second = first + 1; second != indices.end(); ++second)
                ++sharedCommitsByPair[makePairKey(*first, *second)];
        }
    }

    std::vector<Coupling> collectCouplings() const
    {
        auto couplings = std::vector<Coupling> {};

        for (auto& [key, shared]: sharedCommitsByPair)
            if (auto coupling = makeCoupling(key, shared); isStrongEnough(coupling))
                couplings.push_back(std::move(coupling));

        std::ranges::sort(
            couplings,
            [](auto& left, auto& right)
            {
                return std::tie(right.sharedCommits, left.firstPath, left.secondPath)
                       < std::tie(
                           left.sharedCommits, right.firstPath, right.secondPath);
            });
        return couplings;
    }

private:
    std::vector<FileIndex> findAnalysedIndices(const Commit& commit) const
    {
        auto indices = std::vector<FileIndex> {};

        for (auto& path: commit.paths)
            if (auto found = indexByPath.find(path); found != indexByPath.end())
                indices.push_back(found->second);

        std::ranges::sort(indices);
        auto duplicates = std::ranges::unique(indices);
        indices.erase(duplicates.begin(), duplicates.end());
        return indices;
    }

    Coupling makeCoupling(PairKey key, int shared) const
    {
        auto first = getFirstIndex(key);
        auto second = getSecondIndex(key);
        auto fewerCommits = std::min(commitCounts[first], commitCounts[second]);
        return {.firstPath = paths[first],
                .secondPath = paths[second],
                .sharedCommits = shared,
                .couplingPercentage = percent * shared / fewerCommits};
    }

    static bool isStrongEnough(const Coupling& coupling)
    {
        return coupling.sharedCommits >= minimumSharedCommits
               && coupling.couplingPercentage >= minimumCouplingPercentage;
    }

    const std::vector<std::string>& paths;
    std::unordered_map<std::string_view, FileIndex> indexByPath;
    std::vector<int> commitCounts;
    std::unordered_map<PairKey, int> sharedCommitsByPair;
};
} // namespace

std::vector<Coupling>
    findChangeCoupling(const std::vector<Commit>& commits,
                       const std::vector<std::string>& analysedPaths)
{
    auto counter = CouplingCounter {analysedPaths};

    for (auto& commit: commits)
        counter.addCommit(commit);

    return counter.collectCouplings();
}
} // namespace CodeHealth
