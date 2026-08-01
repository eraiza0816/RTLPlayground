package main

import (
	"bufio"
	"fmt"
	"net/http/cookiejar"
	"os"
	"strings"
)

type interactiveState struct {
	client *Client
	mode   string
	config bool
}

func interactiveMode(client *Client) {
	interactiveModeWithMode(client, envOr("MODE", "default"))
}

func interactiveModeWithMode(client *Client, mode string) {
	state := &interactiveState{
		client: client,
		mode:   mode,
	}

	fmt.Printf("rtlpctl: RTLPlayground CLI (connected to %s)\n", client.baseURL)
	fmt.Println("Type 'help' for commands, 'exit' to quit.")

	scanner := bufio.NewScanner(os.Stdin)
	for {
		prompt := "rtlp> "
		if state.mode == "arista" {
			if state.config {
				prompt = "rtlp(config)# "
			} else {
				prompt = "rtlp# "
			}
		}

		fmt.Print(prompt)
		if !scanner.Scan() {
			break
		}
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}
		args := splitArgs(line)
		cmd := args[0]
		cmdArgs := args[1:]

		switch {
		case matchCmd(cmd, "exit") || matchCmd(cmd, "quit"):
			if state.config {
				state.config = false
				continue
			}
			return
		case matchCmd(cmd, "end"):
			if state.mode == "arista" {
				state.config = false
				continue
			}
			return
		case matchCmd(cmd, "help"):
			printInteractiveHelp(state.mode)
		case matchCmd(cmd, "host"):
			if len(cmdArgs) == 0 {
				fmt.Printf("current host: %s\n", client.baseURL)
			} else {
				if err := validateHost(cmdArgs[0]); err != nil {
					fmt.Fprintln(os.Stderr, "Error: invalid host:", err)
				} else {
					host := normalizeHost(cmdArgs[0])
					client.baseURL = fmt.Sprintf("http://%s", host)
					newJar, _ := cookiejar.New(nil)
					client.http.Jar = newJar
					fmt.Printf("host set to %s\n", host)
				}
			}
		case matchCmd(cmd, "password"):
			if len(cmdArgs) == 0 {
				fmt.Println("current password: (set)")
				return
			}
			client.password = cmdArgs[0]
			fmt.Println("password updated")
		case matchCmd(cmd, "login"):
			if err := cmdLogin(client, cmdArgs, false); err != nil {
				fmt.Fprintln(os.Stderr, "Error:", err)
			}
		case matchCmd(cmd, "mode"):
			if len(cmdArgs) == 0 {
				fmt.Printf("current mode: %s\n", state.mode)
			} else {
				switch {
				case matchCmd(cmdArgs[0], "arista"), matchCmd(cmdArgs[0], "default"):
					state.mode = cmdArgs[0]
					state.config = false
					fmt.Printf("mode set to %s\n", cmdArgs[0])
				default:
					fmt.Printf("unknown mode: %s (use: default, arista)\n", cmdArgs[0])
				}
			}
		case matchCmd(cmd, "configure") || matchCmd(cmd, "conf"):
			if state.mode == "arista" && (len(cmdArgs) == 0 || matchCmd(cmdArgs[0], "terminal") || matchCmd(cmdArgs[0], "t")) {
				state.config = true
			} else {
				runInteractiveCmd(state, args)
			}
		default:
			runInteractiveCmd(state, args)
		}
	}
}

func runInteractiveCmd(state *interactiveState, args []string) {
	if state.client.password != "" {
		if err := state.client.Login(); err != nil {
			fmt.Fprintln(os.Stderr, "Error: login failed:", err)
			return
		}
	}
	cfg := config{mode: state.mode, jsonMode: false}
	if err := runCmd(state.client, args, cfg); err != nil {
		if err.Error() == "exit" {
			os.Exit(0)
		}
		fmt.Fprintln(os.Stderr, "Error:", err)
	}
}

func printInteractiveHelp(mode string) {
	if mode == "arista" {
		fmt.Println(`Arista EOS-style commands:
  show interfaces status                  Port status
  show interfaces Ethernet<X> status      Port status (filtered)
  show interfaces counters [Ethernet<X>]  Port counters
  show running-config                     Running configuration
  show vlan                               VLAN list
  show vlan id <vid>                      VLAN details
  show inventory                          System information
  show mac address-table                  MAC forwarding table
  show logging                            Command log
  show port-channel                       LAG groups
  show monitoring                         Mirror configuration
  show queue                              Bandwidth settings
  show system                             System information
  show mtu                                MTU settings
  show config                             Configuration text
  configure [terminal]                    Enter config mode
  copy running-config startup-config      Save configuration
  write memory                            Save configuration
  clear logging                           Clear command log
  enable                                  Privileged mode

Internal commands:
  host [IP]                      Show or set host
  password [PWD]                 Set password
  mode [arista|default]          Switch CLI mode
  exit/quit                      Exit
  help                           This help`)
		return
	}
	fmt.Println(`Commands:
  login <password>       Authenticate with the switch
  host [IP]              Show or set switch IP address
  password [PWD]         Set login password
  status                 Show port status
  info                   Show system information
  vlan <vid>             Show VLAN details (1-4094)
  vlan list              List all VLANs
  counters <port>        Show port counters (1-8)
  eee                    Show EEE configuration
  bandwidth              Show bandwidth settings
  mirror                 Show port mirroring
  lag                    Show LAG groups
  mtu                    Show MTU settings
  sfp-diag               Show SFP module diagnostics
  l2 [idx]               Show L2 table (decimal 0-4095)
  l2 delete <idx>        Delete L2 entry (decimal 0-4095)
  config                 Show running configuration
  config upload <file>   Upload config file
  cmd <text>             Execute CLI command
  cmd-log                Show command history
  cmd-log clear          Clear command history
  upload firmware <file> Upload firmware
  reset                  Reboot the switch
  host [IP]              Show or set host
  password [PWD]         Set password
  mode [arista|default]  Switch CLI mode
  exit, quit             Exit interactive mode
  help                   Show this help`)
}

func splitArgs(line string) []string {
	var args []string
	var current strings.Builder
	inQuote := false
	for _, r := range line {
		switch {
		case r == '"' || r == '\'':
			inQuote = !inQuote
		case r == ' ' && !inQuote:
			if current.Len() > 0 {
				args = append(args, current.String())
				current.Reset()
			}
		default:
			current.WriteRune(r)
		}
	}
	if current.Len() > 0 {
		args = append(args, current.String())
	}
	return args
}
