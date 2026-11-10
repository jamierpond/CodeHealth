#!/usr/bin/env bash
# Prints the compile and link steps each build runs from a fresh configure, then
# times a clean build of codehealth, the rest of the build, a rebuild with no
# change and a rebuild after touching one source file.
# Usage: scripts/measure-build.sh <build directory> [parallel jobs]
set -euo pipefail
cd "$(dirname "$0")/.."
buildDirectory=${1:?usage: scripts/measure-build.sh <build directory> [parallel jobs]}
parallelJobs=${2:-6}
appTarget=codehealth
testTarget=CodeHealthTests
touchedSource=Source/Score.cpp

rm -rf "$buildDirectory"
cmake -G Ninja -B "$buildDirectory" -S . -DCMAKE_BUILD_TYPE=Release >/dev/null

countFreshSteps() { ninja -C "$buildDirectory" -t commands "$@" | wc -l; }
countPendingSteps() { ninja -C "$buildDirectory" -n "$@" | grep -c '^\[' || true; }
listLibraries() { ninja -C "$buildDirectory" -t commands "$@" | grep -oE '[^ /\\]+\.(a|lib)\b' | LC_ALL=C sort -u; }

echo "steps, bare cmake --build (all): $(countFreshSteps)"
echo "steps, $appTarget: $(countFreshSteps "$appTarget")"
echo "steps, $testTarget: $(countFreshSteps "$testTarget")"
echo "steps, C++20 module scans in all: $(ninja -C "$buildDirectory" -t commands | grep -cE 'scan-deps|scanDependencies' || true)"
echo "libraries built by all but not linked into $appTarget or $testTarget:"
LC_ALL=C comm -23 <(listLibraries) <(listLibraries "$appTarget" "$testTarget")

# Compiler time (user + system) barely moves when other jobs share the machine; wall time does.
timeBuild() {
    local label=$1
    shift
    local TIMEFORMAT="seconds, $label: %0R wall, %0U user + %0S system"
    time cmake --build "$buildDirectory" --parallel "$parallelJobs" "$@" >/dev/null
}
timeBuild "clean build of $appTarget" --target "$appTarget"
timeBuild "then the rest of all"
echo "steps, no-change rebuild: $(countPendingSteps)"
timeBuild "no-change rebuild"
touch "$touchedSource"
echo "steps, rebuild after touching $touchedSource: $(countPendingSteps)"
timeBuild "rebuild after touching $touchedSource"
