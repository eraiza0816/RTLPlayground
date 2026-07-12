# SFP+ Slots

The RTL8372/3 provide support for 1 or 2 SFP+ slots, which support fiber and Ethernet
module with speeds of 1GBit, 2.5GBit and 10GBit. 5GBit could be possible but is not
implemented due to the lack of suitable modules.

When a module is inserted, it directly connects to GPIO, I2C and RX/TX data lines of
the SoC. An example schematics can be found here:
[SFP Module Schematics](https://sfp.by/source/manual/SCP6F44-GL-BWE.pdf). Another
resources is [here](https://www.sfptransceiver.com/product_pdf/SFP/SFP%20Design%20Guide.pdf).
The SoC
detects the insertion because the MOD-DEF0 line is pulled low by the module. The
corresponding bit in RTL837X_REG_GPIO_B or RTL837X_REG_GPIO_C will transition from
1 to 0. At that point, the code waits for some 100ms in order for the module to power
up and then reads the EEPROM of the module to get the type of module and in particular
the bit-rate. The EEPROM can be read via the MOD-DEF1 and MOD-DEF2 lines which
provide a standard I2C interface to the standard 24C-EEPROM. The SoCs contain a simple
I2C controller for reading such EEPROMs so that interfacing is very simple.

## I2C Controller

The I2C controller of the RTL8372/3 is very simple and probably designed specifically
for reading 24C EEPROMs. Its use is straight-forward: Configure the I2C bus used
(the code currently uses the default already set regarding what is probably timing)
in the RTL837X_REG_I2C_CTRL register. Then set the EEPROM-register's addresss to be
read in RTL837X_REG_I2C_IN (least-significant byte). The I2C transfer is started
by setting the 0-bit of RTL837X_REG_I2C_CTRL. When this bit is cleared by the
ASIC-side of the SoC, the resulting value can be read in the LSB of RTL837X_REG_I2C_OUT.
This is the code:
```
uint8_t sfp_read_reg(uint8_t slot, uint8_t reg)
{
        // Select I2C-bus according to slot
	if (slot == 0) {
		reg_read_m(RTL837X_REG_I2C_CTRL);
		sfr_mask_data(1, 0xff, 0x72);
		reg_write_m(RTL837X_REG_I2C_CTRL);
	} else {
		reg_read_m(RTL837X_REG_I2C_CTRL);
		sfr_mask_data(1, 0xff, 0x6e);
		reg_write_m(RTL837X_REG_I2C_CTRL);
	}

	REG_WRITE(RTL837X_REG_I2C_IN, 0, 0, 0, reg);

	// Execute I2C Read
	reg_bit_set(RTL837X_REG_I2C_CTRL, 0);

	// Wait for execution to finish
	do {
		reg_read_m(RTL837X_REG_I2C_CTRL);
	} while (sfr_data[3] & 0x1);

	reg_read_m(RTL837X_REG_I2C_OUT);
	return sfr_data[3];
}
```

The description of the data stored in the EEPROM can be found in the
[SFF-8472 standard](https://members.snia.org/document/dl/25916)
The most relevant is byte 12 (0x0c), which gives the signalling rate of the module in
100MBit, including the 25% overhead for error correction. Currently the code looks like
this:
```
static inline uint8_t sfp_rate_to_sds_config(register uint8_t rate)
{
	if (rate == 0xd)
		return SDS_1000BX_FIBER;
	if (rate == 0x1f)  // Ethernet 2.5 GBit
		return SDS_HSG;
	if (rate > 0x65 && rate < 0x70)
		return SDS_10GR;
	return 0xff;
}
```
For example, a 1000MBit fiber module will have a rate coding of 0xd = 13 = 1300Mbit,
which is the rounded-up value for 1250MBit, the error-corrected bit-rate of
a 1000BX fiber module.

## Interfacing the module for RX/TX

In order to transmit data or receive data from the module, the SerDes of the SoC connected
to the module needs to e properly configured. As can be seen from the
[SFP Module Schematics](https://sfp.by/source/manual/SCP6F44-GL-BWE.pdf), the Photo-transistor
of the module is optimized by an amplifier and quantized to bits, which directly arrive
at the SoC in a differential pair. This data still has the 25% overhead of the error correction
codes that were on the fiber. The switch needs to configure the SerDes correctl (sds_config())
and set up the MAC on the SoC to talk to the SDS with the correct bit-rate.

## Other SFP-module GPIOs
SFP modules also provide RX-LOS GPIOs, which pulls low when the fiber or Ethernet
cable is not attached (on either side of the link) and usually also a TX-disable GPIO,
which allows to disable the Laser in order to power down the link. There is typically
also a TX-Fault GPIO which pulls low when the laser overheated. While the RX-Los pin
is connected to the SoC and can be read for the devices with a single SFP+ slot
(for the dual-SFP+ slot KP-9000-6HX-X2 only the RX-LOS pin of the right slot seems to
be connected), the other GPIOs have not been identified and counting lines on the PCB
seems to indicate these pins are unlikely to be connected.

The RX-LOS GPIO does not provide any further benefit, since the link status can also be
read from the link-status registers of the MAC or SDS.

The easiest way to identifiy additional GPIOs of an SFP module is to take a cheap module
apart, solder wires to the pins of the on-board PCB which are then routed back through
the end of the module. By pulling e.g. TX-Fault low while printing out the GPIOs, the
correct GPIO can be identified.

## EEPROM Write via GPIO Bit-bang

The hardware I2C controller of the RTL8372/3 only supports reading the SFP EEPROM.
To write the EEPROM (e.g. to fix a module's configuration or checksum), the I2C pins
must be temporarily switched to GPIO mode and driven directly by software (bit-bang).

This is implemented in `sfp_bitbang.c`. When any write operation is triggered, the
selected I2C pins (SCL/SDA) are reconfigured as GPIOs via `gpio_mux_setup()`, the
I2C write protocol is executed in software, and the pins are restored to their
original I2C function.

The EEPROM address used is `0xA0` (device address `0x50`).

## SFP EEPROM configuration on the Serial Console

The following sub-commands are provided on the serial console for SFP EEPROM
read/write operations:

```
> sfp <slot> dump
  Dumps the full 256-byte EEPROM contents as a hex dump with ASCII side view.
  Example output:
  0000: 03 04 07 00 00 00 00 00  00 00 00 01 0d 00 00 00  ................
  0010: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  ................

> sfp <slot> save
  Reads the full 256-byte EEPROM and saves it as a backup in flash memory
  at address 0x54000 + slot * 256.

> sfp <slot> restore
  Restores the EEPROM from the flash backup at 0x54000 + slot * 256.
  The checksum is automatically fixed after the restore.

> sfp <slot> fix
  Sets bit 0 of byte 3 (indicating the module is a 1x copper cable assembly)
  and recalculates the checksum at byte 0x3F (CC_BASE). Use this to convert
  a module's EEPROM to a copper/SFP-direct-attach type or to repair a
  corrupted checksum.

> sfp <slot> write <hex-offset> <hex-value> [--pw <hex8>]
  Writes a single byte to the EEPROM at the given offset (0x00-0xFF).
  The write is verified by reading back the value. If the offset is within
  the base ID field (0x00-0x3E), a warning is printed suggesting to run
  `sfp <slot> checksum --fix` afterwards to update CC_BASE. If the module
  requires a write-protection password, provide it with --pw.

> sfp <slot> bulk <512-hex-chars>
  Writes all 256 bytes of the EEPROM at once using a hex string of exactly
  512 characters (two hex chars per byte). The checksum is automatically
  fixed after the write.

> sfp <slot> describe
  Displays formatted module information: identifier, connector type, vendor
  name, part number, revision, serial, date code, signalling rate,
  compliance codes (Ethernet/FC), and checksum validity (CC_BASE + CC_EXT).

> sfp <slot> patch [--pw <hex8>]
  Patches the EEPROM to convert a Fibre Channel module to Ethernet:
  - Byte 3 = 0x20 (10GBase-LR)
  - Byte 6 = 0x02 (1000BASE-LX)
  - Byte 7 = 0x00 (clear FC link length)
  - Byte 9 = 0x00 (clear FC speed)
  - CC_BASE recalculated after patching
  If the module requires a write-protection password, provide it with --pw.

> sfp <slot> checksum [--fix] [--pw <hex8>]
  Without --fix: displays current and expected values of CC_BASE (byte 0x3F)
  and CC_EXT (byte 0x5F). With --fix: recalculates and writes both checksums.
  If the module requires a write-protection password, provide it with --pw.

> sfp <slot> clone [--pw <hex8>]
  Writes the full 256-byte EEPROM from the flash buffer (pre-loaded via
  `sfp <slot> restore` or other means). Checksum is auto-fixed after cloning.
  If the module requires a write-protection password, provide it with --pw.
```

## SFP EEPROM Editor (Web Interface)

A dedicated web page at `/sfp_eeprom` allows viewing and editing the SFP EEPROM
in a graphical hex editor:


Features:
- Select SFP slot (1 or 2) and refresh to read the current EEPROM contents
- Click any hex byte to edit it inline (sends `sfp write` via the CLI)
- Download the current EEPROM as a `.bin` file
- Upload a `.bin` file (exactly 256 bytes) to write the entire EEPROM
- Vendor, part number, serial number and module type are displayed at the top

The editor fetches data via the JSON endpoint:
```
GET /sfp_eeprom.json?slot=<n>
Returns: {"slot":<n>,"data":"<256 hex bytes>"}
```

