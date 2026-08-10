# Automation

Type: how-to · Task: automate the switch over telnet/HTTP

## CLI via Telnet

If Telnet is enabled (`telnet on`), CLI commands can be automated via Telnet:

```bash
# Send a single command and read response
echo -e "show\n" | nc -w 2 ${SWITCH_IP} 23
```

Or with password authentication using `expect`:
```bash
expect -c '
set timeout 5
spawn telnet '${SWITCH_IP}'
expect "Password:"
send "'${PASSWORD}'\r"
expect ">"
send "show\r"
expect ">"
send "commit\r"
expect ">"
send "exit\r"
'
```

> **Testing without hardware:** the bundled telnet CLI simulator
> (`tools/output/cli_sim`, built by `make -C tools`) speaks the same protocol
> with the same command set (Lite variant) and keeps state (ports, VLANs,
> hostname) for the lifetime of the process. Point the scripts at
> `127.0.0.1 2323` instead of the switch to test them offline:
> `cli_sim [port]` (default port 2323, password `1234`).

## Firmware Upload

You can automate upload of the firmware via WEB with curl:

1. Authorize with /login endpoint and save cookie:

   ```bash
   curl -c cookies.txt  http://${SWITCH_IP}/login -d pwd=${PASSWORD} -i
   ```

   This will save session cookie in cookies.txt
2. Send the firmware via form:

    ```bash
    curl -b cookies.txt http://${SWITCH_IP}/upload -F "uploadedfile=@${FIRMWARE_FILE_PATH}" -i
    ```

    On success the server responds `HTTP/1.1 200 OK` with body `OK` and then
    resets (the connection drops). On a CRC16 mismatch it responds
    `400 Bad Request` with body `CRC mismatch`.
    Wait for SWITCH_IP to be responding again after the reset.

## Port status

In similar way to upload, you can fetch the json status of the ports.

1. Get the session cookie as for upload.
2. Hit the `/status.json` with cookie:

    ```bash
    curl -b cookies.txt http://${SWITCH_IP}/status.json
    ```
