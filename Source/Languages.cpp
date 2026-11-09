#include "Languages.h"

#include <eacp/Core/Utils/FilePath.h>

extern "C" const TSLanguage* tree_sitter_cpp();
extern "C" const TSLanguage* tree_sitter_javascript();
extern "C" const TSLanguage* tree_sitter_typescript();
extern "C" const TSLanguage* tree_sitter_tsx();
extern "C" const TSLanguage* tree_sitter_objcpp();
extern "C" const TSLanguage* tree_sitter_python();

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
            .dependencyFields = {"source"},
            .identifierNodeTypes = {"identifier",
                                    "property_identifier",
                                    "shorthand_property_identifier",
                                    "shorthand_property_identifier_pattern",
                                    "private_property_identifier",
                                    "type_identifier"},
            .literalNodeTypes = {"number",
                                 "string_fragment",
                                 "regex_pattern",
                                 "escape_sequence"},
            .classNodeTypes = {"class_declaration",
                               "class",
                               "abstract_class_declaration"},
            .fieldNodeTypes = {"public_field_definition", "field_definition"},
            .memberNameNodeTypes = {"property_identifier",
                                    "private_property_identifier"},
            .referenceNodeTypes = {"property_identifier",
                                   "private_property_identifier"}};
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
            .dependencyFields = {"path"},
            .identifierNodeTypes = {"identifier",
                                    "field_identifier",
                                    "type_identifier",
                                    "namespace_identifier",
                                    "statement_identifier"},
            .literalNodeTypes = {"number_literal",
                                 "string_content",
                                 "raw_string_content",
                                 "character",
                                 "escape_sequence",
                                 "system_lib_string"},
            .classNodeTypes = {"class_specifier", "struct_specifier"},
            .fieldNodeTypes = {"field_declaration"},
            .memberNameNodeTypes = {"field_identifier"},
            .referenceNodeTypes = {"identifier", "field_identifier"}};
}

// C++ plus Objective-C's methods, @try and @synchronized; the Objective-C++
// grammar parses plain Objective-C as well.
Language makeObjectiveCFamilyLanguage(std::string name,
                                      std::vector<std::string> extensions)
{
    auto language = makeCFamilyLanguage(std::move(name), std::move(extensions));
    language.getGrammar = tree_sitter_objcpp;
    language.functionNodeTypes.insert("method_definition");
    language.branchingNodeTypes.insert("objc_catch_clause");
    language.nestingNodeTypes.insert(
        {"objc_try_statement", "objc_catch_clause", "synchronized_statement"});
    return language;
}

Language makePythonLanguage()
{
    return {.name = "Python",
            .extensions = {".py", ".pyi"},
            .getGrammar = tree_sitter_python,
            .functionNodeTypes = {"function_definition"},
            // As CodeScene counts Python: each except and raise adds a path;
            // with, `x if c else y`, a comprehension's if and match cases do not.
            .branchingNodeTypes = {"if_statement",
                                   "elif_clause",
                                   "for_statement",
                                   "while_statement",
                                   "except_clause",
                                   "raise_statement",
                                   "for_in_clause"},
            // CodeScene's Python nesting counts conditionals and loops, not
            // try, with or match (scripts/calibration/python-probes.py).
            .nestingNodeTypes = {"if_statement", "for_statement", "while_statement"},
            .logicalOperators = {"and", "or"},
            .logicalOperationNodeTypes = {"boolean_operator"},
            .primitiveTypeNames = {"int", "str", "float", "bool", "bytes"},
            .dependencyNodeTypes = {"import_statement", "import_from_statement"},
            .dependencyFields = {"module_name", "name"},
            .identifierNodeTypes = {"identifier"},
            .literalNodeTypes = {"integer",
                                 "float",
                                 "string_content",
                                 "escape_sequence"},
            .classNodeTypes = {"class_definition"},
            .referenceNodeTypes = {"identifier"},
            .ignoredParameterNodeTypes = {"keyword_separator",
                                          "positional_separator",
                                          "comment"},
            .implicitParameterNames = {"self", "cls"},
            .selfNames = {"self"},
            .definitionWrapperNodeTypes = {"decorated_definition"},
            .statementBlockNodeTypes = {"block"},
            .alternativeClauseNodeTypes = {"elif_clause", "else_clause"},
            .hasDocstrings = true,
            .measuresModuleCohesion = true};
}

const std::vector<Language>& getLanguages()
{
    static const auto languages = std::vector<Language> {
        makeCFamilyLanguage("C++",
                            {".cpp",
                             ".cc",
                             ".cxx",
                             ".h",
                             ".hpp",
                             ".hh",
                             ".hxx",
                             ".ipp",
                             ".inl",
                             ".tpp"}),
        makeCFamilyLanguage("C", {".c"}),
        makeObjectiveCFamilyLanguage("Objective-C++", {".mm"}),
        makeObjectiveCFamilyLanguage("Objective-C", {".m"}),
        makePythonLanguage(),
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
