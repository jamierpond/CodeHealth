#pragma once

#include "Repository.h"

namespace CodeHealth
{
std::string formatHumanSummary(const Analysis& analysis, int entryCount);
std::string formatJson(const Analysis& analysis);
} // namespace CodeHealth
