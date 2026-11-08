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
    NodeTypes logicalOperationNodeTypes {"binary_expression"};
    NodeTypes primitiveTypeNodeTypes {};
    NodeTypes primitiveTypeNames {};
    NodeTypes dependencyNodeTypes {};
    std::vector<std::string_view> dependencyFields;
    NodeTypes identifierNodeTypes {};
    NodeTypes literalNodeTypes {};
    NodeTypes classNodeTypes {};
    NodeTypes fieldNodeTypes {};
    NodeTypes memberNameNodeTypes {};
    NodeTypes referenceNodeTypes {};
    // Parameters that are not arguments: Python's `self`, `cls` and its `*` and `/` markers.
    NodeTypes ignoredParameterNodeTypes {};
    NodeTypes implicitParameterNames {};
    // `self.balance` makes `balance` a field in a language that declares none.
    NodeTypes selfNames {};
    // `@decorator def method(...)`: the wrapper's `definition` field holds the method.
    NodeTypes definitionWrapperNodeTypes {};
};

using LanguageReference = std::reference_wrapper<const Language>;

std::optional<LanguageReference> findLanguageForPath(std::string_view path);
} // namespace CodeHealth
