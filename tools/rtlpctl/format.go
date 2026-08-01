package main

import (
	"encoding/json"
	"fmt"
	"os"
	"strings"
	"text/tabwriter"
)

func printJSON(v interface{}) {
	enc := json.NewEncoder(os.Stdout)
	enc.SetIndent("", "  ")
	enc.Encode(v)
}

func printTable(header []string, rows [][]string) {
	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	for _, h := range header {
		fmt.Fprintf(w, "%s\t", h)
	}
	fmt.Fprintf(w, "\n")
	for _, row := range rows {
		for _, col := range row {
			fmt.Fprintf(w, "%s\t", col)
		}
		fmt.Fprintf(w, "\n")
	}
	w.Flush()
}

func printKV(kv map[string]string) {
	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	for k, v := range kv {
		fmt.Fprintf(w, "%s:\t%s\n", k, v)
	}
	w.Flush()
}

func fmtBool(v interface{}) string {
	switch val := v.(type) {
	case bool:
		if val {
			return "yes"
		}
		return "no"
	case float64:
		if val != 0 {
			return "yes"
		}
		return "no"
	case json.Number:
		n, _ := val.Int64()
		if n != 0 {
			return "yes"
		}
		return "no"
	}
	return "no"
}

func enabledToStr(v interface{}) string {
	if s, ok := v.(string); ok {
		if s == "1" {
			return "enabled"
		}
		return "disabled"
	}
	return fmtBool(v)
}

func fmtInt(v interface{}) string {
	switch val := v.(type) {
	case float64:
		return fmt.Sprintf("%.0f", val)
	case json.Number:
		return val.String()
	case string:
		return val
	}
	return fmt.Sprintf("%v", v)
}

func fmtStr(v interface{}) string {
	if v == nil {
		return ""
	}
	s, ok := v.(string)
	if !ok {
		return fmt.Sprintf("%v", v)
	}
	return s
}

func fmtLink(v interface{}) string {
	switch val := v.(type) {
	case float64:
		n := int(val)
		switch n {
		case 0:
			return "down"
		case 5:
			return "2.5G"
		case 4:
			return "1G"
		case 3:
			return "100M"
		case 2:
			return "10M"
		default:
			return fmt.Sprintf("link(%d)", n)
		}
	case json.Number:
		n, _ := val.Int64()
		switch n {
		case 0:
			return "down"
		case 5:
			return "2.5G"
		case 4:
			return "1G"
		case 3:
			return "100M"
		case 2:
			return "10M"
		default:
			return fmt.Sprintf("link(%d)", n)
		}
	}
	return fmt.Sprintf("%v", v)
}

func fmtCounter(v interface{}) string {
	s := fmtStr(v)
	if s == "" {
		return "0"
	}
	return s
}

func fmtPortStatus(ports []interface{}, asJSON bool) {
	if asJSON {
		printJSON(ports)
		return
	}
	header := []string{"Port", "Name", "Link", "Enabled", "TX Good", "TX Bad", "RX Good", "RX Bad"}
	var rows [][]string
	for _, p := range ports {
		pm := p.(map[string]interface{})
		row := []string{
			fmtInt(pm["portNum"]),
			fmtStr(pm["name"]),
			fmtLink(pm["link"]),
			fmtBool(pm["enabled"]),
			fmtCounter(pm["txG"]),
			fmtCounter(pm["txB"]),
			fmtCounter(pm["rxG"]),
			fmtCounter(pm["rxB"]),
		}
		rows = append(rows, row)
		if sfpVendor := fmtStr(pm["sfp_vendor"]); sfpVendor != "" {
			rows = append(rows, []string{"", fmt.Sprintf("  SFP: %s %s", sfpVendor, fmtStr(pm["sfp_model"]))})
			if serial := fmtStr(pm["sfp_serial"]); serial != "" {
				rows = append(rows, []string{"", fmt.Sprintf("  Serial: %s", serial)})
			}
			if los := pm["sfp_los"]; los != nil {
				rows = append(rows, []string{"", fmt.Sprintf("  LOS: %s", fmtBool(los))})
			}
			rows = append(rows, []string{"", "  (use 'sfp-diag' for module diagnostics)"})
		}
		if adv := fmtStr(pm["adv"]); adv != "" {
			modes := []string{}
			if len(adv) >= 6 {
				if adv[0] == '1' {
					modes = append(modes, "2.5G-FD")
				}
				if adv[1] == '1' {
					modes = append(modes, "1G-FD")
				}
				if adv[2] == '1' {
					modes = append(modes, "100M-FD")
				}
				if adv[3] == '1' {
					modes = append(modes, "100M-HD")
				}
				if adv[4] == '1' {
					modes = append(modes, "10M-FD")
				}
				if adv[5] == '1' {
					modes = append(modes, "10M-HD")
				}
			}
			if len(modes) > 0 {
				rows = append(rows, []string{"", "  Adv: " + strings.Join(modes, " ")})
			}
		}
	}
	printTable(header, rows)
}

func fmtSfpDiag(ports []interface{}, asJSON bool) {
	if asJSON {
		printJSON(ports)
		return
	}
	header := []string{"Port", "Options", "Temp", "Vcc", "TX Bias", "TX Power", "RX Power", "State"}
	var rows [][]string
	for _, p := range ports {
		pm := p.(map[string]interface{})
		rows = append(rows, []string{
			fmtInt(pm["portNum"]),
			fmtStr(pm["sfp_options"]),
			fmtStr(pm["sfp_temp"]),
			fmtStr(pm["sfp_vcc"]),
			fmtStr(pm["sfp_txbias"]),
			fmtStr(pm["sfp_txpower"]),
			fmtStr(pm["sfp_rxpower"]),
			fmtStr(pm["sfp_state"]),
		})
	}
	printTable(header, rows)
}

func fmtInformation(info map[string]interface{}, asJSON bool) {
	if asJSON {
		printJSON(info)
		return
	}
	kv := map[string]string{}
	if v, ok := info["ip_address"]; ok {
		kv["IP Address"] = fmtStr(v)
	}
	if v, ok := info["ip_gateway"]; ok {
		kv["Gateway"] = fmtStr(v)
	}
	if v, ok := info["ip_netmask"]; ok {
		kv["Netmask"] = fmtStr(v)
	}
	if v, ok := info["mac_address"]; ok {
		kv["MAC Address"] = fmtStr(v)
	}
	if v, ok := info["hostname"]; ok {
		kv["Hostname"] = fmtStr(v)
	}
	if v, ok := info["sw_ver"]; ok {
		kv["Software Version"] = fmtStr(v)
	}
	if v, ok := info["build_date"]; ok {
		kv["Build Date"] = fmtStr(v)
	}
	if v, ok := info["hw_ver"]; ok {
		kv["Hardware"] = fmtStr(v)
	}
	if v, ok := info["flash_size"]; ok {
		kv["Flash Size"] = fmtStr(v)
	}
	if v, ok := info["telnet_enabled"]; ok {
		kv["Telnet"] = enabledToStr(v)
	}
	if v, ok := info["web_enabled"]; ok {
		kv["Web UI"] = enabledToStr(v)
	}
	for i := 0; ; i++ {
		key := fmt.Sprintf("sfp_slot_%d", i)
		if v, ok := info[key]; ok && fmtStr(v) != "" {
			kv[fmt.Sprintf("SFP Slot %d", i)] = fmtStr(v)
		} else {
			break
		}
	}
	printKV(kv)
}

func fmtVLAN(vlan map[string]interface{}, asJSON bool) {
	if asJSON {
		printJSON(vlan)
		return
	}
	kv := map[string]string{}
	kv["Members"] = fmtStr(vlan["members"])
	kv["Name"] = fmtStr(vlan["name"])
	kv["PVID"] = fmtStr(vlan["pvid"])
	printKV(kv)
}

func fmtVLANList(list []interface{}, asJSON bool) {
	if asJSON {
		printJSON(list)
		return
	}
	if len(list) == 0 {
		fmt.Println("no VLANs configured")
		return
	}
	header := []string{"VID", "Name"}
	var rows [][]string
	for _, v := range list {
		vm := v.(map[string]interface{})
		rows = append(rows, []string{fmtInt(vm["id"]), fmtStr(vm["name"])})
	}
	printTable(header, rows)
}

func fmtCounters(port int, counters interface{}, asJSON bool) {
	if asJSON {
		printJSON(counters)
		return
	}
	arr, ok := counters.([]interface{})
	if !ok {
		fmt.Println("unexpected counters response format")
		return
	}
	fmt.Printf("Port %d counters (%d registers):\n", port, len(arr))
	for i, c := range arr {
		fmt.Printf("  [%02d] %s\n", i, fmtStr(c))
	}
}

func fmtEEE(ports []interface{}, asJSON bool) {
	if asJSON {
		printJSON(ports)
		return
	}
	header := []string{"Port", "EEE", "EEE LP", "Active"}
	var rows [][]string
	for _, p := range ports {
		pm := p.(map[string]interface{})
		if fmtInt(pm["isSFP"]) == "1" {
			rows = append(rows, []string{fmtInt(pm["portNum"]), "N/A", "N/A", "N/A"})
		} else {
			rows = append(rows, []string{
				fmtInt(pm["portNum"]),
				fmtStr(pm["eee"]),
				fmtStr(pm["eee_lp"]),
				fmtBool(pm["active"]),
			})
		}
	}
	printTable(header, rows)
}

func fmtBandwidth(ports []interface{}, asJSON bool) {
	if asJSON {
		printJSON(ports)
		return
	}
	header := []string{"Port", "Ingress Limit", "Ingress BW", "Ingress FC", "Egress Limit", "Egress BW"}
	var rows [][]string
	for _, p := range ports {
		pm := p.(map[string]interface{})
		rows = append(rows, []string{
			fmtInt(pm["portNum"]),
			fmtBool(pm["iLimited"]),
			fmtStr(pm["iBW"]),
			fmtBool(pm["iFC"]),
			fmtBool(pm["eLimited"]),
			fmtStr(pm["eBW"]),
		})
	}
	printTable(header, rows)
}

func fmtMirror(mirror map[string]interface{}, asJSON bool) {
	if asJSON {
		printJSON(mirror)
		return
	}
	kv := map[string]string{}
	kv["Enabled"] = fmtBool(mirror["enabled"])
	kv["Monitor Port"] = fmtInt(mirror["mPort"])
	kv["TX Monitored Ports"] = fmtStr(mirror["mirror_tx"])
	kv["RX Monitored Ports"] = fmtStr(mirror["mirror_rx"])
	printKV(kv)
}

func fmtLAG(lags []interface{}, asJSON bool) {
	if asJSON {
		printJSON(lags)
		return
	}
	header := []string{"LAG", "Members", "Hash"}
	var rows [][]string
	for _, l := range lags {
		lm := l.(map[string]interface{})
		rows = append(rows, []string{
			fmtInt(lm["lagNum"]),
			fmtStr(lm["members"]),
			fmtStr(lm["hash"]),
		})
	}
	printTable(header, rows)
}

func fmtMTU(ports []interface{}, asJSON bool) {
	if asJSON {
		printJSON(ports)
		return
	}
	header := []string{"Port", "MTU"}
	var rows [][]string
	for _, p := range ports {
		pm := p.(map[string]interface{})
		rows = append(rows, []string{
			fmtInt(pm["portNum"]),
			fmtStr(pm["mtu"]),
		})
	}
	printTable(header, rows)
}

func fmtL2(entries []interface{}, asJSON bool) {
	if asJSON {
		printJSON(entries)
		return
	}
	if len(entries) == 0 {
		fmt.Println("no L2 entries")
		return
	}
	header := []string{"Index", "MAC", "VLAN", "Type", "Port"}
	var rows [][]string
	for _, e := range entries {
		em := e.(map[string]interface{})
		typeStr := "dynamic"
		if fmtStr(em["type"]) == "s" {
			typeStr = "static"
		}
		rows = append(rows, []string{
			fmtStr(em["idx"]),
			fmtStr(em["mac"]),
			fmtStr(em["vlan"]),
			typeStr,
			fmtInt(em["port"]),
		})
	}
	printTable(header, rows)
}

func fmtL2Delete(result map[string]interface{}, asJSON bool) {
	if asJSON {
		printJSON(result)
		return
	}
	if fmtInt(result["result"]) == "1" {
		fmt.Println("entry deleted")
	} else {
		fmt.Println("entry not found")
	}
}
