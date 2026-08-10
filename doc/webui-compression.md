# WebUI asset compression (minify + gzip)

Type: how-to · Task: reduce the flash footprint and transfer size of the
Web UI assets

The static web assets (HTML, JS) are the largest single data block in the
firmware image. Two compression steps shrink them before they are embedded
in the flash:

1. **minify** — strips comments and indentation from the sources
2. **gzip** — compresses the minified files; the 8051 serves the compressed
   bytes and the browser decompresses them, so the CPU is not involved in
   decompression at all

## Pipeline

```text
html/*.html, *.js            raw sources (development)
        │  make
        ▼
output/html_min/             minified copy (build artifact, not committed)
        │  fileadder -z
        ▼
html_data.c / html_data.h    f_data table with gzip flag
html_data.bin (flash)        gzip-compressed bytes
        │  httpd
        ▼
HTTP response with "Content-Encoding: gzip"
```

## Step 1: minify

`tools/minify.py` removes comments and line-leading whitespace without
touching string literals, so the output is functionally identical to the
input. It handles HTML, JS and CSS (CSS is passed through unchanged — it is
already whitespace-minimal).

The Makefile rule `html_min` creates `output/html_min/` from `html/`:

```make
HTML_MIN := output/html_min
.PHONY: html_min
html_min: $(HTML)
	rm -rf $(HTML_MIN)
	mkdir -p $(HTML_MIN)
	@for f in $(HTML); do python3 tools/minify.py $$f $(HTML_MIN)/$$(basename $$f) || exit 1; done
```

`output/html_min/` is a pure build artifact: it is ignored by git and
removed by `make clean`.

## Step 2: gzip

`tools/fileadder -z` gzip-compresses each file before placing it in the
flash image. The compressed size is what the f_data table records and what
the httpd serves.

The generated `html_data.c` entry for a compressed file looks like:

```c
{"/main.js", FDATA_START_main_js, FDATA_SIZE_main_js, mime_JS, 1},
```

The trailing `1` is the gzip flag in `struct f_data` (generated in
`html_data.h`):

```c
struct f_data {
  __code char *file;
  uint32_t start;
  uint16_t len;
  mime_type_t mime;
  uint8_t gzip;
};
```

The Makefile passes `-z` on both fileadder invocations (header generation
and .bin embedding), so the header and the image stay consistent.

## Step 3: serving

`httpd/httpd.c` adds `Content-Encoding: gzip` to the response when the
entry's gzip flag is set:

```c
if (f_data[entry].gzip)
    slen += strtox(outbuf + slen, "Content-Encoding: gzip\r\n");
```

Decompression happens in the browser — the 8051 only copies the compressed
bytes from flash to the TCP stream. The JSON API endpoints never carry the
flag, so API responses are always uncompressed.

## Client compatibility

| Client | Result |
|--------|--------|
| Browser (Web UI) | native `Content-Encoding: gzip` support |
| rtlpctl (Go) | unaffected — API endpoints only; Go's net/http transparently decompresses gzip responses |
| rtlplayground_exporter (Go) | unaffected — same reasoning as above |
| httpd_sim | serves the raw files from disk for development; no gzip involved |
| curl | use `curl --compressed` to request decompressed output |

## Measuring the effect

Rebuild and inspect the generated header:

```bash
make -j16 MACHINE=PCB_K0402WS_V3
grep FDATA_SIZE_ html_data.h
```

Measured on the current Web UI (v0.2.25):

| File | raw | minified | gzip | ratio (gzip/raw) |
|------|-----|----------|------|------|
| main.js | 73,136 | 62,585 | 16,325 | 22.3 % |
| index.html | 31,932 | 29,582 | 7,229 | 22.6 % |
| i18n.js | 13,505 | 12,062 | 3,744 | 27.7 % |
| login.html | 7,321 | 6,096 | 2,352 | 32.1 % |
| chacha20poly1305.js | 5,177 | 4,080 | 1,420 | 27.4 % |
| **Total** | **131,071** | **114,405** | **31,070** | **23.7 %** |

Total flash footprint of the assets: the data block ends at 0x4795e
(≈ 293 KB) instead of 0x57ec5 with the minified-only build, freeing ≈ 66 KB
of flash.

Verify the embedded data round-trips correctly:

```bash
python3 - <<'PYEOF'
import gzip, re
data = open('output/rtlplayground.bin', 'rb').read()
h = open('html_data.h').read()
def val(m): return int(re.search(rf'#define {m} (\w+)', h).group(1), 0)
for name, key in [('main.js', 'main_js'), ('index.html', 'index_html'),
                  ('i18n.js', 'i18n_js'), ('login.html', 'login_html'),
                  ('chacha20poly1305.js', 'chacha20poly1305_js')]:
    start, size = val(f'FDATA_START_{key}'), val(f'FDATA_SIZE_{key}')
    assert gzip.decompress(data[start:start + size]) == \
        open(f'output/html_min/{name}', 'rb').read()
print('OK: all embedded files decompress to the minified sources')
PYEOF
```

## Development notes

- The raw sources in `html/` are never modified; minification happens only
  in the build.
- `tools/webuitest/run.sh` runs the end-to-end Web UI test against
  `httpd_sim`, which serves the raw sources — the test covers the UI
  logic, not the compression path.
- The gzip implementation in `fileadder` uses zlib (`-lz`) with gzip format
  (`deflateInit2(..., 15 + 16, ...)`), `Z_BEST_COMPRESSION`. It runs on the
  build host, not on the switch.
