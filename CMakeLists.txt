cmake_minimum_required(VERSION 3.31)

# The oldest macOS supported, as in eacp and the other pond apps: one value lets them share cached dependency libraries.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/lockstep.cmake)

project(CodeHealth VERSION 0.1.1 LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# No source this project compiles uses C++20 modules, so scanning each one for them is wasted work.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

lockstep_add_package(
        NAME eacp
        GITHUB_REPOSITORY eyalamirmusic/eacp
        GIT_TAG develop
        SYSTEM YES
        OPTIONS
            "EACP_BUILD_GRAPHICS OFF")

include(cmake/TreeSitter.cmake)

# Set after the packages, so only this project's own targets build with -Werror.
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

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
