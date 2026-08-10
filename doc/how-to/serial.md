# How-to: Serial console and first power-up

Type: how-to · Task: connect to the switch console and boot it for the first time

## Connecting a serial interface

All devices expose a UART port.  Connect a serial cable and set the
terminal to **8N1 @ 115200 baud**.

## Powering up

When the switch is powered up it runs its initialisation and provides a
minimal console on the serial interface.  The boot messages and the
console prompt look like this:

```
Detecting CPU
RTL8373 detected
Starting up...
  Flash controller
NIC reset
rtl8372_init called
...
CPU detected: RTL8373
Clock register: 0x00001101
```

The console's command set is described in
[the CLI reference](../commands.md).

## Next steps

- The web interface is reachable at `http://192.168.10.247` by default
  (or the address from `config.txt`); the default password is `1234`.
- For day-to-day management use [rtlpctl](../../tools/rtlpctl) — it is
  the recommended way to operate the switch.
- See [Getting started](../tutorials/getting-started.md) for the first
  configuration steps.
