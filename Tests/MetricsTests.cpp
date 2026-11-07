#include "Common.h"

using namespace nano;
using namespace CodeHealth;
using namespace CodeHealth::Testing;

namespace
{
constexpr auto cppFixture = R"cpp(
int add(int a, int b)
{
    return a + b;
}

int classify(int value, bool strict, bool verbose, int low, int high)
{
    if (value < low && strict || verbose)
        return -1;
    else if (value > high)
        return 1;

    for (auto index = 0; index < value; ++index)
    {
        if (index % 2 == 0)
        {
            while (strict)
            {
                if (verbose)
                    break;
            }
        }
    }

    switch (value)
    {
        case 1: return 2;
        case 2: return 3;
        default: return value > 0 ? 4 : 5;
    }
}

struct Widget
{
    void paint() const
    {
        auto draw = [](bool visible) { if (visible) return; };
        draw(true);
    }
};

template <typename T>
T* findFirst(T* items, int count)
{
    return count > 0 ? items : nullptr;
}
)cpp";
} // namespace

auto tCppFindsEveryFunction =
    test("CppMetrics/finds free, member and template functions") = []
{
    auto file = measureFixture(cppFixture, "fixture.cpp");
    check(file.functions.size() == 4);
    check(file.language == "C++");
};

auto tCppSimpleFunction =
    test("CppMetrics/a straight-line function is all ones") = []
{
    auto add = findFunction(measureFixture(cppFixture, "fixture.cpp"), "add");
    check(add.cyclomaticComplexity == 1);
    check(add.maximumNestingDepth == 0);
    check(add.argumentCount == 2);
    check(add.length == 4);
    check(add.startLine == 2);
};

auto tCppComplexity =
    test("CppMetrics/counts branches, cases and logical operators") = []
{
    auto classify =
        findFunction(measureFixture(cppFixture, "fixture.cpp"), "classify");
    // if, &&, ||, else if, for, if, while, if, case, case, ternary
    check(classify.cyclomaticComplexity == 12);
    check(classify.argumentCount == 5);
    check(classify.complexConditionalCount == 1);
};

auto tCppNesting = test("CppMetrics/else-if does not nest, loops and ifs do") = []
{
    auto classify =
        findFunction(measureFixture(cppFixture, "fixture.cpp"), "classify");
    check(classify.maximumNestingDepth == 4);
    check(classify.bumpCount == 1);
};

auto tCppLambdaStaysInside =
    test("CppMetrics/a lambda counts toward its function") = []
{
    auto paint = findFunction(measureFixture(cppFixture, "fixture.cpp"), "paint");
    check(paint.cyclomaticComplexity == 2);
};

auto tCppBumpyRoad =
    test("CppMetrics/separate nested blocks are separate bumps") = []
{
    auto source = R"cpp(
void process(bool a, bool b)
{
    if (a) { if (b) {} }
    for (;;) { while (b) {} }
    if (b) {}
}
)cpp";
    auto process = findFunction(measureFixture(source, "bumps.h"), "process");
    check(process.bumpCount == 2);
    check(process.maximumNestingDepth == 2);
};

auto tCppLinesOfCode =
    test("CppMetrics/counts lines with or without a final newline") = []
{
    check(measureFixture("int a;\nint b;\n", "a.cpp").linesOfCode == 2);
    check(measureFixture("int a;\nint b;", "a.cpp").linesOfCode == 2);
};

auto tObjectiveCMethods = test(
    "ObjectiveCMetrics/methods are named by selector, with one argument per keyword") =
    []
{
    auto source = R"mm(
@implementation Store
- (void)setValue:(int)value forKey:(NSString*)key {
    if (value > 0 && key) { [self save:value]; }
    for (id item in self.items) { if (item) { NSLog(@"%@", item); } }
}
- (int)count { return 3; }
@end
)mm";
    auto file = measureFixture(source, "Store.mm");
    check(file.language == "Objective-C++");
    check(file.functions.size() == 2);

    auto setter = findFunction(file, "setValue:forKey:");
    check(setter.argumentCount == 2);
    check(setter.hasPrimitiveArgument);
    check(setter.cyclomaticComplexity == 5);
    check(setter.maximumNestingDepth == 2);
    check(findFunction(file, "count").argumentCount == 0);
};
