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
I2C controller for reading and writing such EEPROMs so that interfacing is very simple.

## I2C Controller

The I2C controller of the RTL8372/3 is very simple and probably designed specifically
for 24C EEPROMs. Its use is straight-forward: Configure the I2C bus and device address
in the RTL837X_REG_I2C_CTRL register (I2C_MST1_CTRL1, 0x418). Then set the
EEPROM-register's address to be accessed in RTL837X_REG_I2C_IN (least-significant byte).
The I2C transfer is started by setting the 0-bit (I2C_TRIG) of RTL837X_REG_I2C_CTRL.
When this bit is cleared by the ASIC-side of the SoC, the transfer has finished. For a
read, the resulting value can be read in the LSB of RTL837X_REG_I2C_OUT. Writes are
supported as well (see [EEPROM Write](#eeprom-write) below).

Relevant bits of RTL837X_REG_I2C_CTRL (0x418):

```
READ_MODE       bit 22       0 = standard mode
MEM_ADDR_WIDTH  bits 20-21   EEPROM register address width (0 = 8 bit)
DATA_WIDTH      bits 16-19   data width - 1 (0 = 1 byte)
SCL_OUT_SEL     bits 13-15   I2C bus for SCL
SDA_OUT_SEL     bits 10-12   I2C bus for SDA
DEV_ADDR        bits 3-9     7-bit device address << 3 (0x50 = A0h EEPROM,
                              0x51 = A2h)
RWOP            bit 2        0 = read, 1 = write
I2C_FAIL        bit 1        error flag (set on bus failure)
I2C_TRIG        bit 0        start the transfer (auto-cleared when done)
```

This is the read code (a register offset with bit 7 set targets the A2h device
0x51 instead of the A0h EEPROM 0x50, e.g. for the diagnostic registers):
```
uint8_t sfp_read_reg(uint8_t slot, uint8_t reg)
{
	if (reg & 0x80) {	// Configure SFP readings address (0x51) as I2C device address
		reg &= 0x7f;
		REG_WRITE(RTL837X_REG_I2C_CTRL, 0x00, 0x1 << (I2C_MEM_ADDR_WIDTH-16) | 0,  0x51 >> 5, (0x51 << 3) & 0xff);
	} else {
		REG_WRITE(RTL837X_REG_I2C_CTRL, 0x00, 0x1 << (I2C_MEM_ADDR_WIDTH-16) | 0,  0x50 >> 5, (0x50 << 3) & 0xff);
	}

	reg_read_m(RTL837X_REG_I2C_CTRL);
	sfr_mask_data(1, 0xfc, i2c_bus_from_scl_pin(machine.sfp_port[slot].i2c.scl) << 5 | i2c_bus_from_sda_pin(machine.sfp_port[slot].i2c.sda) << 2);
	reg_write_m(RTL837X_REG_I2C_CTRL);

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

## EEPROM Write

The I2C master controller supports writes as well: select the bus and set the device
address to 0x50 (A0h EEPROM) with RWOP = 1 (write), put the register address into
RTL837X_REG_I2C_IN and the data into RTL837X_REG_I2C_OUT, then trigger exactly as for
a read. When the transfer finishes, check the I2C_FAIL bit (bit 1): a set bit means
the transfer failed on the bus. The write is then verified by reading the register
back:

```
uint8_t sfp_write_reg(uint8_t slot, uint8_t reg, uint8_t data) __reentrant
{
	// configure: DEV_ADDR = 0x50 (A0h), RWOP = 1 (write), 1-byte data
	REG_WRITE(RTL837X_REG_I2C_CTRL, 0x00, 0x10, 0x02, 0x80 | 0x04);
	reg_read_m(RTL837X_REG_I2C_CTRL);
	sfr_mask_data(1, 0xfc, scl_bus << 5 | sda_bus << 2);   // select bus
	reg_write_m(RTL837X_REG_I2C_CTRL);

	REG_WRITE(RTL837X_REG_I2C_IN,  0, 0, 0, reg);   // EEPROM register address
	REG_WRITE(RTL837X_REG_I2C_OUT, 0, 0, 0, data);  // data to write

	reg_bit_set(RTL837X_REG_I2C_CTRL, 0);           // trigger
	do { reg_read_m(RTL837X_REG_I2C_CTRL); } while (sfr_data[3] & 0x1);
	if (sfr_data[3] & 0x02) return 1;               // I2C_FAIL

	// the EEPROM update lags the controller's write completion on the
	// bus-3 port (slot 2), so the readback is polled (50 ms x 5)
	for (ri = 0; ri < 5; ri++) {
		delay(10);
		if (sfp_read_reg(slot, reg) == data) return 0;
	}
	return 1;
}
```

### Write-protection passwords

Many modules require a 4-byte password to unlock the EEPROM for writes. The password
is written to the A2h device (0x51) at registers 0x7B-0x7E (the module's MCU opens a
short unlock window afterwards). If a write without a password is rejected, the
firmware falls back through a built-in dictionary of 39 passwords (from
`sfp-tool/passwords.json`, `00000000` first) and retries the write after each one.

The dictionary lives in `sfp_pw_dict.inc`, which is gitignored (generated from
passwords.json). CI builds create an empty stub so the firmware compiles with only
the inline `00000000` entry.

Note: sending a wrong password can lock some modules (e.g. Finisar/Coherent) until
they are power-cycled. Most modules are writable without any password.

The GPIO bit-bang implementation that preceded this is gone: writes are done
exclusively through the I2C master controller.

## SFP EEPROM configuration on the Serial Console

The following sub-commands are provided on the serial console for SFP EEPROM
read/write operations:

```
> sfp <slot> dump
  Dumps the full 256-byte EEPROM contents as a hex dump with ASCII side view.
  Example output:
  0x0000: 003 004 007 004 000 000 002 000  000 000 000 006 67 000 000 000 ............g...
  0x0010: 008 003 000 1e 46 53 20 20  20 20 20 20 20 20 20 20 ....FS

> sfp <slot> save
  Reads the full 256-byte EEPROM and saves it as a backup in flash memory
  at address 0x6E000 + slot * 256.

> sfp <slot> restore
  Restores the EEPROM from the flash backup at 0x6E000 + slot * 256.
  The checksum is automatically fixed after the restore.

> sfp <slot> fix
  Sets bit 0 of byte 3 (indicating the module is a 1x copper cable assembly)
  and recalculates the checksum at byte 0x3F (CC_BASE). Use this to convert
  a module's EEPROM to a copper/SFP-direct-attach type or to repair a
  corrupted checksum.

> sfp <slot> write <hex-offset> <hex-value> [--pw <hex8>]
  Writes a single byte to the EEPROM at the given offset (0x00-0xFF).
  The write is verified by polling the readback (the bus-3 port returns
  stale values right after a write completes). The affected checksum is
  then updated automatically: writes to 0x00-0x3E recompute CC_BASE
  (byte 0x3F), writes to 0x40-0x5E recompute CC_EXT (byte 0x5F), so the
  module stays valid for the NIC. If the module requires a write-protection
  password, provide it with --pw; otherwise a plain write is attempted
  first and the built-in password dictionary is tried on failure.

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

> sfp <slot> checksum [--fix] [--pw <hex8>]
  Without --fix: displays current and expected values of CC_BASE (byte 0x3F)
  and CC_EXT (byte 0x5F). With --fix: recalculates and writes both checksums.

> sfp <slot> clone [--pw <hex8>]
  Writes the full 256-byte EEPROM from the flash buffer (pre-loaded via
  `sfp <slot> bulk <hex>` or `sfp <slot> restore`). The checksum is
  auto-fixed after cloning.
```

### Password notes

- `--pw <hex8>` is always optional: without it (or when the password is rejected),
  the firmware tries a plain write first and then falls back through the built-in
  password dictionary (39 entries from sfp-tool/passwords.json, `00000000` first).
- A malformed `--pw` argument (e.g. `--pw 12`) is rejected with "Invalid password".
- Some modules (e.g. Finisar/Coherent) lock up when given a wrong password until
  they are power-cycled; most modules are writable without any password.

### Example - recode a Fibre Channel module to Ethernet

```
> sfp 1 describe
  Identifier: 0x03 (SFP)
  Connector: 0x03 (LC)
  Vendor: FINISAR
  PN: FTLF8536P4BCV
  Rate: 96 x100MBd
  Compliance:
  CC_BASE: 0x35 (BAD)
  CC_EXT: 0xce (OK)

> sfp 1 patch
  Patch OK

> sfp 1 describe
  Compliance: 10GBase-LR 1000Base-LX
  CC_BASE: 0x31 (OK)
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

