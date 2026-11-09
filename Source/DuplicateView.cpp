#include "DuplicateView.h"

#include <eacp/Core/Utils/Format.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>

namespace CodeHealth
{
namespace
{
constexpr auto blueColour = "\033[34m";
constexpr auto orangeColour = "\033[38;5;208m";
constexpr auto boldStyle = "\033[1m";
constexpr auto resetStyle = "\033[0m";

enum class LineOrigin
{
    shared,
    firstOnly,
    secondOnly
};

struct ComparedLine
{
    LineOrigin origin = LineOrigin::shared;
    std::string text;
};

struct LineMarker
{
    LineOrigin origin;
    std::string_view prefix;
    std::string_view colour;
};

constexpr auto lineMarkers = std::array {
    LineMarker {LineOrigin::shared, "  ", ""},
    LineMarker {LineOrigin::firstOnly, "A ", blueColour},
    LineMarker {LineOrigin::secondOnly, "B ", orangeColour},
};

const LineMarker& getLineMarker(LineOrigin origin)
{
    return *std::ranges::find(lineMarkers, origin, &LineMarker::origin);
}

struct LineRange
{
    int startLine = 0;
    int endLine = 0;
};

std::vector<std::string> extractLines(const std::string& source,
                                      const LineRange& range)
{
    auto [startLine, endLine] = range;
    auto lines = std::vector<std::string> {};
    auto lineNumber = 1;
    auto lineStart = size_t {0};

    while (lineStart <= source.size() && lineNumber <= endLine)
    {
        auto lineEnd = std::min(source.find('\n', lineStart), source.size());

        if (lineNumber >= startLine)
            lines.push_back(source.substr(lineStart, lineEnd - lineStart));

        lineStart = lineEnd + 1;
        ++lineNumber;
    }

    return lines;
}

bool areSameLine(const ComparedLine& first, const ComparedLine& second)
{
    return eacp::Strings::trim(first.text) == eacp::Strings::trim(second.text);
}

using CommonLengthTable = std::vector<std::vector<int>>;

std::vector<ComparedLine> toComparedLines(const std::vector<std::string>& lines,
                                          LineOrigin origin)
{
    auto compared = std::vector<ComparedLine> {};

    for (auto& line: lines)
        compared.push_back({origin, line});

    return compared;
}

// Longest common subsequence lengths of every pair of suffixes.
CommonLengthTable measureCommonSuffixes(const std::vector<ComparedLine>& first,
                                        const std::vector<ComparedLine>& second)
{
    auto table =
        CommonLengthTable(first.size() + 1, std::vector<int>(second.size() + 1, 0));

    for (auto row = first.size(); row-- > 0;)
    {
        for (auto column = second.size(); column-- > 0;)
        {
            if (areSameLine(first[row], second[column]))
                table[row][column] = table[row + 1][column + 1] + 1;
            else
                table[row][column] =
                    std::max(table[row + 1][column], table[row][column + 1]);
        }
    }

    return table;
}

std::vector<ComparedLine> compareLines(const std::vector<ComparedLine>& first,
                                       const std::vector<ComparedLine>& second)
{
    auto table = measureCommonSuffixes(first, second);
    auto compared = std::vector<ComparedLine> {};
    auto row = size_t {0};
    auto column = size_t {0};

    while (row < first.size() && column < second.size())
    {
        if (areSameLine(first[row], second[column]))
        {
            compared.push_back({LineOrigin::shared, second[column].text});
            ++row;
            ++column;
        }
        else if (table[row + 1][column] >= table[row][column + 1])
            compared.push_back(first[row++]);
        else
            compared.push_back(second[column++]);
    }

    for (; row < first.size(); ++row)
        compared.push_back(first[row]);

    for (; column < second.size(); ++column)
        compared.push_back(second[column]);

    return compared;
}

std::string formatComparedLine(const ComparedLine& line, OutputColouring colouring)
{
    auto& marker = getLineMarker(line.origin);
    auto text = std::string {marker.prefix} + line.text;

    if (colouring == OutputColouring::plain || marker.colour.empty())
        return text + "\n";

    return std::string {marker.colour} + text + resetStyle + "\n";
}

std::string formatCloneHeader(const FunctionClone& clone, OutputColouring colouring)
{
    auto header = eacp::Strings::format(
        "A {} in {}:{}-{}\nB {} in {}:{}-{}\n{:.0f}% similar, {} tokens\n\n",
        clone.firstFunction,
        clone.firstPath,
        clone.firstStartLine,
        clone.firstEndLine,
        clone.secondFunction,
        clone.secondPath,
        clone.secondStartLine,
        clone.secondEndLine,
        clone.similarity * 100.0,
        clone.tokenCount);

    if (colouring == OutputColouring::plain)
        return header;

    return boldStyle + header + resetStyle;
}
} // namespace

std::string formatFunctionCloneDiff(const FunctionClone& clone,
                                    const FunctionCloneSources& sources,
                                    OutputColouring colouring)
{
    auto firstLines =
        toComparedLines(extractLines(sources.firstFileSource,
                                     {clone.firstStartLine, clone.firstEndLine}),
                        LineOrigin::firstOnly);
    auto secondLines =
        toComparedLines(extractLines(sources.secondFileSource,
                                     {clone.secondStartLine, clone.secondEndLine}),
                        LineOrigin::secondOnly);
    auto text = formatCloneHeader(clone, colouring);

    for (auto& line: compareLines(firstLines, secondLines))
        text += formatComparedLine(line, colouring);

    return text;
}
} // namespace CodeHealth
