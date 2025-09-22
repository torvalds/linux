/*	$OpenBSD: evbuffer_tls.c,v 1.3 2017/07/04 15:52:26 bluhm Exp $ */

/*
 * Copyright (c) 2002-2004 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2014-2015 Alexander Bluhm <bluhm@openbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tls.h>

#include "evbuffer_tls.h"

/* prototypes */

void bufferevent_read_pressure_cb(struct evbuffer *, size_t, size_t, void *);
static void buffertls_readcb(int, short, void *);
static void buffertls_writecb(int, short, void *);
static void buffertls_handshakecb(int, short, void *);
int evtls_read(struct evbuffer *, int, int, struct tls *);
int evtls_write(struct evbuffer *, int, struct tls *);

static int
bufferevent_add(struct event *ev, int timeout)
{
	struct timeval tv, *ptv = NULL;

	if (timeout) {
		timerclear(&tv);
		tv.tv_sec = timeout;
		ptv = &tv;
	}

	return (event_add(ev, ptv));
}

static void
buffertls_readcb(int fd, short event, void *arg)
{
	struct buffertls *buftls = arg;
	struct bufferevent *bufev = buftls->bt_bufev;
	struct tls *ctx = buftls->bt_ctx;
	int res = 0;
	short what = EVBUFFER_READ;
	size_t len;
	int howmuch = -1;

	if (event == EV_TIMEOUT) {
		what |= EVBUFFER_TIMEOUT;
		goto error;
	}

	/*
	 * If we have a high watermark configured then we don't want to
	 * read more data than would make us reach the watermark.
	 */
	if (bufev->wm_read.high != 0) {
		howmuch = bufev->wm_read.high - EVBUFFER_LENGTH(bufev->input);
		/* we might have lowered the watermark, stop reading */
		if (howmuch <= 0) {
			struct evbuffer *buf = bufev->input;
			event_del(&bufev->ev_read);
			evbuffer_setcb(buf,
			    bufferevent_read_pressure_cb, bufev);
			return;
		}
	}

	res = evtls_read(bufev->input, fd, howmuch, ctx);
	switch (res) {
	case TLS_WANT_POLLIN:
		bufferevent_add(&bufev->ev_read, bufev->timeout_read);
		return;
	case TLS_WANT_POLLOUT:
		event_del(&bufev->ev_write);
		event_set(&bufev->ev_write, fd, EV_WRITE, buffertls_readcb,
		    buftls);
		bufferevent_add(&bufev->ev_write, bufev->timeout_write);
		return;
	case -1:
		what |= EVBUFFER_ERROR;
		break;
	case 0:
		what |= EVBUFFER_EOF;
		break;
	}
	if (res <= 0)
		goto error;

	event_del(&bufev->ev_write);
	event_set(&bufev->ev_write, fd, EV_WRITE, buffertls_writecb, buftls);
	if (bufev->enabled & EV_READ)
		bufferevent_add(&bufev->ev_read, bufev->timeout_read);
	if (EVBUFFER_LENGTH(bufev->output) != 0 && bufev->enabled & EV_WRITE)
		bufferevent_add(&bufev->ev_write, bufev->timeout_write);

	/* See if this callbacks meets the water marks */
	len = EVBUFFER_LENGTH(bufev->input);
	if (bufev->wm_read.low != 0 && len < bufev->wm_read.low)
		return;
	if (bufev->wm_read.high != 0 && len >= bufev->wm_read.high) {
		struct evbuffer *buf = bufev->input;
		event_del(&bufev->ev_read);

		/* Now schedule a callback for us when the buffer changes */
		evbuffer_setcb(buf, bufferevent_read_pressure_cb, bufev);
	}

	/* Invoke the user callback - must always be called last */
	if (bufev->readcb != NULL)
		(*bufev->readcb)(bufev, bufev->cbarg);
	return;

 error:
	(*bufev->errorcb)(bufev, what, bufev->cbarg);
}

static void
buffertls_writecb(int fd, short event, void *arg)
{
	struct buffertls *buftls = arg;
	struct bufferevent *bufev = buftls->bt_bufev;
	struct tls *ctx = buftls->bt_ctx;
	int res = 0;
	short what = EVBUFFER_WRITE;

	if (event == EV_TIMEOUT) {
		what |= EVBUFFER_TIMEOUT;
		goto error;
	}

	if (EVBUFFER_LENGTH(bufev->output) != 0) {
		res = evtls_write(bufev->output, fd, ctx);
		switch (res) {
		case TLS_WANT_POLLIN:
			event_del(&bufev->ev_read);
			event_set(&bufev->ev_read, fd, EV_READ,
			    buffertls_writecb, buftls);
			bufferevent_add(&bufev->ev_read, bufev->timeout_read);
			return;
		case TLS_WANT_POLLOUT:
			bufferevent_add(&bufev->ev_write, bufev->timeout_write);
			return;
		case -1:
			what |= EVBUFFER_ERROR;
			break;
		case 0:
			what |= EVBUFFER_EOF;
			break;
		}
		if (res <= 0)
			goto error;
	}

	event_del(&bufev->ev_read);
	event_set(&bufev->ev_read, fd, EV_READ, buffertls_readcb, buftls);
	if (bufev->enabled & EV_READ)
		bufferevent_add(&bufev->ev_read, bufev->timeout_read);
	if (EVBUFFER_LENGTH(bufev->output) != 0 && bufev->enabled & EV_WRITE)
		bufferevent_add(&bufev->ev_write, bufev->timeout_write);

	/*
	 * Invoke the user callback if our buffer is drained or below the
	 * low watermark.
	 */
	if (bufev->writecb != NULL &&
	    EVBUFFER_LENGTH(bufev->output) <= bufev->wm_write.low)
		(*bufev->writecb)(bufev, bufev->cbarg);

	return;

 error:
	(*bufev->errorcb)(bufev, what, bufev->cbarg);
}

static void
buffertls_handshakecb(int fd, short event, void *arg)
{
	struct buffertls *buftls = arg;
	struct bufferevent *bufev = buftls->bt_bufev;
	struct tls *ctx = buftls->bt_ctx;
	int res = 0;
	short what = EVBUFFER_HANDSHAKE;

	if (event == EV_TIMEOUT) {
		what |= EVBUFFER_TIMEOUT;
		goto error;
	}

	res = tls_handshake(ctx);
	switch (res) {
	case TLS_WANT_POLLIN:
		bufferevent_add(&bufev->ev_read, bufev->timeout_read);
		return;
	case TLS_WANT_POLLOUT:
		bufferevent_add(&bufev->ev_write, bufev->timeout_write);
		return;
	case -1:
		what |= EVBUFFER_ERROR;
		break;
	}
	if (res < 0)
		goto error;

	/* Handshake was successful, change to read and write callback. */
	event_del(&bufev->ev_read);
	event_del(&bufev->ev_write);
	event_set(&bufev->ev_read, fd, EV_READ, buffertls_readcb, buftls);
	event_set(&bufev->ev_write, fd, EV_WRITE, buffertls_writecb, buftls);
	if (bufev->enabled & EV_READ)
		bufferevent_add(&bufev->ev_read, bufev->timeout_read);
	if (EVBUFFER_LENGTH(bufev->output) != 0 && bufev->enabled & EV_WRITE)
		bufferevent_add(&bufev->ev_write, bufev->timeout_write);

	return;

 error:
	(*bufev->errorcb)(bufev, what, bufev->cbarg);
}

void
buffertls_set(struct buffertls *buftls, struct bufferevent *bufev,
    struct tls *ctx, int fd)
{
	bufferevent_setfd(bufev, fd);
	event_set(&bufev->ev_read, fd, EV_READ, buffertls_readcb, buftls);
	event_set(&bufev->ev_write, fd, EV_WRITE, buffertls_writecb, buftls);
	buftls->bt_bufev = bufev;
	buftls->bt_ctx = ctx;
}

void
buffertls_accept(struct buffertls *buftls, int fd)
{
	struct bufferevent *bufev = buftls->bt_bufev;

	event_del(&bufev->ev_read);
	event_del(&bufev->ev_write);
	event_set(&bufev->ev_read, fd, EV_READ, buffertls_handshakecb, buftls);
	event_set(&bufev->ev_write, fd, EV_WRITE, buffertls_handshakecb,
	    buftls);
	bufferevent_add(&bufev->ev_read, bufev->timeout_read);
}

void
buffertls_connect(struct buffertls *buftls, int fd)
{
	struct bufferevent *bufev = buftls->bt_bufev;

	event_del(&bufev->ev_read);
	event_del(&bufev->ev_write);
	event_set(&bufev->ev_read, fd, EV_READ, buffertls_handshakecb, buftls);
	event_set(&bufev->ev_write, fd, EV_WRITE, buffertls_handshakecb,
	    buftls);
	bufferevent_add(&bufev->ev_write, bufev->timeout_write);
}

/*
 * Reads data from a file descriptor into a buffer.
 */

#define EVBUFFER_MAX_READ	16384

int
evtls_read(struct evbuffer *buf, int fd, int howmuch, struct tls *ctx)
{
	u_char *p;
	size_t oldoff = buf->off;
	int n = EVBUFFER_MAX_READ;

	if (howmuch < 0 || howmuch > n)
		howmuch = n;

	/* If we don't have FIONREAD, we might waste some space here */
	if (evbuffer_expand(buf, howmuch) == -1)
		return (-1);

	/* We can append new data at this point */
	p = buf->buffer + buf->off;

	n = tls_read(ctx, p, howmuch);
	if (n <= 0)
		return (n);

	buf->off += n;

	/* Tell someone about changes in this buffer */
	if (buf->off != oldoff && buf->cb != NULL)
		(*buf->cb)(buf, oldoff, buf->off, buf->cbarg);

	return (n);
}

int
evtls_write(struct evbuffer *buffer, int fd, struct tls *ctx)
{
	int n;

	n = tls_write(ctx, buffer->buffer, buffer->off);
	if (n <= 0)
		return (n);
	evbuffer_drain(buffer, n);

	return (n);
}
