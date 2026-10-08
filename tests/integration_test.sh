#!/usr/bin/env bash
set -euo pipefail
EXE="$1"
INFO="$2"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
CTIMING_OUT="$WORK/out.ctrace" "$EXE" >/dev/null
test -s "$WORK/out.ctrace"
OUT="$("$INFO" "$WORK/out.ctrace")"
echo "$OUT"
echo "$OUT" | grep -q "total_events"
echo "$OUT" | grep -qE '^[[:space:]]*6[[:space:]]+leaf$'
echo "$OUT" | grep -qE '^[[:space:]]*2[[:space:]]+mid$'
echo "$OUT" | grep -qE '^[[:space:]]*1[[:space:]]+main$'
