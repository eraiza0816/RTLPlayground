# Building without the Web UI

The `WEB` build option controls whether the Web UI (HTML, JS, CSS) is embedded
in the firmware. The HTTP server and JSON API endpoints are always included;
only the static web assets are excluded.

## Quick start

```bash
make WEB=0 MACHINE=SWGT024_V2_0_MANAGED
```

Output: `output/<MACHINE>/rtlplayground-<version>-<commit>-<MACHINE>.bin`

## Option values

| `WEB` | Result |
|-------|--------|
| `1` (default) | Full firmware with Web UI, JSON API and telnet |
| `0` | CLI + JSON API + telnet (no Web UI) |

## What changes with WEB=0

| Feature | WEB=1 | WEB=0 |
|---------|-------|-------|
| HTTP server (port 80) | included | included (JSON API only) |
| JSON API endpoints | included | included |
| HTML/JS/CSS files | embedded (~95 KB) | **removed** |
| `web on` / `web off` commands | available | available (toggles HTTP API) |
| `xmodem` firmware update | — | **added** |
| telnet server (port 23) | included | included (dispatched via httpd) |

## Internal details

When `WEB=0` is set, the compiler flag `-DNO_WEBUI` is added and
`html_data.c` (the WebUI static file index) is excluded from the build.

In `httpd/httpd.c`, the `NO_WEBUI` symbol guards:

- `#include "html_data.h"`
- `extern` declarations for `f_data[]` / `mime_strings[]`
- `find_entry()` — returns `0xff` (stub) under `NO_WEBUI`
- The static file serving branch in `httpd_appcall()`

The HTTP server and JSON API endpoints remain fully functional; only static
file delivery is disabled.

### uIP integration

`uip-conf.h` always includes `httpd.h`. `httpd_appcall()` inspects `lport`
and dispatches telnet (23) and HTTP (80) as a unified appcall dispatcher.

## Code space impact

| Bank | WEB=1 | WEB=0 |
|------|-------|-------|
| BANK1 | — | **~19 KB freed** (WebUI removed) |
| BANK2 | — | unchanged |
| BANK3 | — | unchanged |

The freed space in BANK1 can be used for additional CLI features.

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
