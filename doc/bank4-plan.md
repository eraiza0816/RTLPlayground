# BANK4 Creation Plan

Status: **plan only — not implemented**

This document describes how to add a fourth code bank (BANK4) to the
firmware. It was written after an investigation of the memory model, the
flash layout and the firmware-update paths; see the "Background" sections
for the constraints that shape the plan.

## Motivation

The three existing banks are nearly full and the non-banked home area is
capped at 16 KB:

| Region | Used (v0.2.24) | Capacity | Note |
|---|---|---|---|
| HOME / CSEG | ~15 KB | 16 KB (`CODE0_SIZE`) | hard cap, prefetched into code RAM at boot |
| BANK1 | 44,015 B | 48 KB (`CODE_BANK_SIZE`) | ~90 % of window |
| BANK2 | 44,658 B | 48 KB | ~91 % of window |
| BANK3 | 24,846 B | 48 KB | ~51 % of window |

Upcoming features (SFP work, LLDP, new CLI commands) will not fit into
BANK3 and the remaining bank headroom, so a fourth bank is needed.

## Background: the code-banking model

- Code is fetched in **64 KB flash pages** selected by the `PSBANK` SFR
  (0x96). The boot vector prefetches the first `0x4000` bytes (the common
  area, `CODE0_SIZE`) into code RAM, so interrupts/ISRs always work.
- Each page offers a **48 KB bank window** (`CODE_BANK_SIZE 0xc000`) at page
  offset `0x4000`:
  - bank n image range: `(n << 16) + 0x4000 .. (n << 16) + 0xffff`
- Banked calls go through `__sdcc_banked_call` / `__sdcc_banked_ret`
  (crtstart.asm): the trampoline loads the function address into r0:r1 and
  sets `PSBANK = address >> 16` (masked with `0x1f` → up to 32 pages, i.e.
  2 MB; the flash is 512 KB = 8 pages).
- sdcc has no bank-count limit: banks are plain segments placed with
  `-Wl-bBANKn=addr`; the bank number is derived from the address at link
  time. Files opt in with `#pragma codeseg BANKn` / `constseg BANKn` and
  export `__banked` functions.

## Current flash map (512 KB image)

| Page | Range | Content |
|---|---|---|
| 0 | 0x00000-0x03FFF | HOME (common) code — prefetched into RAM |
| 0 | 0x04000-0x13FFF | unused — HOME is deliberately kept within the 16 KB prefetch |
| 1 | 0x14000-0x1FFFF | BANK1 (httpd, rtl837x_port, igmp, html_data) |
| 2 | 0x24000-0x2FFFF | BANK2 (cmd_parser, dhcp, leds, bandwidth, sfp_bitbang) |
| 3 | 0x34000-0x3FFFF | BANK3 (telnetd, stp, poly1305, cmd_help) |
| 4 | 0x40000-0x4FFFF | web filesystem (html data, read via flash MMIO) |
| 5 | 0x50000-0x5D000 | web filesystem (continued) |
| 5 | 0x5E000-0x5E1FF | SFP EEPROM backup (target of this plan) |
| 6 | 0x64000-0x6EFFF | **free — proposed BANK4 window** |
| 6 | 0x6F000-0x6FFFF | default config (`DEFAULT_CONFIG_START`) |
| 7 | 0x70000-0x7FFFF | live config (`CONFIG_START`, `CONFIG_LEN 0x1000`) |
| 8+ | 0x80000-0xFFFFF | firmware update staging area (`FIRMWARE_UPLOAD_START`) |

## Constraints that shaped the plan

### Why not pages 4/5 (0x44000 / 0x54000)?
The web filesystem occupies 0x40000-0x5D000 (html data, ~117 KB, served via
the flash MMIO registers, not via PSBANK). The natural next bank slots are
unavailable.

### Why not page 7 (0x74000, a full free 48 KB window)?
The firmware-update paths copy the new image only up to the config area:

- **Web update** (`check_and_flash_update_image`, rtlplayground.c): verifies
  the staged image at 0x80000 and copies `0x0 .. 0x6FFFF` (stops at
  `CONFIG_START` to preserve the live config).
- **OEM installer** (installer/installer.c): copies 120 sectors =
  `0x0 .. 0x77FFF`.

Code at 0x74000+ would therefore not survive a web update, and — worse —
updating **from any pre-BANK4 firmware** would leave 0xFF/garbage in the
bank window, crashing at the first banked call. The old firmware cannot be
changed, so page 7 is unusable for code without a one-time SOIC-clip
migration, which is rejected.

### Why 0x64000 is safe
- Inside the copy range of both update paths (`0x0 .. 0x6FFFF`).
- No overlap with any existing region once the SFP backup moves (below).
- Works on 512 KB flash (unmanaged devices) and 1 MB flash alike.

## Plan

### Step 0 (prerequisite): relocate the SFP EEPROM backup to 0x5E000

The backup currently sits at `SFP_EEPROM_BACKUP 0x6e000`
(rtl837x_common.h:92), inside the page-6 bank window. Move it to **0x5E000**:

- Only `sfp_bitbang.c` (sfp_save_backup / sfp_restore_backup) references the
  macro; no other code change is needed.
- 0x5E000-0x5E1FF is free: the web filesystem ends around 0x5C900
  (0x5D000 with padding) and nothing else is placed in 0x5E000-0x5EFFF.
- Both update paths cover 0x5E000. Note that firmware updates wipe the
  backup area with the uploaded image (same as today at 0x6E000); the data
  is recoverable with `sfp save`.
- Existing backups stored at 0x6E000 are abandoned (2×256 B of module
  EEPROM data).
- ⚠️ The web filesystem grows forward from 0x40000 and the build tool
  (tools/fileadder) does **not** detect overlaps: keep the html end below
  ~0x5D000. If html grows close to 0x5E000, pick a new backup offset
  outside the BANK4 window (0x64000-0x6EFFF) and the config areas, for
  example 0x5F000, and update this document.
- Update the comment in rtl837x_common.h accordingly.

### Step 1: add BANK4 at 0x64000

- Makefile link line: add `-Wl-bBANK4=0x64000`.
- New/relocated bank code: `#pragma codeseg BANK4` + `#pragma constseg BANK4`
  and `__banked` functions, following the pattern of the existing banks.
- **Usable size: 44 KB** (`0x64000 .. 0x6EFFF`). The 4 KB
  `0x6F000-0x6FFFF` default-config region
  (`flash_default_config()` copies it verbatim to 0x70000) must remain
  untouched — do not move it.
- Candidate content (TBD): SFP feature expansion, LLDP, new CLI commands.
  Prefer moving the largest remaining home/bank code only when it fits.

### Step 2: verify

1. Build both machines (`PCB_K0402WS_V3`, `SWGT024_V2_0_MANAGED`) — also
   with `WEB=0` and `FULL=1`.
2. Inspect `output/<machine>/rtlplayground.map`: the BANK4 segment must be
   placed at 0x64000 and end at ≤ 0x6EFFF. ⚠️ The linker does **not** error
   when a bank crosses its 48 KB window; code beyond the window is
   unreachable at runtime (crash). The .map check is mandatory.
3. On hardware:
   - `sfp 1 save` / `sfp 1 restore` (backup at 0x5E000).
   - Firmware update round trip: upload → reset → `check_and_flash_update_image`
     copies the image including BANK4 → verify the new features work.
   - Factory reset (button / `flash_default_config`) still restores the
     default config.
   - Basic web UI / telnet / rtlpctl smoke test (banked paths).

## Rejected options

| Option | Reason |
|---|---|
| BANK4 at 0x74000 (page 7) | outside web-update copy range; pre-BANK4 firmware would install a broken image |
| Move `DEFAULT_CONFIG_START` (0x6F000) | complicates update copy ranges and factory reset for no gain (BANK4 fits in 44 KB) |
| Bank code in page 8+ (1 MB flash) | 0x80000+ is the reserved update staging area |

## References

- crtstart.asm — `__sdcc_banked_call` / `__sdcc_banked_ret` trampolines
- rtl837x_common.h — `CODE0_SIZE`, `CODE_BANK_SIZE`, `FIRMWARE_UPLOAD_START`,
  `SFP_EEPROM_BACKUP`
- rtlplayground.c — `read_flash()`, `check_and_flash_update_image()`,
  `flash_default_config()`
- httpd/httpd.c — upload handling (`FIRMWARE_UPLOAD_START`)
- installer/installer.c — OEM upgrade copy (120 sectors)
- doc/ghidra.md — RTL837x bank layout description
