#pragma codeseg BANK3
#pragma constseg BANK3

#include "rtl837x_common.h"
#include "rtl837x_flash.h"
#include "rtl837x_regs.h"
#include "rtl837x_sfr.h"
#include "rtl837x_storm.h"
#include "rtl837x_qos.h"
#include "rtl837x_port.h"
#include "machine.h"
#include "uip/uip.h"

/* Dedicated buffer for the serialized startup config: the shared
 * flash_buf is only 512 bytes (FLASH_BUF_SIZE is load-bearing in the
 * firmware update path), and the VLAN/PVID/MTU/LAG/mirror lines do not
 * fit otherwise. */
#define COMMIT_BUF_SIZE 1024
__xdata uint8_t commit_buf[COMMIT_BUF_SIZE];

extern __xdata uint8_t flash_buf[FLASH_BUF_SIZE];
extern __xdata struct flash_region_t flash_region;
extern __xdata char hostname[32];
extern __xdata char passwd[21];
extern __xdata uip_ipaddr_t uip_hostaddr, uip_draddr, uip_netmask;
extern __xdata uint8_t stpEnabled;
extern __xdata uint8_t ledEnabled;
extern __xdata uint16_t management_vlan;
extern __xdata uint8_t telnet_enabled;
extern __xdata uint8_t web_enabled;
extern __xdata uint8_t preshared_key[32];
extern __xdata uint8_t sfr_data[4];
extern __xdata uint8_t cmd_buffer[128];
extern __xdata uint8_t cmd_words_b[15];

extern void reg_read_m(uint16_t reg_addr);
extern void reg_write_m(uint16_t reg_addr);
extern __code const struct machine machine;
extern __xdata uint8_t vlan_names[VLAN_NAMES_SIZE];

static __xdata uint16_t commit_pos;
static __xdata uint8_t cfg_psk_set;
static __xdata uint8_t cfg_psk_i;
static __xdata uint16_t cfg_i;
/* Scratch for the decimal emitters below (block-scope locals of the
 * macros would land in the internal-RAM overlay, which is full) */
static __xdata uint16_t cfg_vv16;
static __xdata uint32_t cfg_vv32;
static __xdata uint8_t cfg_d;
static __xdata uint8_t cfg_z;

#define COMMIT_PUTC(c) do { \
	if (commit_pos < COMMIT_BUF_SIZE) \
		commit_buf[commit_pos++] = (c); \
} while(0)

#define COMMIT_PUTS(s) do { \
	__code char *__p = (s); \
	while (*__p) { \
		if (commit_pos >= COMMIT_BUF_SIZE) break; \
		commit_buf[commit_pos++] = *__p++; \
	} \
} while(0)

#define COMMIT_PUTSX(s) do { \
	__xdata char *__p = (s); \
	while (*__p) { \
		if (commit_pos >= COMMIT_BUF_SIZE) break; \
		commit_buf[commit_pos++] = *__p++; \
	} \
} while(0)

#define COMMIT_BYTE(v) do { \
	cfg_d = (v); \
	if (cfg_d >= 100) { COMMIT_PUTC('0' + (cfg_d / 100)); } \
	if (cfg_d >= 10) { COMMIT_PUTC('0' + ((cfg_d / 10) % 10)); } \
	COMMIT_PUTC('0' + (cfg_d % 10)); \
} while(0)

/* 16 bit decimal output via repeated subtraction, so no 16 bit division
   library routine (__divuint/__moduint) gets linked in.  Leading zeros
   are suppressed; the config parser (atoi_short) accepts both forms. */
#define COMMIT_DEC16(v16) do { \
	cfg_vv16 = (v16); \
	cfg_z = 0; \
	cfg_d = 0; while (cfg_vv16 >= 10000) { cfg_vv16 -= 10000; cfg_d++; } \
	if (cfg_d || cfg_z) { COMMIT_PUTC('0' + cfg_d); cfg_z = 1; } \
	cfg_d = 0; while (cfg_vv16 >= 1000) { cfg_vv16 -= 1000; cfg_d++; } \
	if (cfg_d || cfg_z) { COMMIT_PUTC('0' + cfg_d); cfg_z = 1; } \
	cfg_d = 0; while (cfg_vv16 >= 100) { cfg_vv16 -= 100; cfg_d++; } \
	if (cfg_d || cfg_z) { COMMIT_PUTC('0' + cfg_d); cfg_z = 1; } \
	cfg_d = 0; while (cfg_vv16 >= 10) { cfg_vv16 -= 10; cfg_d++; } \
	if (cfg_d || cfg_z) { COMMIT_PUTC('0' + cfg_d); } \
	COMMIT_PUTC('0' + cfg_vv16); \
} while(0)

/* Same for a 24-bit value (storm rates up to 10,000,000 kbps) */
#define COMMIT_DEC24(v24) do { \
	cfg_vv32 = (v24); \
	cfg_z = 0; \
	cfg_d = 0; while (cfg_vv32 >= 1000000) { cfg_vv32 -= 1000000; cfg_d++; } \
	if (cfg_d || cfg_z) { COMMIT_PUTC('0' + cfg_d); cfg_z = 1; } \
	cfg_d = 0; while (cfg_vv32 >= 100000) { cfg_vv32 -= 100000; cfg_d++; } \
	if (cfg_d || cfg_z) { COMMIT_PUTC('0' + cfg_d); cfg_z = 1; } \
	cfg_d = 0; while (cfg_vv32 >= 10000) { cfg_vv32 -= 10000; cfg_d++; } \
	if (cfg_d || cfg_z) { COMMIT_PUTC('0' + cfg_d); cfg_z = 1; } \
	cfg_d = 0; while (cfg_vv32 >= 1000) { cfg_vv32 -= 1000; cfg_d++; } \
	if (cfg_d || cfg_z) { COMMIT_PUTC('0' + cfg_d); cfg_z = 1; } \
	cfg_d = 0; while (cfg_vv32 >= 100) { cfg_vv32 -= 100; cfg_d++; } \
	if (cfg_d || cfg_z) { COMMIT_PUTC('0' + cfg_d); cfg_z = 1; } \
	cfg_d = 0; while (cfg_vv32 >= 10) { cfg_vv32 -= 10; cfg_d++; } \
	if (cfg_d || cfg_z) { COMMIT_PUTC('0' + cfg_d); } \
	COMMIT_PUTC('0' + cfg_vv32); \
} while(0)

#define COMMIT_IP(ip) do { \
	COMMIT_BYTE((ip)[0] & 0xFF); COMMIT_PUTC('.'); \
	COMMIT_BYTE((ip)[0] >> 8); COMMIT_PUTC('.'); \
	COMMIT_BYTE((ip)[1] & 0xFF); COMMIT_PUTC('.'); \
	COMMIT_BYTE((ip)[1] >> 8); COMMIT_PUTC('\n'); \
} while(0)

#define COMMIT_HEX_DIGIT(n) do { \
	cfg_d = (n) & 0x0f; \
	COMMIT_PUTC(cfg_d < 10 ? '0' + cfg_d : 'a' + cfg_d - 10); \
} while(0)

#define COMMIT_HEX8(v) do { \
	cfg_d = (v); \
	COMMIT_HEX_DIGIT(cfg_d >> 4); \
	COMMIT_HEX_DIGIT(cfg_d); \
} while(0)

static void commit_write_flash(void)
{
	uint16_t written = 0;
	while (written < commit_pos) {
		uint16_t chunk = commit_pos - written;
		if (chunk > FLASH_PAGE_SIZE)
			chunk = FLASH_PAGE_SIZE;
		flash_region.addr = CONFIG_START + written;
		flash_region.len = chunk;
		flash_write_bytes(commit_buf + written);
		written += chunk;
	}
	commit_buf[0] = 0;
	flash_region.addr = CONFIG_START + commit_pos;
	flash_region.len = 1;
	flash_write_bytes(commit_buf);
}

/* Storm type names (BANK3 const; rtl837x_storm.c's table is BANK2 const
 * and not readable from here). */
static __code char * __code storm_type_names[4] = {
	"broadcast", "multicast", "dlf", "unknown-mcast"
};

/* QoS mode names */
static __code char * __code qos_mode_names[4] = {
	"off", "pcp", "dscp", "both"
};

/*
 * Serialize the current in-memory configuration into commit_buf as a
 * NUL-terminated text blob.  Used both by parse_commit() (flash write)
 * and show_running_config() (console output).
 */
static void cfg_serialize_ports(void)
{
	/* PVID: one line per non-default PVID (default is 1). */
	for (cfg_i = machine.min_port; cfg_i <= machine.max_port; cfg_i++) {
		__xdata uint16_t pvid = port_pvid_get(cfg_i);
		if (pvid == 1)
			continue;
		COMMIT_PUTS("pvid ");
		COMMIT_BYTE(machine.log_to_phys_port[cfg_i]);
		COMMIT_PUTC(' ');
		COMMIT_DEC16(pvid);
		COMMIT_PUTC('\n');
	}

	/* MTU: one line per port. */
	for (cfg_i = machine.min_port; cfg_i <= machine.max_port; cfg_i++) {
		reg_read_m(RTL8373_REG_MAC_L2_PORT_MAX_LEN + ((uint16_t) cfg_i << 8));
		__xdata uint16_t mtu = SFR_DATA_U16 & 0x3fff;
		COMMIT_PUTS("mtu ");
		COMMIT_BYTE(machine.log_to_phys_port[cfg_i]);
		COMMIT_PUTC(' ');
		COMMIT_DEC16(mtu);
		COMMIT_PUTC('\n');
	}

}

static void cfg_serialize_lag_mirror(void)
{
	/* LAG groups: one line per non-empty group (members are logical
	 * port bits, serialized as physical port numbers). */
	for (cfg_i = 0; cfg_i < 4; cfg_i++) {
		reg_read_m(RTL837X_TRK_MBR_CTRL_BASE + (cfg_i << 2));
		__xdata uint16_t members = (sfr_data[2] << 8) | sfr_data[3];
		if (!members)
			continue;
		COMMIT_PUTS("lag ");
		COMMIT_BYTE(cfg_i);
		for (__xdata uint8_t lp = machine.min_port; lp <= machine.max_port; lp++) {
			if (members & ((uint16_t)1 << lp)) {
				COMMIT_PUTC(' ');
				COMMIT_BYTE(machine.log_to_phys_port[lp]);
			}
		}
		COMMIT_PUTC('\n');
	}

	/* Mirror: one line when a mirror session is active.  The masks are
	 * logical port bits; ports with only one direction get the r/t
	 * suffix (both directions: no suffix). */
	reg_read_m(RTL837x_MIRROR_CTRL);
	if (sfr_data[3] & 0x01) {
		__xdata uint8_t mport = (sfr_data[3] >> 1) & 0x0f;
		reg_read_m(RTL837x_MIRROR_CONF);
		__xdata uint16_t rx = (sfr_data[0] << 8) | sfr_data[1];
		__xdata uint16_t tx = (sfr_data[2] << 8) | sfr_data[3];
		if (mport <= machine.max_port) {
			COMMIT_PUTS("mirror ");
			COMMIT_BYTE(machine.log_to_phys_port[mport]);
			for (__xdata uint8_t lp = machine.min_port; lp <= machine.max_port; lp++) {
				if ((rx & ((uint16_t)1 << lp)) || (tx & ((uint16_t)1 << lp))) {
					COMMIT_PUTC(' ');
					COMMIT_BYTE(machine.log_to_phys_port[lp]);
					if (!(rx & ((uint16_t)1 << lp)))
						COMMIT_PUTC('t');
					else if (!(tx & ((uint16_t)1 << lp)))
						COMMIT_PUTC('r');
				}
			}
			COMMIT_PUTC('\n');
		}
	}

}

static void cfg_serialize_vlans(void)
{
	/* VLANs: one line per valid entry, in the CLI form
	 * "vlan <vid> [<name>] <port>[t] ..." where 't' marks tagged
	 * members (untagged is the default). */
	{
		__xdata uint16_t vid;
		for (vid = 1; vid < 4095; vid++) {
			if (vlan_get(vid) < 0)
				continue;
			if (!(sfr_data[0] & 0x02))	/* bit 1: entry valid */
				continue;
			/* Entry layout: bits 0-9 member ports, bits 10-19 untagged
			 * ports (sfr_data[3] is the low byte). */
			__xdata uint16_t vmembers = ((uint16_t)(sfr_data[2] & 0x03) << 8) | sfr_data[3];
			__xdata uint16_t vuntag = ((uint16_t)(sfr_data[1] & 0x0f) << 6) | (sfr_data[2] >> 2);
			if (!vmembers)
				continue;
			COMMIT_PUTS("vlan ");
			COMMIT_DEC16(vid);
			__xdata uint16_t vn = vlan_name(vid);
			if (vn != 0xffff) {
				COMMIT_PUTC(' ');
				while (vlan_names[vn] && vlan_names[vn] != ' ')
					COMMIT_PUTC(vlan_names[vn++]);
			}
			for (__xdata uint8_t lp = machine.min_port; lp <= machine.max_port; lp++) {
				if (vmembers & ((uint16_t)1 << lp)) {
					COMMIT_PUTC(' ');
					COMMIT_BYTE(machine.log_to_phys_port[lp]);
					if (!(vuntag & ((uint16_t)1 << lp)))
						COMMIT_PUTC('t');
				}
			}
			COMMIT_PUTC('\n');
			if (commit_pos >= COMMIT_BUF_SIZE - 32)
				break;	/* leave room for the terminator */
		}
	}

}

static uint16_t config_serialize(void)
{
	commit_pos = 0;

	if (hostname[0]) {
		COMMIT_PUTS("hostname "); COMMIT_PUTSX(hostname); COMMIT_PUTC('\n');
	}

	COMMIT_PUTS("ip "); COMMIT_IP(uip_hostaddr);
	COMMIT_PUTS("gw "); COMMIT_IP(uip_draddr);
	COMMIT_PUTS("netmask "); COMMIT_IP(uip_netmask);

	if (passwd[0]) {
		COMMIT_PUTS("passwd "); COMMIT_PUTSX(passwd); COMMIT_PUTC('\n');
	}

	{
		cfg_psk_set = 0;
		for (cfg_psk_i = 0; cfg_psk_i < 32; cfg_psk_i++)
			cfg_psk_set |= preshared_key[cfg_psk_i];
		if (cfg_psk_set) {
			COMMIT_PUTS("preshared_key ");
			for (cfg_psk_i = 0; cfg_psk_i < 32; cfg_psk_i++)
				COMMIT_HEX8(preshared_key[cfg_psk_i]);
			COMMIT_PUTC('\n');
		}
	}

	if (stpEnabled)
		COMMIT_PUTS("stp on\n");
	else
		COMMIT_PUTS("stp off\n");

	if (ledEnabled)
		COMMIT_PUTS("led on\n");
	else
		COMMIT_PUTS("led off\n");

	if (management_vlan >= 2) {
		COMMIT_PUTS("vlan ");
		COMMIT_DEC16(management_vlan);
		COMMIT_PUTS(" mgmt\n");
	}

	COMMIT_PUTS("telnet ");
	if (telnet_enabled) COMMIT_PUTS("on\n"); else COMMIT_PUTS("off\n");
	COMMIT_PUTS("web ");
	if (web_enabled) COMMIT_PUTS("on\n"); else COMMIT_PUTS("off\n");

	/* Storm control: one line per enabled type (replay: all ports).
	 * The 'p'/'k' suffix records the meter mode (pps vs kbps). */
	for (cfg_i = 0; cfg_i < 4; cfg_i++) {
		if (storm_type_en[cfg_i]) {
			COMMIT_PUTS("storm-control on ");
			COMMIT_PUTS(storm_type_names[cfg_i]);
			COMMIT_PUTC(' ');
			COMMIT_DEC24(storm_type_rate[cfg_i]);
			COMMIT_PUTC(storm_type_pps[cfg_i] ? 'p' : 'k');
			COMMIT_PUTC('\n');
		}
	}

	/* QoS mode (the PCP/DSCP maps themselves are not persisted yet:
	 * 64 entries do not fit the config buffer; they reset to
	 * the identity/zero maps on boot.  ACL rules live in the ASIC only. */
	if (qos_mode != QOS_MODE_OFF) {
		COMMIT_PUTS("qos mode ");
		if (qos_mode <= 3)
			COMMIT_PUTS(qos_mode_names[qos_mode]);
		else
			COMMIT_PUTS("pcp");
		COMMIT_PUTC('\n');
	}


	cfg_serialize_ports();
	cfg_serialize_lag_mirror();
	cfg_serialize_vlans();
	commit_buf[commit_pos] = 0;
	return commit_pos;
}

void parse_commit(void) __banked
{
	config_serialize();
	flash_region.addr = CONFIG_START;
	flash_init(0);
	flash_sector_erase();
	commit_write_flash();
	flash_init(1);
	print_string("Config committed\n");
}

/*
 * Show the configuration that would be saved by `commit`, without
 * touching the flash.
 */
static __xdata uint16_t cfg_len;

/* Serialize the running configuration into commit_buf and return its
 * length, without printing.  Used by the HTTP /running-config endpoint
 * (page_impl.c); show_running_config prints the same buffer to the
 * console. */
uint16_t running_config_serialize(void) __banked
{
	return config_serialize();
}

void show_running_config(void) __banked
{
	cfg_len = config_serialize();
	for (cfg_i = 0; cfg_i < cfg_len; cfg_i++)
		write_char(commit_buf[cfg_i]);
}

/*
 * Show the configuration stored in flash (startup-config).
 * Reads CONFIG_START in 256 byte chunks until the NUL terminator
 * (same scan pattern as send_config() in httpd/page_impl.c).
 */
static __xdata uint32_t cfg_pos;
static __xdata uint16_t cfg_left;
static __xdata uint16_t cfg_chunk;

void show_startup_config(void) __banked
{
	cfg_pos = CONFIG_START;
	cfg_left = CONFIG_LEN;

	do {
		cfg_chunk = (cfg_left > 256) ? 256 : cfg_left;
		flash_region.addr = cfg_pos;
		flash_region.len = cfg_chunk;
		flash_read_bulk(flash_buf);
		for (cfg_i = 0; cfg_i < cfg_chunk; cfg_i++) {
			if (!flash_buf[cfg_i])
				return;
			write_char(flash_buf[cfg_i]);
		}
		cfg_left -= cfg_chunk;
		cfg_pos += cfg_chunk;
	} while (cfg_left > 0);
}

void parse_l2_delete(void) __banked
{
	uint16_t idx = 0;
	uint8_t di = cmd_words_b[2];
	while (1) {
		uint8_t c = cmd_buffer[di];
		if (c < '0' || c > '9') break;
		idx = idx * 10 + (c - '0');
		if (idx > 4095) {
			print_string("Invalid L2 index\n");
			return;
		}
		di++;
	}
	TBL_BUSY_WAIT();
	reg_read_m(RTL837x_TBL_DATA_0);
	REG_WRITE(RTL837x_TBL_DATA_0, sfr_data[0], sfr_data[1] & 0xfc, sfr_data[2] | (TBL_LUTREAD_NEXT_L2UC << 6), sfr_data[3]);
	REG_WRITE(RTL837X_TBL_CTRL, (idx >> 8) & 0xf, idx, TBL_L2_UNICAST, TBL_EXECUTE);
	TBL_BUSY_WAIT();
	reg_read_m(RTL837x_L2_DATA_OUT_B);
	if (!(sfr_data[0] & 0x20)) {
		print_string("L2 entry not found\n");
		return;
	}
	sfr_data[0] &= 0x3f;
	reg_write_m(RTL837x_TBL_DATA_IN_B);
	reg_read_m(RTL837x_L2_DATA_OUT_A);
	reg_write_m(RTL837x_TBL_DATA_IN_A);
	reg_read_m(RTL837x_L2_DATA_OUT_C);
	sfr_data[3] &= 0xc0;
	sfr_data[1] &= 0xfe;
	reg_write_m(RTL837x_TBL_DATA_IN_C);
	reg_read_m(RTL837x_TBL_DATA_0);
	REG_WRITE(RTL837x_TBL_DATA_0, sfr_data[0], sfr_data[1], TBL_L2_UNICAST, sfr_data[3]);
	REG_WRITE(RTL837X_TBL_CTRL, idx >> 8, idx, TBL_L2_UNICAST, TBL_WRITE | TBL_EXECUTE);
	TBL_BUSY_WAIT();
	print_string("L2 entry deleted\n");
}
