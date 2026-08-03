package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestValidateCmdText(t *testing.T) {
	valid := []string{
		"",
		"   ",
		"stat",
		"show",
		"version",
		"help",
		"future-command foo bar",
		"preshared_key",
		"preshared_key 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
		"preshared_key 00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF",
		"hostname switch-1",
		"hostname Switch_1",
		"passwd 12345",
		"ip 192.168.1.100",
		"ip dhcp",
		"gw 192.168.1.1",
		"netmask 255.255.255.0",
		"port 5 show",
		"port 1 off",
		"port 2 10m half",
		"port 3 2g5",
		"port 4 auto full",
		"port 5 duplex",
		"port 6 duplex full",
		"port 7 name myport",
		"mtu",
		"mtu show",
		"mtu 1 9000",
		"pvid 3 100",
		"vlan 100",
		"vlan 100 d",
		"vlan 100 mgmt",
		"vlan 100 Default 1t 2u 10",
		"vlan 100 name 1t",
		"ingress t",
		"ingress 1t 2u",
		"isolate 1 2 3",
		"isolate 10 off",
		"isolate 1 show",
		"mirror status",
		"mirror off",
		"mirror 5",
		"mirror 5 1t 2r",
		"mirror 10 1t",
		"lag",
		"lag show",
		"lag 2 1 2",
		"lag 4 10",
		"laghash 0 smac dmac",
		"laghash 3 spa sip dip sport dport",
		"eee on",
		"eee status 5 2g5",
		"eee off 1 100m",
		"bw status 1",
		"bw in 5 100",
		"bw in 5 drop",
		"bw in 5 fc",
		"bw out 5 off",
		"stp",
		"stp on",
		"stp off",
		"igmp",
		"igmp on",
		"igmp show",
		"telnet on",
		"telnet off",
		"web on",
		"web off",
		"l2",
		"l2 forget",
		"l2 del 16",
		"sfp",
		"sfp 1",
		"sfp 1 10g",
		"sfp 2 100m",
		"sfp 1 auto",
		"sfp 1 describe",
		"sfp 1 dump",
		"sfp 1 save",
		"sfp 1 restore",
		"sfp 1 fix",
		"sfp 1 patch",
		"sfp 1 patch --pw 00112233",
		"sfp 1 clone --pw aabbccdd",
		"sfp 1 checksum",
		"sfp 1 checksum --fix",
		"sfp 1 checksum --fix --pw 11223344",
		"sfp 1 write 10 20",
		"sfp 1 write 3f ff --pw deadbeef",
		"sfp 1 bulk " + strings.Repeat("ab", 256),
		"regget 0bb0",
		"regget 0c",
		"regset 0b abcd1234",
		"sdsget 0 2 3",
		"sdsset 1 2 3 4",
		"phyget 0 0 0b",
		"physet 1 2 0b 1234",
		"commit",
		"reset",
	}
	for _, c := range valid {
		if err := validateCmdText(c); err != nil {
			t.Errorf("validateCmdText(%q) = %v, want nil", c, err)
		}
	}
}

func TestValidateCmdTextInvalid(t *testing.T) {
	cases := []struct {
		cmd, want string
	}{
		{"preshared_key 00112233445566778899aabbccddeeff", "64 hex"},
		{"preshared_key " + strings.Repeat("xy", 32), "hex"},
		{"preshared_key abcd abcd", "words"},
		{"hostname switch..name", "invalid character"},
		{"hostname a.b", "invalid character"},
		{"hostname " + strings.Repeat("a", 32), "too long"},
		{"hostname foo bar", "usage"},
		{"passwd 1234", "too short"},
		{"passwd " + strings.Repeat("a", 21), "too long"},
		{"passwd", "usage"},
		{"ip 300.1.1.1", "invalid IP"},
		{"ip 192.168.1.1 extra", "usage"},
		{"ip ::1", "invalid IP"},
		{"gw dhcp", "invalid IP"},
		{"netmask 256.0.0.1", "invalid IP"},
		{"port 0 show", "invalid port"},
		{"port 5", "usage"},
		{"port 5 banana", "unknown port command"},
		{"port 5 1g banana", "invalid duplex"},
		{"port 5 duplex banana", "invalid duplex"},
		{"port 5 duplex half full", "too many"},
		{"port 5 name", "usage"},
		{"port 5 name " + strings.Repeat("n", 32), "too long"},
		{"mtu 5", "usage"},
		{"mtu 1 63", "MTU"},
		{"mtu 1 16384", "MTU"},
		{"mtu 0 9000", "invalid port"},
		{"pvid 5", "usage"},
		{"pvid 5 4095", "VLAN ID"},
		{"pvid 0 100", "invalid port"},
		{"vlan 0", "VLAN ID"},
		{"vlan 4095", "VLAN ID"},
		{"vlan 100 5x", "invalid port"},
		{"vlan 100 11t", "invalid port"},
		{"vlan abc", "VLAN ID"},
		{"ingress", "usage"},
		{"ingress x", "ingress token"},
		{"ingress 1x", "ingress token"},
		{"isolate 1", "usage"},
		{"isolate 11", "invalid port"},
		{"isolate 1 show extra", "too many"},
		{"mirror", "usage"},
		{"mirror 5 1x", "mirror port"},
		{"mirror 11", "mirror port"},
		{"lag 0 1", "LAG group"},
		{"lag 5 1", "LAG group"},
		{"lag 1 11", "invalid port"},
		{"laghash", "usage"},
		{"laghash 4 smac", "LAG group"},
		{"laghash 0 foo", "bad hash type"},
		{"eee", "usage"},
		{"eee banana", "unknown eee"},
		{"eee on 5 100g", "invalid eee argument"},
		{"eee on 0", "invalid eee argument"},
		{"bw", "usage"},
		{"bw in 5", "usage"},
		{"bw status 5 100", "does not take a value"},
		{"bw in 5 zzzz", "hex"},
		{"bw up 5", "unknown bw"},
		{"stp banana", "usage"},
		{"igmp banana", "usage"},
		{"telnet", "usage"},
		{"telnet banana", "usage"},
		{"web", "usage"},
		{"web banana", "usage"},
		{"l2 del", "usage"},
		{"l2 del abc", "L2 index"},
		{"l2 forget extra", "usage"},
		{"l2 del 4096", "L2 index"},
		{"sfp 3", "SFP slot"},
		{"sfp 0 1g", "SFP slot"},
		{"sfp 1 banana", "unknown sfp"},
		{"sfp 1 1g extra", "too many"},
		{"sfp 1 write", "usage"},
		{"sfp 1 write 10", "usage"},
		{"sfp 1 write gg 20", "hex"},
		{"sfp 1 write 10 20 30", "unexpected argument"},
		{"sfp 1 patch --pw 1234", "8 hex"},
		{"sfp 1 patch --pw 12345678 extra", "unexpected argument"},
		{"sfp 1 bulk abc", "512"},
		{"sfp 1 bulk " + strings.Repeat("ab", 255) + "zz", "512"},
		{"regget zz", "hex"},
		{"regget 0bb0 1", "usage"},
		{"regset 0b", "usage"},
		{"regset 0b zz", "hex"},
		{"sdsget 0 2", "usage"},
		{"sdsget x 2 3", "sds-id"},
		{"sdsget 0 2z 3", "page"},
		{"sdsset 0 2 3", "usage"},
		{"phyget 0 0", "usage"},
		{"phyget x 0 0b", "invalid id"},
		{"physet 0 0 0b", "usage"},
		{"commit now", "no arguments"},
		{"reset now", "no arguments"},
	}
	for _, c := range cases {
		err := validateCmdText(c.cmd)
		if err == nil {
			t.Errorf("validateCmdText(%q) = nil, want error containing %q", c.cmd, c.want)
			continue
		}
		if !strings.Contains(err.Error(), c.want) {
			t.Errorf("validateCmdText(%q) = %v, want error containing %q", c.cmd, err, c.want)
		}
	}
}

func TestCmdCmdValidation(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/login":
			w.Header().Set("Set-Cookie", "session=abc")
			http.Redirect(w, r, "/index.html", http.StatusFound)
		case "/cmd":
			if r.Method == "POST" {
				w.WriteHeader(http.StatusOK)
			} else {
				http.NotFound(w, r)
			}
		default:
			http.NotFound(w, r)
		}
	}))
	defer ts.Close()

	client := NewClient(strings.TrimPrefix(ts.URL, "http://"), "1234")
	if err := client.Login(); err != nil {
		t.Fatalf("login failed: %v", err)
	}

	// Invalid command must be rejected without a POST.
	if err := cmdCmd(client, []string{"hostname", "bad..name"}, false, false); err == nil {
		t.Fatal("cmdCmd accepted invalid command without --force")
	}

	// --force bypasses validation and sends the command.
	if err := cmdCmd(client, []string{"hostname", "bad..name"}, false, true); err != nil {
		t.Fatalf("cmdCmd with --force failed: %v", err)
	}

	// Valid command is sent normally.
	if err := cmdCmd(client, []string{"hostname", "switch-1"}, false, false); err != nil {
		t.Fatalf("cmdCmd valid command failed: %v", err)
	}
}
