#include "Repository.h"
#include "History.h"
#include "Languages.h"

#include <eacp/Core/Threads/Thread.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <exception>
#include <set>
#include <stdexcept>
#include <tuple>

namespace CodeHealth
{
namespace
{
constexpr auto filesPerRead = size_t {500};
constexpr auto bytesPerRead = 16LL * 1024 * 1024;
constexpr auto queuedFileLimit = size_t {2000};

const std::set<std::string_view> excludedDirectoryNames {"node_modules",
                                                         "generated",
                                                         "thirdparty",
                                                         "third_party",
                                                         "third-party",
                                                         "vendor",
                                                         "vendored",
                                                         "external",
                                                         "externals",
                                                         "_deps",
                                                         "dist",
                                                         "out",
                                                         "build"};

const std::vector<std::string_view> excludedDirectoryPrefixes {
    "build-", "build_", "cmake-build"};

const std::vector<std::string_view> excludedFileSuffixes {".min.js", ".d.ts"};

bool isExcludedDirectory(const std::string& directoryName)
{
    auto name = eacp::Strings::toLower(directoryName);

    if (excludedDirectoryNames.contains(name))
        return true;

    return std::ranges::any_of(excludedDirectoryPrefixes,
                               [&](auto prefix)
                               { return name.starts_with(prefix); });
}

struct SourceFile
{
    std::string path;
    long long size = 0;
};

// `git ls-tree -l` gives "<mode> <type> <hash> <size>\t<path>" per entry.
std::vector<SourceFile> listSourceFiles(const eacp::FilePath& repository,
                                        const std::string& ref)
{
    auto listing = runGit(repository, {"ls-tree", "-r", "-z", "-l", ref});
    auto files = std::vector<SourceFile> {};

    for (auto& entry: eacp::Strings::split(listing, '\0'))
    {
        auto parts = eacp::Strings::split(entry, '\t', 2);

        if (parts.size() != 2 || !findLanguageForPath(parts[1])
            || isExcludedPath(parts[1]))
            continue;

        auto size = eacp::Strings::trim(parts[0].substr(parts[0].rfind(' ') + 1));
        files.push_back(
            {.path = parts[1], .size = eacp::Strings::parseOr<long long>(size, 0)});
    }

    return files;
}

struct FileToParse
{
    size_t index = 0;
    std::string content;
};

// One reader thread runs `git cat-file` over batches of files, largest first, and
// parser workers take files one at a time off a bounded queue, so a huge file
// starts early instead of holding up the end. There is one reader rather than one per
// worker because eacp's Process leaves its pipes inheritable: a child started
// while another `git cat-file` is running keeps that one's input open, and the
// two wait on each other for end of input.
class ParallelMeasurer
{
public:
    ParallelMeasurer(const AnalysisOptions& optionsToUse,
                     std::vector<SourceFile> filesToMeasure)
        : options(optionsToUse)
        , files(std::move(filesToMeasure))
        , results(files.size())
        , functionTokens(files.size())
    {
        for (auto index = size_t {0}; index < files.size(); ++index)
            largestFirst.push_back(index);

        std::ranges::stable_sort(largestFirst,
                                 std::ranges::greater {},
                                 [&](size_t index) { return files[index].size; });
    }

    RepositoryMeasurement measure()
    {
        auto workerCount = static_cast<size_t>(std::max(1, options.jobCount));
        auto failures = std::vector<std::exception_ptr>(workerCount + 1);
        auto threads = std::vector<eacp::Threads::Thread> {};

        threads.emplace_back(
            [this, &failures]
            { runCatchingFailure(failures.front(), [this] { readAll(); }); });

        for (auto worker = size_t {1}; worker <= workerCount; ++worker)
            threads.emplace_back(
                [this, &failures, worker]
                { runCatchingFailure(failures[worker], [this] { parseFiles(); }); });

        for (auto& thread: threads)
            thread.join();

        for (auto& failure: failures)
            if (failure)
                std::rethrow_exception(failure);

        return addDuplication();
    }

private:
    RepositoryMeasurement addDuplication()
    {
        auto paths = std::vector<std::string> {};

        for (auto& file: files)
            paths.push_back(file.path);

        auto functionClones =
            findFunctionClones(functionTokens, paths, options.jobCount);
        auto duplicatedLines = countDuplicatedLines(functionClones, paths);
        auto duplicatedFunctions = countDuplicatedFunctions(functionClones, paths);

        for (auto index = size_t {0}; index < results.size(); ++index)
        {
            results[index].duplicatedLines = duplicatedLines[index];
            results[index].duplicatedFunctionCount = duplicatedFunctions[index];
        }

        return {.files = std::move(results),
                .functionClones = std::move(functionClones)};
    }

    void runCatchingFailure(std::exception_ptr& failure,
                            const std::function<void()>& work)
    {
        try
        {
            work();
        }
        catch (...)
        {
            failure = std::current_exception();
            closeQueue();
        }
    }

    void readAll()
    {
        for (auto batch = takeReadBatch(); !batch.empty(); batch = takeReadBatch())
        {
            auto paths = std::vector<std::string> {};

            for (auto index: batch)
                paths.push_back(files[index].path);

            auto contents = readFilesAtRef(options.repository, options.ref, paths);

            for (auto position = size_t {0}; position < batch.size(); ++position)
                if (!pushFile({batch[position], std::move(contents[position])}))
                    return;
        }

        closeQueue();
    }

    std::vector<size_t> takeReadBatch()
    {
        auto batch = std::vector<size_t> {};
        auto batchBytes = 0LL;

        while (batch.size() < filesPerRead && batchBytes < bytesPerRead
               && nextFile < largestFirst.size())
        {
            auto index = largestFirst[nextFile++];
            batch.push_back(index);
            batchBytes += files[index].size;
        }

        return batch;
    }

    void parseFiles()
    {
        auto parser = SourceParser {};

        for (auto file = popFile(); file; file = popFile())
        {
            auto& path = files[file->index].path;
            auto& language = findLanguageForPath(path)->get();
            auto measured = parser.measure(file->content, path, language);
            results[file->index] = std::move(measured.metrics);
            functionTokens[file->index] = std::move(measured.functionTokens);
        }
    }

    bool pushFile(FileToParse file)
    {
        auto lock = std::unique_lock {queueMutex};
        queueChanged.wait(
            lock, [this] { return isClosed || queue.size() < queuedFileLimit; });

        if (isClosed)
            return false;

        queue.push_back(std::move(file));
        queueChanged.notify_all();
        return true;
    }

    std::optional<FileToParse> popFile()
    {
        auto lock = std::unique_lock {queueMutex};
        queueChanged.wait(lock, [this] { return isClosed || !queue.empty(); });

        if (queue.empty())
            return std::nullopt;

        auto file = std::move(queue.front());
        queue.pop_front();
        queueChanged.notify_all();
        return file;
    }

    void closeQueue()
    {
        auto lock = std::scoped_lock {queueMutex};
        isClosed = true;
        queueChanged.notify_all();
    }

    const AnalysisOptions& options;
    std::vector<SourceFile> files;
    std::vector<size_t> largestFirst;
    std::vector<FileMetrics> results;
    std::vector<std::vector<FunctionTokens>> functionTokens;
    size_t nextFile = 0;

    std::mutex queueMutex;
    std::condition_variable queueChanged;
    std::deque<FileToParse> queue;
    bool isClosed = false;
};

std::vector<Hotspot> rankHotspots(const std::vector<FileHealth>& files,
                                  const ChangeHistory& history)
{
    auto hotspots = std::vector<Hotspot> {};

    for (auto& file: files)
    {
        auto change = history.find(file.path);

        if (change == history.end())
            continue;

        auto commits = change->second.commitCount;
        hotspots.push_back({.path = file.path,
                            .commitCount = commits,
                            .totalComplexity = file.totalComplexity,
                            .healthScore = file.score,
                            .hotspotScore = static_cast<long long>(commits)
                                            * file.totalComplexity});
    }

    std::ranges::stable_sort(
        hotspots, std::ranges::greater {}, &Hotspot::hotspotScore);
    return hotspots;
}
} // namespace

// Reads files' contents at ref through one `git cat-file --batch`: a BlobHeader
// line and the bytes per file, or "<name> missing".
std::vector<std::string> readFilesAtRef(const eacp::FilePath& repository,
                                        const std::string& ref,
                                        const std::vector<std::string>& paths)
{
    auto requests = std::string {};

    for (auto& path: paths)
        requests += ref + ":" + path + "\n";

    auto format = GitRecords::makeFormat<GitRecords::BlobHeader>(
        std::string(1, GitRecords::fieldSeparator));
    auto batch = runGit(repository, {"cat-file", "--batch=" + format}, requests);
    auto contents = std::vector<std::string> {};
    auto remaining = std::string_view {batch};

    while (contents.size() < paths.size() && !remaining.empty())
    {
        auto headerEnd = remaining.find('\n');
        auto header = GitRecords::readRecord<GitRecords::BlobHeader>(
            remaining.substr(0, headerEnd));
        remaining = remaining.substr(headerEnd + 1);
        auto size = header ? static_cast<size_t>(header->objectSize) : size_t {0};
        contents.emplace_back(remaining.substr(0, size));
        remaining = remaining.substr(header ? size + 1 : 0);
    }

    contents.resize(paths.size());
    return contents;
}

bool isExcludedPath(std::string_view path)
{
    if (std::ranges::any_of(excludedFileSuffixes,
                            [&](auto suffix) { return path.ends_with(suffix); }))
        return true;

    auto components = eacp::Strings::split(path, '/');
    components.pop_back();
    return std::ranges::any_of(components, isExcludedDirectory);
}

RepositoryMeasurement measureRepository(const AnalysisOptions& options)
{
    return ParallelMeasurer {options,
                             listSourceFiles(options.repository, options.ref)}
        .measure();
}

std::vector<FileHealth> assessFiles(const std::vector<FileMetrics>& metrics,
                                    const ScoringRules& rules)
{
    auto files = std::vector<FileHealth> {};

    for (auto& file: metrics)
        files.push_back(assessFileHealth(file, rules));

    std::ranges::stable_sort(files, std::ranges::less {}, &FileHealth::score);
    return files;
}

bool isListedBefore(const FunctionClone& first, const FunctionClone& second)
{
    auto measureSize = [](const FunctionClone& clone)
    { return clone.similarity * clone.tokenCount; };
    auto makePlacement = [](const FunctionClone& clone)
    {
        return std::tie(clone.firstPath,
                        clone.firstStartLine,
                        clone.secondPath,
                        clone.secondStartLine);
    };

    if (measureSize(first) != measureSize(second))
        return measureSize(first) > measureSize(second);

    return makePlacement(first) < makePlacement(second);
}

// Every clone counts toward its files' scores; the report lists the largest.
std::vector<FunctionClone> keepLargestClones(std::vector<FunctionClone> clones)
{
    constexpr auto reportedCloneLimit = size_t {200};
    std::ranges::sort(clones, isListedBefore);
    clones.resize(std::min(clones.size(), reportedCloneLimit));
    return clones;
}

std::vector<FunctionClone> findReportedFunctionClones(const AnalysisOptions& options)
{
    return keepLargestClones(measureRepository(options).functionClones);
}

// History is read on its own thread while the workers parse.
Analysis analyseRepository(const AnalysisOptions& options, const ScoringRules& rules)
{
    auto commits = std::vector<Commit> {};
    auto historyFailure = std::exception_ptr {};
    auto historyReader = eacp::Threads::Thread {
        [&]
        {
            try
            {
                commits =
                    readCommits(options.repository, options.ref, options.since);
            }
            catch (...)
            {
                historyFailure = std::current_exception();
            }
        }};

    auto measurement = measureRepository(options);
    auto& metrics = measurement.files;
    historyReader.join();

    if (historyFailure)
        std::rethrow_exception(historyFailure);

    auto analysedPaths = std::vector<std::string> {};

    for (auto& file: metrics)
        analysedPaths.push_back(file.path);

    auto analysis = Analysis {.repository = options.repository.str(),
                              .revision = options.ref,
                              .since = options.since,
                              .files = assessFiles(metrics, rules)};
    analysis.hotspots = rankHotspots(analysis.files, countCommitsPerFile(commits));
    analysis.couplings = findChangeCoupling(commits, analysedPaths);
    analysis.functionClones =
        keepLargestClones(std::move(measurement.functionClones));
    return analysis;
}
} // namespace CodeHealth
