#include "Common.h"

using namespace nano;
using namespace CodeHealth;
using namespace CodeHealth::Testing;

namespace
{
constexpr auto typeScriptFixture = R"ts(
export function pick(a: number, b?: number, ...rest: number[]): number {
  if (a > 0 && b !== undefined || rest.length > 0) {
    return a;
  }
  return b ?? 0;
}

const double = (value: number) => value * 2;

class Store {
  load(key: string) {
    for (const item of this.items) {
      if (item.key === key) {
        return item;
      }
    }
    return undefined;
  }
}
)ts";

constexpr auto tsxFixture = R"tsx(
export function Panel({ items, open }: Props) {
  const onClick = () => { if (open) { close(); } };
  return (
    <div>
      {open && items.map((item) => <Row key={item.id} item={item} />)}
    </div>
  );
}
)tsx";
} // namespace

auto tTsFunctions =
    test("TypeScriptMetrics/finds declarations, arrows and methods") = []
{
    auto file = measureFixture(typeScriptFixture, "fixture.ts");
    check(file.functions.size() == 3);
    check(file.language == "TypeScript");
};

auto tTsPick = test("TypeScriptMetrics/counts && || ?? and parameters") = []
{
    auto pick =
        findFunction(measureFixture(typeScriptFixture, "fixture.ts"), "pick");
    // if, &&, ||, ??
    check(pick.cyclomaticComplexity == 5);
    check(pick.complexConditionalCount == 1);
    check(pick.argumentCount == 3);
};

auto tTsArrowName = test("TypeScriptMetrics/an arrow takes its variable's name") = []
{
    auto doubled =
        findFunction(measureFixture(typeScriptFixture, "fixture.ts"), "double");
    check(doubled.argumentCount == 1);
    check(doubled.cyclomaticComplexity == 1);
};

auto tTsMethodNesting = test("TypeScriptMetrics/a method nests loops and ifs") = []
{
    auto load =
        findFunction(measureFixture(typeScriptFixture, "fixture.ts"), "load");
    check(load.maximumNestingDepth == 2);
    check(load.cyclomaticComplexity == 3);
};

auto tTsxNamedFunctionsAreSeparate = test(
    "TypeScriptMetrics/a named inner function is its own, named after its parent") =
    []
{
    auto file = measureFixture(tsxFixture, "Panel.tsx");
    check(file.functions.size() == 2);
    check(findFunction(file, "Panel.onClick").cyclomaticComplexity == 2);
};

auto tTsxAnonymousFunctionsFold = test(
    "TypeScriptMetrics/an anonymous callback and JSX logic count toward the component") =
    []
{
    auto panel = findFunction(measureFixture(tsxFixture, "Panel.tsx"), "Panel");
    check(panel.cyclomaticComplexity == 2);
    check(panel.maximumNestingDepth == 0);
    check(panel.length == 7);
};

auto tJavaScript =
    test("TypeScriptMetrics/plain JavaScript parses with the same rules") = []
{
    auto file = measureFixture("function f(a, b) { return a || b; }\n", "f.mjs");
    check(file.language == "JavaScript");
    check(findFunction(file, "f").cyclomaticComplexity == 2);
};
