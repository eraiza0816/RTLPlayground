#ifndef _POLY1305_H_
#define _POLY1305_H_

#include <stdint.h>

struct poly1305_t {
    uint32_t r[5];
    uint32_t pad[4];
    uint32_t h[5];
    uint8_t buf[16];
    uint8_t leftover;
};

/* poly1305_* are only called from crypto/aead.c (BANK3), so no __banked:
 * direct same-bank calls avoid the banked-call stack overhead (+4B each)
 * which would overflow the 8051 reentrant stack. */
void poly1305_init(__xdata struct poly1305_t *ctx, __xdata uint8_t *key) __reentrant;
void poly1305_update(__xdata struct poly1305_t *ctx, __xdata uint8_t *m, uint16_t bytes) __reentrant;
void poly1305_finish(__xdata struct poly1305_t *ctx, __xdata uint8_t *mac) __reentrant;

#endif
