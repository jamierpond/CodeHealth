#pragma once

#include "GitRecords.h"

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace CodeHealth
{
struct Commit
{
    GitRecords::LogRecord log;
    std::vector<std::string> paths;
};

// Reads `git log -z --name-only` with a LogRecord format, whole or a chunk at a time.
class CommitLogParser
{
public:
    void addText(std::string_view text);
    std::vector<Commit> finish();

private:
    void addRecord(std::string_view record);

    std::string pending;
    std::vector<Commit> commits;
};

std::string makeCommitLogFormat();

std::vector<Commit> parseCommitLog(std::string_view log);

struct FileChangeHistory
{
    int commitCount = 0;
};

using ChangeHistory = std::map<std::string, FileChangeHistory>;

ChangeHistory countCommitsPerFile(const std::vector<Commit>& commits);

using OutputHandler = std::function<void(std::string_view)>;

std::string runGit(const eacp::FilePath& repository,
                   const eacp::Vector<std::string>& arguments,
                   const std::string& input = {},
                   const OutputHandler& onOutput = {});

std::vector<Commit> readCommits(const eacp::FilePath& repository,
                                const std::string& ref,
                                const std::string& since);
} // namespace CodeHealth
