/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Orion Hodson <O.Hodson@cs.ucl.ac.uk>
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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHERIN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THEPOSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This driver exists largely as a result of other people's efforts.
 * Much of register handling is based on NetBSD CMI8x38 audio driver
 * by Takuya Shiozaki <AoiMoe@imou.to>.  Chen-Li Tien
 * <cltien@cmedia.com.tw> clarified points regarding the DMA related
 * registers and the 8738 mixer devices.  His Linux driver was also a
 * useful reference point.
 *
 * TODO: MIDI
 *
 * SPDIF contributed by Gerhard Gonter <gonter@whisky.wu-wien.ac.at>.
 *
 * This card/code does not always manage to sample at 44100 - actual
 * rate drifts slightly between recordings (usually 0-3%).  No
 * differences visible in register dumps between times that work and
 * those that don't.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pci/cmireg.h>
#include <dev/sound/isa/sb.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <sys/sysctl.h>
#include <dev/sound/midi/mpu401.h>

#include "mixer_if.h"
#include "mpufoi_if.h"

SND_DECLARE_FILE("$FreeBSD$");

/* Supported chip ID's */
#define CMI8338A_PCI_ID   0x010013f6
#define CMI8338B_PCI_ID   0x010113f6
#define CMI8738_PCI_ID    0x011113f6
#define CMI8738B_PCI_ID   0x011213f6
#define CMI120_USB_ID     0x01030d8c

/* Buffer size max is 64k for permitted DMA boundaries */
#define CMI_DEFAULT_BUFSZ      16384

/* Interrupts per length of buffer */
#define CMI_INTR_PER_BUFFER      2

/* Clarify meaning of named defines in cmireg.h */
#define CMPCI_REG_DMA0_MAX_SAMPLES  CMPCI_REG_DMA0_BYTES
#define CMPCI_REG_DMA0_INTR_SAMPLES CMPCI_REG_DMA0_SAMPLES
#define CMPCI_REG_DMA1_MAX_SAMPLES  CMPCI_REG_DMA1_BYTES
#define CMPCI_REG_DMA1_INTR_SAMPLES CMPCI_REG_DMA1_SAMPLES

/* Our indication of custom mixer control */
#define CMPCI_NON_SB16_CONTROL		0xff

/* Debugging macro's */
#undef DEB
#ifndef DEB
#define DEB(x) /* x */
#endif /* DEB */

#ifndef DEBMIX
#define DEBMIX(x) /* x */
#endif  /* DEBMIX */

/* ------------------------------------------------------------------------- */
/* Structures */

struct sc_info;

struct sc_chinfo {
	struct sc_info		*parent;
	struct pcm_channel	*channel;
	struct snd_dbuf		*buffer;
	u_int32_t		fmt, spd, phys_buf, bps;
	u_int32_t		dma_active:1, dma_was_active:1;
	int			dir;
};

struct sc_info {
	device_t		dev;

	bus_space_tag_t		st;
	bus_space_handle_t	sh;
	bus_dma_tag_t		parent_dmat;
	struct resource		*reg, *irq;
	int			regid, irqid;
	void 			*ih;
	struct mtx		*lock;

	int			spdif_enabled;
	unsigned int		bufsz;
	struct sc_chinfo 	pch, rch;

	struct mpu401	*mpu;
	mpu401_intr_t		*mpu_intr;
	struct resource *mpu_reg;
	int mpu_regid;
	bus_space_tag_t	mpu_bt;
	bus_space_handle_t	mpu_bh;
};

/* Channel caps */

static u_int32_t cmi_fmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};

static struct pcmchan_caps cmi_caps = {5512, 48000, cmi_fmt, 0};

/* ------------------------------------------------------------------------- */
/* Register Utilities */

static u_int32_t
cmi_rd(struct sc_info *sc, int regno, int size)
{
	switch (size) {
	case 1:
		return bus_space_read_1(sc->st, sc->sh, regno);
	case 2:
		return bus_space_read_2(sc->st, sc->sh, regno);
	case 4:
		return bus_space_read_4(sc->st, sc->sh, regno);
	default:
		DEB(printf("cmi_rd: failed 0x%04x %d\n", regno, size));
		return 0xFFFFFFFF;
	}
}

static void
cmi_wr(struct sc_info *sc, int regno, u_int32_t data, int size)
{
	switch (size) {
	case 1:
		bus_space_write_1(sc->st, sc->sh, regno, data);
		break;
	case 2:
		bus_space_write_2(sc->st, sc->sh, regno, data);
		break;
	case 4:
		bus_space_write_4(sc->st, sc->sh, regno, data);
		break;
	}
}

static void
cmi_partial_wr4(struct sc_info *sc,
		int reg, int shift, u_int32_t mask, u_int32_t val)
{
	u_int32_t r;

	r = cmi_rd(sc, reg, 4);
	r &= ~(mask << shift);
	r |= val << shift;
	cmi_wr(sc, reg, r, 4);
}

static void
cmi_clr4(struct sc_info *sc, int reg, u_int32_t mask)
{
	u_int32_t r;

	r = cmi_rd(sc, reg, 4);
	r &= ~mask;
	cmi_wr(sc, reg, r, 4);
}

static void
cmi_set4(struct sc_info *sc, int reg, u_int32_t mask)
{
	u_int32_t r;

	r = cmi_rd(sc, reg, 4);
	r |= mask;
	cmi_wr(sc, reg, r, 4);
}

/* ------------------------------------------------------------------------- */
/* Rate Mapping */

static int cmi_rates[] = {5512, 8000, 11025, 16000,
			  22050, 32000, 44100, 48000};
#define NUM_CMI_RATES (sizeof(cmi_rates)/sizeof(cmi_rates[0]))

/* cmpci_rate_to_regvalue returns sampling freq selector for FCR1
 * register - reg order is 5k,11k,22k,44k,8k,16k,32k,48k */

static u_int32_t
cmpci_rate_to_regvalue(int rate)
{
	int i, r;

	for(i = 0; i < NUM_CMI_RATES - 1; i++) {
		if (rate < ((cmi_rates[i] + cmi_rates[i + 1]) / 2)) {
			break;
		}
	}

	DEB(printf("cmpci_rate_to_regvalue: %d -> %d\n", rate, cmi_rates[i]));

	r = ((i >> 1) | (i << 2)) & 0x07;
	return r;
}

static int
cmpci_regvalue_to_rate(u_int32_t r)
{
	int i;

	i = ((r << 1) | (r >> 2)) & 0x07;
	DEB(printf("cmpci_regvalue_to_rate: %d -> %d\n", r, i));
	return cmi_rates[i];
}

/* ------------------------------------------------------------------------- */
/* ADC/DAC control - there are 2 dma channels on 8738, either can be
 * playback or capture.  We use ch0 for playback and ch1 for capture. */

static void
cmi_dma_prog(struct sc_info *sc, struct sc_chinfo *ch, u_int32_t base)
{
	u_int32_t s, i, sz;

	ch->phys_buf = sndbuf_getbufaddr(ch->buffer);

	cmi_wr(sc, base, ch->phys_buf, 4);
	sz = (u_int32_t)sndbuf_getsize(ch->buffer);

	s = sz / ch->bps - 1;
	cmi_wr(sc, base + 4, s, 2);

	i = sz / (ch->bps * CMI_INTR_PER_BUFFER) - 1;
	cmi_wr(sc, base + 6, i, 2);
}


static void
cmi_ch0_start(struct sc_info *sc, struct sc_chinfo *ch)
{
	cmi_dma_prog(sc, ch, CMPCI_REG_DMA0_BASE);

	cmi_set4(sc, CMPCI_REG_FUNC_0, CMPCI_REG_CH0_ENABLE);
	cmi_set4(sc, CMPCI_REG_INTR_CTRL,
		 CMPCI_REG_CH0_INTR_ENABLE);

	ch->dma_active = 1;
}

static u_int32_t
cmi_ch0_stop(struct sc_info *sc, struct sc_chinfo *ch)
{
	u_int32_t r = ch->dma_active;

	cmi_clr4(sc, CMPCI_REG_INTR_CTRL, CMPCI_REG_CH0_INTR_ENABLE);
	cmi_clr4(sc, CMPCI_REG_FUNC_0, CMPCI_REG_CH0_ENABLE);
        cmi_set4(sc, CMPCI_REG_FUNC_0, CMPCI_REG_CH0_RESET);
        cmi_clr4(sc, CMPCI_REG_FUNC_0, CMPCI_REG_CH0_RESET);
	ch->dma_active = 0;
	return r;
}

static void
cmi_ch1_start(struct sc_info *sc, struct sc_chinfo *ch)
{
	cmi_dma_prog(sc, ch, CMPCI_REG_DMA1_BASE);
	cmi_set4(sc, CMPCI_REG_FUNC_0, CMPCI_REG_CH1_ENABLE);
	/* Enable Interrupts */
	cmi_set4(sc, CMPCI_REG_INTR_CTRL,
		 CMPCI_REG_CH1_INTR_ENABLE);
	DEB(printf("cmi_ch1_start: dma prog\n"));
	ch->dma_active = 1;
}

static u_int32_t
cmi_ch1_stop(struct sc_info *sc, struct sc_chinfo *ch)
{
	u_int32_t r = ch->dma_active;

	cmi_clr4(sc, CMPCI_REG_INTR_CTRL, CMPCI_REG_CH1_INTR_ENABLE);
	cmi_clr4(sc, CMPCI_REG_FUNC_0, CMPCI_REG_CH1_ENABLE);
        cmi_set4(sc, CMPCI_REG_FUNC_0, CMPCI_REG_CH1_RESET);
        cmi_clr4(sc, CMPCI_REG_FUNC_0, CMPCI_REG_CH1_RESET);
	ch->dma_active = 0;
	return r;
}

static void
cmi_spdif_speed(struct sc_info *sc, int speed) {
	u_int32_t fcr1, lcr, mcr;

	if (speed >= 44100) {
		fcr1 = CMPCI_REG_SPDIF0_ENABLE;
		lcr  = CMPCI_REG_XSPDIF_ENABLE;
		mcr  = (speed == 48000) ?
			CMPCI_REG_W_SPDIF_48L | CMPCI_REG_SPDIF_48K : 0;
	} else {
		fcr1 = mcr = lcr = 0;
	}

	cmi_partial_wr4(sc, CMPCI_REG_MISC, 0,
			CMPCI_REG_W_SPDIF_48L | CMPCI_REG_SPDIF_48K, mcr);
	cmi_partial_wr4(sc, CMPCI_REG_FUNC_1, 0,
			CMPCI_REG_SPDIF0_ENABLE, fcr1);
	cmi_partial_wr4(sc, CMPCI_REG_LEGACY_CTRL, 0,
			CMPCI_REG_XSPDIF_ENABLE, lcr);
}

/* ------------------------------------------------------------------------- */
/* Channel Interface implementation */

static void *
cmichan_init(kobj_t obj, void *devinfo,
	     struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct sc_info   *sc = devinfo;
	struct sc_chinfo *ch = (dir == PCMDIR_PLAY) ? &sc->pch : &sc->rch;

	ch->parent     = sc;
	ch->channel    = c;
	ch->bps        = 1;
	ch->fmt        = SND_FORMAT(AFMT_U8, 1, 0);
	ch->spd        = DSP_DEFAULT_SPEED;
	ch->buffer     = b;
	ch->dma_active = 0;
	if (sndbuf_alloc(ch->buffer, sc->parent_dmat, 0, sc->bufsz) != 0) {
		DEB(printf("cmichan_init failed\n"));
		return NULL;
	}

	ch->dir = dir;
	snd_mtxlock(sc->lock);
	if (ch->dir == PCMDIR_PLAY) {
		cmi_dma_prog(sc, ch, CMPCI_REG_DMA0_BASE);
	} else {
		cmi_dma_prog(sc, ch, CMPCI_REG_DMA1_BASE);
	}
	snd_mtxunlock(sc->lock);

	return ch;
}

static int
cmichan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct sc_chinfo *ch = data;
	struct sc_info	*sc = ch->parent;
	u_int32_t f;

	if (format & AFMT_S16_LE) {
		f = CMPCI_REG_FORMAT_16BIT;
		ch->bps = 2;
	} else {
		f = CMPCI_REG_FORMAT_8BIT;
		ch->bps = 1;
	}

	if (AFMT_CHANNEL(format) > 1) {
		f |= CMPCI_REG_FORMAT_STEREO;
		ch->bps *= 2;
	} else {
		f |= CMPCI_REG_FORMAT_MONO;
	}

	snd_mtxlock(sc->lock);
	if (ch->dir == PCMDIR_PLAY) {
		cmi_partial_wr4(ch->parent,
				CMPCI_REG_CHANNEL_FORMAT,
				CMPCI_REG_CH0_FORMAT_SHIFT,
				CMPCI_REG_CH0_FORMAT_MASK,
				f);
	} else {
		cmi_partial_wr4(ch->parent,
				CMPCI_REG_CHANNEL_FORMAT,
				CMPCI_REG_CH1_FORMAT_SHIFT,
				CMPCI_REG_CH1_FORMAT_MASK,
				f);
	}
	snd_mtxunlock(sc->lock);
	ch->fmt = format;

	return 0;
}

static u_int32_t
cmichan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct sc_chinfo *ch = data;
	struct sc_info	*sc = ch->parent;
	u_int32_t r, rsp;

	r = cmpci_rate_to_regvalue(speed);
	snd_mtxlock(sc->lock);
	if (ch->dir == PCMDIR_PLAY) {
		if (speed < 44100) {
			/* disable if req before rate change */
			cmi_spdif_speed(ch->parent, speed);
		}
		cmi_partial_wr4(ch->parent,
				CMPCI_REG_FUNC_1,
				CMPCI_REG_DAC_FS_SHIFT,
				CMPCI_REG_DAC_FS_MASK,
				r);
		if (speed >= 44100 && ch->parent->spdif_enabled) {
			/* enable if req after rate change */
			cmi_spdif_speed(ch->parent, speed);
		}
		rsp = cmi_rd(ch->parent, CMPCI_REG_FUNC_1, 4);
		rsp >>= CMPCI_REG_DAC_FS_SHIFT;
		rsp &= 	CMPCI_REG_DAC_FS_MASK;
	} else {
		cmi_partial_wr4(ch->parent,
				CMPCI_REG_FUNC_1,
				CMPCI_REG_ADC_FS_SHIFT,
				CMPCI_REG_ADC_FS_MASK,
				r);
		rsp = cmi_rd(ch->parent, CMPCI_REG_FUNC_1, 4);
		rsp >>= CMPCI_REG_ADC_FS_SHIFT;
		rsp &= 	CMPCI_REG_ADC_FS_MASK;
	}
	snd_mtxunlock(sc->lock);
	ch->spd = cmpci_regvalue_to_rate(r);

	DEB(printf("cmichan_setspeed (%s) %d -> %d (%d)\n",
		   (ch->dir == PCMDIR_PLAY) ? "play" : "rec",
		   speed, ch->spd, cmpci_regvalue_to_rate(rsp)));

	return ch->spd;
}

static u_int32_t
cmichan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct sc_chinfo *ch = data;
	struct sc_info	 *sc = ch->parent;

	/* user has requested interrupts every blocksize bytes */
	if (blocksize > sc->bufsz / CMI_INTR_PER_BUFFER) {
		blocksize = sc->bufsz / CMI_INTR_PER_BUFFER;
	}
	sndbuf_resize(ch->buffer, CMI_INTR_PER_BUFFER, blocksize);

	return blocksize;
}

static int
cmichan_trigger(kobj_t obj, void *data, int go)
{
	struct sc_chinfo	*ch = data;
	struct sc_info		*sc = ch->parent;

	if (!PCMTRIG_COMMON(go))
		return 0;

	snd_mtxlock(sc->lock);
	if (ch->dir == PCMDIR_PLAY) {
		switch(go) {
		case PCMTRIG_START:
			cmi_ch0_start(sc, ch);
			break;
		case PCMTRIG_STOP:
		case PCMTRIG_ABORT:
			cmi_ch0_stop(sc, ch);
			break;
		}
	} else {
		switch(go) {
		case PCMTRIG_START:
			cmi_ch1_start(sc, ch);
			break;
		case PCMTRIG_STOP:
		case PCMTRIG_ABORT:
			cmi_ch1_stop(sc, ch);
			break;
		}
	}
	snd_mtxunlock(sc->lock);
	return 0;
}

static u_int32_t
cmichan_getptr(kobj_t obj, void *data)
{
	struct sc_chinfo	*ch = data;
	struct sc_info		*sc = ch->parent;
	u_int32_t physptr, bufptr, sz;

	snd_mtxlock(sc->lock);
	if (ch->dir == PCMDIR_PLAY) {
		physptr = cmi_rd(sc, CMPCI_REG_DMA0_BASE, 4);
	} else {
		physptr = cmi_rd(sc, CMPCI_REG_DMA1_BASE, 4);
	}
	snd_mtxunlock(sc->lock);

	sz = sndbuf_getsize(ch->buffer);
	bufptr = (physptr - ch->phys_buf + sz - ch->bps) % sz;

	return bufptr;
}

static void
cmi_intr(void *data)
{
	struct sc_info *sc = data;
	u_int32_t intrstat;
	u_int32_t toclear;

	snd_mtxlock(sc->lock);
	intrstat = cmi_rd(sc, CMPCI_REG_INTR_STATUS, 4);
	if ((intrstat & CMPCI_REG_ANY_INTR) != 0) {

		toclear = 0;
		if (intrstat & CMPCI_REG_CH0_INTR) {
			toclear |= CMPCI_REG_CH0_INTR_ENABLE;
			//cmi_clr4(sc, CMPCI_REG_INTR_CTRL, CMPCI_REG_CH0_INTR_ENABLE);
		}

		if (intrstat & CMPCI_REG_CH1_INTR) {
			toclear |= CMPCI_REG_CH1_INTR_ENABLE;
			//cmi_clr4(sc, CMPCI_REG_INTR_CTRL, CMPCI_REG_CH1_INTR_ENABLE);
		}

		if (toclear) {
			cmi_clr4(sc, CMPCI_REG_INTR_CTRL, toclear);
			snd_mtxunlock(sc->lock);

			/* Signal interrupts to channel */
			if (intrstat & CMPCI_REG_CH0_INTR) {
				chn_intr(sc->pch.channel);
			}

			if (intrstat & CMPCI_REG_CH1_INTR) {
				chn_intr(sc->rch.channel);
			}

			snd_mtxlock(sc->lock);
			cmi_set4(sc, CMPCI_REG_INTR_CTRL, toclear);

		}
	}
	if(sc->mpu_intr) {
		(sc->mpu_intr)(sc->mpu);
	}
	snd_mtxunlock(sc->lock);
	return;
}

static struct pcmchan_caps *
cmichan_getcaps(kobj_t obj, void *data)
{
	return &cmi_caps;
}

static kobj_method_t cmichan_methods[] = {
    	KOBJMETHOD(channel_init,		cmichan_init),
    	KOBJMETHOD(channel_setformat,		cmichan_setformat),
    	KOBJMETHOD(channel_setspeed,		cmichan_setspeed),
    	KOBJMETHOD(channel_setblocksize,	cmichan_setblocksize),
    	KOBJMETHOD(channel_trigger,		cmichan_trigger),
    	KOBJMETHOD(channel_getptr,		cmichan_getptr),
    	KOBJMETHOD(channel_getcaps,		cmichan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(cmichan);

/* ------------------------------------------------------------------------- */
/* Mixer - sb16 with kinks */

static void
cmimix_wr(struct sc_info *sc, u_int8_t port, u_int8_t val)
{
	cmi_wr(sc, CMPCI_REG_SBADDR, port, 1);
	cmi_wr(sc, CMPCI_REG_SBDATA, val, 1);
}

static u_int8_t
cmimix_rd(struct sc_info *sc, u_int8_t port)
{
	cmi_wr(sc, CMPCI_REG_SBADDR, port, 1);
	return (u_int8_t)cmi_rd(sc, CMPCI_REG_SBDATA, 1);
}

struct sb16props {
	u_int8_t  rreg;     /* right reg chan register */
	u_int8_t  stereo:1; /* (no explanation needed, honest) */
	u_int8_t  rec:1;    /* recording source */
	u_int8_t  bits:3;   /* num bits to represent maximum gain rep */
	u_int8_t  oselect;  /* output select mask */
	u_int8_t  iselect;  /* right input select mask */
} static const cmt[SOUND_MIXER_NRDEVICES] = {
	[SOUND_MIXER_SYNTH]   = {CMPCI_SB16_MIXER_FM_R,      1, 1, 5,
				 CMPCI_SB16_SW_FM,   CMPCI_SB16_MIXER_FM_SRC_R},
	[SOUND_MIXER_CD]      = {CMPCI_SB16_MIXER_CDDA_R,    1, 1, 5,
				 CMPCI_SB16_SW_CD,   CMPCI_SB16_MIXER_CD_SRC_R},
	[SOUND_MIXER_LINE]    = {CMPCI_SB16_MIXER_LINE_R,    1, 1, 5,
				 CMPCI_SB16_SW_LINE, CMPCI_SB16_MIXER_LINE_SRC_R},
	[SOUND_MIXER_MIC]     = {CMPCI_SB16_MIXER_MIC,       0, 1, 5,
				 CMPCI_SB16_SW_MIC,  CMPCI_SB16_MIXER_MIC_SRC},
	[SOUND_MIXER_SPEAKER] = {CMPCI_SB16_MIXER_SPEAKER,  0, 0, 2, 0, 0},
	[SOUND_MIXER_PCM]     = {CMPCI_SB16_MIXER_VOICE_R,  1, 0, 5, 0, 0},
	[SOUND_MIXER_VOLUME]  = {CMPCI_SB16_MIXER_MASTER_R, 1, 0, 5, 0, 0},
	/* These controls are not implemented in CMI8738, but maybe at a
	   future date.  They are not documented in C-Media documentation,
	   though appear in other drivers for future h/w (ALSA, Linux, NetBSD).
	*/
	[SOUND_MIXER_IGAIN]   = {CMPCI_SB16_MIXER_INGAIN_R,  1, 0, 2, 0, 0},
	[SOUND_MIXER_OGAIN]   = {CMPCI_SB16_MIXER_OUTGAIN_R, 1, 0, 2, 0, 0},
	[SOUND_MIXER_BASS]    = {CMPCI_SB16_MIXER_BASS_R,    1, 0, 4, 0, 0},
	[SOUND_MIXER_TREBLE]  = {CMPCI_SB16_MIXER_TREBLE_R,  1, 0, 4, 0, 0},
	/* The mic pre-amp is implemented with non-SB16 compatible
	   registers. */
	[SOUND_MIXER_MONITOR]  = {CMPCI_NON_SB16_CONTROL,     0, 1, 4, 0},
};

#define MIXER_GAIN_REG_RTOL(r) (r - 1)

static int
cmimix_init(struct snd_mixer *m)
{
	struct sc_info	*sc = mix_getdevinfo(m);
	u_int32_t	i,v;

	for(i = v = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (cmt[i].bits) v |= 1 << i;
	}
	mix_setdevs(m, v);

	for(i = v = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (cmt[i].rec) v |= 1 << i;
	}
	mix_setrecdevs(m, v);

	cmimix_wr(sc, CMPCI_SB16_MIXER_RESET, 0);
	cmimix_wr(sc, CMPCI_SB16_MIXER_ADCMIX_L, 0);
	cmimix_wr(sc, CMPCI_SB16_MIXER_ADCMIX_R, 0);
	cmimix_wr(sc, CMPCI_SB16_MIXER_OUTMIX,
		  CMPCI_SB16_SW_CD | CMPCI_SB16_SW_MIC | CMPCI_SB16_SW_LINE);
	return 0;
}

static int
cmimix_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
	struct sc_info *sc = mix_getdevinfo(m);
	u_int32_t r, l, max;
	u_int8_t  v;

	max = (1 << cmt[dev].bits) - 1;

	if (cmt[dev].rreg == CMPCI_NON_SB16_CONTROL) {
		/* For time being this can only be one thing (mic in
		 * mic/aux reg) */
		v = cmi_rd(sc, CMPCI_REG_AUX_MIC, 1) & 0xf0;
		l = left * max / 100;
		/* 3 bit gain with LSB MICGAIN off(1),on(1) -> 4 bit value */
		v |= ((l << 1) | (~l >> 3)) & 0x0f;
		cmi_wr(sc, CMPCI_REG_AUX_MIC, v, 1);
		return 0;
	}

	l  = (left * max / 100) << (8 - cmt[dev].bits);
	if (cmt[dev].stereo) {
		r = (right * max / 100) << (8 - cmt[dev].bits);
		cmimix_wr(sc, MIXER_GAIN_REG_RTOL(cmt[dev].rreg), l);
		cmimix_wr(sc, cmt[dev].rreg, r);
		DEBMIX(printf("Mixer stereo write dev %d reg 0x%02x "\
			      "value 0x%02x:0x%02x\n",
			      dev, MIXER_GAIN_REG_RTOL(cmt[dev].rreg), l, r));
	} else {
		r = l;
		cmimix_wr(sc, cmt[dev].rreg, l);
		DEBMIX(printf("Mixer mono write dev %d reg 0x%02x " \
			      "value 0x%02x:0x%02x\n",
			      dev, cmt[dev].rreg, l, l));
	}

	/* Zero gain does not mute channel from output, but this does... */
	v = cmimix_rd(sc, CMPCI_SB16_MIXER_OUTMIX);
	if (l == 0 && r == 0) {
		v &= ~cmt[dev].oselect;
	} else {
		v |= cmt[dev].oselect;
	}
	cmimix_wr(sc,  CMPCI_SB16_MIXER_OUTMIX, v);

	return 0;
}

static u_int32_t
cmimix_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	struct sc_info *sc = mix_getdevinfo(m);
	u_int32_t i, ml, sl;

	ml = sl = 0;
	for(i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if ((1<<i) & src) {
			if (cmt[i].stereo) {
				sl |= cmt[i].iselect;
			} else {
				ml |= cmt[i].iselect;
			}
		}
	}
	cmimix_wr(sc, CMPCI_SB16_MIXER_ADCMIX_R, sl|ml);
	DEBMIX(printf("cmimix_setrecsrc: reg 0x%02x val 0x%02x\n",
		      CMPCI_SB16_MIXER_ADCMIX_R, sl|ml));
	ml = CMPCI_SB16_MIXER_SRC_R_TO_L(ml);
	cmimix_wr(sc, CMPCI_SB16_MIXER_ADCMIX_L, sl|ml);
	DEBMIX(printf("cmimix_setrecsrc: reg 0x%02x val 0x%02x\n",
		      CMPCI_SB16_MIXER_ADCMIX_L, sl|ml));

	return src;
}

/* Optional SPDIF support. */

static int
cmi_initsys(struct sc_info* sc)
{
	/* XXX: an user should be able to set this with a control tool,
	   if not done before 7.0-RELEASE, this needs to be converted
	   to a device specific sysctl "dev.pcm.X.yyy" via
	   device_get_sysctl_*() as discussed on multimedia@ in msg-id
	   <861wujij2q.fsf@xps.des.no> */
	SYSCTL_ADD_INT(device_get_sysctl_ctx(sc->dev), 
		       SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev)),
		       OID_AUTO, "spdif_enabled", CTLFLAG_RW, 
		       &sc->spdif_enabled, 0, 
		       "enable SPDIF output at 44.1 kHz and above");

	return 0;
}

/* ------------------------------------------------------------------------- */
static kobj_method_t cmi_mixer_methods[] = {
	KOBJMETHOD(mixer_init,	cmimix_init),
	KOBJMETHOD(mixer_set,	cmimix_set),
	KOBJMETHOD(mixer_setrecsrc,	cmimix_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(cmi_mixer);

/*
 * mpu401 functions
 */

static unsigned char
cmi_mread(struct mpu401 *arg, void *sc, int reg)
{	
	unsigned int d;

		d = bus_space_read_1(0,0, 0x330 + reg); 
	/*	printf("cmi_mread: reg %x %x\n",reg, d);
	*/
	return d;
}

static void
cmi_mwrite(struct mpu401 *arg, void *sc, int reg, unsigned char b)
{

	bus_space_write_1(0,0,0x330 + reg , b);
}

static int
cmi_muninit(struct mpu401 *arg, void *cookie)
{
	struct sc_info *sc = cookie;

	snd_mtxlock(sc->lock);
	sc->mpu_intr = NULL;
	sc->mpu = NULL;
	snd_mtxunlock(sc->lock);

	return 0;
}

static kobj_method_t cmi_mpu_methods[] = {
    	KOBJMETHOD(mpufoi_read,		cmi_mread),
    	KOBJMETHOD(mpufoi_write,	cmi_mwrite),
    	KOBJMETHOD(mpufoi_uninit,	cmi_muninit),
	KOBJMETHOD_END
};

static DEFINE_CLASS(cmi_mpu, cmi_mpu_methods, 0);

static void
cmi_midiattach(struct sc_info *sc) {
/*
	const struct {
		int port,bits;
	} *p, ports[] = { 
		{0x330,0}, 
		{0x320,1}, 
		{0x310,2}, 
		{0x300,3}, 
		{0,0} } ;
	Notes, CMPCI_REG_VMPUSEL sets the io port for the mpu.  Does
	anyone know how to bus_space tag?
*/
	cmi_clr4(sc, CMPCI_REG_FUNC_1, CMPCI_REG_UART_ENABLE);
	cmi_clr4(sc, CMPCI_REG_LEGACY_CTRL, 
			CMPCI_REG_VMPUSEL_MASK << CMPCI_REG_VMPUSEL_SHIFT);
	cmi_set4(sc, CMPCI_REG_LEGACY_CTRL, 
			0 << CMPCI_REG_VMPUSEL_SHIFT );
	cmi_set4(sc, CMPCI_REG_FUNC_1, CMPCI_REG_UART_ENABLE);
	sc->mpu = mpu401_init(&cmi_mpu_class, sc, cmi_intr, &sc->mpu_intr);
}



/* ------------------------------------------------------------------------- */
/* Power and reset */

static void
cmi_power(struct sc_info *sc, int state)
{
	switch (state) {
	case 0: /* full power */
		cmi_clr4(sc, CMPCI_REG_MISC, CMPCI_REG_POWER_DOWN);
		break;
	default:
		/* power off */
		cmi_set4(sc, CMPCI_REG_MISC, CMPCI_REG_POWER_DOWN);
		break;
	}
}

static int
cmi_init(struct sc_info *sc)
{
	/* Effect reset */
	cmi_set4(sc, CMPCI_REG_MISC, CMPCI_REG_BUS_AND_DSP_RESET);
	DELAY(100);
	cmi_clr4(sc, CMPCI_REG_MISC, CMPCI_REG_BUS_AND_DSP_RESET);

	/* Disable interrupts and channels */
	cmi_clr4(sc, CMPCI_REG_FUNC_0,
		 CMPCI_REG_CH0_ENABLE | CMPCI_REG_CH1_ENABLE);
	cmi_clr4(sc, CMPCI_REG_INTR_CTRL,
		 CMPCI_REG_CH0_INTR_ENABLE | CMPCI_REG_CH1_INTR_ENABLE);

	/* Configure DMA channels, ch0 = play, ch1 = capture */
	cmi_clr4(sc, CMPCI_REG_FUNC_0, CMPCI_REG_CH0_DIR);
	cmi_set4(sc, CMPCI_REG_FUNC_0, CMPCI_REG_CH1_DIR);

	/* Attempt to enable 4 Channel output */
	cmi_set4(sc, CMPCI_REG_MISC, CMPCI_REG_N4SPK3D);

	/* Disable SPDIF1 - not compatible with config */
	cmi_clr4(sc, CMPCI_REG_FUNC_1, CMPCI_REG_SPDIF1_ENABLE);
	cmi_clr4(sc, CMPCI_REG_FUNC_1, CMPCI_REG_SPDIF_LOOP);

	return 0;
}

static void
cmi_uninit(struct sc_info *sc)
{
	/* Disable interrupts and channels */
	cmi_clr4(sc, CMPCI_REG_INTR_CTRL,
		 CMPCI_REG_CH0_INTR_ENABLE |
		 CMPCI_REG_CH1_INTR_ENABLE |
		 CMPCI_REG_TDMA_INTR_ENABLE);
	cmi_clr4(sc, CMPCI_REG_FUNC_0,
		 CMPCI_REG_CH0_ENABLE | CMPCI_REG_CH1_ENABLE);
	cmi_clr4(sc, CMPCI_REG_FUNC_1, CMPCI_REG_UART_ENABLE);

	if( sc->mpu )
		sc->mpu_intr = NULL;
}

/* ------------------------------------------------------------------------- */
/* Bus and device registration */
static int
cmi_probe(device_t dev)
{
	switch(pci_get_devid(dev)) {
	case CMI8338A_PCI_ID:
		device_set_desc(dev, "CMedia CMI8338A");
		return BUS_PROBE_DEFAULT;
	case CMI8338B_PCI_ID:
		device_set_desc(dev, "CMedia CMI8338B");
		return BUS_PROBE_DEFAULT;
	case CMI8738_PCI_ID:
		device_set_desc(dev, "CMedia CMI8738");
		return BUS_PROBE_DEFAULT;
	case CMI8738B_PCI_ID:
		device_set_desc(dev, "CMedia CMI8738B");
		return BUS_PROBE_DEFAULT;
	case CMI120_USB_ID:
	        device_set_desc(dev, "CMedia CMI120");
	        return BUS_PROBE_DEFAULT;
	default:
		return ENXIO;
	}
}

static int
cmi_attach(device_t dev)
{
	struct sc_info		*sc;
	char			status[SND_STATUSLEN];

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->lock = snd_mtxcreate(device_get_nameunit(dev), "snd_cmi softc");
	pci_enable_busmaster(dev);

	sc->dev = dev;
	sc->regid = PCIR_BAR(0);
	sc->reg = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &sc->regid,
					 RF_ACTIVE);
	if (!sc->reg) {
		device_printf(dev, "cmi_attach: Cannot allocate bus resource\n");
		goto bad;
	}
	sc->st = rman_get_bustag(sc->reg);
	sc->sh = rman_get_bushandle(sc->reg);

	if (0)
		cmi_midiattach(sc);

	sc->irqid = 0;
	sc->irq   = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irqid,
					   RF_ACTIVE | RF_SHAREABLE);
	if (!sc->irq ||
	    snd_setup_intr(dev, sc->irq, INTR_MPSAFE, cmi_intr, sc, &sc->ih)) {
		device_printf(dev, "cmi_attach: Unable to map interrupt\n");
		goto bad;
	}

	sc->bufsz = pcm_getbuffersize(dev, 4096, CMI_DEFAULT_BUFSZ, 65536);

	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev), /*alignment*/2,
			       /*boundary*/0,
			       /*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
			       /*highaddr*/BUS_SPACE_MAXADDR,
			       /*filter*/NULL, /*filterarg*/NULL,
			       /*maxsize*/sc->bufsz, /*nsegments*/1,
			       /*maxsegz*/0x3ffff, /*flags*/0,
			       /*lockfunc*/NULL,
			       /*lockfunc*/NULL,
			       &sc->parent_dmat) != 0) {
		device_printf(dev, "cmi_attach: Unable to create dma tag\n");
		goto bad;
	}

	cmi_power(sc, 0);
	if (cmi_init(sc))
		goto bad;

	if (mixer_init(dev, &cmi_mixer_class, sc))
		goto bad;

	if (pcm_register(dev, sc, 1, 1))
		goto bad;

	cmi_initsys(sc);

	pcm_addchan(dev, PCMDIR_PLAY, &cmichan_class, sc);
	pcm_addchan(dev, PCMDIR_REC, &cmichan_class, sc);

	snprintf(status, SND_STATUSLEN, "at io 0x%jx irq %jd %s",
		 rman_get_start(sc->reg), rman_get_start(sc->irq),PCM_KLDSTRING(snd_cmi));
	pcm_setstatus(dev, status);

	DEB(printf("cmi_attach: succeeded\n"));
	return 0;

 bad:
	if (sc->parent_dmat)
		bus_dma_tag_destroy(sc->parent_dmat);
	if (sc->ih)
		bus_teardown_intr(dev, sc->irq, sc->ih);
	if (sc->irq)
		bus_release_resource(dev, SYS_RES_IRQ, sc->irqid, sc->irq);
	if (sc->reg)
		bus_release_resource(dev, SYS_RES_IOPORT, sc->regid, sc->reg);
	if (sc->lock)
		snd_mtxfree(sc->lock);
	if (sc)
		free(sc, M_DEVBUF);

	return ENXIO;
}

static int
cmi_detach(device_t dev)
{
	struct sc_info *sc;
	int r;

	r = pcm_unregister(dev);
	if (r) return r;

	sc = pcm_getdevinfo(dev);
	cmi_uninit(sc);
	cmi_power(sc, 3);

	bus_dma_tag_destroy(sc->parent_dmat);
	bus_teardown_intr(dev, sc->irq, sc->ih);
	bus_release_resource(dev, SYS_RES_IRQ, sc->irqid, sc->irq);
	if(sc->mpu)
		mpu401_uninit(sc->mpu);
	bus_release_resource(dev, SYS_RES_IOPORT, sc->regid, sc->reg);
	if (sc->mpu_reg)
	    bus_release_resource(dev, SYS_RES_IOPORT, sc->mpu_regid, sc->mpu_reg);

	snd_mtxfree(sc->lock);
	free(sc, M_DEVBUF);

	return 0;
}

static int
cmi_suspend(device_t dev)
{
	struct sc_info *sc = pcm_getdevinfo(dev);

	snd_mtxlock(sc->lock);
	sc->pch.dma_was_active = cmi_ch0_stop(sc, &sc->pch);
	sc->rch.dma_was_active = cmi_ch1_stop(sc, &sc->rch);
	cmi_power(sc, 3);
	snd_mtxunlock(sc->lock);
	return 0;
}

static int
cmi_resume(device_t dev)
{
	struct sc_info *sc = pcm_getdevinfo(dev);

	snd_mtxlock(sc->lock);
	cmi_power(sc, 0);
	if (cmi_init(sc) != 0) {
		device_printf(dev, "unable to reinitialize the card\n");
		snd_mtxunlock(sc->lock);
		return ENXIO;
	}

	if (mixer_reinit(dev) == -1) {
		device_printf(dev, "unable to reinitialize the mixer\n");
		snd_mtxunlock(sc->lock);
                return ENXIO;
        }

	if (sc->pch.dma_was_active) {
		cmichan_setspeed(NULL, &sc->pch, sc->pch.spd);
		cmichan_setformat(NULL, &sc->pch, sc->pch.fmt);
		cmi_ch0_start(sc, &sc->pch);
	}

	if (sc->rch.dma_was_active) {
		cmichan_setspeed(NULL, &sc->rch, sc->rch.spd);
		cmichan_setformat(NULL, &sc->rch, sc->rch.fmt);
		cmi_ch1_start(sc, &sc->rch);
	}
	snd_mtxunlock(sc->lock);
	return 0;
}

static device_method_t cmi_methods[] = {
	DEVMETHOD(device_probe,         cmi_probe),
	DEVMETHOD(device_attach,        cmi_attach),
	DEVMETHOD(device_detach,        cmi_detach),
	DEVMETHOD(device_resume,        cmi_resume),
	DEVMETHOD(device_suspend,       cmi_suspend),
	{ 0, 0 }
};

static driver_t cmi_driver = {
	"pcm",
	cmi_methods,
	PCM_SOFTC_SIZE
};

DRIVER_MODULE(snd_cmi, pci, cmi_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_cmi, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_DEPEND(snd_cmi, midi, 1,1,1);
MODULE_VERSION(snd_cmi, 1);
