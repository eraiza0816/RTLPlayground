#pragma codeseg BANK3

#include <stdint.h>
#include "poly1305.h"

#define U8TO32(p) ((((uint32_t)(p)[0] & 0xff)) | (((uint32_t)(p)[1] & 0xff) << 8) | \
		   (((uint32_t)(p)[2] & 0xff) << 16) | (((uint32_t)(p)[3] & 0xff) << 24))

#define P1305_MASK 0x3ffffff

/* h = h * r mod (2^130 - 5), all in 32-bit arithmetic.
 * Each 26-bit product is split exactly into a (low, high) 26-bit pair
 * using 13-bit operand halves, so no 64-bit multiply is required. */
static void poly1305_mul(__xdata struct poly1305_t *ctx) __reentrant
{
	/* Hot computation uses static scratch (single-threaded firmware) to keep
	 * the reentrant stack depth small on the 8051. */
	static __xdata uint32_t m0, m1, m2, t, plo, phi;
	static __xdata uint32_t a, b, c;
	/* volatile: SDCC 4.5.0 mcs51 caches i/j in registers across the hot
	 * multiply and clobbers the register copy (the phi computation reuses
	 * the register holding j), corrupting the lo[]/hi[] accumulation
	 * indices. volatile forces every use through XRAM. */
	static __xdata volatile uint8_t i, j;
	static __xdata uint32_t lo[5];
	static __xdata uint32_t hi[5];

	for (i = 0; i < 5; i++) {
		lo[i] = 0;
		hi[i] = 0;
	}

	for (i = 0; i < 5; i++) {
		a = ctx->h[i];
		for (j = 0; j < 5; j++) {
			b = ctx->r[j];
			m0 = (a & 0x1fff) * (b & 0x1fff);
			m1 = ((a & 0x1fff) * (b >> 13)) + ((a >> 13) * (b & 0x1fff));
			m2 = (a >> 13) * (b >> 13);
			t = m0 + ((m1 & 0x1fff) << 13);
			plo = t & P1305_MASK;
			phi = m2 + (m1 >> 13) + (t >> 26);
			if (i + j >= 5) {
				lo[i + j - 5] += 5 * plo;
				hi[i + j - 5] += 5 * phi;
			} else {
				lo[i + j] += plo;
				hi[i + j] += phi;
			}
		}
	}

	c = hi[0] + (lo[0] >> 26); lo[0] &= P1305_MASK;
	hi[0] = 0;
	lo[1] += c;
	hi[1] += lo[1] >> 26; lo[1] &= P1305_MASK;
	c = hi[1];
	lo[2] += c;
	hi[2] += lo[2] >> 26; lo[2] &= P1305_MASK;
	c = hi[2];
	lo[3] += c;
	hi[3] += lo[3] >> 26; lo[3] &= P1305_MASK;
	c = hi[3];
	lo[4] += c;
	hi[4] += lo[4] >> 26; lo[4] &= P1305_MASK;
	c = hi[4];
	lo[0] += c * 5;
	hi[0] += lo[0] >> 26; lo[0] &= P1305_MASK;
	lo[1] += hi[0];
	hi[1] += lo[1] >> 26; lo[1] &= P1305_MASK;

	for (i = 0; i < 5; i++)
		ctx->h[i] = lo[i];
}

static void poly1305_blocks(__xdata struct poly1305_t *ctx,
                            __xdata uint8_t *m, uint16_t bytes, uint8_t final) __reentrant
{
	uint32_t hibit = final ? 0 : (1UL << 24);

	while (bytes >= 16) {
		ctx->h[0] += (U8TO32(m + 0)      ) & P1305_MASK;
		ctx->h[1] += (U8TO32(m + 3) >>  2) & P1305_MASK;
		ctx->h[2] += (U8TO32(m + 6) >>  4) & P1305_MASK;
		ctx->h[3] += (U8TO32(m + 9) >>  6) & P1305_MASK;
		ctx->h[4] += (U8TO32(m + 12) >>  8) | hibit;
		poly1305_mul(ctx);
		m += 16;
		bytes -= 16;
	}
}

void poly1305_init(__xdata struct poly1305_t *ctx, __xdata uint8_t *key) __reentrant
{
	uint8_t i;

	ctx->r[0] = (U8TO32(key + 0)      ) & 0x3ffffff;
	ctx->r[1] = (U8TO32(key + 3) >>  2) & 0x3ffff03;
	ctx->r[2] = (U8TO32(key + 6) >>  4) & 0x3ffc0ff;
	ctx->r[3] = (U8TO32(key + 9) >>  6) & 0x3f03fff;
	ctx->r[4] = (U8TO32(key + 12) >>  8) & 0x00fffff;

	ctx->pad[0] = U8TO32(key + 16);
	ctx->pad[1] = U8TO32(key + 20);
	ctx->pad[2] = U8TO32(key + 24);
	ctx->pad[3] = U8TO32(key + 28);

	for (i = 0; i < 5; i++)
		ctx->h[i] = 0;
	ctx->leftover = 0;
}

void poly1305_update(__xdata struct poly1305_t *ctx,
                     __xdata uint8_t *m, uint16_t bytes) __reentrant
{
	uint16_t want;

	while (ctx->leftover) {
		want = 16 - ctx->leftover;
		if (want > bytes)
			want = (uint8_t)bytes;
		uint8_t i;
		for (i = 0; i < want; i++)
			ctx->buf[ctx->leftover + i] = m[i];
		bytes -= want;
		m += want;
		ctx->leftover += want;
		if (ctx->leftover < 16)
			return;
		poly1305_blocks(ctx, ctx->buf, 16, 0);
		ctx->leftover = 0;
	}

	if (bytes >= 16) {
		want = (bytes & ~15);
		poly1305_blocks(ctx, m, want, 0);
		m += want;
		bytes -= want;
	}

	if (bytes) {
		uint8_t i;
		for (i = 0; i < bytes; i++)
			ctx->buf[i] = m[i];
		ctx->leftover = bytes;
	}
}

void poly1305_finish(__xdata struct poly1305_t *ctx, __xdata uint8_t *mac) __reentrant
{
	/* All computation uses static scratch (single-threaded firmware) to keep
	 * the reentrant stack depth small on the 8051. */
	static __xdata uint32_t h0, h1, h2, h3, h4, c;
	static __xdata uint32_t g0, g1, g2, g3, g4;
	static __xdata uint32_t mask;
	static __xdata uint32_t h0p, h1p, h2p, h3p, s;
	static __xdata uint8_t cin, co;
	uint8_t i;

	if (ctx->leftover) {
		i = ctx->leftover;
		ctx->buf[i++] = 1;
		for (; i < 16; i++)
			ctx->buf[i] = 0;
		poly1305_blocks(ctx, ctx->buf, 16, 1);
	}

	h0 = ctx->h[0]; h1 = ctx->h[1]; h2 = ctx->h[2];
	h3 = ctx->h[3]; h4 = ctx->h[4];

	c = h1 >> 26; h1 = h1 & P1305_MASK; h2 += c;
	c = h2 >> 26; h2 = h2 & P1305_MASK; h3 += c;
	c = h3 >> 26; h3 = h3 & P1305_MASK; h4 += c;
	c = h4 >> 26; h4 = h4 & P1305_MASK; h0 += c * 5;
	c = h0 >> 26; h0 = h0 & P1305_MASK; h1 += c;

	g0 = h0 + 5; c = g0 >> 26; g0 &= P1305_MASK;
	g1 = h1 + c; c = g1 >> 26; g1 &= P1305_MASK;
	g2 = h2 + c; c = g2 >> 26; g2 &= P1305_MASK;
	g3 = h3 + c; c = g3 >> 26; g3 &= P1305_MASK;
	g4 = h4 + c - (1UL << 26);

	mask = (g4 >> 31) - 1;
	g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
	mask = ~mask;
	h0 = (h0 & mask) | g0;
	h1 = (h1 & mask) | g1;
	h2 = (h2 & mask) | g2;
	h3 = (h3 & mask) | g3;
	h4 = (h4 & mask) | g4;

	h0p = h0 | (h1 << 26);
	h1p = (h1 >> 6) | (h2 << 20);
	h2p = (h2 >> 12) | (h3 << 14);
	h3p = (h3 >> 18) | (h4 << 8);

	s = h0p + ctx->pad[0];
	mac[0] = (uint8_t)s; mac[1] = (uint8_t)(s >> 8);
	mac[2] = (uint8_t)(s >> 16); mac[3] = (uint8_t)(s >> 24);
	cin = (s < h0p) ? 1 : 0;

	s = h1p + ctx->pad[1];
	co = (s < h1p) ? 1 : 0;
	s += cin;
	if (s < cin)
		co = 1;
	mac[4] = (uint8_t)s; mac[5] = (uint8_t)(s >> 8);
	mac[6] = (uint8_t)(s >> 16); mac[7] = (uint8_t)(s >> 24);
	cin = co;

	s = h2p + ctx->pad[2];
	co = (s < h2p) ? 1 : 0;
	s += cin;
	if (s < cin)
		co = 1;
	mac[8] = (uint8_t)s; mac[9] = (uint8_t)(s >> 8);
	mac[10] = (uint8_t)(s >> 16); mac[11] = (uint8_t)(s >> 24);
	cin = co;

	s = h3p + ctx->pad[3];
	co = (s < h3p) ? 1 : 0;
	s += cin;
	if (s < cin)
		co = 1;
	mac[12] = (uint8_t)s; mac[13] = (uint8_t)(s >> 8);
	mac[14] = (uint8_t)(s >> 16); mac[15] = (uint8_t)(s >> 24);

	for (i = 0; i < 5; i++) {
		ctx->h[i] = 0;
		ctx->r[i] = 0;
	}
	ctx->pad[0] = 0; ctx->pad[1] = 0; ctx->pad[2] = 0; ctx->pad[3] = 0;
	ctx->leftover = 0;
}
