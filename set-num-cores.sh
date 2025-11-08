#!/bin/sh
# Set GEMMINI_NUM_CPU_CORES for the calling shell.
# Usage when sourcing: . ./set-num-cores.sh <value>
# Usage when executing: eval "$(./set-num-cores.sh <value>)"

# Detect if script is sourced. 'return' will succeed only when sourced.
sourced=0
(return 0 2>/dev/null) && sourced=1

if [ "$#" -ne 1 ]; then
	if [ "$sourced" -eq 1 ]; then
		printf 'Usage: . %s <value>\n' "${0##*/}" >&2
		return 2
	else
		printf 'Usage: %s <value>\n' "${0##*/}" >&2
		exit 2
	fi
fi

value="$1"

if [ "$sourced" -eq 1 ]; then
	# Set in the current shell
	export GEMMINI_NUM_CPU_CORES="$value"
else
	# Print a safe export command. Escape any single quotes in the value.
	escaped_value=$(printf '%s' "$value" | sed "s/'/'\\\\''/g")
	printf "export GEMMINI_NUM_CPU_CORES='%s'\n" "$escaped_value"
fi