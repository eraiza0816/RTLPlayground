/*
 * Storm control (Tier 3) for the RTL837x platform
 * This code is in the Public Domain
 *
 * Implementation phase 1 per 新機能追加検証/Tier3/storm-control.md:
 * the extended storm control path (CFG_STORM_EXT 0x5514 + STORM_EXT_MTRIDX
 * 0x5518) with one shared meter per storm type (BC=0, MC=1, unk-UC=2,
 * unk-MC=3).  The shared meter rate is a 24-bit value in kbps (mode bit 0)
 * or pps (mode bit 1); burst is a 28-bit value (max = unlimited).
 * Register layouts were taken from the RTL8373 SDK
 * (dal_rtl8373_storm.c / dal_rtl8373_sharemeter.c).
 *
 * All state lives in XDATA: the 8051 internal RAM overlay is full in
 * this firmware, so no new function parameters or locals may be
 * allocated there — every value travels through the static XDATA
 * scratch below.  All public functions are __banked (BANK2); omitting
 * it causes a plain lcall across banks and a boot crash (see
 * 新機能追加検証/README.md).
 */

// #define REGDBG
// #define DEBUG

#include <stdint.h>
#include "rtl837x_common.h"
#include "rtl837x_sfr.h"
#include "rtl837x_regs.h"
#include "rtl837x_storm.h"
#include "machine.h"

#pragma codeseg BANK3
#pragma constseg BANK3

extern __code struct machine machine;
extern __xdata struct machine_runtime machine_detected;
extern __xdata uint8_t sfr_data[4];

/* Software state for `show running-config` serialization */
__xdata uint8_t storm_type_en[4];
__xdata uint32_t storm_type_rate[4];

static __xdata uint8_t storm_type;
static __xdata uint32_t storm_rate;
static __xdata uint8_t storm_byte;

__code char * __code storm_names[4] = {
	"broadcast", "multicast", "dlf", "unknown-mcast"
};

/*
 * Program one shared meter: 24-bit rate (kbps or pps), maximum burst,
 * kbps mode (mode bit clear), IFG excluded (IPG bit clear, like the
 * bandwidth module does for IGBW/EGBW).
 */
static void storm_meter_set(__xdata uint8_t type, __xdata uint32_t rate, __xdata uint8_t pps) __banked
{
	__xdata uint8_t * __xdata rp = (__xdata uint8_t * __xdata)&rate;

	REG_WRITE(RTL837X_SHARED_METER_RATE_CTRL(type), 0, rp[2], rp[1], rp[0]);
	REG_SET(RTL837X_SHARED_METER_BURST_CTRL(type), 0x0fffffff);

	// Mode bit: 0 = kbps, 1 = pps
	reg_read_m(RTL837X_SHARED_METER_MODE(type));
	sfr_mask_data(0, 1 << METER_MODE_PPS_BIT(type), pps ? (1 << METER_MODE_PPS_BIT(type)) : 0);
	reg_write_m(RTL837X_SHARED_METER_MODE(type));

	// IPG counter bit: 0 = do not count IFG
	reg_read_m(RTL837X_SHARED_METER_IPG_CTRL(type));
	sfr_mask_data(0, 1 << METER_IPG_CNTR_BIT(type), 0);
	reg_write_m(RTL837X_SHARED_METER_IPG_CTRL(type));
}

/*
 * Set the port mask in CFG_STORM_EXT (bits 4-13).
 * pmask: the ASIC port mask (PMASK_9 / PMASK_6).
 */
static void storm_ext_portmask(__xdata uint16_t pmask) __banked
{
	reg_read_m(RTL837X_CFG_STORM_EXT);
	storm_byte = (uint8_t)((pmask & 0xf) << 4);
	sfr_mask_data(0, 0xf0, storm_byte);
	storm_byte = (uint8_t)(pmask >> 4);
	sfr_mask_data(1, 0x3f, storm_byte);
	reg_write_m(RTL837X_CFG_STORM_EXT);
}

/*
 * Enable storm control for one storm type on all user ports.
 * type: 0 = broadcast, 1 = multicast, 2 = unknown unicast (dlf),
 *       3 = unknown multicast.
 */
void storm_control_on(__xdata uint8_t type, __xdata uint32_t rate, __xdata uint8_t pps) __banked
{
	if (type > 3) {
		print_string("Invalid storm type\n");
		return;
	}

	storm_meter_set(type, rate, pps);

	// Extended storm control: set the type enable bit and the port mask
	storm_ext_portmask(machine_detected.isRTL8373 ? PMASK_9 : PMASK_6);
	reg_read_m(RTL837X_CFG_STORM_EXT);
	sfr_mask_data(0, 1 << type, 1 << type);
	reg_write_m(RTL837X_CFG_STORM_EXT);

	storm_type_en[type] = 1;
	storm_type_rate[type] = rate;
	print_string("Storm control enabled, type: ");
	storm_print_type(type);
	write_char('\n');
}

/* Disable storm control for one type (or all types with type == 4) */
void storm_control_off(__xdata uint8_t type) __banked
{
	reg_read_m(RTL837X_CFG_STORM_EXT);
	if (type == 4) {
		sfr_mask_data(0, 0x0f, 0);
		storm_type_en[0] = storm_type_en[1] = storm_type_en[2] = storm_type_en[3] = 0;
	} else if (type > 3) {
		print_string("Invalid storm type\n");
		return;
	} else {
		sfr_mask_data(0, 1 << type, 0);
		storm_type_en[type] = 0;
	}
	reg_write_m(RTL837X_CFG_STORM_EXT);
	if (type == 4)
		print_string("Storm control disabled (all types)\n");
	else {
		print_string("Storm control disabled, type: ");
		storm_print_type(type);
		write_char('\n');
	}
}

void storm_control_status(void) __banked
{
	print_string("CFG_STORM_EXT (0x5514): ");
	reg_read_m(RTL837X_CFG_STORM_EXT);
	print_sfr_data();
	write_char('\n');
	print_string("STORM_EXT_MTRIDX (0x5518): ");
	reg_read_m(RTL837X_STORM_EXT_MTRIDX_CFG);
	print_sfr_data();
	write_char('\n');

	for (storm_type = 0; storm_type < 4; storm_type++) {
		storm_print_type(storm_type);
		print_string(": ");
		reg_read_m(RTL837X_SHARED_METER_RATE_CTRL(storm_type));
		print_sfr_data();
		reg_read_m(RTL837X_SHARED_METER_MODE(storm_type));
		storm_byte = (sfr_data[0] >> METER_MODE_PPS_BIT(storm_type)) & 1;
		print_string(storm_byte ? " pps\n" : " kbps\n");
	}
}

/*
 * One-time setup: assign the shared meters, make sure nothing is
 * limited until a storm-control on command is given, and exempt the
 * RMA frames (BPDU/LLDP/...) of RMA group 0 from storm control.
 */
void storm_control_setup(void) __banked
{
	// Meter index per type: BC=0, MC=1, unk-UC=2, unk-MC=3
	storm_rate = (0 << 0) | (1 << 8) | ((uint32_t)2 << 16) | ((uint32_t)3 << 24);
	REG_SET(RTL837X_STORM_EXT_MTRIDX_CFG, storm_rate);

	// All meters unlimited to start with
	REG_SET(RTL837X_SHARED_METER_RATE_CTRL(0), 0x00ffffff);
	REG_SET(RTL837X_SHARED_METER_RATE_CTRL(1), 0x00ffffff);
	REG_SET(RTL837X_SHARED_METER_RATE_CTRL(2), 0x00ffffff);
	REG_SET(RTL837X_SHARED_METER_RATE_CTRL(3), 0x00ffffff);
	REG_SET(RTL837X_SHARED_METER_BURST_CTRL(0), 0x0fffffff);
	REG_SET(RTL837X_SHARED_METER_BURST_CTRL(1), 0x0fffffff);
	REG_SET(RTL837X_SHARED_METER_BURST_CTRL(2), 0x0fffffff);
	REG_SET(RTL837X_SHARED_METER_BURST_CTRL(3), 0x0fffffff);
	REG_SET(RTL837X_SHARED_METER_MODE(0), 0);
	REG_SET(RTL837X_SHARED_METER_MODE(1), 0);
	REG_SET(RTL837X_SHARED_METER_MODE(2), 0);
	REG_SET(RTL837X_SHARED_METER_MODE(3), 0);

	// Disable storm control (clear the type enables, keep the port mask)
	storm_ext_portmask(machine_detected.isRTL8373 ? PMASK_9 : PMASK_6);
	reg_read_m(RTL837X_CFG_STORM_EXT);
	sfr_mask_data(0, 0x0f, 0);
	reg_write_m(RTL837X_CFG_STORM_EXT);

	// Exempt RMA group 0 (BPDU/LLDP and other control frames) from storm control
	reg_read_m(RTL837X_RMA0_CONF);
	sfr_mask_data(0, RMA_DIS_STORM_CTRL, RMA_DIS_STORM_CTRL);
	reg_write_m(RTL837X_RMA0_CONF);

	print_string("storm_control_setup done\n");
}

/* Print the name of a storm type (used from BANK2 code only) */
void storm_print_type(__xdata uint8_t type) __banked
{
	if (type > 3)
		print_string("?");
	else
		print_string(storm_names[type]);
}
