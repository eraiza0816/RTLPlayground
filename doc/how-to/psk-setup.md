# How-to: Set up PSK mode

Type: how-to · Task: configure the pre-shared key and switch to PSK mode

Prerequisites: a running switch and a way to execute commands on it
(serial console, telnet or rtlpctl).

## Steps

1. Set the pre-shared key.  The serial console is recommended so the key
   never crosses the network:

   ```
   preshared_key <64-hex>
   commit
   ```

   or via rtlpctl (note: this transmits the key once, in plaintext):

   ```
   rtlpctl psk <64-hex>
   rtlpctl commit
   ```

2. From now on, password logins are rejected.  Use the PSK everywhere:

   ```
   rtlpctl --psk <64-hex> status          # login + all calls via /enc
   rtlpctl --psk <64-hex> commit
   ```

3. The web UI login page accepts the PSK (64 hex chars); it is stored in
   the browser's localStorage and used for the encrypted session and API
   calls.

4. Telnet keeps working with the password.  It is recommended to disable
   it, since telnet is unencrypted:

   ```
   telnet off
   ```

## Expected result

- `rtlpctl --psk <key> status` works, `rtlpctl --password ...` is rejected
- the web UI logs in with the PSK
- all privileged operations travel through the encrypted `/enc` endpoint

## See also

- [Authentication model](../authentication.md)
