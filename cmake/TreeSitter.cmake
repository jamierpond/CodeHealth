# tree-sitter's core and its grammars, compiled as plain C from their sources:
# no build system of theirs runs, and none pulls in anything else.
CPMAddPackage(NAME tree_sitter GITHUB_REPOSITORY tree-sitter/tree-sitter
        GIT_TAG v0.25.10 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME tree_sitter_cpp GITHUB_REPOSITORY tree-sitter/tree-sitter-cpp
        GIT_TAG v0.23.4 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME tree_sitter_typescript
        GITHUB_REPOSITORY tree-sitter/tree-sitter-typescript
        GIT_TAG v0.23.2 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME tree_sitter_objcpp
        GITHUB_REPOSITORY VladimirMakaev/tree-sitter-objcpp
        GIT_TAG eeef22e310ea16e2156a398190e7a326341950f8 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME tree_sitter_python GITHUB_REPOSITORY tree-sitter/tree-sitter-python
        GIT_TAG v0.25.0 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME tree_sitter_javascript
        GITHUB_REPOSITORY tree-sitter/tree-sitter-javascript
        GIT_TAG v0.25.0 DOWNLOAD_ONLY YES)

add_library(tree-sitter STATIC ${tree_sitter_SOURCE_DIR}/lib/src/lib.c)
target_include_directories(tree-sitter
        PUBLIC ${tree_sitter_SOURCE_DIR}/lib/include
        PRIVATE ${tree_sitter_SOURCE_DIR}/lib/src)

function(add_tree_sitter_grammar target grammarDirectory)
    add_library(${target} STATIC ${grammarDirectory}/src/parser.c)
    if (EXISTS ${grammarDirectory}/src/scanner.c)
        target_sources(${target} PRIVATE ${grammarDirectory}/src/scanner.c)
    endif ()
    target_include_directories(${target} PRIVATE ${grammarDirectory}/src)
    target_link_libraries(${target} PUBLIC tree-sitter)
endfunction()

add_tree_sitter_grammar(tree-sitter-cpp ${tree_sitter_cpp_SOURCE_DIR})
add_tree_sitter_grammar(tree-sitter-javascript ${tree_sitter_javascript_SOURCE_DIR})
add_tree_sitter_grammar(tree-sitter-typescript ${tree_sitter_typescript_SOURCE_DIR}/typescript)
add_tree_sitter_grammar(tree-sitter-tsx ${tree_sitter_typescript_SOURCE_DIR}/tsx)
add_tree_sitter_grammar(tree-sitter-objcpp ${tree_sitter_objcpp_SOURCE_DIR})
add_tree_sitter_grammar(tree-sitter-python ${tree_sitter_python_SOURCE_DIR})

add_library(tree-sitter-grammars INTERFACE)
foreach (target tree-sitter tree-sitter-cpp tree-sitter-javascript
        tree-sitter-typescript tree-sitter-tsx tree-sitter-objcpp tree-sitter-python)
    set_target_properties(${target} PROPERTIES C_STANDARD 11)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W0 /utf-8)
    else ()
        target_compile_options(${target} PRIVATE -w)
    endif ()
    target_link_libraries(tree-sitter-grammars INTERFACE ${target})
endforeach ()
