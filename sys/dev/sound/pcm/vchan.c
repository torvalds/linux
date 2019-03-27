/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2009 Ariff Abdullah <ariff@FreeBSD.org>
 * Copyright (c) 2001 Cameron Grant <cg@FreeBSD.org>
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

/* Almost entirely rewritten to add multi-format/channels mixing support. */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/vchan.h>

SND_DECLARE_FILE("$FreeBSD$");

/*
 * [ac3 , dts , linear , 0, linear, 0]
 */
#define FMTLIST_MAX		6
#define FMTLIST_OFFSET		4
#define DIGFMTS_MAX		2

#ifdef SND_DEBUG
static int snd_passthrough_verbose = 0;
SYSCTL_INT(_hw_snd, OID_AUTO, passthrough_verbose, CTLFLAG_RWTUN,
	&snd_passthrough_verbose, 0, "passthrough verbosity");

#endif

struct vchan_info {
	struct pcm_channel *channel;
	struct pcmchan_caps caps;
	uint32_t fmtlist[FMTLIST_MAX];
	int trigger;
};

static void *
vchan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
    struct pcm_channel *c, int dir)
{
	struct vchan_info *info;
	struct pcm_channel *p;
	uint32_t i, j, *fmtlist;

	KASSERT(dir == PCMDIR_PLAY || dir == PCMDIR_REC,
	    ("vchan_init: bad direction"));
	KASSERT(c != NULL && c->parentchannel != NULL,
	    ("vchan_init: bad channels"));

	info = malloc(sizeof(*info), M_DEVBUF, M_WAITOK | M_ZERO);
	info->channel = c;
	info->trigger = PCMTRIG_STOP;
	p = c->parentchannel;

	CHN_LOCK(p);

	fmtlist = chn_getcaps(p)->fmtlist;
	for (i = 0, j = 0; fmtlist[i] != 0 && j < DIGFMTS_MAX; i++) {
		if (fmtlist[i] & AFMT_PASSTHROUGH)
			info->fmtlist[j++] = fmtlist[i];
	}
	if (p->format & AFMT_VCHAN)
		info->fmtlist[j] = p->format;
	else
		info->fmtlist[j] = VCHAN_DEFAULT_FORMAT;
	info->caps.fmtlist = info->fmtlist +
	    ((p->flags & CHN_F_VCHAN_DYNAMIC) ? 0 : FMTLIST_OFFSET);

	CHN_UNLOCK(p);

	c->flags |= CHN_F_VIRTUAL;

	return (info);
}

static int
vchan_free(kobj_t obj, void *data)
{

	free(data, M_DEVBUF);

	return (0);
}

static int
vchan_setformat(kobj_t obj, void *data, uint32_t format)
{
	struct vchan_info *info;

	info = data;

	CHN_LOCKASSERT(info->channel);

	if (!snd_fmtvalid(format, info->caps.fmtlist))
		return (-1);

	return (0);
}

static uint32_t
vchan_setspeed(kobj_t obj, void *data, uint32_t speed)
{
	struct vchan_info *info;

	info = data;

	CHN_LOCKASSERT(info->channel);

	return (info->caps.maxspeed);
}

static int
vchan_trigger(kobj_t obj, void *data, int go)
{
	struct vchan_info *info;
	struct pcm_channel *c, *p;
	int ret, otrigger;

	info = data;

	if (!PCMTRIG_COMMON(go) || go == info->trigger)
		return (0);

	c = info->channel;
	p = c->parentchannel;
	otrigger = info->trigger;
	info->trigger = go;

	CHN_LOCKASSERT(c);

	CHN_UNLOCK(c);
	CHN_LOCK(p);

	switch (go) {
	case PCMTRIG_START:
		if (otrigger != PCMTRIG_START)
			CHN_INSERT_HEAD(p, c, children.busy);
		break;
	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		if (otrigger == PCMTRIG_START)
			CHN_REMOVE(p, c, children.busy);
		break;
	default:
		break;
	}

	ret = chn_notify(p, CHN_N_TRIGGER);

	CHN_LOCK(c);

	if (ret == 0 && go == PCMTRIG_START && VCHAN_SYNC_REQUIRED(c))
		ret = vchan_sync(c);

	CHN_UNLOCK(c);
	CHN_UNLOCK(p);
	CHN_LOCK(c);

	return (ret);
}

static struct pcmchan_caps *
vchan_getcaps(kobj_t obj, void *data)
{
	struct vchan_info *info;
	struct pcm_channel *c;
	uint32_t pformat, pspeed, pflags, i;

	info = data;
	c = info->channel;
	pformat = c->parentchannel->format;
	pspeed = c->parentchannel->speed;
	pflags = c->parentchannel->flags;

	CHN_LOCKASSERT(c);

	if (pflags & CHN_F_VCHAN_DYNAMIC) {
		info->caps.fmtlist = info->fmtlist;
		if (pformat & AFMT_VCHAN) {
			for (i = 0; info->caps.fmtlist[i] != 0; i++) {
				if (info->caps.fmtlist[i] & AFMT_PASSTHROUGH)
					continue;
				break;
			}
			info->caps.fmtlist[i] = pformat;
		}
		if (c->format & AFMT_PASSTHROUGH)
			info->caps.minspeed = c->speed;
		else 
			info->caps.minspeed = pspeed;
		info->caps.maxspeed = info->caps.minspeed;
	} else {
		info->caps.fmtlist = info->fmtlist + FMTLIST_OFFSET;
		if (pformat & AFMT_VCHAN)
			info->caps.fmtlist[0] = pformat;
		else {
			device_printf(c->dev,
			    "%s(): invalid vchan format 0x%08x",
			    __func__, pformat);
			info->caps.fmtlist[0] = VCHAN_DEFAULT_FORMAT;
		}
		info->caps.minspeed = pspeed;
		info->caps.maxspeed = info->caps.minspeed;
	}

	return (&info->caps);
}

static struct pcmchan_matrix *
vchan_getmatrix(kobj_t obj, void *data, uint32_t format)
{

	return (feeder_matrix_format_map(format));
}

static kobj_method_t vchan_methods[] = {
	KOBJMETHOD(channel_init,		vchan_init),
	KOBJMETHOD(channel_free,		vchan_free),
	KOBJMETHOD(channel_setformat,		vchan_setformat),
	KOBJMETHOD(channel_setspeed,		vchan_setspeed),
	KOBJMETHOD(channel_trigger,		vchan_trigger),
	KOBJMETHOD(channel_getcaps,		vchan_getcaps),
	KOBJMETHOD(channel_getmatrix,		vchan_getmatrix),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(vchan);

static void
pcm_getparentchannel(struct snddev_info *d,
    struct pcm_channel **wrch, struct pcm_channel **rdch)
{
	struct pcm_channel **ch, *wch, *rch, *c;

	KASSERT(d != NULL, ("%s(): NULL snddev_info", __func__));

	PCM_BUSYASSERT(d);
	PCM_UNLOCKASSERT(d);

	wch = NULL;
	rch = NULL;

	CHN_FOREACH(c, d, channels.pcm) {
		CHN_LOCK(c);
		ch = (c->direction == PCMDIR_PLAY) ? &wch : &rch;
		if (c->flags & CHN_F_VIRTUAL) {
			/* Sanity check */
			if (*ch != NULL && *ch != c->parentchannel) {
				CHN_UNLOCK(c);
				*ch = NULL;
				break;
			}
		} else if (c->flags & CHN_F_HAS_VCHAN) {
			/* No way!! */
			if (*ch != NULL) {
				CHN_UNLOCK(c);
				*ch = NULL;
				break;
			}
			*ch = c;
		}
		CHN_UNLOCK(c);
	}

	if (wrch != NULL)
		*wrch = wch;
	if (rdch != NULL)
		*rdch = rch;
}

static int
sysctl_dev_pcm_vchans(SYSCTL_HANDLER_ARGS)
{
	struct snddev_info *d;
	int direction, vchancount;
	int err, cnt;

	d = devclass_get_softc(pcm_devclass, VCHAN_SYSCTL_UNIT(oidp->oid_arg1));
	if (!PCM_REGISTERED(d) || !(d->flags & SD_F_AUTOVCHAN))
		return (EINVAL);

	PCM_LOCK(d);
	PCM_WAIT(d);

	switch (VCHAN_SYSCTL_DIR(oidp->oid_arg1)) {
	case VCHAN_PLAY:
		direction = PCMDIR_PLAY;
		vchancount = d->pvchancount;
		cnt = d->playcount;
		break;
	case VCHAN_REC:
		direction = PCMDIR_REC;
		vchancount = d->rvchancount;
		cnt = d->reccount;
		break;
	default:
		PCM_UNLOCK(d);
		return (EINVAL);
		break;
	}

	if (cnt < 1) {
		PCM_UNLOCK(d);
		return (ENODEV);
	}

	PCM_ACQUIRE(d);
	PCM_UNLOCK(d);

	cnt = vchancount;
	err = sysctl_handle_int(oidp, &cnt, 0, req);

	if (err == 0 && req->newptr != NULL && vchancount != cnt) {
		if (cnt < 0)
			cnt = 0;
		if (cnt > SND_MAXVCHANS)
			cnt = SND_MAXVCHANS;
		err = pcm_setvchans(d, direction, cnt, -1);
	}

	PCM_RELEASE_QUICK(d);

	return err;
}

static int
sysctl_dev_pcm_vchanmode(SYSCTL_HANDLER_ARGS)
{
	struct snddev_info *d;
	struct pcm_channel *c;
	uint32_t dflags;
	int direction, ret;
	char dtype[16];

	d = devclass_get_softc(pcm_devclass, VCHAN_SYSCTL_UNIT(oidp->oid_arg1));
	if (!PCM_REGISTERED(d) || !(d->flags & SD_F_AUTOVCHAN))
		return (EINVAL);

	PCM_LOCK(d);
	PCM_WAIT(d);

	switch (VCHAN_SYSCTL_DIR(oidp->oid_arg1)) {
	case VCHAN_PLAY:
		direction = PCMDIR_PLAY;
		break;
	case VCHAN_REC:
		direction = PCMDIR_REC;
		break;
	default:
		PCM_UNLOCK(d);
		return (EINVAL);
		break;
	}

	PCM_ACQUIRE(d);
	PCM_UNLOCK(d);

	if (direction == PCMDIR_PLAY)
		pcm_getparentchannel(d, &c, NULL);
	else
		pcm_getparentchannel(d, NULL, &c);

	if (c == NULL) {
		PCM_RELEASE_QUICK(d);
		return (EINVAL);
	}

	KASSERT(direction == c->direction, ("%s(): invalid direction %d/%d",
	    __func__, direction, c->direction));

	CHN_LOCK(c);
	if (c->flags & CHN_F_VCHAN_PASSTHROUGH)
		strlcpy(dtype, "passthrough", sizeof(dtype));
	else if (c->flags & CHN_F_VCHAN_ADAPTIVE)
		strlcpy(dtype, "adaptive", sizeof(dtype));
	else
		strlcpy(dtype, "fixed", sizeof(dtype));
	CHN_UNLOCK(c);

	ret = sysctl_handle_string(oidp, dtype, sizeof(dtype), req);
	if (ret == 0 && req->newptr != NULL) {
		if (strcasecmp(dtype, "passthrough") == 0 ||
		    strcmp(dtype, "1") == 0)
			dflags = CHN_F_VCHAN_PASSTHROUGH;
		else if (strcasecmp(dtype, "adaptive") == 0 ||
		    strcmp(dtype, "2") == 0)
			dflags = CHN_F_VCHAN_ADAPTIVE;
		else if (strcasecmp(dtype, "fixed") == 0 ||
		    strcmp(dtype, "0") == 0)
			dflags = 0;
		else {
			PCM_RELEASE_QUICK(d);
			return (EINVAL);
		}
		CHN_LOCK(c);
		if (dflags == (c->flags & CHN_F_VCHAN_DYNAMIC) ||
		    (c->flags & CHN_F_PASSTHROUGH)) {
			CHN_UNLOCK(c);
			PCM_RELEASE_QUICK(d);
			return (0);
		}
		c->flags &= ~CHN_F_VCHAN_DYNAMIC;
		c->flags |= dflags;
		CHN_UNLOCK(c);
	}

	PCM_RELEASE_QUICK(d);

	return (ret);
}

/* 
 * On the fly vchan rate/format settings
 */

#define VCHAN_ACCESSIBLE(c)	(!((c)->flags & (CHN_F_PASSTHROUGH |	\
				 CHN_F_EXCLUSIVE)) &&			\
				 (((c)->flags & CHN_F_VCHAN_DYNAMIC) ||	\
				 CHN_STOPPED(c)))
static int
sysctl_dev_pcm_vchanrate(SYSCTL_HANDLER_ARGS)
{
	struct snddev_info *d;
	struct pcm_channel *c, *ch;
	struct pcmchan_caps *caps;
	int *vchanrate, vchancount, direction, ret, newspd, restart;

	d = devclass_get_softc(pcm_devclass, VCHAN_SYSCTL_UNIT(oidp->oid_arg1));
	if (!PCM_REGISTERED(d) || !(d->flags & SD_F_AUTOVCHAN))
		return (EINVAL);

	PCM_LOCK(d);
	PCM_WAIT(d);

	switch (VCHAN_SYSCTL_DIR(oidp->oid_arg1)) {
	case VCHAN_PLAY:
		direction = PCMDIR_PLAY;
		vchancount = d->pvchancount;
		vchanrate = &d->pvchanrate;
		break;
	case VCHAN_REC:
		direction = PCMDIR_REC;
		vchancount = d->rvchancount;
		vchanrate = &d->rvchanrate;
		break;
	default:
		PCM_UNLOCK(d);
		return (EINVAL);
		break;
	}

	if (vchancount < 1) {
		PCM_UNLOCK(d);
		return (EINVAL);
	}

	PCM_ACQUIRE(d);
	PCM_UNLOCK(d);

	if (direction == PCMDIR_PLAY)
		pcm_getparentchannel(d, &c, NULL);
	else
		pcm_getparentchannel(d, NULL, &c);

	if (c == NULL) {
		PCM_RELEASE_QUICK(d);
		return (EINVAL);
	}

	KASSERT(direction == c->direction, ("%s(): invalid direction %d/%d",
	    __func__, direction, c->direction));

	CHN_LOCK(c);
	newspd = c->speed;
	CHN_UNLOCK(c);

	ret = sysctl_handle_int(oidp, &newspd, 0, req);
	if (ret != 0 || req->newptr == NULL) {
		PCM_RELEASE_QUICK(d);
		return (ret);
	}

	if (newspd < 1 || newspd < feeder_rate_min ||
	    newspd > feeder_rate_max) {
		PCM_RELEASE_QUICK(d);
		return (EINVAL);
	}

	CHN_LOCK(c);

	if (newspd != c->speed && VCHAN_ACCESSIBLE(c)) {
		if (CHN_STARTED(c)) {
			chn_abort(c);
			restart = 1;
		} else
			restart = 0;

		if (feeder_rate_round) {
			caps = chn_getcaps(c);
			RANGE(newspd, caps->minspeed, caps->maxspeed);
			newspd = CHANNEL_SETSPEED(c->methods,
			    c->devinfo, newspd);
		}

		ret = chn_reset(c, c->format, newspd);
		if (ret == 0) {
			*vchanrate = c->speed;
			if (restart != 0) {
				CHN_FOREACH(ch, c, children.busy) {
					CHN_LOCK(ch);
					if (VCHAN_SYNC_REQUIRED(ch))
						vchan_sync(ch);
					CHN_UNLOCK(ch);
				}
				c->flags |= CHN_F_DIRTY;
				ret = chn_start(c, 1);
			}
		}
	}

	CHN_UNLOCK(c);

	PCM_RELEASE_QUICK(d);

	return (ret);
}

static int
sysctl_dev_pcm_vchanformat(SYSCTL_HANDLER_ARGS)
{
	struct snddev_info *d;
	struct pcm_channel *c, *ch;
	uint32_t newfmt;
	int *vchanformat, vchancount, direction, ret, restart;
	char fmtstr[AFMTSTR_LEN];

	d = devclass_get_softc(pcm_devclass, VCHAN_SYSCTL_UNIT(oidp->oid_arg1));
	if (!PCM_REGISTERED(d) || !(d->flags & SD_F_AUTOVCHAN))
		return (EINVAL);

	PCM_LOCK(d);
	PCM_WAIT(d);

	switch (VCHAN_SYSCTL_DIR(oidp->oid_arg1)) {
	case VCHAN_PLAY:
		direction = PCMDIR_PLAY;
		vchancount = d->pvchancount;
		vchanformat = &d->pvchanformat;
		break;
	case VCHAN_REC:
		direction = PCMDIR_REC;
		vchancount = d->rvchancount;
		vchanformat = &d->rvchanformat;
		break;
	default:
		PCM_UNLOCK(d);
		return (EINVAL);
		break;
	}

	if (vchancount < 1) {
		PCM_UNLOCK(d);
		return (EINVAL);
	}

	PCM_ACQUIRE(d);
	PCM_UNLOCK(d);

	if (direction == PCMDIR_PLAY)
		pcm_getparentchannel(d, &c, NULL);
	else
		pcm_getparentchannel(d, NULL, &c);

	if (c == NULL) {
		PCM_RELEASE_QUICK(d);
		return (EINVAL);
	}

	KASSERT(direction == c->direction, ("%s(): invalid direction %d/%d",
	    __func__, direction, c->direction));

	CHN_LOCK(c);

	bzero(fmtstr, sizeof(fmtstr));

	if (snd_afmt2str(c->format, fmtstr, sizeof(fmtstr)) != c->format)
		strlcpy(fmtstr, "<ERROR>", sizeof(fmtstr));

	CHN_UNLOCK(c);

	ret = sysctl_handle_string(oidp, fmtstr, sizeof(fmtstr), req);
	if (ret != 0 || req->newptr == NULL) {
		PCM_RELEASE_QUICK(d);
		return (ret);
	}

	newfmt = snd_str2afmt(fmtstr);
	if (newfmt == 0 || !(newfmt & AFMT_VCHAN)) {
		PCM_RELEASE_QUICK(d);
		return (EINVAL);
	}

	CHN_LOCK(c);

	if (newfmt != c->format && VCHAN_ACCESSIBLE(c)) {
		if (CHN_STARTED(c)) {
			chn_abort(c);
			restart = 1;
		} else
			restart = 0;

		ret = chn_reset(c, newfmt, c->speed);
		if (ret == 0) {
			*vchanformat = c->format;
			if (restart != 0) {
				CHN_FOREACH(ch, c, children.busy) {
					CHN_LOCK(ch);
					if (VCHAN_SYNC_REQUIRED(ch))
						vchan_sync(ch);
					CHN_UNLOCK(ch);
				}
				c->flags |= CHN_F_DIRTY;
				ret = chn_start(c, 1);
			}
		}
	}

	CHN_UNLOCK(c);

	PCM_RELEASE_QUICK(d);

	return (ret);
}

/* virtual channel interface */

#define VCHAN_FMT_HINT(x)	((x) == PCMDIR_PLAY_VIRTUAL) ?		\
				"play.vchanformat" : "rec.vchanformat"
#define VCHAN_SPD_HINT(x)	((x) == PCMDIR_PLAY_VIRTUAL) ?		\
				"play.vchanrate" : "rec.vchanrate"

int
vchan_create(struct pcm_channel *parent, int num)
{
	struct snddev_info *d;
	struct pcm_channel *ch;
	struct pcmchan_caps *parent_caps;
	uint32_t vchanfmt, vchanspd;
	int ret, direction, r, save;

	d = parent->parentsnddev;

	PCM_BUSYASSERT(d);
	CHN_LOCKASSERT(parent);

	if (!(parent->flags & CHN_F_BUSY))
		return (EBUSY);

	if (!(parent->direction == PCMDIR_PLAY ||
	    parent->direction == PCMDIR_REC))
		return (EINVAL);

	d = parent->parentsnddev;

	CHN_UNLOCK(parent);
	PCM_LOCK(d);

	if (parent->direction == PCMDIR_PLAY) {
		direction = PCMDIR_PLAY_VIRTUAL;
		vchanfmt = d->pvchanformat;
		vchanspd = d->pvchanrate;
	} else {
		direction = PCMDIR_REC_VIRTUAL;
		vchanfmt = d->rvchanformat;
		vchanspd = d->rvchanrate;
	}

	/* create a new playback channel */
	ch = pcm_chn_create(d, parent, &vchan_class, direction, num, parent);
	if (ch == NULL) {
		PCM_UNLOCK(d);
		CHN_LOCK(parent);
		return (ENODEV);
	}

	/* add us to our grandparent's channel list */
	ret = pcm_chn_add(d, ch);
	PCM_UNLOCK(d);
	if (ret != 0) {
		pcm_chn_destroy(ch);
		CHN_LOCK(parent);
		return (ret);
	}

	CHN_LOCK(parent);
	/*
	 * Add us to our parent channel's children in reverse order
	 * so future destruction will pick the last (biggest number)
	 * channel.
	 */
	CHN_INSERT_SORT_DESCEND(parent, ch, children);

	if (parent->flags & CHN_F_HAS_VCHAN)
		return (0);

	parent->flags |= CHN_F_HAS_VCHAN;

	parent_caps = chn_getcaps(parent);
	if (parent_caps == NULL)
		ret = EINVAL;

	save = 0;

	if (ret == 0 && vchanfmt == 0) {
		const char *vfmt;

		CHN_UNLOCK(parent);
		r = resource_string_value(device_get_name(parent->dev),
		    device_get_unit(parent->dev), VCHAN_FMT_HINT(direction),
		    &vfmt);
		CHN_LOCK(parent);
		if (r != 0)
			vfmt = NULL;
		if (vfmt != NULL) {
			vchanfmt = snd_str2afmt(vfmt);
			if (vchanfmt != 0 && !(vchanfmt & AFMT_VCHAN))
				vchanfmt = 0;
		}
		if (vchanfmt == 0)
			vchanfmt = VCHAN_DEFAULT_FORMAT;
		save = 1;
	}

	if (ret == 0 && vchanspd == 0) {
		/*
		 * This is very sad. Few soundcards advertised as being
		 * able to do (insanely) higher/lower speed, but in
		 * reality, they simply can't. At least, we give user chance
		 * to set sane value via kernel hints or sysctl.
		 */
		CHN_UNLOCK(parent);
		r = resource_int_value(device_get_name(parent->dev),
		    device_get_unit(parent->dev), VCHAN_SPD_HINT(direction),
		    &vchanspd);
		CHN_LOCK(parent);
		if (r != 0) {
			/*
			 * No saved value, no hint, NOTHING.
			 *
			 * Workaround for sb16 running
			 * poorly at 45k / 49k.
			 */
			switch (parent_caps->maxspeed) {
			case 45000:
			case 49000:
				vchanspd = 44100;
				break;
			default:
				vchanspd = VCHAN_DEFAULT_RATE;
				if (vchanspd > parent_caps->maxspeed)
					vchanspd = parent_caps->maxspeed;
				break;
			}
			if (vchanspd < parent_caps->minspeed)
				vchanspd = parent_caps->minspeed;
		}
		save = 1;
	}

	if (ret == 0) {
		/*
		 * Limit the speed between feeder_rate_min <-> feeder_rate_max.
		 */
		if (vchanspd < feeder_rate_min)
			vchanspd = feeder_rate_min;
		if (vchanspd > feeder_rate_max)
			vchanspd = feeder_rate_max;

		if (feeder_rate_round) {
			RANGE(vchanspd, parent_caps->minspeed,
			    parent_caps->maxspeed);
			vchanspd = CHANNEL_SETSPEED(parent->methods,
			    parent->devinfo, vchanspd);
		}

		ret = chn_reset(parent, vchanfmt, vchanspd);
	}

	if (ret == 0 && save) {
		/*
		 * Save new value.
		 */
		if (direction == PCMDIR_PLAY_VIRTUAL) {
			d->pvchanformat = parent->format;
			d->pvchanrate = parent->speed;
		} else {
			d->rvchanformat = parent->format;
			d->rvchanrate = parent->speed;
		}
	}
	
	/*
	 * If the parent channel supports digital format,
	 * enable passthrough mode.
	 */
	if (ret == 0 && snd_fmtvalid(AFMT_PASSTHROUGH, parent_caps->fmtlist)) {
		parent->flags &= ~CHN_F_VCHAN_DYNAMIC;
		parent->flags |= CHN_F_VCHAN_PASSTHROUGH;
	}

	if (ret != 0) {
		CHN_REMOVE(parent, ch, children);
		parent->flags &= ~CHN_F_HAS_VCHAN;
		CHN_UNLOCK(parent);
		PCM_LOCK(d);
		if (pcm_chn_remove(d, ch) == 0) {
			PCM_UNLOCK(d);
			pcm_chn_destroy(ch);
		} else
			PCM_UNLOCK(d);
		CHN_LOCK(parent);
	}

	return (ret);
}

int
vchan_destroy(struct pcm_channel *c)
{
	struct pcm_channel *parent;
	struct snddev_info *d;
	int ret;

	KASSERT(c != NULL && c->parentchannel != NULL &&
	    c->parentsnddev != NULL, ("%s(): invalid channel=%p",
	    __func__, c));

	CHN_LOCKASSERT(c);

	d = c->parentsnddev;
	parent = c->parentchannel;

	PCM_BUSYASSERT(d);
	CHN_LOCKASSERT(parent);

	CHN_UNLOCK(c);

	if (!(parent->flags & CHN_F_BUSY))
		return (EBUSY);

	if (CHN_EMPTY(parent, children))
		return (EINVAL);

	/* remove us from our parent's children list */
	CHN_REMOVE(parent, c, children);

	if (CHN_EMPTY(parent, children)) {
		parent->flags &= ~(CHN_F_BUSY | CHN_F_HAS_VCHAN);
		chn_reset(parent, parent->format, parent->speed);
	}

	CHN_UNLOCK(parent);

	/* remove us from our grandparent's channel list */
	PCM_LOCK(d);
	ret = pcm_chn_remove(d, c);
	PCM_UNLOCK(d);

	/* destroy ourselves */
	if (ret == 0)
		ret = pcm_chn_destroy(c);

	CHN_LOCK(parent);

	return (ret);
}

int
#ifdef SND_DEBUG
vchan_passthrough(struct pcm_channel *c, const char *caller)
#else
vchan_sync(struct pcm_channel *c)
#endif
{
	int ret;

	KASSERT(c != NULL && c->parentchannel != NULL &&
	    (c->flags & CHN_F_VIRTUAL),
	    ("%s(): invalid passthrough", __func__));
	CHN_LOCKASSERT(c);
	CHN_LOCKASSERT(c->parentchannel);

	sndbuf_setspd(c->bufhard, c->parentchannel->speed);
	c->flags |= CHN_F_PASSTHROUGH;
	ret = feeder_chain(c);
	c->flags &= ~(CHN_F_DIRTY | CHN_F_PASSTHROUGH);
	if (ret != 0)
		c->flags |= CHN_F_DIRTY;

#ifdef SND_DEBUG
	if (snd_passthrough_verbose != 0) {
		char *devname, buf[CHN_NAMELEN];

		devname = dsp_unit2name(buf, sizeof(buf), c->unit);
		device_printf(c->dev,
		    "%s(%s/%s) %s() -> re-sync err=%d\n",
		    __func__, (devname != NULL) ? devname : "dspX", c->comm,
		    caller, ret);
	}
#endif

	return (ret);
}

void
vchan_initsys(device_t dev)
{
	struct snddev_info *d;
	int unit;

	unit = device_get_unit(dev);
	d = device_get_softc(dev);

	/* Play */
	SYSCTL_ADD_PROC(&d->play_sysctl_ctx,
	    SYSCTL_CHILDREN(d->play_sysctl_tree),
	    OID_AUTO, "vchans", CTLTYPE_INT | CTLFLAG_RWTUN,
	    VCHAN_SYSCTL_DATA(unit, PLAY), VCHAN_SYSCTL_DATA_SIZE,
	    sysctl_dev_pcm_vchans, "I", "total allocated virtual channel");
	SYSCTL_ADD_PROC(&d->play_sysctl_ctx,
	    SYSCTL_CHILDREN(d->play_sysctl_tree),
	    OID_AUTO, "vchanmode", CTLTYPE_STRING | CTLFLAG_RWTUN,
	    VCHAN_SYSCTL_DATA(unit, PLAY), VCHAN_SYSCTL_DATA_SIZE,
	    sysctl_dev_pcm_vchanmode, "A",
	    "vchan format/rate selection: 0=fixed, 1=passthrough, 2=adaptive");
	SYSCTL_ADD_PROC(&d->play_sysctl_ctx,
	    SYSCTL_CHILDREN(d->play_sysctl_tree),
	    OID_AUTO, "vchanrate", CTLTYPE_INT | CTLFLAG_RWTUN,
	    VCHAN_SYSCTL_DATA(unit, PLAY), VCHAN_SYSCTL_DATA_SIZE,
	    sysctl_dev_pcm_vchanrate, "I", "virtual channel mixing speed/rate");
	SYSCTL_ADD_PROC(&d->play_sysctl_ctx,
	    SYSCTL_CHILDREN(d->play_sysctl_tree),
	    OID_AUTO, "vchanformat", CTLTYPE_STRING | CTLFLAG_RWTUN,
	    VCHAN_SYSCTL_DATA(unit, PLAY), VCHAN_SYSCTL_DATA_SIZE,
	    sysctl_dev_pcm_vchanformat, "A", "virtual channel mixing format");
	/* Rec */
	SYSCTL_ADD_PROC(&d->rec_sysctl_ctx,
	    SYSCTL_CHILDREN(d->rec_sysctl_tree),
	    OID_AUTO, "vchans", CTLTYPE_INT | CTLFLAG_RWTUN,
	    VCHAN_SYSCTL_DATA(unit, REC), VCHAN_SYSCTL_DATA_SIZE,
	    sysctl_dev_pcm_vchans, "I", "total allocated virtual channel");
	SYSCTL_ADD_PROC(&d->rec_sysctl_ctx,
	    SYSCTL_CHILDREN(d->rec_sysctl_tree),
	    OID_AUTO, "vchanmode", CTLTYPE_STRING | CTLFLAG_RWTUN,
	    VCHAN_SYSCTL_DATA(unit, REC), VCHAN_SYSCTL_DATA_SIZE,
	    sysctl_dev_pcm_vchanmode, "A",
	    "vchan format/rate selection: 0=fixed, 1=passthrough, 2=adaptive");
	SYSCTL_ADD_PROC(&d->rec_sysctl_ctx,
	    SYSCTL_CHILDREN(d->rec_sysctl_tree),
	    OID_AUTO, "vchanrate", CTLTYPE_INT | CTLFLAG_RWTUN,
	    VCHAN_SYSCTL_DATA(unit, REC), VCHAN_SYSCTL_DATA_SIZE,
	    sysctl_dev_pcm_vchanrate, "I", "virtual channel mixing speed/rate");
	SYSCTL_ADD_PROC(&d->rec_sysctl_ctx,
	    SYSCTL_CHILDREN(d->rec_sysctl_tree),
	    OID_AUTO, "vchanformat", CTLTYPE_STRING | CTLFLAG_RWTUN,
	    VCHAN_SYSCTL_DATA(unit, REC), VCHAN_SYSCTL_DATA_SIZE,
	    sysctl_dev_pcm_vchanformat, "A", "virtual channel mixing format");
}
