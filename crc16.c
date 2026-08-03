#ifdef __SDCC
#pragma codeseg BANK1
#endif

#include <stdint.h>

// Pure 1-byte CRC-16/ARC update (poly 0xA001, LSB-first, init 0x0000).
// Shared with the host tool tools/crc_calculator.c (which includes this
// file) so the firmware and the update-image tool cannot drift apart.
uint16_t crc16_update(uint16_t crc, uint8_t a)
{
	uint8_t i;

	crc ^= a;
	for (i = 0; i < 8; i++)
		crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
	return crc;
}

#ifdef __SDCC

extern __xdata uint16_t crc_value;

// Bitwise CRC-16/ARC. Lives in BANK1, the same bank as its main caller
// httpd.c, because the bank-0 window (CSEG + CONST + XINIT) is nearly full:
// neither a lookup table (XINIT/CONST) nor additional CSEG code fits there.
// Mathematically identical to the old crc16.asm table implementation, so the
// update-image integrity check (crc_value == 0xb001) stays compatible with
// existing images.
void crc16(__xdata uint8_t *v) __banked
{
	crc_value = crc16_update(crc_value, *v);
}

#endif
