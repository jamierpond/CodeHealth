#include "History.h"

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>

#include <memory>
#include <mutex>
#include <stdexcept>

namespace CodeHealth
{
namespace
{
// A shallow clone's boundary commits have no parents here, so git would list
// the whole tree for each one. Excluding them with "^<hash>" keeps that out of
// the log altogether.
eacp::Vector<std::string>
    listShallowBoundaryExclusions(const eacp::FilePath& repository)
{
    auto relativePath = eacp::Strings::trim(
        runGit(repository, {"rev-parse", "--git-path", "shallow"}));
    auto path = eacp::FilePath {relativePath};
    auto shallowFile = path.isAbsolute() ? path : repository / relativePath;

    if (!eacp::Files::exists(shallowFile))
        return {};

    auto exclusions = eacp::Vector<std::string> {};

    for (auto& hash: eacp::Strings::split(eacp::Files::readFile(shallowFile), '\n'))
        if (!hash.empty())
            exclusions.add("^" + hash);

    return exclusions;
}
// eacp's Process leaves its pipes inheritable, so two children started at once
// can each inherit the other's pipes. Starting one at a time means a child
// only ever holds pipes of children started before it, never the reverse.
std::unique_ptr<eacp::Processes::Process>
    startProcessAlone(eacp::Processes::ProcessOptions options)
{
    static auto launchMutex = std::mutex {};
    auto lock = std::scoped_lock {launchMutex};
    return std::make_unique<eacp::Processes::Process>(std::move(options));
}
} // namespace

std::string makeCommitLogFormat()
{
    return "--format=%x1e" + GitRecords::makeFormat<GitRecords::LogRecord>("%x1f");
}

void CommitLogParser::addText(std::string_view text)
{
    pending += text;
    auto start = size_t {0};

    for (auto next = pending.find(GitRecords::recordSeparator, start + 1);
         next != std::string::npos;
         next = pending.find(GitRecords::recordSeparator, start + 1))
    {
        addRecord(std::string_view {pending}.substr(start + 1, next - start - 1));
        start = next;
    }

    pending.erase(0, start);
}

std::vector<Commit> CommitLogParser::finish()
{
    if (!pending.empty())
        addRecord(std::string_view {pending}.substr(1));

    pending.clear();
    return std::move(commits);
}

// A record is the LogRecord fields, a NUL, a newline, then NUL-ended paths. A
// commit with no parent lists the whole tree, so it is not counted as a change.
void CommitLogParser::addRecord(std::string_view record)
{
    auto headerEnd = record.find('\0');
    auto log =
        GitRecords::readRecord<GitRecords::LogRecord>(record.substr(0, headerEnd));

    if (!log || log->parentHashes.empty() || headerEnd == std::string_view::npos)
        return;

    auto commit = Commit {.log = std::move(*log)};
    auto paths = record.substr(headerEnd + 1);

    if (paths.starts_with('\n'))
        paths.remove_prefix(1);

    for (auto& path: eacp::Strings::split(paths, '\0'))
        if (!path.empty())
            commit.paths.push_back(path);

    if (!commit.paths.empty())
        commits.push_back(std::move(commit));
}

std::vector<Commit> parseCommitLog(std::string_view log)
{
    auto parser = CommitLogParser {};
    parser.addText(log);
    return parser.finish();
}

ChangeHistory countCommitsPerFile(const std::vector<Commit>& commits)
{
    auto history = ChangeHistory {};

    for (auto& commit: commits)
        for (auto& path: commit.paths)
            ++history[path].commitCount;

    return history;
}

std::string runGit(const eacp::FilePath& repository,
                   const eacp::Vector<std::string>& arguments,
                   const std::string& input,
                   const OutputHandler& onOutput)
{
    // Reads only: no optional locks, so `git diff` does not refresh the index.
    auto gitArguments =
        eacp::Vector<std::string> {"--no-optional-locks", "-C", repository.str()};

    for (auto& argument: arguments)
        gitArguments.add(argument);

    auto process = startProcessAlone({.executable = "git",
                                      .arguments = gitArguments,
                                      .captureOutput = true,
                                      .onOutput = onOutput});

    if (!process->launched())
        throw std::runtime_error {"could not run git; is it on PATH?"};

    process->writeToInput(input);
    process->closeInput();

    if (process->wait() != 0)
        throw std::runtime_error {"git failed: " + process->errorOutput()};

    return process->output();
}

std::vector<Commit> readCommits(const eacp::FilePath& repository,
                                const std::string& ref,
                                const std::string& since)
{
    auto arguments = eacp::Vector<std::string> {"log",
                                                "-z",
                                                "--name-only",
                                                "--no-renames",
                                                makeCommitLogFormat(),
                                                "--ignore-missing",
                                                ref};

    if (!since.empty())
        arguments.add("--since=" + since);

    for (auto& exclusion: listShallowBoundaryExclusions(repository))
        arguments.add(exclusion);

    auto parser = CommitLogParser {};
    runGit(repository,
           arguments,
           {},
           [&](std::string_view chunk) { parser.addText(chunk); });
    return parser.finish();
}
} // namespace CodeHealth
