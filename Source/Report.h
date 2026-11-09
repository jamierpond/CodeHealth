#pragma once

#include "Repository.h"

namespace CodeHealth
{
struct ReportSection
{
    std::string_view name;
    void (*appendSection)(std::string& text,
                          const Analysis& analysis,
                          int entryCount);
};

// The summary's sections in the order it prints them, each named by one word.
const std::vector<ReportSection>& getReportSections();

std::string formatReportSection(const ReportSection& section,
                                const Analysis& analysis,
                                int entryCount);

std::string formatHumanSummary(const Analysis& analysis, int entryCount);
std::string formatJson(const Analysis& analysis);
} // namespace CodeHealth
