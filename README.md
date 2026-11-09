# CodeHealth

A command-line tool that scores how maintainable each file in a git repository
is, from 1 to 10, and ranks hotspots: files that change often and are complex.
It reads C, C++, Objective-C, Objective-C++, Python, TypeScript, TSX, JavaScript
and JSX.

## Install

macOS and Linux (x64 or arm64), into `~/.local/bin` (or `$CODEHEALTH_INSTALL_DIR`):

```bash
curl -fsSL https://raw.githubusercontent.com/jamierpond/CodeHealth/main/install.sh | sh
```

Windows (x64 or ARM64), PowerShell, into `~\.local\bin` (added to your user PATH):

```powershell
irm https://raw.githubusercontent.com/jamierpond/CodeHealth/main/install.ps1 | iex
```

Both download the latest release, check its SHA-256 and print the installed
version. `CODEHEALTH_VERSION=v0.1.0` picks a release.

## In a pull request

```yaml
on: pull_request
jobs:
  codehealth:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: jamierpond/CodeHealth@v1
```

The action downloads the released binary for the runner (cached), fetches just
enough history to find the pull request's base, and runs `codehealth diff`. Each
new finding is an error annotation on its file and line, with the number, the
limit and a fix, for example `processOrder: 14 branches, limit 9: split the
decisions into named functions`; a finding that got worse is a warning. The job
summary lists each changed file's score before and after. Inputs:

- `fail-on`: `score-drop` (default: a changed file's score fell or it gained a
  finding), `new-finding`, or `never`.
- `base`: the commit to compare with (default: the pull request's base).
- `version`: the codehealth release to run.

Without the action, after installing:

```yaml
      - run: codehealth diff ${{ github.event.pull_request.base.sha }} --format github --fail-on new-finding
```

(the checkout needs the base commit: `fetch-depth: 0`, or fetch it first).

## Build

Needs CMake 3.25+, a C++20 compiler and git on `PATH`. Dependencies (eacp,
tree-sitter and its grammars, NanoTest) are fetched by CPM. On Linux, eacp's
configure step needs libcurl headers (`libcurl4-gnutls-dev`), though nothing here
links libcurl.

```bash
just build            # Release build into ./build
just test             # build, tests, then the self-check
just run ~/some/repo  # build, then run codehealth (default: this repository)
just install          # build, then install to ~/.local/bin/codehealth
```

The recipes call the scripts in `scripts/`; on Windows they use MSVC.

## Usage

```bash
codehealth path/to/repo                   # worst files, their findings, hotspots, change coupling
codehealth path/to/repo --json            # everything, for scripts and agents
codehealth path/to/repo --ref main --since 2026-01-01
codehealth path/to/repo --jobs 8          # parser threads; defaults to the core count
codehealth path/to/repo --check 9         # exit 1 if any file scores below 9
```

`--check` scores the files without reading history, prints each file under the
minimum to stderr, and exits 1 if there are any. `scripts/self-check.sh` runs it
on this repository's working tree at 9, and CI fails below that.

### One section of the report

```bash
codehealth health        # worst files and their findings
codehealth hotspots      # commits x complexity
codehealth coupling      # files that change together
codehealth duplicates    # numbered duplicated functions
```

Each prints only that section, from the same analysis as the full report, and
takes `--repository`, `--ref`, `--since`, `--jobs`, `--top` and `--rules`. Any
unambiguous prefix works (`dup`, `dupl`, `duplicates`); an ambiguous one (`d`,
`h`) lists what it could mean and exits 1.

### Duplicated functions

The summary numbers each duplicated pair 1, 2, 3... from the largest down.

```bash
codehealth dup 1                                        # pair 1, in the current repository
codehealth dup --repository path/to/repo --ref main 2
```

prints the two functions line by line. The header names function `A` and
function `B`; shared lines have a blank prefix, lines only in A start with `A`
and lines only in B start with `B`. Lines that differ only in indentation count
as shared. When stdout is a terminal, A's lines are blue and B's are orange.

### Changes since a base

```bash
codehealth diff develop        # merge base of develop and HEAD, against the working tree
codehealth diff                # base: origin's default branch, else HEAD
codehealth diff HEAD~1 --json
codehealth diff develop --check   # exit 1 if a changed file's score dropped or gained a finding
codehealth diff main --format github --fail-on new-finding --summary summary.md
```

Uncommitted and untracked files count. For each changed file it prints the score
before and after, and the findings introduced, made worse and fixed, by function
and line. Files whose score and findings did not change are listed under
"Unchanged" with every finding they still have. "Near your changes" lists files you did not touch that usually change
with one you did, and files that include or import one. From eacp:

```
3 changed source files since HEAD (b7e0463e26)

Unchanged
   9.70 ->  9.70  +0.00  M Lib/eacp/Network/HTTP/Http-Android.cpp
      Excess Arguments in nextHop at line 613 (5 >= 5)
   9.62 ->  9.62  +0.00  M Lib/eacp/Network/WebSocket/WebSocket-Android.cpp
      Complex Conditional in send at line 177 (1 >= 1)
      Primitive Obsession (39.2157 >= 30)
      - -> 10.00  +0.00  ? Lib/eacp/Network/JavaStreams-Android.h
```

`--metrics`, `--rescore`, `--rules` and `--print-rules` expose the raw
measurements and the scoring rules for calibration (`scripts/calibration/`).

## How the scores work

The method follows Adam Tornhill's public work: *Your Code as a Crime Scene*
(2015, second edition 2024), *Software Design X-Rays* (2018), and the papers with
Markus Borg, "Code Red: The Business Impact of Code Quality" (TechDebt 2022) and
"Increasing, not Diminishing: Investigating the Returns of Highly Maintainable
Code" (TechDebt 2024).

- **Per function:** cyclomatic complexity, lines of code (blank lines and comments
  left out), nesting depth, bumpy road (separate blocks of nested conditionals in
  one function), complex conditionals (two or more `&&`, `||` or `??` in one
  condition), argument count, and whether any argument is a primitive type.
- **Per file:** lines of code, mean complexity, and the share of functions taking
  primitive arguments.
- Each finding above its threshold costs points, growing with how far over it is;
  each kind of finding saturates at a cap, so one problem cannot sink a file alone.
  Thresholds are CodeScene's published defaults. The weights and caps are fitted
  against CodeScene's scores on public repositories (eacp, Miro, MakeASound,
  linux), not taken from CodeScene.
- **Hotspots:** commits touching the file times its total complexity.
- **Change coupling:** pairs of files changed in the same commits (at least 10
  shared commits and 30% of the less-changed file's), ignoring commits that touch
  more than 50 files.

This is an independent implementation, not affiliated with CodeScene.

## License

MIT
