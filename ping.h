#ifndef _PING_H_
#define _PING_H_

#include <stdint.h>

/* The destination IP is taken from the global ip[4] (a.b.c.d order)
 * filled in by parse_ip() in cmd_parser.c.  This keeps the function
 * parameter-free: the 8051 internal RAM overlay is full in this
 * firmware, so new functions must not use parameters or locals. */
void ping_start(void);                     // Start pinging the IP in ip[4]
void ping_pump(void);                      // Called from idle() each tick
uint8_t ping_rx(void);                     // Called from handle_rx(); returns 1 if ECHO REPLY consumed
void ping_abort(void);                     // Stop any ping in progress

#endif
