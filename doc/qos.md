# QoS (802.1p priority handling)

Quality of Service lets the switch prioritise traffic based on the
802.1p PCP field (VLAN tag priority) and/or the IP DSCP field, and
selects the egress scheduling per port.

## Implementation

`rtl837x_qos.c` (BANK3) drives the RTL8373 ASIC's priority engine.
The initial scope:

- `qos on` / `qos off` — enable/disable the priority decision weight
  tables
- priority decision mode: **PCP** (802.1p), **DSCP** or **both**
- PCP → queue mapping (all ports): 8 PCP values (0-7) mapped to 8
  queues
- DSCP → internal priority remap: 64 DSCP values (0-63); with the
  identity priority→queue mapping this equals DSCP → queue
- per-port egress scheduling: **strict priority** or **WFQ** (weighted
  fair queueing) with a configurable weight

The ASIC performs the classification and queueing in hardware; the
firmware only programs the tables.

## CLI

```
qos on|off
qos mode pcp|dscp|both
qos pcp <0-7> <queue>
qos dscp <0-63> <queue>
qos sched <port> strict|wfq [weight]
qos status
```

## WebUI

The "QoS" panel shows:

- the priority decision mode selector (Off / PCP / DSCP / PCP+DSCP)
- the PCP → queue table (8 selectors)
- the DSCP → queue table (64 values, edited in 8-column groups)
- the per-port scheduling summary

## JSON endpoint

`GET /qos.json` returns the current configuration:

```json
{"mode":0,
 "pcp":[0,1,2,3,4,5,6,7],
 "dscp":[0,0,0,...],       // 64 entries
 "sched":["S1S1S1S1S1S1S1S1", ...]}  // per port
```

`mode` is 0=off, 1=pcp, 2=dscp, 3=both.  `sched` strings encode the
queue scheduling of each port (`S1` = strict for queue 1, `W1` = WFQ
with weight 1, ...).

## rtlpctl

```
rtlpctl qos mode dscp
rtlpctl qos pcp 3 1
rtlpctl qos dscp 46 7
rtlpctl qos sched 5 wfq 16
rtlpctl qos status
```

## Notes

- QoS settings are runtime state; use `commit` to save them to the
  configuration file.
- The initial scope intentionally keeps one rule per table; extending
  the tables (e.g. per-port PCP mappings) is possible through the same
  ASIC registers.
