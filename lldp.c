/*
 * LLDP (IEEE 802.1AB) neighbor discovery for the RTL837x platform
 * This code is in the Public Domain
 *
 * Sends an LLDPDU from every user port every 30 seconds (dest MAC
 * 01:80:c2:00:00:0e, ethertype 0x88cc) and learns neighbors from
 * received LLDPDUs into a small table.
 *
 * Frame handling follows the STP path: RX frames arrive with
 * eth(14) + rtl_tag(8) + vlan_tag(4) headers (LLDPDU at uip_buf[26]),
 * TX frames are built at uip_buf[RTL_FRAME_DESC_SIZE] with an embedded
 * RTL tag and sent via tcpip_output().
 *
 * All state lives in XDATA: the 8051 internal RAM overlay is full in
 * this firmware, so no new function parameters or locals may be
 * allocated there — every value travels through the static XDATA
 * scratch below.  All public functions are __banked (BANK3); omitting
 * it causes a plain lcall across banks and a boot crash (see
 * 新機能追加検証/README.md).
 */

#pragma codeseg BANK3
#pragma constseg BANK3

#include <stdint.h>
#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "rtl837x_sfr.h"
#include "uip/uip.h"
#include "machine.h"

extern __xdata uint8_t sfr_data[4];

extern __code struct machine machine;
extern __xdata struct uip_eth_addr uip_ethaddr;
extern __xdata uint8_t uip_buf[UIP_CONF_BUFFER_SIZE + 2];
extern __xdata uint16_t uip_len;
extern volatile __xdata uint32_t ticks;
extern __xdata char hostname[32];
extern __xdata char port_names[9][PORT_NAME_SIZE];
extern __xdata uint16_t management_vlan;

__xdata uint8_t lldp_enabled;

#define LLDP_DST_MAC_5 0x0e
#define LLDP_TX_INTERVAL (30 * SYS_TICK_HZ)
#define LLDP_FIRST_TX    (2 * SYS_TICK_HZ)
#define LLDP_DEFAULT_TTL 120
#define LLDP_MAX_NEIGHBORS 8

#define LLDP_ETH_OFFSET RTL_FRAME_DESC_SIZE     /* TX: eth header here */
#define LLDP_TAG_OFFSET (LLDP_ETH_OFFSET + 12)  /* TX: RTL tag here (type position) */
#define LLDP_PDU_OFFSET (LLDP_TAG_OFFSET + 8)   /* TX: LLDPDU here */
#define LLDP_RX_PDU_OFFSET 26                   /* RX: LLDPDU here */

struct lldp_neighbor {
	uint8_t port;              /* receiving port (from RTL tag) */
	uint8_t chassis[6];        /* Chassis ID (MAC sub-type) */
	char port_id[16];           /* Port ID string */
	char sysname[16];           /* System Name string */
	uint8_t ttl;               /* remaining validity in seconds */
	uint32_t last_seen;        /* ticks at last update */
};

static __xdata struct lldp_neighbor lldp_table[LLDP_MAX_NEIGHBORS];
static __xdata uint32_t lldp_tx_next;   /* next LLDPDU send time */
static __xdata uint32_t lldp_sec_next;  /* next 1s aging tick */

/* Dedicated TX frame: uip_buf is shared with RX and the HTTP/telnet
 * appcalls, and the NIC TX DMA can read the source buffer asynchronously,
 * so a frame built there gets clobbered before the DMA completes.  This
 * buffer is written by nothing else.  Layout follows the non-VLAN TX path:
 * [0..3] padding, [4..11] TX descriptor, [12..] the frame. */
static __xdata uint8_t lldp_tx_frame[RTL_FRAME_DESC_SIZE + 96];

/* Scratch (no params/locals allowed in internal RAM) */
static __xdata uint8_t lldp_i;
static __xdata uint8_t lldp_j;
static __xdata uint8_t lldp_port;       /* port under TX/learn */
static __xdata uint8_t lldp_phys;       /* physical port number */
static __xdata uint8_t lldp_byte;       /* byte to append to a TLV */
static __xdata uint8_t lldp_tlv_type;   /* TLV being built */
static __xdata uint16_t lldp_len;       /* TLV length */
static __xdata uint8_t lldp_pos;        /* LLDPDU build position */
static __xdata uint8_t lldp_slot;       /* neighbor slot */
static __xdata uint8_t lldp_val_start;  /* TLV value start (build position) */
static __xdata uint16_t lldp_l2mc_vid;
static __xdata uint16_t lldp_l2mc_guard;
static __xdata uint16_t lldp_saved_vlan;
static __xdata uint32_t lldp_oldest;    /* oldest last_seen */
static __xdata uint8_t lldp_same;       /* chassis match flag */
static __xdata uint8_t lldp_n;          /* string length */
static __xdata struct lldp_neighbor * __xdata lldp_slot_p;  /* pointer INTO XDATA table (the __xdata
                                                     * after '*' places the pointer itself in
                                                     * XDATA; a plain 'static __xdata struct * p'
                                                     * would land in DSEG/internal RAM) */

/* RX parse scratch */
static __xdata uint8_t lldp_rx_portid[16];
static __xdata uint8_t lldp_rx_sysname[16];
static __xdata uint8_t lldp_rx_portid_n;
static __xdata uint8_t lldp_rx_sysname_n;
static __xdata uint8_t lldp_rx_ttl;

/* ---- TLV build helpers (header: type 7 bits + len 9 bits, big-endian) ---- */

static void lldp_tlv_start(void)
{
	uip_buf[LLDP_PDU_OFFSET + lldp_pos] = (uint8_t)((lldp_tlv_type << 1) | (lldp_len >> 8));
	uip_buf[LLDP_PDU_OFFSET + lldp_pos + 1] = (uint8_t)(lldp_len & 0xff);
	lldp_pos += 2;
	lldp_val_start = lldp_pos;
}

static void lldp_tlv_byte(void)
{
	uip_buf[LLDP_PDU_OFFSET + lldp_pos++] = lldp_byte;
}

static void lldp_tlv_done(void)
{
	lldp_pos = lldp_val_start + lldp_len;
}

/* ---- TX ---- */

static void lldp_send_port(void)
{
	/* Ethernet header: dst 01:80:c2:00:00:0e, src = our MAC */
	uip_buf[LLDP_ETH_OFFSET + 0] = 0x01;
	uip_buf[LLDP_ETH_OFFSET + 1] = 0x80;
	uip_buf[LLDP_ETH_OFFSET + 2] = 0xc2;
	uip_buf[LLDP_ETH_OFFSET + 3] = 0x00;
	uip_buf[LLDP_ETH_OFFSET + 4] = 0x00;
	uip_buf[LLDP_ETH_OFFSET + 5] = LLDP_DST_MAC_5;
	for (lldp_i = 0; lldp_i < 6; lldp_i++)
		uip_buf[LLDP_ETH_OFFSET + 6 + lldp_i] = uip_ethaddr.addr[lldp_i];

	/* RTL tag: directed egress to all user ports (pmask, ALLOW clear).
	 * The flags word MUST go through HTONS (a raw constant lands as
	 * EFID, the ASIC then fails to parse the tag and leaks the 0x8899
	 * header onto the wire).  KEEP preserves the ethertype format on
	 * ethertype frames (same as LACP); LEARN_DIS stops the CPU's SA
	 * from being learned on the egress ports. */
	uip_buf[LLDP_TAG_OFFSET + 0] = (uint8_t)(RTL_FRAME_TAG_ID >> 8);
	uip_buf[LLDP_TAG_OFFSET + 1] = (uint8_t)RTL_FRAME_TAG_ID;
	uip_buf[LLDP_TAG_OFFSET + 2] = RTL_FRAME_TAG_VERSION;
	uip_buf[LLDP_TAG_OFFSET + 3] = 0x00;
	uip_buf[LLDP_TAG_OFFSET + 4] = 0x00;
	/* HTONS'd word stored little-endian: flags [0x00, 0xA0].  The high
	 * byte of the swapped value goes into the second byte. */
	uip_buf[LLDP_TAG_OFFSET + 5] = (uint8_t)(HTONS(RTL_TAG_LEARN_DIS | RTL_TAG_KEEP) >> 8);
	uip_buf[LLDP_TAG_OFFSET + 6] = 0x00;
	uip_buf[LLDP_TAG_OFFSET + 7] = 0xff;   /* pmask: all user ports */

	/* Chassis ID TLV: sub-type 4 (MAC address) */
	lldp_tlv_type = 1;
	lldp_len = 7;
	lldp_tlv_start();
	lldp_byte = 0x04;
	lldp_tlv_byte();
	for (lldp_i = 0; lldp_i < 6; lldp_i++) {
		lldp_byte = uip_ethaddr.addr[lldp_i];
		lldp_tlv_byte();
	}
	lldp_tlv_done();

	/* Port ID TLV: sub-type 5 (ifName), "port <phys>" */
	lldp_tlv_type = 2;
	lldp_len = 1 + 6 + (lldp_phys >= 10 ? 1 : 0);
	lldp_tlv_start();
	lldp_byte = 0x05;
	lldp_tlv_byte();
	lldp_byte = 'p'; lldp_tlv_byte();
	lldp_byte = 'o'; lldp_tlv_byte();
	lldp_byte = 'r'; lldp_tlv_byte();
	lldp_byte = 't'; lldp_tlv_byte();
	lldp_byte = ' '; lldp_tlv_byte();
	if (lldp_phys >= 10) {
		lldp_byte = '0' + lldp_phys / 10;
		lldp_tlv_byte();
	}
	lldp_byte = '0' + lldp_phys % 10;
	lldp_tlv_byte();
	lldp_tlv_done();

	/* TTL TLV */
	lldp_tlv_type = 3;
	lldp_len = 2;
	lldp_tlv_start();
	lldp_byte = 0x00;
	lldp_tlv_byte();
	lldp_byte = LLDP_DEFAULT_TTL;
	lldp_tlv_byte();
	lldp_tlv_done();

	/* System Name TLV */
	if (hostname[0]) {
		lldp_n = 0;
		while (hostname[lldp_n] && lldp_n < 15)
			lldp_n++;
		lldp_tlv_type = 5;
		lldp_len = lldp_n;
		lldp_tlv_start();
		for (lldp_i = 0; lldp_i < lldp_n; lldp_i++) {
			lldp_byte = (uint8_t)hostname[lldp_i];
			lldp_tlv_byte();
		}
		lldp_tlv_done();
	}

	/* Port Description TLV: port_names[] or none */
	lldp_n = 0;
	while (port_names[lldp_port][lldp_n] && lldp_n < 31)
		lldp_n++;
	if (lldp_n) {
		lldp_tlv_type = 4;
		lldp_len = lldp_n;
		lldp_tlv_start();
		for (lldp_i = 0; lldp_i < lldp_n; lldp_i++) {
			lldp_byte = (uint8_t)port_names[lldp_port][lldp_i];
			lldp_tlv_byte();
		}
		lldp_tlv_done();
	}

	/* End of LLDPDU TLV */
	lldp_tlv_type = 0;
	lldp_len = 0;
	lldp_tlv_start();

	/* Send via the normal TX path with the management-VLAN insert
	 * suppressed (link-local frames must egress untagged; the splice
	 * would shift the in-frame RTL tag out of the parsed position and
	 * the CPU tag would leak onto the wire as 0x8899). */
	lldp_len = LLDP_PDU_OFFSET + lldp_pos;
	uip_len = lldp_len;
	lldp_saved_vlan = management_vlan;
	management_vlan = 0;
	tcpip_output();
	management_vlan = lldp_saved_vlan;
}

static void lldp_send(void)
{
	/* One frame per tick: the pmask covers all user ports. */
	lldp_phys = 1;
	lldp_send_port();
}

/* ---- RX ---- */

/* Learn a neighbor from the received frame.  The chassis MAC is read
 * from the LLDPDU (sub-type 4 value at LLDPDU+4; per the spec the
 * Chassis ID TLV comes first), the parsed strings come from the RX
 * scratch buffers. */
static void lldp_learn(void)
{
	lldp_slot = LLDP_MAX_NEIGHBORS;
	lldp_oldest = 0xffffffff;

	for (lldp_i = 0; lldp_i < LLDP_MAX_NEIGHBORS; lldp_i++) {
		lldp_slot_p = &lldp_table[lldp_i];
		lldp_same = 1;
		for (lldp_j = 0; lldp_j < 6; lldp_j++)
			if (lldp_slot_p->chassis[lldp_j] != uip_buf[LLDP_RX_PDU_OFFSET + 4 + lldp_j])
				lldp_same = 0;
		if (lldp_same && lldp_slot_p->chassis[0] && lldp_slot_p->port == lldp_port) {
			lldp_slot = lldp_i;
			break;
		}
		if (lldp_slot_p->chassis[0] == 0 && lldp_slot == LLDP_MAX_NEIGHBORS)
			lldp_slot = lldp_i;
		else if (lldp_slot_p->chassis[0] && lldp_slot_p->last_seen < lldp_oldest)
			lldp_oldest = lldp_slot_p->last_seen;
	}
	if (lldp_slot == LLDP_MAX_NEIGHBORS) {
		/* Table full: evict the oldest entry. */
		lldp_oldest = 0xffffffff;
		for (lldp_i = 0; lldp_i < LLDP_MAX_NEIGHBORS; lldp_i++) {
			lldp_slot_p = &lldp_table[lldp_i];
			if (lldp_slot_p->chassis[0] && lldp_slot_p->last_seen < lldp_oldest) {
				lldp_oldest = lldp_slot_p->last_seen;
				lldp_slot = lldp_i;
			}
		}
		if (lldp_slot == LLDP_MAX_NEIGHBORS)
			return;
	}

	lldp_slot_p = &lldp_table[lldp_slot];
	for (lldp_i = 0; lldp_i < 6; lldp_i++)
		lldp_slot_p->chassis[lldp_i] = uip_buf[LLDP_RX_PDU_OFFSET + 4 + lldp_i];
	lldp_slot_p->port = lldp_port;
	for (lldp_i = 0; lldp_i < 16; lldp_i++) {
		lldp_slot_p->port_id[lldp_i] = lldp_rx_portid[lldp_i];
		lldp_slot_p->sysname[lldp_i] = lldp_rx_sysname[lldp_i];
	}
	lldp_slot_p->ttl = lldp_rx_ttl;
	lldp_slot_p->last_seen = ticks;
}

uint8_t lldp_rx(void) __banked
{
	if (!lldp_enabled)
		return 0;
	/* Match dest MAC 01:80:c2:00:00:0e and ethertype 0x88cc */
	if (uip_buf[0] != 0x01 || uip_buf[1] != 0x80 || uip_buf[2] != 0xc2 ||
	    uip_buf[3] != 0x00 || uip_buf[4] != 0x00 || uip_buf[5] != LLDP_DST_MAC_5)
		return 0;
	if (uip_buf[24] != 0x88 || uip_buf[25] != 0xcc)
		return 0;	/* Ignore our own frames (the flooded LLDPDU echoes back to the CPU). */
	if (uip_buf[6] == uip_ethaddr.addr[0] && uip_buf[7] == uip_ethaddr.addr[1] &&
	    uip_buf[8] == uip_ethaddr.addr[2] && uip_buf[9] == uip_ethaddr.addr[3] &&
	    uip_buf[10] == uip_ethaddr.addr[4] && uip_buf[11] == uip_ethaddr.addr[5])
		return 1;

	lldp_rx_portid_n = 0;
	lldp_rx_sysname_n = 0;
	lldp_rx_ttl = LLDP_DEFAULT_TTL;

	/* Parse TLVs */
	lldp_pos = LLDP_RX_PDU_OFFSET;
	while (lldp_pos + 1 < uip_len) {
		lldp_byte = uip_buf[lldp_pos];      /* type(7 bits) | len bit 8 */
		lldp_tlv_type = lldp_byte >> 1;
		lldp_len = 0;
		if (lldp_byte & 1)
			lldp_len = 0x100;
		lldp_byte = uip_buf[lldp_pos + 1];
		lldp_len |= lldp_byte;
		lldp_pos += 2;
		if (lldp_tlv_type == 0)
			break;
		if ((uint16_t)lldp_pos + lldp_len > uip_len)
			break;
		switch (lldp_tlv_type) {
		case 2: /* Port ID */
			if (uip_buf[lldp_pos] == 5 && lldp_len >= 2) {   /* sub-type ifName */
				lldp_n = lldp_len - 1;
				if (lldp_n > 15) lldp_n = 15;
				lldp_j = lldp_pos + 1;
				for (lldp_i = 0; lldp_i < lldp_n; lldp_i++) {
					lldp_byte = uip_buf[lldp_j];
					lldp_rx_portid[lldp_i] = lldp_byte;
					lldp_j++;
				}
				lldp_rx_portid_n = lldp_n;
			}
			break;
		case 3: /* TTL */
			if (lldp_len >= 2)
				lldp_rx_ttl = uip_buf[lldp_pos + 1];
			break;
		case 5: /* System Name */
			lldp_n = lldp_len;
			if (lldp_n > 15) lldp_n = 15;
			lldp_j = lldp_pos;
			for (lldp_i = 0; lldp_i < lldp_n; lldp_i++) {
				lldp_byte = uip_buf[lldp_j];
				lldp_rx_sysname[lldp_i] = lldp_byte;
				lldp_j++;
			}
			lldp_rx_sysname_n = lldp_n;
			break;
		default:
			break;
		}
		lldp_pos += lldp_len;
	}

	/* NUL-terminate the parsed strings */
	lldp_rx_portid[lldp_rx_portid_n] = 0;
	lldp_rx_sysname[lldp_rx_sysname_n] = 0;

	/* Receiving port from the RTL tag pmask (4-bit port number). */
	lldp_port = uip_buf[18] & 0x0f;
	lldp_learn();
	return 1;
}

/* ---- Timer ---- */

void lldp_timers(void) __banked
{
	if (!lldp_enabled)
		return;

	/* 1s aging of neighbor TTLs */
	if ((int32_t)(ticks - lldp_sec_next) >= 0) {
		lldp_sec_next = ticks + SYS_TICK_HZ;
		for (lldp_i = 0; lldp_i < LLDP_MAX_NEIGHBORS; lldp_i++) {
			lldp_slot_p = &lldp_table[lldp_i];
			if (lldp_slot_p->chassis[0] && lldp_slot_p->ttl) {
				lldp_slot_p->ttl--;
				if (!lldp_slot_p->ttl)
					lldp_slot_p->chassis[0] = 0;
			}
		}
	}

	/* 30s periodic LLDPDU transmission */
	if ((int32_t)(ticks - lldp_tx_next) >= 0) {
		lldp_tx_next = ticks + LLDP_TX_INTERVAL;
		lldp_send();
	}
}

/* ---- Setup / show ---- */

/* Static L2 multicast entry: 01:80:c2:00:00:0e in VLAN vid, member mask
 * = CPU port only (bit 9).  The write command hashes MAC+VID itself; the
 * ASIC's TBL_EXECUTE self-clears.  All values are xdata statics: the
 * internal-RAM overlay is full. */
static void lldp_l2mc_set(uint16_t vid)
{
	lldp_l2mc_vid = vid;
	lldp_l2mc_guard = 0;
	do {
		reg_read_m(RTL837X_TBL_CTRL);
	} while ((sfr_data[3] & TBL_EXECUTE) && ++lldp_l2mc_guard);

	REG_WRITE(RTL837x_TBL_DATA_IN_A, 0xc2, 0x00, 0x00, 0x0e);
	REG_WRITE(RTL837x_TBL_DATA_IN_B, 0x20 | (lldp_l2mc_vid >> 8) | (((uint16_t)PMASK_CPU & 0x3) << 6),
		  (uint8_t)lldp_l2mc_vid, 0x01, 0x80);
	REG_WRITE(RTL837x_TBL_DATA_IN_C, 0, 0, 0, PMASK_CPU >> 2);
	REG_WRITE(RTL837X_TBL_CTRL, 0, 0, TBL_L2_UNICAST, TBL_WRITE | TBL_EXECUTE);

	lldp_l2mc_guard = 0;
	do {
		reg_read_m(RTL837X_TBL_CTRL);
	} while ((sfr_data[3] & TBL_EXECUTE) && ++lldp_l2mc_guard);
}

void lldp_setup(void) __banked
{
	/* LLDP RMA action: FORWARD.  The RMA "trap" action cannot deliver
	 * to the internal NIC on this hardware (it targets an external CPU
	 * on a physical port), so RX delivery is done with a static L2
	 * multicast entry for 01:80:c2:00:00:0e whose member mask is the
	 * CPU port only: the forward lookup then hits the entry's mask
	 * instead of the VLAN flood mask (same approach as BPDU
	 * containment).  The CPU's own TX uses a directed pmask in the
	 * RTL tag, so the entry does not interfere with egress. */
	reg_read_m(0x4f18);
	sfr_mask_data(0, 0x30, 0x00);
	reg_write_m(0x4f18);
	reg_read_m(0x4f1c);
	sfr_mask_data(0, 0x08, 0x08);
	reg_write_m(0x4f1c);

	/* Steer LLDP frames to the CPU: entry per VLAN in use (IVL table). */
	lldp_l2mc_set(1);            /* PVID 1 (untagged) */
	if (management_vlan >= 2)
		lldp_l2mc_set(management_vlan);

	lldp_enabled = 0;      /* default off: LLDP discloses the topology */
	lldp_tx_next = ticks + LLDP_FIRST_TX;
	lldp_sec_next = ticks + SYS_TICK_HZ;
	for (lldp_i = 0; lldp_i < LLDP_MAX_NEIGHBORS; lldp_i++)
		lldp_table[lldp_i].chassis[0] = 0;
}

/* Decimal 8-bit print without library division. */
static __xdata uint8_t lldp_dec_v;
static __xdata uint8_t lldp_dec_d;
static __xdata uint8_t lldp_dec_z;

static void lldp_print_dec8(void)
{
	lldp_dec_d = 0;
	lldp_dec_z = 0;
	while (lldp_dec_v >= 100) { lldp_dec_v -= 100; lldp_dec_d++; }
	if (lldp_dec_d || lldp_dec_z) { write_char('0' + lldp_dec_d); lldp_dec_z = 1; }
	lldp_dec_d = 0;
	while (lldp_dec_v >= 10) { lldp_dec_v -= 10; lldp_dec_d++; }
	if (lldp_dec_d || lldp_dec_z) { write_char('0' + lldp_dec_d); }
	write_char('0' + lldp_dec_v);
}

void lldp_show(void) __banked
{
	print_string("Port  Chassis ID         Port ID      System Name      TTL\n");
	for (lldp_i = 0; lldp_i < LLDP_MAX_NEIGHBORS; lldp_i++) {
		lldp_slot_p = &lldp_table[lldp_i];
		if (!lldp_slot_p->chassis[0])
			continue;
		lldp_dec_v = lldp_slot_p->port;
		lldp_print_dec8();
		write_char(' ');
		write_char(' ');
		for (lldp_j = 0; lldp_j < 6; lldp_j++) {
			print_byte(lldp_slot_p->chassis[lldp_j]);
			if (lldp_j < 5) write_char(':');
		}
		write_char(' ');
		write_char(' ');
		if (lldp_slot_p->port_id[0])
			print_string_x(lldp_slot_p->port_id);
		write_char(' ');
		write_char(' ');
		if (lldp_slot_p->sysname[0])
			print_string_x(lldp_slot_p->sysname);
		write_char(' ');
		write_char(' ');
		lldp_dec_v = lldp_slot_p->ttl;
		lldp_print_dec8();
		write_char('\n');
	}
}
