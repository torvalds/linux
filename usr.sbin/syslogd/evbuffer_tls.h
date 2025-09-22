/*	$OpenBSD: evbuffer_tls.h,v 1.5 2015/10/09 16:58:25 bluhm Exp $ */

/*
 * Copyright (c) 2014-2015 Alexander Bluhm <bluhm@openbsd.org>
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

#ifndef _EVBUFFER_TLS_H_
#define _EVBUFFER_TLS_H_

#define EVBUFFER_HANDSHAKE	0x04

struct bufferevent;
struct tls;

struct buffertls {
	struct bufferevent	*bt_bufev;
	struct tls		*bt_ctx;
};

void	buffertls_set(struct buffertls *, struct bufferevent *, struct tls *,
    int);
void	buffertls_accept(struct buffertls *, int);
void	buffertls_connect(struct buffertls *, int);

#endif /* _EVBUFFER_TLS_H_ */
