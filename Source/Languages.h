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
    // When set, a bump is a run of code at the bump depth, ended by a plain
    // statement (a child of one of these block types) outside it, as CodeScene
    // counts Python. Otherwise a bump is one top-level statement that reaches it.
    NodeTypes statementBlockNodeTypes {};
    // Branches after the first (elif, else): each that nests again is its own bump.
    NodeTypes alternativeClauseNodeTypes {};
    // A Python docstring is documentation, so it is not a line of code.
    bool hasDocstrings = false;
    // CodeScene judges a Python module's cohesion as a whole, not class by class.
    bool measuresModuleCohesion = false;
};

using LanguageReference = std::reference_wrapper<const Language>;

std::optional<LanguageReference> findLanguageForPath(std::string_view path);
} // namespace CodeHealth
