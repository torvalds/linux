/*	$OpenBSD: mio.c,v 1.23 2019/06/29 06:05:26 ratchov Exp $	*/
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
#include <sys/time.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "mio_priv.h"

struct mio_hdl *
mio_open(const char *str, unsigned int mode, int nbio)
{
	static char portany[] = MIO_PORTANY;
	struct mio_hdl *hdl;

#ifdef DEBUG
	_sndio_debug_init();
#endif
	if ((mode & (MIO_OUT | MIO_IN)) == 0)
		return NULL;
	if (str == NULL) /* backward compat */
		str = portany;
	if (strcmp(str, portany) == 0 && !issetugid()) {
		str = getenv("MIDIDEVICE");
		if (str == NULL)
			str = portany;
	}
	if (strcmp(str, portany) == 0) {
		hdl = _mio_aucat_open("midithru/0", mode, nbio);
		if (hdl != NULL)
			return hdl;
		return _mio_rmidi_open("rmidi/0", mode, nbio);
	}
	if (_sndio_parsetype(str, "snd") ||
	    _sndio_parsetype(str, "midithru") ||
	    _sndio_parsetype(str, "midi"))
		return _mio_aucat_open(str, mode, nbio);
	if (_sndio_parsetype(str, "rmidi"))
		return _mio_rmidi_open(str, mode, nbio);
	DPRINTF("mio_open: %s: unknown device type\n", str);
	return NULL;
}

void
_mio_create(struct mio_hdl *hdl, struct mio_ops *ops,
    unsigned int mode, int nbio)
{
	hdl->ops = ops;
	hdl->mode = mode;
	hdl->nbio = nbio;
	hdl->eof = 0;
}

void
mio_close(struct mio_hdl *hdl)
{
	hdl->ops->close(hdl);
}

static int
mio_psleep(struct mio_hdl *hdl, int event)
{
	struct pollfd pfd[MIO_MAXNFDS];
	int revents;
	int nfds;

	nfds = mio_nfds(hdl);
	if (nfds > MIO_MAXNFDS) {
		DPRINTF("mio_psleep: %d: too many descriptors\n", nfds);
		hdl->eof = 1;
		return 0;
	}
	for (;;) {
		nfds = mio_pollfd(hdl, pfd, event);
		while (poll(pfd, nfds, -1) == -1) {
			if (errno == EINTR)
				continue;
			DPERROR("mio_psleep: poll");
			hdl->eof = 1;
			return 0;
		}
		revents = mio_revents(hdl, pfd);
		if (revents & POLLHUP) {
			DPRINTF("mio_psleep: hang-up\n");
			return 0;
		}
		if (revents & event)
			break;
	}
	return 1;
}

size_t
mio_read(struct mio_hdl *hdl, void *buf, size_t len)
{
	unsigned int n;
	char *data = buf;
	size_t todo = len;

	if (hdl->eof) {
		DPRINTF("mio_read: eof\n");
		return 0;
	}
	if (!(hdl->mode & MIO_IN)) {
		DPRINTF("mio_read: not input device\n");
		hdl->eof = 1;
		return 0;
	}
	while (todo > 0) {
		n = hdl->ops->read(hdl, data, todo);
		if (n == 0 && hdl->eof)
			break;
		data += n;
		todo -= n;
		if (n > 0 || hdl->nbio)
			break;
		if (!mio_psleep(hdl, POLLIN))
			break;
	}
	return len - todo;
}

size_t
mio_write(struct mio_hdl *hdl, const void *buf, size_t len)
{
	unsigned int n;
	const unsigned char *data = buf;
	size_t todo = len;

	if (hdl->eof) {
		DPRINTF("mio_write: eof\n");
		return 0;
	}
	if (!(hdl->mode & MIO_OUT)) {
		DPRINTF("mio_write: not output device\n");
		hdl->eof = 1;
		return 0;
	}
	while (todo > 0) {
		n = hdl->ops->write(hdl, data, todo);
		if (n == 0) {
			if (hdl->nbio || hdl->eof)
				break;
			if (!mio_psleep(hdl, POLLOUT))
				break;
			continue;
		}
		data += n;
		todo -= n;
	}
	return len - todo;
}

int
mio_nfds(struct mio_hdl *hdl)
{
	return hdl->ops->nfds(hdl);
}

int
mio_pollfd(struct mio_hdl *hdl, struct pollfd *pfd, int events)
{
	if (hdl->eof)
		return 0;
	return hdl->ops->pollfd(hdl, pfd, events);
}

int
mio_revents(struct mio_hdl *hdl, struct pollfd *pfd)
{
	if (hdl->eof)
		return POLLHUP;
	return hdl->ops->revents(hdl, pfd);
}

int
mio_eof(struct mio_hdl *hdl)
{
	return hdl->eof;
}
