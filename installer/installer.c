#include <8051.h>
#include <stdint.h>

// #define REGDBG 1
// #define RXTXDBG 1

#define UPDATE_LOC	0x0001D000
#define HEADER_LENGTH	0x14
#define UPDATE_CODE_LOC	(UPDATE_LOC + HEADER_LENGTH)
#define HEADER_MAGIC	0x12345678

/* Update-image geometry (must match installer/updatebuilder.c).  The
 * payload sum covers the installer code image [0x14, 0x4012) (linked at
 * flash 0x1000, i.e. flash [0x1000, 0x3ffe)), a constant 0xff*HEADER_LENGTH
 * and the payload image [0x4026, image_size) (flash [0x1d014, ...)). */
#define SUM_CODE_START		0x00001000
#define SUM_CODE_END		0x00003ffe
#define SUM_PAYLOAD_START	(UPDATE_CODE_LOC)

#include "../rtl837x_sfr.h"
#include "../rtl837x_regs.h"

// See setup_serial_timer1() for valid baudrate settings!
#define SERIAL_BAUD_RATE 57600
#define CLOCK_HZ 125000000

// Derive the divider settings for the internal clock
#if CLOCK_HZ == 20800000
#define CLOCK_DIV 3
#elif CLOCK_HZ == 31250000
#define CLOCK_DIV 2
#elif CLOCK_HZ == 62500000
#define CLOCK_DIV 1
#elif CLOCK_HZ == 125000000
#define CLOCK_DIV 0
#endif

// We buffer 1 sector as this is also the erase size
__xdata uint8_t buffer[0x1000];
__xdata uint8_t dio_enabled;

__code uint8_t * __code hex = "0123456789abcdef";

void isr_timer0(void) __interrupt(1)
{
}


void isr_ext0(void) __interrupt(0)
{
	EX0 = 0;	// Disable interrupt for the moment
	IT0 = 1;	// Trigger on falling edge of external interrupt
	EX0 = 1;	// Re-enable interrupt
}


void isr_ext1(void) __interrupt(2)
{
	EX1 = 0;
	EX1 = 1;
}


void write_char(char c)
{
	do {
	} while (TI == 0);
	TI = 0;
	if (c =='\n') {
		SBUF = '\r';
		do {
		} while (TI == 0);
		TI = 0;
	}
	SBUF = c;
}


void print_string(__code char *p)
{
	while (*p)
		write_char(*p++);
}


void print_byte(uint8_t a)
{
	write_char(hex[(a >> 4) & 0xf]);
	write_char(hex[a & 0xf]);
}

void print_short(uint16_t a)
{
	print_string("0x");
	for (signed char i = 12; i >= 0; i -= 4) {
		write_char(hex[(a >> i) & 0xf]);
	}
}

/* Set up serial port 0 using Timer 1 as baudrate generator.
 * For x Bd these settings are needed, see table below.
 * NOTE: Settings only valid for F_SYS = 125 MHz!
 * |   Wanted |       | TMR | F_SYS |      |   Actual |        |
 * | baudrate | SMOD0 | DIV |   DIV |  TH1 | baudrate |  Error |
 * | -------- | ----- | --- | ----- | ---- | -------- | ------ |
 * |     1200 |   0   |  12 |   255 | 0x01 |   1276.6 |  6.00% |
 * |     2400 |   0   |  12 |   136 | 0x78 |   2393.5 | −0.27% |
 * |     4800 |   0   |   4 |   203 | 0x35 |   4810.7 |  0.22% |
 * |     9600 |   1   |   4 |   203 | 0x35 |   9621.3 |  0.22% |
 * |    14400 |   1   |   4 |   136 | 0x78 |  14361.2 | −0.27% |
 * |    19200 |   1   |   4 |   102 | 0x9a |  19148.3 | −0.27% |
 * |    38400 |   1   |   4 |    51 | 0xcd |  38296.6 | −0.27% |
 * |    57600 |   1   |   4 |    34 | 0xde |  57444.9 | −0.27% |
 * |   115200 |   1   |   4 |    17 | 0xef | 114889.7 | −0.27% |
 */
#if CLOCK_HZ != 125000000
#warning "SERIAL 0 baudrate setting may only valid for F_CPU = 125 MHz!"
#endif
void setup_serial_timer1(void)
{
	// Timer 1: Mode 2: automatic reload
	TMOD &= 0x0F;
	TMOD |= 0x20; // Timer1: Mode2: Timer, 8-bit with auto-reload
	CKCON |= 0x10; // Timer1 clock divider: F_SYS / 4: T2M = 1, Timer 1 uses clk/4

	PCON |= 0x80; // SMOD0 = 1; Double the Baud Rate, don't divide Timer 1 Overflag signal.

	SCON  = 0x50;  // Mode = 1: ASYNC 8N1 with Timer 2 as baud-rate generator, REN_0 Receive enable

	/* The TH1 register contain the reload value, timer1 when T1 overflows to 0x100.
	 * NOTE: compiler computs the wrong value. 0xF0 is calculated but 0xEF is the right value for 115200.
	 * Also https://www.keil.com/products/c51/baudrate.asp confirms this.
	 * Added 32 before div by 64 to make sure rounding is correct so that the results are right.
	 *
	 * TH1 = 0x100 - (2^SMOD0 * F_SYS) / ( TMR1_DIV / BAUDRATE * 32)
	 */
	TH1 = (0x100 - (((CLOCK_HZ / SERIAL_BAUD_RATE) + 32) / (4 * 16))) & 0xff;

	TCON |= 0x40;	// Start timer 1

	ET1 = 0; // Timer1 Interrupt is NOT wanted!
	TI = 1; // Set TI-interrupt/flag, to flag that the TX-buf is empty.
	RI = 0; // Clear RI-interrupt flag

	ES = 0; // Disable serial IRQ, software is only printing data and just polls TI-flag.
}

/*
 * Configure Memory Managed IO
 */
void flash_configure_mmio(void)
{
	// Set configuration for MMIO access by controller
	if (dio_enabled) {
		SFR_FLASH_MODEB = 0x18;
		SFR_FLASH_CMD_R = 0xbb;	// By default we read with Dual speed
		SFR_FLASH_DUMMYCYCLES = 4;
		return;
	}

	SFR_FLASH_MODEB = 0x0;
	SFR_FLASH_CMD_R = 0xb;	// By default we read with single speed
	SFR_FLASH_DUMMYCYCLES = 8;
}


/*
 * Initializes the flash controller for programmed control
 * The configuration options are not really understood, the SPI speed
 * seems to be directly linked to the CPU frequency
 * This configures fast single IO at 20.8 MHz when the CPU clock is at 20.8MHz
 * and 62.5MHz when the CPU clock is configured at 125MHz
 */
void flash_init(uint8_t enable_dio)
{
	if (enable_dio) {
		// Configure fast DIO via divider/DIO/SIOconfig = 4 and read-cmd being 0xbb (for mmio)
		SFR_FLASH_CONFIG = 9;  // There may be a chip-select in here
		SFR_FLASH_CONF_RCMD = 0xbb;
		SFR_FLASH_CONF_DIV = 4;
	} else {
		// Configure fast read via divider = 8 and read-cmd being 0xb (for mmio)
		SFR_FLASH_CONFIG = 9;
		SFR_FLASH_CONF_RCMD = 0xb;
		SFR_FLASH_CONF_DIV = 8;
	}
	// Test Controller Busy
	while(SFR_FLASH_EXEC_BUSY);

	// Write 0 to status register
	SFR_FLASH_DUMMYCYCLES = 8;
	SFR_FLASH_MODEB = 0;
	SFR_FLASH_TCONF = 0x19;
	SFR_FLASH_CMD = 1;
	SFR_FLASH_DATA0 = 0;
	SFR_FLASH_EXEC_GO = 1;
	while(SFR_FLASH_EXEC_BUSY);

	dio_enabled = enable_dio;
	flash_configure_mmio();
}


uint8_t flash_read_status(void)
{
	// Test Controller Busy (we might call this directly after executing a command)
	__xdata uint16_t guard = 0;
	while(SFR_FLASH_EXEC_BUSY) {
		if (++guard == 0) return 0xff;	/* controller hung */
	}

	// setup status read command
	SFR_FLASH_TCONF = 0x11;
	SFR_FLASH_CMD_R = 5;

	// execute and wait for controller done
	SFR_FLASH_EXEC_GO = 1;
	guard = 0;
	while(SFR_FLASH_EXEC_BUSY) {
		if (++guard == 0) return 0xff;	/* controller hung */
	}

	return SFR_FLASH_DATA0;
}


/*
 * Reads bulk data of length len from the flash memory starging at address src
 * and writes the data into a buffer pointed to by dst in XMEM
 */
void flash_read_bulk(register __xdata uint8_t *dst, __xdata uint32_t src, register uint16_t len)
{
	short status;
	__xdata uint16_t guard = 0;
	do {
		status = flash_read_status();
		if (status == 0xff) {	/* controller hung: give up */
			print_string("Flash controller timeout\n");
			return;
		}
		if (++guard == 0) { print_string("Flash busy timeout\n"); return; }
	} while (status & 0x1);

	// Set fast read mode
	if (dio_enabled) {
		SFR_FLASH_MODEB = 0x18;
		SFR_FLASH_CMD_R = 0xbb;
		SFR_FLASH_DUMMYCYCLES = 4;
	} else {
		SFR_FLASH_MODEB = 0x0;
		SFR_FLASH_CMD_R = 0xb;	// Fast read
		SFR_FLASH_DUMMYCYCLES = 8;	// Add 8 dummy clocks after read?
	}
	// Read 4 bytes
	SFR_FLASH_TCONF = 4;
	while (len) {
		SFR_FLASH_ADDR16 = src >> 16;
		SFR_FLASH_ADDR8 = src >> 8;
		SFR_FLASH_ADDR0 = src;
		src += 4;

		SFR_FLASH_EXEC_GO = 1;
		while(SFR_FLASH_EXEC_BUSY);

		*dst++ = SFR_FLASH_DATA0;
		if (len == 1)
			return;
		*dst++ = SFR_FLASH_DATA8;
		if (len == 2)
			return;
		*dst++ = SFR_FLASH_DATA16;
		if (len == 3)
			return;
		*dst++ = SFR_FLASH_DATA24;

		len -= 4;
	}
}


uint8_t flash_write_enable(void)
{
	short status;
	__xdata uint16_t guard = 0;

	// Wait until busy bit clear
	do {
		status = flash_read_status();
		if (status == 0xff || ++guard == 0) {
			print_string("Flash busy timeout\n");
			return 1;
		}
	} while (status & 0x1);
// 	while (flash_read_status() & 0x1);

	SFR_FLASH_TCONF = 0x18;
	SFR_FLASH_CMD = 6;
	SFR_FLASH_DUMMYCYCLES = 0;
	SFR_FLASH_MODEB = 0;

	SFR_FLASH_EXEC_GO = 1;
	// Wait for write status enabled
	guard = 0;
	do {
		status = flash_read_status();
		if (status == 0xff || ++guard == 0) {
			print_string("Flash busy timeout\n");
			return 1;
		}
	} while (!(status & 0x2));
	return 0;
}


// Erases the 4k sector in which the address lies
void flash_sector_erase(uint32_t addr)
{
	if (flash_write_enable())	/* timed out */
		return;
	SFR_FLASH_TCONF = 8;
	SFR_FLASH_CMD = 0x20;

	SFR_FLASH_ADDR16 = addr >> 16;
	SFR_FLASH_ADDR8 = addr >> 8;
	SFR_FLASH_ADDR0 = addr;

	SFR_FLASH_EXEC_GO = 1;
	{
		__xdata uint16_t guard = 0;
		while (flash_read_status() & 0x1) {
			if (++guard == 0) { print_string("Flash erase timeout\n"); break; }
		}
	}

	flash_configure_mmio();

}


void flash_write_bytes(__xdata uint32_t addr, __xdata uint8_t *ptr, uint16_t len)
{
	uint8_t exit_loop = 0;
	__xdata uint16_t guard = 0;

	while(1) {
		if (flash_write_enable())	/* timed out */
			return;
		SFR_FLASH_CMD = 2;
		SFR_FLASH_TCONF = 0x40 | 8 | 4; // Bytes written is 4, 8 enables write, 0x40 is unknown
		// Last transfer?
		if (len < 5) {
			SFR_FLASH_TCONF = 8 | len;
			exit_loop = 1;
		}

		SFR_FLASH_ADDR16 = addr >> 16;
		SFR_FLASH_ADDR8 = addr >> 8;
		SFR_FLASH_ADDR0 = addr;
		SFR_FLASH_DATA0 = *ptr++;
		SFR_FLASH_DATA8 = *ptr++;
		SFR_FLASH_DATA16 = *ptr++;
		SFR_FLASH_DATA24 = *ptr++;

		// Execute transfer, we wait for completion at top of loop
		SFR_FLASH_EXEC_GO = 1;
		if (exit_loop)
			break;
		len -= 4;
		addr += 4;
	}

	while (flash_read_status() & 0x1);
	flash_configure_mmio();
}


void reg_write(uint16_t reg_addr)
{
	/* Data to write must be in SFR A4, A5, A6, A7 */
	SFR_REG_ADDR_U16 = reg_addr;
	SFR_EXEC_GO = SFR_EXEC_WRITE_REG;
	do {
	} while (SFR_EXEC_STATUS != 0);
}


// 32-bit big-endian read of the update header (updatebuilder htonl's all
// fields).
uint32_t rd32(__xdata uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | p[3];
}


// Verify the update image header at UPDATE_LOC (F9): magic, header
// checksum, payload length and the updatebuilder payload sum.  Returns 0
// on success, 1 on any mismatch (caller must abort the copy).
uint8_t verify_update_header(void)
{
	// Header: magic, length, header sum, payload sum, reserved
	flash_read_bulk(buffer, UPDATE_LOC, HEADER_LENGTH);
	if (rd32(buffer + 0x00) != HEADER_MAGIC) {
		print_string("Invalid update image (bad magic)\n");
		return 1;
	}
	// Header checksum is the byte sum of the header with the +0x08 field
	// itself still zero (updatebuilder computes it before writing it).
	uint32_t hsum = 0;
	for (uint8_t i = 0; i < 0x08; i++)
		hsum += buffer[i];
	for (uint8_t i = 0x0c; i < HEADER_LENGTH; i++)
		hsum += buffer[i];
	if (hsum != rd32(buffer + 0x08)) {
		print_string("Invalid update image (header checksum)\n");
		return 1;
	}
	uint32_t image_size = rd32(buffer + 0x04) + HEADER_LENGTH;
	if (image_size < SUM_PAYLOAD_START - UPDATE_LOC + 0x1000) {
		print_string("Invalid update image (length)\n");
		return 1;
	}

	// Payload sum, byte-wise over the same ranges as updatebuilder:
	// installer code + 0xff*HEADER_LENGTH + payload.
	uint32_t sum = 0;
	__xdata uint32_t addr;
	__xdata uint16_t range_len;

	range_len = SUM_CODE_END - SUM_CODE_START;
	for (addr = SUM_CODE_START; range_len; ) {
		__xdata uint16_t chunk = range_len > 0x1000 ? 0x1000 : range_len;
		flash_read_bulk(buffer, addr, chunk);
		for (uint16_t k = 0; k < chunk; k++)
			sum += buffer[k];
		addr += chunk;
		range_len -= chunk;
	}
	sum += 0xff * HEADER_LENGTH;
	/* Payload image offset 0x4026 lands at flash SUM_PAYLOAD_START, and
	 * image offset 0x4012 (second header) at flash UPDATE_LOC. */
	{
		__xdata uint32_t payload_end = UPDATE_LOC + image_size - 0x4012;
		range_len = payload_end - SUM_PAYLOAD_START;
	}
	for (addr = SUM_PAYLOAD_START; range_len; ) {
		__xdata uint16_t chunk = range_len > 0x1000 ? 0x1000 : range_len;
		flash_read_bulk(buffer, addr, chunk);
		for (uint16_t k = 0; k < chunk; k++)
			sum += buffer[k];
		addr += chunk;
		range_len -= chunk;
	}
	if (sum != rd32(buffer + 0x0c)) {
		print_string("Invalid update image (payload checksum)\n");
		return 1;
	}
	print_string("Update image header verified\n");
	return 0;
}


void installer(void)
{

	CKCON = 0;	// Initial Clock configuration
	SFR_97 = 0;	// HADDR?

	// Set in managed mode:
	SFR_b9 = 0x00;
	SFR_ba = 0x80;

	// Disable all interrupts (global and individually) by setting IE register (SFR A8) to 0
	IE = 0;
	EIE = 0;  // SFR e8: EIE. Disable all external IRQs

	setup_serial_timer1();
	print_string("\nRTLPlayground installer starting...\n");

	// Initialize flash functions with disable DIO because writing does not work otherwise
	flash_init(0);

	if (verify_update_header()) {
		print_string("Aborting update.\n");
		return;
	}

	__xdata uint32_t dest = 0x0;
	__xdata uint32_t source = UPDATE_CODE_LOC;
	// A 512kByte = 4MBit Flash has 128 sectors, we copy only 120
	for (uint8_t i=0; i < 120; i++) {
		print_string("Moving block\n");
		flash_read_bulk(buffer, source, 0x1000);
		for (uint8_t j = 0; j < 32; j++) {
			write_char(' '); print_byte(buffer[j]);
		}
		write_char('\n');
		flash_sector_erase(dest);
		flash_write_bytes(dest, buffer, 0x1000);
		dest += 0x1000;
		source += 0x1000;
	}
	print_string("Done.\n");
	print_string("Reseting now\n");
	REG_SET(RTL837X_REG_RESET, 1);
}
