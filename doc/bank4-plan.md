# BANK4 Creation Plan

Type: explanation · Topic: the fourth code bank and the compacted image layout

Status: **implemented in v0.2.24+** (the WebUI JSON endpoints are the
first BANK4 resident)

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
| BANK1 | 47,894 B | 48 KB (`CODE_BANK_SIZE`) | ~98 % of window (features + WebUI) |
| BANK2 | 48,095 B | 48 KB | ~98 % of window |
| BANK3 | 51,949 B (FULL) | 48 KB | over budget before BANK4 — see below |
| BANK4 | 28,233 B | 48 KB | network stack (uip), crypto (chacha20/poly1305/aead), WebUI JSON endpoints (api_status.c) |

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

### ⚠️ The image is compacted, not page-mapped (corrected model)

The flash image produced by `tools/imagebuilder` does **not** place the
banks at their page positions.  The imagebuilder reads each bank from the
input `.img` at `n*0x10000 + 0x4000` and repacks it into **consecutive
0xC000 slots**:

| Bank | sdcc `-Wl-bBANKn` (input) | `.bin` flash offset |
|---|---|---|
| BANK1 | 0x14000 | 0x04000-0x0FFFF |
| BANK2 | 0x24000 | 0x10000-0x1BFFF |
| BANK3 | 0x34000 | 0x1C000-0x27FFF |
| **BANK4** | **0x44000** | **0x28000-0x33FFF** |

The device fetches the compacted slots: `flash = 0x4000 + (PSBANK-1)*0xC000
+ (PC-0x4000)`.  **BANK4 must therefore be linked at 0x44000, not at a
page-6 address like 0x64000** — with `-Wl-bBANK4=0x64000` the imagebuilder
reads an empty input window (the `.img`'s 0x44000-0x63FFF gap) and places
zeros in the `.bin` bank-4 slot; the first banked call then executes
garbage and hangs the switch.  This was found the hard way on hardware
(commit history: BANK4 WebUI endpoints); verify the `.bin` bank content,
not just the linker `.map`, when adding a bank.

## Current flash map (512 KB image)

The `.bin` layout is the compacted one (see above): banks at 0x4000 /
0x10000 / 0x1C000 / 0x28000, web filesystem at 0x40000+ (added by
fileadder), config at 0x70000.  The 0x60000-0x6FFFF page-6 area is
unused; the SFP EEPROM backup stays at 0x6E000 (no code window
conflict in the compacted model).

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

### Step 0 (prerequisite): ~~relocate the SFP EEPROM backup~~ — not needed

The original plan moved the SFP backup out of a page-6 bank window that
does not exist in the compacted image model.  The backup stays at
`SFP_EEPROM_BACKUP 0x6e000`; the compacted banks end at 0x33FFF and
nothing else uses 0x6E000.

### Step 1: add BANK4

- Makefile link line: `-Wl-bBANK4=0x44000`. ✅ done — note this is the
  **input** position for imagebuilder (see the corrected model above);
  the `.bin` places the bank at 0x28000.
- New/relocated bank code: `#pragma codeseg BANK4` + `#pragma constseg BANK4`
  and `__banked` functions, following the pattern of the existing banks.
- **Usable size: 48 KB** (the `.bin` slot 0x28000-0x33FFF).
- Residents: `httpd/api_status.c` (WebUI JSON status endpoints), the whole
  uip network stack (`uip/*.c` + `udp_apps.c`) and the crypto
  (`crypto/*.c`).  Moving uip and crypto out of BANK1/BANK3 freed ~13 KB
  of contiguous space in each for future HTTP/protocol work (BANK1 free
  14.0 KB, BANK3 free 24.3 KB in the lite build).  The appcall entry
  points (`httpd_appcall`, `udp_callbacks`) must stay `__banked` because
  the network stack now lives in a different bank than the HTTP code —
  a missing `__banked` silently corrupts the call (found on hardware).

### Step 2: verify

1. Build both machines (`PCB_K0402WS_V3`, `SWGT024_V2_0_MANAGED`) — also
   with `WEB=0` and `FULL=1`. ✅ lite + FULL pass on PCB_K0402WS_V3.
2. Inspect `output/<machine>/rtlplayground.map`: the BANK4 segment must be
   placed at 0x44000 and end at ≤ 0x4FFFF. ⚠️ The linker does **not** error
   when a bank crosses its 48 KB window; code beyond the window is
   unreachable at runtime (crash). The .map check is mandatory — **and also
   verify the `.bin`: the bank-4 code must appear at the flash offset
   0x28000** (see the corrected model above; an empty slot means the first
   banked call executes garbage).
3. On hardware:
   - `sfp 1 save` / `sfp 1 restore` (backup stays at 0x6E000).
   - Firmware update round trip: upload → reset → `check_and_flash_update_image`
     copies the image including BANK4 → verify the new features work.
   - Factory reset (button / `flash_default_config`) still restores the
     default config.
   - Basic web UI / telnet / rtlpctl smoke test (banked paths).
   ✅ Round trip verified on the PCB_K0402WS_V3: the BANK4 JSON status
   endpoints respond and the switch stays up.

## Rejected options

| Option | Reason |
|---|---|
| BANK4 at 0x64000 (as originally planned) | breaks the imagebuilder's input pattern: the bank is read from the input at 0x44000, so 0x64000 produces an empty `.bin` slot and the first banked call crashes (found on hardware). |
| BANK5+ beyond 0x54000 | the input pattern `n*0x10000+0x4000` collides with the web filesystem input area for n ≥ 6 (0x64000), and the `.bin` slots would collide with the web filesystem at 0x40000 for the 7th bank. |
| Move `DEFAULT_CONFIG_START` (0x6F000) | complicates update copy ranges and factory reset for no gain (BANK4 fits before the web filesystem). |
| Bank code in page 8+ (1 MB flash) | 0x80000+ is the reserved update staging area. |

## References

- crtstart.asm — `__sdcc_banked_call` / `__sdcc_banked_ret` trampolines
- rtl837x_common.h — `CODE0_SIZE`, `CODE_BANK_SIZE`, `FIRMWARE_UPLOAD_START`,
  `SFP_EEPROM_BACKUP`
- rtlplayground.c — `read_flash()`, `check_and_flash_update_image()`,
  `flash_default_config()`
- httpd/httpd.c — upload handling (`FIRMWARE_UPLOAD_START`)
- installer/installer.c — OEM upgrade copy (120 sectors)
- doc/ghidra.md — RTL837x bank layout description
