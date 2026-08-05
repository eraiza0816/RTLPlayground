package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"testing"
	"time"

	"golang.org/x/sys/unix"
	"golang.org/x/term"
)

// openPty creates a pseudo-terminal pair (/dev/ptmx + /dev/pts/N).
func openPty(t *testing.T) (master, slave *os.File) {
	t.Helper()
	master, err := os.OpenFile("/dev/ptmx", os.O_RDWR, 0)
	if err != nil {
		t.Skipf("open /dev/ptmx: %v", err)
	}
	ptn, err := unix.IoctlGetInt(int(master.Fd()), unix.TIOCGPTN)
	if err != nil {
		master.Close()
		t.Fatalf("TIOCGPTN: %v", err)
	}
	if err := unix.IoctlSetPointerInt(int(master.Fd()), unix.TIOCSPTLCK, 0); err != nil {
		master.Close()
		t.Fatalf("TIOCSPTLCK: %v", err)
	}
	slave, err = os.OpenFile(fmt.Sprintf("/dev/pts/%d", ptn), os.O_RDWR, 0)
	if err != nil {
		master.Close()
		t.Fatalf("open slave: %v", err)
	}
	return master, slave
}

// TestTTYOutputONLCR guards the raw-mode output fix: term.MakeRaw disables
// the tty's LF->CRLF translation (OPOST/ONLCR), which makes interactive
// output drift one column to the right per line. interactiveTTY re-enables
// OPOST|ONLCR after MakeRaw; this test verifies the same termios dance.
func TestTTYOutputONLCR(t *testing.T) {
	master, slave := openPty(t)
	defer master.Close()
	defer slave.Close()

	oldState, err := term.MakeRaw(int(slave.Fd()))
	if err != nil {
		t.Fatalf("MakeRaw: %v", err)
	}
	defer term.Restore(int(slave.Fd()), oldState)

	// Same Oflag restoration as interactiveTTY.
	tio, err := unix.IoctlGetTermios(int(slave.Fd()), unix.TCGETS)
	if err != nil {
		t.Fatalf("TCGETS: %v", err)
	}
	tio.Oflag &^= unix.OCRNL | unix.ONLRET
	tio.Oflag |= unix.OPOST | unix.ONLCR
	if err := unix.IoctlSetTermios(int(slave.Fd()), unix.TCSETS, tio); err != nil {
		t.Fatalf("TCSETS: %v", err)
	}

	// Verify a bare '\n' comes back as '\r\n' (cursor returns to column 0).
	if _, err := slave.Write([]byte("a\nb")); err != nil {
		t.Fatalf("write: %v", err)
	}
	buf := make([]byte, 8)
	n, err := master.Read(buf)
	if err != nil {
		t.Fatalf("read: %v", err)
	}
	if got := string(buf[:n]); got != "a\r\nb" {
		t.Errorf("expected \"a\\r\\nb\", got %q", got)
	}

	// Verify a bare '\r' (used by the line-editor redraw) is NOT translated.
	if _, err := slave.Write([]byte("\r\x1b[K")); err != nil {
		t.Fatalf("write: %v", err)
	}
	n, err = master.Read(buf)
	if err != nil {
		t.Fatalf("read: %v", err)
	}
	if got := string(buf[:n]); got != "\r\x1b[K" {
		t.Errorf("expected \"\\r\\x1b[K\", got %q", got)
	}
}

// TestTTYHistoryNavigation runs the real rtlpctl binary on a pty and checks
// that Up recalls the previous command and Enter re-executes it.
func TestTTYHistoryNavigation(t *testing.T) {
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

	master, slave := openPty(t)
	defer master.Close()
	defer slave.Close()

	cmd := exec.Command(bin)
	cmd.Stdin = slave
	cmd.Stdout = slave
	cmd.Stderr = slave
	cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true, Setctty: true, Ctty: 0}
	if err := cmd.Start(); err != nil {
		t.Fatalf("start: %v", err)
	}
	defer cmd.Process.Kill()

	// One background reader pumps the (blocking) pty master into a buffer;
	// drain() snapshots it after a sleep.
	var mu sync.Mutex
	var sb strings.Builder
	go func() {
		one := make([]byte, 4096)
		for {
			n, err := master.Read(one)
			if err != nil {
				return
			}
			mu.Lock()
			sb.Write(one[:n])
			mu.Unlock()
		}
	}()
	drain := func(d time.Duration) string {
		time.Sleep(d)
		mu.Lock()
		s := sb.String()
		mu.Unlock()
		return s
	}

	// Banner + first "help" execution.
	if got := drain(500 * time.Millisecond); !strings.Contains(got, "rtlp> ") {
		t.Fatalf("no prompt: %q", got)
	}
	master.Write([]byte("help\r"))
	first := drain(800 * time.Millisecond)
	if c := strings.Count(first, "Commands:"); c != 1 {
		t.Fatalf("expected 1 help output, got %d: %q", c, first)
	}

	// Up recalls "help", Enter re-executes it.
	master.Write([]byte("\x1b[A"))
	recalled := drain(400 * time.Millisecond)
	if !strings.Contains(recalled, "rtlp> help") {
		t.Errorf("Up did not recall the command: %q", recalled)
	}
	master.Write([]byte("\r"))
	second := drain(800 * time.Millisecond)
	// The snapshot accumulates everything: banner + first help + recalled help.
	if c := strings.Count(second, "Commands:"); c != 2 {
		t.Errorf("expected the recalled help to be re-executed (2 help outputs total), got %d: %q", c, second)
	}
}
