#!/usr/bin/env bash
# Clones the public repositories used for validation into ~/codescene-clones,
# each with one year of history. Usage: scripts/clone-public-repos.sh [name ...]
set -euo pipefail
declare -A repositories=(
    [linux]=torvalds/linux
    [requests]=psf/requests
    [flask]=pallets/flask
    [django]=django/django
    [fmt]=fmtlib/fmt
    [json]=nlohmann/json
    [googletest]=google/googletest
    [abseil-cpp]=abseil/abseil-cpp
    [llvm-project]=llvm/llvm-project
    [obs-studio]=obsproject/obs-studio
    [JUCE]=juce-framework/JUCE
    [beast]=boostorg/beast
)
since=$(date -v-1y +%Y-%m-%d 2>/dev/null || date -d '1 year ago' +%Y-%m-%d)
names=("$@")
[ ${#names[@]} -eq 0 ] && names=("${!repositories[@]}")
for name in "${names[@]}"; do
    target="$HOME/codescene-clones/$name"
    if [ -d "$target/.git" ]; then
        echo "already cloned: $target"
        continue
    fi
    git clone --quiet --shallow-since="$since" --single-branch \
        "https://github.com/${repositories[$name]}" "$target"
    echo "cloned $name"
done
