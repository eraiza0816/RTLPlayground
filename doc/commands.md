# CLI Command Reference

This document describes all commands available on the serial console.
Commands that have a dedicated document are listed with a brief syntax
summary and a reference to the detailed description.

## Diagnostic and Info Commands

```
> version
  Prints the software version, build date and hardware (machine) name.
  Example output:
  Software version: v0.1.0-12c98ba-dirty
  Build date: Mar 15 2025
  Hardware: LIANGUO_ZX_SWTGW215AS

> time
  Prints the internal tick counter (32-bit, increments at ~10ms) and
  the hardware seconds counter register.

> stat
  Shows link state, speed, and packet counters (TxGood, TxBad, RxGood, RxBad)
  for all ports. (See README for example output.)

> sds
  Prints the current value of the SerDes mode register
  (RTL837X_REG_SDS_MODES, address 0x7b20).

> gpio
  Reads and prints the GPIO input registers (GPIO00-31 and GPIO32-63).
  Two values are shown per pin: the current value and the XOR with the
  previous reading, which highlights pins that have changed state since
  the last `gpio` command.

> history
  Prints the saved command history (wraps at 1024 bytes). Within a session,
  commands are saved to the history buffer after execution.

> rnd
  Generates a 48-bit hardware random number and prints it as 12 hex digits.
  The RLDP random-enable bit is toggled before each read to ensure a fresh
  value.

> flash s|j|u
  Reads flash metadata:
  s: Dumps the 3 security register regions (each 40 bytes at 0x1000, 0x2000,
     0x3000). Used on managed switches to identify the OEM build.
  j: Reads and prints the JEDEC ID of the flash chip.
  u: Reads and prints the 64-bit unique device ID of the flash chip
     (note: only the lower 4 bytes are typically correct).
```

## System Configuration

```
> reset
  Performs a software reset of the switch SoC. The boot process restarts
  and the serial console shows the boot log again.

> stp [on|off]
  Enables or disables the Spanning Tree Protocol.
  on:  Enables STP and calls stp_setup().
  off: Disables STP and calls stp_off().

> passwd <password>
  Changes the web interface login password (max 20 characters).
  The default password is `1234`. This can also be set in config.txt.

> syslog [on|off|ip [<ip-address>]]
  Controls syslog logging.
  Without arguments: prints whether syslog is enabled and the server IP.
  on:  Enables syslog and starts sending messages.
  off: Disables syslog.
  ip <address>: Sets the syslog server IP address. If syslog was running,
                it is restarted with the new address.
  Example:
  > syslog ip 192.168.1.100
  > syslog on
```

## Port Configuration

```
> port <port> show
  Shows the port name and PHY status (speed, duplex, link partner
  capabilities). For SFP ports, no PHY info is available.

> port <port> name <name>
  Assigns a custom name to a port (max 15 characters).

> port <port> [10m|100m|1g|2g5|5g|10g|auto|on|off]
  Forces the port speed.
  on:  Sets speed to auto-negotiation (same as `auto`).
  off: Disables the port.

> port <port> duplex [half|full]
  Forces the duplex mode. Only meaningful with 10m or 100m speeds;
  1G and above are always full-duplex.

> port <port> [10m|100m] [half|full]
  Combines speed and duplex in a single command.

> mtu [show|<port> <size>]
  Shows or sets the per-port maximum frame size (MTU).
  show: Prints MTU values for all ports.
  <port> <size>: Sets the MTU for a given port (max 16383).
  Example:
  > mtu 1 9000
  > mtu show

> isolate <port> [show|off|<port>...]
  Configures port isolation (which ports a given port may communicate with).
  <port>: The port to configure (physical port number).
  show:  Shows the current isolation mask for the port.
  off:   Removes all isolation restrictions (allows communication with all ports).
  <port>...: List of physical ports that the configured port may talk to.
  The CPU port can be specified as port 10.
  Examples:
  > isolate 1 2 3     Port 1 can only talk to ports 2 and 3
  > isolate 1 off     Remove all isolation from port 1

> eee [on|off|status] [<port>] [100m|1g|2g5]
  Controls Energy Efficient Ethernet (EEE).
  Without arguments or with `status`: prints EEE status for all ports
    or a specific port.
  on:  Enables EEE.
  off: Disables EEE.
  The optional speed argument selects which speed's EEE advertisements
  to enable (defaults to 2.5G or 10G depending on hardware).
  Example:
  > eee status
  > eee on 2g5

> bw [in|out|status] <port> [<hex-value>|off|drop|fc]
  Controls per-port bandwidth. See doc/bandwidth.md for details.

> mirror [status|off|<mirror-port> [<port>[r|t]]...]
  Configures port mirroring. See doc/mirroring.md for details.
```

## VLAN Configuration

```
> vlan <vlan-id> [<port>[t|u]...]
  Creates or updates a VLAN. See doc/vlan.md for details.

> vlan <vlan-id> d
  Deletes a VLAN. See doc/vlan.md for details.

> vlan <vlan-id> mgmt
  Restricts management access to the given VLAN. See doc/vlan.md for details.

> vlan show
  Dumps all VLAN table entries. See doc/vlan.md for details.

> pvid <port> <vlan-id>
  Assigns a PVID to a port. See doc/vlan.md for details.

> ingress [<mode>|<port><mode>...]
  Sets ingress VLAN filter mode. See doc/vlan.md for details.
  mode: u (untagged only), t (tagged only), a (any)
```

## L2 and IGMP

```
> l2
  Prints the L2 MAC address table (learned and static entries).
  See doc/l2.md for details.

> l2 forget
  Flushes all dynamically learned L2 entries. See doc/l2.md for details.

> igmp [on|off|show]
  Controls IGMP snooping. See doc/igmp.md for details.
```

## Link Aggregation

```
> lag <group> [<port>...]
  Configures a Link Aggregation Group. See doc/link_aggregation.md for details.

> lag show
  Shows all 4 LAG configurations. See doc/link_aggregation.md for details.

> laghash <group> [<hash-type>...]
  Sets the hash algorithm for a LAG. See doc/link_aggregation.md for details.
```

## Low-Level Register Access

```
> regget <hex-reg>
  Reads a 16-bit register address and prints its 32-bit value.
  <hex-reg>: register address as hex (e.g. 0BB0, 0c).
  Example:
  > regget 0BB0
  REGGET: 0BB0: VAL: 0000000a

> regset <hex-reg> <hex-val>
  Writes a 32-bit value to a 16-bit register address.
  <hex-val>: up to 8 hex digits.
  Example:
  > regset 0b abcd1234

> sdsget <sds-id> <hex-page> <hex-reg>
  Reads a SerDes register by SDS ID, page and register.
  <sds-id>: 0-based SDS index (0, 1, ...)
  <hex-page>: page number as hex (1 byte)
  <hex-reg>:  register number as hex (1 byte)
  Example:
  > sdsget 0 01 02

> sdsset <sds-id> <hex-page> <hex-reg> <hex-val>
  Writes to a SerDes register. <hex-val> is up to 4 hex digits (16-bit).
  Example:
  > sdsset 0 01 02 1234

> phyget <phy-id> <dev-id> <hex-reg>
  Reads a PHY register via Clause-45 (MMD).
  <phy-id>: PHY address (0-based)
  <dev-id>: MMD device address
  <hex-reg>: register address as hex (1 or 2 bytes)
  Example:
  > phyget 4 7 a610

> physet <phy-id> <dev-id> <hex-reg> <hex-val>
  Writes to a PHY register via Clause-45 (MMD).
  <hex-val>: up to 4 hex digits (16-bit).
  Example:
  > physet 4 7 a610 0020
```

## SFP Commands

```
> sfp
  Prints identification and sensor data for all inserted SFP modules
  (vendor, part number, serial, temperature, voltages, TX/RX power).
  See doc/sfp.md for details.

> sfp <slot> [100m|1g|2g5|10g|auto]
  Forces the SFP port speed. Available speeds depend on the module and
  the SerDes configuration.
  Example:
  > sfp 1 10g

> sfp <slot> dump
  Hex dump of the full 256-byte SFP EEPROM. See doc/sfp.md for details.

> sfp <slot> save|restore|fix|write|bulk
  SFP EEPROM write operations. See doc/sfp.md for details.
```
