#include <History.h>
#include <Repository.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace CodeHealth;

auto tExcludes =
    test("Repository/excludes build, vendored and generated directories") = []
{
    check(isExcludedPath("node_modules/x/index.js"));
    check(isExcludedPath("web/build/App.js"));
    check(isExcludedPath("build-release/gen.cpp"));
    check(isExcludedPath("cmake-build-debug/a.cpp"));
    check(isExcludedPath("Lib/ThirdParty/miniz/miniz.c"));
    check(isExcludedPath("src/Generated/Api.ts"));
    check(isExcludedPath("src/types.d.ts"));
    check(!isExcludedPath("Lib/eacp/Core/App.cpp"));
    check(!isExcludedPath("src/BuildSettings.cpp"));
};
