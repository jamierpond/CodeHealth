#include "Languages.h"

#include <eacp/Core/Utils/FilePath.h>

extern "C" const TSLanguage* tree_sitter_cpp();
extern "C" const TSLanguage* tree_sitter_javascript();
extern "C" const TSLanguage* tree_sitter_typescript();
extern "C" const TSLanguage* tree_sitter_tsx();

namespace CodeHealth
{
namespace
{
const NodeTypes javaScriptFunctionNodeTypes {"function_declaration",
                                             "function_expression",
                                             "arrow_function",
                                             "method_definition",
                                             "generator_function_declaration",
                                             "generator_function"};

const NodeTypes javaScriptBranchingNodeTypes {"if_statement",
                                              "for_statement",
                                              "for_in_statement",
                                              "while_statement",
                                              "do_statement",
                                              "catch_clause",
                                              "ternary_expression"};

const NodeTypes javaScriptNestingNodeTypes {"if_statement",
                                            "for_statement",
                                            "for_in_statement",
                                            "while_statement",
                                            "do_statement",
                                            "switch_statement",
                                            "try_statement",
                                            "catch_clause"};

const NodeTypes javaScriptLogicalOperators {"&&", "||", "??"};

Language makeJavaScriptFamilyLanguage(std::string name,
                                      std::vector<std::string> extensions,
                                      const TSLanguage* (*getGrammar)())
{
    return {.name = std::move(name),
            .extensions = std::move(extensions),
            .getGrammar = getGrammar,
            .functionNodeTypes = javaScriptFunctionNodeTypes,
            .branchingNodeTypes = javaScriptBranchingNodeTypes,
            .nestingNodeTypes = javaScriptNestingNodeTypes,
            .caseNodeType = "switch_case",
            .logicalOperators = javaScriptLogicalOperators,
            .primitiveTypeNodeTypes = {"predefined_type"},
            .dependencyNodeTypes = {"import_statement"},
            .dependencyField = "source"};
}

Language makeCFamilyLanguage(std::string name, std::vector<std::string> extensions)
{
    return {.name = std::move(name),
            .extensions = std::move(extensions),
            .getGrammar = tree_sitter_cpp,
            .functionNodeTypes = {"function_definition"},
            .branchingNodeTypes = {"if_statement",
                                   "for_statement",
                                   "for_range_loop",
                                   "while_statement",
                                   "do_statement",
                                   "catch_clause",
                                   "conditional_expression"},
            .nestingNodeTypes = {"if_statement",
                                 "for_statement",
                                 "for_range_loop",
                                 "while_statement",
                                 "do_statement",
                                 "switch_statement",
                                 "try_statement",
                                 "catch_clause"},
            .caseNodeType = "case_statement",
            .logicalOperators = {"&&", "||", "and", "or"},
            .primitiveTypeNodeTypes = {"primitive_type", "sized_type_specifier"},
            .primitiveTypeNames = {"string",
                                   "string_view",
                                   "size_t",
                                   "int8_t",
                                   "int16_t",
                                   "int32_t",
                                   "int64_t",
                                   "uint8_t",
                                   "uint16_t",
                                   "uint32_t",
                                   "uint64_t"},
            .dependencyNodeTypes = {"preproc_include"},
            .dependencyField = "path"};
}

const std::vector<Language>& getLanguages()
{
    static const auto languages = std::vector<Language> {
        makeCFamilyLanguage("C++",
                            {".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh", ".hxx"}),
        makeCFamilyLanguage("C", {".c"}),
        makeJavaScriptFamilyLanguage(
            "JavaScript", {".js", ".jsx", ".mjs", ".cjs"}, tree_sitter_javascript),
        makeJavaScriptFamilyLanguage(
            "TypeScript", {".ts", ".mts", ".cts"}, tree_sitter_typescript),
        makeJavaScriptFamilyLanguage("TSX", {".tsx"}, tree_sitter_tsx),
        makeJavaScriptFamilyLanguage("JSX", {".jsx"}, tree_sitter_javascript),
    };

    return languages;
}
} // namespace

std::optional<LanguageReference> findLanguageForPath(std::string_view path)
{
    auto extension = eacp::FilePath {path}.extension();

    for (auto& language: getLanguages())
        for (auto& candidate: language.extensions)
            if (candidate == extension)
                return std::cref(language);

    return std::nullopt;
}
} // namespace CodeHealth
