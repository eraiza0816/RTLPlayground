package main

import (
	"reflect"
	"testing"
)

func TestDeviceHelpTablesComplete(t *testing.T) {
	seen := map[string]bool{}
	for _, g := range deviceTopCmds {
		if seen[g.name] {
			t.Errorf("duplicate device command %q in deviceTopCmds", g.name)
		}
		seen[g.name] = true
		for _, c := range g.sub {
			if c.name == "" || c.desc == "" {
				t.Errorf("sub-command of %q has empty name/desc", g.name)
			}
		}
	}
	// Every validated write command must be present in the device help so
	// that '?' can document it.
	for name := range cmdValidators {
		if !seen[name] {
			t.Errorf("cmdValidators command %q missing from deviceTopCmds", name)
		}
	}
}

func TestDeviceCompletions(t *testing.T) {
	cases := []struct {
		words  []string
		prefix string
		want   []string
	}{
		{nil, "v", []string{"vlan", "version"}},
		{nil, "sfp", []string{"sfp"}},
		{nil, "zz", nil},
		{[]string{"sfp"}, "", []string{"1g", "2g5", "10g", "100m", "auto", "describe", "dump", "save", "restore", "fix", "patch", "clone", "checksum", "write", "bulk"}},
		{[]string{"sfp"}, "de", []string{"describe"}},
		{[]string{"port"}, "1", []string{"10m", "100m", "1g", "10g"}},
		{[]string{"port"}, "du", []string{"duplex"}},
		{[]string{"vlan"}, "sh", []string{"show"}},
		{[]string{"laghash"}, "sp", []string{"spa", "sport"}},
		{[]string{"nonexistent"}, "x", nil},
	}
	for _, c := range cases {
		got := deviceCompletions(c.words, c.prefix)
		if !reflect.DeepEqual(got, c.want) {
			t.Errorf("deviceCompletions(%v, %q) = %v, want %v", c.words, c.prefix, got, c.want)
		}
	}
}

func TestCommonCompletionPrefix(t *testing.T) {
	cases := []struct {
		in   []string
		want string
	}{
		{[]string{"port", "pvid", "passwd"}, "p"},
		{[]string{"port", "port"}, "port"},
		{[]string{"vlan", "version"}, "v"},
		{[]string{"abc"}, "abc"},
		{nil, ""},
	}
	for _, c := range cases {
		if got := commonCompletionPrefix(c.in); got != c.want {
			t.Errorf("commonCompletionPrefix(%v) = %q, want %q", c.in, got, c.want)
		}
	}
}

func TestAristaCompletions(t *testing.T) {
	cases := []struct {
		words  []string
		prefix string
		want   []string
	}{
		{nil, "sh", []string{"show"}},
		{nil, "wr", []string{"write"}},
		{[]string{"show"}, "in", []string{"interfaces", "inventory"}},
		{[]string{"show"}, "r", []string{"running-config"}},
		{[]string{"show", "interfaces"}, "st", nil},
	}
	for _, c := range cases {
		got := aristaCompletions(c.words, c.prefix, nil)
		if !reflect.DeepEqual(got, c.want) {
			t.Errorf("aristaCompletions(%v, %q) = %v, want %v", c.words, c.prefix, got, c.want)
		}
	}
}
