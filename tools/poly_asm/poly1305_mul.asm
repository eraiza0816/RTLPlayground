; poly1305_mul: h = h * r mod (2^130 - 5), 26-bit limbs.
; 8051 assembler implementation replacing the C version. The C version
; called the SDCC __mullong reentrant library 7 times per block, which
; added ~10-16 bytes of reentrant stack per call to the deepest chain.
; This version uses only 8-bit MUL AB and fixed XDATA scratch, keeping
; the reentrant stack usage at zero and speeding up the hot path.
;
; IMPORTANT: all scratch data lives in XDATA. On the 8051, "direct"
; addressing (MOV A,<direct>, MOV <direct>,A, MOV <direct>,<direct>)
; can only reach IDATA; a 16-bit XDATA symbol used in direct addressing
; is truncated to its low byte and silently accesses IDATA. Every XDATA
; access therefore goes through DPTR + MOVX, keeping 16-bit addresses
; in the r0/r1 register pair where needed.
;
; C prototype: void poly1305_mul(__xdata struct poly1305_t *ctx);
; Struct layout (poly1305.h):
;   r[5]  at ctx + 0x00   (20 bytes, 26-bit limbs, LE uint32)
;   pad   at ctx + 0x14
;   h[5]  at ctx + 0x24   (20 bytes, 26-bit limbs, LE uint32)

	.module poly1305_mul
	.globl _poly1305_mul

	.area XSEG (XDATA)
_plo:	.ds 20			; lo[5]
_phi:	.ds 20			; hi[5]
_ptmp:	.ds 4			; mul16 result / phi
	.globl _ptmp2
_ptmp2:	.ds 4			; plo / second product
_p5:	.ds 4			; 5x scratch
_pal:	.ds 2			; a_lo (13 bits)
_pah:	.ds 2			; a_hi (13 bits)
_pbl:	.ds 2			; b_lo (13 bits)
_pbh:	.ds 2			; b_hi (13 bits)
_pm0:	.ds 4
_pm1:	.ds 4
_pm2:	.ds 4
_pt:	.ds 4
_pc:	.ds 4			; carry word
_pctx:	.ds 2
_paddr:	.ds 2			; target address for the add helpers
_pi:	.ds 1
_pj:	.ds 1

	.area BANK3 (CODE)

; mul16: (r4,r5) * (r6,r7) -> _ptmp[0..3]
mul16:
	mov	dptr, #_ptmp2
	mov	a, #0xAA
	movx	@dptr, a
	mov	a, r4
	mov	b, r6
	mul	ab
	mov	r0, a			; p0_lo
	mov	r1, b			; p0_hi
	mov	a, r4
	mov	b, r7
	mul	ab
	mov	r2, a			; p1a_lo
	mov	r3, b			; p1a_hi
	mov	a, r7
	mov	r4, a			; save Y_hi (X_lo no longer needed)
	mov	a, r5
	mov	b, r6
	mul	ab
	add	a, r2
	mov	r2, a			; p1_lo
	mov	a, b
	addc	a, r3
	mov	r3, a			; p1_hi
	mov	r7, #0
	jnc	mul16_nc
	inc	r7			; p1 bit 16
mul16_nc:
	mov	a, r1
	add	a, r2
	mov	r1, a			; byte1 = p0_hi + p1_lo
	mov	a, r3
	addc	a, r7
	mov	r3, a			; (p1>>8) + carry
	mov	a, r5
	mov	b, r4
	mul	ab			; p2 = r5 * Y_hi
	add	a, r3
	mov	r3, a			; byte2
	mov	a, b
	addc	a, #0
	mov	r4, a			; byte3
	mov	dptr, #_ptmp
	mov	a, r0
	movx	@dptr, a
	inc	dptr
	mov	a, r1
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	inc	dptr
	mov	a, r4
	movx	@dptr, a
	ret

; add_from_ptmp: *(_paddr) += _ptmp (4 bytes)
add_from_ptmp:
	mov	dptr, #_paddr
	movx	a, @dptr
	mov	r0, a
	inc	dptr
	movx	a, @dptr
	mov	r1, a
	mov	dptr, #_ptmp
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	movx	a, @dptr
	add	a, r2
	movx	@dptr, a
	mov	dptr, #_ptmp+1
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	inc	dptr
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_ptmp+2
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	inc	dptr
	inc	dptr
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_ptmp+3
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	inc	dptr
	inc	dptr
	inc	dptr
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	ret

; add_from_ptmp2: *(_paddr) += _ptmp2
add_from_ptmp2:
	mov	dptr, #_paddr
	movx	a, @dptr
	mov	r0, a
	inc	dptr
	movx	a, @dptr
	mov	r1, a
	mov	dptr, #_ptmp2
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	movx	a, @dptr
	add	a, r2
	movx	@dptr, a
	mov	dptr, #_ptmp2+1
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	inc	dptr
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_ptmp2+2
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	inc	dptr
	inc	dptr
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_ptmp2+3
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	inc	dptr
	inc	dptr
	inc	dptr
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	ret

; add_from_p5: *(_paddr) += _p5
add_from_p5:
	mov	dptr, #_paddr
	movx	a, @dptr
	mov	r0, a
	inc	dptr
	movx	a, @dptr
	mov	r1, a
	mov	dptr, #_p5
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	movx	a, @dptr
	add	a, r2
	movx	@dptr, a
	mov	dptr, #_p5+1
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	inc	dptr
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_p5+2
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	inc	dptr
	inc	dptr
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_p5+3
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	inc	dptr
	inc	dptr
	inc	dptr
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	ret

; add_from_pc: *(_paddr) += _pc
add_from_pc:
	mov	dptr, #_paddr
	movx	a, @dptr
	mov	r0, a
	inc	dptr
	movx	a, @dptr
	mov	r1, a
	mov	dptr, #_pc
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	movx	a, @dptr
	add	a, r2
	movx	@dptr, a
	mov	dptr, #_pc+1
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	inc	dptr
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_pc+2
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	inc	dptr
	inc	dptr
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_pc+3
	movx	a, @dptr
	mov	r2, a
	mov	dpl, r0
	mov	dph, r1
	inc	dptr
	inc	dptr
	inc	dptr
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	ret

; load4: *dptr -> _pt (4 bytes)
load4:
	movx	a, @dptr
	mov	r2, a
	inc	dptr
	movx	a, @dptr
	mov	r3, a
	inc	dptr
	movx	a, @dptr
	mov	r6, a
	inc	dptr
	movx	a, @dptr
	mov	r7, a
	mov	dptr, #_pt
	mov	a, r2
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	inc	dptr
	mov	a, r6
	movx	@dptr, a
	inc	dptr
	mov	a, r7
	movx	@dptr, a
	ret

; _p5 = 5 * _ptmp2   (4x + x)
mul5_p5_from_ptmp2:
	mov	dptr, #_ptmp2
	movx	a, @dptr
	rl	a
	rl	a
	anl	a, #0xfc
	mov	r2, a
	mov	dptr, #_p5
	movx	@dptr, a
	mov	dptr, #_ptmp2+1
	movx	a, @dptr
	rl	a
	rl	a
	anl	a, #0xfc
	mov	r3, a
	mov	dptr, #_ptmp2
	movx	a, @dptr
	swap	a
	rr	a
	rr	a
	anl	a, #0x03
	orl	a, r3
	mov	dptr, #_p5+1
	movx	@dptr, a
	mov	dptr, #_ptmp2+2
	movx	a, @dptr
	rl	a
	rl	a
	anl	a, #0xfc
	mov	r3, a
	mov	dptr, #_ptmp2+1
	movx	a, @dptr
	swap	a
	rr	a
	rr	a
	anl	a, #0x03
	orl	a, r3
	mov	dptr, #_p5+2
	movx	@dptr, a
	mov	dptr, #_ptmp2+3
	movx	a, @dptr
	rl	a
	rl	a
	mov	r3, a
	mov	dptr, #_ptmp2+2
	movx	a, @dptr
	swap	a
	rr	a
	rr	a
	anl	a, #0x03
	orl	a, r3
	mov	dptr, #_p5+3
	movx	@dptr, a
	mov	dptr, #_ptmp2
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_p5
	movx	a, @dptr
	add	a, r2
	movx	@dptr, a
	mov	dptr, #_ptmp2+1
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_p5+1
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_ptmp2+2
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_p5+2
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_ptmp2+3
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_p5+3
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	ret

; _ptmp2 = 5 * _ptmp
mul5_ptmp2_from_ptmp:
	mov	dptr, #_ptmp
	movx	a, @dptr
	rl	a
	rl	a
	anl	a, #0xfc
	mov	r2, a
	mov	dptr, #_ptmp2
	movx	@dptr, a
	mov	dptr, #_ptmp+1
	movx	a, @dptr
	rl	a
	rl	a
	anl	a, #0xfc
	mov	r3, a
	mov	dptr, #_ptmp
	movx	a, @dptr
	swap	a
	rr	a
	rr	a
	anl	a, #0x03
	orl	a, r3
	mov	dptr, #_ptmp2+1
	movx	@dptr, a
	mov	dptr, #_ptmp+2
	movx	a, @dptr
	rl	a
	rl	a
	anl	a, #0xfc
	mov	r3, a
	mov	dptr, #_ptmp+1
	movx	a, @dptr
	swap	a
	rr	a
	rr	a
	anl	a, #0x03
	orl	a, r3
	mov	dptr, #_ptmp2+2
	movx	@dptr, a
	mov	dptr, #_ptmp+3
	movx	a, @dptr
	rl	a
	rl	a
	mov	r3, a
	mov	dptr, #_ptmp+2
	movx	a, @dptr
	swap	a
	rr	a
	rr	a
	anl	a, #0x03
	orl	a, r3
	mov	dptr, #_ptmp2+3
	movx	@dptr, a
	mov	dptr, #_ptmp
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_ptmp2
	movx	a, @dptr
	add	a, r2
	movx	@dptr, a
	mov	dptr, #_ptmp+1
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_ptmp2+1
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_ptmp+2
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_ptmp2+2
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_ptmp+3
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_ptmp2+3
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	ret

; _p5 = 5 * _pc
mul5_p5_from_pc:
	mov	dptr, #_pc
	movx	a, @dptr
	rl	a
	rl	a
	anl	a, #0xfc
	mov	r2, a
	mov	dptr, #_p5
	movx	@dptr, a
	mov	dptr, #_pc+1
	movx	a, @dptr
	rl	a
	rl	a
	anl	a, #0xfc
	mov	r3, a
	mov	dptr, #_pc
	movx	a, @dptr
	swap	a
	rr	a
	rr	a
	anl	a, #0x03
	orl	a, r3
	mov	dptr, #_p5+1
	movx	@dptr, a
	mov	dptr, #_pc+2
	movx	a, @dptr
	rl	a
	rl	a
	anl	a, #0xfc
	mov	r3, a
	mov	dptr, #_pc+1
	movx	a, @dptr
	swap	a
	rr	a
	rr	a
	anl	a, #0x03
	orl	a, r3
	mov	dptr, #_p5+2
	movx	@dptr, a
	mov	dptr, #_pc+3
	movx	a, @dptr
	rl	a
	rl	a
	mov	r3, a
	mov	dptr, #_pc+2
	movx	a, @dptr
	swap	a
	rr	a
	rr	a
	anl	a, #0x03
	orl	a, r3
	mov	dptr, #_p5+3
	movx	@dptr, a
	mov	dptr, #_pc
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_p5
	movx	a, @dptr
	add	a, r2
	movx	@dptr, a
	mov	dptr, #_pc+1
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_p5+1
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_pc+2
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_p5+2
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_pc+3
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_p5+3
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	ret

; set _paddr = _plo + (r5 * 4)
paddr_plo:
	mov	a, r5
	rl	a
	rl	a
	add	a, #_plo
	mov	r2, a
	clr	a
	addc	a, #(_plo >> 8)
	mov	r3, a
	mov	dptr, #_paddr
	mov	a, r2
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	ret

; set _paddr = _phi + (r5 * 4)
paddr_phi:
	mov	a, r5
	rl	a
	rl	a
	add	a, #_phi
	mov	r2, a
	clr	a
	addc	a, #(_phi >> 8)
	mov	r3, a
	mov	dptr, #_paddr
	mov	a, r2
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	ret

; copy _ptmp to the target passed in dptr
copy_ptmp:
	mov	r0, dpl
	mov	r1, dph
	mov	dptr, #_ptmp
	movx	a, @dptr
	mov	r2, a
	inc	dptr
	movx	a, @dptr
	mov	r3, a
	inc	dptr
	movx	a, @dptr
	mov	r6, a
	inc	dptr
	movx	a, @dptr
	mov	r7, a
	mov	dpl, r0
	mov	dph, r1
	mov	a, r2
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	inc	dptr
	mov	a, r6
	movx	@dptr, a
	inc	dptr
	mov	a, r7
	movx	@dptr, a
	ret

_poly1305_mul:
	; save ctx -> _pctx
	mov	dptr, #_pctx
	mov	a, dpl
	movx	@dptr, a
	inc	dptr
	mov	a, dph
	movx	@dptr, a

	; zero lo[5] and hi[5]
	mov	dptr, #_plo
	clr	a
	mov	r7, #40
zmul_loop:
	movx	@dptr, a
	inc	dptr
	djnz	r7, zmul_loop

	; _pi = 0
	mov	dptr, #_pi
	clr	a
	movx	@dptr, a
i_loop:
	; r0:r1 = ctx
	mov	dptr, #_pctx
	movx	a, @dptr
	mov	r0, a
	inc	dptr
	movx	a, @dptr
	mov	r1, a
	; dptr = ctx + 0x24 + _pi*4 ; load4 -> _pt
	mov	dptr, #_pi
	movx	a, @dptr
	rl	a
	rl	a
	add	a, #0x24
	mov	r2, a
	clr	a
	addc	a, #0
	mov	r3, a
	mov	a, r2
	add	a, r0
	mov	dpl, a
	mov	a, r3
	addc	a, r1
	mov	dph, a
	lcall	load4			; _pt = h[i]

	; al: byte0 = _pt+0, byte1 = (_pt+1) & 0x1f
	mov	dptr, #_pt
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_pt+1
	movx	a, @dptr
	anl	a, #0x1f
	mov	dptr, #_pal+1
	movx	@dptr, a
	mov	dptr, #_pal
	mov	a, r2
	movx	@dptr, a
	; ah: byte0 = (_pt+0>>5) | (_pt+1<<3)
	mov	dptr, #_pt
	movx	a, @dptr
	swap	a
	rr	a
	anl	a, #0x07
	mov	r2, a
	mov	dptr, #_pt+1
	movx	a, @dptr
	rl	a
	rl	a
	rl	a
	anl	a, #0xf8
	orl	a, r2
	mov	r3, a
	mov	dptr, #_pah
	movx	@dptr, a
	; ah: byte1 = (_pt+1>>5) | (_pt+2<<3)
	mov	dptr, #_pt+1
	movx	a, @dptr
	swap	a
	rr	a
	anl	a, #0x07
	mov	r2, a
	mov	dptr, #_pt+2
	movx	a, @dptr
	rl	a
	rl	a
	rl	a
	anl	a, #0xf8
	orl	a, r2
	mov	dptr, #_pah+1
	movx	@dptr, a

	; _pj = 0
	mov	dptr, #_pj
	clr	a
	movx	@dptr, a
j_loop:
	; r0:r1 = ctx
	mov	dptr, #_pctx
	movx	a, @dptr
	mov	r0, a
	inc	dptr
	movx	a, @dptr
	mov	r1, a
	; dptr = ctx + _pj*4 ; load4 -> _pt
	mov	dptr, #_pj
	movx	a, @dptr
	rl	a
	rl	a
	add	a, r0
	mov	dpl, a
	clr	a
	addc	a, r1
	mov	dph, a
	lcall	load4			; _pt = r[j]

	; bl: byte0 = _pt+0, byte1 = (_pt+1) & 0x1f
	mov	dptr, #_pt
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_pt+1
	movx	a, @dptr
	anl	a, #0x1f
	mov	dptr, #_pbl+1
	movx	@dptr, a
	mov	dptr, #_pbl
	mov	a, r2
	movx	@dptr, a
	; bh: byte0 = (_pt+0>>5) | (_pt+1<<3)
	mov	dptr, #_pt
	movx	a, @dptr
	swap	a
	rr	a
	anl	a, #0x07
	mov	r2, a
	mov	dptr, #_pt+1
	movx	a, @dptr
	rl	a
	rl	a
	rl	a
	anl	a, #0xf8
	orl	a, r2
	mov	r3, a
	mov	dptr, #_pbh
	movx	@dptr, a
	; bh: byte1 = (_pt+1>>5) | (_pt+2<<3)
	mov	dptr, #_pt+1
	movx	a, @dptr
	swap	a
	rr	a
	anl	a, #0x07
	mov	r2, a
	mov	dptr, #_pt+2
	movx	a, @dptr
	rl	a
	rl	a
	rl	a
	anl	a, #0xf8
	orl	a, r2
	mov	dptr, #_pbh+1
	movx	@dptr, a

	; m0 = al * bl
	mov	dptr, #_pal
	movx	a, @dptr
	mov	r4, a
	inc	dptr
	movx	a, @dptr
	mov	r5, a
	mov	dptr, #_pbl
	movx	a, @dptr
	mov	r6, a
	inc	dptr
	movx	a, @dptr
	mov	r7, a
	lcall	mul16
	mov	dptr, #_pm0
	lcall	copy_ptmp

	; m1 = al*bh + ah*bl
	mov	dptr, #_pal
	movx	a, @dptr
	mov	r4, a
	inc	dptr
	movx	a, @dptr
	mov	r5, a
	mov	dptr, #_pbh
	movx	a, @dptr
	mov	r6, a
	inc	dptr
	movx	a, @dptr
	mov	r7, a
	lcall	mul16
	mov	dptr, #_pm1
	lcall	copy_ptmp
	mov	dptr, #_pah
	movx	a, @dptr
	mov	r4, a
	inc	dptr
	movx	a, @dptr
	mov	r5, a
	mov	dptr, #_pbl
	movx	a, @dptr
	mov	r6, a
	inc	dptr
	movx	a, @dptr
	mov	r7, a
	lcall	mul16
	; _pm1 += _ptmp
	mov	dptr, #_ptmp
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_pm1
	movx	a, @dptr
	add	a, r2
	movx	@dptr, a
	mov	dptr, #_ptmp+1
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_pm1+1
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_ptmp+2
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_pm1+2
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	mov	dptr, #_ptmp+3
	movx	a, @dptr
	mov	r2, a
	mov	dptr, #_pm1+3
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a

	; m2 = ah * bh
	mov	dptr, #_pah
	movx	a, @dptr
	mov	r4, a
	inc	dptr
	movx	a, @dptr
	mov	r5, a
	mov	dptr, #_pbh
	movx	a, @dptr
	mov	r6, a
	inc	dptr
	movx	a, @dptr
	mov	r7, a
	lcall	mul16
	mov	dptr, #_pm2
	lcall	copy_ptmp

	; x = _pm1 & 0x1fff: x0 = _pm1+0, x1 = _pm1+1 & 0x1f
	; t = _pm0 + (x << 13)
	mov	dptr, #_pm1
	movx	a, @dptr
	mov	r4, a			; x0
	inc	dptr
	movx	a, @dptr
	anl	a, #0x1f
	mov	r5, a			; x1
	mov	a, r4
	anl	a, #0xe0
	mov	r0, a			; byte1
	mov	a, r4
	swap	a
	rr	a
	anl	a, #0x1f
	mov	r1, a
	mov	a, r5
	rl	a
	rl	a
	rl	a
	rl	a
	rl	a
	anl	a, #0xe0
	orl	a, r1
	mov	r2, a			; byte2
	mov	a, r5
	swap	a
	rr	a
	rr	a
	rr	a
	anl	a, #0x03
	mov	r3, a			; byte3
	; t = _pm0 + {0, r0, r2, r3}
	mov	dptr, #_pm0
	movx	a, @dptr
	mov	dptr, #_pt
	movx	@dptr, a
	inc	dptr
	movx	a, @dptr
	add	a, r0
	movx	@dptr, a
	inc	dptr
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	inc	dptr
	movx	a, @dptr
	addc	a, r3
	movx	@dptr, a

	; plo = t & 0x3ffffff -> _ptmp2
	mov	dptr, #_pt
	movx	a, @dptr
	mov	dptr, #_ptmp2
	movx	@dptr, a
	inc	dptr
	movx	a, @dptr
	movx	@dptr, a
	inc	dptr
	movx	a, @dptr
	anl	a, #0x3f
	movx	@dptr, a
	inc	dptr
	clr	a
	movx	@dptr, a

	; phi = _pm2 + (_pm1 >> 13) + (_pt >> 26) -> _ptmp
	; y0 = (_pm1+0>>5) | (_pm1+1<<3)
	mov	dptr, #_pm1
	movx	a, @dptr
	swap	a
	rr	a
	rr	a
	rr	a
	rr	a
	rr	a
	anl	a, #0x07
	mov	r0, a
	inc	dptr
	movx	a, @dptr
	rl	a
	rl	a
	rl	a
	anl	a, #0xf8
	orl	a, r0
	mov	r1, a			; y0
	; y1 = ((_pm1+1>>5) | (_pm1+2<<3)) & 0x1f
	mov	dptr, #_pm1+1
	movx	a, @dptr
	swap	a
	rr	a
	anl	a, #0x07
	mov	r0, a
	mov	dptr, #_pm1+2
	movx	a, @dptr
	rl	a
	rl	a
	rl	a
	anl	a, #0xf8
	orl	a, r0
	anl	a, #0x1f
	mov	r2, a			; y1
	; t >> 26 = (_pt+3 >> 2)
	mov	dptr, #_pt+3
	movx	a, @dptr
	clr	c
	rrc	a
	clr	c
	rrc	a
	anl	a, #0x03
	mov	r3, a
	; phi = _pm2 + {y0, y1, 0, r3}
	mov	dptr, #_pm2
	movx	a, @dptr
	mov	dptr, #_ptmp
	movx	@dptr, a
	inc	dptr
	movx	a, @dptr
	add	a, r1
	movx	@dptr, a
	inc	dptr
	movx	a, @dptr
	addc	a, r2
	movx	@dptr, a
	inc	dptr
	movx	a, @dptr
	addc	a, r3
	movx	@dptr, a

	; k = _pi + _pj; if k >= 5: k -= 5
	mov	dptr, #_pi
	movx	a, @dptr
	mov	r5, a
	mov	dptr, #_pj
	movx	a, @dptr
	add	a, r5
	mov	r5, a
	clr	c
	subb	a, #5
	jc	no_wrap
	mov	r5, a			; wrapped k
	; 5*plo -> _p5 ; lo[k] += _p5
	lcall	mul5_p5_from_ptmp2
	lcall	paddr_plo
	lcall	add_from_p5
	; 5*phi -> _ptmp2 ; hi[k] += _ptmp2
	lcall	mul5_ptmp2_from_ptmp
	lcall	paddr_phi
	lcall	add_from_ptmp2
	sjmp	do_acc_done
no_wrap:
	; lo[k] += plo (_ptmp2), hi[k] += phi (_ptmp)
	lcall	paddr_plo
	lcall	add_from_ptmp2
	lcall	paddr_phi
	lcall	add_from_ptmp
do_acc_done:

	mov	dptr, #_pj
	movx	a, @dptr
	inc	a
	movx	@dptr, a
	cjne	a, #5, j_again
	sjmp	j_exit
j_again:
	ljmp	j_loop
j_exit:
	mov	dptr, #_pi
	movx	a, @dptr
	inc	a
	movx	@dptr, a
	cjne	a, #5, i_again
	sjmp	i_exit
i_again:
	ljmp	i_loop
i_exit:

	; ---- normalize ----
	; limb 0: c = hi[0] + (lo[0]>>26); lo[0] &= mask; hi[0] = 0
	mov	dptr, #_plo+3
	movx	a, @dptr
	clr	c
	rrc	a
	clr	c
	rrc	a
	anl	a, #0x03
	mov	r2, a
	mov	dptr, #_phi
	movx	a, @dptr
	add	a, r2
	mov	r2, a
	mov	dptr, #_phi+1
	movx	a, @dptr
	addc	a, #0
	mov	r3, a
	mov	dptr, #_phi+2
	movx	a, @dptr
	addc	a, #0
	mov	r6, a
	mov	dptr, #_phi+3
	movx	a, @dptr
	addc	a, #0
	mov	r7, a
	mov	dptr, #_pc
	mov	a, r2
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	inc	dptr
	mov	a, r6
	movx	@dptr, a
	inc	dptr
	mov	a, r7
	movx	@dptr, a
	mov	dptr, #_plo+2
	movx	a, @dptr
	anl	a, #0x3f
	movx	@dptr, a
	mov	dptr, #_plo+3
	clr	a
	movx	@dptr, a
	mov	dptr, #_phi
	clr	a
	movx	@dptr, a
	inc	dptr
	movx	@dptr, a
	inc	dptr
	movx	@dptr, a
	inc	dptr
	movx	@dptr, a
	; limb 1: lo[1] += _pc; c = hi[1] + (lo[1]>>26); lo[1] &= mask
	mov	a, #_plo
	add	a, #4
	mov	r2, a
	clr	a
	addc	a, #(_plo >> 8)
	mov	r3, a
	mov	dptr, #_paddr
	mov	a, r2
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	lcall	add_from_pc
	mov	dptr, #_plo+7
	movx	a, @dptr
	clr	c
	rrc	a
	clr	c
	rrc	a
	anl	a, #0x03
	mov	r2, a
	mov	dptr, #_phi+4
	movx	a, @dptr
	add	a, r2
	mov	r2, a
	mov	dptr, #_phi+5
	movx	a, @dptr
	addc	a, #0
	mov	r3, a
	mov	dptr, #_phi+6
	movx	a, @dptr
	addc	a, #0
	mov	r6, a
	mov	dptr, #_phi+7
	movx	a, @dptr
	addc	a, #0
	mov	r7, a
	mov	dptr, #_pc
	mov	a, r2
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	inc	dptr
	mov	a, r6
	movx	@dptr, a
	inc	dptr
	mov	a, r7
	movx	@dptr, a
	mov	dptr, #_plo+6
	movx	a, @dptr
	anl	a, #0x3f
	movx	@dptr, a
	mov	dptr, #_plo+7
	clr	a
	movx	@dptr, a
	; limb 2
	mov	a, #_plo
	add	a, #8
	mov	r2, a
	clr	a
	addc	a, #(_plo >> 8)
	mov	r3, a
	mov	dptr, #_paddr
	mov	a, r2
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	lcall	add_from_pc
	mov	dptr, #_plo+11
	movx	a, @dptr
	clr	c
	rrc	a
	clr	c
	rrc	a
	anl	a, #0x03
	mov	r2, a
	mov	dptr, #_phi+8
	movx	a, @dptr
	add	a, r2
	mov	r2, a
	mov	dptr, #_phi+9
	movx	a, @dptr
	addc	a, #0
	mov	r3, a
	mov	dptr, #_phi+10
	movx	a, @dptr
	addc	a, #0
	mov	r6, a
	mov	dptr, #_phi+11
	movx	a, @dptr
	addc	a, #0
	mov	r7, a
	mov	dptr, #_pc
	mov	a, r2
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	inc	dptr
	mov	a, r6
	movx	@dptr, a
	inc	dptr
	mov	a, r7
	movx	@dptr, a
	mov	dptr, #_plo+10
	movx	a, @dptr
	anl	a, #0x3f
	movx	@dptr, a
	mov	dptr, #_plo+11
	clr	a
	movx	@dptr, a
	; limb 3
	mov	a, #_plo
	add	a, #12
	mov	r2, a
	clr	a
	addc	a, #(_plo >> 8)
	mov	r3, a
	mov	dptr, #_paddr
	mov	a, r2
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	lcall	add_from_pc
	mov	dptr, #_plo+15
	movx	a, @dptr
	clr	c
	rrc	a
	clr	c
	rrc	a
	anl	a, #0x03
	mov	r2, a
	mov	dptr, #_phi+12
	movx	a, @dptr
	add	a, r2
	mov	r2, a
	mov	dptr, #_phi+13
	movx	a, @dptr
	addc	a, #0
	mov	r3, a
	mov	dptr, #_phi+14
	movx	a, @dptr
	addc	a, #0
	mov	r6, a
	mov	dptr, #_phi+15
	movx	a, @dptr
	addc	a, #0
	mov	r7, a
	mov	dptr, #_pc
	mov	a, r2
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	inc	dptr
	mov	a, r6
	movx	@dptr, a
	inc	dptr
	mov	a, r7
	movx	@dptr, a
	mov	dptr, #_plo+14
	movx	a, @dptr
	anl	a, #0x3f
	movx	@dptr, a
	mov	dptr, #_plo+15
	clr	a
	movx	@dptr, a
	; limb 4
	mov	a, #_plo
	add	a, #16
	mov	r2, a
	clr	a
	addc	a, #(_plo >> 8)
	mov	r3, a
	mov	dptr, #_paddr
	mov	a, r2
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	lcall	add_from_pc
	mov	dptr, #_plo+19
	movx	a, @dptr
	clr	c
	rrc	a
	clr	c
	rrc	a
	anl	a, #0x03
	mov	r2, a
	mov	dptr, #_phi+16
	movx	a, @dptr
	add	a, r2
	mov	r2, a
	mov	dptr, #_phi+17
	movx	a, @dptr
	addc	a, #0
	mov	r3, a
	mov	dptr, #_phi+18
	movx	a, @dptr
	addc	a, #0
	mov	r6, a
	mov	dptr, #_phi+19
	movx	a, @dptr
	addc	a, #0
	mov	r7, a
	mov	dptr, #_pc
	mov	a, r2
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	inc	dptr
	mov	a, r6
	movx	@dptr, a
	inc	dptr
	mov	a, r7
	movx	@dptr, a
	mov	dptr, #_plo+18
	movx	a, @dptr
	anl	a, #0x3f
	movx	@dptr, a
	mov	dptr, #_plo+19
	clr	a
	movx	@dptr, a
	; wrap: lo[0] += 5*c
	lcall	mul5_p5_from_pc
	mov	a, #_plo
	mov	r2, a
	mov	a, #(_plo >> 8)
	mov	r3, a
	mov	dptr, #_paddr
	mov	a, r2
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	lcall	add_from_p5
	; hi[0] += lo[0] >> 26 ; lo[0] &= mask  (hi[0] was zeroed)
	mov	dptr, #_plo+3
	movx	a, @dptr
	clr	c
	rrc	a
	clr	c
	rrc	a
	anl	a, #0x03
	mov	r2, a
	mov	dptr, #_pc
	movx	@dptr, a
	inc	dptr
	clr	a
	movx	@dptr, a
	inc	dptr
	movx	@dptr, a
	inc	dptr
	movx	@dptr, a
	mov	dptr, #_plo+2
	movx	a, @dptr
	anl	a, #0x3f
	movx	@dptr, a
	mov	dptr, #_plo+3
	clr	a
	movx	@dptr, a
	; lo[1] += hi[0] (= _pc); lo[1] &= mask
	mov	a, #_plo
	add	a, #4
	mov	r2, a
	clr	a
	addc	a, #(_plo >> 8)
	mov	r3, a
	mov	dptr, #_paddr
	mov	a, r2
	movx	@dptr, a
	inc	dptr
	mov	a, r3
	movx	@dptr, a
	lcall	add_from_pc
	mov	dptr, #_plo+6
	movx	a, @dptr
	anl	a, #0x3f
	movx	@dptr, a
	mov	dptr, #_plo+7
	clr	a
	movx	@dptr, a

	; write back: h[i] = lo[i]
	mov	dptr, #_pi
	clr	a
	movx	@dptr, a
wb_loop:
	; r0:r1 = ctx + 0x24 + _pi*4
	mov	dptr, #_pctx
	movx	a, @dptr
	mov	r0, a
	inc	dptr
	movx	a, @dptr
	mov	r1, a
	mov	dptr, #_pi
	movx	a, @dptr
	rl	a
	rl	a
	add	a, #0x24
	mov	r2, a
	clr	a
	addc	a, #0
	mov	r3, a
	mov	a, r2
	add	a, r0
	mov	dpl, a
	mov	a, r3
	addc	a, r1
	mov	dph, a
	mov	a, dpl
	mov	r0, a
	mov	a, dph
	mov	r1, a		; r0:r1 = h[i]
	; dptr = _plo + _pi*4
	mov	dptr, #_pi
	movx	a, @dptr
	rl	a
	rl	a
	add	a, #_plo
	mov	dpl, a
	clr	a
	addc	a, #(_plo >> 8)
	mov	dph, a
	mov	r2, #4
wb_loop2:
	movx	a, @dptr
	mov	r3, a
	inc	dptr
	push	dpl
	push	dph
	mov	dpl, r0
	mov	dph, r1
	mov	a, r3
	movx	@dptr, a
	inc	r0
	mov	a, r0
	jnz	wb_nc
	inc	r1
wb_nc:
	pop	dph
	pop	dpl
	djnz	r2, wb_loop2
	mov	dptr, #_pi
	movx	a, @dptr
	inc	a
	movx	@dptr, a
	cjne	a, #5, wb_loop
	ret
