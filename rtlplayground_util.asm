;
; Hand-optimised number parsing and output formatting helpers for the
; DW8051 core (RTL837x).  Replaces C implementations in cmd_parser.c,
; httpd.c, httpd/page_impl.c and rtlplayground.c.
;
; Calling convention follows what SDCC 4.5.0 generates for the prototypes:
;   - first parameter arrives in DPTR (a lone uint8_t arrives in DPL)
;   - remaining parameters arrive in the named PARM storage below
;   - uint8_t return values are delivered in DPL
;
; Placement mirrors the original C code: itoa/print_byte are called from
; several banks so they live in the home area (CSEG); the rest stay in the
; bank of their only caller.
;
	.globl	_atoi_byte
	.globl	_atoi_short
	.globl	_parse_short
	.globl	_string_to_html
	.globl	_itoa
	.globl	_print_byte

	.globl	_atoi_byte_PARM_2
	.globl	_atoi_short_PARM_2

;--------------------------------------------------------
; Parameter storage.  Must match the memory space (OSEG internal RAM) the
; C compiler generates for the callers of each function.
;--------------------------------------------------------
	.area	OSEG	(OVR,DATA)
_atoi_byte_PARM_2::
	.ds 1
_atoi_short_PARM_2::
	.ds 1

; Register address aliases for "push arN" / "pop arN" (bank 0)
	ar0 = 0x00
	ar1 = 0x01
	ar2 = 0x02
	ar3 = 0x03
	ar4 = 0x04
	ar5 = 0x05
	ar6 = 0x06
	ar7 = 0x07

;-------------------------------------------------------------------------
; void itoa(uint8_t v)
; Prints v as decimal (leading zeros suppressed) via write_char.
; v in DPL.  Home area: called from cmd_parser, cmd_editor and rtlplayground.
;-------------------------------------------------------------------------
	.area	CSEG	(CODE)
_itoa:
	mov	a, dpl
	mov	b, #0x64	; 100
	div	ab		; a = v/100, b = v%100
	mov	r5, a		; r5 = hundreds
	jz	itoa_tens
	mov	a, #0x30
	add	a, r5
	mov	dpl, a
	push	ar5
	lcall	_write_char
	pop	ar5
itoa_tens:
	mov	a, b		; v%100
	mov	b, #0x0a
	div	ab		; a = tens, b = ones
	mov	r4, a		; r4 = tens
	mov	r7, b		; r7 = ones
	mov	a, r5
	orl	a, r4		; print_zeros = hundreds | tens
	mov	r5, a
	jz	itoa_ones
	mov	a, #0x30
	add	a, r4
	mov	dpl, a
	push	ar5
	push	ar4
	push	ar7
	lcall	_write_char
	pop	ar7
	pop	ar4
	pop	ar5
itoa_ones:
	mov	a, #0x30
	add	a, r7
	mov	dpl, a
	lcall	_write_char
	ret

;-------------------------------------------------------------------------
; void print_byte(uint8_t a)
; Prints a as two lowercase hex digits via write_char.
; a in DPL.  Home area: used via debug macros from all banks.
;-------------------------------------------------------------------------
_print_byte:
	mov	r6, dpl		; save a
	; high nibble
	mov	a, r6
	swap	a
	anl	a, #0x0f
	add	a, #0x30	; + '0'
	mov	r7, a
	clr	c
	subb	a, #0x3a	; '9' + 1
	jc	print_byte_h_ok
	mov	a, r7
	add	a, #0x27	; 'a' - ('0' + 10)
	mov	r7, a
print_byte_h_ok:
	mov	a, r7
	mov	dpl, a
	push	ar6
	lcall	_write_char
	pop	ar6
	; low nibble
	mov	a, r6
	anl	a, #0x0f
	add	a, #0x30
	mov	r7, a
	clr	c
	subb	a, #0x3a
	jc	print_byte_l_ok
	mov	a, r7
	add	a, #0x27
	mov	r7, a
print_byte_l_ok:
	mov	a, r7
	mov	dpl, a
	lcall	_write_char
	ret

;-------------------------------------------------------------------------
; uint8_t parse_short(__xdata uint8_t *p)
; Parses *p.. as decimal into the global short_parsed (uint16).
; p in DPTR.  Returns 0 on success, 1 if no digit was found.
; BANK1: only called from httpd.c.
;-------------------------------------------------------------------------
	.area	BANK1	(CODE)
_parse_short:
	mov	r2, dpl
	mov	r3, dph		; r2:r3 = p
	mov	r5, #0x01	; err
	mov	r6, #0x00
	mov	r7, #0x00	; 16-bit accumulator
parse_short_loop:
	mov	dpl, r2
	mov	dph, r3
	movx	a, @dptr	; *p
	inc	dptr
	mov	r2, dpl
	mov	r3, dph
	add	a, #0xd0	; digit = *p - '0'
	mov	r1, a
	clr	c
	subb	a, #0x0a	; CY set iff digit <= 9
	jc	parse_short_digit
	; not a digit: store _short_parsed, return err
	mov	dptr, #_short_parsed
	mov	a, r6
	movx	@dptr, a
	inc	dptr
	mov	a, r7
	movx	@dptr, a
	mov	dpl, r5
	ret
parse_short_digit:
	mov	r5, #0x00
	; acc = acc*10 + digit (16-bit, correct modulo 65536 like the C uint16
	; accumulator; a carry out of the high byte is discarded, i.e. wraps)
	push	ar1		; save digit
	mov	a, r6
	mov	b, #0x0a
	mul	ab		; a = lo*10, b = carry_lo
	mov	r1, a		; r1 = lo*10 result
	push	b		; carry_lo
	mov	a, r7
	mov	b, #0x0a
	mul	ab		; a = hi*10, b = carry_hi (discarded: wraps)
	pop	b		; b = carry_lo
	add	a, b		; a = hi*10 + carry_lo
	mov	r7, a		; new acc high
	mov	a, r1
	pop	b		; b = digit
	add	a, b		; a = lo*10 + digit
	mov	r6, a		; new acc low
	mov	a, r7
	addc	a, #0x00	; propagate carry
	mov	r7, a
	sjmp	parse_short_loop

;-------------------------------------------------------------------------
; void string_to_html(__code char *s)
; Appends each char of s to outbuf[slen++] (globals in httpd.c).
; s in DPTR.  Inlines char_to_html to avoid a call per character.
; BANK1: only called from page_impl.c.
;-------------------------------------------------------------------------
_string_to_html:
string_to_html_loop:
	clr	a
	movc	a, @a+dptr	; *s
	jz	string_to_html_done
	mov	r2, a		; save char
	inc	dptr
	push	dpl
	push	dph		; save s
	; read slen (uint16)
	mov	dptr, #_slen
	movx	a, @dptr
	mov	r6, a
	inc	dptr
	movx	a, @dptr
	mov	r7, a
	; outbuf[slen] = r2
	mov	a, r6
	add	a, #_outbuf
	mov	dpl, a
	mov	a, r7
	addc	a, #(_outbuf >> 8)
	mov	dph, a
	mov	a, r2
	movx	@dptr, a
	; slen++
	inc	r6
	cjne	r6, #0x00, string_to_html_nowrap
	inc	r7
string_to_html_nowrap:
	mov	dptr, #_slen
	mov	a, r6
	movx	@dptr, a
	inc	dptr
	mov	a, r7
	movx	@dptr, a
	pop	dph
	pop	dpl		; restore s
	sjmp	string_to_html_loop
string_to_html_done:
	ret

;-------------------------------------------------------------------------
; uint8_t atoi_byte(__xdata uint8_t *out, uint8_t idx)
; Parses cmd_buffer[idx..] as decimal into *out (max 255).
; out in DPTR, idx in _atoi_byte_PARM_2.  Returns 0 on success, 1 on
; overflow or if no digit was found.
; BANK2: only called from cmd_parser.c.
;-------------------------------------------------------------------------
	.area	BANK2	(CODE)
_atoi_byte:
	mov	r2, dpl
	mov	r3, dph		; r2:r3 = out
	mov	r4, _atoi_byte_PARM_2	; idx
	mov	r5, #0x01	; err
	mov	r6, #0x00	; num
atoi_byte_loop:
	; c = cmd_buffer[idx]
	mov	a, r4
	add	a, #_cmd_buffer
	mov	dpl, a
	clr	a
	addc	a, #(_cmd_buffer >> 8)
	mov	dph, a
	movx	a, @dptr
	add	a, #0xd0	; digit = c - '0'
	mov	r1, a
	clr	c
	subb	a, #0x0a	; CY set iff digit <= 9
	jc	atoi_byte_digit
	; not a digit: *out = num, return err
	mov	dpl, r2
	mov	dph, r3
	mov	a, r6
	movx	@dptr, a
	mov	dpl, r5
	ret
atoi_byte_digit:
	mov	r5, #0x00
	; overflow: num > 25 || (num == 25 && digit > 5)
	mov	a, r6
	clr	c
	subb	a, #0x19	; CY set iff num < 25
	jc	atoi_byte_ok
	cjne	r6, #0x19, atoi_byte_overflow	; num != 25 -> num > 25
	mov	a, r1
	clr	c
	subb	a, #0x06	; CY set iff digit < 6
	jc	atoi_byte_ok
atoi_byte_overflow:
	mov	dpl, #0x01
	ret
atoi_byte_ok:
	; num = num*10 + digit  (num <= 25 so num*10 <= 250, no 8-bit overflow)
	mov	a, r6
	mov	b, #0x0a
	mul	ab
	add	a, r1
	mov	r6, a
	inc	r4
	sjmp	atoi_byte_loop

;-------------------------------------------------------------------------
; uint8_t atoi_short(__xdata uint16_t *vlan, uint8_t idx)
; Parses cmd_buffer[idx..] as decimal into *vlan (max 65535).
; vlan in DPTR, idx in _atoi_short_PARM_2.  Returns 0 on success, 1 on
; overflow or if no digit was found.
; BANK2: only called from cmd_parser.c.
;-------------------------------------------------------------------------
_atoi_short:
	mov	r2, dpl
	mov	r3, dph		; r2:r3 = vlan
	mov	r4, _atoi_short_PARM_2	; idx
	mov	r5, #0x01	; err
	; *vlan = 0
	mov	dpl, r2
	mov	dph, r3
	clr	a
	movx	@dptr, a
	inc	dptr
	movx	@dptr, a
	; 16-bit accumulator in r6:r7
	mov	r6, #0x00
	mov	r7, #0x00
atoi_short_loop:
	mov	a, r4
	add	a, #_cmd_buffer
	mov	dpl, a
	clr	a
	addc	a, #(_cmd_buffer >> 8)
	mov	dph, a
	movx	a, @dptr
	add	a, #0xd0	; digit = c - '0'
	mov	r1, a
	clr	c
	subb	a, #0x0a
	jc	atoi_short_digit
	; not a digit: *vlan = r6:r7, return err
	mov	dpl, r2
	mov	dph, r3
	mov	a, r6
	movx	@dptr, a
	inc	dptr
	mov	a, r7
	movx	@dptr, a
	mov	dpl, r5
	ret
atoi_short_digit:
	mov	r5, #0x00
	; Overflow check first (keeps acc <= 6553, so the 16-bit multiply below
	; can never generate a carry out of the high byte):
	;   *vlan > 6553(0x1999) || (*vlan == 6553 && digit > 5)
	mov	a, r7
	clr	c
	subb	a, #0x19
	jc	atoi_short_ok
	jnz	atoi_short_overflow
	mov	a, r6
	clr	c
	subb	a, #0x99
	jc	atoi_short_ok
	jnz	atoi_short_overflow
	mov	a, r1
	clr	c
	subb	a, #0x06
	jc	atoi_short_ok
atoi_short_overflow:
	mov	dpl, #0x01
	ret
atoi_short_ok:
	; acc = acc*10 + digit (16-bit, via two 8-bit multiplies; hi*10 <= 250)
	mov	a, r6
	mov	b, #0x0a
	mul	ab		; a = lo*10, b = carry
	mov	r0, a		; r0 = lo*10 result
	mov	r6, b		; r6 = carry (r6 recomputed below)
	mov	a, r7
	mov	b, #0x0a
	mul	ab		; a = hi*10
	add	a, r6		; + carry from lo
	mov	r7, a		; new acc high
	mov	a, r0
	add	a, r1		; lo*10 + digit
	mov	r6, a		; new acc low
	mov	a, r7
	addc	a, #0x00	; propagate carry
	mov	r7, a
	inc	r4
	sjmp	atoi_short_loop
