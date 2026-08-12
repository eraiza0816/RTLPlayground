package main

import (
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/testutil"
	dto "github.com/prometheus/client_model/go"
)

// mockSwitch serves every API endpoint Collect() touches.  failPaths
// marks endpoints that must answer with 500 (to exercise the
// rtl_scrape_success logic, audit F6).
func mockSwitch(t *testing.T, failPaths map[string]bool) *httptest.Server {
	var mu sync.Mutex
	return httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		mu.Lock()
		fail := failPaths[r.URL.Path]
		mu.Unlock()
		if fail {
			http.Error(w, "boom", http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		var body string
		switch {
		case r.URL.Path == "/information.json":
			body = `{"ip_address":"192.168.1.1","mac_address":"00:11:22:33:44:55","sw_ver":"v1.0","hw_ver":"mock"}`
		case r.URL.Path == "/status.json":
			body = `[
				{"portNum":1,"logPort":4,"name":"p1","isSFP":0,"enabled":1,"link":3,"txG":"0x10","txB":"0x1","rxG":"0x20","rxB":"0x2"},
				{"portNum":2,"logPort":8,"name":"","isSFP":1,"enabled":0,"link":0,"txG":"0x0","txB":"0x0","rxG":"0x0","rxB":"0x0","sfp_vendor":"ACME","sfp_model":"SFP-1G"}]
			`
		case r.URL.Path == "/sfp_diag.json":
			body = `[{"portNum":2,"sfp_options":"1","sfp_temp":"0x1a00","sfp_vcc":"0x64","sfp_txbias":"0x10","sfp_txpower":"0x10","sfp_rxpower":"0x10"}]`
		case r.URL.Path == "/vlanlist":
			body = `[{"id":1,"name":"default"},{"id":10,"name":"test"}]`
		case strings.HasPrefix(r.URL.Path, "/l2.json"):
			if r.URL.Query().Get("idx") == "2" {
				body = `[]`
			} else {
				body = `[{"mac":"00:11:22:33:44:55","vlan":"1","type":"0","port":1,"idx":"0"},
				         {"mac":"00:11:22:33:44:66","vlan":"1","type":"1","port":2,"idx":"1"}]`
			}
		case r.URL.Path == "/mirror.json":
			body = `{"enabled":1,"mPort":1,"mirror_tx":"0001","mirror_rx":"0001"}`
		case r.URL.Path == "/lag.json":
			body = `[{"lagNum":1,"members":"3","hash":"2"}]`
		case r.URL.Path == "/eee.json":
			body = `[{"portNum":1,"isSFP":0,"active":1}]`
		case r.URL.Path == "/bandwidth.json":
			body = `[{"portNum":1,"iLimited":1,"iBW":"1000","iFC":0,"eLimited":1,"eBW":"2000"}]`
		case r.URL.Path == "/mtu.json":
			body = `[{"portNum":1,"mtu":"5dc"}]`
		case strings.HasPrefix(r.URL.Path, "/counters.json"):
			body = `["0x1","0x2","0x3","0x4","0x5","0x6","0x7","0x8"]`
		default:
			http.NotFound(w, r)
			return
		}
		fmt.Fprint(w, body)
	}))
}

// gather collects one scrape via the prometheus registry and returns the
// metric families by name.
func gather(t *testing.T, e *Exporter) map[string]*dto.MetricFamily {
	t.Helper()
	reg := prometheus.NewRegistry()
	reg.MustRegister(e)
	mfs, err := reg.Gather()
	if err != nil {
		t.Fatalf("Gather: %v", err)
	}
	m := map[string]*dto.MetricFamily{}
	for _, mf := range mfs {
		m[mf.GetName()] = mf
	}
	return m
}

// gaugeValue extracts the value of the first metric with the given labels.
func gaugeValue(t *testing.T, mfs map[string]*dto.MetricFamily, name string) float64 {
	t.Helper()
	mf, ok := mfs[name]
	if !ok {
		t.Fatalf("metric %q missing", name)
	}
	if len(mf.GetMetric()) == 0 {
		t.Fatalf("metric %q has no samples", name)
	}
	return mf.GetMetric()[0].GetGauge().GetValue()
}

func TestCollectSuccess(t *testing.T) {
	srv := mockSwitch(t, nil)
	defer srv.Close()
	e := &Exporter{target: srv.URL, client: srv.Client()}

	mfs := gather(t, e)

	if v := gaugeValue(t, mfs, "rtl_scrape_success"); v != 1 {
		t.Errorf("rtl_scrape_success = %v, want 1", v)
	}
	if v := gaugeValue(t, mfs, "rtl_vlan_count"); v != 2 {
		t.Errorf("rtl_vlan_count = %v, want 2", v)
	}
	if v := gaugeValue(t, mfs, "rtl_l2_table_entries"); v != 2 {
		t.Errorf("rtl_l2_table_entries = %v, want 2", v)
	}
	// switch info must carry the label values
	mf := mfs["rtl_switch_info"]
	if mf == nil {
		t.Fatal("rtl_switch_info missing")
	}
	labels := map[string]string{}
	for _, l := range mf.GetMetric()[0].GetLabel() {
		labels[l.GetName()] = l.GetValue()
	}
	if labels["ip_address"] != "192.168.1.1" || labels["hw_ver"] != "mock" || labels["sw_ver"] != "v1.0" {
		t.Errorf("rtl_switch_info labels = %+v", labels)
	}
	// port 1 up, port 2 down
	mf = mfs["rtl_port_up"]
	if mf == nil {
		t.Fatal("rtl_port_up missing")
	}
	for _, m := range mf.GetMetric() {
		port := ""
		for _, l := range m.GetLabel() {
			if l.GetName() == "port" {
				port = l.GetValue()
			}
		}
		want := map[string]float64{"1": 1, "2": 0}[port]
		if m.GetGauge().GetValue() != want {
			t.Errorf("rtl_port_up port=%s = %v, want %v", port, m.GetGauge().GetValue(), want)
		}
	}
	// SFP diagnostics must be exported for the SFP port
	if mfs["rtl_sfp_temperature_celsius"] == nil {
		t.Error("rtl_sfp_temperature_celsius missing")
	}
	if mfs["rtl_port_mib_counter"] == nil {
		t.Error("rtl_port_mib_counter missing")
	}
	if mfs["rtl_mirror_enabled"] == nil || mfs["rtl_lag_members"] == nil ||
		mfs["rtl_eee_active"] == nil || mfs["rtl_port_bandwidth_ingress_limit_bytes"] == nil ||
		mfs["rtl_port_mtu_bytes"] == nil {
		t.Error("one of mirror/lag/eee/bw/mtu metric families missing")
	}
}

// TestCollectFailureSetsScrapeSuccessZero is the F6 regression test: any
// failing endpoint must clear rtl_scrape_success (it previously only
// tracked /information.json and /status.json).
func TestCollectFailureSetsScrapeSuccessZero(t *testing.T) {
	paths := []string{
		"/vlanlist", "/l2.json", "/mirror.json", "/lag.json", "/eee.json",
		"/bandwidth.json", "/mtu.json", "/sfp_diag.json", "/counters.json",
	}
	for _, p := range paths {
		srv := mockSwitch(t, map[string]bool{p: true})
		e := &Exporter{target: srv.URL, client: srv.Client()}
		mfs := gather(t, e)
		if v := gaugeValue(t, mfs, "rtl_scrape_success"); v != 0 {
			t.Errorf("failing %s: rtl_scrape_success = %v, want 0", p, v)
		}
		srv.Close()
	}
}

func TestCollectInfoFailure(t *testing.T) {
	// /information.json and /status.json failures are also tracked.
	for _, p := range []string{"/information.json", "/status.json"} {
		srv := mockSwitch(t, map[string]bool{p: true})
		e := &Exporter{target: srv.URL, client: srv.Client()}
		mfs := gather(t, e)
		if v := gaugeValue(t, mfs, "rtl_scrape_success"); v != 0 {
			t.Errorf("failing %s: rtl_scrape_success = %v, want 0", p, v)
		}
		srv.Close()
	}
}

func TestParseHex64(t *testing.T) {
	tests := []struct {
		in   string
		want uint64
	}{
		{"0x0", 0},
		{"0x10", 0x10},
		{"0xffffffffffffffff", 0xffffffffffffffff},
		{"0", 0},
		{"1a", 0x1a},
		{"garbage", 0},
		{"", 0},
		{"-1", 0},
	}
	for _, tt := range tests {
		if got := parseHex64(tt.in); got != tt.want {
			t.Errorf("parseHex64(%q) = %#x, want %#x", tt.in, got, tt.want)
		}
	}
}

func TestParseHex16(t *testing.T) {
	if v := parseHex16("0x1a00"); v == nil || *v != 0x1a00 {
		t.Errorf("parseHex16(0x1a00) = %v", v)
	}
	if v := parseHex16("0xffff"); v == nil || *v != 0xffff {
		t.Errorf("parseHex16(0xffff) = %v", v)
	}
	for _, bad := range []string{"", "0x", "0x10000", "xyz", "0xzz"} {
		if v := parseHex16(bad); v != nil {
			t.Errorf("parseHex16(%q) = %v, want nil", bad, v)
		}
	}
}

func TestParseHexInt(t *testing.T) {
	if got := parseHexInt("5dc"); got != 0x5dc {
		t.Errorf("parseHexInt(5dc) = %d", got)
	}
	if got := parseHexInt("0x10"); got != 0x10 {
		t.Errorf("parseHexInt(0x10) = %d", got)
	}
	if got := parseHexInt("garbage"); got != 0 {
		t.Errorf("parseHexInt(garbage) = %d, want 0", got)
	}
}

func TestCountL2Entries(t *testing.T) {
	srv := mockSwitch(t, nil)
	defer srv.Close()
	e := &Exporter{target: srv.URL, client: srv.Client()}

	count, ok := e.countL2Entries()
	if !ok || count != 2 {
		t.Errorf("countL2Entries = %d/%v, want 2/true", count, ok)
	}

	// A failing table read must report ok=false (F6: scrape success 0).
	bad := mockSwitch(t, map[string]bool{"/l2.json": true})
	defer bad.Close()
	e2 := &Exporter{target: bad.URL, client: bad.Client()}
	if _, ok := e2.countL2Entries(); ok {
		t.Error("countL2Entries with failing endpoint reported ok=true")
	}
}

func TestCollectScrapeSuccessMetricCount(t *testing.T) {
	// rtl_scrape_success must be emitted exactly once per scrape.
	srv := mockSwitch(t, nil)
	defer srv.Close()
	e := &Exporter{target: srv.URL, client: srv.Client()}
	mfs := gather(t, e)
	mf := mfs["rtl_scrape_success"]
	if mf == nil || len(mf.GetMetric()) != 1 {
		t.Fatalf("rtl_scrape_success metrics = %d, want 1", len(mf.GetMetric()))
	}
}

// TestCollectIsDeterministic guards against goroutine races changing the
// metric set between scrapes (the endpoint goroutines all write to the
// channel; a missing lock or a closed channel would show up here).
func TestCollectIsDeterministic(t *testing.T) {
	srv := mockSwitch(t, nil)
	defer srv.Close()
	e := &Exporter{target: srv.URL, client: srv.Client()}

	sizes := map[string]int{}
	for i := 0; i < 5; i++ {
		mfs := gather(t, e)
		n := 0
		for name, mf := range mfs {
			if mf.GetName() == name {
				n += len(mf.GetMetric())
			}
		}
		sizes[fmt.Sprint(n)]++
	}
	if len(sizes) != 1 {
		t.Errorf("metric count varied across scrapes: %v", sizes)
	}
}

// TestScrapeOutputSanity uses testutil to make sure the scrape is well
// formed enough for prometheus to ingest (dup labels / invalid values
// would fail Gather).
func TestScrapeOutputSanity(t *testing.T) {
	srv := mockSwitch(t, nil)
	defer srv.Close()
	e := &Exporter{target: srv.URL, client: srv.Client()}
	reg := prometheus.NewRegistry()
	reg.MustRegister(e)
	if err := testutil.CollectAndCompare(reg, strings.NewReader(`
# HELP rtl_scrape_success Whether the last scrape succeeded (1=success, 0=failure)
# TYPE rtl_scrape_success gauge
rtl_scrape_success 1
`), "rtl_scrape_success"); err != nil {
		t.Fatalf("CollectAndCompare: %v", err)
	}
}
