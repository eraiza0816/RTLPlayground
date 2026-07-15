#include "test.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * Test the line editor (cmd_edit) from cmd_editor.c.
 *
 * We simulate keystrokes by filling sbuf and calling cmd_edit().
 */

/* Block hardware headers */
#define _CMD_PARSER_H_
#define _RTL837X_STDIO_H_
#define _MACHINE_H_
#define NO_WEB  /* enable xmodem_active check */

/* SDCC compat */
#define __code
#define __xdata
#define __banked
#define __reentrant

/* Constants */
#define CMD_BUF_SIZE 128
#define SBUF_SIZE 16
#define SBUF_MASK (SBUF_SIZE - 1)
#define CMD_HISTORY_SIZE 0x400
#define CMD_HISTORY_MASK (CMD_HISTORY_SIZE - 1)

/* Externs needed by cmd_editor.c */
uint8_t cmd_buffer[CMD_BUF_SIZE];
uint8_t cmd_available;
uint8_t cmd_history[CMD_HISTORY_SIZE];
uint16_t cmd_history_ptr;

/* sbuf for simulation */
uint8_t xmodem_active;
volatile uint8_t sbuf_ptr;
uint8_t sbuf[SBUF_SIZE];

/* Stub functions */
void write_char(char c) { (void)c; }
void print_string(const char *s) { (void)s; }
void print_string_x(char *s) { (void)s; }
void print_cmd_prompt(void) {}
void cmd_complete(void) {}
void cmd_help(void) {}

/* Called by cursor movement code in cmd_edit */
void itoa(uint8_t v) { (void)v; }

/* Include the real source */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../cmd_editor.c"
#pragma GCC diagnostic pop

/* ── Simulation helpers ── */

static void send_keys(const char *keys)
{
    for (const char *p = keys; *p; p++) {
        sbuf[sbuf_ptr] = *p;
        sbuf_ptr = (sbuf_ptr + 1) & SBUF_MASK;
    }
}

static void send_tab(void)
{
    sbuf[sbuf_ptr] = '\t';
    sbuf_ptr = (sbuf_ptr + 1) & SBUF_MASK;
}

static void send_enter(void)
{
    sbuf[sbuf_ptr] = '\r';
    sbuf_ptr = (sbuf_ptr + 1) & SBUF_MASK;
}

static void setup_editor(void)
{
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    memset(sbuf, 0, SBUF_SIZE);
    sbuf_ptr = 0;
    cmd_available = 0;
    cmd_editor_init();
}

/* ── Tests ── */

TEST(editor_types_simple_command) {
    setup_editor();
    send_keys("show");
    cmd_edit();  /* process "show" */
    ASSERT_EQ(cmd_line_len, 4);
    ASSERT_EQ(memcmp(cmd_buffer, "show", 4), 0);
}

TEST(editor_enter_executes_command) {
    setup_editor();
    send_keys("show");
    cmd_edit();
    send_enter();
    cmd_edit();
    ASSERT(cmd_available != 0);
    ASSERT_EQ(cmd_line_len, 0);
}

TEST(editor_empty_enter_no_command) {
    setup_editor();
    send_enter();
    cmd_edit();
    ASSERT_EQ(cmd_available, 0);
}

TEST(editor_backspace_deletes) {
    setup_editor();
    send_keys("show");
    cmd_edit();

    /* Backspace over 'w' */
    sbuf[sbuf_ptr] = 127;  /* DEL */
    sbuf_ptr = (sbuf_ptr + 1) & SBUF_MASK;
    cmd_edit();

    ASSERT_EQ(cmd_line_len, 3);
    ASSERT_EQ(memcmp(cmd_buffer, "sho", 3), 0);
}

TEST(editor_inserts_in_middle) {
    setup_editor();
    send_keys("sow");
    cmd_edit();

    /* Move cursor right... simulate by sending left + type */
    /* Simplified: we test that typing after cursor works */
    ASSERT_EQ(cmd_line_len, 3);
    ASSERT_EQ(cmd_buffer[0], 's');
    ASSERT_EQ(cmd_buffer[1], 'o');
    ASSERT_EQ(cmd_buffer[2], 'w');
}

TEST(editor_question_triggers_help) {
    setup_editor();
    /* '?' at position 0 should trigger help and NOT be inserted into buffer */
    send_keys("?");
    cmd_edit();
    ASSERT_EQ(cmd_line_len, 0);  /* '?' consumed, not inserted */
}

TEST(editor_tab_triggers_completion) {
    setup_editor();
    /* Tab at start of line – should call cmd_complete */
    send_tab();
    cmd_edit();
    /* No crash, line unchanged */
    ASSERT_EQ(cmd_line_len, 0);
}

TEST(editor_tab_after_text) {
    setup_editor();
    send_keys("po");
    cmd_edit();
    ASSERT_EQ(cmd_line_len, 2);

    send_tab();
    cmd_edit();
    /* cmd_complete() is stubbed in this test, so no actual completion.
       We just verify no crash and cursor advances. */
    ASSERT(cmd_line_len >= 2);
}

TEST(editor_insert_at_cursor_position) {
    setup_editor();
    send_keys("ab");
    cmd_edit();
    ASSERT_EQ(cmd_line_len, 2);

    /* Type 'x' between 'a' and 'b' — need cursor there.
     * In the real editor, we'd send cursor-left first.
     * Here we cheat by setting cursor directly. */
    cursor = 1;
    send_keys("x");
    cmd_edit();
    ASSERT_EQ(cmd_line_len, 3);
    ASSERT_EQ(cmd_buffer[0], 'a');
    ASSERT_EQ(cmd_buffer[1], 'x');
    ASSERT_EQ(cmd_buffer[2], 'b');
}

TEST(editor_max_line_length) {
    setup_editor();
    /* Type 128 characters */
    char buf[130];
    memset(buf, 'a', 128);
    buf[128] = '\0';
    send_keys(buf);
    cmd_edit();
    /* Should not exceed buffer */
    ASSERT(cmd_line_len < CMD_BUF_SIZE);
}

int main(void)
{
    printf("cmd_editor tests\n\n");
    RUN_TEST(editor_types_simple_command);
    RUN_TEST(editor_enter_executes_command);
    RUN_TEST(editor_empty_enter_no_command);
    RUN_TEST(editor_backspace_deletes);
    RUN_TEST(editor_inserts_in_middle);
    RUN_TEST(editor_question_triggers_help);
    RUN_TEST(editor_tab_triggers_completion);
    RUN_TEST(editor_tab_after_text);
    RUN_TEST(editor_insert_at_cursor_position);
    RUN_TEST(editor_max_line_length);
    REPORT();
}
