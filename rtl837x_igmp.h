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

#endif
