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
