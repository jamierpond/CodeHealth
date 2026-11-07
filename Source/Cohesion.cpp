#include "Cohesion.h"
#include "Languages.h"
#include "SyntaxTree.h"

#include <map>
#include <numeric>
#include <optional>
#include <set>

namespace CodeHealth
{
namespace
{
struct Method
{
    std::string name;
    std::set<std::string> references;
};

class DisjointGroups
{
public:
    explicit DisjointGroups(size_t count)
        : parents(count)
    {
        std::iota(parents.begin(), parents.end(), size_t {0});
    }

    size_t findGroup(size_t member)
    {
        while (parents[member] != member)
            member = parents[member] = parents[parents[member]];

        return member;
    }

    void join(size_t first, size_t second)
    {
        parents[findGroup(first)] = findGroup(second);
    }

    int countGroups()
    {
        auto roots = std::set<size_t> {};

        for (auto member = size_t {0}; member < parents.size(); ++member)
            roots.insert(findGroup(member));

        return static_cast<int>(roots.size());
    }

private:
    std::vector<size_t> parents;
};

class CohesionMeasurer
{
public:
    CohesionMeasurer(TSNode rootToUse,
                     std::string_view sourceText,
                     const Language& languageToUse)
        : root(rootToUse)
        , source(sourceText)
        , language(languageToUse)
    {
    }

    std::vector<ClassMetrics> measure()
    {
        auto classNodes = std::vector<TSNode> {};

        walkTree(root,
                 true,
                 [&](TSNode node, bool)
                 {
                     if (language.classNodeTypes.contains(getNodeType(node))
                         && hasField(node, "body"))
                         classNodes.push_back(node);

                     collectOutOfLineMethod(node);
                     return std::optional {true};
                 });

        auto classes = std::vector<ClassMetrics> {};

        for (auto classNode: classNodes)
            classes.push_back(measureClass(classNode));

        return classes;
    }

private:
    ClassMetrics measureClass(TSNode classNode)
    {
        auto name = hasField(classNode, "name")
                        ? getNodeText(getField(classNode, "name"), source)
                        : std::string {};
        auto fields = std::set<std::string> {};
        auto methods = std::vector<Method> {};

        forEachNamedChild(
            getField(classNode, "body"),
            [&](TSNode member)
            {
                if (language.fieldNodeTypes.contains(getNodeType(member)))
                    addFieldNames(member, fields);
                else if (language.functionNodeTypes.contains(getNodeType(member)))
                    methods.push_back(
                        describeMethod(member, findMemberName(member)));
            });

        for (auto& method: outOfLineMethodsByClass[name])
            methods.push_back(method);

        return {.name = name,
                .startLine =
                    static_cast<int>(ts_node_start_point(classNode).row) + 1,
                .methodCount = static_cast<int>(methods.size()),
                .fieldCount = static_cast<int>(fields.size()),
                .cohesionGroups = countCohesionGroups(methods, fields)};
    }

    // A C++ field declaration that declares a function is a method, not a field.
    void addFieldNames(TSNode declaration, std::set<std::string>& fields) const
    {
        auto names = std::vector<std::string> {};
        auto declaresFunction = false;

        walkTree(declaration,
                 true,
                 [&](TSNode node, bool)
                 {
                     declaresFunction =
                         declaresFunction
                         || getNodeType(node) == "function_declarator";

                     if (language.memberNameNodeTypes.contains(getNodeType(node)))
                         names.push_back(getNodeText(node, source));

                     return std::optional {true};
                 });

        if (!declaresFunction)
            fields.insert(names.begin(), names.end());
    }

    std::string findMemberName(TSNode function) const
    {
        if (hasField(function, "name"))
            return getNodeText(getField(function, "name"), source);

        auto declarator = getField(function, "declarator");

        while (!ts_node_is_null(declarator) && hasField(declarator, "declarator"))
            declarator = getField(declarator, "declarator");

        if (!ts_node_is_null(declarator) && hasField(declarator, "name"))
            return getNodeText(getField(declarator, "name"), source);

        return ts_node_is_null(declarator) ? std::string {}
                                           : getNodeText(declarator, source);
    }

    Method describeMethod(TSNode function, std::string name) const
    {
        auto method = Method {.name = std::move(name)};
        auto body = getField(function, "body");

        if (ts_node_is_null(body))
            return method;

        walkTree(body,
                 true,
                 [&](TSNode node, bool)
                 {
                     if (language.referenceNodeTypes.contains(getNodeType(node)))
                         method.references.insert(getNodeText(node, source));

                     return std::optional {true};
                 });

        return method;
    }

    // `void Widget::paint() { ... }` written outside the class body.
    void collectOutOfLineMethod(TSNode node)
    {
        if (!language.functionNodeTypes.contains(getNodeType(node)))
            return;

        auto qualified = findQualifiedName(node);

        if (ts_node_is_null(qualified))
            return;

        auto className = getNodeText(getField(qualified, "scope"), source);
        outOfLineMethodsByClass[className].push_back(
            describeMethod(node, getNodeText(getField(qualified, "name"), source)));
    }

    TSNode findQualifiedName(TSNode function) const
    {
        auto declarator = getField(function, "declarator");

        while (!ts_node_is_null(declarator)
               && getNodeType(declarator) != "qualified_identifier")
            declarator = getField(declarator, "declarator");

        return declarator;
    }

    static int countCohesionGroups(const std::vector<Method>& methods,
                                   const std::set<std::string>& fields)
    {
        auto groups = DisjointGroups {methods.size()};
        auto firstUserOfName = std::map<std::string, size_t> {};

        for (auto index = size_t {0}; index < methods.size(); ++index)
            firstUserOfName.emplace(methods[index].name, index);

        for (auto index = size_t {0}; index < methods.size(); ++index)
            for (auto& reference: methods[index].references)
            {
                auto isMember = fields.contains(reference)
                                || firstUserOfName.contains(reference);

                if (!isMember)
                    continue;

                auto [user, isFirst] = firstUserOfName.emplace(reference, index);

                if (!isFirst)
                    groups.join(index, user->second);
            }

        return groups.countGroups();
    }

    TSNode root;
    std::string_view source;
    const Language& language;
    std::map<std::string, std::vector<Method>> outOfLineMethodsByClass;
};
} // namespace

std::vector<ClassMetrics> measureClassCohesion(const TSNode& root,
                                               std::string_view source,
                                               const Language& language)
{
    return CohesionMeasurer {root, source, language}.measure();
}
} // namespace CodeHealth
