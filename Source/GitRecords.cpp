#include "GitRecords.h"

#include <charconv>
#include <map>
#include <stdexcept>

namespace CodeHealth::GitRecords
{
namespace
{
const std::map<std::string_view, std::string_view> placeholdersByField {
    {"parentHashes", "%P"},
    {"authorName", "%an"},
    {"authorTime", "%at"},
    {"objectName", "%(objectname)"},
    {"objectType", "%(objecttype)"},
    {"objectSize", "%(objectsize)"},
};

template <typename Integer>
bool parseWholeInteger(std::string_view text, Integer& target)
{
    auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), target);
    return error == std::errc {} && end == text.data() + text.size();
}
} // namespace

std::string getPlaceholder(std::string_view fieldName)
{
    auto found = placeholdersByField.find(fieldName);

    if (found == placeholdersByField.end())
        throw std::logic_error {"no git placeholder for " + std::string {fieldName}};

    return std::string {found->second};
}

bool assignFieldFromText(Miro::PrimitiveRef target, std::string_view text)
{
    return std::visit(
        [&](auto* value)
        {
            using Value = std::remove_pointer_t<decltype(value)>;

            if constexpr (std::is_same_v<Value, std::string>)
                *value = text;
            else if constexpr (std::is_same_v<Value, int>
                               || std::is_same_v<Value, std::int64_t>)
                return parseWholeInteger(text, *value);
            else
                throw std::logic_error {
                    "git records hold strings and integers only"};

            return true;
        },
        target.data);
}

std::vector<std::string_view> splitFields(std::string_view text)
{
    auto fields = std::vector<std::string_view> {};

    while (true)
    {
        auto end = text.find(fieldSeparator);
        fields.push_back(text.substr(0, end));

        if (end == std::string_view::npos)
            return fields;

        text = text.substr(end + 1);
    }
}
} // namespace CodeHealth::GitRecords
