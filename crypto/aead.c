#pragma codeseg BANK4

#include <stdint.h>
#include "rtl837x_common.h"
#include "chacha.h"
#include "poly1305.h"
#include "aead.h"

static __xdata struct chacha20_t aead_ctx;
static __xdata uint8_t aead_scratch[64];
static __xdata uint8_t aead_polykey[32];

/* One-time Poly1305 key: first 32 bytes of the ChaCha20 keystream block
 * with block counter 0 (RFC8439 section 2.6.2). */
static void aead_poly_key(__xdata uint8_t *key, __xdata uint8_t *nonce) __reentrant
{
	static __xdata uint8_t i;
	uint8_t j;

	for (i = 0; i < 64; i++)
		aead_scratch[i] = 0;
	/* inline chacha20_init to keep the reentrant stack depth small */
	for (j = 0; j < 32; j++)
		aead_ctx.key[j] = key[j];
	for (j = 0; j < 12; j++)
		aead_ctx.nonce[j] = nonce[j];
	aead_ctx.cnt = 0;
	aead_ctx.plaintext = aead_scratch;
	aead_ctx.cyphertext = aead_scratch;
	aead_ctx.length = 64;
	chacha20_encrypt(&aead_ctx);
	for (i = 0; i < AEAD_KEY_LEN; i++)
		aead_polykey[i] = aead_scratch[i];
	/* zero the scratch buffer for use as padding */
	for (i = 0; i < 16; i++)
		aead_scratch[i] = 0;
}

/* tag = Poly1305(one-time key, aad || pad16 || ciphertext || pad16 ||
 *                len(aad) || len(ciphertext)), lengths as 64-bit LE.
 * Parameters arrive via the static aead_tag_ctx to keep the reentrant
 * stack depth small on the 8051. */
struct aead_tag_ctx_t {
	__xdata uint8_t *aad;
	__xdata uint8_t *ciphertext;
	__xdata uint8_t *tag;
	uint16_t aad_len;
	uint16_t len;
};
static __xdata struct aead_tag_ctx_t aead_tag_ctx;

static void aead_tag(void) __reentrant
{
	static __xdata struct poly1305_t p;
	static __xdata uint8_t lenbuf[8];
	static __xdata uint8_t i;

	poly1305_init(&p, aead_polykey);
	if (aead_tag_ctx.aad_len) {
		poly1305_update(&p, aead_tag_ctx.aad, aead_tag_ctx.aad_len);
		if (aead_tag_ctx.aad_len & 0x0f)
			poly1305_update(&p, aead_scratch, 16 - (aead_tag_ctx.aad_len & 0x0f));
	}
	poly1305_update(&p, aead_tag_ctx.ciphertext, aead_tag_ctx.len);
	if (aead_tag_ctx.len & 0x0f)
		poly1305_update(&p, aead_scratch, 16 - (aead_tag_ctx.len & 0x0f));
	for (i = 0; i < 8; i++)
		lenbuf[i] = 0;
	lenbuf[0] = (uint8_t)aead_tag_ctx.aad_len;
	lenbuf[1] = (uint8_t)(aead_tag_ctx.aad_len >> 8);
	poly1305_update(&p, lenbuf, 8);
	lenbuf[0] = (uint8_t)aead_tag_ctx.len;
	lenbuf[1] = (uint8_t)(aead_tag_ctx.len >> 8);
	poly1305_update(&p, lenbuf, 8);
	poly1305_finish(&p, aead_tag_ctx.tag);
}

uint8_t aead_encrypt(__xdata uint8_t *key, __xdata uint8_t *nonce,
                     __xdata uint8_t *aad, uint16_t aad_len,
                     __xdata uint8_t *plaintext, uint16_t len,
                     __xdata uint8_t *ciphertext, __xdata uint8_t *tag) __reentrant __banked
{
	static __xdata uint8_t j;

	aead_poly_key(key, nonce);

	/* inline chacha20_init/set_counter to keep the reentrant stack small */
	for (j = 0; j < 32; j++)
		aead_ctx.key[j] = key[j];
	for (j = 0; j < 12; j++)
		aead_ctx.nonce[j] = nonce[j];
	aead_ctx.cnt = 1;
	aead_ctx.plaintext = plaintext;
	aead_ctx.cyphertext = ciphertext;
	aead_ctx.length = len;
	chacha20_encrypt(&aead_ctx);

	aead_tag_ctx.aad = aad;
	aead_tag_ctx.aad_len = aad_len;
	aead_tag_ctx.ciphertext = ciphertext;
	aead_tag_ctx.len = len;
	aead_tag_ctx.tag = tag;
	aead_tag();
	return 0;
}

uint8_t aead_decrypt(__xdata uint8_t *key, __xdata uint8_t *nonce,
                     __xdata uint8_t *aad, uint16_t aad_len,
                     __xdata uint8_t *ciphertext, uint16_t len,
                     __xdata uint8_t *plaintext, __xdata uint8_t *tag) __reentrant __banked
{
	static __xdata uint8_t comp[AEAD_TAG_LEN];
	static __xdata uint8_t diff;
	static __xdata uint8_t i;
	static __xdata uint8_t j;

	diff = 0;

	aead_poly_key(key, nonce);
	aead_tag_ctx.aad = aad;
	aead_tag_ctx.aad_len = aad_len;
	aead_tag_ctx.ciphertext = ciphertext;
	aead_tag_ctx.len = len;
	aead_tag_ctx.tag = comp;
	aead_tag();
	for (i = 0; i < AEAD_TAG_LEN; i++)
		diff |= comp[i] ^ tag[i];
	if (diff)
		return 1;

	for (j = 0; j < 32; j++)
		aead_ctx.key[j] = key[j];
	for (j = 0; j < 12; j++)
		aead_ctx.nonce[j] = nonce[j];
	aead_ctx.cnt = 1;
	aead_ctx.plaintext = ciphertext;
	aead_ctx.cyphertext = plaintext;
	aead_ctx.length = len;
	chacha20_encrypt(&aead_ctx);
	return 0;
}

/* Test AEAD using the example from RFC8439 section 2.8.2.
 * Firmware-only test hook: compiled only when NOT building the device
 * firmware (the host tools/aead_test.c and the Go aead_test.go verify
 * the vectors); the device build never calls it. */
#ifndef NO_AEAD_TEST
void aead_test(void) __banked
{
	static __code uint8_t key[32] = {
		0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f,
		0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f };
	static __code uint8_t nonce[12] = {
		0x07,0x00,0x00,0x00,0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47 };
	static __code uint8_t aad[12] = {
		0x50,0x51,0x52,0x53,0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7 };
	static __code uint8_t expect_ct[114] = {
		0xd3,0x1a,0x8d,0x34,0x64,0x8e,0x60,0xdb,0x7b,0x86,0xaf,0xbc,0x53,0xef,0x7e,0xc2,
		0xa4,0xad,0xed,0x51,0x29,0x6e,0x08,0xfe,0xa9,0xe2,0xb5,0xa7,0x36,0xee,0x62,0xd6,
		0x3d,0xbe,0xa4,0x5e,0x8c,0xa9,0x67,0x12,0x82,0xfa,0xfb,0x69,0xda,0x92,0x72,0x8b,
		0x1a,0x71,0xde,0x0a,0x9e,0x06,0x0b,0x29,0x05,0xd6,0xa5,0xb6,0x7e,0xcd,0x3b,0x36,
		0x92,0xdd,0xbd,0x7f,0x2d,0x77,0x8b,0x8c,0x98,0x03,0xae,0xe3,0x28,0x09,0x1b,0x58,
		0xfa,0xb3,0x24,0xe4,0xfa,0xd6,0x75,0x94,0x55,0x85,0x80,0x8b,0x48,0x31,0xd7,0xbc,
		0x3f,0xf4,0xde,0xf0,0x8e,0x4b,0x7a,0x9d,0xe5,0x76,0xd2,0x65,0x86,0xce,0xc6,0x4b,
		0x61,0x16 };
	static __code uint8_t expect_tag[16] = {
		0x1a,0xe1,0x0b,0x59,0x4f,0x09,0xe2,0x6a,0x7e,0x90,0x2e,0xcb,0xd0,0x60,0x06,0x91 };
	static __xdata uint8_t xkey[32];
	static __xdata uint8_t xnonce[12];
	static __xdata uint8_t xaad[12];
	static __xdata uint8_t pt[256];
	static __xdata uint8_t ct[256];
	static __xdata uint8_t tag[16];
	static __xdata uint8_t rt[256];
	uint8_t ok = 1;
	uint8_t i;

	/* The crypto API expects __xdata pointers; copy the __code test vectors
	 * to __xdata first (address spaces differ on the 8051). */
	memcpyc(xkey, key, 32);
	memcpyc(xnonce, nonce, 12);
	memcpyc(xaad, aad, 12);

	memcpyc(pt, "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.", 114);
	aead_encrypt(xkey, xnonce, xaad, 12, pt, 114, ct, tag);
	for (i = 0; i < 114; i++)
		if (ct[i] != expect_ct[i])
			ok = 0;
	for (i = 0; i < 16; i++)
		if (tag[i] != expect_tag[i])
			ok = 0;
	if (!ok) {
		print_string("aead_test: encrypt FAIL\n");
		return;
	}
	if (aead_decrypt(xkey, xnonce, xaad, 12, ct, 114, rt, tag)) {
		print_string("aead_test: decrypt FAIL\n");
		return;
	}
	for (i = 0; i < 114; i++)
		if (rt[i] != pt[i]) {
			print_string("aead_test: roundtrip FAIL\n");
			return;
		}
	tag[0] ^= 0x01;
	if (!aead_decrypt(xkey, xnonce, xaad, 12, ct, 114, rt, tag)) {
		print_string("aead_test: tamper not detected FAIL\n");
		return;
	}
	print_string("aead_test: PASS\n");
}
#endif
