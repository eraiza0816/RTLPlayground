#include "test.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * Test the Tab completion and ? help features from cmd_help.c.
 *
 * We include the real source with minimal scaffolding.
 */

/* Block hardware headers */
#define _CMD_PARSER_H_
#define _RTL837X_STDIO_H_
#define _MACHINE_H_

/* SDCC compat */
#define __code
#define __xdata
#define __banked
#define __reentrant
#define int8_t signed char

/* Constants needed by cmd_help.c */
#define CMD_BUF_SIZE 128
#define SBUF_SIZE 16
#define SBUF_MASK (SBUF_SIZE - 1)

/* Extern variables used by cmd_help.c */
uint8_t cmd_buffer[CMD_BUF_SIZE];
uint8_t cmd_line_len;
uint8_t cursor;
uint8_t xmodem_active;

/* Redirect serial output to buffer for assertions */
static char test_out[8192];
static int test_pos;
static int prompt_printed;

void write_char(char c)
{
    if (test_pos < (int)sizeof(test_out) - 1)
        test_out[test_pos++] = c;
    test_out[test_pos] = '\0';
    if (c == '>') prompt_printed = 1;
}

void print_string(const char *s)
{
    while (*s) write_char(*s++);
}

void print_cmd_prompt(void)
{
    prompt_printed = 1;
    fputs("[prompt] ", stderr);
}

/* Stub for some hardware functions (may be referenced but not called in tests) */
void delay(uint16_t t) { (void)t; }
void reset_chip(void) {}
void flash_sector_erase(void) {}
void flash_write_bytes(uint8_t *p) { (void)p; }
void phy_set_speed(void) {}
void port_mirror_set(uint8_t p, uint16_t r, uint16_t t) { (void)p; (void)r; (void)t; }

/* Include the real source */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../cmd_help.c"
#pragma GCC diagnostic pop

/* ── Setup / teardown ── */

static void setup(void)
{
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    cmd_line_len = 0;
    cursor = 0;
    xmodem_active = 0;
    test_pos = 0;
    test_out[0] = '\0';
    prompt_printed = 0;
}

/* ── Tests for command table integrity ── */

TEST(table_top_level_entries_have_names) {
    for (const struct cmd_group __code *g = top_cmds; g->name; g++)
        ASSERT(g->name[0] != '\0');
}

TEST(table_top_level_entries_have_descs) {
    for (const struct cmd_group __code *g = top_cmds; g->name; g++)
        ASSERT(g->desc[0] != '\0');
}

TEST(table_sfp_cmds_have_names) {
    for (const struct cmd_entry __code *e = sfp_cmds; e->name; e++)
        ASSERT(e->name[0] != '\0');
}

/* ── Tests for cmd_complete (Tab) ── */

TEST(complete_partial_first_word) {
    setup();
    memcpy(cmd_buffer, "po", 3);
    cmd_line_len = 2;
    cursor = 2;
    cmd_complete();
    ASSERT_EQ(cmd_line_len, 4);   /* "port" (no trailing space from completion) */
    ASSERT_EQ(cursor, 4);          /* after "port" */
    ASSERT_EQ(memcmp(cmd_buffer, "port", 4), 0);
}

TEST(complete_full_first_word_noop) {
    setup();
    memcpy(cmd_buffer, "reset", 6);
    cmd_line_len = 5;
    cursor = 5;
    cmd_complete();
    ASSERT_EQ(cmd_line_len, 5);
    ASSERT_EQ(memcmp(cmd_buffer, "reset", 5), 0);
}

TEST(complete_no_match_does_nothing) {
    setup();
    memcpy(cmd_buffer, "zzz", 4);
    cmd_line_len = 3;
    cursor = 3;
    cmd_complete();
    ASSERT_EQ(cmd_line_len, 3);
}

TEST(complete_multiple_matches_prints_list) {
    setup();
    cmd_buffer[0] = 's';
    cmd_line_len = 1;
    cursor = 1;
    prompt_printed = 0;
    cmd_complete();
    /* should print matching commands: sds, sfp, stat, stp, … */
    ASSERT_STR_CONTAINS(test_out, "sds");
    ASSERT_STR_CONTAINS(test_out, "sfp");
    ASSERT_STR_CONTAINS(test_out, "stat");
}

TEST(complete_subcommand_port) {
    setup();
    memcpy(cmd_buffer, "port sh", 8);
    cmd_line_len = 7;
    cursor = 7;
    cmd_complete();
    /* "sh" should complete to "show" */
    ASSERT_EQ(cmd_line_len, 9);    /* "port show\0" */
    ASSERT_EQ(memcmp(cmd_buffer, "port show", 9), 0);
}

TEST(complete_subcommand_sfp) {
    setup();
    memcpy(cmd_buffer, "sfp des", 8);
    cmd_line_len = 7;
    cursor = 7;
    cmd_complete();
    ASSERT_EQ(memcmp(cmd_buffer, "sfp describe", 12), 0);
}

TEST(complete_subcommand_empty_shows_all) {
    setup();
    memcpy(cmd_buffer, "eee ", 5);
    cmd_line_len = 4;
    cursor = 4;
    cmd_complete();
    ASSERT_STR_CONTAINS(test_out, "on");
    ASSERT_STR_CONTAINS(test_out, "off");
    ASSERT_STR_CONTAINS(test_out, "status");
}

/* ── Tests for cmd_help (?) ── */

TEST(help_at_start_shows_all_commands) {
    setup();
    cmd_help();
    ASSERT_STR_CONTAINS(test_out, "reset");
    ASSERT_STR_CONTAINS(test_out, "sfp");
    ASSERT_STR_CONTAINS(test_out, "port");
    ASSERT_STR_CONTAINS(test_out, "vlan");
    ASSERT_STR_CONTAINS(test_out, "commit");
}

TEST(help_subcommand_port) {
    setup();
    memcpy(cmd_buffer, "port ", 5);
    cmd_line_len = 5;
    cursor = 5;
    cmd_help();
    ASSERT_STR_CONTAINS(test_out, "show");
    ASSERT_STR_CONTAINS(test_out, "speed");
}

TEST(help_subcommand_unknown) {
    setup();
    memcpy(cmd_buffer, "zzz ", 4);
    cmd_line_len = 4;
    cursor = 4;
    cmd_help();
    ASSERT_STR_CONTAINS(test_out, "unknown");
}

TEST(help_subcommand_no_subcommands) {
    setup();
    memcpy(cmd_buffer, "reset ", 6);
    cmd_line_len = 6;
    cursor = 6;
    cmd_help();
    ASSERT_STR_CONTAINS(test_out, "no sub-commands");
}

/* ── Main ── */

int main(void)
{
    printf("cmd_help / cmd_complete tests\n\n");

    RUN_TEST(table_top_level_entries_have_names);
    RUN_TEST(table_top_level_entries_have_descs);
    RUN_TEST(table_sfp_cmds_have_names);

    RUN_TEST(complete_partial_first_word);
    RUN_TEST(complete_full_first_word_noop);
    RUN_TEST(complete_no_match_does_nothing);
    RUN_TEST(complete_multiple_matches_prints_list);
    RUN_TEST(complete_subcommand_port);
    RUN_TEST(complete_subcommand_sfp);
    RUN_TEST(complete_subcommand_empty_shows_all);

    RUN_TEST(help_at_start_shows_all_commands);
    RUN_TEST(help_subcommand_port);
    RUN_TEST(help_subcommand_unknown);
    RUN_TEST(help_subcommand_no_subcommands);

    REPORT();
}
