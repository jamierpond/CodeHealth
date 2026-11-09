#!/usr/bin/env bash
# Rebuilds, reruns the Python comparison under a label, and checks that no other
# language's scores or findings moved.  Usage: rerun-python.sh "<label>"
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
"$here/../build.sh" > /dev/null
COMPARISON_LABEL="$1" "$here/compare-python.sh" | grep -E 'exit|wrote'
"$here/check-other-languages.py"
grep -A 20 '^## History' "$here/python-comparison.md" | sed -n '5,20p' | grep '^|'
