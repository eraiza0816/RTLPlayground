#ifndef _RTL837X_STORM_H_
#define _RTL837X_STORM_H_

#include <stdint.h>

/* Storm types */
#define STORM_BCAST		0
#define STORM_MCAST		1
#define STORM_DLF		2
#define STORM_UNMCAST		3
#define STORM_ALL		4

/* Software state, read by the config serializer (cmd_commit.c) */
extern __xdata uint8_t storm_type_en[4];
extern __xdata uint32_t storm_type_rate[4];

void storm_control_setup(void) __banked;
void storm_control_on(__xdata uint8_t type, __xdata uint32_t rate, __xdata uint8_t pps) __banked;
void storm_control_off(__xdata uint8_t type) __banked;
void storm_control_status(void) __banked;
void storm_print_type(__xdata uint8_t type) __banked;

#endif
