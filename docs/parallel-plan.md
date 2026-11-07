# Parallel analysis plan

## Where the time goes today (torvalds/linux, one year of history, M-series Mac, 18 cores)

| Phase | Wall time | Parallel? |
|---|---|---|
| `git ls-tree` and `git cat-file --batch`, 65,004 files | ~6 s | yes |
| tree-sitter parse and metrics | ~105 s | yes |
| `git log --name-only` (99,528 commits) and counting | ~12 s | no, one process |
| scoring, sorting, JSON | ~5 s | scoring yes, the rest no |
| **total** | **129 s, 1.97 GB peak** | |

Parsing is 80% of the run and is one file at a time on one core. Everything in it is
independent per file, so it is the part to spread across cores.

## Design

- **Workers.** One `Threads::Thread` per core (`std::thread::hardware_concurrency()`),
  each with its own `TSParser` (a parser is not thread-safe; a language is shared and
  read-only). `Thread` is the joining wrapper eacp already has; nothing else in
  `Docs/Toolbox.md` is needed: no `callAsync`, since workers never touch the main loop.
- **Queue.** The file list from `git ls-tree -l` (which gives blob sizes) is sorted
  largest first, and workers take the next index from one `std::atomic<size_t>`.
  Largest-first means the 195,000-line AMD register headers start at once instead of
  landing on one worker at the end. Per-worker deques with stealing are only worth it
  if the profile then shows workers idle at the tail; with largest-first it should not.
- **Blob reads.** Built as one reader thread running `git cat-file --batch` over
  batches of up to 500 files or 16 MB, largest first, feeding a bounded queue of
  files that the parser workers drain. The planned cat-file per worker deadlocked:
  eacp's Process leaves its pipes inheritable (no close-on-exec), so a child
  started while another `git cat-file` is running keeps that one's input open and
  the two wait on each other for end of input. A fix in eacp (close-on-exec pipes on
  POSIX, an explicit handle list on Windows) would allow a reader per worker again;
  reading is ~6 s of linux, so one reader is not the bottleneck today. Every git
  child is also started under one mutex, so a child only ever holds pipes of
  children started before it.
- **History.** `git log` stays one process, started first and read on its own thread
  while the workers parse, so its ~12 s overlaps parsing instead of following it. Its
  output is parsed as it streams (`ProcessOptions::onOutput`). eacp's Process still
  keeps the whole output too, so streaming saves time, not memory; the memory came
  back another way: the shallow clone's boundary commits are passed to `git log` as
  `^<hash>` exclusions, which cut its output from ~750 MB to 8 MB and its time from
  12 s to 5 s.
- **Output order.** Each worker writes into a pre-sized results vector at its file's
  index, so the result is identical whatever the thread timing. Sorting by score and
  ranking hotspots happen after the join, exactly as now.
- **Memory.** Each worker holds one blob, one tree and one `FileMetrics` at a time; the
  batched reading already in place stops sources accumulating.

## Measured (built)

| | before | 1 job | 18 jobs |
|---|---|---|---|
| linux wall time | 129 s | 115 s | 17 s |
| linux peak memory | 1.97 GB | 1.65 GB | 1.78 GB |
| private C++/TypeScript monorepo, 2,900 files, wall time | 5.5 s | | 0.46 s |
| same repository, peak memory | | | 149 MB |

Output is byte-identical between 1 and 18 jobs on linux, and the tests check it
on a fixture repository and on eacp.

## Expected speedup (as planned)

Parsing at ~105 s over 6 performance and 12 efficiency cores should land around
12–15 s. With `git log` overlapped, the whole linux run should take 15–20 s, against
129 s now. A 2,900-file repository (5.5 s now) should take about 1 s.

## Change coupling, alongside parsing

From the same `git log --name-only` stream, on the history thread:

- every pair of files changed in one commit counts once for that pair;
- commits touching more than 50 files are skipped, as CodeScene does, since mass
  renames and formatting sweeps couple everything to everything;
- a pair is reported when it shares at least 10 commits and at least 30% of the
  commits of the less-changed file (both thresholds are named constants, tuned later);
- only pairs where both files are analysed are kept, so the pair map stays bounded by
  source files rather than every path in history.

## Include and import dependencies

The same parse yields `preproc_include` (C/C++) and `import_statement` /
`require` calls (TypeScript/JavaScript). Each worker records them in its file's
`FileMetrics`; resolving relative paths to repository files happens after the join.
This gives a static dependency graph to set beside change coupling: files that
change together without depending on each other are the interesting ones.

## Considered and rejected

libgit2 for history: no network access is needed, and the git CLI is fast enough
(12 s for a year of linux).
