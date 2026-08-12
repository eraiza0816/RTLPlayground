/*
 * SFP EEPROM write via GPIO bit-bang
 */
#pragma codeseg BANK2

#include <stdint.h>
#include "rtl837x_sfr.h"
#include "rtl837x_regs.h"
#include "rtl837x_common.h"
#include "rtl837x_pins.h"
#include "machine.h"
#include "rtl837x_flash.h"

extern volatile __xdata uint8_t sfr_data[4];
extern __code struct machine machine;
extern __xdata uint8_t flash_buf[FLASH_BUF_SIZE];
extern __xdata struct flash_region_t flash_region;

__xdata uint8_t sfp_pw[4];
__xdata uint8_t sfp_pw_pending;

// Only the most common key stays inline; the full dictionary lives in
// sfp_pw_dict.inc (not committed).
// CI builds create the file empty, in which case only the inline entry is used.
/* Guard counter for the I2C busy-waits (the functions are __reentrant,
 * so they cannot declare __xdata locals; they never actually re-enter). */
static __xdata uint16_t sfp_i2c_guard;

static __code uint8_t sfp_pw_dict[][4] = {
	{ 0x00, 0x00, 0x00, 0x00 },   // empty protection key
#include "sfp_pw_dict.inc"
};

uint8_t sfp_write_reg(uint8_t slot, uint8_t reg, uint8_t data) __reentrant
{
	uint8_t scl = machine.sfp_port[slot].i2c.scl;
	uint8_t sda = machine.sfp_port[slot].i2c.sda;
	uint8_t scl_bus = i2c_bus_from_scl_pin(scl);
	uint8_t sda_bus = i2c_bus_from_sda_pin(sda);
	uint8_t attempt, k;
	uint8_t ndict = sizeof(sfp_pw_dict) / sizeof(sfp_pw_dict[0]);

	for (attempt = 0; attempt <= ndict; attempt++) {
		if (attempt == 0 && !sfp_pw_pending) {
			// no password given: plain write, no unlock
			REG_WRITE(RTL837X_REG_I2C_CTRL, 0x00, 0x10, 0x02, 0x80 | 0x04);
			reg_read_m(RTL837X_REG_I2C_CTRL);
			sfr_mask_data(1, 0xfc, scl_bus << 5 | sda_bus << 2);
			reg_write_m(RTL837X_REG_I2C_CTRL);
		} else {
			// unlock: write the 4-byte password to the A2h device (0x51) 0x7B-0x7E
			if (attempt > 0) {
				for (k = 0; k < 4; k++) sfp_pw[k] = sfp_pw_dict[attempt - 1][k];
			}
			sfp_pw_pending = 0;
			REG_WRITE(RTL837X_REG_I2C_CTRL, 0x00, 0x13, 0x02, 0x88 | 0x04);
			reg_read_m(RTL837X_REG_I2C_CTRL);
			sfr_mask_data(1, 0xfc, scl_bus << 5 | sda_bus << 2);
			reg_write_m(RTL837X_REG_I2C_CTRL);
			REG_WRITE(RTL837X_REG_I2C_IN, 0, 0, 0, 0x7B);
			REG_WRITE(RTL837X_REG_I2C_OUT, sfp_pw[3], sfp_pw[2], sfp_pw[1], sfp_pw[0]);
			reg_bit_set(RTL837X_REG_I2C_CTRL, 0);
			sfp_i2c_guard = 0;
			do {
				reg_read_m(RTL837X_REG_I2C_CTRL);
				if (++sfp_i2c_guard == 0)
					break;
			} while (sfr_data[3] & 0x1);
			// the module's MCU needs time to process the password before
			// the unlock window opens; the window is short, so write promptly
			delay(2);
			// write config for the A0h device (0x50)
			REG_WRITE(RTL837X_REG_I2C_CTRL, 0x00, 0x10, 0x02, 0x80 | 0x04);
			reg_read_m(RTL837X_REG_I2C_CTRL);
			sfr_mask_data(1, 0xfc, scl_bus << 5 | sda_bus << 2);
			reg_write_m(RTL837X_REG_I2C_CTRL);
		}

		REG_WRITE(RTL837X_REG_I2C_IN, 0, 0, 0, reg);
		REG_WRITE(RTL837X_REG_I2C_OUT, 0, 0, 0, data);

		reg_bit_set(RTL837X_REG_I2C_CTRL, 0);
		sfp_i2c_guard = 0;
		do {
			reg_read_m(RTL837X_REG_I2C_CTRL);
			if (++sfp_i2c_guard == 0)
				break;
		} while (sfr_data[3] & 0x1);
		if (sfr_data[3] & 0x02) return 1;

		// the module's EEPROM update lags the controller's write completion
		// (observed on the bus-3 port), so poll the readback for a while
		{
			uint8_t ri;
			for (ri = 0; ri < 5; ri++) {
				delay(10);
				if (sfp_read_reg(slot, reg) == data) return 0;
			}
		}
	}
	return 1;
}

void sfp_dump_eeprom(uint8_t slot) __reentrant
{
	uint16_t i;
	uint8_t j, v;
	for (i = 0; i < 256; i += 16) {
		write_char('\n');
		print_short(i);
		print_string(": ");
		for (j = 0; j < 16; j++) {
			v = sfp_read_reg(slot, (uint8_t)(i + j));
			if (v < 0x10) write_char('0');
			print_byte(v);
			if (j == 7) write_char(' ');
			if (j < 15) write_char(' ');
		}
		write_char(' ');
		for (j = 0; j < 16; j++) {
			v = sfp_read_reg(slot, (uint8_t)(i + j));
			if (v >= 0x20 && v < 0x7f) write_char(v); else write_char('.');
		}
	}
	write_char('\n');
}

uint8_t sfp_fix_checksum(uint8_t slot) __reentrant
{
	uint16_t sum = 0;
	uint8_t i;
	for (i = 0; i < 0x3F; i++) sum += sfp_read_reg(slot, i);
	i = (uint8_t)(sum & 0xFF);
	if (i == sfp_read_reg(slot, 0x3F)) return 0;
	return sfp_write_reg(slot, 0x3F, i);
}

uint8_t sfp_eeprom_fix(uint8_t slot) __reentrant
{
	if ((sfp_read_reg(slot, 3) & 0x01) == 0)
		if (sfp_write_reg(slot, 3, sfp_read_reg(slot, 3) | 0x01)) return 1;
	return sfp_fix_checksum(slot);
}

uint8_t sfp_save_backup(uint8_t slot) __reentrant
{
	uint16_t i;
	for (i = 0; i < 256; i++)
		flash_buf[i] = sfp_read_reg(slot, i);
	flash_region.addr = SFP_EEPROM_BACKUP;
	flash_init(0);
	flash_sector_erase();
	flash_region.addr = SFP_EEPROM_BACKUP + (uint32_t)slot * 256;
	flash_region.len = 256;
	flash_write_bytes(flash_buf);
	flash_init(1);
	return 0;
}

uint8_t sfp_restore_backup(uint8_t slot) __reentrant
{
	uint16_t i;
	flash_region.addr = SFP_EEPROM_BACKUP + (uint32_t)slot * 256;
	flash_region.len = 256;
	flash_read_bulk(flash_buf);
	for (i = 0; i < 256; i++) {
		if (sfp_write_reg(slot, i, flash_buf[i]))
			return 1;
	}
	delay(2);
	return sfp_fix_checksum(slot);
}

uint8_t sfp_bulk_write(uint8_t slot) __reentrant
{
	uint16_t i;
	for (i = 0; i < 256; i++) {
		if (sfp_write_reg(slot, i, flash_buf[i]))
			return 1;
	}
	return sfp_fix_checksum(slot);
}
