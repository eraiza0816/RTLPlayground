/*
 * cli_sim — telnet CLI simulator for RTLPlayground.
 *
 * Simulates the firmware's telnet console (cmd_parser / cmd_editor /
 * telnetd) so CLI automation and documentation can be tested without
 * hardware. Same structure as httpd_sim.c (tools/output/httpd_sim).
 *
 * Usage:
 *   cli_sim [port] [-full]
 *     port  TCP port (default 2323)
 *     -full enable the Full CLI variant (EOS-like modes + '?' help)
 *
 * The simulator is stateful: port/vlan/hostname/passwd changes persist
 * for the lifetime of the process, like the running config of the device.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "httpd_sim.h"

/* ------------------------------------------------------------------ */
/* Simulated device state                                             */
/* ------------------------------------------------------------------ */

#define MAX_PORTS 8
#define MAX_VLANS 16
#define MAX_L2    8

#define PASSWORD "1234"


struct port_state {
	uint8_t enabled;
	uint8_t link;          /* 0=down, 2=10M, 3=100M, 4=1G, 5=2.5G */
	uint8_t force_speed;   /* 0=auto */
	uint8_t duplex;        /* 0=auto, 1=half, 2=full */
	char name[16];
	uint8_t pvid;
	uint32_t txg, txb, rxg, rxb;
};

struct vlan_state {
	uint16_t id;
	char name[33];
	uint16_t members;
	uint16_t tagged;
	uint8_t mgmt;
};

struct l2_entry {
	uint16_t idx;
	char mac[18];
	uint16_t vlan;
	char type;             /* 'l' = learned */
	uint8_t port;
};

static struct port_state ports[MAX_PORTS];
static struct vlan_state vlans[MAX_VLANS];
static int nvlans = 0;
static struct l2_entry l2tbl[MAX_L2] = {
	{ 0x00fc, "18:ec:e7:95:a9:87", 1, 'l', 4 },
	{ 0x06d0, "06:05:17:83:8d:3d", 1, 'l', 9 },
};
static int nl2 = 2;

static char hostname[32] = "PCB-K0402WS-V3.0";
static char passwd[21] = PASSWORD;
static uint16_t management_vlan = 0;
static uint8_t telnet_enabled = 1;
static uint8_t web_enabled = 1;
static uint8_t stp_enabled = 0;
static uint8_t led_enabled = 1;
static uint8_t igmp_enabled = 0;
static uint8_t mirror_enabled = 0;
static uint8_t mirror_port = 0;
static uint8_t eee_enabled[MAX_PORTS];
static char history_buf[512];
static int history_len = 0;

/* SFP module in slot 2 (port 6): FS SFP-10GSR-85 */
static uint8_t sfp_eeprom[256];

/* ------------------------------------------------------------------ */
/* SFP EEPROM simulation                                              */
/* ------------------------------------------------------------------ */

static void sfp_eeprom_init(void)
{
	/* SFF-8472 style EEPROM: identifier, vendor "FS", model
	 * "SFP-10GSR-85", serial "F2330445990". */
	memset(sfp_eeprom, 0, sizeof(sfp_eeprom));
	sfp_eeprom[0] = 0x03;  /* SFP */
	sfp_eeprom[1] = 0x04;  /* 10G */
	const char vendor[] = "FS              ";
	const char model[]  = "SFP-10GSR-85    ";
	const char serial[] = "F2330445990     ";
	memcpy(sfp_eeprom + 20, vendor, 16);
	memcpy(sfp_eeprom + 40, model, 16);
	memcpy(sfp_eeprom + 68, serial, 16);
	sfp_eeprom[3] = 0x67;  /* rate: 10G */
	sfp_eeprom[4] = 0x06;  /* encoding */
	sfp_eeprom[5] = 0x00;  /* length 10G */
	sfp_eeprom[10] = 0x00; /* transceiver: 10G Base-LR */
	sfp_eeprom[11] = 0x20;
	/* CC_BASE: checksum of bytes 0..62 */
	uint8_t sum = 0;
	for (int i = 0; i < 63; i++)
		sum += sfp_eeprom[i];
	sfp_eeprom[63] = sum;
	/* CC_EXT: checksum of bytes 64..94 */
	sum = 0;
	for (int i = 64; i < 95; i++)
		sum += sfp_eeprom[i];
	sfp_eeprom[95] = sum;
}

/* ------------------------------------------------------------------ */
/* Connection handling                                                */
/* ------------------------------------------------------------------ */

struct conn {
	int fd;
	uint8_t auth_ok;
	char line[128];
	int line_len;
	uint8_t iac_state;
	uint8_t iac_cmd;
};

static void conn_send(struct conn *c, const char *s)
{
	ssize_t wr = write(c->fd, s, strlen(s));
	(void)wr;
}

static void conn_send_n(struct conn *c, const void *p, size_t n)
{
	ssize_t wr = write(c->fd, p, n);
	(void)wr;
}

static void conn_send_byte(struct conn *c, char b)
{
	ssize_t wr = write(c->fd, &b, 1);
	(void)wr;
}

static void print_prompt(struct conn *c)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "[%s]> ", hostname);
	conn_send(c, buf);
}

/* IAC negotiation: refuse all options (WONT/DONT), like the firmware. */
static void telnet_negotiate(struct conn *c, uint8_t b)
{
	if (c->iac_state == 1) {
		c->iac_cmd = b;
		c->iac_state = 2;
	} else if (c->iac_state == 2) {
		uint8_t resp[3];
		if (c->iac_cmd == 0xfb /* WILL */ || c->iac_cmd == 0xfd /* DO */) {
			resp[0] = 0xff;
			resp[1] = (c->iac_cmd == 0xfb) ? 0xfc /* DONT */ : 0xfe /* WONT */;
			resp[2] = b;
			conn_send_n(c, resp, 3);
		}
		c->iac_state = 0;
	}
}

/* ------------------------------------------------------------------ */
/* Command handlers                                                   */
/* ------------------------------------------------------------------ */

static void cmd_version(struct conn *c)
{
	conn_send(c, "Software version: v0.2.23-clisim\n");
	conn_send(c, "Build date: 2026-08-04 00:00:00\n");
	conn_send(c, "Hardware: ");
	conn_send(c, hostname);
	conn_send(c, "\n");
}

static void cmd_show(struct conn *c)
{
	char buf[256];
	snprintf(buf, sizeof(buf),
		"Hostname: %s\nIP: 192.168.10.247\nGateway: 192.168.10.1\n"
		"Netmask: 255.255.255.0\nTelnet: %s\nWeb: %s\nSTP: %s\nIGMP: %s\nLED: %s\n",
		hostname,
		telnet_enabled ? "enabled" : "disabled",
		web_enabled ? "enabled" : "disabled",
		stp_enabled ? "on" : "off",
		igmp_enabled ? "on" : "off",
		led_enabled ? "on" : "off");
	conn_send(c, buf);
}

static void cmd_stat(struct conn *c)
{
	conn_send(c, "Port\tState\tLink\tTxGood\t\tTxBad\t\tRxGood\t\tRxBad\n");
	for (int i = 0; i < MAX_PORTS; i++) {
		struct port_state *p = &ports[i];
		const char *link = "Down";
		switch (p->link) {
		case 2: link = "10M"; break;
		case 3: link = "100M"; break;
		case 4: link = "1G"; break;
		case 5: link = "2.5G"; break;
		}
		char buf[160];
		snprintf(buf, sizeof(buf),
			"%d\t%s\t%s\t0x%08x\t0x%08x\t0x%08x\t0x%08x\n",
			i + 1, p->enabled ? "On" : "Off", link,
			p->txg, p->txb, p->rxg, p->rxb);
		conn_send(c, buf);
	}
}

static const char *link_str(int link)
{
	switch (link) {
	case 2: return "10M";
	case 3: return "100M";
	case 4: return "1G";
	case 5: return "2.5G";
	default: return "Down";
	}
}

static void cmd_port(struct conn *c, char **w, int nw)
{
	if (nw < 2) {
		conn_send(c, "Usage: port <n> show|on|off|name|<speed>|duplex\n");
		return;
	}
	int n = atoi(w[1]);
	if (n < 1 || n > MAX_PORTS) {
		conn_send(c, "Invalid port\n");
		return;
	}
	struct port_state *p = &ports[n - 1];
	if (nw == 2) {
		conn_send(c, "No action specified (show/on/off/name/speed)\n");
		return;
	}
	const char *a = w[2];
	if (!strcmp(a, "show")) {
		char buf[256];
		snprintf(buf, sizeof(buf),
			"Logical Port: %02d\nName: %s\nLink speed: %s %s\n"
			"AN enabled, advertising: 10Base-Half 10Base-Full 100Base-Half "
			"100Base-Full 1000Base-Full 2500BaseN-Full\n"
			"Link Partner advertises: 10Base-Half 10Base-Full 100Base-Half "
			"100Base-Full 1000Base-Full\n",
			n, p->name, link_str(p->link),
			(p->duplex == 1) ? "half duplex" : "full duplex");
		conn_send(c, buf);
	} else if (!strcmp(a, "on")) {
		p->enabled = 1;
		conn_send(c, "Port enabled\n");
	} else if (!strcmp(a, "off")) {
		p->enabled = 0;
		p->link = 0;
		conn_send(c, "Port disabled\n");
	} else if (!strcmp(a, "name") && nw >= 4) {
		snprintf(p->name, sizeof(p->name), "%s", w[3]);
		conn_send(c, "Port name set\n");
	} else if (!strcmp(a, "duplex") && nw >= 4) {
		p->duplex = !strcmp(w[3], "half") ? 1 : 2;
		conn_send(c, "Duplex set\n");
	} else if (!strcmp(a, "10m")) {
		p->force_speed = 2; p->link = 2; conn_send(c, "Speed set to 10M\n");
	} else if (!strcmp(a, "100m")) {
		p->force_speed = 3; p->link = 3; conn_send(c, "Speed set to 100M\n");
	} else if (!strcmp(a, "1g")) {
		p->force_speed = 4; p->link = 4; conn_send(c, "Speed set to 1G\n");
	} else if (!strcmp(a, "2g5")) {
		p->force_speed = 5; p->link = 5; conn_send(c, "Speed set to 2.5G\n");
	} else if (!strcmp(a, "5g") || !strcmp(a, "10g")) {
		conn_send(c, "Speed not supported on this port\n");
	} else if (!strcmp(a, "auto")) {
		p->force_speed = 0; conn_send(c, "Speed set to auto\n");
	} else {
		conn_send(c, "Unknown port action\n");
	}
}

static void cmd_vlan(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "show")) {
		conn_send(c, "VLAN\tName\tMembers\tTagged\tMgmt\n");
		for (int i = 0; i < nvlans; i++) {
			char buf[128];
			snprintf(buf, sizeof(buf), "%d\t%s\t0x%04x\t0x%04x\t%s\n",
				vlans[i].id, vlans[i].name[0] ? vlans[i].name : "-",
				vlans[i].members, vlans[i].tagged,
				vlans[i].mgmt ? "yes" : "no");
			conn_send(c, buf);
		}
		return;
	}
	if (nw < 2) {
		conn_send(c, "Usage: vlan show|<id> [d|mgmt|<name>|<port>[t|u]...]\n");
		return;
	}
	int id = atoi(w[1]);
	if (id < 1 || id > 4094) {
		conn_send(c, "Invalid VLAN ID\n");
		return;
	}
	/* Find existing */
	struct vlan_state *v = NULL;
	for (int i = 0; i < nvlans; i++) {
		if (vlans[i].id == id) {
			v = &vlans[i];
			break;
		}
	}
	if (nw == 2 && v) {
		char buf[128];
		snprintf(buf, sizeof(buf), "VLAN %d: Members 0x%04x Tagged 0x%04x\n",
			v->id, v->members, v->tagged);
		conn_send(c, buf);
		return;
	}
	if (nw >= 3 && !strcmp(w[2], "d")) {
		if (v)
			v->id = 0;  /* mark deleted */
		conn_send(c, "VLAN deleted\n");
		return;
	}
	if (nw >= 3 && !strcmp(w[2], "mgmt")) {
		management_vlan = id;
		conn_send(c, "Management VLAN set\n");
		return;
	}
	/* Create or update */
	if (!v) {
		if (nvlans >= MAX_VLANS) {
			conn_send(c, "VLAN table full\n");
			return;
		}
		v = &vlans[nvlans++];
		memset(v, 0, sizeof(*v));
		v->id = id;
	}
	if (nw >= 3 && isalpha((unsigned char)w[2][0]) && strcmp(w[2], "d") &&
	    strcmp(w[2], "mgmt")) {
		snprintf(v->name, sizeof(v->name), "%s", w[2]);
	}
	for (int i = 2; i < nw; i++) {
		const char *tok = w[i];
		if (isdigit((unsigned char)tok[0])) {
			int pn = atoi(tok);
			uint8_t tagged = strchr(tok, 't') != NULL;
			uint8_t untagged = strchr(tok, 'u') != NULL;
			if (pn >= 1 && pn <= MAX_PORTS) {
				if (tagged)
					v->tagged |= 1u << pn;
				else if (untagged)
					v->tagged &= ~(1u << pn);
				v->members |= 1u << pn;
				if (!untagged && !tagged)
					ports[pn - 1].pvid = id;
			}
		}
	}
	char buf[64];
	snprintf(buf, sizeof(buf), "VLAN %d updated (members 0x%04x)\n",
		v->id, v->members);
	conn_send(c, buf);
}

static void cmd_l2(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "forget")) {
		nl2 = 0;
		conn_send(c, "L2 table flushed\n");
		return;
	}
	conn_send(c, "Index\tMAC\tVLAN\tType\tPort\n");
	for (int i = 0; i < nl2; i++) {
		char buf[96];
		snprintf(buf, sizeof(buf), "%04x\t%s\t%03d\t%c\t%d\n",
			l2tbl[i].idx, l2tbl[i].mac, l2tbl[i].vlan,
			l2tbl[i].type, l2tbl[i].port);
		conn_send(c, buf);
	}
}

static void sfp_dump(struct conn *c)
{
	char buf[80];
	for (int row = 0; row < 16; row++) {
		int off = 0;
		off += snprintf(buf + off, sizeof(buf) - off, "%02x: ", row * 16);
		for (int i = 0; i < 16; i++)
			off += snprintf(buf + off, sizeof(buf) - off, "%02x ",
					sfp_eeprom[row * 16 + i]);
		conn_send(c, buf);
		conn_send(c, "\n");
	}
}

static void cmd_sfp(struct conn *c, char **w, int nw)
{
	if (nw < 2) {
		conn_send(c, "Slot 1 - empty\n");
		conn_send(c, "Slot 2 - Rate: 67  Encoding: 06\n");
		conn_send(c, "FS              SFP-10GSR-85    10  \n");
		conn_send(c, "Options: 68\nTemp: 294c\nVcc: 80a7\nTX Bias: 09b4\n"
			    "TX Power: 1136\nRX Power: 0000\nLaser: 0000\nState: 30\n");
		return;
	}
	int slot = atoi(w[1]);
	if (slot != 2) {
		conn_send(c, "Slot 1 - empty\n");
		return;
	}
	if (nw == 2) {
		conn_send(c, "Slot 2 - FS SFP-10GSR-85 (10G)\n");
		return;
	}
	const char *a = w[2];
	if (!strcmp(a, "describe")) {
		conn_send(c, "Slot 2 - Rate: 67  Encoding: 06\n");
		conn_send(c, "FS              SFP-10GSR-85    10  \n");
		conn_send(c, "Options: 68\nTemp: 294c\nVcc: 80a7\nTX Bias: 09b4\n"
			    "TX Power: 1136\nRX Power: 0000\nLaser: 0000\nState: 30\n");
	} else if (!strcmp(a, "dump")) {
		sfp_dump(c);
	} else if (!strcmp(a, "checksum")) {
		conn_send(c, "CC_BASE: ok\nCC_EXT: ok\n");
	} else if (!strcmp(a, "save")) {
		conn_send(c, "SFP EEPROM saved to flash backup\n");
	} else if (!strcmp(a, "restore")) {
		conn_send(c, "SFP EEPROM restored from flash backup\n");
	} else if (!strcmp(a, "fix") || !strcmp(a, "patch") ||
		   !strcmp(a, "clone")) {
		conn_send(c, "SFP EEPROM updated\n");
	} else if (!strcmp(a, "write") && nw >= 5) {
		int off = (int)strtol(w[3], NULL, 16);
		int val = (int)strtol(w[4], NULL, 16);
		if (off >= 0 && off < 256) {
			sfp_eeprom[off] = (uint8_t)val;
			conn_send(c, "Byte written\n");
		} else {
			conn_send(c, "Invalid offset\n");
		}
	} else if (!strcmp(a, "bulk")) {
		conn_send(c, "Bulk write done\n");
	} else if (!strcmp(a, "1g") || !strcmp(a, "2g5") || !strcmp(a, "10g") ||
		   !strcmp(a, "100m") || !strcmp(a, "auto")) {
		conn_send(c, "SFP speed set\n");
	} else {
		conn_send(c, "Unknown SFP command\n");
	}
}

static void cmd_commit(struct conn *c)
{
	conn_send(c, "Config committed\n");
}

static void cmd_mtu(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "show")) {
		conn_send(c, "Port\tMTU\n");
		for (int i = 0; i < MAX_PORTS; i++) {
			char buf[32];
			snprintf(buf, sizeof(buf), "%d\t0x3fff\n", i + 1);
			conn_send(c, buf);
		}
	} else if (nw >= 3) {
		conn_send(c, "MTU set\n");
	} else {
		conn_send(c, "Usage: mtu show|<port> <size>\n");
	}
}

static void cmd_eee(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "status")) {
		conn_send(c, "Port\tEEE\n");
		for (int i = 0; i < MAX_PORTS; i++) {
			char buf[32];
			snprintf(buf, sizeof(buf), "%d\t%s\n", i + 1,
				eee_enabled[i] ? "on" : "off");
			conn_send(c, buf);
		}
	} else if (nw >= 2 && (!strcmp(w[1], "on") || !strcmp(w[1], "off"))) {
		int on = !strcmp(w[1], "on");
		for (int i = 0; i < MAX_PORTS; i++)
			eee_enabled[i] = on;
		conn_send(c, on ? "EEE enabled\n" : "EEE disabled\n");
	} else {
		conn_send(c, "Usage: eee on|off|status\n");
	}
}

static void cmd_mirror(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "status")) {
		char buf[96];
		snprintf(buf, sizeof(buf), "Mirroring: %s\nMonitor Port: %d\n",
			mirror_enabled ? "enabled" : "disabled", mirror_port);
		conn_send(c, buf);
	} else if (nw >= 2 && !strcmp(w[1], "off")) {
		mirror_enabled = 0;
		conn_send(c, "Mirroring disabled\n");
	} else if (nw >= 2) {
		mirror_enabled = 1;
		mirror_port = atoi(w[1]);
		conn_send(c, "Mirroring configured\n");
	}
}

static void cmd_bw(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "status")) {
		conn_send(c, "Port\tIngress\tEgress\n");
		for (int i = 0; i < MAX_PORTS; i++) {
			char buf[48];
			snprintf(buf, sizeof(buf), "%d\t0fffff\t0fffff\n", i + 1);
			conn_send(c, buf);
		}
	} else if (nw >= 2 && (!strcmp(w[1], "in") || !strcmp(w[1], "out"))) {
		conn_send(c, "Bandwidth set\n");
	}
}

static void cmd_storm(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "status")) {
		conn_send(c, "CFG_STORM_EXT (0x5514): 00000000\n"
			  "STORM_EXT_MTRIDX (0x5518): 00000000\n");
	} else if (nw >= 3 && !strcmp(w[1], "on") && nw >= 4) {
		conn_send(c, "Storm control enabled, type: ");
		conn_send(c, w[2]);
		conn_send(c, "\n");
	} else if (nw >= 2 && !strcmp(w[1], "off")) {
		conn_send(c, "Storm control disabled\n");
	} else {
		conn_send(c, "Usage: storm-control on|off|status\n");
	}
}

static void cmd_qos(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "status")) {
		conn_send(c, "Mode: off\nPRI_WEIGHT: 00000000\n");
	} else if (nw >= 3 && !strcmp(w[1], "mode")) {
		conn_send(c, "QoS mode: ");
		conn_send(c, w[2]);
		conn_send(c, "\n");
	} else if (nw >= 2 && !strcmp(w[1], "on")) {
		conn_send(c, "QoS enabled\n");
	} else if (nw >= 2 && !strcmp(w[1], "off")) {
		conn_send(c, "QoS disabled\n");
	} else if (nw >= 2 && !strcmp(w[1], "pcp")) {
		conn_send(c, "PCP -> queue set\n");
	} else if (nw >= 2 && !strcmp(w[1], "dscp")) {
		conn_send(c, "DSCP -> queue set\n");
	} else if (nw >= 2 && !strcmp(w[1], "sched")) {
		conn_send(c, "Queue scheduling set\n");
	} else {
		conn_send(c, "Usage: qos on|off|mode|pcp|dscp|sched|status\n");
	}
}

static void cmd_acl(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "show")) {
		conn_send(c, "Idx Tpl Pmsk Act  Match\n");
	} else if (nw >= 2 && (!strcmp(w[1], "on") || !strcmp(w[1], "off"))) {
		conn_send(c, "ACL ");
		conn_send(c, w[1]);
		conn_send(c, "\n");
	} else if (nw >= 2 && !strcmp(w[1], "add")) {
		conn_send(c, "ACL rule added\n");
	} else if (nw >= 3 && !strcmp(w[1], "del")) {
		conn_send(c, "ACL rule deleted\n");
	} else {
		conn_send(c, "Usage: acl on|off|add|del|show\n");
	}
}

static void cmd_lag(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "show")) {
		conn_send(c, "LAG\tMembers\tHash\n0\t0000000000000000\t3f\n");
	} else if (nw >= 2) {
		conn_send(c, "LAG configured\n");
	}
}

static void cmd_isolate(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "show")) {
		conn_send(c, "Isolation: none\n");
	} else {
		conn_send(c, "Isolation configured\n");
	}
}

static void cmd_ingress(struct conn *c, char **w, int nw)
{
	if (nw < 2) {
		conn_send(c, "Usage: ingress <port><u|t|a>...\n");
		return;
	}
	conn_send(c, "Ingress filter set\n");
}

static void cmd_pvid(struct conn *c, char **w, int nw)
{
	if (nw < 3) {
		conn_send(c, "Usage: pvid <port> <vlan-id>\n");
		return;
	}
	int n = atoi(w[1]);
	if (n >= 1 && n <= MAX_PORTS)
		ports[n - 1].pvid = (uint8_t)atoi(w[2]);
	conn_send(c, "PVID set\n");
}

static void cmd_hostname(struct conn *c, char **w, int nw)
{
	if (nw >= 2) {
		snprintf(hostname, sizeof(hostname), "%s", w[1]);
		conn_send(c, "Hostname set to ");
		conn_send(c, hostname);
		conn_send(c, "\n");
	} else {
		conn_send(c, hostname);
		conn_send(c, "\n");
	}
}

static void cmd_passwd(struct conn *c, char **w, int nw)
{
	if (nw >= 2) {
		snprintf(passwd, sizeof(passwd), "%s", w[1]);
		conn_send(c, "Password changed\n");
	} else {
		conn_send(c, "Usage: passwd <password>\n");
	}
}

static void cmd_ip(struct conn *c, char **w, int nw)
{
	if (nw == 1) {
		conn_send(c, "Current IP: 192.168.10.247 (static)\n");
	} else if (nw >= 2 && !strcmp(w[1], "dhcp")) {
		conn_send(c, "DHCP started\n");
	} else {
		conn_send(c, "Setting ip: ");
		conn_send(c, w[1]);
		conn_send(c, "\n");
	}
}

static void cmd_gw(struct conn *c, char **w, int nw)
{
	if (nw == 1)
		conn_send(c, "Current gw: 192.168.10.1\n");
	else {
		conn_send(c, "Setting gw: ");
		conn_send(c, w[1]);
		conn_send(c, "\n");
	}
}

static void cmd_netmask(struct conn *c, char **w, int nw)
{
	if (nw == 1)
		conn_send(c, "Current netmask: 255.255.255.0\n");
	else {
		conn_send(c, "Setting netmask: ");
		conn_send(c, w[1]);
		conn_send(c, "\n");
	}
}

static void cmd_show_port_vlan(struct conn *c)
{
	conn_send(c, "Ingress VLAN configuration:\n");
	conn_send(c, "Port\tPVID\tType\tFiltering\n");
	for (int i = 0; i < MAX_PORTS; i++) {
		char buf[64];
		snprintf(buf, sizeof(buf), "%d\t0x%04x\tAny\tEnabled\n",
			i + 1, ports[i].pvid);
		conn_send(c, buf);
	}
	conn_send(c, "\nType - Which frame types are allowed: untagged, tagged or any\n"
		    "Filtering - Whether packets not belonging to member VLANs on that "
		    "port are dropped\nPVID - Assumed VLAN for untagged packets\n");
}

static void cmd_show_running_config(struct conn *c)
{
	char buf[256];
	snprintf(buf, sizeof(buf),
		"hostname %s\n"
		"ip 192.168.10.247\n"
		"gw 192.168.10.1\n"
		"netmask 255.255.255.0\n"
		"passwd %s\n"
		"stp %s\n"
		"telnet %s\n"
		"web %s\n"
		"led %s\n",
		hostname, passwd,
		stp_enabled ? "on" : "off",
		telnet_enabled ? "on" : "off",
		web_enabled ? "on" : "off",
		led_enabled ? "on" : "off");
	conn_send(c, buf);
}

static void cmd_show_startup_config(struct conn *c)
{
	/* Simulator: running and startup config are the same. */
	cmd_show_running_config(c);
}

static void cmd_show_arp(struct conn *c)
{
	conn_send(c, "IP               MAC                 Age\n");
	conn_send(c, "192.168.10.1     18:ec:e7:95:a9:87   0s\n");
}

static void cmd_lldp(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "on")) {
		conn_send(c, "LLDP enabled\n");
	} else if (nw >= 2 && !strcmp(w[1], "off")) {
		conn_send(c, "LLDP disabled\n");
	} else if (nw >= 2 && !strcmp(w[1], "show")) {
		conn_send(c, "Port  Chassis ID         Port ID      System Name      TTL\n");
		conn_send(c, "1     18:ec:e7:95:a9:87  port 1      neighbor-switch    118\n");
	} else {
		conn_send(c, "LLDP: disabled\n");
	}
}

static void cmd_stp(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "on")) {
		stp_enabled = 1;
		conn_send(c, "STP enabled\n");
	} else {
		stp_enabled = 0;
		conn_send(c, "STP disabled\n");
	}
}

static void cmd_led(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "on")) {
		led_enabled = 1;
		conn_send(c, "LEDs enabled\n");
	} else if (nw >= 2 && !strcmp(w[1], "off")) {
		led_enabled = 0;
		conn_send(c, "LEDs disabled\n");
	} else {
		conn_send(c, led_enabled ? "LEDs enabled\n" : "LEDs disabled\n");
	}
}

static void cmd_ping(struct conn *c, char **w, int nw)
{
	if (nw < 2) {
		conn_send(c, "Usage: ping <ip>\n");
		return;
	}
	/* Validate dotted quad */
	const char *ip = w[1];
	int dots = 0, digits = 0, ok = 1;
	for (const char *p = ip; *p; p++) {
		if (*p == '.') {
			dots++;
			digits = 0;
		} else if (*p >= '0' && *p <= '9') {
			digits++;
		} else {
			ok = 0;
			break;
		}
	}
	if (!ok || dots != 3) {
		conn_send(c, "Bad IP\n");
		return;
	}
	char buf[160];
	for (int seq = 0; seq < 4; seq++) {
		snprintf(buf, sizeof(buf), "Reply from %s: seq=%d time=0ms\n", ip, seq);
		conn_send(c, buf);
	}
	snprintf(buf, sizeof(buf),
		"--- %s ping statistics ---\n"
		"4 packets transmitted, 4 received, 0%% packet loss\n"
		"rtt min/avg/max = 0/0/0 ms\n",
		ip);
	conn_send(c, buf);
}

static void cmd_igmp(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "on")) {
		igmp_enabled = 1;
		conn_send(c, "IGMP snooping enabled\n");
	} else if (nw >= 2 && !strcmp(w[1], "off")) {
		igmp_enabled = 0;
		conn_send(c, "IGMP snooping disabled\n");
	} else if (nw >= 2 && !strcmp(w[1], "show")) {
		conn_send(c, igmp_enabled ? "IGMP snooping: enabled\n"
					    : "IGMP snooping: disabled\n");
	} else if (nw >= 3 && !strcmp(w[1], "mld")) {
		if (!strcmp(w[2], "on"))
			conn_send(c, "MLD snooping enabled\n");
		else if (!strcmp(w[2], "off"))
			conn_send(c, "MLD snooping disabled\n");
		else if (!strcmp(w[2], "show"))
			conn_send(c, "IGMP_MLD_EN: off\n");
	} else if (nw >= 3 && !strcmp(w[1], "querier")) {
		if (!strcmp(w[2], "on"))
			conn_send(c, "IGMP querier enabled\n");
		else if (!strcmp(w[2], "off"))
			conn_send(c, "IGMP querier disabled\n");
		else if (!strcmp(w[2], "show"))
			conn_send(c, "Query interval: 0\n");
	}
}

static void cmd_telnet(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "on")) {
		telnet_enabled = 1;
		conn_send(c, "Telnet enabled\n");
	} else if (nw >= 2 && !strcmp(w[1], "off")) {
		telnet_enabled = 0;
		conn_send(c, "Telnet disabled\n");
	} else {
		conn_send(c, telnet_enabled ? "Telnet enabled\n" : "Telnet disabled\n");
	}
}

static void cmd_web(struct conn *c, char **w, int nw)
{
	if (nw >= 2 && !strcmp(w[1], "on")) {
		web_enabled = 1;
		conn_send(c, "Web enabled\n");
	} else if (nw >= 2 && !strcmp(w[1], "off")) {
		web_enabled = 0;
		conn_send(c, "Web disabled\n");
	} else {
		conn_send(c, web_enabled ? "Web enabled\n" : "Web disabled\n");
	}
}

static void cmd_time(struct conn *c)
{
	conn_send(c, "  Tick counter: 1234567890   Sec Counter: 00123456\n");
}

static void cmd_history(struct conn *c)
{
	conn_send(c, history_buf);
	if (history_len)
		conn_send(c, "\n");
}

static void cmd_reset(struct conn *c)
{
	conn_send(c, "\nRESET\n\n");
	/* Simulate a reboot by re-initializing the state */
	memset(ports, 0, sizeof(ports));
	for (int i = 0; i < MAX_PORTS; i++) {
		ports[i].enabled = 1;
		ports[i].pvid = 1;
		if (i == 0)
			ports[i].link = 4;  /* port 1 up at 1G */
	}
	nvlans = 0;
	nl2 = 0;
}

static void cmd_rnd(struct conn *c)
{
	conn_send(c, "Random: a1b2c3d4e5f6\n");
}

static void cmd_regget(struct conn *c, char **w, int nw)
{
	if (nw >= 2) {
		char buf[64];
		snprintf(buf, sizeof(buf), "REGGET: %s: VAL: 0000000a\n", w[1]);
		conn_send(c, buf);
	} else {
		conn_send(c, "Usage: regget <hex-reg>\n");
	}
}

static void cmd_regset(struct conn *c, char **w, int nw)
{
	if (nw >= 3) {
		conn_send(c, "Register written\n");
	} else {
		conn_send(c, "Usage: regset <hex-reg> <hex-val>\n");
	}
}

/* ------------------------------------------------------------------ */
/* Command dispatch                                                   */
/* ------------------------------------------------------------------ */

static void execute_line(struct conn *c, char *line)
{
	/* Tokenize */
	char *words[15];
	int nw = 0;
	char *save = NULL;
	for (char *tok = strtok_r(line, " \t", &save);
	     tok && nw < 15; tok = strtok_r(NULL, " \t", &save))
		words[nw++] = tok;
	if (nw == 0)
		return;

	/* Record history */
	if (history_len < (int)sizeof(history_buf) - 1) {
		int l = strlen(line);
		if (history_len + l + 2 < (int)sizeof(history_buf)) {
			if (history_len)
				history_buf[history_len++] = ' ';
			memcpy(history_buf + history_len, line, l);
			history_len += l;
		}
	}

	const char *cmd = words[0];

	/* Lite console: flat command set, no mode system. The simulator
	 * targets the Lite variant only — rtlpctl talks to the device through
	 * the HTTP API and the common command vocabulary, which are identical
	 * in both firmware variants (Full's modes/?/Tab are never used). */
	if (!strcmp(cmd, "?") || !strcmp(cmd, "help")) {
		conn_send(c, "Unknown command\n");
		return;
	}

	if (!strcmp(cmd, "version"))
		cmd_version(c);
	else if (!strcmp(cmd, "show") && nw >= 2 && !strcmp(words[1], "running-config"))
		cmd_show_running_config(c);
	else if (!strcmp(cmd, "show") && nw >= 2 && !strcmp(words[1], "startup-config"))
		cmd_show_startup_config(c);
	else if (!strcmp(cmd, "show") && nw >= 2 && !strcmp(words[1], "arp"))
		cmd_show_arp(c);
	else if (!strcmp(cmd, "show") && nw >= 2 && !strcmp(words[1], "port"))
		cmd_show_port_vlan(c);
	else if (!strcmp(cmd, "ping"))
		cmd_ping(c, words, nw);
	else if (!strcmp(cmd, "show"))
		cmd_show(c);
	else if (!strcmp(cmd, "stat"))
		cmd_stat(c);
	else if (!strcmp(cmd, "port"))
		cmd_port(c, words, nw);
	else if (!strcmp(cmd, "vlan"))
		cmd_vlan(c, words, nw);
	else if (!strcmp(cmd, "l2"))
		cmd_l2(c, words, nw);
	else if (!strcmp(cmd, "sfp"))
		cmd_sfp(c, words, nw);
	else if (!strcmp(cmd, "mtu"))
		cmd_mtu(c, words, nw);
	else if (!strcmp(cmd, "eee"))
		cmd_eee(c, words, nw);
	else if (!strcmp(cmd, "mirror"))
		cmd_mirror(c, words, nw);
	else if (!strcmp(cmd, "bw"))
		cmd_bw(c, words, nw);
	else if (!strcmp(cmd, "storm-control"))
		cmd_storm(c, words, nw);
	else if (!strcmp(cmd, "qos"))
		cmd_qos(c, words, nw);
	else if (!strcmp(cmd, "acl"))
		cmd_acl(c, words, nw);
	else if (!strcmp(cmd, "lag"))
		cmd_lag(c, words, nw);
	else if (!strcmp(cmd, "isolate"))
		cmd_isolate(c, words, nw);
	else if (!strcmp(cmd, "ingress"))
		cmd_ingress(c, words, nw);
	else if (!strcmp(cmd, "pvid"))
		cmd_pvid(c, words, nw);
	else if (!strcmp(cmd, "hostname"))
		cmd_hostname(c, words, nw);
	else if (!strcmp(cmd, "passwd"))
		cmd_passwd(c, words, nw);
	else if (!strcmp(cmd, "ip"))
		cmd_ip(c, words, nw);
	else if (!strcmp(cmd, "gw"))
		cmd_gw(c, words, nw);
	else if (!strcmp(cmd, "netmask"))
		cmd_netmask(c, words, nw);
	else if (!strcmp(cmd, "stp"))
		cmd_stp(c, words, nw);
	else if (!strcmp(cmd, "led"))
		cmd_led(c, words, nw);
	else if (!strcmp(cmd, "lldp"))
		cmd_lldp(c, words, nw);
	else if (!strcmp(cmd, "igmp"))
		cmd_igmp(c, words, nw);
	else if (!strcmp(cmd, "telnet"))
		cmd_telnet(c, words, nw);
	else if (!strcmp(cmd, "web"))
		cmd_web(c, words, nw);
	else if (!strcmp(cmd, "commit"))
		cmd_commit(c);
	else if (!strcmp(cmd, "time"))
		cmd_time(c);
	else if (!strcmp(cmd, "history"))
		cmd_history(c);
	else if (!strcmp(cmd, "reset"))
		cmd_reset(c);
	else if (!strcmp(cmd, "rnd"))
		cmd_rnd(c);
	else if (!strcmp(cmd, "regget"))
		cmd_regget(c, words, nw);
	else if (!strcmp(cmd, "regset"))
		cmd_regset(c, words, nw);
	else if (!strcmp(cmd, "flash") || !strcmp(cmd, "sds") ||
		 !strcmp(cmd, "gpio") || !strcmp(cmd, "sdsget") ||
		 !strcmp(cmd, "sdsset") || !strcmp(cmd, "phyget") ||
		 !strcmp(cmd, "physet") || !strcmp(cmd, "xmodem"))
		conn_send(c, "Simulated: no-op\n");
	else
		conn_send(c, "Unknown command\n");
}

static void handle_connection(int fd)
{
	struct conn c = { 0 };
	c.fd = fd;

	conn_send(&c, "Password: ");
	c.auth_ok = 0;

	char buf[1];
	while (read(fd, buf, 1) == 1) {
		uint8_t b = (uint8_t)buf[0];

		if (b == 0xff) {          /* IAC */
			c.iac_state = 1;
			continue;
		}
		if (c.iac_state) {
			telnet_negotiate(&c, b);
			continue;
		}

		if (!c.auth_ok) {
			if (b == '\r' || b == '\n') {
				if (c.line_len == 0) {
					conn_send(&c, "Password: ");
					continue;
				}
				c.line[c.line_len] = '\0';
				if (strcmp(c.line, passwd) == 0) {
					c.auth_ok = 1;
					conn_send(&c, "\r\n");
					print_prompt(&c);
				} else {
					conn_send(&c, "\r\nPassword: ");
				}
				c.line_len = 0;
			} else if (b == 127 || b == 8) {
				if (c.line_len > 0)
					c.line_len--;
			} else if (c.line_len < (int)sizeof(c.line) - 1) {
				c.line[c.line_len++] = (char)b;
			}
			continue;
		}

		if (b == '\r' || b == '\n') {
			if (c.line_len == 0) {
				conn_send(&c, "\r\n");
				print_prompt(&c);
				continue;
			}
			c.line[c.line_len] = '\0';
			conn_send(&c, "\r\n");
			execute_line(&c, c.line);
			c.line_len = 0;
			print_prompt(&c);
		} else if (b == 127 || b == 8) {
			if (c.line_len > 0) {
				c.line_len--;
				conn_send(&c, "\b \b");
			}
		} else if (b == '\t') {
			/* Tab completion not simulated */
		} else if (b >= 32 && b < 127) {
			if (c.line_len < (int)sizeof(c.line) - 1) {
				c.line[c.line_len++] = (char)b;
				conn_send_byte(&c, (char)b);
			}
		}
	}
	close(fd);
}

/* ------------------------------------------------------------------ */
/* Server boilerplate (same pattern as httpd_sim.c)                   */
/* ------------------------------------------------------------------ */

struct Server serverConstructor(int port, void (*launch)(struct Server *server)) {
	struct Server server;

	server.domain = AF_INET;
	server.service = SOCK_STREAM;
	server.port = port;
	server.protocol = 0;
	server.backlog = 10;

	server.address.sin_family = server.domain;
	server.address.sin_port = htons(port);
	server.address.sin_addr.s_addr = htonl(INADDR_ANY);

	server.socket = socket(server.domain, server.service, server.protocol);
	if (server.socket < 0) {
		perror("Failed to initialize/connect to socket...\n");
		exit(EXIT_FAILURE);
	}

	int opt = 1;
	setsockopt(server.socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	if (bind(server.socket, (struct sockaddr*)&server.address,
		 sizeof(server.address)) < 0) {
		perror("Failed to bind socket...\n");
		exit(EXIT_FAILURE);
	}

	if (listen(server.socket, server.backlog) < 0) {
		perror("Failed to start listening...\n");
		exit(EXIT_FAILURE);
	}

	server.launch = launch;
	return server;
}

void launch(struct Server *server)
{
	while (1) {
		printf("=== Waiting for connection on port %d === \n", server->port);
		fflush(stdout);
		int addrlen = sizeof(server->address);
		int new_socket = accept(server->socket,
					(struct sockaddr*)&server->address,
					(socklen_t*)&addrlen);
		if (new_socket < 0) {
			perror("accept");
			continue;
		}
		printf("Connection accepted\n");
		fflush(stdout);
		handle_connection(new_socket);
	}
}

int main(int argc, char **argv)
{
	signal(SIGPIPE, SIG_IGN);
	setvbuf(stdout, NULL, _IONBF, 0);

	int port = 2323;
	if (argc > 1)
		port = atoi(argv[1]);

	sfp_eeprom_init();
	/* Default port state: port 1 up at 1G, all others down */
	for (int i = 0; i < MAX_PORTS; i++) {
		ports[i].enabled = 1;
		ports[i].pvid = 1;
		ports[i].link = (i == 0) ? 4 : 0;
	}

	printf("cli_sim: telnet CLI simulator (Lite variant) on port %d\n", port);
	printf("password: %s, hostname: %s\n", PASSWORD, hostname);

	struct Server server = serverConstructor(port, launch);
	server.launch(&server);
	return 0;
}
