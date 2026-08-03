#ifndef _CMD_PARSER_H_
#define _CMD_PARSER_H_

#include <stdint.h>

#include "rtl837x_common.h"

extern __xdata uint8_t cmd_buffer[CMD_BUF_SIZE];
extern volatile __xdata uint8_t cmd_available;
extern __xdata uint8_t err_status;

// Shared management password (max 20 chars, NUL-terminated).
// Owner: cmd_parser.c (modified by the `passwd` CLI command, parse_passwd).
// Readers: httpd.c (WebUI login), telnetd.c (telnet login).
// An empty password disables authentication (see parse_telnet in cmd_parser.c).
extern __xdata char passwd[21];

/* Implemented in hand-written assembly (rtlplayground_util.asm) */
uint8_t atoi_byte(__xdata uint8_t *out, uint8_t idx);
uint8_t atoi_short(__xdata uint16_t *vlan, uint8_t idx);

void cmd_tokenize(void) __banked;
void cmd_parser(void) __banked;
void execute_config(void) __banked __reentrant;
void execute_commands(__xdata uint8_t *p) __banked;
void print_sw_version(void) __banked;
void clear_command_history(void) __banked;

void cmd_complete(void) __banked;
void cmd_help(void) __banked;

/* CLI mode definitions (used by cmd_mode.c and cmd_help.c) */
#define MODE_EXEC       0
#define MODE_PRIVILEGED 1
#define MODE_CONFIG     2
#define MODE_CONFIG_IF  3
#define MODE_CONFIG_VLAN 4

extern __xdata uint8_t cli_mode;

#endif
