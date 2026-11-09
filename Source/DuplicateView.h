#pragma once

#include "Duplication.h"

namespace CodeHealth
{
enum class OutputColouring
{
    plain,
    terminalColours
};

struct FunctionCloneSources
{
    std::string firstFileSource;
    std::string secondFileSource;
};

// The two functions line by line: shared lines unmarked, lines only in the
// first marked "A", lines only in the second marked "B".
std::string formatFunctionCloneDiff(const FunctionClone& clone,
                                    const FunctionCloneSources& sources,
                                    OutputColouring colouring);
} // namespace CodeHealth
