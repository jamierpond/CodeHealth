#!/usr/bin/env bash
# Shallow-clones the Python corpus into ~/tmp/calibration-repos, then measures, samples,
# reviews through CodeScene's MCP server and writes python-comparison.md.
# COMPARISON_LABEL names the run in its history table. HOLDOUT=1 compares a second
# sample of different files instead (names end in -holdout), to check for overfitting.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
checkouts="${CALIBRATION_REPOS:-$HOME/tmp/calibration-repos}"
repositories=(pytorch/pytorch tensorflow/tensorflow django/django)
mkdir -p "$checkouts"
names=()
for repository in "${repositories[@]}"; do
    base="${repository#*/}"
    name="$base${HOLDOUT:+-holdout}"
    names+=("$name")
    [ -d "$checkouts/$base" ] || git clone --quiet --depth 1 "https://github.com/$repository" "$checkouts/$base"
    python3 "$here/compare-python.py" measure "$name" "$checkouts/$base"
    [ -f "$here/../../build/calibration/python/$name.sample.txt" ] \
        || python3 "$here/compare-python.py" sample "$name" 50 ${HOLDOUT:+"$base"}
    python3 "$here/compare-python.py" codescene "$name"
done
reported=("${names[@]}")
if [ -n "${HOLDOUT:-}" ]; then
    for repository in "${repositories[@]}"; do reported=("${repository#*/}" "${reported[@]}"); done
fi
python3 "$here/compare-python.py" report "${COMPARISON_LABEL:-latest}" "${reported[@]}"
