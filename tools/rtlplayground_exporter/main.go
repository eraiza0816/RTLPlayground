package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"math"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promhttp"
)

var (
	listen     = flag.String("listen", ":9101", "Exporter listen address")
	target     = flag.String("target", "http://localhost:8080", "Switch URL")
	password   = flag.String("password", "1234", "Switch login password")
)

func main() {
	flag.Parse()

	exp := &Exporter{
		target:   strings.TrimRight(*target, "/"),
		client:   &http.Client{Timeout: 10 * time.Second, CheckRedirect: func(req *http.Request, via []*http.Request) error { return http.ErrUseLastResponse }},
		password: *password,
	}

	if err := exp.login(); err != nil {
		log.Fatalf("Login failed: %v", err)
	}
	log.Printf("Logged in to %s", exp.target)

	prometheus.MustRegister(exp)

	http.Handle("/metrics", promhttp.Handler())
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte(`<html><body><a href="/metrics">/metrics</a></body></html>`))
	})

	log.Printf("Starting rtlplayground_exporter for %s on %s", exp.target, *listen)
	log.Fatal(http.ListenAndServe(*listen, nil))
}

type PortStatus struct {
	PortNum int    `json:"portNum"`
	LogPort int    `json:"logPort"`
	Name    string `json:"name"`
	IsSFP   int    `json:"isSFP"`
	Enabled int    `json:"enabled"`
	Link    int    `json:"link"`
	TxG     string `json:"txG"`
	TxB     string `json:"txB"`
	RxG     string `json:"rxG"`
	RxB     string `json:"rxB"`
	SfpVendor  string `json:"sfp_vendor,omitempty"`
	SfpModel   string `json:"sfp_model,omitempty"`
	SfpSerial  string `json:"sfp_serial,omitempty"`
	SfpOptions string `json:"sfp_options,omitempty"`
	SfpTemp    string `json:"sfp_temp,omitempty"`
	SfpVcc     string `json:"sfp_vcc,omitempty"`
	SfpTxBias  string `json:"sfp_txbias,omitempty"`
	SfpTxPower string `json:"sfp_txpower,omitempty"`
	SfpRxPower string `json:"sfp_rxpower,omitempty"`
}

type SwitchInfo struct {
	IPAddress  string `json:"ip_address"`
	MacAddress string `json:"mac_address"`
	SwVer      string `json:"sw_ver"`
	HwVer      string `json:"hw_ver"`
	BuildDate  string `json:"build_date,omitempty"`
	FlashSize  string `json:"flash_size,omitempty"`
}

type VLANEntry struct {
	ID   string `json:"id"`
	Name string `json:"name"`
}

type SfpDiagEntry struct {
	PortNum    int    `json:"portNum"`
	SfpOptions string `json:"sfp_options,omitempty"`
	SfpTemp    string `json:"sfp_temp,omitempty"`
	SfpVcc     string `json:"sfp_vcc,omitempty"`
	SfpTxBias  string `json:"sfp_txbias,omitempty"`
	SfpTxPower string `json:"sfp_txpower,omitempty"`
	SfpRxPower string `json:"sfp_rxpower,omitempty"`
}

type MirrorConfig struct {
	Enabled  int    `json:"enabled"`
	MPort    int    `json:"mPort"`
	MirrorTX string `json:"mirror_tx"`
	MirrorRX string `json:"mirror_rx"`
}

type LAGGroup struct {
	LagNum  int    `json:"lagNum"`
	Members string `json:"members"`
	Hash    string `json:"hash"`
}

type EEEPort struct {
	PortNum int    `json:"portNum"`
	IsSFP   int    `json:"isSFP"`
	EEE     string `json:"eee,omitempty"`
	EEE_LP  string `json:"eee_lp,omitempty"`
	Active  int    `json:"active"`
}

type BWPort struct {
	PortNum  int    `json:"portNum"`
	ILimited int    `json:"iLimited"`
	IBW      string `json:"iBW"`
	IFC      int    `json:"iFC"`
	ELimited int    `json:"eLimited"`
	EBW      string `json:"eBW"`
}

type MTUPort struct {
	PortNum int    `json:"portNum"`
	MTU     string `json:"mtu"`
}

type L2Entry struct {
	MAC  string `json:"mac"`
	VLAN string `json:"vlan"`
	Type string `json:"type"`
	Port int    `json:"port"`
	Idx  string `json:"idx"`
}

type Exporter struct {
	target   string
	client   *http.Client
	password string
	cookie   string
}

var (
	switchInfoDesc = prometheus.NewDesc(
		"rtl_switch_info",
		"Switch hardware and software information",
		[]string{"ip_address", "mac_address", "sw_ver", "hw_ver"},
		nil,
	)
	portUpDesc = prometheus.NewDesc(
		"rtl_port_up",
		"Port link status (1=up, 0=down)",
		[]string{"port", "name", "logical_port"},
		nil,
	)
	portSpeedDesc = prometheus.NewDesc(
		"rtl_port_speed_bps",
		"Port link speed in bps",
		[]string{"port", "name", "logical_port"},
		nil,
	)
	portEnabledDesc = prometheus.NewDesc(
		"rtl_port_enabled",
		"Port administrative state",
		[]string{"port", "name", "logical_port"},
		nil,
	)
	portTxGoodDesc = prometheus.NewDesc(
		"rtl_port_tx_good_packets_total",
		"Total good packets transmitted",
		[]string{"port", "name", "logical_port"},
		nil,
	)
	portTxBadDesc = prometheus.NewDesc(
		"rtl_port_tx_bad_packets_total",
		"Total error packets transmitted",
		[]string{"port", "name", "logical_port"},
		nil,
	)
	portRxGoodDesc = prometheus.NewDesc(
		"rtl_port_rx_good_packets_total",
		"Total good packets received",
		[]string{"port", "name", "logical_port"},
		nil,
	)
	portRxBadDesc = prometheus.NewDesc(
		"rtl_port_rx_bad_packets_total",
		"Total error packets received",
		[]string{"port", "name", "logical_port"},
		nil,
	)
	sfpTempDesc = prometheus.NewDesc(
		"rtl_sfp_temperature_celsius",
		"SFP module temperature",
		[]string{"port", "vendor", "model"},
		nil,
	)
	sfpVoltageDesc = prometheus.NewDesc(
		"rtl_sfp_voltage_volts",
		"SFP module supply voltage",
		[]string{"port"},
		nil,
	)
	sfpTxBiasDesc = prometheus.NewDesc(
		"rtl_sfp_tx_bias_amperes",
		"SFP module TX bias current",
		[]string{"port"},
		nil,
	)
	sfpTxPowerDesc = prometheus.NewDesc(
		"rtl_sfp_tx_power_dbm",
		"SFP module TX output power",
		[]string{"port"},
		nil,
	)
	sfpRxPowerDesc = prometheus.NewDesc(
		"rtl_sfp_rx_power_dbm",
		"SFP module RX received power",
		[]string{"port"},
		nil,
	)
	mibCounterDesc = prometheus.NewDesc(
		"rtl_port_mib_counter",
		"MIB counter value per port",
		[]string{"port", "counter"},
		nil,
	)
	vlanCountDesc = prometheus.NewDesc(
		"rtl_vlan_count",
		"Number of configured VLANs",
		nil, nil,
	)
	l2EntriesDesc = prometheus.NewDesc(
		"rtl_l2_table_entries",
		"Number of entries in the L2 MAC table",
		nil, nil,
	)
	mirrorEnabledDesc = prometheus.NewDesc(
		"rtl_mirror_enabled",
		"Port mirroring enabled",
		nil, nil,
	)
	mirrorMonitorPortDesc = prometheus.NewDesc(
		"rtl_mirror_monitor_port",
		"Monitor (destination) port for mirroring",
		nil, nil,
	)
	lagMembersDesc = prometheus.NewDesc(
		"rtl_lag_members",
		"LAG group member ports as a bitmask string",
		[]string{"lag_num"},
		nil,
	)
	eeeActiveDesc = prometheus.NewDesc(
		"rtl_eee_active",
		"EEE active on port",
		[]string{"port"},
		nil,
	)
	bwIngressLimitDesc = prometheus.NewDesc(
		"rtl_port_bandwidth_ingress_limit_bytes",
		"Ingress bandwidth limit for port",
		[]string{"port"},
		nil,
	)
	bwEgressLimitDesc = prometheus.NewDesc(
		"rtl_port_bandwidth_egress_limit_bytes",
		"Egress bandwidth limit for port",
		[]string{"port"},
		nil,
	)
	mtuDesc = prometheus.NewDesc(
		"rtl_port_mtu_bytes",
		"MTU (max frame length) for port",
		[]string{"port"},
		nil,
	)
	scrapeDurationDesc = prometheus.NewDesc(
		"rtl_scrape_duration_seconds",
		"Duration of the last scrape",
		nil, nil,
	)
	scrapeSuccessDesc = prometheus.NewDesc(
		"rtl_scrape_success",
		"Whether the last scrape succeeded (1=success, 0=failure)",
		nil, nil,
	)
)

var mibCounterNames = []string{
	"In Octets",
	"Out Octets",
	"In Unicast Pkts",
	"In Multicast Pkts",
	"In Broadcast Pkts",
	"Out Unicast Pkts",
	"Out Multicast Pkts",
	"Out Broadcast Pkts",
	"Out discards",
	"802.1d Tp Port discards",
	"802.3 Single collision",
	"802.3 Multi collision",
	"802.3 Deferred tx",
	"802.3 Late collisions",
	"802.3 Excessive collisions",
	"802.3 Symbol errors",
	"802.3 Control in unk",
	"802.3 In Pause frames",
	"802.3 Out Pause frames",
	"Ether drop events",
	"TX Broadcast Pkts",
	"TX Multicast Pkts",
	"TX CRC Align errors",
	"RX CRC Align errors",
	"TX Undersized Pkts",
	"RX Undersized Pkts",
	"TX Oversized Pkts",
	"RX Oversized Pkts",
	"TX Fragments",
	"RX fragments",
	"TX Jabbers",
	"RX Jabbers",
	"TX Collisions",
	"TX 64 Octets",
	"RX 64 Octets",
	"TX 65-127 Octets",
	"RX 65-127 Octets",
	"TX 128-255 Octets",
	"RX 128-255 Octets",
	"TX 256-511 Octets",
	"RX 256-511 Octets",
	"TX 512-1023 Octets",
	"RX 512-1023 Octets",
	"TX 1024-1518 Octets",
	"RX 1024-1518 Octets",
	"",
	"RX Undersized Drops",
	"TX 1519+ Octets",
	"RX 1519+ Octets",
	"TX Pkts too large",
	"RX Pkts too large",
	"TX Flex Octets S1",
	"RX Flex Octets S1",
	"TX Flex CRC S1",
	"RX Flex CRC S1",
}

func (e *Exporter) Describe(ch chan<- *prometheus.Desc) {
	ch <- switchInfoDesc
	ch <- portUpDesc
	ch <- portSpeedDesc
	ch <- portEnabledDesc
	ch <- portTxGoodDesc
	ch <- portTxBadDesc
	ch <- portRxGoodDesc
	ch <- portRxBadDesc
	ch <- sfpTempDesc
	ch <- sfpVoltageDesc
	ch <- sfpTxBiasDesc
	ch <- sfpTxPowerDesc
	ch <- sfpRxPowerDesc
	ch <- mibCounterDesc
	ch <- vlanCountDesc
	ch <- l2EntriesDesc
	ch <- mirrorEnabledDesc
	ch <- mirrorMonitorPortDesc
	ch <- lagMembersDesc
	ch <- eeeActiveDesc
	ch <- bwIngressLimitDesc
	ch <- bwEgressLimitDesc
	ch <- mtuDesc
	ch <- scrapeDurationDesc
	ch <- scrapeSuccessDesc
}

func (e *Exporter) Collect(ch chan<- prometheus.Metric) {
	success := 1.0
	var wg sync.WaitGroup

	info, err := fetchJSON[SwitchInfo](e, "/information.json")
	if err != nil {
		log.Printf("Error fetching info: %v", err)
		success = 0.0
		info = &SwitchInfo{}
	}
	ch <- prometheus.MustNewConstMetric(switchInfoDesc, prometheus.GaugeValue, 1,
		info.IPAddress, info.MacAddress, info.SwVer, info.HwVer)

	ports, err := fetchJSON[[]PortStatus](e, "/status.json")
	if err != nil {
		log.Printf("Error fetching status: %v", err)
		success = 0.0
		ports = &[]PortStatus{}
	}

	for _, p := range *ports {
		port := strconv.Itoa(p.PortNum)
		logPort := strconv.Itoa(p.LogPort)

		up := 0.0
		if p.Link > 0 {
			up = 1.0
		}
		ch <- prometheus.MustNewConstMetric(portUpDesc, prometheus.GaugeValue, up,
			port, p.Name, logPort)
		ch <- prometheus.MustNewConstMetric(portSpeedDesc, prometheus.GaugeValue,
			float64(linkSpeedToBPS(p.Link)), port, p.Name, logPort)
		ch <- prometheus.MustNewConstMetric(portEnabledDesc, prometheus.GaugeValue,
			float64(p.Enabled), port, p.Name, logPort)

		ch <- prometheus.MustNewConstMetric(portTxGoodDesc, prometheus.CounterValue,
			float64(parseHex64(p.TxG)), port, p.Name, logPort)
		ch <- prometheus.MustNewConstMetric(portTxBadDesc, prometheus.CounterValue,
			float64(parseHex64(p.TxB)), port, p.Name, logPort)
		ch <- prometheus.MustNewConstMetric(portRxGoodDesc, prometheus.CounterValue,
			float64(parseHex64(p.RxG)), port, p.Name, logPort)
		ch <- prometheus.MustNewConstMetric(portRxBadDesc, prometheus.CounterValue,
			float64(parseHex64(p.RxB)), port, p.Name, logPort)

		if p.IsSFP != 0 && p.SfpVendor != "" {
			ch <- prometheus.MustNewConstMetric(sfpTempDesc, prometheus.GaugeValue,
				0, port, p.SfpVendor, p.SfpModel)
		}
	}

	// SFP diagnostics from /sfp_diag.json
	sfpDiag, err := fetchJSON[[]SfpDiagEntry](e, "/sfp_diag.json")
	if err == nil {
		for _, d := range *sfpDiag {
			port := strconv.Itoa(d.PortNum)
			if d.SfpOptions != "" {
				if temp := parseHex16(d.SfpTemp); temp != nil {
					ch <- prometheus.MustNewConstMetric(sfpTempDesc, prometheus.GaugeValue,
						float64(int16(*temp))*0.0625, port, "", "")
				}
				if vcc := parseHex16(d.SfpVcc); vcc != nil {
					ch <- prometheus.MustNewConstMetric(sfpVoltageDesc, prometheus.GaugeValue,
						float64(*vcc)*0.0001, port)
				}
				if txbias := parseHex16(d.SfpTxBias); txbias != nil {
					ch <- prometheus.MustNewConstMetric(sfpTxBiasDesc, prometheus.GaugeValue,
						float64(*txbias)*0.01/1000.0, port)
				}
				if txpw := parseHex16(d.SfpTxPower); txpw != nil {
					if mW := float64(*txpw) * 0.0001; mW > 0 {
						ch <- prometheus.MustNewConstMetric(sfpTxPowerDesc, prometheus.GaugeValue,
							10*math.Log10(mW), port)
					}
				}
				if rxpw := parseHex16(d.SfpRxPower); rxpw != nil {
					if mW := float64(*rxpw) * 0.0001; mW > 0 {
						ch <- prometheus.MustNewConstMetric(sfpRxPowerDesc, prometheus.GaugeValue,
							10*math.Log10(mW), port)
					}
				}
			}
		}
	} else {
		log.Printf("Error fetching sfp_diag: %v", err)
	}

	// MIB counters (parallel per port)
	var mibMu sync.Mutex
	for _, p := range *ports {
		wg.Add(1)
		go func(portNum, logPort int) {
			defer wg.Done()
			counters, err := fetchJSON[[]string](e, fmt.Sprintf("/counters.json?port=%d", logPort))
			if err != nil {
				log.Printf("Error fetching counters for port %d: %v", portNum, err)
				return
			}
			port := strconv.Itoa(portNum)
			for i, val := range *counters {
				if i >= len(mibCounterNames) || mibCounterNames[i] == "" {
					continue
				}
				n := parseHex64(val)
				mibMu.Lock()
				ch <- prometheus.MustNewConstMetric(mibCounterDesc, prometheus.CounterValue,
					float64(n), port, mibCounterNames[i])
				mibMu.Unlock()
			}
		}(p.PortNum, p.LogPort)
	}

	// VLAN list
	wg.Add(1)
	go func() {
		defer wg.Done()
		vlans, err := fetchJSON[[]VLANEntry](e, "/vlanlist")
		if err != nil {
			log.Printf("Error fetching vlanlist: %v", err)
			return
		}
		ch <- prometheus.MustNewConstMetric(vlanCountDesc, prometheus.GaugeValue,
			float64(len(*vlans)))
	}()

	// L2 table
	wg.Add(1)
	go func() {
		defer wg.Done()
		count := e.countL2Entries()
		ch <- prometheus.MustNewConstMetric(l2EntriesDesc, prometheus.GaugeValue,
			float64(count))
	}()

	// Mirror
	wg.Add(1)
	go func() {
		defer wg.Done()
		mirror, err := fetchJSON[MirrorConfig](e, "/mirror.json")
		if err != nil {
			log.Printf("Error fetching mirror: %v", err)
			return
		}
		ch <- prometheus.MustNewConstMetric(mirrorEnabledDesc, prometheus.GaugeValue,
			float64(mirror.Enabled))
		ch <- prometheus.MustNewConstMetric(mirrorMonitorPortDesc, prometheus.GaugeValue,
			float64(mirror.MPort))
	}()

	// LAG
	wg.Add(1)
	go func() {
		defer wg.Done()
		lags, err := fetchJSON[[]LAGGroup](e, "/lag.json")
		if err != nil {
			log.Printf("Error fetching lag: %v", err)
			return
		}
		for _, g := range *lags {
			ch <- prometheus.MustNewConstMetric(lagMembersDesc, prometheus.GaugeValue,
				1, strconv.Itoa(g.LagNum))
		}
	}()

	// EEE
	wg.Add(1)
	go func() {
		defer wg.Done()
		eee, err := fetchJSON[[]EEEPort](e, "/eee.json")
		if err != nil {
			log.Printf("Error fetching eee: %v", err)
			return
		}
		for _, p := range *eee {
			ch <- prometheus.MustNewConstMetric(eeeActiveDesc, prometheus.GaugeValue,
				float64(p.Active), strconv.Itoa(p.PortNum))
		}
	}()

	// Bandwidth
	wg.Add(1)
	go func() {
		defer wg.Done()
		bw, err := fetchJSON[[]BWPort](e, "/bandwidth.json")
		if err != nil {
			log.Printf("Error fetching bandwidth: %v", err)
			return
		}
		for _, p := range *bw {
			port := strconv.Itoa(p.PortNum)
			ibw := parseHexInt(p.IBW)
			ebw := parseHexInt(p.EBW)
			if p.ILimited != 0 && ibw > 0 {
				ch <- prometheus.MustNewConstMetric(bwIngressLimitDesc, prometheus.GaugeValue,
					float64(ibw), port)
			}
			if p.ELimited != 0 && ebw > 0 {
				ch <- prometheus.MustNewConstMetric(bwEgressLimitDesc, prometheus.GaugeValue,
					float64(ebw), port)
			}
		}
	}()

	// MTU
	wg.Add(1)
	go func() {
		defer wg.Done()
		mtus, err := fetchJSON[[]MTUPort](e, "/mtu.json")
		if err != nil {
			log.Printf("Error fetching mtu: %v", err)
			return
		}
		for _, p := range *mtus {
			ch <- prometheus.MustNewConstMetric(mtuDesc, prometheus.GaugeValue,
				float64(parseHexInt(p.MTU)), strconv.Itoa(p.PortNum))
		}
	}()

	wg.Wait()

	ch <- prometheus.MustNewConstMetric(scrapeDurationDesc, prometheus.GaugeValue, 0)
	ch <- prometheus.MustNewConstMetric(scrapeSuccessDesc, prometheus.GaugeValue, success)
}

func (e *Exporter) countL2Entries() int {
	idx := 0
	seen := make(map[uint64]bool)
	for iter := 0; iter < 20; iter++ {
		entries, err := fetchJSON[[]L2Entry](e, fmt.Sprintf("/l2.json?idx=%d", idx))
		if err != nil {
			log.Printf("Error fetching L2 at idx %04x: %v", idx, err)
			break
		}
		if len(*entries) == 0 {
			break
		}
		added := 0
		var lastIdx uint64
		for _, entry := range *entries {
			v, _ := strconv.ParseUint(entry.Idx, 16, 16)
			lastIdx = v
			if !seen[v] {
				seen[v] = true
				added++
			}
		}
		if added == 0 {
			break
		}
		idx = int(lastIdx) + 1
	}
	return len(seen)
}

func (e *Exporter) login() error {
	form := url.Values{"pwd": {e.password}}
	resp, err := e.client.PostForm(e.target+"/login", form)
	if err != nil {
		return fmt.Errorf("POST login: %w", err)
	}
	defer resp.Body.Close()
	for _, c := range resp.Cookies() {
		if c.Name == "session" {
			e.cookie = c.Value
			return nil
		}
	}
	setCookie := resp.Header.Get("Set-Cookie")
	if strings.HasPrefix(setCookie, "session=") {
		e.cookie = setCookie[8:]
		if idx := strings.IndexByte(e.cookie, ';'); idx >= 0 {
			e.cookie = e.cookie[:idx]
		}
		return nil
	}
	return fmt.Errorf("no session cookie in response")
}

func fetchJSON[T any](e *Exporter, path string) (*T, error) {
	req, err := http.NewRequest("GET", e.target+path, nil)
	if err != nil {
		return nil, err
	}
	req.AddCookie(&http.Cookie{Name: "session", Value: e.cookie})
	resp, err := e.client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("GET %s: %w", path, err)
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusUnauthorized {
		if err := e.login(); err != nil {
			return nil, fmt.Errorf("re-login failed: %w", err)
		}
		req, _ = http.NewRequest("GET", e.target+path, nil)
		req.AddCookie(&http.Cookie{Name: "session", Value: e.cookie})
		resp, err = e.client.Do(req)
		if err != nil {
			return nil, fmt.Errorf("GET %s (retry): %w", path, err)
		}
		defer resp.Body.Close()
	}
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("read %s: %w", path, err)
	}
	var v T
	if err := json.Unmarshal(body, &v); err != nil {
		return nil, fmt.Errorf("json %s: %w", path, err)
	}
	return &v, nil
}

func linkSpeedToBPS(code int) uint64 {
	switch code {
	case 0:
		return 0
	case 1:
		return 10000000
	case 2:
		return 100000000
	case 3:
		return 1000000000
	case 4:
		return 500000000
	case 5:
		return 10000000000
	case 6:
		return 2500000000
	case 7:
		return 5000000000
	default:
		return 0
	}
}

func parseHex64(s string) uint64 {
	s = strings.TrimPrefix(s, "0x")
	s = strings.TrimPrefix(s, "0X")
	if s == "" {
		return 0
	}
	v, err := strconv.ParseUint(s, 16, 64)
	if err != nil {
		return 0
	}
	return v
}

func parseHex16(s string) *uint16 {
	s = strings.TrimPrefix(s, "0x")
	s = strings.TrimPrefix(s, "0X")
	if s == "" {
		return nil
	}
	v, err := strconv.ParseUint(s, 16, 16)
	if err != nil {
		return nil
	}
	vv := uint16(v)
	return &vv
}

func parseHexInt(s string) int {
	s = strings.TrimPrefix(s, "0x")
	s = strings.TrimPrefix(s, "0X")
	if s == "" {
		return 0
	}
	v, err := strconv.ParseInt(s, 16, 32)
	if err != nil {
		return 0
	}
	return int(v)
}
