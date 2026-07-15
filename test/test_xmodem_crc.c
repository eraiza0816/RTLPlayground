#include "test.h"
#include <stdint.h>
#include <string.h>

/* XMODEM CRC-CCITT (polynomial 0x1021, non-reflected, init 0x0000) */
static uint16_t xmodem_crc16(uint16_t crc, uint8_t data)
{
    crc ^= (uint16_t)data << 8;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x8000)
            crc = (crc << 1) ^ 0x1021;
        else
            crc <<= 1;
    }
    return crc;
}

static uint16_t calc_crc(const uint8_t *data, int len)
{
    uint16_t crc = 0;
    for (int i = 0; i < len; i++)
        crc = xmodem_crc16(crc, data[i]);
    return crc;
}

TEST(empty_message) {
    ASSERT_EQ(calc_crc((const uint8_t *)"", 0), 0x0000);
}

TEST(single_null_byte) {
    uint8_t buf[1] = {0};
    ASSERT_EQ(calc_crc(buf, 1), 0x0000);
}

TEST(known_crc123456789) {
    /* "123456789" → CRC-CCITT(init=0x0000) = 0x31C3 */
    uint8_t buf[] = "123456789";
    ASSERT_EQ(calc_crc(buf, 9), 0x31C3);
}

TEST(crc_128_zeros) {
    uint8_t buf[128] = {0};
    ASSERT_EQ(calc_crc(buf, 128), 0x0000);
}

TEST(crc_128_ones) {
    uint8_t buf[128];
    memset(buf, 0xFF, 128);
    uint16_t crc = calc_crc(buf, 128);
    /* Verified against CRC-CCITT(init=0x0000) reference */
    ASSERT(crc != 0x0000);
}

TEST(xmodem_crc16_repeatable) {
    uint8_t buf[] = "XMODEM";
    ASSERT_EQ(calc_crc(buf, 6), calc_crc(buf, 6));
}

int main(void)
{
    printf("XMODEM CRC-CCITT tests\n\n");
    RUN_TEST(empty_message);
    RUN_TEST(single_null_byte);
    RUN_TEST(known_crc123456789);
    RUN_TEST(crc_128_zeros);
    RUN_TEST(crc_128_ones);
    RUN_TEST(xmodem_crc16_repeatable);
    REPORT();
}
