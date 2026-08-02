/**
 * ChaCha20-Poly1305 AEAD (RFC 8439) - pure JavaScript implementation.
 * No crypto.subtle dependency, so it works over plain HTTP.
 * All inputs/outputs are Uint8Array.  Poly1305 uses BigInt (modern browsers).
 */
var RTLAEAD = (function() {
  'use strict';

  function rotl32(x, n) {
    return ((x << n) | (x >>> (32 - n))) >>> 0;
  }

  function quarterRound(s, a, b, c, d) {
    s[a] = (s[a] + s[b]) >>> 0;
    s[d] = rotl32(s[d] ^ s[a], 16);
    s[c] = (s[c] + s[d]) >>> 0;
    s[b] = rotl32(s[b] ^ s[c], 12);
    s[a] = (s[a] + s[b]) >>> 0;
    s[d] = rotl32(s[d] ^ s[a], 8);
    s[c] = (s[c] + s[d]) >>> 0;
    s[b] = rotl32(s[b] ^ s[c], 7);
  }

  var SIGMA = [0x61707865, 0x3320646e, 0x79622d32, 0x6b206574];

  function chacha20Block(key, counter, nonce, out32) {
    var s = new Uint32Array(16);
    var t = new Uint32Array(16);
    var i;
    s[0] = SIGMA[0]; s[1] = SIGMA[1]; s[2] = SIGMA[2]; s[3] = SIGMA[3];
    for (i = 0; i < 8; i++)
      s[4 + i] = (key[4 * i] | (key[4 * i + 1] << 8) | (key[4 * i + 2] << 16) | (key[4 * i + 3] << 24)) >>> 0;
    s[12] = counter >>> 0;
    s[13] = nonce[0] | (nonce[1] << 8) | (nonce[2] << 16) | (nonce[3] << 24);
    s[14] = nonce[4] | (nonce[5] << 8) | (nonce[6] << 16) | (nonce[7] << 24);
    s[15] = nonce[8] | (nonce[9] << 8) | (nonce[10] << 16) | (nonce[11] << 24);
    t.set(s);
    for (i = 0; i < 10; i++) {
      quarterRound(t, 0, 4, 8, 12);
      quarterRound(t, 1, 5, 9, 13);
      quarterRound(t, 2, 6, 10, 14);
      quarterRound(t, 3, 7, 11, 15);
      quarterRound(t, 0, 5, 10, 15);
      quarterRound(t, 1, 6, 11, 12);
      quarterRound(t, 2, 7, 8, 13);
      quarterRound(t, 3, 4, 9, 14);
    }
    for (i = 0; i < 16; i++) out32[i] = (t[i] + s[i]) >>> 0;
  }

  function chacha20XOR(key, nonce, counter, data) {
    var out = new Uint8Array(data.length);
    var block = new Uint32Array(16);
    var i, j;
    for (i = 0; i < data.length; i += 64) {
      chacha20Block(key, counter, nonce, block);
      counter = (counter + 1) >>> 0;
      for (j = 0; j < 64 && i + j < data.length; j++) {
        out[i + j] = data[i + j] ^ ((block[j >>> 2] >>> ((j & 3) * 8)) & 0xff);
      }
    }
    return out;
  }

  /* Poly1305 one-time authentication (RFC 8439, BigInt arithmetic). */
  var P1305 = (1n << 130n) - 5n;
  var RCLAMP = 0x0ffffffc0ffffffc0ffffffc0fffffffn;

  function poly1305(key, msg) {
    var r = 0n, s = 0n, i;
    for (i = 0; i < 16; i++) r = (r << 8n) | BigInt(key[15 - i]);
    r &= RCLAMP;
    for (i = 0; i < 16; i++) s = (s << 8n) | BigInt(key[31 - i]);

    var h = 0n;
    var off = 0;
    while (off < msg.length) {
      var n = 0n;
      var blen = Math.min(16, msg.length - off);
      for (i = blen - 1; i >= 0; i--)
        n = (n << 8n) | BigInt(msg[off + i]);
      n |= 1n << BigInt(8 * blen); /* RFC 8439: append a 0x01 byte */
      h = (((h + n) * r) % P1305 + P1305) % P1305;
      off += 16;
    }
    h = h + s;

    var tag = new Uint8Array(16);
    for (i = 0; i < 16; i++) {
      tag[i] = Number(h & 0xffn);
      h >>= 8n;
    }
    return tag;
  }

  /* RFC 8439 AEAD tag: Poly1305(otk, aad || pad16 || ct || pad16 ||
   *   LE64(len(aad)) || LE64(len(ct))) */
  function aeadTag(otk, aad, ct) {
    var aadLen = aad ? aad.length : 0;
    var total = aadLen + (aadLen & 15 ? 16 - (aadLen & 15) : 0) +
                ct.length + (ct.length & 15 ? 16 - (ct.length & 15) : 0) + 16;
    var buf = new Uint8Array(total);
    var o = 0, i;
    if (aad) { buf.set(aad, o); o += aadLen; }
    o += aadLen & 15 ? 16 - (aadLen & 15) : 0;
    buf.set(ct, o); o += ct.length;
    o += ct.length & 15 ? 16 - (ct.length & 15) : 0;
    /* LE64 lengths (aadLen, ct.length are <= 16 bits here) */
    buf[o++] = aadLen & 0xff;
    buf[o++] = (aadLen >>> 8) & 0xff;
    for (i = 0; i < 6; i++) buf[o++] = 0;
    buf[o++] = ct.length & 0xff;
    buf[o++] = (ct.length >>> 8) & 0xff;
    for (i = 0; i < 6; i++) buf[o++] = 0;
    return poly1305(otk, buf);
  }

  /* -------- public API -------- */
  function oneTimeKey(chachaKey, nonce) {
    var block = new Uint32Array(16);
    chacha20Block(chachaKey, 0, nonce, block);
    var k = new Uint8Array(32);
    var i;
    for (i = 0; i < 32; i++) k[i] = (block[i >>> 2] >>> ((i & 3) * 8)) & 0xff;
    return k;
  }

  function encrypt(key, nonce, plaintext, aad) {
    var otk = oneTimeKey(key, nonce);
    var ct = chacha20XOR(key, nonce, 1, plaintext);
    var tag = aeadTag(otk, aad, ct);
    return { ct: ct, tag: tag };
  }

  function decrypt(key, nonce, ct, tag, aad) {
    var otk = oneTimeKey(key, nonce);
    var expect = aeadTag(otk, aad, ct);
    var i, ok = 1;
    for (i = 0; i < 16; i++) if (expect[i] !== tag[i]) ok = 0;
    if (!ok) return null;
    return chacha20XOR(key, nonce, 1, ct);
  }

  /* hex helpers */
  function toHex(b) {
    var s = '', i;
    for (i = 0; i < b.length; i++) s += (b[i] < 16 ? '0' : '') + b[i].toString(16);
    return s;
  }
  function fromHex(h) {
    var b = new Uint8Array(h.length / 2), i;
    for (i = 0; i < b.length; i++) b[i] = parseInt(h.substr(i * 2, 2), 16);
    return b;
  }

  return { encrypt: encrypt, decrypt: decrypt, toHex: toHex, fromHex: fromHex };
})();
