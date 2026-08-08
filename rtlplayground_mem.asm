;
; Hand-optimised memory/string functions for the DW8051 core (RTL837x)
; Replaces the C implementations formerly in rtlplayground.c
;
; Calling convention follows what SDCC 4.5.0 generates for the prototypes
; declared in rtl837x_common.h:
;   - first parameter arrives in DPTR
;   - remaining parameters arrive in the named PARM storage below
;
; XDATA destinations are written via "movx @R1" using the DW8051 MPAGE
; register (0x92) as the high address byte.  MPAGE is not used elsewhere in
; the firmware (the build does not use --xstack), so it is only saved
; defensively.
;
	.globl	_memcpy
	.globl	_memcpyc
	.globl	_memset
	.globl	_strtox
	.globl	_strlen
	.globl	_strlen_x
	.globl	_strcmp

	.globl	_memcpy_PARM_2
	.globl	_memcpy_PARM_3
	.globl	_memcpyc_PARM_2
	.globl	_memcpyc_PARM_3
	.globl	_memset_PARM_2
	.globl	_memset_PARM_3
	.globl	_strtox_PARM_2
	.globl	_strcmp_PARM_2
	.globl	_memcmp
	.globl	_is_mem_zero
	.globl	_memcmp_PARM_2
	.globl	_memcmp_PARM_3
	.globl	_is_mem_zero_PARM_2

	.equ	MPAGE, 0x92	; DW8051 page register for MOVX @Ri

;--------------------------------------------------------
; Parameter storage.  The memory space of every parameter (OSEG internal
; RAM for plain/register parameters, XSEG for __xdata-qualified ones) must
; match what the C compiler generates for the callers, otherwise the
; direct vs. MOVX accesses would disagree.
;--------------------------------------------------------
	.area	OSEG	(OVR,DATA)
_memcpyc_PARM_2::
	.ds 2
_strtox_PARM_2::
	.ds 2
_strcmp_PARM_2::
	.ds 2

	.area	XSEG	(XDATA)
_memcpyc_PARM_3::
	.ds 2
_memset_PARM_3::
	.ds 1
_memcpy_PARM_3::
	.ds 2
_memcpy_PARM_2::
	.ds 2
_memset_PARM_2::
	.ds 1
_memcmp_PARM_2::
	.ds 2
_memcmp_PARM_3::
	.ds 1
_is_mem_zero_PARM_2::
	.ds 1

	.area	CSEG	(CODE)

;-------------------------------------------------------------------------
; void memcpy(__xdata void *dst, __xdata const void *src, uint16_t len)
; dst in DPTR, src in _memcpy_PARM_2 (XDATA), len in _memcpy_PARM_3 (DATA)
;-------------------------------------------------------------------------
_memcpy:
	push	MPAGE
	mov	a, dph
	mov	MPAGE, a		; MPAGE:R1 = dst
	mov	r1, dpl
	mov	dptr, #_memcpy_PARM_2
	movx	a, @dptr
	mov	r4, a
	inc	dptr
	movx	a, @dptr
	mov	r5, a			; r4:r5 = src
	mov	dptr, #_memcpy_PARM_3
	movx	a, @dptr
	mov	r6, a
	inc	dptr
	movx	a, @dptr
	mov	r7, a	; r6:r7 = len
	mov	dpl, r4
	mov	dph, r5			; DPTR = src
memcpy_loop:
	mov	a, r6
	orl	a, r7
	jz	memcpy_done
	movx	a, @dptr
	inc	dptr
	movx	@r1, a
	inc	r1
	cjne	r1, #0x00, memcpy_skip_page
	inc	MPAGE
memcpy_skip_page:
	dec	r6
	cjne	r6, #0xff, memcpy_loop
	dec	r7
	sjmp	memcpy_loop
memcpy_done:
	pop	MPAGE
	ret

;-------------------------------------------------------------------------
; void memcpyc(__xdata uint8_t *dst, __code uint8_t *src, uint16_t len)
; dst in DPTR, src in _memcpyc_PARM_2 (DATA), len in _memcpyc_PARM_3 (DATA)
;-------------------------------------------------------------------------
_memcpyc:
	push	MPAGE
	mov	a, dph
	mov	MPAGE, a		; MPAGE:R1 = dst
	mov	r1, dpl
	mov	r4, _memcpyc_PARM_2
	mov	r5, (_memcpyc_PARM_2 + 1)
	mov	dptr, #_memcpyc_PARM_3
	movx	a, @dptr
	mov	r6, a
	inc	dptr
	movx	a, @dptr
	mov	r7, a
	mov	dpl, r4
	mov	dph, r5			; DPTR = src (code)
memcpyc_loop:
	mov	a, r6
	orl	a, r7
	jz	memcpyc_done
	clr	a
	movc	a, @a+dptr
	inc	dptr
	movx	@r1, a
	inc	r1
	cjne	r1, #0x00, memcpyc_skip_page
	inc	MPAGE
memcpyc_skip_page:
	dec	r6
	cjne	r6, #0xff, memcpyc_loop
	dec	r7
	sjmp	memcpyc_loop
memcpyc_done:
	pop	MPAGE
	ret

;-------------------------------------------------------------------------
; void memset(__xdata uint8_t *dst, __xdata uint8_t v, uint8_t len)
; dst in DPTR, v in _memset_PARM_2 (XDATA), len in _memset_PARM_3 (DATA)
;-------------------------------------------------------------------------
_memset:
	push	MPAGE
	mov	a, dph
	mov	MPAGE, a		; MPAGE:R1 = dst
	mov	r1, dpl
	mov	dptr, #_memset_PARM_2
	movx	a, @dptr
	mov	r5, a			; value
	mov	dptr, #_memset_PARM_3
	movx	a, @dptr
	mov	r4, a	; len (8 bit)
	mov	a, r4
	jz	memset_done
	mov	a, r5
memset_loop:
	movx	@r1, a
	inc	r1
	cjne	r1, #0x00, memset_skip_page
	inc	MPAGE
memset_skip_page:
	djnz	r4, memset_loop
memset_done:
	pop	MPAGE
	ret

;-------------------------------------------------------------------------
; uint16_t strtox(__xdata uint8_t *dst, __code const char *s)
; dst in DPTR, s in _strtox_PARM_2 (DATA).  Returns length in DPL/DPH.
;-------------------------------------------------------------------------
_strtox:
	push	MPAGE
	mov	a, dph
	mov	MPAGE, a		; MPAGE:R1 = dst
	mov	r1, dpl
	mov	r4, _strtox_PARM_2
	mov	r5, (_strtox_PARM_2 + 1)
	mov	r6, #0x00		; length low
	mov	r7, #0x00		; length high
	mov	dpl, r4
	mov	dph, r5			; DPTR = s (code)
strtox_loop:
	clr	a
	movc	a, @a+dptr
	jz	strtox_done
	movx	@r1, a
	inc	r1
	cjne	r1, #0x00, strtox_skip_page
	inc	MPAGE
strtox_skip_page:
	inc	dptr
	inc	r6
	cjne	r6, #0x00, strtox_loop
	inc	r7
	sjmp	strtox_loop
strtox_done:
	movx	@r1, a			; NUL-terminate (A is 0)
	pop	MPAGE
	mov	dpl, r6
	mov	dph, r7
	ret

;-------------------------------------------------------------------------
; uint16_t strlen(__code const char *s)
; s in DPTR.  Returns length in DPL/DPH.
;-------------------------------------------------------------------------
_strlen:
	mov	r6, #0x00
	mov	r7, #0x00
strlen_loop:
	clr	a
	movc	a, @a+dptr
	jz	strlen_done
	inc	dptr
	inc	r6
	cjne	r6, #0x00, strlen_loop
	inc	r7
	sjmp	strlen_loop
strlen_done:
	mov	dpl, r6
	mov	dph, r7
	ret

;-------------------------------------------------------------------------
; uint16_t strlen_x(__xdata const char *s)
; s in DPTR.  Returns length in DPL/DPH.
;-------------------------------------------------------------------------
_strlen_x:
	mov	r6, #0x00
	mov	r7, #0x00
strlenx_loop:
	movx	a, @dptr
	jz	strlenx_done
	inc	dptr
	inc	r6
	cjne	r6, #0x00, strlenx_loop
	inc	r7
	sjmp	strlenx_loop
strlenx_done:
	mov	dpl, r6
	mov	dph, r7
	ret

;-------------------------------------------------------------------------
; char strcmp(__xdata const uint8_t *a, __code const uint8_t *b)
; a in DPTR, b in _strcmp_PARM_2 (DATA).  Returns -1/0/1 in DPL (SDCC
; returns 8-bit values in DPL, not in the accumulator).
;-------------------------------------------------------------------------
_strcmp:
	push	MPAGE
	mov	a, dph
	mov	MPAGE, a		; MPAGE:R1 = a (paged XDATA)
	mov	r1, dpl
	mov	r4, _strcmp_PARM_2
	mov	r5, (_strcmp_PARM_2 + 1)
	mov	dpl, r4
	mov	dph, r5			; DPTR = b (code)
strcmp_loop:
	clr	a
	movc	a, @a+dptr		; b[i]
	jz	strcmp_bzero
	mov	b, a			; save b[i]
	movx	a, @r1			; a[i]
	clr	c
	subb	a, b			; a[i] - b[i]
	jz	strcmp_advance		; equal -> advance both
	jc	strcmp_less		; a[i] < b[i] -> -1
strcmp_greater:
	mov	dpl, #0x01		; a[i] > b[i] -> +1
	pop	MPAGE
	ret
strcmp_advance:
	inc	r1
	cjne	r1, #0x00, strcmp_advance_b
	inc	MPAGE
strcmp_advance_b:
	inc	dptr
	sjmp	strcmp_loop
strcmp_bzero:
	movx	a, @r1			; b[i] == 0: compare a[i] against 0
	jnz	strcmp_greater		; a[i] > 0 -> +1
strcmp_equal:
	mov	dpl, #0x00		; equal -> 0
	pop	MPAGE
	ret
strcmp_less:
	mov	dpl, #0xff		; a[i] < b[i] -> -1
	pop	MPAGE
	ret

;-------------------------------------------------------------------------
; bool memcmp(__xdata uint8_t *dst, __xdata uint8_t *src, uint8_t len)
; "equals" helper: returns 1 if dst[0..len) == src[0..len) (the uip ARP
; table checks use this instead of 16-bit loads).
; dst in DPTR, src in _memcmp_PARM_2 (XDATA), len in _memcmp_PARM_3 (XDATA)
;-------------------------------------------------------------------------
_memcmp:
	push	MPAGE
	mov	a, dph
	mov	MPAGE, a		; MPAGE:R1 = dst
	mov	r1, dpl
	mov	dptr, #_memcmp_PARM_2
	movx	a, @dptr
	mov	r4, a
	inc	dptr
	movx	a, @dptr
	mov	r5, a			; r4:r5 = src
	mov	dptr, #_memcmp_PARM_3
	movx	a, @dptr
	mov	r6, a			; r6 = len
	mov	dpl, r4
	mov	dph, r5			; DPTR = src
memcmp_loop:
	mov	a, r6
	jz	memcmp_eq
	movx	a, @dptr
	inc	dptr
	mov	r7, a
	movx	a, @r1
	inc	r1
	cjne	r1, #0x00, memcmp_page_ok
	inc	MPAGE
memcmp_page_ok:
	xrl	a, r7
	jnz	memcmp_ne
	dec	r6
	sjmp	memcmp_loop
memcmp_ne:
	clr	a
	sjmp	memcmp_ret
memcmp_eq:
	mov	a, #1
memcmp_ret:
	pop	MPAGE
	ret

;-------------------------------------------------------------------------
; bool is_mem_zero(__xdata uint8_t *src, uint8_t len)
; Returns 1 if all bytes in src[0..len) are zero (ARP table entry free
; check).
; src in DPTR, len in _is_mem_zero_PARM_2 (XDATA)
;-------------------------------------------------------------------------
_is_mem_zero:
	push	MPAGE
	mov	a, dph
	mov	MPAGE, a		; MPAGE:R1 = src
	mov	r1, dpl
	mov	dptr, #_is_mem_zero_PARM_2
	movx	a, @dptr
	mov	r6, a			; r6 = len
is_zero_loop:
	mov	a, r6
	jz	is_zero_all
	movx	a, @r1
	inc	r1
	cjne	r1, #0x00, is_zero_page_ok
	inc	MPAGE
is_zero_page_ok:
	jnz	is_zero_not
	dec	r6
	sjmp	is_zero_loop
is_zero_not:
	clr	a
	sjmp	is_zero_ret
is_zero_all:
	mov	a, #1
is_zero_ret:
	pop	MPAGE
	ret
