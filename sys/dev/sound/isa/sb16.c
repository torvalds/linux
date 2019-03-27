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

#define SB16_BUFFSIZE	4096
#define PLAIN_SB16(x) ((((x)->bd_flags) & (BD_F_SB16|BD_F_SB16X)) == BD_F_SB16)

static u_int32_t sb16_fmt8[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	0
};
static struct pcmchan_caps sb16_caps8 = {5000, 45000, sb16_fmt8, 0};

static u_int32_t sb16_fmt16[] = {
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};
static struct pcmchan_caps sb16_caps16 = {5000, 45000, sb16_fmt16, 0};

static u_int32_t sb16x_fmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};
static struct pcmchan_caps sb16x_caps = {5000, 49000, sb16x_fmt, 0};

struct sb_info;

struct sb_chinfo {
	struct sb_info *parent;
	struct pcm_channel *channel;
	struct snd_dbuf *buffer;
	int dir, run, dch;
	u_int32_t fmt, spd, blksz;
};

struct sb_info {
    	struct resource *io_base;	/* I/O address for the board */
    	struct resource *irq;
   	struct resource *drq1;
    	struct resource *drq2;
    	void *ih;
    	bus_dma_tag_t parent_dmat;

	unsigned int bufsize;
    	int bd_id;
    	u_long bd_flags;       /* board-specific flags */
	int prio, prio16;
    	struct sb_chinfo pch, rch;
	device_t parent_dev;
};

#if 0
static void sb_lock(struct sb_info *sb);
static void sb_unlock(struct sb_info *sb);
static int sb_rd(struct sb_info *sb, int reg);
static void sb_wr(struct sb_info *sb, int reg, u_int8_t val);
static int sb_cmd(struct sb_info *sb, u_char val);
/* static int sb_cmd1(struct sb_info *sb, u_char cmd, int val); */
static int sb_cmd2(struct sb_info *sb, u_char cmd, int val);
static u_int sb_get_byte(struct sb_info *sb);
static void sb_setmixer(struct sb_info *sb, u_int port, u_int value);
static int sb_getmixer(struct sb_info *sb, u_int port);
static int sb_reset_dsp(struct sb_info *sb);

static void sb_intr(void *arg);
#endif

/*
 * Common code for the midi and pcm functions
 *
 * sb_cmd write a single byte to the CMD port.
 * sb_cmd1 write a CMD + 1 byte arg
 * sb_cmd2 write a CMD + 2 byte arg
 * sb_get_byte returns a single byte from the DSP data port
 */

static void
sb_lock(struct sb_info *sb) {

	sbc_lock(device_get_softc(sb->parent_dev));
}

static void
sb_lockassert(struct sb_info *sb) {

	sbc_lockassert(device_get_softc(sb->parent_dev));
}

static void
sb_unlock(struct sb_info *sb) {

	sbc_unlock(device_get_softc(sb->parent_dev));
}

static int
port_rd(struct resource *port, int off)
{
	return bus_space_read_1(rman_get_bustag(port), rman_get_bushandle(port), off);
}

static void
port_wr(struct resource *port, int off, u_int8_t data)
{
	bus_space_write_1(rman_get_bustag(port), rman_get_bushandle(port), off, data);
}

static int
sb_rd(struct sb_info *sb, int reg)
{
	return port_rd(sb->io_base, reg);
}

static void
sb_wr(struct sb_info *sb, int reg, u_int8_t val)
{
	port_wr(sb->io_base, reg, val);
}

static int
sb_dspwr(struct sb_info *sb, u_char val)
{
    	int  i;

    	for (i = 0; i < 1000; i++) {
		if ((sb_rd(sb, SBDSP_STATUS) & 0x80))
	    		DELAY((i > 100)? 1000 : 10);
	    	else {
			sb_wr(sb, SBDSP_CMD, val);
			return 1;
		}
    	}
	if (curthread->td_intr_nesting_level == 0)
		printf("sb_dspwr(0x%02x) timed out.\n", val);
    	return 0;
}

static int
sb_cmd(struct sb_info *sb, u_char val)
{
#if 0
	printf("sb_cmd: %x\n", val);
#endif
    	return sb_dspwr(sb, val);
}

/*
static int
sb_cmd1(struct sb_info *sb, u_char cmd, int val)
{
#if 0
    	printf("sb_cmd1: %x, %x\n", cmd, val);
#endif
    	if (sb_dspwr(sb, cmd)) {
		return sb_dspwr(sb, val & 0xff);
    	} else return 0;
}
*/

static int
sb_cmd2(struct sb_info *sb, u_char cmd, int val)
{
	int r;

#if 0
    	printf("sb_cmd2: %x, %x\n", cmd, val);
#endif
	sb_lockassert(sb);
	r = 0;
    	if (sb_dspwr(sb, cmd)) {
		if (sb_dspwr(sb, val & 0xff)) {
			if (sb_dspwr(sb, (val >> 8) & 0xff)) {
				r = 1;
			}
		}
    	}

	return r;
}

/*
 * in the SB, there is a set of indirect "mixer" registers with
 * address at offset 4, data at offset 5
 */
static void
sb_setmixer(struct sb_info *sb, u_int port, u_int value)
{
	sb_lock(sb);
    	sb_wr(sb, SB_MIX_ADDR, (u_char) (port & 0xff)); /* Select register */
    	DELAY(10);
    	sb_wr(sb, SB_MIX_DATA, (u_char) (value & 0xff));
    	DELAY(10);
	sb_unlock(sb);
}

static int
sb_getmixer(struct sb_info *sb, u_int port)
{
    	int val;

    	sb_lockassert(sb);
    	sb_wr(sb, SB_MIX_ADDR, (u_char) (port & 0xff)); /* Select register */
    	DELAY(10);
    	val = sb_rd(sb, SB_MIX_DATA);
    	DELAY(10);

    	return val;
}

static u_int
sb_get_byte(struct sb_info *sb)
{
    	int i;

    	for (i = 1000; i > 0; i--) {
		if (sb_rd(sb, DSP_DATA_AVAIL) & 0x80)
			return sb_rd(sb, DSP_READ);
		else
			DELAY(20);
    	}
    	return 0xffff;
}

static int
sb_reset_dsp(struct sb_info *sb)
{
	u_char b;

	sb_lockassert(sb);
    	sb_wr(sb, SBDSP_RST, 3);
    	DELAY(100);
    	sb_wr(sb, SBDSP_RST, 0);
	b = sb_get_byte(sb);
    	if (b != 0xAA) {
        	DEB(printf("sb_reset_dsp 0x%lx failed\n",
			   rman_get_start(sb->io_base)));
		return ENXIO;	/* Sorry */
    	}
    	return 0;
}

/************************************************************/

struct sb16_mixent {
	int reg;
	int bits;
	int ofs;
	int stereo;
};

static const struct sb16_mixent sb16_mixtab[32] = {
    	[SOUND_MIXER_VOLUME]	= { 0x30, 5, 3, 1 },
    	[SOUND_MIXER_PCM]	= { 0x32, 5, 3, 1 },
    	[SOUND_MIXER_SYNTH]	= { 0x34, 5, 3, 1 },
    	[SOUND_MIXER_CD]	= { 0x36, 5, 3, 1 },
    	[SOUND_MIXER_LINE]	= { 0x38, 5, 3, 1 },
    	[SOUND_MIXER_MIC]	= { 0x3a, 5, 3, 0 },
       	[SOUND_MIXER_SPEAKER]	= { 0x3b, 5, 3, 0 },
    	[SOUND_MIXER_IGAIN]	= { 0x3f, 2, 6, 1 },
    	[SOUND_MIXER_OGAIN]	= { 0x41, 2, 6, 1 },
	[SOUND_MIXER_TREBLE]	= { 0x44, 4, 4, 1 },
    	[SOUND_MIXER_BASS]	= { 0x46, 4, 4, 1 },
	[SOUND_MIXER_LINE1]	= { 0x52, 5, 3, 1 }
};

static int
sb16mix_init(struct snd_mixer *m)
{
    	struct sb_info *sb = mix_getdevinfo(m);

	mix_setdevs(m, SOUND_MASK_SYNTH | SOUND_MASK_PCM | SOUND_MASK_SPEAKER |
     		       SOUND_MASK_LINE | SOUND_MASK_MIC | SOUND_MASK_CD |
     		       SOUND_MASK_IGAIN | SOUND_MASK_OGAIN | SOUND_MASK_LINE1 |
     		       SOUND_MASK_VOLUME | SOUND_MASK_BASS | SOUND_MASK_TREBLE);

	mix_setrecdevs(m, SOUND_MASK_SYNTH | SOUND_MASK_LINE |
			  SOUND_MASK_LINE1 | SOUND_MASK_MIC | SOUND_MASK_CD);

	sb_setmixer(sb, 0x3c, 0x1f); /* make all output active */

	sb_setmixer(sb, 0x3d, 0); /* make all inputs-l off */
	sb_setmixer(sb, 0x3e, 0); /* make all inputs-r off */

	return 0;
}

static int
rel2abs_volume(int x, int max)
{
	int temp;
	
	temp = ((x * max) + 50) / 100;
	if (temp > max)
		temp = max;
	else if (temp < 0)
		temp = 0;
	return (temp);
}

static int
sb16mix_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
    	struct sb_info *sb = mix_getdevinfo(m);
    	const struct sb16_mixent *e;
    	int max;

	e = &sb16_mixtab[dev];
	max = (1 << e->bits) - 1;

	left = rel2abs_volume(left, max);
	right = rel2abs_volume(right, max);

	sb_setmixer(sb, e->reg, left << e->ofs);
	if (e->stereo)
		sb_setmixer(sb, e->reg + 1, right << e->ofs);
	else
		right = left;

	left = (left * 100) / max;
	right = (right * 100) / max;

    	return left | (right << 8);
}

static u_int32_t
sb16mix_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
    	struct sb_info *sb = mix_getdevinfo(m);
    	u_char recdev_l, recdev_r;

	recdev_l = 0;
	recdev_r = 0;
	if (src & SOUND_MASK_MIC) {
		recdev_l |= 0x01; /* mono mic */
		recdev_r |= 0x01;
	}

	if (src & SOUND_MASK_CD) {
		recdev_l |= 0x04; /* l cd */
		recdev_r |= 0x02; /* r cd */
	}

	if (src & SOUND_MASK_LINE) {
		recdev_l |= 0x10; /* l line */
		recdev_r |= 0x08; /* r line */
	}

	if (src & SOUND_MASK_SYNTH) {
		recdev_l |= 0x40; /* l midi */
		recdev_r |= 0x20; /* r midi */
	}

	sb_setmixer(sb, SB16_IMASK_L, recdev_l);
	sb_setmixer(sb, SB16_IMASK_R, recdev_r);

	/* Switch on/off FM tuner source */
	if (src & SOUND_MASK_LINE1)
		sb_setmixer(sb, 0x4a, 0x0c);
	else
		sb_setmixer(sb, 0x4a, 0x00);

	/*
	 * since the same volume controls apply to the input and
	 * output sections, the best approach to have a consistent
	 * behaviour among cards would be to disable the output path
	 * on devices which are used to record.
	 * However, since users like to have feedback, we only disable
	 * the mic -- permanently.
	 */
        sb_setmixer(sb, SB16_OMASK, 0x1f & ~1);

	return src;
}

static kobj_method_t sb16mix_mixer_methods[] = {
    	KOBJMETHOD(mixer_init,		sb16mix_init),
    	KOBJMETHOD(mixer_set,		sb16mix_set),
    	KOBJMETHOD(mixer_setrecsrc,	sb16mix_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(sb16mix_mixer);

/************************************************************/

static void
sb16_release_resources(struct sb_info *sb, device_t dev)
{
    	if (sb->irq) {
    		if (sb->ih)
			bus_teardown_intr(dev, sb->irq, sb->ih);
 		bus_release_resource(dev, SYS_RES_IRQ, 0, sb->irq);
		sb->irq = NULL;
    	}
    	if (sb->drq2) {
		if (sb->drq2 != sb->drq1) {
			isa_dma_release(rman_get_start(sb->drq2));
			bus_release_resource(dev, SYS_RES_DRQ, 1, sb->drq2);
		}
		sb->drq2 = NULL;
    	}
     	if (sb->drq1) {
		isa_dma_release(rman_get_start(sb->drq1));
		bus_release_resource(dev, SYS_RES_DRQ, 0, sb->drq1);
		sb->drq1 = NULL;
    	}
   	if (sb->io_base) {
		bus_release_resource(dev, SYS_RES_IOPORT, 0, sb->io_base);
		sb->io_base = NULL;
    	}
    	if (sb->parent_dmat) {
		bus_dma_tag_destroy(sb->parent_dmat);
		sb->parent_dmat = 0;
    	}
     	free(sb, M_DEVBUF);
}

static int
sb16_alloc_resources(struct sb_info *sb, device_t dev)
{
	int rid;

	rid = 0;
	if (!sb->io_base)
    		sb->io_base = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
			&rid, RF_ACTIVE);

	rid = 0;
	if (!sb->irq)
    		sb->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
			RF_ACTIVE);

	rid = 0;
	if (!sb->drq1)
    		sb->drq1 = bus_alloc_resource_any(dev, SYS_RES_DRQ, &rid,
			RF_ACTIVE);

	rid = 1;
	if (!sb->drq2)
        	sb->drq2 = bus_alloc_resource_any(dev, SYS_RES_DRQ, &rid,
			RF_ACTIVE);

    	if (sb->io_base && sb->drq1 && sb->irq) {
		isa_dma_acquire(rman_get_start(sb->drq1));
		isa_dmainit(rman_get_start(sb->drq1), sb->bufsize);

		if (sb->drq2) {
			isa_dma_acquire(rman_get_start(sb->drq2));
			isa_dmainit(rman_get_start(sb->drq2), sb->bufsize);
		} else {
			sb->drq2 = sb->drq1;
			pcm_setflags(dev, pcm_getflags(dev) | SD_F_SIMPLEX);
		}
		return 0;
	} else return ENXIO;
}

/* sbc does locking for us */
static void
sb_intr(void *arg)
{
    	struct sb_info *sb = (struct sb_info *)arg;
    	int reason, c;

    	/*
     	 * The Vibra16X has separate flags for 8 and 16 bit transfers, but
     	 * I have no idea how to tell capture from playback interrupts...
     	 */

	reason = 0;
	sb_lock(sb);
    	c = sb_getmixer(sb, IRQ_STAT);
    	if (c & 1)
		sb_rd(sb, DSP_DATA_AVAIL); /* 8-bit int ack */

    	if (c & 2)
		sb_rd(sb, DSP_DATA_AVL16); /* 16-bit int ack */
	sb_unlock(sb);

	/*
	 * this tells us if the source is 8-bit or 16-bit dma. We
     	 * have to check the io channel to map it to read or write...
     	 */

	if (sb->bd_flags & BD_F_SB16X) {
    		if (c & 1) { /* 8-bit format */
			if (sb->pch.fmt & AFMT_8BIT)
				reason |= 1;
			if (sb->rch.fmt & AFMT_8BIT)
				reason |= 2;
    		}
    		if (c & 2) { /* 16-bit format */
			if (sb->pch.fmt & AFMT_16BIT)
				reason |= 1;
			if (sb->rch.fmt & AFMT_16BIT)
				reason |= 2;
    		}
	} else {
    		if (c & 1) { /* 8-bit dma */
			if (sb->pch.dch == 1)
				reason |= 1;
			if (sb->rch.dch == 1)
				reason |= 2;
    		}
    		if (c & 2) { /* 16-bit dma */
			if (sb->pch.dch == 2)
				reason |= 1;
			if (sb->rch.dch == 2)
				reason |= 2;
    		}
	}
#if 0
    	printf("sb_intr: reason=%d c=0x%x\n", reason, c);
#endif
    	if ((reason & 1) && (sb->pch.run))
		chn_intr(sb->pch.channel);

    	if ((reason & 2) && (sb->rch.run))
		chn_intr(sb->rch.channel);
}

static int
sb_setup(struct sb_info *sb)
{
	struct sb_chinfo *ch;
	u_int8_t v;
	int l, pprio;

	sb_lock(sb);
	if (sb->bd_flags & BD_F_DMARUN)
		sndbuf_dma(sb->pch.buffer, PCMTRIG_STOP);
	if (sb->bd_flags & BD_F_DMARUN2)
		sndbuf_dma(sb->rch.buffer, PCMTRIG_STOP);
	sb->bd_flags &= ~(BD_F_DMARUN | BD_F_DMARUN2);

	sb_reset_dsp(sb);

	if (sb->bd_flags & BD_F_SB16X) {
		/* full-duplex doesn't work! */
		pprio = sb->pch.run? 1 : 0;
		sndbuf_dmasetup(sb->pch.buffer, pprio? sb->drq1 : sb->drq2);
		sb->pch.dch = pprio? 1 : 0;
		sndbuf_dmasetup(sb->rch.buffer, pprio? sb->drq2 : sb->drq1);
		sb->rch.dch = pprio? 2 : 1;
	} else {
		if (sb->pch.run && sb->rch.run) {
			pprio = (sb->rch.fmt & AFMT_16BIT)? 0 : 1;
			sndbuf_dmasetup(sb->pch.buffer, pprio? sb->drq2 : sb->drq1);
			sb->pch.dch = pprio? 2 : 1;
			sndbuf_dmasetup(sb->rch.buffer, pprio? sb->drq1 : sb->drq2);
			sb->rch.dch = pprio? 1 : 2;
		} else {
			if (sb->pch.run) {
				sndbuf_dmasetup(sb->pch.buffer, (sb->pch.fmt & AFMT_16BIT)? sb->drq2 : sb->drq1);
				sb->pch.dch = (sb->pch.fmt & AFMT_16BIT)? 2 : 1;
				sndbuf_dmasetup(sb->rch.buffer, (sb->pch.fmt & AFMT_16BIT)? sb->drq1 : sb->drq2);
				sb->rch.dch = (sb->pch.fmt & AFMT_16BIT)? 1 : 2;
			} else if (sb->rch.run) {
				sndbuf_dmasetup(sb->pch.buffer, (sb->rch.fmt & AFMT_16BIT)? sb->drq1 : sb->drq2);
				sb->pch.dch = (sb->rch.fmt & AFMT_16BIT)? 1 : 2;
				sndbuf_dmasetup(sb->rch.buffer, (sb->rch.fmt & AFMT_16BIT)? sb->drq2 : sb->drq1);
				sb->rch.dch = (sb->rch.fmt & AFMT_16BIT)? 2 : 1;
			}
		}
	}

	sndbuf_dmasetdir(sb->pch.buffer, PCMDIR_PLAY);
	sndbuf_dmasetdir(sb->rch.buffer, PCMDIR_REC);

	/*
	printf("setup: [pch = %d, pfmt = %d, pgo = %d] [rch = %d, rfmt = %d, rgo = %d]\n",
	       sb->pch.dch, sb->pch.fmt, sb->pch.run, sb->rch.dch, sb->rch.fmt, sb->rch.run);
	*/

	ch = &sb->pch;
	if (ch->run) {
		l = ch->blksz;
		if (ch->fmt & AFMT_16BIT)
			l >>= 1;
		l--;

		/* play speed */
		RANGE(ch->spd, 5000, 45000);
		sb_cmd(sb, DSP_CMD_OUT16);
    		sb_cmd(sb, ch->spd >> 8);
		sb_cmd(sb, ch->spd & 0xff);

		/* play format, length */
		v = DSP_F16_AUTO | DSP_F16_FIFO_ON | DSP_F16_DAC;
		v |= (ch->fmt & AFMT_16BIT)? DSP_DMA16 : DSP_DMA8;
		sb_cmd(sb, v);

		v = (AFMT_CHANNEL(ch->fmt) > 1)? DSP_F16_STEREO : 0;
		v |= (ch->fmt & AFMT_SIGNED)? DSP_F16_SIGNED : 0;
		sb_cmd2(sb, v, l);
		sndbuf_dma(ch->buffer, PCMTRIG_START);
		sb->bd_flags |= BD_F_DMARUN;
	}

	ch = &sb->rch;
	if (ch->run) {
		l = ch->blksz;
		if (ch->fmt & AFMT_16BIT)
			l >>= 1;
		l--;

		/* record speed */
		RANGE(ch->spd, 5000, 45000);
		sb_cmd(sb, DSP_CMD_IN16);
    		sb_cmd(sb, ch->spd >> 8);
		sb_cmd(sb, ch->spd & 0xff);

		/* record format, length */
		v = DSP_F16_AUTO | DSP_F16_FIFO_ON | DSP_F16_ADC;
		v |= (ch->fmt & AFMT_16BIT)? DSP_DMA16 : DSP_DMA8;
		sb_cmd(sb, v);

		v = (AFMT_CHANNEL(ch->fmt) > 1)? DSP_F16_STEREO : 0;
		v |= (ch->fmt & AFMT_SIGNED)? DSP_F16_SIGNED : 0;
		sb_cmd2(sb, v, l);
		sndbuf_dma(ch->buffer, PCMTRIG_START);
		sb->bd_flags |= BD_F_DMARUN2;
	}
	sb_unlock(sb);

    	return 0;
}

/* channel interface */
static void *
sb16chan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct sb_info *sb = devinfo;
	struct sb_chinfo *ch = (dir == PCMDIR_PLAY)? &sb->pch : &sb->rch;

	ch->parent = sb;
	ch->channel = c;
	ch->buffer = b;
	ch->dir = dir;

	if (sndbuf_alloc(ch->buffer, sb->parent_dmat, 0, sb->bufsize) != 0)
		return NULL;

	return ch;
}

static int
sb16chan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct sb_chinfo *ch = data;
	struct sb_info *sb = ch->parent;

	ch->fmt = format;
	sb->prio = ch->dir;
	sb->prio16 = (ch->fmt & AFMT_16BIT)? 1 : 0;

	return 0;
}

static u_int32_t
sb16chan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct sb_chinfo *ch = data;

	ch->spd = speed;
	return speed;
}

static u_int32_t
sb16chan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct sb_chinfo *ch = data;

	ch->blksz = blocksize;
	return ch->blksz;
}

static int
sb16chan_trigger(kobj_t obj, void *data, int go)
{
	struct sb_chinfo *ch = data;
	struct sb_info *sb = ch->parent;

	if (!PCMTRIG_COMMON(go))
		return 0;

	if (go == PCMTRIG_START)
		ch->run = 1;
	else
		ch->run = 0;

	sb_setup(sb);

	return 0;
}

static u_int32_t
sb16chan_getptr(kobj_t obj, void *data)
{
	struct sb_chinfo *ch = data;

	return sndbuf_dmaptr(ch->buffer);
}

static struct pcmchan_caps *
sb16chan_getcaps(kobj_t obj, void *data)
{
	struct sb_chinfo *ch = data;
	struct sb_info *sb = ch->parent;

	if ((sb->prio == 0) || (sb->prio == ch->dir))
		return &sb16x_caps;
	else
		return sb->prio16? &sb16_caps8 : &sb16_caps16;
}

static int
sb16chan_resetdone(kobj_t obj, void *data)
{
	struct sb_chinfo *ch = data;
	struct sb_info *sb = ch->parent;

	sb->prio = 0;

	return 0;
}

static kobj_method_t sb16chan_methods[] = {
    	KOBJMETHOD(channel_init,		sb16chan_init),
    	KOBJMETHOD(channel_resetdone,		sb16chan_resetdone),
    	KOBJMETHOD(channel_setformat,		sb16chan_setformat),
    	KOBJMETHOD(channel_setspeed,		sb16chan_setspeed),
    	KOBJMETHOD(channel_setblocksize,	sb16chan_setblocksize),
    	KOBJMETHOD(channel_trigger,		sb16chan_trigger),
    	KOBJMETHOD(channel_getptr,		sb16chan_getptr),
    	KOBJMETHOD(channel_getcaps,		sb16chan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(sb16chan);

/************************************************************/

static int
sb16_probe(device_t dev)
{
    	char buf[64];
	uintptr_t func, ver, r, f;

	/* The parent device has already been probed. */
	r = BUS_READ_IVAR(device_get_parent(dev), dev, 0, &func);
	if (func != SCF_PCM)
		return (ENXIO);

	r = BUS_READ_IVAR(device_get_parent(dev), dev, 1, &ver);
	f = (ver & 0xffff0000) >> 16;
	ver &= 0x0000ffff;
	if (f & BD_F_SB16) {
		snprintf(buf, sizeof buf, "SB16 DSP %d.%02d%s", (int) ver >> 8, (int) ver & 0xff,
			 (f & BD_F_SB16X)? " (ViBRA16X)" : "");
    		device_set_desc_copy(dev, buf);
		return 0;
	} else
		return (ENXIO);
}

static int
sb16_attach(device_t dev)
{
    	struct sb_info *sb;
	uintptr_t ver;
    	char status[SND_STATUSLEN], status2[SND_STATUSLEN];

    	sb = malloc(sizeof(*sb), M_DEVBUF, M_WAITOK | M_ZERO);
	sb->parent_dev = device_get_parent(dev);
	BUS_READ_IVAR(sb->parent_dev, dev, 1, &ver);
	sb->bd_id = ver & 0x0000ffff;
	sb->bd_flags = (ver & 0xffff0000) >> 16;
	sb->bufsize = pcm_getbuffersize(dev, 4096, SB16_BUFFSIZE, 65536);

	if (sb16_alloc_resources(sb, dev))
		goto no;
	sb_lock(sb);
	if (sb_reset_dsp(sb)) {
		sb_unlock(sb);
		goto no;
	}
	sb_unlock(sb);
	if (mixer_init(dev, &sb16mix_mixer_class, sb))
		goto no;
	if (snd_setup_intr(dev, sb->irq, 0, sb_intr, sb, &sb->ih))
		goto no;

	if (sb->bd_flags & BD_F_SB16X)
		pcm_setflags(dev, pcm_getflags(dev) | SD_F_SIMPLEX);

	sb->prio = 0;

    	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev), /*alignment*/2,
			/*boundary*/0,
			/*lowaddr*/BUS_SPACE_MAXADDR_24BIT,
			/*highaddr*/BUS_SPACE_MAXADDR,
			/*filter*/NULL, /*filterarg*/NULL,
			/*maxsize*/sb->bufsize, /*nsegments*/1,
			/*maxsegz*/0x3ffff, /*flags*/0,
			/*lockfunc*/busdma_lock_mutex, /*lockarg*/&Giant,
			&sb->parent_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		goto no;
    	}

    	if (!(pcm_getflags(dev) & SD_F_SIMPLEX))
		snprintf(status2, SND_STATUSLEN, ":%jd", rman_get_start(sb->drq2));
	else
		status2[0] = '\0';

    	snprintf(status, SND_STATUSLEN, "at io 0x%jx irq %jd drq %jd%s bufsz %u %s",
    	     	rman_get_start(sb->io_base), rman_get_start(sb->irq),
		rman_get_start(sb->drq1), status2, sb->bufsize,
		PCM_KLDSTRING(snd_sb16));

    	if (pcm_register(dev, sb, 1, 1))
		goto no;
	pcm_addchan(dev, PCMDIR_REC, &sb16chan_class, sb);
	pcm_addchan(dev, PCMDIR_PLAY, &sb16chan_class, sb);

    	pcm_setstatus(dev, status);

    	return 0;

no:
    	sb16_release_resources(sb, dev);
    	return ENXIO;
}

static int
sb16_detach(device_t dev)
{
	int r;
	struct sb_info *sb;

	r = pcm_unregister(dev);
	if (r)
		return r;

	sb = pcm_getdevinfo(dev);
    	sb16_release_resources(sb, dev);
	return 0;
}

static device_method_t sb16_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sb16_probe),
	DEVMETHOD(device_attach,	sb16_attach),
	DEVMETHOD(device_detach,	sb16_detach),

	{ 0, 0 }
};

static driver_t sb16_driver = {
	"pcm",
	sb16_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_sb16, sbc, sb16_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_sb16, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_DEPEND(snd_sb16, snd_sbc, 1, 1, 1);
MODULE_VERSION(snd_sb16, 1);
