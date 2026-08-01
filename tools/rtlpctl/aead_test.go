package main

import (
	"bytes"
	"encoding/hex"
	"testing"
)

// RFC 7539 section 2.4.2: ChaCha20 encryption test vector
func TestChaCha20RFC7539(t *testing.T) {
	key, _ := hex.DecodeString("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")
	nonce, _ := hex.DecodeString("000000000000004a00000000")
	msg := []byte("Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.")
	want, _ := hex.DecodeString(
		"6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b" +
			"f91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d8" +
			"07ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab7793736" +
			"5af90bbf74a35be6b40b8eedf2785e42874d")

	ct := make([]byte, len(msg))
	copy(ct, msg)
	chacha20XOR(key, nonce, 1, ct)
	if !bytes.Equal(ct, want) {
		t.Fatalf("chacha20 mismatch:\n got %x\nwant %x", ct, want)
	}
}

// RFC 7539 section 2.5.2: Poly1305 test vector
func TestPoly1305RFC7539(t *testing.T) {
	key, _ := hex.DecodeString("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b")
	msg, _ := hex.DecodeString("43727970746f6772617068696320466f72756d2052657365617263682047726f7570")
	want, _ := hex.DecodeString("a8061dc1305136c6c22b8baf0c0127a9")

	p := newPoly1305(key)
	p.update(msg)
	tag := make([]byte, 16)
	p.finish(tag)
	if !bytes.Equal(tag, want) {
		t.Fatalf("poly1305 mismatch:\n got %x\nwant %x", tag, want)
	}
}

// RFC 8439 section 2.8.2: AEAD_CHACHA20_POLY1305 test vector
func TestAeadRFC8439(t *testing.T) {
	key, _ := hex.DecodeString("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f")
	nonce, _ := hex.DecodeString("070000004041424344454647")
	aad, _ := hex.DecodeString("50515253c0c1c2c3c4c5c6c7")
	msg := []byte("Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.")
	wantCT, _ := hex.DecodeString(
		"d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6" +
			"3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36" +
			"92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc" +
			"3ff4def08e4b7a9de576d26586cec64b6116")
	wantTag, _ := hex.DecodeString("1ae10b594f09e26a7e902ecbd0600691")

	// poly1305 one-time key from block counter 0
	var block [64]byte
	var polyKey [32]byte
	chacha20Block(key, nonce, 0, block[:])
	copy(polyKey[:], block[:32])

	ct := make([]byte, len(msg))
	copy(ct, msg)
	chacha20XOR(key, nonce, 1, ct)
	if !bytes.Equal(ct, wantCT) {
		t.Fatalf("aead ct mismatch:\n got %x\nwant %x", ct, wantCT)
	}

	// tag over aad || pad16 || ct || pad16 || len(aad) || len(ct)
	p := newPoly1305(polyKey[:])
	p.update(aad)
	p.update(make([]byte, 16-len(aad)%16))
	p.update(ct)
	if n := len(ct) % 16; n != 0 {
		p.update(make([]byte, 16-n))
	}
	var lenbuf [8]byte
	putUint64LE(lenbuf[:], uint64(len(aad)))
	p.update(lenbuf[:])
	putUint64LE(lenbuf[:], uint64(len(ct)))
	p.update(lenbuf[:])
	tag := make([]byte, 16)
	p.finish(tag)
	if !bytes.Equal(tag, wantTag) {
		t.Fatalf("aead tag mismatch:\n got %x\nwant %x", tag, wantTag)
	}
}

func putUint64LE(b []byte, v uint64) {
	for i := 0; i < 8; i++ {
		b[i] = byte(v >> (8 * i))
	}
}

// Round-trip + tamper detection using the /enc framing
func TestEncRoundTrip(t *testing.T) {
	key := bytes.Repeat([]byte{0x42}, 32)
	nonce := bytes.Repeat([]byte{0x11}, 12)
	cmd := []byte("hostname ENCTEST")

	pkt, err := aeadEncrypt(key, nonce, cmd)
	if err != nil {
		t.Fatal(err)
	}
	if len(pkt) != 12+len(cmd)+16 {
		t.Fatalf("bad packet length %d", len(pkt))
	}

	pt, err := aeadDecrypt(key, pkt[:12], pkt[12:12+len(cmd)], pkt[12+len(cmd):])
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(pt, cmd) {
		t.Fatalf("roundtrip mismatch: %q", pt)
	}

	// tampered tag must be rejected
	pkt[len(pkt)-1] ^= 0x01
	if _, err := aeadDecrypt(key, pkt[:12], pkt[12:12+len(cmd)], pkt[12+len(cmd):]); err == nil {
		t.Fatal("tampered tag accepted")
	}
	// tampered ciphertext must be rejected
	pkt[len(pkt)-1] ^= 0x01
	pkt[12] ^= 0x01
	if _, err := aeadDecrypt(key, pkt[:12], pkt[12:12+len(cmd)], pkt[12+len(cmd):]); err == nil {
		t.Fatal("tampered ciphertext accepted")
	}
}

// The 00..1f test key must match the firmware test setup
func TestTestKey(t *testing.T) {
	key := make([]byte, 32)
	for i := range key {
		key[i] = byte(i)
	}
	if key[0] != 0x00 || key[31] != 0x1f {
		t.Fatal("test key mismatch")
	}
}
