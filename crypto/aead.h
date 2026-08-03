#ifndef _AEAD_H_
#define _AEAD_H_

#include <stdint.h>

#define AEAD_TAG_LEN 16
#define AEAD_NONCE_LEN 12
#define AEAD_KEY_LEN 32

uint8_t aead_encrypt(__xdata uint8_t *key, __xdata uint8_t *nonce,
                     __xdata uint8_t *aad, uint16_t aad_len,
                     __xdata uint8_t *plaintext, uint16_t len,
                     __xdata uint8_t *ciphertext, __xdata uint8_t *tag) __reentrant __banked;

uint8_t aead_decrypt(__xdata uint8_t *key, __xdata uint8_t *nonce,
                     __xdata uint8_t *aad, uint16_t aad_len,
                     __xdata uint8_t *ciphertext, uint16_t len,
                     __xdata uint8_t *plaintext, __xdata uint8_t *tag) __reentrant __banked;

void aead_test(void) __banked;

#endif
