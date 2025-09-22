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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sndio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "debug.h"
#include "aucat.h"
#include "sioctl_priv.h"

struct sioctl_aucat_hdl {
	struct sioctl_hdl sioctl;
	struct aucat aucat;
	struct sioctl_desc desc;
	struct amsg_ctl_desc buf[16];
	size_t buf_wpos;
	int dump_wait;
};

static void sioctl_aucat_close(struct sioctl_hdl *);
static int sioctl_aucat_nfds(struct sioctl_hdl *);
static int sioctl_aucat_pollfd(struct sioctl_hdl *, struct pollfd *, int);
static int sioctl_aucat_revents(struct sioctl_hdl *, struct pollfd *);
static int sioctl_aucat_setctl(struct sioctl_hdl *, unsigned int, unsigned int);
static int sioctl_aucat_onval(struct sioctl_hdl *);
static int sioctl_aucat_ondesc(struct sioctl_hdl *);

/*
 * operations every device should support
 */
struct sioctl_ops sioctl_aucat_ops = {
	sioctl_aucat_close,
	sioctl_aucat_nfds,
	sioctl_aucat_pollfd,
	sioctl_aucat_revents,
	sioctl_aucat_setctl,
	sioctl_aucat_onval,
	sioctl_aucat_ondesc
};

static int
sioctl_aucat_rdata(struct sioctl_aucat_hdl *hdl)
{
	struct sioctl_desc desc;
	struct amsg_ctl_desc *c;
	size_t rpos;
	int n;

	while (hdl->aucat.rstate == RSTATE_DATA) {

		/* read entries */
		while (hdl->buf_wpos < sizeof(hdl->buf) &&
		    hdl->aucat.rstate == RSTATE_DATA) {
			n = _aucat_rdata(&hdl->aucat,
			    (unsigned char *)hdl->buf + hdl->buf_wpos,
			    sizeof(hdl->buf) - hdl->buf_wpos,
			    &hdl->sioctl.eof);
			if (n == 0 || hdl->sioctl.eof)
				return 0;
			hdl->buf_wpos += n;
		}

		/* parse entries */
		c = hdl->buf;
		rpos = 0;
		while (rpos < hdl->buf_wpos) {
			strlcpy(desc.group, c->group, SIOCTL_NAMEMAX);
			strlcpy(desc.node0.name, c->node0.name, SIOCTL_NAMEMAX);
			desc.node0.unit = (int16_t)ntohs(c->node0.unit);
			strlcpy(desc.node1.name, c->node1.name, SIOCTL_NAMEMAX);
			desc.node1.unit = (int16_t)ntohs(c->node1.unit);
			strlcpy(desc.func, c->func, SIOCTL_NAMEMAX);
			strlcpy(desc.display, c->display, SIOCTL_DISPLAYMAX);
			desc.type = c->type;
			desc.addr = ntohs(c->addr);
			desc.maxval = ntohs(c->maxval);
			_sioctl_ondesc_cb(&hdl->sioctl,
			    &desc, ntohs(c->curval));
			rpos += sizeof(struct amsg_ctl_desc);
			c++;
		}
		hdl->buf_wpos = 0;
	}
	return 1;
}

/*
 * execute the next message, return 0 if blocked
 */
static int
sioctl_aucat_runmsg(struct sioctl_aucat_hdl *hdl)
{
	if (!_aucat_rmsg(&hdl->aucat, &hdl->sioctl.eof))
		return 0;
	switch (ntohl(hdl->aucat.rmsg.cmd)) {
	case AMSG_DATA:
		hdl->buf_wpos = 0;
		if (!sioctl_aucat_rdata(hdl))
			return 0;
		break;
	case AMSG_CTLSET:
		DPRINTF("sioctl_aucat_runmsg: got CTLSET\n");
		_sioctl_onval_cb(&hdl->sioctl,
		    ntohs(hdl->aucat.rmsg.u.ctlset.addr),
		    ntohs(hdl->aucat.rmsg.u.ctlset.val));
		break;
	case AMSG_CTLSYNC:
		DPRINTF("sioctl_aucat_runmsg: got CTLSYNC\n");
		hdl->dump_wait = 0;
		_sioctl_ondesc_cb(&hdl->sioctl, NULL, 0);
		break;
	default:
		DPRINTF("sio_aucat_runmsg: unhandled message %u\n",
		    hdl->aucat.rmsg.cmd);
		hdl->sioctl.eof = 1;
		return 0;
	}
	hdl->aucat.rstate = RSTATE_MSG;
	hdl->aucat.rtodo = sizeof(struct amsg);
	return 1;
}

struct sioctl_hdl *
_sioctl_aucat_open(const char *str, unsigned int mode, int nbio)
{
	struct sioctl_aucat_hdl *hdl;

	hdl = malloc(sizeof(struct sioctl_aucat_hdl));
	if (hdl == NULL)
		return NULL;
	if (!_aucat_open(&hdl->aucat, str, mode))
		goto bad;
	_sioctl_create(&hdl->sioctl, &sioctl_aucat_ops, mode, nbio);
	if (!_aucat_setfl(&hdl->aucat, 1, &hdl->sioctl.eof))
		goto bad;
	hdl->dump_wait = 0;
	return (struct sioctl_hdl *)hdl;
bad:
	free(hdl);
	return NULL;
}

static void
sioctl_aucat_close(struct sioctl_hdl *addr)
{
	struct sioctl_aucat_hdl *hdl = (struct sioctl_aucat_hdl *)addr;

	if (!hdl->sioctl.eof)
		_aucat_setfl(&hdl->aucat, 0, &hdl->sioctl.eof);
	_aucat_close(&hdl->aucat, hdl->sioctl.eof);
	free(hdl);
}

static int
sioctl_aucat_ondesc(struct sioctl_hdl *addr)
{
	struct sioctl_aucat_hdl *hdl = (struct sioctl_aucat_hdl *)addr;

	while (hdl->aucat.wstate != WSTATE_IDLE) {
		if (!_sioctl_psleep(&hdl->sioctl, POLLOUT))
			return 0;
	}
	AMSG_INIT(&hdl->aucat.wmsg);
	hdl->aucat.wmsg.cmd = htonl(AMSG_CTLSUB);
	hdl->aucat.wmsg.u.ctlsub.desc = 1;
	hdl->aucat.wmsg.u.ctlsub.val = 0;
	hdl->aucat.wtodo = sizeof(struct amsg);
	if (!_aucat_wmsg(&hdl->aucat, &hdl->sioctl.eof))
		return 0;
	hdl->dump_wait = 1;
	while (hdl->dump_wait) {
		DPRINTF("psleeping...\n");
		if (!_sioctl_psleep(&hdl->sioctl, 0))
			return 0;
		DPRINTF("psleeping done\n");
	}
	DPRINTF("done\n");
	return 1;
}

static int
sioctl_aucat_onval(struct sioctl_hdl *addr)
{
	struct sioctl_aucat_hdl *hdl = (struct sioctl_aucat_hdl *)addr;

	while (hdl->aucat.wstate != WSTATE_IDLE) {
		if (!_sioctl_psleep(&hdl->sioctl, POLLOUT))
			return 0;
	}
	AMSG_INIT(&hdl->aucat.wmsg);
	hdl->aucat.wmsg.cmd = htonl(AMSG_CTLSUB);
	hdl->aucat.wmsg.u.ctlsub.desc = 1;
	hdl->aucat.wmsg.u.ctlsub.val = 1;
	hdl->aucat.wtodo = sizeof(struct amsg);
	if (!_aucat_wmsg(&hdl->aucat, &hdl->sioctl.eof))
		return 0;
	return 1;
}

static int
sioctl_aucat_setctl(struct sioctl_hdl *addr, unsigned int a, unsigned int v)
{
	struct sioctl_aucat_hdl *hdl = (struct sioctl_aucat_hdl *)addr;

	hdl->aucat.wstate = WSTATE_MSG;
	hdl->aucat.wtodo = sizeof(struct amsg);
	hdl->aucat.wmsg.cmd = htonl(AMSG_CTLSET);
	hdl->aucat.wmsg.u.ctlset.addr = htons(a);
	hdl->aucat.wmsg.u.ctlset.val = htons(v);
	while (hdl->aucat.wstate != WSTATE_IDLE) {
		if (_aucat_wmsg(&hdl->aucat, &hdl->sioctl.eof))
			break;
		if (hdl->sioctl.nbio || !_sioctl_psleep(&hdl->sioctl, POLLOUT))
			return 0;
	}
	return 1;
}

static int
sioctl_aucat_nfds(struct sioctl_hdl *addr)
{
	return 1;
}

static int
sioctl_aucat_pollfd(struct sioctl_hdl *addr, struct pollfd *pfd, int events)
{
	struct sioctl_aucat_hdl *hdl = (struct sioctl_aucat_hdl *)addr;

	return _aucat_pollfd(&hdl->aucat, pfd, events | POLLIN);
}

static int
sioctl_aucat_revents(struct sioctl_hdl *addr, struct pollfd *pfd)
{
	struct sioctl_aucat_hdl *hdl = (struct sioctl_aucat_hdl *)addr;
	int revents;

	revents = _aucat_revents(&hdl->aucat, pfd);
	if (revents & POLLIN) {
		while (1) {
			if (hdl->aucat.rstate == RSTATE_MSG) {
				if (!sioctl_aucat_runmsg(hdl))
					break;
			}
			if (hdl->aucat.rstate == RSTATE_DATA) {
				if (!sioctl_aucat_rdata(hdl))
					break;
			}
		}
		revents &= ~POLLIN;
	}
	if (hdl->sioctl.eof)
		return POLLHUP;
	DPRINTFN(3, "sioctl_aucat_revents: revents = 0x%x\n", revents);
	return revents;
}
