/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Orion Hodson <oho@acm.org>
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
 * als4000.c - driver for the Avance Logic ALS 4000 chipset.
 *
 * The ALS4000 is effectively an SB16 with a PCI interface.
 *
 * This driver derives from ALS4000a.PDF, Bart Hartgers alsa driver, and
 * SB16 register descriptions.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/isa/sb.h>
#include <dev/sound/pci/als4000.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "mixer_if.h"

SND_DECLARE_FILE("$FreeBSD$");

/* Debugging macro's */
#undef DEB
#ifndef DEB
#define DEB(x)  /* x */
#endif /* DEB */

#define ALS_DEFAULT_BUFSZ 16384

/* ------------------------------------------------------------------------- */
/* Structures */

struct sc_info;

struct sc_chinfo {
	struct sc_info		*parent;
	struct pcm_channel	*channel;
	struct snd_dbuf		*buffer;
	u_int32_t		format, speed, phys_buf, bps;
	u_int32_t		dma_active:1, dma_was_active:1;
	u_int8_t		gcr_fifo_status;
	int			dir;
};

struct sc_info {
	device_t		dev;
	bus_space_tag_t		st;
	bus_space_handle_t	sh;
	bus_dma_tag_t		parent_dmat;
	struct resource		*reg, *irq;
	int			regid, irqid;
	void			*ih;
	struct mtx		*lock;

	unsigned int		bufsz;
	struct sc_chinfo	pch, rch;
};

/* Channel caps */

static u_int32_t als_format[] = {
        SND_FORMAT(AFMT_U8, 1, 0),
        SND_FORMAT(AFMT_U8, 2, 0),
        SND_FORMAT(AFMT_S16_LE, 1, 0),
        SND_FORMAT(AFMT_S16_LE, 2, 0),
        0
};

/*
 * I don't believe this rotten soundcard can do 48k, really,
 * trust me.
 */
static struct pcmchan_caps als_caps = { 4000, 44100, als_format, 0 };

/* ------------------------------------------------------------------------- */
/* Register Utilities */

static u_int32_t
als_gcr_rd(struct sc_info *sc, int index)
{
	bus_space_write_1(sc->st, sc->sh, ALS_GCR_INDEX, index);
	return bus_space_read_4(sc->st, sc->sh, ALS_GCR_DATA);
}

static void
als_gcr_wr(struct sc_info *sc, int index, int data)
{
	bus_space_write_1(sc->st, sc->sh, ALS_GCR_INDEX, index);
	bus_space_write_4(sc->st, sc->sh, ALS_GCR_DATA, data);
}

static u_int8_t
als_intr_rd(struct sc_info *sc)
{
	return bus_space_read_1(sc->st, sc->sh, ALS_SB_MPU_IRQ);
}

static void
als_intr_wr(struct sc_info *sc, u_int8_t data)
{
	bus_space_write_1(sc->st, sc->sh, ALS_SB_MPU_IRQ, data);
}

static u_int8_t
als_mix_rd(struct sc_info *sc, u_int8_t index)
{
	bus_space_write_1(sc->st, sc->sh, ALS_MIXER_INDEX, index);
	return bus_space_read_1(sc->st, sc->sh, ALS_MIXER_DATA);
}

static void
als_mix_wr(struct sc_info *sc, u_int8_t index, u_int8_t data)
{
	bus_space_write_1(sc->st, sc->sh, ALS_MIXER_INDEX, index);
	bus_space_write_1(sc->st, sc->sh, ALS_MIXER_DATA, data);
}

static void
als_esp_wr(struct sc_info *sc, u_int8_t data)
{
	u_int32_t	tries, v;

	tries = 1000;
	do {
		v = bus_space_read_1(sc->st, sc->sh, ALS_ESP_WR_STATUS);
		if (~v & 0x80)
			break;
		DELAY(20);
	} while (--tries != 0);

	if (tries == 0)
		device_printf(sc->dev, "als_esp_wr timeout");

	bus_space_write_1(sc->st, sc->sh, ALS_ESP_WR_DATA, data);
}

static int
als_esp_reset(struct sc_info *sc)
{
	u_int32_t	tries, u, v;

	bus_space_write_1(sc->st, sc->sh, ALS_ESP_RST, 1);
	DELAY(10);
	bus_space_write_1(sc->st, sc->sh, ALS_ESP_RST, 0);
	DELAY(30);

	tries = 1000;
	do {
		u = bus_space_read_1(sc->st, sc->sh, ALS_ESP_RD_STATUS8);
		if (u & 0x80) {
			v = bus_space_read_1(sc->st, sc->sh, ALS_ESP_RD_DATA);
			if (v == 0xaa)
				return 0;
			else
				break;
		}
		DELAY(20);
	} while (--tries != 0);

	if (tries == 0)
		device_printf(sc->dev, "als_esp_reset timeout");
	return 1;
}

static u_int8_t
als_ack_read(struct sc_info *sc, u_int8_t addr)
{
	u_int8_t r = bus_space_read_1(sc->st, sc->sh, addr);
	return r;
}

/* ------------------------------------------------------------------------- */
/* Common pcm channel implementation */

static void *
alschan_init(kobj_t obj, void *devinfo,
	     struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct	sc_info	*sc = devinfo;
	struct	sc_chinfo *ch;

	snd_mtxlock(sc->lock);
	if (dir == PCMDIR_PLAY) {
		ch = &sc->pch;
		ch->gcr_fifo_status = ALS_GCR_FIFO0_STATUS;
	} else {
		ch = &sc->rch;
		ch->gcr_fifo_status = ALS_GCR_FIFO1_STATUS;
	}
	ch->dir = dir;
	ch->parent = sc;
	ch->channel = c;
	ch->bps = 1;
	ch->format = SND_FORMAT(AFMT_U8, 1, 0);
	ch->speed = DSP_DEFAULT_SPEED;
	ch->buffer = b;
	snd_mtxunlock(sc->lock);

	if (sndbuf_alloc(ch->buffer, sc->parent_dmat, 0, sc->bufsz) != 0)
		return NULL;

	return ch;
}

static int
alschan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct	sc_chinfo *ch = data;

	ch->format = format;
	return 0;
}

static u_int32_t
alschan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct	sc_chinfo *ch = data, *other;
	struct  sc_info *sc = ch->parent;

	other = (ch->dir == PCMDIR_PLAY) ? &sc->rch : &sc->pch;

	/* Deny request if other dma channel is active */
	if (other->dma_active) {
		ch->speed = other->speed;
		return other->speed;
	}

	ch->speed = speed;
	return speed;
}

static u_int32_t
alschan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct	sc_chinfo *ch = data;
	struct	sc_info *sc = ch->parent;

	if (blocksize > sc->bufsz / 2) {
		blocksize = sc->bufsz / 2;
	}
	sndbuf_resize(ch->buffer, 2, blocksize);
	return blocksize;
}

static u_int32_t
alschan_getptr(kobj_t obj, void *data)
{
	struct sc_chinfo *ch = data;
	struct sc_info *sc = ch->parent;
	int32_t pos, sz;

	snd_mtxlock(sc->lock);
	pos = als_gcr_rd(ch->parent, ch->gcr_fifo_status) & 0xffff;
	snd_mtxunlock(sc->lock);
	sz  = sndbuf_getsize(ch->buffer);
	return (2 * sz - pos - 1) % sz;
}

static struct pcmchan_caps*
alschan_getcaps(kobj_t obj, void *data)
{
	return &als_caps;
}

static void
als_set_speed(struct sc_chinfo *ch)
{
	struct sc_info *sc = ch->parent;
	struct sc_chinfo *other;

	other = (ch->dir == PCMDIR_PLAY) ? &sc->rch : &sc->pch;
	if (other->dma_active == 0) {
		als_esp_wr(sc, ALS_ESP_SAMPLE_RATE);
		als_esp_wr(sc, ch->speed >> 8);
		als_esp_wr(sc, ch->speed & 0xff);
	} else {
		DEB(printf("speed locked at %d (tried %d)\n",
			   other->speed, ch->speed));
	}
}

/* ------------------------------------------------------------------------- */
/* Playback channel implementation */

#define ALS_8BIT_CMD(x, y)  { (x), (y), DSP_DMA8,  DSP_CMD_DMAPAUSE_8  }
#define ALS_16BIT_CMD(x, y) { (x), (y),	DSP_DMA16, DSP_CMD_DMAPAUSE_16 }

struct playback_command {
	u_int32_t pcm_format;	/* newpcm format */
	u_int8_t  format_val;	/* sb16 format value */
	u_int8_t  dma_prog;	/* sb16 dma program */
	u_int8_t  dma_stop;	/* sb16 stop register */
} static const playback_cmds[] = {
	ALS_8BIT_CMD(SND_FORMAT(AFMT_U8, 1, 0), DSP_MODE_U8MONO),
	ALS_8BIT_CMD(SND_FORMAT(AFMT_U8, 2, 0), DSP_MODE_U8STEREO),
	ALS_16BIT_CMD(SND_FORMAT(AFMT_S16_LE, 1, 0), DSP_MODE_S16MONO),
	ALS_16BIT_CMD(SND_FORMAT(AFMT_S16_LE, 2, 0), DSP_MODE_S16STEREO),
};

static const struct playback_command*
als_get_playback_command(u_int32_t format)
{
	u_int32_t i, n;

	n = sizeof(playback_cmds) / sizeof(playback_cmds[0]);
	for (i = 0; i < n; i++) {
		if (playback_cmds[i].pcm_format == format) {
			return &playback_cmds[i];
		}
	}
	DEB(printf("als_get_playback_command: invalid format 0x%08x\n",
		   format));
	return &playback_cmds[0];
}

static void
als_playback_start(struct sc_chinfo *ch)
{
	const struct playback_command *p;
	struct	sc_info *sc = ch->parent;
	u_int32_t	buf, bufsz, count, dma_prog;

	buf = sndbuf_getbufaddr(ch->buffer);
	bufsz = sndbuf_getsize(ch->buffer);
	count = bufsz / 2;
	if (ch->format & AFMT_16BIT)
		count /= 2;
	count--;

	als_esp_wr(sc, DSP_CMD_SPKON);
	als_set_speed(ch);

	als_gcr_wr(sc, ALS_GCR_DMA0_START, buf);
	als_gcr_wr(sc, ALS_GCR_DMA0_MODE, (bufsz - 1) | 0x180000);

	p = als_get_playback_command(ch->format);
	dma_prog = p->dma_prog | DSP_F16_DAC | DSP_F16_AUTO | DSP_F16_FIFO_ON;

	als_esp_wr(sc, dma_prog);
	als_esp_wr(sc, p->format_val);
	als_esp_wr(sc, count & 0xff);
	als_esp_wr(sc, count >> 8);

	ch->dma_active = 1;
}

static int
als_playback_stop(struct sc_chinfo *ch)
{
	const struct playback_command *p;
	struct sc_info *sc = ch->parent;
	u_int32_t active;

	active = ch->dma_active;
	if (active) {
		p = als_get_playback_command(ch->format);
		als_esp_wr(sc, p->dma_stop);
	}
	ch->dma_active = 0;
	return active;
}

static int
alspchan_trigger(kobj_t obj, void *data, int go)
{
	struct	sc_chinfo *ch = data;
	struct sc_info *sc = ch->parent;

	if (!PCMTRIG_COMMON(go))
		return 0;

	snd_mtxlock(sc->lock);
	switch(go) {
	case PCMTRIG_START:
		als_playback_start(ch);
		break;
	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		als_playback_stop(ch);
		break;
	default:
		break;
	}
	snd_mtxunlock(sc->lock);
	return 0;
}

static kobj_method_t alspchan_methods[] = {
	KOBJMETHOD(channel_init,		alschan_init),
	KOBJMETHOD(channel_setformat,		alschan_setformat),
	KOBJMETHOD(channel_setspeed,		alschan_setspeed),
	KOBJMETHOD(channel_setblocksize,	alschan_setblocksize),
	KOBJMETHOD(channel_trigger,		alspchan_trigger),
	KOBJMETHOD(channel_getptr,		alschan_getptr),
	KOBJMETHOD(channel_getcaps,		alschan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(alspchan);

/* ------------------------------------------------------------------------- */
/* Capture channel implementation */

static u_int8_t
als_get_fifo_format(struct sc_info *sc, u_int32_t format)
{
	switch (format) {
	case SND_FORMAT(AFMT_U8, 1, 0):
		return ALS_FIFO1_8BIT;
	case SND_FORMAT(AFMT_U8, 2, 0):
		return ALS_FIFO1_8BIT | ALS_FIFO1_STEREO;
	case SND_FORMAT(AFMT_S16_LE, 1, 0):
		return ALS_FIFO1_SIGNED;
	case SND_FORMAT(AFMT_S16_LE, 2, 0):
		return ALS_FIFO1_SIGNED | ALS_FIFO1_STEREO;
	}
	device_printf(sc->dev, "format not found: 0x%08x\n", format);
	return ALS_FIFO1_8BIT;
}

static void
als_capture_start(struct sc_chinfo *ch)
{
	struct	sc_info *sc = ch->parent;
	u_int32_t	buf, bufsz, count, dma_prog;

	buf = sndbuf_getbufaddr(ch->buffer);
	bufsz = sndbuf_getsize(ch->buffer);
	count = bufsz / 2;
	if (ch->format & AFMT_16BIT)
		count /= 2;
	count--;

	als_esp_wr(sc, DSP_CMD_SPKON);
	als_set_speed(ch);

	als_gcr_wr(sc, ALS_GCR_FIFO1_START, buf);
	als_gcr_wr(sc, ALS_GCR_FIFO1_COUNT, (bufsz - 1));

	als_mix_wr(sc, ALS_FIFO1_LENGTH_LO, count & 0xff);
	als_mix_wr(sc, ALS_FIFO1_LENGTH_HI, count >> 8);

	dma_prog = ALS_FIFO1_RUN | als_get_fifo_format(sc, ch->format);
	als_mix_wr(sc, ALS_FIFO1_CONTROL, dma_prog);

	ch->dma_active = 1;
}

static int
als_capture_stop(struct sc_chinfo *ch)
{
	struct sc_info *sc = ch->parent;
	u_int32_t active;

	active = ch->dma_active;
	if (active) {
		als_mix_wr(sc, ALS_FIFO1_CONTROL, ALS_FIFO1_STOP);
	}
	ch->dma_active = 0;
	return active;
}

static int
alsrchan_trigger(kobj_t obj, void *data, int go)
{
	struct	sc_chinfo *ch = data;
	struct sc_info *sc = ch->parent;

	snd_mtxlock(sc->lock);
	switch(go) {
	case PCMTRIG_START:
		als_capture_start(ch);
		break;
	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		als_capture_stop(ch);
		break;
	}
	snd_mtxunlock(sc->lock);
	return 0;
}

static kobj_method_t alsrchan_methods[] = {
	KOBJMETHOD(channel_init,		alschan_init),
	KOBJMETHOD(channel_setformat,		alschan_setformat),
	KOBJMETHOD(channel_setspeed,		alschan_setspeed),
	KOBJMETHOD(channel_setblocksize,	alschan_setblocksize),
	KOBJMETHOD(channel_trigger,		alsrchan_trigger),
	KOBJMETHOD(channel_getptr,		alschan_getptr),
	KOBJMETHOD(channel_getcaps,		alschan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(alsrchan);

/* ------------------------------------------------------------------------- */
/* Mixer related */

/*
 * ALS4000 has an sb16 mixer, with some additional controls that we do
 * not yet a means to support.
 */

struct sb16props {
	u_int8_t lreg;
	u_int8_t rreg;
	u_int8_t bits;
	u_int8_t oselect;
	u_int8_t iselect; /* left input mask */
} static const amt[SOUND_MIXER_NRDEVICES] = {
	[SOUND_MIXER_VOLUME]  = { 0x30, 0x31, 5, 0x00, 0x00 },
	[SOUND_MIXER_PCM]     = { 0x32, 0x33, 5, 0x00, 0x00 },
	[SOUND_MIXER_SYNTH]   = { 0x34, 0x35, 5, 0x60, 0x40 },
	[SOUND_MIXER_CD]      = { 0x36, 0x37, 5, 0x06, 0x04 },
	[SOUND_MIXER_LINE]    = { 0x38, 0x39, 5, 0x18, 0x10 },
	[SOUND_MIXER_MIC]     = { 0x3a, 0x00, 5, 0x01, 0x01 },
	[SOUND_MIXER_SPEAKER] = { 0x3b, 0x00, 2, 0x00, 0x00 },
	[SOUND_MIXER_IGAIN]   = { 0x3f, 0x40, 2, 0x00, 0x00 },
	[SOUND_MIXER_OGAIN]   = { 0x41, 0x42, 2, 0x00, 0x00 },
	/* The following have register values but no h/w implementation */
	[SOUND_MIXER_TREBLE]  = { 0x44, 0x45, 4, 0x00, 0x00 },
	[SOUND_MIXER_BASS]    = { 0x46, 0x47, 4, 0x00, 0x00 }
};

static int
alsmix_init(struct snd_mixer *m)
{
	u_int32_t i, v;

	for (i = v = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (amt[i].bits) v |= 1 << i;
	}
	mix_setdevs(m, v);

	for (i = v = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (amt[i].iselect) v |= 1 << i;
	}
	mix_setrecdevs(m, v);
	return 0;
}

static int
alsmix_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
	struct sc_info *sc = mix_getdevinfo(m);
	u_int32_t r, l, v, mask;

	/* Fill upper n bits in mask with 1's */
	mask = ((1 << amt[dev].bits) - 1) << (8 - amt[dev].bits);

	l = (left * mask / 100) & mask;
	v = als_mix_rd(sc, amt[dev].lreg) & ~mask;
	als_mix_wr(sc, amt[dev].lreg, l | v);

	if (amt[dev].rreg) {
		r = (right * mask / 100) & mask;
		v = als_mix_rd(sc, amt[dev].rreg) & ~mask;
		als_mix_wr(sc, amt[dev].rreg, r | v);
	} else {
		r = 0;
	}

	/* Zero gain does not mute channel from output, but this does. */
	v = als_mix_rd(sc, SB16_OMASK);
	if (l == 0 && r == 0) {
		v &= ~amt[dev].oselect;
	} else {
		v |= amt[dev].oselect;
	}
	als_mix_wr(sc, SB16_OMASK, v);
	return 0;
}

static u_int32_t
alsmix_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	struct sc_info *sc = mix_getdevinfo(m);
	u_int32_t i, l, r;

	for (i = l = r = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (src & (1 << i)) {
			if (amt[i].iselect == 1) {	/* microphone */
				l |= amt[i].iselect;
				r |= amt[i].iselect;
			} else {
				l |= amt[i].iselect;
				r |= amt[i].iselect >> 1;
			}
		}
	}

	als_mix_wr(sc, SB16_IMASK_L, l);
	als_mix_wr(sc, SB16_IMASK_R, r);
	return src;
}

static kobj_method_t als_mixer_methods[] = {
	KOBJMETHOD(mixer_init,		alsmix_init),
	KOBJMETHOD(mixer_set,		alsmix_set),
	KOBJMETHOD(mixer_setrecsrc,	alsmix_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(als_mixer);

/* ------------------------------------------------------------------------- */
/* Interrupt Handler */

static void
als_intr(void *p)
{
	struct sc_info *sc = (struct sc_info *)p;
	u_int8_t intr, sb_status;

	snd_mtxlock(sc->lock);
	intr = als_intr_rd(sc);

	if (intr & 0x80) {
		snd_mtxunlock(sc->lock);
		chn_intr(sc->pch.channel);
		snd_mtxlock(sc->lock);
	}

	if (intr & 0x40) {
		snd_mtxunlock(sc->lock);
		chn_intr(sc->rch.channel);
		snd_mtxlock(sc->lock);
	}

	/* ACK interrupt in PCI core */
	als_intr_wr(sc, intr);

	/* ACK interrupt in SB core */
	sb_status = als_mix_rd(sc, IRQ_STAT);

	if (sb_status & ALS_IRQ_STATUS8)
		als_ack_read(sc, ALS_ESP_RD_STATUS8);
	if (sb_status & ALS_IRQ_STATUS16)
		als_ack_read(sc, ALS_ESP_RD_STATUS16);
	if (sb_status & ALS_IRQ_MPUIN)
		als_ack_read(sc, ALS_MIDI_DATA);
	if (sb_status & ALS_IRQ_CR1E)
		als_ack_read(sc, ALS_CR1E_ACK_PORT);

	snd_mtxunlock(sc->lock);
	return;
}

/* ------------------------------------------------------------------------- */
/* H/W initialization */

static int
als_init(struct sc_info *sc)
{
	u_int32_t i, v;

	/* Reset Chip */
	if (als_esp_reset(sc)) {
		return 1;
	}

	/* Enable write on DMA_SETUP register */
	v = als_mix_rd(sc, ALS_SB16_CONFIG);
	als_mix_wr(sc, ALS_SB16_CONFIG, v | 0x80);

	/* Select DMA0 */
	als_mix_wr(sc, ALS_SB16_DMA_SETUP, 0x01);

	/* Disable write on DMA_SETUP register */
	als_mix_wr(sc, ALS_SB16_CONFIG, v & 0x7f);

	/* Enable interrupts */
	v  = als_gcr_rd(sc, ALS_GCR_MISC);
	als_gcr_wr(sc, ALS_GCR_MISC, v | 0x28000);

	/* Black out GCR DMA registers */
	for (i = 0x91; i <= 0x96; i++) {
		als_gcr_wr(sc, i, 0);
	}

	/* Emulation mode */
	v = als_gcr_rd(sc, ALS_GCR_DMA_EMULATION);
	als_gcr_wr(sc, ALS_GCR_DMA_EMULATION, v);
	DEB(printf("GCR_DMA_EMULATION 0x%08x\n", v));
	return 0;
}

static void
als_uninit(struct sc_info *sc)
{
	/* Disable interrupts */
	als_gcr_wr(sc, ALS_GCR_MISC, 0);
}

/* ------------------------------------------------------------------------- */
/* Probe and attach card */

static int
als_pci_probe(device_t dev)
{
	if (pci_get_devid(dev) == ALS_PCI_ID0) {
		device_set_desc(dev, "Avance Logic ALS4000");
		return BUS_PROBE_DEFAULT;
	}
	return ENXIO;
}

static void
als_resource_free(device_t dev, struct sc_info *sc)
{
	if (sc->reg) {
		bus_release_resource(dev, SYS_RES_IOPORT, sc->regid, sc->reg);
		sc->reg = NULL;
	}
	if (sc->ih) {
		bus_teardown_intr(dev, sc->irq, sc->ih);
		sc->ih = NULL;
	}
	if (sc->irq) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->irqid, sc->irq);
		sc->irq = NULL;
	}
	if (sc->parent_dmat) {
		bus_dma_tag_destroy(sc->parent_dmat);
		sc->parent_dmat = 0;
	}
	if (sc->lock) {
		snd_mtxfree(sc->lock);
		sc->lock = NULL;
	}
}

static int
als_resource_grab(device_t dev, struct sc_info *sc)
{
	sc->regid = PCIR_BAR(0);
	sc->reg = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &sc->regid,
					 RF_ACTIVE);
	if (sc->reg == NULL) {
		device_printf(dev, "unable to allocate register space\n");
		goto bad;
	}
	sc->st = rman_get_bustag(sc->reg);
	sc->sh = rman_get_bushandle(sc->reg);

	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irqid,
					 RF_ACTIVE | RF_SHAREABLE);
	if (sc->irq == NULL) {
		device_printf(dev, "unable to allocate interrupt\n");
		goto bad;
	}

	if (snd_setup_intr(dev, sc->irq, INTR_MPSAFE, als_intr,
			   sc, &sc->ih)) {
		device_printf(dev, "unable to setup interrupt\n");
		goto bad;
	}

	sc->bufsz = pcm_getbuffersize(dev, 4096, ALS_DEFAULT_BUFSZ, 65536);

	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev),
			       /*alignment*/2, /*boundary*/0,
			       /*lowaddr*/BUS_SPACE_MAXADDR_24BIT,
			       /*highaddr*/BUS_SPACE_MAXADDR,
			       /*filter*/NULL, /*filterarg*/NULL,
			       /*maxsize*/sc->bufsz,
			       /*nsegments*/1, /*maxsegz*/0x3ffff,
			       /*flags*/0, /*lockfunc*/NULL,
			       /*lockarg*/NULL, &sc->parent_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		goto bad;
	}
	return 0;
 bad:
	als_resource_free(dev, sc);
	return ENXIO;
}

static int
als_pci_attach(device_t dev)
{
	struct sc_info *sc;
	char status[SND_STATUSLEN];

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->lock = snd_mtxcreate(device_get_nameunit(dev), "snd_als4000 softc");
	sc->dev = dev;

	pci_enable_busmaster(dev);
	/*
	 * By default the power to the various components on the
         * ALS4000 is entirely controlled by the pci powerstate.  We
         * could attempt finer grained control by setting GCR6.31.
	 */
	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		/* Reset the power state. */
		device_printf(dev, "chip is in D%d power mode "
			      "-- setting to D0\n", pci_get_powerstate(dev));
		pci_set_powerstate(dev, PCI_POWERSTATE_D0);
	}

	if (als_resource_grab(dev, sc)) {
		device_printf(dev, "failed to allocate resources\n");
		goto bad_attach;
	}

	if (als_init(sc)) {
		device_printf(dev, "failed to initialize hardware\n");
		goto bad_attach;
	}

	if (mixer_init(dev, &als_mixer_class, sc)) {
		device_printf(dev, "failed to initialize mixer\n");
		goto bad_attach;
	}

	if (pcm_register(dev, sc, 1, 1)) {
		device_printf(dev, "failed to register pcm entries\n");
		goto bad_attach;
	}

	pcm_addchan(dev, PCMDIR_PLAY, &alspchan_class, sc);
	pcm_addchan(dev, PCMDIR_REC,  &alsrchan_class, sc);

	snprintf(status, SND_STATUSLEN, "at io 0x%jx irq %jd %s",
		 rman_get_start(sc->reg), rman_get_start(sc->irq),PCM_KLDSTRING(snd_als4000));
	pcm_setstatus(dev, status);
	return 0;

 bad_attach:
	als_resource_free(dev, sc);
	free(sc, M_DEVBUF);
	return ENXIO;
}

static int
als_pci_detach(device_t dev)
{
	struct sc_info *sc;
	int r;

	r = pcm_unregister(dev);
	if (r)
		return r;

	sc = pcm_getdevinfo(dev);
	als_uninit(sc);
	als_resource_free(dev, sc);
	free(sc, M_DEVBUF);
	return 0;
}

static int
als_pci_suspend(device_t dev)
{
	struct sc_info *sc = pcm_getdevinfo(dev);

	snd_mtxlock(sc->lock);
	sc->pch.dma_was_active = als_playback_stop(&sc->pch);
	sc->rch.dma_was_active = als_capture_stop(&sc->rch);
	als_uninit(sc);
	snd_mtxunlock(sc->lock);
	return 0;
}

static int
als_pci_resume(device_t dev)
{
	struct sc_info *sc = pcm_getdevinfo(dev);


	snd_mtxlock(sc->lock);
	if (als_init(sc) != 0) {
		device_printf(dev, "unable to reinitialize the card\n");
		snd_mtxunlock(sc->lock);
		return ENXIO;
	}

	if (mixer_reinit(dev) != 0) {
		device_printf(dev, "unable to reinitialize the mixer\n");
		snd_mtxunlock(sc->lock);
		return ENXIO;
	}

	if (sc->pch.dma_was_active) {
		als_playback_start(&sc->pch);
	}

	if (sc->rch.dma_was_active) {
		als_capture_start(&sc->rch);
	}
	snd_mtxunlock(sc->lock);

	return 0;
}

static device_method_t als_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		als_pci_probe),
	DEVMETHOD(device_attach,	als_pci_attach),
	DEVMETHOD(device_detach,	als_pci_detach),
	DEVMETHOD(device_suspend,	als_pci_suspend),
	DEVMETHOD(device_resume,	als_pci_resume),
	{ 0, 0 }
};

static driver_t als_driver = {
	"pcm",
	als_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_als4000, pci, als_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_als4000, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_als4000, 1);
