# How-to: Compiling the firmware

Type: how-to · Task: build the firmware image for your device

## Prerequisites

Debian 12/13 (Ubuntu 24.04 ships an older sdcc — version 4.5 is required):

```
sudo apt install make gcc sdcc xxd python-is-python3 libjson-c-dev
```

## 1. Select the machine

Edit `machine.h` and uncomment the machine type that matches your device.
The list of supported devices is in
[doc/supported_devices.md](../supported_devices.md).

> [!TIP]
> You can write configuration parameters into `config.txt` (see
> [the CLI reference](../commands.md)) so the switch gets a correct IP
> configuration at first boot.

## 2. Build

```
make
```

The image is written to
`RTLPlayground/output/rtlplayground_version_machine.bin`, for example:

```
rtlplayground-v0.1.0-12c98ba-dirty-LIANGUO_ZX_SWTGW215AS.bin
```

The filename ends in `.bin` (not `.img`) so IMSProg is happy.

### CLI variants

Two console variants are selected with the `FULL` variable (same mechanism
as the `WEB` WebUI switch):

| Variant | Build command | Console |
|---------|---------------|---------|
| **Lite** (default) | `make` | Flat CLI: every command directly from the `[hostname]> ` prompt, no modes, no `?`/help/Tab completion on the device (provided by `rtlpctl` instead) |
| **Full** | `make FULL=1` | Legacy EOS-style CLI: `enable` / `configure terminal` mode hierarchy, `?`/help and Tab completion |

The variant is reflected in the image name and in `version`
(`...-lite-...` / `...-full-...`).  Both variants keep the same HTTP API
(`/cmd` password-authenticated in CONFIG mode, `/enc` PSK-authenticated
`commit`) and the same rtlpctl support.
See [doc/cli-variants.md](../cli-variants.md) for the full comparison.

> [!CAUTION]
> This image can be flashed directly to the chip OR through the firmware
> update/upgrade interface of an already-running RTLPlayground.

## 3. Build the OEM upgrade image (managed devices only)

Managed switches can be updated from their original firmware using a
specific upgrade image.  First build the direct-flashing image (step 1-2),
then:

```
cd installer
make
```

The image is written to `RTLPlayground/installer/output/rtlplayground_oem_upgrade.bin`.

> [!CAUTION]
> This image must ONLY be used for the original OEM firmware's web
> interface firmware upgrade.  You do not need it if you are already on
> RTLPlayground.  Unless you go back to the original OEM firmware, you
> would only flash it once; future upgrades only need step 1-2.

Example console output:

```
RTLPlayground/installer$ make
mkdir -p output
gcc updatebuilder.c -o output/updatebuilder
sdas8051 -plosgff -o output/crtstart.rel crtstart.asm
sdcc -mmcs51 --code-loc 0x1000 -o output/installer.rel -c installer.c
sdcc -mmcs51 -Wl-bHOME=0x1100 -Wl-r -o output/rtlinstaller.ihx output/crtstart.rel output/installer.rel
./output/updatebuilder -i output/rtlinstaller.ihx -o output/rtlplayground_oem_upgrade.bin ../output/rtlplayground.bin
Input file size: 524288
Bytes read: 524288
EOF
Payload sum 1 is: 0x25100
Payload sum 2 is: 0x25100
Payload sum with header is: 0x264ec
Payload sum is: 0xf8fe94
Header checksum is: 0x5a1
```
