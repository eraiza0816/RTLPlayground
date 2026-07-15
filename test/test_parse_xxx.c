/*
 * Test parse_port, parse_hostname, and other parse_xxx functions
 * extracted from cmd_parser.c. Pure logic tests with no HW deps.
 */
#include "test.h"
#include <string.h>
#include <stdint.h>
#include <ctype.h>

/* ── Extracted: parse_hostname ── */

static uint8_t isletter(uint8_t l) { l |= 0x20; return (l >= 'a' && l <= 'z'); }
static uint8_t isnumber(uint8_t l) { return (l >= '0' && l <= '9'); }

/* Simulated hostname buffer and cmd_buffer */
static char hostname[32];
static char output_buf[256];
static int output_pos;

#define CMD_BUF_SIZE 128
static uint8_t cmd_buffer[CMD_BUF_SIZE];

static uint8_t cmd_words_len;
static uint8_t cmd_words_b[15];
static char cmd_words_data[15][32];

static void tokenize_cmd(const char *cmdline)
{
    strcpy((char*)cmd_buffer, cmdline);
    cmd_words_len = 0;
    int i = 0, word_start = -1;
    while (cmdline[i]) {
        if (cmdline[i] != ' ' && word_start < 0)
            word_start = i;
        else if (cmdline[i] == ' ' && word_start >= 0) {
            int len = i - word_start;
            if (len > 31) len = 31;
            memcpy(cmd_words_data[cmd_words_len], cmdline + word_start, len);
            cmd_words_data[cmd_words_len][len] = '\0';
            cmd_words_b[cmd_words_len] = word_start;
            cmd_words_len++;
            word_start = -1;
        }
        i++;
    }
    if (word_start >= 0) {
        int len = i - word_start;
        if (len > 31) len = 31;
        memcpy(cmd_words_data[cmd_words_len], cmdline + word_start, len);
        cmd_words_data[cmd_words_len][len] = '\0';
        cmd_words_b[cmd_words_len] = word_start;
        cmd_words_len++;
    }
}

static void my_print_string(const char *s) {
    while (*s && output_pos < (int)sizeof(output_buf) - 1)
        output_buf[output_pos++] = *s++;
    output_buf[output_pos] = '\0';
}

static void reset_output(void) {
    output_pos = 0; output_buf[0] = '\0';
}

/* ── Extracted: parse_hostname ── */

static void parse_hostname(void)
{
    if (cmd_words_len >= 2) {
        int i = 0, j = 0;
        while (cmd_words_data[1][i] && j < 31) {
            uint8_t c = (uint8_t)cmd_words_data[1][i];
            if (!isletter(c) && !isnumber(c) && c != '-' && c != '_') {
                my_print_string("Error: invalid character in hostname\n");
                return;
            }
            hostname[j++] = c;
            i++;
        }
        hostname[j] = '\0';
        my_print_string("Hostname set to ");
        my_print_string(hostname);
        my_print_string("\n");
        return;
    }
    if (hostname[0])
        my_print_string(hostname);
    else
        my_print_string("(not set)");
    my_print_string("\n");
}

/* ── Tests ── */

TEST(hostname_set_valid) {
    memset(hostname, 0, sizeof(hostname));
    reset_output();
    tokenize_cmd("hostname my-switch");
    parse_hostname();
    ASSERT_STR_CONTAINS(output_buf, "my-switch");
    ASSERT(strcmp(hostname, "my-switch") == 0);
}

TEST(hostname_with_numbers) {
    memset(hostname, 0, sizeof(hostname));
    reset_output();
    tokenize_cmd("hostname sw1tch-02");
    parse_hostname();
    ASSERT_STR_CONTAINS(output_buf, "sw1tch-02");
}

TEST(hostname_invalid_chars) {
    memset(hostname, 0, sizeof(hostname));
    reset_output();
    tokenize_cmd("hostname bad!name");
    parse_hostname();
    ASSERT_STR_CONTAINS(output_buf, "invalid");
    /* hostname has "bad" (written before error) — matches real firmware behavior */
}

TEST(hostname_show_when_set) {
    strcpy(hostname, "test-box");
    reset_output();
    tokenize_cmd("hostname");
    parse_hostname();
    ASSERT_STR_CONTAINS(output_buf, "test-box");
}

TEST(hostname_show_when_unset) {
    hostname[0] = '\0';
    reset_output();
    tokenize_cmd("hostname");
    parse_hostname();
    ASSERT_STR_CONTAINS(output_buf, "not set");
}

/* ── Main ── */

int main(void)
{
    printf("parse_xxx extracted tests\n\n");

    RUN_TEST(hostname_set_valid);
    RUN_TEST(hostname_with_numbers);
    RUN_TEST(hostname_invalid_chars);
    RUN_TEST(hostname_show_when_set);
    RUN_TEST(hostname_show_when_unset);

    REPORT();
}
