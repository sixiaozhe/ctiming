#!/usr/bin/env bash
set -euo pipefail
EXE="$1"
ANALYZE="$2"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
CTIMING_OUT="$WORK/out.ctrace" "$EXE" >/dev/null
test -s "$WORK/out.ctrace"
TXT="$("$ANALYZE" "$WORK/out.ctrace" --top 30)"
echo "$TXT"
echo "$TXT" | grep -q "fib"
echo "$TXT" | grep -q "busy_kernel"
echo "$TXT" | grep -q "worker"
"$ANALYZE" "$WORK/out.ctrace" --json "$WORK/a.json" >/dev/null
test -s "$WORK/a.json"
"$ANALYZE" "$WORK/out.ctrace" -o "$WORK/report.html" >/dev/null
test -s "$WORK/report.html"
grep -q 'id="ct-data"' "$WORK/report.html"
if command -v python3 >/dev/null 2>&1; then
  python3 - "$WORK/a.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["trace"]["threads"] >= 2, "expected multiple threads"
assert any("fib" in f["name"] for f in d["functions"]), "fib not found"
leaked = [f["name"] for f in d["functions"] if f["calls"] > 0 and (
    "__gnu_cxx::" in f["name"] or "operator new" in f["name"])]
assert not leaked, "standard library symbols leaked: %r" % leaked[:5]
PY
fi
