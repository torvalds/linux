/*
 * Copyright (c) 2017 Antoine Kaufmann <toni@famkaufmann.info>
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

#include <sys/un.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "smtpd.h"
#include "log.h"

/*
 * The PROXYv2 protocol is described here:
 * http://www.haproxy.org/download/1.8/doc/proxy-protocol.txt
 */

#define PROXY_CLOCAL 0x0
#define PROXY_CPROXY 0x1
#define PROXY_AF_UNSPEC 0x0
#define PROXY_AF_INET 0x1
#define PROXY_AF_INET6 0x2
#define PROXY_AF_UNIX 0x3
#define PROXY_TF_UNSPEC 0x0
#define PROXY_TF_STREAM 0x1
#define PROXY_TF_DGRAM 0x2

#define PROXY_SESSION_TIMEOUT	300

static const uint8_t pv2_signature[] = {
	0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D,
	0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A
};

struct proxy_hdr_v2 {
	uint8_t sig[12];
	uint8_t ver_cmd;
	uint8_t fam;
	uint16_t len;
} __attribute__((packed));


struct proxy_addr_ipv4 {
	uint32_t src_addr;
	uint32_t dst_addr;
	uint16_t src_port;
	uint16_t dst_port;
} __attribute__((packed));

struct proxy_addr_ipv6 {
	uint8_t src_addr[16];
	uint8_t dst_addr[16];
	uint16_t src_port;
	uint16_t dst_port;
} __attribute__((packed));

struct proxy_addr_unix {
	uint8_t src_addr[108];
	uint8_t dst_addr[108];
} __attribute__((packed));

union proxy_addr {
	struct proxy_addr_ipv4 ipv4;
	struct proxy_addr_ipv6 ipv6;
	struct proxy_addr_unix un;
} __attribute__((packed));

struct proxy_session {
	struct listener	*l;
	struct io	*io;

	uint64_t	id;
	int		fd;
	uint16_t	header_len;
	uint16_t	header_total;
	uint16_t	addr_len;
	uint16_t	addr_total;

	struct sockaddr_storage	ss;
	struct proxy_hdr_v2	hdr;
	union proxy_addr	addr;

	void (*cb_accepted)(struct listener *, int,
	    const struct sockaddr_storage *, struct io *);
	void (*cb_dropped)(struct listener *, int,
	    const struct sockaddr_storage *);
};

static void proxy_io(struct io *, int, void *);
static void proxy_error(struct proxy_session *, const char *, const char *);
static int proxy_header_validate(struct proxy_session *);
static int proxy_translate_ss(struct proxy_session *);

int
proxy_session(struct listener *listener, int sock,
    const struct sockaddr_storage *ss,
    void (*accepted)(struct listener *, int,
	const struct sockaddr_storage *, struct io *),
    void (*dropped)(struct listener *, int,
	const struct sockaddr_storage *));

int
proxy_session(struct listener *listener, int sock,
    const struct sockaddr_storage *ss,
    void (*accepted)(struct listener *, int,
	const struct sockaddr_storage *, struct io *),
    void (*dropped)(struct listener *, int,
	const struct sockaddr_storage *))
{
	struct proxy_session *s;

	if ((s = calloc(1, sizeof(*s))) == NULL)
		return (-1);

	if ((s->io = io_new()) == NULL) {
		free(s);
		return (-1);
	}

	s->id = generate_uid();
	s->l = listener;
	s->fd = sock;
	s->header_len = 0;
	s->addr_len = 0;
	s->ss = *ss;
	s->cb_accepted = accepted;
	s->cb_dropped = dropped;

	io_set_callback(s->io, proxy_io, s);
	io_set_fd(s->io, sock);
	io_set_timeout(s->io, PROXY_SESSION_TIMEOUT * 1000);
	io_set_read(s->io);

	log_info("%016"PRIx64" smtp event=proxy address=%s",
	    s->id, ss_to_text(&s->ss));

	return 0;
}

static void
proxy_io(struct io *io, int evt, void *arg)
{
	struct proxy_session	*s = arg;
	struct proxy_hdr_v2	*h = &s->hdr;
	uint8_t *buf;
	size_t len, off;

	switch (evt) {

	case IO_DATAIN:
		buf = io_data(io);
		len = io_datalen(io);

		if (s->header_len < sizeof(s->hdr)) {
			/* header is incomplete */
			off = sizeof(s->hdr) - s->header_len;
			off = (len < off ? len : off);
			memcpy((uint8_t *) &s->hdr + s->header_len, buf, off);

			s->header_len += off;
			buf += off;
			len -= off;
			io_drop(s->io, off);

			if (s->header_len < sizeof(s->hdr)) {
				/* header is still not complete */
				return;
			}

			if (proxy_header_validate(s) != 0)
				return;
		}

		if (s->addr_len < s->addr_total) {
			/* address is incomplete */
			off = s->addr_total - s->addr_len;
			off = (len < off ? len : off);
			memcpy((uint8_t *) &s->addr + s->addr_len, buf, off);

			s->header_len += off;
			s->addr_len += off;
			buf += off;
			len -= off;
			io_drop(s->io, off);

			if (s->addr_len < s->addr_total) {
				/* address is still not complete */
				return;
			}
		}

		if (s->header_len < s->header_total) {
			/* additional parameters not complete */
			/* these are ignored for now, but we still need to drop
			 * the bytes from the buffer */
			off = s->header_total - s->header_len;
			off = (len < off ? len : off);

			s->header_len += off;
			io_drop(s->io, off);

			if (s->header_len < s->header_total)
				/* not complete yet */
				return;
		}

		switch(h->ver_cmd & 0xF) {
		case PROXY_CLOCAL:
			/* local address, no need to modify ss */
			break;

		case PROXY_CPROXY:
			if (proxy_translate_ss(s) != 0)
				return;
			break;

		default:
			proxy_error(s, "protocol error", "unknown command");
			return;
		}

		s->cb_accepted(s->l, s->fd, &s->ss, s->io);
		/* we passed off s->io, so it does not need to be freed here */
		free(s);
		break;

	case IO_TIMEOUT:
		proxy_error(s, "timeout", NULL);
		break;

	case IO_DISCONNECTED:
		proxy_error(s, "disconnected", NULL);
		break;

	case IO_ERROR:
		proxy_error(s, "IO error", io_error(io));
		break;

	default:
		fatalx("proxy_io()");
	}

}

static void
proxy_error(struct proxy_session *s, const char *reason, const char *extra)
{
	if (extra)
		log_info("proxy %p event=closed address=%s reason=\"%s (%s)\"",
		    s, ss_to_text(&s->ss), reason, extra);
	else
		log_info("proxy %p event=closed address=%s reason=\"%s\"",
		    s, ss_to_text(&s->ss), reason);

	s->cb_dropped(s->l, s->fd, &s->ss);
	io_free(s->io);
	free(s);
}

static int
proxy_header_validate(struct proxy_session *s)
{
	struct proxy_hdr_v2 *h = &s->hdr;

	if (memcmp(h->sig, pv2_signature,
		sizeof(pv2_signature)) != 0) {
		proxy_error(s, "protocol error", "invalid signature");
		return (-1);
	}

	if ((h->ver_cmd >> 4) != 2) {
		proxy_error(s, "protocol error", "invalid version");
		return (-1);
	}

	switch (h->fam) {
	case (PROXY_AF_UNSPEC << 4 | PROXY_TF_UNSPEC):
		s->addr_total = 0;
		break;

	case (PROXY_AF_INET << 4 | PROXY_TF_STREAM):
		s->addr_total = sizeof(s->addr.ipv4);
		break;

	case (PROXY_AF_INET6 << 4 | PROXY_TF_STREAM):
		s->addr_total = sizeof(s->addr.ipv6);
		break;

	case (PROXY_AF_UNIX << 4 | PROXY_TF_STREAM):
		s->addr_total = sizeof(s->addr.un);
		break;

	default:
		proxy_error(s, "protocol error", "unsupported address family");
		return (-1);
	}

	s->header_total = ntohs(h->len);
	if (s->header_total > UINT16_MAX - sizeof(struct proxy_hdr_v2)) {
		proxy_error(s, "protocol error", "header too long");
		return (-1);
	}
	s->header_total += sizeof(struct proxy_hdr_v2);

	if (s->header_total < sizeof(struct proxy_hdr_v2) + s->addr_total) {
		proxy_error(s, "protocol error", "address info too short");
		return (-1);
	}

	return 0;
}

static int
proxy_translate_ss(struct proxy_session *s)
{
	struct sockaddr_in *sin = (struct sockaddr_in *) &s->ss;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) &s->ss;
	struct sockaddr_un *sun = (struct sockaddr_un *) &s->ss;
	size_t sun_len;

	switch (s->hdr.fam) {
	case (PROXY_AF_UNSPEC << 4 | PROXY_TF_UNSPEC):
		/* unspec: only supported for local */
		proxy_error(s, "address translation", "UNSPEC family not "
		    "supported for PROXYing");
		return (-1);

	case (PROXY_AF_INET << 4 | PROXY_TF_STREAM):
		memset(&s->ss, 0, sizeof(s->ss));
		sin->sin_family = AF_INET;
		sin->sin_port = s->addr.ipv4.src_port;
		sin->sin_addr.s_addr = s->addr.ipv4.src_addr;
		break;

	case (PROXY_AF_INET6 << 4 | PROXY_TF_STREAM):
		memset(&s->ss, 0, sizeof(s->ss));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = s->addr.ipv6.src_port;
		memcpy(sin6->sin6_addr.s6_addr, s->addr.ipv6.src_addr,
		    sizeof(s->addr.ipv6.src_addr));
		break;

	case (PROXY_AF_UNIX << 4 | PROXY_TF_STREAM):
		memset(&s->ss, 0, sizeof(s->ss));
		sun_len = strnlen(s->addr.un.src_addr,
		    sizeof(s->addr.un.src_addr));
		if (sun_len > sizeof(sun->sun_path)) {
			proxy_error(s, "address translation", "Unix socket path"
			    " longer than supported");
			return (-1);
		}
		sun->sun_family = AF_UNIX;
		memcpy(sun->sun_path, s->addr.un.src_addr, sun_len);
		break;

	default:
		fatalx("proxy_translate_ss()");
	}

	return 0;
}
