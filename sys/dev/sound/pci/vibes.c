/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Orion Hodson <O.Hodson@cs.ucl.ac.uk>
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
 * This card has the annoying habit of "clicking" when attached and
 * detached, haven't been able to remedy this with any combination of
 * muting.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pci/vibes.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "mixer_if.h"

SND_DECLARE_FILE("$FreeBSD$");

/* ------------------------------------------------------------------------- */
/* Constants */

#define SV_PCI_ID		0xca005333
#define SV_DEFAULT_BUFSZ	16384
#define SV_MIN_BLKSZ		128
#define SV_INTR_PER_BUFFER	2

#ifndef DEB
#define DEB(x) /* (x) */
#endif

/* ------------------------------------------------------------------------- */
/* Structures */

struct sc_info;

struct sc_chinfo {
	struct sc_info	*parent;
	struct pcm_channel	*channel;
	struct snd_dbuf	*buffer;
	u_int32_t	fmt, spd;
	int		dir;
	int		dma_active, dma_was_active;
};

struct sc_info {
	device_t		dev;

	/* DMA buffer allocator */
	bus_dma_tag_t		parent_dmat;

	/* Enhanced register resources */
	struct resource 	*enh_reg;
	bus_space_tag_t		enh_st;
	bus_space_handle_t	enh_sh;
	int			enh_type;
	int			enh_rid;

	/* DMA configuration */
	struct resource		*dmaa_reg, *dmac_reg;
	bus_space_tag_t		dmaa_st, dmac_st;
	bus_space_handle_t	dmaa_sh, dmac_sh;
	int			dmaa_type, dmac_type;
	int			dmaa_rid, dmac_rid;

	/* Interrupt resources */
	struct resource 	*irq;
	int			irqid;
	void			*ih;

	/* User configurable buffer size */
	unsigned int		bufsz;

	struct sc_chinfo	rch, pch;
	u_int8_t		rev;
};

static u_int32_t sc_fmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};

static struct pcmchan_caps sc_caps = {8000, 48000, sc_fmt, 0};

/* ------------------------------------------------------------------------- */
/* Register Manipulations */

#define sv_direct_set(x, y, z) _sv_direct_set(x, y, z, __LINE__)

static u_int8_t
sv_direct_get(struct sc_info *sc, u_int8_t reg)
{
	return bus_space_read_1(sc->enh_st, sc->enh_sh, reg);
}

static void
_sv_direct_set(struct sc_info *sc, u_int8_t reg, u_int8_t val, int line)
{
	u_int8_t n;
	bus_space_write_1(sc->enh_st, sc->enh_sh, reg, val);

	n = sv_direct_get(sc, reg);
	if (n != val) {
		device_printf(sc->dev, "sv_direct_set register 0x%02x %d != %d from line %d\n", reg, n, val, line);
	}
}

static u_int8_t
sv_indirect_get(struct sc_info *sc, u_int8_t reg)
{
	if (reg == SV_REG_FORMAT || reg == SV_REG_ANALOG_PWR)
		reg |= SV_CM_INDEX_MCE;

	bus_space_write_1(sc->enh_st, sc->enh_sh, SV_CM_INDEX, reg);
	return bus_space_read_1(sc->enh_st, sc->enh_sh, SV_CM_DATA);
}

#define sv_indirect_set(x, y, z) _sv_indirect_set(x, y, z, __LINE__)

static void
_sv_indirect_set(struct sc_info *sc, u_int8_t reg, u_int8_t val, int line)
{
	if (reg == SV_REG_FORMAT || reg == SV_REG_ANALOG_PWR)
		reg |= SV_CM_INDEX_MCE;

	bus_space_write_1(sc->enh_st, sc->enh_sh, SV_CM_INDEX, reg);
	bus_space_write_1(sc->enh_st, sc->enh_sh, SV_CM_DATA, val);

	reg &= ~SV_CM_INDEX_MCE;
	if (reg != SV_REG_ADC_PLLM) {
		u_int8_t n;
		n = sv_indirect_get(sc, reg);
		if (n != val) {
			device_printf(sc->dev, "sv_indirect_set register 0x%02x %d != %d line %d\n", reg, n, val, line);
		}
	}
}

static void
sv_dma_set_config(bus_space_tag_t st, bus_space_handle_t sh,
		  u_int32_t base, u_int32_t count, u_int8_t mode)
{
	bus_space_write_4(st, sh, SV_DMA_ADDR, base);
	bus_space_write_4(st, sh, SV_DMA_COUNT, count & 0xffffff);
	bus_space_write_1(st, sh, SV_DMA_MODE, mode);

	DEB(printf("base 0x%08x count %5d mode 0x%02x\n",
		   base, count, mode));
}

static u_int32_t
sv_dma_get_count(bus_space_tag_t st, bus_space_handle_t sh)
{
	return bus_space_read_4(st, sh, SV_DMA_COUNT) & 0xffffff;
}

/* ------------------------------------------------------------------------- */
/* Play / Record Common Interface */

static void *
svchan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct sc_info		*sc = devinfo;
	struct sc_chinfo	*ch;
	ch = (dir == PCMDIR_PLAY) ? &sc->pch : &sc->rch;

	ch->parent = sc;
	ch->channel = c;
	ch->dir = dir;

	if (sndbuf_alloc(b, sc->parent_dmat, 0, sc->bufsz) != 0) {
		DEB(printf("svchan_init failed\n"));
		return NULL;
	}
	ch->buffer = b;
	ch->fmt = SND_FORMAT(AFMT_U8, 1, 0);
	ch->spd = DSP_DEFAULT_SPEED;
	ch->dma_active = ch->dma_was_active = 0;

	return ch;
}

static struct pcmchan_caps *
svchan_getcaps(kobj_t obj, void *data)
{
        return &sc_caps;
}

static u_int32_t
svchan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct sc_chinfo *ch = data;
	struct sc_info *sc = ch->parent;

        /* user has requested interrupts every blocksize bytes */
	RANGE(blocksize, SV_MIN_BLKSZ, sc->bufsz / SV_INTR_PER_BUFFER);
	sndbuf_resize(ch->buffer, SV_INTR_PER_BUFFER, blocksize);
	DEB(printf("svchan_setblocksize: %d\n", blocksize));
	return blocksize;
}

static int
svchan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct sc_chinfo *ch = data;
	/* NB Just note format here as setting format register
	 * generates noise if dma channel is inactive. */
	ch->fmt  = (AFMT_CHANNEL(format) > 1) ? SV_AFMT_STEREO : SV_AFMT_MONO;
	ch->fmt |= (format & AFMT_16BIT) ? SV_AFMT_S16 : SV_AFMT_U8;
	return 0;
}

static u_int32_t
svchan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct sc_chinfo *ch = data;
	RANGE(speed, 8000, 48000);
	ch->spd = speed;
	return speed;
}

/* ------------------------------------------------------------------------- */
/* Recording interface */

static int
sv_set_recspeed(struct sc_info *sc, u_int32_t speed)
{
	u_int32_t	f_out, f_actual;
	u_int32_t	rs, re, r, best_r = 0, r2, t, n, best_n = 0;
	int32_t		m, best_m = 0, ms, me, err, min_err;

	/* This algorithm is a variant described in sonicvibes.pdf
	 * appendix A.  This search is marginally more extensive and
	 * results in (nominally) better sample rate matching. */

	f_out = SV_F_SCALE * speed;
	min_err = 0x7fffffff;

	/* Find bounds of r to examine, rs <= r <= re */
	t = 80000000 / f_out;
	for (rs = 1; (1 << rs) < t; rs++);

	t = 150000000 / f_out;
	for (re = 1; (2 << re) < t; re++);
	if (re > 7) re = 7;

	/* Search over r, n, m */
	for (r = rs; r <= re; r++) {
		r2 = (1 << r);
		for (n = 3; n < 34; n++) {
			m = f_out * n / (SV_F_REF / r2);
			ms = (m > 3) ? (m - 1) : 3;
			me = (m < 129) ? (m + 1) : 129;
			for (m = ms; m <= me; m++) {
				f_actual = m * SV_F_REF / (n * r2);
				if (f_actual > f_out) {
					err = f_actual - f_out;
				} else {
					err = f_out - f_actual;
				}
				if (err < min_err) {
					best_r = r;
					best_m = m - 2;
					best_n = n - 2;
					min_err = err;
					if (err == 0) break;
				}
			}
		}
	}

	sv_indirect_set(sc, SV_REG_ADC_PLLM, best_m);
	sv_indirect_set(sc, SV_REG_ADC_PLLN,
			SV_ADC_PLLN(best_n) | SV_ADC_PLLR(best_r));
	DEB(printf("svrchan_setspeed: %d -> PLLM 0x%02x PLLNR 0x%08x\n",
		   speed,
		   sv_indirect_get(sc, SV_REG_ADC_PLLM),
		   sv_indirect_get(sc, SV_REG_ADC_PLLN)));
	return 0;
}

static int
svrchan_trigger(kobj_t obj, void *data, int go)
{
	struct sc_chinfo	*ch = data;
	struct sc_info 		*sc = ch->parent;
	u_int32_t		count, enable;
	u_int8_t		v;

	switch(go) {
	case PCMTRIG_START:
		/* Set speed */
		sv_set_recspeed(sc, ch->spd);

		/* Set format */
		v  = sv_indirect_get(sc, SV_REG_FORMAT) & ~SV_AFMT_DMAC_MSK;
		v |= SV_AFMT_DMAC(ch->fmt);
		sv_indirect_set(sc, SV_REG_FORMAT, v);

		/* Program DMA */
		count = sndbuf_getsize(ch->buffer) / 2; /* DMAC uses words */
		sv_dma_set_config(sc->dmac_st, sc->dmac_sh,
				  sndbuf_getbufaddr(ch->buffer),
				  count - 1,
				  SV_DMA_MODE_AUTO | SV_DMA_MODE_RD);
		count = count / SV_INTR_PER_BUFFER - 1;
		sv_indirect_set(sc, SV_REG_DMAC_COUNT_HI, count >> 8);
		sv_indirect_set(sc, SV_REG_DMAC_COUNT_LO, count & 0xff);

		/* Enable DMA */
		enable = sv_indirect_get(sc, SV_REG_ENABLE) | SV_RECORD_ENABLE;
		sv_indirect_set(sc, SV_REG_ENABLE, enable);
		ch->dma_active = 1;
		break;
	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		enable = sv_indirect_get(sc, SV_REG_ENABLE) & ~SV_RECORD_ENABLE;
		sv_indirect_set(sc, SV_REG_ENABLE, enable);
		ch->dma_active = 0;
		break;
	}

	return 0;
}

static u_int32_t
svrchan_getptr(kobj_t obj, void *data)
{
	struct sc_chinfo	*ch = data;
	struct sc_info 		*sc = ch->parent;
	u_int32_t sz, remain;

	sz = sndbuf_getsize(ch->buffer);
	/* DMAC uses words */
	remain = (sv_dma_get_count(sc->dmac_st, sc->dmac_sh) + 1) * 2;
	return sz - remain;
}

static kobj_method_t svrchan_methods[] = {
        KOBJMETHOD(channel_init,                svchan_init),
        KOBJMETHOD(channel_setformat,           svchan_setformat),
        KOBJMETHOD(channel_setspeed,            svchan_setspeed),
        KOBJMETHOD(channel_setblocksize,        svchan_setblocksize),
        KOBJMETHOD(channel_trigger,             svrchan_trigger),
        KOBJMETHOD(channel_getptr,              svrchan_getptr),
        KOBJMETHOD(channel_getcaps,             svchan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(svrchan);

/* ------------------------------------------------------------------------- */
/* Playback interface */

static int
svpchan_trigger(kobj_t obj, void *data, int go)
{
	struct sc_chinfo	*ch = data;
	struct sc_info		*sc = ch->parent;
	u_int32_t		count, enable, speed;
	u_int8_t		v;

	switch(go) {
	case PCMTRIG_START:
		/* Set speed */
		speed = (ch->spd * 65536) / 48000;
		if (speed > 65535)
			speed = 65535;
		sv_indirect_set(sc, SV_REG_PCM_SAMPLING_HI, speed >> 8);
		sv_indirect_set(sc, SV_REG_PCM_SAMPLING_LO, speed & 0xff);

		/* Set format */
		v  = sv_indirect_get(sc, SV_REG_FORMAT) & ~SV_AFMT_DMAA_MSK;
		v |= SV_AFMT_DMAA(ch->fmt);
		sv_indirect_set(sc, SV_REG_FORMAT, v);

		/* Program DMA */
		count = sndbuf_getsize(ch->buffer);
		sv_dma_set_config(sc->dmaa_st, sc->dmaa_sh,
				  sndbuf_getbufaddr(ch->buffer),
				  count - 1,
				  SV_DMA_MODE_AUTO | SV_DMA_MODE_WR);
		count = count / SV_INTR_PER_BUFFER - 1;
		sv_indirect_set(sc, SV_REG_DMAA_COUNT_HI, count >> 8);
		sv_indirect_set(sc, SV_REG_DMAA_COUNT_LO, count & 0xff);

		/* Enable DMA */
		enable = sv_indirect_get(sc, SV_REG_ENABLE);
		enable = (enable | SV_PLAY_ENABLE) & ~SV_PLAYBACK_PAUSE;
		sv_indirect_set(sc, SV_REG_ENABLE, enable);
		ch->dma_active = 1;
		break;
	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		enable = sv_indirect_get(sc, SV_REG_ENABLE) & ~SV_PLAY_ENABLE;
		sv_indirect_set(sc, SV_REG_ENABLE, enable);
		ch->dma_active = 0;
		break;
	}

	return 0;
}

static u_int32_t
svpchan_getptr(kobj_t obj, void *data)
{
	struct sc_chinfo	*ch = data;
	struct sc_info 		*sc = ch->parent;
	u_int32_t sz, remain;

	sz = sndbuf_getsize(ch->buffer);
	/* DMAA uses bytes */
	remain = sv_dma_get_count(sc->dmaa_st, sc->dmaa_sh) + 1;
	return (sz - remain);
}

static kobj_method_t svpchan_methods[] = {
        KOBJMETHOD(channel_init,                svchan_init),
        KOBJMETHOD(channel_setformat,           svchan_setformat),
        KOBJMETHOD(channel_setspeed,            svchan_setspeed),
        KOBJMETHOD(channel_setblocksize,        svchan_setblocksize),
        KOBJMETHOD(channel_trigger,             svpchan_trigger),
        KOBJMETHOD(channel_getptr,              svpchan_getptr),
        KOBJMETHOD(channel_getcaps,             svchan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(svpchan);

/* ------------------------------------------------------------------------- */
/* Mixer support */

struct sv_mix_props {
	u_int8_t	reg;		/* Register */
	u_int8_t	stereo:1;	/* Supports 2 channels */
	u_int8_t	mute:1;		/* Supports muting */
	u_int8_t	neg:1;		/* Negative gain */
	u_int8_t	max;		/* Max gain */
	u_int8_t	iselect;	/* Input selector */
} static const mt [SOUND_MIXER_NRDEVICES] = {
	[SOUND_MIXER_LINE1]  = {SV_REG_AUX1,      1, 1, 1, SV_DEFAULT_MAX, SV_INPUT_AUX1},
	[SOUND_MIXER_CD]     = {SV_REG_CD,        1, 1, 1, SV_DEFAULT_MAX, SV_INPUT_CD},
	[SOUND_MIXER_LINE]   = {SV_REG_LINE,      1, 1, 1, SV_DEFAULT_MAX, SV_INPUT_LINE},
	[SOUND_MIXER_MIC]    = {SV_REG_MIC,       0, 1, 1, SV_MIC_MAX,     SV_INPUT_MIC},
	[SOUND_MIXER_SYNTH]  = {SV_REG_SYNTH,     0, 1, 1, SV_DEFAULT_MAX, 0},
	[SOUND_MIXER_LINE2]  = {SV_REG_AUX2,      1, 1, 1, SV_DEFAULT_MAX, SV_INPUT_AUX2},
	[SOUND_MIXER_VOLUME] = {SV_REG_MIX,       1, 1, 1, SV_DEFAULT_MAX, 0},
	[SOUND_MIXER_PCM]    = {SV_REG_PCM,       1, 1, 1, SV_PCM_MAX,     0},
	[SOUND_MIXER_RECLEV] = {SV_REG_ADC_INPUT, 1, 0, 0, SV_ADC_MAX, 0},
};

static void
sv_channel_gain(struct sc_info *sc, u_int32_t dev, u_int32_t gain, u_int32_t channel)
{
	u_int8_t	v;
	int32_t		g;

	g = mt[dev].max * gain / 100;
	if (mt[dev].neg)
		g = mt[dev].max - g;
	v  = sv_indirect_get(sc, mt[dev].reg + channel) & ~mt[dev].max;
	v |= g;

	if (mt[dev].mute) {
		if (gain == 0) {
			v |= SV_MUTE;
		} else {
			v &= ~SV_MUTE;
		}
	}
	sv_indirect_set(sc, mt[dev].reg + channel, v);
}

static int
sv_gain(struct sc_info *sc, u_int32_t dev, u_int32_t left, u_int32_t right)
{
	sv_channel_gain(sc, dev, left, 0);
	if (mt[dev].stereo)
		sv_channel_gain(sc, dev, right, 1);
	return 0;
}

static void
sv_mix_mute_all(struct sc_info *sc)
{
	int32_t i;
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (mt[i].reg) sv_gain(sc, i, 0, 0);
	}
}

static int
sv_mix_init(struct snd_mixer *m)
{
	u_int32_t 	i, v;

	for(i = v = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (mt[i].max) v |= (1 << i);
	}
	mix_setdevs(m, v);

	for(i = v = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (mt[i].iselect) v |= (1 << i);
	}
	mix_setrecdevs(m, v);

	return 0;
}

static int
sv_mix_set(struct snd_mixer *m, u_int32_t dev, u_int32_t left, u_int32_t right)
{
	struct sc_info	*sc = mix_getdevinfo(m);
	return sv_gain(sc, dev, left, right);
}

static u_int32_t
sv_mix_setrecsrc(struct snd_mixer *m, u_int32_t mask)
{
	struct sc_info	*sc = mix_getdevinfo(m);
	u_int32_t	i, v;

	v = sv_indirect_get(sc, SV_REG_ADC_INPUT) & SV_INPUT_GAIN_MASK;
	for(i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if ((1 << i) & mask) {
			v |= mt[i].iselect;
		}
	}
	DEB(printf("sv_mix_setrecsrc: mask 0x%08x adc_input 0x%02x\n", mask, v));
	sv_indirect_set(sc, SV_REG_ADC_INPUT, v);
	return mask;
}

static kobj_method_t sv_mixer_methods[] = {
        KOBJMETHOD(mixer_init,		sv_mix_init),
        KOBJMETHOD(mixer_set,		sv_mix_set),
        KOBJMETHOD(mixer_setrecsrc,	sv_mix_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(sv_mixer);

/* ------------------------------------------------------------------------- */
/* Power management and reset */

static void
sv_power(struct sc_info *sc, int state)
{
	u_int8_t v;

        switch (state) {
        case 0:
		/* power on */
		v = sv_indirect_get(sc, SV_REG_ANALOG_PWR) &~ SV_ANALOG_OFF;
		v |= SV_ANALOG_OFF_SRS | SV_ANALOG_OFF_SPLL;
		sv_indirect_set(sc, SV_REG_ANALOG_PWR, v);
		v = sv_indirect_get(sc, SV_REG_DIGITAL_PWR) &~ SV_DIGITAL_OFF;
		v |= SV_DIGITAL_OFF_SYN | SV_DIGITAL_OFF_MU | SV_DIGITAL_OFF_GP;
		sv_indirect_set(sc, SV_REG_DIGITAL_PWR, v);
                break;
        default:
		/* power off */
		v = sv_indirect_get(sc, SV_REG_ANALOG_PWR) | SV_ANALOG_OFF;
		sv_indirect_set(sc, SV_REG_ANALOG_PWR, v);
		v = sv_indirect_get(sc, SV_REG_DIGITAL_PWR) | SV_DIGITAL_OFF;
		sv_indirect_set(sc, SV_REG_DIGITAL_PWR, SV_DIGITAL_OFF);
                break;
        }
        DEB(printf("Power state %d\n", state));
}

static int
sv_init(struct sc_info *sc)
{
	u_int8_t	v;

	/* Effect reset */
	v  = sv_direct_get(sc, SV_CM_CONTROL) & ~SV_CM_CONTROL_ENHANCED;
	v |= SV_CM_CONTROL_RESET;
	sv_direct_set(sc, SV_CM_CONTROL, v);
	DELAY(50);

	v = sv_direct_get(sc, SV_CM_CONTROL) & ~SV_CM_CONTROL_RESET;
	sv_direct_set(sc, SV_CM_CONTROL, v);
	DELAY(50);

	/* Set in enhanced mode */
	v = sv_direct_get(sc, SV_CM_CONTROL);
	v |= SV_CM_CONTROL_ENHANCED;
	sv_direct_set(sc, SV_CM_CONTROL, v);

	/* Enable interrupts (UDM and MIDM are superfluous) */
	v = sv_direct_get(sc, SV_CM_IMR);
	v &= ~(SV_CM_IMR_AMSK | SV_CM_IMR_CMSK | SV_CM_IMR_SMSK);
	sv_direct_set(sc, SV_CM_IMR, v);

	/* Select ADC PLL for ADC clock */
	v = sv_indirect_get(sc, SV_REG_CLOCK_SOURCE) & ~SV_CLOCK_ALTERNATE;
	sv_indirect_set(sc, SV_REG_CLOCK_SOURCE, v);

	/* Disable loopback - binds ADC and DAC rates */
	v = sv_indirect_get(sc, SV_REG_LOOPBACK) & ~SV_LOOPBACK_ENABLE;
	sv_indirect_set(sc, SV_REG_LOOPBACK, v);

	/* Disable SRS */
	v = sv_indirect_get(sc, SV_REG_SRS_SPACE) | SV_SRS_DISABLED;
	sv_indirect_set(sc, SV_REG_SRS_SPACE, v);

	/* Get revision */
	sc->rev = sv_indirect_get(sc, SV_REG_REVISION);

	return 0;
}

static int
sv_suspend(device_t dev)
{
	struct sc_info	*sc = pcm_getdevinfo(dev);

	sc->rch.dma_was_active = sc->rch.dma_active;
	svrchan_trigger(NULL, &sc->rch, PCMTRIG_ABORT);

	sc->pch.dma_was_active = sc->pch.dma_active;
	svrchan_trigger(NULL, &sc->pch, PCMTRIG_ABORT);

	sv_mix_mute_all(sc);
	sv_power(sc, 3);

	return 0;
}

static int
sv_resume(device_t dev)
{
	struct sc_info	*sc = pcm_getdevinfo(dev);

	sv_mix_mute_all(sc);
	sv_power(sc, 0);
	if (sv_init(sc) == -1) {
		device_printf(dev, "unable to reinitialize the card\n");
		return ENXIO;
	}

	if (mixer_reinit(dev) == -1) {
		device_printf(dev, "unable to reinitialize the mixer\n");
                return ENXIO;
        }

	if (sc->rch.dma_was_active) {
		svrchan_trigger(0, &sc->rch, PCMTRIG_START);
	}

	if (sc->pch.dma_was_active) {
		svpchan_trigger(0, &sc->pch, PCMTRIG_START);
	}

	return 0;
}

/* ------------------------------------------------------------------------- */
/* Resource related */

static void
sv_intr(void *data)
{
	struct sc_info	*sc = data;
	u_int8_t	status;

	status = sv_direct_get(sc, SV_CM_STATUS);
	if (status & SV_CM_STATUS_AINT)
		chn_intr(sc->pch.channel);

	if (status & SV_CM_STATUS_CINT)
		chn_intr(sc->rch.channel);

	status &= ~(SV_CM_STATUS_AINT|SV_CM_STATUS_CINT);
	DEB(if (status) printf("intr 0x%02x ?\n", status));

	return;
}

static int
sv_probe(device_t dev)
{
	switch(pci_get_devid(dev)) {
	case SV_PCI_ID:
		device_set_desc(dev, "S3 Sonicvibes");
		return BUS_PROBE_DEFAULT;
	default:
		return ENXIO;
	}
}

static int
sv_attach(device_t dev) {
	struct sc_info	*sc;
	rman_res_t	count, midi_start, games_start;
	u_int32_t	data;
	char		status[SND_STATUSLEN];
	u_long		sdmaa, sdmac, ml, mu;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->dev = dev;

	pci_enable_busmaster(dev);

        if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
                device_printf(dev, "chip is in D%d power mode "
                              "-- setting to D0\n", pci_get_powerstate(dev));
                pci_set_powerstate(dev, PCI_POWERSTATE_D0);
        }
	sc->enh_rid  = SV_PCI_ENHANCED;
	sc->enh_type = SYS_RES_IOPORT;
	sc->enh_reg  = bus_alloc_resource_any(dev, sc->enh_type,
					      &sc->enh_rid, RF_ACTIVE);
	if (sc->enh_reg == NULL) {
		device_printf(dev, "sv_attach: cannot allocate enh\n");
		return ENXIO;
	}
	sc->enh_st = rman_get_bustag(sc->enh_reg);
	sc->enh_sh = rman_get_bushandle(sc->enh_reg);

	data = pci_read_config(dev, SV_PCI_DMAA, 4);
	DEB(printf("sv_attach: initial dmaa 0x%08x\n", data));
	data = pci_read_config(dev, SV_PCI_DMAC, 4);
	DEB(printf("sv_attach: initial dmac 0x%08x\n", data));

	/* Initialize DMA_A and DMA_C */
	pci_write_config(dev, SV_PCI_DMAA, SV_PCI_DMA_EXTENDED, 4);
	pci_write_config(dev, SV_PCI_DMAC, 0, 4);

	/* Register IRQ handler */
	sc->irqid = 0;
        sc->irq   = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irqid,
					   RF_ACTIVE | RF_SHAREABLE);
        if (!sc->irq ||
	    snd_setup_intr(dev, sc->irq, 0, sv_intr, sc, &sc->ih)) {
                device_printf(dev, "sv_attach: Unable to map interrupt\n");
                goto fail;
        }

	sc->bufsz = pcm_getbuffersize(dev, 4096, SV_DEFAULT_BUFSZ, 65536);
        if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev), /*alignment*/2,
			       /*boundary*/0,
                               /*lowaddr*/BUS_SPACE_MAXADDR_24BIT,
                               /*highaddr*/BUS_SPACE_MAXADDR,
                               /*filter*/NULL, /*filterarg*/NULL,
                               /*maxsize*/sc->bufsz, /*nsegments*/1,
                               /*maxsegz*/0x3ffff, /*flags*/0,
			       /*lockfunc*/busdma_lock_mutex,
			       /*lockarg*/&Giant, &sc->parent_dmat) != 0) {
                device_printf(dev, "sv_attach: Unable to create dma tag\n");
                goto fail;
        }

	/* Power up and initialize */
	sv_mix_mute_all(sc);
	sv_power(sc, 0);
	sv_init(sc);

	if (mixer_init(dev, &sv_mixer_class, sc) != 0) {
		device_printf(dev, "sv_attach: Mixer failed to initialize\n");
		goto fail;
	}

	/* XXX This is a hack, and it's ugly.  Okay, the deal is this
	 * card has two more io regions that available for automatic
	 * configuration by the pci code.  These need to be allocated
	 * to used as control registers for the DMA engines.
	 * Unfortunately FBSD has no bus_space_foo() functions so we
	 * have to grab port space in region of existing resources.  Go
	 * for space between midi and game ports.
	 */
	bus_get_resource(dev, SYS_RES_IOPORT, SV_PCI_MIDI, &midi_start, &count);
	bus_get_resource(dev, SYS_RES_IOPORT, SV_PCI_GAMES, &games_start, &count);

	if (games_start < midi_start) {
		ml = games_start;
		mu = midi_start;
	} else {
		ml = midi_start;
		mu = games_start;
	}
	/* Check assumptions about space availability and
           alignment. How driver loaded can determine whether
           games_start > midi_start or vice versa */
	if ((mu - ml >= 0x800)  ||
	    ((mu - ml) % 0x200)) {
		device_printf(dev, "sv_attach: resource assumptions not met "
			      "(midi 0x%08lx, games 0x%08lx)\n",
			      (u_long)midi_start, (u_long)games_start);
		goto fail;
	}

	sdmaa = ml + 0x40;
	sdmac = sdmaa + 0x40;

	/* Add resources to list of pci resources for this device - from here on
	 * they look like normal pci resources. */
	bus_set_resource(dev, SYS_RES_IOPORT, SV_PCI_DMAA, sdmaa, SV_PCI_DMAA_SIZE);
	bus_set_resource(dev, SYS_RES_IOPORT, SV_PCI_DMAC, sdmac, SV_PCI_DMAC_SIZE);

	/* Cache resource short-cuts for dma_a */
	sc->dmaa_rid = SV_PCI_DMAA;
	sc->dmaa_type = SYS_RES_IOPORT;
	sc->dmaa_reg  = bus_alloc_resource_any(dev, sc->dmaa_type,
					       &sc->dmaa_rid, RF_ACTIVE);
	if (sc->dmaa_reg == NULL) {
		device_printf(dev, "sv_attach: cannot allocate dmaa\n");
		goto fail;
	}
	sc->dmaa_st = rman_get_bustag(sc->dmaa_reg);
	sc->dmaa_sh = rman_get_bushandle(sc->dmaa_reg);

	/* Poke port into dma_a configuration, nb bit flags to enable dma */
	data = pci_read_config(dev, SV_PCI_DMAA, 4) | SV_PCI_DMA_ENABLE | SV_PCI_DMA_EXTENDED;
	data = ((u_int32_t)sdmaa & 0xfffffff0) | (data & 0x0f);
	pci_write_config(dev, SV_PCI_DMAA, data, 4);
	DEB(printf("dmaa: 0x%x 0x%x\n", data, pci_read_config(dev, SV_PCI_DMAA, 4)));

	/* Cache resource short-cuts for dma_c */
	sc->dmac_rid = SV_PCI_DMAC;
	sc->dmac_type = SYS_RES_IOPORT;
	sc->dmac_reg  = bus_alloc_resource_any(dev, sc->dmac_type,
					       &sc->dmac_rid, RF_ACTIVE);
	if (sc->dmac_reg == NULL) {
		device_printf(dev, "sv_attach: cannot allocate dmac\n");
		goto fail;
	}
	sc->dmac_st = rman_get_bustag(sc->dmac_reg);
	sc->dmac_sh = rman_get_bushandle(sc->dmac_reg);

	/* Poke port into dma_c configuration, nb bit flags to enable dma */
	data = pci_read_config(dev, SV_PCI_DMAC, 4) | SV_PCI_DMA_ENABLE | SV_PCI_DMA_EXTENDED;
	data = ((u_int32_t)sdmac & 0xfffffff0) | (data & 0x0f);
	pci_write_config(dev, SV_PCI_DMAC, data, 4);
	DEB(printf("dmac: 0x%x 0x%x\n", data, pci_read_config(dev, SV_PCI_DMAC, 4)));

	if (bootverbose)
		printf("Sonicvibes: revision %d.\n", sc->rev);

        if (pcm_register(dev, sc, 1, 1)) {
		device_printf(dev, "sv_attach: pcm_register fail\n");
                goto fail;
	}

        pcm_addchan(dev, PCMDIR_PLAY, &svpchan_class, sc);
        pcm_addchan(dev, PCMDIR_REC,  &svrchan_class, sc);

        snprintf(status, SND_STATUSLEN, "at io 0x%jx irq %jd %s",
                 rman_get_start(sc->enh_reg),  rman_get_start(sc->irq),PCM_KLDSTRING(snd_vibes));
        pcm_setstatus(dev, status);

        DEB(printf("sv_attach: succeeded\n"));

	return 0;

 fail:
	if (sc->parent_dmat)
		bus_dma_tag_destroy(sc->parent_dmat);
        if (sc->ih)
		bus_teardown_intr(dev, sc->irq, sc->ih);
        if (sc->irq)
		bus_release_resource(dev, SYS_RES_IRQ, sc->irqid, sc->irq);
	if (sc->enh_reg)
		bus_release_resource(dev, sc->enh_type, sc->enh_rid, sc->enh_reg);
	if (sc->dmaa_reg)
		bus_release_resource(dev, sc->dmaa_type, sc->dmaa_rid, sc->dmaa_reg);
	if (sc->dmac_reg)
		bus_release_resource(dev, sc->dmac_type, sc->dmac_rid, sc->dmac_reg);
	return ENXIO;
}

static int
sv_detach(device_t dev) {
	struct sc_info	*sc;
	int		r;

	r = pcm_unregister(dev);
	if (r) return r;

	sc = pcm_getdevinfo(dev);
	sv_mix_mute_all(sc);
	sv_power(sc, 3);

	bus_dma_tag_destroy(sc->parent_dmat);
	bus_teardown_intr(dev, sc->irq, sc->ih);
	bus_release_resource(dev, SYS_RES_IRQ, sc->irqid, sc->irq);
	bus_release_resource(dev, sc->enh_type, sc->enh_rid, sc->enh_reg);
	bus_release_resource(dev, sc->dmaa_type, sc->dmaa_rid, sc->dmaa_reg);
	bus_release_resource(dev, sc->dmac_type, sc->dmac_rid, sc->dmac_reg);

	free(sc, M_DEVBUF);

	return 0;
}

static device_method_t sc_methods[] = {
        DEVMETHOD(device_probe,         sv_probe),
        DEVMETHOD(device_attach,        sv_attach),
        DEVMETHOD(device_detach,        sv_detach),
        DEVMETHOD(device_resume,        sv_resume),
        DEVMETHOD(device_suspend,       sv_suspend),
        { 0, 0 }
};

static driver_t sonicvibes_driver = {
        "pcm",
        sc_methods,
        PCM_SOFTC_SIZE
};

DRIVER_MODULE(snd_vibes, pci, sonicvibes_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_vibes, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_vibes, 1);
