#pragma once

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

struct TSLanguage;

namespace CodeHealth
{
using NodeTypes = std::set<std::string_view>;

struct Language
{
    std::string name;
    std::vector<std::string> extensions;
    const TSLanguage* (*getGrammar)();
    NodeTypes functionNodeTypes;
    NodeTypes branchingNodeTypes;
    NodeTypes nestingNodeTypes;
    std::string_view caseNodeType;
    NodeTypes logicalOperators;
    NodeTypes primitiveTypeNodeTypes {};
    NodeTypes primitiveTypeNames {};
    NodeTypes dependencyNodeTypes {};
    std::string_view dependencyField;
    NodeTypes identifierNodeTypes {};
    NodeTypes literalNodeTypes {};
};

using LanguageReference = std::reference_wrapper<const Language>;

std::optional<LanguageReference> findLanguageForPath(std::string_view path);
} // namespace CodeHealth
