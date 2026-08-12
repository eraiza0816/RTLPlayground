package main

import (
	"bufio"
	"encoding/hex"
	"fmt"
	"os"
	"strings"
)

type config struct {
	host     string
	password string
	psk      string
	jsonMode bool
	mode     string
	force    bool
}

func main() {
	loadDotEnv(".env")
	cfg := parseFlags()
	args := os.Args[1:]
	args = filterFlags(args, &cfg)
	if cfg.host == "" {
		cfg.host = envOr("RTLP_HOST", "192.168.1.1")
	}
	if cfg.password == "" {
		cfg.password = envOr("RTLP_PASSWORD", "")
	}
	if cfg.mode == "" {
		cfg.mode = envOr("MODE", "default")
	}

	if err := validateHost(cfg.host); err != nil {
		fmt.Fprintln(os.Stderr, "Error: invalid host:", err)
		os.Exit(1)
	}
	cfg.host = normalizeHost(cfg.host)

	if err := validateMode(cfg.mode); err != nil {
		fmt.Fprintln(os.Stderr, "Error:", err)
		os.Exit(1)
	}

	client := NewClient(cfg.host, cfg.password)

	if cfg.psk != "" {
		key, err := hex.DecodeString(cfg.psk)
		if err != nil || len(key) != aeadKeyLen {
			fmt.Fprintln(os.Stderr, "Error: RTLP_PSK must be 64 hex characters")
			os.Exit(1)
		}
		client.psk = key
	}

	if len(args) == 0 {
		interactiveModeWithMode(client, cfg.mode)
		return
	}

	if err := runCmd(client, args, cfg); err != nil {
		fmt.Fprintln(os.Stderr, "Error:", err)
		os.Exit(1)
	}
}

func parseFlags() config {
	cfg := config{
		host:     envOr("RTLP_HOST", ""),
		password: envOr("RTLP_PASSWORD", ""),
		psk:      envOr("RTLP_PSK", ""),
		mode:     envOr("MODE", ""),
	}
	return cfg
}

func filterFlags(args []string, cfg *config) []string {
	var remaining []string
	// Options are only recognized before the first non-option argument
	// (the command word).  Everything from the command on is passed
	// through untouched, so a command argument that happens to start
	// with "--" (e.g. `passwd --json`) is not consumed by the flag
	// parser.  Option values (--host <ip>, ...) are consumed here.
	seenCommand := false
	for i := 0; i < len(args); i++ {
		if !seenCommand && strings.HasPrefix(args[i], "-") {
			switch {
			case args[i] == "--help" || args[i] == "-h":
				printHelp()
				os.Exit(0)
			case args[i] == "--json" || args[i] == "-j":
				cfg.jsonMode = true
			case args[i] == "--force" || args[i] == "-f":
				cfg.force = true
			case args[i] == "--host" && i+1 < len(args):
				i++
				cfg.host = args[i]
			case strings.HasPrefix(args[i], "--host="):
				cfg.host = args[i][7:]
			case args[i] == "--password" && i+1 < len(args):
				i++
				cfg.password = args[i]
			case strings.HasPrefix(args[i], "--password="):
				cfg.password = args[i][11:]
			case args[i] == "--psk" && i+1 < len(args):
				i++
				cfg.psk = args[i]
			case strings.HasPrefix(args[i], "--psk="):
				cfg.psk = args[i][6:]
			case args[i] == "--env-file" && i+1 < len(args):
				i++
				loadDotEnv(args[i])
			case strings.HasPrefix(args[i], "--env-file="):
				loadDotEnv(args[i][11:])
			case args[i] == "--mode" && i+1 < len(args):
				i++
				cfg.mode = args[i]
			case strings.HasPrefix(args[i], "--mode="):
				cfg.mode = args[i][7:]
			default:
				remaining = append(remaining, args[i])
			}
			continue
		}
		seenCommand = true
		remaining = append(remaining, args[i])
	}
	return remaining
}

func runCmd(client *Client, args []string, cfg config) error {
	if cfg.mode == "arista" {
		return runAristaCmd(client, args, cfg.jsonMode)
	}

	cmd := args[0]
	cmdArgs := args[1:]

	// Login with either the password or the PSK (a PSK-only invocation
	// must not hit the password path and fail with 401).
	if cmd != "login" && (client.password != "" || len(client.psk) == aeadKeyLen) {
		if err := client.Login(); err != nil {
			return fmt.Errorf("login failed: %w", err)
		}
	}

	switch cmd {
	case "login":
		return cmdLogin(client, cmdArgs, cfg.jsonMode)
	case "status":
		return cmdStatus(client, cmdArgs, cfg.jsonMode)
	case "info":
		return cmdInfo(client, cmdArgs, cfg.jsonMode)
	case "vlan":
		return cmdVLAN(client, cmdArgs, cfg.jsonMode)
	case "counters":
		return cmdCounters(client, cmdArgs, cfg.jsonMode)
	case "eee":
		return cmdEEE(client, cmdArgs, cfg.jsonMode)
	case "bandwidth":
		return cmdBandwidth(client, cmdArgs, cfg.jsonMode)
	case "mirror":
		return cmdMirror(client, cmdArgs, cfg.jsonMode)
	case "lag":
		return cmdLAG(client, cmdArgs, cfg.jsonMode)
	case "mtu":
		return cmdMTU(client, cmdArgs, cfg.jsonMode)
	case "sfp-diag", "sfpdiag":
		return cmdSfpDiag(client, cmdArgs, cfg.jsonMode)
	case "l2":
		return cmdL2(client, cmdArgs, cfg.jsonMode)
	case "config":
		return cmdConfig(client, cmdArgs, cfg.jsonMode)
	case "cmd-log", "cmdlog", "log":
		return cmdCmdLog(client, cmdArgs, cfg.jsonMode)
	case "cmd":
		return cmdCmd(client, cmdArgs, cfg.jsonMode, cfg.force)
	case "ping":
		return cmdPing(client, cmdArgs, cfg.jsonMode)
	case "lldp":
		return cmdLldp(client, cmdArgs, cfg.jsonMode)
	case "igmp":
		return cmdIgmp(client, cmdArgs, cfg.jsonMode)
	case "storm-control":
		return cmdStorm(client, cmdArgs, cfg.jsonMode)
	case "qos":
		return cmdQos(client, cmdArgs, cfg.jsonMode)
	case "acl":
		return cmdAcl(client, cmdArgs, cfg.jsonMode)
	case "hostname":
		return cmdHostname(client, cmdArgs, cfg.jsonMode)
	case "passwd":
		return cmdPasswd(client, cmdArgs, cfg.jsonMode)
	case "ip":
		return cmdIP(client, cmdArgs, cfg.jsonMode)
	case "gw":
		return cmdGW(client, cmdArgs, cfg.jsonMode)
	case "netmask":
		return cmdNetmask(client, cmdArgs, cfg.jsonMode)
	case "port":
		return cmdPort(client, cmdArgs, cfg.jsonMode)
	case "pvid":
		return cmdPvid(client, cmdArgs, cfg.jsonMode)
	case "ingress":
		return cmdIngress(client, cmdArgs, cfg.jsonMode)
	case "isolate":
		return cmdIsolate(client, cmdArgs, cfg.jsonMode)
	case "laghash":
		return cmdLaghash(client, cmdArgs, cfg.jsonMode)
	case "stp":
		return cmdStp(client, cmdArgs, cfg.jsonMode)
	case "telnet":
		return cmdTelnet(client, cmdArgs, cfg.jsonMode)
	case "web":
		return cmdWeb(client, cmdArgs, cfg.jsonMode)
	case "commit":
		return cmdCommit(client, cmdArgs, cfg.jsonMode)
	case "sfp":
		return cmdSfp(client, cmdArgs, cfg.jsonMode)
	case "psk", "preshared-key":
		return cmdPsk(client, cmdArgs, cfg.jsonMode)
	case "regget":
		return cmdRegget(client, cmdArgs, cfg.jsonMode)
	case "regset":
		return cmdRegset(client, cmdArgs, cfg.jsonMode)
	case "sdsget":
		return cmdSdsget(client, cmdArgs, cfg.jsonMode)
	case "sdsset":
		return cmdSdsset(client, cmdArgs, cfg.jsonMode)
	case "phyget":
		return cmdPhyget(client, cmdArgs, cfg.jsonMode)
	case "physet":
		return cmdPhyset(client, cmdArgs, cfg.jsonMode)
	case "show":
		return cmdShow(client, cmdArgs, cfg.jsonMode)
	case "enc-cmd", "enccmd":
		return cmdEnc(client, cmdArgs, cfg.jsonMode, cfg.force)
	case "enc-api", "encapi":
		return cmdEncAPI(client, cmdArgs, cfg.jsonMode)
	case "upload":
		return cmdUpload(client, cmdArgs, cfg.jsonMode)
	case "reset":
		return cmdReset(client, cmdArgs, cfg.jsonMode)
	case "help":
		printHelp()
		return nil
	default:
		return fmt.Errorf("unknown command: %s\nRun 'rtlpctl --help' for usage.", cmd)
	}
}

func loadDotEnv(path string) {
	f, err := os.Open(path)
	if err != nil {
		return
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		line = strings.TrimPrefix(line, "export ")
		k, v, ok := strings.Cut(line, "=")
		if !ok {
			continue
		}
		k = strings.TrimSpace(k)
		v = strings.TrimSpace(v)
		if len(v) >= 2 && (v[0] == '"' && v[len(v)-1] == '"' || v[0] == '\'' && v[len(v)-1] == '\'') {
			v = v[1 : len(v)-1]
		}
		if os.Getenv(k) == "" {
			os.Setenv(k, v)
		}
	}
}

func envOr(key, def string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return def
}
