#pragma once

#include "History.h"

#include <Miro/Reflect.h>

namespace CodeHealth
{
struct Coupling
{
    std::string firstPath;
    std::string secondPath;
    int sharedCommits = 0;
    int firstCommitCount = 0;
    int secondCommitCount = 0;
    double couplingPercentage = 0.0;

    MIRO_REFLECT(firstPath,
                 secondPath,
                 sharedCommits,
                 firstCommitCount,
                 secondCommitCount,
                 couplingPercentage)
};

// Pairs of analysed files that change in the same commits.
std::vector<Coupling>
    findChangeCoupling(const std::vector<Commit>& commits,
                       const std::vector<std::string>& analysedPaths);
} // namespace CodeHealth
