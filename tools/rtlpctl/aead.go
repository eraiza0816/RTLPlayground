package main

// ChaCha20-Poly1305 AEAD (RFC 8439) implemented with the Go standard library
// only, matching the firmware's /enc endpoint:
//   body: nonce[12] || ciphertext || tag[16]
//
// NOTE: this file is intentionally duplicated verbatim in
// tools/rtlplayground_exporter/aead.go. Keep both copies in sync; they
// implement the same AEAD as the firmware's crypto/*.c, which is validated
// against the RFC 8439 vectors by tools/aead_test.c (host build) and by
// the Go vectors in aead_test.go.

// ChaCha20-Poly1305 AEAD (RFC 8439) implemented with the Go standard library
// only, matching the firmware's /enc endpoint:
//   body: nonce[12] || ciphertext || tag[16]

import (
	"encoding/binary"
	"errors"
)

var (
	errKeyLen    = errors.New("invalid key length")
	errTagMismatch = errors.New("tag mismatch")
)

const (
	aeadKeyLen  = 32
	aeadNonceLen = 12
	aeadTagLen  = 16
)

// chacha20Block generates one 64-byte keystream block.
func chacha20Block(key []byte, nonce []byte, counter uint32, out []byte) {
	var s [16]uint32
	s[0] = 0x61707865
	s[1] = 0x3320646e
	s[2] = 0x79622d32
	s[3] = 0x6b206574
	for i := 0; i < 8; i++ {
		s[4+i] = binary.LittleEndian.Uint32(key[i*4:])
	}
	s[12] = counter
	for i := 0; i < 3; i++ {
		s[13+i] = binary.LittleEndian.Uint32(nonce[i*4:])
	}
	var w [16]uint32
	copy(w[:], s[:])
	for i := 0; i < 10; i++ {
		quarterRound(&w, 0, 4, 8, 12)
		quarterRound(&w, 1, 5, 9, 13)
		quarterRound(&w, 2, 6, 10, 14)
		quarterRound(&w, 3, 7, 11, 15)
		quarterRound(&w, 0, 5, 10, 15)
		quarterRound(&w, 1, 6, 11, 12)
		quarterRound(&w, 2, 7, 8, 13)
		quarterRound(&w, 3, 4, 9, 14)
	}
	for i := 0; i < 16; i++ {
		binary.LittleEndian.PutUint32(out[i*4:], w[i]+s[i])
	}
}

func quarterRound(w *[16]uint32, a, b, c, d int) {
	w[a] += w[b]
	w[d] ^= w[a]
	w[d] = rotl32(w[d], 16)
	w[c] += w[d]
	w[b] ^= w[c]
	w[b] = rotl32(w[b], 12)
	w[a] += w[b]
	w[d] ^= w[a]
	w[d] = rotl32(w[d], 8)
	w[c] += w[d]
	w[b] ^= w[c]
	w[b] = rotl32(w[b], 7)
}

func rotl32(v uint32, n uint) uint32 {
	return v<<n | v>>(32-n)
}

// chacha20XOR encrypts plaintext in place starting at the given block counter.
func chacha20XOR(key, nonce []byte, counter uint32, data []byte) {
	var block [64]byte
	for len(data) > 0 {
		chacha20Block(key, nonce, counter, block[:])
		counter++
		n := len(data)
		if n > 64 {
			n = 64
		}
		for i := 0; i < n; i++ {
			data[i] ^= block[i]
		}
		data = data[n:]
	}
}

// poly1305 with 26-bit limbs (64-bit intermediates).
type poly1305 struct {
	r   [5]uint32
	pad [4]uint32
	h   [5]uint32
	buf [16]byte
	left int
}

func newPoly1305(key []byte) *poly1305 {
	p := &poly1305{}
	p.r[0] = binary.LittleEndian.Uint32(key[0:]) & 0x3ffffff
	p.r[1] = binary.LittleEndian.Uint32(key[3:]) >> 2 & 0x3ffff03
	p.r[2] = binary.LittleEndian.Uint32(key[6:]) >> 4 & 0x3ffc0ff
	p.r[3] = binary.LittleEndian.Uint32(key[9:]) >> 6 & 0x3f03fff
	p.r[4] = binary.LittleEndian.Uint32(key[12:]) >> 8 & 0x00fffff
	for i := 0; i < 4; i++ {
		p.pad[i] = binary.LittleEndian.Uint32(key[16+i*4:])
	}
	return p
}

func (p *poly1305) update(data []byte) {
	if p.left > 0 {
		n := 16 - p.left
		if n > len(data) {
			n = len(data)
		}
		copy(p.buf[p.left:], data[:n])
		p.left += n
		data = data[n:]
		if p.left < 16 {
			return
		}
		p.blocks(p.buf[:], false)
		p.left = 0
	}
	if n := len(data) &^ 15; n > 0 {
		p.blocks(data[:n], false)
		data = data[n:]
	}
	if len(data) > 0 {
		copy(p.buf[:], data)
		p.left = len(data)
	}
}

func (p *poly1305) blocks(m []byte, final bool) {
	hibit := uint64(1) << 24
	if final {
		hibit = 0
	}
	r := p.r
	s1 := uint64(r[1]) * 5
	s2 := uint64(r[2]) * 5
	s3 := uint64(r[3]) * 5
	s4 := uint64(r[4]) * 5
	h := &p.h

	for len(m) >= 16 {
		h0 := uint64(h[0]) + uint64(binary.LittleEndian.Uint32(m[0:])&0x3ffffff)
		h1 := uint64(h[1]) + uint64(binary.LittleEndian.Uint32(m[3:])>>2&0x3ffffff)
		h2 := uint64(h[2]) + uint64(binary.LittleEndian.Uint32(m[6:])>>4&0x3ffffff)
		h3 := uint64(h[3]) + uint64(binary.LittleEndian.Uint32(m[9:])>>6&0x3ffffff)
		h4 := uint64(h[4]) + uint64(binary.LittleEndian.Uint32(m[12:])>>8) + hibit

		d0 := h0*uint64(r[0]) + h1*s4 + h2*s3 + h3*s2 + h4*s1
		d1 := h0*uint64(r[1]) + h1*uint64(r[0]) + h2*s4 + h3*s3 + h4*s2
		d2 := h0*uint64(r[2]) + h1*uint64(r[1]) + h2*uint64(r[0]) + h3*s4 + h4*s3
		d3 := h0*uint64(r[3]) + h1*uint64(r[2]) + h2*uint64(r[1]) + h3*uint64(r[0]) + h4*s4
		d4 := h0*uint64(r[4]) + h1*uint64(r[3]) + h2*uint64(r[2]) + h3*uint64(r[1]) + h4*uint64(r[0])

		c := d0 >> 26
		h[0] = uint32(d0 & 0x3ffffff)
		d1 += c
		c = d1 >> 26
		h[1] = uint32(d1 & 0x3ffffff)
		d2 += c
		c = d2 >> 26
		h[2] = uint32(d2 & 0x3ffffff)
		d3 += c
		c = d3 >> 26
		h[3] = uint32(d3 & 0x3ffffff)
		d4 += c
		c = d4 >> 26
		h[4] = uint32(d4 & 0x3ffffff)
		h[0] += uint32(c * 5)
		c = uint64(h[0]) >> 26
		h[0] &= 0x3ffffff
		h[1] += uint32(c)

		m = m[16:]
	}
}

func (p *poly1305) finish(tag []byte) {
	if p.left > 0 {
		p.buf[p.left] = 1
		for i := p.left + 1; i < 16; i++ {
			p.buf[i] = 0
		}
		p.blocks(p.buf[:], true)
	}
	h0, h1, h2, h3, h4 := uint64(p.h[0]), uint64(p.h[1]), uint64(p.h[2]), uint64(p.h[3]), uint64(p.h[4])

	c := h1 >> 26
	h1 &= 0x3ffffff
	h2 += c
	c = h2 >> 26
	h2 &= 0x3ffffff
	h3 += c
	c = h3 >> 26
	h3 &= 0x3ffffff
	h4 += c
	c = h4 >> 26
	h4 &= 0x3ffffff
	h0 += c * 5
	c = h0 >> 26
	h0 &= 0x3ffffff
	h1 += c

	g0 := h0 + 5
	c = g0 >> 26
	g0 &= 0x3ffffff
	g1 := h1 + c
	c = g1 >> 26
	g1 &= 0x3ffffff
	g2 := h2 + c
	c = g2 >> 26
	g2 &= 0x3ffffff
	g3 := h3 + c
	c = g3 >> 26
	g3 &= 0x3ffffff
	g4 := h4 + c - (1 << 26)

	mask := ^uint64(int64(g4) >> 63)
	g0 &= mask
	g1 &= mask
	g2 &= mask
	g3 &= mask
	g4 &= mask
	mask = ^mask
	h0 = h0&mask | g0
	h1 = h1&mask | g1
	h2 = h2&mask | g2
	h3 = h3&mask | g3
	h4 = h4&mask | g4

	words := [4]uint32{
		uint32(h0) | uint32(h1<<26),
		uint32(h1>>6) | uint32(h2<<20),
		uint32(h2>>12) | uint32(h3<<14),
		uint32(h3>>18) | uint32(h4<<8),
	}
	var carry uint64
	for i := 0; i < 4; i++ {
		sum := uint64(words[i]) + uint64(p.pad[i]) + carry
		binary.LittleEndian.PutUint32(tag[i*4:], uint32(sum))
		carry = sum >> 32
	}
}

// aeadEncrypt returns nonce || ciphertext || tag.
func aeadEncrypt(key, nonce, plaintext []byte) ([]byte, error) {
	if len(key) != aeadKeyLen || len(nonce) != aeadNonceLen {
		return nil, errKeyLen
	}
	// one-time poly1305 key: first 32 bytes of keystream block counter 0
	var block [64]byte
	var polyKey [32]byte
	chacha20Block(key, nonce, 0, block[:])
	copy(polyKey[:], block[:32])

	ct := make([]byte, len(plaintext))
	copy(ct, plaintext)
	chacha20XOR(key, nonce, 1, ct)

	tag := aeadTagCompute(polyKey[:], ct)
	out := make([]byte, 0, aeadNonceLen+len(ct)+aeadTagLen)
	out = append(out, nonce...)
	out = append(out, ct...)
	out = append(out, tag...)
	return out, nil
}

func aeadDecrypt(key, nonce, ciphertext, tag []byte) ([]byte, error) {
	if len(key) != aeadKeyLen || len(nonce) != aeadNonceLen {
		return nil, errKeyLen
	}
	var block [64]byte
	var polyKey [32]byte
	chacha20Block(key, nonce, 0, block[:])
	copy(polyKey[:], block[:32])

	comp := aeadTagCompute(polyKey[:], ciphertext)
	for i := range tag {
		if comp[i] != tag[i] {
			return nil, errTagMismatch
		}
	}
	pt := make([]byte, len(ciphertext))
	copy(pt, ciphertext)
	chacha20XOR(key, nonce, 1, pt)
	return pt, nil
}

// aeadTagCompute = Poly1305(ciphertext || pad16 || len(aad) || len(ct)), aad is empty.
func aeadTagCompute(polyKey, ciphertext []byte) []byte {
	p := newPoly1305(polyKey)
	p.update(ciphertext)
	if n := len(ciphertext) % 16; n != 0 {
		p.update(make([]byte, 16-n))
	}
	var lenbuf [8]byte
	p.update(lenbuf[:]) // aad length (0)
	binary.LittleEndian.PutUint64(lenbuf[:], uint64(len(ciphertext)))
	p.update(lenbuf[:])
	tag := make([]byte, aeadTagLen)
	p.finish(tag)
	return tag
}
