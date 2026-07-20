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

## Current Status (HEAD `c1a4274`)

| Improvement | Status | Details |
|-------------|--------|---------|
| SFP diagnostics → `/sfp_diag.json` | ✅ Done | `page_impl.c:778-809`, `httpd.c:687-688` |
| Cache-Control on JSON | ❌ Not done | `HTTP_RESPONCE_JSON` has no Cache-Control header |
| VLAN N+1 elimination | ❌ Not done | `loadVlanTable()` still fetches per-VLAN (`main.js:512`) |
| EEE page pollStatus removal | ❌ Not done | `main.js:102` |
| Port Config poll optimization | ❌ Not done | `main.js:88` polls every 5s |
| Version | `v0.2.18` | Makefile line 1 |

---

## Recommended Plan

### Phase 1 (High Impact, Low Risk)

1. **VLAN N+1 fix** — Backend: `send_vlanlist()` to include `members` field. Frontend: `loadVlanTable()` to use inline data. Removes N serial requests per VLAN table render.
   - **Known issue:** PVID (`port_pvid_get()`) causes OSEG overflow during link. Include `members` only and default PVID to 0 on frontend.

2. **Cache-Control** — Add `Cache-Control: private, max-age=2` to `HTTP_RESPONCE_JSON`. Allows 2s browser caching for rapid page switches.

3. **Smart polling** — Remove `pollStatus()` from EEE page. Change Port Config to single fetch (no interval). Reduces unnecessary backend load.

### Phase 2 (Medium Impact)

4. **SFP diag lazy-load** — Fetch `/sfp_diag.json` only on Dashboard, and only when user hovers over an SFP port.

5. **ETag / If-None-Match** — Add conditional GET support to JSON endpoints. Backend complexity high, but would eliminate redundant transfers entirely.

### Cannot Do

- **Concurrent requests:** Backend global state (`outbuf`, `slen`) precludes parallel handling without major refactoring of uIP appstate.
- **HTTP keep-alive:** uIP does not support connection reuse.

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
