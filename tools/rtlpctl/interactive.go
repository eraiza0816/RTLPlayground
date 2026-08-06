package main

import (
	"bufio"
	"fmt"
	"net/http/cookiejar"
	"os"
	"strings"
	"time"

	"golang.org/x/sys/unix"
	"golang.org/x/term"
)

type interactiveState struct {
	client   *Client
	mode     string
	config   bool
	hostname string
}

func interactiveMode(client *Client) {
	interactiveModeWithMode(client, envOr("MODE", "default"))
}

func interactiveModeWithMode(client *Client, mode string) {
	state := &interactiveState{
		client: client,
		mode:   mode,
	}
	state.refreshHostname()

	fmt.Printf("rtlpctl: RTLPlayground CLI (connected to %s)\n", client.baseURL)
	fmt.Println("Type 'help' for commands, 'exit' to quit. Tab completes, '?' shows device help, arrow keys recall history.")

	if term.IsTerminal(int(os.Stdin.Fd())) {
		interactiveTTY(state)
	} else {
		interactivePiped(state)
	}
}

// refreshHostname fetches the device hostname for the prompt (best effort:
// any failure leaves the prompt without the hostname). Uses a short timeout
// so an unreachable device does not stall the shell at startup.
func (state *interactiveState) refreshHostname() {
	state.hostname = ""
	if state.client.password == "" {
		return
	}
	client := state.client.http
	client.Timeout = 2 * time.Second
	defer func() { client.Timeout = 0 }()
	if err := state.client.Login(); err != nil {
		return
	}
	data, err := state.client.GetJSON("/information.json")
	if err != nil {
		return
	}
	if m, ok := data.(map[string]interface{}); ok {
		state.hostname = fmtStr(m["hostname"])
	}
}

func interactivePrompt(state *interactiveState) string {
	prefix := "rtlp"
	if state.hostname != "" {
		prefix = "rtlpctl@" + state.hostname
	}
	if state.mode == "arista" {
		if state.config {
			return prefix + "(config)# "
		}
		return prefix + "# "
	}
	return prefix + "> "
}

// executeLine runs one input line. Returns false when the shell should exit.
func (state *interactiveState) executeLine(line string) bool {
	line = strings.TrimSpace(line)
	if line == "" {
		return true
	}
	args := splitArgs(line)
	cmd := args[0]
	cmdArgs := args[1:]

	switch {
	case matchCmd(cmd, "exit") || matchCmd(cmd, "quit"):
		if state.config {
			state.config = false
			return true
		}
		return false
	case matchCmd(cmd, "end"):
		if state.mode == "arista" {
			state.config = false
			return true
		}
		return false
	case matchCmd(cmd, "help"):
		printInteractiveHelp(state.mode)
	case matchCmd(cmd, "host"):
		if len(cmdArgs) == 0 {
			fmt.Printf("current host: %s\n", state.client.baseURL)
		} else {
			if err := validateHost(cmdArgs[0]); err != nil {
				fmt.Fprintln(os.Stderr, "Error: invalid host:", err)
			} else {
				host := normalizeHost(cmdArgs[0])
				state.client.baseURL = fmt.Sprintf("http://%s", host)
				newJar, _ := cookiejar.New(nil)
				state.client.http.Jar = newJar
				fmt.Printf("host set to %s\n", host)
				state.refreshHostname()
			}
		}
	case matchCmd(cmd, "password"):
		if len(cmdArgs) == 0 {
			fmt.Println("current password: (set)")
		} else {
			state.client.password = cmdArgs[0]
			fmt.Println("password updated")
			state.refreshHostname()
		}
	case matchCmd(cmd, "login"):
		if err := cmdLogin(state.client, cmdArgs, false); err != nil {
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
	return true
}

// interactivePiped handles non-TTY stdin (pipes, tests): plain line mode.
func interactivePiped(state *interactiveState) {
	scanner := bufio.NewScanner(os.Stdin)
	for {
		fmt.Print(interactivePrompt(state))
		if !scanner.Scan() {
			break
		}
		if !state.executeLine(scanner.Text()) {
			return
		}
	}
}

// interactiveTTY provides a raw-mode line editor with Tab completion and
// '?' context help (the firmware equivalents, hosted in rtlpctl).
func interactiveTTY(state *interactiveState) {
	oldState, err := term.MakeRaw(int(os.Stdin.Fd()))
	if err != nil {
		interactivePiped(state)
		return
	}
	defer term.Restore(int(os.Stdin.Fd()), oldState)

	// MakeRaw also disables output post-processing (OPOST/ONLCR), so a bare
	// '\n' only moves the cursor down without returning to column 0, drifting
	// every new line to the right. Re-enable the tty's LF->CRLF translation
	// (while keeping CR untranslated) so all output stays left-aligned.
	if tio, err := unix.IoctlGetTermios(int(os.Stdin.Fd()), unix.TCGETS); err == nil {
		tio.Oflag &^= unix.OCRNL | unix.ONLRET
		tio.Oflag |= unix.OPOST | unix.ONLCR
		unix.IoctlSetTermios(int(os.Stdin.Fd()), unix.TCSETS, tio)
	}

	var line []rune
	cursor := 0

	// Command history navigation (arrow up/down), like the firmware's
	// line editor. histPos == len(hist) means a fresh editing line.
	var hist []string
	histPos := 0
	var savedLine []rune

	redraw := func() {
		p := interactivePrompt(state)
		fmt.Printf("\r\033[K%s%s", p, string(line))
		if n := len(line) - cursor; n > 0 {
			fmt.Printf("\033[%dD", n)
		}
	}
	finishLine := func() {
		fmt.Println()
		cmd := string(line)
		if !state.executeLine(cmd) {
			return
		}
		if cmd != "" {
			hist = append(hist, cmd)
			if len(hist) > 100 {
				hist = hist[1:]
			}
		}
		line = nil
		cursor = 0
		histPos = len(hist)
		savedLine = nil
		fmt.Print(interactivePrompt(state))
	}

	fmt.Print(interactivePrompt(state))
	r := bufio.NewReader(os.Stdin)
	for {
		b, err := r.ReadByte()
		if err != nil {
			return
		}
		switch b {
		case 3: // ^C
			fmt.Println("^C")
			return
		case 4: // ^D
			if len(line) == 0 {
				fmt.Println()
				return
			}
		case '\r', '\n':
			finishLine()
		case 127, 8: // Backspace / ^H
			if cursor > 0 {
				line = append(line[:cursor-1], line[cursor:]...)
				cursor--
				redraw()
			}
		case '\t':
			newLine, newCursor := completeDeviceWord(state, line, cursor)
			if newLine != nil {
				line = newLine
				cursor = newCursor
				redraw()
			}
		case '?':
			if cursor == 0 || line[cursor-1] == ' ' {
				showDeviceContextHelp(state, string(line))
				redraw()
			} else {
				line = append(line[:cursor], append([]rune{'?'}, line[cursor:]...)...)
				cursor++
				redraw()
			}
		case 27: // ESC [ A/B/C/D: arrow keys
			seq, err := r.ReadByte()
			if err != nil {
				return
			}
			if seq != '[' {
				break
			}
			dir, err := r.ReadByte()
			if err != nil {
				return
			}
			switch dir {
			case 'A': // Up: previous history entry
				if len(hist) == 0 {
					break
				}
				if histPos == len(hist) {
					savedLine = line
				}
				if histPos > 0 {
					histPos--
					line = []rune(hist[histPos])
					cursor = len(line)
					redraw()
				}
			case 'B': // Down: next history entry (or back to the edited line)
				if histPos < len(hist) {
					histPos++
					if histPos == len(hist) {
						line = savedLine
					} else {
						line = []rune(hist[histPos])
					}
					cursor = len(line)
					redraw()
				}
			case 'C': // Right: move cursor forward
				if cursor < len(line) {
					cursor++
					redraw()
				}
			case 'D': // Left: move cursor back
				if cursor > 0 {
					cursor--
					redraw()
				}
			}
		default:
			if b >= 32 && b < 127 {
				line = append(line[:cursor], append([]rune{rune(b)}, line[cursor:]...)...)
				cursor++
				redraw()
			}
		}
	}
}

// currentWord returns the word being completed at the end of the line.
func currentWord(line []rune) (prefix string, wordStart int) {
	wordStart = len(line)
	for wordStart > 0 && line[wordStart-1] != ' ' {
		wordStart--
	}
	return string(line[wordStart:]), wordStart
}

// completeDeviceWord completes the word at the end of the line. Returns the
// new line and cursor, or (nil, 0) when nothing was completed.
func completeDeviceWord(state *interactiveState, line []rune, cursor int) ([]rune, int) {
	if cursor != len(line) || len(line) == 0 {
		return nil, 0
	}
	prefix, wordStart := currentWord(line)
	words := splitArgs(string(line[:wordStart]))
	matches := deviceCompletions(words, prefix)
	if state.mode == "arista" {
		matches = aristaCompletions(words, prefix, matches)
	} else if len(words) == 0 {
		for _, c := range internalCmdNames {
			if strings.HasPrefix(c, prefix) {
				matches = append(matches, c)
			}
		}
	}
	switch {
	case len(matches) == 0:
		// no candidates: bell
		fmt.Print("\a")
		return nil, 0
	case len(matches) == 1:
		complete := matches[0]
		newLine := append(append([]rune{}, line[:wordStart]...), []rune(complete)...)
		newLine = append(newLine, line[cursor:]...)
		return newLine, wordStart + len([]rune(complete))
	default:
		cp := commonCompletionPrefix(matches)
		if len(cp) > len(prefix) {
			newLine := append(append([]rune{}, line[:wordStart]...), []rune(cp)...)
			newLine = append(newLine, line[cursor:]...)
			return newLine, wordStart + len([]rune(cp))
		}
		fmt.Println()
		for _, m := range matches {
			fmt.Printf("  %s\n", m)
		}
		return nil, 0
	}
}

func commonCompletionPrefix(matches []string) string {
	if len(matches) == 0 {
		return ""
	}
	cp := matches[0]
	for _, m := range matches[1:] {
		for len(cp) > 0 && !strings.HasPrefix(m, cp) {
			cp = cp[:len(cp)-1]
		}
	}
	return cp
}

var internalCmdNames = []string{
	"exit", "quit", "end", "help", "host", "password", "login",
	"mode", "configure", "conf",
}

var aristaShowSubs = []string{
	"interfaces", "running-config", "startup-config", "arp", "vlan", "inventory", "mac",
	"logging", "port-channel", "monitoring", "queue", "system",
	"mtu", "eee", "config", "cmd-log",
}

var aristaTopNames = []string{
	"show", "configure", "copy", "write", "clear", "enable",
	"disable", "exit", "end", "login", "help",
}

func aristaCompletions(words []string, prefix string, device []string) []string {
	if len(words) == 0 {
		for _, c := range aristaTopNames {
			if strings.HasPrefix(c, prefix) {
				device = append(device, c)
			}
		}
		return device
	}
	if matchCmd(words[0], "show") {
		for _, c := range aristaShowSubs {
			if strings.HasPrefix(c, prefix) {
				device = append(device, c)
			}
		}
	}
	return device
}

// showDeviceContextHelp prints device help for a line ending in '?'.
func showDeviceContextHelp(state *interactiveState, line string) {
	trim := strings.TrimSuffix(line, "?")
	trim = strings.TrimSpace(trim)
	fmt.Println()
	if state.mode == "arista" {
		printInteractiveHelp("arista")
		return
	}
	if trim == "" {
		printDeviceHelp()
		return
	}
	words := splitArgs(trim)
	if g := deviceCmdGroupByName(words[0]); g != nil {
		printDeviceSubHelp(g)
	} else {
		fmt.Println("  (unknown command)")
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
  clear mac address-table dynamic         Flush learned MACs
  clear logging                           Clear command log
  enable                                  Privileged mode

Device commands (sent as-is to the switch):
  '?' lists them; Tab completes.

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
  ping <ip>              Send 4 ICMP echoes from the switch
  lldp [on|off|show]     LLDP neighbor discovery
  igmp [on|off|show]     IGMP snooping control
  igmp querier [..]      ASIC IGMP/MLD querier (on|off|show)
  igmp mld [..]          MLD snooping control (on|off|show)
  storm-control ...      Storm control: on <type> <rate>[k|p], off, status
  qos ...                QoS: on|off|mode|pcp|dscp|sched|status
  acl ...                ACL: on|off|add|del|show
  host [IP]              Show or set host
  password [PWD]         Set password
  mode [arista|default]  Switch CLI mode
  exit, quit             Exit interactive mode
  help                   Show this help

Device commands:
  '?' alone lists all device commands; 'sfp ?' lists a command's
  sub-commands. Tab completes device commands.
  See doc/commands.md for the full reference.`)
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
