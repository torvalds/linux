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
 *
 * $FreeBSD$
 */

/*
 * Intel High Definition Audio (Audio function quirks) driver for FreeBSD.
 */

#ifndef _HDAA_QUIRKS_H_
#define _HDAA_QUIRKS_H_

#define HDAA_GPIO_SHIFT(n)	(n * 3)
#define HDAA_GPIO_MASK(n)	(0x7 << (n * 3))
#define HDAA_GPIO_KEEP(n)	(0x0 << (n * 3))
#define HDAA_GPIO_SET(n)	(0x1 << (n * 3))
#define HDAA_GPIO_CLEAR(n)	(0x2 << (n * 3))
#define HDAA_GPIO_DISABLE(n)	(0x3 << (n * 3))
#define HDAA_GPIO_INPUT(n)	(0x4 << (n * 3))

/* 9 - 25 = anything else */
#define HDAA_QUIRK_SOFTPCMVOL	(1 << 9)
#define HDAA_QUIRK_FIXEDRATE	(1 << 10)
#define HDAA_QUIRK_FORCESTEREO	(1 << 11)
#define HDAA_QUIRK_EAPDINV	(1 << 12)
#define HDAA_QUIRK_SENSEINV	(1 << 14)

/* 26 - 31 = vrefs */
#define HDAA_QUIRK_IVREF50	(1 << 26)
#define HDAA_QUIRK_IVREF80	(1 << 27)
#define HDAA_QUIRK_IVREF100	(1 << 28)
#define HDAA_QUIRK_OVREF50	(1 << 29)
#define HDAA_QUIRK_OVREF80	(1 << 30)
#define HDAA_QUIRK_OVREF100	(1U << 31)

#define HDAA_QUIRK_IVREF	(HDAA_QUIRK_IVREF50 | HDAA_QUIRK_IVREF80 | \
						HDAA_QUIRK_IVREF100)
#define HDAA_QUIRK_OVREF	(HDAA_QUIRK_OVREF50 | HDAA_QUIRK_OVREF80 | \
						HDAA_QUIRK_OVREF100)
#define HDAA_QUIRK_VREF		(HDAA_QUIRK_IVREF | HDAA_QUIRK_OVREF)

#define HDAA_AMP_VOL_DEFAULT	(-1)
#define HDAA_AMP_MUTE_DEFAULT	(0xffffffff)
#define HDAA_AMP_MUTE_NONE	(0)
#define HDAA_AMP_MUTE_LEFT	(1 << 0)
#define HDAA_AMP_MUTE_RIGHT	(1 << 1)
#define HDAA_AMP_MUTE_ALL	(HDAA_AMP_MUTE_LEFT | HDAA_AMP_MUTE_RIGHT)

#define HDAA_AMP_LEFT_MUTED(v)	((v) & (HDAA_AMP_MUTE_LEFT))
#define HDAA_AMP_RIGHT_MUTED(v)	(((v) & HDAA_AMP_MUTE_RIGHT) >> 1)

/* Widget in playback receiving signal from recording. */
#define HDAA_ADC_MONITOR		(1 << 0)
/* Input mixer widget needs volume control as destination. */
#define HDAA_IMIX_AS_DST		(2 << 0)

#define HDAA_CTL_OUT		1
#define HDAA_CTL_IN		2

#define HDA_MAX_CONNS	32
#define HDA_MAX_NAMELEN	32

struct hdaa_audio_as;
struct hdaa_audio_ctl;
struct hdaa_chan;
struct hdaa_devinfo;
struct hdaa_pcm_devinfo;
struct hdaa_widget;

struct hdaa_widget {
	nid_t nid;
	int type;
	int enable;
	int nconns, selconn;
	int waspin;
	uint32_t pflags;
	int bindas;
	int bindseqmask;
	int ossdev;
	uint32_t ossmask;
	int unsol;
	nid_t conns[HDA_MAX_CONNS];
	u_char connsenable[HDA_MAX_CONNS];
	char name[HDA_MAX_NAMELEN];
	uint8_t	*eld;
	int	eld_len;
	struct hdaa_devinfo *devinfo;
	struct {
		uint32_t widget_cap;
		uint32_t outamp_cap;
		uint32_t inamp_cap;
		uint32_t supp_stream_formats;
		uint32_t supp_pcm_size_rate;
		uint32_t eapdbtl;
	} param;
	union {
		struct {
			uint32_t config;
			uint32_t original;
			uint32_t newconf;
			uint32_t cap;
			uint32_t ctrl;
			int	connected;
		} pin;
		struct {
			uint8_t	stripecap;
		} conv;
	} wclass;
};

struct hdaa_audio_ctl {
	struct hdaa_widget *widget, *childwidget;
	int enable;
	int index, dir, ndir;
	int mute, step, size, offset;
	int left, right, forcemute;
	uint32_t muted;
	uint32_t ossmask;	/* OSS devices that may affect control. */
	int	devleft[SOUND_MIXER_NRDEVICES]; /* Left ampl in 1/4dB. */
	int	devright[SOUND_MIXER_NRDEVICES]; /* Right ampl in 1/4dB. */
	int	devmute[SOUND_MIXER_NRDEVICES]; /* Mutes per OSS device. */
};

/* Association is a group of pins bound for some special function. */
struct hdaa_audio_as {
	u_char enable;
	u_char index;
	u_char dir;
	u_char pincnt;
	u_char fakeredir;
	u_char digital;
	uint16_t pinset;
	nid_t hpredir;
	nid_t pins[16];
	nid_t dacs[2][16];
	int num_chans;
	int chans[2];
	int location;	/* Pins location, if all have the same */
	int mixed;	/* Mixed/multiplexed recording, not multichannel. */
	struct hdaa_pcm_devinfo *pdevinfo;
};

struct hdaa_pcm_devinfo {
	device_t dev;
	struct hdaa_devinfo *devinfo;
	struct	snd_mixer *mixer;
	int	index;
	int	registered;
	int	playas, recas;
	u_char	left[SOUND_MIXER_NRDEVICES];
	u_char	right[SOUND_MIXER_NRDEVICES];
	int	minamp[SOUND_MIXER_NRDEVICES]; /* Minimal amps in 1/4dB. */
	int	maxamp[SOUND_MIXER_NRDEVICES]; /* Maximal amps in 1/4dB. */
	int	chan_size;
	int	chan_blkcnt;
	u_char	digital;
	uint32_t	ossmask;	/* Mask of supported OSS devices. */
	uint32_t	recsrc;		/* Mask of supported OSS sources. */
	int		autorecsrc;
};

struct hdaa_devinfo {
	device_t		dev;
	struct mtx		*lock;
	nid_t			nid;
	nid_t			startnode, endnode;
	uint32_t		outamp_cap;
	uint32_t		inamp_cap;
	uint32_t		supp_stream_formats;
	uint32_t		supp_pcm_size_rate;
	uint32_t		gpio_cap;
	uint32_t		quirks;
	uint32_t		newquirks;
	uint32_t		gpio;
	uint32_t		newgpio;
	uint32_t		gpo;
	uint32_t		newgpo;
	int			nodecnt;
	int			ctlcnt;
	int			ascnt;
	int			num_devs;
	int			num_chans;
	struct hdaa_widget	*widget;
	struct hdaa_audio_ctl	*ctl;
	struct hdaa_audio_as	*as;
	struct hdaa_pcm_devinfo	*devs;
	struct hdaa_chan	*chans;
	struct callout		poll_jack;
	int			poll_ival;
};

#define HDAA_CHN_RUNNING	0x00000001
#define HDAA_CHN_SUSPEND	0x00000002

struct hdaa_chan {
	struct snd_dbuf *b;
	struct pcm_channel *c;
	struct pcmchan_caps caps;
	struct hdaa_devinfo *devinfo;
	struct hdaa_pcm_devinfo *pdevinfo;
	uint32_t spd, fmt, fmtlist[32], pcmrates[16];
	uint32_t supp_stream_formats, supp_pcm_size_rate;
	uint32_t blkcnt, blksz;
	uint32_t *dmapos;
	uint32_t flags;
	int dir;
	int off;
	int sid;
	int bit16, bit32;
	int channels;	/* Number of audio channels. */
	int as;		/* Number of association. */
	int asindex;	/* Index within association. */
	nid_t io[16];
	uint8_t	stripecap;	/* AND of stripecap of all ios. */
	uint8_t	stripectl;	/* stripe to use to all ios. */
};

#define MINQDB(ctl)							\
	((0 - (ctl)->offset) * ((ctl)->size + 1))

#define MAXQDB(ctl)							\
	(((ctl)->step - (ctl)->offset) * ((ctl)->size + 1))

#define RANGEQDB(ctl)							\
	((ctl)->step * ((ctl)->size + 1))

#define VAL2QDB(ctl, val) 						\
	(((ctl)->size + 1) * ((int)(val) - (ctl)->offset))

#define QDB2VAL(ctl, qdb) 						\
	imax(imin((((qdb) + (ctl)->size / 2 * ((qdb) > 0 ? 1 : -1)) /	\
	 ((ctl)->size + 1) + (ctl)->offset), (ctl)->step), 0)

#define hdaa_codec_id(devinfo)						\
		(((uint32_t)hda_get_vendor_id(devinfo->dev) << 16) +	\
		hda_get_device_id(devinfo->dev))

#define hdaa_card_id(devinfo)					\
		(((uint32_t)hda_get_subdevice_id(devinfo->dev) << 16) +	\
		hda_get_subvendor_id(devinfo->dev))

struct hdaa_widget	*hdaa_widget_get(struct hdaa_devinfo *, nid_t);
uint32_t		hdaa_widget_pin_patch(uint32_t config, const char *str);
uint32_t		hdaa_gpio_patch(uint32_t gpio, const char *str);

void			hdaa_patch(struct hdaa_devinfo *devinfo);
void			hdaa_patch_direct(struct hdaa_devinfo *devinfo);

#endif
