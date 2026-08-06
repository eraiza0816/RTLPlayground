#ifndef _LLDP_H_
#define _LLDP_H_

#include <stdint.h>

#define LLDP_MAX_NEIGHBORS 8

/* LLDP (IEEE 802.1AB) neighbor discovery.
 *
 * All functions are __banked: lldp.c lives in BANK3 while the callers
 * are in the home area (rtlplayground.c) and BANK2 (cmd_parser.c).
 * Omitting __banked causes a plain lcall that does not switch PSBANK
 * and crashes at boot (see 新機能追加検証/README.md). */

void lldp_setup(void) __banked;    // Called from main() at boot: RMA trap + state init
void lldp_timers(void) __banked;   // Called from idle(): 1s TTL aging + 30s LLDPDU send
uint8_t lldp_rx(void) __banked;    // Called from handle_rx() else-branch; 1 if LLDPDU consumed
void lldp_show(void) __banked;     // Print the neighbor table
uint8_t lldp_neighbor_get(__xdata uint8_t n) __banked;  // Copy slot n into lldp_json; 1 if in use

extern __xdata uint8_t lldp_enabled;

/* Neighbor slot view for the HTTP /lldp.json endpoint */
struct lldp_json_view {
	uint8_t port;
	uint8_t chassis[6];
	char port_id[16];
	char sysname[16];
	uint8_t ttl;
};
extern __xdata struct lldp_json_view lldp_json;

#endif
