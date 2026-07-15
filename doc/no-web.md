# Building without the Web Interface

The `WEB` build option controls whether the web interface (HTTP server, JSON API,
embedded HTML/JS files) is included in the firmware.

## Quick start

```bash
make WEB=0 MACHINE=SWGT024_V2_0_MANAGED
```

Output: `output/<MACHINE>/rtlplayground-<version>-<commit>-<MACHINE>.bin`

## Option values

| `WEB` | Result |
|-------|--------|
| `1` (default) | Full firmware with web UI |
| `0` | CLI-only firmware, no web |

## What changes with WEB=0

| Feature | WEB=1 | WEB=0 |
|---------|-------|-------|
| HTTP server (port 80) | included | removed |
| JSON API endpoints | included | removed |
| HTML/JS/CSS files | embedded (~95 KB) | removed |
| `web on` / `web off` commands | available | removed |
| `xmodem` firmware upload | — | **added** |
| `passwd` command | web + telnet password | telnet password only |

## Code space impact

| Bank | WEB=1 | WEB=0 |
|------|-------|-------|
| BANK1 | 40,800 / 49,152 (83%) | **21,650 / 49,152 (44%)** |
| BANK2 | 45,099 / 49,152 (92%) | **44,777 / 49,152 (91%)** |
| BANK3 | 11,365 / 49,152 (23%) | **12,166 / 49,152 (25%)** |

The ~19 KB freed in BANK1 can be used for additional CLI features.

## Firmware update via serial (XMODEM)

With `WEB=0`, the `xmodem` command is available for firmware updates over UART.

1. Connect via serial terminal (e.g. Teraterm, PuTTY, minicom)
2. Type `xmodem` and press Enter
3. From the terminal program, start an XMODEM send of the firmware `.bin` file
4. The switch writes the image to flash and reboots automatically

```text
> xmodem
XMODEM: waiting for transfer... (cancel: Ctrl+X)
[receiving...]
Transfer OK. Rebooting...
```
