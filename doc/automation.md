# Automation

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

    You can expect that server will close connection, without responding to request.
    Wait for SWITCH_IP to be responding again.

## Port status

In similar way to upload, you can fetch the json status of the ports.

1. Get the session cookie as for upload.
2. Hit the `/status.json` with cookie:

    ```bash
    curl -b cookies.txt http://${SWITCH_IP}/status.json
    ```
