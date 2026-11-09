#include "Common.h"

#include <Score.h>

using namespace nano;
using namespace CodeHealth;
using namespace CodeHealth::Testing;

// Each fixture mirrors a probe in scripts/calibration/python-probes.py, where
// CodeScene's answer for the same code is recorded.
namespace
{
constexpr auto exceptionsAndRaise = R"py(
def handle(a, b):
    try:
        a += 1
    except ValueError:
        raise KeyError(a)
    except KeyError:
        a -= 1
    with open(b) as f:
        a = 1 if b else 2
    a = [x for x in b if x]
    match a:
        case 1:
            a += 1
        case _:
            a += 2
    return a
)py";

constexpr auto tryAndWithAroundIfs = R"py(
def nested(a, b):
    for x in b:
        try:
            with open(b) as f:
                if a:
                    if b:
                        a += 1
    return a
)py";

constexpr auto bumpShapes = R"py(
def two_ifs_in_one_loop(a, b):
    for x in b:
        if a:
            a += 1
        if b:
            a += 2
    return a


def two_nested_chunks_in_one_loop(a, b):
    for x in b:
        if a:
            if b:
                a += 1
        a += 1
        if b:
            if a:
                a += 2
    return a


def branches_each_nesting(a, b):
    if a:
        if b:
            a += 1
    elif b:
        if a:
            a += 2
    return a


def two_loops_each_holding_an_if(a, b):
    for x in b:
        if a:
            a += 1
    for x in b:
        if b:
            a += 2
    return a
)py";

constexpr auto documentedModule = R"py(
"""A module docstring
over two lines."""


class Account:
    """A class docstring."""

    def deposit(self, amount):
        """Add amount.

        Returns the balance.
        """
        if amount > 0:
            return amount
        return 0
)py";

constexpr auto undocumentedModule = R"py(
class Account:
    def deposit(self, amount):
        if amount > 0:
            return amount
        return 0
)py";

constexpr auto testCaseAndHelpers = R"py(
import unittest


def make_value():
    return 1


def unrelated():
    return 2


class ValueTests(unittest.TestCase):
    def test_first(self):
        self.assertEqual(make_value(), 1)

    def test_second(self):
        self.assertEqual(3, 3)

    def test_third(self):
        self.assertTrue(True)
)py";

} // namespace

auto tPythonExceptionComplexity = test(
    "PythonRules/each except and raise is a path; with, x if c else y, a comprehension's if and match cases are not") =
    []
{
    auto handle = findFunction(measureFixture(exceptionsAndRaise, "a.py"), "handle");
    // 1 + two excepts + raise + comprehension for
    check(handle.cyclomaticComplexity == 5);
};

auto tPythonNesting =
    test("PythonRules/try and with do not nest; loops and conditionals do") = []
{
    auto nested =
        findFunction(measureFixture(tryAndWithAroundIfs, "a.py"), "nested");
    check(nested.maximumNestingDepth == 3);
};

auto tPythonBumps = test(
    "PythonRules/a bump is a run of nested code; a top-level or shallow plain statement, or the next branch of a shallow if, ends it") =
    []
{
    auto file = measureFixture(bumpShapes, "a.py");
    check(findFunction(file, "two_ifs_in_one_loop").bumpCount == 1);
    check(findFunction(file, "two_nested_chunks_in_one_loop").bumpCount == 2);
    check(findFunction(file, "two_loops_each_holding_an_if").bumpCount == 2);
    check(findFunction(file, "branches_each_nesting").bumpCount == 2);
};

auto tPythonDocstrings = test(
    "PythonRules/module, class and function docstrings are not code: documented and undocumented score the same") =
    []
{
    auto documented = measureFixture(documentedModule, "a.py");
    auto undocumented = measureFixture(undocumentedModule, "a.py");

    check(documented.linesOfCode == undocumented.linesOfCode);
    check(findFunction(documented, "deposit").length
          == findFunction(undocumented, "deposit").length);
    check(assessFileHealth(documented).score
          == assessFileHealth(undocumented).score);
};

auto tPythonModuleCohesion = test(
    "PythonRules/module cohesion groups functions by calls and shared fields; calling self.method shares nothing") =
    []
{
    auto file = measureFixture(testCaseAndHelpers, "a.py");
    // {make_value, test_first}, {unrelated}, {test_second}, {test_third}
    check(file.moduleCohesionGroups == 4);
    check(file.classes.front().cohesionGroups == 3);
};

auto tCppHasNoModuleCohesion =
    test("PythonRules/other languages do not measure module cohesion") = []
{
    auto file =
        measureFixture("int f() { return 1; }\nint g() { return 2; }\n", "a.cpp");
    check(file.moduleCohesionGroups == 0);
};
