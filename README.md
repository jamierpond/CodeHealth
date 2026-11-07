# CodeHealth

A command-line tool that scores how maintainable each file in a git repository
is, from 1 to 10, and ranks hotspots: files that change often and are complex.
It reads C, C++, Objective-C, Objective-C++, TypeScript, TSX, JavaScript and JSX.

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

### Changes since a base

```bash
codehealth diff develop        # merge base of develop and HEAD, against the working tree
codehealth diff                # base: origin's default branch, else HEAD
codehealth diff HEAD~1 --json
codehealth diff develop --check   # exit 1 if a changed file's score dropped or gained a finding
```

Uncommitted and untracked files count. For each changed file it prints the score
before and after, and the findings introduced, made worse and fixed, by function
and line. "Near your changes" lists files you did not touch that usually change
with one you did, and files that include or import one. From eacp:

```
345 changed source files since HEAD~1 (0550c80b2a)

345 changed files kept their score and findings

Near your changes
   5.64  Lib/eacp/WebView/WebView/WebView-Windows.cpp  usually changes with Lib/eacp/WebView/WebView/WebView.h (31 of 49 commits)
   8.26  Lib/eacp/GPU/Codegen/ShaderGraph.cpp  usually changes with Lib/eacp/GPU/Codegen/ShaderGraph.h (29 of 31 commits)
   7.39  Lib/eacp/Camera/Camera-Android.cpp  depends on Lib/eacp/Camera/Camera.h
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
