package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"net/http/cookiejar"
	"net/url"
	"os"
	"path/filepath"
	"strings"
)

type Client struct {
	baseURL  string
	password string
	http     *http.Client
}

func NewClient(host, password string) *Client {
	jar, _ := cookiejar.New(nil)
	return &Client{
		baseURL:  fmt.Sprintf("http://%s", host),
		password: password,
		http: &http.Client{
			Jar: jar,
			CheckRedirect: func(req *http.Request, via []*http.Request) error {
				if len(via) >= 10 {
					return fmt.Errorf("too many redirects")
				}
				return nil
			},
		},
	}
}

func (c *Client) Login() error {
	req, err := http.NewRequest("POST", c.baseURL+"/login", strings.NewReader("pwd="+url.QueryEscape(c.password)))
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")

	noRedirect := func(req *http.Request, via []*http.Request) error {
		return http.ErrUseLastResponse
	}
	c.http.CheckRedirect = noRedirect
	resp, err := c.http.Do(req)
	c.http.CheckRedirect = func(req *http.Request, via []*http.Request) error {
		if len(via) >= 10 {
			return fmt.Errorf("too many redirects")
		}
		return nil
	}
	if err != nil {
		return fmt.Errorf("login request failed: %w", err)
	}
	defer resp.Body.Close()

	// Manually store cookies since ErrUseLastResponse skips automatic jar update
	if c.http.Jar != nil && resp.StatusCode == http.StatusFound {
		c.http.Jar.SetCookies(resp.Request.URL, resp.Cookies())
	}

	for _, cc := range resp.Cookies() {
		if cc.Name == "session" {
			return nil
		}
	}
	if resp.StatusCode == http.StatusOK || resp.StatusCode == http.StatusFound {
		body, _ := io.ReadAll(resp.Body)
		return fmt.Errorf("login failed (status %d): %s", resp.StatusCode, strings.TrimSpace(string(body)))
	}
	body, _ := io.ReadAll(resp.Body)
	return fmt.Errorf("login failed (status %d): %s", resp.StatusCode, strings.TrimSpace(string(body)))
}

func (c *Client) get(path string) (*http.Response, error) {
	req, err := http.NewRequest("GET", c.baseURL+path, nil)
	if err != nil {
		return nil, err
	}
	return c.http.Do(req)
}

func (c *Client) GetJSON(path string) (interface{}, error) {
	resp, err := c.get(path)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusUnauthorized {
		return nil, fmt.Errorf("unauthorized - please login first")
	}
	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return nil, fmt.Errorf("request failed (status %d): %s", resp.StatusCode, strings.TrimSpace(string(body)))
	}
	return decodeJSON(resp.Body)
}

func (c *Client) GetText(path string) (string, error) {
	resp, err := c.get(path)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusUnauthorized {
		return "", fmt.Errorf("unauthorized - please login first")
	}
	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return "", fmt.Errorf("request failed (status %d): %s", resp.StatusCode, strings.TrimSpace(string(body)))
	}
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", err
	}
	return string(body), nil
}

func (c *Client) PostForm(path string, data url.Values) (string, error) {
	req, err := http.NewRequest("POST", c.baseURL+path, strings.NewReader(data.Encode()))
	if err != nil {
		return "", err
	}
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	resp, err := c.http.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	statusOK := resp.StatusCode == http.StatusOK || resp.StatusCode == http.StatusFound
	if !statusOK {
		return "", fmt.Errorf("request failed (status %d): %s", resp.StatusCode, strings.TrimSpace(string(body)))
	}
	return string(body), nil
}

func (c *Client) PostRaw(path, contentType string, body io.Reader) error {
	req, err := http.NewRequest("POST", c.baseURL+path, body)
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", contentType)
	resp, err := c.http.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusOK {
		return nil
	}
	b, _ := io.ReadAll(resp.Body)
	return fmt.Errorf("request failed (status %d): %s", resp.StatusCode, strings.TrimSpace(string(b)))
}

func (c *Client) UploadFile(path, fieldName, filePath string) error {
	f, err := os.Open(filePath)
	if err != nil {
		return fmt.Errorf("cannot open file: %w", err)
	}
	defer f.Close()

	var buf bytes.Buffer
	w := multipart.NewWriter(&buf)
	fw, err := w.CreateFormFile(fieldName, filepath.Base(filePath))
	if err != nil {
		return err
	}
	if _, err := io.Copy(fw, f); err != nil {
		return err
	}
	w.Close()

	req, err := http.NewRequest("POST", c.baseURL+path, &buf)
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", w.FormDataContentType())
	resp, err := c.http.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusOK || resp.StatusCode == http.StatusSwitchingProtocols {
		return nil
	}
	b, _ := io.ReadAll(resp.Body)
	return fmt.Errorf("upload failed (status %d): %s", resp.StatusCode, strings.TrimSpace(string(b)))
}

func decodeJSON(r io.Reader) (interface{}, error) {
	d := json.NewDecoder(r)
	d.UseNumber()
	var v interface{}
	if err := d.Decode(&v); err != nil {
		return nil, err
	}
	return v, nil
}
