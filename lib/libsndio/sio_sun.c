/*	$OpenBSD: sio_sun.c,v 1.30 2022/12/27 17:10:07 jmc Exp $	*/
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
#include <sys/ioctl.h>
#include <sys/audioio.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "sio_priv.h"

#define DEVPATH_PREFIX	"/dev/audio"
#define DEVPATH_MAX 	(1 +		\
	sizeof(DEVPATH_PREFIX) - 1 +	\
	sizeof(int) * 3)

struct sio_sun_hdl {
	struct sio_hdl sio;
	int fd;
	int filling;
	unsigned int ibpf, obpf;	/* bytes per frame */
	unsigned int ibytes, obytes;	/* bytes the hw transferred */
	unsigned int ierr, oerr;	/* frames the hw dropped */
	int idelta, odelta;		/* position reported to client */
};

static void sio_sun_close(struct sio_hdl *);
static int sio_sun_start(struct sio_hdl *);
static int sio_sun_flush(struct sio_hdl *);
static int sio_sun_setpar(struct sio_hdl *, struct sio_par *);
static int sio_sun_getpar(struct sio_hdl *, struct sio_par *);
static int sio_sun_getcap(struct sio_hdl *, struct sio_cap *);
static size_t sio_sun_read(struct sio_hdl *, void *, size_t);
static size_t sio_sun_write(struct sio_hdl *, const void *, size_t);
static int sio_sun_nfds(struct sio_hdl *);
static int sio_sun_pollfd(struct sio_hdl *, struct pollfd *, int);
static int sio_sun_revents(struct sio_hdl *, struct pollfd *);

static struct sio_ops sio_sun_ops = {
	sio_sun_close,
	sio_sun_setpar,
	sio_sun_getpar,
	sio_sun_getcap,
	sio_sun_write,
	sio_sun_read,
	sio_sun_start,
	NULL,
	sio_sun_flush,
	sio_sun_nfds,
	sio_sun_pollfd,
	sio_sun_revents,
	NULL, /* setvol */
	NULL, /* getvol */
};

static int
sio_sun_adjpar(struct sio_sun_hdl *hdl, struct audio_swpar *ap)
{
	if (hdl->sio.eof)
		return 0;
	if (ioctl(hdl->fd, AUDIO_SETPAR, ap) == -1) {
		DPERROR("AUDIO_SETPAR");
		hdl->sio.eof = 1;
		return 0;
	}
	if (ioctl(hdl->fd, AUDIO_GETPAR, ap) == -1) {
		DPERROR("AUDIO_GETPAR");
		hdl->sio.eof = 1;
		return 0;
	}
	return 1;
}

/*
 * try to set the device to the given parameters and check that the
 * device can use them; return 1 on success, 0 on failure or error
 */
static int
sio_sun_testpar(struct sio_sun_hdl *hdl, struct sio_enc *enc,
    unsigned int pchan, unsigned int rchan, unsigned int rate)
{
	struct audio_swpar ap;

	AUDIO_INITPAR(&ap);
	if (enc != NULL) {
		ap.sig = enc->sig;
		ap.bits = enc->bits;
		ap.bps = enc->bps;
		if (ap.bps > 1)
			ap.le = enc->le;
		if (ap.bps * 8 > ap.bits)
			ap.msb = enc->msb;
	}
	if (rate)
		ap.rate = rate;
	if (pchan && (hdl->sio.mode & SIO_PLAY))
		ap.pchan = pchan;
	if (rchan && (hdl->sio.mode & SIO_REC))
		ap.rchan = rchan;
	if (!sio_sun_adjpar(hdl, &ap))
		return 0;
	if (pchan && ap.pchan != pchan)
		return 0;
	if (rchan && ap.rchan != rchan)
		return 0;
	if (rate && ap.rate != rate)
		return 0;
	if (enc) {
		if (ap.sig != enc->sig)
			return 0;
		if (ap.bits != enc->bits)
			return 0;
		if (ap.bps != enc->bps)
			return 0;
		if (ap.bps > 1 && ap.le != enc->le)
			return 0;
		if (ap.bits < ap.bps * 8 && ap.msb != enc->msb)
			return 0;
	}
	return 1;
}

/*
 * guess device capabilities
 */
static int
sio_sun_getcap(struct sio_hdl *sh, struct sio_cap *cap)
{
	static unsigned int chans[] = {
		1, 2, 4, 6, 8, 10, 12
	};
	static unsigned int rates[] = {
		8000, 11025, 12000, 16000, 22050, 24000,
		32000, 44100, 48000, 64000, 88200, 96000
	};
	static unsigned int encs[] = {
		8, 16, 24, 32
	};
	struct sio_sun_hdl *hdl = (struct sio_sun_hdl *)sh;
	struct audio_swpar savepar, ap;
	unsigned int nconf = 0;
	unsigned int enc_map = 0, rchan_map = 0, pchan_map = 0, rate_map;
	unsigned int i, j, conf;

	if (ioctl(hdl->fd, AUDIO_GETPAR, &savepar) == -1) {
		DPERROR("AUDIO_GETPAR");
		hdl->sio.eof = 1;
		return 0;
	}

	/*
	 * get a subset of supported encodings
	 */
	for (i = 0; i < sizeof(encs) / sizeof(encs[0]); i++) {
		AUDIO_INITPAR(&ap);
		ap.bits = encs[i];
		ap.sig = (ap.bits > 8) ? 1 : 0;
		if (!sio_sun_adjpar(hdl, &ap))
			return 0;
		if (ap.bits == encs[i]) {
			cap->enc[i].sig = ap.sig;
			cap->enc[i].bits = ap.bits;
			cap->enc[i].le = ap.le;
			cap->enc[i].bps = ap.bps;
			cap->enc[i].msb = ap.msb;
			enc_map |= 1 << i;
		}
	}

	/*
	 * fill channels
	 *
	 * for now we're lucky: all kernel devices assume that the
	 * number of channels and the encoding are independent so we can
	 * use the current encoding and try various channels.
	 */
	if (hdl->sio.mode & SIO_PLAY) {
		for (i = 0; i < sizeof(chans) / sizeof(chans[0]); i++) {
			AUDIO_INITPAR(&ap);
			ap.pchan = chans[i];
			if (!sio_sun_adjpar(hdl, &ap))
				return 0;
			if (ap.pchan == chans[i]) {
				cap->pchan[i] = chans[i];
				pchan_map |= (1 << i);
			}
		}
	}
	if (hdl->sio.mode & SIO_REC) {
		for (i = 0; i < sizeof(chans) / sizeof(chans[0]); i++) {
			AUDIO_INITPAR(&ap);
			ap.pchan = chans[i];
			if (!sio_sun_adjpar(hdl, &ap))
				return 0;
			if (ap.rchan == chans[i]) {
				cap->rchan[i] = chans[i];
				rchan_map |= (1 << i);
			}
		}
	}

	/*
	 * fill rates
	 *
	 * rates are not independent from other parameters (eg. on
	 * uaudio devices), so certain rates may not be allowed with
	 * certain encodings. We have to check rates for all encodings
	 */
	for (j = 0; j < sizeof(encs) / sizeof(encs[0]); j++) {
		rate_map = 0;
		if ((enc_map & (1 << j)) == 0)
			continue;
		for (i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
			if (sio_sun_testpar(hdl,
				&cap->enc[j], 0, 0, rates[i])) {
				cap->rate[i] = rates[i];
				rate_map |= (1 << i);
			}
		}
		for (conf = 0; conf < nconf; conf++) {
			if (cap->confs[conf].rate == rate_map) {
				cap->confs[conf].enc |= (1 << j);
				break;
			}
		}
		if (conf == nconf) {
			if (nconf == SIO_NCONF)
				break;
			cap->confs[nconf].enc = (1 << j);
			cap->confs[nconf].pchan = pchan_map;
			cap->confs[nconf].rchan = rchan_map;
			cap->confs[nconf].rate = rate_map;
			nconf++;
		}
	}
	cap->nconf = nconf;

	if (ioctl(hdl->fd, AUDIO_SETPAR, &savepar) == -1) {
		DPERROR("AUDIO_SETPAR");
		hdl->sio.eof = 1;
		return 0;
	}
	return 1;
}

int
sio_sun_getfd(const char *str, unsigned int mode, int nbio)
{
	const char *p;
	char path[DEVPATH_MAX];
	unsigned int devnum;
	int fd, flags;

#ifdef DEBUG
	_sndio_debug_init();
#endif
	p = _sndio_parsetype(str, "rsnd");
	if (p == NULL) {
		DPRINTF("sio_sun_getfd: %s: \"rsnd\" expected\n", str);
		return -1;
	}
	switch (*p) {
	case '/':
		p++;
		break;
	default:
		DPRINTF("sio_sun_getfd: %s: '/' expected\n", str);
		return -1;
	}
	p = _sndio_parsenum(p, &devnum, 255);
	if (p == NULL || *p != '\0') {
		DPRINTF("sio_sun_getfd: %s: number expected after '/'\n", str);
		return -1;
	}
	snprintf(path, sizeof(path), DEVPATH_PREFIX "%u", devnum);
	if (mode == (SIO_PLAY | SIO_REC))
		flags = O_RDWR;
	else
		flags = (mode & SIO_PLAY) ? O_WRONLY : O_RDONLY;
	while ((fd = open(path, flags | O_NONBLOCK | O_CLOEXEC)) == -1) {
		if (errno == EINTR)
			continue;
		DPERROR(path);
		return -1;
	}
	return fd;
}

struct sio_hdl *
sio_sun_fdopen(int fd, unsigned int mode, int nbio)
{
	struct sio_sun_hdl *hdl;

#ifdef DEBUG
	_sndio_debug_init();
#endif
	hdl = malloc(sizeof(struct sio_sun_hdl));
	if (hdl == NULL)
		return NULL;
	_sio_create(&hdl->sio, &sio_sun_ops, mode, nbio);

	/*
	 * pause the device
	 */
	if (ioctl(fd, AUDIO_STOP) == -1) {
		DPERROR("AUDIO_STOP");
		free(hdl);
		return NULL;
	}
	hdl->fd = fd;
	hdl->filling = 0;
	return (struct sio_hdl *)hdl;
}

struct sio_hdl *
_sio_sun_open(const char *str, unsigned int mode, int nbio)
{
	struct sio_hdl *hdl;
	int fd;

	fd = sio_sun_getfd(str, mode, nbio);
	if (fd == -1)
		return NULL;
	hdl = sio_sun_fdopen(fd, mode, nbio);
	if (hdl != NULL)
		return hdl;
	while (close(fd) == -1 && errno == EINTR)
		; /* retry */
	return NULL;
}

static void
sio_sun_close(struct sio_hdl *sh)
{
	struct sio_sun_hdl *hdl = (struct sio_sun_hdl *)sh;

	while (close(hdl->fd) == -1 && errno == EINTR)
		; /* retry */
	free(hdl);
}

static int
sio_sun_start(struct sio_hdl *sh)
{
	struct sio_sun_hdl *hdl = (struct sio_sun_hdl *)sh;

	hdl->obpf = hdl->sio.par.pchan * hdl->sio.par.bps;
	hdl->ibpf = hdl->sio.par.rchan * hdl->sio.par.bps;
	hdl->ibytes = 0;
	hdl->obytes = 0;
	hdl->ierr = 0;
	hdl->oerr = 0;
	hdl->idelta = 0;
	hdl->odelta = 0;

	if (hdl->sio.mode & SIO_PLAY) {
		/*
		 * keep the device paused and let sio_sun_pollfd() trigger the
		 * start later, to avoid buffer underruns
		 */
		hdl->filling = 1;
	} else {
		/*
		 * no play buffers to fill, start now!
		 */
		if (ioctl(hdl->fd, AUDIO_START) == -1) {
			DPERROR("AUDIO_START");
			hdl->sio.eof = 1;
			return 0;
		}
		_sio_onmove_cb(&hdl->sio, 0);
	}
	return 1;
}

static int
sio_sun_flush(struct sio_hdl *sh)
{
	struct sio_sun_hdl *hdl = (struct sio_sun_hdl *)sh;

	if (hdl->filling) {
		hdl->filling = 0;
		return 1;
	}
	if (ioctl(hdl->fd, AUDIO_STOP) == -1) {
		DPERROR("AUDIO_STOP");
		hdl->sio.eof = 1;
		return 0;
	}
	return 1;
}

static int
sio_sun_setpar(struct sio_hdl *sh, struct sio_par *par)
{
	struct sio_sun_hdl *hdl = (struct sio_sun_hdl *)sh;
	struct audio_swpar ap;

	AUDIO_INITPAR(&ap);
	ap.sig = par->sig;
	ap.le = par->le;
	ap.bits = par->bits;
	ap.bps = par->bps;
	ap.msb = par->msb;
	ap.rate = par->rate;
	if (hdl->sio.mode & SIO_PLAY)
		ap.pchan = par->pchan;
	if (hdl->sio.mode & SIO_REC)
		ap.rchan = par->rchan;
	if (par->round != ~0U && par->appbufsz != ~0U) {
		ap.round = par->round;
		ap.nblks = par->appbufsz / par->round;
	} else if (par->round != ~0U) {
		ap.round = par->round;
		ap.nblks = 2;
	} else if (par->appbufsz != ~0U) {
		ap.round = par->appbufsz / 2;
		ap.nblks = 2;
	}
	if (ioctl(hdl->fd, AUDIO_SETPAR, &ap) == -1) {
		DPERROR("AUDIO_SETPAR");
		hdl->sio.eof = 1;
		return 0;
	}
	return 1;
}

static int
sio_sun_getpar(struct sio_hdl *sh, struct sio_par *par)
{
	struct sio_sun_hdl *hdl = (struct sio_sun_hdl *)sh;
	struct audio_swpar ap;

	if (ioctl(hdl->fd, AUDIO_GETPAR, &ap) == -1) {
		DPERROR("AUDIO_GETPAR");
		hdl->sio.eof = 1;
		return 0;
	}
	par->sig = ap.sig;
	par->le = ap.le;
	par->bits = ap.bits;
	par->bps = ap.bps;
	par->msb = ap.msb;
	par->rate = ap.rate;
	par->pchan = ap.pchan;
	par->rchan = ap.rchan;
	par->round = ap.round;
	par->appbufsz = par->bufsz = ap.nblks * ap.round;
	par->xrun = SIO_IGNORE;
	return 1;
}

static size_t
sio_sun_read(struct sio_hdl *sh, void *buf, size_t len)
{
	struct sio_sun_hdl *hdl = (struct sio_sun_hdl *)sh;
	ssize_t n;

	while ((n = read(hdl->fd, buf, len)) == -1) {
		if (errno == EINTR)
			continue;
		if (errno != EAGAIN) {
			DPERROR("sio_sun_read: read");
			hdl->sio.eof = 1;
		}
		return 0;
	}
	if (n == 0) {
		DPRINTF("sio_sun_read: eof\n");
		hdl->sio.eof = 1;
		return 0;
	}
	return n;
}

static size_t
sio_sun_write(struct sio_hdl *sh, const void *buf, size_t len)
{
	struct sio_sun_hdl *hdl = (struct sio_sun_hdl *)sh;
	const unsigned char *data = buf;
	ssize_t n, todo;

	todo = len;
	while ((n = write(hdl->fd, data, todo)) == -1) {
		if (errno == EINTR)
			continue;
		if (errno != EAGAIN) {
			DPERROR("sio_sun_write: write");
			hdl->sio.eof = 1;
		}
		return 0;
	}
	return n;
}

static int
sio_sun_nfds(struct sio_hdl *hdl)
{
	return 1;
}

static int
sio_sun_pollfd(struct sio_hdl *sh, struct pollfd *pfd, int events)
{
	struct sio_sun_hdl *hdl = (struct sio_sun_hdl *)sh;

	pfd->fd = hdl->fd;
	pfd->events = events;
	if (hdl->filling && hdl->sio.wused == hdl->sio.par.bufsz *
		hdl->sio.par.pchan * hdl->sio.par.bps) {
		hdl->filling = 0;
		if (ioctl(hdl->fd, AUDIO_START) == -1) {
			DPERROR("AUDIO_START");
			hdl->sio.eof = 1;
			return 0;
		}
		_sio_onmove_cb(&hdl->sio, 0);
	}
	return 1;
}

int
sio_sun_revents(struct sio_hdl *sh, struct pollfd *pfd)
{
	struct sio_sun_hdl *hdl = (struct sio_sun_hdl *)sh;
	struct audio_pos ap;
	int dierr = 0, doerr = 0, offset, delta;
	int revents = pfd->revents;

	if ((pfd->revents & POLLHUP) ||
	    (pfd->revents & (POLLIN | POLLOUT)) == 0)
		return pfd->revents;
	if (ioctl(hdl->fd, AUDIO_GETPOS, &ap) == -1) {
		DPERROR("sio_sun_revents: GETPOS");
		hdl->sio.eof = 1;
		return POLLHUP;
	}
	if (hdl->sio.mode & SIO_PLAY) {
		delta = (ap.play_pos - hdl->obytes) / hdl->obpf;
		doerr = (ap.play_xrun - hdl->oerr) / hdl->obpf;
		hdl->obytes = ap.play_pos;
		hdl->oerr = ap.play_xrun;
		hdl->odelta += delta;
		if (!(hdl->sio.mode & SIO_REC)) {
			hdl->idelta += delta;
			dierr = doerr;
		}
		if (doerr > 0)
			DPRINTFN(2, "play xrun %d\n", doerr);
	}
	if (hdl->sio.mode & SIO_REC) {
		delta = (ap.rec_pos - hdl->ibytes) / hdl->ibpf;
		dierr = (ap.rec_xrun - hdl->ierr) / hdl->ibpf;
		hdl->ibytes = ap.rec_pos;
		hdl->ierr = ap.rec_xrun;
		hdl->idelta += delta;
		if (!(hdl->sio.mode & SIO_PLAY)) {
			hdl->odelta += delta;
			doerr = dierr;
		}
		if (dierr > 0)
			DPRINTFN(2, "rec xrun %d\n", dierr);
	}

	/*
	 * GETPOS reports positions including xruns,
	 * so we have to subtract to get the real position
	 */
	hdl->idelta -= dierr;
	hdl->odelta -= doerr;

	offset = doerr - dierr;
	if (offset > 0) {
		hdl->sio.rdrop += offset * hdl->ibpf;
		hdl->idelta -= offset;
		DPRINTFN(2, "will drop %d and pause %d\n", offset, doerr);
	} else if (offset < 0) {
		hdl->sio.wsil += -offset * hdl->obpf;
		hdl->odelta -= -offset;
		DPRINTFN(2, "will insert %d and pause %d\n", -offset, dierr);
	}

	delta = (hdl->idelta > hdl->odelta) ? hdl->idelta : hdl->odelta;
	if (delta > 0) {
		_sio_onmove_cb(&hdl->sio, delta);
		hdl->idelta -= delta;
		hdl->odelta -= delta;
	}
	return revents;
}
