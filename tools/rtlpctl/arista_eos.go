package main

import (
	"encoding/json"
	"fmt"
	"os"
	"strconv"
	"strings"
	"sync/atomic"
	"text/tabwriter"
)

var eapiID int64

func nextEAPIID() int {
	return int(atomic.AddInt64(&eapiID, 1))
}

func eapiResult(output interface{}, encoding string) {
	printJSON(map[string]interface{}{
		"jsonrpc": "2.0",
		"id":      nextEAPIID(),
		"result": []map[string]interface{}{
			{"encoding": encoding, "output": output},
		},
	})
}

func eapiError(msg string) {
	printJSON(map[string]interface{}{
		"jsonrpc": "2.0",
		"id":      nextEAPIID(),
		"error":   map[string]interface{}{"code": -1, "message": msg},
	})
}

func matchCmd(input, full string) bool {
	input = strings.ToLower(strings.TrimSpace(input))
	full = strings.ToLower(strings.TrimSpace(full))
	if len(input) == 0 || len(full) == 0 {
		return false
	}
	if len(input) > len(full) {
		return false
	}
	return full[:len(input)] == input
}

func eosCmpEthernet(s string) string {
	s = strings.ToLower(s)
	s = strings.TrimPrefix(s, "ethernet")
	s = strings.TrimPrefix(s, "et")
	return strings.TrimSpace(s)
}

func eosPortName(portNum interface{}) string {
	return fmt.Sprintf("Et%s", fmtInt(portNum))
}

func eosLinkStatus(link interface{}) string {
	if fmtLink(link) == "down" {
		return "notconnect"
	}
	return "connected"
}

func runAristaCmd(client *Client, args []string, jsonMode bool) error {
	if len(args) == 0 {
		return nil
	}
	if client.password != "" && !matchCmd(args[0], "login") {
		if err := client.Login(); err != nil {
			return fmt.Errorf("login failed: %w", err)
		}
	}

	cmd := args[0]
	switch {
	case matchCmd(cmd, "show"):
		return aristaShow(client, args[1:], jsonMode)
	case matchCmd(cmd, "no"):
		return aristaNo(client, args[1:], jsonMode)
	case matchCmd(cmd, "enable"):
		return nil
	case matchCmd(cmd, "configure"):
		fmt.Println("Entering configuration mode")
		return nil
	case matchCmd(cmd, "copy"):
		return aristaCopy(client, args[1:], jsonMode)
	case matchCmd(cmd, "write"):
		return aristaWrite(client, args[1:], jsonMode)
	case matchCmd(cmd, "clear"):
		return aristaClear(client, args[1:], jsonMode)
	case matchCmd(cmd, "exit") || matchCmd(cmd, "quit") || matchCmd(cmd, "end"):
		return fmt.Errorf("exit")
	case matchCmd(cmd, "login"):
		if len(args) < 2 {
			return fmt.Errorf("login <password>")
		}
		client.password = args[1]
		return client.Login()
	case matchCmd(cmd, "ping"):
		return aristaPing(client, args[1:], jsonMode)
	case matchCmd(cmd, "lldp"):
		return aristaLldp(client, args[1:], jsonMode)
	case matchCmd(cmd, "storm-control"):
		return aristaStorm(client, args[1:], jsonMode)
	case matchCmd(cmd, "qos"):
		return aristaQos(client, args[1:], jsonMode)
	case matchCmd(cmd, "acl"):
		return aristaAcl(client, args[1:], jsonMode)
	case matchCmd(cmd, "ip"):
		return aristaIp(client, args[1:], jsonMode)
	case matchCmd(cmd, "hostname"):
		return aristaHostname(client, args[1:], jsonMode)
	case matchCmd(cmd, "username"):
		return aristaUsername(client, args[1:], jsonMode)
	case matchCmd(cmd, "spanning-tree"):
		return aristaSpanningTree(client, args[1:], jsonMode)
	case matchCmd(cmd, "vlan"):
		return aristaVlanConfig(client, args[1:], jsonMode)
	case matchCmd(cmd, "interface"):
		return aristaInterface(client, args[1:], jsonMode)
	case matchCmd(cmd, "telnet"):
		return aristaTelnet(client, args[1:], jsonMode)
	case matchCmd(cmd, "web"):
		return aristaWeb(client, args[1:], jsonMode)
	case matchCmd(cmd, "commit"):
		return aristaConsole(client, "commit", jsonMode)
	case matchCmd(cmd, "pvid"):
		return aristaPvid(client, args[1:], jsonMode)
	case matchCmd(cmd, "isolate"):
		return aristaConsole(client, "isolate "+strings.Join(args[1:], " "), jsonMode)
	case matchCmd(cmd, "ingress"):
		return aristaConsole(client, "ingress "+strings.Join(args[1:], " "), jsonMode)
	case matchCmd(cmd, "laghash"):
		return aristaLaghash(client, args[1:], jsonMode)
	case matchCmd(cmd, "sfp"):
		return aristaConsole(client, "sfp "+strings.Join(args[1:], " "), jsonMode)
	case matchCmd(cmd, "regget"):
		return aristaConsole(client, "regget "+strings.Join(args[1:], " "), jsonMode)
	case matchCmd(cmd, "regset"):
		return aristaConsole(client, "regset "+strings.Join(args[1:], " "), jsonMode)
	case matchCmd(cmd, "sdsget"):
		return aristaConsole(client, "sdsget "+strings.Join(args[1:], " "), jsonMode)
	case matchCmd(cmd, "sdsset"):
		return aristaConsole(client, "sdsset "+strings.Join(args[1:], " "), jsonMode)
	case matchCmd(cmd, "phyget"):
		return aristaConsole(client, "phyget "+strings.Join(args[1:], " "), jsonMode)
	case matchCmd(cmd, "physet"):
		return aristaConsole(client, "physet "+strings.Join(args[1:], " "), jsonMode)
	case matchCmd(cmd, "preshared-key") || matchCmd(cmd, "psk"):
		if len(args) != 2 {
			return fmt.Errorf("usage: preshared-key <64-hex-chars>")
		}
		if err := validatePSK(args[1]); err != nil {
			return err
		}
		return aristaConsole(client, "preshared_key "+args[1], jsonMode)
	default:
		if jsonMode {
			eapiError(fmt.Sprintf("%% Unknown command: %s", strings.Join(args, " ")))
		} else {
			fmt.Fprintf(os.Stderr, "%% Unknown command: %s\n", strings.Join(args, " "))
		}
		return nil
	}
}

func aristaShow(client *Client, args []string, jsonMode bool) error {
	if len(args) == 0 {
		aristaUnknown("show", jsonMode)
		return nil
	}
	sub := args[0]
	switch {
	case matchCmd(sub, "interfaces") || matchCmd(sub, "int"):
		return aristaShowInterfaces(client, args[1:], jsonMode)
	case matchCmd(sub, "running-config") || matchCmd(sub, "run"):
		/* The /running-config endpoint (firmware v0.2.24+) returns the
		 * in-memory config that 'commit' would save; /config serves
		 * the flash copy (startup-config). */
		data, err := client.GetText("/running-config")
		if err != nil {
			return err
		}
		if jsonMode {
			eapiResult(data, "text")
		} else {
			fmt.Print(data)
			if !strings.HasSuffix(data, "\n") {
				fmt.Println()
			}
		}
		return nil
	case matchCmd(sub, "startup-config"):
		data, err := client.GetText("/config")
		if err != nil {
			return err
		}
		if jsonMode {
			eapiResult(data, "text")
		} else {
			fmt.Print(data)
			if !strings.HasSuffix(data, "\n") {
				fmt.Println()
			}
		}
		return nil
	case matchCmd(sub, "arp"):
		return aristaConsoleCmd(client, "show arp", jsonMode)
	case matchCmd(sub, "vlan"):
		return aristaShowVlan(client, args[1:], jsonMode)
	case matchCmd(sub, "inventory") || matchCmd(sub, "inv"):
		data, err := client.GetJSON("/information.json")
		if err != nil {
			return err
		}
		return eosFormatInfo(data, jsonMode)
	case matchCmd(sub, "mac"):
		return aristaShowMac(client, args[1:], jsonMode)
	case matchCmd(sub, "logging") || matchCmd(sub, "log"):
		return aristaShowLogging(client, args[1:], jsonMode)
	case matchCmd(sub, "port-channel") || matchCmd(sub, "lag"):
		data, err := client.GetJSON("/lag.json")
		if err != nil {
			return err
		}
		return eosFormatLag(data, jsonMode)
	case matchCmd(sub, "monitoring") || matchCmd(sub, "mirror"):
		data, err := client.GetJSON("/mirror.json")
		if err != nil {
			return err
		}
		return eosFormatMirror(data, jsonMode)
	case matchCmd(sub, "queue") || matchCmd(sub, "bandwidth"):
		data, err := client.GetJSON("/bandwidth.json")
		if err != nil {
			return err
		}
		return eosFormatBandwidth(data, jsonMode)
	case matchCmd(sub, "system") || matchCmd(sub, "sys"):
		data, err := client.GetJSON("/information.json")
		if err != nil {
			return err
		}
		return eosFormatInfo(data, jsonMode)
	case matchCmd(sub, "mtu"):
		data, err := client.GetJSON("/mtu.json")
		if err != nil {
			return err
		}
		return eosFormatMTU(data, jsonMode)
	case matchCmd(sub, "eee"):
		data, err := client.GetJSON("/eee.json")
		if err != nil {
			return err
		}
		return eosFormatEEE(data, jsonMode)
	case matchCmd(sub, "config") || matchCmd(sub, "conf"):
		data, err := client.GetText("/config")
		if err != nil {
			return err
		}
		if jsonMode {
			eapiResult(data, "text")
		} else {
			fmt.Print(data)
			if !strings.HasSuffix(data, "\n") {
				fmt.Println()
			}
		}
		return nil
	case matchCmd(sub, "cmd-log") || matchCmd(sub, "history") || matchCmd(sub, "log"):
		data, err := client.GetText("/cmd_log")
		if err != nil {
			return err
		}
		if jsonMode {
			eapiResult(data, "text")
		} else {
			fmt.Print(data)
		}
		return nil
	case matchCmd(sub, "lldp"):
		// show lldp neighbors (the neighbor table is printed on the
		// switch console)
		return aristaConsoleCmd(client, "lldp show", jsonMode)
	case matchCmd(sub, "ip"):
		return aristaShowIp(client, args[1:], jsonMode)
	case matchCmd(sub, "qos"):
		return aristaConsoleCmd(client, "qos status", jsonMode)
	case matchCmd(sub, "access-lists") || matchCmd(sub, "acl"):
		return aristaConsoleCmd(client, "acl show", jsonMode)
	case matchCmd(sub, "storm-control"):
		return aristaConsoleCmd(client, "storm-control status", jsonMode)
	default:
		aristaUnknown("show "+sub, jsonMode)
		return nil
	}
}

func aristaShowInterfaces(client *Client, args []string, jsonMode bool) error {
	if len(args) == 0 {
		data, err := client.GetJSON("/status.json")
		if err != nil {
			return err
		}
		ports, ok := data.([]interface{})
		if !ok {
			return fmt.Errorf("unexpected response format")
		}
		return eosFormatStatus(ports, jsonMode, 0)
	}
	sub := strings.ToLower(args[0])
	if matchCmd(sub, "counters") || matchCmd(sub, "count") || matchCmd(sub, "cnt") {
		if len(args) > 1 {
			if err := validateCountersPort(eosCmpEthernet(strings.Join(args[1:], " "))); err != nil {
				return err
			}
		}
		port := parseEthernetPort(strings.Join(args[1:], " "))
		data, err := client.GetJSON(fmt.Sprintf("/counters.json?port=%d", port))
		if err != nil {
			return err
		}
		return eosFormatCounters(data, jsonMode, port)
	}
	if matchCmd(sub, "status") || matchCmd(sub, "stat") || matchCmd(sub, "brief") {
		port := 0
		if len(args) > 1 {
			if err := validateCountersPort(eosCmpEthernet(args[1])); err != nil {
				return err
			}
			port = parseEthernetPort(args[1])
		}
		data, err := client.GetJSON("/status.json")
		if err != nil {
			return err
		}
		ports, ok := data.([]interface{})
		if !ok {
			return fmt.Errorf("unexpected response format")
		}
		return eosFormatStatus(ports, jsonMode, port)
	}
	port := parseEthernetPort(sub)
	if port > 0 {
		if port > 8 {
			aristaUnknown("show interfaces "+sub, jsonMode)
			return nil
		}
		data, err := client.GetJSON("/status.json")
		if err != nil {
			return err
		}
		ports, ok := data.([]interface{})
		if !ok {
			return fmt.Errorf("unexpected response format")
		}
		return eosFormatStatus(ports, jsonMode, port)
	}
	aristaUnknown("show interfaces "+sub, jsonMode)
	return nil
}

func aristaShowVlan(client *Client, args []string, jsonMode bool) error {
	if len(args) >= 2 && matchCmd(args[0], "id") {
		if err := validateVLANID(args[1]); err != nil {
			return err
		}
		data, err := client.GetJSON(fmt.Sprintf("/vlan.json?vid=%s", args[1]))
		if err != nil {
			return err
		}
		return eosFormatVlan(data, jsonMode, args[1])
	}
	data, err := client.GetJSON("/vlanlist")
	if err != nil {
		return err
	}
	return eosFormatVlanList(data, jsonMode)
}

func aristaShowMac(client *Client, args []string, jsonMode bool) error {
	idx := "0"
	if len(args) > 0 && matchCmd(args[0], "address-table") {
		if len(args) > 1 {
			idx = args[1]
		}
	} else if len(args) > 0 {
		idx = args[0]
	}
	if err := validateL2Idx(idx); err != nil {
		return err
	}
	data, err := client.GetJSON(fmt.Sprintf("/l2.json?idx=%s", idx))
	if err != nil {
		return err
	}
	return eosFormatMacTable(data, jsonMode)
}

func aristaShowLogging(client *Client, args []string, jsonMode bool) error {
	data, err := client.GetText("/cmd_log")
	if err != nil {
		return err
	}
	if jsonMode {
		eapiResult(data, "text")
	} else {
		fmt.Print(data)
	}
	return nil
}

// aristaConsole validates a device CLI command locally and sends it via
// /cmd (output appears on the switch console).
func aristaConsole(client *Client, cmdText string, jsonMode bool) error {
	if err := validateCmdText(cmdText); err != nil {
		return err
	}
	return aristaConsoleCmd(client, cmdText, jsonMode)
}

// aristaPing maps EOS "ping <ip>" to the switch's ICMP echo sender.
func aristaPing(client *Client, args []string, jsonMode bool) error {
	if len(args) != 1 {
		aristaUnknown("ping", jsonMode)
		return nil
	}
	if err := validateIPAddr(args[0]); err != nil {
		return err
	}
	return aristaConsoleCmd(client, "ping "+args[0], jsonMode)
}

// aristaLldp maps EOS "lldp enable|disable" to the device lldp on|off.
func aristaLldp(client *Client, args []string, jsonMode bool) error {
	switch {
	case len(args) == 0:
		return aristaConsoleCmd(client, "lldp", jsonMode)
	case len(args) == 1 && matchCmd(args[0], "enable"):
		return aristaConsole(client, "lldp on", jsonMode)
	case len(args) == 1 && matchCmd(args[0], "disable"):
		return aristaConsole(client, "lldp off", jsonMode)
	}
	aristaUnknown("lldp "+strings.Join(args, " "), jsonMode)
	return nil
}

// aristaStorm maps EOS "storm-control <type> level <rate>" to the device
// storm-control command.  EOS types: broadcast, multicast,
// unknown-unicast (device: dlf), unknown-multicast (device: unknown-mcast).
func aristaStorm(client *Client, args []string, jsonMode bool) error {
	if len(args) < 2 || !matchCmd(args[1], "level") {
		aristaUnknown("storm-control "+strings.Join(args, " "), jsonMode)
		return nil
	}
	devType, ok := map[string]string{
		"broadcast":        "broadcast",
		"multicast":        "multicast",
		"unknown-unicast":  "dlf",
		"unknown-multicast": "unknown-mcast",
	}[strings.ToLower(args[0])]
	if !ok {
		return fmt.Errorf("unknown storm-control type: %q (use broadcast, multicast, unknown-unicast, unknown-multicast)", args[0])
	}
	if len(args) != 3 {
		return fmt.Errorf("usage: storm-control <type> level <rate>[k|p]")
	}
	if err := validateStormRate(args[2]); err != nil {
		return err
	}
	return aristaConsole(client, "storm-control on "+devType+" "+args[2], jsonMode)
}

// aristaQos passes the device qos command through (the firmware qos CLI
// is already the canonical form; EOS has no direct equivalent).
func aristaQos(client *Client, args []string, jsonMode bool) error {
	cmdText := "qos"
	if len(args) > 0 {
		cmdText = "qos " + strings.Join(args, " ")
	}
	return aristaConsole(client, cmdText, jsonMode)
}

// aristaAcl passes the device acl command through (see aristaQos).
func aristaAcl(client *Client, args []string, jsonMode bool) error {
	if len(args) == 0 {
		return fmt.Errorf("usage: acl on|off|show|add|del")
	}
	cmdText := "acl " + strings.Join(args, " ")
	return aristaConsole(client, cmdText, jsonMode)
}

// aristaIp maps the EOS IP configuration commands:
//   ip address <a.b.c.d>[/<prefix>]   -> ip + netmask
//   ip address dhcp                   -> ip dhcp
//   ip default-gateway <a.b.c.d>      -> gw
//   ip route 0.0.0.0/0 <a.b.c.d>      -> gw
func aristaIp(client *Client, args []string, jsonMode bool) error {
	if len(args) < 2 {
		aristaUnknown("ip "+strings.Join(args, " "), jsonMode)
		return nil
	}
	switch {
	case matchCmd(args[0], "address") && len(args) == 2 && matchCmd(args[1], "dhcp"):
		return aristaConsole(client, "ip dhcp", jsonMode)
	case matchCmd(args[0], "address") && len(args) == 2:
		addr := args[1]
		prefix := -1
		if i := strings.IndexByte(addr, '/'); i >= 0 {
			n, err := strconv.Atoi(addr[i+1:])
			if err != nil || n < 0 || n > 32 {
				return fmt.Errorf("invalid prefix: %q (must be 0-32)", addr[i+1:])
			}
			prefix = n
			addr = addr[:i]
		}
		if err := validateIPAddr(addr); err != nil {
			return err
		}
		if err := aristaConsole(client, "ip "+addr, jsonMode); err != nil {
			return err
		}
		if prefix >= 0 {
			return aristaConsole(client, "netmask "+prefixToNetmask(prefix), jsonMode)
		}
		return nil
	case matchCmd(args[0], "default-gateway") && len(args) == 2:
		if err := validateIPAddr(args[1]); err != nil {
			return err
		}
		return aristaConsole(client, "gw "+args[1], jsonMode)
	case matchCmd(args[0], "route") && len(args) == 3 && matchCmd(args[1], "0.0.0.0/0"):
		if err := validateIPAddr(args[2]); err != nil {
			return err
		}
		return aristaConsole(client, "gw "+args[2], jsonMode)
	case matchCmd(args[0], "igmp") && len(args) >= 3 && matchCmd(args[1], "snooping"):
		switch {
		case len(args) == 3 && matchCmd(args[2], "enable"):
			return aristaConsole(client, "igmp on", jsonMode)
		case len(args) == 3 && matchCmd(args[2], "disable"):
			return aristaConsole(client, "igmp off", jsonMode)
		}
		aristaUnknown("ip "+strings.Join(args, " "), jsonMode)
		return nil
	}
	aristaUnknown("ip "+strings.Join(args, " "), jsonMode)
	return nil
}

// aristaShowIp handles "show ip ..." (IGMP snooping state).
func aristaShowIp(client *Client, args []string, jsonMode bool) error {
	if len(args) >= 3 && matchCmd(args[0], "igmp") && matchCmd(args[1], "snooping") {
		sub := args[2]
		switch {
		case matchCmd(sub, "querier"):
			return aristaConsoleCmd(client, "igmp querier show", jsonMode)
		case matchCmd(sub, "groups") || matchCmd(sub, "mld"):
			return aristaConsoleCmd(client, "igmp mld show", jsonMode)
		default:
			return aristaConsoleCmd(client, "igmp show", jsonMode)
		}
	}
	aristaUnknown("show ip "+strings.Join(args, " "), jsonMode)
	return nil
}

// aristaHostname maps EOS "hostname <name>" to the device command.
func aristaHostname(client *Client, args []string, jsonMode bool) error {
	if len(args) != 1 {
		return fmt.Errorf("usage: hostname <name>")
	}
	if err := validateDeviceHostname(args[0]); err != nil {
		return err
	}
	return aristaConsole(client, "hostname "+args[0], jsonMode)
}

// aristaUsername maps EOS "username <name> [secret|password] <pw>" to the
// device web password.
func aristaUsername(client *Client, args []string, jsonMode bool) error {
	if len(args) != 3 || !matchCmd(args[1], "secret") && !matchCmd(args[1], "password") {
		return fmt.Errorf("usage: username <name> [secret|password] <password>")
	}
	if err := validatePassword(args[2]); err != nil {
		return err
	}
	return aristaConsole(client, "passwd "+args[2], jsonMode)
}

// aristaSpanningTree maps EOS "spanning-tree mode <mode>" to stp on.
// "no spanning-tree mode" is handled by aristaNo.
func aristaSpanningTree(client *Client, args []string, jsonMode bool) error {
	if len(args) == 2 && matchCmd(args[0], "mode") {
		switch strings.ToLower(args[1]) {
		case "stp", "mstp", "rstp", "rapid-pvst":
			return aristaConsole(client, "stp on", jsonMode)
		}
	}
	return fmt.Errorf("usage: spanning-tree mode stp|mstp|rstp")
}

// aristaVlanConfig maps EOS "vlan <id>" / "vlan <id> name <name>" to the
// device vlan create/name.  "no vlan <id>" is handled by aristaNo.
func aristaVlanConfig(client *Client, args []string, jsonMode bool) error {
	if len(args) < 1 || len(args) > 3 {
		return fmt.Errorf("usage: vlan <id> [name <name>]")
	}
	if err := validateVLANID(args[0]); err != nil {
		return err
	}
	cmdText := "vlan " + args[0]
	if len(args) == 3 && matchCmd(args[1], "name") {
		cmdText += " " + args[2]
	}
	return aristaConsole(client, cmdText, jsonMode)
}

// aristaInterface maps EOS interface configuration.  The interface is
// given in the same command (rtlpctl is stateless per invocation):
//   interface EthernetX                          -> show port status
//   interface EthernetX speed <speed>            -> port X <speed>
//   interface EthernetX duplex <half|full>       -> port X duplex
//   interface EthernetX switchport access vlan V -> pvid X V
//   interface EthernetX mtu <size>               -> mtu X <size>
//   interface EthernetX description <name>       -> port X name
//   interface EthernetX shutdown                 -> port X off
//   no interface EthernetX shutdown              -> port X on (aristaNo)
func aristaInterface(client *Client, args []string, jsonMode bool) error {
	if len(args) == 0 {
		return fmt.Errorf("usage: interface Ethernet<port> [speed|duplex|switchport|mtu|description|shutdown]")
	}
	port := parseEthernetPort(args[0])
	if port < 1 || port > 9 {
		return fmt.Errorf("invalid interface: %q (use Ethernet1-Ethernet9)", args[0])
	}
	if len(args) == 1 {
		// bare "interface EtX": show the port state
		data, err := client.GetJSON("/status.json")
		if err != nil {
			return err
		}
		ports, ok := data.([]interface{})
		if !ok {
			return fmt.Errorf("unexpected response format")
		}
		return eosFormatStatus(ports, jsonMode, port)
	}
	sub := args[1]
	switch {
	case matchCmd(sub, "speed") && len(args) == 3:
		if err := validateSpeedWord(args[2]); err != nil {
			return err
		}
		return aristaConsole(client, fmt.Sprintf("port %d %s", port, args[2]), jsonMode)
	case matchCmd(sub, "duplex") && len(args) == 3:
		if err := validateDuplex(args[2]); err != nil {
			return err
		}
		return aristaConsole(client, fmt.Sprintf("port %d duplex %s", port, args[2]), jsonMode)
	case matchCmd(sub, "switchport") && len(args) == 5 && matchCmd(args[2], "access") && matchCmd(args[3], "vlan"):
		if err := validateVLANID(args[4]); err != nil {
			return err
		}
		return aristaConsole(client, fmt.Sprintf("pvid %d %s", port, args[4]), jsonMode)
	case matchCmd(sub, "mtu") && len(args) == 3:
		if err := validateMTU(args[2]); err != nil {
			return err
		}
		return aristaConsole(client, fmt.Sprintf("mtu %d %s", port, args[2]), jsonMode)
	case matchCmd(sub, "description") && len(args) == 3:
		if err := validateDeviceHostname(args[2]); err != nil {
			return err
		}
		return aristaConsole(client, fmt.Sprintf("port %d name %s", port, args[2]), jsonMode)
	case matchCmd(sub, "shutdown") && len(args) == 2:
		return aristaConsole(client, fmt.Sprintf("port %d off", port), jsonMode)
	}
	aristaUnknown("interface "+strings.Join(args, " "), jsonMode)
	return nil
}

// aristaTelnet maps EOS "telnet server enable|disable" and the plain
// "telnet on|off" to the device telnet command.
func aristaTelnet(client *Client, args []string, jsonMode bool) error {
	if len(args) == 2 && matchCmd(args[0], "server") && (matchCmd(args[1], "enable") || matchCmd(args[1], "disable")) {
		on := "off"
		if matchCmd(args[1], "enable") {
			on = "on"
		}
		return aristaConsole(client, "telnet "+on, jsonMode)
	}
	if len(args) == 1 && (matchCmd(args[0], "on") || matchCmd(args[0], "off")) {
		return aristaConsole(client, "telnet "+args[0], jsonMode)
	}
	return fmt.Errorf("usage: telnet [server enable|disable] | telnet on|off")
}

// aristaWeb maps EOS "web server enable|disable" and the plain "web on|off"
// to the device web command.
func aristaWeb(client *Client, args []string, jsonMode bool) error {
	if len(args) == 2 && matchCmd(args[0], "server") && (matchCmd(args[1], "enable") || matchCmd(args[1], "disable")) {
		on := "off"
		if matchCmd(args[1], "enable") {
			on = "on"
		}
		return aristaConsole(client, "web "+on, jsonMode)
	}
	if len(args) == 1 && (matchCmd(args[0], "on") || matchCmd(args[0], "off")) {
		return aristaConsole(client, "web "+args[0], jsonMode)
	}
	return fmt.Errorf("usage: web [server enable|disable] | web on|off")
}

// aristaPvid maps EOS "pvid <port> <vid>" to the device pvid command.
func aristaPvid(client *Client, args []string, jsonMode bool) error {
	if len(args) != 2 {
		return fmt.Errorf("usage: pvid <port> <vid>")
	}
	if err := validatePortNum(args[0], 9); err != nil {
		return err
	}
	if err := validateVLANID(args[1]); err != nil {
		return err
	}
	return aristaConsole(client, "pvid "+args[0]+" "+args[1], jsonMode)
}

// aristaLaghash maps EOS "laghash <hash> [fields...]" to the device
// laghash command (validated host-side).
func aristaLaghash(client *Client, args []string, jsonMode bool) error {
	if len(args) == 0 {
		return fmt.Errorf("usage: laghash <hash> [smac|dmac|spa|sip|dip|sport|dport]")
	}
	cmdText := "laghash " + strings.Join(args, " ")
	if err := validateCmdText(cmdText); err != nil {
		return err
	}
	return aristaConsole(client, cmdText, jsonMode)
}

// aristaNo handles the EOS "no <command>" inverse mappings.
func aristaNo(client *Client, args []string, jsonMode bool) error {
	if len(args) == 0 {
		aristaUnknown("no", jsonMode)
		return nil
	}
	switch {
	case matchCmd(args[0], "spanning-tree"):
		return aristaConsole(client, "stp off", jsonMode)
	case matchCmd(args[0], "lldp"):
		return aristaConsole(client, "lldp off", jsonMode)
	case matchCmd(args[0], "ip") && len(args) >= 3 && matchCmd(args[1], "igmp") && matchCmd(args[2], "snooping"):
		return aristaConsole(client, "igmp off", jsonMode)
	case matchCmd(args[0], "storm-control") && len(args) >= 2:
		devType, ok := map[string]string{
			"broadcast":         "broadcast",
			"multicast":         "multicast",
			"unknown-unicast":   "dlf",
			"unknown-multicast": "unknown-mcast",
		}[strings.ToLower(args[1])]
		if ok {
			return aristaConsole(client, "storm-control off "+devType, jsonMode)
		}
		return fmt.Errorf("unknown storm-control type: %q", args[1])
	case matchCmd(args[0], "vlan") && len(args) >= 2:
		if err := validateVLANID(args[1]); err != nil {
			return err
		}
		return aristaConsole(client, "vlan "+args[1]+" d", jsonMode)
	case matchCmd(args[0], "interface") && len(args) >= 2:
		port := parseEthernetPort(args[1])
		if port < 1 || port > 9 {
			return fmt.Errorf("invalid interface: %q", args[1])
		}
		sub := ""
		if len(args) >= 3 {
			sub = args[2]
		}
		switch {
		case matchCmd(sub, "shutdown"):
			return aristaConsole(client, fmt.Sprintf("port %d on", port), jsonMode)
		case matchCmd(sub, "switchport") && len(args) >= 5 && matchCmd(args[3], "access") && matchCmd(args[4], "vlan"):
			return aristaConsole(client, fmt.Sprintf("pvid %d 1", port), jsonMode)
		}
	case matchCmd(args[0], "telnet"):
		return aristaConsole(client, "telnet off", jsonMode)
	case matchCmd(args[0], "web"):
		return aristaConsole(client, "web off", jsonMode)
	case matchCmd(args[0], "isolate") && len(args) >= 2:
		if err := validatePortNum(args[1], 9); err != nil {
			return err
		}
		return aristaConsole(client, "isolate "+args[1]+" off", jsonMode)
	case matchCmd(args[0], "pvid") && len(args) >= 2:
		if err := validatePortNum(args[1], 9); err != nil {
			return err
		}
		return aristaConsole(client, fmt.Sprintf("pvid %s 1", args[1]), jsonMode)
	}
	aristaUnknown("no "+strings.Join(args, " "), jsonMode)
	return nil
}

// prefixToNetmask converts a CIDR prefix length to a dotted-quad mask.
func prefixToNetmask(prefix int) string {
	if prefix == 0 {
		return "0.0.0.0"
	}
	mask := uint32(0xffffffff) << uint(32-prefix)
	return fmt.Sprintf("%d.%d.%d.%d",
		byte(mask>>24), byte(mask>>16), byte(mask>>8), byte(mask))
}


// aristaSaveConfig persists the running configuration to flash (EOS
// "write memory" / "copy running-config startup-config"). The firmware only
// allows `commit` through the PSK-authenticated /enc path (privileged mode);
// the password-only /cmd path runs in MODE_CONFIG and cannot commit.
func aristaSaveConfig(client *Client, jsonMode bool) error {
	_, err := client.PostEnc("commit")
	if err != nil {
		msg := "config save requires a pre-shared key: use --psk <64-hex> " +
			"(must match the PSK configured on the switch)"
		if jsonMode {
			eapiError(msg)
		} else {
			fmt.Fprintln(os.Stderr, msg)
		}
		return nil
	}
	if jsonMode {
		eapiResult("OK", "text")
	} else {
		fmt.Println("OK")
	}
	return nil
}

func aristaCopy(client *Client, args []string, jsonMode bool) error {
	if len(args) >= 2 && matchCmd(args[0], "running-config") && matchCmd(args[1], "startup-config") {
		return aristaSaveConfig(client, jsonMode)
	}
	aristaUnknown("copy "+strings.Join(args, " "), jsonMode)
	return nil
}

func aristaWrite(client *Client, args []string, jsonMode bool) error {
	if len(args) == 0 || (len(args) >= 1 && matchCmd(args[0], "memory")) {
		return aristaSaveConfig(client, jsonMode)
	}
	aristaUnknown("write "+strings.Join(args, " "), jsonMode)
	return nil
}

func aristaClear(client *Client, args []string, jsonMode bool) error {
	if len(args) >= 3 && matchCmd(args[0], "mac") && matchCmd(args[1], "address-table") && matchCmd(args[2], "dynamic") {
		err := client.PostRaw("/cmd", "text/plain", strings.NewReader("l2 forget"))
		if err != nil {
			return err
		}
		if jsonMode {
			eapiResult("OK", "text")
		} else {
			fmt.Println("OK")
		}
		return nil
	}
	if len(args) >= 1 && matchCmd(args[0], "logging") {
		_, err := client.GetText("/cmd_log_clear")
		if err != nil {
			return err
		}
		if jsonMode {
			eapiResult("OK", "text")
		} else {
			fmt.Println("OK")
		}
		return nil
	}
	aristaUnknown("clear "+strings.Join(args, " "), jsonMode)
	return nil
}

func aristaUnknown(cmd string, jsonMode bool) {
	if jsonMode {
		eapiError(fmt.Sprintf("%% Unknown command: %s", cmd))
	} else {
		fmt.Fprintf(os.Stderr, "%% Unknown command: %s\n", cmd)
	}
}

// aristaConsoleCmd sends a CLI command whose output appears on the
// switch's serial console (the /cmd endpoint only acknowledges
// execution, so there is nothing to render here).
func aristaConsoleCmd(client *Client, cmdText string, jsonMode bool) error {
	if err := client.PostRaw("/cmd", "text/plain", strings.NewReader(cmdText)); err != nil {
		return err
	}
	if jsonMode {
		eapiResult("OK (output appears on the switch console)", "text")
	} else {
		fmt.Println("OK (output appears on the switch console)")
	}
	return nil
}

func parseEthernetPort(s string) int {
	s = eosCmpEthernet(s)
	n, err := strconv.Atoi(s)
	if err != nil {
		return 0
	}
	return n
}

func eosFormatStatus(ports []interface{}, jsonMode bool, filterPort int) error {
	if jsonMode {
		ifaces := map[string]interface{}{}
		for _, p := range ports {
			pm := p.(map[string]interface{})
			pn := int(fmtIntTo64(pm["portNum"]))
			if filterPort > 0 && pn != filterPort {
				continue
			}
			ifaces[eosPortName(pm["portNum"])] = map[string]interface{}{
				"name":       fmtStr(pm["name"]),
				"linkStatus": eosLinkStatus(pm["link"]),
				"speed":      eosSpeed(pm["link"]),
				"duplex":     "full",
				"enabled":    fmtIntTo64(pm["enabled"]) != 0,
			}
		}
		eapiResult(map[string]interface{}{"interfaces": ifaces}, "json")
		return nil
	}

	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "Port\tName\tStatus\tVlan\tDuplex\tSpeed\tType")
	for _, p := range ports {
		pm := p.(map[string]interface{})
		pn := int(fmtIntTo64(pm["portNum"]))
		if filterPort > 0 && pn != filterPort {
			continue
		}
		status := eosLinkStatus(pm["link"])
		if fmtIntTo64(pm["enabled"]) == 0 {
			status = "disabled"
		}
		speed := eosSpeed(pm["link"])
		if speed == "down" {
			speed = ""
		}
		fmt.Fprintf(w, "%s\t%s\t%s\t1\tfull\t%s\t\n",
			eosPortName(pm["portNum"]),
			fmtStr(pm["name"]),
			status,
			speed)
	}
	w.Flush()
	return nil
}

// eosSpeed maps the /status.json link code to an EOS-style speed string
// (same codes as fmtLink: 1=10M, 2=100M, 3=1G, 5=10G, 6=2.5G, 7=5G).
func eosSpeed(link interface{}) string {
	switch v := link.(type) {
	case float64:
		switch int(v) {
		case 1:
			return "10M"
		case 2:
			return "100M"
		case 3:
			return "1G"
		case 5:
			return "10G"
		case 6:
			return "2.5G"
		case 7:
			return "5G"
		}
	case json.Number:
		n, _ := v.Int64()
		switch n {
		case 1:
			return "10M"
		case 2:
			return "100M"
		case 3:
			return "1G"
		case 5:
			return "10G"
		case 6:
			return "2.5G"
		case 7:
			return "5G"
		}
	}
	return "down"
}

func eosFormatInfo(data interface{}, jsonMode bool) error {
	info, ok := data.(map[string]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	if jsonMode {
		eapiResult(map[string]interface{}{
			"systemFqdn":     fmtStr(info["ip_address"]),
			"hardwareEthernetManagement": map[string]interface{}{
				"mac": fmtStr(info["mac_address"]),
			},
			"version": fmtStr(info["sw_ver"]),
			"modelName": fmtStr(info["hw_ver"]),
			"internalVersion": fmtStr(info["build_date"]),
		}, "json")
		return nil
	}
	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintf(w, "System IP:\t%s\n", fmtStr(info["ip_address"]))
	fmt.Fprintf(w, "Gateway:\t%s\n", fmtStr(info["ip_gateway"]))
	fmt.Fprintf(w, "Netmask:\t%s\n", fmtStr(info["ip_netmask"]))
	fmt.Fprintf(w, "MAC Address:\t%s\n", fmtStr(info["mac_address"]))
	fmt.Fprintf(w, "Software Version:\t%s\n", fmtStr(info["sw_ver"]))
	fmt.Fprintf(w, "Hardware:\t%s\n", fmtStr(info["hw_ver"]))
	fmt.Fprintf(w, "Build Date:\t%s\n", fmtStr(info["build_date"]))
	w.Flush()
	return nil
}

func eosFormatVlan(data interface{}, jsonMode bool, vid string) error {
	vlan, ok := data.(map[string]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	if jsonMode {
		eapiResult(map[string]interface{}{
			"vlanId":   vid,
			"name":     fmtStr(vlan["name"]),
			"members":  fmtStr(vlan["members"]),
			"pvid":     fmtStr(vlan["pvid"]),
		}, "json")
		return nil
	}
	fmt.Printf("VLAN %s:\n", vid)
	fmt.Printf("  Name:    %s\n", fmtStr(vlan["name"]))
	fmt.Printf("  Members: %s\n", fmtStr(vlan["members"]))
	fmt.Printf("  PVID:    %s\n", fmtStr(vlan["pvid"]))
	return nil
}

func eosFormatVlanList(data interface{}, jsonMode bool) error {
	list, ok := data.([]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	if jsonMode {
		var vlans []map[string]interface{}
		for _, v := range list {
			vm := v.(map[string]interface{})
			vlans = append(vlans, map[string]interface{}{
				"vlanId": fmtInt(vm["id"]),
				"name":   fmtStr(vm["name"]),
			})
		}
		eapiResult(vlans, "json")
		return nil
	}
	if len(list) == 0 {
		fmt.Println("No VLANs configured")
		return nil
	}
	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "VLAN\tName\tStatus")
	for _, v := range list {
		vm := v.(map[string]interface{})
		fmt.Fprintf(w, "%s\t%s\tactive\n", fmtInt(vm["id"]), fmtStr(vm["name"]))
	}
	w.Flush()
	return nil
}

func eosFormatCounters(data interface{}, jsonMode bool, port int) error {
	if jsonMode {
		eapiResult(data, "json")
		return nil
	}
	arr, ok := data.([]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	for i, c := range arr {
		fmt.Printf("%3d: %s\n", i, fmtStr(c))
	}
	return nil
}

func eosFormatLag(data interface{}, jsonMode bool) error {
	lags, ok := data.([]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	if jsonMode {
		eapiResult(lags, "json")
		return nil
	}
	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "LAG\tMembers\tHash")
	for _, l := range lags {
		lm := l.(map[string]interface{})
		fmt.Fprintf(w, "%s\t%s\t%s\n", fmtInt(lm["lagNum"]), fmtStr(lm["members"]), fmtStr(lm["hash"]))
	}
	w.Flush()
	return nil
}

func eosFormatMirror(data interface{}, jsonMode bool) error {
	mirror, ok := data.(map[string]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	if jsonMode {
		eapiResult(mirror, "json")
		return nil
	}
	fmt.Printf("Mirroring: %s\n", fmtBool(mirror["enabled"]))
	fmt.Printf("  Monitor Port: %s\n", fmtInt(mirror["mPort"]))
	fmt.Printf("  TX Ports: %s\n", fmtStr(mirror["mirror_tx"]))
	fmt.Printf("  RX Ports: %s\n", fmtStr(mirror["mirror_rx"]))
	return nil
}

func eosFormatBandwidth(data interface{}, jsonMode bool) error {
	ports, ok := data.([]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	if jsonMode {
		eapiResult(ports, "json")
		return nil
	}
	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "Port\tIngress Limit\tIngress BW\tIngress FC\tEgress Limit\tEgress BW")
	for _, p := range ports {
		pm := p.(map[string]interface{})
		fmt.Fprintf(w, "%s\t%s\t%s\t%s\t%s\t%s\n",
			eosPortName(pm["portNum"]),
			fmtBool(pm["iLimited"]),
			fmtStr(pm["iBW"]),
			fmtBool(pm["iFC"]),
			fmtBool(pm["eLimited"]),
			fmtStr(pm["eBW"]))
	}
	w.Flush()
	return nil
}

func eosFormatMTU(data interface{}, jsonMode bool) error {
	ports, ok := data.([]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	if jsonMode {
		eapiResult(ports, "json")
		return nil
	}
	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "Port\tMTU")
	for _, p := range ports {
		pm := p.(map[string]interface{})
		fmt.Fprintf(w, "%s\t%s\n", eosPortName(pm["portNum"]), fmtStr(pm["mtu"]))
	}
	w.Flush()
	return nil
}

func eosFormatEEE(data interface{}, jsonMode bool) error {
	ports, ok := data.([]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	if jsonMode {
		eapiResult(ports, "json")
		return nil
	}
	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "Port\tEEE\tEEE LP\tActive")
	for _, p := range ports {
		pm := p.(map[string]interface{})
		if fmtInt(pm["isSFP"]) == "1" {
			fmt.Fprintf(w, "%s\tN/A\tN/A\tN/A\n", eosPortName(pm["portNum"]))
		} else {
			fmt.Fprintf(w, "%s\t%s\t%s\t%s\n",
				eosPortName(pm["portNum"]),
				fmtStr(pm["eee"]),
				fmtStr(pm["eee_lp"]),
				fmtBool(pm["active"]))
		}
	}
	w.Flush()
	return nil
}

func eosFormatMacTable(data interface{}, jsonMode bool) error {
	entries, ok := data.([]interface{})
	if !ok {
		return fmt.Errorf("unexpected response format")
	}
	if jsonMode {
		eapiResult(entries, "json")
		return nil
	}
	if len(entries) == 0 {
		fmt.Println("No MAC entries")
		return nil
	}
	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "Index\tMAC Address\tVLAN\tType\tPort")
	for _, e := range entries {
		em := e.(map[string]interface{})
		typeStr := "dynamic"
		if fmtStr(em["type"]) == "s" {
			typeStr = "static"
		}
		fmt.Fprintf(w, "%s\t%s\t%s\t%s\t%s\n",
			fmtStr(em["idx"]),
			fmtStr(em["mac"]),
			fmtStr(em["vlan"]),
			typeStr,
			eosPortName(em["port"]))
	}
	w.Flush()
	return nil
}

func fmtIntTo64(v interface{}) int64 {
	switch val := v.(type) {
	case float64:
		return int64(val)
	case json.Number:
		n, _ := val.Int64()
		return n
	}
	return 0
}
