# Authentication model

Type: explanation · Topic: why and how password and PSK authentication work

The switch supports two independent authentication mechanisms.  Which
one is used depends on whether a **pre-shared key (PSK)** is configured.

## Password authentication (default)

When no PSK is configured, the management interface is protected by the
system password:

- the web UI and rtlpctl log in with `POST /login` and the `pwd` field
- the telnet console asks for the same password
- the session cookie gates the HTTP endpoints

This is the out-of-the-box behaviour and requires no setup.

## PSK mode (recommended)

Setting a pre-shared key (`preshared_key <64-hex>` on the console or
`rtlpctl psk <64-hex>`) switches the switch into **PSK mode**:

- **password logins are rejected** — `POST /login` with `pwd=` always
  fails
- the web UI and rtlpctl authenticate with an **encrypted challenge**:
  the client encrypts a fixed 12-byte string ("RTLP-LOGIN-1") with the
  PSK (ChaCha20-Poly1305) and posts `enc=<hex(nonce||ct||tag)>` to
  `/login`.  The PSK itself never leaves the client.
- a **replay guard** rejects a login that reuses the last accepted
  nonce
- the telnet console **keeps working with the password** (it is
  independent of PSK mode); it is recommended to disable it anyway
  (`telnet off`) once PSK mode is used, because telnet is unencrypted

### Why PSK mode?

The firmware deliberately keeps input validation and the management
plane as small as possible (see `rtlpctl`'s "Why a host-side CLI?"
section).  The PSK additionally protects the management traffic:

- after login, privileged operations (e.g. `commit`) go through the
  encrypted `/enc` endpoint, so command text and responses cannot be
  read or modified in transit
- the web UI switches all API calls to `/enc` automatically when a PSK
  is stored in the browser's localStorage

To configure PSK mode, see
[How-to: Set up PSK mode](how-to/psk-setup.md).

### PSK leak scenarios (accepted)

The PSK is a symmetric key that must exist on both sides:

- **initial setup** transmits the key once in plaintext (telnet or
  `/cmd`) — unavoidable, since the encryption does not exist yet
- the configuration file contains the key in plaintext
  (`preshared_key <64-hex>` line in `/config` and `/running-config`)
- the client side stores it (rtlpctl's `--psk`/`.env`, the browser's
  localStorage)

These are accepted: nothing but the key itself is ever protected by the
PSK, so the goal is that the key is **never leaked during normal
operation**.  With the encrypted login challenge and the `/enc`
endpoint, the PSK is not transmitted during operation, which is the
property that matters.
