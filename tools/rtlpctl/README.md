# rtlpctl — RTLPlayground CLI

`rtlpctl` is a command-line tool to control every feature of the RTLPlayground Web UI on RTL837x switches.

It supports both interactive mode and one-shot command mode, with optional JSON output.

## Compatibility

`rtlpctl` is developed and tested against firmware **v0.2.23** (the `VERSION` variable in the top-level `Makefile`).

## Why a host-side CLI?

Ideally, strict input validation would live in the switch firmware.
However, the RTL837x firmware runs on a constrained 8051 core: its code
space is organised into 48 KB bank windows and its internal RAM is a
hard 128-byte limit.  A comprehensive validation layer in the firmware
would cost exactly the code space and computation budget that the
feature set is already consuming (internal RAM in particular — the
compiler's overlay segment is full, so even small parser helpers can
fail to link).

This tool therefore hosts the validation checks on the **client side**:

- every command is checked locally (argument counts, port numbers,
  VLAN IDs, MTU ranges, speeds, rates, MAC addresses, IP prefixes, hex
  values, ...) before it is sent to the switch
- `config upload` applies the same checks to every line of a
  configuration file before anything reaches the flash
- the firmware only has to acknowledge and execute, keeping its
  footprint small

Operating the switch **only through this tool** is recommended: it turns
"garbage reaches the registers" into "invalid input is refused before
it leaves the client", which is the safest way to change settings and
run diagnostics on a firmware that deliberately skips its own input
validation.

Because the validation lives on the client, it can be extended or
tightened at any time **without updating the switch firmware**: safer
checks are a client-side change only.  The same applies to new commands
and to configuration files — nothing about the device itself has to
change to operate it safely.

For configuration changes in particular, the pre-shared key provides
an extra layer: setting a PSK (`psk <hex64>` on the device, `--psk` on
the client) switches the switch into **PSK mode** (see
[doc/authentication.md](../../doc/authentication.md)):

- password logins are **rejected** on the switch while a PSK is set
- rtlpctl logs in with an **encrypted challenge** (`enc=<hex>`), so the
  PSK itself never leaves the client
- privileged operations (e.g. `commit`) use the encrypted `/enc`
  endpoint, so the commands travel over the wire encrypted and the
  settings changes cannot be read or tampered with in transit
- the telnet console keeps working with the password; it is recommended
  to disable it (`telnet off`) in PSK mode

## Build

```bash
cd tools/rtlpctl
go build -o rtlpctl .
```

Only Go standard library dependencies.

## Usage

```
rtlpctl [--host HOST] [--password PASS] [--json] <command> [args...]
```

### Global Flags

| Flag | Env var | Default | Description |
|------|---------|---------|-------------|
| `--host HOST` | `RTLP_HOST` | `192.168.1.1` | Switch IP address |
| `--password PASS` | `RTLP_PASSWORD` | — | Login password |
| `--env-file FILE` | — | `.env` | Load a .env file |
| `--mode MODE` | `MODE` | `default` | CLI mode (`default` / `arista`) |
| `--json` | — | — | Output raw JSON (with arista mode: EAPI JSON-RPC format) |
| `--force` | — | — | Bypass local command validation (`cmd` / `enc-cmd`) |

Global flags are only parsed **before** the command word; everything
from the command on is passed through untouched.  A command argument
that happens to start with `--` (e.g. `rtlpctl passwd --json` to set a
password literally) is therefore not consumed by the flag parser.

### Command Validation

The firmware has deliberately minimal input validation to save code space
, so `cmd` / `enc-cmd` command text is validated on the host
before it is sent. All known write commands are checked:

- `preshared_key` — key must be exactly 64 hex chars (the firmware no longer
  validates this and would silently store `0xff` bytes)
- `hostname` — 1-31 chars, letters/digits/`-`/`_` only (the firmware now
  truncates silently at the first invalid character)
- `passwd` — 5-20 chars
- `ip` / `gw` / `netmask` — dotted-quad IPv4 (`ip dhcp` allowed)
- `port`, `mtu`, `pvid`, `vlan`, `ingress`, `isolate`, `mirror`, `lag`,
  `laghash`, `eee`, `bw`, `stp`, `igmp` (incl. `querier`/`mld`), `lldp`,
  `storm-control`, `qos`, `acl`, `telnet`, `web`, `l2`, `sfp`,
  `regget`/`regset`/`sdsget`/`sdsset`/`phyget`/`physet`, `commit`, `reset`
  — argument counts, port numbers, VLAN IDs, speeds, rates, MAC addresses,
  IP prefixes, hex values etc.

Unknown or read-only commands pass through unchanged, so future firmware
commands are never blocked. To send a command that fails validation anyway
(e.g. on an older firmware), use `--force`:

```bash
rtlpctl cmd "hostname my-switch.old-firmware" --force
```

`config upload <file>` applies the same validation to **every non-empty
line** of the configuration file before anything is sent: the firmware
replays the file through its command parser without validating the
numbers itself (VLAN/MTU/PVID ranges etc.), so a bad line is rejected
locally with a `config line N: ...` error. Empty lines are skipped.

### Commands

#### Read

| Command | Endpoint | Description |
|---------|----------|-------------|
| `status` | GET /status.json | Port status, link state, counters |
| `info` | GET /information.json | System info (IP, MAC, version etc.) |
| `vlan <vid>` | GET /vlan.json?vid=<vid> | VLAN details (members, name, PVID) (1-4094) |
| `vlan list` | GET /vlanlist | VLAN list |
| `counters <port>` | GET /counters.json?port=<port> | Port hardware counters (single digit 1-9; omitted port lists all) |
| `eee` | GET /eee.json | EEE settings |
| `bandwidth` | GET /bandwidth.json | Bandwidth control settings |
| `mirror` | GET /mirror.json | Port mirroring configuration |
| `lag` | GET /lag.json | Link aggregation groups |
| `mtu` | GET /mtu.json | Per-port MTU settings |
| `sfp-diag` | GET /sfp_diag.json | SFP module diagnostics (DDM) |
| `l2 [idx]` | GET /l2.json?idx=<idx> | L2 forwarding table (decimal 0-4095) |
| `config` | GET /config | Current configuration (CLI format) |
| `cmd-log` | GET /cmd_log | Command history |

#### Write

| Command | Endpoint | Description |
|---------|----------|-------------|
| `login <password>` | POST /login | Authenticate |
| `cmd <text>` | POST /cmd | Execute a CLI command |
| `l2 delete <idx>` | GET /l2_del.json?idx=<idx> | Delete L2 entry (decimal 0-4095) |
| `cmd-log clear` | GET /cmd_log_clear | Clear command history |
| `config upload <file>` | POST /config (multipart) | Upload configuration file (validated line by line) |
| `upload firmware <file>` | POST /upload (multipart) | Firmware update |
| `reset` | GET /reset | Reboot the switch |

#### Console commands (output appears on the switch console)

The firmware's `/cmd` endpoint acknowledges execution only; the command
output is printed on the switch's serial console.  Commands in this
group send their device CLI text through `/cmd` (validated locally
first, see Command Validation):

| Command | Description |
|---------|-------------|
| `ping <ip>` | Send 4 ICMP echoes from the switch |
| `lldp [on\|off\|show]` | LLDP neighbor discovery |
| `igmp [on\|off\|show]` | IGMP snooping control |
| `igmp querier [on\|off\|show]` | ASIC IGMP/MLD querier |
| `igmp mld [on\|off\|show]` | MLD snooping control |
| `storm-control ...` | `on <type> <rate>[k\|p]`, `off [type\|all]`, `status` |
| `qos ...` | `on\|off\|status`, `mode pcp\|dscp\|both`, `pcp`, `dscp`, `sched` |
| `acl ...` | `on\|off`, `add <port> <permit\|deny> <match>`, `del <idx>`, `show` |
| `show arp` | Switch ARP cache |
| `hostname <name>` | Set the switch hostname |
| `passwd <new>` | Change the web/telnet password |
| `ip <a.b.c.d>\|dhcp` | Set the management IP (or use DHCP) |
| `gw <a.b.c.d>` | Set the default gateway |
| `netmask <a.b.c.d>` | Set the netmask |
| `port <n> ...` | Port config: `show`, `name`, `on`, `off`, `duplex`, `speed`, `auto` |
| `pvid <port> <vid>` | Set the port VLAN ID |
| `ingress [ports...]` | 802.1Q ingress filtering (`t` = tagged-only) |
| `isolate <port> [ports...]` | Port isolation; `<port> off` clears it |
| `laghash <hash> [fields]` | LAG hash (0-3) + `smac\|dmac\|spa\|sip\|dip\|sport\|dport` |
| `stp [on\|off\|show]` | Spanning-tree protocol |
| `telnet on\|off` | Enable/disable the telnet console |
| `web on\|off` | Enable/disable the web UI |
| `commit` | Save the running config to flash |
| `psk <hex64>` \| `psk off` | Set or clear the device preshared key (encrypted `/enc`). While a key is set, run `psk off` with `--psk <current-key>` (password logins are rejected in PSK mode) |
| `sfp ...` | SFP module control: speed, `describe`, `dump`, `save`, `restore`, `checksum [--fix]`, `fix`, `patch`, `clone`, `write <off> <val>`, `bulk <hex>` (all take `--pw <hex8>` where applicable) |
| `regget <addr>` | Read an RTL8370 register (hex) |
| `regset <addr> <hex>` | Write an RTL8370 register |
| `sdsget <bank> <page> <reg>` | Read a register via SDS access |
| `sdsset <bank> <page> <reg> <hex>` | Write a register via SDS access |
| `phyget <port> <addr> <reg>` | Read a PHY register |
| `physet <port> <addr> <reg> <hex>` | Write a PHY register |

`show running-config` and `show startup-config` (and the Arista
`show running-config` / `sh run`) fetch their text over HTTP
(`/running-config` resp. `/config`) and print it locally.

### Examples

```bash
# Authenticate and show port status
rtlpctl --host 192.168.1.1 --password 1234 status

# JSON output
rtlpctl --host 192.168.1.1 --password 1234 --json info

# Use environment variables
export RTLP_HOST=192.168.1.1
export RTLP_PASSWORD=1234
rtlpctl status
rtlpctl vlan list

# Load credentials from .env file
echo -e "RTLP_HOST=192.168.1.1\nRTLP_PASSWORD=1234" > .env
rtlpctl status
# .env is automatically loaded from the current directory
# --env-file can specify an alternate path

# Execute CLI commands (change settings)
rtlpctl cmd "ip 192.168.1.100"
rtlpctl cmd "vlan 100 1t 2t"

# Delete L2 entry (decimal index)
rtlpctl l2 delete 16

# Firmware update
rtlpctl upload firmware rtlplayground.bin

# Help
rtlpctl --help
```

### Interactive Mode

Start without arguments to enter interactive mode.

The prompt shows the device hostname when it can be fetched
(`rtlpctl@<hostname>> ` / `rtlpctl@<hostname># ` in arista mode); without
login credentials or when the device is unreachable it falls back to
`rtlp> `.

```bash
$ rtlpctl --host 192.168.1.1
rtlpctl: RTLPlayground CLI (connected to http://192.168.1.1)
Type 'help' for commands, 'exit' to quit. Tab completes, '?' shows device help.
rtlpctl@sw1> login 1234
OK
rtlpctl@sw1> status
Port  Name     Link   Enabled  TX Good  TX Bad  RX Good  RX Bad
1     Port 1   1G     yes      123456   0       654321   0
2     Port 2   down   no       0        0       0        0
...
rtlpctl@sw1> vlan 100
VLAN 100:
Members:  0x00060011
Name:     Default
PVID:     0x00000001
rtlpctl@sw1> cmd "ip 192.168.1.100"
OK
rtlpctl@sw1> exit
```

The firmware console has no `?`/`help`/Tab completion; in interactive mode
**rtlpctl provides them instead** (a TTY is required):

- `?` alone lists all device commands; `sfp ?` lists the sub-commands of a
  command (`?` after a space shows context help)
- **Tab** completes the current word: device commands, sub-commands
  (`port 1<tab>` → `port 10m`...), and in arista mode the EOS vocabulary
  (`show int<tab>` → `show interfaces`)
- **Arrow keys**: up/down recall the command history and re-run an entry
  (the line being edited is preserved), left/right move the cursor

The following internal commands are available in interactive mode:

| Command | Description |
|---------|-------------|
| `host [IP]` | Show/change the target IP |
| `password [PWD]` | Set the password |
| `exit` / `quit` | Exit |
| `help` | Show help |
| `mode [arista\|default]` | Switch CLI mode |

## Arista EOS Mode

Use `--mode arista` or the environment variable `MODE=arista` for Arista EOS-compatible CLI mode.

### Arista Command Mapping

| Arista command | Internal endpoint |
|---------------|-------------------|
| `show interfaces status` | GET /status.json |
| `show interfaces Ethernet<X> status` | GET /status.json (port filtered) |
| `show interfaces counters [Ethernet<X>]` | GET /counters.json | Without a port, loops Et1-Et9 |
| `show running-config` | GET /running-config |
| `show startup-config` | GET /config |
| `show vlan` | GET /vlanlist |
| `show vlan id <vid>` | GET /vlan.json |
| `show inventory` | GET /information.json |
| `show mac address-table` | GET /l2.json |
| `show logging` | GET /cmd_log |
| `show port-channel` | GET /lag.json |
| `show monitoring` | GET /mirror.json |
| `show queue` | GET /bandwidth.json |
| `show system` | GET /information.json |
| `show mtu` | GET /mtu.json |
| `show config` | GET /config |
| `show lldp neighbors` | console `lldp show` |
| `show ip igmp snooping` | console `igmp show` |
| `show ip igmp snooping querier` | console `igmp querier show` |
| `show ip igmp snooping groups` | console `igmp mld show` |
| `show qos` | console `qos status` |
| `show storm-control` | console `storm-control status` |
| `show ip access-lists` | console `acl show` |
| `ping <ip>` | console `ping <ip>` |
| `lldp enable` / `lldp disable` | console `lldp on` / `lldp off` |
| `ip igmp snooping enable` / `disable` | console `igmp on` / `igmp off` |
| `storm-control <type> level <rate>` | console `storm-control on <type> <rate>` |
| `hostname <name>` | console `hostname <name>` |
| `ip address <a.b.c.d>[/prefix]` | console `ip <a.b.c.d>` + `netmask` |
| `ip address dhcp` | console `ip dhcp` |
| `ip default-gateway <ip>` / `ip route 0.0.0.0/0 <ip>` | console `gw <ip>` |
| `username <name> [secret\|password] <pw>` | console `passwd <pw>` |
| `spanning-tree mode <mode>` | console `stp on` |
| `vlan <id>` / `vlan <id> name <name>` | console `vlan <id>` / `vlan <id> <name>` |
| `interface Ethernet<X> speed <speed>` | console `port <X> <speed>` |
| `interface Ethernet<X> duplex <half\|full>` | console `port <X> duplex ...` |
| `interface Ethernet<X> switchport access vlan <id>` | console `pvid <X> <id>` |
| `interface Ethernet<X> mtu <size>` | console `mtu <X> <size>` |
| `interface Ethernet<X> description <name>` | console `port <X> name <name>` |
| `interface Ethernet<X> shutdown` | console `port <X> off` |
| `no spanning-tree mode` | console `stp off` |
| `no vlan <id>` | console `vlan <id> d` |
| `no storm-control <type> level` | console `storm-control off <type>` |
| `no lldp` / `no ip igmp snooping` | console `lldp off` / `igmp off` |
| `no interface Ethernet<X> shutdown` | console `port <X> on` |
| `telnet server enable` / `disable` (or `telnet on\|off`) | console `telnet on` / `telnet off` |
| `web server enable` / `disable` (or `web on\|off`) | console `web on` / `web off` |
| `commit` | console `commit` |
| `pvid <port> <vid>` | console `pvid <port> <vid>` |
| `no pvid <port>` | console `pvid <port> 1` |
| `isolate <port> [ports...]` | console `isolate ...` |
| `no isolate <port>` | console `isolate <port> off` |
| `ingress [ports...]` | console `ingress ...` |
| `laghash <hash> [fields...]` | console `laghash ...` |
| `sfp ...` | console `sfp ...` |
| `regget` / `regset` / `sdsget` / `sdsset` / `phyget` / `physet` | console passthrough |
| `preshared-key <hex64>` (or `psk <hex64>`) \| `psk off` | console `preshared_key <hex64>` / `preshared_key` (clear) |
| `configure [terminal]` | Enter config mode |
| `copy running-config startup-config` | Save configuration |
| `write memory` | Save configuration |
| `clear mac address-table dynamic` | Flush learned MACs (`l2 forget`) |
| `clear logging` | Clear command log |
| `enable` | Privileged mode |

Storm-control types map to the device: `broadcast`, `multicast`,
`unknown-unicast` (device `dlf`), `unknown-multicast` (device
`unknown-mcast`).  "Console" commands are sent through `/cmd`; their
output appears on the switch's serial console.

> **Note:** `copy running-config startup-config` / `write memory` save the
> configuration via the encrypted `/enc` endpoint and therefore require a
> pre-shared key (`--psk <64-hex>`, matching the PSK configured on the
> switch). Without a PSK the command prints a hint instead of silently
> doing nothing.

### EAPI JSON-RPC Output

Combining `--mode arista --json` produces output in Arista eAPI-compatible JSON-RPC format.

```bash
rtlpctl --host 192.168.1.1 --password 1234 --mode arista --json show interfaces status
```

Example output:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": [
    {
      "encoding": "json",
      "output": {
        "interfaces": {
          "Et1": {
            "name": "Port 1",
            "linkStatus": "connected",
            "speed": "1g",
            "duplex": "full",
            "enabled": true
          }
        }
      }
    }
  ]
}
```

Text-based commands return with `encoding: "text"`:

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "result": [
    {
      "encoding": "text",
      "output": "! RTLPlayground configuration\nip 192.168.1.1\n..."
    }
  ]
}
```

Unknown commands return in EAPI error format:

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "error": {
    "code": -1,
    "message": "% Unknown command: show foo"
  }
}
```

### Arista Mode Examples

```bash
# One-shot
rtlpctl --host 192.168.1.1 --password 1234 --mode arista show interfaces status
rtlpctl --host 192.168.1.1 --password 1234 --mode arista show vlan

# EAPI JSON output
rtlpctl --host 192.168.1.1 --password 1234 --mode arista --json show mac address-table

# Interactive mode
rtlpctl --host 192.168.1.1 --password 1234
rtlp# show interfaces status
Port   Name     Status       Vlan  Duplex  Speed  Type
Et1    Port 1   connected    1     full    1G     10/100/1000BaseTX
Et2    Port 2   notconnect   1     full    1G     ...
...

rtlp# configure terminal
rtlp(config)# exit
rtlp# exit

# Mode via .env
echo -e "MODE=arista\nRTLP_PASSWORD=1234" > .env
rtlpctl show interfaces status
```

## Tests

```bash
# Unit tests only (fast)
go test -short ./...

# All tests (including binary integration tests)
go test ./...
```

Tests cover:
- Argument parsing (`splitArgs`, `filterFlags`)
- Output formatting (`fmtLink`, `fmtBool`, `fmtInt`, `fmtStr`, table formatting)
- Command validation (`validateCmdText` and per-command validators)
- HTTP client (authentication, all endpoints, error handling)
- Binary E2E (actual binary execution against httptest server)
- Interactive mode
