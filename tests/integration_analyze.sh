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
if "$ANALYZE" >/dev/null 2>&1; then echo "expected usage error"; exit 1; fi
if "$ANALYZE" "$WORK/nonexistent.ctrace" >/dev/null 2>&1; then echo "expected open error"; exit 1; fi
if "$ANALYZE" "$WORK/out.ctrace" --min-total abc >/dev/null 2>&1; then echo "expected parse error"; exit 1; fi
if "$ANALYZE" "$WORK/out.ctrace" --min-total -5 >/dev/null 2>&1; then echo "expected negative error"; exit 1; fi
FILTERED="$("$ANALYZE" "$WORK/out.ctrace" --include 'leaf*')"
FTABLE="$(echo "$FILTERED" | grep -E '^  [0-9]')"
echo "$FTABLE" | grep -q "leaf"
if echo "$FTABLE" | grep -q "mid"; then echo "include filter failed"; exit 1; fi
EXCLUDED="$("$ANALYZE" "$WORK/out.ctrace" --exclude 'leaf*')"
XTABLE="$(echo "$EXCLUDED" | grep -E '^  [0-9]')"
if echo "$XTABLE" | grep -q "leaf"; then echo "exclude filter failed"; exit 1; fi
