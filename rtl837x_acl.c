/*
 * ACL (Tier 3) for the RTL837x platform
 * This code is in the Public Domain
 *
 * Ingress ACL per 新機能追加検証/Tier3/acl.md.  The rule and action
 * tables live in the ITA block shared with the L2/VLAN tables
 * (RTL837X_TBL_CTRL = 0x5cac), so every access waits for the busy bit
 * like the other table drivers.
 *
 * Initial scope: one rule = one template.  Supported match fields:
 *   - mac  aa:bb:cc:dd:ee:ff   (template 0: DMAC)
 *   - vlan <id>                (template 4: CTAG VID)
 *   - ip   <addr>[/mask]       (template 1: IPv4 DIP)
 * Combining several field types in one rule needs multi-template
 * chaining (as the SDK's cfg_add does) and is not implemented yet.
 * Rules apply per port via the rule's active port mask (rule_info
 * bits 11-20).  deny = redirect to an empty port mask (SDK
 * FILTER_ENACT_DROP), permit = no action.  Packets that match no rule
 * are permitted (unmatched-permit per port).
 *
 * ACL rule persistence in the config (commit) is not implemented yet:
 * the rule table lives in the ASIC only.
 *
 * All state lives in XDATA: the 8051 internal RAM overlay is full in
 * this firmware, so no new function parameters or locals may be
 * allocated there.  All public functions are __banked (BANK2).
 */

// #define REGDBG
// #define DEBUG

#include <stdint.h>
#include "rtl837x_common.h"
#include "rtl837x_sfr.h"
#include "rtl837x_regs.h"
#include "rtl837x_acl.h"
#include "machine.h"

#pragma codeseg BANK3
#pragma constseg BANK3

extern __code struct machine machine;
extern __xdata struct machine_runtime machine_detected;
extern __xdata uint8_t sfr_data[4];

/* ACL field codes (SDK dal_rtl8373_acl.h RTL8373_ACLFIELDTYPES) */
#define ACL_F_DMAC0		0x00
#define ACL_F_DMAC1		0x01
#define ACL_F_DMAC2		0x02
#define ACL_F_SMAC0		0x03
#define ACL_F_ETHERTYPE		0x06
#define ACL_F_STAG		0x07
#define ACL_F_CTAG		0x08
#define ACL_F_IP4SIP0		0x10
#define ACL_F_IP4SIP1		0x11
#define ACL_F_IP4DIP0		0x12
#define ACL_F_IP4DIP1		0x13
#define ACL_F_IPTOSPROTO		0x14
#define ACL_F_L4SPORT		0x15
#define ACL_F_L4DPORT		0x16
#define ACL_F_VIDRANGE		0x30
#define ACL_F_IPRANGE		0x31
#define ACL_F_PORTRANGE		0x32
#define ACL_F_FIELD_VALID	0x33
#define ACL_F_SELECT00		0x40

/* The 5 fixed templates (SDK rtl8373_filter_templateField).
 * Written to ACL_TEMPLATE_CTRL as compile-time constants (fields 4-7 in
 * the low 32 bits at reg, fields 0-3 at reg+4) so that no code-space
 * array access (which needs internal-RAM temporaries) is required. */
#define ACL_TPL0_HI 0x4f060504u
#define ACL_TPL0_LO 0x03020100u
#define ACL_TPL1_HI 0x40161514u
#define ACL_TPL1_LO 0x13121110u
#define ACL_TPL2_HI 0x48474645u
#define ACL_TPL2_LO 0x44431312u
#define ACL_TPL3_HI 0x4e4d4c4bu
#define ACL_TPL3_LO 0x4a491110u
#define ACL_TPL4_HI 0x33424107u
#define ACL_TPL4_LO 0x08323130u

/* Rule builder scratch, filled by the caller (cmd_parser.c) */
__xdata uint16_t acl_field[8];      /* 8 x 16-bit rule fields */
__xdata uint16_t acl_care[8];       /* corresponding care bits */
static __xdata uint32_t acl_rule_info;     /* template + port mask + valid */
static __xdata uint8_t acl_idx;            /* rule index 0-95 */
static __xdata uint8_t acl_byte;
static __xdata uint8_t acl_valid;
static __xdata uint16_t acl_pmask;
static __xdata uint32_t acl_d0;            /* display scratch */
static __xdata uint32_t acl_d1;
/* Rule-info word of the rule under inspection, kept as raw bytes */
static __xdata uint8_t acl_ri_b[4];
/* 32-bit words are built byte-wise through this union: compound 32-bit
 * expressions need internal-RAM temporaries, and the DSEG/overlay area
 * is completely full in this firmware. */
static __xdata union {
	uint32_t u32;
	uint8_t b[4];
} acl_u;

/* Wait until the table controller is idle (any outstanding op done) */
static void acl_tbl_busy(void) __banked
{
	do {
		reg_read_m(RTL837X_TBL_CTRL);
	} while (sfr_data[3] & TBL_EXECUTE);
}

/*
 * Write rule `idx` with the current acl_field[]/acl_care[]/acl_rule_info.
 * Follows the SDK's _rtl8373_setAclRule: invalidate first, then care
 * bits, then data bits with the valid bit set.
 */
static void acl_rule_write(__xdata uint8_t idx) __banked
{
	// Walk the field arrays with a byte pointer: variable 16-bit index
	// arithmetic would need an internal-RAM temporary (DSEG is full).
	__xdata uint8_t * __xdata fp;
	__xdata uint8_t w;

	// Invalidate the entry first (valid bit = b[2] bit 5)
	acl_u.b[0] = acl_ri_b[0];
	acl_u.b[1] = acl_ri_b[1];
	acl_u.b[2] = acl_ri_b[2] & ~0x20;
	acl_u.b[3] = acl_ri_b[3];
	REG_SET(RTL837X_ITA_WRITE_DATA0(4), acl_u.u32);
	REG_WRITE(RTL837X_TBL_CTRL, 0, RTL837X_ACLRULETBADDR(1, idx),
		  TB_TARGET_ACLRULE, TB_OP_WRITE << 1 | TB_EXECUTE);
	acl_tbl_busy();

	// Care bits: one word per two 16-bit fields, byte-wise
	fp = (__xdata uint8_t * __xdata)&acl_care[0];
	for (w = 0; w < 4; w++) {
		acl_u.b[0] = fp[0];
		acl_u.b[1] = fp[1];
		acl_u.b[2] = fp[2];
		acl_u.b[3] = fp[3];
		REG_SET(RTL837X_ITA_WRITE_DATA0(w), acl_u.u32);
		fp += 4;
	}
	REG_WRITE(RTL837X_TBL_CTRL, 0, RTL837X_ACLRULETBADDR(0, idx),
		  TB_TARGET_ACLRULE, TB_OP_WRITE << 1 | TB_EXECUTE);
	acl_tbl_busy();

	// Data bits with valid = 1
	fp = (__xdata uint8_t * __xdata)&acl_field[0];
	for (w = 0; w < 4; w++) {
		acl_u.b[0] = fp[0];
		acl_u.b[1] = fp[1];
		acl_u.b[2] = fp[2];
		acl_u.b[3] = fp[3];
		REG_SET(RTL837X_ITA_WRITE_DATA0(w), acl_u.u32);
		fp += 4;
	}
	acl_u.b[0] = acl_ri_b[0];
	acl_u.b[1] = acl_ri_b[1];
	acl_u.b[2] = acl_ri_b[2];
	acl_u.b[3] = acl_ri_b[3];
	REG_SET(RTL837X_ITA_WRITE_DATA0(4), acl_u.u32);
	REG_WRITE(RTL837X_TBL_CTRL, 0, RTL837X_ACLRULETBADDR(1, idx),
		  TB_TARGET_ACLRULE, TB_OP_WRITE << 1 | TB_EXECUTE);
	acl_tbl_busy();
}

/* Read the rule-info word of rule `idx` into acl_ri_b[] (LSB-first).
 * NOTE: reg_read_m fills sfr_data big-endian (sfr_data[0] = bits
 * 24-31, sfr_data[3] = bits 0-7), so the copy reverses the bytes. */
static void acl_rule_info_get(__xdata uint8_t idx) __banked
{
	REG_WRITE(RTL837X_TBL_CTRL, 0, RTL837X_ACLRULETBADDR(1, idx),
		  TB_TARGET_ACLRULE, TB_OP_READ << 1 | TB_EXECUTE);
	acl_tbl_busy();
	reg_read_m(RTL837X_ITA_READ_DATA0(4));
	acl_ri_b[0] = sfr_data[3];
	acl_ri_b[1] = sfr_data[2];
	acl_ri_b[2] = sfr_data[1];
	acl_ri_b[3] = sfr_data[0];
}

/*
 * Enable/disable the ACL engine on all user ports.
 * enable: 1 = all ports enabled, 0 = all disabled.
 */
void acl_enable(__xdata uint8_t enable) __banked
{
	acl_pmask = machine_detected.isRTL8373 ? PMASK_9 : PMASK_6;
	reg_read_m(RTL837X_ACL_PORT_EN);
	if (enable) {
		sfr_mask_data(0, acl_pmask & 0xff, acl_pmask & 0xff);
		sfr_mask_data(1, (acl_pmask >> 8) & 0xff, (acl_pmask >> 8) & 0xff);
	} else {
		sfr_mask_data(0, acl_pmask & 0xff, 0);
		sfr_mask_data(1, (acl_pmask >> 8) & 0xff, 0);
	}
	reg_write_m(RTL837X_ACL_PORT_EN);
	if (enable)
		print_string("ACL enabled\n");
	else
		print_string("ACL disabled\n");
}

/*
 * One-time setup: reset the rule/action tables, program the 5 templates,
 * enable the engine on all user ports with unmatched-permit.
 */
void acl_setup(void) __banked
{
	// Reset the rule and action tables
	for (acl_idx = 0; acl_idx < RTL837X_ACLRULENO; acl_idx++)
		REG_SET(RTL837X_ACL_ACT_CTRL(acl_idx), 0xff);
	reg_read_m(RTL837X_ACL_CTRL);
	sfr_mask_data(0, ACL_CTRL_TABLE_RST, ACL_CTRL_TABLE_RST);
	reg_write_m(RTL837X_ACL_CTRL);

	// Program the 5 templates (compile-time constants, no array access)
	REG_SET(RTL837X_ACL_TEMPLATE_CTRL(0), ACL_TPL0_HI);
	REG_SET(RTL837X_ACL_TEMPLATE_CTRL(0) + 4, ACL_TPL0_LO);
	REG_SET(RTL837X_ACL_TEMPLATE_CTRL(1), ACL_TPL1_HI);
	REG_SET(RTL837X_ACL_TEMPLATE_CTRL(1) + 4, ACL_TPL1_LO);
	REG_SET(RTL837X_ACL_TEMPLATE_CTRL(2), ACL_TPL2_HI);
	REG_SET(RTL837X_ACL_TEMPLATE_CTRL(2) + 4, ACL_TPL2_LO);
	REG_SET(RTL837X_ACL_TEMPLATE_CTRL(3), ACL_TPL3_HI);
	REG_SET(RTL837X_ACL_TEMPLATE_CTRL(3) + 4, ACL_TPL3_LO);
	REG_SET(RTL837X_ACL_TEMPLATE_CTRL(4), ACL_TPL4_HI);
	REG_SET(RTL837X_ACL_TEMPLATE_CTRL(4) + 4, ACL_TPL4_LO);

	// Engine on all user ports, unmatched packets permitted
	acl_enable(1);
	acl_pmask = machine_detected.isRTL8373 ? PMASK_9 : PMASK_6;
	reg_read_m(RTL837X_ACL_PORT_UNMATCH_PERMIT);
	sfr_mask_data(0, acl_pmask & 0xff, acl_pmask & 0xff);
	sfr_mask_data(1, (acl_pmask >> 8) & 0xff, (acl_pmask >> 8) & 0xff);
	reg_write_m(RTL837X_ACL_PORT_UNMATCH_PERMIT);

	print_string("acl_setup done\n");
}

/*
 * Add one rule.  port: logical port number, action: 0 = permit, 1 = deny,
 * template: 0 = mac, 1 = ip, 4 = vlan.  The match data comes from the
 * acl_field[]/acl_care[] scratch (filled by the caller in cmd_parser).
 */
uint8_t acl_rule_add(__xdata uint8_t port, __xdata uint8_t action, __xdata uint8_t template) __banked
{
	// Find a free rule slot
	for (acl_idx = 0; acl_idx < RTL837X_ACLRULENO; acl_idx++) {
		acl_tbl_busy();
		acl_rule_info_get(acl_idx);
		if (!((acl_ri_b[2] >> 5) & 1))       // valid bit = b[2] bit 5
			break;
	}
	if (acl_idx >= RTL837X_ACLRULENO) {
		print_string("ACL rule table full\n");
		return 1;
	}

	// Build the rule-info word byte-wise: template (bits 0-2),
	// active port mask bit at 11+port, valid bit 21 (= b[2] bit 5)
	acl_ri_b[0] = template;
	acl_ri_b[1] = 0;
	acl_ri_b[2] = 0;
	acl_ri_b[3] = 0;
	if (port <= 4)
		acl_ri_b[1] = (uint8_t)(1 << (port + 3));
	else
		acl_ri_b[2] = (uint8_t)(1 << (port - 5));
	acl_ri_b[2] |= 0x20;                     // valid

	acl_tbl_busy();
	acl_rule_write(acl_idx);

	// Action: deny = redirect to an empty port mask, permit = no action
	if (action) {
		REG_SET(RTL837X_ACL_ACT_CTRL(acl_idx), ACL_ACT_CTRL_FWD);
	} else {
		REG_SET(RTL837X_ACL_ACT_CTRL(acl_idx), 0);
	}

	// ACT entry: word 0 and 2 are zero; word 1 has fwdAct=REDIRECT
	// (bit 17 = b[2] bit 1) for deny
	acl_u.b[0] = 0; acl_u.b[1] = 0;
	acl_u.b[2] = action ? 0x02 : 0;
	acl_u.b[3] = 0;
	REG_SET(RTL837X_ITA_WRITE_DATA0(0), 0);
	REG_SET(RTL837X_ITA_WRITE_DATA0(1), acl_u.u32);
	REG_SET(RTL837X_ITA_WRITE_DATA0(2), 0);
	REG_WRITE(RTL837X_TBL_CTRL, 0, acl_idx, TB_TARGET_ACLACT,
		  TB_OP_WRITE << 1 | TB_EXECUTE);
	acl_tbl_busy();

	print_string("ACL rule ");
	print_short(acl_idx);
	print_string(" added\n");
	return 0;
}

/* Delete one rule by index */
void acl_rule_del(__xdata uint8_t idx) __banked
{
	if (idx >= RTL837X_ACLRULENO) {
		print_string("ACL rule index 0-95\n");
		return;
	}
	acl_tbl_busy();
	acl_rule_info_get(idx);
	if (!((acl_ri_b[2] >> 5) & 1)) {
		print_string("ACL rule empty\n");
		return;
	}

	// Invalidate (clear the valid bit) and clear the action
	acl_u.b[0] = acl_ri_b[0];
	acl_u.b[1] = acl_ri_b[1];
	acl_u.b[2] = acl_ri_b[2] & ~0x20;
	acl_u.b[3] = acl_ri_b[3];
	REG_SET(RTL837X_ITA_WRITE_DATA0(4), acl_u.u32);
	REG_WRITE(RTL837X_TBL_CTRL, 0, RTL837X_ACLRULETBADDR(1, idx),
		  TB_TARGET_ACLRULE, TB_OP_WRITE << 1 | TB_EXECUTE);
	acl_tbl_busy();
	REG_SET(RTL837X_ACL_ACT_CTRL(idx), 0);
	print_string("ACL rule ");
	print_short(idx);
	print_string(" deleted\n");
}

/* Show all valid rules (read back from the ASIC) */
void acl_show(void) __banked
{
	print_string("Idx Tpl Pmsk Act  Match\n");
	for (acl_idx = 0; acl_idx < RTL837X_ACLRULENO; acl_idx++) {
		acl_tbl_busy();
		acl_rule_info_get(acl_idx);
		if (!((acl_ri_b[2] >> 5) & 1))
			continue;

		// Active port mask: bits 11-20.  Bits 11-15 = b[1] bits 3-7
		// (pmask bits 0-4), bits 16-20 = b[2] bits 0-4 (pmask bits 5-9)
		acl_pmask = (uint16_t)((acl_ri_b[2] & 0x1f) << 5) | (uint16_t)(acl_ri_b[1] >> 3);
		acl_byte = acl_ri_b[0] & 0x7;

		// Action: the ACT_CTRL FWD bit (bit 5 = sfr_data[3] bit 5,
		// sfr_data is big-endian)
		reg_read_m(RTL837X_ACL_ACT_CTRL(acl_idx));
		acl_valid = (sfr_data[3] >> 5) & 1;

		print_short(acl_idx);
		write_char(' ');
		itoa(acl_byte);
		write_char(' ');
		print_short(acl_pmask);
		print_string(acl_valid ? " deny  " : " permit");

		reg_read_m(RTL837X_ITA_READ_DATA0(0));
		acl_u.b[0] = sfr_data[3]; acl_u.b[1] = sfr_data[2];
		acl_u.b[2] = sfr_data[1]; acl_u.b[3] = sfr_data[0];
		acl_d0 = acl_u.u32;
		reg_read_m(RTL837X_ITA_READ_DATA0(1));
		acl_u.b[0] = sfr_data[3]; acl_u.b[1] = sfr_data[2];
		acl_u.b[2] = sfr_data[1]; acl_u.b[3] = sfr_data[0];
		acl_d1 = acl_u.u32;

		if (acl_byte == 0) {
			// DMAC: mac[5]..mac[0]
			print_string(" mac ");
			itoa((acl_d1 >> 8) & 0xff); write_char(':');
			itoa(acl_d1 & 0xff); write_char(':');
			itoa((acl_d0 >> 24) & 0xff); write_char(':');
			itoa((acl_d0 >> 16) & 0xff); write_char(':');
			itoa((acl_d0 >> 8) & 0xff); write_char(':');
			itoa(acl_d0 & 0xff);
		} else if (acl_byte == 1) {
			// DIP: field[2] = ip low 16 bits, field[3] = ip high 16
			// bits, both in word 1 (acl_d1)
			print_string(" ip ");
			itoa((acl_d1 >> 24) & 0xff); write_char('.');
			itoa((acl_d1 >> 16) & 0xff); write_char('.');
			itoa((acl_d1 >> 8) & 0xff); write_char('.');
			itoa(acl_d1 & 0xff);
		} else if (acl_byte == 4) {
			// CTAG VID
			print_string(" vlan ");
			print_short((acl_d1 >> 16) & 0xfff);
		}
		write_char('\n');
	}
	print_string("ACL show done\n");
}
