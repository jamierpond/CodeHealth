#pragma once

#include <History.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/TemporaryFile.h>

namespace CodeHealth::Testing
{
// A throwaway repository written through git's plumbing, so paths no
// filesystem allows (tabs, quotes on Windows) can still be committed; Git for
// Windows refuses those in the index unless core.protectNTFS is off.
class FixtureRepository
{
public:
    FixtureRepository()
    {
        eacp::Files::createDirectories(getPath());
        runGit(getPath(), {"init", "-q"});
    }

    const eacp::FilePath& getPath() const { return directory.path(); }

    void stageFile(const std::string& path, const std::string& content)
    {
        auto hash = eacp::Strings::trim(
            runGit(getPath(), {"hash-object", "-w", "--stdin"}, content));
        runGit(getPath(),
               {"-c",
                "core.protectNTFS=false",
                "update-index",
                "--add",
                "--cacheinfo",
                "100644," + hash + "," + path});
    }

    void unstageFile(const std::string& path)
    {
        runGit(getPath(), {"update-index", "--force-remove", path});
    }

    void writeWorkingFile(const std::string& path, const std::string& content)
    {
        eacp::Files::writeFile(getPath() / path, content);
    }

    void checkOutHead() { runGit(getPath(), {"reset", "-q", "--hard"}); }

    void createBranch(const std::string& name)
    {
        runGit(getPath(), {"branch", name});
    }

    void commit(const std::string& authorName, long long authorTime)
    {
        runGit(getPath(),
               {"-c",
                "user.name=" + authorName,
                "-c",
                "user.email=fixture@example.com",
                "commit",
                "-q",
                "-m",
                "change",
                "--date=@" + std::to_string(authorTime)});
    }

private:
    eacp::TemporaryFile directory;
};
} // namespace CodeHealth::Testing
