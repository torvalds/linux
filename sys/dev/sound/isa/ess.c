/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Cameron Grant <cg@freebsd.org>
 * Copyright (c) 1997,1998 Luigi Rizzo
 *
 * Derived from files in the Voxware 3.5 distribution,
 * Copyright by Hannu Savolainen 1994, under the same copyright
 * conditions.
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

#include  <dev/sound/isa/sb.h>
#include  <dev/sound/chip.h>

#include <isa/isavar.h>

#include "mixer_if.h"

SND_DECLARE_FILE("$FreeBSD$");

#define ESS_BUFFSIZE (4096)
#define ABS(x) (((x) < 0)? -(x) : (x))

/* audio2 never generates irqs and sounds very noisy */
#undef ESS18XX_DUPLEX

/* more accurate clocks and split audio1/audio2 rates */
#define ESS18XX_NEWSPEED

static u_int32_t ess_pfmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S8, 1, 0),
	SND_FORMAT(AFMT_S8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	SND_FORMAT(AFMT_U16_LE, 1, 0),
	SND_FORMAT(AFMT_U16_LE, 2, 0),
	0
};

static struct pcmchan_caps ess_playcaps = {6000, 48000, ess_pfmt, 0};

static u_int32_t ess_rfmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S8, 1, 0),
	SND_FORMAT(AFMT_S8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	SND_FORMAT(AFMT_U16_LE, 1, 0),
	SND_FORMAT(AFMT_U16_LE, 2, 0),
	0
};

static struct pcmchan_caps ess_reccaps = {6000, 48000, ess_rfmt, 0};

struct ess_info;

struct ess_chinfo {
	struct ess_info *parent;
	struct pcm_channel *channel;
	struct snd_dbuf *buffer;
	int dir, hwch, stopping, run;
	u_int32_t fmt, spd, blksz;
};

struct ess_info {
	device_t parent_dev;
    	struct resource *io_base;	/* I/O address for the board */
    	struct resource *irq;
   	struct resource *drq1;
    	struct resource *drq2;
    	void *ih;
    	bus_dma_tag_t parent_dmat;

	unsigned int bufsize;
    	int type;
	unsigned int duplex:1, newspeed:1;
    	u_long bd_flags;       /* board-specific flags */
    	struct ess_chinfo pch, rch;
};

#if 0
static int ess_rd(struct ess_info *sc, int reg);
static void ess_wr(struct ess_info *sc, int reg, u_int8_t val);
static int ess_dspready(struct ess_info *sc);
static int ess_cmd(struct ess_info *sc, u_char val);
static int ess_cmd1(struct ess_info *sc, u_char cmd, int val);
static int ess_get_byte(struct ess_info *sc);
static void ess_setmixer(struct ess_info *sc, u_int port, u_int value);
static int ess_getmixer(struct ess_info *sc, u_int port);
static int ess_reset_dsp(struct ess_info *sc);

static int ess_write(struct ess_info *sc, u_char reg, int val);
static int ess_read(struct ess_info *sc, u_char reg);

static void ess_intr(void *arg);
static int ess_setupch(struct ess_info *sc, int ch, int dir, int spd, u_int32_t fmt, int len);
static int ess_start(struct ess_chinfo *ch);
static int ess_stop(struct ess_chinfo *ch);
#endif

/*
 * Common code for the midi and pcm functions
 *
 * ess_cmd write a single byte to the CMD port.
 * ess_cmd1 write a CMD + 1 byte arg
 * ess_cmd2 write a CMD + 2 byte arg
 * ess_get_byte returns a single byte from the DSP data port
 *
 * ess_write is actually ess_cmd1
 * ess_read access ext. regs via ess_cmd(0xc0, reg) followed by ess_get_byte
 */

static void
ess_lock(struct ess_info *sc) {

	sbc_lock(device_get_softc(sc->parent_dev));
}

static void
ess_unlock(struct ess_info *sc) {

	sbc_unlock(device_get_softc(sc->parent_dev));
}

static int
port_rd(struct resource *port, int off)
{
	return bus_space_read_1(rman_get_bustag(port),
				rman_get_bushandle(port),
				off);
}

static void
port_wr(struct resource *port, int off, u_int8_t data)
{
	bus_space_write_1(rman_get_bustag(port),
			  rman_get_bushandle(port),
			  off, data);
}

static int
ess_rd(struct ess_info *sc, int reg)
{
	return port_rd(sc->io_base, reg);
}

static void
ess_wr(struct ess_info *sc, int reg, u_int8_t val)
{
	port_wr(sc->io_base, reg, val);
}

static int
ess_dspready(struct ess_info *sc)
{
	return ((ess_rd(sc, SBDSP_STATUS) & 0x80) == 0);
}

static int
ess_dspwr(struct ess_info *sc, u_char val)
{
    	int  i;

    	for (i = 0; i < 1000; i++) {
		if (ess_dspready(sc)) {
	    		ess_wr(sc, SBDSP_CMD, val);
	    		return 1;
		}
		if (i > 10) DELAY((i > 100)? 1000 : 10);
    	}
    	printf("ess_dspwr(0x%02x) timed out.\n", val);
    	return 0;
}

static int
ess_cmd(struct ess_info *sc, u_char val)
{
#if 0
	printf("ess_cmd: %x\n", val);
#endif
    	return ess_dspwr(sc, val);
}

static int
ess_cmd1(struct ess_info *sc, u_char cmd, int val)
{
#if 0
    	printf("ess_cmd1: %x, %x\n", cmd, val);
#endif
    	if (ess_dspwr(sc, cmd)) {
		return ess_dspwr(sc, val & 0xff);
    	} else return 0;
}

static void
ess_setmixer(struct ess_info *sc, u_int port, u_int value)
{
	DEB(printf("ess_setmixer: reg=%x, val=%x\n", port, value);)
    	ess_wr(sc, SB_MIX_ADDR, (u_char) (port & 0xff)); /* Select register */
    	DELAY(10);
    	ess_wr(sc, SB_MIX_DATA, (u_char) (value & 0xff));
    	DELAY(10);
}

static int
ess_getmixer(struct ess_info *sc, u_int port)
{
    	int val;
    	ess_wr(sc, SB_MIX_ADDR, (u_char) (port & 0xff)); /* Select register */
    	DELAY(10);
    	val = ess_rd(sc, SB_MIX_DATA);
    	DELAY(10);

    	return val;
}

static int
ess_get_byte(struct ess_info *sc)
{
    	int i;

    	for (i = 1000; i > 0; i--) {
		if (ess_rd(sc, DSP_DATA_AVAIL) & 0x80)
			return ess_rd(sc, DSP_READ);
		else
			DELAY(20);
    	}
    	return -1;
}

static int
ess_write(struct ess_info *sc, u_char reg, int val)
{
    	return ess_cmd1(sc, reg, val);
}

static int
ess_read(struct ess_info *sc, u_char reg)
{
    	return (ess_cmd(sc, 0xc0) && ess_cmd(sc, reg))? ess_get_byte(sc) : -1;
}

static int
ess_reset_dsp(struct ess_info *sc)
{
    	ess_wr(sc, SBDSP_RST, 3);
    	DELAY(100);
    	ess_wr(sc, SBDSP_RST, 0);
    	if (ess_get_byte(sc) != 0xAA) {
        	DEB(printf("ess_reset_dsp 0x%lx failed\n",
			   rman_get_start(sc->io_base)));
		return ENXIO;	/* Sorry */
    	}
    	ess_cmd(sc, 0xc6);
    	return 0;
}

static void
ess_release_resources(struct ess_info *sc, device_t dev)
{
    	if (sc->irq) {
    		if (sc->ih)
			bus_teardown_intr(dev, sc->irq, sc->ih);
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq);
		sc->irq = NULL;
    	}
    	if (sc->drq1) {
		isa_dma_release(rman_get_start(sc->drq1));
		bus_release_resource(dev, SYS_RES_DRQ, 0, sc->drq1);
		sc->drq1 = NULL;
    	}
    	if (sc->drq2) {
		isa_dma_release(rman_get_start(sc->drq2));
		bus_release_resource(dev, SYS_RES_DRQ, 1, sc->drq2);
		sc->drq2 = NULL;
    	}
    	if (sc->io_base) {
		bus_release_resource(dev, SYS_RES_IOPORT, 0, sc->io_base);
		sc->io_base = NULL;
    	}
    	if (sc->parent_dmat) {
		bus_dma_tag_destroy(sc->parent_dmat);
		sc->parent_dmat = 0;
    	}
     	free(sc, M_DEVBUF);
}

static int
ess_alloc_resources(struct ess_info *sc, device_t dev)
{
	int rid;

	rid = 0;
	if (!sc->io_base)
    		sc->io_base = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
						     &rid, RF_ACTIVE);
	rid = 0;
	if (!sc->irq)
    		sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
						 &rid, RF_ACTIVE);
	rid = 0;
	if (!sc->drq1)
    		sc->drq1 = bus_alloc_resource_any(dev, SYS_RES_DRQ,
						  &rid, RF_ACTIVE);
	rid = 1;
	if (!sc->drq2)
        	sc->drq2 = bus_alloc_resource_any(dev, SYS_RES_DRQ,
						  &rid, RF_ACTIVE);

    	if (sc->io_base && sc->drq1 && sc->irq) {
  		isa_dma_acquire(rman_get_start(sc->drq1));
		isa_dmainit(rman_get_start(sc->drq1), sc->bufsize);

		if (sc->drq2) {
			isa_dma_acquire(rman_get_start(sc->drq2));
			isa_dmainit(rman_get_start(sc->drq2), sc->bufsize);
		}

		return 0;
	} else return ENXIO;
}

static void
ess_intr(void *arg)
{
    	struct ess_info *sc = (struct ess_info *)arg;
	int src, pirq, rirq;

	ess_lock(sc);
	src = 0;
	if (ess_getmixer(sc, 0x7a) & 0x80)
		src |= 2;
	if (ess_rd(sc, 0x0c) & 0x01)
		src |= 1;

	pirq = (src & sc->pch.hwch)? 1 : 0;
	rirq = (src & sc->rch.hwch)? 1 : 0;

	if (pirq) {
		if (sc->pch.run) {
			ess_unlock(sc);
			chn_intr(sc->pch.channel);
			ess_lock(sc);
		}
		if (sc->pch.stopping) {
			sc->pch.run = 0;
			sndbuf_dma(sc->pch.buffer, PCMTRIG_STOP);
			sc->pch.stopping = 0;
			if (sc->pch.hwch == 1)
				ess_write(sc, 0xb8, ess_read(sc, 0xb8) & ~0x01);
			else
				ess_setmixer(sc, 0x78, ess_getmixer(sc, 0x78) & ~0x03);
		}
	}

	if (rirq) {
		if (sc->rch.run) {
			ess_unlock(sc);
			chn_intr(sc->rch.channel);
			ess_lock(sc);
		}
		if (sc->rch.stopping) {
			sc->rch.run = 0;
			sndbuf_dma(sc->rch.buffer, PCMTRIG_STOP);
			sc->rch.stopping = 0;
			/* XXX: will this stop audio2? */
			ess_write(sc, 0xb8, ess_read(sc, 0xb8) & ~0x01);
		}
	}

	if (src & 2)
		ess_setmixer(sc, 0x7a, ess_getmixer(sc, 0x7a) & ~0x80);
	if (src & 1)
    		ess_rd(sc, DSP_DATA_AVAIL);
	ess_unlock(sc);
}

/* utility functions for ESS */
static u_int8_t
ess_calcspeed8(int *spd)
{
	int speed = *spd;
	u_int32_t t;

	if (speed > 22000) {
		t = (795500 + speed / 2) / speed;
		speed = (795500 + t / 2) / t;
		t = (256 - t) | 0x80;
	} else {
		t = (397700 + speed / 2) / speed;
		speed = (397700 + t / 2) / t;
		t = 128 - t;
	}
	*spd = speed;
	return t & 0x000000ff;
}

static u_int8_t
ess_calcspeed9(int *spd)
{
	int speed, s0, s1, use0;
	u_int8_t t0, t1;

	/* rate = source / (256 - divisor) */
	/* divisor = 256 - (source / rate) */
	speed = *spd;
	t0 = 128 - (793800 / speed);
	s0 = 793800 / (128 - t0);

	t1 = 128 - (768000 / speed);
	s1 = 768000 / (128 - t1);
	t1 |= 0x80;

	use0 = (ABS(speed - s0) < ABS(speed - s1))? 1 : 0;

	*spd = use0? s0 : s1;
	return use0? t0 : t1;
}

static u_int8_t
ess_calcfilter(int spd)
{
	int cutoff;

	/* cutoff = 7160000 / (256 - divisor) */
	/* divisor = 256 - (7160000 / cutoff) */
	cutoff = (spd * 9 * 82) / 20;
	return (256 - (7160000 / cutoff));
}

static int
ess_setupch(struct ess_info *sc, int ch, int dir, int spd, u_int32_t fmt, int len)
{
	int play = (dir == PCMDIR_PLAY)? 1 : 0;
	int b16 = (fmt & AFMT_16BIT)? 1 : 0;
	int stereo = (AFMT_CHANNEL(fmt) > 1)? 1 : 0;
	int unsign = (fmt == AFMT_U8 || fmt == AFMT_U16_LE)? 1 : 0;
	u_int8_t spdval, fmtval;


	spdval = (sc->newspeed)? ess_calcspeed9(&spd) : ess_calcspeed8(&spd);
	len = -len;

	if (ch == 1) {
		KASSERT((dir == PCMDIR_PLAY) || (dir == PCMDIR_REC), ("ess_setupch: dir1 bad"));
		/* transfer length low */
		ess_write(sc, 0xa4, len & 0x00ff);
		/* transfer length high */
		ess_write(sc, 0xa5, (len & 0xff00) >> 8);
		/* autoinit, dma dir */
		ess_write(sc, 0xb8, 0x04 | (play? 0x00 : 0x0a));
		/* mono/stereo */
		ess_write(sc, 0xa8, (ess_read(sc, 0xa8) & ~0x03) | (stereo? 0x01 : 0x02));
		/* demand mode, 4 bytes/xfer */
		ess_write(sc, 0xb9, 0x02);
		/* sample rate */
        	ess_write(sc, 0xa1, spdval);
		/* filter cutoff */
		ess_write(sc, 0xa2, ess_calcfilter(spd));
		/* setup dac/adc */
		if (play)
			ess_write(sc, 0xb6, unsign? 0x80 : 0x00);
		/* mono, b16: signed, load signal */
		ess_write(sc, 0xb7, 0x51 | (unsign? 0x00 : 0x20));
		/* setup fifo */
		ess_write(sc, 0xb7, 0x90 | (unsign? 0x00 : 0x20) |
					   (b16? 0x04 : 0x00) |
					   (stereo? 0x08 : 0x40));
		/* irq control */
		ess_write(sc, 0xb1, (ess_read(sc, 0xb1) & 0x0f) | 0x50);
		/* drq control */
		ess_write(sc, 0xb2, (ess_read(sc, 0xb2) & 0x0f) | 0x50);
	} else if (ch == 2) {
		KASSERT(dir == PCMDIR_PLAY, ("ess_setupch: dir2 bad"));
		/* transfer length low */
		ess_setmixer(sc, 0x74, len & 0x00ff);
		/* transfer length high */
		ess_setmixer(sc, 0x76, (len & 0xff00) >> 8);
		/* autoinit, 4 bytes/req */
		ess_setmixer(sc, 0x78, 0x90);
		fmtval = b16 | (stereo << 1) | (unsign << 2);
		/* enable irq, set format */
		ess_setmixer(sc, 0x7a, 0x40 | fmtval);
		if (sc->newspeed) {
			/* sample rate */
			ess_setmixer(sc, 0x70, spdval);
			/* filter cutoff */
			ess_setmixer(sc, 0x72, ess_calcfilter(spd));
		}
	}

	return 0;
}
static int
ess_start(struct ess_chinfo *ch)
{
	struct ess_info *sc = ch->parent;
    	int play = (ch->dir == PCMDIR_PLAY)? 1 : 0;

	ess_lock(sc);
	ess_setupch(sc, ch->hwch, ch->dir, ch->spd, ch->fmt, ch->blksz);
	ch->stopping = 0;
	if (ch->hwch == 1)
		ess_write(sc, 0xb8, ess_read(sc, 0xb8) | 0x01);
	else
		ess_setmixer(sc, 0x78, ess_getmixer(sc, 0x78) | 0x03);
	if (play)
		ess_cmd(sc, DSP_CMD_SPKON);
	ess_unlock(sc);
	return 0;
}

static int
ess_stop(struct ess_chinfo *ch)
{
	struct ess_info *sc = ch->parent;
    	int play = (ch->dir == PCMDIR_PLAY)? 1 : 0;

	ess_lock(sc);
	ch->stopping = 1;
	if (ch->hwch == 1)
		ess_write(sc, 0xb8, ess_read(sc, 0xb8) & ~0x04);
	else
		ess_setmixer(sc, 0x78, ess_getmixer(sc, 0x78) & ~0x10);
	if (play)
		ess_cmd(sc, DSP_CMD_SPKOFF);
	ess_unlock(sc);
	return 0;
}

/* -------------------------------------------------------------------- */
/* channel interface for ESS18xx */
static void *
esschan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct ess_info *sc = devinfo;
	struct ess_chinfo *ch = (dir == PCMDIR_PLAY)? &sc->pch : &sc->rch;

	ch->parent = sc;
	ch->channel = c;
	ch->buffer = b;
	if (sndbuf_alloc(ch->buffer, sc->parent_dmat, 0, sc->bufsize) != 0)
		return NULL;
	ch->dir = dir;
	ch->hwch = 1;
	if ((dir == PCMDIR_PLAY) && (sc->duplex))
		ch->hwch = 2;
	sndbuf_dmasetup(ch->buffer, (ch->hwch == 1)? sc->drq1 : sc->drq2);
	return ch;
}

static int
esschan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct ess_chinfo *ch = data;

	ch->fmt = format;
	return 0;
}

static u_int32_t
esschan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct ess_chinfo *ch = data;
	struct ess_info *sc = ch->parent;

	ch->spd = speed;
	if (sc->newspeed)
		ess_calcspeed9(&ch->spd);
	else
		ess_calcspeed8(&ch->spd);
	return ch->spd;
}

static u_int32_t
esschan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct ess_chinfo *ch = data;

	ch->blksz = blocksize;
	return ch->blksz;
}

static int
esschan_trigger(kobj_t obj, void *data, int go)
{
	struct ess_chinfo *ch = data;

	if (!PCMTRIG_COMMON(go))
		return 0;

	switch (go) {
	case PCMTRIG_START:
		ch->run = 1;
		sndbuf_dma(ch->buffer, go);
		ess_start(ch);
		break;

	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
	default:
		ess_stop(ch);
		break;
	}
	return 0;
}

static u_int32_t
esschan_getptr(kobj_t obj, void *data)
{
	struct ess_chinfo *ch = data;

	return sndbuf_dmaptr(ch->buffer);
}

static struct pcmchan_caps *
esschan_getcaps(kobj_t obj, void *data)
{
	struct ess_chinfo *ch = data;

	return (ch->dir == PCMDIR_PLAY)? &ess_playcaps : &ess_reccaps;
}

static kobj_method_t esschan_methods[] = {
    	KOBJMETHOD(channel_init,		esschan_init),
    	KOBJMETHOD(channel_setformat,		esschan_setformat),
    	KOBJMETHOD(channel_setspeed,		esschan_setspeed),
    	KOBJMETHOD(channel_setblocksize,	esschan_setblocksize),
    	KOBJMETHOD(channel_trigger,		esschan_trigger),
    	KOBJMETHOD(channel_getptr,		esschan_getptr),
    	KOBJMETHOD(channel_getcaps,		esschan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(esschan);

/************************************************************/

static int
essmix_init(struct snd_mixer *m)
{
    	struct ess_info *sc = mix_getdevinfo(m);

	mix_setrecdevs(m, SOUND_MASK_CD | SOUND_MASK_MIC | SOUND_MASK_LINE |
			  SOUND_MASK_IMIX);

	mix_setdevs(m, SOUND_MASK_SYNTH | SOUND_MASK_PCM | SOUND_MASK_LINE |
		       SOUND_MASK_MIC | SOUND_MASK_CD | SOUND_MASK_VOLUME |
		       SOUND_MASK_LINE1 | SOUND_MASK_SPEAKER);

	ess_setmixer(sc, 0, 0); /* reset */

	return 0;
}

static int
essmix_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
    	struct ess_info *sc = mix_getdevinfo(m);
    	int preg = 0, rreg = 0, l, r;

	l = (left * 15) / 100;
	r = (right * 15) / 100;
	switch (dev) {
	case SOUND_MIXER_SYNTH:
		preg = 0x36;
		rreg = 0x6b;
		break;

	case SOUND_MIXER_PCM:
		preg = 0x14;
		rreg = 0x7c;
		break;

	case SOUND_MIXER_LINE:
		preg = 0x3e;
		rreg = 0x6e;
		break;

	case SOUND_MIXER_MIC:
		preg = 0x1a;
		rreg = 0x68;
		break;

	case SOUND_MIXER_LINE1:
		preg = 0x3a;
		rreg = 0x6c;
		break;

	case SOUND_MIXER_CD:
		preg = 0x38;
		rreg = 0x6a;
		break;

	case SOUND_MIXER_SPEAKER:
		preg = 0x3c;
		break;

 	case SOUND_MIXER_VOLUME:
		l = left? (left * 63) / 100 : 64;
		r = right? (right * 63) / 100 : 64;
		ess_setmixer(sc, 0x60, l);
		ess_setmixer(sc, 0x62, r);
		left = (l == 64)? 0 : (l * 100) / 63;
		right = (r == 64)? 0 : (r * 100) / 63;
    		return left | (right << 8);
	}

	if (preg)
		ess_setmixer(sc, preg, (l << 4) | r);
	if (rreg)
		ess_setmixer(sc, rreg, (l << 4) | r);

	left = (l * 100) / 15;
	right = (r * 100) / 15;

    	return left | (right << 8);
}

static u_int32_t
essmix_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
    	struct ess_info *sc = mix_getdevinfo(m);
    	u_char recdev;

    	switch (src) {
	case SOUND_MASK_CD:
		recdev = 0x02;
		break;

	case SOUND_MASK_LINE:
		recdev = 0x06;
		break;

	case SOUND_MASK_IMIX:
		recdev = 0x05;
		break;

	case SOUND_MASK_MIC:
	default:
		recdev = 0x00;
		src = SOUND_MASK_MIC;
		break;
	}

	ess_setmixer(sc, 0x1c, recdev);

	return src;
}

static kobj_method_t essmixer_methods[] = {
    	KOBJMETHOD(mixer_init,		essmix_init),
    	KOBJMETHOD(mixer_set,		essmix_set),
    	KOBJMETHOD(mixer_setrecsrc,	essmix_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(essmixer);

/************************************************************/

static int
ess_probe(device_t dev)
{
	uintptr_t func, ver, r, f;

	/* The parent device has already been probed. */
	r = BUS_READ_IVAR(device_get_parent(dev), dev, 0, &func);
	if (func != SCF_PCM)
		return (ENXIO);

	r = BUS_READ_IVAR(device_get_parent(dev), dev, 1, &ver);
	f = (ver & 0xffff0000) >> 16;
	if (!(f & BD_F_ESS))
		return (ENXIO);

    	device_set_desc(dev, "ESS 18xx DSP");

	return 0;
}

static int
ess_attach(device_t dev)
{
    	struct ess_info *sc;
    	char status[SND_STATUSLEN], buf[64];
	int ver;

    	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->parent_dev = device_get_parent(dev);
	sc->bufsize = pcm_getbuffersize(dev, 4096, ESS_BUFFSIZE, 65536);
    	if (ess_alloc_resources(sc, dev))
		goto no;
    	if (ess_reset_dsp(sc))
		goto no;
    	if (mixer_init(dev, &essmixer_class, sc))
		goto no;

	sc->duplex = 0;
	sc->newspeed = 0;
	ver = (ess_getmixer(sc, 0x40) << 8) | ess_rd(sc, SB_MIX_DATA);
	snprintf(buf, sizeof buf, "ESS %x DSP", ver);
	device_set_desc_copy(dev, buf);
	if (bootverbose)
		device_printf(dev, "ESS%x detected", ver);

	switch (ver) {
	case 0x1869:
	case 0x1879:
#ifdef ESS18XX_DUPLEX
		sc->duplex = sc->drq2? 1 : 0;
#endif
#ifdef ESS18XX_NEWSPEED
		sc->newspeed = 1;
#endif
		break;
	}
	if (bootverbose)
		printf("%s%s\n", sc->duplex? ", duplex" : "",
				 sc->newspeed? ", newspeed" : "");

	if (sc->newspeed)
		ess_setmixer(sc, 0x71, 0x22);

	snd_setup_intr(dev, sc->irq, 0, ess_intr, sc, &sc->ih);
    	if (!sc->duplex)
		pcm_setflags(dev, pcm_getflags(dev) | SD_F_SIMPLEX);

    	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev), /*alignment*/2,
			/*boundary*/0,
			/*lowaddr*/BUS_SPACE_MAXADDR_24BIT,
			/*highaddr*/BUS_SPACE_MAXADDR,
			/*filter*/NULL, /*filterarg*/NULL,
			/*maxsize*/sc->bufsize, /*nsegments*/1,
			/*maxsegz*/0x3ffff,
			/*flags*/0, /*lockfunc*/busdma_lock_mutex,
			/*lockarg*/&Giant, &sc->parent_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		goto no;
    	}

    	if (sc->drq2)
		snprintf(buf, SND_STATUSLEN, ":%jd", rman_get_start(sc->drq2));
	else
		buf[0] = '\0';

    	snprintf(status, SND_STATUSLEN, "at io 0x%jx irq %jd drq %jd%s bufsz %u %s",
		rman_get_start(sc->io_base), rman_get_start(sc->irq),
		rman_get_start(sc->drq1), buf, sc->bufsize,
		PCM_KLDSTRING(snd_ess));

    	if (pcm_register(dev, sc, 1, 1))
		goto no;
      	pcm_addchan(dev, PCMDIR_REC, &esschan_class, sc);
	pcm_addchan(dev, PCMDIR_PLAY, &esschan_class, sc);
	pcm_setstatus(dev, status);

    	return 0;

no:
    	ess_release_resources(sc, dev);
    	return ENXIO;
}

static int
ess_detach(device_t dev)
{
	int r;
	struct ess_info *sc;

	r = pcm_unregister(dev);
	if (r)
		return r;

	sc = pcm_getdevinfo(dev);
    	ess_release_resources(sc, dev);
	return 0;
}

static int
ess_resume(device_t dev)
{
	struct ess_info *sc;

	sc = pcm_getdevinfo(dev);

	if (ess_reset_dsp(sc)) {
		device_printf(dev, "unable to reset DSP at resume\n");
		return ENXIO;
	}

	if (mixer_reinit(dev)) {
		device_printf(dev, "unable to reinitialize mixer at resume\n");
		return ENXIO;
	}

	return 0;
}

static device_method_t ess_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ess_probe),
	DEVMETHOD(device_attach,	ess_attach),
	DEVMETHOD(device_detach,	ess_detach),
	DEVMETHOD(device_resume,	ess_resume),

	{ 0, 0 }
};

static driver_t ess_driver = {
	"pcm",
	ess_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_ess, sbc, ess_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_ess, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_DEPEND(snd_ess, snd_sbc, 1, 1, 1);
MODULE_VERSION(snd_ess, 1);

/************************************************************/

static devclass_t esscontrol_devclass;

static struct isa_pnp_id essc_ids[] = {
	{0x06007316, "ESS Control"},
	{0}
};

static int
esscontrol_probe(device_t dev)
{
	int i;

	i = ISA_PNP_PROBE(device_get_parent(dev), dev, essc_ids);
	if (i == 0)
		device_quiet(dev);
	return i;
}

static int
esscontrol_attach(device_t dev)
{
#ifdef notyet
    	struct resource *io;
	int rid, i, x;

	rid = 0;
    	io = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid, RF_ACTIVE);
	x = 0;
	for (i = 0; i < 0x100; i++) {
		port_wr(io, 0, i);
		x = port_rd(io, 1);
		if ((i & 0x0f) == 0)
			printf("%3.3x: ", i);
		printf("%2.2x ", x);
		if ((i & 0x0f) == 0x0f)
			printf("\n");
	}
	bus_release_resource(dev, SYS_RES_IOPORT, 0, io);
	io = NULL;
#endif

    	return 0;
}

static int
esscontrol_detach(device_t dev)
{
	return 0;
}

static device_method_t esscontrol_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		esscontrol_probe),
	DEVMETHOD(device_attach,	esscontrol_attach),
	DEVMETHOD(device_detach,	esscontrol_detach),

	{ 0, 0 }
};

static driver_t esscontrol_driver = {
	"esscontrol",
	esscontrol_methods,
	1,
};

DRIVER_MODULE(esscontrol, isa, esscontrol_driver, esscontrol_devclass, 0, 0);
DRIVER_MODULE(esscontrol, acpi, esscontrol_driver, esscontrol_devclass, 0, 0);
ISA_PNP_INFO(essc_ids);
