#pragma once

#include "Cohesion.h"
#include "Duplication.h"

#include <Miro/Reflect.h>
#include <eacp/Core/Utils/Pimpl.h>

#include <string>
#include <string_view>
#include <vector>

namespace CodeHealth
{
struct FunctionMetrics
{
    std::string name;
    int startLine = 0;
    int length = 0;
    int cyclomaticComplexity = 1;
    int maximumNestingDepth = 0;
    int bumpCount = 0;
    int complexConditionalCount = 0;
    int argumentCount = 0;
    bool hasPrimitiveArgument = false;

    MIRO_REFLECT(name,
                 startLine,
                 length,
                 cyclomaticComplexity,
                 maximumNestingDepth,
                 bumpCount,
                 complexConditionalCount,
                 argumentCount,
                 hasPrimitiveArgument)
};

struct FileMetrics
{
    std::string path;
    std::string language;
    int linesOfCode = 0;
    std::vector<FunctionMetrics> functions;
    std::vector<std::string> dependencies;
    int duplicatedLines = 0;
    std::vector<ClassMetrics> classes;

    int calculateTotalComplexity() const;

    MIRO_REFLECT(path,
                 language,
                 linesOfCode,
                 functions,
                 dependencies,
                 duplicatedLines,
                 classes)
};

struct MeasuredSource
{
    FileMetrics metrics;
    std::vector<FunctionTokens> functionTokens;
};

struct Language;

// Owns one tree-sitter parser, so each thread measuring files needs its own.
class SourceParser
{
public:
    SourceParser();

    MeasuredSource measure(std::string_view source,
                           const std::string& path,
                           const Language& language);

private:
    struct Native;
    eacp::Pimpl<Native> native;
};
} // namespace CodeHealth
