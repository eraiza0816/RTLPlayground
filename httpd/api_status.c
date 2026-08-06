/*
 * JSON status endpoints for the Tier 1-3 features (BANK3)
 * This code is in the Public Domain
 *
 * Served by the HTTP GET handler (httpd.c, BANK1): /ping.json,
 * /arp.json, /lldp.json, /igmp.json, /storm-control.json, /qos.json
 * and /acl.json.  All functions are __banked; the XDATA helpers they
 * call (itoa_html etc., page_impl.c) are __banked as well.
 *
 * All state lives in XDATA: the 8051 internal RAM (DSEG/OSEG overlay)
 * is essentially full in this firmware, so no new function parameters
 * or locals may be allocated there.
 */

#pragma codeseg BANK4
#pragma constseg BANK4

#include "rtl837x_sfr.h"
#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "rtl837x_storm.h"
#include "rtl837x_qos.h"
#include "rtl837x_igmp.h"
#include "rtl837x_acl.h"
#include "ping.h"
#include "lldp.h"
#include "uip/uip_arp.h"
#include "machine.h"

extern __code const struct machine machine;
extern __xdata uint8_t outbuf[TCP_OUTBUF_SIZE];
extern __xdata uint16_t slen;
extern __xdata uint8_t sfr_data[4];

/* BANK1 helpers (page_impl.c) */
extern void itoa_html(uint8_t v) __banked;
extern void itoa16_html(uint16_t v) __banked;
extern void byte_to_html(uint8_t val) __banked;
extern void char_to_html(char c) __banked;

__code uint8_t * __code api_json_hdr =
	"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: private, max-age=1\r\n\r\n";

/* JSON helpers: emit a JSON string (printable ASCII only) and a 32-bit
 * value as 8 lowercase hex chars. */
static void json_str(__code char * __xdata s)
{
	while (*s)
		outbuf[slen++] = *s++;
}

static void json_str_x(__xdata char * __xdata s)
{
	while (*s)
		outbuf[slen++] = *s++;
}

static void json_hex32(__xdata uint8_t * __xdata b)
{
	byte_to_html(b[3]);
	byte_to_html(b[2]);
	byte_to_html(b[1]);
	byte_to_html(b[0]);
}

/* GET /ping.json: current/last ICMP echo state of the CLI ping. */
void send_ping(void) __banked
{
	slen = strtox(outbuf, api_json_hdr);
	char_to_html('{');
	json_str("\"state\":");
	itoa_html(ping_state);
	json_str(",\"dst\":\"");
	itoa_html(ping_dst[0]); char_to_html('.');
	itoa_html(ping_dst[1]); char_to_html('.');
	itoa_html(ping_dst[2]); char_to_html('.');
	itoa_html(ping_dst[3]);
	char_to_html('"');
	json_str(",\"sent\":");
	itoa_html(ping_sent);
	json_str(",\"rcvd\":");
	itoa_html(ping_rcvd);
	json_str(",\"min_rtt\":");
	/* min_rtt is 0xffff until the first reply (no-reply sentinel),
	 * which itoa16_html cannot print; report 0 when nothing was
	 * received */
	itoa16_html(ping_rcvd ? ping_min_rtt : 0);
	json_str(",\"max_rtt\":");
	itoa16_html(ping_max_rtt);
	json_str(",\"sum_rtt\":");
	itoa16_html(ping_sum_rtt);
	char_to_html('}');
}

/* GET /arp.json: the uIP ARP cache. */
void send_arp(void) __banked
{
	__xdata uint8_t arp_ip[4];
	__xdata uint8_t arp_mac[6];
	__xdata uint8_t arp_age;
	__xdata uint8_t j;
	__xdata uint8_t n;
	__xdata uint8_t first = 1;

	slen = strtox(outbuf, api_json_hdr);
	char_to_html('[');
	n = 0;
	while (uip_arp_entry_next(arp_ip, arp_mac, &arp_age)) {
		n++;
		if (slen + 70 > TCP_OUTBUF_SIZE)
			break;
		if (!first)
			char_to_html(',');
		first = 0;
		char_to_html('{');
		json_str("\"ip\":\"");
		itoa_html(arp_ip[0]); char_to_html('.');
		itoa_html(arp_ip[1]); char_to_html('.');
		itoa_html(arp_ip[2]); char_to_html('.');
		itoa_html(arp_ip[3]);
		char_to_html('"');
		json_str(",\"mac\":\"");
		for (j = 0; j < 6; j++) {
			if (j) char_to_html(':');
			byte_to_html(arp_mac[j]);
		}
		char_to_html('"');
		json_str(",\"age\":");
		itoa_html(arp_age);
		char_to_html('}');
		if (n >= UIP_ARPTAB_SIZE)
			break;
	}
	char_to_html(']');
}

/* GET /lldp.json: the LLDP neighbor table. */
void send_lldp(void) __banked
{
	__xdata uint8_t i;
	__xdata uint8_t j;
	__xdata uint8_t first = 1;

	slen = strtox(outbuf, api_json_hdr);
	char_to_html('[');
	for (i = 0; i < LLDP_MAX_NEIGHBORS; i++) {
		if (!lldp_neighbor_get(i))
			continue;
		if (slen + 100 > TCP_OUTBUF_SIZE)
			break;
		if (!first)
			char_to_html(',');
		first = 0;
		char_to_html('{');
		json_str("\"port\":");
		itoa_html(machine.log_to_phys_port[lldp_json.port]);
		json_str(",\"chassis\":\"");
		for (j = 0; j < 6; j++) {
			if (j) char_to_html(':');
			byte_to_html(lldp_json.chassis[j]);
		}
		char_to_html('"');
		json_str(",\"port_id\":\"");
		json_str_x(lldp_json.port_id);
		char_to_html('"');
		json_str(",\"sysname\":\"");
		json_str_x(lldp_json.sysname);
		char_to_html('"');
		json_str(",\"ttl\":");
		itoa_html(lldp_json.ttl);
		char_to_html('}');
	}
	char_to_html(']');
}

/* GET /igmp.json: IGMP/MLD engine state, per-port MLD ops and the
 * ASIC group database. */
void send_igmp(void) __banked
{
	__xdata uint16_t g;
	__xdata uint16_t p;
	__xdata uint8_t first = 1;

	slen = strtox(outbuf, api_json_hdr);
	igmp_json_state();
	char_to_html('{');
	json_str("\"mld_en\":");
	itoa_html(igmp_json_mld_en);
	json_str(",\"querier\":");
	itoa_html(igmp_json_querier);
	json_str(",\"ops\":[");
	for (p = 0; p <= machine.max_port - machine.min_port; p++) {
		if (p) char_to_html(',');
		itoa_html(igmp_json_ops[machine.phys_to_log_port[p]]);
	}
	char_to_html(']');
	json_str(",\"groups\":[");
	first = 1;
	g = 0;
	while (1) {
		g = igmp_json_group_next(g);
		if (g == 0xffff)
			break;
		if (slen + 40 > TCP_OUTBUF_SIZE)
			break;
		if (!first)
			char_to_html(',');
		first = 0;
		char_to_html('{');
		json_str("\"idx\":");
		itoa16_html(g);
		json_str(",\"pmask\":");
		itoa16_html(igmp_json_gmask);
		char_to_html('}');
		g++;
	}
	json_str("]}");
}

/* GET /storm-control.json: storm control state per type. */
void send_storm(void) __banked
{
	__xdata uint8_t t;
	__xdata uint8_t en;
	__xdata uint8_t * __xdata rp;
	__xdata uint8_t first = 1;

	slen = strtox(outbuf, api_json_hdr);
	char_to_html('[');
	for (t = 0; t < 4; t++) {
		if (slen + 60 > TCP_OUTBUF_SIZE)
			break;
		if (!first)
			char_to_html(',');
		first = 0;
		reg_read_m(RTL837X_SHARED_METER_MODE(t));
		en = (sfr_data[0] >> METER_MODE_PPS_BIT(t)) & 1;
		char_to_html('{');
		json_str("\"type\":");
		itoa_html(t);
		json_str(",\"en\":");
		itoa_html(storm_type_en[t]);
		json_str(",\"rate\":\"");
		rp = (__xdata uint8_t * __xdata)&storm_type_rate[t];
		json_hex32(rp);
		char_to_html('"');
		json_str(",\"pps\":");
		itoa_html(en);
		char_to_html('}');
	}
	char_to_html(']');
}

/* GET /qos.json: QoS mode, PCP/DSCP queue maps and queue scheduling. */
void send_qos(void) __banked
{
	__xdata uint8_t i;
	__xdata uint8_t port;
	__xdata uint8_t first = 1;

	slen = strtox(outbuf, api_json_hdr);
	char_to_html('{');
	json_str("\"mode\":");
	itoa_html(qos_mode);
	json_str(",\"pcp\":[");
	for (i = 0; i < 8; i++) {
		if (i) char_to_html(',');
		itoa_html(qos_pcp_map[i]);
	}
	char_to_html(']');
	json_str(",\"dscp\":[");
	for (i = 0; i < 64; i++) {
		if (i) char_to_html(',');
		itoa_html(qos_dscp_map[i]);
	}
	char_to_html(']');
	json_str(",\"sched\":[");
	first = 1;
	for (port = machine.min_port; port <= machine.max_port; port++) {
		if (slen + 40 > TCP_OUTBUF_SIZE)
			break;
		if (!first)
			char_to_html(',');
		first = 0;
		char_to_html('"');
		for (i = 0; i < 8; i++) {
			reg_read_m(RTL837X_SCHED_PORT_Q_CTRL_SET(port, i));
			if (sfr_data[3] & SCHED_Q_STRICT_EN)
				char_to_html('S');
			else
				char_to_html('W');
			itoa_html(sfr_data[3] & SCHED_Q_WEIGHT_MASK);
		}
		char_to_html('"');
	}
	char_to_html(']');
	char_to_html('}');
}

/* GET /acl.json: the ingress ACL rule table. */
void send_acl(void) __banked
{
	__xdata uint8_t i;
	__xdata uint8_t first = 1;

	slen = strtox(outbuf, api_json_hdr);
	char_to_html('[');
	for (i = 0; i < RTL837X_ACLRULENO; i++) {
		if (!acl_rule_json_get(i))
			continue;
		if (slen + 90 > TCP_OUTBUF_SIZE)
			break;
		if (!first)
			char_to_html(',');
		first = 0;
		char_to_html('{');
		json_str("\"idx\":");
		itoa16_html(i);
		json_str(",\"tpl\":");
		itoa_html(acl_json_data.template);
		json_str(",\"pmask\":");
		itoa16_html(acl_json_data.pmask);
		json_str(",\"action\":");
		itoa_html(acl_json_data.action);
		json_str(",\"data0\":\"");
		json_hex32((__xdata uint8_t * __xdata)&acl_json_data.data0);
		char_to_html('"');
		json_str(",\"data1\":\"");
		json_hex32((__xdata uint8_t * __xdata)&acl_json_data.data1);
		char_to_html('"');
		char_to_html('}');
	}
	char_to_html(']');
}
