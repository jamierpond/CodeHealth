#!/usr/bin/env bash
# Runs codehealth with the action's arguments input, split on whitespace; quotes are not interpreted.
# Env: CODEHEALTH_BINARY, CODEHEALTH_ARGUMENTS.
set -euo pipefail
read -ra arguments <<< "$CODEHEALTH_ARGUMENTS"
exec "$CODEHEALTH_BINARY" "${arguments[@]}"
