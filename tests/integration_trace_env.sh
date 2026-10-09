#!/usr/bin/env bash
set -euo pipefail
EXE="$1"
ANALYZE="$2"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
CTIMING_TRACE='hot' CTIMING_OUT="$WORK/out.ctrace" "$EXE" once >/dev/null
test -s "$WORK/out.ctrace"
"$ANALYZE" "$WORK/out.ctrace" --json "$WORK/a.json" >/dev/null
python3 - "$WORK/a.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
names = [f["name"] for f in d["functions"] if f["calls"] > 0]
assert any(n == "hot" or n.endswith("hot") for n in names), names
assert any("leaf" in n for n in names), names
assert not any("unrelated" in n for n in names), "unrelated leaked: %r" % names
PY
