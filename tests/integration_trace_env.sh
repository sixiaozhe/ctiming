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
calls = {f["name"]: f["calls"] for f in d["functions"] if f["calls"] > 0}
names = list(calls)
assert any(n == "hot" or n.endswith("hot") for n in names), names
assert any("leaf" in n for n in names), names
assert any(n == "rec" or n.endswith("rec") for n in names), names
rname = next(n for n in names if n == "rec" or n.endswith("rec"))
assert calls[rname] == 4, calls[rname]
assert not any("unrelated" in n for n in names), "unrelated leaked: %r" % names
PY

CTIMING_TRACE='hot' CTIMING_EXCLUDE='*leaf*' CTIMING_OUT="$WORK/out2.ctrace" "$EXE" once >/dev/null
test -s "$WORK/out2.ctrace"
"$ANALYZE" "$WORK/out2.ctrace" --json "$WORK/a2.json" >/dev/null
python3 - "$WORK/a2.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
names = [f["name"] for f in d["functions"] if f["calls"] > 0]
assert any(n == "hot" or n.endswith("hot") for n in names), names
assert not any("leaf" in n for n in names), "leaf not excluded: %r" % names
assert not any("unrelated" in n for n in names), "unrelated leaked: %r" % names
PY

CTIMING_TRACE='hot' CTIMING_INCLUDE='zzz-no-match*' CTIMING_OUT="$WORK/out3.ctrace" "$EXE" once >/dev/null
test -s "$WORK/out3.ctrace"
"$ANALYZE" "$WORK/out3.ctrace" --json "$WORK/a3.json" >/dev/null
python3 - "$WORK/a3.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
names = [f["name"] for f in d["functions"] if f["calls"] > 0]
assert any(n == "hot" or n.endswith("hot") for n in names), names
assert any("leaf" in n for n in names), names
assert not any("unrelated" in n for n in names), "unrelated leaked: %r" % names
PY
