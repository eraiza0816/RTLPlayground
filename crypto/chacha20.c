#pragma codeseg BANK4

#include <stdint.h>
#include "chacha.h"

#define ROTL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

static void quarter_round(__xdata uint32_t *a, __xdata uint32_t *b,
                           __xdata uint32_t *c, __xdata uint32_t *d) __reentrant
{
    *a += *b; *d ^= *a; *d = ROTL32(*d, 16);
    *c += *d; *b ^= *c; *b = ROTL32(*b, 12);
    *a += *b; *d ^= *a; *d = ROTL32(*d, 8);
    *c += *d; *b ^= *c; *b = ROTL32(*b, 7);
}

static __xdata uint32_t chacha_s_block[16];

static void chacha20_block(__xdata struct chacha20_t *ctx, __xdata uint32_t *keystream) __reentrant
{
    uint8_t i;
    __xdata uint32_t *state = (__xdata uint32_t *)ctx->wstate;

    chacha_s_block[0] = 0x61707865; chacha_s_block[1] = 0x3320646e;
    chacha_s_block[2] = 0x79622d32; chacha_s_block[3] = 0x6b206574;
    for (i = 0; i < 8; i++)
        chacha_s_block[4+i] = ((uint32_t)ctx->key[i*4+3] << 24) | ((uint32_t)ctx->key[i*4+2] << 16)
               | ((uint32_t)ctx->key[i*4+1] << 8) | ctx->key[i*4];
    chacha_s_block[12] = ctx->cnt;
    for (i = 0; i < 3; i++)
        chacha_s_block[13+i] = ((uint32_t)ctx->nonce[i*4+3] << 24) | ((uint32_t)ctx->nonce[i*4+2] << 16)
                | ((uint32_t)ctx->nonce[i*4+1] << 8) | ctx->nonce[i*4];

    for (i = 0; i < 16; i++)
        state[i] = chacha_s_block[i];

    for (i = 0; i < 10; i++) {
        quarter_round(&state[0], &state[4], &state[8], &state[12]);
        quarter_round(&state[1], &state[5], &state[9], &state[13]);
        quarter_round(&state[2], &state[6], &state[10], &state[14]);
        quarter_round(&state[3], &state[7], &state[11], &state[15]);
        quarter_round(&state[0], &state[5], &state[10], &state[15]);
        quarter_round(&state[1], &state[6], &state[11], &state[12]);
        quarter_round(&state[2], &state[7], &state[8], &state[13]);
        quarter_round(&state[3], &state[4], &state[9], &state[14]);
    }

    for (i = 0; i < 16; i++)
        keystream[i] = state[i] + chacha_s_block[i];
}

/* chacha20_init / chacha20_set_counter are used only by the host-side
 * test vectors (tools/aead_test.c, RFC 7539 section 2.4.2); the firmware
 * AEAD inlines the equivalent setup (crypto/aead.c) to keep the code and
 * the IRAM footprint small.  Compile them only for the host test. */
#ifndef NO_CHACHA_HELPERS
void chacha20_init(__xdata struct chacha20_t *ctx,
                   __xdata uint8_t *key, uint16_t key_len,
                   __xdata uint8_t *nonce, uint16_t nonce_len) __reentrant
{
    uint8_t i;
    for (i = 0; i < 32 && i < key_len; i++)
        ctx->key[i] = key[i];
    ctx->cnt = 0;
    for (i = 0; i < 12 && i < nonce_len; i++)
        ctx->nonce[i] = nonce[i];
}

void chacha20_set_counter(__xdata struct chacha20_t *ctx, uint32_t counter) __reentrant
{
    ctx->cnt = counter;
}
#endif

static __xdata uint32_t chacha_keystream[16];

void chacha20_encrypt(__xdata struct chacha20_t *ctx) __reentrant
{
    __xdata uint8_t *p = ctx->plaintext;
    uint8_t i, j;
    uint16_t remaining = ctx->length;

    while (remaining) {
        chacha20_block(ctx, chacha_keystream);
        i = (remaining > 64) ? 64 : (uint8_t)remaining;
        for (j = 0; j < i; j++)
            ctx->cyphertext[j] = p[j] ^ ((uint8_t *)chacha_keystream)[j];
        p += i;
        ctx->cyphertext += i;
        remaining -= i;
        ctx->cnt++;
    }
}
