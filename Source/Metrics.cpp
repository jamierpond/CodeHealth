#include "Metrics.h"
#include "Languages.h"
#include "SyntaxTree.h"

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
constexpr auto quoteCharacters = std::string_view {"\"'<>`"};
const std::set<std::string_view> typeWrapperNodeTypes {"type_annotation", "type"};

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

// An Objective-C method has no name or parameter fields: it is named by its
// selector, `setValue:forKey:`, and takes one argument per keyword.
bool isSelectorMethod(TSNode function)
{
    return getNodeType(function) == "method_definition"
           && !hasField(function, "name");
}

std::string findSelectorName(TSNode method, std::string_view source)
{
    auto name = std::string {};

    forEachNamedChild(
        method,
        [&](TSNode child)
        {
            if (getNodeType(child) == "identifier" && name.empty())
                name = getNodeText(child, source);
            else if (getNodeType(child) == "keyword_declarator")
                name += getNodeText(ts_node_named_child(child, 0), source) + ":";
        });

    return name;
}

int countSelectorArguments(TSNode method)
{
    auto count = 0;
    forEachNamedChild(
        method,
        [&](TSNode child)
        { count += getNodeType(child) == "keyword_declarator" ? 1 : 0; });
    return count;
}

TSNode findBody(TSNode function)
{
    auto body = getField(function, "body");

    if (!ts_node_is_null(body) || !isSelectorMethod(function))
        return body;

    forEachNamedChild(function,
                      [&](TSNode child)
                      {
                          if (getNodeType(child) == "compound_statement")
                              body = child;
                      });
    return body;
}

bool isArgument(TSNode parameter, const Language& language, std::string_view source)
{
    auto type = getNodeType(parameter);

    if (type == "comment" || language.ignoredParameterNodeTypes.contains(type))
        return false;

    return !language.implicitParameterNames.contains(getNodeText(parameter, source));
}

int countParameters(TSNode signature,
                    const Language& language,
                    std::string_view source)
{
    if (isSelectorMethod(signature))
        return countSelectorArguments(signature);

    if (hasField(signature, "parameter"))
        return 1;

    auto count = 0;
    forEachNamedChild(getField(signature, "parameters"),
                      [&](TSNode parameter)
                      { count += isArgument(parameter, language, source) ? 1 : 0; });
    return count;
}

std::string
    findFunctionName(TSNode function, TSNode signature, std::string_view source)
{
    if (isSelectorMethod(function))
        return findSelectorName(function, source);

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

                     if (!startsOwnFunction(node, enclosingName))
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

        return ts_node_child_count(node) == 0
               && language.primitiveTypeNames.contains(getNodeText(node, source));
    }

    bool startsOwnFunction(TSNode node, const std::string& enclosingName) const
    {
        return isFunction(node) && (enclosingName.empty() || isNamedFunction(node));
    }

    // The parameter's own type, unwrapped from a TypeScript annotation or a
    // C++ qualified name, so `std::string` counts and `{a: string}` does not.
    bool hasPrimitiveType(TSNode parameter) const
    {
        auto type = getField(parameter, "type");

        if (ts_node_is_null(type))
            return false;

        if (typeWrapperNodeTypes.contains(getNodeType(type)))
            type = ts_node_named_child(type, 0);

        if (getNodeType(type) == "qualified_identifier")
            type = getField(type, "name");

        return isPrimitiveType(type);
    }

    // `(int)value` in a selector: the keyword's method_type names the type.
    bool hasPrimitiveSelectorArgument(TSNode method) const
    {
        auto hasPrimitive = false;
        walkTree(method,
                 true,
                 [&](TSNode node, bool)
                 {
                     if (getNodeType(node) == "compound_statement")
                         return std::optional<bool> {};

                     hasPrimitive =
                         hasPrimitive
                         || (getNodeType(node) == "type_name"
                             && isPrimitiveType(ts_node_named_child(node, 0)));
                     return std::optional {true};
                 });
        return hasPrimitive;
    }

    bool hasPrimitiveParameter(TSNode signature) const
    {
        if (isSelectorMethod(signature))
            return hasPrimitiveSelectorArgument(signature);

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

    std::string findFirstFieldText(TSNode node,
                                   const std::vector<std::string_view>& fields) const
    {
        for (auto field: fields)
            if (hasField(node, field))
                return getNodeText(getField(node, field), source);

        return {};
    }

    // "Foo.h", <vector> and './foo' all become the bare target.
    std::string findDependencyTarget(TSNode node) const
    {
        auto target = findFirstFieldText(node, language.dependencyFields);
        auto isQuote = [](char c)
        { return quoteCharacters.find(c) != std::string_view::npos; };
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
        if (!language.logicalOperationNodeTypes.contains(getNodeType(node)))
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
            metrics.argumentCount = countParameters(signature, language, source);
            metrics.hasPrimitiveArgument = hasPrimitiveParameter(signature);
        }

        auto body = findBody(function);
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

        return enterNode(node, position, metrics);
    }

    // Moves one level deeper for a nesting statement, and records the bump the
    // node sits in once it is deep enough.
    WalkPosition
        enterNode(TSNode node, WalkPosition position, FunctionMetrics& metrics)
    {
        if (isNesting(node, position.parent))
        {
            if (position.depth == 0)
                position.topLevelStatement = node;

            ++position.depth;
        }

        metrics.maximumNestingDepth =
            std::max(metrics.maximumNestingDepth, position.depth);

        if (position.depth >= bumpNestingDepth)
            bumpyStatements.insert(ts_node_start_byte(position.topLevelStatement));

        position.parent = node;
        return position;
    }

    bool isNesting(TSNode node, TSNode parent) const
    {
        return language.nestingNodeTypes.contains(getNodeType(node))
               && !isElseIf(node, parent);
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
    metrics.classes = measureClassCohesion(root, source, language);
    auto functionTokens = sliceFunctionTokens(metrics.functions, measurer);
    return {.metrics = std::move(metrics),
            .functionTokens = std::move(functionTokens)};
}
} // namespace CodeHealth
