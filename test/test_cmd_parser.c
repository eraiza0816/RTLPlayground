#include "test.h"
#include <string.h>
#include <stdint.h>

/*
 * Test the core parser functions by including the relevant parts
 * of the source. We provide only the minimal scaffolding needed.
 */

/* SDCC-compat macros that the code expects */
#define __code
#define __xdata
#define __banked
#define __reentrant

/* Data structures and globals used by the code under test */
#define CMD_BUF_SIZE 128
#define N_WORDS 15
#define CMD_HISTORY_SIZE 0x400
#define CMD_HISTORY_MASK (CMD_HISTORY_SIZE - 1)

#define ERR_OK              0
#define ERR_TOO_MANY_ARGUMENTS  1
#define ERR_CMD_TOO_LONG    2

uint8_t cmd_buffer[CMD_BUF_SIZE];
uint8_t cmd_available;
uint8_t cmd_words_len;
uint8_t cmd_words_b[N_WORDS];
uint8_t cmd_history[CMD_HISTORY_SIZE];
uint16_t cmd_history_ptr;
uint8_t err_status;
char save_cmd;

/* Stub functions called by the extracted code */
void print_string(const char *s) { (void)s; }
void write_char(char c) { (void)c; }
void print_string_x(char *s) { (void)s; }

/* Include the functions we want to test — extracted into a fragment */
/* isletter, isnumber, cmd_compare, cmd_tokenize are self-contained */

/* ── isletter ── */
uint8_t isletter(uint8_t l) {
    l |= 0x20;
    l -= 'a';
    return (l <= ('z'-'a'));
}

/* ── isnumber ── */
uint8_t isnumber(uint8_t l) {
    l -= '0';
    return (l <= ('9'-'0'));
}

/* ── cmd_compare ── */
uint8_t cmd_compare(uint8_t start, uint8_t *cmd) {
    if (cmd_words_len == 0 || start > (cmd_words_len - 1))
        return 0;
    uint8_t i = cmd_words_b[start];
    uint8_t j = 0;
    do {
        uint8_t c = cmd[j];
        uint8_t b = cmd_buffer[i];
        if (c == '\0') {
            if ((b == ' ') || (b == '\0'))
                return 1;
            break;
        }
        if (b != c)
            break;
        j += 1;
        i += 1;
    } while (i < CMD_BUF_SIZE);
    return 0;
}

/* ── cmd_tokenize ── */
void cmd_tokenize(void) {
    err_status = ERR_OK;
    uint8_t line_ptr = 0;
    uint8_t is_white = 1;
    uint8_t word = 0;
    uint8_t c = 0;

    while (1) {
        c = cmd_buffer[line_ptr];
        if (c == '\0') {
            cmd_words_len = word;
            break;
        }
        if (line_ptr == CMD_BUF_SIZE - 1) {
            err_status = ERR_CMD_TOO_LONG;
            return;
        }
        if (is_white && c != ' ') {
            is_white = 0;
            cmd_words_b[word++] = line_ptr;
            if (word >= N_WORDS) {
                cmd_words_len = 0;
                err_status = ERR_TOO_MANY_ARGUMENTS;
                return;
            }
        } else if (c == ' ') {
            is_white = 1;
        }
        line_ptr++;
    }
}

/* ── atoi_byte ── */
uint8_t atoi_byte(uint8_t *out, uint8_t idx) {
    uint8_t err = 1;
    uint8_t num = 0;
    while (isnumber(cmd_buffer[idx])) {
        err = 0;
        uint8_t digit = cmd_buffer[idx] - '0';
        if (num > 25 || (num == 25 && digit > 5))
            return 1;
        num = (num * 10) + digit;
        idx++;
    }
    *out = num;
    return err;
}

/* ── atoi_short ── */
uint8_t atoi_short(uint16_t *vlan, uint8_t idx) {
    uint8_t err = 1;
    *vlan = 0;
    while (isnumber(cmd_buffer[idx])) {
        err = 0;
        uint8_t digit = cmd_buffer[idx] - '0';
        if (*vlan > 6553 || (*vlan == 6553 && digit > 5))
            return 1;
        *vlan = (*vlan * 10) + digit;
        idx++;
    }
    return err;
}

/* ── parse_ip ── */
uint8_t ip[4];
uint8_t parse_ip(uint8_t idx) {
    uint8_t b;
    uint16_t val;
    for (b = 0; b < 4; b++) {
        val = 0;
        if (!isnumber(cmd_buffer[idx]))
            return 255;
        while (isnumber(cmd_buffer[idx])) {
            val = val * 10 + (cmd_buffer[idx] - '0');
            if (val > 255)
                return 255;
            idx++;
        }
        ip[b] = val;
        if (b < 3 && cmd_buffer[idx++] != '.')
            return 255;
    }
    if (cmd_buffer[idx] != '\0' && cmd_buffer[idx] != ' ')
        return 255;
    return 0;
}

/* ── Tests ── */

TEST(isletter_lowercase) {
    ASSERT(isletter('a'));
    ASSERT(isletter('z'));
    ASSERT(isletter('A'));  /* lowercased internally */
    ASSERT(!isletter('0'));
}

TEST(isnumber_digits) {
    ASSERT(isnumber('0'));
    ASSERT(isnumber('9'));
    ASSERT(!isnumber('a'));
}

TEST(cmd_compare_exact_match) {
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    memcpy(cmd_buffer, "port 1", 7);
    cmd_words_len = 2;
    cmd_words_b[0] = 0;
    cmd_words_b[1] = 5;
    ASSERT(cmd_compare(0, (uint8_t *)"port"));
    ASSERT(cmd_compare(1, (uint8_t *)"1"));
}

TEST(cmd_compare_no_match) {
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    memcpy(cmd_buffer, "xyz", 4);
    cmd_words_len = 1;
    cmd_words_b[0] = 0;
    ASSERT(!cmd_compare(0, (uint8_t *)"port"));
}

TEST(cmd_compare_empty) {
    cmd_words_len = 0;
    ASSERT(!cmd_compare(0, (uint8_t *)"port"));
}

TEST(cmd_tokenize_simple) {
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    memcpy(cmd_buffer, "show vlan", 10);
    cmd_tokenize();
    ASSERT_EQ(err_status, ERR_OK);
    ASSERT_EQ(cmd_words_len, 2);
    ASSERT_EQ(cmd_words_b[0], 0);
    ASSERT_EQ(cmd_words_b[1], 5);
}

TEST(cmd_tokenize_extra_spaces) {
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    memcpy(cmd_buffer, "  port   1  ", 13);
    cmd_tokenize();
    ASSERT_EQ(err_status, ERR_OK);
    ASSERT_EQ(cmd_words_len, 2);
    ASSERT_EQ(cmd_words_b[0], 2);
    ASSERT_EQ(cmd_words_b[1], 9);
}

TEST(cmd_tokenize_empty) {
    cmd_buffer[0] = '\0';
    cmd_tokenize();
    ASSERT_EQ(cmd_words_len, 0);
}

TEST(atoi_byte_simple) {
    uint8_t val = 0;
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    memcpy(cmd_buffer, "123", 3);
    ASSERT_EQ(atoi_byte(&val, 0), 0);
    ASSERT_EQ(val, 123);
}

TEST(atoi_byte_max) {
    uint8_t val = 0;
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    memcpy(cmd_buffer, "255", 3);
    ASSERT_EQ(atoi_byte(&val, 0), 0);
    ASSERT_EQ(val, 255);
}

TEST(atoi_byte_overflow) {
    uint8_t val = 0;
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    memcpy(cmd_buffer, "256", 3);
    ASSERT(atoi_byte(&val, 0) != 0);
}

TEST(atoi_short_simple) {
    uint16_t val = 0;
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    memcpy(cmd_buffer, "1000", 4);
    ASSERT_EQ(atoi_short(&val, 0), 0);
    ASSERT_EQ(val, 1000);
}

TEST(atoi_short_max) {
    uint16_t val = 0;
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    memcpy(cmd_buffer, "65535", 5);
    ASSERT_EQ(atoi_short(&val, 0), 0);
    ASSERT_EQ(val, 65535);
}

TEST(parse_ip_valid) {
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    memcpy(cmd_buffer, "192.168.1.1", 12);
    ASSERT_EQ(parse_ip(0), 0);
    ASSERT_EQ(ip[0], 192);
    ASSERT_EQ(ip[1], 168);
    ASSERT_EQ(ip[2], 1);
    ASSERT_EQ(ip[3], 1);
}

TEST(parse_ip_invalid) {
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    memcpy(cmd_buffer, "999.999.999.999", 16);
    ASSERT(parse_ip(0) != 0);
}

TEST(parse_ip_no_dots) {
    memset(cmd_buffer, 0, CMD_BUF_SIZE);
    memcpy(cmd_buffer, "19216811", 9);
    ASSERT(parse_ip(0) != 0);
}

int main(void)
{
    printf("cmd_parser core tests\n\n");

    RUN_TEST(isletter_lowercase);
    RUN_TEST(isnumber_digits);

    RUN_TEST(cmd_compare_exact_match);
    RUN_TEST(cmd_compare_no_match);
    RUN_TEST(cmd_compare_empty);

    RUN_TEST(cmd_tokenize_simple);
    RUN_TEST(cmd_tokenize_extra_spaces);
    RUN_TEST(cmd_tokenize_empty);

    RUN_TEST(atoi_byte_simple);
    RUN_TEST(atoi_byte_max);
    RUN_TEST(atoi_byte_overflow);
    RUN_TEST(atoi_short_simple);
    RUN_TEST(atoi_short_max);

    RUN_TEST(parse_ip_valid);
    RUN_TEST(parse_ip_invalid);
    RUN_TEST(parse_ip_no_dots);

    REPORT();
}
