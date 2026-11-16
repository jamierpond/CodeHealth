#!/usr/bin/env bash
# Prints version=<release tag> for $GITHUB_OUTPUT; "latest" becomes the newest release's tag.
# Usage: resolve-version.sh <version tag or latest>
set -euo pipefail
requestedVersion="$1"
if [ "$requestedVersion" != latest ]; then
    echo "version=$requestedVersion"
    exit 0
fi
latestReleaseUrl="https://github.com/jamierpond/CodeHealth/releases/latest"
# The page redirects to .../releases/tag/<tag>, which needs no token, unlike the rate-limited API.
resolvedUrl="$(curl -fsSL --retry 3 -o /dev/null -w '%{url_effective}' "$latestReleaseUrl")" || resolvedUrl=""
tag="${resolvedUrl##*/releases/tag/}"
if [ -z "$resolvedUrl" ] || [ "$tag" = "$resolvedUrl" ] || [ -z "$tag" ]; then
    echo "::error title=codehealth::could not find the latest codehealth release at $latestReleaseUrl: set the action's version input to a tag" >&2
    exit 1
fi
echo "version=$tag"
