#!/usr/bin/env bash
set -euo pipefail
EXE="$1"
ANALYZE="$2"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
CTIMING_OUT="$WORK/out.ctrace" "$EXE" >/dev/null
"$ANALYZE" "$WORK/out.ctrace" -o "$WORK/report.html" >/dev/null
test -s "$WORK/report.html"
grep -q '<!DOCTYPE html>' "$WORK/report.html"
grep -q 'id="ct-data"' "$WORK/report.html"
grep -q 'CT.registerTab' "$WORK/report.html"
if command -v python3 >/dev/null 2>&1; then
  python3 - "$WORK/report.html" <<'PY'
import json, re, sys
html = open(sys.argv[1], encoding="utf-8").read()
m = re.search(r'<script id="ct-data" type="application/json">(.*?)</script>', html, re.S)
assert m, "data script not found"
data = json.loads(m.group(1))
assert "functions" in data and "instances" in data and "aggregated" in data
assert any(f.get("name") == "leaf" for f in data["functions"])
PY
fi
if command -v node >/dev/null 2>&1; then
  node "$HERE/viewer_smoke.js" "$WORK/report.html"
fi
