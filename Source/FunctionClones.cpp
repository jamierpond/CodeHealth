#include "Duplication.h"

#include <eacp/Core/Threads/Thread.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <optional>
#include <span>
#include <unordered_map>

namespace CodeHealth
{
namespace
{
constexpr auto bagSimilarity = 0.8;
constexpr auto sequenceSimilarity = 0.9;
constexpr auto trigramBase = std::uint64_t {0x10003};
// A trigram this common says nothing about which functions match.
constexpr auto maximumPostingLength = size_t {256};
constexpr auto bitsPerWord = size_t {64};

using Trigram = std::uint64_t;

struct FunctionEntry
{
    std::uint32_t file = 0;
    const FunctionTokens* tokens = nullptr;
    std::vector<Trigram> bag;
    std::vector<Trigram> rarestPrefix;
};

std::vector<Trigram> makeTrigramBag(const std::vector<std::uint32_t>& kinds)
{
    auto bag = std::vector<Trigram> {};

    for (auto index = size_t {2}; index < kinds.size(); ++index)
        bag.push_back((kinds[index - 2] * trigramBase + kinds[index - 1])
                          * trigramBase
                      + kinds[index]);

    std::ranges::sort(bag);
    return bag;
}

size_t countSharedElements(const std::vector<Trigram>& first,
                           const std::vector<Trigram>& second)
{
    auto shared = size_t {0};
    auto left = first.begin();
    auto right = second.begin();

    while (left != first.end() && right != second.end())
    {
        if (*left < *right)
            ++left;
        else if (*right < *left)
            ++right;
        else
        {
            ++shared;
            ++left;
            ++right;
        }
    }

    return shared;
}

size_t calculateRequiredOverlap(size_t larger)
{
    return static_cast<size_t>(
        std::ceil(bagSimilarity * static_cast<double>(larger)));
}

class CloneFinder
{
public:
    CloneFinder(const std::vector<std::vector<FunctionTokens>>& functionsPerFile,
                const std::vector<std::string>& pathsToUse)
        : paths(pathsToUse)
    {
        for (auto file = std::uint32_t {0}; file < functionsPerFile.size(); ++file)
        {
            firstEntryOfFile.push_back(entries.size());

            for (auto& function: functionsPerFile[file])
                entries.push_back({.file = file,
                                   .tokens = &function,
                                   .bag = makeTrigramBag(function.kinds)});
        }

        orderByRarity();
        buildPrefixIndex();
    }

    std::vector<FunctionClone> find(int jobCount)
    {
        auto clonesPerEntry =
            std::vector<std::vector<FunctionClone>>(entries.size());
        auto next = std::atomic<size_t> {0};
        auto workers = std::vector<eacp::Threads::Thread> {};

        for (auto worker = 0; worker < std::max(1, jobCount); ++worker)
            workers.emplace_back(
                [&]
                {
                    auto lastSeen =
                        std::vector<size_t>(entries.size(), entries.size());

                    for (auto index = next++; index < entries.size(); index = next++)
                        clonesPerEntry[index] = findClonesOf(index, lastSeen);
                });

        for (auto& worker: workers)
            worker.join();

        auto clones = std::vector<FunctionClone> {};

        for (auto& found: clonesPerEntry)
            clones.insert(clones.end(), found.begin(), found.end());

        return clones;
    }

private:
    // Keeps each bag's rarest trigrams: any pair similar enough shares one of them.
    void orderByRarity()
    {
        auto frequencies = std::unordered_map<Trigram, std::uint32_t> {};

        for (auto& entry: entries)
            for (auto trigram: entry.bag)
                ++frequencies[trigram];

        for (auto& entry: entries)
        {
            auto ranked = std::vector<std::pair<std::uint32_t, Trigram>> {};

            for (auto trigram: entry.bag)
                ranked.emplace_back(frequencies.at(trigram), trigram);

            std::ranges::sort(ranked);
            auto size = ranked.size();
            auto prefixLength =
                size == 0 ? 0 : size - calculateRequiredOverlap(size) + 1;

            for (auto& [frequency, trigram]: std::span {ranked}.first(prefixLength))
                entry.rarestPrefix.push_back(trigram);
        }
    }

    void buildPrefixIndex()
    {
        for (auto index = size_t {0}; index < entries.size(); ++index)
            for (auto trigram: entries[index].rarestPrefix)
                postings[trigram].push_back(index);
    }

    // Only earlier entries are compared, so each pair is found once.
    std::vector<FunctionClone> findClonesOf(size_t index,
                                            std::vector<size_t>& lastSeen) const
    {
        auto& entry = entries[index];
        auto clones = std::vector<FunctionClone> {};

        for (auto trigram: entry.rarestPrefix)
            for (auto other: selectCandidates(postings.at(trigram), entry))
            {
                if (other >= index || lastSeen[other] == index)
                    continue;

                lastSeen[other] = index;

                if (auto clone = confirmClone(entries[other], entry))
                    clones.push_back(*clone);
            }

        return clones;
    }

    // A trigram shared by too many functions in the repository still finds
    // copies within the entry's own file, where duplication counts toward the
    // score: in a large repository every trigram of a short function is common.
    std::span<const size_t> selectCandidates(const std::vector<size_t>& posting,
                                             const FunctionEntry& entry) const
    {
        if (posting.size() <= maximumPostingLength)
            return posting;

        auto index = static_cast<size_t>(&entry - entries.data());
        auto fileStart = firstEntryOfFile[entry.file];
        auto first = std::ranges::lower_bound(posting, fileStart);
        auto last = std::ranges::lower_bound(posting, index);
        return {first, last};
    }

    std::optional<FunctionClone> confirmClone(const FunctionEntry& first,
                                              const FunctionEntry& second) const
    {
        auto larger = std::max(first.bag.size(), second.bag.size());
        auto smaller = std::min(first.bag.size(), second.bag.size());

        if (smaller < calculateRequiredOverlap(larger)
            || countSharedElements(first.bag, second.bag)
                   < calculateRequiredOverlap(larger))
            return std::nullopt;

        auto& firstKinds = first.tokens->kinds;
        auto& secondKinds = second.tokens->kinds;
        auto longest = std::max(firstKinds.size(), secondKinds.size());
        auto similarity = static_cast<double>(measureLongestCommonSubsequence(
                              firstKinds, secondKinds))
                          / static_cast<double>(longest);

        if (similarity < sequenceSimilarity)
            return std::nullopt;

        return FunctionClone {.firstPath = paths[first.file],
                              .firstFunction = first.tokens->name,
                              .firstStartLine = first.tokens->startLine,
                              .firstEndLine = first.tokens->endLine,
                              .secondPath = paths[second.file],
                              .secondFunction = second.tokens->name,
                              .secondStartLine = second.tokens->startLine,
                              .secondEndLine = second.tokens->endLine,
                              .similarity = similarity,
                              .tokenCount = static_cast<int>(longest)};
    }

    const std::vector<std::string>& paths;
    std::vector<FunctionEntry> entries;
    std::vector<size_t> firstEntryOfFile;
    std::unordered_map<Trigram, std::vector<size_t>> postings;
};
} // namespace

// Hyyro's bit-parallel LCS length (2004): one bit per token of first.
std::size_t measureLongestCommonSubsequence(const std::vector<std::uint32_t>& first,
                                            const std::vector<std::uint32_t>& second)
{
    auto wordCount = (first.size() + bitsPerWord - 1) / bitsPerWord;
    auto matches = std::unordered_map<std::uint32_t, std::vector<std::uint64_t>> {};

    for (auto index = size_t {0}; index < first.size(); ++index)
    {
        auto& mask = matches[first[index]];
        mask.resize(wordCount, 0);
        mask[index / bitsPerWord] |= std::uint64_t {1} << (index % bitsPerWord);
    }

    auto row = std::vector<std::uint64_t>(wordCount, ~std::uint64_t {0});

    for (auto kind: second)
    {
        auto found = matches.find(kind);

        if (found == matches.end())
            continue;

        auto carry = std::uint64_t {0};

        for (auto word = size_t {0}; word < wordCount; ++word)
        {
            auto matched = row[word] & found->second[word];
            auto sum = row[word] + matched + carry;
            carry = (sum < row[word] || (carry != 0 && sum == row[word])) ? 1 : 0;
            row[word] = sum | (row[word] & ~found->second[word]);
        }
    }

    auto unset = size_t {0};

    for (auto index = size_t {0}; index < first.size(); ++index)
        unset +=
            (row[index / bitsPerWord] >> (index % bitsPerWord) & 1) == 0 ? 1 : 0;

    return unset;
}

std::vector<FunctionClone> findFunctionClones(
    const std::vector<std::vector<FunctionTokens>>& functionsPerFile,
    const std::vector<std::string>& paths,
    int jobCount)
{
    return CloneFinder {functionsPerFile, paths}.find(jobCount);
}
} // namespace CodeHealth
