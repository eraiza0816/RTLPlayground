#ifndef _PING_H_
#define _PING_H_

#include <stdint.h>

/* The destination IP is taken from the global ip[4] (a.b.c.d order)
 * filled in by parse_ip() in cmd_parser.c.  This keeps the function
 * parameter-free: the 8051 internal RAM overlay is full in this
 * firmware, so new functions must not use parameters or locals.
 *
 * __banked is REQUIRED: ping.c lives in BANK2 while the callers are in
 * the home area (rtlplayground.c idle/handle_rx) and BANK2
 * (cmd_parser.c).  Without it the compiler emits a plain lcall that
 * does not switch PSBANK, jumping into the wrong bank (= garbage code)
 * and crashing at boot. */
void ping_start(void) __banked;    // Start pinging the IP in ip[4]
void ping_pump(void) __banked;     // Called from idle() each tick
uint8_t ping_rx(void) __banked;    // Called from handle_rx(); returns 1 if ECHO REPLY consumed
void ping_abort(void) __banked;    // Stop any ping in progress

/* Live ping state, read by the HTTP /ping.json endpoint (page_impl.c).
 * state: 0 = idle, 1 = running; dst in a.b.c.d order. */
extern __xdata uint8_t ping_state;
extern __xdata uint8_t ping_dst[4];
extern __xdata uint8_t ping_sent;
extern __xdata uint8_t ping_rcvd;
extern __xdata uint16_t ping_min_rtt;
extern __xdata uint16_t ping_max_rtt;
extern __xdata uint16_t ping_sum_rtt;

#endif
