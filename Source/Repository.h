#pragma once

#include "Coupling.h"
#include "Score.h"

#include <eacp/Core/Utils/FilePath.h>

namespace CodeHealth
{
struct Hotspot
{
    std::string path;
    int commitCount = 0;
    int totalComplexity = 0;
    double healthScore = 10.0;
    long long hotspotScore = 0;

    MIRO_REFLECT(path, commitCount, totalComplexity, healthScore, hotspotScore)
};

struct AnalysisOptions
{
    eacp::FilePath repository;
    std::string ref = "HEAD";
    std::string since;
    int jobCount = 1;
};

struct Analysis
{
    std::string repository;
    std::string revision;
    std::string since;
    std::vector<FileHealth> files;
    std::vector<Hotspot> hotspots;
    std::vector<Coupling> couplings;
    std::vector<FunctionClone> functionClones;

    MIRO_REFLECT(
        repository, revision, since, files, hotspots, couplings, functionClones)
};

bool isExcludedPath(std::string_view path);

std::vector<std::string> readFilesAtRef(const eacp::FilePath& repository,
                                        const std::string& ref,
                                        const std::vector<std::string>& paths);

struct RepositoryMeasurement
{
    std::vector<FileMetrics> files;
    std::vector<FunctionClone> functionClones;
};

RepositoryMeasurement measureRepository(const AnalysisOptions& options);

std::vector<FileHealth> assessFiles(const std::vector<FileMetrics>& metrics,
                                    const ScoringRules& rules);

Analysis analyseRepository(const AnalysisOptions& options,
                           const ScoringRules& rules);
} // namespace CodeHealth
