#ifndef _RTL837X_IGMP_H_
#define _RTL837X_IGMP_H_

#include <stdint.h>

void igmp_setup(void) __banked;
void igmp_enable(void) __banked;
void igmp_router_port_set(uint16_t pmask) __banked;
void igmp_packet_handler(void) __banked;
void igmp_show(void) __banked;
void igmp_querier_on(void) __banked;
void igmp_querier_off(void) __banked;
void igmp_querier_show(void) __banked;
void igmp_mld_on(void) __banked;
void igmp_mld_off(void) __banked;
void igmp_mld_show(void) __banked;
void igmp_json_state(void) __banked;
uint16_t igmp_json_group_next(__xdata uint16_t idx) __banked;

/* Status scratch for the HTTP /igmp.json endpoint */
extern __xdata uint8_t igmp_json_mld_en;
extern __xdata uint8_t igmp_json_ops[9];
extern __xdata uint16_t igmp_json_gmask;
extern __xdata uint8_t igmp_json_querier;

#endif
