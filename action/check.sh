#!/usr/bin/env bash
# Runs `codehealth diff` against the pull request's base for the GitHub Action.
# Env: CODEHEALTH_BINARY, CODEHEALTH_BASE (may be empty), CODEHEALTH_FAIL_ON,
# PULL_REQUEST_BASE_SHA, PUSH_BEFORE_SHA and DEFAULT_BRANCH from the event, GITHUB_REF.
set -euo pipefail

maximumDeepenRounds=10
deepenStep=50
missingCommit=0000000000000000000000000000000000000000

# Sets base (what codehealth compares with) and fetchSource (what to fetch from origin for it).
chooseBase() {
    local commit
    for commit in "${CODEHEALTH_BASE:-}" "${PULL_REQUEST_BASE_SHA:-}" "${PUSH_BEFORE_SHA:-}"; do
        if [ -n "$commit" ] && [ "$commit" != "$missingCommit" ]; then
            base="$commit" fetchSource="$commit"
            return
        fi
    done
    if [ -z "${DEFAULT_BRANCH:-}" ]; then
        echo "::error title=codehealth::no base to compare with: set the action's base input" >&2
        exit 1
    fi
    if [ "${GITHUB_REF:-}" = "refs/heads/$DEFAULT_BRANCH" ]; then
        echo "::notice title=codehealth::nothing to compare $DEFAULT_BRANCH with: the event names no earlier commit (first push of the repository, or a manual run); check skipped"
        exit 0
    fi
    base="origin/$DEFAULT_BRANCH"
    fetchSource="+refs/heads/$DEFAULT_BRANCH:refs/remotes/origin/$DEFAULT_BRANCH"
}

# Fetches the base and only as much history as it takes to find the merge base.
fetchUntilMergeBase() {
    git cat-file -e "$base^{commit}" 2>/dev/null || git fetch --quiet --no-tags --depth=1 origin "$fetchSource"
    for ((round = 0; round < maximumDeepenRounds; ++round)); do
        git merge-base "$base" HEAD >/dev/null 2>&1 && return
        git fetch --quiet --no-tags --deepen="$deepenStep" origin "$fetchSource" "$(git rev-parse HEAD)"
    done
    git merge-base "$base" HEAD >/dev/null || { echo "::error title=codehealth::no common history with $base" >&2; exit 1; }
}

chooseBase
fetchUntilMergeBase
"$CODEHEALTH_BINARY" diff "$base" --format github --fail-on "$CODEHEALTH_FAIL_ON" \
    --summary "$GITHUB_STEP_SUMMARY"
