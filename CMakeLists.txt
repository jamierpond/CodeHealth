cmake_minimum_required(VERSION 3.25)

project(CodeHealth VERSION 0.1.0 LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CPM.cmake)

set(EACP_BUILD_GRAPHICS OFF CACHE BOOL "" FORCE)
set(EACP_BUILD_WEBVIEW OFF CACHE BOOL "" FORCE)

CPMAddPackage(
        NAME eacp
        GITHUB_REPOSITORY eyalamirmusic/eacp
        GIT_TAG develop
        SYSTEM YES
        EXCLUDE_FROM_ALL YES)

include(cmake/TreeSitter.cmake)

add_library(CodeHealthCore STATIC
        Source/Languages.cpp
        Source/Metrics.cpp
        Source/Score.cpp
        Source/Cohesion.cpp
        Source/Coupling.cpp
        Source/Diff.cpp
        Source/GitHubReport.cpp
        Source/Duplication.cpp
        Source/DuplicateView.cpp
        Source/CommandNames.cpp
        Source/FunctionClones.cpp
        Source/History.cpp
        Source/GitRecords.cpp
        Source/Repository.cpp
        Source/Report.cpp)
target_include_directories(CodeHealthCore PUBLIC Source)
target_link_libraries(CodeHealthCore PUBLIC eacp-core PRIVATE tree-sitter-grammars)

add_executable(codehealth Source/Main.cpp)
target_link_libraries(codehealth PRIVATE CodeHealthCore)
target_compile_definitions(codehealth PRIVATE CODEHEALTH_VERSION="${PROJECT_VERSION}")

if (PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_subdirectory(Tests)
endif ()
