# Ping (ICMP echo)

The switch itself can send ICMP echo requests to a user-specified IP
address and report round-trip time statistics.  This is useful for
diagnosing the network from the switch's point of view (e.g. "can the
switch reach the gateway?").

## Implementation

`ping.c` (BANK3) implements a small ICMP echo sender:

- sends **4 echo requests** (see `PING_COUNT`) to the target
- resolves the destination MAC through the existing `uip_arp_out()`
  path: while the ARP entry is unknown, the pending echo is replaced by
  an ARP request and retried on the next tick
- reports RTT statistics: min / max / sum (the WebUI shows min, average
  and max)
- every attempt times out after 2 seconds (`PING_TIMEOUT`); a failed
  ARP resolution also times out instead of blocking the sender

The echo request and the reply parsing are done in software; the switch
ASIC's own L2 forwarding handles the rest of the packet path.

## CLI

```
ping <ip>
```

## WebUI

System Settings → Console tab: a "Ping" input field starts a ping and
polls the live result (sent/received counts and RTT) via `/ping.json`.

## JSON endpoint

`GET /ping.json` returns the current/last ping state:

```json
{"state":0,"dst":"192.168.10.100","sent":4,"rcvd":4,"min_rtt":5,"max_rtt":30,"sum_rtt":45}
```

`state` is 1 while a ping is running and 0 when idle.  When nothing was
received, `min_rtt` reports 0 (the internal no-reply sentinel 0xffff is
never exposed).

## rtlpctl

```
rtlpctl ping <ip>
```

sends the ping; the output appears on the switch's serial console (the
`/cmd` endpoint only acknowledges execution).  The live result can be
read back with `curl /ping.json`.

## Notes

- The echo sender runs in the main loop, so a running ping briefly
  shares the CPU with the web server; the WebUI shows the progress
  while it runs.
