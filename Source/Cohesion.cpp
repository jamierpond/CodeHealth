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
    std::set<std::string> selfAttributes;
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

    // LCOM4 over the whole module: its functions and every class's methods.
    // Inside a class, methods relate as in measureClass; any function or method
    // that names a top-level function or class relates to it.
    int measureModuleCohesionGroups()
    {
        auto units = std::vector<Method> {};
        auto classes = std::vector<ModuleClass> {};
        auto firstUnitByTopLevelName = std::map<std::string, size_t> {};

        forEachNamedChild(
            root,
            [&](TSNode child)
            {
                if (language.definitionWrapperNodeTypes.contains(getNodeType(child)))
                    child = getField(child, "definition");

                addModuleMember(child, units, classes, firstUnitByTopLevelName);
            });

        auto groups = DisjointGroups {units.size()};

        for (auto& moduleClass: classes)
            joinMethodsSharingMembers(
                groups,
                {units.begin() + moduleClass.firstUnit,
                 units.begin() + moduleClass.firstUnit + moduleClass.methodCount},
                moduleClass.firstUnit,
                moduleClass.fields);

        for (auto index = size_t {0}; index < units.size(); ++index)
            for (auto& reference: units[index].references)
                if (auto found = firstUnitByTopLevelName.find(reference);
                    found != firstUnitByTopLevelName.end())
                    groups.join(index, found->second);

        return groups.countGroups();
    }

private:
    struct ModuleClass
    {
        size_t firstUnit = 0;
        size_t methodCount = 0;
        std::set<std::string> fields;
    };

    void addModuleMember(TSNode member,
                         std::vector<Method>& units,
                         std::vector<ModuleClass>& classes,
                         std::map<std::string, size_t>& firstUnitByTopLevelName)
    {
        auto type = getNodeType(member);

        if (language.functionNodeTypes.contains(type))
        {
            firstUnitByTopLevelName.emplace(findMemberName(member), units.size());
            units.push_back(describeMethod(member, findMemberName(member)));
            return;
        }

        if (!language.classNodeTypes.contains(type) || !hasField(member, "body"))
            return;

        auto moduleClass = ModuleClass {.firstUnit = units.size()};

        forEachNamedChild(
            getField(member, "body"),
            [&](TSNode method)
            {
                if (language.definitionWrapperNodeTypes.contains(
                        getNodeType(method)))
                    method = getField(method, "definition");

                if (!language.functionNodeTypes.contains(getNodeType(method)))
                    return;

                units.push_back(describeMethod(method, findMemberName(method)));
                moduleClass.fields.insert(units.back().selfAttributes.begin(),
                                          units.back().selfAttributes.end());
            });

        moduleClass.methodCount = units.size() - moduleClass.firstUnit;

        if (moduleClass.methodCount > 0)
            firstUnitByTopLevelName.emplace(
                getNodeText(getField(member, "name"), source),
                moduleClass.firstUnit);

        classes.push_back(std::move(moduleClass));
    }

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
                if (language.definitionWrapperNodeTypes.contains(
                        getNodeType(member)))
                    member = getField(member, "definition");

                if (language.fieldNodeTypes.contains(getNodeType(member)))
                    addFieldNames(member, fields);
                else if (language.functionNodeTypes.contains(getNodeType(member)))
                    methods.push_back(
                        describeMethod(member, findMemberName(member)));
            });

        for (auto& method: outOfLineMethodsByClass[name])
            methods.push_back(method);

        for (auto& method: methods)
            fields.insert(method.selfAttributes.begin(),
                          method.selfAttributes.end());

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

                     if (isSelfAttribute(node) && !isCalled(node))
                         method.selfAttributes.insert(
                             getNodeText(getField(node, "attribute"), source));

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

        // `int ::f()` names the global scope: no class to attach it to.
        if (ts_node_is_null(qualified) || !hasField(qualified, "scope"))
            return;

        auto className = getNodeText(getField(qualified, "scope"), source);
        outOfLineMethodsByClass[className].push_back(
            describeMethod(node, getNodeText(getField(qualified, "name"), source)));
    }

    // `self.assertEqual(...)` calls a method, maybe an inherited one; it is not
    // a field the caller shares with the methods that use it.
    static bool isCalled(TSNode node)
    {
        auto parent = ts_node_parent(node);
        return getNodeType(parent) == "call"
               && ts_node_eq(getField(parent, "function"), node);
    }

    bool isSelfAttribute(TSNode node) const
    {
        return getNodeType(node) == "attribute" && hasField(node, "object")
               && language.selfNames.contains(
                   getNodeText(getField(node, "object"), source));
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
        joinMethodsSharingMembers(groups, methods, 0, fields);
        return groups.countGroups();
    }

    // Methods are related when they use the same field or one calls the other;
    // firstIndex places these methods among everything groups holds.
    static void joinMethodsSharingMembers(DisjointGroups& groups,
                                          const std::vector<Method>& methods,
                                          size_t firstIndex,
                                          const std::set<std::string>& fields)
    {
        auto firstUserOfName = std::map<std::string, size_t> {};

        for (auto index = size_t {0}; index < methods.size(); ++index)
            firstUserOfName.emplace(methods[index].name, firstIndex + index);

        for (auto index = size_t {0}; index < methods.size(); ++index)
            for (auto& reference: methods[index].references)
            {
                auto isMember = fields.contains(reference)
                                || firstUserOfName.contains(reference);

                if (!isMember)
                    continue;

                auto [user, isFirst] =
                    firstUserOfName.emplace(reference, firstIndex + index);

                if (!isFirst)
                    groups.join(firstIndex + index, user->second);
            }
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

int measureModuleCohesionGroups(const TSNode& root,
                                std::string_view source,
                                const Language& language)
{
    return CohesionMeasurer {root, source, language}.measureModuleCohesionGroups();
}
} // namespace CodeHealth
