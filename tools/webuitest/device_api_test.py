#!/usr/bin/env python3
"""Real-device HTTP API smoke tests for the audit D-group fixes.

Runs against a real switch (the httpd_sim simulator does not reproduce
the firmware JSON escaping, the PVID mask or the transfer behaviour):

  - D1: a port name containing a quote must not break /status.json
        (and the other JSON APIs must stay valid JSON)
  - D5: /vlanlist carries a per-VLAN PVID port mask, and it tracks a
        pvid change on a non-management port
  - D8: /main.js (gzip) must arrive essentially complete and expand
        to a full, runnable script
  - A7: GET /reset returns 200 (optional --reset-test, reboots!)

The tests restore the modified state (port name, PVID) afterwards.

Usage: python3 device_api_test.py --host <ip> [--password <pwd>]
       [--port N] [--reset-test]
"""

import argparse
import gzip
import json
import re
import socket
import sys
import time

import requests


def _post_one_segment(host, path, body, content_type, cookies, timeout=15):
    """POST with headers and body in a single TCP segment.

    The firmware's uIP httpd processes one segment per appcall and does
    not buffer split POST bodies on the login path.  Clients that send
    the headers and the body in separate writes (python requests /
    http.client) are rejected.  curl and Go's http client coalesce
    small bodies into one segment and work fine.
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
            headers[k.strip().lower().decode("latin1")] = v.strip().decode("latin1")
    return status, headers, body


def login(session, host, password):
    resp = _post_one_segment(host, "/login", "pwd=" + password,
                             "application/x-www-form-urlencoded",
                             session.cookies.get_dict())
    status, headers, _ = _parse_response(resp)
    if status != 302 or "index.html" not in headers.get("location", ""):
        raise RuntimeError("login failed: HTTP %d Location %r"
                           % (status, headers.get("location", "")))
    m = re.search(rb"session=([0-9a-fA-F]+)", headers.get("set-cookie", "").encode("latin1"))
    if not m:
        raise RuntimeError("login did not set session cookie")
    session.cookies.set("session", m.group(1).decode("ascii"), domain=host)


def get_json(session, host, path):
    r = session.get("http://%s%s" % (host, path), timeout=20)
    if r.status_code != 200:
        raise AssertionError("%s: HTTP %d" % (path, r.status_code))
    return json.loads(r.text)


def cmd(session, host, text):
    resp = _post_one_segment(host, "/cmd", text + "\n", "text/plain",
                             session.cookies.get_dict())
    status, _, _ = _parse_response(resp)
    if status != 200:
        raise AssertionError("/cmd %r: HTTP %d" % (text, status))


def cmd_expect_fail(session, host, text):
    """Send a /cmd that is expected to be rejected (invalid input)."""
    resp = _post_one_segment(host, "/cmd", text + "\n", "text/plain",
                             session.cookies.get_dict())
    status, _, _ = _parse_response(resp)
    if status == 200:
        raise AssertionError("/cmd %r unexpectedly accepted" % text)
    return status


def telnet_cmd(host, password, command, timeout=25):
    """Run one CLI command over telnet (used for `commit`, which is
    MODE_PRIVILEGED and not reachable through the HTTP API)."""
    s = socket.create_connection((host, 23), timeout=10)
    s.settimeout(timeout)
    buf = b""
    while b"Password:" not in buf:
        buf += s.recv(1024)
    s.sendall((password + "\r\n").encode("latin1"))
    buf = b""
    while b"> " not in buf and b"#" not in buf:
        buf += s.recv(1024)
    s.sendall((command + "\r\n").encode("latin1"))
    resp = b""
    try:
        while True:
            chunk = s.recv(1024)
            if not chunk:
                break
            resp += chunk
    except socket.timeout:
        pass
    s.close()
    return resp.decode("latin1", errors="replace")


PASS = []


def check(name, ok, detail=""):
    PASS.append((name, ok, detail))
    print("%s %-34s %s" % ("PASS" if ok else "FAIL", name, detail))


def main():
    ap = argparse.ArgumentParser(description="RTLPlayground device API smoke tests")
    ap.add_argument("--host", default="192.168.10.247")
    ap.add_argument("--password", default="1234")
    ap.add_argument("--port", type=int, default=5,
                    help="non-management physical port used for the PVID test (default 5)")
    ap.add_argument("--reset-test", action="store_true",
                    help="also verify GET /reset (reboots the switch)")
    args = ap.parse_args()

    s = requests.Session()
    login(s, args.host, args.password)

    # --- Core APIs must be valid JSON (D1 baseline) -------------------------
    try:
        info = get_json(s, args.host, "/information.json")
        check("information.json valid JSON", True, "hostname=%s" % info.get("hostname"))
    except Exception as e:
        check("information.json valid JSON", False, str(e))
        print("Cannot continue without a working API.", file=sys.stderr)
        sys.exit(1)
    try:
        get_json(s, args.host, "/status.json")
        check("status.json valid JSON", True)
    except Exception as e:
        check("status.json valid JSON", False, str(e))
    try:
        vl = get_json(s, args.host, "/vlanlist")
        check("vlanlist valid JSON", True, "%d VLANs" % len(vl))
    except Exception as e:
        check("vlanlist valid JSON", False, str(e))
        vl = None

    # --- D1: quote in a port name must not break /status.json --------------
    try:
        cmd(s, args.host, "port %d name te\"st" % args.port)
        data = get_json(s, args.host, "/status.json")
        names = {p.get("portNum"): p.get("name") for p in data}
        ok = names.get(args.port) == 'te"st'
        check("D1 port name with quote survives JSON", ok,
              "port %d name=%r" % (args.port, names.get(args.port)))
    except Exception as e:
        check("D1 port name with quote survives JSON", False, str(e))
    finally:
        try:
            cmd(s, args.host, "port %d name" % args.port)
        except Exception:
            pass

    # --- D5: /vlanlist carries a PVID mask that tracks pvid changes --------
    try:
        before = {v["id"]: v.get("pvid") for v in vl} if vl else {}
        cmd(s, args.host, "pvid %d 2" % args.port)
        time.sleep(0.5)
        after = {v["id"]: v.get("pvid") for v in get_json(s, args.host, "/vlanlist")}
        check("D5 pvid field present in /vlanlist",
              "pvid" in (vl[0] if vl else {}) and before.get(1) is not None,
              "vlan1 pvid=%s" % before.get(1))
        # the mask uses logical port bits; map the physical port via /status.json
        logp = next((p["logPort"] for p in get_json(s, args.host, "/status.json")
                     if p.get("portNum") == args.port), args.port)
        mask_before = int(before.get(1) or "0", 16)
        mask_after = int(after.get(1) or "0", 16)
        bit = 1 << logp
        check("D5 pvid mask tracks pvid change",
              bool(mask_before & bit) and not (mask_after & bit),
              "vlan1 pvid mask %#x -> %#x (port %d logical %d bit %#x)"
              % (mask_before, mask_after, args.port, logp, bit))
    except Exception as e:
        check("D5 pvid mask tracks pvid change", False, str(e))
    finally:
        try:
            cmd(s, args.host, "pvid %d 1" % args.port)
        except Exception:
            pass

    # --- D8: /main.js arrives complete -------------------------------------
    # requests auto-decompresses Content-Encoding: gzip, so r.content is
    # the full script; a truncation would show up as a short/invalid body.
    try:
        r = s.get("http://%s/main.js" % args.host, timeout=90)
        body = r.content
        ok = r.status_code == 200 and len(body) > 50000 and b"function nav" in body
        check("D8 main.js arrives complete (gzip auto-decompressed)", ok,
              "%d bytes, nav() marker %s" % (len(body),
                                             "present" if b"function nav" in body else "missing"))
    except Exception as e:
        check("D8 main.js transfer", False, str(e))

    # ===================================================================
    # C group: config commit coverage and CLI validation
    # ===================================================================

    # --- C1: running-config serializes vlan/pvid/mtu plus the base --------
    try:
        cmd(s, args.host, "vlan 10 1 2")
        cmd(s, args.host, "pvid %d 2" % args.port)
        cfg = s.get("http://%s/running-config" % args.host, timeout=20).text
        ok = ("vlan 10 1 2" in cfg and "pvid %d 2" % args.port in cfg
              and "mtu " in cfg and "ip " in cfg and "netmask " in cfg
              and "web on" in cfg)
        check("C1 running-config has vlan/pvid/mtu + base", ok,
              "vlan10=%s pvid=%s mtu=%s base=%s"
              % ("vlan 10 1 2" in cfg, "pvid %d 2" % args.port in cfg,
                 "mtu " in cfg, "ip " in cfg and "web on" in cfg))
    except Exception as e:
        check("C1 running-config has vlan/pvid/mtu + base", False, str(e))
    finally:
        try:
            cmd(s, args.host, "vlan 10 d")
            cmd(s, args.host, "pvid %d 1" % args.port)
        except Exception:
            pass

    # --- C2/C7: storm-control pps flag is serialized and displayed --------
    try:
        cmd(s, args.host, "storm-control on multicast 1000p")
        cfg = s.get("http://%s/running-config" % args.host, timeout=20).text
        ok = "storm-control on multicast 1000p" in cfg
        check("C2 commit serializes the pps flag", ok,
              cfg.splitlines() and [l for l in cfg.splitlines() if l.startswith("storm-control")])
        storm = get_json(s, args.host, "/storm-control.json")
        pps = next((e.get("pps") for e in storm if e.get("type") == 1), None)
        check("C7 storm-control.json shows pps mode", pps == 1,
              "type=1 pps=%s" % pps)
    except Exception as e:
        check("C2/C7 storm pps", False, str(e))
    finally:
        try:
            cmd(s, args.host, "storm-control off all")
        except Exception:
            pass

    # --- C5: qos dscp parses multi-digit values; out-of-range is inert ----
    # The CLI rejects bad values by printing usage and doing nothing (the
    # HTTP /cmd still answers 200), so verify the map state is unchanged.
    try:
        qos0 = get_json(s, args.host, "/qos.json")
        old46 = qos0.get("dscp", [None] * 64)[46]
        cmd(s, args.host, "qos dscp 46 3")
        qos1 = get_json(s, args.host, "/qos.json")
        val = qos1.get("dscp", [None] * 64)[46]
        check("C5 qos dscp 46 3 maps dscp 46 -> queue 3", val == 3,
              "dscp[46]=%s" % val)
        cmd(s, args.host, "qos dscp 46 9")
        qos2 = get_json(s, args.host, "/qos.json")
        val2 = qos2.get("dscp", [None] * 64)[46]
        check("C5 out-of-range queue leaves the map unchanged", val2 == 3,
              "dscp[46]=%s (still 3)" % val2)
    except Exception as e:
        check("C5 qos dscp", False, str(e))
    finally:
        try:
            cmd(s, args.host, "qos dscp 46 %d" % (old46 if old46 is not None else 0))
        except Exception:
            pass

    # --- C6: LAG group out of range is inert (no register write) ----------
    try:
        lag0 = get_json(s, args.host, "/lag.json")
        cmd(s, args.host, "lag 4 1")
        lag1 = get_json(s, args.host, "/lag.json")
        check("C6 lag group 4 rejected without touching registers",
              lag0 == lag1, "lag.json unchanged")
    except Exception as e:
        check("C6 lag group 4 rejected without touching registers", False, str(e))

    # --- C12: bandwidth rate validation is inert on bad input -------------
    try:
        bw0 = get_json(s, args.host, "/bandwidth.json")
        p0 = next(p for p in bw0 if p.get("portNum") == args.port)
        cmd(s, args.host, "bw in %d 123" % args.port)       # odd hex digits
        cmd(s, args.host, "bw in %d 123456" % args.port)    # > 20 bit
        bw1 = get_json(s, args.host, "/bandwidth.json")
        p1 = next(p for p in bw1 if p.get("portNum") == args.port)
        check("C12 odd digits and >20bit rates rejected", p0 == p1,
              "port %d bandwidth unchanged" % args.port)
        cmd(s, args.host, "bw in %d 0100" % args.port)      # valid even rate
        bw2 = get_json(s, args.host, "/bandwidth.json")
        p2 = next(p for p in bw2 if p.get("portNum") == args.port)
        check("C12 valid even-digit rate accepted", p2.get("iLimited") == 1,
              "iLimited=%s iBW=%s" % (p2.get("iLimited"), p2.get("iBW")))
    except Exception as e:
        check("C12 bandwidth validation", False, str(e))
    finally:
        try:
            cmd(s, args.host, "bw in %d off" % args.port)
            cmd(s, args.host, "bw out %d off" % args.port)
        except Exception:
            pass

    # --- C1b: commit writes the config to flash ---------------------------
    # `commit` is MODE_PRIVILEGED (telnet only).  After it, /config must
    # contain the vlan/pvid lines; the boot replay of these lines is
    # covered by C1's serialize checks (execute_config feeds every line
    # through the same cmd_parser).
    try:
        cmd(s, args.host, "vlan 10 1 2")
        cmd(s, args.host, "pvid %d 2" % args.port)
        out = telnet_cmd(args.host, args.password, "commit")
        ok = "Config committed" in out
        check("C1b commit writes the config", ok, "Config committed" if ok else "no ack")
        cfg3 = s.get("http://%s/config" % args.host, timeout=20).text
        ok = ("vlan 10 1 2" in cfg3 and "pvid %d 2" % args.port in cfg3
              and "ip " in cfg3 and "web on" in cfg3)
        check("C1b saved config has vlan/pvid/base", ok,
              "vlan10=%s pvid=%s base=%s"
              % ("vlan 10 1 2" in cfg3, "pvid %d 2" % args.port in cfg3,
                 "ip " in cfg3 and "web on" in cfg3))
    except Exception as e:
        check("C1b commit persistence", False, str(e))
    finally:
        # leave the switch with the default config (erase the committed lines)
        try:
            cmd(s, args.host, "vlan 10 d")
            cmd(s, args.host, "pvid %d 1" % args.port)
            import io
            default_cfg = ("ip 192.168.10.247\ngw 192.168.10.1\n"
                           "netmask 255.255.255.0\ntelnet on\nweb on\n")
            s.post("http://%s/config" % args.host,
                   files={"configuration": ("config.txt", io.BytesIO(default_cfg.encode()),
                                            "application/octet-stream")}, timeout=20)
        except Exception:
            pass

    # --- C3: intentionally not tested via /cmd ----------------------------
    # `stp on` puts every user port into blocking (the upstream STP
    # implementation has no port state transitions), which isolates the
    # management port as well; the switch then needs a power cycle.
    # The C3 fixes (HTONS on the RTL tag flags, 1/256 s timers, CPU-port
    # mask, bounded stp_in) are wire-level and not verifiable over HTTP.

    # --- A7: /reset (optional, reboots the switch) -------------------------
    if args.reset_test:
        try:
            r = s.get("http://%s/reset" % args.host, timeout=10)
            check("A7 /reset returns 200", r.status_code == 200, "HTTP %d" % r.status_code)
        except Exception as e:
            check("A7 /reset returns 200", False, str(e))

    failed = [n for n, ok, _ in PASS if not ok]
    print()
    print("%d passed, %d failed" % (len(PASS) - len(failed), len(failed)))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
