#pragma once

#include <string_view>
#include <vector>

namespace CodeHealth
{
// Every name that starts with the typed word; an exact name wins on its own.
std::vector<std::string_view>
    findNamesMatchingPrefix(const std::vector<std::string_view>& names,
                            std::string_view typedWord);
} // namespace CodeHealth
