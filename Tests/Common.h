#pragma once

#include <Languages.h>
#include <Metrics.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>

namespace CodeHealth::Testing
{
inline FileMetrics measureFixture(std::string_view source, std::string_view path)
{
    auto language = findLanguageForPath(path);
    nano::check(language.has_value());
    return SourceParser {}.measure(source, std::string {path}, language->get());
}

inline FunctionMetrics findFunction(const FileMetrics& file, std::string_view name)
{
    auto found = std::ranges::find(file.functions, name, &FunctionMetrics::name);
    nano::check(found != file.functions.end());
    return found == file.functions.end() ? FunctionMetrics {} : *found;
}
} // namespace CodeHealth::Testing
