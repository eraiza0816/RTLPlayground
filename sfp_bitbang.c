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

static void gpio_set(uint8_t pin, uint8_t hi) __reentrant
{
	if (pin >= 32) {
		if (hi) reg_bit_clear(RTL837X_REG_GPIO_32_63_DIRECTION, pin % 32);
		else { reg_bit_clear(RTL837X_REG_GPIO_32_63_OUTPUT, pin % 32); reg_bit_set(RTL837X_REG_GPIO_32_63_DIRECTION, pin % 32); }
	} else {
		if (hi) reg_bit_clear(RTL837X_REG_GPIO_00_31_DIRECTION, pin % 32);
		else { reg_bit_clear(RTL837X_REG_GPIO_00_31_OUTPUT, pin % 32); reg_bit_set(RTL837X_REG_GPIO_00_31_DIRECTION, pin % 32); }
	}
}

static void i2c_delay(void)
{
	__xdata uint8_t i;
	for (i = 0; i < 100; i++);
}

static uint8_t gpio_read(uint8_t pin) __reentrant
{
	if (pin >= 32) reg_read_m(RTL837X_REG_GPIO_32_63_INPUT);
	else reg_read_m(RTL837X_REG_GPIO_00_31_INPUT);
	return !!(sfr_data[3-((pin>>3)&3)] & (1 << (pin & 7)));
}

uint8_t sfp_write_reg(uint8_t slot, uint8_t reg, uint8_t data) __reentrant
{
	uint8_t scl = machine.sfp_port[slot].i2c.scl;
	uint8_t sda = machine.sfp_port[slot].i2c.sda;
	uint8_t mask, err = 0;
	uint8_t saved[4], i;

	reg_read_m(RTL837X_PIN_MUX_1);
	for (i = 0; i < 4; i++) saved[i] = sfr_data[i];
	gpio_mux_setup(scl);
	gpio_mux_setup(sda);
	gpio_set(scl, 1);
	i2c_delay();
	gpio_set(sda, 1);
	i2c_delay();

	gpio_set(sda, 0);
	i2c_delay();
	gpio_set(scl, 0);
	i2c_delay();

	err = 0;
	for (mask = 0x80; mask; mask >>= 1) {
		gpio_set(sda, !(0xA0 & mask));
		i2c_delay();
		gpio_set(scl, 1);
		i2c_delay();
		gpio_set(scl, 0);
		i2c_delay();
	}
	gpio_set(sda, 1);
	i2c_delay();
	gpio_set(scl, 1);
	i2c_delay();
	if (gpio_read(sda)) err = 1;
	gpio_set(scl, 0);
	i2c_delay();

	if (!err) for (mask = 0x80; mask; mask >>= 1) {
		gpio_set(sda, !(reg & mask));
		i2c_delay();
		gpio_set(scl, 1);
		i2c_delay();
		gpio_set(scl, 0);
		i2c_delay();
	}
	if (!err) {
		gpio_set(sda, 1);
		i2c_delay();
		gpio_set(scl, 1);
		i2c_delay();
		if (gpio_read(sda)) err = 1;
		gpio_set(scl, 0);
		i2c_delay();
	}

	if (!err) for (mask = 0x80; mask; mask >>= 1) {
		gpio_set(sda, !(data & mask));
		i2c_delay();
		gpio_set(scl, 1);
		i2c_delay();
		gpio_set(scl, 0);
		i2c_delay();
	}
	if (!err) {
		gpio_set(sda, 1);
		i2c_delay();
		gpio_set(scl, 1);
		i2c_delay();
		if (gpio_read(sda)) err = 1;
		gpio_set(scl, 0);
		i2c_delay();
	}

	gpio_set(sda, 0);
	i2c_delay();
	gpio_set(scl, 1);
	i2c_delay();
	gpio_set(sda, 1);
	i2c_delay();

	for (i = 0; i < 4; i++) sfr_data[i] = saved[i];
	reg_write_m(RTL837X_PIN_MUX_1);

	if (err) return 1;
	delay(2);
	return sfp_read_reg(slot, reg) == data ? 0 : 1;
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
	uint8_t i;
	for (i = 0; i < 256; i++)
		flash_buf[i] = sfp_read_reg(slot, i);
	flash_region.addr = SFP_EEPROM_BACKUP;
	flash_sector_erase();
	flash_region.addr = SFP_EEPROM_BACKUP + (uint32_t)slot * 256;
	flash_region.len = 256;
	flash_write_bytes(flash_buf);
	return 0;
}

uint8_t sfp_restore_backup(uint8_t slot) __reentrant
{
	uint8_t i;
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
	uint8_t i;
	for (i = 0; i < 256; i++) {
		if (sfp_write_reg(slot, i, flash_buf[i]))
			return 1;
	}
	return sfp_fix_checksum(slot);
}
