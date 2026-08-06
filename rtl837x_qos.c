/*
 * QoS (Tier 3) for the RTL837x platform
 * This code is in the Public Domain
 *
 * Implements the initial QoS scope per 新機能追加検証/Tier3/qos.md:
 *  - qos on/off                    (priority decision weight tables)
 *  - qos mode pcp|dscp|both        (dot1q vs dscp based priority)
 *  - qos pcp <0-7> <queue>         (PCP -> queue mapping, all ports)
 *  - qos dscp <0-63> <queue>       (DSCP -> internal priority remap;
 *                                   with the identity pri->queue mapping
 *                                   this equals DSCP -> queue)
 *  - qos sched <port> strict|wfq [weight]
 * Register layouts were taken from the RTL8373 SDK (dal_rtl8373_qos.c):
 *  - QID_TO_PRI (0x51a4 + port*4): 3 bits per priority at pri*4
 *  - PRI_WEIGHT (0x5198 + idx*4):   5 x 5-bit weights, higher wins
 *  - DOT1Q_PRI_REMAP (0x5174):      PCP -> internal priority, 3 bits each
 *  - PRI_SEL_REMAP_DSCP (0x5178):   DSCP -> internal priority, 3 bits each
 *  - SCHED_PORT_Q_CTRL_SET (0x1d28 + port*1024 + qid*4): STRICT_EN bit 7,
 *    WFQ weight bits 0-6 (0 = strict priority queue)
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
#include "rtl837x_qos.h"
#include "machine.h"

#pragma codeseg BANK3
#pragma constseg BANK3

extern __code struct machine machine;
extern __xdata uint8_t sfr_data[4];

/* Software state for `show running-config` serialization */
__xdata uint8_t qos_mode;            /* 0 = off, 1 = pcp, 2 = dscp, 3 = both */
__xdata uint8_t qos_pcp_map[8];      /* PCP -> queue */
__xdata uint8_t qos_dscp_map[64];    /* DSCP -> internal priority */

static __xdata uint8_t qos_port;
static __xdata uint8_t qos_idx;
static __xdata uint8_t qos_val;
/* 32-bit register values are built byte-wise through this union: a
 * compound 32-bit expression needs an internal-RAM temporary, and the
 * DSEG/overlay area is completely full in this firmware. */
static __xdata union {
	uint32_t u32;
	uint8_t b[4];
} qos_u;

/* Pointer into the DSCP map, adjusted byte-wise (pointer arithmetic on
 * an XDATA pointer would spill a 2-byte temporary to DSEG) */
static __xdata union {
	uint8_t __xdata * p;
	uint8_t b[2];
} qos_dp_u;

/* Default priority decision weights (SDK g_priorityDecision):
 * DOT1Q=1, PORT=16, DSCP=4, ACL=2, SVLAN=8 */
#define QOS_WEIGHT_DEFAULT	((uint32_t)0x01 | ((uint32_t)16 << PRI_WEIGHT_PORT_OFFSET) | ((uint32_t)4 << PRI_WEIGHT_DSCP_OFFSET) | ((uint32_t)2 << PRI_WEIGHT_ACL_OFFSET) | ((uint32_t)8 << PRI_WEIGHT_SVLAN_OFFSET))
/* Only the DOT1Q (PCP) source decides */
#define QOS_WEIGHT_PCP		((uint32_t)16 << PRI_WEIGHT_DOT1Q_OFFSET)
/* Only the DSCP source decides */
#define QOS_WEIGHT_DSCP		((uint32_t)16 << PRI_WEIGHT_DSCP_OFFSET)
/* Both PCP and DSCP, with the higher internal priority winning */
#define QOS_WEIGHT_BOTH		(((uint32_t)16 << PRI_WEIGHT_DOT1Q_OFFSET) | ((uint32_t)16 << PRI_WEIGHT_DSCP_OFFSET))


static void qos_write_weights(__xdata uint32_t weight) __banked
{
	__xdata uint8_t * __xdata wp = (__xdata uint8_t * __xdata)&weight;

	REG_WRITE(RTL837X_PRI_WEIGHT, wp[3], wp[2], wp[1], wp[0]);
	REG_WRITE(RTL837X_PRI_WEIGHT + 4, wp[3], wp[2], wp[1], wp[0]);
}


/* Apply the pri->queue map (QID_TO_PRI) for one port */
static void qos_apply_pcp_map(__xdata uint8_t port) __banked
{
	// 3 bits per priority at pri*4: byte-wise build, no 32-bit temps
	qos_u.b[0] = (uint8_t)(qos_pcp_map[0] | (qos_pcp_map[1] << 4));
	qos_u.b[1] = (uint8_t)(qos_pcp_map[2] | (qos_pcp_map[3] << 4));
	qos_u.b[2] = (uint8_t)(qos_pcp_map[4] | (qos_pcp_map[5] << 4));
	qos_u.b[3] = (uint8_t)(qos_pcp_map[6] | (qos_pcp_map[7] << 4));
	REG_SET(RTL837X_QID_TO_PRI + (port << 2), qos_u.u32);
}


/*
 * One-time setup, mirroring dal_rtl8373_qos_init(): identity PCP->pri and
 * pri->queue mappings, DSCP remap all zero, default weight table, port
 * priority 0, remarking disabled, all queues strict priority.
 */
void qos_setup(void) __banked
{
	for (qos_idx = 0; qos_idx < 8; qos_idx++)
		qos_pcp_map[qos_idx] = qos_idx;
	for (qos_idx = 0; qos_idx < 64; qos_idx++)
		qos_dscp_map[qos_idx] = 0;
	qos_mode = 0;

	// PCP -> internal priority: identity
	REG_SET(RTL837X_DOT1Q_PRI_REMAP, 0x76543210);

	// DSCP -> internal priority: all 0 (7 registers, 10 values each)
	for (qos_idx = 0; qos_idx < 7; qos_idx++)
		REG_SET(RTL837X_PRI_SEL_REMAP_DSCP + (qos_idx << 2), 0);

	// Default weight table (both tables)
	qos_write_weights(QOS_WEIGHT_DEFAULT);

	// Per-port weight table select: table 0
	for (qos_port = machine.min_port; qos_port <= machine.max_port; qos_port++) {
		reg_read_m(RTL837X_PORT_WEIGHT_SEL);
		qos_idx = qos_port % 10;
		sfr_mask_data(qos_idx >> 3, 1 << (qos_idx & 7), 0);
		reg_write_m(RTL837X_PORT_WEIGHT_SEL);

		// Port-based priority: 0
		reg_read_m(RTL837X_PORT_PRI);
		qos_idx = (qos_port % 10) * 3;
		sfr_mask_data(qos_idx >> 3, 0x07 << (qos_idx & 7), 0);
		reg_write_m(RTL837X_PORT_PRI);

		// Remarking disabled
		reg_read_m(RTL837X_RMK_PORT_CTRL(qos_port));
		sfr_mask_data(0, RMK_PORT_IPRI_RMK_EN | RMK_PORT_DSCP_RMK_EN, 0);
		reg_write_m(RTL837X_RMK_PORT_CTRL(qos_port));

		// pri -> queue: identity
		qos_apply_pcp_map(qos_port);

		// All queues strict priority
		for (qos_idx = 0; qos_idx < 8; qos_idx++) {
			reg_read_m(RTL837X_SCHED_PORT_Q_CTRL_SET(qos_port, qos_idx));
			sfr_mask_data(0, SCHED_Q_STRICT_EN, SCHED_Q_STRICT_EN);
			reg_write_m(RTL837X_SCHED_PORT_Q_CTRL_SET(qos_port, qos_idx));
		}
	}

	print_string("qos_setup done\n");
}


/* qos on: enable the PCP based priority decision (weights above) */
void qos_on(void) __banked
{
	qos_mode = 1;
	qos_write_weights(QOS_WEIGHT_PCP);
	print_string("QoS enabled (PCP priority)\n");
}

/* qos off: restore the default weight table (port priority dominant) */
void qos_off(void) __banked
{
	qos_mode = 0;
	qos_write_weights(QOS_WEIGHT_DEFAULT);
	print_string("QoS disabled\n");
}

/* qos mode pcp|dscp|both */
void qos_mode_set(__xdata uint8_t mode) __banked
{
	if (mode == 1) {
		qos_write_weights(QOS_WEIGHT_PCP);
		print_string("QoS mode: PCP (802.1p)\n");
	} else if (mode == 2) {
		qos_write_weights(QOS_WEIGHT_DSCP);
		print_string("QoS mode: DSCP\n");
	} else if (mode == 3) {
		qos_write_weights(QOS_WEIGHT_BOTH);
		print_string("QoS mode: PCP + DSCP\n");
	} else {
		print_string("Usage: qos mode pcp|dscp|both\n");
		return;
	}
	qos_mode = mode;
}

/* qos pcp <0-7> <queue>: PCP -> queue mapping on all ports */
void qos_pcp_set(__xdata uint8_t pri, __xdata uint8_t queue) __banked
{
	if (pri > 7 || queue > 7) {
		print_string("PCP and queue must be 0-7\n");
		return;
	}
	qos_pcp_map[pri] = queue;
	for (qos_port = machine.min_port; qos_port <= machine.max_port; qos_port++)
		qos_apply_pcp_map(qos_port);
	print_string("PCP ");
	itoa(pri);
	print_string(" -> queue ");
	itoa(queue);
	write_char('\n');
}

/* qos dscp <0-63> <queue>: DSCP -> internal priority remap on all ports.
 * With the identity pri->queue map this gives DSCP -> queue. */
void qos_dscp_set(__xdata uint8_t dscp, __xdata uint8_t queue) __banked
{
	// 3 bits per DSCP value, 10 values per register (4 in the last one).
	// The register word is built byte-wise (see qos_u): 32-bit compound
	// expressions and 16-bit divisions need internal-RAM temporaries,
	// and the DSEG/overlay area is completely full in this firmware.
	__xdata uint8_t g = 0;
	__xdata uint8_t off = 0;
	__xdata uint8_t t;

	if (dscp > 63 || queue > 7) {
		print_string("DSCP must be 0-63, queue 0-7\n");
		return;
	}
	qos_dscp_map[dscp] = queue;

	// g = dscp / 10, off = (dscp / 10) * 10 — byte math only
	t = dscp;
	while (t >= 10) {
		t -= 10;
		g++;
		off += 10;
	}

	// Point into the map at the group base, byte-wise (offsets here
	// never exceed 255, so only the low byte can carry)
	qos_dp_u.p = (__xdata uint8_t * __xdata)&qos_dscp_map[0];
	qos_dp_u.b[0] += off;
	if (qos_dp_u.b[0] < off)
		qos_dp_u.b[1]++;

	// Build the register bytes one statement at a time: compound
	// pointer-deref expressions would spill a temporary to DSEG
	if (g == 6) {
		// Last register: only DSCP 60-63 (bits 0-11)
		qos_dp_u.b[0] += 3;                 // point at map[63]
		if (qos_dp_u.b[0] < 3)
			qos_dp_u.b[1]++;
		qos_u.b[0] = qos_dp_u.p[-3];
		qos_u.b[0] |= (uint8_t)(qos_dp_u.p[-2] << 3);
		qos_u.b[0] |= (uint8_t)(qos_dp_u.p[-1] << 6);
		qos_u.b[1] = (uint8_t)(qos_dp_u.p[-1] >> 2);
		qos_u.b[1] |= (uint8_t)(qos_dp_u.p[0] << 1);
		qos_u.b[2] = 0;
		qos_u.b[3] = 0;
	} else {
		qos_dp_u.b[0] += 9;                 // point at map[g*10+9]
		if (qos_dp_u.b[0] < 9)
			qos_dp_u.b[1]++;
		qos_u.b[0] = qos_dp_u.p[-9];
		qos_u.b[0] |= (uint8_t)(qos_dp_u.p[-8] << 3);
		qos_u.b[0] |= (uint8_t)(qos_dp_u.p[-7] << 6);
		qos_u.b[1] = (uint8_t)(qos_dp_u.p[-7] >> 2);
		qos_u.b[1] |= (uint8_t)(qos_dp_u.p[-6] << 1);
		qos_u.b[1] |= (uint8_t)(qos_dp_u.p[-5] << 4);
		qos_u.b[1] |= (uint8_t)(qos_dp_u.p[-4] << 7);
		qos_u.b[2] = (uint8_t)(qos_dp_u.p[-4] >> 1);
		qos_u.b[2] |= (uint8_t)(qos_dp_u.p[-3] << 2);
		qos_u.b[2] |= (uint8_t)(qos_dp_u.p[-2] << 5);
		qos_u.b[3] = qos_dp_u.p[-1];
		qos_u.b[3] |= (uint8_t)(qos_dp_u.p[0] << 3);
	}

	qos_val = (uint8_t)(g << 2);
	REG_SET(RTL837X_PRI_SEL_REMAP_DSCP + qos_val, qos_u.u32);
	print_string("DSCP ");
	itoa(dscp);
	print_string(" -> queue ");
	itoa(queue);
	write_char('\n');
}

/* qos sched <port> strict|wfq [weight] */
void qos_sched_set(__xdata uint8_t port, __xdata uint8_t wfq, __xdata uint8_t weight) __banked
{
	if (wfq) {
		for (qos_idx = 0; qos_idx < 8; qos_idx++) {
			reg_read_m(RTL837X_SCHED_PORT_Q_CTRL_SET(port, qos_idx));
			sfr_mask_data(0, SCHED_Q_STRICT_EN, 0);
			sfr_mask_data(0, SCHED_Q_WEIGHT_MASK, weight & SCHED_Q_WEIGHT_MASK);
			reg_write_m(RTL837X_SCHED_PORT_Q_CTRL_SET(port, qos_idx));
		}
		print_string("WFQ weight ");
		itoa(weight);
		print_string(" on all queues, port ");
		itoa(machine.log_to_phys_port[port]);
		write_char('\n');
	} else {
		for (qos_idx = 0; qos_idx < 8; qos_idx++) {
			reg_read_m(RTL837X_SCHED_PORT_Q_CTRL_SET(port, qos_idx));
			sfr_mask_data(0, SCHED_Q_STRICT_EN, SCHED_Q_STRICT_EN);
			reg_write_m(RTL837X_SCHED_PORT_Q_CTRL_SET(port, qos_idx));
		}
		print_string("Strict priority on all queues, port ");
		itoa(machine.log_to_phys_port[port]);
		write_char('\n');
	}
}

void qos_status(void) __banked
{
	print_string("Mode: ");
	if (qos_mode == 1) print_string("pcp\n");
	else if (qos_mode == 2) print_string("dscp\n");
	else if (qos_mode == 3) print_string("both\n");
	else print_string("off\n");

	print_string("PRI_WEIGHT: ");
	reg_read_m(RTL837X_PRI_WEIGHT);
	print_sfr_data();
	write_char('\n');

	print_string("PCP->queue:");
	for (qos_idx = 0; qos_idx < 8; qos_idx++) {
		write_char(' ');
		itoa(qos_idx);
		write_char(':');
		itoa(qos_pcp_map[qos_idx]);
	}
	write_char('\n');

	print_string("DSCP->queue:");
	for (qos_idx = 0; qos_idx < 64; qos_idx += 4) {
		write_char(' ');
		itoa(qos_idx);
		write_char(':');
		itoa(qos_dscp_map[qos_idx]);
	}
	write_char('\n');

	print_string("Queue scheduling (per port):\n");
	for (qos_port = machine.min_port; qos_port <= machine.max_port; qos_port++) {
		write_char('\t');
		itoa(machine.log_to_phys_port[qos_port]);
		write_char(':');
		for (qos_idx = 0; qos_idx < 8; qos_idx++) {
			// NOTE: reg_read_m fills sfr_data big-endian; the queue
			// control bits (STRICT + weight) are in bits 0-7 =
			// sfr_data[3]
			reg_read_m(RTL837X_SCHED_PORT_Q_CTRL_SET(qos_port, qos_idx));
			qos_val = sfr_data[3];
			write_char(' ');
			if (qos_val & SCHED_Q_STRICT_EN)
				write_char('S');
			else
				write_char('W');
			itoa(qos_val & SCHED_Q_WEIGHT_MASK);
		}
		write_char('\n');
	}
}
