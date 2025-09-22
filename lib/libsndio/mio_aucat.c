/*	$OpenBSD: mio_aucat.c,v 1.12 2016/01/09 08:27:24 ratchov Exp $	*/
/*
 * Copyright (c) 2008 Alexandre Ratchov <alex@caoua.org>
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
#include <sys/un.h>
#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aucat.h"
#include "debug.h"
#include "mio_priv.h"

struct mio_aucat_hdl {
	struct mio_hdl mio;
	struct aucat aucat;
	int events;
};

static void mio_aucat_close(struct mio_hdl *);
static size_t mio_aucat_read(struct mio_hdl *, void *, size_t);
static size_t mio_aucat_write(struct mio_hdl *, const void *, size_t);
static int mio_aucat_nfds(struct mio_hdl *);
static int mio_aucat_pollfd(struct mio_hdl *, struct pollfd *, int);
static int mio_aucat_revents(struct mio_hdl *, struct pollfd *);

static struct mio_ops mio_aucat_ops = {
	mio_aucat_close,
	mio_aucat_write,
	mio_aucat_read,
	mio_aucat_nfds,
	mio_aucat_pollfd,
	mio_aucat_revents
};

/*
 * execute the next message, return 0 if blocked
 */
static int
mio_aucat_runmsg(struct mio_aucat_hdl *hdl)
{
	int delta;

	if (!_aucat_rmsg(&hdl->aucat, &hdl->mio.eof))
		return 0;
	switch (ntohl(hdl->aucat.rmsg.cmd)) {
	case AMSG_DATA:
		return 1;
	case AMSG_FLOWCTL:
		delta = ntohl(hdl->aucat.rmsg.u.ts.delta);
		hdl->aucat.maxwrite += delta;
		DPRINTF("aucat: flowctl = %d, maxwrite = %d\n",
		    delta, hdl->aucat.maxwrite);
		break;
	default:
		DPRINTF("mio_aucat_runmsg: unhandled message %u\n",
		    hdl->aucat.rmsg.cmd);
		hdl->mio.eof = 1;
		return 0;
	}
	hdl->aucat.rstate = RSTATE_MSG;
	hdl->aucat.rtodo = sizeof(struct amsg);
	return 1;
}

struct mio_hdl *
_mio_aucat_open(const char *str, unsigned int mode, int nbio)
{
	struct mio_aucat_hdl *hdl;

	hdl = malloc(sizeof(struct mio_aucat_hdl));
	if (hdl == NULL)
		return NULL;
	if (!_aucat_open(&hdl->aucat, str, mode))
		goto bad;
	_mio_create(&hdl->mio, &mio_aucat_ops, mode, nbio);
	if (!_aucat_setfl(&hdl->aucat, 1, &hdl->mio.eof))
		goto bad;
	return (struct mio_hdl *)hdl;
bad:
	free(hdl);
	return NULL;
}

static void
mio_aucat_close(struct mio_hdl *sh)
{
	struct mio_aucat_hdl *hdl = (struct mio_aucat_hdl *)sh;

	if (!hdl->mio.eof)
		_aucat_setfl(&hdl->aucat, 0, &hdl->mio.eof);
	_aucat_close(&hdl->aucat, hdl->mio.eof);
	free(hdl);
}

static size_t
mio_aucat_read(struct mio_hdl *sh, void *buf, size_t len)
{
	struct mio_aucat_hdl *hdl = (struct mio_aucat_hdl *)sh;

	while (hdl->aucat.rstate == RSTATE_MSG) {
		if (!mio_aucat_runmsg(hdl))
			return 0;
	}
	return _aucat_rdata(&hdl->aucat, buf, len, &hdl->mio.eof);
}

static size_t
mio_aucat_write(struct mio_hdl *sh, const void *buf, size_t len)
{
	struct mio_aucat_hdl *hdl = (struct mio_aucat_hdl *)sh;
	size_t n;

	if (len <= 0 || hdl->aucat.maxwrite <= 0)
		return 0;
	if (len > hdl->aucat.maxwrite)
		len = hdl->aucat.maxwrite;
	n = _aucat_wdata(&hdl->aucat, buf, len, 1, &hdl->mio.eof);
	hdl->aucat.maxwrite -= n;
	return n;
}

static int
mio_aucat_nfds(struct mio_hdl *sh)
{
	return 1;
}

static int
mio_aucat_pollfd(struct mio_hdl *sh, struct pollfd *pfd, int events)
{
	struct mio_aucat_hdl *hdl = (struct mio_aucat_hdl *)sh;

	hdl->events = events;
	if (hdl->aucat.maxwrite <= 0)
		events &= ~POLLOUT;
	return _aucat_pollfd(&hdl->aucat, pfd, events);
}

static int
mio_aucat_revents(struct mio_hdl *sh, struct pollfd *pfd)
{
	struct mio_aucat_hdl *hdl = (struct mio_aucat_hdl *)sh;
	int revents = pfd->revents;

	if (revents & POLLIN) {
		while (hdl->aucat.rstate == RSTATE_MSG) {
			if (!mio_aucat_runmsg(hdl))
				break;
		}
		if (hdl->aucat.rstate != RSTATE_DATA)
			revents &= ~POLLIN;
	}
	if (revents & POLLOUT) {
		if (hdl->aucat.maxwrite <= 0)
			revents &= ~POLLOUT;
	}
	if (hdl->mio.eof)
		return POLLHUP;
	return revents & (hdl->events | POLLHUP);
}
