#!/bin/bash
# RTLPlayground management-plane performance test runner.
#
# Usage: ./run.sh <ip> [--password xxxx] [--long]
#
# Runs the HTTP/ICMP measurements (perf_http.py) and the WebUI
# section-switch measurement (perf_webui.js), then writes a combined
# markdown report to report/perf-<date>.md.  Re-run the same command
# after a firmware change and diff the report/ directory.
set -euo pipefail

cd "$(dirname "$0")/../.."

HOST="${1:-192.168.10.247}"
shift || true
PASSWORD="1234"
LONG=""
while [ "$#" -gt 0 ]; do
	case "$1" in
		--password) PASSWORD="$2"; shift 2 ;;
		--long) LONG="--long"; shift ;;
		*) echo "Unknown option: $1" >&2; exit 1 ;;
	esac
done

REPORT_DIR=tools/perftest/report
mkdir -p "$REPORT_DIR"
DATE=$(date +%Y%m%d-%H%M%S)
HTTP_JSON="$REPORT_DIR/perf-http-$DATE.json"
WEBUI_JSON="$REPORT_DIR/perf-webui-$DATE.json"
MD="$REPORT_DIR/perf-$DATE.md"

echo "== HTTP/ICMP measurements =="
python3 tools/perftest/perf_http.py --host "$HOST" --password "$PASSWORD" $LONG --out "$HTTP_JSON"

if command -v node >/dev/null 2>&1; then
	echo "== WebUI section-switch measurements =="
	(cd tools/perftest && node perf_webui.js "http://$HOST" "$PASSWORD")
	WEBUI_REPORT="$(ls -t "$REPORT_DIR"/perf-webui-*.json | head -1)"
else
	echo "!! node not found; skipping WebUI measurements" >&2
	WEBUI_REPORT=""
fi

{
	echo "# RTLPlayground performance report ($DATE)"
	echo
	echo "Target: $HOST  (run with: ./run.sh $HOST --password ...)"
	echo
	echo "## ICMP RTT"
	python3 - "$HTTP_JSON" <<'PYEOF'
import json, sys
d = json.load(open(sys.argv[1]))
icmp = d.get("icmp") or {}
if "error" in icmp:
	print("(ping failed: %s)" % icmp["error"])
else:
	print("| median | p95 | max | samples |")
	print("|---|---|---|---|")
	print("| %s ms | %s ms | %s ms | %s |" % (
		icmp.get("median_ms"), icmp.get("p95_ms"), icmp.get("max_ms"), icmp.get("n")))
print()
print("## HTTP API latency (ms)")
print("| endpoint | median | p95 | max | failures |")
print("|---|---|---|---|---|")
for k, v in (d.get("api") or {}).items():
	print("| %s | %s | %s | %s | %s |" % (k, v.get("median_ms"), v.get("p95_ms"), v.get("max_ms"), v.get("failures")))
print()
print("## Static assets (ms, n=%d)" % (((((d.get("assets") or {}).get("cold") or {}).get("/")) or {}).get("n") or 0))
print("| mode | path | median | p95 | max |")
print("|---|---|---|---|---|")
for mode, m in (d.get("assets") or {}).items():
	for k, v in m.items():
		print("| %s | %s | %s | %s | %s |" % (mode, k, v.get("median_ms"), v.get("p95_ms"), v.get("max_ms")))
print()
print("## /config (ms)")
c = d.get("config") or {}
print("| median | p95 | max | failures |")
print("|---|---|---|---|")
print("| %s | %s | %s | %s |" % (c.get("median_ms"), c.get("p95_ms"), c.get("max_ms"), c.get("failures")))
print()
print("## Concurrent polling (/status.json every 2 s)")
cc = d.get("concurrent") or {}
print("| median | p95 | max | samples | failures |")
print("|---|---|---|---|---|")
s = cc.get("stats") or {}
print("| %s | %s | %s | %s | %s |" % (s.get("median_ms"), s.get("p95_ms"), s.get("max_ms"), s.get("n"), cc.get("failures")))
print()
print("## Upload throughput")
u = d.get("upload") or {}
print("| bytes | ms | MB/s | http status | expected |")
print("|---|---|---|---|---|")
print("| %s | %s | %s | %s | %s |" % (u.get("bytes"), u.get("ms"), u.get("MBps"), u.get("status"), u.get("expected")))
PYEOF
	echo
	if [ -n "$WEBUI_REPORT" ]; then
		echo "## WebUI section switches (ms, median of 3)"
		echo
		node -e "
const r = require('./$WEBUI_REPORT');
console.log('### Cold start: load=%sms render=%sms' , r.cold.load_ms, r.cold.render_ms);
console.log();
console.log('| section | median | samples |');
console.log('|---|---|---|');
for (const [k, v] of Object.entries(r.sections)) {
  const num = v.filter(x => x !== 'timeout');
  const med = num.length ? num.sort((a,b)=>a-b)[Math.floor(num.length/2)] : 'timeout';
  console.log('| ' + k + ' | ' + med + ' | ' + JSON.stringify(v) + ' |');
}
"
	fi
} > "$MD"

echo
echo "Report: $MD"
