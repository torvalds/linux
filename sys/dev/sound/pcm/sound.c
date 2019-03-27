/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2009 Ariff Abdullah <ariff@FreeBSD.org>
 * Portions Copyright (c) Ryan Beasley <ryan.beasley@gmail.com> - GSoC 2006
 * Copyright (c) 1999 Cameron Grant <cg@FreeBSD.org>
 * Copyright (c) 1997 Luigi Rizzo
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/ac97.h>
#include <dev/sound/pcm/vchan.h>
#include <dev/sound/pcm/dsp.h>
#include <dev/sound/pcm/sndstat.h>
#include <dev/sound/version.h>
#include <sys/limits.h>
#include <sys/sysctl.h>

#include "feeder_if.h"

SND_DECLARE_FILE("$FreeBSD$");

devclass_t pcm_devclass;

int pcm_veto_load = 1;

int snd_unit = -1;

static int snd_unit_auto = -1;
SYSCTL_INT(_hw_snd, OID_AUTO, default_auto, CTLFLAG_RWTUN,
    &snd_unit_auto, 0, "assign default unit to a newly attached device");

int snd_maxautovchans = 16;

SYSCTL_NODE(_hw, OID_AUTO, snd, CTLFLAG_RD, 0, "Sound driver");

static void pcm_sysinit(device_t);

/*
 * XXX I've had enough with people not telling proper version/arch
 *     while reporting problems, not after 387397913213th questions/requests.
 */
static char snd_driver_version[] =
    __XSTRING(SND_DRV_VERSION)"/"MACHINE_ARCH;
SYSCTL_STRING(_hw_snd, OID_AUTO, version, CTLFLAG_RD, &snd_driver_version,
    0, "driver version/arch");

/**
 * @brief Unit number allocator for syncgroup IDs
 */
struct unrhdr *pcmsg_unrhdr = NULL;

static int
sndstat_prepare_pcm(SNDSTAT_PREPARE_PCM_ARGS)
{
	SNDSTAT_PREPARE_PCM_BEGIN();
	SNDSTAT_PREPARE_PCM_END();
}

void *
snd_mtxcreate(const char *desc, const char *type)
{
	struct mtx *m;

	m = malloc(sizeof(*m), M_DEVBUF, M_WAITOK | M_ZERO);
	mtx_init(m, desc, type, MTX_DEF);
	return m;
}

void
snd_mtxfree(void *m)
{
	struct mtx *mtx = m;

	mtx_destroy(mtx);
	free(mtx, M_DEVBUF);
}

void
snd_mtxassert(void *m)
{
#ifdef INVARIANTS
	struct mtx *mtx = m;

	mtx_assert(mtx, MA_OWNED);
#endif
}

int
snd_setup_intr(device_t dev, struct resource *res, int flags, driver_intr_t hand, void *param, void **cookiep)
{
	struct snddev_info *d;

	flags &= INTR_MPSAFE;
	flags |= INTR_TYPE_AV;
	d = device_get_softc(dev);
	if (d != NULL && (flags & INTR_MPSAFE))
		d->flags |= SD_F_MPSAFE;

	return bus_setup_intr(dev, res, flags, NULL, hand, param, cookiep);
}

static void
pcm_clonereset(struct snddev_info *d)
{
	int cmax;

	PCM_BUSYASSERT(d);

	cmax = d->playcount + d->reccount - 1;
	if (d->pvchancount > 0)
		cmax += max(d->pvchancount, snd_maxautovchans) - 1;
	if (d->rvchancount > 0)
		cmax += max(d->rvchancount, snd_maxautovchans) - 1;
	if (cmax > PCMMAXCLONE)
		cmax = PCMMAXCLONE;
	(void)snd_clone_gc(d->clones);
	(void)snd_clone_setmaxunit(d->clones, cmax);
}

int
pcm_setvchans(struct snddev_info *d, int direction, int newcnt, int num)
{
	struct pcm_channel *c, *ch, *nch;
	struct pcmchan_caps *caps;
	int i, err, vcnt;

	PCM_BUSYASSERT(d);

	if ((direction == PCMDIR_PLAY && d->playcount < 1) ||
	    (direction == PCMDIR_REC && d->reccount < 1))
		return (ENODEV);

	if (!(d->flags & SD_F_AUTOVCHAN))
		return (EINVAL);

	if (newcnt < 0 || newcnt > SND_MAXVCHANS)
		return (E2BIG);

	if (direction == PCMDIR_PLAY)
		vcnt = d->pvchancount;
	else if (direction == PCMDIR_REC)
		vcnt = d->rvchancount;
	else
		return (EINVAL);

	if (newcnt > vcnt) {
		KASSERT(num == -1 ||
		    (num >= 0 && num < SND_MAXVCHANS && (newcnt - 1) == vcnt),
		    ("bogus vchan_create() request num=%d newcnt=%d vcnt=%d",
		    num, newcnt, vcnt));
		/* add new vchans - find a parent channel first */
		ch = NULL;
		CHN_FOREACH(c, d, channels.pcm) {
			CHN_LOCK(c);
			if (c->direction == direction &&
			    ((c->flags & CHN_F_HAS_VCHAN) || (vcnt == 0 &&
			    c->refcount < 1 &&
			    !(c->flags & (CHN_F_BUSY | CHN_F_VIRTUAL))))) {
				/* 
				 * Reuse hw channel with vchans already
				 * created.
				 */
				if (c->flags & CHN_F_HAS_VCHAN) {
					ch = c;
					break;
				}
				/*
				 * No vchans ever created, look for
				 * channels with supported formats.
				 */
				caps = chn_getcaps(c);
				if (caps == NULL) {
					CHN_UNLOCK(c);
					continue;
				}
				for (i = 0; caps->fmtlist[i] != 0; i++) {
					if (caps->fmtlist[i] & AFMT_CONVERTIBLE)
						break;
				}
				if (caps->fmtlist[i] != 0) {
					ch = c;
				    	break;
				}
			}
			CHN_UNLOCK(c);
		}
		if (ch == NULL)
			return (EBUSY);
		ch->flags |= CHN_F_BUSY;
		err = 0;
		while (err == 0 && newcnt > vcnt) {
			err = vchan_create(ch, num);
			if (err == 0)
				vcnt++;
			else if (err == E2BIG && newcnt > vcnt)
				device_printf(d->dev,
				    "%s: err=%d Maximum channel reached.\n",
				    __func__, err);
		}
		if (vcnt == 0)
			ch->flags &= ~CHN_F_BUSY;
		CHN_UNLOCK(ch);
		if (err != 0)
			return (err);
		else
			pcm_clonereset(d);
	} else if (newcnt < vcnt) {
		KASSERT(num == -1,
		    ("bogus vchan_destroy() request num=%d", num));
		CHN_FOREACH(c, d, channels.pcm) {
			CHN_LOCK(c);
			if (c->direction != direction ||
			    CHN_EMPTY(c, children) ||
			    !(c->flags & CHN_F_HAS_VCHAN)) {
				CHN_UNLOCK(c);
				continue;
			}
			CHN_FOREACH_SAFE(ch, c, nch, children) {
				CHN_LOCK(ch);
				if (vcnt == 1 && c->refcount > 0) {
					CHN_UNLOCK(ch);
					break;
				}
				if (!(ch->flags & CHN_F_BUSY) &&
				    ch->refcount < 1) {
					err = vchan_destroy(ch);
					if (err == 0)
						vcnt--;
				} else
					CHN_UNLOCK(ch);
				if (vcnt == newcnt)
					break;
			}
			CHN_UNLOCK(c);
			break;
		}
		pcm_clonereset(d);
	}

	return (0);
}

/* return error status and a locked channel */
int
pcm_chnalloc(struct snddev_info *d, struct pcm_channel **ch, int direction,
    pid_t pid, char *comm, int devunit)
{
	struct pcm_channel *c;
	int err, vchancount, vchan_num;

	KASSERT(d != NULL && ch != NULL && (devunit == -1 ||
	    !(devunit & ~(SND_U_MASK | SND_D_MASK | SND_C_MASK))) &&
	    (direction == PCMDIR_PLAY || direction == PCMDIR_REC),
	    ("%s(): invalid d=%p ch=%p direction=%d pid=%d devunit=%d",
	    __func__, d, ch, direction, pid, devunit));
	PCM_BUSYASSERT(d);

	/* Double check again. */
	if (devunit != -1) {
		switch (snd_unit2d(devunit)) {
		case SND_DEV_DSPHW_PLAY:
		case SND_DEV_DSPHW_VPLAY:
			if (direction != PCMDIR_PLAY)
				return (ENOTSUP);
			break;
		case SND_DEV_DSPHW_REC:
		case SND_DEV_DSPHW_VREC:
			if (direction != PCMDIR_REC)
				return (ENOTSUP);
			break;
		default:
			if (!(direction == PCMDIR_PLAY ||
			    direction == PCMDIR_REC))
				return (ENOTSUP);
			break;
		}
	}

	*ch = NULL;
	vchan_num = 0;
	vchancount = (direction == PCMDIR_PLAY) ? d->pvchancount :
	    d->rvchancount;

retry_chnalloc:
	err = ENOTSUP;
	/* scan for a free channel */
	CHN_FOREACH(c, d, channels.pcm) {
		CHN_LOCK(c);
		if (devunit == -1 && c->direction == direction &&
		    (c->flags & CHN_F_VIRTUAL)) {
			if (vchancount < snd_maxautovchans &&
			    vchan_num < CHN_CHAN(c)) {
			    	CHN_UNLOCK(c);
				goto vchan_alloc;
			}
			vchan_num++;
		}
		if (c->direction == direction && !(c->flags & CHN_F_BUSY) &&
		    (devunit == -1 || devunit == -2 || c->unit == devunit)) {
			c->flags |= CHN_F_BUSY;
			c->pid = pid;
			strlcpy(c->comm, (comm != NULL) ? comm :
			    CHN_COMM_UNKNOWN, sizeof(c->comm));
			*ch = c;
			return (0);
		} else if (c->unit == devunit) {
			if (c->direction != direction)
				err = ENOTSUP;
			else if (c->flags & CHN_F_BUSY)
				err = EBUSY;
			else
				err = EINVAL;
			CHN_UNLOCK(c);
			return (err);
		} else if ((devunit == -1 || devunit == -2) &&
		    c->direction == direction && (c->flags & CHN_F_BUSY))
			err = EBUSY;
		CHN_UNLOCK(c);
	}

	if (devunit == -2)
		return (err);

vchan_alloc:
	/* no channel available */
	if (devunit == -1 || snd_unit2d(devunit) == SND_DEV_DSPHW_VPLAY ||
	    snd_unit2d(devunit) == SND_DEV_DSPHW_VREC) {
		if (!(vchancount > 0 && vchancount < snd_maxautovchans) &&
		    (devunit == -1 || snd_unit2c(devunit) < snd_maxautovchans))
			return (err);
		err = pcm_setvchans(d, direction, vchancount + 1,
		    (devunit == -1) ? -1 : snd_unit2c(devunit));
		if (err == 0) {
			if (devunit == -1)
				devunit = -2;
			goto retry_chnalloc;
		}
	}

	return (err);
}

/* release a locked channel and unlock it */
int
pcm_chnrelease(struct pcm_channel *c)
{
	PCM_BUSYASSERT(c->parentsnddev);
	CHN_LOCKASSERT(c);

	c->flags &= ~CHN_F_BUSY;
	c->pid = -1;
	strlcpy(c->comm, CHN_COMM_UNUSED, sizeof(c->comm));
	CHN_UNLOCK(c);

	return (0);
}

int
pcm_chnref(struct pcm_channel *c, int ref)
{
	PCM_BUSYASSERT(c->parentsnddev);
	CHN_LOCKASSERT(c);

	c->refcount += ref;

	return (c->refcount);
}

int
pcm_inprog(struct snddev_info *d, int delta)
{
	PCM_LOCKASSERT(d);

	d->inprog += delta;

	return (d->inprog);
}

static void
pcm_setmaxautovchans(struct snddev_info *d, int num)
{
	PCM_BUSYASSERT(d);

	if (num < 0)
		return;

	if (num >= 0 && d->pvchancount > num)
		(void)pcm_setvchans(d, PCMDIR_PLAY, num, -1);
	else if (num > 0 && d->pvchancount == 0)
		(void)pcm_setvchans(d, PCMDIR_PLAY, 1, -1);

	if (num >= 0 && d->rvchancount > num)
		(void)pcm_setvchans(d, PCMDIR_REC, num, -1);
	else if (num > 0 && d->rvchancount == 0)
		(void)pcm_setvchans(d, PCMDIR_REC, 1, -1);

	pcm_clonereset(d);
}

static int
sysctl_hw_snd_default_unit(SYSCTL_HANDLER_ARGS)
{
	struct snddev_info *d;
	int error, unit;

	unit = snd_unit;
	error = sysctl_handle_int(oidp, &unit, 0, req);
	if (error == 0 && req->newptr != NULL) {
		d = devclass_get_softc(pcm_devclass, unit);
		if (!PCM_REGISTERED(d) || CHN_EMPTY(d, channels.pcm))
			return EINVAL;
		snd_unit = unit;
		snd_unit_auto = 0;
	}
	return (error);
}
/* XXX: do we need a way to let the user change the default unit? */
SYSCTL_PROC(_hw_snd, OID_AUTO, default_unit,
	    CTLTYPE_INT | CTLFLAG_RWTUN | CTLFLAG_ANYBODY,
	    0, sizeof(int), sysctl_hw_snd_default_unit, "I",
	    "default sound device");

static int
sysctl_hw_snd_maxautovchans(SYSCTL_HANDLER_ARGS)
{
	struct snddev_info *d;
	int i, v, error;

	v = snd_maxautovchans;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (v < 0)
			v = 0;
		if (v > SND_MAXVCHANS)
			v = SND_MAXVCHANS;
		snd_maxautovchans = v;
		for (i = 0; pcm_devclass != NULL &&
		    i < devclass_get_maxunit(pcm_devclass); i++) {
			d = devclass_get_softc(pcm_devclass, i);
			if (!PCM_REGISTERED(d))
				continue;
			PCM_ACQUIRE_QUICK(d);
			pcm_setmaxautovchans(d, v);
			PCM_RELEASE_QUICK(d);
		}
	}
	return (error);
}
SYSCTL_PROC(_hw_snd, OID_AUTO, maxautovchans, CTLTYPE_INT | CTLFLAG_RWTUN,
            0, sizeof(int), sysctl_hw_snd_maxautovchans, "I", "maximum virtual channel");

struct pcm_channel *
pcm_chn_create(struct snddev_info *d, struct pcm_channel *parent, kobj_class_t cls, int dir, int num, void *devinfo)
{
	struct pcm_channel *ch;
	int direction, err, rpnum, *pnum, max;
	int udc, device, chan;
	char *dirs, *devname, buf[CHN_NAMELEN];

	PCM_BUSYASSERT(d);
	PCM_LOCKASSERT(d);
	KASSERT(num >= -1, ("invalid num=%d", num));


	switch (dir) {
	case PCMDIR_PLAY:
		dirs = "play";
		direction = PCMDIR_PLAY;
		pnum = &d->playcount;
		device = SND_DEV_DSPHW_PLAY;
		max = SND_MAXHWCHAN;
		break;
	case PCMDIR_PLAY_VIRTUAL:
		dirs = "virtual";
		direction = PCMDIR_PLAY;
		pnum = &d->pvchancount;
		device = SND_DEV_DSPHW_VPLAY;
		max = SND_MAXVCHANS;
		break;
	case PCMDIR_REC:
		dirs = "record";
		direction = PCMDIR_REC;
		pnum = &d->reccount;
		device = SND_DEV_DSPHW_REC;
		max = SND_MAXHWCHAN;
		break;
	case PCMDIR_REC_VIRTUAL:
		dirs = "virtual";
		direction = PCMDIR_REC;
		pnum = &d->rvchancount;
		device = SND_DEV_DSPHW_VREC;
		max = SND_MAXVCHANS;
		break;
	default:
		return (NULL);
	}

	chan = (num == -1) ? 0 : num;

	if (*pnum >= max || chan >= max)
		return (NULL);

	rpnum = 0;

	CHN_FOREACH(ch, d, channels.pcm) {
		if (CHN_DEV(ch) != device)
			continue;
		if (chan == CHN_CHAN(ch)) {
			if (num != -1) {
				device_printf(d->dev,
				    "channel num=%d allocated!\n", chan);
				return (NULL);
			}
			chan++;
			if (chan >= max) {
				device_printf(d->dev,
				    "chan=%d > %d\n", chan, max);
				return (NULL);
			}
		}
		rpnum++;
	}

	if (*pnum != rpnum) {
		device_printf(d->dev,
		    "%s(): WARNING: pnum screwed : dirs=%s pnum=%d rpnum=%d\n",
		    __func__, dirs, *pnum, rpnum);
		return (NULL);
	}

	udc = snd_mkunit(device_get_unit(d->dev), device, chan);
	devname = dsp_unit2name(buf, sizeof(buf), udc);

	if (devname == NULL) {
		device_printf(d->dev,
		    "Failed to query device name udc=0x%08x\n", udc);
		return (NULL);
	}

	PCM_UNLOCK(d);
	ch = malloc(sizeof(*ch), M_DEVBUF, M_WAITOK | M_ZERO);
	ch->methods = kobj_create(cls, M_DEVBUF, M_WAITOK | M_ZERO);
	ch->unit = udc;
	ch->pid = -1;
	strlcpy(ch->comm, CHN_COMM_UNUSED, sizeof(ch->comm));
	ch->parentsnddev = d;
	ch->parentchannel = parent;
	ch->dev = d->dev;
	ch->trigger = PCMTRIG_STOP;
	snprintf(ch->name, sizeof(ch->name), "%s:%s:%s",
	    device_get_nameunit(ch->dev), dirs, devname);

	err = chn_init(ch, devinfo, dir, direction);
	PCM_LOCK(d);
	if (err) {
		device_printf(d->dev, "chn_init(%s) failed: err = %d\n",
		    ch->name, err);
		kobj_delete(ch->methods, M_DEVBUF);
		free(ch, M_DEVBUF);
		return (NULL);
	}

	return (ch);
}

int
pcm_chn_destroy(struct pcm_channel *ch)
{
	struct snddev_info *d;
	int err;

	d = ch->parentsnddev;
	PCM_BUSYASSERT(d);

	err = chn_kill(ch);
	if (err) {
		device_printf(ch->dev, "chn_kill(%s) failed, err = %d\n",
		    ch->name, err);
		return (err);
	}

	kobj_delete(ch->methods, M_DEVBUF);
	free(ch, M_DEVBUF);

	return (0);
}

int
pcm_chn_add(struct snddev_info *d, struct pcm_channel *ch)
{
	PCM_BUSYASSERT(d);
	PCM_LOCKASSERT(d);
	KASSERT(ch != NULL && (ch->direction == PCMDIR_PLAY ||
	    ch->direction == PCMDIR_REC), ("Invalid pcm channel"));

	CHN_INSERT_SORT_ASCEND(d, ch, channels.pcm);

	switch (CHN_DEV(ch)) {
	case SND_DEV_DSPHW_PLAY:
		d->playcount++;
		break;
	case SND_DEV_DSPHW_VPLAY:
		d->pvchancount++;
		break;
	case SND_DEV_DSPHW_REC:
		d->reccount++;
		break;
	case SND_DEV_DSPHW_VREC:
		d->rvchancount++;
		break;
	default:
		break;
	}

	d->devcount++;

	return (0);
}

int
pcm_chn_remove(struct snddev_info *d, struct pcm_channel *ch)
{
	struct pcm_channel *tmp;

	PCM_BUSYASSERT(d);
	PCM_LOCKASSERT(d);

	tmp = NULL;

	CHN_FOREACH(tmp, d, channels.pcm) {
		if (tmp == ch)
			break;
	}

	if (tmp != ch)
		return (EINVAL);

	CHN_REMOVE(d, ch, channels.pcm);

	switch (CHN_DEV(ch)) {
	case SND_DEV_DSPHW_PLAY:
		d->playcount--;
		break;
	case SND_DEV_DSPHW_VPLAY:
		d->pvchancount--;
		break;
	case SND_DEV_DSPHW_REC:
		d->reccount--;
		break;
	case SND_DEV_DSPHW_VREC:
		d->rvchancount--;
		break;
	default:
		break;
	}

	d->devcount--;

	return (0);
}

int
pcm_addchan(device_t dev, int dir, kobj_class_t cls, void *devinfo)
{
	struct snddev_info *d = device_get_softc(dev);
	struct pcm_channel *ch;
	int err;

	PCM_BUSYASSERT(d);

	PCM_LOCK(d);
	ch = pcm_chn_create(d, NULL, cls, dir, -1, devinfo);
	if (!ch) {
		device_printf(d->dev, "pcm_chn_create(%s, %d, %p) failed\n",
		    cls->name, dir, devinfo);
		PCM_UNLOCK(d);
		return (ENODEV);
	}

	err = pcm_chn_add(d, ch);
	PCM_UNLOCK(d);
	if (err) {
		device_printf(d->dev, "pcm_chn_add(%s) failed, err=%d\n",
		    ch->name, err);
		pcm_chn_destroy(ch);
	}

	return (err);
}

static int
pcm_killchan(device_t dev)
{
	struct snddev_info *d = device_get_softc(dev);
	struct pcm_channel *ch;
	int error;

	PCM_BUSYASSERT(d);

	ch = CHN_FIRST(d, channels.pcm);

	PCM_LOCK(d);
	error = pcm_chn_remove(d, ch);
	PCM_UNLOCK(d);
	if (error)
		return (error);
	return (pcm_chn_destroy(ch));
}

static int
pcm_best_unit(int old)
{
	struct snddev_info *d;
	int i, best, bestprio, prio;

	best = -1;
	bestprio = -100;
	for (i = 0; pcm_devclass != NULL &&
	    i < devclass_get_maxunit(pcm_devclass); i++) {
		d = devclass_get_softc(pcm_devclass, i);
		if (!PCM_REGISTERED(d))
			continue;
		prio = 0;
		if (d->playcount == 0)
			prio -= 10;
		if (d->reccount == 0)
			prio -= 2;
		if (prio > bestprio || (prio == bestprio && i == old)) {
			best = i;
			bestprio = prio;
		}
	}
	return (best);
}

int
pcm_setstatus(device_t dev, char *str)
{
	struct snddev_info *d = device_get_softc(dev);

	/* should only be called once */
	if (d->flags & SD_F_REGISTERED)
		return (EINVAL);

	PCM_BUSYASSERT(d);

	if (d->playcount == 0 || d->reccount == 0)
		d->flags |= SD_F_SIMPLEX;

	if (d->playcount > 0 || d->reccount > 0)
		d->flags |= SD_F_AUTOVCHAN;

	pcm_setmaxautovchans(d, snd_maxautovchans);

	strlcpy(d->status, str, SND_STATUSLEN);

	PCM_LOCK(d);

	/* Last stage, enable cloning. */
	if (d->clones != NULL)
		(void)snd_clone_enable(d->clones);

	/* Done, we're ready.. */
	d->flags |= SD_F_REGISTERED;

	PCM_RELEASE(d);

	PCM_UNLOCK(d);

	/*
	 * Create all sysctls once SD_F_REGISTERED is set else
	 * tunable sysctls won't work:
	 */
	pcm_sysinit(dev);

	if (snd_unit_auto < 0)
		snd_unit_auto = (snd_unit < 0) ? 1 : 0;
	if (snd_unit < 0 || snd_unit_auto > 1)
		snd_unit = device_get_unit(dev);
	else if (snd_unit_auto == 1)
		snd_unit = pcm_best_unit(snd_unit);

	return (0);
}

uint32_t
pcm_getflags(device_t dev)
{
	struct snddev_info *d = device_get_softc(dev);

	return d->flags;
}

void
pcm_setflags(device_t dev, uint32_t val)
{
	struct snddev_info *d = device_get_softc(dev);

	d->flags = val;
}

void *
pcm_getdevinfo(device_t dev)
{
	struct snddev_info *d = device_get_softc(dev);

	return d->devinfo;
}

unsigned int
pcm_getbuffersize(device_t dev, unsigned int minbufsz, unsigned int deflt, unsigned int maxbufsz)
{
	struct snddev_info *d = device_get_softc(dev);
	int sz, x;

	sz = 0;
	if (resource_int_value(device_get_name(dev), device_get_unit(dev), "buffersize", &sz) == 0) {
		x = sz;
		RANGE(sz, minbufsz, maxbufsz);
		if (x != sz)
			device_printf(dev, "'buffersize=%d' hint is out of range (%d-%d), using %d\n", x, minbufsz, maxbufsz, sz);
		x = minbufsz;
		while (x < sz)
			x <<= 1;
		if (x > sz)
			x >>= 1;
		if (x != sz) {
			device_printf(dev, "'buffersize=%d' hint is not a power of 2, using %d\n", sz, x);
			sz = x;
		}
	} else {
		sz = deflt;
	}

	d->bufsz = sz;

	return sz;
}

static int
sysctl_dev_pcm_bitperfect(SYSCTL_HANDLER_ARGS)
{
	struct snddev_info *d;
	int err, val;

	d = oidp->oid_arg1;
	if (!PCM_REGISTERED(d))
		return (ENODEV);

	PCM_LOCK(d);
	PCM_WAIT(d);
	val = (d->flags & SD_F_BITPERFECT) ? 1 : 0;
	PCM_ACQUIRE(d);
	PCM_UNLOCK(d);

	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err == 0 && req->newptr != NULL) {
		if (!(val == 0 || val == 1)) {
			PCM_RELEASE_QUICK(d);
			return (EINVAL);
		}

		PCM_LOCK(d);

		d->flags &= ~SD_F_BITPERFECT;
		d->flags |= (val != 0) ? SD_F_BITPERFECT : 0;

		PCM_RELEASE(d);
		PCM_UNLOCK(d);
	} else
		PCM_RELEASE_QUICK(d);

	return (err);
}

#ifdef SND_DEBUG
static int
sysctl_dev_pcm_clone_flags(SYSCTL_HANDLER_ARGS)
{
	struct snddev_info *d;
	uint32_t flags;
	int err;

	d = oidp->oid_arg1;
	if (!PCM_REGISTERED(d) || d->clones == NULL)
		return (ENODEV);

	PCM_ACQUIRE_QUICK(d);

	flags = snd_clone_getflags(d->clones);
	err = sysctl_handle_int(oidp, &flags, 0, req);

	if (err == 0 && req->newptr != NULL) {
		if (flags & ~SND_CLONE_MASK)
			err = EINVAL;
		else
			(void)snd_clone_setflags(d->clones, flags);
	}

	PCM_RELEASE_QUICK(d);

	return (err);
}

static int
sysctl_dev_pcm_clone_deadline(SYSCTL_HANDLER_ARGS)
{
	struct snddev_info *d;
	int err, deadline;

	d = oidp->oid_arg1;
	if (!PCM_REGISTERED(d) || d->clones == NULL)
		return (ENODEV);

	PCM_ACQUIRE_QUICK(d);

	deadline = snd_clone_getdeadline(d->clones);
	err = sysctl_handle_int(oidp, &deadline, 0, req);

	if (err == 0 && req->newptr != NULL) {
		if (deadline < 0)
			err = EINVAL;
		else
			(void)snd_clone_setdeadline(d->clones, deadline);
	}

	PCM_RELEASE_QUICK(d);

	return (err);
}

static int
sysctl_dev_pcm_clone_gc(SYSCTL_HANDLER_ARGS)
{
	struct snddev_info *d;
	int err, val;

	d = oidp->oid_arg1;
	if (!PCM_REGISTERED(d) || d->clones == NULL)
		return (ENODEV);

	val = 0;
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err == 0 && req->newptr != NULL && val != 0) {
		PCM_ACQUIRE_QUICK(d);
		val = snd_clone_gc(d->clones);
		PCM_RELEASE_QUICK(d);
		if (bootverbose != 0 || snd_verbose > 3)
			device_printf(d->dev, "clone gc: pruned=%d\n", val);
	}

	return (err);
}

static int
sysctl_hw_snd_clone_gc(SYSCTL_HANDLER_ARGS)
{
	struct snddev_info *d;
	int i, err, val;

	val = 0;
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err == 0 && req->newptr != NULL && val != 0) {
		for (i = 0; pcm_devclass != NULL &&
		    i < devclass_get_maxunit(pcm_devclass); i++) {
			d = devclass_get_softc(pcm_devclass, i);
			if (!PCM_REGISTERED(d) || d->clones == NULL)
				continue;
			PCM_ACQUIRE_QUICK(d);
			val = snd_clone_gc(d->clones);
			PCM_RELEASE_QUICK(d);
			if (bootverbose != 0 || snd_verbose > 3)
				device_printf(d->dev, "clone gc: pruned=%d\n",
				    val);
		}
	}

	return (err);
}
SYSCTL_PROC(_hw_snd, OID_AUTO, clone_gc, CTLTYPE_INT | CTLFLAG_RWTUN,
    0, sizeof(int), sysctl_hw_snd_clone_gc, "I",
    "global clone garbage collector");
#endif

static void
pcm_sysinit(device_t dev)
{
  	struct snddev_info *d = device_get_softc(dev);

  	/* XXX: an user should be able to set this with a control tool, the
	   sysadmin then needs min+max sysctls for this */
	SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
            OID_AUTO, "buffersize", CTLFLAG_RD, &d->bufsz, 0, "allocated buffer size");
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "bitperfect", CTLTYPE_INT | CTLFLAG_RWTUN, d, sizeof(d),
	    sysctl_dev_pcm_bitperfect, "I",
	    "bit-perfect playback/recording (0=disable, 1=enable)");
#ifdef SND_DEBUG
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "clone_flags", CTLTYPE_UINT | CTLFLAG_RWTUN, d, sizeof(d),
	    sysctl_dev_pcm_clone_flags, "IU",
	    "clone flags");
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "clone_deadline", CTLTYPE_INT | CTLFLAG_RWTUN, d, sizeof(d),
	    sysctl_dev_pcm_clone_deadline, "I",
	    "clone expiration deadline (ms)");
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "clone_gc", CTLTYPE_INT | CTLFLAG_RWTUN, d, sizeof(d),
	    sysctl_dev_pcm_clone_gc, "I",
	    "clone garbage collector");
#endif
	if (d->flags & SD_F_AUTOVCHAN)
		vchan_initsys(dev);
	if (d->flags & SD_F_EQ)
		feeder_eq_initsys(dev);
}

int
pcm_register(device_t dev, void *devinfo, int numplay, int numrec)
{
	struct snddev_info *d;
	int i;

	if (pcm_veto_load) {
		device_printf(dev, "disabled due to an error while initialising: %d\n", pcm_veto_load);

		return EINVAL;
	}

	if (device_get_unit(dev) > PCMMAXUNIT) {
		device_printf(dev, "PCMMAXUNIT reached : unit=%d > %d\n",
		    device_get_unit(dev), PCMMAXUNIT);
		device_printf(dev,
		    "Use 'hw.snd.maxunit' tunable to raise the limit.\n");
		return ENODEV;
	}

	d = device_get_softc(dev);
	d->dev = dev;
	d->lock = snd_mtxcreate(device_get_nameunit(dev), "sound cdev");
	cv_init(&d->cv, device_get_nameunit(dev));
	PCM_ACQUIRE_QUICK(d);
	dsp_cdevinfo_init(d);
#if 0
	/*
	 * d->flags should be cleared by the allocator of the softc.
	 * We cannot clear this field here because several devices set
	 * this flag before calling pcm_register().
	 */
	d->flags = 0;
#endif
	i = 0;
	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "vpc", &i) != 0 || i != 0)
		d->flags |= SD_F_VPC;

	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "bitperfect", &i) == 0 && i != 0)
		d->flags |= SD_F_BITPERFECT;

	d->devinfo = devinfo;
	d->devcount = 0;
	d->reccount = 0;
	d->playcount = 0;
	d->pvchancount = 0;
	d->rvchancount = 0;
	d->pvchanrate = 0;
	d->pvchanformat = 0;
	d->rvchanrate = 0;
	d->rvchanformat = 0;
	d->inprog = 0;

	/*
	 * Create clone manager, disabled by default. Cloning will be
	 * enabled during final stage of driver initialization through
	 * pcm_setstatus().
	 */
	d->clones = snd_clone_create(SND_U_MASK | SND_D_MASK, PCMMAXCLONE,
	    SND_CLONE_DEADLINE_DEFAULT, SND_CLONE_WAITOK |
	    SND_CLONE_GC_ENABLE | SND_CLONE_GC_UNREF |
	    SND_CLONE_GC_LASTREF | SND_CLONE_GC_EXPIRED);

	CHN_INIT(d, channels.pcm);
	CHN_INIT(d, channels.pcm.busy);
	CHN_INIT(d, channels.pcm.opened);

	/* XXX This is incorrect, but lets play along for now. */
	if ((numplay == 0 || numrec == 0) && numplay != numrec)
		d->flags |= SD_F_SIMPLEX;

	sysctl_ctx_init(&d->play_sysctl_ctx);
	d->play_sysctl_tree = SYSCTL_ADD_NODE(&d->play_sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO, "play",
	    CTLFLAG_RD, 0, "playback channels node");
	sysctl_ctx_init(&d->rec_sysctl_ctx);
	d->rec_sysctl_tree = SYSCTL_ADD_NODE(&d->rec_sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO, "rec",
	    CTLFLAG_RD, 0, "record channels node");

	if (numplay > 0 || numrec > 0)
		d->flags |= SD_F_AUTOVCHAN;

	sndstat_register(dev, d->status, sndstat_prepare_pcm);

	return 0;
}

int
pcm_unregister(device_t dev)
{
	struct snddev_info *d;
	struct pcm_channel *ch;
	struct thread *td;

	td = curthread;
	d = device_get_softc(dev);

	if (!PCM_ALIVE(d)) {
		device_printf(dev, "unregister: device not configured\n");
		return (0);
	}

	PCM_LOCK(d);
	PCM_WAIT(d);

	if (d->inprog != 0) {
		device_printf(dev, "unregister: operation in progress\n");
		PCM_UNLOCK(d);
		return (EBUSY);
	}

	PCM_ACQUIRE(d);
	PCM_UNLOCK(d);

	CHN_FOREACH(ch, d, channels.pcm) {
		CHN_LOCK(ch);
		if (ch->refcount > 0) {
			device_printf(dev,
			    "unregister: channel %s busy (pid %d)\n",
			    ch->name, ch->pid);
			CHN_UNLOCK(ch);
			PCM_RELEASE_QUICK(d);
			return (EBUSY);
		}
		CHN_UNLOCK(ch);
	}

	if (d->clones != NULL) {
		if (snd_clone_busy(d->clones) != 0) {
			device_printf(dev, "unregister: clone busy\n");
			PCM_RELEASE_QUICK(d);
			return (EBUSY);
		} else {
			PCM_LOCK(d);
			(void)snd_clone_disable(d->clones);
			PCM_UNLOCK(d);
		}
	}

	if (mixer_uninit(dev) == EBUSY) {
		device_printf(dev, "unregister: mixer busy\n");
		PCM_LOCK(d);
		if (d->clones != NULL)
			(void)snd_clone_enable(d->clones);
		PCM_RELEASE(d);
		PCM_UNLOCK(d);
		return (EBUSY);
	}

	/* remove /dev/sndstat entry first */
	sndstat_unregister(dev);
	
	PCM_LOCK(d);
	d->flags |= SD_F_DYING;
	d->flags &= ~SD_F_REGISTERED;
	PCM_UNLOCK(d);

	/*
	 * No lock being held, so this thing can be flushed without
	 * stucking into devdrn oblivion.
	 */
	if (d->clones != NULL) {
		snd_clone_destroy(d->clones);
		d->clones = NULL;
	}

	if (d->play_sysctl_tree != NULL) {
		sysctl_ctx_free(&d->play_sysctl_ctx);
		d->play_sysctl_tree = NULL;
	}
	if (d->rec_sysctl_tree != NULL) {
		sysctl_ctx_free(&d->rec_sysctl_ctx);
		d->rec_sysctl_tree = NULL;
	}

	while (!CHN_EMPTY(d, channels.pcm))
		pcm_killchan(dev);

	dsp_cdevinfo_flush(d);

	PCM_LOCK(d);
	PCM_RELEASE(d);
	cv_destroy(&d->cv);
	PCM_UNLOCK(d);
	snd_mtxfree(d->lock);

	if (snd_unit == device_get_unit(dev)) {
		snd_unit = pcm_best_unit(-1);
		if (snd_unit_auto == 0)
			snd_unit_auto = 1;
	}

	return (0);
}

/************************************************************************/

/**
 * @brief	Handle OSSv4 SNDCTL_SYSINFO ioctl.
 *
 * @param si	Pointer to oss_sysinfo struct where information about the
 * 		sound subsystem will be written/copied.
 *
 * This routine returns information about the sound system, such as the
 * current OSS version, number of audio, MIDI, and mixer drivers, etc.
 * Also includes a bitmask showing which of the above types of devices
 * are open (busy).
 *
 * @note
 * Calling threads must not hold any snddev_info or pcm_channel locks.
 *
 * @author	Ryan Beasley <ryanb@FreeBSD.org>
 */
void
sound_oss_sysinfo(oss_sysinfo *si)
{
	static char si_product[] = "FreeBSD native OSS ABI";
	static char si_version[] = __XSTRING(__FreeBSD_version);
	static char si_license[] = "BSD";
	static int intnbits = sizeof(int) * 8;	/* Better suited as macro?
						   Must pester a C guru. */

	struct snddev_info *d;
	struct pcm_channel *c;
	int i, j, ncards;
	
	ncards = 0;

	strlcpy(si->product, si_product, sizeof(si->product));
	strlcpy(si->version, si_version, sizeof(si->version));
	si->versionnum = SOUND_VERSION;
	strlcpy(si->license, si_license, sizeof(si->license));

	/*
	 * Iterate over PCM devices and their channels, gathering up data
	 * for the numaudios, ncards, and openedaudio fields.
	 */
	si->numaudios = 0;
	bzero((void *)&si->openedaudio, sizeof(si->openedaudio));

	j = 0;

	for (i = 0; pcm_devclass != NULL &&
	    i < devclass_get_maxunit(pcm_devclass); i++) {
		d = devclass_get_softc(pcm_devclass, i);
		if (!PCM_REGISTERED(d))
			continue;

		/* XXX Need Giant magic entry ??? */

		/* See note in function's docblock */
		PCM_UNLOCKASSERT(d);
		PCM_LOCK(d);

		si->numaudios += d->devcount;
		++ncards;

		CHN_FOREACH(c, d, channels.pcm) {
			CHN_UNLOCKASSERT(c);
			CHN_LOCK(c);
			if (c->flags & CHN_F_BUSY)
				si->openedaudio[j / intnbits] |=
				    (1 << (j % intnbits));
			CHN_UNLOCK(c);
			j++;
		}

		PCM_UNLOCK(d);
	}
	si->numaudioengines = si->numaudios;

	si->numsynths = 0;	/* OSSv4 docs:  this field is obsolete */
	/**
	 * @todo	Collect num{midis,timers}.
	 *
	 * Need access to sound/midi/midi.c::midistat_lock in order
	 * to safely touch midi_devices and get a head count of, well,
	 * MIDI devices.  midistat_lock is a global static (i.e., local to
	 * midi.c), but midi_devices is a regular global; should the mutex
	 * be publicized, or is there another way to get this information?
	 *
	 * NB:	MIDI/sequencer stuff is currently on hold.
	 */
	si->nummidis = 0;
	si->numtimers = 0;
	si->nummixers = mixer_count;
	si->numcards = ncards;
		/* OSSv4 docs:	Intended only for test apps; API doesn't
		   really have much of a concept of cards.  Shouldn't be
		   used by applications. */

	/**
	 * @todo	Fill in "busy devices" fields.
	 *
	 *  si->openedmidi = " MIDI devices
	 */
	bzero((void *)&si->openedmidi, sizeof(si->openedmidi));

	/*
	 * Si->filler is a reserved array, but according to docs each
	 * element should be set to -1.
	 */
	for (i = 0; i < sizeof(si->filler)/sizeof(si->filler[0]); i++)
		si->filler[i] = -1;
}

int
sound_oss_card_info(oss_card_info *si)
{
	struct snddev_info *d;
	int i, ncards;
	
	ncards = 0;

	for (i = 0; pcm_devclass != NULL &&
	    i < devclass_get_maxunit(pcm_devclass); i++) {
		d = devclass_get_softc(pcm_devclass, i);
		if (!PCM_REGISTERED(d))
			continue;

		if (ncards++ != si->card)
			continue;

		PCM_UNLOCKASSERT(d);
		PCM_LOCK(d);
		
		strlcpy(si->shortname, device_get_nameunit(d->dev),
		    sizeof(si->shortname));
		strlcpy(si->longname, device_get_desc(d->dev),
		    sizeof(si->longname));
		strlcpy(si->hw_info, d->status, sizeof(si->hw_info));
		si->intr_count = si->ack_count = 0;

		PCM_UNLOCK(d);

		return (0);
	}
	return (ENXIO);
}

/************************************************************************/

static int
sound_modevent(module_t mod, int type, void *data)
{
	int ret;
#if 0
	return (midi_modevent(mod, type, data));
#else
	ret = 0;

	switch(type) {
		case MOD_LOAD:
			pcmsg_unrhdr = new_unrhdr(1, INT_MAX, NULL);
			break;
		case MOD_UNLOAD:
			if (pcmsg_unrhdr != NULL) {
				delete_unrhdr(pcmsg_unrhdr);
				pcmsg_unrhdr = NULL;
			}
			break;
		case MOD_SHUTDOWN:
			break;
		default:
			ret = ENOTSUP;
	}

	return ret;
#endif
}

DEV_MODULE(sound, sound_modevent, NULL);
MODULE_VERSION(sound, SOUND_MODVER);
