#include "Common.h"

using namespace nano;
using namespace CodeHealth;
using namespace CodeHealth::Testing;

namespace
{
constexpr auto twoResponsibilities = R"cpp(
class Account
{
public:
    void deposit(int amount) { balance += amount; log(); }
    int getBalance() const { return balance; }
    void setTitle(const char* text) { title = text; }
    void log();

private:
    int balance = 0;
    const char* title = nullptr;
};

void Account::log() { history += balance; }
)cpp";

constexpr auto oneResponsibility = R"ts(
class Counter {
  count = 0;
  step = 1;
  increment() { this.count += this.step; }
  reset() { this.count = 0; this.increment(); }
  setStep(step: number) { this.step = step; }
}
)ts";
} // namespace

auto tCppCohesionGroups = test(
    "Cohesion/C++ methods split by the fields they share, out-of-line ones included") =
    []
{
    auto file = measureFixture(twoResponsibilities, "account.cpp");
    check(file.classes.size() == 1);
    check(file.classes.front().name == "Account");
    check(file.classes.front().methodCount == 4);
    check(file.classes.front().fieldCount == 2);
    check(file.classes.front().cohesionGroups == 2);
};

auto tTypeScriptCohesionGroups = test(
    "Cohesion/TypeScript methods joined through this.fields and calls are one group") =
    []
{
    auto file = measureFixture(oneResponsibility, "counter.ts");
    check(file.classes.size() == 1);
    check(file.classes.front().methodCount == 3);
    check(file.classes.front().cohesionGroups == 1);
};

auto tGlobalScopeDefinition = test(
    "Cohesion/a definition qualified only by the global scope, int ::f(), is measured") =
    []
{
    auto file = measureFixture("struct A { int x; };\nint ::f() { return 0; }\n",
                               "global.cpp");
    check(file.classes.size() == 1);
    check(file.classes.front().methodCount == 0);
    check(file.functions.size() == 1);
};
