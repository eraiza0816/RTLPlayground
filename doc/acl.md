# Ingress ACL

Type: reference · Feature: ingress ACL rules (MAC/VLAN/IP)

Access Control Lists filter incoming frames based on their destination
MAC address, VLAN tag or IP address.  A rule either **permits** or
**denies** matching frames on a port.

## Implementation

`rtl837x_acl.c` (BANK3) programs the RTL8373 ASIC's ingress ACL
engine.  The rule and action tables live in the ITA block shared with
the L2/VLAN tables (`RTL837X_TBL_CTRL` = 0x5cac), so every access waits
for the busy bit like the other table drivers.

The initial scope: one rule = one template.  Supported match fields:

- `mac aa:bb:cc:dd:ee:ff`   — template 0 (destination MAC)
- `vlan <id>`               — template 4 (CTAG VID, 1-4095)
- `ip <addr>[/<prefix>]`    — template 1 (destination IP, optional
  prefix 0-32)

Each rule is bound to a single physical port and has an action
(permit/deny).  The hardware compares the template field with a
configurable care mask (the prefix selects the significant bits for IP
rules).

> Note: on some boards the ACL engine is strap-disabled; rules still
> write and read back, but may not filter traffic.  Check the hardware
> documentation for the specific device.

## CLI

```
acl on|off
acl show
acl add <port> <permit|deny> <match>
acl del <idx>
```

Examples:

```
acl add 3 deny ip 192.168.1.99/32
acl add 1 permit mac aa:bb:cc:dd:ee:ff
acl add 5 deny vlan 5
acl del 0
```

Up to 96 rules (indices 0-95) are supported.

## WebUI

The "ACL" panel lists the rules and provides a form to add a new rule
(port, action, match type and value) plus a delete button per rule.

## JSON endpoint

`GET /acl.json` returns the rules:

```json
[{"idx":0,"tpl":1,"pmask":64,"action":1,"data0":"00000000","data1":"c0a80163"}]
```

`tpl` is the template (0=MAC, 1=IP, 4=VLAN), `pmask` the port bitmask,
`action` 0=permit / 1=deny, `data0`/`data1` the raw template words (IP
rules encode the address in `data1`).

## rtlpctl

```
rtlpctl acl add 3 deny ip 192.168.1.99/32
rtlpctl acl del 0
rtlpctl acl show
```

## Notes

- Rules are runtime state; use `commit` to persist them.
- The initial scope supports one rule per template index; the engine
  stores rule/action pairs in the shared ITA block, so adding more
  match fields is a table-programming extension.
