/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2009 Ariff Abdullah <ariff@FreeBSD.org>
 * Portions Copyright (c) Ryan Beasley <ryan.beasley@gmail.com> - GSoC 2006
 * Copyright (c) 1999 Cameron Grant <cg@FreeBSD.org>
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

#include "feeder_if.h"
#include "mixer_if.h"

SND_DECLARE_FILE("$FreeBSD$");

static MALLOC_DEFINE(M_MIXER, "mixer", "mixer");

static int mixer_bypass = 1;
SYSCTL_INT(_hw_snd, OID_AUTO, vpc_mixer_bypass, CTLFLAG_RWTUN,
    &mixer_bypass, 0,
    "control channel pcm/rec volume, bypassing real mixer device");

#define MIXER_NAMELEN	16
struct snd_mixer {
	KOBJ_FIELDS;
	void *devinfo;
	int busy;
	int hwvol_muted;
	int hwvol_mixer;
	int hwvol_step;
	int type;
	device_t dev;
	u_int32_t hwvol_mute_level;
	u_int32_t devs;
	u_int32_t recdevs;
	u_int32_t recsrc;
	u_int16_t level[32];
	u_int8_t parent[32];
	u_int32_t child[32];
	u_int8_t realdev[32];
	char name[MIXER_NAMELEN];
	struct mtx *lock;
	oss_mixer_enuminfo enuminfo;
	/** 
	 * Counter is incremented when applications change any of this
	 * mixer's controls.  A change in value indicates that persistent
	 * mixer applications should update their displays.
	 */
	int modify_counter;
};

static u_int16_t snd_mixerdefaults[SOUND_MIXER_NRDEVICES] = {
	[SOUND_MIXER_VOLUME]	= 75,
	[SOUND_MIXER_BASS]	= 50,
	[SOUND_MIXER_TREBLE]	= 50,
	[SOUND_MIXER_SYNTH]	= 75,
	[SOUND_MIXER_PCM]	= 75,
	[SOUND_MIXER_SPEAKER]	= 75,
	[SOUND_MIXER_LINE]	= 75,
	[SOUND_MIXER_MIC] 	= 0,
	[SOUND_MIXER_CD]	= 75,
	[SOUND_MIXER_IGAIN]	= 0,
	[SOUND_MIXER_LINE1]	= 75,
	[SOUND_MIXER_VIDEO]	= 75,
	[SOUND_MIXER_RECLEV]	= 75,
	[SOUND_MIXER_OGAIN]	= 50,
	[SOUND_MIXER_MONITOR]	= 75,
};

static char* snd_mixernames[SOUND_MIXER_NRDEVICES] = SOUND_DEVICE_NAMES;

static d_open_t mixer_open;
static d_close_t mixer_close;
static d_ioctl_t mixer_ioctl;

static struct cdevsw mixer_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	mixer_open,
	.d_close =	mixer_close,
	.d_ioctl =	mixer_ioctl,
	.d_name =	"mixer",
};

/**
 * Keeps a count of mixer devices; used only by OSSv4 SNDCTL_SYSINFO ioctl.
 */
int mixer_count = 0;

static eventhandler_tag mixer_ehtag = NULL;

static struct cdev *
mixer_get_devt(device_t dev)
{
	struct snddev_info *snddev;

	snddev = device_get_softc(dev);

	return snddev->mixer_dev;
}

static int
mixer_lookup(char *devname)
{
	int i;

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		if (strncmp(devname, snd_mixernames[i],
		    strlen(snd_mixernames[i])) == 0)
			return i;
	return -1;
}

#define MIXER_SET_UNLOCK(x, y)		do {				\
	if ((y) != 0)							\
		snd_mtxunlock((x)->lock);				\
} while (0)

#define MIXER_SET_LOCK(x, y)		do {				\
	if ((y) != 0)							\
		snd_mtxlock((x)->lock);					\
} while (0)

static int
mixer_set_softpcmvol(struct snd_mixer *m, struct snddev_info *d,
    u_int left, u_int right)
{
	struct pcm_channel *c;
	int dropmtx, acquiremtx;

	if (!PCM_REGISTERED(d))
		return (EINVAL);

	if (mtx_owned(m->lock))
		dropmtx = 1;
	else
		dropmtx = 0;
	
	if (!(d->flags & SD_F_MPSAFE) || mtx_owned(d->lock) != 0)
		acquiremtx = 0;
	else
		acquiremtx = 1;

	/*
	 * Be careful here. If we're coming from cdev ioctl, it is OK to
	 * not doing locking AT ALL (except on individual channel) since
	 * we've been heavily guarded by pcm cv, or if we're still
	 * under Giant influence. Since we also have mix_* calls, we cannot
	 * assume such protection and just do the lock as usuall.
	 */
	MIXER_SET_UNLOCK(m, dropmtx);
	MIXER_SET_LOCK(d, acquiremtx);

	CHN_FOREACH(c, d, channels.pcm.busy) {
		CHN_LOCK(c);
		if (c->direction == PCMDIR_PLAY &&
		    (c->feederflags & (1 << FEEDER_VOLUME)))
			chn_setvolume_multi(c, SND_VOL_C_MASTER, left, right,
			    (left + right) >> 1);
		CHN_UNLOCK(c);
	}

	MIXER_SET_UNLOCK(d, acquiremtx);
	MIXER_SET_LOCK(m, dropmtx);

	return (0);
}

static int
mixer_set_eq(struct snd_mixer *m, struct snddev_info *d,
    u_int dev, u_int level)
{
	struct pcm_channel *c;
	struct pcm_feeder *f;
	int tone, dropmtx, acquiremtx;

	if (dev == SOUND_MIXER_TREBLE)
		tone = FEEDEQ_TREBLE;
	else if (dev == SOUND_MIXER_BASS)
		tone = FEEDEQ_BASS;
	else
		return (EINVAL);

	if (!PCM_REGISTERED(d))
		return (EINVAL);

	if (mtx_owned(m->lock))
		dropmtx = 1;
	else
		dropmtx = 0;
	
	if (!(d->flags & SD_F_MPSAFE) || mtx_owned(d->lock) != 0)
		acquiremtx = 0;
	else
		acquiremtx = 1;

	/*
	 * Be careful here. If we're coming from cdev ioctl, it is OK to
	 * not doing locking AT ALL (except on individual channel) since
	 * we've been heavily guarded by pcm cv, or if we're still
	 * under Giant influence. Since we also have mix_* calls, we cannot
	 * assume such protection and just do the lock as usuall.
	 */
	MIXER_SET_UNLOCK(m, dropmtx);
	MIXER_SET_LOCK(d, acquiremtx);

	CHN_FOREACH(c, d, channels.pcm.busy) {
		CHN_LOCK(c);
		f = chn_findfeeder(c, FEEDER_EQ);
		if (f != NULL)
			(void)FEEDER_SET(f, tone, level);
		CHN_UNLOCK(c);
	}

	MIXER_SET_UNLOCK(d, acquiremtx);
	MIXER_SET_LOCK(m, dropmtx);

	return (0);
}

static int
mixer_set(struct snd_mixer *m, u_int dev, u_int lev)
{
	struct snddev_info *d;
	u_int l, r, tl, tr;
	u_int32_t parent = SOUND_MIXER_NONE, child = 0;
	u_int32_t realdev;
	int i, dropmtx;

	if (m == NULL || dev >= SOUND_MIXER_NRDEVICES ||
	    (0 == (m->devs & (1 << dev))))
		return -1;

	l = min((lev & 0x00ff), 100);
	r = min(((lev & 0xff00) >> 8), 100);
	realdev = m->realdev[dev];

	d = device_get_softc(m->dev);
	if (d == NULL)
		return -1;

	/* It is safe to drop this mutex due to Giant. */
	if (!(d->flags & SD_F_MPSAFE) && mtx_owned(m->lock) != 0)
		dropmtx = 1;
	else
		dropmtx = 0;

	MIXER_SET_UNLOCK(m, dropmtx);

	/* TODO: recursive handling */
	parent = m->parent[dev];
	if (parent >= SOUND_MIXER_NRDEVICES)
		parent = SOUND_MIXER_NONE;
	if (parent == SOUND_MIXER_NONE)
		child = m->child[dev];

	if (parent != SOUND_MIXER_NONE) {
		tl = (l * (m->level[parent] & 0x00ff)) / 100;
		tr = (r * ((m->level[parent] & 0xff00) >> 8)) / 100;
		if (dev == SOUND_MIXER_PCM && (d->flags & SD_F_SOFTPCMVOL))
			(void)mixer_set_softpcmvol(m, d, tl, tr);
		else if (realdev != SOUND_MIXER_NONE &&
		    MIXER_SET(m, realdev, tl, tr) < 0) {
			MIXER_SET_LOCK(m, dropmtx);
			return -1;
		}
	} else if (child != 0) {
		for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
			if (!(child & (1 << i)) || m->parent[i] != dev)
				continue;
			realdev = m->realdev[i];
			tl = (l * (m->level[i] & 0x00ff)) / 100;
			tr = (r * ((m->level[i] & 0xff00) >> 8)) / 100;
			if (i == SOUND_MIXER_PCM &&
			    (d->flags & SD_F_SOFTPCMVOL))
				(void)mixer_set_softpcmvol(m, d, tl, tr);
			else if (realdev != SOUND_MIXER_NONE)
				MIXER_SET(m, realdev, tl, tr);
		}
		realdev = m->realdev[dev];
		if (realdev != SOUND_MIXER_NONE &&
		    MIXER_SET(m, realdev, l, r) < 0) {
				MIXER_SET_LOCK(m, dropmtx);
				return -1;
		}
	} else {
		if (dev == SOUND_MIXER_PCM && (d->flags & SD_F_SOFTPCMVOL))
			(void)mixer_set_softpcmvol(m, d, l, r);
		else if ((dev == SOUND_MIXER_TREBLE ||
		    dev == SOUND_MIXER_BASS) && (d->flags & SD_F_EQ))
			(void)mixer_set_eq(m, d, dev, (l + r) >> 1);
		else if (realdev != SOUND_MIXER_NONE &&
		    MIXER_SET(m, realdev, l, r) < 0) {
			MIXER_SET_LOCK(m, dropmtx);
			return -1;
		}
	}

	MIXER_SET_LOCK(m, dropmtx);

	m->level[dev] = l | (r << 8);
	m->modify_counter++;

	return 0;
}

static int
mixer_get(struct snd_mixer *mixer, int dev)
{
	if ((dev < SOUND_MIXER_NRDEVICES) && (mixer->devs & (1 << dev)))
		return mixer->level[dev];
	else
		return -1;
}

static int
mixer_setrecsrc(struct snd_mixer *mixer, u_int32_t src)
{
	struct snddev_info *d;
	u_int32_t recsrc;
	int dropmtx;

	d = device_get_softc(mixer->dev);
	if (d == NULL)
		return -1;
	if (!(d->flags & SD_F_MPSAFE) && mtx_owned(mixer->lock) != 0)
		dropmtx = 1;
	else
		dropmtx = 0;
	src &= mixer->recdevs;
	if (src == 0)
		src = mixer->recdevs & SOUND_MASK_MIC;
	if (src == 0)
		src = mixer->recdevs & SOUND_MASK_MONITOR;
	if (src == 0)
		src = mixer->recdevs & SOUND_MASK_LINE;
	if (src == 0 && mixer->recdevs != 0)
		src = (1 << (ffs(mixer->recdevs) - 1));
	/* It is safe to drop this mutex due to Giant. */
	MIXER_SET_UNLOCK(mixer, dropmtx);
	recsrc = MIXER_SETRECSRC(mixer, src);
	MIXER_SET_LOCK(mixer, dropmtx);

	mixer->recsrc = recsrc;

	return 0;
}

static int
mixer_getrecsrc(struct snd_mixer *mixer)
{
	return mixer->recsrc;
}

/**
 * @brief Retrieve the route number of the current recording device
 *
 * OSSv4 assigns routing numbers to recording devices, unlike the previous
 * API which relied on a fixed table of device numbers and names.  This
 * function returns the routing number of the device currently selected
 * for recording.
 *
 * For now, this function is kind of a goofy compatibility stub atop the
 * existing sound system.  (For example, in theory, the old sound system
 * allows multiple recording devices to be specified via a bitmask.)
 *
 * @param m	mixer context container thing
 *
 * @retval 0		success
 * @retval EIDRM	no recording device found (generally not possible)
 * @todo Ask about error code
 */
static int
mixer_get_recroute(struct snd_mixer *m, int *route)
{
	int i, cnt;

	cnt = 0;

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		/** @todo can user set a multi-device mask? (== or &?) */
		if ((1 << i) == m->recsrc)
			break;
		if ((1 << i) & m->recdevs)
			++cnt;
	}

	if (i == SOUND_MIXER_NRDEVICES)
		return EIDRM;

	*route = cnt;
	return 0;
}

/**
 * @brief Select a device for recording
 *
 * This function sets a recording source based on a recording device's
 * routing number.  Said number is translated to an old school recdev
 * mask and passed over mixer_setrecsrc. 
 *
 * @param m	mixer context container thing
 *
 * @retval 0		success(?)
 * @retval EINVAL	User specified an invalid device number
 * @retval otherwise	error from mixer_setrecsrc
 */
static int
mixer_set_recroute(struct snd_mixer *m, int route)
{
	int i, cnt, ret;

	ret = 0;
	cnt = 0;

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if ((1 << i) & m->recdevs) {
			if (route == cnt)
				break;
			++cnt;
		}
	}

	if (i == SOUND_MIXER_NRDEVICES)
		ret = EINVAL;
	else
		ret = mixer_setrecsrc(m, (1 << i));

	return ret;
}

void
mix_setdevs(struct snd_mixer *m, u_int32_t v)
{
	struct snddev_info *d;
	int i;

	if (m == NULL)
		return;

	d = device_get_softc(m->dev);
	if (d != NULL && (d->flags & SD_F_SOFTPCMVOL))
		v |= SOUND_MASK_PCM;
	if (d != NULL && (d->flags & SD_F_EQ))
		v |= SOUND_MASK_TREBLE | SOUND_MASK_BASS;
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (m->parent[i] < SOUND_MIXER_NRDEVICES)
			v |= 1 << m->parent[i];
		v |= m->child[i];
	}
	m->devs = v;
}

/**
 * @brief Record mask of available recording devices
 *
 * Calling functions are responsible for defining the mask of available
 * recording devices.  This function records that value in a structure
 * used by the rest of the mixer code.
 *
 * This function also populates a structure used by the SNDCTL_DSP_*RECSRC*
 * family of ioctls that are part of OSSV4.  All recording device labels
 * are concatenated in ascending order corresponding to their routing
 * numbers.  (Ex:  a system might have 0 => 'vol', 1 => 'cd', 2 => 'line',
 * etc.)  For now, these labels are just the standard recording device
 * names (cd, line1, etc.), but will eventually be fully dynamic and user
 * controlled.
 *
 * @param m	mixer device context container thing
 * @param v	mask of recording devices
 */
void
mix_setrecdevs(struct snd_mixer *m, u_int32_t v)
{
	oss_mixer_enuminfo *ei;
	char *loc;
	int i, nvalues, nwrote, nleft, ncopied;

	ei = &m->enuminfo;

	nvalues = 0;
	nwrote = 0;
	nleft = sizeof(ei->strings);
	loc = ei->strings;

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if ((1 << i) & v) {
			ei->strindex[nvalues] = nwrote;
			ncopied = strlcpy(loc, snd_mixernames[i], nleft) + 1;
			    /* strlcpy retval doesn't include terminator */

			nwrote += ncopied;
			nleft -= ncopied;
			nvalues++;

			/*
			 * XXX I don't think this should ever be possible.
			 * Even with a move to dynamic device/channel names,
			 * each label is limited to ~16 characters, so that'd
			 * take a LOT to fill this buffer.
			 */
			if ((nleft <= 0) || (nvalues >= OSS_ENUM_MAXVALUE)) {
				device_printf(m->dev,
				    "mix_setrecdevs:  Not enough room to store device names--please file a bug report.\n");
				device_printf(m->dev, 
				    "mix_setrecdevs:  Please include details about your sound hardware, OS version, etc.\n");
				break;
			}

			loc = &ei->strings[nwrote];
		}
	}

	/*
	 * NB:	The SNDCTL_DSP_GET_RECSRC_NAMES ioctl ignores the dev
	 * 	and ctrl fields.
	 */
	ei->nvalues = nvalues;
	m->recdevs = v;
}

void
mix_setparentchild(struct snd_mixer *m, u_int32_t parent, u_int32_t childs)
{
	u_int32_t mask = 0;
	int i;

	if (m == NULL || parent >= SOUND_MIXER_NRDEVICES)
		return;
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (i == parent)
			continue;
		if (childs & (1 << i)) {
			mask |= 1 << i;
			if (m->parent[i] < SOUND_MIXER_NRDEVICES)
				m->child[m->parent[i]] &= ~(1 << i);
			m->parent[i] = parent;
			m->child[i] = 0;
		}
	}
	mask &= ~(1 << parent);
	m->child[parent] = mask;
}

void
mix_setrealdev(struct snd_mixer *m, u_int32_t dev, u_int32_t realdev)
{
	if (m == NULL || dev >= SOUND_MIXER_NRDEVICES ||
	    !(realdev == SOUND_MIXER_NONE || realdev < SOUND_MIXER_NRDEVICES))
		return;
	m->realdev[dev] = realdev;
}

u_int32_t
mix_getparent(struct snd_mixer *m, u_int32_t dev)
{
	if (m == NULL || dev >= SOUND_MIXER_NRDEVICES)
		return SOUND_MIXER_NONE;
	return m->parent[dev];
}

u_int32_t
mix_getchild(struct snd_mixer *m, u_int32_t dev)
{
	if (m == NULL || dev >= SOUND_MIXER_NRDEVICES)
		return 0;
	return m->child[dev];
}

u_int32_t
mix_getdevs(struct snd_mixer *m)
{
	return m->devs;
}

u_int32_t
mix_getrecdevs(struct snd_mixer *m)
{
	return m->recdevs;
}

void *
mix_getdevinfo(struct snd_mixer *m)
{
	return m->devinfo;
}

static struct snd_mixer *
mixer_obj_create(device_t dev, kobj_class_t cls, void *devinfo,
    int type, const char *desc)
{
	struct snd_mixer *m;
	int i;

	KASSERT(dev != NULL && cls != NULL && devinfo != NULL,
	    ("%s(): NULL data dev=%p cls=%p devinfo=%p",
	    __func__, dev, cls, devinfo));
	KASSERT(type == MIXER_TYPE_PRIMARY || type == MIXER_TYPE_SECONDARY,
	    ("invalid mixer type=%d", type));

	m = (struct snd_mixer *)kobj_create(cls, M_MIXER, M_WAITOK | M_ZERO);
	snprintf(m->name, sizeof(m->name), "%s:mixer",
	    device_get_nameunit(dev));
	if (desc != NULL) {
		strlcat(m->name, ":", sizeof(m->name));
		strlcat(m->name, desc, sizeof(m->name));
	}
	m->lock = snd_mtxcreate(m->name, (type == MIXER_TYPE_PRIMARY) ?
	    "primary pcm mixer" : "secondary pcm mixer");
	m->type = type;
	m->devinfo = devinfo;
	m->busy = 0;
	m->dev = dev;
	for (i = 0; i < (sizeof(m->parent) / sizeof(m->parent[0])); i++) {
		m->parent[i] = SOUND_MIXER_NONE;
		m->child[i] = 0;
		m->realdev[i] = i;
	}

	if (MIXER_INIT(m)) {
		snd_mtxlock(m->lock);
		snd_mtxfree(m->lock);
		kobj_delete((kobj_t)m, M_MIXER);
		return (NULL);
	}

	return (m);
}

int
mixer_delete(struct snd_mixer *m)
{
	KASSERT(m != NULL, ("NULL snd_mixer"));
	KASSERT(m->type == MIXER_TYPE_SECONDARY,
	    ("%s(): illegal mixer type=%d", __func__, m->type));

	/* mixer uninit can sleep --hps */

	MIXER_UNINIT(m);

	snd_mtxfree(m->lock);
	kobj_delete((kobj_t)m, M_MIXER);

	--mixer_count;

	return (0);
}

struct snd_mixer *
mixer_create(device_t dev, kobj_class_t cls, void *devinfo, const char *desc)
{
	struct snd_mixer *m;

	m = mixer_obj_create(dev, cls, devinfo, MIXER_TYPE_SECONDARY, desc);

	if (m != NULL)
		++mixer_count;

	return (m);
}

int
mixer_init(device_t dev, kobj_class_t cls, void *devinfo)
{
	struct snddev_info *snddev;
	struct snd_mixer *m;
	u_int16_t v;
	struct cdev *pdev;
	int i, unit, devunit, val;

	snddev = device_get_softc(dev);
	if (snddev == NULL)
		return (-1);

	if (resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "eq", &val) == 0 && val != 0) {
		snddev->flags |= SD_F_EQ;
		if ((val & SD_F_EQ_MASK) == val)
			snddev->flags |= val;
		else
			snddev->flags |= SD_F_EQ_DEFAULT;
		snddev->eqpreamp = 0;
	}

	m = mixer_obj_create(dev, cls, devinfo, MIXER_TYPE_PRIMARY, NULL);
	if (m == NULL)
		return (-1);

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		v = snd_mixerdefaults[i];

		if (resource_int_value(device_get_name(dev),
		    device_get_unit(dev), snd_mixernames[i], &val) == 0) {
			if (val >= 0 && val <= 100) {
				v = (u_int16_t) val;
			}
		}

		mixer_set(m, i, v | (v << 8));
	}

	mixer_setrecsrc(m, 0); /* Set default input. */

	unit = device_get_unit(dev);
	devunit = snd_mkunit(unit, SND_DEV_CTL, 0);
	pdev = make_dev(&mixer_cdevsw, PCMMINOR(devunit),
		 UID_ROOT, GID_WHEEL, 0666, "mixer%d", unit);
	pdev->si_drv1 = m;
	snddev->mixer_dev = pdev;

	++mixer_count;

	if (bootverbose) {
		for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
			if (!(m->devs & (1 << i)))
				continue;
			if (m->realdev[i] != i) {
				device_printf(dev, "Mixer \"%s\" -> \"%s\":",
				    snd_mixernames[i],
				    (m->realdev[i] < SOUND_MIXER_NRDEVICES) ?
				    snd_mixernames[m->realdev[i]] : "none");
			} else {
				device_printf(dev, "Mixer \"%s\":",
				    snd_mixernames[i]);
			}
			if (m->parent[i] < SOUND_MIXER_NRDEVICES)
				printf(" parent=\"%s\"",
				    snd_mixernames[m->parent[i]]);
			if (m->child[i] != 0)
				printf(" child=0x%08x", m->child[i]);
			printf("\n");
		}
		if (snddev->flags & SD_F_SOFTPCMVOL)
			device_printf(dev, "Soft PCM mixer ENABLED\n");
		if (snddev->flags & SD_F_EQ)
			device_printf(dev, "EQ Treble/Bass ENABLED\n");
	}

	return (0);
}

int
mixer_uninit(device_t dev)
{
	int i;
	struct snddev_info *d;
	struct snd_mixer *m;
	struct cdev *pdev;

	d = device_get_softc(dev);
	pdev = mixer_get_devt(dev);
	if (d == NULL || pdev == NULL || pdev->si_drv1 == NULL)
		return EBADF;

	m = pdev->si_drv1;
	KASSERT(m != NULL, ("NULL snd_mixer"));
	KASSERT(m->type == MIXER_TYPE_PRIMARY,
	    ("%s(): illegal mixer type=%d", __func__, m->type));

	snd_mtxlock(m->lock);

	if (m->busy) {
		snd_mtxunlock(m->lock);
		return EBUSY;
	}

	/* destroy dev can sleep --hps */

	snd_mtxunlock(m->lock);

	pdev->si_drv1 = NULL;
	destroy_dev(pdev);

	snd_mtxlock(m->lock);

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		mixer_set(m, i, 0);

	mixer_setrecsrc(m, SOUND_MASK_MIC);

	snd_mtxunlock(m->lock);

	/* mixer uninit can sleep --hps */

	MIXER_UNINIT(m);

	snd_mtxfree(m->lock);
	kobj_delete((kobj_t)m, M_MIXER);

	d->mixer_dev = NULL;

	--mixer_count;

	return 0;
}

int
mixer_reinit(device_t dev)
{
	struct snd_mixer *m;
	struct cdev *pdev;
	int i;

	pdev = mixer_get_devt(dev);
	m = pdev->si_drv1;
	snd_mtxlock(m->lock);

	i = MIXER_REINIT(m);
	if (i) {
		snd_mtxunlock(m->lock);
		return i;
	}

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		mixer_set(m, i, m->level[i]);

	mixer_setrecsrc(m, m->recsrc);
	snd_mtxunlock(m->lock);

	return 0;
}

static int
sysctl_hw_snd_hwvol_mixer(SYSCTL_HANDLER_ARGS)
{
	char devname[32];
	int error, dev;
	struct snd_mixer *m;

	m = oidp->oid_arg1;
	snd_mtxlock(m->lock);
	strlcpy(devname, snd_mixernames[m->hwvol_mixer], sizeof(devname));
	snd_mtxunlock(m->lock);
	error = sysctl_handle_string(oidp, &devname[0], sizeof(devname), req);
	snd_mtxlock(m->lock);
	if (error == 0 && req->newptr != NULL) {
		dev = mixer_lookup(devname);
		if (dev == -1) {
			snd_mtxunlock(m->lock);
			return EINVAL;
		}
		else if (dev != m->hwvol_mixer) {
			m->hwvol_mixer = dev;
			m->hwvol_muted = 0;
		}
	}
	snd_mtxunlock(m->lock);
	return error;
}

int
mixer_hwvol_init(device_t dev)
{
	struct snd_mixer *m;
	struct cdev *pdev;

	pdev = mixer_get_devt(dev);
	m = pdev->si_drv1;

	m->hwvol_mixer = SOUND_MIXER_VOLUME;
	m->hwvol_step = 5;
	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
            OID_AUTO, "hwvol_step", CTLFLAG_RWTUN, &m->hwvol_step, 0, "");
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
            OID_AUTO, "hwvol_mixer", CTLTYPE_STRING | CTLFLAG_RWTUN, m, 0,
	    sysctl_hw_snd_hwvol_mixer, "A", "");
	return 0;
}

void
mixer_hwvol_mute_locked(struct snd_mixer *m)
{
	if (m->hwvol_muted) {
		m->hwvol_muted = 0;
		mixer_set(m, m->hwvol_mixer, m->hwvol_mute_level);
	} else {
		m->hwvol_muted++;
		m->hwvol_mute_level = mixer_get(m, m->hwvol_mixer);
		mixer_set(m, m->hwvol_mixer, 0);
	}
}

void
mixer_hwvol_mute(device_t dev)
{
	struct snd_mixer *m;
	struct cdev *pdev;

	pdev = mixer_get_devt(dev);
	m = pdev->si_drv1;
	snd_mtxlock(m->lock);
	mixer_hwvol_mute_locked(m);
	snd_mtxunlock(m->lock);
}

void
mixer_hwvol_step_locked(struct snd_mixer *m, int left_step, int right_step)
{
	int level, left, right;

	if (m->hwvol_muted) {
		m->hwvol_muted = 0;
		level = m->hwvol_mute_level;
	} else
		level = mixer_get(m, m->hwvol_mixer);
	if (level != -1) {
		left = level & 0xff;
		right = (level >> 8) & 0xff;
		left += left_step * m->hwvol_step;
		if (left < 0)
			left = 0;
		else if (left > 100)
			left = 100;
		right += right_step * m->hwvol_step;
		if (right < 0)
			right = 0;
		else if (right > 100)
			right = 100;
		mixer_set(m, m->hwvol_mixer, left | right << 8);
	}
}

void
mixer_hwvol_step(device_t dev, int left_step, int right_step)
{
	struct snd_mixer *m;
	struct cdev *pdev;

	pdev = mixer_get_devt(dev);
	m = pdev->si_drv1;
	snd_mtxlock(m->lock);
	mixer_hwvol_step_locked(m, left_step, right_step);
	snd_mtxunlock(m->lock);
}

int
mixer_busy(struct snd_mixer *m)
{
	KASSERT(m != NULL, ("NULL snd_mixer"));

	return (m->busy);
}

int
mix_set(struct snd_mixer *m, u_int dev, u_int left, u_int right)
{
	int ret;

	KASSERT(m != NULL, ("NULL snd_mixer"));

	snd_mtxlock(m->lock);
	ret = mixer_set(m, dev, left | (right << 8));
	snd_mtxunlock(m->lock);

	return ((ret != 0) ? ENXIO : 0);
}

int
mix_get(struct snd_mixer *m, u_int dev)
{
	int ret;

	KASSERT(m != NULL, ("NULL snd_mixer"));

	snd_mtxlock(m->lock);
	ret = mixer_get(m, dev);
	snd_mtxunlock(m->lock);

	return (ret);
}

int
mix_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	int ret;

	KASSERT(m != NULL, ("NULL snd_mixer"));

	snd_mtxlock(m->lock);
	ret = mixer_setrecsrc(m, src);
	snd_mtxunlock(m->lock);

	return ((ret != 0) ? ENXIO : 0);
}

u_int32_t
mix_getrecsrc(struct snd_mixer *m)
{
	u_int32_t ret;

	KASSERT(m != NULL, ("NULL snd_mixer"));

	snd_mtxlock(m->lock);
	ret = mixer_getrecsrc(m);
	snd_mtxunlock(m->lock);

	return (ret);
}

int
mix_get_type(struct snd_mixer *m)
{
	KASSERT(m != NULL, ("NULL snd_mixer"));

	return (m->type);
}

/* ----------------------------------------------------------------------- */

static int
mixer_open(struct cdev *i_dev, int flags, int mode, struct thread *td)
{
	struct snddev_info *d;
	struct snd_mixer *m;


	if (i_dev == NULL || i_dev->si_drv1 == NULL)
		return (EBADF);

	m = i_dev->si_drv1;
	d = device_get_softc(m->dev);
	if (!PCM_REGISTERED(d))
		return (EBADF);

	/* XXX Need Giant magic entry ??? */

	snd_mtxlock(m->lock);
	m->busy = 1;
	snd_mtxunlock(m->lock);

	return (0);
}

static int
mixer_close(struct cdev *i_dev, int flags, int mode, struct thread *td)
{
	struct snddev_info *d;
	struct snd_mixer *m;
	int ret;

	if (i_dev == NULL || i_dev->si_drv1 == NULL)
		return (EBADF);

	m = i_dev->si_drv1;
	d = device_get_softc(m->dev);
	if (!PCM_REGISTERED(d))
		return (EBADF);

	/* XXX Need Giant magic entry ??? */

	snd_mtxlock(m->lock);
	ret = (m->busy == 0) ? EBADF : 0;
	m->busy = 0;
	snd_mtxunlock(m->lock);

	return (ret);
}

static int
mixer_ioctl_channel(struct cdev *dev, u_long cmd, caddr_t arg, int mode,
    struct thread *td, int from)
{
	struct snddev_info *d;
	struct snd_mixer *m;
	struct pcm_channel *c, *rdch, *wrch;
	pid_t pid;
	int j, ret;

	if (td == NULL || td->td_proc == NULL)
		return (-1);

	m = dev->si_drv1;
	d = device_get_softc(m->dev);
	j = cmd & 0xff;

	switch (j) {
	case SOUND_MIXER_PCM:
	case SOUND_MIXER_RECLEV:
	case SOUND_MIXER_DEVMASK:
	case SOUND_MIXER_CAPS:
	case SOUND_MIXER_STEREODEVS:
		break;
	default:
		return (-1);
		break;
	}

	pid = td->td_proc->p_pid;
	rdch = NULL;
	wrch = NULL;
	c = NULL;
	ret = -1;

	/*
	 * This is unfair. Imagine single proc opening multiple
	 * instances of same direction. What we do right now
	 * is looking for the first matching proc/pid, and just
	 * that. Nothing more. Consider it done.
	 *
	 * The better approach of controlling specific channel
	 * pcm or rec volume is by doing mixer ioctl
	 * (SNDCTL_DSP_[SET|GET][PLAY|REC]VOL / SOUND_MIXER_[PCM|RECLEV]
	 * on its open fd, rather than cracky mixer bypassing here.
	 */
	CHN_FOREACH(c, d, channels.pcm.opened) {
		CHN_LOCK(c);
		if (c->pid != pid ||
		    !(c->feederflags & (1 << FEEDER_VOLUME))) {
			CHN_UNLOCK(c);
			continue;
		}
		if (rdch == NULL && c->direction == PCMDIR_REC) {
			rdch = c;
			if (j == SOUND_MIXER_RECLEV)
				goto mixer_ioctl_channel_proc;
		} else if (wrch == NULL && c->direction == PCMDIR_PLAY) {
			wrch = c;
			if (j == SOUND_MIXER_PCM)
				goto mixer_ioctl_channel_proc;
		}
		CHN_UNLOCK(c);
		if (rdch != NULL && wrch != NULL)
			break;
	}

	if (rdch == NULL && wrch == NULL)
		return (-1);

	if ((j == SOUND_MIXER_DEVMASK || j == SOUND_MIXER_CAPS ||
	    j == SOUND_MIXER_STEREODEVS) &&
	    (cmd & ~0xff) == MIXER_READ(0)) {
		snd_mtxlock(m->lock);
		*(int *)arg = mix_getdevs(m);
		snd_mtxunlock(m->lock);
		if (rdch != NULL)
			*(int *)arg |= SOUND_MASK_RECLEV;
		if (wrch != NULL)
			*(int *)arg |= SOUND_MASK_PCM;
		ret = 0;
	}

	return (ret);

mixer_ioctl_channel_proc:

	KASSERT(c != NULL, ("%s(): NULL channel", __func__));
	CHN_LOCKASSERT(c);

	if ((cmd & ~0xff) == MIXER_WRITE(0)) {
		int left, right, center;

		left = *(int *)arg & 0x7f;
		right = (*(int *)arg >> 8) & 0x7f;
		center = (left + right) >> 1;
		chn_setvolume_multi(c, SND_VOL_C_PCM, left, right, center);
	} else if ((cmd & ~0xff) == MIXER_READ(0)) {
		*(int *)arg = CHN_GETVOLUME(c, SND_VOL_C_PCM, SND_CHN_T_FL);
		*(int *)arg |=
		    CHN_GETVOLUME(c, SND_VOL_C_PCM, SND_CHN_T_FR) << 8;
	}

	CHN_UNLOCK(c);

	return (0);
}

static int
mixer_ioctl(struct cdev *i_dev, u_long cmd, caddr_t arg, int mode,
    struct thread *td)
{
	struct snddev_info *d;
	int ret;

	if (i_dev == NULL || i_dev->si_drv1 == NULL)
		return (EBADF);

	d = device_get_softc(((struct snd_mixer *)i_dev->si_drv1)->dev);
	if (!PCM_REGISTERED(d))
		return (EBADF);

	PCM_GIANT_ENTER(d);
	PCM_ACQUIRE_QUICK(d);

	ret = -1;

	if (mixer_bypass != 0 && (d->flags & SD_F_VPC))
		ret = mixer_ioctl_channel(i_dev, cmd, arg, mode, td,
		    MIXER_CMD_CDEV);

	if (ret == -1)
		ret = mixer_ioctl_cmd(i_dev, cmd, arg, mode, td,
		    MIXER_CMD_CDEV);

	PCM_RELEASE_QUICK(d);
	PCM_GIANT_LEAVE(d);

	return (ret);
}

static void
mixer_mixerinfo(struct snd_mixer *m, mixer_info *mi)
{
	bzero((void *)mi, sizeof(*mi));
	strlcpy(mi->id, m->name, sizeof(mi->id));
	strlcpy(mi->name, device_get_desc(m->dev), sizeof(mi->name));
	mi->modify_counter = m->modify_counter;
}

/*
 * XXX Make sure you can guarantee concurrency safety before calling this
 *     function, be it through Giant, PCM_*, etc !
 */
int
mixer_ioctl_cmd(struct cdev *i_dev, u_long cmd, caddr_t arg, int mode,
    struct thread *td, int from)
{
	struct snd_mixer *m;
	int ret = EINVAL, *arg_i = (int *)arg;
	int v = -1, j = cmd & 0xff;

	/*
	 * Certain ioctls may be made on any type of device (audio, mixer,
	 * and MIDI).  Handle those special cases here.
	 */
	if (IOCGROUP(cmd) == 'X') {
		switch (cmd) {
		case SNDCTL_SYSINFO:
			sound_oss_sysinfo((oss_sysinfo *)arg);
			return (0);
		case SNDCTL_CARDINFO:
			return (sound_oss_card_info((oss_card_info *)arg));
	    	case SNDCTL_AUDIOINFO:
	    	case SNDCTL_AUDIOINFO_EX:
	    	case SNDCTL_ENGINEINFO:
			return (dsp_oss_audioinfo(i_dev, (oss_audioinfo *)arg));
		case SNDCTL_MIXERINFO:
			return (mixer_oss_mixerinfo(i_dev, (oss_mixerinfo *)arg));
		}
		return (EINVAL);
	}

	m = i_dev->si_drv1;

	if (m == NULL)
		return (EBADF);

	snd_mtxlock(m->lock);
	if (from == MIXER_CMD_CDEV && !m->busy) {
		snd_mtxunlock(m->lock);
		return (EBADF);
	}
	switch (cmd) {
	case SNDCTL_DSP_GET_RECSRC_NAMES:
		bcopy((void *)&m->enuminfo, arg, sizeof(oss_mixer_enuminfo));
		ret = 0;
		goto done;
	case SNDCTL_DSP_GET_RECSRC:
		ret = mixer_get_recroute(m, arg_i);
		goto done;
	case SNDCTL_DSP_SET_RECSRC:
		ret = mixer_set_recroute(m, *arg_i);
		goto done;
	case OSS_GETVERSION:
		*arg_i = SOUND_VERSION;
		ret = 0;
		goto done;
	case SOUND_MIXER_INFO:
		mixer_mixerinfo(m, (mixer_info *)arg);
		ret = 0;
		goto done;
	}
	if ((cmd & ~0xff) == MIXER_WRITE(0)) {
		if (j == SOUND_MIXER_RECSRC)
			ret = mixer_setrecsrc(m, *arg_i);
		else
			ret = mixer_set(m, j, *arg_i);
		snd_mtxunlock(m->lock);
		return ((ret == 0) ? 0 : ENXIO);
	}
	if ((cmd & ~0xff) == MIXER_READ(0)) {
		switch (j) {
		case SOUND_MIXER_DEVMASK:
		case SOUND_MIXER_CAPS:
		case SOUND_MIXER_STEREODEVS:
			v = mix_getdevs(m);
			break;
		case SOUND_MIXER_RECMASK:
			v = mix_getrecdevs(m);
			break;
		case SOUND_MIXER_RECSRC:
			v = mixer_getrecsrc(m);
			break;
		default:
			v = mixer_get(m, j);
		}
		*arg_i = v;
		snd_mtxunlock(m->lock);
		return ((v != -1) ? 0 : ENXIO);
	}
done:
	snd_mtxunlock(m->lock);
	return (ret);
}

static void
mixer_clone(void *arg,
    struct ucred *cred,
    char *name, int namelen, struct cdev **dev)
{
	struct snddev_info *d;

	if (*dev != NULL)
		return;
	if (strcmp(name, "mixer") == 0) {
		d = devclass_get_softc(pcm_devclass, snd_unit);
		if (PCM_REGISTERED(d) && d->mixer_dev != NULL) {
			*dev = d->mixer_dev;
			dev_ref(*dev);
		}
	}
}

static void
mixer_sysinit(void *p)
{
	if (mixer_ehtag != NULL)
		return;
	mixer_ehtag = EVENTHANDLER_REGISTER(dev_clone, mixer_clone, 0, 1000);
}

static void
mixer_sysuninit(void *p)
{
	if (mixer_ehtag == NULL)
		return;
	EVENTHANDLER_DEREGISTER(dev_clone, mixer_ehtag);
	mixer_ehtag = NULL;
}

SYSINIT(mixer_sysinit, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, mixer_sysinit, NULL);
SYSUNINIT(mixer_sysuninit, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, mixer_sysuninit, NULL);

/**
 * @brief Handler for SNDCTL_MIXERINFO
 *
 * This function searches for a mixer based on the numeric ID stored
 * in oss_miserinfo::dev.  If set to -1, then information about the
 * current mixer handling the request is provided.  Note, however, that
 * this ioctl may be made with any sound device (audio, mixer, midi).
 *
 * @note Caller must not hold any PCM device, channel, or mixer locks.
 *
 * See http://manuals.opensound.com/developer/SNDCTL_MIXERINFO.html for
 * more information.
 *
 * @param i_dev	character device on which the ioctl arrived
 * @param arg	user argument (oss_mixerinfo *)
 *
 * @retval EINVAL	oss_mixerinfo::dev specified a bad value
 * @retval 0		success
 */
int
mixer_oss_mixerinfo(struct cdev *i_dev, oss_mixerinfo *mi)
{
	struct snddev_info *d;
	struct snd_mixer *m;
	int nmix, i;

	/*
	 * If probing the device handling the ioctl, make sure it's a mixer
	 * device.  (This ioctl is valid on audio, mixer, and midi devices.)
	 */
	if (mi->dev == -1 && i_dev->si_devsw != &mixer_cdevsw)
		return (EINVAL);

	d = NULL;
	m = NULL;
	nmix = 0;

	/*
	 * There's a 1:1 relationship between mixers and PCM devices, so
	 * begin by iterating over PCM devices and search for our mixer.
	 */
	for (i = 0; pcm_devclass != NULL &&
	    i < devclass_get_maxunit(pcm_devclass); i++) {
		d = devclass_get_softc(pcm_devclass, i);
		if (!PCM_REGISTERED(d))
			continue;

		/* XXX Need Giant magic entry */

		/* See the note in function docblock. */
		PCM_UNLOCKASSERT(d);
		PCM_LOCK(d);

		if (d->mixer_dev != NULL && d->mixer_dev->si_drv1 != NULL &&
		    ((mi->dev == -1 && d->mixer_dev == i_dev) ||
		    mi->dev == nmix)) {
			m = d->mixer_dev->si_drv1;
			mtx_lock(m->lock);

			/*
			 * At this point, the following synchronization stuff
			 * has happened:
			 * - a specific PCM device is locked.
			 * - a specific mixer device has been locked, so be
			 *   sure to unlock when existing.
			 */
			bzero((void *)mi, sizeof(*mi));
			mi->dev = nmix;
			snprintf(mi->id, sizeof(mi->id), "mixer%d", i);
			strlcpy(mi->name, m->name, sizeof(mi->name));
			mi->modify_counter = m->modify_counter;
			mi->card_number = i;
			/*
			 * Currently, FreeBSD assumes 1:1 relationship between
			 * a pcm and mixer devices, so this is hardcoded to 0.
			 */
			mi->port_number = 0;

			/**
			 * @todo Fill in @sa oss_mixerinfo::mixerhandle.
			 * @note From 4Front:  "mixerhandle is an arbitrary
			 *       string that identifies the mixer better than
			 *       the device number (mixerinfo.dev).  Device
			 *       numbers may change depending on the order the
			 *       drivers are loaded. However the handle should
			 *       remain the same provided that the sound card
			 *       is not moved to another PCI slot."
			 */

			/**
			 * @note
			 * @sa oss_mixerinfo::magic is a reserved field.
			 * 
			 * @par
			 * From 4Front:  "magic is usually 0. However some
			 * devices may have dedicated setup utilities and the
			 * magic field may contain an unique driver specific
			 * value (managed by [4Front])."
			 */

			mi->enabled = device_is_attached(m->dev) ? 1 : 0;
			/**
			 * The only flag for @sa oss_mixerinfo::caps is
			 * currently MIXER_CAP_VIRTUAL, which I'm not sure we
			 * really worry about.
			 */
			/**
			 * Mixer extensions currently aren't supported, so
			 * leave @sa oss_mixerinfo::nrext blank for now.
			 */
			/**
			 * @todo Fill in @sa oss_mixerinfo::priority (requires
			 *       touching drivers?)
			 * @note The priority field is for mixer applets to
			 * determine which mixer should be the default, with 0
			 * being least preferred and 10 being most preferred.
			 * From 4Front:  "OSS drivers like ICH use higher
			 * values (10) because such chips are known to be used
			 * only on motherboards.  Drivers for high end pro
			 * devices use 0 because they will never be the
			 * default mixer. Other devices use values 1 to 9
			 * depending on the estimated probability of being the
			 * default device.
			 *
			 * XXX Described by Hannu@4Front, but not found in
			 *     soundcard.h.
			strlcpy(mi->devnode, devtoname(d->mixer_dev),
			sizeof(mi->devnode));
			mi->legacy_device = i;
			 */
			mtx_unlock(m->lock);
		} else
			++nmix;

		PCM_UNLOCK(d);

		if (m != NULL)
			return (0);
	}

	return (EINVAL);
}

/*
 * Allow the sound driver to use the mixer lock to protect its mixer
 * data:
 */
struct mtx *
mixer_get_lock(struct snd_mixer *m)
{
	if (m->lock == NULL) {
		return (&Giant);
	}
	return (m->lock);
}

int
mix_get_locked(struct snd_mixer *m, u_int dev, int *pleft, int *pright)
{
	int level;

	level = mixer_get(m, dev);
	if (level < 0) {
		*pright = *pleft = -1;
		return (-1);
	}

	*pleft = level & 0xFF;
	*pright = (level >> 8) & 0xFF;

	return (0);
}

int
mix_set_locked(struct snd_mixer *m, u_int dev, int left, int right)
{
	int level;

	level = (left & 0xFF) | ((right & 0xFF) << 8);

	return (mixer_set(m, dev, level));
}
