#include "Diff.h"
#include "Report.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/App/AppEnvironment.h>
#include <eacp/Core/App/Args.h>
#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Format.h>

#include <algorithm>
#include <optional>
#include <thread>

namespace
{
constexpr auto defaultEntryCount = 15;
constexpr auto jsonIndent = 2;

struct CommandLine
{
    CodeHealth::AnalysisOptions analysis {.repository = eacp::FilePath {"."}};
    bool printJson = false;
    bool printMetrics = false;
    bool printRules = false;
    eacp::FilePath rulesPath;
    eacp::FilePath metricsToRescore;
    int entryCount = defaultEntryCount;
    std::optional<double> minimumScore;
};

template <typename T>
T readJsonFile(const eacp::FilePath& path)
{
    auto value = T {};
    Miro::fromJSONString(value, eacp::Files::readFile(path));
    return value;
}

CodeHealth::ScoringRules loadScoringRules(const eacp::FilePath& path)
{
    if (path.str().empty())
        return CodeHealth::getDefaultScoringRules();

    return readJsonFile<CodeHealth::ScoringRules>(path);
}

// Scores the files alone, with no history, and fails when any is below minimum.
int checkMinimumScore(const CommandLine& command,
                      const CodeHealth::ScoringRules& rules,
                      double minimum)
{
    auto files = CodeHealth::assessFiles(
        CodeHealth::measureRepository(command.analysis).files, rules);
    auto failing = std::ranges::count_if(
        files, [&](auto& file) { return file.score < minimum; });

    for (auto& file: files)
        if (file.score < minimum)
            eacp::Console::printError(
                eacp::Strings::format("{:5.2f}  {}", file.score, file.path));

    return failing == 0 ? 0 : 1;
}

int runCommand(const CommandLine& command)
{
    using namespace CodeHealth;

    auto rules = loadScoringRules(command.rulesPath);

    if (command.printRules)
    {
        eacp::Console::print(Miro::toJSONString(rules, jsonIndent));
        return 0;
    }

    if (command.minimumScore)
        return checkMinimumScore(command, rules, *command.minimumScore);

    if (!command.metricsToRescore.str().empty())
    {
        auto metrics =
            readJsonFile<std::vector<FileMetrics>>(command.metricsToRescore);
        eacp::Console::print(
            Miro::toJSONString(assessFiles(metrics, rules), jsonIndent));
        return 0;
    }

    if (command.printMetrics)
    {
        eacp::Console::print(Miro::toJSONString(
            measureRepository(command.analysis).files, jsonIndent));
        return 0;
    }

    auto analysis = analyseRepository(command.analysis, rules);

    if (command.printJson)
        eacp::Console::print(formatJson(analysis));
    else
        eacp::Console::print(formatHumanSummary(analysis, command.entryCount));

    return 0;
}

struct DiffCommandLine
{
    CodeHealth::DiffOptions diff {.repository = eacp::FilePath {"."}};
    bool printJson = false;
    bool failOnRegression = false;
};

int runDiff(const eacp::Vector<std::string>& arguments)
{
    using namespace eacp;

    auto command = DiffCommandLine {};
    auto cli = Args::Parser {"codehealth diff",
                             "Code Health of the source files changed since a base: "
                             "merge base against the working tree."};
    cli.option("--repository", command.diff.repository, "the repository to look at");
    cli.option(
        "--since", command.diff.since, "history read for files near the changes");
    cli.flag("--json", command.printJson, "print the whole report as JSON");
    cli.flag("--check",
             command.failOnRegression,
             "exit 1 if a changed file's score went down or a new finding appeared");
    cli.positional(command.diff.base,
                   "base (default: origin's default branch, else HEAD)");

    if (auto code = cli.exitCodeFor(cli.parse(arguments)))
        return *code;

    auto report = CodeHealth::analyseChanges(command.diff,
                                             CodeHealth::getDefaultScoringRules());

    if (command.printJson)
        Console::print(Miro::toJSONString(report, jsonIndent));
    else
        Console::print(CodeHealth::formatDiffSummary(report));

    return command.failOnRegression && report.hasRegression() ? 1 : 0;
}

int runCodeHealth()
{
    using namespace eacp;

    auto& arguments = Apps::commandLineArgs();

    if (arguments.size() > 1 && arguments[1] == "diff")
    {
        auto diffArguments = eacp::Vector<std::string> {};

        for (auto index = 2; index < arguments.size(); ++index)
            diffArguments.add(arguments[index]);

        return runDiff(diffArguments);
    }

    auto command = CommandLine {};
    command.analysis.jobCount =
        static_cast<int>(std::thread::hardware_concurrency());
    auto cli = Args::Parser {
        "codehealth", "Code Health scores and hotspots for a git repository."};
    cli.option("--ref", command.analysis.ref, "the commit to analyse");
    cli.option(
        "--since", command.analysis.since, "only count commits after this date");
    cli.option(
           "--jobs", command.analysis.jobCount, "how many files to parse at once")
        .atLeast(1);
    cli.option("--top", command.entryCount, "how many files and hotspots to list")
        .atLeast(1);
    cli.flag("--json", command.printJson, "print the whole analysis as JSON");
    cli.flag("--metrics",
             command.printMetrics,
             "print every file's raw measurements as JSON, unscored");
    cli.flag("--print-rules",
             command.printRules,
             "print the scoring rules in use as JSON");
    cli.option("--rules",
               command.rulesPath,
               "scoring rules JSON to use instead of the built-in ones");
    cli.option("--rescore",
               command.metricsToRescore,
               "score a --metrics JSON file instead of a repository");
    cli.option("--check",
               command.minimumScore,
               "score the files without history and fail if any is below this");
    cli.positional(command.analysis.repository, "repository");

    if (auto code = cli.exitCodeFor(cli.parse()))
        return *code;

    return runCommand(command);
}
} // namespace

int main(int argc, char* argv[])
{
    return eacp::Apps::runConsole(argc, argv, runCodeHealth);
}
