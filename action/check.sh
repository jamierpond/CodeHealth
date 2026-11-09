#!/usr/bin/env bash
# Runs `codehealth diff` against the pull request's base for the GitHub Action.
# Env: CODEHEALTH_BINARY, CODEHEALTH_BASE (may be empty), CODEHEALTH_FAIL_ON,
# PULL_REQUEST_BASE_SHA and PUSH_BEFORE_SHA from the event.
set -euo pipefail

maximumDeepenRounds=10
deepenStep=50

chooseBase() {
    if [ -n "${CODEHEALTH_BASE:-}" ]; then echo "$CODEHEALTH_BASE"; return; fi
    if [ -n "${PULL_REQUEST_BASE_SHA:-}" ]; then echo "$PULL_REQUEST_BASE_SHA"; return; fi
    if [ -n "${PUSH_BEFORE_SHA:-}" ] && [ "$PUSH_BEFORE_SHA" != 0000000000000000000000000000000000000000 ]; then
        echo "$PUSH_BEFORE_SHA"; return
    fi
    echo "::error title=codehealth::no base to compare with: set the action's base input" >&2
    exit 1
}

# Fetches the base and only as much history as it takes to find the merge base.
fetchUntilMergeBase() {
    local base="$1"
    git cat-file -e "$base^{commit}" 2>/dev/null || git fetch --quiet --no-tags --depth=1 origin "$base"
    for ((round = 0; round < maximumDeepenRounds; ++round)); do
        git merge-base "$base" HEAD >/dev/null 2>&1 && return
        git fetch --quiet --no-tags --deepen="$deepenStep" origin "$base" "$(git rev-parse HEAD)"
    done
    git merge-base "$base" HEAD >/dev/null || { echo "::error title=codehealth::no common history with $base" >&2; exit 1; }
}

base="$(chooseBase)"
fetchUntilMergeBase "$base"
"$CODEHEALTH_BINARY" diff "$base" --format github --fail-on "$CODEHEALTH_FAIL_ON" \
    --summary "$GITHUB_STEP_SUMMARY"
