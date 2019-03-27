/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Stephane E. Potvin <sepotvin@videotron.ca>
 * Copyright (c) 2006 Ariff Abdullah <ariff@FreeBSD.org>
 * Copyright (c) 2008-2012 Alexander Motin <mav@FreeBSD.org>
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

/*
 * Intel High Definition Audio (Audio function) driver for FreeBSD.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>

#include <sys/ctype.h>
#include <sys/taskqueue.h>

#include <dev/sound/pci/hda/hdac.h>
#include <dev/sound/pci/hda/hdaa.h>
#include <dev/sound/pci/hda/hda_reg.h>

#include "mixer_if.h"

SND_DECLARE_FILE("$FreeBSD$");

#define hdaa_lock(devinfo)	snd_mtxlock((devinfo)->lock)
#define hdaa_unlock(devinfo)	snd_mtxunlock((devinfo)->lock)
#define hdaa_lockassert(devinfo) snd_mtxassert((devinfo)->lock)
#define hdaa_lockowned(devinfo)	mtx_owned((devinfo)->lock)

static const struct {
	const char *key;
	uint32_t value;
} hdaa_quirks_tab[] = {
	{ "softpcmvol", HDAA_QUIRK_SOFTPCMVOL },
	{ "fixedrate", HDAA_QUIRK_FIXEDRATE },
	{ "forcestereo", HDAA_QUIRK_FORCESTEREO },
	{ "eapdinv", HDAA_QUIRK_EAPDINV },
	{ "senseinv", HDAA_QUIRK_SENSEINV },
	{ "ivref50", HDAA_QUIRK_IVREF50 },
	{ "ivref80", HDAA_QUIRK_IVREF80 },
	{ "ivref100", HDAA_QUIRK_IVREF100 },
	{ "ovref50", HDAA_QUIRK_OVREF50 },
	{ "ovref80", HDAA_QUIRK_OVREF80 },
	{ "ovref100", HDAA_QUIRK_OVREF100 },
	{ "ivref", HDAA_QUIRK_IVREF },
	{ "ovref", HDAA_QUIRK_OVREF },
	{ "vref", HDAA_QUIRK_VREF },
};

#define HDA_PARSE_MAXDEPTH	10

MALLOC_DEFINE(M_HDAA, "hdaa", "HDA Audio");

static const char *HDA_COLORS[16] = {"Unknown", "Black", "Grey", "Blue",
    "Green", "Red", "Orange", "Yellow", "Purple", "Pink", "Res.A", "Res.B",
    "Res.C", "Res.D", "White", "Other"};

static const char *HDA_DEVS[16] = {"Line-out", "Speaker", "Headphones", "CD",
    "SPDIF-out", "Digital-out", "Modem-line", "Modem-handset", "Line-in",
    "AUX", "Mic", "Telephony", "SPDIF-in", "Digital-in", "Res.E", "Other"};

static const char *HDA_CONNS[4] = {"Jack", "None", "Fixed", "Both"};

static const char *HDA_CONNECTORS[16] = {
    "Unknown", "1/8", "1/4", "ATAPI", "RCA", "Optical", "Digital", "Analog",
    "DIN", "XLR", "RJ-11", "Combo", "0xc", "0xd", "0xe", "Other" };

static const char *HDA_LOCS[64] = {
    "0x00", "Rear", "Front", "Left", "Right", "Top", "Bottom", "Rear-panel",
	"Drive-bay", "0x09", "0x0a", "0x0b", "0x0c", "0x0d", "0x0e", "0x0f",
    "Internal", "0x11", "0x12", "0x13", "0x14", "0x15", "0x16", "Riser",
	"0x18", "Onboard", "0x1a", "0x1b", "0x1c", "0x1d", "0x1e", "0x1f",
    "External", "Ext-Rear", "Ext-Front", "Ext-Left", "Ext-Right", "Ext-Top", "Ext-Bottom", "0x07",
	"0x28", "0x29", "0x2a", "0x2b", "0x2c", "0x2d", "0x2e", "0x2f",
    "Other", "0x31", "0x32", "0x33", "0x34", "0x35", "Other-Bott", "Lid-In",
	"Lid-Out", "0x39", "0x3a", "0x3b", "0x3c", "0x3d", "0x3e", "0x3f" };

static const char *HDA_GPIO_ACTIONS[8] = {
    "keep", "set", "clear", "disable", "input", "0x05", "0x06", "0x07"};

static const char *HDA_HDMI_CODING_TYPES[18] = {
    "undefined", "LPCM", "AC-3", "MPEG1", "MP3", "MPEG2", "AAC-LC", "DTS",
    "ATRAC", "DSD", "E-AC-3", "DTS-HD", "MLP", "DST", "WMAPro", "HE-AAC",
    "HE-AACv2", "MPEG-Surround"
};

/* Default */
static uint32_t hdaa_fmt[] = {
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};

static struct pcmchan_caps hdaa_caps = {48000, 48000, hdaa_fmt, 0};

static const struct {
	uint32_t	rate;
	int		valid;
	uint16_t	base;
	uint16_t	mul;
	uint16_t	div;
} hda_rate_tab[] = {
	{   8000, 1, 0x0000, 0x0000, 0x0500 },	/* (48000 * 1) / 6 */
	{   9600, 0, 0x0000, 0x0000, 0x0400 },	/* (48000 * 1) / 5 */
	{  12000, 0, 0x0000, 0x0000, 0x0300 },	/* (48000 * 1) / 4 */
	{  16000, 1, 0x0000, 0x0000, 0x0200 },	/* (48000 * 1) / 3 */
	{  18000, 0, 0x0000, 0x1000, 0x0700 },	/* (48000 * 3) / 8 */
	{  19200, 0, 0x0000, 0x0800, 0x0400 },	/* (48000 * 2) / 5 */
	{  24000, 0, 0x0000, 0x0000, 0x0100 },	/* (48000 * 1) / 2 */
	{  28800, 0, 0x0000, 0x1000, 0x0400 },	/* (48000 * 3) / 5 */
	{  32000, 1, 0x0000, 0x0800, 0x0200 },	/* (48000 * 2) / 3 */
	{  36000, 0, 0x0000, 0x1000, 0x0300 },	/* (48000 * 3) / 4 */
	{  38400, 0, 0x0000, 0x1800, 0x0400 },	/* (48000 * 4) / 5 */
	{  48000, 1, 0x0000, 0x0000, 0x0000 },	/* (48000 * 1) / 1 */
	{  64000, 0, 0x0000, 0x1800, 0x0200 },	/* (48000 * 4) / 3 */
	{  72000, 0, 0x0000, 0x1000, 0x0100 },	/* (48000 * 3) / 2 */
	{  96000, 1, 0x0000, 0x0800, 0x0000 },	/* (48000 * 2) / 1 */
	{ 144000, 0, 0x0000, 0x1000, 0x0000 },	/* (48000 * 3) / 1 */
	{ 192000, 1, 0x0000, 0x1800, 0x0000 },	/* (48000 * 4) / 1 */
	{   8820, 0, 0x4000, 0x0000, 0x0400 },	/* (44100 * 1) / 5 */
	{  11025, 1, 0x4000, 0x0000, 0x0300 },	/* (44100 * 1) / 4 */
	{  12600, 0, 0x4000, 0x0800, 0x0600 },	/* (44100 * 2) / 7 */
	{  14700, 0, 0x4000, 0x0000, 0x0200 },	/* (44100 * 1) / 3 */
	{  17640, 0, 0x4000, 0x0800, 0x0400 },	/* (44100 * 2) / 5 */
	{  18900, 0, 0x4000, 0x1000, 0x0600 },	/* (44100 * 3) / 7 */
	{  22050, 1, 0x4000, 0x0000, 0x0100 },	/* (44100 * 1) / 2 */
	{  25200, 0, 0x4000, 0x1800, 0x0600 },	/* (44100 * 4) / 7 */
	{  26460, 0, 0x4000, 0x1000, 0x0400 },	/* (44100 * 3) / 5 */
	{  29400, 0, 0x4000, 0x0800, 0x0200 },	/* (44100 * 2) / 3 */
	{  33075, 0, 0x4000, 0x1000, 0x0300 },	/* (44100 * 3) / 4 */
	{  35280, 0, 0x4000, 0x1800, 0x0400 },	/* (44100 * 4) / 5 */
	{  44100, 1, 0x4000, 0x0000, 0x0000 },	/* (44100 * 1) / 1 */
	{  58800, 0, 0x4000, 0x1800, 0x0200 },	/* (44100 * 4) / 3 */
	{  66150, 0, 0x4000, 0x1000, 0x0100 },	/* (44100 * 3) / 2 */
	{  88200, 1, 0x4000, 0x0800, 0x0000 },	/* (44100 * 2) / 1 */
	{ 132300, 0, 0x4000, 0x1000, 0x0000 },	/* (44100 * 3) / 1 */
	{ 176400, 1, 0x4000, 0x1800, 0x0000 },	/* (44100 * 4) / 1 */
};
#define HDA_RATE_TAB_LEN (sizeof(hda_rate_tab) / sizeof(hda_rate_tab[0]))

const static char *ossnames[] = SOUND_DEVICE_NAMES;

/****************************************************************************
 * Function prototypes
 ****************************************************************************/
static int	hdaa_pcmchannel_setup(struct hdaa_chan *);

static void	hdaa_widget_connection_select(struct hdaa_widget *, uint8_t);
static void	hdaa_audio_ctl_amp_set(struct hdaa_audio_ctl *,
						uint32_t, int, int);
static struct	hdaa_audio_ctl *hdaa_audio_ctl_amp_get(struct hdaa_devinfo *,
							nid_t, int, int, int);
static void	hdaa_audio_ctl_amp_set_internal(struct hdaa_devinfo *,
				nid_t, int, int, int, int, int, int);

static void	hdaa_dump_pin_config(struct hdaa_widget *w, uint32_t conf);

static char *
hdaa_audio_ctl_ossmixer_mask2allname(uint32_t mask, char *buf, size_t len)
{
	int i, first = 1;

	bzero(buf, len);
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (mask & (1 << i)) {
			if (first == 0)
				strlcat(buf, ", ", len);
			strlcat(buf, ossnames[i], len);
			first = 0;
		}
	}
	return (buf);
}

static struct hdaa_audio_ctl *
hdaa_audio_ctl_each(struct hdaa_devinfo *devinfo, int *index)
{
	if (devinfo == NULL ||
	    index == NULL || devinfo->ctl == NULL ||
	    devinfo->ctlcnt < 1 ||
	    *index < 0 || *index >= devinfo->ctlcnt)
		return (NULL);
	return (&devinfo->ctl[(*index)++]);
}

static struct hdaa_audio_ctl *
hdaa_audio_ctl_amp_get(struct hdaa_devinfo *devinfo, nid_t nid, int dir,
						int index, int cnt)
{
	struct hdaa_audio_ctl *ctl;
	int i, found = 0;

	if (devinfo == NULL || devinfo->ctl == NULL)
		return (NULL);

	i = 0;
	while ((ctl = hdaa_audio_ctl_each(devinfo, &i)) != NULL) {
		if (ctl->enable == 0)
			continue;
		if (ctl->widget->nid != nid)
			continue;
		if (dir && ctl->ndir != dir)
			continue;
		if (index >= 0 && ctl->ndir == HDAA_CTL_IN &&
		    ctl->dir == ctl->ndir && ctl->index != index)
			continue;
		found++;
		if (found == cnt || cnt <= 0)
			return (ctl);
	}

	return (NULL);
}

static const struct matrix {
	struct pcmchan_matrix	m;
	int			analog;
} matrixes[]  = {
    { SND_CHN_MATRIX_MAP_1_0,	1 },
    { SND_CHN_MATRIX_MAP_2_0,	1 },
    { SND_CHN_MATRIX_MAP_2_1,	0 },
    { SND_CHN_MATRIX_MAP_3_0,	0 },
    { SND_CHN_MATRIX_MAP_3_1,	0 },
    { SND_CHN_MATRIX_MAP_4_0,	1 },
    { SND_CHN_MATRIX_MAP_4_1,	0 },
    { SND_CHN_MATRIX_MAP_5_0,	0 },
    { SND_CHN_MATRIX_MAP_5_1,	1 },
    { SND_CHN_MATRIX_MAP_6_0,	0 },
    { SND_CHN_MATRIX_MAP_6_1,	0 },
    { SND_CHN_MATRIX_MAP_7_0,	0 },
    { SND_CHN_MATRIX_MAP_7_1,	1 },
};

static const char *channel_names[] = SND_CHN_T_NAMES;

/*
 * Connected channels change handler.
 */
static void
hdaa_channels_handler(struct hdaa_audio_as *as)
{
	struct hdaa_pcm_devinfo *pdevinfo = as->pdevinfo;
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	struct hdaa_chan *ch = &devinfo->chans[as->chans[0]];
	struct hdaa_widget *w;
	uint8_t *eld;
	int i, total, sub, assume, channels;
	uint16_t cpins, upins, tpins;

	cpins = upins = 0;
	eld = NULL;
	for (i = 0; i < 16; i++) {
		if (as->pins[i] <= 0)
			continue;
		w = hdaa_widget_get(devinfo, as->pins[i]);
		if (w == NULL)
			continue;
		if (w->wclass.pin.connected == 1)
			cpins |= (1 << i);
		else if (w->wclass.pin.connected != 0)
			upins |= (1 << i);
		if (w->eld != NULL && w->eld_len >= 8)
			eld = w->eld;
	}
	tpins = cpins | upins;
	if (as->hpredir >= 0)
		tpins &= 0x7fff;
	if (tpins == 0)
		tpins = as->pinset;

	total = sub = assume = channels = 0;
	if (eld) {
		/* Map CEA speakers to sound(4) channels. */
		if (eld[7] & 0x01) /* Front Left/Right */
			channels |= SND_CHN_T_MASK_FL | SND_CHN_T_MASK_FR;
		if (eld[7] & 0x02) /* Low Frequency Effect */
			channels |= SND_CHN_T_MASK_LF;
		if (eld[7] & 0x04) /* Front Center */
			channels |= SND_CHN_T_MASK_FC;
		if (eld[7] & 0x08) { /* Rear Left/Right */
			/* If we have both RLR and RLRC, report RLR as side. */
			if (eld[7] & 0x40) /* Rear Left/Right Center */
			    channels |= SND_CHN_T_MASK_SL | SND_CHN_T_MASK_SR;
			else
			    channels |= SND_CHN_T_MASK_BL | SND_CHN_T_MASK_BR;
		}
		if (eld[7] & 0x10) /* Rear center */
			channels |= SND_CHN_T_MASK_BC;
		if (eld[7] & 0x20) /* Front Left/Right Center */
			channels |= SND_CHN_T_MASK_FLC | SND_CHN_T_MASK_FRC;
		if (eld[7] & 0x40) /* Rear Left/Right Center */
			channels |= SND_CHN_T_MASK_BL | SND_CHN_T_MASK_BR;
	} else if (as->pinset != 0 && (tpins & 0xffe0) == 0) {
		/* Map UAA speakers to sound(4) channels. */
		if (tpins & 0x0001)
			channels |= SND_CHN_T_MASK_FL | SND_CHN_T_MASK_FR;
		if (tpins & 0x0002)
			channels |= SND_CHN_T_MASK_FC | SND_CHN_T_MASK_LF;
		if (tpins & 0x0004)
			channels |= SND_CHN_T_MASK_BL | SND_CHN_T_MASK_BR;
		if (tpins & 0x0008)
			channels |= SND_CHN_T_MASK_FLC | SND_CHN_T_MASK_FRC;
		if (tpins & 0x0010) {
			/* If there is no back pin, report side as back. */
			if ((as->pinset & 0x0004) == 0)
			    channels |= SND_CHN_T_MASK_BL | SND_CHN_T_MASK_BR;
			else
			    channels |= SND_CHN_T_MASK_SL | SND_CHN_T_MASK_SR;
		}
	} else if (as->mixed) {
		/* Mixed assoc can be only stereo or theoretically mono. */
		if (ch->channels == 1)
			channels |= SND_CHN_T_MASK_FC;
		else
			channels |= SND_CHN_T_MASK_FL | SND_CHN_T_MASK_FR;
	}
	if (channels) {	/* We have some usable channels info. */
		HDA_BOOTVERBOSE(
			device_printf(pdevinfo->dev, "%s channel set is: ",
			    as->dir == HDAA_CTL_OUT ? "Playback" : "Recording");
			for (i = 0; i < SND_CHN_T_MAX; i++)
				if (channels & (1 << i))
					printf("%s, ", channel_names[i]);
			printf("\n");
		);
		/* Look for maximal fitting matrix. */
		for (i = 0; i < sizeof(matrixes) / sizeof(struct matrix); i++) {
			if (as->pinset != 0 && matrixes[i].analog == 0)
				continue;
			if ((matrixes[i].m.mask & ~channels) == 0) {
				total = matrixes[i].m.channels;
				sub = matrixes[i].m.ext;
			}
		}
	}
	if (total == 0) {
		assume = 1;
		total = ch->channels;
		sub = (total == 6 || total == 8) ? 1 : 0;
	}
	HDA_BOOTVERBOSE(
		device_printf(pdevinfo->dev,
		    "%s channel matrix is: %s%d.%d (%s)\n",
		    as->dir == HDAA_CTL_OUT ? "Playback" : "Recording",
		    assume ? "unknown, assuming " : "", total - sub, sub,
		    cpins != 0 ? "connected" :
		    (upins != 0 ? "unknown" : "disconnected"));
	);
}

/*
 * Headphones redirection change handler.
 */
static void
hdaa_hpredir_handler(struct hdaa_widget *w)
{
	struct hdaa_devinfo *devinfo = w->devinfo;
	struct hdaa_audio_as *as = &devinfo->as[w->bindas];
	struct hdaa_widget *w1;
	struct hdaa_audio_ctl *ctl;
	uint32_t val;
	int j, connected = w->wclass.pin.connected;

	HDA_BOOTVERBOSE(
		device_printf((as->pdevinfo && as->pdevinfo->dev) ?
		    as->pdevinfo->dev : devinfo->dev,
		    "Redirect output to: %s\n",
		    connected ? "headphones": "main");
	);
	/* (Un)Mute headphone pin. */
	ctl = hdaa_audio_ctl_amp_get(devinfo,
	    w->nid, HDAA_CTL_IN, -1, 1);
	if (ctl != NULL && ctl->mute) {
		/* If pin has muter - use it. */
		val = connected ? 0 : 1;
		if (val != ctl->forcemute) {
			ctl->forcemute = val;
			hdaa_audio_ctl_amp_set(ctl,
			    HDAA_AMP_MUTE_DEFAULT,
			    HDAA_AMP_VOL_DEFAULT, HDAA_AMP_VOL_DEFAULT);
		}
	} else {
		/* If there is no muter - disable pin output. */
		if (connected)
			val = w->wclass.pin.ctrl |
			    HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE;
		else
			val = w->wclass.pin.ctrl &
			    ~HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE;
		if (val != w->wclass.pin.ctrl) {
			w->wclass.pin.ctrl = val;
			hda_command(devinfo->dev,
			    HDA_CMD_SET_PIN_WIDGET_CTRL(0,
			    w->nid, w->wclass.pin.ctrl));
		}
	}
	/* (Un)Mute other pins. */
	for (j = 0; j < 15; j++) {
		if (as->pins[j] <= 0)
			continue;
		ctl = hdaa_audio_ctl_amp_get(devinfo,
		    as->pins[j], HDAA_CTL_IN, -1, 1);
		if (ctl != NULL && ctl->mute) {
			/* If pin has muter - use it. */
			val = connected ? 1 : 0;
			if (val == ctl->forcemute)
				continue;
			ctl->forcemute = val;
			hdaa_audio_ctl_amp_set(ctl,
			    HDAA_AMP_MUTE_DEFAULT,
			    HDAA_AMP_VOL_DEFAULT, HDAA_AMP_VOL_DEFAULT);
			continue;
		}
		/* If there is no muter - disable pin output. */
		w1 = hdaa_widget_get(devinfo, as->pins[j]);
		if (w1 != NULL) {
			if (connected)
				val = w1->wclass.pin.ctrl &
				    ~HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE;
			else
				val = w1->wclass.pin.ctrl |
				    HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE;
			if (val != w1->wclass.pin.ctrl) {
				w1->wclass.pin.ctrl = val;
				hda_command(devinfo->dev,
				    HDA_CMD_SET_PIN_WIDGET_CTRL(0,
				    w1->nid, w1->wclass.pin.ctrl));
			}
		}
	}
}

/*
 * Recording source change handler.
 */
static void
hdaa_autorecsrc_handler(struct hdaa_audio_as *as, struct hdaa_widget *w)
{
	struct hdaa_pcm_devinfo *pdevinfo = as->pdevinfo;
	struct hdaa_devinfo *devinfo;
	struct hdaa_widget *w1;
	int i, mask, fullmask, prio, bestprio;
	char buf[128];

	if (!as->mixed || pdevinfo == NULL || pdevinfo->mixer == NULL)
		return;
	/* Don't touch anything if we asked not to. */
	if (pdevinfo->autorecsrc == 0 ||
	    (pdevinfo->autorecsrc == 1 && w != NULL))
		return;
	/* Don't touch anything if "mix" or "speaker" selected. */
	if (pdevinfo->recsrc & (SOUND_MASK_IMIX | SOUND_MASK_SPEAKER))
		return;
	/* Don't touch anything if several selected. */
	if (ffs(pdevinfo->recsrc) != fls(pdevinfo->recsrc))
		return;
	devinfo = pdevinfo->devinfo;
	mask = fullmask = 0;
	bestprio = 0;
	for (i = 0; i < 16; i++) {
		if (as->pins[i] <= 0)
			continue;
		w1 = hdaa_widget_get(devinfo, as->pins[i]);
		if (w1 == NULL || w1->enable == 0)
			continue;
		if (w1->wclass.pin.connected == 0)
			continue;
		prio = (w1->wclass.pin.connected == 1) ? 2 : 1;
		if (prio < bestprio)
			continue;
		if (prio > bestprio) {
			mask = 0;
			bestprio = prio;
		}
		mask |= (1 << w1->ossdev);
		fullmask |= (1 << w1->ossdev);
	}
	if (mask == 0)
		return;
	/* Prefer newly connected input. */
	if (w != NULL && (mask & (1 << w->ossdev)))
		mask = (1 << w->ossdev);
	/* Prefer previously selected input */
	if (mask & pdevinfo->recsrc)
		mask &= pdevinfo->recsrc;
	/* Prefer mic. */
	if (mask & SOUND_MASK_MIC)
		mask = SOUND_MASK_MIC;
	/* Prefer monitor (2nd mic). */
	if (mask & SOUND_MASK_MONITOR)
		mask = SOUND_MASK_MONITOR;
	/* Just take first one. */
	mask = (1 << (ffs(mask) - 1));
	HDA_BOOTVERBOSE(
		hdaa_audio_ctl_ossmixer_mask2allname(mask, buf, sizeof(buf));
		device_printf(pdevinfo->dev,
		    "Automatically set rec source to: %s\n", buf);
	);
	hdaa_unlock(devinfo);
	mix_setrecsrc(pdevinfo->mixer, mask);
	hdaa_lock(devinfo);
}

/*
 * Jack presence detection event handler.
 */
static void
hdaa_presence_handler(struct hdaa_widget *w)
{
	struct hdaa_devinfo *devinfo = w->devinfo;
	struct hdaa_audio_as *as;
	uint32_t res;
	int connected, old;

	if (w->enable == 0 || w->type !=
	    HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
		return;

	if (HDA_PARAM_PIN_CAP_PRESENCE_DETECT_CAP(w->wclass.pin.cap) == 0 ||
	    (HDA_CONFIG_DEFAULTCONF_MISC(w->wclass.pin.config) & 1) != 0)
		return;

	res = hda_command(devinfo->dev, HDA_CMD_GET_PIN_SENSE(0, w->nid));
	connected = (res & HDA_CMD_GET_PIN_SENSE_PRESENCE_DETECT) != 0;
	if (devinfo->quirks & HDAA_QUIRK_SENSEINV)
		connected = !connected;
	old = w->wclass.pin.connected;
	if (connected == old)
		return;
	w->wclass.pin.connected = connected;
	HDA_BOOTVERBOSE(
		if (connected || old != 2) {
			device_printf(devinfo->dev,
			    "Pin sense: nid=%d sense=0x%08x (%sconnected)\n",
			    w->nid, res, !connected ? "dis" : "");
		}
	);

	as = &devinfo->as[w->bindas];
	if (as->hpredir >= 0 && as->pins[15] == w->nid)
		hdaa_hpredir_handler(w);
	if (as->dir == HDAA_CTL_IN && old != 2)
		hdaa_autorecsrc_handler(as, w);
	if (old != 2)
		hdaa_channels_handler(as);
}

/*
 * Callback for poll based presence detection.
 */
static void
hdaa_jack_poll_callback(void *arg)
{
	struct hdaa_devinfo *devinfo = arg;
	struct hdaa_widget *w;
	int i;

	hdaa_lock(devinfo);
	if (devinfo->poll_ival == 0) {
		hdaa_unlock(devinfo);
		return;
	}
	for (i = 0; i < devinfo->ascnt; i++) {
		if (devinfo->as[i].hpredir < 0)
			continue;
		w = hdaa_widget_get(devinfo, devinfo->as[i].pins[15]);
		if (w == NULL || w->enable == 0 || w->type !=
		    HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		hdaa_presence_handler(w);
	}
	callout_reset(&devinfo->poll_jack, devinfo->poll_ival,
	    hdaa_jack_poll_callback, devinfo);
	hdaa_unlock(devinfo);
}

static void
hdaa_eld_dump(struct hdaa_widget *w)
{
	struct hdaa_devinfo *devinfo = w->devinfo;
	device_t dev = devinfo->dev;
	uint8_t *sad;
	int len, mnl, i, sadc, fmt;

	if (w->eld == NULL || w->eld_len < 4)
		return;
	device_printf(dev,
	    "ELD nid=%d: ELD_Ver=%u Baseline_ELD_Len=%u\n",
	    w->nid, w->eld[0] >> 3, w->eld[2]);
	if ((w->eld[0] >> 3) != 0x02)
		return;
	len = min(w->eld_len, (u_int)w->eld[2] * 4);
	mnl = w->eld[4] & 0x1f;
	device_printf(dev,
	    "ELD nid=%d: CEA_EDID_Ver=%u MNL=%u\n",
	    w->nid, w->eld[4] >> 5, mnl);
	sadc = w->eld[5] >> 4;
	device_printf(dev,
	    "ELD nid=%d: SAD_Count=%u Conn_Type=%u S_AI=%u HDCP=%u\n",
	    w->nid, sadc, (w->eld[5] >> 2) & 0x3,
	    (w->eld[5] >> 1) & 0x1, w->eld[5] & 0x1);
	device_printf(dev,
	    "ELD nid=%d: Aud_Synch_Delay=%ums\n",
	    w->nid, w->eld[6] * 2);
	device_printf(dev,
	    "ELD nid=%d: Channels=0x%b\n",
	    w->nid, w->eld[7],
	    "\020\07RLRC\06FLRC\05RC\04RLR\03FC\02LFE\01FLR");
	device_printf(dev,
	    "ELD nid=%d: Port_ID=0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
	    w->nid, w->eld[8], w->eld[9], w->eld[10], w->eld[11],
	    w->eld[12], w->eld[13], w->eld[14], w->eld[15]);
	device_printf(dev,
	    "ELD nid=%d: Manufacturer_Name=0x%02x%02x\n",
	    w->nid, w->eld[16], w->eld[17]);
	device_printf(dev,
	    "ELD nid=%d: Product_Code=0x%02x%02x\n",
	    w->nid, w->eld[18], w->eld[19]);
	device_printf(dev,
	    "ELD nid=%d: Monitor_Name_String='%.*s'\n",
	    w->nid, mnl, &w->eld[20]);
	for (i = 0; i < sadc; i++) {
		sad = &w->eld[20 + mnl + i * 3];
		fmt = (sad[0] >> 3) & 0x0f;
		if (fmt == HDA_HDMI_CODING_TYPE_REF_CTX) {
			fmt = (sad[2] >> 3) & 0x1f;
			if (fmt < 1 || fmt > 3)
				fmt = 0;
			else
				fmt += 14;
		}
		device_printf(dev,
		    "ELD nid=%d: %s %dch freqs=0x%b",
		    w->nid, HDA_HDMI_CODING_TYPES[fmt], (sad[0] & 0x07) + 1,
		    sad[1], "\020\007192\006176\00596\00488\00348\00244\00132");
		switch (fmt) {
		case HDA_HDMI_CODING_TYPE_LPCM:
			printf(" sizes=0x%b",
			    sad[2] & 0x07, "\020\00324\00220\00116");
			break;
		case HDA_HDMI_CODING_TYPE_AC3:
		case HDA_HDMI_CODING_TYPE_MPEG1:
		case HDA_HDMI_CODING_TYPE_MP3:
		case HDA_HDMI_CODING_TYPE_MPEG2:
		case HDA_HDMI_CODING_TYPE_AACLC:
		case HDA_HDMI_CODING_TYPE_DTS:
		case HDA_HDMI_CODING_TYPE_ATRAC:
			printf(" max_bitrate=%d", sad[2] * 8000);
			break;
		case HDA_HDMI_CODING_TYPE_WMAPRO:
			printf(" profile=%d", sad[2] & 0x07);
			break;
		}
		printf("\n");
	}
}

static void
hdaa_eld_handler(struct hdaa_widget *w)
{
	struct hdaa_devinfo *devinfo = w->devinfo;
	uint32_t res;
	int i;

	if (w->enable == 0 || w->type !=
	    HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
		return;

	if (HDA_PARAM_PIN_CAP_PRESENCE_DETECT_CAP(w->wclass.pin.cap) == 0 ||
	    (HDA_CONFIG_DEFAULTCONF_MISC(w->wclass.pin.config) & 1) != 0)
		return;

	res = hda_command(devinfo->dev, HDA_CMD_GET_PIN_SENSE(0, w->nid));
	if ((w->eld != 0) == ((res & HDA_CMD_GET_PIN_SENSE_ELD_VALID) != 0))
		return;
	if (w->eld != NULL) {
		w->eld_len = 0;
		free(w->eld, M_HDAA);
		w->eld = NULL;
	}
	HDA_BOOTVERBOSE(
		device_printf(devinfo->dev,
		    "Pin sense: nid=%d sense=0x%08x "
		    "(%sconnected, ELD %svalid)\n",
		    w->nid, res,
		    (res & HDA_CMD_GET_PIN_SENSE_PRESENCE_DETECT) ? "" : "dis",
		    (res & HDA_CMD_GET_PIN_SENSE_ELD_VALID) ? "" : "in");
	);
	if ((res & HDA_CMD_GET_PIN_SENSE_ELD_VALID) == 0)
		return;

	res = hda_command(devinfo->dev,
	    HDA_CMD_GET_HDMI_DIP_SIZE(0, w->nid, 0x08));
	if (res == HDA_INVALID)
		return;
	w->eld_len = res & 0xff;
	if (w->eld_len != 0)
		w->eld = malloc(w->eld_len, M_HDAA, M_ZERO | M_NOWAIT);
	if (w->eld == NULL) {
		w->eld_len = 0;
		return;
	}

	for (i = 0; i < w->eld_len; i++) {
		res = hda_command(devinfo->dev,
		    HDA_CMD_GET_HDMI_ELDD(0, w->nid, i));
		if (res & 0x80000000)
			w->eld[i] = res & 0xff;
	}
	HDA_BOOTVERBOSE(
		hdaa_eld_dump(w);
	);
	hdaa_channels_handler(&devinfo->as[w->bindas]);
}

/*
 * Pin sense initializer.
 */
static void
hdaa_sense_init(struct hdaa_devinfo *devinfo)
{
	struct hdaa_audio_as *as;
	struct hdaa_widget *w;
	int i, poll = 0;

	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0 || w->type !=
		    HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		if (HDA_PARAM_AUDIO_WIDGET_CAP_UNSOL_CAP(w->param.widget_cap)) {
			if (w->unsol < 0)
				w->unsol = HDAC_UNSOL_ALLOC(
				    device_get_parent(devinfo->dev),
				    devinfo->dev, w->nid);
			hda_command(devinfo->dev,
			    HDA_CMD_SET_UNSOLICITED_RESPONSE(0, w->nid,
			    HDA_CMD_SET_UNSOLICITED_RESPONSE_ENABLE | w->unsol));
		}
		as = &devinfo->as[w->bindas];
		if (as->hpredir >= 0 && as->pins[15] == w->nid) {
			if (HDA_PARAM_PIN_CAP_PRESENCE_DETECT_CAP(w->wclass.pin.cap) == 0 ||
			    (HDA_CONFIG_DEFAULTCONF_MISC(w->wclass.pin.config) & 1) != 0) {
				device_printf(devinfo->dev,
				    "No presence detection support at nid %d\n",
				    w->nid);
			} else {
				if (w->unsol < 0)
					poll = 1;
				HDA_BOOTVERBOSE(
					device_printf(devinfo->dev,
					    "Headphones redirection for "
					    "association %d nid=%d using %s.\n",
					    w->bindas, w->nid,
					    (w->unsol < 0) ? "polling" :
					    "unsolicited responses");
				);
			}
		}
		hdaa_presence_handler(w);
		if (!HDA_PARAM_PIN_CAP_DP(w->wclass.pin.cap) &&
		    !HDA_PARAM_PIN_CAP_HDMI(w->wclass.pin.cap))
			continue;
		hdaa_eld_handler(w);
	}
	if (poll) {
		callout_reset(&devinfo->poll_jack, 1,
		    hdaa_jack_poll_callback, devinfo);
	}
}

static void
hdaa_sense_deinit(struct hdaa_devinfo *devinfo)
{
	struct hdaa_widget *w;
	int i;

	callout_stop(&devinfo->poll_jack);
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0 || w->type !=
		    HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		if (w->unsol < 0)
			continue;
		hda_command(devinfo->dev,
		    HDA_CMD_SET_UNSOLICITED_RESPONSE(0, w->nid, 0));
		HDAC_UNSOL_FREE(
		    device_get_parent(devinfo->dev), devinfo->dev,
		    w->unsol);
		w->unsol = -1;
	}
}

uint32_t
hdaa_widget_pin_patch(uint32_t config, const char *str)
{
	char buf[256];
	char *key, *value, *rest, *bad;
	int ival, i;

	strlcpy(buf, str, sizeof(buf));
	rest = buf;
	while ((key = strsep(&rest, "=")) != NULL) {
		value = strsep(&rest, " \t");
		if (value == NULL)
			break;
		ival = strtol(value, &bad, 10);
		if (strcmp(key, "seq") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_SEQUENCE_MASK;
			config |= ((ival << HDA_CONFIG_DEFAULTCONF_SEQUENCE_SHIFT) &
			    HDA_CONFIG_DEFAULTCONF_SEQUENCE_MASK);
		} else if (strcmp(key, "as") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_ASSOCIATION_MASK;
			config |= ((ival << HDA_CONFIG_DEFAULTCONF_ASSOCIATION_SHIFT) &
			    HDA_CONFIG_DEFAULTCONF_ASSOCIATION_MASK);
		} else if (strcmp(key, "misc") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_MISC_MASK;
			config |= ((ival << HDA_CONFIG_DEFAULTCONF_MISC_SHIFT) &
			    HDA_CONFIG_DEFAULTCONF_MISC_MASK);
		} else if (strcmp(key, "color") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_COLOR_MASK;
			if (bad[0] == 0) {
				config |= ((ival << HDA_CONFIG_DEFAULTCONF_COLOR_SHIFT) &
				    HDA_CONFIG_DEFAULTCONF_COLOR_MASK);
			}
			for (i = 0; i < 16; i++) {
				if (strcasecmp(HDA_COLORS[i], value) == 0) {
					config |= (i << HDA_CONFIG_DEFAULTCONF_COLOR_SHIFT);
					break;
				}
			}
		} else if (strcmp(key, "ctype") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_CONNECTION_TYPE_MASK;
			if (bad[0] == 0) {
			config |= ((ival << HDA_CONFIG_DEFAULTCONF_CONNECTION_TYPE_SHIFT) &
			    HDA_CONFIG_DEFAULTCONF_CONNECTION_TYPE_MASK);
			}
			for (i = 0; i < 16; i++) {
				if (strcasecmp(HDA_CONNECTORS[i], value) == 0) {
					config |= (i << HDA_CONFIG_DEFAULTCONF_CONNECTION_TYPE_SHIFT);
					break;
				}
			}
		} else if (strcmp(key, "device") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			if (bad[0] == 0) {
				config |= ((ival << HDA_CONFIG_DEFAULTCONF_DEVICE_SHIFT) &
				    HDA_CONFIG_DEFAULTCONF_DEVICE_MASK);
				continue;
			}
			for (i = 0; i < 16; i++) {
				if (strcasecmp(HDA_DEVS[i], value) == 0) {
					config |= (i << HDA_CONFIG_DEFAULTCONF_DEVICE_SHIFT);
					break;
				}
			}
		} else if (strcmp(key, "loc") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_LOCATION_MASK;
			if (bad[0] == 0) {
				config |= ((ival << HDA_CONFIG_DEFAULTCONF_LOCATION_SHIFT) &
				    HDA_CONFIG_DEFAULTCONF_LOCATION_MASK);
				continue;
			}
			for (i = 0; i < 64; i++) {
				if (strcasecmp(HDA_LOCS[i], value) == 0) {
					config |= (i << HDA_CONFIG_DEFAULTCONF_LOCATION_SHIFT);
					break;
				}
			}
		} else if (strcmp(key, "conn") == 0) {
			config &= ~HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK;
			if (bad[0] == 0) {
				config |= ((ival << HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_SHIFT) &
				    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
				continue;
			}
			for (i = 0; i < 4; i++) {
				if (strcasecmp(HDA_CONNS[i], value) == 0) {
					config |= (i << HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_SHIFT);
					break;
				}
			}
		}
	}
	return (config);
}

uint32_t
hdaa_gpio_patch(uint32_t gpio, const char *str)
{
	char buf[256];
	char *key, *value, *rest;
	int ikey, i;

	strlcpy(buf, str, sizeof(buf));
	rest = buf;
	while ((key = strsep(&rest, "=")) != NULL) {
		value = strsep(&rest, " \t");
		if (value == NULL)
			break;
		ikey = strtol(key, NULL, 10);
		if (ikey < 0 || ikey > 7)
			continue;
		for (i = 0; i < 7; i++) {
			if (strcasecmp(HDA_GPIO_ACTIONS[i], value) == 0) {
				gpio &= ~HDAA_GPIO_MASK(ikey);
				gpio |= i << HDAA_GPIO_SHIFT(ikey);
				break;
			}
		}
	}
	return (gpio);
}

static void
hdaa_local_patch_pin(struct hdaa_widget *w)
{
	device_t dev = w->devinfo->dev;
	const char *res = NULL;
	uint32_t config, orig;
	char buf[32];

	config = orig = w->wclass.pin.config;
	snprintf(buf, sizeof(buf), "cad%u.nid%u.config",
	    hda_get_codec_id(dev), w->nid);
	if (resource_string_value(device_get_name(
	    device_get_parent(device_get_parent(dev))),
	    device_get_unit(device_get_parent(device_get_parent(dev))),
	    buf, &res) == 0) {
		if (strncmp(res, "0x", 2) == 0) {
			config = strtol(res + 2, NULL, 16);
		} else {
			config = hdaa_widget_pin_patch(config, res);
		}
	}
	snprintf(buf, sizeof(buf), "nid%u.config", w->nid);
	if (resource_string_value(device_get_name(dev), device_get_unit(dev),
	    buf, &res) == 0) {
		if (strncmp(res, "0x", 2) == 0) {
			config = strtol(res + 2, NULL, 16);
		} else {
			config = hdaa_widget_pin_patch(config, res);
		}
	}
	HDA_BOOTVERBOSE(
		if (config != orig)
			device_printf(w->devinfo->dev,
			    "Patching pin config nid=%u 0x%08x -> 0x%08x\n",
			    w->nid, orig, config);
	);
	w->wclass.pin.newconf = w->wclass.pin.config = config;
}

static void
hdaa_dump_audio_formats_sb(struct sbuf *sb, uint32_t fcap, uint32_t pcmcap)
{
	uint32_t cap;

	cap = fcap;
	if (cap != 0) {
		sbuf_printf(sb, "     Stream cap: 0x%08x", cap);
		if (HDA_PARAM_SUPP_STREAM_FORMATS_AC3(cap))
			sbuf_printf(sb, " AC3");
		if (HDA_PARAM_SUPP_STREAM_FORMATS_FLOAT32(cap))
			sbuf_printf(sb, " FLOAT32");
		if (HDA_PARAM_SUPP_STREAM_FORMATS_PCM(cap))
			sbuf_printf(sb, " PCM");
		sbuf_printf(sb, "\n");
	}
	cap = pcmcap;
	if (cap != 0) {
		sbuf_printf(sb, "        PCM cap: 0x%08x", cap);
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_8BIT(cap))
			sbuf_printf(sb, " 8");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_16BIT(cap))
			sbuf_printf(sb, " 16");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_20BIT(cap))
			sbuf_printf(sb, " 20");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_24BIT(cap))
			sbuf_printf(sb, " 24");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_32BIT(cap))
			sbuf_printf(sb, " 32");
		sbuf_printf(sb, " bits,");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_8KHZ(cap))
			sbuf_printf(sb, " 8");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_11KHZ(cap))
			sbuf_printf(sb, " 11");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_16KHZ(cap))
			sbuf_printf(sb, " 16");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_22KHZ(cap))
			sbuf_printf(sb, " 22");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_32KHZ(cap))
			sbuf_printf(sb, " 32");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_44KHZ(cap))
			sbuf_printf(sb, " 44");
		sbuf_printf(sb, " 48");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_88KHZ(cap))
			sbuf_printf(sb, " 88");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_96KHZ(cap))
			sbuf_printf(sb, " 96");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_176KHZ(cap))
			sbuf_printf(sb, " 176");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_192KHZ(cap))
			sbuf_printf(sb, " 192");
		sbuf_printf(sb, " KHz\n");
	}
}

static void
hdaa_dump_pin_sb(struct sbuf *sb, struct hdaa_widget *w)
{
	uint32_t pincap, conf;

	pincap = w->wclass.pin.cap;

	sbuf_printf(sb, "        Pin cap: 0x%08x", pincap);
	if (HDA_PARAM_PIN_CAP_IMP_SENSE_CAP(pincap))
		sbuf_printf(sb, " ISC");
	if (HDA_PARAM_PIN_CAP_TRIGGER_REQD(pincap))
		sbuf_printf(sb, " TRQD");
	if (HDA_PARAM_PIN_CAP_PRESENCE_DETECT_CAP(pincap))
		sbuf_printf(sb, " PDC");
	if (HDA_PARAM_PIN_CAP_HEADPHONE_CAP(pincap))
		sbuf_printf(sb, " HP");
	if (HDA_PARAM_PIN_CAP_OUTPUT_CAP(pincap))
		sbuf_printf(sb, " OUT");
	if (HDA_PARAM_PIN_CAP_INPUT_CAP(pincap))
		sbuf_printf(sb, " IN");
	if (HDA_PARAM_PIN_CAP_BALANCED_IO_PINS(pincap))
		sbuf_printf(sb, " BAL");
	if (HDA_PARAM_PIN_CAP_HDMI(pincap))
		sbuf_printf(sb, " HDMI");
	if (HDA_PARAM_PIN_CAP_VREF_CTRL(pincap)) {
		sbuf_printf(sb, " VREF[");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_50(pincap))
			sbuf_printf(sb, " 50");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_80(pincap))
			sbuf_printf(sb, " 80");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_100(pincap))
			sbuf_printf(sb, " 100");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_GROUND(pincap))
			sbuf_printf(sb, " GROUND");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_HIZ(pincap))
			sbuf_printf(sb, " HIZ");
		sbuf_printf(sb, " ]");
	}
	if (HDA_PARAM_PIN_CAP_EAPD_CAP(pincap))
		sbuf_printf(sb, " EAPD");
	if (HDA_PARAM_PIN_CAP_DP(pincap))
		sbuf_printf(sb, " DP");
	if (HDA_PARAM_PIN_CAP_HBR(pincap))
		sbuf_printf(sb, " HBR");
	sbuf_printf(sb, "\n");
	conf = w->wclass.pin.config;
	sbuf_printf(sb, "     Pin config: 0x%08x", conf);
	sbuf_printf(sb, " as=%d seq=%d "
	    "device=%s conn=%s ctype=%s loc=%s color=%s misc=%d\n",
	    HDA_CONFIG_DEFAULTCONF_ASSOCIATION(conf),
	    HDA_CONFIG_DEFAULTCONF_SEQUENCE(conf),
	    HDA_DEVS[HDA_CONFIG_DEFAULTCONF_DEVICE(conf)],
	    HDA_CONNS[HDA_CONFIG_DEFAULTCONF_CONNECTIVITY(conf)],
	    HDA_CONNECTORS[HDA_CONFIG_DEFAULTCONF_CONNECTION_TYPE(conf)],
	    HDA_LOCS[HDA_CONFIG_DEFAULTCONF_LOCATION(conf)],
	    HDA_COLORS[HDA_CONFIG_DEFAULTCONF_COLOR(conf)],
	    HDA_CONFIG_DEFAULTCONF_MISC(conf));
	sbuf_printf(sb, "    Pin control: 0x%08x", w->wclass.pin.ctrl);
	if (w->wclass.pin.ctrl & HDA_CMD_SET_PIN_WIDGET_CTRL_HPHN_ENABLE)
		sbuf_printf(sb, " HP");
	if (w->wclass.pin.ctrl & HDA_CMD_SET_PIN_WIDGET_CTRL_IN_ENABLE)
		sbuf_printf(sb, " IN");
	if (w->wclass.pin.ctrl & HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE)
		sbuf_printf(sb, " OUT");
	if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(w->param.widget_cap)) {
		if ((w->wclass.pin.ctrl &
		    HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE_MASK) == 0x03)
			sbuf_printf(sb, " HBR");
		else if ((w->wclass.pin.ctrl &
		    HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE_MASK) != 0)
			sbuf_printf(sb, " EPTs");
	} else {
		if ((w->wclass.pin.ctrl &
		    HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE_MASK) != 0)
			sbuf_printf(sb, " VREFs");
	}
	sbuf_printf(sb, "\n");
}

static void
hdaa_dump_amp_sb(struct sbuf *sb, uint32_t cap, const char *banner)
{
	int offset, size, step;

	offset = HDA_PARAM_OUTPUT_AMP_CAP_OFFSET(cap);
	size = HDA_PARAM_OUTPUT_AMP_CAP_STEPSIZE(cap);
	step = HDA_PARAM_OUTPUT_AMP_CAP_NUMSTEPS(cap);
	sbuf_printf(sb, "     %s amp: 0x%08x "
	    "mute=%d step=%d size=%d offset=%d (%+d/%+ddB)\n",
	    banner, cap,
	    HDA_PARAM_OUTPUT_AMP_CAP_MUTE_CAP(cap),
	    step, size, offset,
	    ((0 - offset) * (size + 1)) / 4,
	    ((step - offset) * (size + 1)) / 4);
}


static int
hdaa_sysctl_caps(SYSCTL_HANDLER_ARGS)
{
	struct hdaa_devinfo *devinfo;
	struct hdaa_widget *w, *cw;
	struct sbuf sb;
	char buf[64];
	int error, j;

	w = (struct hdaa_widget *)oidp->oid_arg1;
	devinfo = w->devinfo;
	sbuf_new_for_sysctl(&sb, NULL, 256, req);

	sbuf_printf(&sb, "%s%s\n", w->name,
	    (w->enable == 0) ? " [DISABLED]" : "");
	sbuf_printf(&sb, "     Widget cap: 0x%08x",
	    w->param.widget_cap);
	if (w->param.widget_cap & 0x0ee1) {
		if (HDA_PARAM_AUDIO_WIDGET_CAP_LR_SWAP(w->param.widget_cap))
		    sbuf_printf(&sb, " LRSWAP");
		if (HDA_PARAM_AUDIO_WIDGET_CAP_POWER_CTRL(w->param.widget_cap))
		    sbuf_printf(&sb, " PWR");
		if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(w->param.widget_cap))
		    sbuf_printf(&sb, " DIGITAL");
		if (HDA_PARAM_AUDIO_WIDGET_CAP_UNSOL_CAP(w->param.widget_cap))
		    sbuf_printf(&sb, " UNSOL");
		if (HDA_PARAM_AUDIO_WIDGET_CAP_PROC_WIDGET(w->param.widget_cap))
		    sbuf_printf(&sb, " PROC");
		if (HDA_PARAM_AUDIO_WIDGET_CAP_STRIPE(w->param.widget_cap))
		    sbuf_printf(&sb, " STRIPE(x%d)",
			1 << (fls(w->wclass.conv.stripecap) - 1));
		j = HDA_PARAM_AUDIO_WIDGET_CAP_CC(w->param.widget_cap);
		if (j == 1)
		    sbuf_printf(&sb, " STEREO");
		else if (j > 1)
		    sbuf_printf(&sb, " %dCH", j + 1);
	}
	sbuf_printf(&sb, "\n");
	if (w->bindas != -1) {
		sbuf_printf(&sb, "    Association: %d (0x%04x)\n",
		    w->bindas, w->bindseqmask);
	}
	if (w->ossmask != 0 || w->ossdev >= 0) {
		sbuf_printf(&sb, "            OSS: %s",
		    hdaa_audio_ctl_ossmixer_mask2allname(w->ossmask, buf, sizeof(buf)));
		if (w->ossdev >= 0)
		    sbuf_printf(&sb, " (%s)", ossnames[w->ossdev]);
		sbuf_printf(&sb, "\n");
	}
	if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT ||
	    w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT) {
		hdaa_dump_audio_formats_sb(&sb,
		    w->param.supp_stream_formats,
		    w->param.supp_pcm_size_rate);
	} else if (w->type ==
	    HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX || w->waspin)
		hdaa_dump_pin_sb(&sb, w);
	if (w->param.eapdbtl != HDA_INVALID) {
		sbuf_printf(&sb, "           EAPD: 0x%08x%s%s%s\n",
		    w->param.eapdbtl,
		    (w->param.eapdbtl & HDA_CMD_SET_EAPD_BTL_ENABLE_LR_SWAP) ?
		     " LRSWAP" : "",
		    (w->param.eapdbtl & HDA_CMD_SET_EAPD_BTL_ENABLE_EAPD) ?
		     " EAPD" : "",
		    (w->param.eapdbtl & HDA_CMD_SET_EAPD_BTL_ENABLE_BTL) ?
		     " BTL" : "");
	}
	if (HDA_PARAM_AUDIO_WIDGET_CAP_OUT_AMP(w->param.widget_cap) &&
	    w->param.outamp_cap != 0)
		hdaa_dump_amp_sb(&sb, w->param.outamp_cap, "Output");
	if (HDA_PARAM_AUDIO_WIDGET_CAP_IN_AMP(w->param.widget_cap) &&
	    w->param.inamp_cap != 0)
		hdaa_dump_amp_sb(&sb, w->param.inamp_cap, " Input");
	if (w->nconns > 0)
		sbuf_printf(&sb, "    Connections: %d\n", w->nconns);
	for (j = 0; j < w->nconns; j++) {
		cw = hdaa_widget_get(devinfo, w->conns[j]);
		sbuf_printf(&sb, "          + %s<- nid=%d [%s]",
		    (w->connsenable[j] == 0)?"[DISABLED] ":"",
		    w->conns[j], (cw == NULL) ? "GHOST!" : cw->name);
		if (cw == NULL)
			sbuf_printf(&sb, " [UNKNOWN]");
		else if (cw->enable == 0)
			sbuf_printf(&sb, " [DISABLED]");
		if (w->nconns > 1 && w->selconn == j && w->type !=
		    HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER)
			sbuf_printf(&sb, " (selected)");
		sbuf_printf(&sb, "\n");
	}
	error = sbuf_finish(&sb);
	sbuf_delete(&sb);
	return (error);
}

static int
hdaa_sysctl_config(SYSCTL_HANDLER_ARGS)
{
	char buf[256];
	int error;
	uint32_t conf;

	conf = *(uint32_t *)oidp->oid_arg1;
	snprintf(buf, sizeof(buf), "0x%08x as=%d seq=%d "
	    "device=%s conn=%s ctype=%s loc=%s color=%s misc=%d",
	    conf,
	    HDA_CONFIG_DEFAULTCONF_ASSOCIATION(conf),
	    HDA_CONFIG_DEFAULTCONF_SEQUENCE(conf),
	    HDA_DEVS[HDA_CONFIG_DEFAULTCONF_DEVICE(conf)],
	    HDA_CONNS[HDA_CONFIG_DEFAULTCONF_CONNECTIVITY(conf)],
	    HDA_CONNECTORS[HDA_CONFIG_DEFAULTCONF_CONNECTION_TYPE(conf)],
	    HDA_LOCS[HDA_CONFIG_DEFAULTCONF_LOCATION(conf)],
	    HDA_COLORS[HDA_CONFIG_DEFAULTCONF_COLOR(conf)],
	    HDA_CONFIG_DEFAULTCONF_MISC(conf));
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (strncmp(buf, "0x", 2) == 0)
		conf = strtol(buf + 2, NULL, 16);
	else
		conf = hdaa_widget_pin_patch(conf, buf);
	*(uint32_t *)oidp->oid_arg1 = conf;
	return (0);
}

static void
hdaa_config_fetch(const char *str, uint32_t *on, uint32_t *off)
{
	int i = 0, j, k, len, inv;

	for (;;) {
		while (str[i] != '\0' &&
		    (str[i] == ',' || isspace(str[i]) != 0))
			i++;
		if (str[i] == '\0')
			return;
		j = i;
		while (str[j] != '\0' &&
		    !(str[j] == ',' || isspace(str[j]) != 0))
			j++;
		len = j - i;
		if (len > 2 && strncmp(str + i, "no", 2) == 0)
			inv = 2;
		else
			inv = 0;
		for (k = 0; len > inv && k < nitems(hdaa_quirks_tab); k++) {
			if (strncmp(str + i + inv,
			    hdaa_quirks_tab[k].key, len - inv) != 0)
				continue;
			if (len - inv != strlen(hdaa_quirks_tab[k].key))
				continue;
			if (inv == 0) {
				*on |= hdaa_quirks_tab[k].value;
				*off &= ~hdaa_quirks_tab[k].value;
			} else {
				*off |= hdaa_quirks_tab[k].value;
				*on &= ~hdaa_quirks_tab[k].value;
			}
			break;
		}
		i = j;
	}
}

static int
hdaa_sysctl_quirks(SYSCTL_HANDLER_ARGS)
{
	char buf[256];
	int error, n = 0, i;
	uint32_t quirks, quirks_off;

	quirks = *(uint32_t *)oidp->oid_arg1;
	buf[0] = 0;
	for (i = 0; i < nitems(hdaa_quirks_tab); i++) {
		if ((quirks & hdaa_quirks_tab[i].value) != 0)
			n += snprintf(buf + n, sizeof(buf) - n, "%s%s",
			    n != 0 ? "," : "", hdaa_quirks_tab[i].key);
	}
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (strncmp(buf, "0x", 2) == 0)
		quirks = strtol(buf + 2, NULL, 16);
	else {
		quirks = 0;
		hdaa_config_fetch(buf, &quirks, &quirks_off);
	}
	*(uint32_t *)oidp->oid_arg1 = quirks;
	return (0);
}

static void
hdaa_local_patch(struct hdaa_devinfo *devinfo)
{
	struct hdaa_widget *w;
	const char *res = NULL;
	uint32_t quirks_on = 0, quirks_off = 0, x;
	int i;

	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL)
			continue;
		if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			hdaa_local_patch_pin(w);
	}

	if (resource_string_value(device_get_name(devinfo->dev),
	    device_get_unit(devinfo->dev), "config", &res) == 0) {
		if (res != NULL && strlen(res) > 0)
			hdaa_config_fetch(res, &quirks_on, &quirks_off);
		devinfo->quirks |= quirks_on;
		devinfo->quirks &= ~quirks_off;
	}
	if (devinfo->newquirks == -1)
		devinfo->newquirks = devinfo->quirks;
	else
		devinfo->quirks = devinfo->newquirks;
	HDA_BOOTHVERBOSE(
		device_printf(devinfo->dev,
		    "Config options: 0x%08x\n", devinfo->quirks);
	);

	if (resource_string_value(device_get_name(devinfo->dev),
	    device_get_unit(devinfo->dev), "gpio_config", &res) == 0) {
		if (strncmp(res, "0x", 2) == 0) {
			devinfo->gpio = strtol(res + 2, NULL, 16);
		} else {
			devinfo->gpio = hdaa_gpio_patch(devinfo->gpio, res);
		}
	}
	if (devinfo->newgpio == -1)
		devinfo->newgpio = devinfo->gpio;
	else
		devinfo->gpio = devinfo->newgpio;
	if (devinfo->newgpo == -1)
		devinfo->newgpo = devinfo->gpo;
	else
		devinfo->gpo = devinfo->newgpo;
	HDA_BOOTHVERBOSE(
		device_printf(devinfo->dev, "GPIO config options:");
		for (i = 0; i < 7; i++) {
			x = (devinfo->gpio & HDAA_GPIO_MASK(i)) >> HDAA_GPIO_SHIFT(i);
			if (x != 0)
				printf(" %d=%s", i, HDA_GPIO_ACTIONS[x]);
		}
		printf("\n");
	);
}

static void
hdaa_widget_connection_parse(struct hdaa_widget *w)
{
	uint32_t res;
	int i, j, max, ents, entnum;
	nid_t nid = w->nid;
	nid_t cnid, addcnid, prevcnid;

	w->nconns = 0;

	res = hda_command(w->devinfo->dev,
	    HDA_CMD_GET_PARAMETER(0, nid, HDA_PARAM_CONN_LIST_LENGTH));

	ents = HDA_PARAM_CONN_LIST_LENGTH_LIST_LENGTH(res);

	if (ents < 1)
		return;

	entnum = HDA_PARAM_CONN_LIST_LENGTH_LONG_FORM(res) ? 2 : 4;
	max = (sizeof(w->conns) / sizeof(w->conns[0])) - 1;
	prevcnid = 0;

#define CONN_RMASK(e)		(1 << ((32 / (e)) - 1))
#define CONN_NMASK(e)		(CONN_RMASK(e) - 1)
#define CONN_RESVAL(r, e, n)	((r) >> ((32 / (e)) * (n)))
#define CONN_RANGE(r, e, n)	(CONN_RESVAL(r, e, n) & CONN_RMASK(e))
#define CONN_CNID(r, e, n)	(CONN_RESVAL(r, e, n) & CONN_NMASK(e))

	for (i = 0; i < ents; i += entnum) {
		res = hda_command(w->devinfo->dev,
		    HDA_CMD_GET_CONN_LIST_ENTRY(0, nid, i));
		for (j = 0; j < entnum; j++) {
			cnid = CONN_CNID(res, entnum, j);
			if (cnid == 0) {
				if (w->nconns < ents)
					device_printf(w->devinfo->dev,
					    "WARNING: nid=%d has zero cnid "
					    "entnum=%d j=%d index=%d "
					    "entries=%d found=%d res=0x%08x\n",
					    nid, entnum, j, i,
					    ents, w->nconns, res);
				else
					goto getconns_out;
			}
			if (cnid < w->devinfo->startnode ||
			    cnid >= w->devinfo->endnode) {
				HDA_BOOTVERBOSE(
					device_printf(w->devinfo->dev,
					    "WARNING: nid=%d has cnid outside "
					    "of the AFG range j=%d "
					    "entnum=%d index=%d res=0x%08x\n",
					    nid, j, entnum, i, res);
				);
			}
			if (CONN_RANGE(res, entnum, j) == 0)
				addcnid = cnid;
			else if (prevcnid == 0 || prevcnid >= cnid) {
				device_printf(w->devinfo->dev,
				    "WARNING: Invalid child range "
				    "nid=%d index=%d j=%d entnum=%d "
				    "prevcnid=%d cnid=%d res=0x%08x\n",
				    nid, i, j, entnum, prevcnid,
				    cnid, res);
				addcnid = cnid;
			} else
				addcnid = prevcnid + 1;
			while (addcnid <= cnid) {
				if (w->nconns > max) {
					device_printf(w->devinfo->dev,
					    "Adding %d (nid=%d): "
					    "Max connection reached! max=%d\n",
					    addcnid, nid, max + 1);
					goto getconns_out;
				}
				w->connsenable[w->nconns] = 1;
				w->conns[w->nconns++] = addcnid++;
			}
			prevcnid = cnid;
		}
	}

getconns_out:
	return;
}

static void
hdaa_widget_parse(struct hdaa_widget *w)
{
	device_t dev = w->devinfo->dev;
	uint32_t wcap, cap;
	nid_t nid = w->nid;
	char buf[64];

	w->param.widget_cap = wcap = hda_command(dev,
	    HDA_CMD_GET_PARAMETER(0, nid, HDA_PARAM_AUDIO_WIDGET_CAP));
	w->type = HDA_PARAM_AUDIO_WIDGET_CAP_TYPE(wcap);

	hdaa_widget_connection_parse(w);

	if (HDA_PARAM_AUDIO_WIDGET_CAP_OUT_AMP(wcap)) {
		if (HDA_PARAM_AUDIO_WIDGET_CAP_AMP_OVR(wcap))
			w->param.outamp_cap =
			    hda_command(dev,
			    HDA_CMD_GET_PARAMETER(0, nid,
			    HDA_PARAM_OUTPUT_AMP_CAP));
		else
			w->param.outamp_cap =
			    w->devinfo->outamp_cap;
	} else
		w->param.outamp_cap = 0;

	if (HDA_PARAM_AUDIO_WIDGET_CAP_IN_AMP(wcap)) {
		if (HDA_PARAM_AUDIO_WIDGET_CAP_AMP_OVR(wcap))
			w->param.inamp_cap =
			    hda_command(dev,
			    HDA_CMD_GET_PARAMETER(0, nid,
			    HDA_PARAM_INPUT_AMP_CAP));
		else
			w->param.inamp_cap =
			    w->devinfo->inamp_cap;
	} else
		w->param.inamp_cap = 0;

	if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT ||
	    w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT) {
		if (HDA_PARAM_AUDIO_WIDGET_CAP_FORMAT_OVR(wcap)) {
			cap = hda_command(dev,
			    HDA_CMD_GET_PARAMETER(0, nid,
			    HDA_PARAM_SUPP_STREAM_FORMATS));
			w->param.supp_stream_formats = (cap != 0) ? cap :
			    w->devinfo->supp_stream_formats;
			cap = hda_command(dev,
			    HDA_CMD_GET_PARAMETER(0, nid,
			    HDA_PARAM_SUPP_PCM_SIZE_RATE));
			w->param.supp_pcm_size_rate = (cap != 0) ? cap :
			    w->devinfo->supp_pcm_size_rate;
		} else {
			w->param.supp_stream_formats =
			    w->devinfo->supp_stream_formats;
			w->param.supp_pcm_size_rate =
			    w->devinfo->supp_pcm_size_rate;
		}
		if (HDA_PARAM_AUDIO_WIDGET_CAP_STRIPE(w->param.widget_cap)) {
			w->wclass.conv.stripecap = hda_command(dev,
			    HDA_CMD_GET_STRIPE_CONTROL(0, w->nid)) >> 20;
		} else
			w->wclass.conv.stripecap = 1;
	} else {
		w->param.supp_stream_formats = 0;
		w->param.supp_pcm_size_rate = 0;
	}

	if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) {
		w->wclass.pin.original = w->wclass.pin.newconf =
		    w->wclass.pin.config = hda_command(dev,
			HDA_CMD_GET_CONFIGURATION_DEFAULT(0, w->nid));
		w->wclass.pin.cap = hda_command(dev,
		    HDA_CMD_GET_PARAMETER(0, w->nid, HDA_PARAM_PIN_CAP));
		w->wclass.pin.ctrl = hda_command(dev,
		    HDA_CMD_GET_PIN_WIDGET_CTRL(0, nid));
		w->wclass.pin.connected = 2;
		if (HDA_PARAM_PIN_CAP_EAPD_CAP(w->wclass.pin.cap)) {
			w->param.eapdbtl = hda_command(dev,
			    HDA_CMD_GET_EAPD_BTL_ENABLE(0, nid));
			w->param.eapdbtl &= 0x7;
			w->param.eapdbtl |= HDA_CMD_SET_EAPD_BTL_ENABLE_EAPD;
		} else
			w->param.eapdbtl = HDA_INVALID;
	}
	w->unsol = -1;

	hdaa_unlock(w->devinfo);
	snprintf(buf, sizeof(buf), "nid%d", w->nid);
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    buf, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    w, 0, hdaa_sysctl_caps, "A", "Node capabilities");
	if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) {
		snprintf(buf, sizeof(buf), "nid%d_config", w->nid);
		SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
		    buf, CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE,
		    &w->wclass.pin.newconf, 0, hdaa_sysctl_config, "A",
		    "Current pin configuration");
		snprintf(buf, sizeof(buf), "nid%d_original", w->nid);
		SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
		    buf, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
		    &w->wclass.pin.original, 0, hdaa_sysctl_config, "A",
		    "Original pin configuration");
	}
	hdaa_lock(w->devinfo);
}

static void
hdaa_widget_postprocess(struct hdaa_widget *w)
{
	const char *typestr;

	w->type = HDA_PARAM_AUDIO_WIDGET_CAP_TYPE(w->param.widget_cap);
	switch (w->type) {
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT:
		typestr = "audio output";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT:
		typestr = "audio input";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER:
		typestr = "audio mixer";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR:
		typestr = "audio selector";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX:
		typestr = "pin";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_POWER_WIDGET:
		typestr = "power widget";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_VOLUME_WIDGET:
		typestr = "volume widget";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_BEEP_WIDGET:
		typestr = "beep widget";
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_VENDOR_WIDGET:
		typestr = "vendor widget";
		break;
	default:
		typestr = "unknown type";
		break;
	}
	strlcpy(w->name, typestr, sizeof(w->name));

	if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) {
		uint32_t config;
		const char *devstr;
		int conn, color;

		config = w->wclass.pin.config;
		devstr = HDA_DEVS[(config & HDA_CONFIG_DEFAULTCONF_DEVICE_MASK) >>
		    HDA_CONFIG_DEFAULTCONF_DEVICE_SHIFT];
		conn = (config & HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK) >>
		    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_SHIFT;
		color = (config & HDA_CONFIG_DEFAULTCONF_COLOR_MASK) >>
		    HDA_CONFIG_DEFAULTCONF_COLOR_SHIFT;
		strlcat(w->name, ": ", sizeof(w->name));
		strlcat(w->name, devstr, sizeof(w->name));
		strlcat(w->name, " (", sizeof(w->name));
		if (conn == 0 && color != 0 && color != 15) {
			strlcat(w->name, HDA_COLORS[color], sizeof(w->name));
			strlcat(w->name, " ", sizeof(w->name));
		}
		strlcat(w->name, HDA_CONNS[conn], sizeof(w->name));
		strlcat(w->name, ")", sizeof(w->name));
	}
}

struct hdaa_widget *
hdaa_widget_get(struct hdaa_devinfo *devinfo, nid_t nid)
{
	if (devinfo == NULL || devinfo->widget == NULL ||
		    nid < devinfo->startnode || nid >= devinfo->endnode)
		return (NULL);
	return (&devinfo->widget[nid - devinfo->startnode]);
}

static void
hdaa_audio_ctl_amp_set_internal(struct hdaa_devinfo *devinfo, nid_t nid,
					int index, int lmute, int rmute,
					int left, int right, int dir)
{
	uint16_t v = 0;

	HDA_BOOTHVERBOSE(
		device_printf(devinfo->dev,
		    "Setting amplifier nid=%d index=%d %s mute=%d/%d vol=%d/%d\n",
		    nid,index,dir ? "in" : "out",lmute,rmute,left,right);
	);
	if (left != right || lmute != rmute) {
		v = (1 << (15 - dir)) | (1 << 13) | (index << 8) |
		    (lmute << 7) | left;
		hda_command(devinfo->dev,
		    HDA_CMD_SET_AMP_GAIN_MUTE(0, nid, v));
		v = (1 << (15 - dir)) | (1 << 12) | (index << 8) |
		    (rmute << 7) | right;
	} else
		v = (1 << (15 - dir)) | (3 << 12) | (index << 8) |
		    (lmute << 7) | left;

	hda_command(devinfo->dev,
	    HDA_CMD_SET_AMP_GAIN_MUTE(0, nid, v));
}

static void
hdaa_audio_ctl_amp_set(struct hdaa_audio_ctl *ctl, uint32_t mute,
						int left, int right)
{
	nid_t nid;
	int lmute, rmute;

	nid = ctl->widget->nid;

	/* Save new values if valid. */
	if (mute != HDAA_AMP_MUTE_DEFAULT)
		ctl->muted = mute;
	if (left != HDAA_AMP_VOL_DEFAULT)
		ctl->left = left;
	if (right != HDAA_AMP_VOL_DEFAULT)
		ctl->right = right;
	/* Prepare effective values */
	if (ctl->forcemute) {
		lmute = 1;
		rmute = 1;
		left = 0;
		right = 0;
	} else {
		lmute = HDAA_AMP_LEFT_MUTED(ctl->muted);
		rmute = HDAA_AMP_RIGHT_MUTED(ctl->muted);
		left = ctl->left;
		right = ctl->right;
	}
	/* Apply effective values */
	if (ctl->dir & HDAA_CTL_OUT)
		hdaa_audio_ctl_amp_set_internal(ctl->widget->devinfo, nid, ctl->index,
		    lmute, rmute, left, right, 0);
	if (ctl->dir & HDAA_CTL_IN)
		hdaa_audio_ctl_amp_set_internal(ctl->widget->devinfo, nid, ctl->index,
		    lmute, rmute, left, right, 1);
}

static void
hdaa_widget_connection_select(struct hdaa_widget *w, uint8_t index)
{
	if (w == NULL || w->nconns < 1 || index > (w->nconns - 1))
		return;
	HDA_BOOTHVERBOSE(
		device_printf(w->devinfo->dev,
		    "Setting selector nid=%d index=%d\n", w->nid, index);
	);
	hda_command(w->devinfo->dev,
	    HDA_CMD_SET_CONNECTION_SELECT_CONTROL(0, w->nid, index));
	w->selconn = index;
}

/****************************************************************************
 * Device Methods
 ****************************************************************************/

static void *
hdaa_channel_init(kobj_t obj, void *data, struct snd_dbuf *b,
					struct pcm_channel *c, int dir)
{
	struct hdaa_chan *ch = data;
	struct hdaa_pcm_devinfo *pdevinfo = ch->pdevinfo;
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;

	hdaa_lock(devinfo);
	if (devinfo->quirks & HDAA_QUIRK_FIXEDRATE) {
		ch->caps.minspeed = ch->caps.maxspeed = 48000;
		ch->pcmrates[0] = 48000;
		ch->pcmrates[1] = 0;
	}
	ch->dir = dir;
	ch->b = b;
	ch->c = c;
	ch->blksz = pdevinfo->chan_size / pdevinfo->chan_blkcnt;
	ch->blkcnt = pdevinfo->chan_blkcnt;
	hdaa_unlock(devinfo);

	if (sndbuf_alloc(ch->b, bus_get_dma_tag(devinfo->dev),
	    hda_get_dma_nocache(devinfo->dev) ? BUS_DMA_NOCACHE :
	    BUS_DMA_COHERENT,
	    pdevinfo->chan_size) != 0)
		return (NULL);

	return (ch);
}

static int
hdaa_channel_setformat(kobj_t obj, void *data, uint32_t format)
{
	struct hdaa_chan *ch = data;
	int i;

	for (i = 0; ch->caps.fmtlist[i] != 0; i++) {
		if (format == ch->caps.fmtlist[i]) {
			ch->fmt = format;
			return (0);
		}
	}

	return (EINVAL);
}

static uint32_t
hdaa_channel_setspeed(kobj_t obj, void *data, uint32_t speed)
{
	struct hdaa_chan *ch = data;
	uint32_t spd = 0, threshold;
	int i;

	/* First look for equal or multiple frequency. */
	for (i = 0; ch->pcmrates[i] != 0; i++) {
		spd = ch->pcmrates[i];
		if (speed != 0 && spd / speed * speed == spd) {
			ch->spd = spd;
			return (spd);
		}
	}
	/* If no match, just find nearest. */
	for (i = 0; ch->pcmrates[i] != 0; i++) {
		spd = ch->pcmrates[i];
		threshold = spd + ((ch->pcmrates[i + 1] != 0) ?
		    ((ch->pcmrates[i + 1] - spd) >> 1) : 0);
		if (speed < threshold)
			break;
	}
	ch->spd = spd;
	return (spd);
}

static uint16_t
hdaa_stream_format(struct hdaa_chan *ch)
{
	int i;
	uint16_t fmt;

	fmt = 0;
	if (ch->fmt & AFMT_S16_LE)
		fmt |= ch->bit16 << 4;
	else if (ch->fmt & AFMT_S32_LE)
		fmt |= ch->bit32 << 4;
	else
		fmt |= 1 << 4;
	for (i = 0; i < HDA_RATE_TAB_LEN; i++) {
		if (hda_rate_tab[i].valid && ch->spd == hda_rate_tab[i].rate) {
			fmt |= hda_rate_tab[i].base;
			fmt |= hda_rate_tab[i].mul;
			fmt |= hda_rate_tab[i].div;
			break;
		}
	}
	fmt |= (AFMT_CHANNEL(ch->fmt) - 1);

	return (fmt);
}

static int
hdaa_allowed_stripes(uint16_t fmt)
{
	static const int bits[8] = { 8, 16, 20, 24, 32, 32, 32, 32 };
	int size;

	size = bits[(fmt >> 4) & 0x03];
	size *= (fmt & 0x0f) + 1;
	size *= ((fmt >> 11) & 0x07) + 1;
	return (0xffffffffU >> (32 - fls(size / 8)));
}

static void
hdaa_audio_setup(struct hdaa_chan *ch)
{
	struct hdaa_audio_as *as = &ch->devinfo->as[ch->as];
	struct hdaa_widget *w, *wp;
	int i, j, k, chn, cchn, totalchn, totalextchn, c;
	uint16_t fmt, dfmt;
	/* Mapping channel pairs to codec pins/converters. */
	const static uint16_t convmap[2][5] =
	    /*  1.0     2.0     4.0     5.1     7.1  */
	    {{ 0x0010, 0x0001, 0x0201, 0x0231, 0x4231 },	/* no dup. */
	     { 0x0010, 0x0001, 0x2201, 0x2231, 0x4231 }};	/* side dup. */
	/* Mapping formats to HDMI channel allocations. */
	const static uint8_t hdmica[2][8] =
	    /*  1     2     3     4     5     6     7     8  */
	    {{ 0x02, 0x00, 0x04, 0x08, 0x0a, 0x0e, 0x12, 0x12 }, /* x.0 */
	     { 0x01, 0x03, 0x01, 0x03, 0x09, 0x0b, 0x0f, 0x13 }}; /* x.1 */
	/* Mapping formats to HDMI channels order. */
	const static uint32_t hdmich[2][8] =
	    /*  1  /  5     2  /  6     3  /  7     4  /  8  */
	    {{ 0xFFFF0F00, 0xFFFFFF10, 0xFFF2FF10, 0xFF32FF10,
	       0xFF324F10, 0xF5324F10, 0x54326F10, 0x54326F10 }, /* x.0 */
	     { 0xFFFFF000, 0xFFFF0100, 0xFFFFF210, 0xFFFF2310,
	       0xFF32F410, 0xFF324510, 0xF6324510, 0x76325410 }}; /* x.1 */
	int convmapid = -1;
	nid_t nid;
	uint8_t csum;

	totalchn = AFMT_CHANNEL(ch->fmt);
	totalextchn = AFMT_EXTCHANNEL(ch->fmt);
	HDA_BOOTHVERBOSE(
		device_printf(ch->pdevinfo->dev,
		    "PCMDIR_%s: Stream setup fmt=%08x (%d.%d) speed=%d\n",
		    (ch->dir == PCMDIR_PLAY) ? "PLAY" : "REC",
		    ch->fmt, totalchn - totalextchn, totalextchn, ch->spd);
	);
	fmt = hdaa_stream_format(ch);

	/* Set channels to I/O converters mapping for known speaker setups. */
	if ((as->pinset == 0x0007 || as->pinset == 0x0013) || /* Standard 5.1 */
	    (as->pinset == 0x0017)) /* Standard 7.1 */
		convmapid = (ch->dir == PCMDIR_PLAY);

	dfmt = HDA_CMD_SET_DIGITAL_CONV_FMT1_DIGEN;
	if (ch->fmt & AFMT_AC3)
		dfmt |= HDA_CMD_SET_DIGITAL_CONV_FMT1_NAUDIO;

	chn = 0;
	for (i = 0; ch->io[i] != -1; i++) {
		w = hdaa_widget_get(ch->devinfo, ch->io[i]);
		if (w == NULL)
			continue;

		/* If HP redirection is enabled, but failed to use same
		   DAC, make last DAC to duplicate first one. */
		if (as->fakeredir && i == (as->pincnt - 1)) {
			c = (ch->sid << 4);
		} else {
			/* Map channels to I/O converters, if set. */
			if (convmapid >= 0)
				chn = (((convmap[convmapid][totalchn / 2]
				    >> i * 4) & 0xf) - 1) * 2;
			if (chn < 0 || chn >= totalchn) {
				c = 0;
			} else {
				c = (ch->sid << 4) | chn;
			}
		}
		hda_command(ch->devinfo->dev,
		    HDA_CMD_SET_CONV_FMT(0, ch->io[i], fmt));
		if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(w->param.widget_cap)) {
			hda_command(ch->devinfo->dev,
			    HDA_CMD_SET_DIGITAL_CONV_FMT1(0, ch->io[i], dfmt));
		}
		hda_command(ch->devinfo->dev,
		    HDA_CMD_SET_CONV_STREAM_CHAN(0, ch->io[i], c));
		if (HDA_PARAM_AUDIO_WIDGET_CAP_STRIPE(w->param.widget_cap)) {
			hda_command(ch->devinfo->dev,
			    HDA_CMD_SET_STRIPE_CONTROL(0, w->nid, ch->stripectl));
		}
		cchn = HDA_PARAM_AUDIO_WIDGET_CAP_CC(w->param.widget_cap);
		if (cchn > 1 && chn < totalchn) {
			cchn = min(cchn, totalchn - chn - 1);
			hda_command(ch->devinfo->dev,
			    HDA_CMD_SET_CONV_CHAN_COUNT(0, ch->io[i], cchn));
		}
		HDA_BOOTHVERBOSE(
			device_printf(ch->pdevinfo->dev,
			    "PCMDIR_%s: Stream setup nid=%d: "
			    "fmt=0x%04x, dfmt=0x%04x, chan=0x%04x, "
			    "chan_count=0x%02x, stripe=%d\n",
			    (ch->dir == PCMDIR_PLAY) ? "PLAY" : "REC",
			    ch->io[i], fmt, dfmt, c, cchn, ch->stripectl);
		);
		for (j = 0; j < 16; j++) {
			if (as->dacs[ch->asindex][j] != ch->io[i])
				continue;
			nid = as->pins[j];
			wp = hdaa_widget_get(ch->devinfo, nid);
			if (wp == NULL)
				continue;
			if (!HDA_PARAM_PIN_CAP_DP(wp->wclass.pin.cap) &&
			    !HDA_PARAM_PIN_CAP_HDMI(wp->wclass.pin.cap))
				continue;

			/* Set channel mapping. */
			for (k = 0; k < 8; k++) {
				hda_command(ch->devinfo->dev,
				    HDA_CMD_SET_HDMI_CHAN_SLOT(0, nid,
				    (((hdmich[totalextchn == 0 ? 0 : 1][totalchn - 1]
				     >> (k * 4)) & 0xf) << 4) | k));
			}

			/*
			 * Enable High Bit Rate (HBR) Encoded Packet Type
			 * (EPT), if supported and needed (8ch data).
			 */
			if (HDA_PARAM_PIN_CAP_HDMI(wp->wclass.pin.cap) &&
			    HDA_PARAM_PIN_CAP_HBR(wp->wclass.pin.cap)) {
				wp->wclass.pin.ctrl &=
				    ~HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE_MASK;
				if ((ch->fmt & AFMT_AC3) && (cchn == 7))
					wp->wclass.pin.ctrl |= 0x03;
				hda_command(ch->devinfo->dev,
				    HDA_CMD_SET_PIN_WIDGET_CTRL(0, nid,
				    wp->wclass.pin.ctrl));
			}

			/* Stop audio infoframe transmission. */
			hda_command(ch->devinfo->dev,
			    HDA_CMD_SET_HDMI_DIP_INDEX(0, nid, 0x00));
			hda_command(ch->devinfo->dev,
			    HDA_CMD_SET_HDMI_DIP_XMIT(0, nid, 0x00));

			/* Clear audio infoframe buffer. */
			hda_command(ch->devinfo->dev,
			    HDA_CMD_SET_HDMI_DIP_INDEX(0, nid, 0x00));
			for (k = 0; k < 32; k++)
				hda_command(ch->devinfo->dev,
				    HDA_CMD_SET_HDMI_DIP_DATA(0, nid, 0x00));

			/* Write HDMI/DisplayPort audio infoframe. */
			hda_command(ch->devinfo->dev,
			    HDA_CMD_SET_HDMI_DIP_INDEX(0, nid, 0x00));
			if (w->eld != NULL && w->eld_len >= 6 &&
			    ((w->eld[5] >> 2) & 0x3) == 1) { /* DisplayPort */
				hda_command(ch->devinfo->dev,
				    HDA_CMD_SET_HDMI_DIP_DATA(0, nid, 0x84));
				hda_command(ch->devinfo->dev,
				    HDA_CMD_SET_HDMI_DIP_DATA(0, nid, 0x1b));
				hda_command(ch->devinfo->dev,
				    HDA_CMD_SET_HDMI_DIP_DATA(0, nid, 0x44));
			} else {	/* HDMI */
				hda_command(ch->devinfo->dev,
				    HDA_CMD_SET_HDMI_DIP_DATA(0, nid, 0x84));
				hda_command(ch->devinfo->dev,
				    HDA_CMD_SET_HDMI_DIP_DATA(0, nid, 0x01));
				hda_command(ch->devinfo->dev,
				    HDA_CMD_SET_HDMI_DIP_DATA(0, nid, 0x0a));
				csum = 0;
				csum -= 0x84 + 0x01 + 0x0a + (totalchn - 1) +
				    hdmica[totalextchn == 0 ? 0 : 1][totalchn - 1];
				hda_command(ch->devinfo->dev,
				    HDA_CMD_SET_HDMI_DIP_DATA(0, nid, csum));
			}
			hda_command(ch->devinfo->dev,
			    HDA_CMD_SET_HDMI_DIP_DATA(0, nid, totalchn - 1));
			hda_command(ch->devinfo->dev,
			    HDA_CMD_SET_HDMI_DIP_DATA(0, nid, 0x00));
			hda_command(ch->devinfo->dev,
			    HDA_CMD_SET_HDMI_DIP_DATA(0, nid, 0x00));
			hda_command(ch->devinfo->dev,
			    HDA_CMD_SET_HDMI_DIP_DATA(0, nid,
			    hdmica[totalextchn == 0 ? 0 : 1][totalchn - 1]));

			/* Start audio infoframe transmission. */
			hda_command(ch->devinfo->dev,
			    HDA_CMD_SET_HDMI_DIP_INDEX(0, nid, 0x00));
			hda_command(ch->devinfo->dev,
			    HDA_CMD_SET_HDMI_DIP_XMIT(0, nid, 0xc0));
		}
		chn += cchn + 1;
	}
}

/*
 * Greatest Common Divisor.
 */
static unsigned
gcd(unsigned a, unsigned b)
{
	u_int c;

	while (b != 0) {
		c = a;
		a = b;
		b = (c % b);
	}
	return (a);
}

/*
 * Least Common Multiple.
 */
static unsigned
lcm(unsigned a, unsigned b)
{

	return ((a * b) / gcd(a, b));
}

static int
hdaa_channel_setfragments(kobj_t obj, void *data,
					uint32_t blksz, uint32_t blkcnt)
{
	struct hdaa_chan *ch = data;

	blksz -= blksz % lcm(HDA_DMA_ALIGNMENT, sndbuf_getalign(ch->b));

	if (blksz > (sndbuf_getmaxsize(ch->b) / HDA_BDL_MIN))
		blksz = sndbuf_getmaxsize(ch->b) / HDA_BDL_MIN;
	if (blksz < HDA_BLK_MIN)
		blksz = HDA_BLK_MIN;
	if (blkcnt > HDA_BDL_MAX)
		blkcnt = HDA_BDL_MAX;
	if (blkcnt < HDA_BDL_MIN)
		blkcnt = HDA_BDL_MIN;

	while ((blksz * blkcnt) > sndbuf_getmaxsize(ch->b)) {
		if ((blkcnt >> 1) >= HDA_BDL_MIN)
			blkcnt >>= 1;
		else if ((blksz >> 1) >= HDA_BLK_MIN)
			blksz >>= 1;
		else
			break;
	}

	if ((sndbuf_getblksz(ch->b) != blksz ||
	    sndbuf_getblkcnt(ch->b) != blkcnt) &&
	    sndbuf_resize(ch->b, blkcnt, blksz) != 0)
		device_printf(ch->devinfo->dev, "%s: failed blksz=%u blkcnt=%u\n",
		    __func__, blksz, blkcnt);

	ch->blksz = sndbuf_getblksz(ch->b);
	ch->blkcnt = sndbuf_getblkcnt(ch->b);

	return (0);
}

static uint32_t
hdaa_channel_setblocksize(kobj_t obj, void *data, uint32_t blksz)
{
	struct hdaa_chan *ch = data;

	hdaa_channel_setfragments(obj, data, blksz, ch->pdevinfo->chan_blkcnt);

	return (ch->blksz);
}

static void
hdaa_channel_stop(struct hdaa_chan *ch)
{
	struct hdaa_devinfo *devinfo = ch->devinfo;
	struct hdaa_widget *w;
	int i;

	if ((ch->flags & HDAA_CHN_RUNNING) == 0)
		return;
	ch->flags &= ~HDAA_CHN_RUNNING;
	HDAC_STREAM_STOP(device_get_parent(devinfo->dev), devinfo->dev,
	    ch->dir == PCMDIR_PLAY ? 1 : 0, ch->sid);
	for (i = 0; ch->io[i] != -1; i++) {
		w = hdaa_widget_get(ch->devinfo, ch->io[i]);
		if (w == NULL)
			continue;
		if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(w->param.widget_cap)) {
			hda_command(devinfo->dev,
			    HDA_CMD_SET_DIGITAL_CONV_FMT1(0, ch->io[i], 0));
		}
		hda_command(devinfo->dev,
		    HDA_CMD_SET_CONV_STREAM_CHAN(0, ch->io[i],
		    0));
	}
	HDAC_STREAM_FREE(device_get_parent(devinfo->dev), devinfo->dev,
	    ch->dir == PCMDIR_PLAY ? 1 : 0, ch->sid);
}

static int
hdaa_channel_start(struct hdaa_chan *ch)
{
	struct hdaa_devinfo *devinfo = ch->devinfo;
	uint32_t fmt;

	fmt = hdaa_stream_format(ch);
	ch->stripectl = fls(ch->stripecap & hdaa_allowed_stripes(fmt) &
	    hda_get_stripes_mask(devinfo->dev)) - 1;
	ch->sid = HDAC_STREAM_ALLOC(device_get_parent(devinfo->dev), devinfo->dev,
	    ch->dir == PCMDIR_PLAY ? 1 : 0, fmt, ch->stripectl, &ch->dmapos);
	if (ch->sid <= 0)
		return (EBUSY);
	hdaa_audio_setup(ch);
	HDAC_STREAM_RESET(device_get_parent(devinfo->dev), devinfo->dev,
	    ch->dir == PCMDIR_PLAY ? 1 : 0, ch->sid);
	HDAC_STREAM_START(device_get_parent(devinfo->dev), devinfo->dev,
	    ch->dir == PCMDIR_PLAY ? 1 : 0, ch->sid,
	    sndbuf_getbufaddr(ch->b), ch->blksz, ch->blkcnt);
	ch->flags |= HDAA_CHN_RUNNING;
	return (0);
}

static int
hdaa_channel_trigger(kobj_t obj, void *data, int go)
{
	struct hdaa_chan *ch = data;
	int error = 0;

	if (!PCMTRIG_COMMON(go))
		return (0);

	hdaa_lock(ch->devinfo);
	switch (go) {
	case PCMTRIG_START:
		error = hdaa_channel_start(ch);
		break;
	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		hdaa_channel_stop(ch);
		break;
	default:
		break;
	}
	hdaa_unlock(ch->devinfo);

	return (error);
}

static uint32_t
hdaa_channel_getptr(kobj_t obj, void *data)
{
	struct hdaa_chan *ch = data;
	struct hdaa_devinfo *devinfo = ch->devinfo;
	uint32_t ptr;

	hdaa_lock(devinfo);
	if (ch->dmapos != NULL) {
		ptr = *(ch->dmapos);
	} else {
		ptr = HDAC_STREAM_GETPTR(
		    device_get_parent(devinfo->dev), devinfo->dev,
		    ch->dir == PCMDIR_PLAY ? 1 : 0, ch->sid);
	}
	hdaa_unlock(devinfo);

	/*
	 * Round to available space and force 128 bytes aligment.
	 */
	ptr %= ch->blksz * ch->blkcnt;
	ptr &= HDA_BLK_ALIGN;

	return (ptr);
}

static struct pcmchan_caps *
hdaa_channel_getcaps(kobj_t obj, void *data)
{
	return (&((struct hdaa_chan *)data)->caps);
}

static kobj_method_t hdaa_channel_methods[] = {
	KOBJMETHOD(channel_init,		hdaa_channel_init),
	KOBJMETHOD(channel_setformat,		hdaa_channel_setformat),
	KOBJMETHOD(channel_setspeed,		hdaa_channel_setspeed),
	KOBJMETHOD(channel_setblocksize,	hdaa_channel_setblocksize),
	KOBJMETHOD(channel_setfragments,	hdaa_channel_setfragments),
	KOBJMETHOD(channel_trigger,		hdaa_channel_trigger),
	KOBJMETHOD(channel_getptr,		hdaa_channel_getptr),
	KOBJMETHOD(channel_getcaps,		hdaa_channel_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(hdaa_channel);

static int
hdaa_audio_ctl_ossmixer_init(struct snd_mixer *m)
{
	struct hdaa_pcm_devinfo *pdevinfo = mix_getdevinfo(m);
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	struct hdaa_widget *w, *cw;
	uint32_t mask, recmask;
	int i, j;

	hdaa_lock(devinfo);
	pdevinfo->mixer = m;

	/* Make sure that in case of soft volume it won't stay muted. */
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		pdevinfo->left[i] = 100;
		pdevinfo->right[i] = 100;
	}

	/* Declare volume controls assigned to this association. */
	mask = pdevinfo->ossmask;
	if (pdevinfo->playas >= 0) {
		/* Declate EAPD as ogain control. */
		for (i = devinfo->startnode; i < devinfo->endnode; i++) {
			w = hdaa_widget_get(devinfo, i);
			if (w == NULL || w->enable == 0)
				continue;
			if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX ||
			    w->param.eapdbtl == HDA_INVALID ||
			    w->bindas != pdevinfo->playas)
				continue;
			mask |= SOUND_MASK_OGAIN;
			break;
		}

		/* Declare soft PCM volume if needed. */
		if ((mask & SOUND_MASK_PCM) == 0 ||
		    (devinfo->quirks & HDAA_QUIRK_SOFTPCMVOL) ||
		    pdevinfo->minamp[SOUND_MIXER_PCM] ==
		     pdevinfo->maxamp[SOUND_MIXER_PCM]) {
			mask |= SOUND_MASK_PCM;
			pcm_setflags(pdevinfo->dev, pcm_getflags(pdevinfo->dev) | SD_F_SOFTPCMVOL);
			HDA_BOOTHVERBOSE(
				device_printf(pdevinfo->dev,
				    "Forcing Soft PCM volume\n");
			);
		}

		/* Declare master volume if needed. */
		if ((mask & SOUND_MASK_VOLUME) == 0) {
			mask |= SOUND_MASK_VOLUME;
			mix_setparentchild(m, SOUND_MIXER_VOLUME,
			    SOUND_MASK_PCM);
			mix_setrealdev(m, SOUND_MIXER_VOLUME,
			    SOUND_MIXER_NONE);
			HDA_BOOTHVERBOSE(
				device_printf(pdevinfo->dev,
				    "Forcing master volume with PCM\n");
			);
		}
	}

	/* Declare record sources available to this association. */
	recmask = 0;
	if (pdevinfo->recas >= 0) {
		for (i = 0; i < 16; i++) {
			if (devinfo->as[pdevinfo->recas].dacs[0][i] < 0)
				continue;
			w = hdaa_widget_get(devinfo,
			    devinfo->as[pdevinfo->recas].dacs[0][i]);
			if (w == NULL || w->enable == 0)
				continue;
			for (j = 0; j < w->nconns; j++) {
				if (w->connsenable[j] == 0)
					continue;
				cw = hdaa_widget_get(devinfo, w->conns[j]);
				if (cw == NULL || cw->enable == 0)
					continue;
				if (cw->bindas != pdevinfo->recas &&
				    cw->bindas != -2)
					continue;
				recmask |= cw->ossmask;
			}
		}
	}

	recmask &= (1 << SOUND_MIXER_NRDEVICES) - 1;
	mask &= (1 << SOUND_MIXER_NRDEVICES) - 1;
	pdevinfo->ossmask = mask;

	mix_setrecdevs(m, recmask);
	mix_setdevs(m, mask);

	hdaa_unlock(devinfo);

	return (0);
}

/*
 * Update amplification per pdevinfo per ossdev, calculate summary coefficient
 * and write it to codec, update *left and *right to reflect remaining error.
 */
static void
hdaa_audio_ctl_dev_set(struct hdaa_audio_ctl *ctl, int ossdev,
    int mute, int *left, int *right)
{
	int i, zleft, zright, sleft, sright, smute, lval, rval;

	ctl->devleft[ossdev] = *left;
	ctl->devright[ossdev] = *right;
	ctl->devmute[ossdev] = mute;
	smute = sleft = sright = zleft = zright = 0;
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		sleft += ctl->devleft[i];
		sright += ctl->devright[i];
		smute |= ctl->devmute[i];
		if (i == ossdev)
			continue;
		zleft += ctl->devleft[i];
		zright += ctl->devright[i];
	}
	lval = QDB2VAL(ctl, sleft);
	rval = QDB2VAL(ctl, sright);
	hdaa_audio_ctl_amp_set(ctl, smute, lval, rval);
	*left -= VAL2QDB(ctl, lval) - VAL2QDB(ctl, QDB2VAL(ctl, zleft));
	*right -= VAL2QDB(ctl, rval) - VAL2QDB(ctl, QDB2VAL(ctl, zright));
}

/*
 * Trace signal from source, setting volumes on the way.
 */
static void
hdaa_audio_ctl_source_volume(struct hdaa_pcm_devinfo *pdevinfo,
    int ossdev, nid_t nid, int index, int mute, int left, int right, int depth)
{
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	struct hdaa_widget *w, *wc;
	struct hdaa_audio_ctl *ctl;
	int i, j, conns = 0;

	if (depth > HDA_PARSE_MAXDEPTH)
		return;

	w = hdaa_widget_get(devinfo, nid);
	if (w == NULL || w->enable == 0)
		return;

	/* Count number of active inputs. */
	if (depth > 0) {
		for (j = 0; j < w->nconns; j++) {
			if (!w->connsenable[j])
				continue;
			conns++;
		}
	}

	/* If this is not a first step - use input mixer.
	   Pins have common input ctl so care must be taken. */
	if (depth > 0 && (conns == 1 ||
	    w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)) {
		ctl = hdaa_audio_ctl_amp_get(devinfo, w->nid, HDAA_CTL_IN,
		    index, 1);
		if (ctl)
			hdaa_audio_ctl_dev_set(ctl, ossdev, mute, &left, &right);
	}

	/* If widget has own ossdev - not traverse it.
	   It will be traversed on its own. */
	if (w->ossdev >= 0 && depth > 0)
		return;

	/* We must not traverse pin */
	if ((w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT ||
	    w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) &&
	    depth > 0)
		return;

	/*
	 * If signals mixed, we can't assign controls farther.
	 * Ignore this on depth zero. Caller must knows why.
	 */
	if (conns > 1 &&
	    (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER ||
	     w->selconn != index))
		return;

	ctl = hdaa_audio_ctl_amp_get(devinfo, w->nid, HDAA_CTL_OUT, -1, 1);
	if (ctl)
		hdaa_audio_ctl_dev_set(ctl, ossdev, mute, &left, &right);

	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		wc = hdaa_widget_get(devinfo, i);
		if (wc == NULL || wc->enable == 0)
			continue;
		for (j = 0; j < wc->nconns; j++) {
			if (wc->connsenable[j] && wc->conns[j] == nid) {
				hdaa_audio_ctl_source_volume(pdevinfo, ossdev,
				    wc->nid, j, mute, left, right, depth + 1);
			}
		}
	}
	return;
}

/*
 * Trace signal from destination, setting volumes on the way.
 */
static void
hdaa_audio_ctl_dest_volume(struct hdaa_pcm_devinfo *pdevinfo,
    int ossdev, nid_t nid, int index, int mute, int left, int right, int depth)
{
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	struct hdaa_audio_as *as = devinfo->as;
	struct hdaa_widget *w, *wc;
	struct hdaa_audio_ctl *ctl;
	int i, j, consumers, cleft, cright;

	if (depth > HDA_PARSE_MAXDEPTH)
		return;

	w = hdaa_widget_get(devinfo, nid);
	if (w == NULL || w->enable == 0)
		return;

	if (depth > 0) {
		/* If this node produce output for several consumers,
		   we can't touch it. */
		consumers = 0;
		for (i = devinfo->startnode; i < devinfo->endnode; i++) {
			wc = hdaa_widget_get(devinfo, i);
			if (wc == NULL || wc->enable == 0)
				continue;
			for (j = 0; j < wc->nconns; j++) {
				if (wc->connsenable[j] && wc->conns[j] == nid)
					consumers++;
			}
		}
		/* The only exception is if real HP redirection is configured
		   and this is a duplication point.
		   XXX: Actually exception is not completely correct.
		   XXX: Duplication point check is not perfect. */
		if ((consumers == 2 && (w->bindas < 0 ||
		    as[w->bindas].hpredir < 0 || as[w->bindas].fakeredir ||
		    (w->bindseqmask & (1 << 15)) == 0)) ||
		    consumers > 2)
			return;

		/* Else use it's output mixer. */
		ctl = hdaa_audio_ctl_amp_get(devinfo, w->nid,
		    HDAA_CTL_OUT, -1, 1);
		if (ctl)
			hdaa_audio_ctl_dev_set(ctl, ossdev, mute, &left, &right);
	}

	/* We must not traverse pin */
	if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX &&
	    depth > 0)
		return;

	for (i = 0; i < w->nconns; i++) {
		if (w->connsenable[i] == 0)
			continue;
		if (index >= 0 && i != index)
			continue;
		cleft = left;
		cright = right;
		ctl = hdaa_audio_ctl_amp_get(devinfo, w->nid,
		    HDAA_CTL_IN, i, 1);
		if (ctl)
			hdaa_audio_ctl_dev_set(ctl, ossdev, mute, &cleft, &cright);
		hdaa_audio_ctl_dest_volume(pdevinfo, ossdev, w->conns[i], -1,
		    mute, cleft, cright, depth + 1);
	}
}

/*
 * Set volumes for the specified pdevinfo and ossdev.
 */
static void
hdaa_audio_ctl_dev_volume(struct hdaa_pcm_devinfo *pdevinfo, unsigned dev)
{
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	struct hdaa_widget *w, *cw;
	uint32_t mute;
	int lvol, rvol;
	int i, j;

	mute = 0;
	if (pdevinfo->left[dev] == 0) {
		mute |= HDAA_AMP_MUTE_LEFT;
		lvol = -4000;
	} else
		lvol = ((pdevinfo->maxamp[dev] - pdevinfo->minamp[dev]) *
		    pdevinfo->left[dev] + 50) / 100 + pdevinfo->minamp[dev];
	if (pdevinfo->right[dev] == 0) {
		mute |= HDAA_AMP_MUTE_RIGHT;
		rvol = -4000;
	} else
		rvol = ((pdevinfo->maxamp[dev] - pdevinfo->minamp[dev]) *
		    pdevinfo->right[dev] + 50) / 100 + pdevinfo->minamp[dev];
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->bindas < 0) {
			if (pdevinfo->index != 0)
				continue;
		} else {
			if (w->bindas != pdevinfo->playas &&
			    w->bindas != pdevinfo->recas)
				continue;
		}
		if (dev == SOUND_MIXER_RECLEV &&
		    w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT) {
			hdaa_audio_ctl_dest_volume(pdevinfo, dev,
			    w->nid, -1, mute, lvol, rvol, 0);
			continue;
		}
		if (dev == SOUND_MIXER_VOLUME &&
		    w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX &&
		    devinfo->as[w->bindas].dir == HDAA_CTL_OUT) {
			hdaa_audio_ctl_dest_volume(pdevinfo, dev,
			    w->nid, -1, mute, lvol, rvol, 0);
			continue;
		}
		if (dev == SOUND_MIXER_IGAIN &&
		    w->pflags & HDAA_ADC_MONITOR) {
			for (j = 0; j < w->nconns; j++) {
				if (!w->connsenable[j])
				    continue;
				cw = hdaa_widget_get(devinfo, w->conns[j]);
				if (cw == NULL || cw->enable == 0)
				    continue;
				if (cw->bindas == -1)
				    continue;
				if (cw->bindas >= 0 &&
				    devinfo->as[cw->bindas].dir != HDAA_CTL_IN)
					continue;
				hdaa_audio_ctl_dest_volume(pdevinfo, dev,
				    w->nid, j, mute, lvol, rvol, 0);
			}
			continue;
		}
		if (w->ossdev != dev)
			continue;
		hdaa_audio_ctl_source_volume(pdevinfo, dev,
		    w->nid, -1, mute, lvol, rvol, 0);
		if (dev == SOUND_MIXER_IMIX && (w->pflags & HDAA_IMIX_AS_DST))
			hdaa_audio_ctl_dest_volume(pdevinfo, dev,
			    w->nid, -1, mute, lvol, rvol, 0);
	}
}

/*
 * OSS Mixer set method.
 */
static int
hdaa_audio_ctl_ossmixer_set(struct snd_mixer *m, unsigned dev,
					unsigned left, unsigned right)
{
	struct hdaa_pcm_devinfo *pdevinfo = mix_getdevinfo(m);
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	struct hdaa_widget *w;
	int i;

	hdaa_lock(devinfo);

	/* Save new values. */
	pdevinfo->left[dev] = left;
	pdevinfo->right[dev] = right;

	/* 'ogain' is the special case implemented with EAPD. */
	if (dev == SOUND_MIXER_OGAIN) {
		uint32_t orig;
		w = NULL;
		for (i = devinfo->startnode; i < devinfo->endnode; i++) {
			w = hdaa_widget_get(devinfo, i);
			if (w == NULL || w->enable == 0)
				continue;
			if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX ||
			    w->param.eapdbtl == HDA_INVALID)
				continue;
			break;
		}
		if (i >= devinfo->endnode) {
			hdaa_unlock(devinfo);
			return (-1);
		}
		orig = w->param.eapdbtl;
		if (left == 0)
			w->param.eapdbtl &= ~HDA_CMD_SET_EAPD_BTL_ENABLE_EAPD;
		else
			w->param.eapdbtl |= HDA_CMD_SET_EAPD_BTL_ENABLE_EAPD;
		if (orig != w->param.eapdbtl) {
			uint32_t val;

			val = w->param.eapdbtl;
			if (devinfo->quirks & HDAA_QUIRK_EAPDINV)
				val ^= HDA_CMD_SET_EAPD_BTL_ENABLE_EAPD;
			hda_command(devinfo->dev,
			    HDA_CMD_SET_EAPD_BTL_ENABLE(0, w->nid, val));
		}
		hdaa_unlock(devinfo);
		return (left | (left << 8));
	}

	/* Recalculate all controls related to this OSS device. */
	hdaa_audio_ctl_dev_volume(pdevinfo, dev);

	hdaa_unlock(devinfo);
	return (left | (right << 8));
}

/*
 * Set mixer settings to our own default values:
 * +20dB for mics, -10dB for analog vol, mute for igain, 0dB for others.
 */
static void
hdaa_audio_ctl_set_defaults(struct hdaa_pcm_devinfo *pdevinfo)
{
	int amp, vol, dev;

	for (dev = 0; dev < SOUND_MIXER_NRDEVICES; dev++) {
		if ((pdevinfo->ossmask & (1 << dev)) == 0)
			continue;

		/* If the value was overriden, leave it as is. */
		if (resource_int_value(device_get_name(pdevinfo->dev),
		    device_get_unit(pdevinfo->dev), ossnames[dev], &vol) == 0)
			continue;

		vol = -1;
		if (dev == SOUND_MIXER_OGAIN)
			vol = 100;
		else if (dev == SOUND_MIXER_IGAIN)
			vol = 0;
		else if (dev == SOUND_MIXER_MIC ||
		    dev == SOUND_MIXER_MONITOR)
			amp = 20 * 4;	/* +20dB */
		else if (dev == SOUND_MIXER_VOLUME && !pdevinfo->digital)
			amp = -10 * 4;	/* -10dB */
		else
			amp = 0;
		if (vol < 0 &&
		    (pdevinfo->maxamp[dev] - pdevinfo->minamp[dev]) <= 0) {
			vol = 100;
		} else if (vol < 0) {
			vol = ((amp - pdevinfo->minamp[dev]) * 100 +
			    (pdevinfo->maxamp[dev] - pdevinfo->minamp[dev]) / 2) /
			    (pdevinfo->maxamp[dev] - pdevinfo->minamp[dev]);
			vol = imin(imax(vol, 1), 100);
		}
		mix_set(pdevinfo->mixer, dev, vol, vol);
	}
}

/*
 * Recursively commutate specified record source.
 */
static uint32_t
hdaa_audio_ctl_recsel_comm(struct hdaa_pcm_devinfo *pdevinfo, uint32_t src, nid_t nid, int depth)
{
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	struct hdaa_widget *w, *cw;
	struct hdaa_audio_ctl *ctl;
	char buf[64];
	int i, muted;
	uint32_t res = 0;

	if (depth > HDA_PARSE_MAXDEPTH)
		return (0);

	w = hdaa_widget_get(devinfo, nid);
	if (w == NULL || w->enable == 0)
		return (0);

	for (i = 0; i < w->nconns; i++) {
		if (w->connsenable[i] == 0)
			continue;
		cw = hdaa_widget_get(devinfo, w->conns[i]);
		if (cw == NULL || cw->enable == 0 || cw->bindas == -1)
			continue;
		/* Call recursively to trace signal to it's source if needed. */
		if ((src & cw->ossmask) != 0) {
			if (cw->ossdev < 0) {
				res |= hdaa_audio_ctl_recsel_comm(pdevinfo, src,
				    w->conns[i], depth + 1);
			} else {
				res |= cw->ossmask;
			}
		}
		/* We have two special cases: mixers and others (selectors). */
		if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER) {
			ctl = hdaa_audio_ctl_amp_get(devinfo,
			    w->nid, HDAA_CTL_IN, i, 1);
			if (ctl == NULL) 
				continue;
			/* If we have input control on this node mute them
			 * according to requested sources. */
			muted = (src & cw->ossmask) ? 0 : 1;
			if (muted != ctl->forcemute) {
				ctl->forcemute = muted;
				hdaa_audio_ctl_amp_set(ctl,
				    HDAA_AMP_MUTE_DEFAULT,
				    HDAA_AMP_VOL_DEFAULT, HDAA_AMP_VOL_DEFAULT);
			}
			HDA_BOOTHVERBOSE(
				device_printf(pdevinfo->dev,
				    "Recsel (%s): nid %d source %d %s\n",
				    hdaa_audio_ctl_ossmixer_mask2allname(
				    src, buf, sizeof(buf)),
				    nid, i, muted?"mute":"unmute");
			);
		} else {
			if (w->nconns == 1)
				break;
			if ((src & cw->ossmask) == 0)
				continue;
			/* If we found requested source - select it and exit. */
			hdaa_widget_connection_select(w, i);
			HDA_BOOTHVERBOSE(
				device_printf(pdevinfo->dev,
				    "Recsel (%s): nid %d source %d select\n",
				    hdaa_audio_ctl_ossmixer_mask2allname(
				    src, buf, sizeof(buf)),
				    nid, i);
			);
			break;
		}
	}
	return (res);
}

static uint32_t
hdaa_audio_ctl_ossmixer_setrecsrc(struct snd_mixer *m, uint32_t src)
{
	struct hdaa_pcm_devinfo *pdevinfo = mix_getdevinfo(m);
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	struct hdaa_widget *w;
	struct hdaa_audio_as *as;
	struct hdaa_audio_ctl *ctl;
	struct hdaa_chan *ch;
	int i, j;
	uint32_t ret = 0xffffffff;

	hdaa_lock(devinfo);
	if (pdevinfo->recas < 0) {
		hdaa_unlock(devinfo);
		return (0);
	}
	as = &devinfo->as[pdevinfo->recas];

	/* For non-mixed associations we always recording everything. */
	if (!as->mixed) {
		hdaa_unlock(devinfo);
		return (mix_getrecdevs(m));
	}

	/* Commutate requested recsrc for each ADC. */
	for (j = 0; j < as->num_chans; j++) {
		ch = &devinfo->chans[as->chans[j]];
		for (i = 0; ch->io[i] >= 0; i++) {
			w = hdaa_widget_get(devinfo, ch->io[i]);
			if (w == NULL || w->enable == 0)
				continue;
			ret &= hdaa_audio_ctl_recsel_comm(pdevinfo, src,
			    ch->io[i], 0);
		}
	}
	if (ret == 0xffffffff)
		ret = 0;

	/*
	 * Some controls could be shared. Reset volumes for controls
	 * related to previously chosen devices, as they may no longer
	 * affect the signal.
	 */
	i = 0;
	while ((ctl = hdaa_audio_ctl_each(devinfo, &i)) != NULL) {
		if (ctl->enable == 0 ||
		    !(ctl->ossmask & pdevinfo->recsrc))
			continue;
		if (!((pdevinfo->playas >= 0 &&
		    ctl->widget->bindas == pdevinfo->playas) ||
		    (pdevinfo->recas >= 0 &&
		    ctl->widget->bindas == pdevinfo->recas) ||
		    (pdevinfo->index == 0 &&
		    ctl->widget->bindas == -2)))
			continue;
		for (j = 0; j < SOUND_MIXER_NRDEVICES; j++) {
			if (pdevinfo->recsrc & (1 << j)) {
				ctl->devleft[j] = 0;
				ctl->devright[j] = 0;
				ctl->devmute[j] = 0;
			}
		}
	}

	/*
	 * Some controls could be shared. Set volumes for controls
	 * related to devices selected both previously and now.
	 */
	for (j = 0; j < SOUND_MIXER_NRDEVICES; j++) {
		if ((ret | pdevinfo->recsrc) & (1 << j))
			hdaa_audio_ctl_dev_volume(pdevinfo, j);
	}

	pdevinfo->recsrc = ret;
	hdaa_unlock(devinfo);
	return (ret);
}

static kobj_method_t hdaa_audio_ctl_ossmixer_methods[] = {
	KOBJMETHOD(mixer_init,		hdaa_audio_ctl_ossmixer_init),
	KOBJMETHOD(mixer_set,		hdaa_audio_ctl_ossmixer_set),
	KOBJMETHOD(mixer_setrecsrc,	hdaa_audio_ctl_ossmixer_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(hdaa_audio_ctl_ossmixer);

static void
hdaa_dump_gpi(struct hdaa_devinfo *devinfo)
{
	device_t dev = devinfo->dev;
	int i;
	uint32_t data, wake, unsol, sticky;

	if (HDA_PARAM_GPIO_COUNT_NUM_GPI(devinfo->gpio_cap) > 0) {
		data = hda_command(dev,
		    HDA_CMD_GET_GPI_DATA(0, devinfo->nid));
		wake = hda_command(dev,
		    HDA_CMD_GET_GPI_WAKE_ENABLE_MASK(0, devinfo->nid));
		unsol = hda_command(dev,
		    HDA_CMD_GET_GPI_UNSOLICITED_ENABLE_MASK(0, devinfo->nid));
		sticky = hda_command(dev,
		    HDA_CMD_GET_GPI_STICKY_MASK(0, devinfo->nid));
		for (i = 0; i < HDA_PARAM_GPIO_COUNT_NUM_GPI(devinfo->gpio_cap); i++) {
			device_printf(dev, " GPI%d:%s%s%s state=%d", i,
				    (sticky & (1 << i)) ? " sticky" : "",
				    (unsol & (1 << i)) ? " unsol" : "",
				    (wake & (1 << i)) ? " wake" : "",
				    (data >> i) & 1);
		}
	}
}

static void
hdaa_dump_gpio(struct hdaa_devinfo *devinfo)
{
	device_t dev = devinfo->dev;
	int i;
	uint32_t data, dir, enable, wake, unsol, sticky;

	if (HDA_PARAM_GPIO_COUNT_NUM_GPIO(devinfo->gpio_cap) > 0) {
		data = hda_command(dev,
		    HDA_CMD_GET_GPIO_DATA(0, devinfo->nid));
		enable = hda_command(dev,
		    HDA_CMD_GET_GPIO_ENABLE_MASK(0, devinfo->nid));
		dir = hda_command(dev,
		    HDA_CMD_GET_GPIO_DIRECTION(0, devinfo->nid));
		wake = hda_command(dev,
		    HDA_CMD_GET_GPIO_WAKE_ENABLE_MASK(0, devinfo->nid));
		unsol = hda_command(dev,
		    HDA_CMD_GET_GPIO_UNSOLICITED_ENABLE_MASK(0, devinfo->nid));
		sticky = hda_command(dev,
		    HDA_CMD_GET_GPIO_STICKY_MASK(0, devinfo->nid));
		for (i = 0; i < HDA_PARAM_GPIO_COUNT_NUM_GPIO(devinfo->gpio_cap); i++) {
			device_printf(dev, " GPIO%d: ", i);
			if ((enable & (1 << i)) == 0) {
				printf("disabled\n");
				continue;
			}
			if ((dir & (1 << i)) == 0) {
				printf("input%s%s%s",
				    (sticky & (1 << i)) ? " sticky" : "",
				    (unsol & (1 << i)) ? " unsol" : "",
				    (wake & (1 << i)) ? " wake" : "");
			} else
				printf("output");
			printf(" state=%d\n", (data >> i) & 1);
		}
	}
}

static void
hdaa_dump_gpo(struct hdaa_devinfo *devinfo)
{
	device_t dev = devinfo->dev;
	int i;
	uint32_t data;

	if (HDA_PARAM_GPIO_COUNT_NUM_GPO(devinfo->gpio_cap) > 0) {
		data = hda_command(dev,
		    HDA_CMD_GET_GPO_DATA(0, devinfo->nid));
		for (i = 0; i < HDA_PARAM_GPIO_COUNT_NUM_GPO(devinfo->gpio_cap); i++) {
			device_printf(dev, " GPO%d: state=%d", i,
				    (data >> i) & 1);
		}
	}
}

static void
hdaa_audio_parse(struct hdaa_devinfo *devinfo)
{
	struct hdaa_widget *w;
	uint32_t res;
	int i;
	nid_t nid;

	nid = devinfo->nid;

	res = hda_command(devinfo->dev,
	    HDA_CMD_GET_PARAMETER(0, nid, HDA_PARAM_GPIO_COUNT));
	devinfo->gpio_cap = res;

	HDA_BOOTVERBOSE(
		device_printf(devinfo->dev,
		    "NumGPIO=%d NumGPO=%d "
		    "NumGPI=%d GPIWake=%d GPIUnsol=%d\n",
		    HDA_PARAM_GPIO_COUNT_NUM_GPIO(devinfo->gpio_cap),
		    HDA_PARAM_GPIO_COUNT_NUM_GPO(devinfo->gpio_cap),
		    HDA_PARAM_GPIO_COUNT_NUM_GPI(devinfo->gpio_cap),
		    HDA_PARAM_GPIO_COUNT_GPI_WAKE(devinfo->gpio_cap),
		    HDA_PARAM_GPIO_COUNT_GPI_UNSOL(devinfo->gpio_cap));
		hdaa_dump_gpi(devinfo);
		hdaa_dump_gpio(devinfo);
		hdaa_dump_gpo(devinfo);
	);

	res = hda_command(devinfo->dev,
	    HDA_CMD_GET_PARAMETER(0, nid, HDA_PARAM_SUPP_STREAM_FORMATS));
	devinfo->supp_stream_formats = res;

	res = hda_command(devinfo->dev,
	    HDA_CMD_GET_PARAMETER(0, nid, HDA_PARAM_SUPP_PCM_SIZE_RATE));
	devinfo->supp_pcm_size_rate = res;

	res = hda_command(devinfo->dev,
	    HDA_CMD_GET_PARAMETER(0, nid, HDA_PARAM_OUTPUT_AMP_CAP));
	devinfo->outamp_cap = res;

	res = hda_command(devinfo->dev,
	    HDA_CMD_GET_PARAMETER(0, nid, HDA_PARAM_INPUT_AMP_CAP));
	devinfo->inamp_cap = res;

	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL)
			device_printf(devinfo->dev, "Ghost widget! nid=%d!\n", i);
		else {
			w->devinfo = devinfo;
			w->nid = i;
			w->enable = 1;
			w->selconn = -1;
			w->pflags = 0;
			w->ossdev = -1;
			w->bindas = -1;
			w->param.eapdbtl = HDA_INVALID;
			hdaa_widget_parse(w);
		}
	}
}

static void
hdaa_audio_postprocess(struct hdaa_devinfo *devinfo)
{
	struct hdaa_widget *w;
	int i;

	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL)
			continue;
		hdaa_widget_postprocess(w);
	}
}

static void
hdaa_audio_ctl_parse(struct hdaa_devinfo *devinfo)
{
	struct hdaa_audio_ctl *ctls;
	struct hdaa_widget *w, *cw;
	int i, j, cnt, max, ocap, icap;
	int mute, offset, step, size;

	/* XXX This is redundant */
	max = 0;
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->param.outamp_cap != 0)
			max++;
		if (w->param.inamp_cap != 0) {
			switch (w->type) {
			case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR:
			case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER:
				for (j = 0; j < w->nconns; j++) {
					cw = hdaa_widget_get(devinfo,
					    w->conns[j]);
					if (cw == NULL || cw->enable == 0)
						continue;
					max++;
				}
				break;
			default:
				max++;
				break;
			}
		}
	}
	devinfo->ctlcnt = max;

	if (max < 1)
		return;

	ctls = (struct hdaa_audio_ctl *)malloc(
	    sizeof(*ctls) * max, M_HDAA, M_ZERO | M_NOWAIT);

	if (ctls == NULL) {
		/* Blekh! */
		device_printf(devinfo->dev, "unable to allocate ctls!\n");
		devinfo->ctlcnt = 0;
		return;
	}

	cnt = 0;
	for (i = devinfo->startnode; cnt < max && i < devinfo->endnode; i++) {
		if (cnt >= max) {
			device_printf(devinfo->dev, "%s: Ctl overflow!\n",
			    __func__);
			break;
		}
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		ocap = w->param.outamp_cap;
		icap = w->param.inamp_cap;
		if (ocap != 0) {
			mute = HDA_PARAM_OUTPUT_AMP_CAP_MUTE_CAP(ocap);
			step = HDA_PARAM_OUTPUT_AMP_CAP_NUMSTEPS(ocap);
			size = HDA_PARAM_OUTPUT_AMP_CAP_STEPSIZE(ocap);
			offset = HDA_PARAM_OUTPUT_AMP_CAP_OFFSET(ocap);
			/*if (offset > step) {
				HDA_BOOTVERBOSE(
					device_printf(devinfo->dev,
					    "BUGGY outamp: nid=%d "
					    "[offset=%d > step=%d]\n",
					    w->nid, offset, step);
				);
				offset = step;
			}*/
			ctls[cnt].enable = 1;
			ctls[cnt].widget = w;
			ctls[cnt].mute = mute;
			ctls[cnt].step = step;
			ctls[cnt].size = size;
			ctls[cnt].offset = offset;
			ctls[cnt].left = offset;
			ctls[cnt].right = offset;
			if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX ||
			    w->waspin)
				ctls[cnt].ndir = HDAA_CTL_IN;
			else 
				ctls[cnt].ndir = HDAA_CTL_OUT;
			ctls[cnt++].dir = HDAA_CTL_OUT;
		}

		if (icap != 0) {
			mute = HDA_PARAM_OUTPUT_AMP_CAP_MUTE_CAP(icap);
			step = HDA_PARAM_OUTPUT_AMP_CAP_NUMSTEPS(icap);
			size = HDA_PARAM_OUTPUT_AMP_CAP_STEPSIZE(icap);
			offset = HDA_PARAM_OUTPUT_AMP_CAP_OFFSET(icap);
			/*if (offset > step) {
				HDA_BOOTVERBOSE(
					device_printf(devinfo->dev,
					    "BUGGY inamp: nid=%d "
					    "[offset=%d > step=%d]\n",
					    w->nid, offset, step);
				);
				offset = step;
			}*/
			switch (w->type) {
			case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR:
			case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER:
				for (j = 0; j < w->nconns; j++) {
					if (cnt >= max) {
						device_printf(devinfo->dev,
						    "%s: Ctl overflow!\n",
						    __func__);
						break;
					}
					cw = hdaa_widget_get(devinfo,
					    w->conns[j]);
					if (cw == NULL || cw->enable == 0)
						continue;
					ctls[cnt].enable = 1;
					ctls[cnt].widget = w;
					ctls[cnt].childwidget = cw;
					ctls[cnt].index = j;
					ctls[cnt].mute = mute;
					ctls[cnt].step = step;
					ctls[cnt].size = size;
					ctls[cnt].offset = offset;
					ctls[cnt].left = offset;
					ctls[cnt].right = offset;
				ctls[cnt].ndir = HDAA_CTL_IN;
					ctls[cnt++].dir = HDAA_CTL_IN;
				}
				break;
			default:
				if (cnt >= max) {
					device_printf(devinfo->dev,
					    "%s: Ctl overflow!\n",
					    __func__);
					break;
				}
				ctls[cnt].enable = 1;
				ctls[cnt].widget = w;
				ctls[cnt].mute = mute;
				ctls[cnt].step = step;
				ctls[cnt].size = size;
				ctls[cnt].offset = offset;
				ctls[cnt].left = offset;
				ctls[cnt].right = offset;
				if (w->type ==
				    HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
					ctls[cnt].ndir = HDAA_CTL_OUT;
				else 
					ctls[cnt].ndir = HDAA_CTL_IN;
				ctls[cnt++].dir = HDAA_CTL_IN;
				break;
			}
		}
	}

	devinfo->ctl = ctls;
}

static void
hdaa_audio_as_parse(struct hdaa_devinfo *devinfo)
{
	struct hdaa_audio_as *as;
	struct hdaa_widget *w;
	int i, j, cnt, max, type, dir, assoc, seq, first, hpredir;

	/* Count present associations */
	max = 0;
	for (j = 1; j < 16; j++) {
		for (i = devinfo->startnode; i < devinfo->endnode; i++) {
			w = hdaa_widget_get(devinfo, i);
			if (w == NULL || w->enable == 0)
				continue;
			if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
				continue;
			if (HDA_CONFIG_DEFAULTCONF_ASSOCIATION(w->wclass.pin.config)
			    != j)
				continue;
			max++;
			if (j != 15)  /* There could be many 1-pin assocs #15 */
				break;
		}
	}

	devinfo->ascnt = max;

	if (max < 1)
		return;

	as = (struct hdaa_audio_as *)malloc(
	    sizeof(*as) * max, M_HDAA, M_ZERO | M_NOWAIT);

	if (as == NULL) {
		/* Blekh! */
		device_printf(devinfo->dev, "unable to allocate assocs!\n");
		devinfo->ascnt = 0;
		return;
	}

	for (i = 0; i < max; i++) {
		as[i].hpredir = -1;
		as[i].digital = 0;
		as[i].num_chans = 1;
		as[i].location = -1;
	}

	/* Scan associations skipping as=0. */
	cnt = 0;
	for (j = 1; j < 16 && cnt < max; j++) {
		first = 16;
		hpredir = 0;
		for (i = devinfo->startnode; i < devinfo->endnode; i++) {
			w = hdaa_widget_get(devinfo, i);
			if (w == NULL || w->enable == 0)
				continue;
			if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
				continue;
			assoc = HDA_CONFIG_DEFAULTCONF_ASSOCIATION(w->wclass.pin.config);
			seq = HDA_CONFIG_DEFAULTCONF_SEQUENCE(w->wclass.pin.config);
			if (assoc != j) {
				continue;
			}
			KASSERT(cnt < max,
			    ("%s: Associations owerflow (%d of %d)",
			    __func__, cnt, max));
			type = w->wclass.pin.config &
			    HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			/* Get pin direction. */
			if (type == HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_OUT ||
			    type == HDA_CONFIG_DEFAULTCONF_DEVICE_SPEAKER ||
			    type == HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT ||
			    type == HDA_CONFIG_DEFAULTCONF_DEVICE_SPDIF_OUT ||
			    type == HDA_CONFIG_DEFAULTCONF_DEVICE_DIGITAL_OTHER_OUT)
				dir = HDAA_CTL_OUT;
			else
				dir = HDAA_CTL_IN;
			/* If this is a first pin - create new association. */
			if (as[cnt].pincnt == 0) {
				as[cnt].enable = 1;
				as[cnt].index = j;
				as[cnt].dir = dir;
			}
			if (seq < first)
				first = seq;
			/* Check association correctness. */
			if (as[cnt].pins[seq] != 0) {
				device_printf(devinfo->dev, "%s: Duplicate pin %d (%d) "
				    "in association %d! Disabling association.\n",
				    __func__, seq, w->nid, j);
				as[cnt].enable = 0;
			}
			if (dir != as[cnt].dir) {
				device_printf(devinfo->dev, "%s: Pin %d has wrong "
				    "direction for association %d! Disabling "
				    "association.\n",
				    __func__, w->nid, j);
				as[cnt].enable = 0;
			}
			if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(w->param.widget_cap)) {
				as[cnt].digital |= 0x1;
				if (HDA_PARAM_PIN_CAP_HDMI(w->wclass.pin.cap))
					as[cnt].digital |= 0x2;
				if (HDA_PARAM_PIN_CAP_DP(w->wclass.pin.cap))
					as[cnt].digital |= 0x4;
			}
			if (as[cnt].location == -1) {
				as[cnt].location =
				    HDA_CONFIG_DEFAULTCONF_LOCATION(w->wclass.pin.config);
			} else if (as[cnt].location !=
			    HDA_CONFIG_DEFAULTCONF_LOCATION(w->wclass.pin.config)) {
				as[cnt].location = -2;
			}
			/* Headphones with seq=15 may mean redirection. */
			if (type == HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT &&
			    seq == 15)
				hpredir = 1;
			as[cnt].pins[seq] = w->nid;
			as[cnt].pincnt++;
			/* Association 15 is a multiple unassociated pins. */
			if (j == 15)
				cnt++;
		}
		if (j != 15 && as[cnt].pincnt > 0) {
			if (hpredir && as[cnt].pincnt > 1)
				as[cnt].hpredir = first;
			cnt++;
		}
	}
	for (i = 0; i < max; i++) {
		if (as[i].dir == HDAA_CTL_IN && (as[i].pincnt == 1 ||
		    as[i].pins[14] > 0 || as[i].pins[15] > 0))
			as[i].mixed = 1;
	}
	HDA_BOOTVERBOSE(
		device_printf(devinfo->dev,
		    "%d associations found:\n", max);
		for (i = 0; i < max; i++) {
			device_printf(devinfo->dev,
			    "Association %d (%d) %s%s:\n",
			    i, as[i].index, (as[i].dir == HDAA_CTL_IN)?"in":"out",
			    as[i].enable?"":" (disabled)");
			for (j = 0; j < 16; j++) {
				if (as[i].pins[j] == 0)
					continue;
				device_printf(devinfo->dev,
				    " Pin nid=%d seq=%d\n",
				    as[i].pins[j], j);
			}
		}
	);

	devinfo->as = as;
}

/*
 * Trace path from DAC to pin.
 */
static nid_t
hdaa_audio_trace_dac(struct hdaa_devinfo *devinfo, int as, int seq, nid_t nid,
    int dupseq, int min, int only, int depth)
{
	struct hdaa_widget *w;
	int i, im = -1;
	nid_t m = 0, ret;

	if (depth > HDA_PARSE_MAXDEPTH)
		return (0);
	w = hdaa_widget_get(devinfo, nid);
	if (w == NULL || w->enable == 0)
		return (0);
	HDA_BOOTHVERBOSE(
		if (!only) {
			device_printf(devinfo->dev,
			    " %*stracing via nid %d\n",
				depth + 1, "", w->nid);
		}
	);
	/* Use only unused widgets */
	if (w->bindas >= 0 && w->bindas != as) {
		HDA_BOOTHVERBOSE(
			if (!only) {
				device_printf(devinfo->dev,
				    " %*snid %d busy by association %d\n",
					depth + 1, "", w->nid, w->bindas);
			}
		);
		return (0);
	}
	if (dupseq < 0) {
		if (w->bindseqmask != 0) {
			HDA_BOOTHVERBOSE(
				if (!only) {
					device_printf(devinfo->dev,
					    " %*snid %d busy by seqmask %x\n",
						depth + 1, "", w->nid, w->bindseqmask);
				}
			);
			return (0);
		}
	} else {
		/* If this is headphones - allow duplicate first pin. */
		if (w->bindseqmask != 0 &&
		    (w->bindseqmask & (1 << dupseq)) == 0) {
			HDA_BOOTHVERBOSE(
				device_printf(devinfo->dev,
				    " %*snid %d busy by seqmask %x\n",
					depth + 1, "", w->nid, w->bindseqmask);
			);
			return (0);
		}
	}

	switch (w->type) {
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT:
		/* Do not traverse input. AD1988 has digital monitor
		for which we are not ready. */
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT:
		/* If we are tracing HP take only dac of first pin. */
		if ((only == 0 || only == w->nid) &&
		    (w->nid >= min) && (dupseq < 0 || w->nid ==
		    devinfo->as[as].dacs[0][dupseq]))
			m = w->nid;
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX:
		if (depth > 0)
			break;
		/* Fall */
	default:
		/* Find reachable DACs with smallest nid respecting constraints. */
		for (i = 0; i < w->nconns; i++) {
			if (w->connsenable[i] == 0)
				continue;
			if (w->selconn != -1 && w->selconn != i)
				continue;
			if ((ret = hdaa_audio_trace_dac(devinfo, as, seq,
			    w->conns[i], dupseq, min, only, depth + 1)) != 0) {
				if (m == 0 || ret < m) {
					m = ret;
					im = i;
				}
				if (only || dupseq >= 0)
					break;
			}
		}
		if (im >= 0 && only && ((w->nconns > 1 &&
		    w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER) ||
		    w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR))
			w->selconn = im;
		break;
	}
	if (m && only) {
		w->bindas = as;
		w->bindseqmask |= (1 << seq);
	}
	HDA_BOOTHVERBOSE(
		if (!only) {
			device_printf(devinfo->dev,
			    " %*snid %d returned %d\n",
				depth + 1, "", w->nid, m);
		}
	);
	return (m);
}

/*
 * Trace path from widget to ADC.
 */
static nid_t
hdaa_audio_trace_adc(struct hdaa_devinfo *devinfo, int as, int seq, nid_t nid,
    int mixed, int min, int only, int depth, int *length, int onlylength)
{
	struct hdaa_widget *w, *wc;
	int i, j, im, lm = HDA_PARSE_MAXDEPTH;
	nid_t m = 0, ret;

	if (depth > HDA_PARSE_MAXDEPTH)
		return (0);
	w = hdaa_widget_get(devinfo, nid);
	if (w == NULL || w->enable == 0)
		return (0);
	HDA_BOOTHVERBOSE(
		device_printf(devinfo->dev,
		    " %*stracing via nid %d\n",
			depth + 1, "", w->nid);
	);
	/* Use only unused widgets */
	if (w->bindas >= 0 && w->bindas != as) {
		HDA_BOOTHVERBOSE(
			device_printf(devinfo->dev,
			    " %*snid %d busy by association %d\n",
				depth + 1, "", w->nid, w->bindas);
		);
		return (0);
	}
	if (!mixed && w->bindseqmask != 0) {
		HDA_BOOTHVERBOSE(
			device_printf(devinfo->dev,
			    " %*snid %d busy by seqmask %x\n",
				depth + 1, "", w->nid, w->bindseqmask);
		);
		return (0);
	}
	switch (w->type) {
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT:
		if ((only == 0 || only == w->nid) && (w->nid >= min) &&
		    (onlylength == 0 || onlylength == depth)) {
			m = w->nid;
			*length = depth;
		}
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX:
		if (depth > 0)
			break;
		/* Fall */
	default:
		/* Try to find reachable ADCs with specified nid. */
		for (j = devinfo->startnode; j < devinfo->endnode; j++) {
			wc = hdaa_widget_get(devinfo, j);
			if (wc == NULL || wc->enable == 0)
				continue;
			im = -1;
			for (i = 0; i < wc->nconns; i++) {
				if (wc->connsenable[i] == 0)
					continue;
				if (wc->conns[i] != nid)
					continue;
				if ((ret = hdaa_audio_trace_adc(devinfo, as, seq,
				    j, mixed, min, only, depth + 1,
				    length, onlylength)) != 0) {
					if (m == 0 || ret < m ||
					    (ret == m && *length < lm)) {
						m = ret;
						im = i;
						lm = *length;
					} else
						*length = lm;
					if (only)
						break;
				}
			}
			if (im >= 0 && only && ((wc->nconns > 1 &&
			    wc->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER) ||
			    wc->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR))
				wc->selconn = im;
		}
		break;
	}
	if (m && only) {
		w->bindas = as;
		w->bindseqmask |= (1 << seq);
	}
	HDA_BOOTHVERBOSE(
		device_printf(devinfo->dev,
		    " %*snid %d returned %d\n",
			depth + 1, "", w->nid, m);
	);
	return (m);
}

/*
 * Erase trace path of the specified association.
 */
static void
hdaa_audio_undo_trace(struct hdaa_devinfo *devinfo, int as, int seq)
{
	struct hdaa_widget *w;
	int i;

	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->bindas == as) {
			if (seq >= 0) {
				w->bindseqmask &= ~(1 << seq);
				if (w->bindseqmask == 0) {
					w->bindas = -1;
					w->selconn = -1;
				}
			} else {
				w->bindas = -1;
				w->bindseqmask = 0;
				w->selconn = -1;
			}
		}
	}
}

/*
 * Trace association path from DAC to output
 */
static int
hdaa_audio_trace_as_out(struct hdaa_devinfo *devinfo, int as, int seq)
{
	struct hdaa_audio_as *ases = devinfo->as;
	int i, hpredir;
	nid_t min, res;

	/* Find next pin */
	for (i = seq; i < 16 && ases[as].pins[i] == 0; i++)
		;
	/* Check if there is no any left. If so - we succeeded. */
	if (i == 16)
		return (1);

	hpredir = (i == 15 && ases[as].fakeredir == 0)?ases[as].hpredir:-1;
	min = 0;
	do {
		HDA_BOOTHVERBOSE(
			device_printf(devinfo->dev,
			    " Tracing pin %d with min nid %d",
			    ases[as].pins[i], min);
			if (hpredir >= 0)
				printf(" and hpredir %d", hpredir);
			printf("\n");
		);
		/* Trace this pin taking min nid into account. */
		res = hdaa_audio_trace_dac(devinfo, as, i,
		    ases[as].pins[i], hpredir, min, 0, 0);
		if (res == 0) {
			/* If we failed - return to previous and redo it. */
			HDA_BOOTVERBOSE(
				device_printf(devinfo->dev,
				    " Unable to trace pin %d seq %d with min "
				    "nid %d",
				    ases[as].pins[i], i, min);
				if (hpredir >= 0)
					printf(" and hpredir %d", hpredir);
				printf("\n");
			);
			return (0);
		}
		HDA_BOOTVERBOSE(
			device_printf(devinfo->dev,
			    " Pin %d traced to DAC %d",
			    ases[as].pins[i], res);
			if (hpredir >= 0)
				printf(" and hpredir %d", hpredir);
			if (ases[as].fakeredir)
				printf(" with fake redirection");
			printf("\n");
		);
		/* Trace again to mark the path */
		hdaa_audio_trace_dac(devinfo, as, i,
		    ases[as].pins[i], hpredir, min, res, 0);
		ases[as].dacs[0][i] = res;
		/* We succeeded, so call next. */
		if (hdaa_audio_trace_as_out(devinfo, as, i + 1))
			return (1);
		/* If next failed, we should retry with next min */
		hdaa_audio_undo_trace(devinfo, as, i);
		ases[as].dacs[0][i] = 0;
		min = res + 1;
	} while (1);
}

/*
 * Check equivalency of two DACs.
 */
static int
hdaa_audio_dacs_equal(struct hdaa_widget *w1, struct hdaa_widget *w2)
{
	struct hdaa_devinfo *devinfo = w1->devinfo;
	struct hdaa_widget *w3;
	int i, j, c1, c2;

	if (memcmp(&w1->param, &w2->param, sizeof(w1->param)))
		return (0);
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w3 = hdaa_widget_get(devinfo, i);
		if (w3 == NULL || w3->enable == 0)
			continue;
		if (w3->bindas != w1->bindas)
			continue;
		if (w3->nconns == 0)
			continue;
		c1 = c2 = -1;
		for (j = 0; j < w3->nconns; j++) {
			if (w3->connsenable[j] == 0)
				continue;
			if (w3->conns[j] == w1->nid)
				c1 = j;
			if (w3->conns[j] == w2->nid)
				c2 = j;
		}
		if (c1 < 0)
			continue;
		if (c2 < 0)
			return (0);
		if (w3->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER)
			return (0);
	}
	return (1);
}

/*
 * Check equivalency of two ADCs.
 */
static int
hdaa_audio_adcs_equal(struct hdaa_widget *w1, struct hdaa_widget *w2)
{
	struct hdaa_devinfo *devinfo = w1->devinfo;
	struct hdaa_widget *w3, *w4;
	int i;

	if (memcmp(&w1->param, &w2->param, sizeof(w1->param)))
		return (0);
	if (w1->nconns != 1 || w2->nconns != 1)
		return (0);
	if (w1->conns[0] == w2->conns[0])
		return (1);
	w3 = hdaa_widget_get(devinfo, w1->conns[0]);
	if (w3 == NULL || w3->enable == 0)
		return (0);
	w4 = hdaa_widget_get(devinfo, w2->conns[0]);
	if (w4 == NULL || w4->enable == 0)
		return (0);
	if (w3->bindas == w4->bindas && w3->bindseqmask == w4->bindseqmask)
		return (1);
	if (w4->bindas >= 0)
		return (0);
	if (w3->type != w4->type)
		return (0);
	if (memcmp(&w3->param, &w4->param, sizeof(w3->param)))
		return (0);
	if (w3->nconns != w4->nconns)
		return (0);
	for (i = 0; i < w3->nconns; i++) {
		if (w3->conns[i] != w4->conns[i])
			return (0);
	}
	return (1);
}

/*
 * Look for equivalent DAC/ADC to implement second channel.
 */
static void
hdaa_audio_adddac(struct hdaa_devinfo *devinfo, int asid)
{
	struct hdaa_audio_as *as = &devinfo->as[asid];
	struct hdaa_widget *w1, *w2;
	int i, pos;
	nid_t nid1, nid2;

	HDA_BOOTVERBOSE(
		device_printf(devinfo->dev,
		    "Looking for additional %sC "
		    "for association %d (%d)\n",
		    (as->dir == HDAA_CTL_OUT) ? "DA" : "AD",
		    asid, as->index);
	);

	/* Find the exisitng DAC position and return if found more the one. */
	pos = -1;
	for (i = 0; i < 16; i++) {
		if (as->dacs[0][i] <= 0)
			continue;
		if (pos >= 0 && as->dacs[0][i] != as->dacs[0][pos])
			return;
		pos = i;
	}

	nid1 = as->dacs[0][pos];
	w1 = hdaa_widget_get(devinfo, nid1);
	w2 = NULL;
	for (nid2 = devinfo->startnode; nid2 < devinfo->endnode; nid2++) {
		w2 = hdaa_widget_get(devinfo, nid2);
		if (w2 == NULL || w2->enable == 0)
			continue;
		if (w2->bindas >= 0)
			continue;
		if (w1->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT) {
			if (w2->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT)
				continue;
			if (hdaa_audio_dacs_equal(w1, w2))
				break;
		} else {
			if (w2->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT)
				continue;
			if (hdaa_audio_adcs_equal(w1, w2))
				break;
		}
	}
	if (nid2 >= devinfo->endnode)
		return;
	w2->bindas = w1->bindas;
	w2->bindseqmask = w1->bindseqmask;
	if (w1->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT) {
		HDA_BOOTVERBOSE(
			device_printf(devinfo->dev,
			    " ADC %d considered equal to ADC %d\n", nid2, nid1);
		);
		w1 = hdaa_widget_get(devinfo, w1->conns[0]);
		w2 = hdaa_widget_get(devinfo, w2->conns[0]);
		w2->bindas = w1->bindas;
		w2->bindseqmask = w1->bindseqmask;
	} else {
		HDA_BOOTVERBOSE(
			device_printf(devinfo->dev,
			    " DAC %d considered equal to DAC %d\n", nid2, nid1);
		);
	}
	for (i = 0; i < 16; i++) {
		if (as->dacs[0][i] <= 0)
			continue;
		as->dacs[as->num_chans][i] = nid2;
	}
	as->num_chans++;
}

/*
 * Trace association path from input to ADC
 */
static int
hdaa_audio_trace_as_in(struct hdaa_devinfo *devinfo, int as)
{
	struct hdaa_audio_as *ases = devinfo->as;
	struct hdaa_widget *w;
	int i, j, k, length;

	for (j = devinfo->startnode; j < devinfo->endnode; j++) {
		w = hdaa_widget_get(devinfo, j);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT)
			continue;
		if (w->bindas >= 0 && w->bindas != as)
			continue;

		/* Find next pin */
		for (i = 0; i < 16; i++) {
			if (ases[as].pins[i] == 0)
				continue;

			HDA_BOOTHVERBOSE(
				device_printf(devinfo->dev,
				    " Tracing pin %d to ADC %d\n",
				    ases[as].pins[i], j);
			);
			/* Trace this pin taking goal into account. */
			if (hdaa_audio_trace_adc(devinfo, as, i,
			    ases[as].pins[i], 1, 0, j, 0, &length, 0) == 0) {
				/* If we failed - return to previous and redo it. */
				HDA_BOOTVERBOSE(
					device_printf(devinfo->dev,
					    " Unable to trace pin %d to ADC %d, undo traces\n",
					    ases[as].pins[i], j);
				);
				hdaa_audio_undo_trace(devinfo, as, -1);
				for (k = 0; k < 16; k++)
					ases[as].dacs[0][k] = 0;
				break;
			}
			HDA_BOOTVERBOSE(
				device_printf(devinfo->dev,
				    " Pin %d traced to ADC %d\n",
				    ases[as].pins[i], j);
			);
			ases[as].dacs[0][i] = j;
		}
		if (i == 16)
			return (1);
	}
	return (0);
}

/*
 * Trace association path from input to multiple ADCs
 */
static int
hdaa_audio_trace_as_in_mch(struct hdaa_devinfo *devinfo, int as, int seq)
{
	struct hdaa_audio_as *ases = devinfo->as;
	int i, length;
	nid_t min, res;

	/* Find next pin */
	for (i = seq; i < 16 && ases[as].pins[i] == 0; i++)
		;
	/* Check if there is no any left. If so - we succeeded. */
	if (i == 16)
		return (1);

	min = 0;
	do {
		HDA_BOOTHVERBOSE(
			device_printf(devinfo->dev,
			    " Tracing pin %d with min nid %d",
			    ases[as].pins[i], min);
			printf("\n");
		);
		/* Trace this pin taking min nid into account. */
		res = hdaa_audio_trace_adc(devinfo, as, i,
		    ases[as].pins[i], 0, min, 0, 0, &length, 0);
		if (res == 0) {
			/* If we failed - return to previous and redo it. */
			HDA_BOOTVERBOSE(
				device_printf(devinfo->dev,
				    " Unable to trace pin %d seq %d with min "
				    "nid %d",
				    ases[as].pins[i], i, min);
				printf("\n");
			);
			return (0);
		}
		HDA_BOOTVERBOSE(
			device_printf(devinfo->dev,
			    " Pin %d traced to ADC %d\n",
			    ases[as].pins[i], res);
		);
		/* Trace again to mark the path */
		hdaa_audio_trace_adc(devinfo, as, i,
		    ases[as].pins[i], 0, min, res, 0, &length, length);
		ases[as].dacs[0][i] = res;
		/* We succeeded, so call next. */
		if (hdaa_audio_trace_as_in_mch(devinfo, as, i + 1))
			return (1);
		/* If next failed, we should retry with next min */
		hdaa_audio_undo_trace(devinfo, as, i);
		ases[as].dacs[0][i] = 0;
		min = res + 1;
	} while (1);
}

/*
 * Trace input monitor path from mixer to output association.
 */
static int
hdaa_audio_trace_to_out(struct hdaa_devinfo *devinfo, nid_t nid, int depth)
{
	struct hdaa_audio_as *ases = devinfo->as;
	struct hdaa_widget *w, *wc;
	int i, j;
	nid_t res = 0;

	if (depth > HDA_PARSE_MAXDEPTH)
		return (0);
	w = hdaa_widget_get(devinfo, nid);
	if (w == NULL || w->enable == 0)
		return (0);
	HDA_BOOTHVERBOSE(
		device_printf(devinfo->dev,
		    " %*stracing via nid %d\n",
			depth + 1, "", w->nid);
	);
	/* Use only unused widgets */
	if (depth > 0 && w->bindas != -1) {
		if (w->bindas < 0 || ases[w->bindas].dir == HDAA_CTL_OUT) {
			HDA_BOOTHVERBOSE(
				device_printf(devinfo->dev,
				    " %*snid %d found output association %d\n",
					depth + 1, "", w->nid, w->bindas);
			);
			if (w->bindas >= 0)
				w->pflags |= HDAA_ADC_MONITOR;
			return (1);
		} else {
			HDA_BOOTHVERBOSE(
				device_printf(devinfo->dev,
				    " %*snid %d busy by input association %d\n",
					depth + 1, "", w->nid, w->bindas);
			);
			return (0);
		}
	}

	switch (w->type) {
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT:
		/* Do not traverse input. AD1988 has digital monitor
		for which we are not ready. */
		break;
	case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX:
		if (depth > 0)
			break;
		/* Fall */
	default:
		/* Try to find reachable ADCs with specified nid. */
		for (j = devinfo->startnode; j < devinfo->endnode; j++) {
			wc = hdaa_widget_get(devinfo, j);
			if (wc == NULL || wc->enable == 0)
				continue;
			for (i = 0; i < wc->nconns; i++) {
				if (wc->connsenable[i] == 0)
					continue;
				if (wc->conns[i] != nid)
					continue;
				if (hdaa_audio_trace_to_out(devinfo,
				    j, depth + 1) != 0) {
					res = 1;
					if (wc->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR &&
					    wc->selconn == -1)
						wc->selconn = i;
				}
			}
		}
		break;
	}
	if (res && w->bindas == -1)
		w->bindas = -2;

	HDA_BOOTHVERBOSE(
		device_printf(devinfo->dev,
		    " %*snid %d returned %d\n",
			depth + 1, "", w->nid, res);
	);
	return (res);
}

/*
 * Trace extra associations (beeper, monitor)
 */
static void
hdaa_audio_trace_as_extra(struct hdaa_devinfo *devinfo)
{
	struct hdaa_audio_as *as = devinfo->as;
	struct hdaa_widget *w;
	int j;

	/* Input monitor */
	/* Find mixer associated with input, but supplying signal
	   for output associations. Hope it will be input monitor. */
	HDA_BOOTVERBOSE(
		device_printf(devinfo->dev,
		    "Tracing input monitor\n");
	);
	for (j = devinfo->startnode; j < devinfo->endnode; j++) {
		w = hdaa_widget_get(devinfo, j);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER)
			continue;
		if (w->bindas < 0 || as[w->bindas].dir != HDAA_CTL_IN)
			continue;
		HDA_BOOTVERBOSE(
			device_printf(devinfo->dev,
			    " Tracing nid %d to out\n",
			    j);
		);
		if (hdaa_audio_trace_to_out(devinfo, w->nid, 0)) {
			HDA_BOOTVERBOSE(
				device_printf(devinfo->dev,
				    " nid %d is input monitor\n",
					w->nid);
			);
			w->ossdev = SOUND_MIXER_IMIX;
		}
	}

	/* Other inputs monitor */
	/* Find input pins supplying signal for output associations.
	   Hope it will be input monitoring. */
	HDA_BOOTVERBOSE(
		device_printf(devinfo->dev,
		    "Tracing other input monitors\n");
	);
	for (j = devinfo->startnode; j < devinfo->endnode; j++) {
		w = hdaa_widget_get(devinfo, j);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		if (w->bindas < 0 || as[w->bindas].dir != HDAA_CTL_IN)
			continue;
		HDA_BOOTVERBOSE(
			device_printf(devinfo->dev,
			    " Tracing nid %d to out\n",
			    j);
		);
		if (hdaa_audio_trace_to_out(devinfo, w->nid, 0)) {
			HDA_BOOTVERBOSE(
				device_printf(devinfo->dev,
				    " nid %d is input monitor\n",
					w->nid);
			);
		}
	}

	/* Beeper */
	HDA_BOOTVERBOSE(
		device_printf(devinfo->dev,
		    "Tracing beeper\n");
	);
	for (j = devinfo->startnode; j < devinfo->endnode; j++) {
		w = hdaa_widget_get(devinfo, j);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_BEEP_WIDGET)
			continue;
		HDA_BOOTHVERBOSE(
			device_printf(devinfo->dev,
			    " Tracing nid %d to out\n",
			    j);
		);
		if (hdaa_audio_trace_to_out(devinfo, w->nid, 0)) {
			HDA_BOOTVERBOSE(
				device_printf(devinfo->dev,
				    " nid %d traced to out\n",
				    j);
			);
		}
		w->bindas = -2;
	}
}

/*
 * Bind assotiations to PCM channels
 */
static void
hdaa_audio_bind_as(struct hdaa_devinfo *devinfo)
{
	struct hdaa_audio_as *as = devinfo->as;
	int i, j, cnt = 0, free;

	for (j = 0; j < devinfo->ascnt; j++) {
		if (as[j].enable)
			cnt += as[j].num_chans;
	}
	if (devinfo->num_chans == 0) {
		devinfo->chans = (struct hdaa_chan *)malloc(
		    sizeof(struct hdaa_chan) * cnt,
		    M_HDAA, M_ZERO | M_NOWAIT);
		if (devinfo->chans == NULL) {
			device_printf(devinfo->dev,
			    "Channels memory allocation failed!\n");
			return;
		}
	} else {
		devinfo->chans = (struct hdaa_chan *)realloc(devinfo->chans,
		    sizeof(struct hdaa_chan) * (devinfo->num_chans + cnt),
		    M_HDAA, M_ZERO | M_NOWAIT);
		if (devinfo->chans == NULL) {
			devinfo->num_chans = 0;
			device_printf(devinfo->dev,
			    "Channels memory allocation failed!\n");
			return;
		}
		/* Fixup relative pointers after realloc */
		for (j = 0; j < devinfo->num_chans; j++)
			devinfo->chans[j].caps.fmtlist = devinfo->chans[j].fmtlist;
	}
	free = devinfo->num_chans;
	devinfo->num_chans += cnt;

	for (j = free; j < free + cnt; j++) {
		devinfo->chans[j].devinfo = devinfo;
		devinfo->chans[j].as = -1;
	}

	/* Assign associations in order of their numbers, */
	for (j = 0; j < devinfo->ascnt; j++) {
		if (as[j].enable == 0)
			continue;
		for (i = 0; i < as[j].num_chans; i++) {
			devinfo->chans[free].as = j;
			devinfo->chans[free].asindex = i;
			devinfo->chans[free].dir =
			    (as[j].dir == HDAA_CTL_IN) ? PCMDIR_REC : PCMDIR_PLAY;
			hdaa_pcmchannel_setup(&devinfo->chans[free]);
			as[j].chans[i] = free;
			free++;
		}
	}
}

static void
hdaa_audio_disable_nonaudio(struct hdaa_devinfo *devinfo)
{
	struct hdaa_widget *w;
	int i;

	/* Disable power and volume widgets. */
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_POWER_WIDGET ||
		    w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_VOLUME_WIDGET) {
			w->enable = 0;
			HDA_BOOTHVERBOSE(
				device_printf(devinfo->dev, 
				    " Disabling nid %d due to it's"
				    " non-audio type.\n",
				    w->nid);
			);
		}
	}
}

static void
hdaa_audio_disable_useless(struct hdaa_devinfo *devinfo)
{
	struct hdaa_widget *w, *cw;
	struct hdaa_audio_ctl *ctl;
	int done, found, i, j, k;

	/* Disable useless pins. */
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) {
			if ((w->wclass.pin.config &
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK) ==
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_NONE) {
				w->enable = 0;
				HDA_BOOTHVERBOSE(
					device_printf(devinfo->dev, 
					    " Disabling pin nid %d due"
					    " to None connectivity.\n",
					    w->nid);
				);
			} else if ((w->wclass.pin.config &
			    HDA_CONFIG_DEFAULTCONF_ASSOCIATION_MASK) == 0) {
				w->enable = 0;
				HDA_BOOTHVERBOSE(
					device_printf(devinfo->dev, 
					    " Disabling unassociated"
					    " pin nid %d.\n",
					    w->nid);
				);
			}
		}
	}
	do {
		done = 1;
		/* Disable and mute controls for disabled widgets. */
		i = 0;
		while ((ctl = hdaa_audio_ctl_each(devinfo, &i)) != NULL) {
			if (ctl->enable == 0)
				continue;
			if (ctl->widget->enable == 0 ||
			    (ctl->childwidget != NULL &&
			    ctl->childwidget->enable == 0)) {
				ctl->forcemute = 1;
				ctl->muted = HDAA_AMP_MUTE_ALL;
				ctl->left = 0;
				ctl->right = 0;
				ctl->enable = 0;
				if (ctl->ndir == HDAA_CTL_IN)
					ctl->widget->connsenable[ctl->index] = 0;
				done = 0;
				HDA_BOOTHVERBOSE(
					device_printf(devinfo->dev, 
					    " Disabling ctl %d nid %d cnid %d due"
					    " to disabled widget.\n", i,
					    ctl->widget->nid,
					    (ctl->childwidget != NULL)?
					    ctl->childwidget->nid:-1);
				);
			}
		}
		/* Disable useless widgets. */
		for (i = devinfo->startnode; i < devinfo->endnode; i++) {
			w = hdaa_widget_get(devinfo, i);
			if (w == NULL || w->enable == 0)
				continue;
			/* Disable inputs with disabled child widgets. */
			for (j = 0; j < w->nconns; j++) {
				if (w->connsenable[j]) {
					cw = hdaa_widget_get(devinfo, w->conns[j]);
					if (cw == NULL || cw->enable == 0) {
						w->connsenable[j] = 0;
						HDA_BOOTHVERBOSE(
							device_printf(devinfo->dev, 
							    " Disabling nid %d connection %d due"
							    " to disabled child widget.\n",
							    i, j);
						);
					}
				}
			}
			if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR &&
			    w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER)
				continue;
			/* Disable mixers and selectors without inputs. */
			found = 0;
			for (j = 0; j < w->nconns; j++) {
				if (w->connsenable[j]) {
					found = 1;
					break;
				}
			}
			if (found == 0) {
				w->enable = 0;
				done = 0;
				HDA_BOOTHVERBOSE(
					device_printf(devinfo->dev, 
					    " Disabling nid %d due to all it's"
					    " inputs disabled.\n", w->nid);
				);
			}
			/* Disable nodes without consumers. */
			if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_SELECTOR &&
			    w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER)
				continue;
			found = 0;
			for (k = devinfo->startnode; k < devinfo->endnode; k++) {
				cw = hdaa_widget_get(devinfo, k);
				if (cw == NULL || cw->enable == 0)
					continue;
				for (j = 0; j < cw->nconns; j++) {
					if (cw->connsenable[j] && cw->conns[j] == i) {
						found = 1;
						break;
					}
				}
			}
			if (found == 0) {
				w->enable = 0;
				done = 0;
				HDA_BOOTHVERBOSE(
					device_printf(devinfo->dev, 
					    " Disabling nid %d due to all it's"
					    " consumers disabled.\n", w->nid);
				);
			}
		}
	} while (done == 0);

}

static void
hdaa_audio_disable_unas(struct hdaa_devinfo *devinfo)
{
	struct hdaa_audio_as *as = devinfo->as;
	struct hdaa_widget *w, *cw;
	struct hdaa_audio_ctl *ctl;
	int i, j, k;

	/* Disable unassosiated widgets. */
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->bindas == -1) {
			w->enable = 0;
			HDA_BOOTHVERBOSE(
				device_printf(devinfo->dev, 
				    " Disabling unassociated nid %d.\n",
				    w->nid);
			);
		}
	}
	/* Disable input connections on input pin and
	 * output on output. */
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		if (w->bindas < 0)
			continue;
		if (as[w->bindas].dir == HDAA_CTL_IN) {
			for (j = 0; j < w->nconns; j++) {
				if (w->connsenable[j] == 0)
					continue;
				w->connsenable[j] = 0;
				HDA_BOOTHVERBOSE(
					device_printf(devinfo->dev, 
					    " Disabling connection to input pin "
					    "nid %d conn %d.\n",
					    i, j);
				);
			}
			ctl = hdaa_audio_ctl_amp_get(devinfo, w->nid,
			    HDAA_CTL_IN, -1, 1);
			if (ctl && ctl->enable) {
				ctl->forcemute = 1;
				ctl->muted = HDAA_AMP_MUTE_ALL;
				ctl->left = 0;
				ctl->right = 0;
				ctl->enable = 0;
			}
		} else {
			ctl = hdaa_audio_ctl_amp_get(devinfo, w->nid,
			    HDAA_CTL_OUT, -1, 1);
			if (ctl && ctl->enable) {
				ctl->forcemute = 1;
				ctl->muted = HDAA_AMP_MUTE_ALL;
				ctl->left = 0;
				ctl->right = 0;
				ctl->enable = 0;
			}
			for (k = devinfo->startnode; k < devinfo->endnode; k++) {
				cw = hdaa_widget_get(devinfo, k);
				if (cw == NULL || cw->enable == 0)
					continue;
				for (j = 0; j < cw->nconns; j++) {
					if (cw->connsenable[j] && cw->conns[j] == i) {
						cw->connsenable[j] = 0;
						HDA_BOOTHVERBOSE(
							device_printf(devinfo->dev, 
							    " Disabling connection from output pin "
							    "nid %d conn %d cnid %d.\n",
							    k, j, i);
						);
						if (cw->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX &&
						    cw->nconns > 1)
							continue;
						ctl = hdaa_audio_ctl_amp_get(devinfo, k,
					    HDAA_CTL_IN, j, 1);
						if (ctl && ctl->enable) {
							ctl->forcemute = 1;
							ctl->muted = HDAA_AMP_MUTE_ALL;
							ctl->left = 0;
							ctl->right = 0;
							ctl->enable = 0;
						}
					}
				}
			}
		}
	}
}

static void
hdaa_audio_disable_notselected(struct hdaa_devinfo *devinfo)
{
	struct hdaa_audio_as *as = devinfo->as;
	struct hdaa_widget *w;
	int i, j;

	/* On playback path we can safely disable all unseleted inputs. */
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->nconns <= 1)
			continue;
		if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER)
			continue;
		if (w->bindas < 0 || as[w->bindas].dir == HDAA_CTL_IN)
			continue;
		for (j = 0; j < w->nconns; j++) {
			if (w->connsenable[j] == 0)
				continue;
			if (w->selconn < 0 || w->selconn == j)
				continue;
			w->connsenable[j] = 0;
			HDA_BOOTHVERBOSE(
				device_printf(devinfo->dev, 
				    " Disabling unselected connection "
				    "nid %d conn %d.\n",
				    i, j);
			);
		}
	}
}

static void
hdaa_audio_disable_crossas(struct hdaa_devinfo *devinfo)
{
	struct hdaa_audio_as *ases = devinfo->as;
	struct hdaa_widget *w, *cw;
	struct hdaa_audio_ctl *ctl;
	int i, j;

	/* Disable crossassociatement and unwanted crosschannel connections. */
	/* ... using selectors */
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->nconns <= 1)
			continue;
		if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER)
			continue;
		/* Allow any -> mix */
		if (w->bindas == -2)
			continue;
		for (j = 0; j < w->nconns; j++) {
			if (w->connsenable[j] == 0)
				continue;
			cw = hdaa_widget_get(devinfo, w->conns[j]);
			if (cw == NULL || w->enable == 0)
				continue;
			/* Allow mix -> out. */
			if (cw->bindas == -2 && w->bindas >= 0 &&
			    ases[w->bindas].dir == HDAA_CTL_OUT)
				continue;
			/* Allow mix -> mixed-in. */
			if (cw->bindas == -2 && w->bindas >= 0 &&
			    ases[w->bindas].mixed)
				continue;
			/* Allow in -> mix. */
			if ((w->pflags & HDAA_ADC_MONITOR) &&
			     cw->bindas >= 0 &&
			     ases[cw->bindas].dir == HDAA_CTL_IN)
				continue;
			/* Allow if have common as/seqs. */
			if (w->bindas == cw->bindas &&
			    (w->bindseqmask & cw->bindseqmask) != 0)
				continue;
			w->connsenable[j] = 0;
			HDA_BOOTHVERBOSE(
				device_printf(devinfo->dev, 
				    " Disabling crossassociatement connection "
				    "nid %d conn %d cnid %d.\n",
				    i, j, cw->nid);
			);
		}
	}
	/* ... using controls */
	i = 0;
	while ((ctl = hdaa_audio_ctl_each(devinfo, &i)) != NULL) {
		if (ctl->enable == 0 || ctl->childwidget == NULL)
			continue;
		/* Allow any -> mix */
		if (ctl->widget->bindas == -2)
			continue;
		/* Allow mix -> out. */
		if (ctl->childwidget->bindas == -2 &&
		    ctl->widget->bindas >= 0 &&
		    ases[ctl->widget->bindas].dir == HDAA_CTL_OUT)
			continue;
		/* Allow mix -> mixed-in. */
		if (ctl->childwidget->bindas == -2 &&
		    ctl->widget->bindas >= 0 &&
		    ases[ctl->widget->bindas].mixed)
			continue;
		/* Allow in -> mix. */
		if ((ctl->widget->pflags & HDAA_ADC_MONITOR) &&
		    ctl->childwidget->bindas >= 0 &&
		    ases[ctl->childwidget->bindas].dir == HDAA_CTL_IN)
			continue;
		/* Allow if have common as/seqs. */
		if (ctl->widget->bindas == ctl->childwidget->bindas &&
		    (ctl->widget->bindseqmask & ctl->childwidget->bindseqmask) != 0)
			continue;
		ctl->forcemute = 1;
		ctl->muted = HDAA_AMP_MUTE_ALL;
		ctl->left = 0;
		ctl->right = 0;
		ctl->enable = 0;
		if (ctl->ndir == HDAA_CTL_IN)
			ctl->widget->connsenable[ctl->index] = 0;
		HDA_BOOTHVERBOSE(
			device_printf(devinfo->dev, 
			    " Disabling crossassociatement connection "
			    "ctl %d nid %d cnid %d.\n", i,
			    ctl->widget->nid,
			    ctl->childwidget->nid);
		);
	}

}

/*
 * Find controls to control amplification for source and calculate possible
 * amplification range.
 */
static int
hdaa_audio_ctl_source_amp(struct hdaa_devinfo *devinfo, nid_t nid, int index,
    int ossdev, int ctlable, int depth, int *minamp, int *maxamp)
{
	struct hdaa_widget *w, *wc;
	struct hdaa_audio_ctl *ctl;
	int i, j, conns = 0, tminamp, tmaxamp, cminamp, cmaxamp, found = 0;

	if (depth > HDA_PARSE_MAXDEPTH)
		return (found);

	w = hdaa_widget_get(devinfo, nid);
	if (w == NULL || w->enable == 0)
		return (found);

	/* Count number of active inputs. */
	if (depth > 0) {
		for (j = 0; j < w->nconns; j++) {
			if (!w->connsenable[j])
				continue;
			conns++;
		}
	}

	/* If this is not a first step - use input mixer.
	   Pins have common input ctl so care must be taken. */
	if (depth > 0 && ctlable && (conns == 1 ||
	    w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)) {
		ctl = hdaa_audio_ctl_amp_get(devinfo, w->nid, HDAA_CTL_IN,
		    index, 1);
		if (ctl) {
			ctl->ossmask |= (1 << ossdev);
			found++;
			if (*minamp == *maxamp) {
				*minamp += MINQDB(ctl);
				*maxamp += MAXQDB(ctl);
			}
		}
	}

	/* If widget has own ossdev - not traverse it.
	   It will be traversed on its own. */
	if (w->ossdev >= 0 && depth > 0)
		return (found);

	/* We must not traverse pin */
	if ((w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT ||
	    w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) &&
	    depth > 0)
		return (found);

	/* record that this widget exports such signal, */
	w->ossmask |= (1 << ossdev);

	/*
	 * If signals mixed, we can't assign controls farther.
	 * Ignore this on depth zero. Caller must knows why.
	 */
	if (conns > 1 &&
	    w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER)
		ctlable = 0;

	if (ctlable) {
		ctl = hdaa_audio_ctl_amp_get(devinfo, w->nid, HDAA_CTL_OUT, -1, 1);
		if (ctl) {
			ctl->ossmask |= (1 << ossdev);
			found++;
			if (*minamp == *maxamp) {
				*minamp += MINQDB(ctl);
				*maxamp += MAXQDB(ctl);
			}
		}
	}

	cminamp = cmaxamp = 0;
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		wc = hdaa_widget_get(devinfo, i);
		if (wc == NULL || wc->enable == 0)
			continue;
		for (j = 0; j < wc->nconns; j++) {
			if (wc->connsenable[j] && wc->conns[j] == nid) {
				tminamp = tmaxamp = 0;
				found += hdaa_audio_ctl_source_amp(devinfo,
				    wc->nid, j, ossdev, ctlable, depth + 1,
				    &tminamp, &tmaxamp);
				if (cminamp == 0 && cmaxamp == 0) {
					cminamp = tminamp;
					cmaxamp = tmaxamp;
				} else if (tminamp != tmaxamp) {
					cminamp = imax(cminamp, tminamp);
					cmaxamp = imin(cmaxamp, tmaxamp);
				}
			}
		}
	}
	if (*minamp == *maxamp && cminamp < cmaxamp) {
		*minamp += cminamp;
		*maxamp += cmaxamp;
	}
	return (found);
}

/*
 * Find controls to control amplification for destination and calculate
 * possible amplification range.
 */
static int
hdaa_audio_ctl_dest_amp(struct hdaa_devinfo *devinfo, nid_t nid, int index,
    int ossdev, int depth, int *minamp, int *maxamp)
{
	struct hdaa_audio_as *as = devinfo->as;
	struct hdaa_widget *w, *wc;
	struct hdaa_audio_ctl *ctl;
	int i, j, consumers, tminamp, tmaxamp, cminamp, cmaxamp, found = 0;

	if (depth > HDA_PARSE_MAXDEPTH)
		return (found);

	w = hdaa_widget_get(devinfo, nid);
	if (w == NULL || w->enable == 0)
		return (found);

	if (depth > 0) {
		/* If this node produce output for several consumers,
		   we can't touch it. */
		consumers = 0;
		for (i = devinfo->startnode; i < devinfo->endnode; i++) {
			wc = hdaa_widget_get(devinfo, i);
			if (wc == NULL || wc->enable == 0)
				continue;
			for (j = 0; j < wc->nconns; j++) {
				if (wc->connsenable[j] && wc->conns[j] == nid)
					consumers++;
			}
		}
		/* The only exception is if real HP redirection is configured
		   and this is a duplication point.
		   XXX: Actually exception is not completely correct.
		   XXX: Duplication point check is not perfect. */
		if ((consumers == 2 && (w->bindas < 0 ||
		    as[w->bindas].hpredir < 0 || as[w->bindas].fakeredir ||
		    (w->bindseqmask & (1 << 15)) == 0)) ||
		    consumers > 2)
			return (found);

		/* Else use it's output mixer. */
		ctl = hdaa_audio_ctl_amp_get(devinfo, w->nid,
		    HDAA_CTL_OUT, -1, 1);
		if (ctl) {
			ctl->ossmask |= (1 << ossdev);
			found++;
			if (*minamp == *maxamp) {
				*minamp += MINQDB(ctl);
				*maxamp += MAXQDB(ctl);
			}
		}
	}

	/* We must not traverse pin */
	if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX &&
	    depth > 0)
		return (found);

	cminamp = cmaxamp = 0;
	for (i = 0; i < w->nconns; i++) {
		if (w->connsenable[i] == 0)
			continue;
		if (index >= 0 && i != index)
			continue;
		tminamp = tmaxamp = 0;
		ctl = hdaa_audio_ctl_amp_get(devinfo, w->nid,
		    HDAA_CTL_IN, i, 1);
		if (ctl) {
			ctl->ossmask |= (1 << ossdev);
			found++;
			if (*minamp == *maxamp) {
				tminamp += MINQDB(ctl);
				tmaxamp += MAXQDB(ctl);
			}
		}
		found += hdaa_audio_ctl_dest_amp(devinfo, w->conns[i], -1, ossdev,
		    depth + 1, &tminamp, &tmaxamp);
		if (cminamp == 0 && cmaxamp == 0) {
			cminamp = tminamp;
			cmaxamp = tmaxamp;
		} else if (tminamp != tmaxamp) {
			cminamp = imax(cminamp, tminamp);
			cmaxamp = imin(cmaxamp, tmaxamp);
		}
	}
	if (*minamp == *maxamp && cminamp < cmaxamp) {
		*minamp += cminamp;
		*maxamp += cmaxamp;
	}
	return (found);
}

/*
 * Assign OSS names to sound sources
 */
static void
hdaa_audio_assign_names(struct hdaa_devinfo *devinfo)
{
	struct hdaa_audio_as *as = devinfo->as;
	struct hdaa_widget *w;
	int i, j;
	int type = -1, use, used = 0;
	static const int types[7][13] = {
	    { SOUND_MIXER_LINE, SOUND_MIXER_LINE1, SOUND_MIXER_LINE2, 
	      SOUND_MIXER_LINE3, -1 },	/* line */
	    { SOUND_MIXER_MONITOR, SOUND_MIXER_MIC, -1 }, /* int mic */
	    { SOUND_MIXER_MIC, SOUND_MIXER_MONITOR, -1 }, /* ext mic */
	    { SOUND_MIXER_CD, -1 },	/* cd */
	    { SOUND_MIXER_SPEAKER, -1 },	/* speaker */
	    { SOUND_MIXER_DIGITAL1, SOUND_MIXER_DIGITAL2, SOUND_MIXER_DIGITAL3,
	      -1 },	/* digital */
	    { SOUND_MIXER_LINE, SOUND_MIXER_LINE1, SOUND_MIXER_LINE2,
	      SOUND_MIXER_LINE3, SOUND_MIXER_PHONEIN, SOUND_MIXER_PHONEOUT,
	      SOUND_MIXER_VIDEO, SOUND_MIXER_RADIO, SOUND_MIXER_DIGITAL1,
	      SOUND_MIXER_DIGITAL2, SOUND_MIXER_DIGITAL3, SOUND_MIXER_MONITOR,
	      -1 }	/* others */
	};

	/* Surely known names */
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->bindas == -1)
			continue;
		use = -1;
		switch (w->type) {
		case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX:
			if (as[w->bindas].dir == HDAA_CTL_OUT)
				break;
			type = -1;
			switch (w->wclass.pin.config & HDA_CONFIG_DEFAULTCONF_DEVICE_MASK) {
			case HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_IN:
				type = 0;
				break;
			case HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN:
				if ((w->wclass.pin.config & HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK)
				    == HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_JACK)
					break;
				type = 1;
				break;
			case HDA_CONFIG_DEFAULTCONF_DEVICE_CD:
				type = 3;
				break;
			case HDA_CONFIG_DEFAULTCONF_DEVICE_SPEAKER:
				type = 4;
				break;
			case HDA_CONFIG_DEFAULTCONF_DEVICE_SPDIF_IN:
			case HDA_CONFIG_DEFAULTCONF_DEVICE_DIGITAL_OTHER_IN:
				type = 5;
				break;
			}
			if (type == -1)
				break;
			j = 0;
			while (types[type][j] >= 0 &&
			    (used & (1 << types[type][j])) != 0) {
				j++;
			}
			if (types[type][j] >= 0)
				use = types[type][j];
			break;
		case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT:
			use = SOUND_MIXER_PCM;
			break;
		case HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_BEEP_WIDGET:
			use = SOUND_MIXER_SPEAKER;
			break;
		default:
			break;
		}
		if (use >= 0) {
			w->ossdev = use;
			used |= (1 << use);
		}
	}
	/* Semi-known names */
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->ossdev >= 0)
			continue;
		if (w->bindas == -1)
			continue;
		if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		if (as[w->bindas].dir == HDAA_CTL_OUT)
			continue;
		type = -1;
		switch (w->wclass.pin.config & HDA_CONFIG_DEFAULTCONF_DEVICE_MASK) {
		case HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_OUT:
		case HDA_CONFIG_DEFAULTCONF_DEVICE_SPEAKER:
		case HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT:
		case HDA_CONFIG_DEFAULTCONF_DEVICE_AUX:
			type = 0;
			break;
		case HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN:
			type = 2;
			break;
		case HDA_CONFIG_DEFAULTCONF_DEVICE_SPDIF_OUT:
		case HDA_CONFIG_DEFAULTCONF_DEVICE_DIGITAL_OTHER_OUT:
			type = 5;
			break;
		}
		if (type == -1)
			break;
		j = 0;
		while (types[type][j] >= 0 &&
		    (used & (1 << types[type][j])) != 0) {
			j++;
		}
		if (types[type][j] >= 0) {
			w->ossdev = types[type][j];
			used |= (1 << types[type][j]);
		}
	}
	/* Others */
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->ossdev >= 0)
			continue;
		if (w->bindas == -1)
			continue;
		if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		if (as[w->bindas].dir == HDAA_CTL_OUT)
			continue;
		j = 0;
		while (types[6][j] >= 0 &&
		    (used & (1 << types[6][j])) != 0) {
			j++;
		}
		if (types[6][j] >= 0) {
			w->ossdev = types[6][j];
			used |= (1 << types[6][j]);
		}
	}
}

static void
hdaa_audio_build_tree(struct hdaa_devinfo *devinfo)
{
	struct hdaa_audio_as *as = devinfo->as;
	int j, res;

	/* Trace all associations in order of their numbers. */
	for (j = 0; j < devinfo->ascnt; j++) {
		if (as[j].enable == 0)
			continue;
		HDA_BOOTVERBOSE(
			device_printf(devinfo->dev,
			    "Tracing association %d (%d)\n", j, as[j].index);
		);
		if (as[j].dir == HDAA_CTL_OUT) {
retry:
			res = hdaa_audio_trace_as_out(devinfo, j, 0);
			if (res == 0 && as[j].hpredir >= 0 &&
			    as[j].fakeredir == 0) {
				/* If CODEC can't do analog HP redirection
				   try to make it using one more DAC. */
				as[j].fakeredir = 1;
				goto retry;
			}
		} else if (as[j].mixed)
			res = hdaa_audio_trace_as_in(devinfo, j);
		else
			res = hdaa_audio_trace_as_in_mch(devinfo, j, 0);
		if (res) {
			HDA_BOOTVERBOSE(
				device_printf(devinfo->dev,
				    "Association %d (%d) trace succeeded\n",
				    j, as[j].index);
			);
		} else {
			HDA_BOOTVERBOSE(
				device_printf(devinfo->dev,
				    "Association %d (%d) trace failed\n",
				    j, as[j].index);
			);
			as[j].enable = 0;
		}
	}

	/* Look for additional DACs/ADCs. */
	for (j = 0; j < devinfo->ascnt; j++) {
		if (as[j].enable == 0)
			continue;
		hdaa_audio_adddac(devinfo, j);
	}

	/* Trace mixer and beeper pseudo associations. */
	hdaa_audio_trace_as_extra(devinfo);
}

/*
 * Store in pdevinfo new data about whether and how we can control signal
 * for OSS device to/from specified widget.
 */
static void
hdaa_adjust_amp(struct hdaa_widget *w, int ossdev,
    int found, int minamp, int maxamp)
{
	struct hdaa_devinfo *devinfo = w->devinfo;
	struct hdaa_pcm_devinfo *pdevinfo;

	if (w->bindas >= 0)
		pdevinfo = devinfo->as[w->bindas].pdevinfo;
	else
		pdevinfo = &devinfo->devs[0];
	if (found)
		pdevinfo->ossmask |= (1 << ossdev);
	if (minamp == 0 && maxamp == 0)
		return;
	if (pdevinfo->minamp[ossdev] == 0 && pdevinfo->maxamp[ossdev] == 0) {
		pdevinfo->minamp[ossdev] = minamp;
		pdevinfo->maxamp[ossdev] = maxamp;
	} else {
		pdevinfo->minamp[ossdev] = imax(pdevinfo->minamp[ossdev], minamp);
		pdevinfo->maxamp[ossdev] = imin(pdevinfo->maxamp[ossdev], maxamp);
	}
}

/*
 * Trace signals from/to all possible sources/destionstions to find possible
 * recording sources, OSS device control ranges and to assign controls.
 */
static void
hdaa_audio_assign_mixers(struct hdaa_devinfo *devinfo)
{
	struct hdaa_audio_as *as = devinfo->as;
	struct hdaa_widget *w, *cw;
	int i, j, minamp, maxamp, found;

	/* Assign mixers to the tree. */
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		minamp = maxamp = 0;
		if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT ||
		    w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_BEEP_WIDGET ||
		    (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX &&
		    as[w->bindas].dir == HDAA_CTL_IN)) {
			if (w->ossdev < 0)
				continue;
			found = hdaa_audio_ctl_source_amp(devinfo, w->nid, -1,
			    w->ossdev, 1, 0, &minamp, &maxamp);
			hdaa_adjust_amp(w, w->ossdev, found, minamp, maxamp);
		} else if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT) {
			found = hdaa_audio_ctl_dest_amp(devinfo, w->nid, -1,
			    SOUND_MIXER_RECLEV, 0, &minamp, &maxamp);
			hdaa_adjust_amp(w, SOUND_MIXER_RECLEV, found, minamp, maxamp);
		} else if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX &&
		    as[w->bindas].dir == HDAA_CTL_OUT) {
			found = hdaa_audio_ctl_dest_amp(devinfo, w->nid, -1,
			    SOUND_MIXER_VOLUME, 0, &minamp, &maxamp);
			hdaa_adjust_amp(w, SOUND_MIXER_VOLUME, found, minamp, maxamp);
		}
		if (w->ossdev == SOUND_MIXER_IMIX) {
			minamp = maxamp = 0;
			found = hdaa_audio_ctl_source_amp(devinfo, w->nid, -1,
			    w->ossdev, 1, 0, &minamp, &maxamp);
			if (minamp == maxamp) {
				/* If we are unable to control input monitor
				   as source - try to control it as destination. */
				found += hdaa_audio_ctl_dest_amp(devinfo, w->nid, -1,
				    w->ossdev, 0, &minamp, &maxamp);
				w->pflags |= HDAA_IMIX_AS_DST;
			}
			hdaa_adjust_amp(w, w->ossdev, found, minamp, maxamp);
		}
		if (w->pflags & HDAA_ADC_MONITOR) {
			for (j = 0; j < w->nconns; j++) {
				if (!w->connsenable[j])
				    continue;
				cw = hdaa_widget_get(devinfo, w->conns[j]);
				if (cw == NULL || cw->enable == 0)
				    continue;
				if (cw->bindas == -1)
				    continue;
				if (cw->bindas >= 0 &&
				    as[cw->bindas].dir != HDAA_CTL_IN)
					continue;
				minamp = maxamp = 0;
				found = hdaa_audio_ctl_dest_amp(devinfo,
				    w->nid, j, SOUND_MIXER_IGAIN, 0,
				    &minamp, &maxamp);
				hdaa_adjust_amp(w, SOUND_MIXER_IGAIN,
				    found, minamp, maxamp);
			}
		}
	}
}

static void
hdaa_audio_prepare_pin_ctrl(struct hdaa_devinfo *devinfo)
{
	struct hdaa_audio_as *as = devinfo->as;
	struct hdaa_widget *w;
	uint32_t pincap;
	int i;

	for (i = 0; i < devinfo->nodecnt; i++) {
		w = &devinfo->widget[i];
		if (w == NULL)
			continue;
		if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX &&
		    w->waspin == 0)
			continue;

		pincap = w->wclass.pin.cap;

		/* Disable everything. */
		w->wclass.pin.ctrl &= ~(
		    HDA_CMD_SET_PIN_WIDGET_CTRL_HPHN_ENABLE |
		    HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE |
		    HDA_CMD_SET_PIN_WIDGET_CTRL_IN_ENABLE |
		    HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE_MASK);

		if (w->enable == 0) {
			/* Pin is unused so left it disabled. */
			continue;
		} else if (w->waspin) {
			/* Enable input for beeper input. */
			w->wclass.pin.ctrl |=
			    HDA_CMD_SET_PIN_WIDGET_CTRL_IN_ENABLE;
		} else if (w->bindas < 0 || as[w->bindas].enable == 0) {
			/* Pin is unused so left it disabled. */
			continue;
		} else if (as[w->bindas].dir == HDAA_CTL_IN) {
			/* Input pin, configure for input. */
			if (HDA_PARAM_PIN_CAP_INPUT_CAP(pincap))
				w->wclass.pin.ctrl |=
				    HDA_CMD_SET_PIN_WIDGET_CTRL_IN_ENABLE;

			if ((devinfo->quirks & HDAA_QUIRK_IVREF100) &&
			    HDA_PARAM_PIN_CAP_VREF_CTRL_100(pincap))
				w->wclass.pin.ctrl |=
				    HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE(
				    HDA_CMD_PIN_WIDGET_CTRL_VREF_ENABLE_100);
			else if ((devinfo->quirks & HDAA_QUIRK_IVREF80) &&
			    HDA_PARAM_PIN_CAP_VREF_CTRL_80(pincap))
				w->wclass.pin.ctrl |=
				    HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE(
				    HDA_CMD_PIN_WIDGET_CTRL_VREF_ENABLE_80);
			else if ((devinfo->quirks & HDAA_QUIRK_IVREF50) &&
			    HDA_PARAM_PIN_CAP_VREF_CTRL_50(pincap))
				w->wclass.pin.ctrl |=
				    HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE(
				    HDA_CMD_PIN_WIDGET_CTRL_VREF_ENABLE_50);
		} else {
			/* Output pin, configure for output. */
			if (HDA_PARAM_PIN_CAP_OUTPUT_CAP(pincap))
				w->wclass.pin.ctrl |=
				    HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE;

			if (HDA_PARAM_PIN_CAP_HEADPHONE_CAP(pincap) &&
			    (w->wclass.pin.config &
			    HDA_CONFIG_DEFAULTCONF_DEVICE_MASK) ==
			    HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT)
				w->wclass.pin.ctrl |=
				    HDA_CMD_SET_PIN_WIDGET_CTRL_HPHN_ENABLE;

			if ((devinfo->quirks & HDAA_QUIRK_OVREF100) &&
			    HDA_PARAM_PIN_CAP_VREF_CTRL_100(pincap))
				w->wclass.pin.ctrl |=
				    HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE(
				    HDA_CMD_PIN_WIDGET_CTRL_VREF_ENABLE_100);
			else if ((devinfo->quirks & HDAA_QUIRK_OVREF80) &&
			    HDA_PARAM_PIN_CAP_VREF_CTRL_80(pincap))
				w->wclass.pin.ctrl |=
				    HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE(
				    HDA_CMD_PIN_WIDGET_CTRL_VREF_ENABLE_80);
			else if ((devinfo->quirks & HDAA_QUIRK_OVREF50) &&
			    HDA_PARAM_PIN_CAP_VREF_CTRL_50(pincap))
				w->wclass.pin.ctrl |=
				    HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE(
				    HDA_CMD_PIN_WIDGET_CTRL_VREF_ENABLE_50);
		}
	}
}

static void
hdaa_audio_ctl_commit(struct hdaa_devinfo *devinfo)
{
	struct hdaa_audio_ctl *ctl;
	int i, z;

	i = 0;
	while ((ctl = hdaa_audio_ctl_each(devinfo, &i)) != NULL) {
		if (ctl->enable == 0 || ctl->ossmask != 0) {
			/* Mute disabled and mixer controllable controls.
			 * Last will be initialized by mixer_init().
			 * This expected to reduce click on startup. */
			hdaa_audio_ctl_amp_set(ctl, HDAA_AMP_MUTE_ALL, 0, 0);
			continue;
		}
		/* Init fixed controls to 0dB amplification. */
		z = ctl->offset;
		if (z > ctl->step)
			z = ctl->step;
		hdaa_audio_ctl_amp_set(ctl, HDAA_AMP_MUTE_NONE, z, z);
	}
}

static void
hdaa_gpio_commit(struct hdaa_devinfo *devinfo)
{
	uint32_t gdata, gmask, gdir;
	int i, numgpio;

	numgpio = HDA_PARAM_GPIO_COUNT_NUM_GPIO(devinfo->gpio_cap);
	if (devinfo->gpio != 0 && numgpio != 0) {
		gdata = hda_command(devinfo->dev,
		    HDA_CMD_GET_GPIO_DATA(0, devinfo->nid));
		gmask = hda_command(devinfo->dev,
		    HDA_CMD_GET_GPIO_ENABLE_MASK(0, devinfo->nid));
		gdir = hda_command(devinfo->dev,
		    HDA_CMD_GET_GPIO_DIRECTION(0, devinfo->nid));
		for (i = 0; i < numgpio; i++) {
			if ((devinfo->gpio & HDAA_GPIO_MASK(i)) ==
			    HDAA_GPIO_SET(i)) {
				gdata |= (1 << i);
				gmask |= (1 << i);
				gdir |= (1 << i);
			} else if ((devinfo->gpio & HDAA_GPIO_MASK(i)) ==
			    HDAA_GPIO_CLEAR(i)) {
				gdata &= ~(1 << i);
				gmask |= (1 << i);
				gdir |= (1 << i);
			} else if ((devinfo->gpio & HDAA_GPIO_MASK(i)) ==
			    HDAA_GPIO_DISABLE(i)) {
				gmask &= ~(1 << i);
			} else if ((devinfo->gpio & HDAA_GPIO_MASK(i)) ==
			    HDAA_GPIO_INPUT(i)) {
				gmask |= (1 << i);
				gdir &= ~(1 << i);
			}
		}
		HDA_BOOTVERBOSE(
			device_printf(devinfo->dev, "GPIO commit\n");
		);
		hda_command(devinfo->dev,
		    HDA_CMD_SET_GPIO_ENABLE_MASK(0, devinfo->nid, gmask));
		hda_command(devinfo->dev,
		    HDA_CMD_SET_GPIO_DIRECTION(0, devinfo->nid, gdir));
		hda_command(devinfo->dev,
		    HDA_CMD_SET_GPIO_DATA(0, devinfo->nid, gdata));
		HDA_BOOTVERBOSE(
			hdaa_dump_gpio(devinfo);
		);
	}
}

static void
hdaa_gpo_commit(struct hdaa_devinfo *devinfo)
{
	uint32_t gdata;
	int i, numgpo;

	numgpo = HDA_PARAM_GPIO_COUNT_NUM_GPO(devinfo->gpio_cap);
	if (devinfo->gpo != 0 && numgpo != 0) {
		gdata = hda_command(devinfo->dev,
		    HDA_CMD_GET_GPO_DATA(0, devinfo->nid));
		for (i = 0; i < numgpo; i++) {
			if ((devinfo->gpio & HDAA_GPIO_MASK(i)) ==
			    HDAA_GPIO_SET(i)) {
				gdata |= (1 << i);
			} else if ((devinfo->gpio & HDAA_GPIO_MASK(i)) ==
			    HDAA_GPIO_CLEAR(i)) {
				gdata &= ~(1 << i);
			}
		}
		HDA_BOOTVERBOSE(
			device_printf(devinfo->dev, "GPO commit\n");
		);
		hda_command(devinfo->dev,
		    HDA_CMD_SET_GPO_DATA(0, devinfo->nid, gdata));
		HDA_BOOTVERBOSE(
			hdaa_dump_gpo(devinfo);
		);
	}
}

static void
hdaa_audio_commit(struct hdaa_devinfo *devinfo)
{
	struct hdaa_widget *w;
	int i;

	/* Commit controls. */
	hdaa_audio_ctl_commit(devinfo);

	/* Commit selectors, pins and EAPD. */
	for (i = 0; i < devinfo->nodecnt; i++) {
		w = &devinfo->widget[i];
		if (w == NULL)
			continue;
		if (w->selconn == -1)
			w->selconn = 0;
		if (w->nconns > 0)
			hdaa_widget_connection_select(w, w->selconn);
		if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX ||
		    w->waspin) {
			hda_command(devinfo->dev,
			    HDA_CMD_SET_PIN_WIDGET_CTRL(0, w->nid,
			    w->wclass.pin.ctrl));
		}
		if (w->param.eapdbtl != HDA_INVALID) {
			uint32_t val;

			val = w->param.eapdbtl;
			if (devinfo->quirks &
			    HDAA_QUIRK_EAPDINV)
				val ^= HDA_CMD_SET_EAPD_BTL_ENABLE_EAPD;
			hda_command(devinfo->dev,
			    HDA_CMD_SET_EAPD_BTL_ENABLE(0, w->nid,
			    val));
		}
	}

	hdaa_gpio_commit(devinfo);
	hdaa_gpo_commit(devinfo);
}

static void
hdaa_powerup(struct hdaa_devinfo *devinfo)
{
	int i;

	hda_command(devinfo->dev,
	    HDA_CMD_SET_POWER_STATE(0,
	    devinfo->nid, HDA_CMD_POWER_STATE_D0));
	DELAY(100);

	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		hda_command(devinfo->dev,
		    HDA_CMD_SET_POWER_STATE(0,
		    i, HDA_CMD_POWER_STATE_D0));
	}
	DELAY(1000);
}

static int
hdaa_pcmchannel_setup(struct hdaa_chan *ch)
{
	struct hdaa_devinfo *devinfo = ch->devinfo;
	struct hdaa_audio_as *as = devinfo->as;
	struct hdaa_widget *w;
	uint32_t cap, fmtcap, pcmcap;
	int i, j, ret, channels, onlystereo;
	uint16_t pinset;

	ch->caps = hdaa_caps;
	ch->caps.fmtlist = ch->fmtlist;
	ch->bit16 = 1;
	ch->bit32 = 0;
	ch->pcmrates[0] = 48000;
	ch->pcmrates[1] = 0;
	ch->stripecap = 0xff;

	ret = 0;
	channels = 0;
	onlystereo = 1;
	pinset = 0;
	fmtcap = devinfo->supp_stream_formats;
	pcmcap = devinfo->supp_pcm_size_rate;

	for (i = 0; i < 16; i++) {
		/* Check as is correct */
		if (ch->as < 0)
			break;
		/* Cound only present DACs */
		if (as[ch->as].dacs[ch->asindex][i] <= 0)
			continue;
		/* Ignore duplicates */
		for (j = 0; j < ret; j++) {
			if (ch->io[j] == as[ch->as].dacs[ch->asindex][i])
				break;
		}
		if (j < ret)
			continue;

		w = hdaa_widget_get(devinfo, as[ch->as].dacs[ch->asindex][i]);
		if (w == NULL || w->enable == 0)
			continue;
		cap = w->param.supp_stream_formats;
		if (!HDA_PARAM_SUPP_STREAM_FORMATS_PCM(cap) &&
		    !HDA_PARAM_SUPP_STREAM_FORMATS_AC3(cap))
			continue;
		/* Many CODECs does not declare AC3 support on SPDIF.
		   I don't beleave that they doesn't support it! */
		if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(w->param.widget_cap))
			cap |= HDA_PARAM_SUPP_STREAM_FORMATS_AC3_MASK;
		if (ret == 0) {
			fmtcap = cap;
			pcmcap = w->param.supp_pcm_size_rate;
		} else {
			fmtcap &= cap;
			pcmcap &= w->param.supp_pcm_size_rate;
		}
		ch->io[ret++] = as[ch->as].dacs[ch->asindex][i];
		ch->stripecap &= w->wclass.conv.stripecap;
		/* Do not count redirection pin/dac channels. */
		if (i == 15 && as[ch->as].hpredir >= 0)
			continue;
		channels += HDA_PARAM_AUDIO_WIDGET_CAP_CC(w->param.widget_cap) + 1;
		if (HDA_PARAM_AUDIO_WIDGET_CAP_CC(w->param.widget_cap) != 1)
			onlystereo = 0;
		pinset |= (1 << i);
	}
	ch->io[ret] = -1;
	ch->channels = channels;

	if (as[ch->as].fakeredir)
		ret--;
	/* Standard speaks only about stereo pins and playback, ... */
	if ((!onlystereo) || as[ch->as].mixed)
		pinset = 0;
	/* ..., but there it gives us info about speakers layout. */
	as[ch->as].pinset = pinset;

	ch->supp_stream_formats = fmtcap;
	ch->supp_pcm_size_rate = pcmcap;

	/*
	 *  8bit = 0
	 * 16bit = 1
	 * 20bit = 2
	 * 24bit = 3
	 * 32bit = 4
	 */
	if (ret > 0) {
		i = 0;
		if (HDA_PARAM_SUPP_STREAM_FORMATS_PCM(fmtcap)) {
			if (HDA_PARAM_SUPP_PCM_SIZE_RATE_16BIT(pcmcap))
				ch->bit16 = 1;
			else if (HDA_PARAM_SUPP_PCM_SIZE_RATE_8BIT(pcmcap))
				ch->bit16 = 0;
			if (HDA_PARAM_SUPP_PCM_SIZE_RATE_24BIT(pcmcap))
				ch->bit32 = 3;
			else if (HDA_PARAM_SUPP_PCM_SIZE_RATE_20BIT(pcmcap))
				ch->bit32 = 2;
			else if (HDA_PARAM_SUPP_PCM_SIZE_RATE_32BIT(pcmcap))
				ch->bit32 = 4;
			if (!(devinfo->quirks & HDAA_QUIRK_FORCESTEREO)) {
				ch->fmtlist[i++] = SND_FORMAT(AFMT_S16_LE, 1, 0);
				if (ch->bit32)
					ch->fmtlist[i++] = SND_FORMAT(AFMT_S32_LE, 1, 0);
			}
			if (channels >= 2) {
				ch->fmtlist[i++] = SND_FORMAT(AFMT_S16_LE, 2, 0);
				if (ch->bit32)
					ch->fmtlist[i++] = SND_FORMAT(AFMT_S32_LE, 2, 0);
			}
			if (channels >= 3 && !onlystereo) {
				ch->fmtlist[i++] = SND_FORMAT(AFMT_S16_LE, 3, 0);
				if (ch->bit32)
					ch->fmtlist[i++] = SND_FORMAT(AFMT_S32_LE, 3, 0);
				ch->fmtlist[i++] = SND_FORMAT(AFMT_S16_LE, 3, 1);
				if (ch->bit32)
					ch->fmtlist[i++] = SND_FORMAT(AFMT_S32_LE, 3, 1);
			}
			if (channels >= 4) {
				ch->fmtlist[i++] = SND_FORMAT(AFMT_S16_LE, 4, 0);
				if (ch->bit32)
					ch->fmtlist[i++] = SND_FORMAT(AFMT_S32_LE, 4, 0);
				if (!onlystereo) {
					ch->fmtlist[i++] = SND_FORMAT(AFMT_S16_LE, 4, 1);
					if (ch->bit32)
						ch->fmtlist[i++] = SND_FORMAT(AFMT_S32_LE, 4, 1);
				}
			}
			if (channels >= 5 && !onlystereo) {
				ch->fmtlist[i++] = SND_FORMAT(AFMT_S16_LE, 5, 0);
				if (ch->bit32)
					ch->fmtlist[i++] = SND_FORMAT(AFMT_S32_LE, 5, 0);
				ch->fmtlist[i++] = SND_FORMAT(AFMT_S16_LE, 5, 1);
				if (ch->bit32)
					ch->fmtlist[i++] = SND_FORMAT(AFMT_S32_LE, 5, 1);
			}
			if (channels >= 6) {
				ch->fmtlist[i++] = SND_FORMAT(AFMT_S16_LE, 6, 1);
				if (ch->bit32)
					ch->fmtlist[i++] = SND_FORMAT(AFMT_S32_LE, 6, 1);
				if (!onlystereo) {
					ch->fmtlist[i++] = SND_FORMAT(AFMT_S16_LE, 6, 0);
					if (ch->bit32)
						ch->fmtlist[i++] = SND_FORMAT(AFMT_S32_LE, 6, 0);
				}
			}
			if (channels >= 7 && !onlystereo) {
				ch->fmtlist[i++] = SND_FORMAT(AFMT_S16_LE, 7, 0);
				if (ch->bit32)
					ch->fmtlist[i++] = SND_FORMAT(AFMT_S32_LE, 7, 0);
				ch->fmtlist[i++] = SND_FORMAT(AFMT_S16_LE, 7, 1);
				if (ch->bit32)
					ch->fmtlist[i++] = SND_FORMAT(AFMT_S32_LE, 7, 1);
			}
			if (channels >= 8) {
				ch->fmtlist[i++] = SND_FORMAT(AFMT_S16_LE, 8, 1);
				if (ch->bit32)
					ch->fmtlist[i++] = SND_FORMAT(AFMT_S32_LE, 8, 1);
			}
		}
		if (HDA_PARAM_SUPP_STREAM_FORMATS_AC3(fmtcap)) {
			ch->fmtlist[i++] = SND_FORMAT(AFMT_AC3, 2, 0);
			if (channels >= 8) {
				ch->fmtlist[i++] = SND_FORMAT(AFMT_AC3, 8, 0);
				ch->fmtlist[i++] = SND_FORMAT(AFMT_AC3, 8, 1);
			}
		}
		ch->fmtlist[i] = 0;
		i = 0;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_8KHZ(pcmcap))
			ch->pcmrates[i++] = 8000;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_11KHZ(pcmcap))
			ch->pcmrates[i++] = 11025;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_16KHZ(pcmcap))
			ch->pcmrates[i++] = 16000;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_22KHZ(pcmcap))
			ch->pcmrates[i++] = 22050;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_32KHZ(pcmcap))
			ch->pcmrates[i++] = 32000;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_44KHZ(pcmcap))
			ch->pcmrates[i++] = 44100;
		/* if (HDA_PARAM_SUPP_PCM_SIZE_RATE_48KHZ(pcmcap)) */
		ch->pcmrates[i++] = 48000;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_88KHZ(pcmcap))
			ch->pcmrates[i++] = 88200;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_96KHZ(pcmcap))
			ch->pcmrates[i++] = 96000;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_176KHZ(pcmcap))
			ch->pcmrates[i++] = 176400;
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_192KHZ(pcmcap))
			ch->pcmrates[i++] = 192000;
		/* if (HDA_PARAM_SUPP_PCM_SIZE_RATE_384KHZ(pcmcap)) */
		ch->pcmrates[i] = 0;
		if (i > 0) {
			ch->caps.minspeed = ch->pcmrates[0];
			ch->caps.maxspeed = ch->pcmrates[i - 1];
		}
	}

	return (ret);
}

static void
hdaa_prepare_pcms(struct hdaa_devinfo *devinfo)
{
	struct hdaa_audio_as *as = devinfo->as;
	int i, j, k, apdev = 0, ardev = 0, dpdev = 0, drdev = 0;

	for (i = 0; i < devinfo->ascnt; i++) {
		if (as[i].enable == 0)
			continue;
		if (as[i].dir == HDAA_CTL_IN) {
			if (as[i].digital)
				drdev++;
			else
				ardev++;
		} else {
			if (as[i].digital)
				dpdev++;
			else
				apdev++;
		}
	}
	devinfo->num_devs =
	    max(ardev, apdev) + max(drdev, dpdev);
	devinfo->devs =
	    (struct hdaa_pcm_devinfo *)malloc(
	    devinfo->num_devs * sizeof(struct hdaa_pcm_devinfo),
	    M_HDAA, M_ZERO | M_NOWAIT);
	if (devinfo->devs == NULL) {
		device_printf(devinfo->dev,
		    "Unable to allocate memory for devices\n");
		return;
	}
	for (i = 0; i < devinfo->num_devs; i++) {
		devinfo->devs[i].index = i;
		devinfo->devs[i].devinfo = devinfo;
		devinfo->devs[i].playas = -1;
		devinfo->devs[i].recas = -1;
		devinfo->devs[i].digital = 255;
	}
	for (i = 0; i < devinfo->ascnt; i++) {
		if (as[i].enable == 0)
			continue;
		for (j = 0; j < devinfo->num_devs; j++) {
			if (devinfo->devs[j].digital != 255 &&
			    (!devinfo->devs[j].digital) !=
			    (!as[i].digital))
				continue;
			if (as[i].dir == HDAA_CTL_IN) {
				if (devinfo->devs[j].recas >= 0)
					continue;
				devinfo->devs[j].recas = i;
			} else {
				if (devinfo->devs[j].playas >= 0)
					continue;
				devinfo->devs[j].playas = i;
			}
			as[i].pdevinfo = &devinfo->devs[j];
			for (k = 0; k < as[i].num_chans; k++) {
				devinfo->chans[as[i].chans[k]].pdevinfo =
				    &devinfo->devs[j];
			}
			devinfo->devs[j].digital = as[i].digital;
			break;
		}
	}
}

static void
hdaa_create_pcms(struct hdaa_devinfo *devinfo)
{
	int i;

	for (i = 0; i < devinfo->num_devs; i++) {
		struct hdaa_pcm_devinfo *pdevinfo = &devinfo->devs[i];

		pdevinfo->dev = device_add_child(devinfo->dev, "pcm", -1);
		device_set_ivars(pdevinfo->dev, (void *)pdevinfo);
	}
}

static void
hdaa_dump_ctls(struct hdaa_pcm_devinfo *pdevinfo, const char *banner, uint32_t flag)
{
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	struct hdaa_audio_ctl *ctl;
	char buf[64];
	int i, j, printed = 0;

	if (flag == 0) {
		flag = ~(SOUND_MASK_VOLUME | SOUND_MASK_PCM |
		    SOUND_MASK_CD | SOUND_MASK_LINE | SOUND_MASK_RECLEV |
		    SOUND_MASK_MIC | SOUND_MASK_SPEAKER | SOUND_MASK_IGAIN |
		    SOUND_MASK_OGAIN | SOUND_MASK_IMIX | SOUND_MASK_MONITOR);
	}

	for (j = 0; j < SOUND_MIXER_NRDEVICES; j++) {
		if ((flag & (1 << j)) == 0)
			continue;
		i = 0;
		printed = 0;
		while ((ctl = hdaa_audio_ctl_each(devinfo, &i)) != NULL) {
			if (ctl->enable == 0 ||
			    ctl->widget->enable == 0)
				continue;
			if (!((pdevinfo->playas >= 0 &&
			    ctl->widget->bindas == pdevinfo->playas) ||
			    (pdevinfo->recas >= 0 &&
			    ctl->widget->bindas == pdevinfo->recas) ||
			    (ctl->widget->bindas == -2 && pdevinfo->index == 0)))
				continue;
			if ((ctl->ossmask & (1 << j)) == 0)
				continue;

			if (printed == 0) {
				if (banner != NULL) {
					device_printf(pdevinfo->dev, "%s", banner);
				} else {
					device_printf(pdevinfo->dev, "Unknown Ctl");
				}
				printf(" (OSS: %s)",
				    hdaa_audio_ctl_ossmixer_mask2allname(1 << j,
				    buf, sizeof(buf)));
				if (pdevinfo->ossmask & (1 << j)) {
					printf(": %+d/%+ddB\n",
					    pdevinfo->minamp[j] / 4,
					    pdevinfo->maxamp[j] / 4);
				} else
					printf("\n");
				printed = 1;
			}
			device_printf(pdevinfo->dev, "   +- ctl %2d (nid %3d %s", i,
				ctl->widget->nid,
				(ctl->ndir == HDAA_CTL_IN)?"in ":"out");
			if (ctl->ndir == HDAA_CTL_IN && ctl->ndir == ctl->dir)
				printf(" %2d): ", ctl->index);
			else
				printf("):    ");
			if (ctl->step > 0) {
				printf("%+d/%+ddB (%d steps)%s\n",
				    MINQDB(ctl) / 4,
				    MAXQDB(ctl) / 4,
				    ctl->step + 1,
				    ctl->mute?" + mute":"");
			} else
				printf("%s\n", ctl->mute?"mute":"");
		}
	}
	if (printed)
		device_printf(pdevinfo->dev, "\n");
}

static void
hdaa_dump_audio_formats(device_t dev, uint32_t fcap, uint32_t pcmcap)
{
	uint32_t cap;

	cap = fcap;
	if (cap != 0) {
		device_printf(dev, "     Stream cap: 0x%08x", cap);
		if (HDA_PARAM_SUPP_STREAM_FORMATS_AC3(cap))
			printf(" AC3");
		if (HDA_PARAM_SUPP_STREAM_FORMATS_FLOAT32(cap))
			printf(" FLOAT32");
		if (HDA_PARAM_SUPP_STREAM_FORMATS_PCM(cap))
			printf(" PCM");
		printf("\n");
	}
	cap = pcmcap;
	if (cap != 0) {
		device_printf(dev, "        PCM cap: 0x%08x", cap);
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_8BIT(cap))
			printf(" 8");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_16BIT(cap))
			printf(" 16");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_20BIT(cap))
			printf(" 20");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_24BIT(cap))
			printf(" 24");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_32BIT(cap))
			printf(" 32");
		printf(" bits,");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_8KHZ(cap))
			printf(" 8");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_11KHZ(cap))
			printf(" 11");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_16KHZ(cap))
			printf(" 16");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_22KHZ(cap))
			printf(" 22");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_32KHZ(cap))
			printf(" 32");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_44KHZ(cap))
			printf(" 44");
		printf(" 48");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_88KHZ(cap))
			printf(" 88");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_96KHZ(cap))
			printf(" 96");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_176KHZ(cap))
			printf(" 176");
		if (HDA_PARAM_SUPP_PCM_SIZE_RATE_192KHZ(cap))
			printf(" 192");
		printf(" KHz\n");
	}
}

static void
hdaa_dump_pin(struct hdaa_widget *w)
{
	uint32_t pincap;

	pincap = w->wclass.pin.cap;

	device_printf(w->devinfo->dev, "        Pin cap: 0x%08x", pincap);
	if (HDA_PARAM_PIN_CAP_IMP_SENSE_CAP(pincap))
		printf(" ISC");
	if (HDA_PARAM_PIN_CAP_TRIGGER_REQD(pincap))
		printf(" TRQD");
	if (HDA_PARAM_PIN_CAP_PRESENCE_DETECT_CAP(pincap))
		printf(" PDC");
	if (HDA_PARAM_PIN_CAP_HEADPHONE_CAP(pincap))
		printf(" HP");
	if (HDA_PARAM_PIN_CAP_OUTPUT_CAP(pincap))
		printf(" OUT");
	if (HDA_PARAM_PIN_CAP_INPUT_CAP(pincap))
		printf(" IN");
	if (HDA_PARAM_PIN_CAP_BALANCED_IO_PINS(pincap))
		printf(" BAL");
	if (HDA_PARAM_PIN_CAP_HDMI(pincap))
		printf(" HDMI");
	if (HDA_PARAM_PIN_CAP_VREF_CTRL(pincap)) {
		printf(" VREF[");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_50(pincap))
			printf(" 50");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_80(pincap))
			printf(" 80");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_100(pincap))
			printf(" 100");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_GROUND(pincap))
			printf(" GROUND");
		if (HDA_PARAM_PIN_CAP_VREF_CTRL_HIZ(pincap))
			printf(" HIZ");
		printf(" ]");
	}
	if (HDA_PARAM_PIN_CAP_EAPD_CAP(pincap))
		printf(" EAPD");
	if (HDA_PARAM_PIN_CAP_DP(pincap))
		printf(" DP");
	if (HDA_PARAM_PIN_CAP_HBR(pincap))
		printf(" HBR");
	printf("\n");
	device_printf(w->devinfo->dev, "     Pin config: 0x%08x\n",
	    w->wclass.pin.config);
	device_printf(w->devinfo->dev, "    Pin control: 0x%08x", w->wclass.pin.ctrl);
	if (w->wclass.pin.ctrl & HDA_CMD_SET_PIN_WIDGET_CTRL_HPHN_ENABLE)
		printf(" HP");
	if (w->wclass.pin.ctrl & HDA_CMD_SET_PIN_WIDGET_CTRL_IN_ENABLE)
		printf(" IN");
	if (w->wclass.pin.ctrl & HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE)
		printf(" OUT");
	if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(w->param.widget_cap)) {
		if ((w->wclass.pin.ctrl &
		    HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE_MASK) == 0x03)
			printf(" HBR");
		else if ((w->wclass.pin.ctrl &
		    HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE_MASK) != 0)
			printf(" EPTs");
	} else {
		if ((w->wclass.pin.ctrl &
		    HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE_MASK) != 0)
			printf(" VREFs");
	}
	printf("\n");
}

static void
hdaa_dump_pin_config(struct hdaa_widget *w, uint32_t conf)
{

	device_printf(w->devinfo->dev, "%2d %08x %-2d %-2d "
	    "%-13s %-5s %-7s %-10s %-7s %d%s\n",
	    w->nid, conf,
	    HDA_CONFIG_DEFAULTCONF_ASSOCIATION(conf),
	    HDA_CONFIG_DEFAULTCONF_SEQUENCE(conf),
	    HDA_DEVS[HDA_CONFIG_DEFAULTCONF_DEVICE(conf)],
	    HDA_CONNS[HDA_CONFIG_DEFAULTCONF_CONNECTIVITY(conf)],
	    HDA_CONNECTORS[HDA_CONFIG_DEFAULTCONF_CONNECTION_TYPE(conf)],
	    HDA_LOCS[HDA_CONFIG_DEFAULTCONF_LOCATION(conf)],
	    HDA_COLORS[HDA_CONFIG_DEFAULTCONF_COLOR(conf)],
	    HDA_CONFIG_DEFAULTCONF_MISC(conf),
	    (w->enable == 0)?" DISA":"");
}

static void
hdaa_dump_pin_configs(struct hdaa_devinfo *devinfo)
{
	struct hdaa_widget *w;
	int i;

	device_printf(devinfo->dev, "nid   0x    as seq "
	    "device       conn  jack    loc        color   misc\n");
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL)
			continue;
		if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		hdaa_dump_pin_config(w, w->wclass.pin.config);
	}
}

static void
hdaa_dump_amp(device_t dev, uint32_t cap, const char *banner)
{
	int offset, size, step;

	offset = HDA_PARAM_OUTPUT_AMP_CAP_OFFSET(cap);
	size = HDA_PARAM_OUTPUT_AMP_CAP_STEPSIZE(cap);
	step = HDA_PARAM_OUTPUT_AMP_CAP_NUMSTEPS(cap);
	device_printf(dev, "     %s amp: 0x%08x "
	    "mute=%d step=%d size=%d offset=%d (%+d/%+ddB)\n",
	    banner, cap,
	    HDA_PARAM_OUTPUT_AMP_CAP_MUTE_CAP(cap),
	    step, size, offset,
	    ((0 - offset) * (size + 1)) / 4,
	    ((step - offset) * (size + 1)) / 4);
}

static void
hdaa_dump_nodes(struct hdaa_devinfo *devinfo)
{
	struct hdaa_widget *w, *cw;
	char buf[64];
	int i, j;

	device_printf(devinfo->dev, "\n");
	device_printf(devinfo->dev, "Default parameters:\n");
	hdaa_dump_audio_formats(devinfo->dev,
	    devinfo->supp_stream_formats,
	    devinfo->supp_pcm_size_rate);
	hdaa_dump_amp(devinfo->dev, devinfo->inamp_cap, " Input");
	hdaa_dump_amp(devinfo->dev, devinfo->outamp_cap, "Output");
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL) {
			device_printf(devinfo->dev, "Ghost widget nid=%d\n", i);
			continue;
		}
		device_printf(devinfo->dev, "\n");
		device_printf(devinfo->dev, "            nid: %d%s\n", w->nid,
		    (w->enable == 0) ? " [DISABLED]" : "");
		device_printf(devinfo->dev, "           Name: %s\n", w->name);
		device_printf(devinfo->dev, "     Widget cap: 0x%08x",
		    w->param.widget_cap);
		if (w->param.widget_cap & 0x0ee1) {
			if (HDA_PARAM_AUDIO_WIDGET_CAP_LR_SWAP(w->param.widget_cap))
			    printf(" LRSWAP");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_POWER_CTRL(w->param.widget_cap))
			    printf(" PWR");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(w->param.widget_cap))
			    printf(" DIGITAL");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_UNSOL_CAP(w->param.widget_cap))
			    printf(" UNSOL");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_PROC_WIDGET(w->param.widget_cap))
			    printf(" PROC");
			if (HDA_PARAM_AUDIO_WIDGET_CAP_STRIPE(w->param.widget_cap))
			    printf(" STRIPE(x%d)",
				1 << (fls(w->wclass.conv.stripecap) - 1));
			j = HDA_PARAM_AUDIO_WIDGET_CAP_CC(w->param.widget_cap);
			if (j == 1)
			    printf(" STEREO");
			else if (j > 1)
			    printf(" %dCH", j + 1);
		}
		printf("\n");
		if (w->bindas != -1) {
			device_printf(devinfo->dev, "    Association: %d (0x%04x)\n",
			    w->bindas, w->bindseqmask);
		}
		if (w->ossmask != 0 || w->ossdev >= 0) {
			device_printf(devinfo->dev, "            OSS: %s",
			    hdaa_audio_ctl_ossmixer_mask2allname(w->ossmask, buf, sizeof(buf)));
			if (w->ossdev >= 0)
			    printf(" (%s)", ossnames[w->ossdev]);
			printf("\n");
		}
		if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_OUTPUT ||
		    w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT) {
			hdaa_dump_audio_formats(devinfo->dev,
			    w->param.supp_stream_formats,
			    w->param.supp_pcm_size_rate);
		} else if (w->type ==
		    HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX || w->waspin)
			hdaa_dump_pin(w);
		if (w->param.eapdbtl != HDA_INVALID)
			device_printf(devinfo->dev, "           EAPD: 0x%08x\n",
			    w->param.eapdbtl);
		if (HDA_PARAM_AUDIO_WIDGET_CAP_OUT_AMP(w->param.widget_cap) &&
		    w->param.outamp_cap != 0)
			hdaa_dump_amp(devinfo->dev, w->param.outamp_cap, "Output");
		if (HDA_PARAM_AUDIO_WIDGET_CAP_IN_AMP(w->param.widget_cap) &&
		    w->param.inamp_cap != 0)
			hdaa_dump_amp(devinfo->dev, w->param.inamp_cap, " Input");
		if (w->nconns > 0)
			device_printf(devinfo->dev, "    Connections: %d\n", w->nconns);
		for (j = 0; j < w->nconns; j++) {
			cw = hdaa_widget_get(devinfo, w->conns[j]);
			device_printf(devinfo->dev, "          + %s<- nid=%d [%s]",
			    (w->connsenable[j] == 0)?"[DISABLED] ":"",
			    w->conns[j], (cw == NULL) ? "GHOST!" : cw->name);
			if (cw == NULL)
				printf(" [UNKNOWN]");
			else if (cw->enable == 0)
				printf(" [DISABLED]");
			if (w->nconns > 1 && w->selconn == j && w->type !=
			    HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_MIXER)
				printf(" (selected)");
			printf("\n");
		}
	}

}

static void
hdaa_dump_dst_nid(struct hdaa_pcm_devinfo *pdevinfo, nid_t nid, int depth)
{
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	struct hdaa_widget *w, *cw;
	char buf[64];
	int i;

	if (depth > HDA_PARSE_MAXDEPTH)
		return;

	w = hdaa_widget_get(devinfo, nid);
	if (w == NULL || w->enable == 0)
		return;

	if (depth == 0)
		device_printf(pdevinfo->dev, "%*s", 4, "");
	else
		device_printf(pdevinfo->dev, "%*s  + <- ", 4 + (depth - 1) * 7, "");
	printf("nid=%d [%s]", w->nid, w->name);

	if (depth > 0) {
		if (w->ossmask == 0) {
			printf("\n");
			return;
		}
		printf(" [src: %s]", 
		    hdaa_audio_ctl_ossmixer_mask2allname(
			w->ossmask, buf, sizeof(buf)));
		if (w->ossdev >= 0) {
			printf("\n");
			return;
		}
	}
	printf("\n");

	for (i = 0; i < w->nconns; i++) {
		if (w->connsenable[i] == 0)
			continue;
		cw = hdaa_widget_get(devinfo, w->conns[i]);
		if (cw == NULL || cw->enable == 0 || cw->bindas == -1)
			continue;
		hdaa_dump_dst_nid(pdevinfo, w->conns[i], depth + 1);
	}

}

static void
hdaa_dump_dac(struct hdaa_pcm_devinfo *pdevinfo)
{
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	struct hdaa_audio_as *as;
	struct hdaa_widget *w;
	nid_t *nids;
	int chid, i;

	if (pdevinfo->playas < 0)
		return;

	device_printf(pdevinfo->dev, "Playback:\n");

	chid = devinfo->as[pdevinfo->playas].chans[0];
	hdaa_dump_audio_formats(pdevinfo->dev,
	    devinfo->chans[chid].supp_stream_formats,
	    devinfo->chans[chid].supp_pcm_size_rate);
	for (i = 0; i < devinfo->as[pdevinfo->playas].num_chans; i++) {
		chid = devinfo->as[pdevinfo->playas].chans[i];
		device_printf(pdevinfo->dev, "            DAC:");
		for (nids = devinfo->chans[chid].io; *nids != -1; nids++)
			printf(" %d", *nids);
		printf("\n");
	}

	as = &devinfo->as[pdevinfo->playas];
	for (i = 0; i < 16; i++) {
		if (as->pins[i] <= 0)
			continue;
		w = hdaa_widget_get(devinfo, as->pins[i]);
		if (w == NULL || w->enable == 0)
			continue;
		device_printf(pdevinfo->dev, "\n");
		hdaa_dump_dst_nid(pdevinfo, as->pins[i], 0);
	}
	device_printf(pdevinfo->dev, "\n");
}

static void
hdaa_dump_adc(struct hdaa_pcm_devinfo *pdevinfo)
{
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	struct hdaa_widget *w;
	nid_t *nids;
	int chid, i;

	if (pdevinfo->recas < 0)
		return;

	device_printf(pdevinfo->dev, "Record:\n");

	chid = devinfo->as[pdevinfo->recas].chans[0];
	hdaa_dump_audio_formats(pdevinfo->dev,
	    devinfo->chans[chid].supp_stream_formats,
	    devinfo->chans[chid].supp_pcm_size_rate);
	for (i = 0; i < devinfo->as[pdevinfo->recas].num_chans; i++) {
		chid = devinfo->as[pdevinfo->recas].chans[i];
		device_printf(pdevinfo->dev, "            ADC:");
		for (nids = devinfo->chans[chid].io; *nids != -1; nids++)
			printf(" %d", *nids);
		printf("\n");
	}

	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_AUDIO_INPUT)
			continue;
		if (w->bindas != pdevinfo->recas)
			continue;
		device_printf(pdevinfo->dev, "\n");
		hdaa_dump_dst_nid(pdevinfo, i, 0);
	}
	device_printf(pdevinfo->dev, "\n");
}

static void
hdaa_dump_mix(struct hdaa_pcm_devinfo *pdevinfo)
{
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	struct hdaa_widget *w;
	int i;
	int printed = 0;

	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0)
			continue;
		if (w->ossdev != SOUND_MIXER_IMIX)
			continue;
		if (w->bindas != pdevinfo->recas)
			continue;
		if (printed == 0) {
			printed = 1;
			device_printf(pdevinfo->dev, "Input Mix:\n");
		}
		device_printf(pdevinfo->dev, "\n");
		hdaa_dump_dst_nid(pdevinfo, i, 0);
	}
	if (printed)
		device_printf(pdevinfo->dev, "\n");
}

static void
hdaa_pindump(device_t dev)
{
	struct hdaa_devinfo *devinfo = device_get_softc(dev);
	struct hdaa_widget *w;
	uint32_t res, pincap, delay;
	int i;

	device_printf(dev, "Dumping AFG pins:\n");
	device_printf(dev, "nid   0x    as seq "
	    "device       conn  jack    loc        color   misc\n");
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->type !=
		    HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		hdaa_dump_pin_config(w, w->wclass.pin.config);
		pincap = w->wclass.pin.cap;
		device_printf(dev, "    Caps: %2s %3s %2s %4s %4s",
		    HDA_PARAM_PIN_CAP_INPUT_CAP(pincap)?"IN":"",
		    HDA_PARAM_PIN_CAP_OUTPUT_CAP(pincap)?"OUT":"",
		    HDA_PARAM_PIN_CAP_HEADPHONE_CAP(pincap)?"HP":"",
		    HDA_PARAM_PIN_CAP_EAPD_CAP(pincap)?"EAPD":"",
		    HDA_PARAM_PIN_CAP_VREF_CTRL(pincap)?"VREF":"");
		if (HDA_PARAM_PIN_CAP_IMP_SENSE_CAP(pincap) ||
		    HDA_PARAM_PIN_CAP_PRESENCE_DETECT_CAP(pincap)) {
			if (HDA_PARAM_PIN_CAP_TRIGGER_REQD(pincap)) {
				delay = 0;
				hda_command(dev,
				    HDA_CMD_SET_PIN_SENSE(0, w->nid, 0));
				do {
					res = hda_command(dev,
					    HDA_CMD_GET_PIN_SENSE(0, w->nid));
					if (res != 0x7fffffff && res != 0xffffffff)
						break;
					DELAY(10);
				} while (++delay < 10000);
			} else {
				delay = 0;
				res = hda_command(dev, HDA_CMD_GET_PIN_SENSE(0,
				    w->nid));
			}
			printf(" Sense: 0x%08x (%sconnected%s)", res,
			    (res & HDA_CMD_GET_PIN_SENSE_PRESENCE_DETECT) ?
			     "" : "dis",
			    (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(w->param.widget_cap) &&
			     (res & HDA_CMD_GET_PIN_SENSE_ELD_VALID)) ?
			      ", ELD valid" : "");
			if (delay > 0)
				printf(" delay %dus", delay * 10);
		}
		printf("\n");
	}
	device_printf(dev,
	    "NumGPIO=%d NumGPO=%d NumGPI=%d GPIWake=%d GPIUnsol=%d\n",
	    HDA_PARAM_GPIO_COUNT_NUM_GPIO(devinfo->gpio_cap),
	    HDA_PARAM_GPIO_COUNT_NUM_GPO(devinfo->gpio_cap),
	    HDA_PARAM_GPIO_COUNT_NUM_GPI(devinfo->gpio_cap),
	    HDA_PARAM_GPIO_COUNT_GPI_WAKE(devinfo->gpio_cap),
	    HDA_PARAM_GPIO_COUNT_GPI_UNSOL(devinfo->gpio_cap));
	hdaa_dump_gpi(devinfo);
	hdaa_dump_gpio(devinfo);
	hdaa_dump_gpo(devinfo);
}

static void
hdaa_configure(device_t dev)
{
	struct hdaa_devinfo *devinfo = device_get_softc(dev);
	struct hdaa_audio_ctl *ctl;
	int i;

	HDA_BOOTHVERBOSE(
		device_printf(dev, "Applying built-in patches...\n");
	);
	hdaa_patch(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Applying local patches...\n");
	);
	hdaa_local_patch(devinfo);
	hdaa_audio_postprocess(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Parsing Ctls...\n");
	);
	hdaa_audio_ctl_parse(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Disabling nonaudio...\n");
	);
	hdaa_audio_disable_nonaudio(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Disabling useless...\n");
	);
	hdaa_audio_disable_useless(devinfo);
	HDA_BOOTVERBOSE(
		device_printf(dev, "Patched pins configuration:\n");
		hdaa_dump_pin_configs(devinfo);
	);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Parsing pin associations...\n");
	);
	hdaa_audio_as_parse(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Building AFG tree...\n");
	);
	hdaa_audio_build_tree(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Disabling unassociated "
		    "widgets...\n");
	);
	hdaa_audio_disable_unas(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Disabling nonselected "
		    "inputs...\n");
	);
	hdaa_audio_disable_notselected(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Disabling useless...\n");
	);
	hdaa_audio_disable_useless(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Disabling "
		    "crossassociatement connections...\n");
	);
	hdaa_audio_disable_crossas(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Disabling useless...\n");
	);
	hdaa_audio_disable_useless(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Binding associations to channels...\n");
	);
	hdaa_audio_bind_as(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Assigning names to signal sources...\n");
	);
	hdaa_audio_assign_names(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Preparing PCM devices...\n");
	);
	hdaa_prepare_pcms(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Assigning mixers to the tree...\n");
	);
	hdaa_audio_assign_mixers(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Preparing pin controls...\n");
	);
	hdaa_audio_prepare_pin_ctrl(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "AFG commit...\n");
	);
	hdaa_audio_commit(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Applying direct built-in patches...\n");
	);
	hdaa_patch_direct(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Pin sense init...\n");
	);
	hdaa_sense_init(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Creating PCM devices...\n");
	);
	hdaa_create_pcms(devinfo);

	HDA_BOOTVERBOSE(
		if (devinfo->quirks != 0) {
			device_printf(dev, "FG config/quirks:");
			for (i = 0; i < nitems(hdaa_quirks_tab); i++) {
				if ((devinfo->quirks &
				    hdaa_quirks_tab[i].value) ==
				    hdaa_quirks_tab[i].value)
					printf(" %s", hdaa_quirks_tab[i].key);
			}
			printf("\n");
		}
	);

	HDA_BOOTHVERBOSE(
		device_printf(dev, "\n");
		device_printf(dev, "+-----------+\n");
		device_printf(dev, "| HDA NODES |\n");
		device_printf(dev, "+-----------+\n");
		hdaa_dump_nodes(devinfo);

		device_printf(dev, "\n");
		device_printf(dev, "+----------------+\n");
		device_printf(dev, "| HDA AMPLIFIERS |\n");
		device_printf(dev, "+----------------+\n");
		device_printf(dev, "\n");
		i = 0;
		while ((ctl = hdaa_audio_ctl_each(devinfo, &i)) != NULL) {
			device_printf(dev, "%3d: nid %3d %s (%s) index %d", i,
			    (ctl->widget != NULL) ? ctl->widget->nid : -1,
			    (ctl->ndir == HDAA_CTL_IN)?"in ":"out",
			    (ctl->dir == HDAA_CTL_IN)?"in ":"out",
			    ctl->index);
			if (ctl->childwidget != NULL)
				printf(" cnid %3d", ctl->childwidget->nid);
			else
				printf("         ");
			printf(" ossmask=0x%08x\n",
			    ctl->ossmask);
			device_printf(dev, 
			    "       mute: %d step: %3d size: %3d off: %3d%s\n",
			    ctl->mute, ctl->step, ctl->size, ctl->offset,
			    (ctl->enable == 0) ? " [DISABLED]" : 
			    ((ctl->ossmask == 0) ? " [UNUSED]" : ""));
		}
		device_printf(dev, "\n");
	);
}

static void
hdaa_unconfigure(device_t dev)
{
	struct hdaa_devinfo *devinfo = device_get_softc(dev);
	struct hdaa_widget *w;
	int i, j;

	HDA_BOOTHVERBOSE(
		device_printf(dev, "Pin sense deinit...\n");
	);
	hdaa_sense_deinit(devinfo);
	free(devinfo->ctl, M_HDAA);
	devinfo->ctl = NULL;
	devinfo->ctlcnt = 0;
	free(devinfo->as, M_HDAA);
	devinfo->as = NULL;
	devinfo->ascnt = 0;
	free(devinfo->devs, M_HDAA);
	devinfo->devs = NULL;
	devinfo->num_devs = 0;
	free(devinfo->chans, M_HDAA);
	devinfo->chans = NULL;
	devinfo->num_chans = 0;
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL)
			continue;
		w->enable = 1;
		w->selconn = -1;
		w->pflags = 0;
		w->bindas = -1;
		w->bindseqmask = 0;
		w->ossdev = -1;
		w->ossmask = 0;
		for (j = 0; j < w->nconns; j++)
			w->connsenable[j] = 1;
		if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			w->wclass.pin.config = w->wclass.pin.newconf;
		if (w->eld != NULL) {
			w->eld_len = 0;
			free(w->eld, M_HDAA);
			w->eld = NULL;
		}
	}
}

static int
hdaa_sysctl_gpi_state(SYSCTL_HANDLER_ARGS)
{
	struct hdaa_devinfo *devinfo = oidp->oid_arg1;
	device_t dev = devinfo->dev;
	char buf[256];
	int n = 0, i, numgpi;
	uint32_t data = 0;

	buf[0] = 0;
	hdaa_lock(devinfo);
	numgpi = HDA_PARAM_GPIO_COUNT_NUM_GPI(devinfo->gpio_cap);
	if (numgpi > 0) {
		data = hda_command(dev,
		    HDA_CMD_GET_GPI_DATA(0, devinfo->nid));
	}
	hdaa_unlock(devinfo);
	for (i = 0; i < numgpi; i++) {
		n += snprintf(buf + n, sizeof(buf) - n, "%s%d=%d",
		    n != 0 ? " " : "", i, ((data >> i) & 1));
	}
	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}

static int
hdaa_sysctl_gpio_state(SYSCTL_HANDLER_ARGS)
{
	struct hdaa_devinfo *devinfo = oidp->oid_arg1;
	device_t dev = devinfo->dev;
	char buf[256];
	int n = 0, i, numgpio;
	uint32_t data = 0, enable = 0, dir = 0;

	buf[0] = 0;
	hdaa_lock(devinfo);
	numgpio = HDA_PARAM_GPIO_COUNT_NUM_GPIO(devinfo->gpio_cap);
	if (numgpio > 0) {
		data = hda_command(dev,
		    HDA_CMD_GET_GPIO_DATA(0, devinfo->nid));
		enable = hda_command(dev,
		    HDA_CMD_GET_GPIO_ENABLE_MASK(0, devinfo->nid));
		dir = hda_command(dev,
		    HDA_CMD_GET_GPIO_DIRECTION(0, devinfo->nid));
	}
	hdaa_unlock(devinfo);
	for (i = 0; i < numgpio; i++) {
		n += snprintf(buf + n, sizeof(buf) - n, "%s%d=",
		    n != 0 ? " " : "", i);
		if ((enable & (1 << i)) == 0) {
			n += snprintf(buf + n, sizeof(buf) - n, "disabled");
			continue;
		}
		n += snprintf(buf + n, sizeof(buf) - n, "%sput(%d)",
		    ((dir >> i) & 1) ? "out" : "in", ((data >> i) & 1));
	}
	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}

static int
hdaa_sysctl_gpio_config(SYSCTL_HANDLER_ARGS)
{
	struct hdaa_devinfo *devinfo = oidp->oid_arg1;
	char buf[256];
	int error, n = 0, i, numgpio;
	uint32_t gpio, x;

	gpio = devinfo->newgpio;
	numgpio = HDA_PARAM_GPIO_COUNT_NUM_GPIO(devinfo->gpio_cap);
	buf[0] = 0;
	for (i = 0; i < numgpio; i++) {
		x = (gpio & HDAA_GPIO_MASK(i)) >> HDAA_GPIO_SHIFT(i);
		n += snprintf(buf + n, sizeof(buf) - n, "%s%d=%s",
		    n != 0 ? " " : "", i, HDA_GPIO_ACTIONS[x]);
	}
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (strncmp(buf, "0x", 2) == 0)
		gpio = strtol(buf + 2, NULL, 16);
	else
		gpio = hdaa_gpio_patch(gpio, buf);
	hdaa_lock(devinfo);
	devinfo->newgpio = devinfo->gpio = gpio;
	hdaa_gpio_commit(devinfo);
	hdaa_unlock(devinfo);
	return (0);
}

static int
hdaa_sysctl_gpo_state(SYSCTL_HANDLER_ARGS)
{
	struct hdaa_devinfo *devinfo = oidp->oid_arg1;
	device_t dev = devinfo->dev;
	char buf[256];
	int n = 0, i, numgpo;
	uint32_t data = 0;

	buf[0] = 0;
	hdaa_lock(devinfo);
	numgpo = HDA_PARAM_GPIO_COUNT_NUM_GPO(devinfo->gpio_cap);
	if (numgpo > 0) {
		data = hda_command(dev,
		    HDA_CMD_GET_GPO_DATA(0, devinfo->nid));
	}
	hdaa_unlock(devinfo);
	for (i = 0; i < numgpo; i++) {
		n += snprintf(buf + n, sizeof(buf) - n, "%s%d=%d",
		    n != 0 ? " " : "", i, ((data >> i) & 1));
	}
	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}

static int
hdaa_sysctl_gpo_config(SYSCTL_HANDLER_ARGS)
{
	struct hdaa_devinfo *devinfo = oidp->oid_arg1;
	char buf[256];
	int error, n = 0, i, numgpo;
	uint32_t gpo, x;

	gpo = devinfo->newgpo;
	numgpo = HDA_PARAM_GPIO_COUNT_NUM_GPO(devinfo->gpio_cap);
	buf[0] = 0;
	for (i = 0; i < numgpo; i++) {
		x = (gpo & HDAA_GPIO_MASK(i)) >> HDAA_GPIO_SHIFT(i);
		n += snprintf(buf + n, sizeof(buf) - n, "%s%d=%s",
		    n != 0 ? " " : "", i, HDA_GPIO_ACTIONS[x]);
	}
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (strncmp(buf, "0x", 2) == 0)
		gpo = strtol(buf + 2, NULL, 16);
	else
		gpo = hdaa_gpio_patch(gpo, buf);
	hdaa_lock(devinfo);
	devinfo->newgpo = devinfo->gpo = gpo;
	hdaa_gpo_commit(devinfo);
	hdaa_unlock(devinfo);
	return (0);
}

static int
hdaa_sysctl_reconfig(SYSCTL_HANDLER_ARGS)
{
	device_t dev;
	struct hdaa_devinfo *devinfo;
	int error, val;

	dev = oidp->oid_arg1;
	devinfo = device_get_softc(dev);
	if (devinfo == NULL)
		return (EINVAL);
	val = 0;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL || val == 0)
		return (error);

	HDA_BOOTHVERBOSE(
		device_printf(dev, "Reconfiguration...\n");
	);
	if ((error = device_delete_children(dev)) != 0)
		return (error);
	hdaa_lock(devinfo);
	hdaa_unconfigure(dev);
	hdaa_configure(dev);
	hdaa_unlock(devinfo);
	bus_generic_attach(dev);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Reconfiguration done\n");
	);
	return (0);
}

static int
hdaa_suspend(device_t dev)
{
	struct hdaa_devinfo *devinfo = device_get_softc(dev);
	int i;

	HDA_BOOTHVERBOSE(
		device_printf(dev, "Suspend...\n");
	);
	hdaa_lock(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Stop streams...\n");
	);
	for (i = 0; i < devinfo->num_chans; i++) {
		if (devinfo->chans[i].flags & HDAA_CHN_RUNNING) {
			devinfo->chans[i].flags |= HDAA_CHN_SUSPEND;
			hdaa_channel_stop(&devinfo->chans[i]);
		}
	}
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Power down FG"
		    " nid=%d to the D3 state...\n",
		    devinfo->nid);
	);
	hda_command(devinfo->dev,
	    HDA_CMD_SET_POWER_STATE(0,
	    devinfo->nid, HDA_CMD_POWER_STATE_D3));
	callout_stop(&devinfo->poll_jack);
	hdaa_unlock(devinfo);
	callout_drain(&devinfo->poll_jack);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Suspend done\n");
	);
	return (0);
}

static int
hdaa_resume(device_t dev)
{
	struct hdaa_devinfo *devinfo = device_get_softc(dev);
	int i;

	HDA_BOOTHVERBOSE(
		device_printf(dev, "Resume...\n");
	);
	hdaa_lock(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Power up audio FG nid=%d...\n",
		    devinfo->nid);
	);
	hdaa_powerup(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "AFG commit...\n");
	);
	hdaa_audio_commit(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Applying direct built-in patches...\n");
	);
	hdaa_patch_direct(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Pin sense init...\n");
	);
	hdaa_sense_init(devinfo);

	hdaa_unlock(devinfo);
	for (i = 0; i < devinfo->num_devs; i++) {
		struct hdaa_pcm_devinfo *pdevinfo = &devinfo->devs[i];
		HDA_BOOTHVERBOSE(
			device_printf(pdevinfo->dev,
			    "OSS mixer reinitialization...\n");
		);
		if (mixer_reinit(pdevinfo->dev) == -1)
			device_printf(pdevinfo->dev,
			    "unable to reinitialize the mixer\n");
	}
	hdaa_lock(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Start streams...\n");
	);
	for (i = 0; i < devinfo->num_chans; i++) {
		if (devinfo->chans[i].flags & HDAA_CHN_SUSPEND) {
			devinfo->chans[i].flags &= ~HDAA_CHN_SUSPEND;
			hdaa_channel_start(&devinfo->chans[i]);
		}
	}
	hdaa_unlock(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Resume done\n");
	);
	return (0);
}

static int
hdaa_probe(device_t dev)
{
	const char *pdesc;
	char buf[128];

	if (hda_get_node_type(dev) != HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE_AUDIO)
		return (ENXIO);
	pdesc = device_get_desc(device_get_parent(dev));
	snprintf(buf, sizeof(buf), "%.*s Audio Function Group",
	    (int)(strlen(pdesc) - 10), pdesc);
	device_set_desc_copy(dev, buf);
	return (BUS_PROBE_DEFAULT);
}

static int
hdaa_attach(device_t dev)
{
	struct hdaa_devinfo *devinfo = device_get_softc(dev);
	uint32_t res;
	nid_t nid = hda_get_node_id(dev);

	devinfo->dev = dev;
	devinfo->lock = HDAC_GET_MTX(device_get_parent(dev), dev);
	devinfo->nid = nid;
	devinfo->newquirks = -1;
	devinfo->newgpio = -1;
	devinfo->newgpo = -1;
	callout_init(&devinfo->poll_jack, 1);
	devinfo->poll_ival = hz;

	hdaa_lock(devinfo);
	res = hda_command(dev,
	    HDA_CMD_GET_PARAMETER(0 , nid, HDA_PARAM_SUB_NODE_COUNT));
	hdaa_unlock(devinfo);

	devinfo->nodecnt = HDA_PARAM_SUB_NODE_COUNT_TOTAL(res);
	devinfo->startnode = HDA_PARAM_SUB_NODE_COUNT_START(res);
	devinfo->endnode = devinfo->startnode + devinfo->nodecnt;

	HDA_BOOTVERBOSE(
		device_printf(dev, "Subsystem ID: 0x%08x\n",
		    hda_get_subsystem_id(dev));
	);
	HDA_BOOTHVERBOSE(
		device_printf(dev,
		    "Audio Function Group at nid=%d: %d subnodes %d-%d\n",
		    nid, devinfo->nodecnt,
		    devinfo->startnode, devinfo->endnode - 1);
	);

	if (devinfo->nodecnt > 0)
		devinfo->widget = (struct hdaa_widget *)malloc(
		    sizeof(*(devinfo->widget)) * devinfo->nodecnt, M_HDAA,
		    M_WAITOK | M_ZERO);
	else
		devinfo->widget = NULL;

	hdaa_lock(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Powering up...\n");
	);
	hdaa_powerup(devinfo);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Parsing audio FG...\n");
	);
	hdaa_audio_parse(devinfo);
	HDA_BOOTVERBOSE(
		device_printf(dev, "Original pins configuration:\n");
		hdaa_dump_pin_configs(devinfo);
	);
	hdaa_configure(dev);
	hdaa_unlock(devinfo);

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "config", CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    &devinfo->newquirks, 0, hdaa_sysctl_quirks, "A",
	    "Configuration options");
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "gpi_state", CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    devinfo, 0, hdaa_sysctl_gpi_state, "A", "GPI state");
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "gpio_state", CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    devinfo, 0, hdaa_sysctl_gpio_state, "A", "GPIO state");
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "gpio_config", CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    devinfo, 0, hdaa_sysctl_gpio_config, "A", "GPIO configuration");
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "gpo_state", CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    devinfo, 0, hdaa_sysctl_gpo_state, "A", "GPO state");
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "gpo_config", CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    devinfo, 0, hdaa_sysctl_gpo_config, "A", "GPO configuration");
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "reconfig", CTLTYPE_INT | CTLFLAG_RW,
	    dev, 0, hdaa_sysctl_reconfig, "I", "Reprocess configuration");
	bus_generic_attach(dev);
	return (0);
}

static int
hdaa_detach(device_t dev)
{
	struct hdaa_devinfo *devinfo = device_get_softc(dev);
	int error;

	if ((error = device_delete_children(dev)) != 0)
		return (error);

	hdaa_lock(devinfo);
	hdaa_unconfigure(dev);
	devinfo->poll_ival = 0;
	callout_stop(&devinfo->poll_jack);
	hdaa_unlock(devinfo);
	callout_drain(&devinfo->poll_jack);

	free(devinfo->widget, M_HDAA);
	return (0);
}

static int
hdaa_print_child(device_t dev, device_t child)
{
	struct hdaa_devinfo *devinfo = device_get_softc(dev);
	struct hdaa_pcm_devinfo *pdevinfo =
	    (struct hdaa_pcm_devinfo *)device_get_ivars(child);
	struct hdaa_audio_as *as;
	int retval, first = 1, i;

	retval = bus_print_child_header(dev, child);
	retval += printf(" at nid ");
	if (pdevinfo->playas >= 0) {
		as = &devinfo->as[pdevinfo->playas];
		for (i = 0; i < 16; i++) {
			if (as->pins[i] <= 0)
				continue;
			retval += printf("%s%d", first ? "" : ",", as->pins[i]);
			first = 0;
		}
	}
	if (pdevinfo->recas >= 0) {
		if (pdevinfo->playas >= 0) {
			retval += printf(" and ");
			first = 1;
		}
		as = &devinfo->as[pdevinfo->recas];
		for (i = 0; i < 16; i++) {
			if (as->pins[i] <= 0)
				continue;
			retval += printf("%s%d", first ? "" : ",", as->pins[i]);
			first = 0;
		}
	}
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

static int
hdaa_child_location_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{
	struct hdaa_devinfo *devinfo = device_get_softc(dev);
	struct hdaa_pcm_devinfo *pdevinfo =
	    (struct hdaa_pcm_devinfo *)device_get_ivars(child);
	struct hdaa_audio_as *as;
	int first = 1, i, len = 0;

	len += snprintf(buf + len, buflen - len, "nid=");
	if (pdevinfo->playas >= 0) {
		as = &devinfo->as[pdevinfo->playas];
		for (i = 0; i < 16; i++) {
			if (as->pins[i] <= 0)
				continue;
			len += snprintf(buf + len, buflen - len,
			    "%s%d", first ? "" : ",", as->pins[i]);
			first = 0;
		}
	}
	if (pdevinfo->recas >= 0) {
		as = &devinfo->as[pdevinfo->recas];
		for (i = 0; i < 16; i++) {
			if (as->pins[i] <= 0)
				continue;
			len += snprintf(buf + len, buflen - len,
			    "%s%d", first ? "" : ",", as->pins[i]);
			first = 0;
		}
	}
	return (0);
}

static void
hdaa_stream_intr(device_t dev, int dir, int stream)
{
	struct hdaa_devinfo *devinfo = device_get_softc(dev);
	struct hdaa_chan *ch;
	int i;

	for (i = 0; i < devinfo->num_chans; i++) {
		ch = &devinfo->chans[i];
		if (!(ch->flags & HDAA_CHN_RUNNING))
			continue;
		if (ch->dir == ((dir == 1) ? PCMDIR_PLAY : PCMDIR_REC) &&
		    ch->sid == stream) {
			hdaa_unlock(devinfo);
			chn_intr(ch->c);
			hdaa_lock(devinfo);
		}
	}
}

static void
hdaa_unsol_intr(device_t dev, uint32_t resp)
{
	struct hdaa_devinfo *devinfo = device_get_softc(dev);
	struct hdaa_widget *w;
	int i, tag, flags;

	HDA_BOOTHVERBOSE(
		device_printf(dev, "Unsolicited response %08x\n", resp);
	);
	tag = resp >> 26;
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL || w->enable == 0 || w->type !=
		    HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		if (w->unsol != tag)
			continue;
		if (HDA_PARAM_PIN_CAP_DP(w->wclass.pin.cap) ||
		    HDA_PARAM_PIN_CAP_HDMI(w->wclass.pin.cap))
			flags = resp & 0x03;
		else
			flags = 0x01;
		if (flags & 0x01)
			hdaa_presence_handler(w);
		if (flags & 0x02)
			hdaa_eld_handler(w);
	}
}

static device_method_t hdaa_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		hdaa_probe),
	DEVMETHOD(device_attach,	hdaa_attach),
	DEVMETHOD(device_detach,	hdaa_detach),
	DEVMETHOD(device_suspend,	hdaa_suspend),
	DEVMETHOD(device_resume,	hdaa_resume),
	/* Bus interface */
	DEVMETHOD(bus_print_child,	hdaa_print_child),
	DEVMETHOD(bus_child_location_str, hdaa_child_location_str),
	DEVMETHOD(hdac_stream_intr,	hdaa_stream_intr),
	DEVMETHOD(hdac_unsol_intr,	hdaa_unsol_intr),
	DEVMETHOD(hdac_pindump,		hdaa_pindump),
	DEVMETHOD_END
};

static driver_t hdaa_driver = {
	"hdaa",
	hdaa_methods,
	sizeof(struct hdaa_devinfo),
};

static devclass_t hdaa_devclass;

DRIVER_MODULE(snd_hda, hdacc, hdaa_driver, hdaa_devclass, NULL, NULL);

static void
hdaa_chan_formula(struct hdaa_devinfo *devinfo, int asid,
    char *buf, int buflen)
{
	struct hdaa_audio_as *as;
	int c;

	as = &devinfo->as[asid];
	c = devinfo->chans[as->chans[0]].channels;
	if (c == 1)
		snprintf(buf, buflen, "mono");
	else if (c == 2) {
		if (as->hpredir < 0)
			buf[0] = 0;
		else
			snprintf(buf, buflen, "2.0");
	} else if (as->pinset == 0x0003)
		snprintf(buf, buflen, "3.1");
	else if (as->pinset == 0x0005 || as->pinset == 0x0011)
		snprintf(buf, buflen, "4.0");
	else if (as->pinset == 0x0007 || as->pinset == 0x0013)
		snprintf(buf, buflen, "5.1");
	else if (as->pinset == 0x0017)
		snprintf(buf, buflen, "7.1");
	else
		snprintf(buf, buflen, "%dch", c);
	if (as->hpredir >= 0)
		strlcat(buf, "+HP", buflen);
}

static int
hdaa_chan_type(struct hdaa_devinfo *devinfo, int asid)
{
	struct hdaa_audio_as *as;
	struct hdaa_widget *w;
	int i, t = -1, t1;

	as = &devinfo->as[asid];
	for (i = 0; i < 16; i++) {
		w = hdaa_widget_get(devinfo, as->pins[i]);
		if (w == NULL || w->enable == 0 || w->type !=
		    HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
			continue;
		t1 = HDA_CONFIG_DEFAULTCONF_DEVICE(w->wclass.pin.config);
		if (t == -1)
			t = t1;
		else if (t != t1) {
			t = -2;
			break;
		}
	}
	return (t);
}

static int
hdaa_sysctl_32bit(SYSCTL_HANDLER_ARGS)
{
	struct hdaa_audio_as *as = (struct hdaa_audio_as *)oidp->oid_arg1;
	struct hdaa_pcm_devinfo *pdevinfo = as->pdevinfo;
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	struct hdaa_chan *ch;
	int error, val, i;
	uint32_t pcmcap;

	ch = &devinfo->chans[as->chans[0]];
	val = (ch->bit32 == 4) ? 32 : ((ch->bit32 == 3) ? 24 :
	    ((ch->bit32 == 2) ? 20 : 0));
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	pcmcap = ch->supp_pcm_size_rate;
	if (val == 32 && HDA_PARAM_SUPP_PCM_SIZE_RATE_32BIT(pcmcap))
		ch->bit32 = 4;
	else if (val == 24 && HDA_PARAM_SUPP_PCM_SIZE_RATE_24BIT(pcmcap))
		ch->bit32 = 3;
	else if (val == 20 && HDA_PARAM_SUPP_PCM_SIZE_RATE_20BIT(pcmcap))
		ch->bit32 = 2;
	else
		return (EINVAL);
	for (i = 1; i < as->num_chans; i++)
		devinfo->chans[as->chans[i]].bit32 = ch->bit32;
	return (0);
}

static int
hdaa_pcm_probe(device_t dev)
{
	struct hdaa_pcm_devinfo *pdevinfo =
	    (struct hdaa_pcm_devinfo *)device_get_ivars(dev);
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	const char *pdesc;
	char chans1[8], chans2[8];
	char buf[128];
	int loc1, loc2, t1, t2;

	if (pdevinfo->playas >= 0)
		loc1 = devinfo->as[pdevinfo->playas].location;
	else
		loc1 = devinfo->as[pdevinfo->recas].location;
	if (pdevinfo->recas >= 0)
		loc2 = devinfo->as[pdevinfo->recas].location;
	else
		loc2 = loc1;
	if (loc1 != loc2)
		loc1 = -2;
	if (loc1 >= 0 && HDA_LOCS[loc1][0] == '0')
		loc1 = -2;
	chans1[0] = 0;
	chans2[0] = 0;
	t1 = t2 = -1;
	if (pdevinfo->playas >= 0) {
		hdaa_chan_formula(devinfo, pdevinfo->playas,
		    chans1, sizeof(chans1));
		t1 = hdaa_chan_type(devinfo, pdevinfo->playas);
	}
	if (pdevinfo->recas >= 0) {
		hdaa_chan_formula(devinfo, pdevinfo->recas,
		    chans2, sizeof(chans2));
		t2 = hdaa_chan_type(devinfo, pdevinfo->recas);
	}
	if (chans1[0] != 0 || chans2[0] != 0) {
		if (chans1[0] == 0 && pdevinfo->playas >= 0)
			snprintf(chans1, sizeof(chans1), "2.0");
		else if (chans2[0] == 0 && pdevinfo->recas >= 0)
			snprintf(chans2, sizeof(chans2), "2.0");
		if (strcmp(chans1, chans2) == 0)
			chans2[0] = 0;
	}
	if (t1 == -1)
		t1 = t2;
	else if (t2 == -1)
		t2 = t1;
	if (t1 != t2)
		t1 = -2;
	if (pdevinfo->digital)
		t1 = -2;
	pdesc = device_get_desc(device_get_parent(dev));
	snprintf(buf, sizeof(buf), "%.*s (%s%s%s%s%s%s%s%s%s)",
	    (int)(strlen(pdesc) - 21), pdesc,
	    loc1 >= 0 ? HDA_LOCS[loc1] : "", loc1 >= 0 ? " " : "",
	    (pdevinfo->digital == 0x7)?"HDMI/DP":
	    ((pdevinfo->digital == 0x5)?"DisplayPort":
	    ((pdevinfo->digital == 0x3)?"HDMI":
	    ((pdevinfo->digital)?"Digital":"Analog"))),
	    chans1[0] ? " " : "", chans1,
	    chans2[0] ? "/" : "", chans2,
	    t1 >= 0 ? " " : "", t1 >= 0 ? HDA_DEVS[t1] : "");
	device_set_desc_copy(dev, buf);
	return (BUS_PROBE_SPECIFIC);
}

static int
hdaa_pcm_attach(device_t dev)
{
	struct hdaa_pcm_devinfo *pdevinfo =
	    (struct hdaa_pcm_devinfo *)device_get_ivars(dev);
	struct hdaa_devinfo *devinfo = pdevinfo->devinfo;
	struct hdaa_audio_as *as;
	struct snddev_info *d;
	char status[SND_STATUSLEN];
	int i;

	pdevinfo->chan_size = pcm_getbuffersize(dev,
	    HDA_BUFSZ_MIN, HDA_BUFSZ_DEFAULT, HDA_BUFSZ_MAX);

	HDA_BOOTVERBOSE(
		hdaa_dump_dac(pdevinfo);
		hdaa_dump_adc(pdevinfo);
		hdaa_dump_mix(pdevinfo);
		hdaa_dump_ctls(pdevinfo, "Master Volume", SOUND_MASK_VOLUME);
		hdaa_dump_ctls(pdevinfo, "PCM Volume", SOUND_MASK_PCM);
		hdaa_dump_ctls(pdevinfo, "CD Volume", SOUND_MASK_CD);
		hdaa_dump_ctls(pdevinfo, "Microphone Volume", SOUND_MASK_MIC);
		hdaa_dump_ctls(pdevinfo, "Microphone2 Volume", SOUND_MASK_MONITOR);
		hdaa_dump_ctls(pdevinfo, "Line-in Volume", SOUND_MASK_LINE);
		hdaa_dump_ctls(pdevinfo, "Speaker/Beep Volume", SOUND_MASK_SPEAKER);
		hdaa_dump_ctls(pdevinfo, "Recording Level", SOUND_MASK_RECLEV);
		hdaa_dump_ctls(pdevinfo, "Input Mix Level", SOUND_MASK_IMIX);
		hdaa_dump_ctls(pdevinfo, "Input Monitoring Level", SOUND_MASK_IGAIN);
		hdaa_dump_ctls(pdevinfo, NULL, 0);
	);

	if (resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "blocksize", &i) == 0 && i > 0) {
		i &= HDA_BLK_ALIGN;
		if (i < HDA_BLK_MIN)
			i = HDA_BLK_MIN;
		pdevinfo->chan_blkcnt = pdevinfo->chan_size / i;
		i = 0;
		while (pdevinfo->chan_blkcnt >> i)
			i++;
		pdevinfo->chan_blkcnt = 1 << (i - 1);
		if (pdevinfo->chan_blkcnt < HDA_BDL_MIN)
			pdevinfo->chan_blkcnt = HDA_BDL_MIN;
		else if (pdevinfo->chan_blkcnt > HDA_BDL_MAX)
			pdevinfo->chan_blkcnt = HDA_BDL_MAX;
	} else
		pdevinfo->chan_blkcnt = HDA_BDL_DEFAULT;

	/* 
	 * We don't register interrupt handler with snd_setup_intr
	 * in pcm device. Mark pcm device as MPSAFE manually.
	 */
	pcm_setflags(dev, pcm_getflags(dev) | SD_F_MPSAFE);

	HDA_BOOTHVERBOSE(
		device_printf(dev, "OSS mixer initialization...\n");
	);
	if (mixer_init(dev, &hdaa_audio_ctl_ossmixer_class, pdevinfo) != 0)
		device_printf(dev, "Can't register mixer\n");

	HDA_BOOTHVERBOSE(
		device_printf(dev, "Registering PCM channels...\n");
	);
	if (pcm_register(dev, pdevinfo, (pdevinfo->playas >= 0)?1:0,
	    (pdevinfo->recas >= 0)?1:0) != 0)
		device_printf(dev, "Can't register PCM\n");

	pdevinfo->registered++;

	d = device_get_softc(dev);
	if (pdevinfo->playas >= 0) {
		as = &devinfo->as[pdevinfo->playas];
		for (i = 0; i < as->num_chans; i++)
			pcm_addchan(dev, PCMDIR_PLAY, &hdaa_channel_class,
			    &devinfo->chans[as->chans[i]]);
		SYSCTL_ADD_PROC(&d->play_sysctl_ctx,
		    SYSCTL_CHILDREN(d->play_sysctl_tree), OID_AUTO,
		    "32bit", CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
		    as, sizeof(as), hdaa_sysctl_32bit, "I",
		    "Resolution of 32bit samples (20/24/32bit)");
	}
	if (pdevinfo->recas >= 0) {
		as = &devinfo->as[pdevinfo->recas];
		for (i = 0; i < as->num_chans; i++)
			pcm_addchan(dev, PCMDIR_REC, &hdaa_channel_class,
			    &devinfo->chans[as->chans[i]]);
		SYSCTL_ADD_PROC(&d->rec_sysctl_ctx,
		    SYSCTL_CHILDREN(d->rec_sysctl_tree), OID_AUTO,
		    "32bit", CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
		    as, sizeof(as), hdaa_sysctl_32bit, "I",
		    "Resolution of 32bit samples (20/24/32bit)");
		pdevinfo->autorecsrc = 2;
		resource_int_value(device_get_name(dev), device_get_unit(dev),
		    "rec.autosrc", &pdevinfo->autorecsrc);
		SYSCTL_ADD_INT(&d->rec_sysctl_ctx,
		    SYSCTL_CHILDREN(d->rec_sysctl_tree), OID_AUTO,
		    "autosrc", CTLFLAG_RW,
		    &pdevinfo->autorecsrc, 0,
		    "Automatic recording source selection");
	}

	if (pdevinfo->mixer != NULL) {
		hdaa_audio_ctl_set_defaults(pdevinfo);
		hdaa_lock(devinfo);
		if (pdevinfo->playas >= 0) {
			as = &devinfo->as[pdevinfo->playas];
			hdaa_channels_handler(as);
		}
		if (pdevinfo->recas >= 0) {
			as = &devinfo->as[pdevinfo->recas];
			hdaa_autorecsrc_handler(as, NULL);
			hdaa_channels_handler(as);
		}
		hdaa_unlock(devinfo);
	}

	snprintf(status, SND_STATUSLEN, "on %s %s",
	    device_get_nameunit(device_get_parent(dev)),
	    PCM_KLDSTRING(snd_hda));
	pcm_setstatus(dev, status);

	return (0);
}

static int
hdaa_pcm_detach(device_t dev)
{
	struct hdaa_pcm_devinfo *pdevinfo =
	    (struct hdaa_pcm_devinfo *)device_get_ivars(dev);
	int err;

	if (pdevinfo->registered > 0) {
		err = pcm_unregister(dev);
		if (err != 0)
			return (err);
	}

	return (0);
}

static device_method_t hdaa_pcm_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		hdaa_pcm_probe),
	DEVMETHOD(device_attach,	hdaa_pcm_attach),
	DEVMETHOD(device_detach,	hdaa_pcm_detach),
	DEVMETHOD_END
};

static driver_t hdaa_pcm_driver = {
	"pcm",
	hdaa_pcm_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_hda_pcm, hdaa, hdaa_pcm_driver, pcm_devclass, NULL, NULL);
MODULE_DEPEND(snd_hda, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_hda, 1);
