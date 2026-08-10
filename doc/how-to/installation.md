# How-to: Installing the firmware

Type: how-to · Task: get the RTLPlayground image onto your switch

There are two ways to install, depending on the device and the situation.

## Option A: Installation through the web interface (software way)

Managed switches (running either the OEM firmware or RTLPlayground) can be
upgraded via the web interface.  Unmanaged switches cannot be flashed this
way (see Option B).

1. Go to the "Firmware update" tab.
2. Select the correct file:

> [!IMPORTANT]
> - Already running RTLPlayground: upload
>   `RTLPlayground/output/rtlplayground_Version_Machine.bin`
> - Original OEM firmware: upload
>   `RTLPlayground/installer/output/rtlplayground_oem_upgrade.bin`

3. Check once more that your device matches the machine type before
   flashing, and make sure you have a backup of the original firmware.
4. Push the **Upload File** button.

## Option B: Flashing the ROM directly (hardware way, the only rescue path)

This is the only way to flash unmanaged switches (if the ROM chip is large
enough), and the only way to unbrick a device when something went wrong.

> [!IMPORTANT]
> You need a SOIC-8 clip to flash the ROM chip directly on board
> (alternatively de-solder the flash chip and use a SOIC adapter).
> Use the binary file `RTLPlayground/output/rtlplayground_Version_Machine.bin`.

> [!CAUTION]
> Opening the switch voids the warranty.

1. Disconnect power from the switch.
2. Open the switch.
3. Attach the clip onto the flash chip (red line on Pin 1; Pin 1 has a
   point marker).
4. Connect the USB flash programmer; the power LED on the switch will light
   up — check the cabling if not.
5. Don't panic: mixing up GND and 3.3 V usually does not destroy the switch.
6. Use IMSProg, flashrom or any programmer to detect the chip.
7. **Make a backup (dump) of the existing firmware!**
8. Erase the ROM (blank).
9. Load the firmware into IMSProg.
10. Flash the ROM chip.
11. Disconnect the clip.
12. Ready for the first boot.

## Next steps

- [Connecting a serial interface](serial.md)
- [Getting started](../tutorials/getting-started.md)
