/*	$OpenBSD: ossaudio.c,v 1.21 2020/04/02 19:57:10 ratchov Exp $	*/
/*	$NetBSD: ossaudio.c,v 1.14 2001/05/10 01:53:48 augustss Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This is an OSS (Linux) sound API emulator.
 * It provides the essentials of the API.
 */

#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <poll.h>
#include <sndio.h>
#include <stdlib.h>
#include <stdio.h>
#include "soundcard.h"

#ifdef DEBUG
#define DPRINTF(...) do { fprintf(stderr, __VA_ARGS__); } while (0)
#else
#define DPRINTF(...) do {} while (0)
#endif

#define GET_DEV(com) ((com) & 0xff)
#define INTARG (*(int*)argp)

struct control {
	struct control *next;
	int type;		/* one of SOUND_MIXER_xxx */
	int chan;		/* 0 -> left, 1 -> right, -1 -> mono */
	int addr;		/* sioctl control id */
	int value;		/* current value */
	int max;
};

static int mixer_ioctl(int, unsigned long, void *);

static int initialized;
static struct control *controls;
static struct sioctl_hdl *hdl;
static char *dev_name = SIO_DEVANY;
static struct pollfd *pfds;

int
_oss_ioctl(int fd, unsigned long com, ...)
{
	va_list ap;
	void *argp;

	va_start(ap, com);
	argp = va_arg(ap, void *);
	va_end(ap);
	if (IOCGROUP(com) == 'P')
		return ENOTTY;
	else if (IOCGROUP(com) == 'M')
		return mixer_ioctl(fd, com, argp);
	else
		return (ioctl)(fd, com, argp);
}

/*
 * new control
 */
static void
mixer_ondesc(void *unused, struct sioctl_desc *d, int val)
{
	struct control *i, **pi;
	int type;

	if (d == NULL)
		return;

	/*
	 * delete existing control with the same address
	 */
	for (pi = &controls; (i = *pi) != NULL; pi = &i->next) {
		if (d->addr == i->addr) {
			*pi = i->next;
			free(i);
			break;
		}
	}

	/*
	 * we support only numeric "level" controls, first 2 channels
	 */
	if (d->type != SIOCTL_NUM || d->node0.unit >= 2 ||
	    strcmp(d->func, "level") != 0)
		return;

	/*
	 * We expose top-level input.level and output.level as OSS
	 * volume and microphone knobs. By default sndiod exposes
	 * the underlying hardware knobs as hw/input.level and
	 * hw/output.level that we map to OSS gain controls. This
	 * ensures useful knobs are exposed no matter if sndiod
	 * is running or not.
	 */ 
	if (d->group[0] == 0) {
		if (strcmp(d->node0.name, "output") == 0)
			type = SOUND_MIXER_VOLUME;
		else if (strcmp(d->node0.name, "input") == 0)
			type = SOUND_MIXER_MIC;
		else
			return;
	} else if (strcmp(d->group, "hw") == 0) {
		if (strcmp(d->node0.name, "output") == 0)
			type = SOUND_MIXER_OGAIN;
		else if (strcmp(d->node0.name, "input") == 0)
			type = SOUND_MIXER_IGAIN;
		else
			return;
	} else
		return;

	i = malloc(sizeof(struct control));
	if (i == NULL) {
		DPRINTF("%s: cannot allocate control\n", __func__);		
		return;
	}

	i->addr = d->addr;
	i->chan = d->node0.unit;
	i->max = d->maxval;
	i->value = val;
	i->type = type;
	i->next = controls;
	controls = i;
	DPRINTF("%s: %d: used as %d, chan = %d, value = %d\n", __func__,
	    i->addr, i->type, i->chan, i->value);
}

/*
 * control value changed
 */
static void
mixer_onval(void *unused, unsigned int addr, unsigned int value)
{
	struct control *c;

	for (c = controls; ; c = c->next) {
		if (c == NULL) {
			DPRINTF("%s: %d: change ignored\n", __func__, addr);
			return;
		}
		if (c->addr == addr)
			break;
	}

	DPRINTF("%s: %d: changed to %d\n", __func__, addr, value);
	c->value = value;
}

static int
mixer_init(void)
{
	if (initialized)
		return hdl != NULL;

	initialized = 1;

	hdl = sioctl_open(dev_name, SIOCTL_READ | SIOCTL_WRITE, 0);
	if (hdl == NULL) {
		DPRINTF("%s: cannot open audio control device\n", __func__);
		return 0;
	}

	pfds = calloc(sioctl_nfds(hdl), sizeof(struct pollfd));
	if (pfds == NULL) {
		DPRINTF("%s: cannot allocate pfds\n", __func__);
		goto bad_close;
	}

	if (!sioctl_ondesc(hdl, mixer_ondesc, NULL)) {
		DPRINTF("%s: cannot get controls descriptions\n", __func__);
		goto bad_free;
	}

	if (!sioctl_onval(hdl, mixer_onval, NULL)) {
		DPRINTF("%s: cannot get controls values\n", __func__);
		goto bad_free;
	}

	return 1;

bad_free:
	free(pfds);
bad_close:
	sioctl_close(hdl);
	return 0;
}

static int
mixer_ioctl(int fd, unsigned long com, void *argp)
{
	struct control *c;
	struct mixer_info *omi;
	int idat = 0;
	int v, n;

	if (!mixer_init()) {
		DPRINTF("%s: not initialized\n", __func__);
		errno = EIO;
		return -1;
	}

	n = sioctl_pollfd(hdl, pfds, POLLIN);
	if (n > 0) {
		n = poll(pfds, n, 0);
		if (n == -1)
			return -1;
		if (n > 0)
			sioctl_revents(hdl, pfds);
	}

	switch (com) {
	case OSS_GETVERSION:
		idat = SOUND_VERSION;
		break;
	case SOUND_MIXER_INFO:
	case SOUND_OLD_MIXER_INFO:
		omi = argp;
		if (com == SOUND_MIXER_INFO)
			omi->modify_counter = 1;
		strlcpy(omi->id, dev_name, sizeof omi->id);
		strlcpy(omi->name, dev_name, sizeof omi->name);
		return 0;
	case SOUND_MIXER_READ_RECSRC:
	case SOUND_MIXER_READ_RECMASK:
		idat = 0;
		for (c = controls; c != NULL; c = c->next)
			idat |= 1 << c->type;
		idat &= (1 << SOUND_MIXER_MIC) | (1 << SOUND_MIXER_IGAIN);
		DPRINTF("%s: SOUND_MIXER_READ_RECSRC: %d\n", __func__, idat);
		break;
	case SOUND_MIXER_READ_DEVMASK:
		idat = 0;
		for (c = controls; c != NULL; c = c->next)
			idat |= 1 << c->type;
		DPRINTF("%s: SOUND_MIXER_READ_DEVMASK: %d\n", __func__, idat);
		break;
	case SOUND_MIXER_READ_STEREODEVS:
		idat = 0;
		for (c = controls; c != NULL; c = c->next) {
			if (c->chan == 1)
				idat |= 1 << c->type;
		}
		DPRINTF("%s: SOUND_MIXER_STEREODEVS: %d\n", __func__, idat);
		break;
	case SOUND_MIXER_READ_CAPS:
		idat = 0;
		DPRINTF("%s: SOUND_MIXER_READ_CAPS: %d\n", __func__, idat);
		break;
	case SOUND_MIXER_WRITE_RECSRC:
	case SOUND_MIXER_WRITE_R_RECSRC:
		DPRINTF("%s: SOUND_MIXER_WRITE_RECSRC\n", __func__);
		errno = EINVAL;
		return -1;
	default:
		if (MIXER_READ(SOUND_MIXER_FIRST) <= com &&
		    com < MIXER_READ(SOUND_MIXER_NRDEVICES)) {
		doread:
			idat = 0;
			n = GET_DEV(com);
			for (c = controls; c != NULL; c = c->next) {
				if (c->type != n)
					continue;
				v = (c->value * 100 + c->max / 2) / c->max;
				if (c->chan == 1)
					v <<= 8;
				idat |= v;
			}
			DPRINTF("%s: MIXER_READ: %d: 0x%04x\n",
			    __func__, n, idat);
			break;
		} else if ((MIXER_WRITE_R(SOUND_MIXER_FIRST) <= com &&
			   com < MIXER_WRITE_R(SOUND_MIXER_NRDEVICES)) ||
			   (MIXER_WRITE(SOUND_MIXER_FIRST) <= com &&
			   com < MIXER_WRITE(SOUND_MIXER_NRDEVICES))) {
			idat = INTARG;
			n = GET_DEV(com);
			for (c = controls; c != NULL; c = c->next) {
				if (c->type != n)
					continue;
				v = idat;
				if (c->chan == 1)
					v >>= 8;
				v &= 0xff;
				if (v > 100)
					v = 100;
				v = (v * c->max + 50) / 100;
				sioctl_setval(hdl, c->addr, v);
				DPRINTF("%s: MIXER_WRITE: %d: %d\n",
				    __func__, n, v);
			}
			if (MIXER_WRITE(SOUND_MIXER_FIRST) <= com &&
			   com < MIXER_WRITE(SOUND_MIXER_NRDEVICES))
				return 0;
			goto doread;
		} else {
			errno = EINVAL;
			return -1;
		}
	}

	INTARG = idat;
	return 0;
}
