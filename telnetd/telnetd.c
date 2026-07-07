#pragma codeseg BANK3

#include <8051.h>
#include <stdint.h>
#include "telnetd.h"
#include "uip.h"

extern __xdata uint8_t uip_buf[UIP_BUFSIZE + 2];
extern __xdata uint16_t uip_slen;

#define TX_BUF 512
#define RX_BUF 256
#define RING_ADVANCE(ptr, amount) (((ptr) + (amount)) & (TX_BUF - 1))
#define UIP_APPBUF_OFFSET 66

#define AUTH_WAIT 0
#define AUTH_OK   1

extern volatile __xdata uint8_t cmd_available;
extern __xdata uint8_t cmd_buffer[128];
extern __xdata char passwd[21];

static __xdata uint8_t tx_buf[TX_BUF];
static __xdata uint16_t tx_head, tx_tail, tx_inflight;
static __xdata uint8_t rx_buf[RX_BUF], rx_pos;
static __xdata uint8_t auth_state;

static __xdata uint8_t telnet_connected;
static __xdata uint8_t telnet_echo;

static uint16_t ring_used(void)
{
    if (tx_head >= tx_tail) return tx_head - tx_tail;
    return TX_BUF - tx_tail + tx_head;
}

static uint16_t ring_space(void)
{
    return (TX_BUF - 1) - ring_used();
}

static void drain(void)
{
    uint16_t avail;

    if (tx_inflight > 0) return;

    avail = ring_used();
    if (avail == 0) return;

    {
        uint16_t mss = uip_mss();
        if (mss == 0) mss = 536;
        if (avail > mss) avail = mss;
    }

    EA = 0;
    {
        uint16_t i;
        for (i = 0; i < avail; i++)
            uip_buf[UIP_APPBUF_OFFSET + i] = tx_buf[RING_ADVANCE(tx_tail, i)];
        for (i = 0; i < 3; i++)
            uip_buf[UIP_APPBUF_OFFSET + avail + i] = 0;
    }
    uip_slen = avail;
    tx_inflight = avail;
    EA = 1;
}

void telnet_tx_enqueue(char c) __banked
{
    if (!telnet_connected) return;

    EA = 0;
    if (ring_space() < 1) { EA = 1; return; }
    tx_buf[tx_head] = c;
    tx_head = RING_ADVANCE(tx_head, 1);
    EA = 1;
}

uint8_t telnet_is_connected(void) __banked
{
    return telnet_connected;
}

uint8_t telnet_echo_enabled(void) __banked
{
    return telnet_echo;
}

static void send_iac(uint8_t cmd, uint8_t opt)
{
    EA = 0;
    if (ring_space() < 3) { EA = 1; return; }
    tx_buf[tx_head] = IAC; tx_head = RING_ADVANCE(tx_head, 1);
    tx_buf[tx_head] = cmd; tx_head = RING_ADVANCE(tx_head, 1);
    tx_buf[tx_head] = opt; tx_head = RING_ADVANCE(tx_head, 1);
    EA = 1;
}

static void process_input(__xdata uint8_t *data, uint16_t len)
{
    uint8_t iac_state = 0;
    uint8_t iac_cmd = 0;
    uint16_t i;
    for (i = 0; i < len; i++) {
        uint8_t c = data[i];

        if (iac_state == 1) {
            iac_state = 0;
            if (c != IAC) {
                iac_cmd = c;
                iac_state = 2;
                continue;
            }
        } else if (iac_state == 2) {
            iac_state = 0;
            if (iac_cmd == WILL) send_iac(DONT, c);
            else if (iac_cmd == DO) send_iac(WONT, c);
            continue;
        } else if (c == IAC) {
            iac_state = 1;
            continue;
        }

        if (c == '\r' || c == '\n') {
            if (rx_pos == 0) {
                if (auth_state == AUTH_OK && telnet_echo) {
                    telnet_tx_enqueue('\r');
                    telnet_tx_enqueue('\n');
                    telnet_tx_enqueue('>'); telnet_tx_enqueue(' ');
                }
                continue;
            }

            if (telnet_echo) {
                telnet_tx_enqueue('\r');
                telnet_tx_enqueue('\n');
            }

            uint8_t j;
            for (j = 0; j < rx_pos && j < 127; j++)
                cmd_buffer[j] = rx_buf[j];
            cmd_buffer[j] = '\0';
            rx_pos = 0;

            if (auth_state == AUTH_WAIT) {
                uint8_t ok = 1;
                for (uint8_t pc = 0; pc < 20; pc++) {
                    if (cmd_buffer[pc] != passwd[pc]) { ok = 0; break; }
                    if (passwd[pc] == '\0') break;
                }
                if (ok) {
                    auth_state = AUTH_OK;
                    telnet_echo = 1;
                    telnet_tx_enqueue('\r');
                    telnet_tx_enqueue('\n');
                    telnet_tx_enqueue('>'); telnet_tx_enqueue(' ');
                } else {
                    telnet_tx_enqueue('\r');
                    telnet_tx_enqueue('\n');
                    telnet_tx_enqueue('P'); telnet_tx_enqueue('a');
                    telnet_tx_enqueue('s'); telnet_tx_enqueue('s');
                    telnet_tx_enqueue('w'); telnet_tx_enqueue('o');
                    telnet_tx_enqueue('r'); telnet_tx_enqueue('d');
                    telnet_tx_enqueue(':'); telnet_tx_enqueue(' ');
                }
            } else {
                cmd_available = 1;
            }
        } else if (c >= 0x20 && c <= 0x7E) {
            if (rx_pos < RX_BUF - 1) {
                rx_buf[rx_pos++] = c;
                if (telnet_echo) telnet_tx_enqueue(c);
            }
        } else if ((c == 0x08 || c == 0x7F) && rx_pos > 0) {
            rx_pos--;
            if (telnet_echo) {
                telnet_tx_enqueue(0x08);
                telnet_tx_enqueue(' ');
                telnet_tx_enqueue(0x08);
            }
        }
    }
}

static void reset_connection_state(void)
{
    telnet_echo = 0;
    auth_state = AUTH_WAIT;
    rx_pos = 0;
    tx_head = 0;
    tx_tail = 0;
    tx_inflight = 0;
}

void telnetd_init(void) __banked
{
    telnet_connected = 0;
    reset_connection_state();
    uip_listen(HTONS(23));
}

void telnetd_appcall(void) __banked
{
    if (uip_connected()) {
        telnet_connected = 1;
        reset_connection_state();

        send_iac(WILL, SGA);
        telnet_tx_enqueue('P'); telnet_tx_enqueue('a');
        telnet_tx_enqueue('s'); telnet_tx_enqueue('s');
        telnet_tx_enqueue('w'); telnet_tx_enqueue('o');
        telnet_tx_enqueue('r'); telnet_tx_enqueue('d');
        telnet_tx_enqueue(':'); telnet_tx_enqueue(' ');
        drain();
        return;
    }
    if (uip_closed() || uip_aborted() || uip_timedout()) {
        telnet_connected = 0;
        tx_inflight = 0;
        return;
    }
    if (uip_acked()) {
        EA = 0;
        tx_tail = RING_ADVANCE(tx_tail, tx_inflight);
        tx_inflight = 0;
        EA = 1;
    }
    if (uip_rexmit()) {
        if (tx_inflight > 0) {
            uint16_t i;
            for (i = 0; i < tx_inflight; i++)
                uip_buf[UIP_APPBUF_OFFSET + i] = tx_buf[RING_ADVANCE(tx_tail, i)];
            for (i = 0; i < 3; i++)
                uip_buf[UIP_APPBUF_OFFSET + tx_inflight + i] = 0;
            uip_slen = tx_inflight;
        }
        return;
    }
    if (uip_poll()) {
        drain();
        return;
    }
    if (uip_newdata()) {
        process_input(uip_appdata, uip_len);
    }
    drain();
}
