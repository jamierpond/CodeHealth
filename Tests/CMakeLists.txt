if (NOT TARGET NanoTest)
    CPMAddPackage(
            NAME NanoTest
            GITHUB_REPOSITORY eyalamirmusic/NanoTest
            GIT_TAG main
            SYSTEM YES)
endif ()

add_executable(CodeHealthTests
        MetricsTests.cpp
        TypeScriptMetricsTests.cpp
        ScoreTests.cpp
        RepositoryTests.cpp
        LargeSourceTests.cpp
        CouplingTests.cpp
        ParallelTests.cpp
        GitRecordsTests.cpp)
target_link_libraries(CodeHealthTests PRIVATE NanoTest NanoTestMain CodeHealthCore)
target_compile_definitions(CodeHealthTests PRIVATE CODEHEALTH_EACP_SOURCE_DIR="${eacp_SOURCE_DIR}")
nano_discover_tests(CodeHealthTests)
