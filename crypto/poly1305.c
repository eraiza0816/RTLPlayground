#pragma codeseg BANK3

#include <stdint.h>
#include "poly1305.h"

#define U8TO32(p) ((((uint32_t)(p)[0] & 0xff)) | (((uint32_t)(p)[1] & 0xff) << 8) | \
		   (((uint32_t)(p)[2] & 0xff) << 16) | (((uint32_t)(p)[3] & 0xff) << 24))

#define P1305_MASK 0x3ffffff

static __xdata uint32_t p1305_hibit;
static __xdata uint16_t p1305_want;
static __xdata uint8_t p1305_i;

/* h = h * r mod (2^130 - 5), all in 32-bit arithmetic.
 * Each 26-bit product is split exactly into a (low, high) 26-bit pair
 * using 13-bit operand halves, so no 64-bit multiply is required. */
/* r[] pre-split into 13-bit halves once (r is constant after init) so the
 * multiply loop never recomputes the 32-bit shifts. */
static __xdata uint16_t poly1305_rlo[5];
static __xdata uint16_t poly1305_rhi[5];
static __xdata uint16_t p1305_alo, p1305_ahi;

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
		p1305_alo = (uint16_t)(a & 0x1fff);
		p1305_ahi = (uint16_t)(a >> 13);
		for (j = 0; j < 5; j++) {
			b = ctx->r[j];
			m0 = (uint32_t)p1305_alo * poly1305_rlo[j];
			m1 = ((uint32_t)p1305_alo * poly1305_rhi[j]) +
			     ((uint32_t)p1305_ahi * poly1305_rlo[j]);
			m2 = (uint32_t)p1305_ahi * poly1305_rhi[j];
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
	p1305_hibit = final ? 0 : (1UL << 24);

	while (bytes >= 16) {
		/* 26-bit limb loads: the i-th limb starts at m + i*3 and is
		 * shifted right by i*2 (h[4] keeps the 2^130 bit via hibit) */
		for (p1305_i = 0; p1305_i < 4; p1305_i++)
			ctx->h[p1305_i] += (U8TO32(m + p1305_i * 3) >> (p1305_i * 2)) & P1305_MASK;
		ctx->h[4] += (U8TO32(m + 12) >> 8) | p1305_hibit;
		poly1305_mul(ctx);
		m += 16;
		bytes -= 16;
	}
}

/* r[] masks and shifts are uniform apart from the bit width: the table
 * keeps init a loop instead of nine unrolled U8TO32/mask sequences. */
static __code uint32_t poly1305_rmask[5] = {
	0x3ffffff, 0x3ffff03, 0x3ffc0ff, 0x3f03fff, 0x00fffff };

void poly1305_init(__xdata struct poly1305_t *ctx, __xdata uint8_t *key) __reentrant
{
	uint8_t i;

	for (i = 0; i < 5; i++) {
		ctx->r[i] = (U8TO32(key + i * 3) >> (i * 2)) & poly1305_rmask[i];
		poly1305_rlo[i] = (uint16_t)(ctx->r[i] & 0x1fff);
		poly1305_rhi[i] = (uint16_t)(ctx->r[i] >> 13);
	}
	for (i = 0; i < 4; i++)
		ctx->pad[i] = U8TO32(key + 16 + i * 4);

	for (i = 0; i < 5; i++)
		ctx->h[i] = 0;
	ctx->leftover = 0;
}

void poly1305_update(__xdata struct poly1305_t *ctx,
                     __xdata uint8_t *m, uint16_t bytes) __reentrant
{
	while (ctx->leftover) {
		p1305_want = 16 - ctx->leftover;
		if (p1305_want > bytes)
			p1305_want = (uint8_t)bytes;
		for (p1305_i = 0; p1305_i < p1305_want; p1305_i++)
			ctx->buf[ctx->leftover + p1305_i] = m[p1305_i];
		bytes -= p1305_want;
		m += p1305_want;
		ctx->leftover += p1305_want;
		if (ctx->leftover < 16)
			return;
		poly1305_blocks(ctx, ctx->buf, 16, 0);
		ctx->leftover = 0;
	}

	if (bytes >= 16) {
		p1305_want = (bytes & ~15);
		poly1305_blocks(ctx, m, p1305_want, 0);
		m += p1305_want;
		bytes -= p1305_want;
	}

	if (bytes) {
		for (p1305_i = 0; p1305_i < bytes; p1305_i++)
			ctx->buf[p1305_i] = m[p1305_i];
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
	static __xdata uint32_t hxp[4], s;
	static __xdata uint8_t cin, co;
	static __xdata uint8_t i;

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

	hxp[0] = h0 | (h1 << 26);
	hxp[1] = (h1 >> 6) | (h2 << 20);
	hxp[2] = (h2 >> 12) | (h3 << 14);
	hxp[3] = (h3 >> 18) | (h4 << 8);

	/* tag = h + pad with the 32-bit carry propagated across the 4 words;
	 * one loop instead of four unrolled blocks keeps the code small */
	cin = 0;
	for (i = 0; i < 4; i++) {
		s = hxp[i] + ctx->pad[i];
		co = (s < hxp[i]) ? 1 : 0;
		s += cin;
		if (s < cin)
			co = 1;
		mac[i*4] = (uint8_t)s; mac[i*4+1] = (uint8_t)(s >> 8);
		mac[i*4+2] = (uint8_t)(s >> 16); mac[i*4+3] = (uint8_t)(s >> 24);
		cin = co;
	}

	for (i = 0; i < 5; i++) {
		ctx->h[i] = 0;
		ctx->r[i] = 0;
	}
	ctx->pad[0] = 0; ctx->pad[1] = 0; ctx->pad[2] = 0; ctx->pad[3] = 0;
	ctx->leftover = 0;
}
