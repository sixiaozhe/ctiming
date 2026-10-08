#!/usr/bin/env bash
set -euo pipefail
EXE="$1"
ANALYZE="$2"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
CTIMING_OUT="$WORK/out.ctrace" "$EXE" >/dev/null
"$ANALYZE" "$WORK/out.ctrace" --json "$WORK/analysis.json" >/dev/null
test -s "$WORK/analysis.json"
grep -q '"main"' "$WORK/analysis.json"
grep -q '"leaf"' "$WORK/analysis.json"
grep -q '"call_graph"' "$WORK/analysis.json"
if command -v python3 >/dev/null 2>&1; then
  python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$WORK/analysis.json"
fi
TXT="$("$ANALYZE" "$WORK/out.ctrace")"
echo "$TXT"
echo "$TXT" | grep -q "leaf"
echo "$TXT" | grep -q "total"
