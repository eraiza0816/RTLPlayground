# WebUI Performance Investigation

## Overview

WebUIの描画速度低下の原因調査レポート。
フロントエンド（`html/main.js`）、バックエンド（`httpd/httpd.c`, `httpd/page_impl.c`）、およびネットワークスタック（uIP）の各層でボトルネックを特定した。

---

## Architecture

### Frontend
- Vanilla JS (no framework), single `main.js` (~53KB)
- HTTP client: `XMLHttpRequest` with serialized queue (`fetchAPI`)
- Polling: 5s intervals for `/status.json`, `/information.json`; 2s for `/l2.json`
- No virtual DOM, no state management, no performance instrumentation

### Backend
- MCU: 8051 (SDCC), uIP TCP/IP stack
- HTTP server: single-threaded, global state (`outbuf`, `slen`, `o_idx`)
- JSON built character-by-character into `outbuf` (2500 bytes)
- uIP MSS: ~1460 bytes (`UIP_BUFSIZE=2000`)

---

## Bottleneck Analysis

### 1. Serialized Request Queue (Frontend)

`fetchAPI()` (`main.js:17-47`) processes requests strictly one-at-a-time via `reqQ`. Each completed request is followed by a 50ms gap (`main.js:42`).

**Page load request chains (all serial):**

| Page | Requests in sequence |
|------|---------------------|
| Dashboard | `/status.json` → `/information.json` |
| Port Config | `/status.json` → `/mtu.json` → `/config` |
| VLAN | `/vlanlist` → `/vlan.json?vid=X₁` → `/vlan.json?vid=X₂` ... (**N+1 problem**) |
| L2 Table | `/config` → `/l2.json?idx=0` → `/l2.json?idx=30` ... |
| System | `/information.json` → `/config` |
| EEE | `/status.json` → `/eee.json` |

**Impact:** Each request requires a full TCP connection (no keep-alive) + backend processing + JSON generation. With N VLANs, VLAN page requires N+1 sequential round-trips.

**Limitation:** Cannot parallelize requests because the backend uses global variables (`outbuf`, `slen`, `o_idx` in `httpd.c:34-39`). Concurrent requests would corrupt each other's state.

### 2. Heavy Backend Processing per Request

#### `/status.json` (`page_impl.c:671-776`)
- Iterates all ports (up to 28)
- Per non-SFP port: multiple PHY MMD register reads (`page_impl.c:735-750`)
- Per SFP port: I2C register reads (vendor, model, serial, LOS) (`page_impl.c:716-729`)
- Counter reads for Tx/Rx Good/Bad packets via hardware register access (`page_impl.c:775-791`)
- **Note:** SFP diagnostics (temperature, Vcc, Tx bias, power) were split to `/sfp_diag.json` in commit `1d34eff`

#### `/counters.json` (`page_impl.c:325-343`)
- Reads 55 MIB hardware counters sequentially via STAT_GET macro
- Each counter requires register read + hex conversion

#### `/vlan.json?vid=X` (`page_impl.c:297-322`)
- Reads VLAN register via `vlan_get()`
- Iterates all ports for PVID calculation: `port_pvid_get()` per port

### 3. No Caching

- Every GET request appends `_t=<timestamp>` cache-buster (`main.js:32`)
- JSON responses carry no `Cache-Control` header (`page_impl.c:51`)
- Result: every poll = full backend processing, even when data hasn't changed

### 4. TCP Buffering Constraints

- `TCP_OUTBUF_SIZE = 2500` bytes (`rtl837x_common.h:37`)
- uIP MSS ≈ 1460 bytes (`UIP_BUFSIZE=2000 - headers`)
- JSON exceeding MSS is sent in segments, each requiring ACK before next (`httpd.c:593-612`)
- `/status.json` for 24 ports with SFP vendor/model/serial can exceed MSS → minimum 2 TCP round-trips

### 5. Unnecessary Polling

- **EEE page** (`main.js:102`): fetches `/status.json` every 5s, but EEE configuration uses only `/eee.json` data
- **Port Config page** (`main.js:88`): polls `/status.json` every 5s for link status column; link status changes slowly and doesn't need frequent updates

---

## Current Status (v0.2.21)

| Improvement | Status | Details |
|-------------|--------|---------|
| SFP diagnostics → `/sfp_diag.json` | ✅ Done | `page_impl.c` |
| Cache-Control on JSON | ✅ Done | `HTTP_RESPONCE_JSON` = `Cache-Control: private, max-age=1` |
| VLAN N+1 elimination | ✅ Done | `send_vlanlist()` includes `members`; `loadVlanTable()` uses inline data |
| EEE page pollStatus removal | ✅ Done | `nav('eee')` fetches status once, no interval |
| Port Config poll optimization | ✅ Done | `nav('port')` fetches status once, no interval |
| L2 table diff-update | ✅ Done | `fillL2()` updates rows only on change; fast walk then 10s refresh |
| Tab-hidden polling pause | ✅ Done | `visibilitychange` clears intervals while hidden |
| SFP diag throttle | ⚠️ Partial | Fetched only on Dashboard, throttled to 15s (hover-lazy not implemented) |
| File-corruption mitigation | ✅ Done | Scripts load sequentially after `DOMContentLoaded` + favicon suppressed (see below) |
| Version | `v0.2.21` | Makefile line 1 |

### File-corruption root cause (fixed)

The browser preloads `<script src>` resources in parallel with the HTML
document. Because the backend serves responses from a single shared buffer
(`outbuf`/`slen`), concurrent connections corrupted file transfers (truncated
or spliced JS → `t is not defined`, `nav is not a function`). Fixed in the
frontend by loading `i18n.js`/`main.js` sequentially after `DOMContentLoaded`
and suppressing the favicon request, so the device never serves two responses
at once.

---

## Recommended Plan (implementation status)

### Phase 1 (High Impact, Low Risk) — ✅ all done

1. **VLAN N+1 fix** — ✅ Done. `send_vlanlist()` includes `members`; `loadVlanTable()` uses inline data.
   - **Known issue:** PVID (`port_pvid_get()`) causes OSEG overflow during link. Members only are inlined; PVID defaults to 0 on the frontend.

2. **Cache-Control** — ✅ Done. `Cache-Control: private, max-age=1` (doc originally suggested `max-age=2`; `1` was chosen for fresher polls). The frontend `_t` cache-buster was removed so the cache is effective.

3. **Smart polling** — ✅ Done. `pollStatus()` removed from EEE page; Port Config uses a single fetch.

### Phase 2 (Medium Impact)

4. **SFP diag lazy-load** — ⚠️ Partial. `/sfp_diag.json` is fetched only while the Dashboard is active and throttled to every 15s. Hover-only fetching is not implemented.

5. **ETag / If-None-Match** — ❌ Not implemented. Replaced by short `max-age=1` caching and removal of the cache-buster.

### Cannot Do

- **Concurrent requests:** Backend global state (`outbuf`, `slen`) still precludes parallel handling without refactoring uIP appstate. Mitigated on the frontend by loading scripts sequentially after `DOMContentLoaded` and suppressing the favicon request (no concurrent requests in normal use).
- **HTTP keep-alive:** uIP does not support connection reuse. Every request is a full TCP connection.

---

## Measurement Guide

To quantify improvements, add timing instrumentation:

```js
// In fetchAPI() success handler:
console.timeEnd(url);  // measure round-trip
// Or use performance.now() for high-res timing
```

Browser DevTools Network tab provides:
- **Waterfall**: shows serialization of requests
- **TTFB**: backend processing time per endpoint
- **Content Download**: JSON payload size impact

Key measurements to take:
1. VLAN page: count `/vlan.json?vid=X` requests vs total VLANs (confirms N+1)
2. `/status.json` TTFB: time spent in PHY/I2C register reads
3. Page switch time: time from `nav()` call to last DOM update
