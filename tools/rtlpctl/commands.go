package main

import (
	"fmt"
	"strconv"
	"strings"
)

func cmdLogin(client *Client, args []string, asJSON bool) error {
	if len(args) == 0 {
		if client.password == "" {
			return fmt.Errorf("usage: login <password>")
		}
	} else {
		client.password = args[0]
	}
	if err := client.Login(); err != nil {
		return err
	}
	fmt.Println("OK")
	return nil
}

func cmdStatus(client *Client, args []string, asJSON bool) error {
	data, err := client.GetJSON("/status.json")
	if err != nil {
		return err
	}
	ports, ok := data.([]interface{})
	if !ok {
		// some firmware returns object wrapper
		if m, ok2 := data.(map[string]interface{}); ok2 {
			if p, ok3 := m["ports"]; ok3 {
				ports, _ = p.([]interface{})
			}
		}
		if ports == nil {
			return fmt.Errorf("unexpected response format")
		}
	}
	fmtPortStatus(ports, asJSON)
	return nil
}

func cmdInfo(client *Client, args []string, asJSON bool) error {
	data, err := client.GetJSON("/information.json")
	if err != nil {
		return err
	}
	info, ok := data.(map[string]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	fmtInformation(info, asJSON)
	return nil
}

func cmdVLAN(client *Client, args []string, asJSON bool) error {
	if len(args) == 0 {
		return fmt.Errorf("usage: vlan <vid> | vlan list")
	}
	if args[0] == "list" {
		data, err := client.GetJSON("/vlanlist")
		if err != nil {
			return err
		}
		list, _ := data.([]interface{})
		fmtVLANList(list, asJSON)
		return nil
	}
	vid := args[0]
	if err := validateVLANID(vid); err != nil {
		return err
	}
	data, err := client.GetJSON(fmt.Sprintf("/vlan.json?vid=%s", vid))
	if err != nil {
		return err
	}
	vlan, ok := data.(map[string]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	fmt.Printf("VLAN %s:\n", vid)
	fmtVLAN(vlan, asJSON)
	return nil
}

func cmdCounters(client *Client, args []string, asJSON bool) error {
	if len(args) == 0 {
		return fmt.Errorf("usage: counters <port>")
	}
	if err := validateCountersPort(args[0]); err != nil {
		return err
	}
	port, err := strconv.Atoi(args[0])
	if err != nil {
		return fmt.Errorf("invalid port number: %s", args[0])
	}
	data, err := client.GetJSON(fmt.Sprintf("/counters.json?port=%d", port))
	if err != nil {
		return err
	}
	fmtCounters(port, data, asJSON)
	return nil
}

func cmdEEE(client *Client, args []string, asJSON bool) error {
	data, err := client.GetJSON("/eee.json")
	if err != nil {
		return err
	}
	ports, ok := data.([]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	fmtEEE(ports, asJSON)
	return nil
}

func cmdBandwidth(client *Client, args []string, asJSON bool) error {
	data, err := client.GetJSON("/bandwidth.json")
	if err != nil {
		return err
	}
	ports, ok := data.([]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	fmtBandwidth(ports, asJSON)
	return nil
}

func cmdMirror(client *Client, args []string, asJSON bool) error {
	data, err := client.GetJSON("/mirror.json")
	if err != nil {
		return err
	}
	mirror, ok := data.(map[string]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	fmtMirror(mirror, asJSON)
	return nil
}

func cmdLAG(client *Client, args []string, asJSON bool) error {
	data, err := client.GetJSON("/lag.json")
	if err != nil {
		return err
	}
	lags, ok := data.([]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	fmtLAG(lags, asJSON)
	return nil
}

func cmdMTU(client *Client, args []string, asJSON bool) error {
	data, err := client.GetJSON("/mtu.json")
	if err != nil {
		return err
	}
	ports, ok := data.([]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	fmtMTU(ports, asJSON)
	return nil
}

func cmdL2(client *Client, args []string, asJSON bool) error {
	if len(args) > 0 && args[0] == "delete" {
		if len(args) < 2 {
			return fmt.Errorf("usage: l2 delete <idx>")
		}
		if err := validateL2Idx(args[1]); err != nil {
			return err
		}
		data, err := client.GetJSON(fmt.Sprintf("/l2_del.json?idx=%s", args[1]))
		if err != nil {
			return err
		}
		result, ok := data.(map[string]interface{})
		if !ok {
			return fmt.Errorf("unexpected response format")
		}
		fmtL2Delete(result, asJSON)
		return nil
	}
	idx := "0"
	if len(args) > 0 && args[0] != "get" {
		idx = args[0]
	} else if len(args) > 1 && args[0] == "get" {
		idx = args[1]
	}
	if err := validateL2Idx(idx); err != nil {
		return err
	}
	data, err := client.GetJSON(fmt.Sprintf("/l2.json?idx=%s", idx))
	if err != nil {
		return err
	}
	entries, ok := data.([]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	fmtL2(entries, asJSON)
	return nil
}

func cmdConfig(client *Client, args []string, asJSON bool) error {
	if len(args) > 0 && args[0] == "upload" {
		if len(args) < 2 {
			return fmt.Errorf("usage: config upload <file>")
		}
		if err := validateFile(args[1]); err != nil {
			return err
		}
		return client.UploadFile("/config", "configuration", args[1])
	}
	text, err := client.GetText("/config")
	if err != nil {
		return err
	}
	if asJSON {
		printJSON(map[string]string{"config": text})
	} else {
		fmt.Print(text)
		if !strings.HasSuffix(text, "\n") {
			fmt.Println()
		}
	}
	return nil
}

func cmdCmdLog(client *Client, args []string, asJSON bool) error {
	if len(args) > 0 && args[0] == "clear" {
		_, err := client.GetText("/cmd_log_clear")
		if err != nil {
			return err
		}
		fmt.Println("OK")
		return nil
	}
	text, err := client.GetText("/cmd_log")
	if err != nil {
		return err
	}
	if asJSON {
		printJSON(map[string]string{"cmd_log": text})
	} else {
		fmt.Print(text)
		if !strings.HasSuffix(text, "\n") {
			fmt.Println()
		}
	}
	return nil
}

// cmdCmd sends a CLI command.  The command text is validated locally first
// because the firmware has minimal input validation; --force bypasses this.
func cmdCmd(client *Client, args []string, asJSON bool, force bool) error {
	if len(args) == 0 {
		return fmt.Errorf("usage: cmd <command-text>")
	}
	cmdText := strings.Join(args, " ")
	if err := validateCmdText(cmdText); err != nil {
		if !force {
			return fmt.Errorf("validation: %w (use --force to send anyway)", err)
		}
	}
	err := client.PostRaw("/cmd", "text/plain", strings.NewReader(cmdText))
	if err != nil {
		return err
	}
	fmt.Println("OK")
	return nil
}

// sendConsoleCmd posts a CLI command whose output appears on the switch's
// serial console (the /cmd endpoint only acknowledges execution, so there
// is nothing to render here).
func sendConsoleCmd(client *Client, cmdText string) error {
	if err := client.PostRaw("/cmd", "text/plain", strings.NewReader(cmdText)); err != nil {
		return err
	}
	fmt.Println("OK (output appears on the switch console)")
	return nil
}

// cmdShow fetches the running/startup config and the ARP cache.
// The configs are served as text by the firmware (/running-config and
// /config); the ARP cache is only available on the switch console.
func cmdShow(client *Client, args []string, asJSON bool) error {
	if len(args) == 0 {
		return fmt.Errorf("usage: show running-config|startup-config|arp")
	}
	switch args[0] {
	case "running-config", "startup-config":
		path := "/running-config"
		if args[0] == "startup-config" {
			path = "/config"
		}
		text, err := client.GetText(path)
		if err != nil {
			return err
		}
		if asJSON {
			printJSON(map[string]string{args[0]: text})
		} else {
			fmt.Print(text)
			if !strings.HasSuffix(text, "\n") {
				fmt.Println()
			}
		}
		return nil
	case "arp":
		return sendConsoleCmd(client, "show arp")
	}
	return fmt.Errorf("unknown show target: %q (use running-config, startup-config, arp)", args[0])
}

// cmdPing runs the switch's ICMP echo sender.  The command text is
// validated locally first; the ping output appears on the switch's
// serial console (the /cmd endpoint only acknowledges execution).
func cmdPing(client *Client, args []string, asJSON bool) error {
	if len(args) != 1 {
		return fmt.Errorf("usage: ping <ip-address>")
	}
	if err := validateIPAddr(args[0]); err != nil {
		return err
	}
	return sendConsoleCmd(client, "ping "+args[0])
}

// cmdLldp controls LLDP (IEEE 802.1AB neighbor discovery).
// "lldp show" prints the neighbor table on the switch console.
func cmdLldp(client *Client, args []string, asJSON bool) error {
	cmdText := "lldp"
	if len(args) > 0 {
		switch args[0] {
		case "on", "off", "show":
			cmdText = "lldp " + args[0]
		default:
			return fmt.Errorf("usage: lldp [on|off|show]")
		}
	}
	if len(args) > 1 {
		return fmt.Errorf("usage: lldp [on|off|show]")
	}
	return sendConsoleCmd(client, cmdText)
}

// cmdIgmp controls IGMP snooping, the HW querier and MLD snooping
// (Tier 2/3).  Output appears on the switch console.
func cmdIgmp(client *Client, args []string, asJSON bool) error {
	cmdText := "igmp"
	switch len(args) {
	case 0:
	case 1:
		switch args[0] {
		case "on", "off", "show":
			cmdText = "igmp " + args[0]
		default:
			return fmt.Errorf("usage: igmp [on|off|show|querier on|off|show|mld on|off|show]")
		}
	case 2:
		switch args[0] {
		case "querier", "mld":
			switch args[1] {
			case "on", "off", "show":
				cmdText = "igmp " + args[0] + " " + args[1]
			default:
				return fmt.Errorf("usage: igmp %s on|off|show", args[0])
			}
		default:
			return fmt.Errorf("usage: igmp [on|off|show|querier on|off|show|mld on|off|show]")
		}
	default:
		return fmt.Errorf("usage: igmp [on|off|show|querier on|off|show|mld on|off|show]")
	}
	return sendConsoleCmd(client, cmdText)
}

// cmdStorm controls storm control (Tier 3).  The argument list mirrors
// the device CLI; it is validated locally before sending.
func cmdStorm(client *Client, args []string, asJSON bool) error {
	if len(args) == 0 {
		return fmt.Errorf("usage: storm-control on|off|status")
	}
	cmdText := "storm-control " + strings.Join(args, " ")
	if err := validateCmdText(cmdText); err != nil {
		return err
	}
	return sendConsoleCmd(client, cmdText)
}

// cmdQos controls QoS priority (Tier 3).  The argument list mirrors the
// device CLI; it is validated locally before sending.
func cmdQos(client *Client, args []string, asJSON bool) error {
	cmdText := "qos"
	if len(args) > 0 {
		cmdText = "qos " + strings.Join(args, " ")
	}
	if err := validateCmdText(cmdText); err != nil {
		return err
	}
	return sendConsoleCmd(client, cmdText)
}

// cmdAcl controls ingress ACL rules (Tier 3).  The argument list mirrors
// the device CLI; it is validated locally before sending.
func cmdAcl(client *Client, args []string, asJSON bool) error {
	if len(args) == 0 {
		return fmt.Errorf("usage: acl on|off|show|add|del")
	}
	cmdText := "acl " + strings.Join(args, " ")
	if err := validateCmdText(cmdText); err != nil {
		return err
	}
	return sendConsoleCmd(client, cmdText)
}

// ---- Dedicated subcommands for the remaining device CLI commands ----
// Each validates locally (the firmware has minimal input validation) and
// sends the command via /cmd; output appears on the switch console.

// cmdDeviceCmd validates and sends a one-line device CLI command.
func cmdDeviceCmd(client *Client, name string, args []string, usage string) error {
	if len(args) == 0 {
		return fmt.Errorf("usage: %s", usage)
	}
	cmdText := name + " " + strings.Join(args, " ")
	if err := validateCmdText(cmdText); err != nil {
		return err
	}
	return sendConsoleCmd(client, cmdText)
}

func cmdHostname(client *Client, args []string, asJSON bool) error {
	if len(args) != 1 {
		return fmt.Errorf("usage: hostname <name>")
	}
	if err := validateDeviceHostname(args[0]); err != nil {
		return err
	}
	return sendConsoleCmd(client, "hostname "+args[0])
}

func cmdPasswd(client *Client, args []string, asJSON bool) error {
	if len(args) != 1 {
		return fmt.Errorf("usage: passwd <new-password>")
	}
	if err := validatePassword(args[0]); err != nil {
		return err
	}
	return sendConsoleCmd(client, "passwd "+args[0])
}

func cmdIP(client *Client, args []string, asJSON bool) error {
	if len(args) != 1 || args[0] != "dhcp" {
		if len(args) != 1 {
			return fmt.Errorf("usage: ip <a.b.c.d> | ip dhcp")
		}
		if err := validateIPAddr(args[0]); err != nil {
			return err
		}
	}
	return sendConsoleCmd(client, "ip "+args[0])
}

func cmdGW(client *Client, args []string, asJSON bool) error {
	if len(args) != 1 {
		return fmt.Errorf("usage: gw <a.b.c.d>")
	}
	if err := validateIPAddr(args[0]); err != nil {
		return err
	}
	return sendConsoleCmd(client, "gw "+args[0])
}

func cmdNetmask(client *Client, args []string, asJSON bool) error {
	if len(args) != 1 {
		return fmt.Errorf("usage: netmask <a.b.c.d>")
	}
	if err := validateIPAddr(args[0]); err != nil {
		return err
	}
	return sendConsoleCmd(client, "netmask "+args[0])
}

func cmdPort(client *Client, args []string, asJSON bool) error {
	if len(args) < 2 {
		return fmt.Errorf("usage: port <n> show|name|on|off|duplex|speed|auto")
	}
	if err := validatePortNum(args[0], 9); err != nil {
		return err
	}
	p := args[0]
	switch args[1] {
	case "show":
		return sendConsoleCmd(client, "port "+p+" show")
	case "name":
		if len(args) != 3 {
			return fmt.Errorf("usage: port <n> name <name>")
		}
		if err := validateDeviceHostname(args[2]); err != nil {
			return err
		}
		return sendConsoleCmd(client, "port "+p+" name "+args[2])
	case "on", "off":
		return sendConsoleCmd(client, "port "+p+" "+args[1])
	case "duplex":
		if len(args) != 3 {
			return fmt.Errorf("usage: port <n> duplex <half|full>")
		}
		if err := validateDuplex(args[2]); err != nil {
			return err
		}
		return sendConsoleCmd(client, "port "+p+" duplex "+args[2])
	default:
		if err := validateSpeedWord(args[1]); err != nil {
			return err
		}
		return sendConsoleCmd(client, "port "+p+" "+args[1])
	}
}

func cmdPvid(client *Client, args []string, asJSON bool) error {
	if len(args) != 2 {
		return fmt.Errorf("usage: pvid <port> <vid>")
	}
	if err := validatePortNum(args[0], 9); err != nil {
		return err
	}
	if err := validateVLANID(args[1]); err != nil {
		return err
	}
	return sendConsoleCmd(client, "pvid "+args[0]+" "+args[1])
}

func cmdIngress(client *Client, args []string, asJSON bool) error {
	return cmdDeviceCmd(client, "ingress", args, "ingress [ports...]")
}

func cmdIsolate(client *Client, args []string, asJSON bool) error {
	return cmdDeviceCmd(client, "isolate", args, "isolate <port> [ports...] | isolate <port> off|show")
}

func cmdLaghash(client *Client, args []string, asJSON bool) error {
	return cmdDeviceCmd(client, "laghash", args, "laghash <hash> [smac|dmac|spa|sip|dip|sport|dport]")
}

func cmdStp(client *Client, args []string, asJSON bool) error {
	return cmdDeviceCmd(client, "stp", args, "stp [on|off|show]")
}

func cmdTelnet(client *Client, args []string, asJSON bool) error {
	if len(args) != 1 || (args[0] != "on" && args[0] != "off") {
		return fmt.Errorf("usage: telnet on|off")
	}
	return sendConsoleCmd(client, "telnet "+args[0])
}

func cmdWeb(client *Client, args []string, asJSON bool) error {
	if len(args) != 1 || (args[0] != "on" && args[0] != "off") {
		return fmt.Errorf("usage: web on|off")
	}
	return sendConsoleCmd(client, "web "+args[0])
}

func cmdCommit(client *Client, args []string, asJSON bool) error {
	if len(args) != 0 {
		return fmt.Errorf("usage: commit")
	}
	return sendConsoleCmd(client, "commit")
}

// cmdSfp covers the SFP feature family.  Sensitive operations (save,
// restore, fix, patch, clone, checksum --fix, write) accept --pw <hex8>;
// the token is validated but passed through to the firmware as-is.
func cmdSfp(client *Client, args []string, asJSON bool) error {
	if len(args) == 0 {
		return fmt.Errorf("usage: sfp [1|2] [1g|2g5|10g|100m|auto|describe|dump|save|restore|fix|patch|clone|checksum|write|bulk]")
	}
	cmdText := "sfp " + strings.Join(args, " ")
	if err := validateCmdText(cmdText); err != nil {
		return err
	}
	return sendConsoleCmd(client, cmdText)
}

// cmdPsk sets the device-side preshared key for the encrypted /enc
// endpoint (console command preshared_key).  Use --psk/--env-file to
// configure the local key for enc-cmd instead.
func cmdPsk(client *Client, args []string, asJSON bool) error {
	if len(args) != 1 {
		return fmt.Errorf("usage: psk <64-hex-chars>")
	}
	if err := validatePSK(args[0]); err != nil {
		return err
	}
	return sendConsoleCmd(client, "preshared_key "+args[0])
}

func cmdRegget(client *Client, args []string, asJSON bool) error {
	return cmdDeviceCmd(client, "regget", args, "regget <addr>")
}

func cmdRegset(client *Client, args []string, asJSON bool) error {
	return cmdDeviceCmd(client, "regset", args, "regset <addr> <hexvalue>")
}

func cmdSdsget(client *Client, args []string, asJSON bool) error {
	return cmdDeviceCmd(client, "sdsget", args, "sdsget <bank> <page> <reg>")
}

func cmdSdsset(client *Client, args []string, asJSON bool) error {
	return cmdDeviceCmd(client, "sdsset", args, "sdsset <bank> <page> <reg> <hexvalue>")
}

func cmdPhyget(client *Client, args []string, asJSON bool) error {
	return cmdDeviceCmd(client, "phyget", args, "phyget <port> <addr> <reg>")
}

func cmdPhyset(client *Client, args []string, asJSON bool) error {
	return cmdDeviceCmd(client, "physet", args, "physet <port> <addr> <reg> <hexvalue>")
}

// cmdEnc sends a command through the encrypted /enc endpoint (PSK auth).
func cmdEnc(client *Client, args []string, asJSON bool, force bool) error {
	if len(args) == 0 {
		return fmt.Errorf("usage: enc-cmd <command-text>")
	}
	if len(client.psk) != aeadKeyLen {
		return fmt.Errorf("pre-shared key not configured: set RTLP_PSK=<64-hex-chars> or use --psk")
	}
	cmdText := strings.Join(args, " ")
	if err := validateCmdText(cmdText); err != nil {
		if !force {
			return fmt.Errorf("validation: %w (use --force to send anyway)", err)
		}
	}
	respText, err := client.PostEnc(cmdText)
	if err != nil {
		return err
	}
	fmt.Println(respText)
	return nil
}

func cmdEncAPI(client *Client, args []string, asJSON bool) error {
	if len(args) == 0 {
		return fmt.Errorf("usage: enc-api <path> (e.g. /status.json)")
	}
	if len(client.psk) != aeadKeyLen {
		return fmt.Errorf("pre-shared key not configured: set RTLP_PSK=<64-hex-chars> or use --psk")
	}
	path := args[0]
	if path[0] != '/' {
		path = "/" + path
	}
	respText, err := client.PostEnc("api " + path)
	if err != nil {
		return err
	}
	fmt.Println(respText)
	return nil
}

func cmdUpload(client *Client, args []string, asJSON bool) error {
	if len(args) < 2 {
		return fmt.Errorf("usage: upload firmware <file>")
	}
	if args[0] != "firmware" {
		return fmt.Errorf("unknown upload type: %s (use: firmware)", args[0])
	}
	if err := validateFile(args[1]); err != nil {
		return err
	}
	return client.UploadFile("/upload", "uploadedfile", args[1])
}

func cmdReset(client *Client, args []string, asJSON bool) error {
	// The firmware closes the connection without sending an HTTP response,
	// so a response is not expected (nor required) here.
	resp, err := client.get("/reset")
	if err == nil {
		resp.Body.Close()
	}
	fmt.Println("reset command sent")
	return nil
}

func cmdSfpDiag(client *Client, args []string, asJSON bool) error {
	data, err := client.GetJSON("/sfp_diag.json")
	if err != nil {
		return err
	}
	ports, ok := data.([]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	fmtSfpDiag(ports, asJSON)
	return nil
}

func printHelp() {
	fmt.Println(`Usage: rtlpctl [global flags] <command> [args...]

Global flags:
  --host HOST         Switch IP address (env: RTLP_HOST, default: 192.168.1.1)
  --password PASS     Login password (env: RTLP_PASSWORD)
  --psk HEX64         Pre-shared key for /enc (env: RTLP_PSK, 64 hex chars)
  --env-file FILE     Load .env file (default: .env)
  --mode MODE         CLI mode: default or arista (env: MODE, default: default)
  --json              Output raw JSON
  --force             Bypass local command validation (cmd / enc-cmd)
  --help              Show this help

Commands (default mode):
  login <password>           Authenticate with the switch
  status                     Show port status and counters
  info                       Show system information
  vlan <vid>                 Show VLAN details (1-4094)
  vlan list                  List all VLANs
  counters <port>            Show port hardware counters (1-8)
  eee                        Show EEE configuration
  bandwidth                  Show bandwidth settings
  mirror                     Show port mirroring configuration
  lag                        Show link aggregation groups
  mtu                        Show per-port MTU settings
  sfp-diag                   Show SFP module diagnostics (DDM)
  l2 [idx]                   Show L2 forwarding table (decimal 0-4095)
  l2 delete <idx>            Delete an L2 table entry (decimal 0-4095)
  config                     Show running configuration
  config upload <file>       Upload configuration file
  cmd <text>                 Execute CLI command
  enc-cmd <text>             Execute CLI command via encrypted /enc (PSK)
  cmd-log                    Show command history log
  cmd-log clear              Clear command history
  upload firmware <file>     Upload firmware image
  reset                      Reboot the switch
  ping <ip>                  Send 4 ICMP echoes from the switch (v0.2.24+)
  lldp [on|off|show]         LLDP neighbor discovery (v0.2.24+)
  igmp [on|off|show]         IGMP snooping control (v0.2.24+)
  igmp querier [on|off|show] ASIC IGMP/MLD querier (v0.2.24+)
  igmp mld [on|off|show]     MLD snooping control (v0.2.24+)
  storm-control ...          Storm control: on <type> <rate>[k|p], off, status
  qos ...                    QoS: on|off|mode|pcp|dscp|sched|status
  acl ...                    ACL: on|off|add|del|show
  show running-config        Show the config 'commit' would save (console)
  show startup-config        Show the saved config from flash (console)
  show arp                   Show the switch ARP cache (console)
  hostname <name>            Set the switch hostname
  passwd <new>               Change the web/telnet password
  ip <a.b.c.d>|dhcp          Set the management IP (or use DHCP)
  gw <a.b.c.d>               Set the default gateway
  netmask <a.b.c.d>          Set the netmask
  port <n> ...               Port config: show|name|on|off|duplex|speed|auto
  pvid <port> <vid>          Set the port VLAN ID
  ingress [ports...]         Set 802.1Q ingress filtering (t = tagged-only)
  isolate <port> [ports...]  Port isolation; <port> off clears it
  laghash <hash> [fields]    LAG hash: 0-3 + smac|dmac|spa|sip|dip|sport|dport
  stp [on|off|show]          Spanning-tree protocol
  telnet on|off              Enable/disable the telnet console
  web on|off                 Enable/disable the web UI
  commit                     Save the running config to flash
  psk <hex64>                Set the device preshared key (encrypted /enc)
  sfp ...                    SFP module control (see below)
  regget <addr>              Read an RTL8370 register (hex)
  regset <addr> <hex>        Write an RTL8370 register
  sdsget <bank> <page> <reg> Read a register via SDS access
  sdsset <bank> <page> <reg> <hex>
                               Write a register via SDS access
  phyget <port> <addr> <reg> Read a PHY register
  physet <port> <addr> <reg> <hex>
                               Write a PHY register

SFP commands:
  sfp                          Show all SFP slots
  sfp [1|2] [1g|2g5|10g]       Set SFP speed (1g, 2g5, 10g, 100m, auto)
  sfp [1|2] dump               Hex dump of SFP EEPROM (0x00-0xFF)
  sfp [1|2] describe           Show vendor, model, serial, checksum status
  sfp [1|2] save               Save EEPROM to flash backup
  sfp [1|2] restore            Restore EEPROM from flash backup
  sfp [1|2] checksum [--fix]   Verify CC_BASE/CC_EXT; --fix rewrites them
  sfp [1|2] fix                Recode EEPROM for copper passthrough
  sfp [1|2] patch [--pw <hex8>]
                               Recode an FC (Fibre Channel) module to
                               Ethernet: byte 0x03=0x20 (10GBase-LR),
                               0x06=0x02, 0x07=0x00, 0x09=0x00, then fix
                               the checksum
  sfp [1|2] clone [--pw <hex8>]
                               Write all 256 bytes from the flash buffer
  sfp [1|2] write <off> <val> [--pw <hex8>]
                               Write one byte. <off> = EEPROM byte offset
                               (0x00-0xFF), <val> = byte value; both hex.
                               Example: "sfp 2 write 33 35" sets byte 0x33
                               to ASCII '5' (model "...-87" -> "...-85").
                               On success the affected checksum is updated
                               automatically (CC_BASE for 0x00-0x3E,
                               CC_EXT for 0x40-0x5E).
  sfp [1|2] bulk <512hexchars> Bulk-write all 256 EEPROM bytes (512 hex chars)

  --pw <hex8>: 8-hex-char EEPROM unlock password. If omitted or rejected,
               the firmware tries a plain write first, then falls back
               through its built-in password dictionary (39 entries,
               00000000 first).

Arista mode (--mode arista):
  Use Arista EOS-style commands (show interfaces status, show vlan, etc.)
  Combined with --json outputs EAPI-compatible JSON-RPC format.

Interactive mode:
  Run without arguments to enter interactive shell.`)
}
