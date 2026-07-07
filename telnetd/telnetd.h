#ifndef __TELNETD_H__
#define __TELNETD_H__

#include <stdint.h>

#define IAC     0xFF
#define WILL    0xFB
#define WONT    0xFC
#define DO      0xFD
#define DONT    0xFE
#define SGA     0x03

void telnetd_init(void) __banked;
void telnetd_appcall(void) __banked;
extern __xdata uint8_t telnet_connected;
extern __xdata uint8_t telnet_echo;
void telnet_tx_enqueue(char c) __banked;

#endif
