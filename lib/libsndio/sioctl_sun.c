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
/*
 * the way the sun mixer is designed doesn't let us representing
 * it easily with the sioctl api. For now expose only few
 * white-listed controls the same way as we do in kernel
 * for the wskbd volume keys.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/audioio.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <sndio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "sioctl_priv.h"

#define DEVPATH_PREFIX	"/dev/audioctl"
#define DEVPATH_MAX 	(1 +		\
	sizeof(DEVPATH_PREFIX) - 1 +	\
	sizeof(int) * 3)

struct volume
{
	int nch;			/* channels in the level control */
	int level_idx;			/* index of the level control */
	int level_val[8];		/* current value */
	int mute_idx;			/* index of the mute control */
	int mute_val;			/* per channel state of mute control */
	int base_addr;
	char *name;
};

struct sioctl_sun_hdl {
	struct sioctl_hdl sioctl;
	char display[SIOCTL_DISPLAYMAX];
	int display_addr;
	struct volume output, input;
	int fd, events;
};

static void sioctl_sun_close(struct sioctl_hdl *);
static int sioctl_sun_nfds(struct sioctl_hdl *);
static int sioctl_sun_pollfd(struct sioctl_hdl *, struct pollfd *, int);
static int sioctl_sun_revents(struct sioctl_hdl *, struct pollfd *);
static int sioctl_sun_setctl(struct sioctl_hdl *, unsigned int, unsigned int);
static int sioctl_sun_onval(struct sioctl_hdl *);
static int sioctl_sun_ondesc(struct sioctl_hdl *);

/*
 * operations every device should support
 */
struct sioctl_ops sioctl_sun_ops = {
	sioctl_sun_close,
	sioctl_sun_nfds,
	sioctl_sun_pollfd,
	sioctl_sun_revents,
	sioctl_sun_setctl,
	sioctl_sun_onval,
	sioctl_sun_ondesc
};

static int
initmute(struct sioctl_sun_hdl *hdl, struct mixer_devinfo *info)
{
	struct mixer_devinfo mi;
	char name[MAX_AUDIO_DEV_LEN];

	for (mi.index = info->next; mi.index != -1; mi.index = mi.next) {
		if (ioctl(hdl->fd, AUDIO_MIXER_DEVINFO, &mi) < 0)
			break;
		if (strcmp(mi.label.name, AudioNmute) == 0)
			return mi.index;
	}

	/* try "_mute" suffix */
	snprintf(name, sizeof(name), "%s_mute", info->label.name);
	for (mi.index = 0; ; mi.index++) {
		if (ioctl(hdl->fd, AUDIO_MIXER_DEVINFO, &mi) < 0)
			break;
		if (info->mixer_class == mi.mixer_class &&
		    strcmp(mi.label.name, name) == 0)
			return mi.index;
	}
	return -1;
}

static int
initvol(struct sioctl_sun_hdl *hdl, struct volume *vol, char *cn, char *dn)
{
	struct mixer_devinfo dev, cls;

	for (dev.index = 0; ; dev.index++) {
		if (ioctl(hdl->fd, AUDIO_MIXER_DEVINFO, &dev) < 0)
			break;
		if (dev.type != AUDIO_MIXER_VALUE)
			continue;
		cls.index = dev.mixer_class;
		if (ioctl(hdl->fd, AUDIO_MIXER_DEVINFO, &cls) < 0)
			break;
		if (strcmp(cls.label.name, cn) == 0 &&
		    strcmp(dev.label.name, dn) == 0) {
			vol->nch = dev.un.v.num_channels;
			vol->level_idx = dev.index;
			vol->mute_idx = initmute(hdl, &dev);
			DPRINTF("using %s.%s, %d channels, %s\n", cn, dn,
			    vol->nch, vol->mute_idx >= 0 ? "mute" : "no mute");
			return 1;
		}
	}
	vol->level_idx = vol->mute_idx = -1;
	return 0;
}

static void
init(struct sioctl_sun_hdl *hdl)
{
	static struct {
		char *cn, *dn;
	} output_names[] = {
		{AudioCoutputs, AudioNmaster},
		{AudioCinputs,  AudioNdac},
		{AudioCoutputs, AudioNdac},
		{AudioCoutputs, AudioNoutput}
	}, input_names[] = {
		{AudioCrecord, AudioNrecord},
		{AudioCrecord, AudioNvolume},
		{AudioCinputs, AudioNrecord},
		{AudioCinputs, AudioNvolume},
		{AudioCinputs, AudioNinput}
	};
	struct audio_device getdev;
	int i;

	for (i = 0; i < sizeof(output_names) / sizeof(output_names[0]); i++) {
		if (initvol(hdl, &hdl->output,
			output_names[i].cn, output_names[i].dn)) {
			hdl->output.name = "output";
			hdl->output.base_addr = 0;
			break;
		}
	}
	for (i = 0; i < sizeof(input_names) / sizeof(input_names[0]); i++) {
		if (initvol(hdl, &hdl->input,
			input_names[i].cn, input_names[i].dn)) {
			hdl->input.name = "input";
			hdl->input.base_addr = 64;
			break;
		}
	}

	hdl->display_addr = 128;
	if (ioctl(hdl->fd, AUDIO_GETDEV, &getdev) == -1)
		strlcpy(hdl->display, "unknown", SIOCTL_DISPLAYMAX);
	else
		strlcpy(hdl->display, getdev.name, SIOCTL_DISPLAYMAX);
	DPRINTF("init: server.device: display = %s\n", hdl->display);
}

static int
setvol(struct sioctl_sun_hdl *hdl, struct volume *vol, int addr, int val)
{
	struct mixer_ctrl ctrl;
	int i;

	addr -= vol->base_addr;
	if (vol->level_idx >= 0 && addr >= 0 && addr < vol->nch) {
		if (vol->level_val[addr] == val) {
			DPRINTF("level %d, no change\n", val);
			return 1;
		}
		vol->level_val[addr] = val;
		ctrl.dev = vol->level_idx;
		ctrl.type = AUDIO_MIXER_VALUE;
		ctrl.un.value.num_channels = vol->nch;
		for (i = 0; i < vol->nch; i++)
			ctrl.un.value.level[i] = vol->level_val[i];
		DPRINTF("vol %d setting to %d\n", addr, vol->level_val[addr]);
		if (ioctl(hdl->fd, AUDIO_MIXER_WRITE, &ctrl) < 0) {
			DPRINTF("level write failed\n");
			return 0;
		}
		_sioctl_onval_cb(&hdl->sioctl, vol->base_addr + addr, val);
		return 1;
	}

	addr -= 32;
	if (vol->mute_idx >= 0 && addr >= 0 && addr < vol->nch) {
		val = val ? 1 : 0;
		if (vol->mute_val == val) {
			DPRINTF("mute %d, no change\n", val);
			return 1;
		}
		vol->mute_val = val;
		ctrl.dev = vol->mute_idx;
		ctrl.type = AUDIO_MIXER_ENUM;
		ctrl.un.ord = val;
		DPRINTF("mute setting to %d\n", val);
		if (ioctl(hdl->fd, AUDIO_MIXER_WRITE, &ctrl) < 0) {
			DPERROR("mute write\n");
			return 0;
		}
		for (i = 0; i < vol->nch; i++) {
			_sioctl_onval_cb(&hdl->sioctl,
			    vol->base_addr + 32 + i, val);
		}
		return 1;
	}
	return 1;
}

static int
scanvol(struct sioctl_sun_hdl *hdl, struct volume *vol)
{
	struct sioctl_desc desc;
	struct mixer_ctrl ctrl;
	int i, val;

	memset(&desc, 0, sizeof(struct sioctl_desc));
	if (vol->level_idx >= 0) {
		ctrl.dev = vol->level_idx;
		ctrl.type = AUDIO_MIXER_VALUE;
		ctrl.un.value.num_channels = vol->nch;
		if (ioctl(hdl->fd, AUDIO_MIXER_READ, &ctrl) < 0) {
			DPRINTF("level read failed\n");
			return 0;
		}
		desc.type = SIOCTL_NUM;
		desc.maxval = AUDIO_MAX_GAIN;
		desc.node1.name[0] = 0;
		desc.node1.unit = -1;
		strlcpy(desc.func, "level", SIOCTL_NAMEMAX);
		strlcpy(desc.node0.name, vol->name, SIOCTL_NAMEMAX);
		for (i = 0; i < vol->nch; i++) {
			desc.node0.unit = i;
			desc.addr = vol->base_addr + i;
			val = ctrl.un.value.level[i];
			vol->level_val[i] = val;
			_sioctl_ondesc_cb(&hdl->sioctl, &desc, val);
		}
	}
	if (vol->mute_idx >= 0) {
		ctrl.dev = vol->mute_idx;
		ctrl.type = AUDIO_MIXER_ENUM;
		if (ioctl(hdl->fd, AUDIO_MIXER_READ, &ctrl) < 0) {
			DPRINTF("mute read failed\n");
			return 0;
		}
		desc.type = SIOCTL_SW;
		desc.maxval = 1;
		desc.node1.name[0] = 0;
		desc.node1.unit = -1;
		strlcpy(desc.func, "mute", SIOCTL_NAMEMAX);
		strlcpy(desc.node0.name, vol->name, SIOCTL_NAMEMAX);
		val = ctrl.un.ord ? 1 : 0;
		vol->mute_val = val;
		for (i = 0; i < vol->nch; i++) {
			desc.node0.unit = i;
			desc.addr = vol->base_addr + 32 + i;
			_sioctl_ondesc_cb(&hdl->sioctl, &desc, val);
		}
	}
	return 1;
}

static int
updatevol(struct sioctl_sun_hdl *hdl, struct volume *vol, int idx)
{
	struct mixer_ctrl ctrl;
	int val, i;

	if (idx == vol->mute_idx)
		ctrl.type = AUDIO_MIXER_ENUM;
	else {
		ctrl.type = AUDIO_MIXER_VALUE;
		ctrl.un.value.num_channels = vol->nch;
	}
	ctrl.dev = idx;
	if (ioctl(hdl->fd, AUDIO_MIXER_READ, &ctrl) == -1) {
		DPERROR("sioctl_sun_revents: ioctl\n");
		hdl->sioctl.eof = 1;
		return 0;
	}
	if (idx == vol->mute_idx) {
		val = ctrl.un.ord ? 1 : 0;
		if (vol->mute_val == val)
			return 1;
		vol->mute_val = val;
		for (i = 0; i < vol->nch; i++) {
			_sioctl_onval_cb(&hdl->sioctl,
			    vol->base_addr + 32 + i, val);
		}
	} else {
		for (i = 0; i < vol->nch; i++) {
			val = ctrl.un.value.level[i];
			if (vol->level_val[i] == val)
				continue;
			vol->level_val[i] = val;
			_sioctl_onval_cb(&hdl->sioctl,
			    vol->base_addr + i, val);
		}
	}
	return 1;
}

int
sioctl_sun_getfd(const char *str, unsigned int mode, int nbio)
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
		DPRINTF("sioctl_sun_getfd: %s: \"rsnd\" expected\n", str);
		return -1;
	}
	switch (*p) {
	case '/':
		p++;
		break;
	default:
		DPRINTF("sioctl_sun_getfd: %s: '/' expected\n", str);
		return -1;
	}
	if (strcmp(p, "default") == 0) {
		devnum = 0;
	} else {
		p = _sndio_parsenum(p, &devnum, 255);
		if (p == NULL || *p != '\0') {
			DPRINTF("sioctl_sun_getfd: %s: number expected after '/'\n", str);
			return -1;
		}
	}
	snprintf(path, sizeof(path), DEVPATH_PREFIX "%u", devnum);
	if (mode == (SIOCTL_READ | SIOCTL_WRITE))
		flags = O_RDWR;
	else
		flags = (mode & SIOCTL_WRITE) ? O_WRONLY : O_RDONLY;
	while ((fd = open(path, flags | O_NONBLOCK | O_CLOEXEC)) < 0) {
		if (errno == EINTR)
			continue;
		DPERROR(path);
		return -1;
	}
	return fd;
}

struct sioctl_hdl *
sioctl_sun_fdopen(int fd, unsigned int mode, int nbio)
{
	struct sioctl_sun_hdl *hdl;

#ifdef DEBUG
	_sndio_debug_init();
#endif
	hdl = malloc(sizeof(struct sioctl_sun_hdl));
	if (hdl == NULL)
		return NULL;
	_sioctl_create(&hdl->sioctl, &sioctl_sun_ops, mode, nbio);
	hdl->fd = fd;
	init(hdl);
	return (struct sioctl_hdl *)hdl;
}

struct sioctl_hdl *
_sioctl_sun_open(const char *str, unsigned int mode, int nbio)
{
	struct sioctl_hdl *hdl;
	int fd;

	fd = sioctl_sun_getfd(str, mode, nbio);
	if (fd < 0)
		return NULL;
	hdl = sioctl_sun_fdopen(fd, mode, nbio);
	if (hdl != NULL)
		return hdl;
	while (close(fd) < 0 && errno == EINTR)
		; /* retry */
	return NULL;
}

static void
sioctl_sun_close(struct sioctl_hdl *addr)
{
	struct sioctl_sun_hdl *hdl = (struct sioctl_sun_hdl *)addr;

	close(hdl->fd);
	free(hdl);
}

static int
sioctl_sun_ondesc(struct sioctl_hdl *addr)
{
	struct sioctl_sun_hdl *hdl = (struct sioctl_sun_hdl *)addr;
	struct sioctl_desc desc;

	if (!scanvol(hdl, &hdl->output) ||
	    !scanvol(hdl, &hdl->input)) {
		hdl->sioctl.eof = 1;
		return 0;
	}

	/* report "server.device" control */
	memset(&desc, 0, sizeof(struct sioctl_desc));
	desc.type = SIOCTL_SEL;
	desc.maxval = 1;
	strlcpy(desc.func, "device", SIOCTL_NAMEMAX);
	strlcpy(desc.node0.name, "server", SIOCTL_NAMEMAX);
	desc.node0.unit = -1;
	strlcpy(desc.node1.name, "0", SIOCTL_NAMEMAX);
	desc.node1.unit = -1;
	strlcpy(desc.display, hdl->display, SIOCTL_DISPLAYMAX);
	desc.addr = hdl->display_addr;
	_sioctl_ondesc_cb(&hdl->sioctl, &desc, 1);

	_sioctl_ondesc_cb(&hdl->sioctl, NULL, 0);
	return 1;
}

static int
sioctl_sun_onval(struct sioctl_hdl *addr)
{
	return 1;
}

static int
sioctl_sun_setctl(struct sioctl_hdl *arg, unsigned int addr, unsigned int val)
{
	struct sioctl_sun_hdl *hdl = (struct sioctl_sun_hdl *)arg;

	if (!setvol(hdl, &hdl->output, addr, val) ||
	    !setvol(hdl, &hdl->input, addr, val)) {
		hdl->sioctl.eof = 1;
		return 0;
	}
	return 1;
}

static int
sioctl_sun_nfds(struct sioctl_hdl *addr)
{
	return 1;
}

static int
sioctl_sun_pollfd(struct sioctl_hdl *addr, struct pollfd *pfd, int events)
{
	struct sioctl_sun_hdl *hdl = (struct sioctl_sun_hdl *)addr;

	hdl->events = events;

	/*
	 * The audio(4) driver doesn't support POLLOUT, so if it is
	 * requested, don't set the struct pollfd. The AUDIO_MIXER_WRITE
	 * ioctl never blocks, so just return POLLOUT in sioctl_sun_revents().
	 */
	if (events & POLLOUT)
		return 0;

	pfd->fd = hdl->fd;
	pfd->events = POLLIN;
	return 1;
}

static int
sioctl_sun_revents(struct sioctl_hdl *arg, struct pollfd *pfd)
{
	struct sioctl_sun_hdl *hdl = (struct sioctl_sun_hdl *)arg;
	struct volume *vol;
	int idx, n;

	if (hdl->events & POLLOUT)
		return POLLOUT;

	if (pfd->revents & POLLIN) {
		while (1) {
			n = read(hdl->fd, &idx, sizeof(int));
			if (n == -1) {
				if (errno == EINTR || errno == EAGAIN)
					break;
				DPERROR("read");
				hdl->sioctl.eof = 1;
				return POLLHUP;
			}
			if (n < sizeof(int)) {
				DPRINTF("sioctl_sun_revents: short read\n");
				hdl->sioctl.eof = 1;
				return POLLHUP;
			}

			if (idx == hdl->output.level_idx ||
			    idx == hdl->output.mute_idx) {
				vol = &hdl->output;
			} else if (idx == hdl->input.level_idx ||
			    idx == hdl->input.mute_idx) {
				vol = &hdl->input;
			} else
				continue;

			if (!updatevol(hdl, vol, idx))
				return POLLHUP;
		}
	}
	return 0;
}
