# CLI Variants: Lite and Full

Type: reference · Feature: the Lite and Full console CLI variants

The firmware ships with **two console CLI variants** selected at build time by
the `FULL` make variable (the same mechanism as the `WEB` WebUI switch).
The default build is **Lite**; CI builds all four combinations
(WEB/CLI × Lite/Full).

| | Lite (default) | Full |
|---|---|---|
| Build | `make` | `make FULL=1` |
| Console | Flat CLI — every command directly from the prompt | Legacy EOS-style mode hierarchy |
| Modes (`enable` / `configure terminal` / `exit` / `end`) | not present | present |
| `?` / `help` / Tab completion on the device | not present (provided by rtlpctl) | present |
| Prompt | `[hostname]> ` always | `[hostname]> ` / `[hostname]# ` / `[hostname](config)# ` |
| Output suffix | `-lite` in image name and `version` | `-full` in image name and `version` |

Both variants share everything else: the HTTP API with its privilege model,
the telnet server, the command vocabulary, and full rtlpctl support.

## Building

```sh
make                          # Lite, with WebUI
make WEB=0                    # Lite, without WebUI
make FULL=1                   # Full, with WebUI
make WEB=0 FULL=1             # Full, without WebUI
```

The variant is reflected in the image name and in the `version` command:

```
output/PCB_K0402WS_V3/rtlplayground-v0.2.23-<hash>-lite-PCB_K0402WS_V3.bin
output/PCB_K0402WS_V3/rtlplayground-v0.2.23-<hash>-full-PCB_K0402WS_V3.bin
```

The CI workflow (`.github/workflows/build.yml`) builds all four variants per
machine and renames them `WEB-…`, `CLI-…`, `WEB-FULL-…`, `CLI-FULL-…`.

## Console behavior

### Lite (default)

The console is a flat CLI:

- All commands are available directly from the `[hostname]> ` prompt
  (`vlan 100 1 2`, `port 1 1g`, `commit`, …).
- There is no mode system: `enable`, `configure terminal`, `exit` and `end`
  are unknown commands.
- There is no `?` / `help` / Tab completion in the firmware — the equivalent
  functionality is provided by the rtlpctl interactive shell instead
  (see [tools/rtlpctl](../tools/rtlpctl/README.md)).

### Full

The legacy EOS-style console:

- `enable` enters privileged mode (`[hostname]# `), `configure terminal`
  enters configuration mode (`[hostname](config)# `); `exit` / `end` /
  `disable` move back down.
- Configuration commands (`vlan`, `port`, `ip`, …) are only accepted in
  configuration mode; debug commands (`regget`, `sfp`, `reset`, …) require at
  least privileged mode. Otherwise the console answers
  `Not available here`.
- `?` / `help` and Tab completion are built into the device
  (`cmd_help.c`, `cmd_complete`), including sub-command help
  (`port ?`) and mode-aware completion.

## Shared surface

The following are **identical** in both variants and are what rtlpctl talks to:

- **HTTP API** (`httpd/`): `/status.json`, `/vlanlist`, `/config`,
  `/counters.json`, `/l2.json`, `/enc`, … and the privilege model:
  - password-authenticated `POST /cmd` runs in `MODE_CONFIG` (configuration
    commands only — `commit` and debug commands are rejected),
  - PSK-authenticated `POST /enc` with plaintext `commit` runs in
    `MODE_PRIVILEGED` (the only way to save the configuration over HTTP).
- **Command vocabulary**: the `parse_*` command set in `cmd_parser.c` is
  shared, so `rtlpctl cmd …`, the WebUI and automation scripts behave
  identically on both variants.
- **Telnet server** and its password authentication.

The mode values used by the HTTP API (`MODE_EXEC/PRIVILEGED/CONFIG`) exist in
both builds. In the Lite build they only serve the API privilege separation
(`cmd_console` marks console input and bypasses mode gating); in the Full
build the interactive console also uses them as the EOS-like hierarchy.

## Implementation

The split is implemented with `#ifdef FULL_CLI` (defined only when
`FULL=1`):

- `cmd_mode.c` (mode transition commands) and `cmd_help.c` (help strings,
  `?`/help, Tab completion) are compiled **only** in the Full build
  (`Makefile` adds them to `SRCS`).
- `cmd_parser.c` / `cmd_parser.h`: the mode-transition dispatch branches and
  the `cmd_complete`/`cmd_help` declarations are `#ifdef FULL_CLI`; the
  `cmd_console` console-bypass flag exists only in the Lite build.
- `cmd_editor.c`: the `?` and Tab handling in the serial line editor is
  `#ifdef FULL_CLI` (in Lite, `?` is a plain character and Tab is ignored).
- `telnetd/telnetd.c`: Tab completion and the `cli_mode = MODE_EXEC`
  assignment on login are `#ifdef FULL_CLI`.
- `rtlplayground.c`: the mode-based prompt is `#ifdef FULL_CLI`; the Lite
  build always prints `[hostname]> `.

## Resource usage (PCB_K0402WS_V3, v0.2.23)

| Segment | Lite | Full |
|---|---|---|
| BANK1 (httpd) | 43,325 B (66.1 %) | 43,325 B (66.1 %) |
| BANK2 (parser) | 42,421 B (64.7 %) | 42,817 B (65.3 %) |
| BANK3 (console/help) | 24,441 B (37.3 %) | 32,163 B (49.1 %) |

The Full variant adds roughly 8 KB of code in BANK3 (the help tables,
completion machinery and mode transitions).

## Which one to use

- **Lite** is the default and the recommended variant: it is smaller, and
  remote management goes through rtlpctl / the WebUI anyway, which provide
  the same convenience (command help, Tab completion, EOS-style
  `--mode arista`).
- **Full** preserves the legacy on-device console for people who prefer an
  EOS-like interactive session over telnet/serial without host-side tooling.

Related documents: [cli-design.md](cli-design.md) (EOS compatibility
analysis), [commands.md](commands.md) (Lite console reference),
[telnet.md](telnet.md) (remote console).
