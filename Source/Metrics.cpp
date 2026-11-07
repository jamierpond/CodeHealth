#include "Metrics.h"
#include "Languages.h"

#include <tree_sitter/api.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <set>

namespace CodeHealth
{
namespace
{
constexpr auto bumpNestingDepth = 2;
constexpr auto complexConditionalOperatorCount = 2;
constexpr auto anonymousFunctionName = "<anonymous>";

struct ParserDeleter
{
    void operator()(TSParser* parser) const { ts_parser_delete(parser); }
};

struct TreeDeleter
{
    void operator()(TSTree* tree) const { ts_tree_delete(tree); }
};

using ParserPointer = std::unique_ptr<TSParser, ParserDeleter>;
using TreePointer = std::unique_ptr<TSTree, TreeDeleter>;

// Walks the children through a cursor: ts_node_child(node, index) is linear
// in index, so an index loop is quadratic on a node with many children.
template <typename Visitor>
void forEachChild(TSNode node, Visitor&& visitor)
{
    auto cursor = ts_tree_cursor_new(node);

    if (ts_tree_cursor_goto_first_child(&cursor))
    {
        do
        {
            visitor(ts_tree_cursor_current_node(&cursor));
        } while (ts_tree_cursor_goto_next_sibling(&cursor));
    }

    ts_tree_cursor_delete(&cursor);
}

template <typename Visitor>
void forEachNamedChild(TSNode node, Visitor&& visitor)
{
    forEachChild(node,
                 [&](TSNode child)
                 {
                     if (ts_node_is_named(child))
                         visitor(child);
                 });
}

// Visits root and everything under it depth first, in source order, without
// recursion: a worker thread's stack is small and generated sources nest deep.
// The visitor gets each node with its parent's context and returns the context
// for its children, or nothing to skip them.
template <typename Context, typename Visitor>
void walkTree(TSNode root, Context rootContext, Visitor&& visitor)
{
    auto pending =
        std::vector<std::pair<TSNode, Context>> {{root, std::move(rootContext)}};

    while (!pending.empty())
    {
        auto [node, context] = std::move(pending.back());
        pending.pop_back();

        auto childContext = visitor(node, context);

        if (!childContext)
            continue;

        auto firstChild = pending.size();
        forEachChild(
            node, [&](TSNode child) { pending.emplace_back(child, *childContext); });
        std::reverse(pending.begin() + static_cast<std::ptrdiff_t>(firstChild),
                     pending.end());
    }
}

std::string_view getNodeType(TSNode node)
{
    return ts_node_type(node);
}

TSNode getField(TSNode node, std::string_view field)
{
    return ts_node_child_by_field_name(
        node, field.data(), static_cast<uint32_t>(field.size()));
}

bool hasField(TSNode node, std::string_view field)
{
    return !ts_node_is_null(getField(node, field));
}

std::string getNodeText(TSNode node, std::string_view source)
{
    auto start = ts_node_start_byte(node);
    auto end = ts_node_end_byte(node);
    return std::string {source.substr(start, end - start)};
}

bool isElseIf(TSNode node, TSNode parent)
{
    return getNodeType(node) == "if_statement"
           && getNodeType(parent) == "else_clause";
}

TSNode findFunctionDeclarator(TSNode declarator)
{
    while (!ts_node_is_null(declarator)
           && getNodeType(declarator) != "function_declarator")
        declarator = getField(declarator, "declarator");

    return declarator;
}

TSNode findSignature(TSNode function)
{
    if (hasField(function, "declarator"))
        return findFunctionDeclarator(getField(function, "declarator"));

    return function;
}

int countParameters(TSNode signature)
{
    if (hasField(signature, "parameter"))
        return 1;

    auto parameters = getField(signature, "parameters");
    auto count = 0;

    forEachNamedChild(parameters,
                      [&](TSNode parameter)
                      {
                          if (getNodeType(parameter) != "comment")
                              ++count;
                      });

    return count;
}

std::string
    findFunctionName(TSNode function, TSNode signature, std::string_view source)
{
    if (hasField(signature, "declarator"))
        return getNodeText(getField(signature, "declarator"), source);

    if (hasField(function, "name"))
        return getNodeText(getField(function, "name"), source);

    auto parent = ts_node_parent(function);

    for (auto field: {"name", "key", "left"})
        if (hasField(parent, field))
            return getNodeText(getField(parent, field), source);

    return anonymousFunctionName;
}

class SourceMeasurer
{
public:
    SourceMeasurer(const Language& languageToUse,
                   std::string_view sourceText,
                   TSNode root)
        : language(languageToUse)
        , source(sourceText)
        , codeRows(ts_node_end_point(root).row + 1, false)
    {
        markCodeRows(root);
    }

    int countLinesOfCode() const
    {
        return static_cast<int>(std::ranges::count(codeRows, true));
    }

    // An anonymous function inside another one (a callback, an effect) is part
    // of it; a named one is its own function, named after its enclosing one.
    void collectFunctions(TSNode root,
                          std::vector<FunctionMetrics>& functions,
                          std::vector<std::string>& dependencies)
    {
        walkTree(root,
                 std::string {},
                 [&](TSNode node, const std::string& enclosingName)
                 {
                     if (isDependency(node))
                         dependencies.push_back(findDependencyTarget(node));

                     if (!isFunction(node)
                         || (!enclosingName.empty() && !isNamedFunction(node)))
                         return std::optional {enclosingName};

                     auto metrics = measureFunction(node);

                     if (!enclosingName.empty())
                         metrics.name = enclosingName + "." + metrics.name;

                     functions.push_back(metrics);
                     functionByteRanges.emplace_back(ts_node_start_byte(node),
                                                     ts_node_end_byte(node));
                     return std::optional {metrics.name};
                 });
    }

private:
    struct WalkPosition
    {
        TSNode parent;
        int depth = 0;
        TSNode topLevelStatement;
    };

    void markCodeRows(TSNode root)
    {
        walkTree(root,
                 true,
                 [&](TSNode node, bool) -> std::optional<bool>
                 {
                     if (getNodeType(node) == "comment")
                         return std::nullopt;

                     if (ts_node_child_count(node) > 0)
                         return true;

                     for (auto row = ts_node_start_point(node).row;
                          row <= ts_node_end_point(node).row;
                          ++row)
                         codeRows[row] = true;

                     tokens.push_back({classifyToken(node),
                                       ts_node_start_point(node).row,
                                       ts_node_start_byte(node)});
                     return std::nullopt;
                 });
    }

    // Identifiers and literals each fold into one kind, past every grammar symbol.
    std::uint32_t classifyToken(TSNode node) const
    {
        constexpr auto identifierKind = std::uint32_t {0x10000};
        constexpr auto literalKind = std::uint32_t {0x10001};
        auto type = getNodeType(node);

        if (language.identifierNodeTypes.contains(type))
            return identifierKind;

        if (language.literalNodeTypes.contains(type))
            return literalKind;

        return ts_node_symbol(node);
    }

    int countCodeRowsIn(TSNode node) const
    {
        auto first = codeRows.begin() + ts_node_start_point(node).row;
        auto last = codeRows.begin() + ts_node_end_point(node).row + 1;
        return static_cast<int>(std::count(first, last, true));
    }

    bool isPrimitiveType(TSNode node) const
    {
        auto type = getNodeType(node);

        if (language.primitiveTypeNodeTypes.contains(type))
            return true;

        return type == "type_identifier"
               && language.primitiveTypeNames.contains(getNodeText(node, source));
    }

    // The parameter's own type, unwrapped from a TypeScript annotation or a
    // C++ qualified name, so `std::string` counts and `{a: string}` does not.
    bool hasPrimitiveType(TSNode parameter) const
    {
        auto type = getField(parameter, "type");

        if (!ts_node_is_null(type) && getNodeType(type) == "type_annotation")
            type = ts_node_named_child(type, 0);

        if (!ts_node_is_null(type) && getNodeType(type) == "qualified_identifier")
            type = getField(type, "name");

        return !ts_node_is_null(type) && isPrimitiveType(type);
    }

    bool hasPrimitiveParameter(TSNode signature) const
    {
        auto parameters = getField(signature, "parameters");

        if (ts_node_is_null(parameters))
            return false;

        auto hasPrimitive = false;
        forEachNamedChild(
            parameters,
            [&](TSNode parameter)
            { hasPrimitive = hasPrimitive || hasPrimitiveType(parameter); });
        return hasPrimitive;
    }

    bool isDependency(TSNode node) const
    {
        return language.dependencyNodeTypes.contains(getNodeType(node));
    }

    // "Foo.h", <vector> and './foo' all become the bare target.
    std::string findDependencyTarget(TSNode node) const
    {
        auto target = getNodeText(getField(node, language.dependencyField), source);
        auto isQuote = [](char c)
        { return c == '"' || c == '\'' || c == '<' || c == '>' || c == '`'; };
        auto first = std::ranges::find_if_not(target, isQuote);
        auto last = std::find_if_not(target.rbegin(), target.rend(), isQuote).base();
        return first < last ? std::string {first, last} : std::string {};
    }

    bool isFunction(TSNode node) const
    {
        return language.functionNodeTypes.contains(getNodeType(node));
    }

    bool isNamedFunction(TSNode node) const
    {
        auto signature = findSignature(node);
        return ts_node_is_null(signature)
               || findFunctionName(node, signature, source) != anonymousFunctionName;
    }

    bool isLogicalOperation(TSNode node) const
    {
        if (getNodeType(node) != "binary_expression")
            return false;

        auto operatorNode = getField(node, "operator");
        return !ts_node_is_null(operatorNode)
               && language.logicalOperators.contains(getNodeType(operatorNode));
    }

    bool isCaseWithValue(TSNode node) const
    {
        return getNodeType(node) == language.caseNodeType && hasField(node, "value");
    }

    bool isBranch(TSNode node) const
    {
        return language.branchingNodeTypes.contains(getNodeType(node))
               || isCaseWithValue(node) || isLogicalOperation(node);
    }

    int countLogicalOperators(TSNode root) const
    {
        auto count = 0;
        walkTree(root,
                 true,
                 [&](TSNode node, bool)
                 {
                     count += isLogicalOperation(node) ? 1 : 0;
                     return std::optional {true};
                 });
        return count;
    }

    FunctionMetrics measureFunction(TSNode function)
    {
        auto metrics = FunctionMetrics {};
        metrics.startLine = static_cast<int>(ts_node_start_point(function).row) + 1;
        metrics.length = countCodeRowsIn(function);

        auto signature = findSignature(function);

        if (!ts_node_is_null(signature))
        {
            metrics.name = findFunctionName(function, signature, source);
            metrics.argumentCount = countParameters(signature);
            metrics.hasPrimitiveArgument = hasPrimitiveParameter(signature);
        }

        auto body = getField(function, "body");
        bumpyStatements.clear();

        if (!ts_node_is_null(body))
            forEachChild(
                body,
                [&](TSNode statement)
                {
                    walkTree(statement,
                             WalkPosition {.parent = body, .topLevelStatement = {}},
                             [&](TSNode node, const WalkPosition& position)
                             { return visit(node, position, metrics); });
                });

        metrics.bumpCount = static_cast<int>(bumpyStatements.size());
        return metrics;
    }

    // Counts one node toward its function and returns where its children sit.
    std::optional<WalkPosition>
        visit(TSNode node, WalkPosition position, FunctionMetrics& metrics)
    {
        if (isFunction(node) && isNamedFunction(node))
        {
            metrics.length -= countCodeRowsIn(node);
            return std::nullopt;
        }

        if (isBranch(node))
            ++metrics.cyclomaticComplexity;

        if (isComplexConditional(node))
            ++metrics.complexConditionalCount;

        auto nests = language.nestingNodeTypes.contains(getNodeType(node))
                     && !isElseIf(node, position.parent);

        if (nests && position.depth == 0)
            position.topLevelStatement = node;

        if (nests)
            ++position.depth;

        metrics.maximumNestingDepth =
            std::max(metrics.maximumNestingDepth, position.depth);

        if (position.depth >= bumpNestingDepth)
            bumpyStatements.insert(ts_node_start_byte(position.topLevelStatement));

        position.parent = node;
        return position;
    }

    bool isComplexConditional(TSNode node) const
    {
        auto condition = getField(node, "condition");
        return !ts_node_is_null(condition)
               && countLogicalOperators(condition)
                      >= complexConditionalOperatorCount;
    }

    const Language& language;
    std::string_view source;
    std::set<uint32_t> bumpyStatements;
    std::vector<bool> codeRows;

public:
    std::vector<Token> tokens;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> functionByteRanges;
};

// Each function long enough to be checked for duplication, with its tokens.
std::vector<FunctionTokens>
    sliceFunctionTokens(const std::vector<FunctionMetrics>& functions,
                        const SourceMeasurer& measurer)
{
    constexpr auto minimumLinesOfCode = 10;
    auto sliced = std::vector<FunctionTokens> {};

    for (auto index = size_t {0}; index < functions.size(); ++index)
    {
        if (functions[index].length < minimumLinesOfCode)
            continue;

        auto [startByte, endByte] = measurer.functionByteRanges[index];
        auto byStart = [](const Token& token) { return token.startByte; };
        auto first =
            std::ranges::lower_bound(measurer.tokens, startByte, {}, byStart);
        auto last = std::ranges::lower_bound(measurer.tokens, endByte, {}, byStart);
        auto function = FunctionTokens {
            .name = functions[index].name,
            .startLine = functions[index].startLine,
            .endLine = first == last ? functions[index].startLine
                                     : static_cast<int>((last - 1)->row) + 1};

        for (auto token = first; token != last; ++token)
            function.kinds.push_back(token->kind);

        sliced.push_back(std::move(function));
    }

    return sliced;
}

} // namespace

int FileMetrics::calculateTotalComplexity() const
{
    auto total = 0;

    for (auto& function: functions)
        total += function.cyclomaticComplexity;

    return total;
}

struct SourceParser::Native
{
    ParserPointer parser {ts_parser_new()};
};

SourceParser::SourceParser() = default;

MeasuredSource SourceParser::measure(std::string_view source,
                                     const std::string& path,
                                     const Language& language)
{
    ts_parser_set_language(native->parser.get(), language.getGrammar());

    auto tree =
        TreePointer {ts_parser_parse_string(native->parser.get(),
                                            nullptr,
                                            source.data(),
                                            static_cast<uint32_t>(source.size()))};

    auto root = ts_tree_root_node(tree.get());
    auto measurer = SourceMeasurer {language, source, root};
    auto metrics = FileMetrics {.path = path,
                                .language = language.name,
                                .linesOfCode = measurer.countLinesOfCode()};
    measurer.collectFunctions(root, metrics.functions, metrics.dependencies);
    auto functionTokens = sliceFunctionTokens(metrics.functions, measurer);
    return {.metrics = std::move(metrics),
            .functionTokens = std::move(functionTokens)};
}
} // namespace CodeHealth
