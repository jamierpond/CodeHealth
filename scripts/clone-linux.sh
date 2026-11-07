#!/usr/bin/env bash
# Clones torvalds/linux with one year of history into ~/codescene-clones/linux.
set -euo pipefail
target=${1:-$HOME/codescene-clones/linux}
since=$(date -v-1y +%Y-%m-%d 2>/dev/null || date -d '1 year ago' +%Y-%m-%d)
[ -d "$target/.git" ] && { echo "already cloned: $target"; exit 0; }
git clone --shallow-since="$since" --single-branch https://github.com/torvalds/linux "$target"
