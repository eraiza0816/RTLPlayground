
#include "httpd.h"
#include "page_impl.h"
#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "rtl837x_leds.h"
#include "cmd_parser.h"
#include "rtl837x_flash.h"
#include "uip.h"
#include "crypto/aead.h"
#ifndef NO_WEBUI
#include "html_data.h"
#endif

// #define DEBUG
#include "debug.h"

#define SESSION_ID_LENGTH 24
#define SESSION_TIMEOUT 200

#define CMARK_S 6

#pragma codeseg BANK1
#pragma constseg BANK1

extern volatile __xdata uint8_t sfr_data[4];
extern __code uint8_t * __code hex;
#ifndef NO_WEBUI
extern __code struct f_data f_data[];
extern __code char * __code mime_strings[];
#endif
extern __xdata struct flash_region_t flash_region;
extern __xdata uint8_t cli_mode;
extern __xdata uint32_t flash_size;

// Flash buffer to optimize flash writing speed, write_len is the current filling position
extern __xdata uint8_t flash_buf[FLASH_BUF_SIZE];
__xdata uint32_t uptr; // Current flash write position
__xdata uint16_t write_len;

__xdata uint8_t outbuf[TCP_OUTBUF_SIZE];
__xdata uint8_t entry;
__xdata uint16_t slen;
__xdata uint16_t o_idx;
__xdata uint8_t pending_reset;
__xdata uint16_t len_left;
__xdata uint16_t cont_len;
__xdata uint32_t cont_addr;

// HTTP header properties
__xdata uint8_t boundary[72];
__xdata uint8_t * __xdata content_type = 0;
__xdata uint8_t * __xdata session = 0;

// Global variables holding POST state
__xdata uint16_t bindex; // Current index into the boundary
__xdata uint8_t verify_crc;
__xdata uint32_t max_upload;
__xdata uint16_t short_parsed;

// Shared management password. Declared in cmd_parser.h; modified by the CLI
// `passwd` command (cmd_parser.c), read by WebUI (here) and telnet (telnetd.c).
__xdata char passwd[21];
__xdata char session_id[SESSION_ID_LENGTH + 1];
__xdata uint8_t authenticated;
__xdata uint8_t preshared_key[AEAD_KEY_LEN];
__xdata uint32_t now;
__xdata uint8_t * __xdata timeptr;
__xdata uint32_t last_session_use;
/* Login rate limiting: 5 failed attempts lock /login for 30 s. */
__xdata uint8_t login_failures;
__xdata uint32_t login_now;
__xdata uint32_t login_locked_until;
/* /enc nonce replay guard (last accepted nonce). */
__xdata uint8_t enc_last_set;
__xdata uint8_t enc_last_nonce[AEAD_NONCE_LEN];
/* Content-Length of the current POST request (0 = header absent). Used to
 * detect bodies that arrive split across TCP segments: uIP delivers one
 * segment per appcall, so a split body would otherwise silently execute
 * only the first part. */
__xdata uint16_t content_length;

#define TSTATE_NONE		0
#define TSTATE_TX		1
#define TSTATE_ACKED 		2
#define TSTATE_CLOSED 		3
#define TSTATE_POST 		4
#define TSTATE_MULTIPART	5
#define TSTATE_FLASH_DONE	6

extern __xdata uint16_t crc_value;
extern __xdata uint8_t ledEnabled;
#include "machine.h"
extern __code const struct machine machine;
__xdata uint16_t crc_final;
void crc16(__xdata uint8_t *v) __banked;


inline uint8_t is_separator(uint8_t c)
{
	return c == ' ' || c == '\t' || c == '?' || c == '=';
}


void httpd_init(void) __banked
{
	__xdata struct httpd_state * __xdata s = &(uip_conn->appstate);
	// Start listening to port 80
	uip_listen(HTONS(80));
	s->tstate = TSTATE_CLOSED;
}


#ifndef NO_WEBUI
uint8_t find_entry(__xdata uint8_t *e)
{
	uint8_t i, j;

	for (i = 0; f_data[i].len; i++) {
		j = 0;
		while (f_data[i].file[j] && (f_data[i].file[j] == e[j])) {
			j++;
		}
		if ((!f_data[i].file[j]) && (!e[j])) {
			return i;
		}
	}
	return 0xff;
}
#else
uint8_t find_entry(__xdata uint8_t *e)
{
	(void)e;
	return 0xff;
}
#endif


bool is_word(__xdata uint8_t *xdata_str_p, __code uint8_t * __xdata code_str_p)
{
	uint8_t u, c;

	while (1) {
		u = *xdata_str_p++;
		c = *code_str_p++;

		if (c == '\0') {
			if (u != '\0' && u != ' ' && u != '\t' && u != ':' && u != '?' && u != '=' && u != '\n' && u != '\r' && u != ';')
				return false;
			return true;
		}

		if (c != u) {
			return false;
		}
	}
}


bool is_url_word_x(__xdata uint8_t *uri_str_p, __xdata uint8_t *src_str_p)
{
	uint8_t u, s;

	while(1) {
		u = *uri_str_p++;
		s = *src_str_p++;

		if (s == '\0') {
			if (u != '\0' && u != ' ' && u != '\t' && u != ':' && u != '?' && u != '=' && u != '\n' && u != '\r' && u != ';')
				return false;
			return true;
		}

		if (u == '%') {
			bool again = true;
			u = 0;

			while(1) {
				// Swap instruction is fine for rotation
				u = (u << 4) | (u >> 4);

				uint8_t p = *uri_str_p++;
				u |= p - '0' < 10 ? (p - '0') : (p - 'A' + 10);

				// force `jbc`-instruction.
				if (again) {
					again = false;
				} else {
					break;
				}
			}
		} else if (u == '+') {
			u = ' ';
		}

		if (s != u) {
			return false;
		}
	}
}


bool is_word_x(__xdata uint8_t *lhs_str_p, __xdata uint8_t *rhs_str_p)
{
	uint8_t u, c;

	while (1) {
		u = *lhs_str_p++;
		c = *rhs_str_p++;

		if (c == '\0') {
			if (u != '\0' && u != ' ' && u != '\t' && u != ':' && u != '?' && u != '=' && u != '\n' && u != '\r' && u != ';')
				return false;
			return true;
		}

		if (c != u) {
			return false;
		}
	}
}


uint8_t parse_short(__xdata uint8_t *p);


void send_not_found(void)
{
	slen = strtox(outbuf, "HTTP/1.1 404 Not found\r\nContent-Type: text/html\r\n\r\n" \
			      "<!DOCTYPE HTML PUBLIC>\n<title>404 Not Found</title>\n<h1>Not Found</h1>\n");
}


void send_bad_request(void)
{
	slen = strtox(outbuf, "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\n" \
			      "<!DOCTYPE HTML PUBLIC>\n<title>400 Bad Request</title>\n<h1>Bad Request</h1>\n");
}


void send_to_login(void)
{
	slen = strtox(outbuf, "HTTP/1.1 302 Found\r\n" \
			      "Location: login.html\r\n\r\n");
}


void send_unauthorized(void)
{
	slen = strtox(outbuf, "HTTP/1.1 401 Unauthorized\r\n\r\n");
}


__xdata uint8_t *skip_boundary(__xdata uint8_t *p)
{
	while (*p) {
		if (is_word_x(p, boundary + 2))
			return p + strlen_x(boundary + 2);
		p++;
	}
	return p;
}


__xdata uint8_t *scan_header(__xdata uint8_t * __xdata p)
{
	content_type = 0;
	session = 0;
	authenticated = 0;
	content_length = 0;

	while (*p != '\r' || *(p + 1) != '\n' || *(p + 2) != '\r' || *(p + 3) != '\n') {
		dbg_char(*p);
		if (!*p++)
			break;
		if (is_word(p, "\nContent-Type:"))
			content_type = p + 15;
		else if (is_word(p, "\nCookie:")) {
			/* The header may hold several cookies in any order; match
			 * the "session" key by name (is_word() rejects a longer
			 * key like "sessionx" because '=' terminates the match). */
			__xdata uint8_t *c = p + 8;	/* past "\nCookie:" */
			while (*c && *c != '\r' && *c != '\n') {
				if (is_word(c, "session")) {
					session = c + 8;	/* past "session=" */
					break;
				}
				c++;
			}
		}
		else if (is_word(p, "\nContent-Length:")) {
			/* Header format: "\nContent-Length: <digits>" — skip the
			 * separator space (may be absent, e.g. "Content-Length:12"). */
			__xdata uint8_t * __xdata cl = p + 16;
			if (*cl == ' ')
				cl++;
			/* Bounded: at most 4 digits are accumulated into the uint16
			 * accumulator.  A 5th digit or more (value >= 10000) would
			 * wrap (e.g. 65536 -> 0) and bypass the incomplete-body
			 * checks below, so any larger value is clamped to 0xffff
			 * (the /cmd, /enc and /login bodies never reach 10k). */
			__xdata uint8_t digits = 0;
			while (*cl >= '0' && *cl <= '9') {
				digits++;
				if (digits >= 5) {
					content_length = 0xffff;
					break;
				}
				content_length = content_length * 10 + (*cl++ - '0');
			}
		}
	}
	if (content_type && is_word(content_type, "multipart/form-data; boundary")) {
		dbg_string("\nFound multipart\n");
		content_type += 30;
		uint8_t i = 0;
		while (i < (sizeof(boundary) - 5) &&
				content_type[i] != '\r' && content_type[i] != '\n') {
			boundary[i + 4] = content_type[i];
			i++;
		}
		// The boundary between parts is "\r\n--" + the boundary given in the header
		boundary[0] = '\r';
		boundary[1] = '\n';
		boundary[2] = '-';
		boundary[3] = '-';
		boundary[i + 4] = 0;
	}

	read_tick_counter(&now);

	if (session) {
		if (now - last_session_use > SESSION_TIMEOUT) {
			dbg_string("Session expired\n");
		} else {
			if (is_word_x(session, session_id))
				authenticated = 1;
			else
				dbg_string("Invalid session cookie!\n");
		}
	}
	return p;
}


void gen_random_bytes(__xdata uint8_t *b, uint8_t bytes)
{
	__xdata uint8_t i = 0;
	while (bytes) {
		if (!i)
			get_random_32();
		b[--bytes] = itohex(sfr_data[i]);
		if (!bytes) { break; }
		b[--bytes] = itohex(sfr_data[i] >> 4 | sfr_data[i] << 4);
		i = (i + 1) & 0x3;
	}
}

/* Fill b with bytes random raw bytes (gen_random_bytes() writes hex
 * digits, which would halve the entropy of binary nonces). */
void gen_random_raw(__xdata uint8_t *b, uint8_t bytes)
{
	__xdata uint8_t i = 0;
	while (bytes) {
		if (!i)
			get_random_32();
		*b++ = sfr_data[i];
		i = (i + 1) & 0x3;
		bytes--;
	}
}


/*
 * Reads post data from the http stream and writes it into flash memory
 * Input: the current position in the TCP buffer (uip_appdata)
 * Returns 1: More data to read, 0: Upload complete, all parts reads
 */
uint8_t stream_upload(uint16_t bptr)
{
	__xdata uint8_t *p = uip_appdata;
	__xdata struct httpd_state * __xdata s = &(uip_conn->appstate);

	/* Guard the destination area: a bug in the size accounting above
	 * must not let the transfer erase and write past the config or the
	 * firmware upload area into the code region. */
	if (verify_crc) {
		if (uptr + FLASH_PAGE_SIZE >= FIRMWARE_UPLOAD_START + flash_size) {
			s->tstate = TSTATE_FLASH_DONE;
			send_bad_request();
			return 0;
		}
	} else if (uptr + FLASH_PAGE_SIZE >= CONFIG_START + CONFIG_LEN) {
		s->tstate = TSTATE_FLASH_DONE;
		send_bad_request();
		return 0;
	}

	dbg_string("Stream_upload called: ");
	dbg_short(bptr); dbg_char('\n');

	do {
		if (bptr >= uip_len) {
			s->tstate = TSTATE_POST;
			return 1;
		}
		// Have we reached the end of the part?
		if (!boundary[bindex]) {
			s->tstate = TSTATE_NONE;
			dbg_string("len 2: "); dbg_short(write_len); dbg_char(' ');
			flash_region.addr = uptr;
			flash_region.len = write_len;
			flash_write_bytes(flash_buf);
			uptr += write_len;
			write_len = 0;
			// TODO: This is a bit premature, what about a nice web-page saying the device will reset???
			if (verify_crc) {
				dbg_string("CRC16: "); dbg_short(crc_final); dbg_char('\n');
				if (crc_final == 0xb001) {
					print_string("Checksum OK.\nUpload to flash done, will reset!\n");
					/* A web page instead of plain text: the browser would
					 * otherwise sit on a response that never ends while
					 * the switch resets.  The meta refresh sends it back
					 * to the login page once the reboot is done. */
					slen = strtox(outbuf,
						"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n"
						"<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
						"<meta http-equiv=\"refresh\" content=\"5;url=/login.html\"></head>"
						"<body style=\"font-family:sans-serif;text-align:center;margin-top:20%\">"
						"<h2>Firmware upload complete</h2>"
						"<p>The switch is updating and will reboot. "
						"<a href=\"/login.html\">Continue to login</a></p>"
						"</body></html>\n");
					pending_reset = 1;
				} else {
					print_string("Checksum incorrect! Aborting.\n");
					if (!ledEnabled) {
						// Upload failed while the LEDs were temporarily
						// enabled for the update; restore the off state.
						leds_set_enabled(0);
						set_sys_led_state(SYS_LED_OFF);
					}
					slen = strtox(outbuf, "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nCRC mismatch\n");
				}
				s->tstate = TSTATE_FLASH_DONE;
			}
			// Make sure there is a 0 at the end of the uploaded data
			flash_buf[0] = 0;
			flash_region.addr = uptr;
			flash_region.len = 1;
			flash_write_bytes(flash_buf);
			if (bptr >= uip_len)
				return 0;
			if(!verify_crc)
				//ugly hack to signal connection finished after config upload.
				uip_close();
			return 1;
		}
		if (p[bptr] == boundary[bindex]) {
			if (!bindex)
				crc_final = crc_value;
			crc16(p + bptr);
			bptr++;
			bindex++;
		} else {
			if (bindex) {
				memcpy(flash_buf + write_len, boundary, bindex);
				write_len += bindex;
				bindex = 0;
				// After a false partial match, re-check the current byte
				// against boundary[0] instead of blindly appending it as data.
				// This prevents false matches that consume the actual boundary.
				if (p[bptr] == boundary[0]) {
					crc_final = crc_value;
					crc16(p + bptr);
					bptr++;
					bindex = 1;
					goto skip_data;
				}
			}
			crc16(p + bptr);
			flash_buf[write_len++] = p[bptr++];
			if (write_len >= FLASH_PAGE_SIZE) {
				dbg_string("len: "); dbg_short(write_len); dbg_char(' ');
				dbg_string("CRC16: "); dbg_short(crc_value); dbg_char('\n');
				if (uptr % FLASH_SECTOR_SIZE == 0) {
					flash_region.addr = uptr;
					flash_sector_erase();
				}
				flash_region.addr = uptr;
				flash_region.len = FLASH_PAGE_SIZE;
				flash_write_bytes(flash_buf);
				uptr += FLASH_PAGE_SIZE;
				write_len -= FLASH_PAGE_SIZE;

				// Copy the remaining byte for the next page to the beginning of the buffer.
				if (write_len > 0) {
					memcpy(flash_buf, flash_buf + FLASH_PAGE_SIZE, write_len);
				}
			}
			bindex = 0;
		}
		skip_data: ;
	} while(1);
}


/* Look up "?key=value" after the path in q (the path is NUL-terminated).
 * The query bytes follow the NUL; the scan is bounded because /enc
 * leaves binary ciphertext behind the query.  On success short_parsed
 * holds the value and 1 is returned; otherwise short_parsed is 0.
 * Locals live in XDATA (the 8051 internal RAM is full) and the key
 * is passed via a global for the same reason. */
__code uint8_t * __xdata api_query_key;

static uint8_t api_query_u16(__xdata uint8_t *q)
{
	__code uint8_t *key = api_query_key;
	__xdata uint8_t * __xdata p = q;
	__xdata uint8_t qn = 0;
	__xdata uint8_t i = 0;
	__xdata uint8_t * __xdata k = 0;
	short_parsed = 0;
	while (*p)
		p++;			/* skip the (NUL-terminated) path */
	if (p == q)
		return 0;
	p++;				/* skip the NUL (the former '?') */
	while (qn < 100 && *p) {
		k = p;
		while (qn < 100 && *p && *p != '=' && *p != '&') {
			p++;
			qn++;
		}
		if (*p == '=') {
			i = 0;
			while (key[i] && k[i] == key[i])
				i++;
			if (!key[i]) {
				parse_short(p + 1);
				return 1;
			}
		}
		while (qn < 100 && *p && *p != '&') {
			p++;
			qn++;
		}
		if (*p == '&') {
			p++;
			qn++;
		}
	}
	return 0;
}


/* Execute a JSON API request.  q is a NUL-terminated path (e.g. "/status.json").
 * On success the response (HTTP header + JSON body) is written to outbuf and
 * slen is updated.  Returns 1 on success, 0 if the path is unknown. */
static uint8_t handle_api_path(__xdata uint8_t *q)
{
	/* Normalize: the plaintext GET path is already cut at '?', but the
	 * /enc "api <path>?x=y" form passes the raw string followed by
	 * binary ciphertext.  Truncate at the query so the strcmp()s below
	 * cannot run into that data. */
	/* Locals live in XDATA: the 8051 internal RAM is full. */
	__xdata uint8_t * __xdata qq = q;
	while (*qq && *qq != '?')
		qq++;
	*qq = '\0';
	if (!strcmp(q, "/status.json")) {
		send_status();
		return 1;
	} else if (!strcmp(q, "/information.json")) {
		send_basic_info();
		return 1;
	} else if (!strcmp(q, "/vlan.json")) {
		api_query_key = "vid";
		api_query_u16(q);
		send_vlan(short_parsed);
		return 1;
	} else if (is_word(q, "/sfp_diag.json")) {
		send_sfp_diag();
		return 1;
	} else if (is_word(q, "/counters.json")) {
		api_query_key = "port";
		api_query_u16(q);
		if (short_parsed < 1 || short_parsed > 9) {
			send_bad_request();
			return 1;
		}
		send_counters((char)short_parsed);
		return 1;
	} else if (is_word(q, "/eee.json")) {
		send_eee();
		return 1;
	} else if (is_word(q, "/bandwidth.json")) {
		send_bandwidth();
		return 1;
	} else if (is_word(q, "/l2.json")) {
		api_query_key = "idx";
		api_query_u16(q);
		send_l2(short_parsed);
		return 1;
	} else if (is_word(q, "/l2_del.json")) {
		api_query_key = "idx";
		api_query_u16(q);
		l2_delete(short_parsed);
		return 1;
	} else if (is_word(q, "/mirror.json")) {
		send_mirror();
		return 1;
	} else if (is_word(q, "/mtu.json")) {
		send_mtu();
		return 1;
	} else if (is_word(q, "/lag.json")) {
		send_lag();
		return 1;
	} else if (is_word(q, "/sfp_eeprom.json")) {
		api_query_key = "slot";
		api_query_u16(q);
		if (short_parsed >= machine.n_sfp) {
			send_bad_request();
			return 1;
		}
		send_sfp_eeprom((uint8_t)short_parsed);
		return 1;
	} else if (is_word(q, "/vlanlist")) {
		send_vlanlist();
		return 1;
	} else if (is_word(q, "/config")) {
		send_config();
		return 1;
	} else if (is_word(q, "/running-config")) {
		send_running_config();
		return 1;
	} else if (!strcmp(q, "/reset")) {
		pending_reset = 1;
		slen = strtox(outbuf, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK\n");
		return 1;
	} else if (!strcmp(q, "/logout")) {
		session_id[0] = 0;	/* invalidate every session */
		slen = strtox(outbuf, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK\n");
		return 1;
	} else if (is_word(q, "/ping.json")) {
		send_ping();
		return 1;
	} else if (is_word(q, "/arp.json")) {
		send_arp();
		return 1;
	} else if (is_word(q, "/lldp.json")) {
		send_lldp();
		return 1;
	} else if (is_word(q, "/igmp.json")) {
		send_igmp();
		return 1;
	} else if (is_word(q, "/storm-control.json")) {
		send_storm();
		return 1;
	} else if (is_word(q, "/qos.json")) {
		send_qos();
		return 1;
	} else if (is_word(q, "/acl.json")) {
		send_acl();
		return 1;
	} else if (is_word(q, "/cmd_log")) {
		send_cmd_log();
		return 1;
	} else if (is_word(q, "/cmd_log_clear")) {
		clear_command_history();
		send_mtu(); /* dummy response */
		return 1;
	}
	return 0;
}


/* Encrypted login challenge: the client encrypts this fixed string with
 * the pre-shared key and posts hex(nonce[12] || ct || tag) as enc=...
 * A wrong key/tag or a replayed nonce fails the login. */
static __code uint8_t login_challenge[12] = "RTLP-LOGIN-1"; /* exactly 12 bytes */

static uint8_t login_hexval(uint8_t c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return 0xff;
}

/* Verify the encrypted login challenge pointed to by hexp (64 hex chars:
 * nonce[12] || ct[12] || tag[16]).  Returns 1 on success and records the
 * nonce so a replayed login is rejected. */
static uint8_t login_psk(__xdata uint8_t *hexp)
{
	static __xdata uint8_t nonce[AEAD_NONCE_LEN];
	static __xdata uint8_t enc[AEAD_NONCE_LEN + AEAD_TAG_LEN];
	static __xdata uint8_t pt[AEAD_NONCE_LEN];
	static __xdata uint8_t last_nonce[AEAD_NONCE_LEN];
	static __xdata uint8_t last_set;
	static __xdata uint8_t i;
	uint8_t hi, lo;

	for (i = 0; i < AEAD_NONCE_LEN; i++) {
		hi = login_hexval(hexp[i * 2]);
		lo = login_hexval(hexp[i * 2 + 1]);
		if (hi == 0xff || lo == 0xff)
			return 0;
		nonce[i] = (hi << 4) | lo;
	}
	if (last_set) {
		/* replay guard: the nonce must differ from the last accepted one */
		for (i = 0; i < AEAD_NONCE_LEN; i++)
			if (nonce[i] != last_nonce[i])
				break;
		if (i == AEAD_NONCE_LEN)
			return 0;
	}
	for (i = 0; i < AEAD_NONCE_LEN + AEAD_TAG_LEN; i++) {
		hi = login_hexval(hexp[AEAD_NONCE_LEN * 2 + i * 2]);
		lo = login_hexval(hexp[AEAD_NONCE_LEN * 2 + i * 2 + 1]);
		if (hi == 0xff || lo == 0xff)
			return 0;
		enc[i] = (hi << 4) | lo;
	}
	if (aead_decrypt(preshared_key, nonce, 0, 0, enc, AEAD_NONCE_LEN, pt,
			 enc + AEAD_NONCE_LEN))
		return 0; /* wrong key or tampered */
	for (i = 0; i < AEAD_NONCE_LEN; i++)
		if (pt[i] != login_challenge[i])
			return 0;
	for (i = 0; i < AEAD_NONCE_LEN; i++)
		last_nonce[i] = nonce[i];
	last_set = 1;
	return 1;
}

/* POST /enc: body is nonce[12] || ciphertext || tag[16].
 * The plaintext is command text, verified against the pre-shared key.
 * The response body is nonce[12] || ciphertext || tag[16] of a JSON string. */
void handle_enc(__xdata uint8_t *body)
{
	static __xdata uint16_t body_len;
	static __xdata uint16_t ct_len;
	static __xdata uint16_t i;
	static __xdata uint8_t psk_set;
	static __xdata uint8_t resp_nonce[AEAD_NONCE_LEN];
	static __xdata uint8_t resp_json[16];
	static __xdata uint8_t enc_scratch[TCP_OUTBUF_SIZE];
	static __code uint8_t resp_ok[] = "{\"result\":\"ok\"}";

	body_len = uip_len - (body - uip_appdata);
	psk_set = 0;

	for (i = 0; i < AEAD_KEY_LEN; i++)
		psk_set |= preshared_key[i];
	if (!psk_set) {
		send_unauthorized();
		return;
	}
	if (body_len < AEAD_NONCE_LEN + AEAD_TAG_LEN + 1) {
		send_bad_request();
		return;
	}
	ct_len = body_len - AEAD_NONCE_LEN - AEAD_TAG_LEN;
	if (ct_len > CMD_BUF_SIZE - 1) {
		send_bad_request();
		return;
	}
	if (aead_decrypt(preshared_key, body, 0, 0, body + AEAD_NONCE_LEN, ct_len,
			 body + AEAD_NONCE_LEN, body + AEAD_NONCE_LEN + ct_len)) {
		send_unauthorized();
		return;
	}
	/* Replay protection: a request whose nonce equals the last accepted
	 * one is a replayed frame and must not re-execute the command.
	 * Only advance the recorded nonce after a successful decrypt. */
	if (enc_last_set) {
		for (i = 0; i < AEAD_NONCE_LEN; i++)
			if (body[i] != enc_last_nonce[i])
				break;
		if (i == AEAD_NONCE_LEN) {
			send_unauthorized();
			return;
		}
	}
	for (i = 0; i < AEAD_NONCE_LEN; i++)
		enc_last_nonce[i] = body[i];
	enc_last_set = 1;
	body[AEAD_NONCE_LEN + ct_len] = '\0';
	if (is_word(body + AEAD_NONCE_LEN, "api")) {
		/* API mode: "api <path>" renders a JSON API response and returns it
		 * encrypted (nonce[12] || ct || tag[16]).  The renderer writes
		 * "HTTP/1.1 ... \r\n\r\n" plus the JSON body into outbuf; copy the
		 * body into a scratch buffer and assemble the encrypted response. */
		if (!handle_api_path(body + AEAD_NONCE_LEN + 4)) {
			send_not_found();
			return;
		}
		body_len = 0;
		while (body_len + 3 < slen && !(outbuf[body_len] == '\r' &&
		       outbuf[body_len + 1] == '\n' && outbuf[body_len + 2] == '\r' &&
		       outbuf[body_len + 3] == '\n'))
			body_len++;
		body_len += 4;
		ct_len = slen - body_len;
		/* The response is header + nonce[12] + ct + tag[16] in outbuf;
		 * bound ct so everything fits instead of overflowing XRAM. */
		slen = strtox(outbuf, "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n\r\n");
		if (!ct_len || ct_len > TCP_OUTBUF_SIZE - slen - AEAD_NONCE_LEN - AEAD_TAG_LEN) {
			send_bad_request();
			return;
		}
		for (i = 0; i < ct_len; i++)
			enc_scratch[i] = outbuf[body_len + i];
		gen_random_raw(resp_nonce, AEAD_NONCE_LEN);
		for (i = 0; i < AEAD_NONCE_LEN; i++)
			outbuf[slen++] = resp_nonce[i];
		aead_encrypt(preshared_key, resp_nonce, 0, 0,
			     enc_scratch, ct_len,
			     outbuf + slen, outbuf + slen + ct_len);
		slen += ct_len + AEAD_TAG_LEN;
		return;
	}
	if (is_word(body + AEAD_NONCE_LEN, "session")) {
		/* Issue a web session cookie so the browser can use /upload
		 * and /config (multipart POST) without the password login. */
		gen_random_bytes(session_id, SESSION_ID_LENGTH);
		session_id[SESSION_ID_LENGTH] = '\0';
		read_tick_counter(&last_session_use);
		slen = strtox(outbuf, "HTTP/1.1 200 OK\r\nSet-Cookie: session=");
		for (i = 0; i < SESSION_ID_LENGTH; i++)
			outbuf[slen++] = session_id[i];
		slen += strtox(outbuf + slen, "; Path=/; HttpOnly; SameSite=Strict\r\nContent-Type: application/octet-stream\r\n\r\n");
		gen_random_raw(resp_nonce, AEAD_NONCE_LEN);
		for (i = 0; i < AEAD_NONCE_LEN; i++)
			outbuf[slen++] = resp_nonce[i];
		memcpyc(resp_json, resp_ok, 16);
		aead_encrypt(preshared_key, resp_nonce, 0, 0, resp_json, 16,
			     outbuf + slen, outbuf + slen + 16);
		slen += AEAD_TAG_LEN * 2;
		return;
	}
	if (is_word(body + AEAD_NONCE_LEN, "commit")) {
		/* commit is a privileged-mode command; PSK auth grants it. */
		cli_mode = MODE_PRIVILEGED;
		execute_commands(body + AEAD_NONCE_LEN);
		cli_mode = MODE_EXEC;
	} else {
		cli_mode = MODE_CONFIG;
		execute_commands(body + AEAD_NONCE_LEN);
		cli_mode = MODE_EXEC;
	}
	if (err_status != ERR_OK) {
		send_bad_request();
		return;
	}
	slen = strtox(outbuf, "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n\r\n");
	gen_random_raw(resp_nonce, AEAD_NONCE_LEN);
	for (i = 0; i < AEAD_NONCE_LEN; i++)
		outbuf[slen++] = resp_nonce[i];
	memcpyc(resp_json, resp_ok, 16);
	aead_encrypt(preshared_key, resp_nonce, 0, 0, resp_json, 16,
		     outbuf + slen, outbuf + slen + 16);
	slen += AEAD_TAG_LEN * 2;
}


void handle_post(void)
{
	static __xdata uint8_t login_psk_set;
	static __xdata uint8_t login_i;
	__xdata struct httpd_state * __xdata s = &(uip_conn->appstate);
	__xdata uint8_t *p = uip_appdata;
	__xdata uint8_t *request_path = p + 5;

	// Was the multipart header sent in multiple packets?
	if (s->tstate != TSTATE_MULTIPART) {
		dbg_string("Is POST\n");
		p += 5;  // Skip post
		// Find end of request path
		while (*p && !is_separator(*p))
			p++;
		*p++ = '\0';

		// Find end of request header
		boundary[0] ='\0';
		p = scan_header(p);
		dbg_string("Boundary: >"); dbg_string_x(boundary); dbg_string("<\n");
		if (!*p || !content_type) {
			dbg_string("Bad Request!\n");
			send_not_found();
			return;
		}
		if (is_word(request_path, "/upload")) {
			if (!authenticated) {
				send_unauthorized();
				return;
			}
			if (flash_size < FIRMWARE_UPLOAD_START*2)
			{
				print_string("Flash too small for firmware upload!\n");
				send_bad_request();
				return;
			}
			print_string("Firmware upload started.");
			uptr = FIRMWARE_UPLOAD_START;
			verify_crc = 1;
			max_upload = 1024576;
		} else if (is_word(request_path, "/config")) {
			if (!authenticated) {
				send_unauthorized();
				return;
			}
			dbg_string("Configuration upload, erasing config mem!\n");
			uptr = CONFIG_START;
			verify_crc = 0;
			max_upload = 2048;
			flash_region.addr = CONFIG_START;
			flash_sector_erase();
		}
		// Check for other POST requests, which are not multipart, below
	} else {
		dbg_string("Multipart request\n");
	}

	if (is_word(request_path, "/cmd")) {
		if (!authenticated) {
			send_unauthorized();
			return;
		}
		/* scan_header() returns a pointer at the "\r\n\r\n" separator, so
		 * the body starts 4 bytes later. Reject requests whose body did
		 * not fully arrive in this segment (uIP delivers one segment per
		 * appcall; a split body would silently execute only its first
		 * part, which is dangerous for configuration commands). */
		if (content_length && content_length > (uip_len - (uint16_t)(p - uip_appdata) - 4)) {
			dbg_string("Incomplete POST body\n");
			send_bad_request();
			return;
		}
		cli_mode = MODE_CONFIG;
		execute_commands(p);
		cli_mode = MODE_EXEC;
		if (err_status != ERR_OK) {
			send_bad_request();
			return;
		}
	} else if (is_word(request_path, "/enc")) {
		if (content_length && content_length > (uip_len - (uint16_t)(p - uip_appdata) - 4)) {
			dbg_string("Incomplete POST body\n");
			send_bad_request();
			return;
		}
		handle_enc(p + 4);
		return;
	} else if (is_word(request_path, "/login")) {
		dbg_string("POST login\n");

		if (!content_type || !is_word(content_type, "application/x-www-form-urlencoded")) {
			dbg_string("Bad request!\n");
			send_bad_request();
			return;
		}

		/* Same incomplete-body guard as /cmd and /enc: uIP delivers one
		 * segment per appcall, and a client that sends the headers and
		 * the form body in separate segments would make the password
		 * comparison below read past the end of the received data. */
		if (content_length && content_length > (uip_len - (uint16_t)(p - uip_appdata) - 4)) {
			dbg_string("Incomplete POST body\n");
			send_bad_request();
			return;
		}

		/* Rate limit: 5 failed attempts lock the login for 30 s (global,
		 * not per-IP -- the switch has no per-IP accounting). */
		read_tick_counter(&login_now);
		if (login_failures >= 5) {
			if (login_now < login_locked_until) {
				dbg_string("Login rate-limited\n");
				slen = strtox(outbuf, "HTTP/1.1 302 Found\r\nLocation: login.html\r\n\r\n");
				return;
			}
			login_failures = 0;	/* lock expired: re-arm */
		}

		/* When a pre-shared key is configured, password authentication is
		 * disabled: the web UI and rtlpctl log in with the encrypted
		 * challenge (enc=<hex>) so the PSK itself never leaves the client. */
		login_psk_set = 0;
		for (login_i = 0; login_i < AEAD_KEY_LEN; login_i++)
			login_psk_set |= preshared_key[login_i];
		if (login_psk_set) {
			if (is_word(p + 4, "enc") && login_psk(p + 8)) {
				dbg_string("PSK login accepted\n");
				login_failures = 0;
				read_tick_counter(&last_session_use);
				gen_random_bytes(session_id, SESSION_ID_LENGTH);
				session_id[SESSION_ID_LENGTH] = '\0';
				slen = strtox(outbuf, "HTTP/1.1 302 Found\r\nLocation: index.html\r\n" \
						  "Set-Cookie: session=");
				for (register uint8_t si = 0; si < SESSION_ID_LENGTH; si++)
					outbuf[slen++] = session_id[si];
				slen += strtox(outbuf + slen, "; Path=/; HttpOnly; SameSite=Strict\r\n\r\n");
			} else {
				dbg_string("PSK mode: password rejected\n");
				slen = strtox(outbuf, "HTTP/1.1 302 Found\r\nLocation: login.html\r\n\r\n");
			}
			return;
		}

		p += 8; // Read also over "pwd="
		if (is_url_word_x(p, passwd)) {
			dbg_string("Password accepted!\n");
			login_failures = 0;
			read_tick_counter(&last_session_use);
			gen_random_bytes(session_id, SESSION_ID_LENGTH);
			session_id[SESSION_ID_LENGTH] = '\0';
			slen = strtox(outbuf, "HTTP/1.1 302 Found\r\nLocation: index.html\r\n" \
					      "Set-Cookie: session=");
			for (register uint8_t i = 0; i < SESSION_ID_LENGTH; i++)
				outbuf[slen++] = session_id[i];
			slen += strtox(outbuf + slen, "; Path=/; HttpOnly; SameSite=Strict\r\n\r\n");
		} else {
			dbg_string("Password invalid!\n");
			login_failures++;
			if (login_failures >= 5) {
				read_tick_counter(&login_now);
				login_locked_until = login_now + 30;
			}
			slen = strtox(outbuf, "HTTP/1.1 302 Found\r\nLocation: login.html\r\n\r\n");
		}
		return;
	} else if (s->tstate == TSTATE_MULTIPART || is_word(request_path, "/upload") || is_word(request_path, "/config")) {
		dbg_string("POST upload/config request\n");
		if (!authenticated) {
			send_unauthorized();
			return;
		}
		if (!boundary[0]) {
			dbg_string("Bad request, no boundary!\n");
			send_bad_request();
			return;
		}
		// We skip the intial parts as part of the header
		do {
			p = skip_boundary(p);
			if (!*p) {
				s->tstate = TSTATE_MULTIPART;
				return;
			}
			p = scan_header(p);
			if (!*p)
				goto bad_request;
		} while (!content_type || !is_word(content_type, "application/octet-stream"));
		dbg_string("Have content octets\n");
		p += 4; // Skip \r\n\r\n sequence at end of preamble of part

		flash_init(0); // Re-initialize flash for non-DIO operation, otherwise flashing fails
		if (!ledEnabled)
			leds_set_enabled(1); // Keep the status LED visible during the update
		set_sys_led_state(SYS_LED_FAST);

		crc_value = 0;
		bindex = 0;
		write_len = 0;
		stream_upload(p - uip_appdata);

		dbg_string("Done reading first fragment\n");
		return;

	} else {
		send_not_found();
		return;
	}
	slen = strtox(outbuf, "HTTP/1.1 200 OK\r\n\r\n");
	return;
bad_request:
	send_bad_request();
	return;
}


extern void telnetd_appcall(void) __banked;
extern __xdata uint8_t telnet_enabled;
extern __xdata uint8_t web_enabled;

void httpd_appcall(void) __banked
{
	if (uip_conn->lport == HTONS(23)) {
		if (telnet_enabled)
			telnetd_appcall();
		else
			uip_close();
		return;
	}
	if (!web_enabled) {
		uip_close();
		return;
	}
	__xdata struct httpd_state * __xdata s = &(uip_conn->appstate);

	dbg_char('P');
#ifdef DEBUG
	if (uip_newdata())
		write_char('N');
	print_byte(s->tstate);
	write_char(' ');
#endif
	if(uip_connected() && s->tstate == TSTATE_CLOSED) {
		dbg_string("Connected...\n");
		s->tstate = TSTATE_NONE;
	} else if (uip_closed()) {
		dbg_string("Connection closed\n");
		s->tstate = TSTATE_CLOSED;
	} else if (uip_aborted()) {
		dbg_string("Connection aborted\n");
		uip_close();
		s->tstate = TSTATE_CLOSED;
	} else if (uip_poll()) {
		uip_len = 0;
		if (s->tstate == TSTATE_ACKED) {
			if (pending_reset) {
				dbg_string("Upload OK, resetting\n");
				pending_reset = 0;
				reset_chip();
			}
			dbg_string("Closing because everything has been transmitted\n");
			uip_close();
			s->tstate = TSTATE_CLOSED;
		}
	} else if (uip_acked() && s->tstate == TSTATE_TX) {
		dbg_string("ACK\n");
		if (slen > uip_mss()) {
			slen -= uip_mss();
			o_idx += uip_mss();
		} else {
			slen = 0;
			o_idx += slen;
		}

		s->tstate = TSTATE_ACKED;

		if (slen > uip_mss()) {
			dbg_string("Sending A: "); dbg_short(slen); dbg_char('\n');
			uip_send(outbuf + o_idx, uip_mss());
			s->tstate = TSTATE_TX;
		} else if (slen > 0) {
			dbg_string("Sending B: "); dbg_short(slen); dbg_char('\n');
			uip_send(outbuf + o_idx, slen);
			s->tstate = TSTATE_TX;
	} else if (cont_len) {
			dbg_string("CONT cont_len: "); dbg_short(cont_len);
			slen = cont_len > uip_mss() ? uip_mss() : cont_len;

			/* The chunk is loaded at the start of outbuf; reset the
			 * offset so the ack/rexmit paths send the right region. */
			o_idx = 0;
			dbg_string("cont_addr: "); dbg_char('\n');
			flash_region.addr = cont_addr;
			flash_region.len = slen;
			flash_read_bulk(outbuf);
			uip_send(outbuf, slen);
			cont_len -= slen;
			cont_addr += slen;
			s->tstate = TSTATE_TX;
		}
	} else if (uip_newdata() && s->tstate == TSTATE_POST) {
		// Enforce the upload limit by subtracting what we have received.
		if (uip_len <= max_upload) {
			max_upload -= uip_len;
			stream_upload(0);
			if (s->tstate == TSTATE_FLASH_DONE)
				goto do_send;
			write_char('.');
		} else {
			send_bad_request();
			goto do_send;
		}
	} else if (uip_newdata() && s->tstate != TSTATE_TX) {
		cont_len = 0;
		dbg_char('<'); dbg_short(uip_len); dbg_char('\n');
		__xdata uint8_t *p = uip_appdata;
		// Mark end of request header with \0
		p[uip_len] = 0;
#ifdef DEBUG
		while (*p)
			dbg_char(*p++);
		dbg_char('\n');
#endif
		p = uip_appdata;
		if (is_word(p, "POST") || s->tstate == TSTATE_MULTIPART) {
			handle_post();
			// If this is an ongoing post stream, then wait for the next packet
			if (s->tstate == TSTATE_POST || s->tstate == TSTATE_MULTIPART) {
				uip_len = 0;
				return;
			}
			goto do_send;
		}

#ifdef DEBUG
		if (is_word(p, "GET"))
			dbg_string("GET request ");
#endif
		p += 4;
		scan_header(p);
		__xdata uint8_t *q = p;
		while (!is_separator(*p))
			p++;
		*p = '\0';
		dbg_string_x(q);
		dbg_char('\n');

		s->tstate = TSTATE_NONE;
		entry = find_entry(q);
		dbg_string("Entry is: "); dbg_byte(entry); dbg_char('\n');
		if (entry == 0xff) {
			if (!authenticated) {
				dbg_string("Not authorized!\n");
				send_unauthorized();
				goto do_send;
			}
			dbg_string("Not file entry\n");
			/* API access also refreshes the session timeout: a client
			 * that only talks to the JSON API (rtlpctl, exporter)
			 * would otherwise be cut off after 200 s. */
			reg_read_m(RTL837X_REG_SEC_COUNTER);
			timeptr = (uint8_t*)&last_session_use;
			timeptr[0] = sfr_data[3]; timeptr[1] = sfr_data[2]; timeptr[2] = sfr_data[1]; timeptr[3] = sfr_data[0];
			if (!handle_api_path(q)) {
				send_not_found();
			}
		}
#ifndef NO_WEBUI
		else {
			dbg_string("Have entry, authenticated: "); dbg_byte(authenticated); dbg_char('\n');
			// A web-page is actively accessed, we can reset session time-out
			reg_read_m(RTL837X_REG_SEC_COUNTER);
			timeptr = (uint8_t*)&last_session_use; // last_session_use is Little endian
			timeptr[0] = sfr_data[3]; timeptr[1] = sfr_data[2]; timeptr[2] = sfr_data[1]; timeptr[3] = sfr_data[0];

			slen = strtox(outbuf, "HTTP/1.1 200 OK\r\nContent-Type: ");
			slen += strtox(outbuf + slen, mime_strings[f_data[entry].mime]);
			slen += strtox(outbuf + slen, "; charset=UTF-8\r\n");
			if (f_data[entry].gzip)
				slen += strtox(outbuf + slen, "Content-Encoding: gzip\r\n");
			slen += strtox(outbuf + slen, "Cache-Control: no-cache, no-store, must-revalidate\r\nAccess-Control-Allow-Origin: *\r\nContent-Security-Policy: style-src 'self' 'unsafe-inline'\r\n\r\n");

			len_left = f_data[entry].len;
			if (len_left > (TCP_OUTBUF_SIZE - slen)) {
				cont_len = len_left - (TCP_OUTBUF_SIZE - slen);
				len_left = TCP_OUTBUF_SIZE - slen;
				cont_addr = f_data[entry].start + len_left;
			}
			dbg_string("MIME: "); dbg_string(mime_strings[f_data[entry].mime]); dbg_char('\n');
			flash_region.addr = f_data[entry].start;
			flash_region.len = len_left;
			flash_read_bulk(outbuf + slen);
			slen += len_left;
		}
#endif
do_send:
		dbg_string("slen: "); dbg_short(slen); dbg_char('\n');
		o_idx = 0;
		if (slen > uip_mss()) {
			dbg_string("Sending a: "); dbg_short(slen); dbg_char('\n');
			uip_send(outbuf + o_idx, uip_mss());
			dbg_string("Sending a done\n");
		} else {
			dbg_string("Sending b: "); dbg_short(slen); dbg_char('\n');
			uip_send(outbuf + o_idx, slen);
			dbg_string("Sending b done\n");
		}
		s->tstate = TSTATE_TX;
	} else if (uip_rexmit()) { // Connection established, need to rexmit?
		dbg_string("RETRANSMIT requested\n");
		if (slen > uip_mss()) {
			dbg_string("Sending C: "); dbg_short(slen); dbg_char('\n');
			uip_send(outbuf + o_idx, uip_mss());
			dbg_string("Sending C done\n");
		} else if (slen > 0) {
			dbg_string("Sending D: "); dbg_short(slen); dbg_char('\n');
			uip_send(outbuf + o_idx, slen);
			dbg_string("Sending D done\n");
		}
		s->tstate = TSTATE_TX;
		uip_len = 0;
	} else {
		uip_len = 0;
	}
}

