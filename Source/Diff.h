#pragma once

#include "Score.h"

#include <eacp/Core/Utils/FilePath.h>

#include <optional>

namespace CodeHealth
{
struct FindingChange
{
    std::string biomarker;
    std::string functionName;
    int line = 0;
    double valueBefore = 0.0;
    double valueAfter = 0.0;

    MIRO_REFLECT(biomarker, functionName, line, valueBefore, valueAfter)
};

// A changed source file: before is the merge base, after is the working tree.
struct FileChange
{
    std::string path;
    std::string previousPath;
    std::string status;
    std::optional<double> scoreBefore;
    std::optional<double> scoreAfter;
    double scoreChange = 0.0;
    std::vector<FindingChange> introducedFindings;
    std::vector<FindingChange> worsenedFindings;
    std::vector<FindingChange> fixedFindings;

    bool isDegraded() const;
    bool isImproved() const;

    MIRO_REFLECT(path,
                 previousPath,
                 status,
                 scoreBefore,
                 scoreAfter,
                 scoreChange,
                 introducedFindings,
                 worsenedFindings,
                 fixedFindings)
};

// An unchanged file worth a look: it usually changes with a changed file, or
// includes or imports one.
struct NearbyFile
{
    std::string path;
    std::string changedPath;
    std::string reason;
    int sharedCommits = 0;
    int commitCount = 0;
    double score = 10.0;

    MIRO_REFLECT(path, changedPath, reason, sharedCommits, commitCount, score)
};

struct DiffOptions
{
    eacp::FilePath repository;
    std::string base;
    std::string since = "1 year ago";
};

struct DiffReport
{
    std::string base;
    std::string mergeBase;
    std::vector<FileChange> changes;
    std::vector<NearbyFile> nearbyFiles;

    bool hasRegression() const;

    MIRO_REFLECT(base, mergeBase, changes, nearbyFiles)
};

DiffReport analyseChanges(const DiffOptions& options, const ScoringRules& rules);

std::string formatDiffSummary(const DiffReport& report);
} // namespace CodeHealth
