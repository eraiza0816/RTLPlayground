# rtlpctl — RTLPlayground CLI

`rtlpctl` is a command-line tool to control every feature of the RTLPlayground Web UI on RTL837x switches.

It supports both interactive mode and one-shot command mode, with optional JSON output.

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

### Command Validation

The firmware has deliberately minimal input validation to save code space
(commit 6a28c58), so `cmd` / `enc-cmd` command text is validated on the host
before it is sent. All known write commands are checked:

- `preshared_key` — key must be exactly 64 hex chars (the firmware no longer
  validates this and would silently store `0xff` bytes)
- `hostname` — 1-31 chars, letters/digits/`-`/`_` only (the firmware now
  truncates silently at the first invalid character)
- `passwd` — 5-20 chars
- `ip` / `gw` / `netmask` — dotted-quad IPv4 (`ip dhcp` allowed)
- `port`, `mtu`, `pvid`, `vlan`, `ingress`, `isolate`, `mirror`, `lag`,
  `laghash`, `eee`, `bw`, `stp`, `igmp`, `telnet`, `web`, `l2`, `sfp`,
  `regget`/`regset`/`sdsget`/`sdsset`/`phyget`/`physet`, `commit`, `reset`
  — argument counts, port numbers, VLAN IDs, speeds, hex values etc.

Unknown or read-only commands pass through unchanged, so future firmware
commands are never blocked. To send a command that fails validation anyway
(e.g. on an older firmware), use `--force`:

```bash
rtlpctl cmd "hostname my-switch.old-firmware" --force
```

### Commands

#### Read

| Command | Endpoint | Description |
|---------|----------|-------------|
| `status` | GET /status.json | Port status, link state, counters |
| `info` | GET /information.json | System info (IP, MAC, version etc.) |
| `vlan <vid>` | GET /vlan.json?vid=<vid> | VLAN details (members, name, PVID) (1-4094) |
| `vlan list` | GET /vlanlist | VLAN list |
| `counters <port>` | GET /counters.json?port=<port> | Port hardware counters (single digit 1-8) |
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
| `config upload <file>` | POST /config (multipart) | Upload configuration file |
| `upload firmware <file>` | POST /upload (multipart) | Firmware update |
| `reset` | GET /reset | Reboot the switch |

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

```bash
$ rtlpctl --host 192.168.1.1
rtlpctl: RTLPlayground CLI (connected to http://192.168.1.1)
Type 'help' for commands, 'exit' to quit.
rtlp> login 1234
OK
rtlp> status
Port  Name     Link   Enabled  TX Good  TX Bad  RX Good  RX Bad
1     Port 1   1G     yes      123456   0       654321   0
2     Port 2   down   no       0        0       0        0
...
rtlp> vlan 100
VLAN 100:
Members:  0x00060011
Name:     Default
PVID:     0x00000001
rtlp> cmd "ip 192.168.1.100"
OK
rtlp> exit
```

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
| `show interfaces counters [Ethernet<X>]` | GET /counters.json |
| `show running-config` | GET /config |
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
| `configure [terminal]` | Enter config mode |
| `copy running-config startup-config` | Save configuration |
| `write memory` | Save configuration |
| `clear logging` | Clear command log |
| `enable` | Privileged mode |

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
