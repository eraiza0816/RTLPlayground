# LLDP (IEEE 802.1AB)

Type: reference · Feature: LLDP (IEEE 802.1AB) neighbor discovery

Link Layer Discovery Protocol lets the switch advertise itself to
neighboring switches/endpoints and learn the neighbors it is connected
to.  The firmware sends an LLDPDU from every user port and stores the
received neighbor information in a small table.

## Implementation

`lldp.c` (BANK3):

- sends an LLDPDU from every user port every **30 seconds**
  (dest MAC `01:80:c2:00:00:0e`, ethertype `0x88cc`)
- learns neighbors from received LLDPDUs into a small neighbor table
  (`struct lldp_neighbor`)
- frame handling follows the STP path: received frames arrive with
  eth(14) + rtl_tag(8) + vlan_tag(4) headers (the LLDPDU starts at
  `uip_buf[26]`)

LLDP frames use a destination MAC that switches must not forward, so
the ASIC's normal L2 path handles the frame classification; the CPU
traps the LLDP ethertype.

## CLI

```
lldp on|off|show
```

- `lldp on` / `lldp off` — enable/disable LLDP operation
- `lldp show` — print the learned neighbor table to the console

## WebUI

The neighbor table is exposed read-only through `/lldp.json`; the
console output appears on the switch's serial console.

## JSON endpoint

`GET /lldp.json` returns the learned neighbors:

```json
[]
```

or, with neighbors:

```json
[{"port":1,"chassis_id":"...","system_name":"...","port_id":"..."}]
```

## rtlpctl

```
rtlpctl lldp on|off|show
```

## Notes

- The neighbor table has a fixed number of slots; when it is full the
  oldest entry is reused.
- LLDP is disabled by default; enable it with `lldp on` or via the
  configuration file (`commit` saves the setting).
