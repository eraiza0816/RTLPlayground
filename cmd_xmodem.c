#include "rtl837x_common.h"
#include "rtl837x_flash.h"
#include "machine.h"

#pragma codeseg BANK3
#pragma constseg BANK3

__xdata uint8_t xmodem_active;
static __xdata uint8_t xm_pkt_buf[128];

extern volatile __xdata uint32_t ticks;
extern __xdata uint8_t sbuf[SBUF_SIZE];
extern volatile __xdata uint8_t sbuf_ptr;
extern __xdata uint8_t flash_buf[FLASH_BUF_SIZE];
extern __xdata struct flash_region_t flash_region;
extern __xdata uint8_t l;

static uint16_t xmodem_crc16(uint16_t crc, uint8_t data)
{
    crc ^= (uint16_t)data << 8;
    for (uint8_t i = 0; i < 8; i++) {
        if (crc & 0x8000)
            crc = (crc << 1) ^ 0x1021;
        else
            crc <<= 1;
    }
    return crc;
}

void parse_xmodem(void) __banked __reentrant
{
    uint8_t last_ptr = sbuf_ptr;
    uint8_t seq = 1;
    uint8_t pkt_pos = 0;
    uint16_t pkt_crc = 0;
    uint8_t state = 0;
    uint32_t flash_addr = FIRMWARE_UPLOAD_START;
    uint16_t pkt_count = 0;
    uint32_t tmo = ticks;
    uint8_t nak_count = 0;

    xmodem_active = 1;
    print_string("\nXMODEM: waiting for transfer... (cancel: Ctrl+X)\n");
    write_char('C');

    while (1) {
        if (last_ptr != sbuf_ptr) {
            uint8_t b = sbuf[last_ptr];
            last_ptr = (last_ptr + 1) & SBUF_MASK;
            tmo = ticks;

            switch (state) {
            case 0:
                if (b == 0x01) { seq = 1; pkt_pos = 0; pkt_crc = 0; state = 1; }
                else if (b == 0x04) { write_char(0x06); goto verify; }
                else if (b == 0x18) { print_string("\nCancelled\n"); goto done; }
                break;
            case 1:
                if (b == seq) { state = 2; }
                else if (b == (uint8_t)(seq - 1)) { write_char(0x06); state = 0; }
                else { write_char(0x15); state = 0; if (++nak_count > 20) goto done; }
                break;
            case 2:
                if (b == (uint8_t)(255 - seq)) { state = 3; }
                else { write_char(0x15); state = 0; if (++nak_count > 20) goto done; }
                break;
            case 3:
                xm_pkt_buf[pkt_pos++] = b;
                if (pkt_pos >= 128) { state = 4; }
                break;
            case 4:
                pkt_crc = (uint16_t)b << 8; state = 5;
                break;
            case 5: {
                pkt_crc |= b;
                uint16_t crc = 0x0000;
                for (uint8_t i = 0; i < 128; i++)
                    crc = xmodem_crc16(crc, xm_pkt_buf[i]);
                if (crc != pkt_crc) {
                    write_char(0x15);
                    state = 0;
                    if (++nak_count > 20) goto done;
                    break;
                }
                nak_count = 0;
                write_char(0x06);
                uint8_t off = (seq - 1) & 3;
                for (uint8_t i = 0; i < 128; i++)
                    flash_buf[(off << 7) + i] = xm_pkt_buf[i];
                pkt_count++;
                if (off == 3) {
                    if ((pkt_count & 7) == 4) {
                        flash_region.addr = flash_addr;
                        flash_sector_erase();
                    }
                    flash_region.addr = flash_addr;
                    flash_region.len = FLASH_BUF_SIZE;
                    flash_write_bytes(flash_buf);
                    flash_addr += FLASH_BUF_SIZE;
                    write_char('.');
                }
                seq++;
                state = 0;
                break;
            }
            }
        } else {
            if ((uint16_t)(ticks - tmo) > (SYS_TICK_HZ * 10)) {
                print_string("\nTimeout\n");
                break;
            }
        }
    }

verify:
    print_string("\nTransfer OK. Rebooting...\n");
    delay(200);
    reset_chip();

done:
    l = last_ptr;
    xmodem_active = 0;
}
