#include "Common.h"

using namespace nano;
using namespace CodeHealth;
using namespace CodeHealth::Testing;

namespace
{
constexpr auto pythonFixture = R"py(
import os
from collections import OrderedDict


class Account:
    def __init__(self, owner: str, *args, **kwargs):
        self.owner = owner
        self.balance = 0

    def deposit(self, amount: int):
        if amount > 0 and self.owner or amount == -1:
            self.balance += amount
        elif amount < -10:
            for step in range(3):
                while step:
                    step -= 1
        total = [x for x in range(amount) if x % 2]
        pick = lambda v: v if v else 0
        return self.balance

    @property
    def title(self):
        return self.owner


async def fetch(url, *, timeout=3):
    def retry():
        try:
            return 1
        except ValueError:
            return 2
    return retry()
)py";
} // namespace

auto tPythonFunctions = test(
    "PythonMetrics/methods, async functions and nested defs; lambdas fold in") = []
{
    auto file = measureFixture(pythonFixture, "account.py");
    check(file.language == "Python");
    check(file.functions.size() == 5);
    check(findFunction(file, "fetch.retry").cyclomaticComplexity == 2);
};

auto tPythonComplexity = test(
    "PythonMetrics/branches, comprehensions, boolean operators and conditional expressions") =
    []
{
    auto deposit =
        findFunction(measureFixture(pythonFixture, "account.py"), "deposit");
    // if, and, or, elif, for, while, comprehension for and if, conditional expression
    check(deposit.cyclomaticComplexity == 10);
    check(deposit.complexConditionalCount == 1);
    check(deposit.maximumNestingDepth == 3);
};

auto tPythonArguments = test(
    "PythonMetrics/self and the * marker are not arguments; *args and **kwargs are one each") =
    []
{
    auto file = measureFixture(pythonFixture, "account.py");
    check(findFunction(file, "__init__").argumentCount == 3);
    check(findFunction(file, "__init__").hasPrimitiveArgument);
    check(findFunction(file, "fetch").argumentCount == 2);
};

auto tPythonClasses = test(
    "PythonMetrics/self attributes are a class's fields, decorated methods included") =
    []
{
    auto file = measureFixture(pythonFixture, "account.py");
    check(file.classes.size() == 1);
    check(file.classes.front().methodCount == 3);
    check(file.classes.front().fieldCount == 2);
    check(file.classes.front().cohesionGroups == 1);
    check(file.dependencies == std::vector<std::string> {"os", "collections"});
};
