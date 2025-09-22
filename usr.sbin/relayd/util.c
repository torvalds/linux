/*	$OpenBSD: util.c,v 1.5 2024/09/20 02:00:46 jsg Exp $	*/

/*
 * Copyright (c) 2006 - 2015 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <netdb.h>
#include <ctype.h>

#include "relayd.h"

const char *
host_error(enum host_error he)
{
	switch (he) {
	case HCE_NONE:
		return ("none");
		break;
	case HCE_ABORT:
		return ("aborted");
		break;
	case HCE_INTERVAL_TIMEOUT:
		return ("interval timeout");
		break;
	case HCE_ICMP_OK:
		return ("icmp ok");
		break;
	case HCE_ICMP_READ_TIMEOUT:
		return ("icmp read timeout");
		break;
	case HCE_ICMP_WRITE_TIMEOUT:
		return ("icmp write timeout");
		break;
	case HCE_TCP_SOCKET_ERROR:
		return ("tcp socket error");
		break;
	case HCE_TCP_SOCKET_LIMIT:
		return ("tcp socket limit");
		break;
	case HCE_TCP_SOCKET_OPTION:
		return ("tcp socket option");
		break;
	case HCE_TCP_CONNECT_FAIL:
		return ("tcp connect failed");
		break;
	case HCE_TCP_CONNECT_TIMEOUT:
		return ("tcp connect timeout");
		break;
	case HCE_TCP_CONNECT_OK:
		return ("tcp connect ok");
		break;
	case HCE_TCP_WRITE_TIMEOUT:
		return ("tcp write timeout");
		break;
	case HCE_TCP_WRITE_FAIL:
		return ("tcp write failed");
		break;
	case HCE_TCP_READ_TIMEOUT:
		return ("tcp read timeout");
		break;
	case HCE_TCP_READ_FAIL:
		return ("tcp read failed");
		break;
	case HCE_SCRIPT_OK:
		return ("script ok");
		break;
	case HCE_SCRIPT_FAIL:
		return ("script failed");
		break;
	case HCE_TLS_CONNECT_OK:
		return ("tls connect ok");
		break;
	case HCE_TLS_CONNECT_FAIL:
		return ("tls connect failed");
		break;
	case HCE_TLS_CONNECT_TIMEOUT:
		return ("tls connect timeout");
		break;
	case HCE_TLS_CONNECT_ERROR:
		return ("tls connect error");
		break;
	case HCE_TLS_READ_TIMEOUT:
		return ("tls read timeout");
		break;
	case HCE_TLS_WRITE_TIMEOUT:
		return ("tls write timeout");
		break;
	case HCE_TLS_READ_ERROR:
		return ("tls read error");
		break;
	case HCE_TLS_WRITE_ERROR:
		return ("tls write error");
		break;
	case HCE_SEND_EXPECT_FAIL:
		return ("send/expect failed");
		break;
	case HCE_SEND_EXPECT_OK:
		return ("send/expect ok");
		break;
	case HCE_HTTP_CODE_ERROR:
		return ("http code malformed");
		break;
	case HCE_HTTP_CODE_FAIL:
		return ("http code mismatch");
		break;
	case HCE_HTTP_CODE_OK:
		return ("http code ok");
		break;
	case HCE_HTTP_DIGEST_ERROR:
		return ("http digest malformed");
		break;
	case HCE_HTTP_DIGEST_FAIL:
		return ("http digest mismatch");
		break;
	case HCE_HTTP_DIGEST_OK:
		return ("http digest ok");
		break;
	}
	/* NOTREACHED */
	return ("invalid");
}

const char *
host_status(enum host_status status)
{
	switch (status) {
	case HOST_DOWN:
		return ("down");
	case HOST_UNKNOWN:
		return ("unknown");
	case HOST_UP:
		return ("up");
	}
	/* NOTREACHED */
	return ("invalid");
}

const char *
table_check(enum table_check check)
{
	switch (check) {
	case CHECK_NOCHECK:
		return ("none");
	case CHECK_ICMP:
		return ("icmp");
	case CHECK_TCP:
		return ("tcp");
	case CHECK_HTTP_CODE:
		return ("http code");
	case CHECK_HTTP_DIGEST:
		return ("http digest");
	case CHECK_BINSEND_EXPECT:
	case CHECK_SEND_EXPECT:
		return ("send expect");
	case CHECK_SCRIPT:
		return ("script");
	}
	/* NOTREACHED */
	return ("invalid");
}

#ifdef DEBUG
const char *
relay_state(enum relay_state state)
{
	switch (state) {
	case STATE_INIT:
		return ("init");
	case STATE_PENDING:
		return ("pending");
	case STATE_PRECONNECT:
		return ("preconnect");
	case STATE_CONNECTED:
		return ("connected");
	case STATE_CLOSED:
		return ("closed");
	case STATE_DONE:
		return ("done");
	}
	/* NOTREACHED */
	return ("invalid");
}
#endif

const char *
print_availability(u_long cnt, u_long up)
{
	static char buf[BUFSIZ];

	if (cnt == 0)
		return ("");
	bzero(buf, sizeof(buf));
	snprintf(buf, sizeof(buf), "%.2f%%", (double)up / cnt * 100);
	return (buf);
}

const char *
print_host(struct sockaddr_storage *ss, char *buf, size_t len)
{
	if (getnameinfo((struct sockaddr *)ss, ss->ss_len,
	    buf, len, NULL, 0, NI_NUMERICHOST) != 0) {
		buf[0] = '\0';
		return (NULL);
	}
	return (buf);
}

const char *
print_time(struct timeval *a, struct timeval *b, char *buf, size_t len)
{
	struct timeval		tv;
	u_long			h, sec, min;

	timerclear(&tv);
	timersub(a, b, &tv);
	sec = tv.tv_sec % 60;
	min = tv.tv_sec / 60 % 60;
	h = tv.tv_sec / 60 / 60;

	snprintf(buf, len, "%.2lu:%.2lu:%.2lu", h, min, sec);
	return (buf);
}

const char *
printb_flags(const u_int32_t v, const char *bits)
{
	static char	 buf[2][BUFSIZ];
	static int	 idx = 0;
	int		 i, any = 0;
	char		 c, *p, *r;

	p = r = buf[++idx % 2];
	bzero(p, BUFSIZ);

	if (bits) {
		bits++;
		while ((i = *bits++)) {
			if (v & (1 << (i - 1))) {
				if (any) {
					*p++ = ',';
					*p++ = ' ';
				}
				any = 1;
				for (; (c = *bits) > 32; bits++) {
					if (c == '_')
						*p++ = ' ';
					else
						*p++ = tolower((u_char)c);
				}
			} else
				for (; *bits > 32; bits++)
					;
		}
	}

	return (r);
}

void
getmonotime(struct timeval *tv)
{
	struct timespec	 ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		fatal("clock_gettime");

	TIMESPEC_TO_TIMEVAL(tv, &ts);
}

struct ibuf *
string2binary(const char *string)
{
	struct ibuf	*ibuf = NULL;
	unsigned char	 ch, r;
	size_t		 i, len;

	len = strlen(string);
	if (len % 2 != 0)
		goto fail;
	if ((ibuf = ibuf_open(len / 2)) == NULL)
		goto fail;

	while (*string) {
		r = 0;
		for (i = 0; i < 2; i++) {
			ch = string[i];
			if (isdigit(ch))
				ch -= '0';
			else if (islower(ch))
				ch -= ('a' - 10);
			else if (isupper(ch))
				ch -= ('A' - 10);
			else
				goto fail;
			if (ch > 0xf)
				goto fail;
			r = r << 4 | ch;
		}
		if (ibuf_add_n8(ibuf, r) == -1)
			goto fail;
		string += 2;
	}

	return ibuf;

fail:
	ibuf_free(ibuf);
	return NULL;
}

void
print_hex(uint8_t *buf, off_t offset, size_t length)
{
	unsigned int	 i;

	if (log_getverbose() < 3 || !length)
		return;

	for (i = 0; i < length; i++) {
		if (i && (i % 4) == 0) {
			if ((i % 32) == 0)
				print_debug("\n");
			else
				print_debug(" ");
		}
		print_debug("%02x", buf[offset + i]);
	}
	print_debug("\n");
}

void
print_debug(const char *emsg, ...)
{
	va_list	 ap;

	if (log_getverbose() > 2) {
		va_start(ap, emsg);
		vfprintf(stderr, emsg, ap);
		va_end(ap);
	}
}
