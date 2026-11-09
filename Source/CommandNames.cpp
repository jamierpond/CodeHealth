#include "CommandNames.h"

#include <algorithm>
#include <iterator>

namespace CodeHealth
{
namespace
{
bool isExactName(const std::vector<std::string_view>& names,
                 std::string_view typedWord)
{
    return std::ranges::find(names, typedWord) != names.end();
}

std::vector<std::string_view>
    keepNamesStartingWith(const std::vector<std::string_view>& names,
                          std::string_view typedWord)
{
    auto matches = std::vector<std::string_view> {};
    std::ranges::copy_if(names,
                         std::back_inserter(matches),
                         [&](auto name) { return name.starts_with(typedWord); });
    return matches;
}
} // namespace

std::vector<std::string_view>
    findNamesMatchingPrefix(const std::vector<std::string_view>& names,
                            std::string_view typedWord)
{
    if (typedWord.empty())
        return {};

    if (isExactName(names, typedWord))
        return {typedWord};

    return keepNamesStartingWith(names, typedWord);
}
} // namespace CodeHealth
