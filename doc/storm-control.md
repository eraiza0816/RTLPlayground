# Storm Control

Storm control limits the rate of broadcast, multicast and unknown
unicast frames on the ingress side, protecting the switch and the
connected devices from frame storms (e.g. a misbehaving device flooding
broadcasts).

## Implementation

`rtl837x_storm.c` (BANK3) uses the extended storm-control path of the
RTL8373 ASIC:

- `CFG_STORM_EXT` (0x5514) + the storm extension meter index
  `STORM_EXT_MTRIDX` (0x5518)
- one shared meter per storm type:
  - BC (broadcast)      = meter 0
  - MC (multicast)      = meter 1
  - unk-UC (DLF)        = meter 2
  - unk-MC              = meter 3
- the shared meter rate is a 24-bit value in **kbps** (mode bit 0) or
  **pps** (mode bit 1); the burst is a 28-bit value (max = unlimited)

Register layouts were taken from the RTL8373 SDK
(`dal_rtl8373_storm.c` / `dal_rtl8373_sharemeter.c`).  The limit applies
to all user ports collectively (a shared meter), so the rate is the
total allowed for the switch.

All state lives in XDATA; the 8051 internal RAM overlay is full in this
firmware, so the driver adds no new function parameters or locals in
internal RAM.

## CLI

```
storm-control on <broadcast|multicast|dlf|unknown-mcast> <rate>[k|p]
storm-control off [broadcast|multicast|dlf|unknown-mcast|all]
storm-control status
```

- `on <type> <rate>` — enable the storm limit.  The rate suffix `k`
  means kbps (default), `p` means pps.  The maximum rate is 10,000,000.
- `off [type|all]` — disable the limit for a type (or all types)
- `status` — show the current limits

## WebUI

A "Storm Control" panel lists all four storm types with an enable
checkbox, a rate input and a kbps/pps selector per type.

## JSON endpoint

`GET /storm-control.json` returns the current configuration:

```json
[{"type":0,"en":0,"rate":"00000000","pps":0},
 {"type":1,"en":0,"rate":"00000000","pps":0}, ...]
```

`type` is 0..3 (broadcast, multicast, dlf, unknown-mcast), `en` the
enable state, `rate` the 24-bit rate in hex, `pps` the mode (1 = pps).

## rtlpctl

```
rtlpctl storm-control on broadcast 100k
rtlpctl storm-control off all
rtlpctl storm-control status
```

## Notes

- The rate is a *shared* meter across all ports: a 100k limit means 100
  kbps in total, not per port.
- The setting is saved with `commit` and restored from the
  configuration file on boot.
