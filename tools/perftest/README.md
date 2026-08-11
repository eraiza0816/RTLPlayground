# RTLPlayground performance test tool

Host-side measurements of the **management plane** of the RTL8373 switch
(8051 CPU + uIP stack): the Web UI, the HTTP API and ICMP.  The L2
forwarding fabric is hardware and is **not** measured here (wire-speed;
would need an external traffic generator).

## Prerequisites

- `python3` with the `requests` module
- `node` with the Playwright install under `tools/webuitest/node_modules`
  (`make -C tools webuitest`-equivalent; `tools/webuitest/run.sh`
  installs it on first use)
- Linux `ping`

## Usage

```sh
./run.sh <ip> [--password xxxx] [--long]
```

Example:

```sh
./run.sh 192.168.10.247 --password 1234
```

`--long` extends the concurrent-polling test from 10 s to 1 h
(stability / memory-exhaustion check).  The default run takes about
2-3 minutes.

Results land in `tools/perftest/report/` (gitignored):

- `perf-<date>.md` — combined markdown report
- `perf-http-<date>.json` / `perf-webui-<date>.json` — raw samples

Re-run the same command after a firmware change and compare the report
files to quantify the impact.

## What is measured

| # | Test | Details |
|---|---|---|
| 1 | ICMP RTT | 100 pings to the management CPU, median / p95 / max |
| 2 | HTTP API latency | `/status.json`, `/information.json`, `/vlanlist`, 100 GETs each |
| 3 | Static assets | `/`, `/main.js`, `/i18n.js`, cold (cache disabled) and warm |
| 4 | WebUI section switches | Playwright: click each of the 14 sidebar sections, poll until the panel's content appears, median of 3 |
| 5 | Concurrent polling | N clients (default 5) polling `/status.json` every 2 s for 10 s (1 h with `--long`) |
| 6 | Upload throughput | 512 KiB `POST /upload`, transfer rate |
| 7 | Long-term stability | the `--long` polling run doubles as a 1 h soak test |

## Safety notes

- The upload test sends **random bytes**: the firmware rejects them with
  a `400 CRC mismatch` and never writes flash or resets.  Never point
  the upload test at a real firmware image — a valid CRC would trigger
  an actual update and reset.
- The WebUI login uses the default `1234` password if `--password` is
  not given; the device password can also be supplied via
  `RTLP_PASSWORD` (perf_http.py) or the second argument
  (perf_webui.js).
