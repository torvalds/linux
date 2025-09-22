/*	$OpenBSD: sio_aucat.c,v 1.21 2022/04/29 08:30:48 ratchov Exp $	*/
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
#include "sio_priv.h"

struct sio_aucat_hdl {
	struct sio_hdl sio;
	struct aucat aucat;
	unsigned int rbpf, wbpf;	/* read and write bytes-per-frame */
	int events;			/* events the user requested */
	unsigned int curvol, reqvol;	/* current and requested volume */
	int delta;			/* some of received deltas */
#define PSTATE_INIT	0
#define PSTATE_RUN	1
	int pstate;
	size_t round;	       		/* write block size */
	size_t walign;			/* align write packets size to this */
};

static void sio_aucat_close(struct sio_hdl *);
static int sio_aucat_start(struct sio_hdl *);
static int sio_aucat_stop(struct sio_hdl *);
static int sio_aucat_flush(struct sio_hdl *);
static int sio_aucat_setpar(struct sio_hdl *, struct sio_par *);
static int sio_aucat_getpar(struct sio_hdl *, struct sio_par *);
static int sio_aucat_getcap(struct sio_hdl *, struct sio_cap *);
static size_t sio_aucat_read(struct sio_hdl *, void *, size_t);
static size_t sio_aucat_write(struct sio_hdl *, const void *, size_t);
static int sio_aucat_nfds(struct sio_hdl *);
static int sio_aucat_pollfd(struct sio_hdl *, struct pollfd *, int);
static int sio_aucat_revents(struct sio_hdl *, struct pollfd *);
static int sio_aucat_setvol(struct sio_hdl *, unsigned int);
static void sio_aucat_getvol(struct sio_hdl *);

static struct sio_ops sio_aucat_ops = {
	sio_aucat_close,
	sio_aucat_setpar,
	sio_aucat_getpar,
	sio_aucat_getcap,
	sio_aucat_write,
	sio_aucat_read,
	sio_aucat_start,
	sio_aucat_stop,
	sio_aucat_flush,
	sio_aucat_nfds,
	sio_aucat_pollfd,
	sio_aucat_revents,
	sio_aucat_setvol,
	sio_aucat_getvol
};

/*
 * execute the next message, return 0 if blocked
 */
static int
sio_aucat_runmsg(struct sio_aucat_hdl *hdl)
{
	int delta;
	unsigned int size, ctl;

	if (!_aucat_rmsg(&hdl->aucat, &hdl->sio.eof))
		return 0;
	switch (ntohl(hdl->aucat.rmsg.cmd)) {
	case AMSG_DATA:
		size = ntohl(hdl->aucat.rmsg.u.data.size);
		if (size == 0 || size % hdl->rbpf) {
			DPRINTF("sio_aucat_runmsg: bad data message\n");
			hdl->sio.eof = 1;
			return 0;
		}
		DPRINTFN(3, "aucat: data(%d)\n", size);
		return 1;
	case AMSG_FLOWCTL:
		delta = ntohl(hdl->aucat.rmsg.u.ts.delta);
		hdl->aucat.maxwrite += delta * (int)hdl->wbpf;
		DPRINTFN(3, "aucat: flowctl(%d), maxwrite = %d\n",
		    delta, hdl->aucat.maxwrite);
		break;
	case AMSG_MOVE:
		delta = ntohl(hdl->aucat.rmsg.u.ts.delta);
		hdl->delta += delta;
		DPRINTFN(3, "aucat: move(%d), delta = %d, maxwrite = %d\n",
		    delta, hdl->delta, hdl->aucat.maxwrite);
		if (hdl->delta >= 0) {
			_sio_onmove_cb(&hdl->sio, hdl->delta);
			hdl->delta = 0;
		}
		break;
	case AMSG_SETVOL:
		ctl = ntohl(hdl->aucat.rmsg.u.vol.ctl);
		hdl->curvol = hdl->reqvol = ctl;
		DPRINTFN(3, "aucat: setvol(%d)\n", ctl);
		_sio_onvol_cb(&hdl->sio, ctl);
		break;
	case AMSG_STOP:
		DPRINTFN(3, "aucat: stop()\n");
		hdl->pstate = PSTATE_INIT;
		break;
	default:
		DPRINTF("sio_aucat_runmsg: unhandled message %u\n",
		    hdl->aucat.rmsg.cmd);
		hdl->sio.eof = 1;
		return 0;
	}
	hdl->aucat.rstate = RSTATE_MSG;
	hdl->aucat.rtodo = sizeof(struct amsg);
	return 1;
}

static int
sio_aucat_buildmsg(struct sio_aucat_hdl *hdl)
{
	if (hdl->curvol != hdl->reqvol) {
		hdl->aucat.wstate = WSTATE_MSG;
		hdl->aucat.wtodo = sizeof(struct amsg);
		hdl->aucat.wmsg.cmd = htonl(AMSG_SETVOL);
		hdl->aucat.wmsg.u.vol.ctl = htonl(hdl->reqvol);
		hdl->curvol = hdl->reqvol;
		return _aucat_wmsg(&hdl->aucat, &hdl->sio.eof);
	}
	return 0;
}

struct sio_hdl *
_sio_aucat_open(const char *str, unsigned int mode, int nbio)
{
	struct sio_aucat_hdl *hdl;

	hdl = malloc(sizeof(struct sio_aucat_hdl));
	if (hdl == NULL)
		return NULL;
	if (!_aucat_open(&hdl->aucat, str, mode)) {
		free(hdl);
		return NULL;
	}
	_sio_create(&hdl->sio, &sio_aucat_ops, mode, nbio);
	hdl->curvol = SIO_MAXVOL;
	hdl->reqvol = SIO_MAXVOL;
	hdl->pstate = PSTATE_INIT;
	hdl->round = 0xdeadbeef;
	hdl->walign = 0xdeadbeef;
	return (struct sio_hdl *)hdl;
}

static void
sio_aucat_close(struct sio_hdl *sh)
{
	struct sio_aucat_hdl *hdl = (struct sio_aucat_hdl *)sh;

	if (!hdl->sio.eof && hdl->sio.started)
		(void)sio_aucat_stop(&hdl->sio);
	_aucat_close(&hdl->aucat, hdl->sio.eof);
	free(hdl);
}

static int
sio_aucat_start(struct sio_hdl *sh)
{
	struct sio_aucat_hdl *hdl = (struct sio_aucat_hdl *)sh;

	hdl->wbpf = hdl->sio.par.bps * hdl->sio.par.pchan;
	hdl->rbpf = hdl->sio.par.bps * hdl->sio.par.rchan;
	hdl->aucat.maxwrite = 0;
	hdl->round = hdl->sio.par.round;
	hdl->delta = 0;
	DPRINTFN(2, "aucat: start, maxwrite = %d\n", hdl->aucat.maxwrite);

	AMSG_INIT(&hdl->aucat.wmsg);
	hdl->aucat.wmsg.cmd = htonl(AMSG_START);
	hdl->aucat.wtodo = sizeof(struct amsg);
	if (!_aucat_wmsg(&hdl->aucat, &hdl->sio.eof))
		return 0;
	hdl->aucat.rstate = RSTATE_MSG;
	hdl->aucat.rtodo = sizeof(struct amsg);
	if (!_aucat_setfl(&hdl->aucat, 1, &hdl->sio.eof))
		return 0;
	hdl->walign = hdl->round * hdl->wbpf;
	hdl->pstate = PSTATE_RUN;
	return 1;
}

static int
sio_aucat_drain(struct sio_hdl *sh, int drain)
{
#define ZERO_MAX 0x400
	static unsigned char zero[ZERO_MAX];
	struct sio_aucat_hdl *hdl = (struct sio_aucat_hdl *)sh;
	unsigned int n, count;

	if (!_aucat_setfl(&hdl->aucat, 0, &hdl->sio.eof))
		return 0;
	/*
	 * complete message or data block in progress
	 */
	if (hdl->aucat.wstate == WSTATE_MSG) {
		if (!_aucat_wmsg(&hdl->aucat, &hdl->sio.eof))
			return 0;
	}
	if (hdl->aucat.wstate == WSTATE_DATA) {
		hdl->aucat.maxwrite = hdl->aucat.wtodo;
		while (hdl->aucat.wstate != WSTATE_IDLE) {
			count = hdl->aucat.wtodo;
			if (count > ZERO_MAX)
				count = ZERO_MAX;
			n = sio_aucat_write(&hdl->sio, zero, count);
			if (n == 0)
				return 0;
		}
	}

	/*
	 * send stop message
	 */
	AMSG_INIT(&hdl->aucat.wmsg);
	hdl->aucat.wmsg.cmd = htonl(AMSG_STOP);
	hdl->aucat.wmsg.u.stop.drain = drain;
	hdl->aucat.wtodo = sizeof(struct amsg);
	if (!_aucat_wmsg(&hdl->aucat, &hdl->sio.eof))
		return 0;

	/*
	 * wait for the STOP ACK
	 */
	while (hdl->pstate != PSTATE_INIT) {
		switch (hdl->aucat.rstate) {
		case RSTATE_MSG:
			if (!sio_aucat_runmsg(hdl))
				return 0;
			break;
		case RSTATE_DATA:
			if (!sio_aucat_read(&hdl->sio, zero, ZERO_MAX))
				return 0;
			break;
		}
	}
	return 1;
}

static int
sio_aucat_stop(struct sio_hdl *sh)
{
	return sio_aucat_drain(sh, 1);
}

static int
sio_aucat_flush(struct sio_hdl *sh)
{
	return sio_aucat_drain(sh, 0);
}

static int
sio_aucat_setpar(struct sio_hdl *sh, struct sio_par *par)
{
	struct sio_aucat_hdl *hdl = (struct sio_aucat_hdl *)sh;

	AMSG_INIT(&hdl->aucat.wmsg);
	hdl->aucat.wmsg.cmd = htonl(AMSG_SETPAR);
	hdl->aucat.wmsg.u.par.bits = par->bits;
	hdl->aucat.wmsg.u.par.bps = par->bps;
	hdl->aucat.wmsg.u.par.sig = par->sig;
	hdl->aucat.wmsg.u.par.le = par->le;
	hdl->aucat.wmsg.u.par.msb = par->msb;
	hdl->aucat.wmsg.u.par.rate = htonl(par->rate);
	hdl->aucat.wmsg.u.par.appbufsz = htonl(par->appbufsz);
	hdl->aucat.wmsg.u.par.xrun = par->xrun;
	if (hdl->sio.mode & SIO_REC)
		hdl->aucat.wmsg.u.par.rchan = htons(par->rchan);
	if (hdl->sio.mode & SIO_PLAY)
		hdl->aucat.wmsg.u.par.pchan = htons(par->pchan);
	hdl->aucat.wtodo = sizeof(struct amsg);
	if (!_aucat_wmsg(&hdl->aucat, &hdl->sio.eof))
		return 0;
	return 1;
}

static int
sio_aucat_getpar(struct sio_hdl *sh, struct sio_par *par)
{
	struct sio_aucat_hdl *hdl = (struct sio_aucat_hdl *)sh;

	AMSG_INIT(&hdl->aucat.wmsg);
	hdl->aucat.wmsg.cmd = htonl(AMSG_GETPAR);
	hdl->aucat.wtodo = sizeof(struct amsg);
	if (!_aucat_wmsg(&hdl->aucat, &hdl->sio.eof))
		return 0;
	hdl->aucat.rtodo = sizeof(struct amsg);
	if (!_aucat_rmsg(&hdl->aucat, &hdl->sio.eof))
		return 0;
	if (ntohl(hdl->aucat.rmsg.cmd) != AMSG_GETPAR) {
		DPRINTF("sio_aucat_getpar: protocol err\n");
		hdl->sio.eof = 1;
		return 0;
	}
	par->bits = hdl->aucat.rmsg.u.par.bits;
	par->bps = hdl->aucat.rmsg.u.par.bps;
	par->sig = hdl->aucat.rmsg.u.par.sig;
	par->le = hdl->aucat.rmsg.u.par.le;
	par->msb = hdl->aucat.rmsg.u.par.msb;
	par->rate = ntohl(hdl->aucat.rmsg.u.par.rate);
	par->bufsz = ntohl(hdl->aucat.rmsg.u.par.bufsz);
	par->appbufsz = ntohl(hdl->aucat.rmsg.u.par.appbufsz);
	par->xrun = hdl->aucat.rmsg.u.par.xrun;
	par->round = ntohl(hdl->aucat.rmsg.u.par.round);
	if (hdl->sio.mode & SIO_PLAY)
		par->pchan = ntohs(hdl->aucat.rmsg.u.par.pchan);
	if (hdl->sio.mode & SIO_REC)
		par->rchan = ntohs(hdl->aucat.rmsg.u.par.rchan);
	return 1;
}

static int
sio_aucat_getcap(struct sio_hdl *sh, struct sio_cap *cap)
{
	unsigned int i, bps, le, sig, chan, rindex, rmult;
	static unsigned int rates[] = { 8000, 11025, 12000 };

	bps = 1;
	sig = le = 0;
	cap->confs[0].enc = 0;
	for (i = 0; i < SIO_NENC; i++) {
		if (bps > 4)
			break;
		cap->confs[0].enc |= 1 << i;
		cap->enc[i].bits = bps == 4 ? 24 : bps * 8;
		cap->enc[i].bps = bps;
		cap->enc[i].sig = sig ^ 1;
		cap->enc[i].le = bps > 1 ? le : SIO_LE_NATIVE;
		cap->enc[i].msb = 1;
		le++;
		if (le > 1 || bps == 1) {
			le = 0;
			sig++;
		}
		if (sig > 1 || (le == 0 && bps > 1)) {
			sig = 0;
			bps++;
		}
	}
	chan = 1;
	cap->confs[0].rchan = 0;
	for (i = 0; i < SIO_NCHAN; i++) {
		if (chan > 16)
			break;
		cap->confs[0].rchan |= 1 << i;
		cap->rchan[i] = chan;
		if (chan >= 12) {
			chan += 4;
		} else if (chan >= 2) {
			chan += 2;
		} else
			chan++;
	}
	chan = 1;
	cap->confs[0].pchan = 0;
	for (i = 0; i < SIO_NCHAN; i++) {
		if (chan > 16)
			break;
		cap->confs[0].pchan |= 1 << i;
		cap->pchan[i] = chan;
		if (chan >= 12) {
			chan += 4;
		} else if (chan >= 2) {
			chan += 2;
		} else
			chan++;
	}
	rindex = 0;
	rmult = 1;
	cap->confs[0].rate = 0;
	for (i = 0; i < SIO_NRATE; i++) {
		if (rmult >= 32)
			break;
		cap->rate[i] = rates[rindex] * rmult;
		cap->confs[0].rate |= 1 << i;
		rindex++;
		if (rindex == sizeof(rates) / sizeof(unsigned int)) {
			rindex = 0;
			rmult *= 2;
		}
	}
	cap->nconf = 1;
	return 1;
}

static size_t
sio_aucat_read(struct sio_hdl *sh, void *buf, size_t len)
{
	struct sio_aucat_hdl *hdl = (struct sio_aucat_hdl *)sh;

	while (hdl->aucat.rstate == RSTATE_MSG) {
		if (!sio_aucat_runmsg(hdl))
			return 0;
	}
	return _aucat_rdata(&hdl->aucat, buf, len, &hdl->sio.eof);
}

static size_t
sio_aucat_write(struct sio_hdl *sh, const void *buf, size_t len)
{
	struct sio_aucat_hdl *hdl = (struct sio_aucat_hdl *)sh;
	size_t n;

	while (hdl->aucat.wstate == WSTATE_IDLE) {
		if (!sio_aucat_buildmsg(hdl))
			break;
	}
	if (len <= 0 || hdl->aucat.maxwrite <= 0)
		return 0;
	if (len > hdl->aucat.maxwrite)
		len = hdl->aucat.maxwrite;
	if (len > hdl->walign)
		len = hdl->walign;
	n = _aucat_wdata(&hdl->aucat, buf, len, hdl->wbpf, &hdl->sio.eof);
	hdl->aucat.maxwrite -= n;
	hdl->walign -= n;
	if (hdl->walign == 0)
		hdl->walign = hdl->round * hdl->wbpf;
	return n;
}

static int
sio_aucat_nfds(struct sio_hdl *hdl)
{
	return 1;
 }

static int
sio_aucat_pollfd(struct sio_hdl *sh, struct pollfd *pfd, int events)
{
	struct sio_aucat_hdl *hdl = (struct sio_aucat_hdl *)sh;

	hdl->events = events;
	if (hdl->aucat.maxwrite <= 0)
		events &= ~POLLOUT;
	return _aucat_pollfd(&hdl->aucat, pfd, events);
}

static int
sio_aucat_revents(struct sio_hdl *sh, struct pollfd *pfd)
{
	struct sio_aucat_hdl *hdl = (struct sio_aucat_hdl *)sh;
	int revents = pfd->revents;

	if (revents & POLLIN) {
		while (hdl->aucat.rstate == RSTATE_MSG) {
			if (!sio_aucat_runmsg(hdl))
				break;
		}
		if (hdl->aucat.rstate != RSTATE_DATA)
			revents &= ~POLLIN;
	}
	if (revents & POLLOUT) {
		if (hdl->aucat.maxwrite <= 0)
			revents &= ~POLLOUT;
	}
	if (hdl->sio.eof)
		return POLLHUP;
	DPRINTFN(3, "sio_aucat_revents: %x\n", revents & hdl->events);
	return revents & (hdl->events | POLLHUP);
}

static int
sio_aucat_setvol(struct sio_hdl *sh, unsigned int vol)
{
	struct sio_aucat_hdl *hdl = (struct sio_aucat_hdl *)sh;

	hdl->reqvol = vol;
	return 1;
}

static void
sio_aucat_getvol(struct sio_hdl *sh)
{
	struct sio_aucat_hdl *hdl = (struct sio_aucat_hdl *)sh;

	_sio_onvol_cb(&hdl->sio, hdl->reqvol);
	return;
}
