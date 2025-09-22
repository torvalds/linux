/*	$OpenBSD: sioctl.c,v 1.2 2021/11/01 14:43:24 ratchov Exp $	*/
/*
 * Copyright (c) 2014-2020 Alexandre Ratchov <alex@caoua.org>
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
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "sioctl_priv.h"

struct sioctl_hdl *
sioctl_open(const char *str, unsigned int mode, int nbio)
{
	static char devany[] = SIO_DEVANY;
	struct sioctl_hdl *hdl;

#ifdef DEBUG
	_sndio_debug_init();
#endif
	if (str == NULL) /* backward compat */
		str = devany;
	if (strcmp(str, devany) == 0 && !issetugid()) {
		str = getenv("AUDIODEVICE");
		if (str == NULL)
			str = devany;
	}
	if (strcmp(str, devany) == 0) {
		hdl = _sioctl_aucat_open("snd/default", mode, nbio);
		if (hdl != NULL)
			return hdl;
		return _sioctl_sun_open("rsnd/0", mode, nbio);
	}
	if (_sndio_parsetype(str, "snd"))
		return _sioctl_aucat_open(str, mode, nbio);
	if (_sndio_parsetype(str, "rsnd"))
		return _sioctl_sun_open(str, mode, nbio);
	DPRINTF("sioctl_open: %s: unknown device type\n", str);
	return NULL;
}

void
_sioctl_create(struct sioctl_hdl *hdl, struct sioctl_ops *ops,
    unsigned int mode, int nbio)
{
	hdl->ops = ops;
	hdl->mode = mode;
	hdl->nbio = nbio;
	hdl->eof = 0;
	hdl->ctl_cb = NULL;
}

int
_sioctl_psleep(struct sioctl_hdl *hdl, int event)
{
	struct pollfd pfds[SIOCTL_MAXNFDS];
	int revents, nfds;

	for (;;) {
		nfds = sioctl_pollfd(hdl, pfds, event);
		if (nfds == 0)
			return 0;
		while (poll(pfds, nfds, -1) < 0) {
			if (errno == EINTR)
				continue;
			DPERROR("sioctl_psleep: poll");
			hdl->eof = 1;
			return 0;
		}
		revents = sioctl_revents(hdl, pfds);
		if (revents & POLLHUP) {
			DPRINTF("sioctl_psleep: hang-up\n");
			return 0;
		}
		if (event == 0 || (revents & event))
			break;
	}
	return 1;
}

void
sioctl_close(struct sioctl_hdl *hdl)
{
	hdl->ops->close(hdl);
}

int
sioctl_nfds(struct sioctl_hdl *hdl)
{
	return hdl->ops->nfds(hdl);
}

int
sioctl_pollfd(struct sioctl_hdl *hdl, struct pollfd *pfd, int events)
{
	if (hdl->eof)
		return 0;
	return hdl->ops->pollfd(hdl, pfd, events);
}

int
sioctl_revents(struct sioctl_hdl *hdl, struct pollfd *pfd)
{
	if (hdl->eof)
		return POLLHUP;
	return hdl->ops->revents(hdl, pfd);
}

int
sioctl_eof(struct sioctl_hdl *hdl)
{
	return hdl->eof;
}

int
sioctl_ondesc(struct sioctl_hdl *hdl,
    void (*cb)(void *, struct sioctl_desc *, int), void *arg)
{
	hdl->desc_cb = cb;
	hdl->desc_arg = arg;
	return hdl->ops->ondesc(hdl);
}

int
sioctl_onval(struct sioctl_hdl *hdl,
    void (*cb)(void *, unsigned int, unsigned int), void *arg)
{
	hdl->ctl_cb = cb;
	hdl->ctl_arg = arg;
	return hdl->ops->onctl(hdl);
}

void
_sioctl_ondesc_cb(struct sioctl_hdl *hdl,
    struct sioctl_desc *desc, unsigned int val)
{
	if (desc) {
		DPRINTF("_sioctl_ondesc_cb: %u -> %s[%d].%s=%s[%d]:%d\n",
		    desc->addr,
		    desc->node0.name, desc->node0.unit,
		    desc->func,
		    desc->node1.name, desc->node1.unit,
		    val);
	}
	if (hdl->desc_cb)
		hdl->desc_cb(hdl->desc_arg, desc, val);
}

void
_sioctl_onval_cb(struct sioctl_hdl *hdl, unsigned int addr, unsigned int val)
{
	DPRINTF("_sioctl_onval_cb: %u -> %u\n", addr, val);
	if (hdl->ctl_cb)
		hdl->ctl_cb(hdl->ctl_arg, addr, val);
}

int
sioctl_setval(struct sioctl_hdl *hdl, unsigned int addr, unsigned int val)
{
	if (!(hdl->mode & SIOCTL_WRITE))
		return 0;
	return hdl->ops->setctl(hdl, addr, val);
}
