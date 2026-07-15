#include "test.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#define _CMD_PARSER_H_
#define _RTL837X_STDIO_H_
#define _MACHINE_H_
#define NO_WEB

#define __code
#define __xdata
#define __banked
#define __reentrant

#define CMD_BUF_SIZE 128
#define SBUF_SIZE 16
#define SBUF_MASK (SBUF_SIZE - 1)
#define CMD_HISTORY_SIZE 0x400
#define CMD_HISTORY_MASK (CMD_HISTORY_SIZE - 1)

uint8_t cmd_buffer[CMD_BUF_SIZE];
uint8_t cmd_available;
uint8_t cmd_history[CMD_HISTORY_SIZE];
uint16_t cmd_history_ptr;
volatile uint8_t sbuf_ptr;
uint8_t sbuf[SBUF_SIZE];
uint8_t xmodem_active;

static char test_out[4096];
static int test_pos;

void write_char(char c) {
    if (test_pos < (int)sizeof(test_out) - 1)
        test_out[test_pos++] = c;
    test_out[test_pos] = '\0';
}
void print_string(const char *s) { while (*s) write_char(*s++); }
void print_string_x(char *s) { print_string(s); }
void print_cmd_prompt(void) {}
void cmd_complete(void) {}
void cmd_help(void) {}
void itoa(uint8_t v) {
    char buf[8]; snprintf(buf, sizeof(buf), "%u", v); for (char *p = buf; *p; p++) write_char(*p);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../cmd_editor.c"
#pragma GCC diagnostic pop

/* ── Helpers ── */

static void setup(void)
{
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    memset(sbuf, 0, SBUF_SIZE);
    memset(cmd_history, 0, CMD_HISTORY_SIZE);
    sbuf_ptr = 0;
    cmd_available = 0;
    cmd_history_ptr = 0;
    xmodem_active = 0;
    test_pos = 0;
    test_out[0] = '\0';
    cmd_editor_init();
}

static void send_byte(uint8_t b) {
    sbuf[sbuf_ptr] = b;
    sbuf_ptr = (sbuf_ptr + 1) & SBUF_MASK;
}

static void send_keys(const char *s) {
    for (const char *p = s; *p; p++) send_byte(*p);
}

static void send_esc_seq(const char *seq) {
    send_byte(0x1B); send_byte('[');
    for (const char *p = seq; *p; p++) send_byte(*p);
}

/* ── Basic input ── */

TEST(types_simple_word) {
    setup(); send_keys("show"); cmd_edit();
    ASSERT_EQ(cmd_line_len, 4);
    ASSERT_EQ(memcmp(cmd_buffer, "show", 4), 0);
}

TEST(enter_executes) {
    setup(); send_keys("show"); cmd_edit(); send_byte('\r'); cmd_edit();
    ASSERT(cmd_available != 0);
    ASSERT_EQ(cmd_line_len, 0);
}

TEST(empty_enter_noop) {
    setup(); send_byte('\r'); cmd_edit();
    ASSERT_EQ(cmd_available, 0);
}

TEST(backspace_deletes) {
    setup(); send_keys("show"); cmd_edit();
    send_byte(127); cmd_edit();
    ASSERT_EQ(cmd_line_len, 3);
    ASSERT_EQ(memcmp(cmd_buffer, "sho", 3), 0);
}

/* ── Cursor movement (ESC sequences) ── */

TEST(cursor_left_moves_back) {
    setup(); send_keys("abc"); cmd_edit();
    ASSERT_EQ(cursor, 3);
    send_esc_seq("D"); cmd_edit();  /* cursor left */
    ASSERT_EQ(cursor, 2);
    send_esc_seq("D"); cmd_edit();
    ASSERT_EQ(cursor, 1);
}

TEST(cursor_left_at_start_stays) {
    setup(); send_keys("abc"); cmd_edit(); cursor = 0;
    send_esc_seq("D"); cmd_edit();
    ASSERT_EQ(cursor, 0);
}

TEST(cursor_right_moves_forward) {
    setup(); send_keys("abc"); cmd_edit(); cursor = 0;
    send_esc_seq("C"); cmd_edit();
    ASSERT_EQ(cursor, 1);
    send_esc_seq("C"); cmd_edit();
    ASSERT_EQ(cursor, 2);
}

TEST(cursor_right_at_end_stays) {
    setup(); send_keys("abc"); cmd_edit();
    ASSERT_EQ(cursor, 3);
    send_esc_seq("C"); cmd_edit();
    ASSERT_EQ(cursor, 3);
}

TEST(insert_in_middle) {
    setup(); send_keys("ac"); cmd_edit(); cursor = 1;
    send_keys("b"); cmd_edit();
    ASSERT_EQ(cmd_line_len, 3);
    ASSERT_EQ(cmd_buffer[0], 'a');
    ASSERT_EQ(cmd_buffer[1], 'b');
    ASSERT_EQ(cmd_buffer[2], 'c');
}

TEST(delete_key_removes_at_cursor) {
    setup(); send_keys("abcd"); cmd_edit(); cursor = 1;
    send_esc_seq("3~"); cmd_edit();  /* DEL key */
    ASSERT_EQ(cmd_line_len, 3);
    ASSERT_EQ(cmd_buffer[0], 'a');
    ASSERT_EQ(cmd_buffer[1], 'c');
    ASSERT_EQ(cmd_buffer[2], 'd');
}

TEST(delete_at_end_does_nothing) {
    setup(); send_keys("ab"); cmd_edit();
    ASSERT_EQ(cursor, 2);
    send_esc_seq("3~"); cmd_edit();
    ASSERT_EQ(cmd_line_len, 2);
}

/* ── History (UP/DOWN arrows) ── */

/*
 * Populate history buffer in the format cmd_parser.c uses:
 *   1. cmd_history_ptr is advanced past the stored data
 *   2. Data is stored from (ptr - len) to (ptr - 1), then '\n' at ptr
 *   3. cmd_history_ptr points to '\n'
 */
static void add_history(const char *cmd)
{
    /* Match cmd_parser.c's history format:
     * i = strlen (position past last char, == null position)
     * store '\n' at cmd_history_ptr, advance ptr
     * then store chars from i-1 down to 0 */
    uint8_t i = strlen(cmd);
    cmd_history_ptr = (cmd_history_ptr + i) & CMD_HISTORY_MASK;
    uint16_t p = cmd_history_ptr;
    cmd_history[cmd_history_ptr] = '\n';
    cmd_history_ptr = (cmd_history_ptr + 1) & CMD_HISTORY_MASK;
    do {
        i--;
        p = (p - 1) & CMD_HISTORY_MASK;
        cmd_history[p] = cmd[i];
    } while (i);
}

TEST(history_add_works) {
    memset(cmd_history, 0, sizeof(cmd_history));
    cmd_history_ptr = 0;
    add_history("port 1");
    ASSERT(cmd_history_ptr > 0);
    ASSERT(cmd_history[0] == 'p');
}

TEST(history_empty_does_nothing) {
    setup();
    send_esc_seq("A"); cmd_edit();
    ASSERT_EQ(cmd_line_len, 0);
}

/* ── Buffer overflow ── */

TEST(max_line_length) {
    setup();
    char buf[130];
    memset(buf, 'a', 128); buf[128] = '\0';
    send_keys(buf); cmd_edit();
    ASSERT(cmd_line_len < CMD_BUF_SIZE);
}

int main(void)
{
    printf("cmd_editor tests\n\n");

    RUN_TEST(types_simple_word);
    RUN_TEST(enter_executes);
    RUN_TEST(empty_enter_noop);
    RUN_TEST(backspace_deletes);

    RUN_TEST(cursor_left_moves_back);
    RUN_TEST(cursor_left_at_start_stays);
    RUN_TEST(cursor_right_moves_forward);
    RUN_TEST(cursor_right_at_end_stays);
    RUN_TEST(insert_in_middle);
    RUN_TEST(delete_key_removes_at_cursor);
    RUN_TEST(delete_at_end_does_nothing);

    RUN_TEST(history_add_works);
    RUN_TEST(history_empty_does_nothing);

    RUN_TEST(max_line_length);

    REPORT();
}
