package main

import (
	"bytes"
	"encoding/hex"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// testPSKHex is the shared PSK used by the mock /enc endpoint and the
// --psk flag in tests that exercise the encrypted API.
const testPSKHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"

// mockCmdBodies records POST /cmd bodies so tests can assert what the
// arista emulation layer actually sends to the switch.
var mockCmdBodies []string

func TestSplitArgs(t *testing.T) {
	tests := []struct {
		input string
		want  []string
	}{
		{"status", []string{"status"}},
		{"vlan 100", []string{"vlan", "100"}},
		{"vlan list", []string{"vlan", "list"}},
		{"cmd \"ip 192.168.1.1\"", []string{"cmd", "ip 192.168.1.1"}},
		{"cmd 'ip 192.168.1.1'", []string{"cmd", "ip 192.168.1.1"}},
		{"l2 delete 0x10", []string{"l2", "delete", "0x10"}},
		{"  spaced  args  ", []string{"spaced", "args"}},
		{"", nil},
	}
	for _, tt := range tests {
		got := splitArgs(tt.input)
		if !stringSliceEqual(got, tt.want) {
			t.Errorf("splitArgs(%q) = %v, want %v", tt.input, got, tt.want)
		}
	}
}

func TestFilterFlags(t *testing.T) {
	tests := []struct {
		args     []string
		wantCfg  config
		wantArgs []string
	}{
		{
			args:     []string{"--host", "10.0.0.1", "status"},
			wantCfg:  config{host: "10.0.0.1"},
			wantArgs: []string{"status"},
		},
		{
			args:     []string{"--host=10.0.0.1", "--password", "abc", "--json", "info"},
			wantCfg:  config{host: "10.0.0.1", password: "abc", jsonMode: true},
			wantArgs: []string{"info"},
		},
		{
			args:     []string{"vlan", "100"},
			wantCfg:  config{},
			wantArgs: []string{"vlan", "100"},
		},
		{
			args:     []string{"--password=secret"},
			wantCfg:  config{password: "secret"},
			wantArgs: nil,
		},
	}
	for _, tt := range tests {
		cfg := config{}
		got := filterFlags(tt.args, &cfg)
		if cfg.host != tt.wantCfg.host || cfg.password != tt.wantCfg.password || cfg.jsonMode != tt.wantCfg.jsonMode {
			t.Errorf("filterFlags(%v) cfg = %+v, want %+v", tt.args, cfg, tt.wantCfg)
		}
		if !stringSliceEqual(got, tt.wantArgs) {
			t.Errorf("filterFlags(%v) args = %v, want %v", tt.args, got, tt.wantArgs)
		}
	}
}

func TestFmtLink(t *testing.T) {
	tests := []struct {
		v    interface{}
		want string
	}{
		{float64(0), "down"},
		{float64(1), "10M"},
		{float64(2), "100M"},
		{float64(3), "1G"},
		{float64(5), "10G"},
		{float64(6), "2.5G"},
		{float64(7), "5G"},
		{float64(4), "link(4)"},
		{float64(99), "link(99)"},
	}
	for _, tt := range tests {
		got := fmtLink(tt.v)
		if got != tt.want {
			t.Errorf("fmtLink(%v) = %q, want %q", tt.v, got, tt.want)
		}
	}
}

func TestFmtBool(t *testing.T) {
	tests := []struct {
		v    interface{}
		want string
	}{
		{float64(1), "yes"},
		{float64(0), "no"},
		{true, "yes"},
		{false, "no"},
	}
	for _, tt := range tests {
		got := fmtBool(tt.v)
		if got != tt.want {
			t.Errorf("fmtBool(%v) = %q, want %q", tt.v, got, tt.want)
		}
	}
}

func stringSliceEqual(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

func TestEnvOr(t *testing.T) {
	os.Setenv("TEST_RTLP_ENV", "hello")
	defer os.Unsetenv("TEST_RTLP_ENV")
	if got := envOr("TEST_RTLP_ENV", "fallback"); got != "hello" {
		t.Errorf("envOr = %q, want %q", got, "hello")
	}
	if got := envOr("TEST_RTLP_NONEXIST", "fallback"); got != "fallback" {
		t.Errorf("envOr = %q, want %q", got, "fallback")
	}
}

func TestMatchCmd(t *testing.T) {
	tests := []struct {
		input string
		full  string
		want  bool
	}{
		{"show", "show", true},
		{"sh", "show", true},
		{"s", "show", true},
		{"showw", "show", false},
		{"SHOW", "show", true},
		{"sh", "SHOW", true},
		{"configure", "configure", true},
		{"conf", "configure", true},
		{"running-config", "running-config", true},
		{"run", "running-config", true},
		{"interfaces", "interfaces", true},
		{"int", "interfaces", true},
		{"", "show", false},
		{"show", "", false},
		{"status", "status", true},
		{"stat", "status", true},
	}
	for _, tt := range tests {
		got := matchCmd(tt.input, tt.full)
		if got != tt.want {
			t.Errorf("matchCmd(%q, %q) = %v, want %v", tt.input, tt.full, got, tt.want)
		}
	}
}

func TestValidateMode(t *testing.T) {
	for _, m := range []string{"default", "arista"} {
		if err := validateMode(m); err != nil {
			t.Errorf("validateMode(%q) unexpected error: %v", m, err)
		}
	}
	for _, m := range []string{"", "Arista", "foo", "eos"} {
		if err := validateMode(m); err == nil {
			t.Errorf("validateMode(%q) expected error", m)
		}
	}
}

func TestValidateFile(t *testing.T) {
	tmp := t.TempDir()
	file := tmp + "/firmware.bin"
	if err := os.WriteFile(file, []byte("test"), 0644); err != nil {
		t.Fatal(err)
	}
	if err := validateFile(file); err != nil {
		t.Errorf("validateFile(%q) unexpected error: %v", file, err)
	}
	for _, p := range []string{"", "/nonexistent/path.bin", tmp, tmp + "/subdir"} {
		if err := validateFile(p); err == nil {
			t.Errorf("validateFile(%q) expected error", p)
		}
	}
}

func TestValidateHost(t *testing.T) {
	valid := []string{
		"192.168.1.1",
		"10.0.0.1",
		"::1",
		"[::1]",
		"[2001:db8::1]",
		"[2001:db8::1]:8080",
		"192.168.1.1:8080",
		"host.example.com",
		"localhost",
		"rtl-switch-1",
	}
	for _, h := range valid {
		if err := validateHost(h); err != nil {
			t.Errorf("validateHost(%q) unexpected error: %v", h, err)
		}
	}

	invalid := []string{
		"",
		"http://192.168.1.1",
		"https://host.example.com",
		"1.2.3.4:",
		"1.2.3.4:99999",
		"1.2.3.4:abc",
		"[::1",
		"[::1]extra",
		"host name",
		"-badhost",
		"bad-.host",
	}
	for _, h := range invalid {
		if err := validateHost(h); err == nil {
			t.Errorf("validateHost(%q) expected error", h)
		}
	}
}

func TestValidateCountersPort(t *testing.T) {
	for _, p := range []string{"1", "2", "8"} {
		if err := validateCountersPort(p); err != nil {
			t.Errorf("validateCountersPort(%q) unexpected error: %v", p, err)
		}
	}
	for _, p := range []string{"0", "9", "10", "12", "abc", "", "1 "} {
		if err := validateCountersPort(p); err == nil {
			t.Errorf("validateCountersPort(%q) expected error", p)
		}
	}
}

func TestValidateVLANID(t *testing.T) {
	for _, v := range []string{"1", "100", "4094"} {
		if err := validateVLANID(v); err != nil {
			t.Errorf("validateVLANID(%q) unexpected error: %v", v, err)
		}
	}
	for _, v := range []string{"0", "4095", "4096", "100.5", "abc", "", "-1"} {
		if err := validateVLANID(v); err == nil {
			t.Errorf("validateVLANID(%q) expected error", v)
		}
	}
}

func TestValidateL2Idx(t *testing.T) {
	for _, v := range []string{"0", "16", "4095"} {
		if err := validateL2Idx(v); err != nil {
			t.Errorf("validateL2Idx(%q) unexpected error: %v", v, err)
		}
	}
	for _, v := range []string{"4096", "0x10", "abc", "", "-1"} {
		if err := validateL2Idx(v); err == nil {
			t.Errorf("validateL2Idx(%q) expected error", v)
		}
	}
}

func TestLoadDotEnv(t *testing.T) {
	content := "# comment\nRTLP_HOST=10.0.0.1\nRTLP_PASSWORD=secret\nQUOTED=\"val\"\nEXPORTED=ok\n"
	tmp := t.TempDir() + "/.env"
	os.WriteFile(tmp, []byte(content), 0644)

	loadDotEnv(tmp)
	if got := os.Getenv("RTLP_HOST"); got != "10.0.0.1" {
		t.Errorf("RTLP_HOST = %q", got)
	}
	if got := os.Getenv("RTLP_PASSWORD"); got != "secret" {
		t.Errorf("RTLP_PASSWORD = %q", got)
	}
	if got := os.Getenv("QUOTED"); got != "val" {
		t.Errorf("QUOTED = %q", got)
	}
	if got := os.Getenv("EXPORTED"); got != "ok" {
		t.Errorf("EXPORTED = %q", got)
	}
	os.Unsetenv("RTLP_HOST")
	os.Unsetenv("RTLP_PASSWORD")
	os.Unsetenv("QUOTED")
	os.Unsetenv("EXPORTED")

	loadDotEnv("/nonexistent/.env")
}

func TestPrintJSON(t *testing.T) {
	data := map[string]interface{}{"key": "value"}
	old := os.Stdout
	r, w, _ := os.Pipe()
	os.Stdout = w
	printJSON(data)
	w.Close()
	var buf bytes.Buffer
	buf.ReadFrom(r)
	os.Stdout = old
	out := buf.String()
	if !strings.Contains(out, `"key"`) || !strings.Contains(out, `"value"`) {
		t.Errorf("printJSON output missing expected content: %s", out)
	}
}

func TestFmtInt(t *testing.T) {
	if got := fmtInt(float64(42)); got != "42" {
		t.Errorf("fmtInt(42) = %q", got)
	}
}

func TestFmtStr(t *testing.T) {
	if got := fmtStr("hello"); got != "hello" {
		t.Errorf("fmtStr = %q", got)
	}
	if got := fmtStr(nil); got != "" {
		t.Errorf("fmtStr(nil) = %q", got)
	}
	if got := fmtStr(42); got != "42" {
		t.Errorf("fmtStr(42) = %q", got)
	}
}

func TestFmtCounter(t *testing.T) {
	if got := fmtCounter("abc123"); got != "abc123" {
		t.Errorf("fmtCounter = %q", got)
	}
	if got := fmtCounter(""); got != "0" {
		t.Errorf("fmtCounter('') = %q", got)
	}
}

func TestClientEndpoints(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/login" {
			pwd := r.FormValue("pwd")
			if pwd == "test123" {
				w.Header().Set("Set-Cookie", "session=abc123def456; SameSite=Strict")
				http.Redirect(w, r, "/index.html", http.StatusFound)
			} else {
				http.Redirect(w, r, "/login.html", http.StatusFound)
			}
			return
		}
		c, err := r.Cookie("session")
		if err != nil || c.Value != "abc123def456" {
			http.Error(w, "Unauthorized", http.StatusUnauthorized)
			return
		}
		switch r.URL.Path {
		case "/status.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[{"portNum":1,"name":"Port 1","link":4,"enabled":1,"txG":"1a2b","txB":"0","rxG":"3c4d","rxB":"0"}]`)
		case "/information.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `{"ip_address":"10.0.0.1","hostname":"mock-host","sw_ver":"v1.0","hw_ver":"TEST"}`)
		case "/vlan.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `{"members":"0x0001","name":"default","pvid":"0x0001"}`)
		case "/vlanlist":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[{"id":1,"name":"default"},{"id":100,"name":"test"}]`)
		case "/eee.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[{"portNum":1,"isSFP":0,"eee":"111","eee_lp":"000","active":1}]`)
		case "/bandwidth.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[{"portNum":1,"iLimited":1,"iBW":"123456","iFC":0,"eLimited":0,"eBW":"000000"}]`)
		case "/mirror.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `{"enabled":1,"mPort":2,"mirror_tx":"0001","mirror_rx":"0002"}`)
		case "/lag.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[{"lagNum":0,"members":"0001","hash":"00"}]`)
		case "/mtu.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[{"portNum":1,"mtu":"5ee"}]`)
		case "/sfp_diag.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[{"portNum":7,"sfp_options":"0x40","sfp_temp":"0x2a2a","sfp_vcc":"0x3030","sfp_txbias":"0x2d2d","sfp_txpower":"0x3030","sfp_rxpower":"0x3030","sfp_state":"0x03"}]`)
		case "/l2.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[{"mac":"00:01:2f:00:00:01","vlan":"001","type":"l","port":7,"idx":"0x10"}]`)
		case "/l2_del.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `{"result":1}`)
		case "/config":
			w.Header().Set("Content-Type", "text/plain")
			fmt.Fprint(w, "ip 192.168.1.1\nnetmask 255.255.255.0\n")
		case "/cmd_log":
			w.Header().Set("Content-Type", "text/plain")
			fmt.Fprint(w, "ip 192.168.1.1\nstatus\n")
		case "/cmd_log_clear":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[{"portNum":1,"mtu":"5ee"}]`)
		case "/counters.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `["0x001","0x002","0x003"]`)
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

	host := strings.TrimPrefix(ts.URL, "http://")
	client := NewClient(host, "test123")

	if err := client.Login(); err != nil {
		t.Fatalf("login failed: %v", err)
	}

	t.Run("status", func(t *testing.T) {
		data, err := client.GetJSON("/status.json")
		if err != nil {
			t.Fatal(err)
		}
		ports, ok := data.([]interface{})
		if !ok || len(ports) == 0 {
			t.Fatal("expected array")
		}
	})

	t.Run("info", func(t *testing.T) {
		data, err := client.GetJSON("/information.json")
		if err != nil {
			t.Fatal(err)
		}
		m, ok := data.(map[string]interface{})
		if !ok || fmtStr(m["ip_address"]) != "10.0.0.1" {
			t.Fatal("unexpected info response")
		}
	})

	t.Run("vlan", func(t *testing.T) {
		data, err := client.GetJSON("/vlan.json?vid=1")
		if err != nil {
			t.Fatal(err)
		}
		m, ok := data.(map[string]interface{})
		if !ok || fmtStr(m["name"]) != "default" {
			t.Fatal("unexpected vlan response")
		}
	})

	t.Run("vlanlist", func(t *testing.T) {
		data, err := client.GetJSON("/vlanlist")
		if err != nil {
			t.Fatal(err)
		}
		list, ok := data.([]interface{})
		if !ok || len(list) != 2 {
			t.Fatal("unexpected vlanlist response")
		}
	})

	t.Run("eee", func(t *testing.T) {
		data, err := client.GetJSON("/eee.json")
		if err != nil {
			t.Fatal(err)
		}
		_, ok := data.([]interface{})
		if !ok {
			t.Fatal("expected array")
		}
	})

	t.Run("bandwidth", func(t *testing.T) {
		data, err := client.GetJSON("/bandwidth.json")
		if err != nil {
			t.Fatal(err)
		}
		_, ok := data.([]interface{})
		if !ok {
			t.Fatal("expected array")
		}
	})

	t.Run("mirror", func(t *testing.T) {
		data, err := client.GetJSON("/mirror.json")
		if err != nil {
			t.Fatal(err)
		}
		m, ok := data.(map[string]interface{})
		if !ok || fmtInt(m["mPort"]) != "2" {
			t.Fatal("unexpected mirror response")
		}
	})

	t.Run("lag", func(t *testing.T) {
		data, err := client.GetJSON("/lag.json")
		if err != nil {
			t.Fatal(err)
		}
		_, ok := data.([]interface{})
		if !ok {
			t.Fatal("expected array")
		}
	})

	t.Run("mtu", func(t *testing.T) {
		data, err := client.GetJSON("/mtu.json")
		if err != nil {
			t.Fatal(err)
		}
		_, ok := data.([]interface{})
		if !ok {
			t.Fatal("expected array")
		}
	})

	t.Run("sfp_diag", func(t *testing.T) {
		data, err := client.GetJSON("/sfp_diag.json")
		if err != nil {
			t.Fatal(err)
		}
		arr, ok := data.([]interface{})
		if !ok || len(arr) != 1 {
			t.Fatal("unexpected sfp_diag response")
		}
		pm := arr[0].(map[string]interface{})
		if fmtStr(pm["sfp_temp"]) != "0x2a2a" {
			t.Fatal("unexpected sfp_diag fields")
		}
	})

	t.Run("l2", func(t *testing.T) {
		data, err := client.GetJSON("/l2.json?idx=0")
		if err != nil {
			t.Fatal(err)
		}
		_, ok := data.([]interface{})
		if !ok {
			t.Fatal("expected array")
		}
	})

	t.Run("l2_del", func(t *testing.T) {
		data, err := client.GetJSON("/l2_del.json?idx=16")
		if err != nil {
			t.Fatal(err)
		}
		m, ok := data.(map[string]interface{})
		if !ok || fmtInt(m["result"]) != "1" {
			t.Fatal("unexpected l2_del response")
		}
	})

	t.Run("config_text", func(t *testing.T) {
		text, err := client.GetText("/config")
		if err != nil {
			t.Fatal(err)
		}
		if !strings.Contains(text, "ip 192.168.1.1") {
			t.Fatal("unexpected config response")
		}
	})

	t.Run("cmd_log", func(t *testing.T) {
		text, err := client.GetText("/cmd_log")
		if err != nil {
			t.Fatal(err)
		}
		if !strings.Contains(text, "status") {
			t.Fatal("unexpected cmd_log response")
		}
	})

	t.Run("counters", func(t *testing.T) {
		data, err := client.GetJSON("/counters.json?port=1")
		if err != nil {
			t.Fatal(err)
		}
		arr, ok := data.([]interface{})
		if !ok || len(arr) != 3 {
			t.Fatal("unexpected counters response")
		}
	})

	t.Run("cmd_post", func(t *testing.T) {
		err := client.PostRaw("/cmd", "text/plain", strings.NewReader("status"))
		if err != nil {
			t.Fatal(err)
		}
	})

	t.Run("unauthorized", func(t *testing.T) {
		c2 := NewClient(host, "wrong")
		c2.http.Jar = nil
		_, err := c2.GetJSON("/status.json")
		if err == nil || !strings.Contains(err.Error(), "unauthorized") {
			t.Fatalf("expected unauthorized error, got: %v", err)
		}
	})
}

func TestLoginFailure(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/login" {
			http.Redirect(w, r, "/login.html", http.StatusFound)
		}
	}))
	defer ts.Close()

	host := strings.TrimPrefix(ts.URL, "http://")
	client := NewClient(host, "wrong")
	if err := client.Login(); err == nil {
		t.Fatal("expected login failure")
	}
}

func TestAuthRequiredEndpoints(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, "Unauthorized", http.StatusUnauthorized)
	}))
	defer ts.Close()

	host := strings.TrimPrefix(ts.URL, "http://")
	client := NewClient(host, "")
	_, err := client.GetJSON("/status.json")
	if err == nil || !strings.Contains(err.Error(), "unauthorized") {
		t.Fatalf("expected unauthorized, got: %v", err)
	}
}

func TestCmdResetNoResponse(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// The firmware closes the connection without sending an HTTP response.
		hj, ok := w.(http.Hijacker)
		if !ok {
			t.Fatal("server does not support hijacking")
		}
		conn, _, err := hj.Hijack()
		if err != nil {
			t.Fatal(err)
		}
		conn.Close()
	}))
	defer ts.Close()

	host := strings.TrimPrefix(ts.URL, "http://")
	client := NewClient(host, "")

	old := os.Stdout
	r, w, _ := os.Pipe()
	os.Stdout = w
	err := cmdReset(client, nil, false)
	w.Close()
	os.Stdout = old
	var buf bytes.Buffer
	io.Copy(&buf, r)

	if err != nil {
		t.Fatalf("cmdReset should succeed even without an HTTP response, got: %v", err)
	}
	if !strings.Contains(buf.String(), "reset command sent") {
		t.Errorf("expected success message, got: %q", buf.String())
	}
}

func TestBinaryEndToEnd(t *testing.T) {
	if testing.Short() {
		t.Skip("skipping binary integration test in short mode")
	}

	bin, err := filepath.Abs("rtlpctl")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(bin); os.IsNotExist(err) {
		t.Skip("rtlpctl binary not built, skipping")
	}

	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/login" {
			pwd := r.FormValue("pwd")
			if pwd == "test123" {
				w.Header().Set("Set-Cookie", "session=abc123def456; SameSite=Strict")
				http.Redirect(w, r, "/index.html", http.StatusFound)
			} else {
				http.Redirect(w, r, "/login.html", http.StatusFound)
			}
			return
		}
		c, err := r.Cookie("session")
		if err != nil || c.Value != "abc123def456" {
			http.Error(w, "Unauthorized", http.StatusUnauthorized)
			return
		}
		switch r.URL.Path {
		case "/status.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[{"portNum":1,"name":"Port 1","link":4,"enabled":1,"txG":"1a2b","txB":"0","rxG":"3c4d","rxB":"0"}]`)
		case "/information.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `{"ip_address":"10.0.0.1","hostname":"mock-host","sw_ver":"v1.0","hw_ver":"TEST"}`)
		case "/vlanlist":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[{"id":1,"name":"default"},{"id":100,"name":"test"}]`)
		case "/cmd_log":
			w.Header().Set("Content-Type", "text/plain")
			fmt.Fprint(w, "ip 192.168.1.1\nstatus\n")
		case "/cmd_log_clear":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[]`)
		case "/sfp_diag.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[{"portNum":7,"sfp_options":"0x40","sfp_temp":"0x2a2a","sfp_vcc":"0x3030","sfp_txbias":"0x2d2d","sfp_txpower":"0x3030","sfp_rxpower":"0x3030","sfp_state":"0x03"}]`)
		default:
			w.Header().Set("Content-Type", "text/plain")
			fmt.Fprint(w, "mock")
		}
	}))
	defer ts.Close()

	host := strings.TrimPrefix(ts.URL, "http://")

	t.Run("help", func(t *testing.T) {
		out, err := exec.Command(bin, "--help").Output()
		if err != nil {
			t.Fatal(err)
		}
		for _, want := range []string{"status", "info", "vlan", "login", "Interactive mode"} {
			if !strings.Contains(string(out), want) {
				t.Errorf("--help missing %q", want)
			}
		}
	})

	t.Run("status_formatted", func(t *testing.T) {
		out, err := exec.Command(bin, "--host", host, "--password", "test123", "status").CombinedOutput()
		if err != nil {
			t.Fatalf("status failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "Port 1") {
			t.Errorf("expected 'Port 1' in output, got: %s", out)
		}
		if strings.Contains(string(out), "{") {
			t.Errorf("expected formatted output, not JSON: %s", out)
		}
	})

	t.Run("status_json", func(t *testing.T) {
		out, err := exec.Command(bin, "--host", host, "--password", "test123", "--json", "status").CombinedOutput()
		if err != nil {
			t.Fatalf("status --json failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), `"portNum"`) {
			t.Errorf("expected JSON output, got: %s", out)
		}
	})

	t.Run("info", func(t *testing.T) {
		out, err := exec.Command(bin, "--host", host, "--password", "test123", "info").CombinedOutput()
		if err != nil {
			t.Fatalf("info failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "10.0.0.1") {
			t.Errorf("expected IP in output, got: %s", out)
		}
	})

	t.Run("vlan_list", func(t *testing.T) {
		out, err := exec.Command(bin, "--host", host, "--password", "test123", "vlan", "list").CombinedOutput()
		if err != nil {
			t.Fatalf("vlan list failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "default") {
			t.Errorf("expected vlan list output, got: %s", out)
		}
	})

	t.Run("cmd_log", func(t *testing.T) {
		out, err := exec.Command(bin, "--host", host, "--password", "test123", "cmd-log").CombinedOutput()
		if err != nil {
			t.Fatalf("cmd-log failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "status") {
			t.Errorf("expected cmd log output, got: %s", out)
		}
	})

	t.Run("sfp_diag", func(t *testing.T) {
		out, err := exec.Command(bin, "--host", host, "--password", "test123", "sfp-diag").CombinedOutput()
		if err != nil {
			t.Fatalf("sfp-diag failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "0x2a2a") {
			t.Errorf("expected sfp-diag output, got: %s", out)
		}
	})

	t.Run("cmd_log_clear", func(t *testing.T) {
		out, err := exec.Command(bin, "--host", host, "--password", "test123", "cmd-log", "clear").CombinedOutput()
		if err != nil {
			t.Fatalf("cmd-log clear failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "OK") {
			t.Errorf("expected OK, got: %s", out)
		}
	})

	t.Run("login_then_status", func(t *testing.T) {
		loginOut, err := exec.Command(bin, "--host", host, "login", "test123").CombinedOutput()
		if err != nil {
			t.Fatalf("login failed: %v\noutput: %s", err, loginOut)
		}
		if !strings.Contains(string(loginOut), "OK") {
			t.Errorf("expected OK, got: %s", loginOut)
		}
	})

	t.Run("unknown_command", func(t *testing.T) {
		out, _ := exec.Command(bin, "--host", host, "nonexistent").CombinedOutput()
		if !strings.Contains(string(out), "unknown command") {
			t.Errorf("expected error message, got: %s", out)
		}
	})

	t.Run("interactive_exit", func(t *testing.T) {
		cmd := exec.Command(bin, "--host", host)
		cmd.Stdin = strings.NewReader("exit\n")
		out, err := cmd.CombinedOutput()
		if err != nil {
			t.Fatalf("interactive mode failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "rtlpctl") {
			t.Errorf("expected banner, got: %s", out)
		}
	})

	t.Run("interactive_login_status", func(t *testing.T) {
		cmd := exec.Command(bin, "--host", host)
		cmd.Stdin = strings.NewReader("login test123\nstatus\nexit\n")
		out, err := cmd.CombinedOutput()
		if err != nil {
			t.Fatalf("interactive failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "Port 1") {
			t.Errorf("expected status output in interactive mode, got: %s", out)
		}
	})

	t.Run("env_file", func(t *testing.T) {
		envContent := fmt.Sprintf("RTLP_HOST=%s\nRTLP_PASSWORD=test123\n", host)
		envPath := t.TempDir() + "/rtlp.env"
		if err := os.WriteFile(envPath, []byte(envContent), 0644); err != nil {
			t.Fatal(err)
		}
		cmd := exec.Command(bin, "--env-file", envPath, "status")
		out, err := cmd.Output()
		if err != nil {
			t.Fatalf("status via env-file failed: %v\noutput: %s", err, string(out))
		}
		if !strings.Contains(string(out), "Port 1") {
			t.Errorf("expected status output, got: %s", string(out))
		}
	})

	t.Run("arista_show_interfaces_status", func(t *testing.T) {
		out, err := exec.Command(bin, "--host", host, "--password", "test123", "--mode", "arista",
			"show", "interfaces", "status").CombinedOutput()
		if err != nil {
			t.Fatalf("arista show interfaces status failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "Et1") {
			t.Errorf("expected Et1 in output, got: %s", out)
		}
	})

	t.Run("arista_show_interfaces_status_json", func(t *testing.T) {
		out, err := exec.Command(bin, "--host", host, "--password", "test123", "--mode", "arista", "--json",
			"show", "interfaces", "status").CombinedOutput()
		if err != nil {
			t.Fatalf("arista json failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "jsonrpc") || !strings.Contains(string(out), "Et1") {
			t.Errorf("expected EAPI JSON, got: %s", out)
		}
	})

	t.Run("arista_show_running_config", func(t *testing.T) {
		ts2 := newMockServer(t)
		defer ts2.Close()
		h := strings.TrimPrefix(ts2.URL, "http://")
		out, err := exec.Command(bin, "--host", h, "--password", "test123", "--mode", "arista",
			"show", "running-config").CombinedOutput()
		if err != nil {
			t.Fatalf("arista show running-config failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "ip 192.168") {
			t.Errorf("expected config output, got: %s", out)
		}
	})

	t.Run("arista_show_vlan", func(t *testing.T) {
		ts2 := newMockServer(t)
		defer ts2.Close()
		h := strings.TrimPrefix(ts2.URL, "http://")
		out, err := exec.Command(bin, "--host", h, "--password", "test123", "--mode", "arista",
			"show", "vlan").CombinedOutput()
		if err != nil {
			t.Fatalf("arista show vlan failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "active") {
			t.Errorf("expected vlan output, got: %s", out)
		}
	})

	t.Run("arista_show_inventory", func(t *testing.T) {
		ts2 := newMockServer(t)
		defer ts2.Close()
		h := strings.TrimPrefix(ts2.URL, "http://")
		out, err := exec.Command(bin, "--host", h, "--password", "test123", "--mode", "arista",
			"show", "inventory").CombinedOutput()
		if err != nil {
			t.Fatalf("arista show inventory failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "10.0.0.1") {
			t.Errorf("expected info output, got: %s", out)
		}
	})

	t.Run("arista_show_mac", func(t *testing.T) {
		ts2 := newMockServer(t)
		defer ts2.Close()
		h := strings.TrimPrefix(ts2.URL, "http://")
		out, err := exec.Command(bin, "--host", h, "--password", "test123", "--mode", "arista",
			"show", "mac", "address-table").CombinedOutput()
		if err != nil {
			t.Fatalf("arista show mac failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "00:01") {
			t.Errorf("expected mac output, got: %s", out)
		}
	})

	t.Run("arista_unknown_command", func(t *testing.T) {
		out, _ := exec.Command(bin, "--host", host, "--password", "test123", "--mode", "arista",
			"show", "foo").CombinedOutput()
		if !strings.Contains(string(out), "Unknown command") {
			t.Errorf("expected Unknown command error, got: %s", out)
		}
	})

	t.Run("arista_json_unknown_command", func(t *testing.T) {
		out, _ := exec.Command(bin, "--host", host, "--password", "test123", "--mode", "arista", "--json",
			"show", "foo").CombinedOutput()
		if !strings.Contains(string(out), "jsonrpc") || !strings.Contains(string(out), "error") {
			t.Errorf("expected EAPI error, got: %s", out)
		}
	})

	t.Run("arista_mode_env", func(t *testing.T) {
		envContent := fmt.Sprintf("RTLP_HOST=%s\nRTLP_PASSWORD=test123\nMODE=arista\n", host)
		envPath := t.TempDir() + "/arista.env"
		if err := os.WriteFile(envPath, []byte(envContent), 0644); err != nil {
			t.Fatal(err)
		}
		out, err := exec.Command(bin, "--env-file", envPath,
			"show", "interfaces", "status").CombinedOutput()
		if err != nil {
			t.Fatalf("arista env mode failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "Et1") {
			t.Errorf("expected Et1 in output, got: %s", out)
		}
	})

	t.Run("arista_abbrev_show_int_status", func(t *testing.T) {
		ts2 := newMockServer(t)
		defer ts2.Close()
		h := strings.TrimPrefix(ts2.URL, "http://")
		out, err := exec.Command(bin, "--host", h, "--password", "test123", "--mode", "arista",
			"sh", "int", "stat").CombinedOutput()
		if err != nil {
			t.Fatalf("arista abbrev failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "Et1") {
			t.Errorf("expected Et1 in output, got: %s", out)
		}
	})

	t.Run("arista_abbrev_sh_run", func(t *testing.T) {
		ts2 := newMockServer(t)
		defer ts2.Close()
		h := strings.TrimPrefix(ts2.URL, "http://")
		out, err := exec.Command(bin, "--host", h, "--password", "test123", "--mode", "arista",
			"sh", "run").CombinedOutput()
		if err != nil {
			t.Fatalf("arista abbrev sh run failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "ip 192.168") {
			t.Errorf("expected config output, got: %s", out)
		}
	})

	t.Run("arista_abbrev_sh_vlan", func(t *testing.T) {
		ts2 := newMockServer(t)
		defer ts2.Close()
		h := strings.TrimPrefix(ts2.URL, "http://")
		out, err := exec.Command(bin, "--host", h, "--password", "test123", "--mode", "arista",
			"sh", "vlan").CombinedOutput()
		if err != nil {
			t.Fatalf("arista abbrev sh vlan failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "active") {
			t.Errorf("expected vlan output, got: %s", out)
		}
	})

	t.Run("arista_abbrev_sh_mac", func(t *testing.T) {
		ts2 := newMockServer(t)
		defer ts2.Close()
		h := strings.TrimPrefix(ts2.URL, "http://")
		out, err := exec.Command(bin, "--host", h, "--password", "test123", "--mode", "arista",
			"sh", "mac").CombinedOutput()
		if err != nil {
			t.Fatalf("arista abbrev sh mac failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "00:01") {
			t.Errorf("expected mac output, got: %s", out)
		}
	})

	t.Run("arista_abbrev_sh_inv", func(t *testing.T) {
		ts2 := newMockServer(t)
		defer ts2.Close()
		h := strings.TrimPrefix(ts2.URL, "http://")
		out, err := exec.Command(bin, "--host", h, "--password", "test123", "--mode", "arista",
			"sh", "inv").CombinedOutput()
		if err != nil {
			t.Fatalf("arista abbrev sh inv failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "10.0.0.1") {
			t.Errorf("expected info output, got: %s", out)
		}
	})

	t.Run("arista_abbrev_conf_t", func(t *testing.T) {
		ts2 := newMockServer(t)
		defer ts2.Close()
		h := strings.TrimPrefix(ts2.URL, "http://")
		cmd := exec.Command(bin, "--host", h, "--password", "test123", "--mode", "arista")
		cmd.Stdin = strings.NewReader("conf t\nend\nexit\n")
		out, err := cmd.CombinedOutput()
		if err != nil {
			t.Fatalf("arista abbrev conf t failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "config)#") {
			t.Errorf("expected config mode prompt, got: %s", out)
		}
	})

	t.Run("arista_abbrev_wr_mem", func(t *testing.T) {
		ts2 := newMockServer(t)
		defer ts2.Close()
		h := strings.TrimPrefix(ts2.URL, "http://")
		mockCmdBodies = nil
		out, err := exec.Command(bin, "--host", h, "--password", "test123",
			"--psk", testPSKHex, "--mode", "arista",
			"wr", "mem").CombinedOutput()
		if err != nil {
			t.Fatalf("arista abbrev wr mem failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "OK") {
			t.Errorf("expected OK, got: %s", out)
		}
		if len(mockCmdBodies) != 0 {
			t.Errorf("write memory must not use /cmd, got bodies: %v", mockCmdBodies)
		}
	})

	t.Run("arista_abbrev_copy_run_start", func(t *testing.T) {
		ts2 := newMockServer(t)
		defer ts2.Close()
		h := strings.TrimPrefix(ts2.URL, "http://")
		mockCmdBodies = nil
		out, err := exec.Command(bin, "--host", h, "--password", "test123",
			"--psk", testPSKHex, "--mode", "arista",
			"copy", "run", "start").CombinedOutput()
		if err != nil {
			t.Fatalf("arista abbrev copy run start failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "OK") {
			t.Errorf("expected OK, got: %s", out)
		}
		if len(mockCmdBodies) != 0 {
			t.Errorf("copy running-config startup-config must not use /cmd, got bodies: %v", mockCmdBodies)
		}
	})

	t.Run("arista_write_mem_without_psk", func(t *testing.T) {
		ts2 := newMockServer(t)
		defer ts2.Close()
		h := strings.TrimPrefix(ts2.URL, "http://")
		out, err := exec.Command(bin, "--host", h, "--password", "test123",
			"--mode", "arista", "write", "memory").CombinedOutput()
		if err != nil {
			t.Fatalf("arista write memory failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "pre-shared key") {
			t.Errorf("expected PSK hint, got: %s", out)
		}
	})

	t.Run("interactive_hostname_prompt", func(t *testing.T) {
		ts2 := newMockServer(t)
		defer ts2.Close()
		h := strings.TrimPrefix(ts2.URL, "http://")
		cmd := exec.Command(bin, "--host", h, "--password", "test123")
		cmd.Stdin = strings.NewReader("\n")
		out, err := cmd.CombinedOutput()
		if err != nil {
			t.Fatalf("interactive failed: %v\noutput: %s", err, out)
		}
		if !strings.Contains(string(out), "rtlpctl@mock-host> ") {
			t.Errorf("expected hostname prompt, got: %s", out)
		}
	})
}

func newMockServer(t *testing.T) *httptest.Server {
	psk, _ := hex.DecodeString(testPSKHex)
	return httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/login" {
			pwd := r.FormValue("pwd")
			if pwd == "test123" {
				w.Header().Set("Set-Cookie", "session=abc123def456; SameSite=Strict")
				http.Redirect(w, r, "/index.html", http.StatusFound)
			} else {
				http.Redirect(w, r, "/login.html", http.StatusFound)
			}
			return
		}
		c, err := r.Cookie("session")
		if err != nil || c.Value != "abc123def456" {
			http.Error(w, "Unauthorized", http.StatusUnauthorized)
			return
		}
		switch r.URL.Path {
		case "/enc":
			body, _ := io.ReadAll(r.Body)
			if len(body) < aeadNonceLen+aeadTagLen {
				http.Error(w, "short enc request", http.StatusBadRequest)
				return
			}
			pt, err := aeadDecrypt(psk, body[:aeadNonceLen],
				body[aeadNonceLen:len(body)-aeadTagLen], body[len(body)-aeadTagLen:])
			if err != nil {
				http.Error(w, "decrypt failed", http.StatusBadRequest)
				return
			}
			// Mirrors the firmware: /enc with a non-commit word executes
			// commands; here we just echo the command back to the caller.
			if string(pt) != "commit" {
				fmt.Fprint(w, "mock")
				return
			}
			nonce := make([]byte, aeadNonceLen)
			resp, err := aeadEncrypt(psk, nonce, []byte(`{"result":"ok"}`))
			if err != nil {
				http.Error(w, "encrypt failed", http.StatusInternalServerError)
				return
			}
			w.Header().Set("Content-Type", "application/octet-stream")
			w.Write(resp)
		case "/cmd":
			body, _ := io.ReadAll(r.Body)
			mockCmdBodies = append(mockCmdBodies, string(body))
			w.Header().Set("Content-Type", "text/plain")
			fmt.Fprint(w, "OK")
		case "/status.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[{"portNum":1,"name":"Port 1","link":4,"enabled":1,"txG":"1a2b","txB":"0","rxG":"3c4d","rxB":"0"}]`)
		case "/information.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `{"ip_address":"10.0.0.1","hostname":"mock-host","sw_ver":"v1.0","hw_ver":"TEST"}`)
		case "/vlan.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `{"members":"0x0001","name":"default","pvid":"0x0001"}`)
		case "/vlanlist":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[{"id":1,"name":"default"},{"id":100,"name":"test"}]`)
		case "/counters.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `["0x001","0x002","0x003"]`)
		case "/l2.json":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[{"mac":"00:01:2f:00:00:01","vlan":"001","type":"l","port":7,"idx":"0x10"}]`)
		case "/config":
			w.Header().Set("Content-Type", "text/plain")
			fmt.Fprint(w, "ip 192.168.1.1\nnetmask 255.255.255.0\n")
		case "/cmd_log":
			w.Header().Set("Content-Type", "text/plain")
			fmt.Fprint(w, "ip 192.168.1.1\nstatus\n")
		case "/cmd_log_clear":
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `[]`)
		default:
			w.Header().Set("Content-Type", "text/plain")
			fmt.Fprint(w, "mock")
		}
	}))
}

func TestEosSpeed(t *testing.T) {
	tests := []struct {
		link interface{}
		want string
	}{
		{float64(0), "down"},
		{float64(1), "10M"},
		{float64(2), "100M"},
		{float64(3), "1G"},
		{float64(5), "10G"},
		{float64(6), "2.5G"},
		{float64(7), "5G"},
		{float64(4), "down"},
	}
	for _, tt := range tests {
		if got := eosSpeed(tt.link); got != tt.want {
			t.Errorf("eosSpeed(%v) = %q, want %q", tt.link, got, tt.want)
		}
	}
}
