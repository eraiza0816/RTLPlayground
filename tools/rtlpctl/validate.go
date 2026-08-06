package main

import (
	"fmt"
	"net"
	"os"
	"strconv"
	"strings"
)

func validateMode(mode string) error {
	if mode != "default" && mode != "arista" {
		return fmt.Errorf("invalid mode: %q (must be 'default' or 'arista')", mode)
	}
	return nil
}

func validateFile(path string) error {
	if path == "" {
		return fmt.Errorf("empty file path")
	}
	info, err := os.Stat(path)
	if err != nil {
		return fmt.Errorf("cannot access file %q: %w", path, err)
	}
	if info.IsDir() {
		return fmt.Errorf("%q is a directory, not a file", path)
	}
	return nil
}

func validateHost(host string) error {	if host == "" {
		return fmt.Errorf("empty host")
	}
	if strings.Contains(host, "://") {
		return fmt.Errorf("host must not include a scheme: %s", host)
	}

	hostPart := host
	portPart := ""

	switch {
	case strings.HasPrefix(host, "["):
		end := strings.Index(host, "]")
		if end == -1 {
			return fmt.Errorf("invalid host: missing closing ']' in %s", host)
		}
		hostPart = host[1:end]
		rest := host[end+1:]
		if rest != "" {
			if !strings.HasPrefix(rest, ":") {
				return fmt.Errorf("invalid host: unexpected %q after ']'", rest)
			}
			portPart = rest[1:]
			if portPart == "" {
				return fmt.Errorf("invalid host: empty port")
			}
		}
	case strings.Count(host, ":") == 1:
		idx := strings.LastIndex(host, ":")
		hostPart = host[:idx]
		portPart = host[idx+1:]
		if portPart == "" {
			return fmt.Errorf("invalid host: empty port")
		}
	default:
		// 0 colons (IPv4/hostname) or bare IPv6 (multiple colons)
		if ip := net.ParseIP(host); ip == nil {
			if strings.Count(host, ":") > 1 {
				return fmt.Errorf("invalid host: bare IPv6 must be bracketed: [%s]", host)
			}
		}
	}

	if ip := net.ParseIP(hostPart); ip == nil {
		if err := validateHostname(hostPart); err != nil {
			return err
		}
	}

	if portPart != "" {
		port, err := strconv.Atoi(portPart)
		if err != nil || port < 1 || port > 65535 {
			return fmt.Errorf("invalid port: %s", portPart)
		}
	}
	return nil
}

// normalizeHost brackets a bare IPv6 address so it is safe to embed in a URL.
func normalizeHost(host string) string {
	if strings.Contains(host, ":") && !strings.HasPrefix(host, "[") {
		if ip := net.ParseIP(host); ip != nil && ip.To4() == nil {
			return "[" + host + "]"
		}
	}
	return host
}

func validateHostname(s string) error {
	if s == "" {
		return fmt.Errorf("empty host")
	}
	if len(s) > 253 {
		return fmt.Errorf("hostname too long: %s", s)
	}
	for i, c := range s {
		ok := c == '-' || c == '.' ||
			(c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
		if !ok {
			return fmt.Errorf("invalid character %q in hostname: %s", c, s)
		}
		if c == '-' && (i == 0 || s[i-1] == '.' || i == len(s)-1 || s[i+1] == '.') {
			return fmt.Errorf("hostname label cannot start or end with '-': %s", s)
		}
	}
	if strings.Contains(s, "..") {
		return fmt.Errorf("hostname contains empty label: %s", s)
	}
	return nil
}

func validateCountersPort(s string) error {
	if len(s) != 1 || s[0] < '1' || s[0] > '8' {
		return fmt.Errorf("invalid port: %q (must be a single digit 1-8)", s)
	}
	return nil
}

func validateVLANID(s string) error {
	n, err := strconv.Atoi(s)
	if err != nil || n < 1 || n > 4094 {
		return fmt.Errorf("invalid VLAN ID: %q (must be a number 1-4094)", s)
	}
	return nil
}

func validateL2Idx(s string) error {
	n, err := strconv.Atoi(s)
	if err != nil || n < 0 || n > 4095 {
		return fmt.Errorf("invalid L2 index: %q (must be a number 0-4095)", s)
	}
	return nil
}

func isHexStr(s string) bool {
	for _, c := range s {
		if !(c >= '0' && c <= '9' || c >= 'a' && c <= 'f' || c >= 'A' && c <= 'F') {
			return false
		}
	}
	return true
}

// validatePSK verifies a pre-shared key: exactly 64 hex chars (32 bytes).
// The firmware no longer validates this (invalid chars yield 0xff bytes),
// so the CLI is the enforcing side.
func validatePSK(s string) error {
	if len(s) != 64 {
		return fmt.Errorf("pre-shared key must be exactly 64 hex chars (got %d)", len(s))
	}
	if !isHexStr(s) {
		return fmt.Errorf("pre-shared key must be hex: invalid character in %q", s)
	}
	return nil
}

// validateDeviceHostname verifies a switch hostname: 1-31 chars from
// [a-zA-Z0-9_-].  The firmware no longer validates this and truncates at the
// first invalid character (e.g. a dot), which would silently change the name.
func validateDeviceHostname(s string) error {
	if s == "" {
		return fmt.Errorf("hostname is empty")
	}
	if len(s) > 31 {
		return fmt.Errorf("hostname too long: %d chars (max 31)", len(s))
	}
	for _, c := range s {
		ok := c == '-' || c == '_' ||
			(c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
		if !ok {
			return fmt.Errorf("hostname contains invalid character %q (allowed: letters, digits, '-', '_')", c)
		}
	}
	return nil
}

// validatePassword verifies the web login password: 5-20 chars, matching the
// firmware checks (min 5 in parse_passwd, max 20 per docs).
func validatePassword(s string) error {
	if len(s) < 5 {
		return fmt.Errorf("password too short: %d chars (min 5)", len(s))
	}
	if len(s) > 20 {
		return fmt.Errorf("password too long: %d chars (max 20)", len(s))
	}
	return nil
}

// validateIPAddr checks an IPv4 address.  The firmware still validates
// addresses on the device (dotted-quad only), this is a pre-check to catch
// typos early.
func validateIPAddr(s string) error {
	if ip := net.ParseIP(s); ip == nil || ip.To4() == nil {
		return fmt.Errorf("invalid IP address: %q", s)
	}
	return nil
}

// validateIPOrPrefix checks an IPv4 address with an optional /prefix
// suffix (used by the ACL "ip <addr>[/<prefix>]" match).
func validateIPOrPrefix(s string) error {
	host := s
	if i := strings.IndexByte(s, '/'); i >= 0 {
		host = s[:i]
		prefix := s[i+1:]
		n, err := strconv.Atoi(prefix)
		if err != nil || n < 0 || n > 32 {
			return fmt.Errorf("invalid prefix: %q (must be 0-32)", prefix)
		}
		if strings.ContainsAny(prefix, "+- ") {
			return fmt.Errorf("invalid prefix: %q (must be 0-32)", prefix)
		}
	}
	return validateIPAddr(host)
}

// validateMacAddr checks a MAC address in aa:bb:cc:dd:ee:ff form.
func validateMacAddr(s string) error {
	if len(s) != 17 {
		return fmt.Errorf("invalid MAC address: %q (use aa:bb:cc:dd:ee:ff)", s)
	}
	for i := 0; i < 17; i++ {
		c := s[i]
		if i%3 == 2 {
			if c != ':' {
				return fmt.Errorf("invalid MAC address: %q (use aa:bb:cc:dd:ee:ff)", s)
			}
			continue
		}
		if !(c >= '0' && c <= '9' || c >= 'a' && c <= 'f' || c >= 'A' && c <= 'F') {
			return fmt.Errorf("invalid MAC address: %q (use aa:bb:cc:dd:ee:ff)", s)
		}
	}
	return nil
}

// validatePortNum checks a single-digit physical port number (1..maxPort).
func validatePortNum(s string, maxPort int) error {
	if len(s) != 1 || s[0] < '1' || s[0] > '9' {
		return fmt.Errorf("invalid port: %q (must be a single digit 1-%d)", s, maxPort)
	}
	if int(s[0]-'0') > maxPort {
		return fmt.Errorf("invalid port: %q (must be 1-%d)", s, maxPort)
	}
	return nil
}

// validatePortToken checks a physical port (1-9) or "10" for the CPU port.
// Used by commands that accept the 2-digit CPU port token (vlan, isolate,
// mirror, lag).
func validatePortToken(s string) error {
	if s == "10" {
		return nil
	}
	if len(s) != 1 || s[0] < '1' || s[0] > '9' {
		return fmt.Errorf("invalid port: %q (must be 1-9, or 10 for CPU)", s)
	}
	return nil
}

// validateSpeedWord checks a port speed keyword accepted by `port`/`sfp`.
func validateSpeedWord(s string) error {
	switch s {
	case "10m", "100m", "1g", "2g5", "5g", "10g", "auto", "on", "off":
		return nil
	}
	return fmt.Errorf("invalid speed: %q (use 10m, 100m, 1g, 2g5, 5g, 10g, auto, on, off)", s)
}

func validateDuplex(s string) error {
	if s != "half" && s != "full" {
		return fmt.Errorf("invalid duplex: %q (use half or full)", s)
	}
	return nil
}

// validateMTU checks an MTU value.  The firmware only enforces the 16383
// maximum; the 64 minimum matches its own TODO.
func validateMTU(s string) error {
	n, err := strconv.Atoi(s)
	if err != nil || n < 64 || n > 16383 {
		return fmt.Errorf("invalid MTU: %q (must be a number 64-16383)", s)
	}
	return nil
}

// validateSfpSlot checks an SFP slot number.  The firmware accepts only
// slots 1-2; whether a machine actually has the slot is checked on-device.
func validateSfpSlot(s string) error {
	if len(s) != 1 || s[0] < '1' || s[0] > '2' {
		return fmt.Errorf("invalid SFP slot: %q (must be 1 or 2)", s)
	}
	return nil
}

// validateHexArg checks a hex value of minBytes..maxBytes bytes.
func validateHexArg(s string, minBytes, maxBytes int) error {
	if !isHexStr(s) {
		return fmt.Errorf("invalid hex value: %q", s)
	}
	minChars, maxChars := minBytes*2-1, maxBytes*2
	if len(s) < minChars || len(s) > maxChars {
		return fmt.Errorf("invalid hex value: %q (must be %d-%d hex chars)", s, minChars, maxChars)
	}
	return nil
}

// validateIngressToken checks an ingress filter token: "t", "u" or "a" for
// all ports, or "<port><t|u|a>" for a single port.
func validateIngressToken(s string) error {
	if len(s) == 1 {
		if s[0] == 't' || s[0] == 'u' || s[0] == 'a' {
			return nil
		}
	} else if len(s) == 2 && s[0] >= '1' && s[0] <= '9' {
		if s[1] == 't' || s[1] == 'u' || s[1] == 'a' {
			return nil
		}
	}
	return fmt.Errorf("invalid ingress token: %q (use [<port>]t|u|a)", s)
}

// validateVlanPortToken checks a VLAN member token: port (1-9 or 10=CPU)
// with an optional t/u suffix.
func validateVlanPortToken(s string) error {
	body := strings.TrimSuffix(strings.TrimSuffix(s, "t"), "u")
	return validatePortToken(body)
}
