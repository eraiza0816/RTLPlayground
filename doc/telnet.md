# Telnet Access

The RTLPlayground firmware provides a Telnet server on port 23, allowing remote CLI access
over the network. This is useful for configuring the switch without a serial console connection.

## Enabling Telnet

Telnet is **disabled by default** for security. Enable it via the CLI or Web UI:

```
> telnet on
Telnet enabled
```

The setting can be saved to flash with `commit` to persist across reboots.

## Connecting

Connect to the switch IP address on port 23 using any Telnet client:

```
telnet 192.168.10.247
```

## Authentication

After connecting, you are prompted for a password:

```
Password:
```

The default password is `1234`. The password can be changed at runtime via the CLI
(`passwd <newpassword>`). Authentication does not lock the session on failure —
you are simply re-prompted.

Once authenticated, the CLI prompt `[hostname]> ` appears and you can enter any CLI command.
The console is a flat CLI in the default **Lite** build: there is no
`enable` / `configure terminal` mode system and no `?` / `help` / Tab
completion on the device. For command help, Tab completion, or an Arista
EOS-style interface, use `rtlpctl` interactively (`rtlpctl --host <ip>
--password <pw>`; `?` shows device commands, Tab completes). The **Full**
build (`make FULL=1`) provides the EOS-like mode hierarchy and
`?`/help/Tab completion on the device itself.

## Available Commands

All CLI commands listed in the [README](../README.md) are available via Telnet, including:

- `show` — Display current settings
- `commit` — Save current settings to flash
- `ip`, `gw`, `netmask` — Network configuration
- `hostname`, `passwd` — System configuration
- `port`, `vlan`, `lag`, `mirror`, `eee`, `bw` — Port and switching features
- `stp`, `igmp`, `syslog`, `sfp`, `stat`, `mtu` — Advanced features

## Implementation Notes

The Telnet server runs in BANK3 (0x34000) and uses the uIP TCP/IP stack.
Key implementation details:

- **Output buffering:** A 2048-byte ring buffer (`tx_buf[2048]`) collects CLI
  output. Data is transmitted during uIP poll intervals (every few
  milliseconds). Received data is buffered in a 256-byte `rx_buf[256]`.
- **IAC negotiation:** The server responds with WONT/DONT to all Telnet options,
  keeping the protocol simple. Local echo is handled by the client.
- **Authentication state:** Echo is suppressed (`telnet_echo = 0`) during password
  input so the password is not displayed on screen.
- **Newline handling:** `\n` is automatically preceded by `\r` for proper
  carriage-return behavior expected by Telnet clients.
