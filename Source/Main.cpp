#include "CommandNames.h"
#include "Diff.h"
#include "GitHubReport.h"
#include "DuplicateView.h"
#include "Report.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/App/AppEnvironment.h>
#include <eacp/Core/App/Args.h>
#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Format.h>

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <thread>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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
    bool printVersion = false;
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
    std::string format = "text";
    std::string failOn;
    eacp::FilePath summaryPath;
};

void printDiffReport(const DiffCommandLine& command,
                     const CodeHealth::DiffReport& report)
{
    if (command.printJson || command.format == "json")
        eacp::Console::print(Miro::toJSONString(report, jsonIndent));
    else if (command.format == "github")
        eacp::Console::print(CodeHealth::formatGitHubAnnotations(report));
    else
        eacp::Console::print(CodeHealth::formatDiffSummary(report));
}

void writeStepSummary(const DiffCommandLine& command,
                      const CodeHealth::DiffReport& report)
{
    if (command.summaryPath.empty())
        return;

    eacp::Files::appendFile(command.summaryPath,
                            CodeHealth::formatGitHubStepSummary(report));
}

std::optional<CodeHealth::FailureRule>
    chooseFailureRule(const DiffCommandLine& command)
{
    if (!command.failOn.empty())
        return CodeHealth::findFailureRule(command.failOn);

    if (command.failOnRegression)
        return CodeHealth::FailureRule::scoreDrop;

    return CodeHealth::FailureRule::never;
}

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
    cli.option("--format",
               command.format,
               "text, json, or github (workflow annotations on each new or "
               "worsened finding)");
    cli.option("--summary",
               command.summaryPath,
               "append a Markdown table of score changes to this file "
               "(a GitHub step summary)");
    cli.flag("--check",
             command.failOnRegression,
             "exit 1 if a changed file's score went down or a new finding appeared");
    cli.option("--fail-on",
               command.failOn,
               "score-drop, new-finding or never: what exits 1 (--check is "
               "score-drop)");
    cli.positional(command.diff.base,
                   "base (default: origin's default branch, else HEAD)");

    if (auto code = cli.exitCodeFor(cli.parse(arguments)))
        return *code;

    auto rule = chooseFailureRule(command);

    if (!rule)
    {
        Console::printError("--fail-on takes score-drop, new-finding or never");
        return 2;
    }

    auto report = CodeHealth::analyseChanges(command.diff,
                                             CodeHealth::getDefaultScoringRules());
    printDiffReport(command, report);
    writeStepSummary(command, report);

    auto failure = CodeHealth::findGateFailure(report, *rule);

    if (!failure)
        return 0;

    if (command.format == "github")
        Console::print(CodeHealth::formatGitHubFailure(*failure));
    else
        Console::printError(*failure);

    return 1;
}

bool isStandardOutputTerminal()
{
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

CodeHealth::OutputColouring chooseOutputColouring()
{
    if (isStandardOutputTerminal())
        return CodeHealth::OutputColouring::terminalColours;

    return CodeHealth::OutputColouring::plain;
}

int showDuplicatedPair(const CodeHealth::AnalysisOptions& options, int pairNumber)
{
    using namespace eacp;

    auto clones = CodeHealth::findReportedFunctionClones(options);
    auto pairCount = static_cast<int>(clones.size());

    if (pairNumber < 1 || pairNumber > pairCount)
    {
        Console::printError(
            Strings::format("no duplicated pair {}; valid numbers are 1 to {}",
                            pairNumber,
                            pairCount));
        return 1;
    }

    auto& clone = clones[static_cast<size_t>(pairNumber - 1)];
    auto sources = CodeHealth::readFilesAtRef(
        options.repository, options.ref, {clone.firstPath, clone.secondPath});
    Console::print(CodeHealth::formatFunctionCloneDiff(
        clone, {sources[0], sources[1]}, chooseOutputColouring()));
    return 0;
}

using NumberedItemViewer = int (*)(const CodeHealth::AnalysisOptions&, int);

struct NumberedItemSection
{
    std::string_view sectionName;
    NumberedItemViewer showItem;
};

constexpr auto numberedItemSections = std::array {
    NumberedItemSection {"duplicates", showDuplicatedPair},
};

std::optional<NumberedItemViewer>
    findNumberedItemViewer(std::string_view sectionName)
{
    auto match = std::ranges::find(
        numberedItemSections, sectionName, &NumberedItemSection::sectionName);

    if (match == numberedItemSections.end())
        return std::nullopt;

    return match->showItem;
}

struct SectionCommandLine
{
    CodeHealth::AnalysisOptions analysis {.repository = eacp::FilePath {"."}};
    eacp::FilePath rulesPath;
    int entryCount = defaultEntryCount;
    std::optional<int> itemNumber;
};

int runReportSection(const CodeHealth::ReportSection& section,
                     const eacp::Vector<std::string>& arguments)
{
    using namespace eacp;

    auto command = SectionCommandLine {};
    command.analysis.jobCount =
        static_cast<int>(std::thread::hardware_concurrency());
    auto viewItem = findNumberedItemViewer(section.name);
    auto programName = "codehealth " + std::string {section.name};
    auto cli = Args::Parser {programName,
                             "Only the " + std::string {section.name}
                                 + " section of the report."};
    cli.option(
        "--repository", command.analysis.repository, "the repository to look at");
    cli.option("--ref", command.analysis.ref, "the commit to analyse");
    cli.option(
        "--since", command.analysis.since, "only count commits after this date");
    cli.option(
           "--jobs", command.analysis.jobCount, "how many files to parse at once")
        .atLeast(1);
    cli.option("--top", command.entryCount, "how many entries to list").atLeast(1);
    cli.option("--rules",
               command.rulesPath,
               "scoring rules JSON to use instead of the built-in ones");

    if (viewItem)
        cli.positional(
            command.itemNumber, "number", "show only this numbered entry");

    if (auto code = cli.exitCodeFor(cli.parse(arguments)))
        return *code;

    if (command.itemNumber)
        return (*viewItem)(command.analysis, *command.itemNumber);

    auto analysis = CodeHealth::analyseRepository(
        command.analysis, loadScoringRules(command.rulesPath));
    Console::print(
        CodeHealth::formatReportSection(section, analysis, command.entryCount));
    return 0;
}

eacp::Vector<std::string> collectArgumentsAfter(int firstIndex)
{
    auto& arguments = eacp::Apps::commandLineArgs();
    auto remaining = eacp::Vector<std::string> {};

    for (auto index = firstIndex; index < arguments.size(); ++index)
        remaining.add(arguments[index]);

    return remaining;
}

using SubcommandRunner = std::function<int(const eacp::Vector<std::string>&)>;

struct Subcommand
{
    std::string_view name;
    SubcommandRunner run;
};

std::vector<Subcommand> makeSubcommands()
{
    auto subcommands = std::vector<Subcommand> {{"diff", runDiff}};

    for (auto& section: CodeHealth::getReportSections())
        subcommands.push_back({section.name,
                               [&section](auto& arguments)
                               { return runReportSection(section, arguments); }});

    return subcommands;
}

std::vector<std::string_view>
    getSubcommandNames(const std::vector<Subcommand>& subcommands)
{
    auto names = std::vector<std::string_view> {};

    for (auto& subcommand: subcommands)
        names.push_back(subcommand.name);

    return names;
}

std::string joinNames(const std::vector<std::string_view>& names)
{
    auto joined = std::string {};

    for (auto name: names)
        joined += (joined.empty() ? "" : ", ") + std::string {name};

    return joined;
}

std::optional<int> runMatchingSubcommand()
{
    auto& arguments = eacp::Apps::commandLineArgs();

    if (arguments.size() < 2)
        return std::nullopt;

    auto subcommands = makeSubcommands();
    auto typedWord = std::string_view {arguments[1]};
    auto matches = CodeHealth::findNamesMatchingPrefix(
        getSubcommandNames(subcommands), typedWord);

    if (matches.empty())
        return std::nullopt;

    if (matches.size() > 1)
    {
        eacp::Console::printError("'" + std::string {typedWord}
                                  + "' could mean: " + joinNames(matches));
        return 1;
    }

    auto& chosen =
        *std::ranges::find(subcommands, matches.front(), &Subcommand::name);
    return chosen.run(collectArgumentsAfter(2));
}

int runCodeHealth()
{
    using namespace eacp;

    if (auto code = runMatchingSubcommand())
        return *code;

    auto command = CommandLine {};
    command.analysis.jobCount =
        static_cast<int>(std::thread::hardware_concurrency());
    auto cli = Args::Parser {
        "codehealth",
        "Code Health scores and hotspots for a git repository. Subcommands, "
        "any unambiguous prefix works: diff [base]; health, hotspots, coupling, "
        "duplicates [number] print one section of the report."};
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

    cli.flag("--version", command.printVersion, "print codehealth's version");

    if (auto code = cli.exitCodeFor(cli.parse()))
        return *code;

    if (command.printVersion)
    {
        eacp::Console::print(std::string {"codehealth "} + CODEHEALTH_VERSION);
        return 0;
    }

    return runCommand(command);
}
} // namespace

int main(int argc, char* argv[])
{
    return eacp::Apps::runConsole(argc, argv, runCodeHealth);
}
