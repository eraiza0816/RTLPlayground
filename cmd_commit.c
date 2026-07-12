#pragma codeseg BANK3
#pragma constseg BANK3

#include "rtl837x_common.h"
#include "rtl837x_flash.h"
#include "rtl837x_regs.h"
#include "rtl837x_sfr.h"
#include "uip/uip.h"
#include "syslog.h"

extern __xdata uint8_t flash_buf[FLASH_BUF_SIZE];
extern __xdata struct flash_region_t flash_region;
extern __xdata char hostname[32];
extern __xdata char passwd[21];
extern __xdata uip_ipaddr_t uip_hostaddr, uip_draddr, uip_netmask;
extern __xdata uint8_t stpEnabled;
extern __xdata uint16_t management_vlan;
extern __xdata struct syslog_state syslog_state;
extern __xdata uint8_t telnet_enabled;
extern __xdata uint8_t web_enabled;
extern __xdata uint8_t sfr_data[4];
extern __xdata uint8_t cmd_buffer[128];
extern __xdata uint8_t cmd_words_b[15];

extern void reg_read_m(uint16_t reg_addr);
extern void reg_write_m(uint16_t reg_addr);

static __xdata uint8_t commit_pos;

#define COMMIT_PUTC(c) do { \
	if (commit_pos < FLASH_BUF_SIZE) \
		flash_buf[commit_pos++] = (c); \
} while(0)

#define COMMIT_PUTS(s) do { \
	__code char *__p = (s); \
	while (*__p) { \
		if (commit_pos >= FLASH_BUF_SIZE) break; \
		flash_buf[commit_pos++] = *__p++; \
	} \
} while(0)

#define COMMIT_PUTSX(s) do { \
	__xdata char *__p = (s); \
	while (*__p) { \
		if (commit_pos >= FLASH_BUF_SIZE) break; \
		flash_buf[commit_pos++] = *__p++; \
	} \
} while(0)

#define COMMIT_BYTE(v) do { \
	uint8_t __v = (v); \
	if (__v >= 100) { COMMIT_PUTC('0' + (__v / 100)); } \
	if (__v >= 10) { COMMIT_PUTC('0' + ((__v / 10) % 10)); } \
	COMMIT_PUTC('0' + (__v % 10)); \
} while(0)

#define COMMIT_IP(ip) do { \
	COMMIT_BYTE((ip)[0] & 0xFF); COMMIT_PUTC('.'); \
	COMMIT_BYTE((ip)[0] >> 8); COMMIT_PUTC('.'); \
	COMMIT_BYTE((ip)[1] & 0xFF); COMMIT_PUTC('.'); \
	COMMIT_BYTE((ip)[1] >> 8); COMMIT_PUTC('\n'); \
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
		flash_write_bytes(flash_buf + written);
		written += chunk;
	}
	flash_buf[0] = 0;
	flash_region.addr = CONFIG_START + commit_pos;
	flash_region.len = 1;
	flash_write_bytes(flash_buf);
}

void parse_commit(void) __banked
{
	commit_pos = 0;
	flash_region.addr = CONFIG_START;
	flash_sector_erase();

	if (hostname[0]) {
		COMMIT_PUTS("hostname "); COMMIT_PUTSX(hostname); COMMIT_PUTC('\n');
	}

	COMMIT_PUTS("ip "); COMMIT_IP(uip_hostaddr);
	COMMIT_PUTS("gw "); COMMIT_IP(uip_draddr);
	COMMIT_PUTS("netmask "); COMMIT_IP(uip_netmask);

	if (passwd[0]) {
		COMMIT_PUTS("passwd "); COMMIT_PUTSX(passwd); COMMIT_PUTC('\n');
	}

	if (stpEnabled)
		COMMIT_PUTS("stp on\n");
	else
		COMMIT_PUTS("stp off\n");

	if (management_vlan >= 2) {
		COMMIT_PUTS("vlan ");
		COMMIT_BYTE(management_vlan / 100);
		COMMIT_BYTE((management_vlan / 10) % 10);
		COMMIT_BYTE(management_vlan % 10);
		COMMIT_PUTS(" mgmt\n");
	}

	COMMIT_PUTS("syslog ");
	if (syslog_state.enabled) {
		COMMIT_PUTS("on\nsyslog ip ");
		COMMIT_BYTE(syslog_state.server_ip[0]); COMMIT_PUTC('.');
		COMMIT_BYTE(syslog_state.server_ip[1]); COMMIT_PUTC('.');
		COMMIT_BYTE(syslog_state.server_ip[2]); COMMIT_PUTC('.');
		COMMIT_BYTE(syslog_state.server_ip[3]); COMMIT_PUTC('\n');
	} else {
		COMMIT_PUTS("off\n");
	}

	COMMIT_PUTS("telnet ");
	if (telnet_enabled) COMMIT_PUTS("on\n"); else COMMIT_PUTS("off\n");
	COMMIT_PUTS("web ");
	if (web_enabled) COMMIT_PUTS("on\n"); else COMMIT_PUTS("off\n");

	commit_write_flash();
	print_string("Config committed\n");
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
	do { reg_read_m(RTL837X_TBL_CTRL); } while (sfr_data[3] & TBL_EXECUTE);
	reg_read_m(RTL837x_TBL_DATA_0);
	REG_WRITE(RTL837x_TBL_DATA_0, sfr_data[0], sfr_data[1] & 0xfc, sfr_data[2] | (TBL_LUTREAD_NEXT_L2UC << 6), sfr_data[3]);
	REG_WRITE(RTL837X_TBL_CTRL, (idx >> 8) & 0xf, idx, TBL_L2_UNICAST, TBL_EXECUTE);
	do { reg_read_m(RTL837X_TBL_CTRL); } while (sfr_data[3] & TBL_EXECUTE);
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
	do { reg_read_m(RTL837X_TBL_CTRL); } while (sfr_data[3] & TBL_EXECUTE);
	print_string("L2 entry deleted\n");
}
