package main

import (
	"crypto/rand"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"testing"
)

// TestEncAPI verifies the encrypted /enc API path: the request must be
// encrypted with the PSK ("api <path>" plaintext) and the encrypted response
// must be decrypted and parsed.  Mirrors firmware v0.2.23+ behavior.
func TestEncAPI(t *testing.T) {
	key := make([]byte, aeadKeyLen)
	for i := range key {
		key[i] = byte(i)
	}

	var gotCmd string
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/enc" {
			http.NotFound(w, r)
			return
		}
		body, err := io.ReadAll(r.Body)
		if err != nil {
			t.Fatalf("read body: %v", err)
		}
		if len(body) < aeadNonceLen+aeadTagLen {
			t.Fatalf("short body: %d", len(body))
		}
		pt, err := aeadDecrypt(key, body[:aeadNonceLen],
			body[aeadNonceLen:len(body)-aeadTagLen], body[len(body)-aeadTagLen:])
		if err != nil {
			w.WriteHeader(http.StatusUnauthorized)
			return
		}
		gotCmd = string(pt)

		resp := []byte(`[{"portNum":1,"logPort":4,"name":"port1","isSFP":0,"enabled":1,"link":2,"txG":"0x10","txB":"0","rxG":"0x20","rxB":"0"}]`)
		nonce := make([]byte, aeadNonceLen)
		if _, err := rand.Read(nonce); err != nil {
			t.Fatal(err)
		}
		pkt, err := aeadEncrypt(key, nonce, resp)
		if err != nil {
			t.Fatal(err)
		}
		w.Write(pkt)
	}))
	defer server.Close()

	e := &Exporter{target: server.URL, client: server.Client(), psk: key}

	ports, err := fetchJSON[[]PortStatus](e, "/status.json")
	if err != nil {
		t.Fatalf("fetchJSON: %v", err)
	}
	if gotCmd != "api /status.json" {
		t.Errorf("decrypted command = %q, want %q", gotCmd, "api /status.json")
	}
	if len(*ports) != 1 || (*ports)[0].PortNum != 1 {
		t.Errorf("unexpected ports: %+v", ports)
	}

	// Wrong key must be rejected (401 on the wire).
	e.psk = make([]byte, aeadKeyLen)
	if _, err := fetchJSON[[]PortStatus](e, "/status.json"); err == nil {
		t.Error("expected error with wrong key")
	}
}

// TestEncAPIRoundTrip checks that /enc responses larger than one block
// (multi-block ChaCha20 stream) survive the round trip.
func TestEncAPIRoundTrip(t *testing.T) {
	key := make([]byte, aeadKeyLen)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		body, _ := io.ReadAll(r.Body)
		pt, err := aeadDecrypt(key, body[:aeadNonceLen],
			body[aeadNonceLen:len(body)-aeadTagLen], body[len(body)-aeadTagLen:])
		if err != nil {
			w.WriteHeader(http.StatusUnauthorized)
			return
		}
		if string(pt) != "api /counters.json?port=1" {
			t.Errorf("cmd = %q", pt)
		}
		resp := []byte(`{"counters":["0x0000000000000001","0x0000000000000002","0x0000000000000003","0x0000000000000004","0x0000000000000005","0x0000000000000006","0x0000000000000007","0x0000000000000008"]}`)
		nonce := make([]byte, aeadNonceLen)
		rand.Read(nonce)
		pkt, _ := aeadEncrypt(key, nonce, resp)
		w.Write(pkt)
	}))
	defer server.Close()

	e := &Exporter{target: server.URL, client: server.Client(), psk: key}
	body, err := e.encAPI("/counters.json?port=1")
	if err != nil {
		t.Fatalf("encAPI: %v", err)
	}
	var m map[string][]string
	if err := json.Unmarshal(body, &m); err != nil {
		t.Fatalf("json: %v", err)
	}
	if len(m["counters"]) != 8 {
		t.Errorf("unexpected counters: %v", m)
	}
}

func TestLinkSpeedToBPS(t *testing.T) {
	tests := []struct {
		code int
		want uint64
	}{
		{0, 0},   // down
		{1, 10e6},  // 10M
		{2, 100e6}, // 100M
		{3, 1e9},   // 1G
		{4, 0},     // undefined in the firmware
		{5, 10e9},  // 10G
		{6, 2.5e9}, // 2.5G
		{7, 5e9},   // 5G
		{99, 0},
	}
	for _, tt := range tests {
		if got := linkSpeedToBPS(tt.code); got != tt.want {
			t.Errorf("linkSpeedToBPS(%d) = %d, want %d", tt.code, got, tt.want)
		}
	}
}
