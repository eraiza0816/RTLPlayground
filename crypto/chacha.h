#ifndef _CHACHA_H_
#define _CHACHA_H_

#include <stdint.h>

struct chacha20_t {
    uint8_t wstate[64];
    uint8_t constant[16];
    uint8_t key[32];
    uint32_t cnt;
    uint8_t nonce[12];
    __xdata uint8_t *plaintext;
    uint16_t length;
    __xdata uint8_t *cyphertext;
};

/* chacha20_* are only called from crypto/aead.c (BANK3), so no __banked:
 * direct same-bank calls avoid the banked-call stack overhead (+4B each)
 * which would overflow the 8051 reentrant stack. */
void chacha20_init(__xdata struct chacha20_t *ctx,
                   __xdata uint8_t *key, uint16_t key_len,
                   __xdata uint8_t *nonce, uint16_t nonce_len) __reentrant;
void chacha20_encrypt(__xdata struct chacha20_t *ctx) __reentrant;
void chacha20_set_counter(__xdata struct chacha20_t *ctx, uint32_t counter) __reentrant;

#endif
