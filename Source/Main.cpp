#include "Report.h"

#include <eacp/Core/App/App.h>
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
        CodeHealth::measureRepository(command.analysis), rules);
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
        eacp::Console::print(
            Miro::toJSONString(measureRepository(command.analysis), jsonIndent));
        return 0;
    }

    auto analysis = analyseRepository(command.analysis, rules);

    if (command.printJson)
        eacp::Console::print(formatJson(analysis));
    else
        eacp::Console::print(formatHumanSummary(analysis, command.entryCount));

    return 0;
}

int runCodeHealth()
{
    using namespace eacp;

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
