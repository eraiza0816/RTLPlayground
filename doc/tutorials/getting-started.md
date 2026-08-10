# Tutorial: Getting started with RTLPlayground

Type: tutorial · Learning goal: get a freshly flashed switch up and running

This lesson walks you through the first minutes with an RTLPlayground
switch.  It assumes the firmware is already installed (see
[How-to: Installing](../how-to/installation.md)).

## Step 1 — Boot and connect

Power the switch.  On first boot it defaults to:

- IP address `192.168.10.247`
- web password `1234`

Verify the switch answers:

```
ping 192.168.10.247
```

> [!NOTE]
> If you put an `ip` line into `config.txt` before compiling, the switch
> uses that address instead.

## Step 2 — Log in to the web interface

Open `http://192.168.10.247` in a browser and log in with `1234`.
You should see the dashboard with the port overview.

Checkpoint: the dashboard shows your ports and their link status.

## Step 3 — Try the command line with rtlpctl

[rtlpctl](../../tools/rtlpctl) is the recommended way to operate the
switch.  Build it, then:

```
rtlpctl --host 192.168.10.247 --password 1234 info
rtlpctl --host 192.168.10.247 --password 1234 status
```

Checkpoint: `info` shows the model, firmware version and network
settings; `status` shows the port states.

## Step 4 — Make a first change

Change the switch hostname:

```
rtlpctl --host 192.168.10.247 --password 1234 hostname my-switch
rtlpctl --host 192.168.10.247 --password 1234 commit
```

`commit` saves the setting to flash so it survives a reboot.

Checkpoint: `rtlpctl ... info` now shows `my-switch`.

## Step 5 — Secure the switch (recommended)

The default password is public.  Set your own password, and consider the
pre-shared-key (PSK) mode which rejects password logins and encrypts the
management traffic:

```
rtlpctl --host 192.168.10.247 --password 1234 passwd <your-password>
rtlpctl --host 192.168.10.247 --password 1234 telnet off
```

For PSK mode see [Authentication](../authentication.md).

Checkpoint: the old password no longer works.

## You are done

You have a running, configured switch.  From here:

- explore the features (VLAN, QoS, storm control, ACL...) in
  the [documentation index](../../README.md#documentation)
- operate the switch with [rtlpctl](../../tools/rtlpctl)
