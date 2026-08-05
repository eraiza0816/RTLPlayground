package main

import "fmt"

// Device command help tables, ported from the firmware's cmd_help.c
// (removed in v0.2.24). The firmware console no longer provides
// ?/help or Tab completion; rtlpctl provides them host-side.

type deviceCmd struct {
	name string
	desc string
}

type deviceCmdGroup struct {
	name string
	desc string
	sub  []deviceCmd
}

var sfpDeviceCmds = []deviceCmd{
	{"1g", "Force 1Gbps speed"},
	{"2g5", "Force 2.5Gbps speed"},
	{"10g", "Force 10Gbps speed"},
	{"100m", "Force 100Mbps speed"},
	{"auto", "Auto-negotiate speed"},
	{"describe", "Show detailed SFP module information"},
	{"dump", "Hex dump of SFP EEPROM"},
	{"save", "Save SFP EEPROM to flash backup"},
	{"restore", "Restore SFP EEPROM from flash backup"},
	{"fix", "Fix SFP EEPROM for copper passthrough"},
	{"patch", "Patch SFP EEPROM"},
	{"clone", "Clone SFP EEPROM from flash buffer"},
	{"checksum", "Verify or fix SFP EEPROM checksums"},
	{"write", "Write a byte to SFP EEPROM"},
	{"bulk", "Bulk-write SFP EEPROM from hex data"},
}

var portDeviceCmds = []deviceCmd{
	{"show", "Show port status and PHY information"},
	{"name", "Set custom port name"},
	{"10m", "Force 10Mbps speed"},
	{"100m", "Force 100Mbps speed"},
	{"1g", "Force 1Gbps speed"},
	{"2g5", "Force 2.5Gbps speed"},
	{"5g", "Force 5Gbps speed"},
	{"10g", "Force 10Gbps speed"},
	{"auto", "Auto-negotiate speed"},
	{"on", "Enable port"},
	{"off", "Disable port"},
	{"duplex", "Set duplex mode (half/full)"},
}

var vlanDeviceCmds = []deviceCmd{
	{"show", "Show all VLANs or members of a VLAN"},
	{"mgmt", "Set management VLAN ID"},
	{"d", "Delete a VLAN"},
}

var flashDeviceCmds = []deviceCmd{
	{"s", "Read security registers"},
	{"j", "Read JEDEC ID"},
	{"u", "Read unique ID"},
}

var l2DeviceCmds = []deviceCmd{
	{"forget", "Flush dynamically learned L2 entries"},
	{"del", "Delete a specific L2 entry"},
}

var igmpDeviceCmds = []deviceCmd{
	{"on", "Enable IGMP snooping"},
	{"off", "Disable IGMP snooping"},
	{"show", "Show IGMP snooping status"},
}

var stpDeviceCmds = []deviceCmd{
	{"on", "Enable Spanning Tree Protocol"},
	{"off", "Disable Spanning Tree Protocol"},
}

var isolateDeviceCmds = []deviceCmd{
	{"show", "Show isolation configuration"},
	{"off", "Disable port isolation"},
}

var ingressDeviceCmds = []deviceCmd{
	{"u", "Allow untagged frames only"},
	{"t", "Allow tagged frames only"},
	{"a", "Allow any frames"},
}

var mirrorDeviceCmds = []deviceCmd{
	{"status", "Show mirroring status"},
	{"off", "Disable port mirroring"},
}

var lagDeviceCmds = []deviceCmd{
	{"show", "Show LAG status and member ports"},
}

var laghashDeviceCmds = []deviceCmd{
	{"spa", "Source port number"},
	{"smac", "Source MAC address"},
	{"dmac", "Destination MAC address"},
	{"sip", "Source IP address"},
	{"dip", "Destination IP address"},
	{"sport", "Source TCP/UDP port"},
	{"dport", "Destination TCP/UDP port"},
}

var eeeDeviceCmds = []deviceCmd{
	{"on", "Enable Energy Efficient Ethernet"},
	{"off", "Disable Energy Efficient Ethernet"},
	{"status", "Show EEE status"},
}

var bwDeviceCmds = []deviceCmd{
	{"in", "Configure ingress bandwidth"},
	{"out", "Configure egress bandwidth"},
	{"status", "Show bandwidth control status"},
}

var telnetDeviceCmds = []deviceCmd{
	{"on", "Enable telnet server"},
	{"off", "Disable telnet server"},
}

var webDeviceCmds = []deviceCmd{
	{"on", "Enable web interface"},
	{"off", "Disable web interface"},
}

var mtuDeviceCmds = []deviceCmd{
	{"show", "Show MTU for all ports"},
}

var deviceTopCmds = []deviceCmdGroup{
	{"reset", "Perform software reset of the switch", nil},
	{"sfp", "SFP module control and configuration", sfpDeviceCmds},
	{"stat", "Show port statistics and packet counters", nil},
	{"flash", "Read flash metadata (security/JEDEC/UID)", flashDeviceCmds},
	{"port", "Port speed, duplex, and name configuration", portDeviceCmds},
	{"mtu", "Per-port maximum frame size (MTU)", mtuDeviceCmds},
	{"ip", "Show or set IP address, enable DHCP", nil},
	{"gw", "Show or set default gateway", nil},
	{"netmask", "Show or set network mask", nil},
	{"l2", "L2 MAC address table show, forget, delete", l2DeviceCmds},
	{"igmp", "IGMP snooping control", igmpDeviceCmds},
	{"stp", "Spanning Tree Protocol control", stpDeviceCmds},
	{"pvid", "Set port VLAN ID (PVID)", nil},
	{"vlan", "VLAN create, delete, show, and management", vlanDeviceCmds},
	{"isolate", "Port isolation configuration", isolateDeviceCmds},
	{"ingress", "Ingress VLAN filter mode", ingressDeviceCmds},
	{"mirror", "Port mirroring configuration", mirrorDeviceCmds},
	{"lag", "Link Aggregation Group configuration", lagDeviceCmds},
	{"laghash", "LAG hash algorithm selection", laghashDeviceCmds},
	{"sds", "Show SerDes mode register", nil},
	{"gpio", "Read and print GPIO input status", nil},
	{"regget", "Read switch register by address (hex)", nil},
	{"regset", "Write switch register by address (hex)", nil},
	{"sdsget", "Read SerDes register", nil},
	{"sdsset", "Write SerDes register", nil},
	{"phyget", "Read PHY register (Clause-45)", nil},
	{"physet", "Write PHY register (Clause-45)", nil},
	{"rnd", "Generate hardware random number", nil},
	{"passwd", "Set web interface password", nil},
	{"preshared_key", "Set the /enc pre-shared key (64 hex chars)", nil},
	{"hostname", "Show or set device hostname", nil},
	{"eee", "Energy Efficient Ethernet control", eeeDeviceCmds},
	{"bw", "Per-port bandwidth control", bwDeviceCmds},
	{"telnet", "Telnet server control", telnetDeviceCmds},
	{"web", "Web interface control", webDeviceCmds},
	{"commit", "Save running configuration to flash", nil},
	{"show", "Show system information", nil},
	{"version", "Print software version and build info", nil},
	{"time", "Show internal tick and hardware counters", nil},
	{"history", "Show command history", nil},
	{"xmodem", "Receive firmware update via XMODEM (serial)", nil},
}

// deviceCmdGroupByName returns the group for the given top-level command.
func deviceCmdGroupByName(name string) *deviceCmdGroup {
	for i := range deviceTopCmds {
		if deviceTopCmds[i].name == name {
			return &deviceTopCmds[i]
		}
	}
	return nil
}

// printDeviceHelp prints the top-level device command list with descriptions.
func printDeviceHelp() {
	for _, g := range deviceTopCmds {
		fmt.Printf("  %-10s %s\n", g.name, g.desc)
	}
}

// printDeviceSubHelp prints the sub-commands of a top-level command.
func printDeviceSubHelp(group *deviceCmdGroup) {
	if group.sub == nil {
		fmt.Printf("  %s: (no sub-commands)\n", group.name)
		return
	}
	for _, c := range group.sub {
		fmt.Printf("  %-10s %s\n", c.name, c.desc)
	}
}

// deviceCompletions returns command names matching the prefix. The first
// word of the line is completed against the top-level command table, later
// words against the first command's sub-command table.
func deviceCompletions(words []string, prefix string) []string {
	var table []deviceCmd
	if len(words) == 0 {
		for _, g := range deviceTopCmds {
			table = append(table, deviceCmd{g.name, g.desc})
		}
	} else if g := deviceCmdGroupByName(words[0]); g != nil {
		table = g.sub
	}
	var out []string
	for _, c := range table {
		if len(prefix) <= len(c.name) && c.name[:len(prefix)] == prefix {
			out = append(out, c.name)
		}
	}
	return out
}
