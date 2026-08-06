package main

import (
	"fmt"
	"strconv"
	"strings"
)

// cmdValidators maps each known device write command to its validator.
// Commands not listed here (read-only or unknown) pass through unchanged.
var cmdValidators = map[string]func([]string) error{
	"commit":        vNoArgs("commit"),
	"reset":         vNoArgs("reset"),
	"preshared_key": vPSK,
	"hostname":      vHostname,
	"passwd":        vPasswd,
	"ip":            vIPAddrCmd("ip", true),
	"gw":            vIPAddrCmd("gw", false),
	"netmask":       vIPAddrCmd("netmask", false),
	"port":          vPort,
	"mtu":           vMTU,
	"pvid":          vPvid,
	"vlan":          vVlan,
	"ingress":       vIngress,
	"isolate":       vIsolate,
	"mirror":        vMirror,
	"lag":           vLag,
	"laghash":       vLagHash,
	"eee":           vEEE,
	"bw":            vBW,
	"stp":           vStp,
	"igmp":          vIgmp,
	"lldp":          vLLDP,
	"ping":          vPing,
	"storm-control": vStorm,
	"qos":           vQos,
	"acl":           vAcl,
	"telnet":        vOnOff("telnet"),
	"web":           vOnOff("web"),
	"l2":            vL2,
	"sfp":           vSFP,
	"regget":        vRegGet,
	"regset":        vRegSet,
	"sdsget":        vSdsGet,
	"sdsset":        vSdsSet,
	"phyget":        vPhyGet,
	"physet":        vPhySet,
}

// validateCmdText checks a device CLI command string before it is sent to the
// switch.  The firmware intentionally has minimal input validation (commit
// 6a28c58: "trim input validation and shorten messages to reduce BANK2
// size"), so the host-side CLI must reject malformed input.
//
// Only known write commands are validated; unknown or read-only commands pass
// through unchanged so that future firmware commands are never blocked.
func validateCmdText(text string) error {
	words := strings.Fields(text)
	if len(words) == 0 {
		return nil
	}
	v, ok := cmdValidators[words[0]]
	if !ok {
		return nil
	}
	if err := v(words); err != nil {
		return fmt.Errorf("%s: %w", words[0], err)
	}
	return nil
}

func vNoArgs(cmd string) func([]string) error {
	return func(words []string) error {
		if len(words) != 1 {
			return fmt.Errorf("takes no arguments")
		}
		return nil
	}
}

// vOnOff requires exactly "on" or "off" (telnet, web).
func vOnOff(cmd string) func([]string) error {
	return func(words []string) error {
		if len(words) != 2 || words[1] != "on" && words[1] != "off" {
			return fmt.Errorf("usage: %s on|off", cmd)
		}
		return nil
	}
}

func vPSK(words []string) error {
	switch len(words) {
	case 1:
		return nil // clears the key
	case 2:
		return validatePSK(words[1])
	}
	return fmt.Errorf("expected 64 hex chars or no argument to clear, got %d words", len(words))
}

func vHostname(words []string) error {
	switch len(words) {
	case 1:
		return nil // show current name
	case 2:
		return validateDeviceHostname(words[1])
	}
	return fmt.Errorf("usage: hostname <name> (max 31 chars)")
}

func vPasswd(words []string) error {
	if len(words) != 2 {
		return fmt.Errorf("usage: passwd <password> (5-20 chars)")
	}
	return validatePassword(words[1])
}

// vIPAddrCmd validates ip/gw/netmask.  The device still validates these, but
// a pre-check catches typos before a malformed address is applied.
func vIPAddrCmd(cmd string, allowDHCP bool) func([]string) error {
	return func(words []string) error {
		switch len(words) {
		case 1:
			return nil // show
		case 2:
			if allowDHCP && words[1] == "dhcp" {
				return nil
			}
			return validateIPAddr(words[1])
		}
		return fmt.Errorf("usage: %s <ip-address>", cmd)
	}
}

func vPort(words []string) error {
	if len(words) < 3 {
		return fmt.Errorf("usage: port <port> [show|on|off|<speed> [half|full]|name <name>|duplex [half|full]]")
	}
	if err := validatePortNum(words[1], 9); err != nil {
		return err
	}
	switch words[2] {
	case "show":
		if len(words) > 3 {
			return fmt.Errorf("too many arguments after show")
		}
		return nil
	case "name":
		if len(words) != 4 {
			return fmt.Errorf("usage: port <port> name <name> (max 31 chars)")
		}
		if len(words[3]) > 31 {
			return fmt.Errorf("port name too long: %d chars (max 31)", len(words[3]))
		}
		return nil
	case "duplex":
		if len(words) > 4 {
			return fmt.Errorf("too many arguments after duplex")
		}
		if len(words) == 4 {
			return validateDuplex(words[3])
		}
		return nil // device defaults to half
	case "10m", "100m", "1g", "2g5", "5g", "10g", "auto", "on", "off":
		if len(words) == 4 {
			return validateDuplex(words[3])
		}
		if len(words) > 4 {
			return fmt.Errorf("too many arguments after %s", words[2])
		}
		return nil
	}
	return fmt.Errorf("unknown port command: %q (use show, on, off, <speed> [half|full], name, duplex)", words[2])
}

func vMTU(words []string) error {
	switch len(words) {
	case 1:
		return nil
	case 2:
		if words[1] == "show" {
			return nil
		}
		return fmt.Errorf("usage: mtu [show|<port> <size>]")
	case 3:
		if err := validatePortNum(words[1], 9); err != nil {
			return err
		}
		return validateMTU(words[2])
	}
	return fmt.Errorf("usage: mtu [show|<port> <size>]")
}

func vPvid(words []string) error {
	if len(words) != 3 {
		return fmt.Errorf("usage: pvid <port> <vlan-id> (1-4094)")
	}
	if err := validatePortNum(words[1], 9); err != nil {
		return err
	}
	return validateVLANID(words[2])
}

func vVlan(words []string) error {
	if len(words) < 2 {
		return fmt.Errorf("usage: vlan [show|<vlan-id> [d|mgmt|<name>|<port>[t|u]...]] (vlan-id 1-4094)")
	}
	if words[1] == "show" {
		if len(words) > 2 {
			return fmt.Errorf("too many arguments after show")
		}
		return nil
	}
	if err := validateVLANID(words[1]); err != nil {
		return err
	}
	for _, w := range words[2:] {
		switch {
		case w == "d" || w == "mgmt":
			// d and mgmt are only meaningful as the sole argument
		case w[0] >= 'a' && w[0] <= 'z' || w[0] >= 'A' && w[0] <= 'Z':
			// VLAN name token; length is not bounded on the device
		default:
			if err := validateVlanPortToken(w); err != nil {
				return err
			}
		}
	}
	return nil
}

func vIngress(words []string) error {
	if len(words) < 2 {
		return fmt.Errorf("usage: ingress [<port>]t|u|a...")
	}
	for _, w := range words[1:] {
		if err := validateIngressToken(w); err != nil {
			return err
		}
	}
	return nil
}

func vIsolate(words []string) error {
	if len(words) < 3 {
		if len(words) == 2 {
			if err := validatePortToken(words[1]); err != nil {
				return err
			}
		}
		return fmt.Errorf("usage: isolate <port> [show|off|<port>...]")
	}
	if err := validatePortToken(words[1]); err != nil {
		return err
	}
	switch words[2] {
	case "show", "off":
		if len(words) > 3 {
			return fmt.Errorf("too many arguments after %s", words[2])
		}
		return nil
	}
	for _, w := range words[2:] {
		if err := validatePortToken(w); err != nil {
			return err
		}
	}
	return nil
}

func vMirror(words []string) error {
	switch len(words) {
	case 1:
		return fmt.Errorf("usage: mirror [status|off|<mirroring-port> [<port>[t|r]]...]")
	case 2:
		if words[1] == "status" || words[1] == "off" {
			return nil
		}
	}
	for _, w := range words[1:] {
		body := strings.TrimSuffix(strings.TrimSuffix(w, "t"), "r")
		if err := validatePortToken(body); err != nil {
			return fmt.Errorf("invalid mirror port: %q", w)
		}
	}
	return nil
}

func vLag(words []string) error {
	if len(words) == 1 || len(words) == 2 && words[1] == "show" {
		return nil
	}
	if len(words[1]) != 1 || words[1][0] < '1' || words[1][0] > '4' {
		return fmt.Errorf("invalid LAG group: %q (must be 1-4)", words[1])
	}
	for _, w := range words[2:] {
		if err := validatePortToken(w); err != nil {
			return err
		}
	}
	return nil
}

func vLagHash(words []string) error {
	if len(words) < 2 {
		return fmt.Errorf("usage: laghash <group> [spa|smac|dmac|sip|dip|sport|dport]...")
	}
	if len(words[1]) != 1 || words[1][0] < '0' || words[1][0] > '3' {
		return fmt.Errorf("invalid LAG group: %q (must be 0-3)", words[1])
	}
	for _, w := range words[2:] {
		switch w {
		case "spa", "smac", "dmac", "sip", "dip", "sport", "dport":
		default:
			return fmt.Errorf("bad hash type: %q (use spa, smac, dmac, sip, dip, sport, dport)", w)
		}
	}
	return nil
}

func vEEE(words []string) error {
	if len(words) < 2 {
		return fmt.Errorf("usage: eee [on|off|status] [port] [100m|1g|2g5]")
	}
	switch words[1] {
	case "on", "off", "status":
	default:
		return fmt.Errorf("unknown eee command: %q (use on, off, status)", words[1])
	}
	for _, w := range words[2:] {
		switch w {
		case "100m", "1g", "2g5":
		default:
			if err := validatePortNum(w, 9); err != nil {
				return fmt.Errorf("invalid eee argument: %q (use a port 1-9 or 100m, 1g, 2g5)", w)
			}
		}
	}
	return nil
}

func vBW(words []string) error {
	if len(words) < 3 {
		return fmt.Errorf("usage: bw [in|out|status] <port> [<hexvalue>|off|drop|fc]")
	}
	switch words[1] {
	case "in", "out", "status":
	default:
		return fmt.Errorf("unknown bw direction: %q (use in, out, status)", words[1])
	}
	if err := validatePortNum(words[2], 9); err != nil {
		return err
	}
	switch len(words) {
	case 3:
		if words[1] == "status" {
			return nil
		}
		return fmt.Errorf("usage: bw [in|out|status] <port> [<hexvalue>|off|drop|fc]")
	case 4:
		switch words[3] {
		case "off", "drop", "fc":
			if words[1] == "in" || words[1] == "out" {
				return nil
			}
			return fmt.Errorf("bw %s does not take a value", words[1])
		}
		if words[1] == "in" || words[1] == "out" {
			return validateHexArg(words[3], 1, 4)
		}
		return fmt.Errorf("bw %s does not take a value", words[1])
	}
	return fmt.Errorf("too many arguments")
}

func vStp(words []string) error {
	if len(words) > 2 {
		return fmt.Errorf("usage: stp [on|off]")
	}
	if len(words) == 2 && words[1] != "on" && words[1] != "off" {
		return fmt.Errorf("usage: stp [on|off]")
	}
	return nil
}

func vIgmp(words []string) error {
	if len(words) == 3 && words[1] == "querier" {
		switch words[2] {
		case "on", "off", "show":
			return nil
		}
		return fmt.Errorf("usage: igmp querier on|off|show")
	}
	if len(words) == 3 && words[1] == "mld" {
		switch words[2] {
		case "on", "off", "show":
			return nil
		}
		return fmt.Errorf("usage: igmp mld on|off|show")
	}
	if len(words) > 2 {
		return fmt.Errorf("usage: igmp [on|off|show|querier on|off|show|mld on|off|show]")
	}
	if len(words) == 2 && words[1] != "on" && words[1] != "off" && words[1] != "show" {
		return fmt.Errorf("usage: igmp [on|off|show]")
	}
	return nil
}

// vLLDP validates the lldp command (LLDP neighbor discovery, Tier 2).
func vLLDP(words []string) error {
	if len(words) > 2 {
		return fmt.Errorf("usage: lldp [on|off|show]")
	}
	if len(words) == 2 && words[1] != "on" && words[1] != "off" && words[1] != "show" {
		return fmt.Errorf("usage: lldp [on|off|show]")
	}
	return nil
}

// vPing validates the ping command: exactly one dotted-quad IP address.
func vPing(words []string) error {
	if len(words) != 2 {
		return fmt.Errorf("usage: ping <ip-address>")
	}
	return validateIPAddr(words[1])
}

// vStorm validates the storm-control command (Tier 3): per-type rate
// limiting of broadcast/multicast/unknown frames on all user ports.
func vStorm(words []string) error {
	if len(words) == 2 && words[1] == "status" {
		return nil
	}
	if len(words) >= 3 && words[1] == "on" {
		if len(words) != 4 {
			return fmt.Errorf("usage: storm-control on <broadcast|multicast|dlf|unknown-mcast> <rate>[k|p]")
		}
		switch words[2] {
		case "broadcast", "multicast", "dlf", "unknown-mcast":
		default:
			return fmt.Errorf("unknown storm type: %q (use broadcast, multicast, dlf, unknown-mcast)", words[2])
		}
		return validateStormRate(words[3])
	}
	if len(words) >= 2 && words[1] == "off" {
		if len(words) == 2 {
			return nil // no type: all types
		}
		if len(words) != 3 {
			return fmt.Errorf("usage: storm-control off [broadcast|multicast|dlf|unknown-mcast|all]")
		}
		switch words[2] {
		case "broadcast", "multicast", "dlf", "unknown-mcast", "all":
			return nil
		}
		return fmt.Errorf("unknown storm type: %q (use broadcast, multicast, dlf, unknown-mcast, all)", words[2])
	}
	return fmt.Errorf("usage: storm-control on|off|status")
}

// validateStormRate checks a storm rate: decimal 1-10000000 with an
// optional k (kbps, default) or p (pps) suffix, matching the firmware.
func validateStormRate(s string) error {
	r := s
	if len(r) > 0 {
		switch r[len(r)-1] {
		case 'k', 'K', 'p', 'P':
			r = r[:len(r)-1]
		}
	}
	if r == "" {
		return fmt.Errorf("missing rate in %q (use 1-10000000 with optional k or p suffix)", s)
	}
	n := 0
	for _, c := range r {
		if c < '0' || c > '9' {
			return fmt.Errorf("invalid rate: %q (use 1-10000000 with optional k or p suffix)", s)
		}
		n = n*10 + int(c-'0')
		if n > 10000000 {
			return fmt.Errorf("rate too high: %q (max 10000000)", s)
		}
	}
	if n == 0 {
		return fmt.Errorf("rate must be > 0: %q", s)
	}
	return nil
}

// vQos validates the qos command (Tier 3): priority decision weights,
// PCP/DSCP queue maps and queue scheduling.
func vQos(words []string) error {
	if len(words) == 1 {
		return nil // plain "qos" shows the status
	}
	switch words[1] {
	case "on", "off", "status":
		if len(words) != 2 {
			return fmt.Errorf("usage: qos %s", words[1])
		}
		return nil
	case "mode":
		if len(words) != 3 {
			return fmt.Errorf("usage: qos mode pcp|dscp|both")
		}
		switch words[2] {
		case "pcp", "dscp", "both":
			return nil
		}
		return fmt.Errorf("unknown qos mode: %q (use pcp, dscp, both)", words[2])
	case "pcp", "dscp":
		if len(words) != 4 {
			return fmt.Errorf("usage: qos %s <value> <queue>", words[1])
		}
		max := 7
		if words[1] == "dscp" {
			max = 63
		}
		v, err := strconv.Atoi(words[2])
		if err != nil || v < 0 || v > max {
			return fmt.Errorf("invalid %s value: %q (must be 0-%d)", words[1], words[2], max)
		}
		q, err := strconv.Atoi(words[3])
		if err != nil || q < 0 || q > 7 {
			return fmt.Errorf("invalid queue: %q (must be 0-7)", words[3])
		}
		return nil
	case "sched":
		if len(words) < 4 || len(words) > 5 {
			return fmt.Errorf("usage: qos sched <port> strict|wfq [weight]")
		}
		if err := validatePortNum(words[2], 9); err != nil {
			return err
		}
		switch words[3] {
		case "strict":
			if len(words) != 4 {
				return fmt.Errorf("usage: qos sched <port> strict")
			}
			return nil
		case "wfq":
			if len(words) != 5 {
				return fmt.Errorf("usage: qos sched <port> wfq <weight>")
			}
			w, err := strconv.Atoi(words[4])
			if err != nil || w < 1 || w > 127 {
				return fmt.Errorf("invalid WFQ weight: %q (must be 1-127)", words[4])
			}
			return nil
		}
		return fmt.Errorf("unknown scheduler: %q (use strict or wfq)", words[3])
	}
	return fmt.Errorf("usage: qos on|off|status|mode pcp|dscp|both|pcp|dscp|sched")
}

// vAcl validates the acl command (Tier 3): ingress rules with one match
// field (mac, vlan or ip) per rule.
func vAcl(words []string) error {
	if len(words) == 1 {
		return fmt.Errorf("usage: acl on|off|show|add|del")
	}
	if len(words) == 2 {
		switch words[1] {
		case "on", "off", "show":
			return nil
		}
		return fmt.Errorf("usage: acl on|off|show|add|del")
	}
	if words[1] == "del" {
		if len(words) != 3 {
			return fmt.Errorf("usage: acl del <idx> (0-95)")
		}
		n, err := strconv.Atoi(words[2])
		if err != nil || n < 0 || n > 95 {
			return fmt.Errorf("invalid ACL rule index: %q (must be 0-95)", words[2])
		}
		return nil
	}
	if words[1] == "add" {
		if len(words) != 6 {
			return fmt.Errorf("usage: acl add <port> <permit|deny> [mac <aa:bb:cc:dd:ee:ff> | vlan <id> | ip <addr>[/<prefix>]]")
		}
		if err := validatePortNum(words[2], 9); err != nil {
			return err
		}
		switch words[3] {
		case "permit", "deny":
		default:
			return fmt.Errorf("action must be permit or deny: %q", words[3])
		}
		switch words[4] {
		case "mac":
			return validateMacAddr(words[5])
		case "vlan":
			n, err := strconv.Atoi(words[5])
			if err != nil || n < 1 || n > 4095 {
				return fmt.Errorf("invalid VLAN ID: %q (must be 1-4095)", words[5])
			}
			return nil
		case "ip":
			return validateIPOrPrefix(words[5])
		}
		return fmt.Errorf("unknown ACL match field: %q (use mac, vlan, ip)", words[4])
	}
	return fmt.Errorf("usage: acl on|off|show|add|del")
}

func vL2(words []string) error {
	switch len(words) {
	case 1:
		return nil // show learned entries
	case 2:
		if words[1] == "forget" {
			return nil
		}
		return fmt.Errorf("usage: l2 [forget|del <idx>]")
	case 3:
		if words[1] != "del" {
			return fmt.Errorf("usage: l2 [forget|del <idx>]")
		}
		return validateL2Idx(words[2])
	}
	return fmt.Errorf("usage: l2 [forget|del <idx>]")
}

func vSFP(words []string) error {
	if len(words) == 1 {
		return nil // list all slots
	}
	if err := validateSfpSlot(words[1]); err != nil {
		return err
	}
	if len(words) == 2 {
		return nil // show slot info
	}
	sub := words[2]
	switch sub {
	case "1g", "2g5", "10g", "100m", "auto":
		if len(words) > 3 {
			return fmt.Errorf("too many arguments after %s", sub)
		}
		return nil
	case "describe", "dump", "save", "restore", "fix", "patch", "clone":
		return validatePwArgs(words[3:])
	case "checksum":
		return validatePwArgs(words[3:]) // [--fix] [--pw <hex8>]
	case "write":
		if len(words) < 5 {
			return fmt.Errorf("usage: sfp <slot> write <offset> <value> [--pw <hex8>]")
		}
		if err := validateHexArg(words[3], 1, 1); err != nil {
			return fmt.Errorf("invalid write offset: %w", err)
		}
		if err := validateHexArg(words[4], 1, 1); err != nil {
			return fmt.Errorf("invalid write value: %w", err)
		}
		return validatePwArgs(words[5:])
	case "bulk":
		if len(words) != 4 {
			return fmt.Errorf("usage: sfp <slot> bulk <512hexchars>")
		}
		if len(words[3]) != 512 || !isHexStr(words[3]) {
			return fmt.Errorf("bulk data must be exactly 512 hex chars")
		}
		return nil
	}
	return fmt.Errorf("unknown sfp subcommand: %q (use 1g, 2g5, 10g, 100m, auto, describe, dump, save, restore, fix, patch, clone, checksum, write, bulk)", sub)
}

// validatePwArgs checks optional "--pw <hex8>" arguments.  Any other tokens
// are rejected to catch typos in SFP commands.
func validatePwArgs(words []string) error {
	if len(words) == 0 {
		return nil
	}
	if words[0] == "--fix" {
		return validatePwArgs(words[1:])
	}
	if len(words) == 2 && words[0] == "--pw" {
		if len(words[1]) != 8 || !isHexStr(words[1]) {
			return fmt.Errorf("--pw must be 8 hex chars")
		}
		return nil
	}
	return fmt.Errorf("unexpected argument: %q (use [--fix] [--pw <hex8>])", words[0])
}

func vRegGet(words []string) error {
	if len(words) != 2 {
		return fmt.Errorf("usage: regget <hexvalue>")
	}
	return validateHexArg(words[1], 1, 2)
}

func vRegSet(words []string) error {
	if len(words) != 3 {
		return fmt.Errorf("usage: regset <hexvalue> <hexvalue>")
	}
	if err := validateHexArg(words[1], 1, 2); err != nil {
		return fmt.Errorf("invalid register: %w", err)
	}
	return validateHexArg(words[2], 1, 4)
}

func vSdsGet(words []string) error {
	if len(words) != 4 {
		return fmt.Errorf("usage: sdsget <sds-id> <hex:page> <hex:reg>")
	}
	return validateSdsArgs(words[1:])
}

func vSdsSet(words []string) error {
	if len(words) != 5 {
		return fmt.Errorf("usage: sdsset <sds-id> <hex:page> <hex:reg> <hex:val>")
	}
	if err := validateSdsArgs(words[1:]); err != nil {
		return err
	}
	return validateHexArg(words[4], 1, 2)
}

func validateSdsArgs(words []string) error {
	if _, err := strconv.Atoi(words[0]); err != nil {
		return fmt.Errorf("invalid sds-id: %q (must be a number 0-255)", words[0])
	}
	if err := validateHexArg(words[1], 1, 1); err != nil {
		return fmt.Errorf("invalid page: %w", err)
	}
	return validateHexArg(words[2], 1, 1)
}

func vPhyGet(words []string) error {
	if len(words) != 4 {
		return fmt.Errorf("usage: phyget <phy-id> <dev-id> <hex:reg>")
	}
	return validatePhyArgs(words[1:])
}

func vPhySet(words []string) error {
	if len(words) != 5 {
		return fmt.Errorf("usage: physet <phy-id> <dev-id> <hex:reg> <hex:val>")
	}
	if err := validatePhyArgs(words[1:]); err != nil {
		return err
	}
	return validateHexArg(words[4], 1, 2)
}

func validatePhyArgs(words []string) error {
	for _, w := range words[:2] {
		if _, err := strconv.Atoi(w); err != nil {
			return fmt.Errorf("invalid id: %q (must be a number 0-255)", w)
		}
	}
	return validateHexArg(words[2], 1, 2)
}
