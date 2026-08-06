#ifndef _RTL837X_QOS_H_
#define _RTL837X_QOS_H_

#include <stdint.h>

/* QoS modes */
#define QOS_MODE_OFF		0
#define QOS_MODE_PCP		1
#define QOS_MODE_DSCP		2
#define QOS_MODE_BOTH		3

/* Software state, read by the config serializer (cmd_commit.c) */
extern __xdata uint8_t qos_mode;
extern __xdata uint8_t qos_pcp_map[8];
extern __xdata uint8_t qos_dscp_map[64];

void qos_setup(void) __banked;
void qos_on(void) __banked;
void qos_off(void) __banked;
void qos_mode_set(__xdata uint8_t mode) __banked;
void qos_pcp_set(__xdata uint8_t pri, __xdata uint8_t queue) __banked;
void qos_dscp_set(__xdata uint8_t dscp, __xdata uint8_t queue) __banked;
void qos_sched_set(__xdata uint8_t port, __xdata uint8_t wfq, __xdata uint8_t weight) __banked;
void qos_status(void) __banked;

#endif
