#pragma once

#include <Miro/Reflect.h>

#include <cstdint>
#include <string>
#include <vector>

namespace CodeHealth
{
// One source token, with identifiers and literals folded into one kind each,
// so renamed copies still match.
struct Token
{
    std::uint32_t kind = 0;
    std::uint32_t row = 0;
    std::uint32_t startByte = 0;
};

struct FunctionTokens
{
    std::string name;
    int startLine = 0;
    int endLine = 0;
    std::vector<std::uint32_t> kinds;
};

struct FunctionClone
{
    std::string firstPath;
    std::string firstFunction;
    int firstStartLine = 0;
    int firstEndLine = 0;
    std::string secondPath;
    std::string secondFunction;
    int secondStartLine = 0;
    int secondEndLine = 0;
    double similarity = 0.0;
    int tokenCount = 0;

    MIRO_REFLECT(firstPath,
                 firstFunction,
                 firstStartLine,
                 firstEndLine,
                 secondPath,
                 secondFunction,
                 secondStartLine,
                 secondEndLine,
                 similarity,
                 tokenCount)
};

// Pairs of functions with nearly the same tokens: candidates from rare shared
// token trigrams (SourcererCC, Sajnani et al. 2016), confirmed by the longest
// common subsequence of their token streams (NiCad, Roy and Cordy 2008).
std::vector<FunctionClone> findFunctionClones(
    const std::vector<std::vector<FunctionTokens>>& functionsPerFile,
    const std::vector<std::string>& paths,
    int jobCount);

std::size_t
    measureLongestCommonSubsequence(const std::vector<std::uint32_t>& first,
                                    const std::vector<std::uint32_t>& second);

// Pairs of functions with nearly the same tokens: candidates from rare shared
// token trigrams (SourcererCC, Sajnani et al. 2016), confirmed by the longest
// common subsequence of their token streams (NiCad, Roy and Cordy 2008).
std::vector<FunctionClone> findFunctionClones(
    const std::vector<std::vector<FunctionTokens>>& functionsPerFile,
    const std::vector<std::string>& paths,
    int jobCount);

std::size_t
    measureLongestCommonSubsequence(const std::vector<std::uint32_t>& first,
                                    const std::vector<std::uint32_t>& second);

// Lines inside a clone of another function in the same file, each counted
// once, per path. CodeScene reviews one file at a time, so its duplication is
// within a file, and these are what count toward the score.
std::vector<int>
    countDuplicatedLines(const std::vector<FunctionClone>& functionClones,
                         const std::vector<std::string>& paths);
} // namespace CodeHealth
