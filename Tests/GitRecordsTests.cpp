#include "FixtureRepository.h"

#include <GitRecords.h>
#include <Repository.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace CodeHealth;
using namespace CodeHealth::Testing;

namespace
{
constexpr auto firstCommitTime = 1700000000LL;
constexpr auto secondCommitTime = 1700000600LL;
const auto awkwardPath = std::string {"src/we\"ird\ttab ü.cpp"};
const auto awkwardAuthor = std::string {"Jö \"Q\"\tTab"};
const auto awkwardSource = std::string {"int f(int a) { return a ? 1 : 0; }\n"};

void makeAwkwardHistory(FixtureRepository& repository)
{
    repository.stageFile(awkwardPath, "int f(int a) { return a; }\n");
    repository.stageFile("plain.cpp", "int g() { return 0; }\n");
    repository.commit("First", firstCommitTime);
    repository.stageFile(awkwardPath, awkwardSource);
    repository.stageFile("plain.cpp", "int g() { return 1; }\n");
    repository.commit(awkwardAuthor, secondCommitTime);
}
} // namespace

auto tFormatFromFieldOrder =
    test("GitRecords/the format string follows the struct's field order") = []
{
    check(GitRecords::makeFormat<GitRecords::LogRecord>("%x1f")
          == "%P%x1f%an%x1f%at");
};

auto tRecordWithWrongShape =
    test("GitRecords/a line that is not a record reads as nothing") = []
{
    check(!GitRecords::readRecord<GitRecords::BlobHeader>("HEAD:nope missing"));
    check(!GitRecords::readRecord<GitRecords::BlobHeader>("abc\x1f"
                                                          "blob\x1fnot-a-number"));
};

auto tLogRoundTrip = test(
    "GitRecords/commits round-trip names and paths with quotes, tabs and unicode") =
    []
{
    auto repository = FixtureRepository {};
    makeAwkwardHistory(repository);

    auto commits = readCommits(repository.getPath(), "HEAD", {});
    check(commits.size() == 1);
    check(commits.front().log.authorName == awkwardAuthor);
    check(commits.front().log.authorTime == secondCommitTime);
    check(commits.front().paths
          == std::vector<std::string> {"plain.cpp", awkwardPath});
};

auto tLogStreamsInPieces =
    test("GitRecords/a log fed a byte at a time parses as when read whole") = []
{
    auto repository = FixtureRepository {};
    makeAwkwardHistory(repository);

    auto log = runGit(repository.getPath(),
                      {"log", "-z", "--name-only", makeCommitLogFormat()});
    auto parser = CommitLogParser {};

    for (auto character: log)
        parser.addText(std::string_view {&character, 1});

    auto streamed = parser.finish();
    check(streamed.size() == 1);
    check(streamed.front().paths
          == readCommits(repository.getPath(), "HEAD", {}).front().paths);
};

auto tBlobRoundTrip = test(
    "GitRecords/file contents round-trip through cat-file for an awkward path") = []
{
    auto repository = FixtureRepository {};
    makeAwkwardHistory(repository);

    auto metrics = measureRepository({.repository = repository.getPath()});
    auto awkward = std::ranges::find(metrics, awkwardPath, &FileMetrics::path);
    check(awkward != metrics.end());
    check(awkward != metrics.end()
          && awkward->functions.front().cyclomaticComplexity == 2);
};
