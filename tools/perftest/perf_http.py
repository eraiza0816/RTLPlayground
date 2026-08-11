#!/usr/bin/env python3
"""HTTP / ICMP performance measurements for the RTLPlayground management CPU.

Measures the management-plane performance of the switch's 8051 CPU + uIP
stack: ICMP RTT, JSON API latency, static asset delivery (cold/warm),
concurrent polling load, startup-config fetch and firmware upload
throughput.  L2 wire-speed forwarding is out of scope (hardware fabric).

Only the management CPU is exercised; nothing in this script can damage
the device.  The upload test sends random bytes: the firmware rejects
them with a CRC mismatch (400) and never starts a flash write or reset.
Never point this at a real firmware file.
"""

import argparse
import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time
from datetime import datetime

import requests


def _post_one_segment(host, path, body, content_type, cookies, timeout=15):
    """POST with headers and body in a single TCP segment.

    The firmware's uIP httpd processes one segment per appcall and does
    not buffer split POST bodies on the login path.  Clients that send
    the headers and the body in separate writes (python requests /
    http.client) therefore get the header-only segment first and are
    rejected.  curl and Go's http client coalesce small bodies into one
    segment and work fine.  Returns the raw HTTP response bytes.
    """
    req = ("POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: %s\r\n"
           "Content-Length: %d\r\nConnection: close\r\n"
           % (path, host, content_type, len(body)))
    if "session" in cookies:
        req += "Cookie: session=%s\r\n" % cookies["session"]
    req += "\r\n" + body
    s = socket.create_connection((host, 80), timeout=timeout)
    s.settimeout(timeout)
    s.sendall(req.encode("latin1"))
    resp = b""
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            resp += chunk
    except socket.timeout:
        pass
    s.close()
    return resp


def _parse_response(resp):
    head, _, body = resp.partition(b"\r\n\r\n")
    lines = head.split(b"\r\n")
    status = int(lines[0].split(b" ")[1])
    headers = {}
    for line in lines[1:]:
        if b":" in line:
            k, _, v = line.partition(b":")
            headers[k.strip().lower().decode("latin1")] = v.strip()
    return status, headers, body


def median(samples):
    s = sorted(samples)
    n = len(s)
    if not n:
        return None
    return s[n // 2]


def p95(samples):
    s = sorted(samples)
    if not s:
        return None
    return s[int(len(s) * 0.95) - 1]


def stats(samples):
    return {
        "median_ms": round(median(samples), 2) if samples else None,
        "p95_ms": round(p95(samples), 2) if samples else None,
        "max_ms": round(max(samples), 2) if samples else None,
        "n": len(samples),
    }


def login(session, host, password):
    resp = _post_one_segment(host, "/login", "pwd=" + password,
                             "application/x-www-form-urlencoded",
                             session.cookies.get_dict())
    status, headers, _ = _parse_response(resp)
    if status != 302 or b"index.html" not in headers.get("location", b""):
        raise RuntimeError("login failed: HTTP %d Location %r"
                           % (status, headers.get("location", b"")))
    m = re.search(rb"session=([0-9a-fA-F]+)", headers.get("set-cookie", b""))
    if not m:
        raise RuntimeError("login did not set session cookie")
    session.cookies.set("session", m.group(1).decode("ascii"), domain=host)


def measure_icmp(host, count):
    try:
        out = subprocess.run(
            ["ping", "-n", "-i", "0.2", "-c", str(count), host],
            capture_output=True, text=True, timeout=count + 30,
        ).stdout
    except Exception as e:
        return {"error": str(e)}
    times = [float(m) for m in re.findall(r"time=(\d+(?:\.\d+)?)\s*ms", out)]
    if not times:
        return {"error": "no RTT samples parsed", "raw_tail": out[-200:]}
    return stats(times)


def measure_http(session, host, paths, count, headers=None):
    results = {}
    for path in paths:
        samples = []
        failures = 0
        for _ in range(count):
            try:
                t0 = time.monotonic()
                r = session.get("http://%s%s" % (host, path), headers=headers, timeout=15)
                dt = (time.monotonic() - t0) * 1000
                if r.status_code != 200:
                    failures += 1
                samples.append(dt)
            except Exception:
                failures += 1
        results[path] = stats(samples)
        results[path]["failures"] = failures
    return results


def measure_concurrent(session, host, workers, seconds, sleep=2.0):
    samples = []
    failures = 0
    lock = threading.Lock()
    stop = time.monotonic() + seconds

    def worker():
        nonlocal failures
        while time.monotonic() < stop:
            t0 = time.monotonic()
            try:
                r = session.get("http://%s/status.json" % host, timeout=15)
                dt = (time.monotonic() - t0) * 1000
                with lock:
                    if r.status_code == 200:
                        samples.append(dt)
                    else:
                        failures += 1
            except Exception:
                with lock:
                    failures += 1
            time.sleep(sleep)

    threads = [threading.Thread(target=worker) for _ in range(workers)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    return stats(samples), failures


def measure_upload(session, host, size=512 * 1024):
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(os.urandom(size))
        path = f.name
    try:
        t0 = time.monotonic()
        with open(path, "rb") as f:
            r = session.post(
                "http://%s/upload" % host,
                files={"uploadedfile": ("random.bin", f, "application/octet-stream")},
                timeout=120,
            )
        dt = (time.monotonic() - t0) * 1000
        ok = r.status_code in (200, 400)  # 400 = CRC mismatch, expected for random bytes
        return {
            "bytes": size,
            "ms": round(dt, 1),
            "MBps": round(size / dt * 1000 / 1e6, 3) if dt else None,
            "status": r.status_code,
            "expected": ok,
            "body": r.text.strip()[:80],
        }
    finally:
        os.unlink(path)


def main():
    ap = argparse.ArgumentParser(description="RTLPlayground management-plane performance tests")
    ap.add_argument("--host", default=os.environ.get("RTLP_HOST", "192.168.10.247"))
    ap.add_argument("--password", default=os.environ.get("RTLP_PASSWORD", "1234"))
    ap.add_argument("--count", type=int, default=100, help="samples per API/asset test")
    ap.add_argument("--concurrent", type=int, default=5, help="concurrent polling clients")
    ap.add_argument("--long", action="store_true", help="run the polling test for 1 hour")
    ap.add_argument("--out", default=None, help="JSON output file (default report/perf-http-<date>.json)")
    args = ap.parse_args()

    report = {"host": args.host, "date": datetime.now().isoformat(timespec="seconds")}
    session = requests.Session()
    try:
        login(session, args.host, args.password)
    except Exception as e:
        print("FATAL: %s" % e, file=sys.stderr)
        sys.exit(2)
    report["login_ok"] = True

    print("== ICMP RTT (ping %d) ==" % args.count)
    report["icmp"] = measure_icmp(args.host, args.count)
    print(json.dumps(report["icmp"]))

    print("== HTTP API latency (n=%d) ==" % args.count)
    report["api"] = measure_http(session, args.host, ["/status.json", "/information.json", "/vlanlist"], args.count)
    for k, v in report["api"].items():
        print("%-18s %s" % (k, v))

    print("== Static assets, cold vs warm (n=%d) ==" % args.count)
    report["assets"] = {}
    for mode, hdr in (("cold", {"Cache-Control": "no-cache"}), ("warm", None)):
        report["assets"][mode] = measure_http(session, args.host, ["/", "/main.js", "/i18n.js"], args.count, headers=hdr)
        for k, v in report["assets"][mode].items():
            print("  %-5s %-10s %s" % (mode, k, v))

    print("== /config latency (n=%d) ==" % (args.count // 10))
    report["config"] = measure_http(session, args.host, ["/config"], max(1, args.count // 10))["/config"]
    print(json.dumps(report["config"]))

    seconds = 3600 if args.long else 10
    print("== Concurrent polling: %d clients x %.0fs ==" % (args.concurrent, seconds))
    report["concurrent"] = {}
    report["concurrent"]["stats"], report["concurrent"]["failures"] = measure_concurrent(
        session, args.host, args.concurrent, seconds
    )
    print(json.dumps(report["concurrent"]))

    print("== Firmware upload throughput (512 KiB, random = safe) ==")
    report["upload"] = measure_upload(session, args.host)
    print(json.dumps(report["upload"]))

    out = args.out
    if not out:
        d = datetime.now().strftime("%Y%m%d-%H%M%S")
        out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "report", "perf-http-%s.json" % d)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        json.dump(report, f, indent=2)
    print("Report written to %s" % out)
    sys.exit(0)


if __name__ == "__main__":
    main()
