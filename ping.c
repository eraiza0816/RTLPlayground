/*
 * ICMP echo request sender (ping) for the RTL837x platform
 * This code is in the Public Domain
 *
 * Sends a small number of ICMP echo requests to a user-specified IP
 * address and reports RTT statistics.  ARP resolution is performed via
 * the existing uip_arp_out() path: while the destination MAC is unknown
 * the pending echo is replaced by an ARP request and retried on the
 * next tick.
 *
 * Checksums are computed in hardware by the ASIC (TX tag chksum_flags),
 * so the IP/ICMP checksum fields are left zero.
 */

#pragma codeseg BANK2
#pragma constseg BANK2

#include <stdint.h>
#include "rtl837x_common.h"
#include "uip/uip.h"
#include "uip/uip_arp.h"

extern volatile __xdata uint32_t ticks;
extern __xdata uint8_t ip[4];           // filled by parse_ip() in cmd_parser.c

#define PING_COUNT       4
#define PING_INTERVAL    SYS_TICK_HZ
#define PING_TIMEOUT     (2 * SYS_TICK_HZ)

/* All state lives in XDATA: the 8051 internal RAM (DSEG/OSEG overlay)
 * is essentially full in this firmware, so no new function parameters
 * or locals may be allocated there.  Scratch values are shared statics. */
static __xdata uint8_t ping_state;      // 0 = idle, 1 = running
static __xdata uint8_t ping_dst[4];     // destination in a.b.c.d order
static __xdata uint16_t ping_id;        // ICMP identifier
static __xdata uint8_t ping_seq;        // current sequence number
static __xdata uint8_t ping_sent;       // number of echoes actually transmitted
static __xdata uint8_t ping_rcvd;       // number of replies received
static __xdata uint32_t ping_next_ticks;
static __xdata uint32_t ping_tmo_ticks;
static __xdata uint32_t ping_tx_ticks;
static __xdata uint16_t ping_min_rtt;   // RTT in ms
static __xdata uint16_t ping_max_rtt;
static __xdata uint16_t ping_sum_rtt;
static __xdata uint8_t ping_rtt_set;

static __xdata uint16_t ping_v16;       // shared scratch
static __xdata uint8_t ping_v8;         // shared scratch
static __xdata uint16_t ping_w16;       // shared scratch
static __xdata struct uip_icmpip_hdr *ping_hdr;

static void ping_print_ip(void)
{
	itoa(ping_dst[0]); write_char('.');
	itoa(ping_dst[1]); write_char('.');
	itoa(ping_dst[2]); write_char('.');
	itoa(ping_dst[3]);
}

/* Decimal 16-bit output without any library division (avoids pulling
 * __divuint into the build).  Value must be placed in ping_v16 first;
 * the value is destroyed.  No locals: the 8051 internal RAM overlay is
 * full in this firmware, so only XDATA globals are used. */
static __xdata uint8_t ping_dec_digits[6];
static __xdata uint8_t ping_dec_n;
static __xdata uint8_t ping_dec_flag;

static void ping_print_dec16(void)
{
	/* Extract least-significant digits first into ping_dec_digits[],
	 * repeatedly dividing ping_v16 by 10 without a library division:
	 * quotient accumulates in ping_v8, remainder stays in ping_v16. */
	ping_dec_n = 0;
	do {
		ping_v8 = 0;
		while (ping_v16 >= 10) { ping_v16 -= 10; ping_v8++; }
		ping_dec_digits[ping_dec_n++] = (uint8_t)ping_v16;
		ping_v16 = ping_v8;
	} while (ping_v16);
	/* Print digits in reverse order, suppressing leading zeros. */
	ping_dec_flag = 0;
	while (ping_dec_n) {
		ping_dec_n--;
		if (ping_dec_digits[ping_dec_n] || ping_dec_flag || ping_dec_n == 0) {
			write_char('0' + ping_dec_digits[ping_dec_n]);
			ping_dec_flag = 1;
		}
	}
}

/* ping_v16 = ping_v16 / ping_v8, without library division. */
static void ping_div16(void)
{
	ping_w16 = 0;
	while (ping_v16 >= ping_v8) {
		ping_v16 -= ping_v8;
		ping_w16++;
	}
	ping_v16 = ping_w16;
}

static void ping_send(void)
{
	ping_hdr = (__xdata struct uip_icmpip_hdr *)&uip_buf[UIP_LLH_LEN];

	ping_hdr->vhl = 0x45;
	ping_hdr->tos = 0;
	ping_hdr->len[0] = 0;
	ping_hdr->len[1] = UIP_IPH_LEN + 8;          // 28 bytes total
	ping_hdr->ipid[0] = 0;
	ping_hdr->ipid[1] = (uint8_t)ping_id;
	ping_hdr->ipoffset[0] = 0;
	ping_hdr->ipoffset[1] = 0;
	ping_hdr->ttl = 64;
	ping_hdr->proto = UIP_PROTO_ICMP;
	ping_hdr->ipchksum = 0;                      // computed by ASIC on TX
	ping_hdr->srcipaddr[0] = uip_hostaddr[0];
	ping_hdr->srcipaddr[1] = uip_hostaddr[1];
	ping_hdr->destipaddr[0] = (uint16_t)(ping_dst[0]) | ((uint16_t)(ping_dst[1]) << 8);
	ping_hdr->destipaddr[1] = (uint16_t)(ping_dst[2]) | ((uint16_t)(ping_dst[3]) << 8);
	ping_hdr->type = 8;                          // ICMP_ECHO
	ping_hdr->icode = 0;
	ping_hdr->icmpchksum = 0;                    // computed by ASIC on TX
	ping_hdr->id = HTONS(ping_id);
	ping_hdr->seqno = HTONS(ping_seq);

	uip_len = UIP_IPH_LEN + 8;

	/* uip_arp_out() prepends the ethernet header or, if the destination
	 * MAC is unknown, replaces the whole packet with an ARP request. */
	uip_arp_out();
	if (uip_buf[24] == 0x08 && uip_buf[25] == 0x06) {
		/* ARP request: transmit it and retry the echo next tick.  The
		 * per-echo timeout was started when this attempt began (in
		 * ping_pump), so an unresolvable ARP eventually times out. */
		tcpip_output();
		ping_next_ticks = ticks + 1;
		return;
	}

	tcpip_output();
	ping_tx_ticks = ticks;
	ping_sent++;
	ping_tmo_ticks = ticks + PING_TIMEOUT;
	ping_next_ticks = ticks + PING_INTERVAL;
}

static void ping_stats_print(void)
{
	print_string("--- ");
	ping_print_ip();
	print_string(" ping statistics ---\n");
	ping_v16 = ping_sent;
	ping_print_dec16();
	print_string(" packets transmitted, ");
	ping_v16 = ping_rcvd;
	ping_print_dec16();
	print_string(" received, ");
	/* Loss percentage = 100 * (sent - rcvd) / sent */
	if (ping_sent) {
		ping_v16 = (uint16_t)(ping_sent - ping_rcvd) * 100;
		ping_v8 = ping_sent;
		ping_div16();
		ping_print_dec16();
	}
	print_string("% packet loss\n");
	if (ping_rtt_set) {
		print_string("rtt min/avg/max = ");
		ping_v16 = ping_min_rtt;
		ping_print_dec16();
		print_string("/");
		ping_v16 = ping_sum_rtt;
		ping_v8 = ping_rcvd;
		ping_div16();
		ping_print_dec16();
		print_string("/");
		ping_v16 = ping_max_rtt;
		ping_print_dec16();
		print_string(" ms\n");
	}
}


void ping_start(void) __banked
{
	if (ping_state) {
		print_string("Ping already in progress\n");
		return;
	}
	ping_dst[0] = ip[0]; ping_dst[1] = ip[1];
	ping_dst[2] = ip[2]; ping_dst[3] = ip[3];
	ping_id++;
	ping_seq = 0;
	ping_sent = 0;
	ping_rcvd = 0;
	ping_min_rtt = 0xffff;
	ping_max_rtt = 0;
	ping_sum_rtt = 0;
	ping_rtt_set = 0;
	ping_next_ticks = ticks;
	ping_tmo_ticks = 0;
	ping_state = 1;
}


void ping_pump(void) __banked
{
	if (ping_state == 0)
		return;

	if (ping_tmo_ticks && ticks >= ping_tmo_ticks) {
		/* No reply for the current echo in time. */
		print_string("Request timeout: seq=");
		itoa(ping_seq);
		write_char('\n');
		ping_seq++;
		if (ping_seq >= PING_COUNT) {
			ping_stats_print();
			ping_state = 0;
			return;
		}
		/* Start the next attempt's timeout now so that an unresolvable
		 * ARP also times out. */
		ping_tmo_ticks = ticks + PING_TIMEOUT;
		ping_next_ticks = ticks + 1;
	}

	if (ticks < ping_next_ticks)
		return;

	ping_send();
}


uint8_t ping_rx(void) __banked
{
	if (ping_state == 0)
		return 0;

	/* proto must be ICMP (1) */
	if (uip_buf[UIP_LLH_LEN + 9] != UIP_PROTO_ICMP)
		return 0;
	/* ICMP type must be echo reply (0) */
	if (uip_buf[UIP_LLH_LEN + 20] != 0)
		return 0;
	/* Source IP must match the ping destination */
	if (uip_buf[UIP_LLH_LEN + 12] != ping_dst[0] ||
	    uip_buf[UIP_LLH_LEN + 13] != ping_dst[1] ||
	    uip_buf[UIP_LLH_LEN + 14] != ping_dst[2] ||
	    uip_buf[UIP_LLH_LEN + 15] != ping_dst[3])
		return 0;
	/* ICMP identifier must match */
	if (uip_buf[UIP_LLH_LEN + 24] != (uint8_t)(ping_id >> 8) ||
	    uip_buf[UIP_LLH_LEN + 25] != (uint8_t)ping_id)
		return 0;

	ping_w16 = (uint16_t)((ticks - ping_tx_ticks) * 5);
	ping_rcvd++;
	if (!ping_rtt_set) {
		ping_min_rtt = ping_max_rtt = ping_w16;
		ping_rtt_set = 1;
	} else {
		if (ping_w16 < ping_min_rtt) ping_min_rtt = ping_w16;
		if (ping_w16 > ping_max_rtt) ping_max_rtt = ping_w16;
	}
	ping_sum_rtt += ping_w16;

	print_string("Reply from ");
	ping_print_ip();
	print_string(": seq=");
	itoa(ping_seq);
	print_string(" time=");
	ping_v16 = ping_w16;
	ping_print_dec16();
	print_string("ms\n");

	ping_seq++;
	if (ping_seq >= PING_COUNT) {
		ping_stats_print();
		ping_state = 0;
	} else {
		ping_next_ticks = ticks + PING_INTERVAL;
		/* Cover the ARP-resolution phase of the next echo too. */
		ping_tmo_ticks = ticks + PING_INTERVAL + PING_TIMEOUT;
	}
	return 1;
}


void ping_abort(void) __banked
{
	if (ping_state) {
		ping_stats_print();
		ping_state = 0;
	}
}
