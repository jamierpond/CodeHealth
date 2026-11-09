#!/usr/bin/env bash
# Fails if any source file in this repository, as it is on disk now (untracked
# files included), scores below 9.
set -euo pipefail
cd "$(dirname "$0")/.."
binary=build/codehealth
[ -x build/codehealth.exe ] && binary=build/codehealth.exe
index=$(mktemp)
trap 'rm -f "$index"' EXIT
cp "$(git rev-parse --git-path index)" "$index"
GIT_INDEX_FILE="$index" git add -A
tree=$(GIT_INDEX_FILE="$index" git write-tree)
"$binary" . --ref "$tree" --check 9
