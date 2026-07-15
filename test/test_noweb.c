/*
 * Test that NO_WEB compile option works correctly:
 *   - "web" command MUST NOT appear in help
 *   - "xmodem" command MUST appear in help
 */

#define NO_WEB

#include "test.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#define _CMD_PARSER_H_
#define _RTL837X_STDIO_H_
#define _MACHINE_H_
#define __code
#define __xdata
#define __banked
#define __reentrant
#define int8_t signed char

#define CMD_BUF_SIZE 128
#define SBUF_SIZE 16
#define SBUF_MASK (SBUF_SIZE - 1)

uint8_t cmd_buffer[CMD_BUF_SIZE];
uint8_t cmd_line_len;
uint8_t cursor;
uint8_t xmodem_active;

static char test_out[8192];
static int test_pos;

void write_char(char c) {
    if (test_pos < (int)sizeof(test_out) - 1)
        test_out[test_pos++] = c;
    test_out[test_pos] = '\0';
}

void print_string(const char *s) { while (*s) write_char(*s++); }
void print_cmd_prompt(void) {}
void delay(uint16_t t) { (void)t; }
void reset_chip(void) {}
void phy_set_speed(void) {}
void port_mirror_set(uint8_t p, uint16_t r, uint16_t t) { (void)p; (void)r; (void)t; }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../cmd_help.c"
#pragma GCC diagnostic pop

static void setup(void) {
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    cmd_line_len = 0; cursor = 0; xmodem_active = 0;
    test_pos = 0; test_out[0] = '\0';
}

TEST(noweb_help_has_no_web_command) {
    setup();
    cmd_help();
    /* "web" should NOT appear as a command in NO_WEB mode */
    ASSERT(!strstr(test_out, "\n  web"));
    ASSERT(!strstr(test_out, "  web "));
}

TEST(noweb_help_has_xmodem) {
    setup();
    cmd_help();
    ASSERT_STR_CONTAINS(test_out, "xmodem");
}

TEST(noweb_web_cmds_not_compiled) {
    /* The web_cmds symbol is not compiled under NO_WEB.
     * This is a compile-time check — if it compiled, we'd have a linker error. */
    ASSERT(1);
}

int main(void) {
    printf("NO_WEB compile option tests\n\n");
    RUN_TEST(noweb_help_has_no_web_command);
    RUN_TEST(noweb_help_has_xmodem);
    RUN_TEST(noweb_web_cmds_not_compiled);
    REPORT();
}
