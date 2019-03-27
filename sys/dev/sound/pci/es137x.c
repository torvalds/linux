/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-4-Clause
 *
 * Copyright (c) 1999 Russell Cattelan <cattelan@thebarn.com>
 * Copyright (c) 1998 Joachim Kuebart <joachim.kuebart@gmx.net>
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

/*-
 * Copyright (c) 1999 Cameron Grant <cg@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgement:
 *	This product includes software developed by Joachim Kuebart.
 *
 * 4. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Support the ENSONIQ AudioPCI board and Creative Labs SoundBlaster PCI
 * boards based on the ES1370, ES1371 and ES1373 chips.
 *
 * Part of this code was heavily inspired by the linux driver from
 * Thomas Sailer (sailer@ife.ee.ethz.ch)
 * Just about everything has been touched and reworked in some way but
 * the all the underlying sequences/timing/register values are from
 * Thomas' code.
*/

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/ac97.h>
#include <dev/sound/pci/es137x.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <sys/sysctl.h>

#include "mixer_if.h"

SND_DECLARE_FILE("$FreeBSD$");

#define MEM_MAP_REG 0x14

/* PCI IDs of supported chips */
#define ES1370_PCI_ID 0x50001274
#define ES1371_PCI_ID 0x13711274
#define ES1371_PCI_ID2 0x13713274
#define CT5880_PCI_ID 0x58801274
#define CT4730_PCI_ID 0x89381102

#define ES1371REV_ES1371_A  0x02
#define ES1371REV_ES1371_B  0x09

#define ES1371REV_ES1373_8  0x08
#define ES1371REV_ES1373_A  0x04
#define ES1371REV_ES1373_B  0x06

#define ES1371REV_CT5880_A  0x07

#define CT5880REV_CT5880_C  0x02
#define CT5880REV_CT5880_D  0x03
#define CT5880REV_CT5880_E  0x04

#define CT4730REV_CT4730_A  0x00

#define ES_DEFAULT_BUFSZ 4096

/* 2 DAC for playback, 1 ADC for record */
#define ES_DAC1		0
#define ES_DAC2		1
#define ES_ADC		2
#define ES_NCHANS	3

#define ES_DMA_SEGS_MIN	2
#define ES_DMA_SEGS_MAX	256
#define ES_BLK_MIN	64
#define ES_BLK_ALIGN	(~(ES_BLK_MIN - 1))

#define ES1370_DAC1_MINSPEED	5512
#define ES1370_DAC1_MAXSPEED	44100

/* device private data */
struct es_info;

struct es_chinfo {
	struct es_info *parent;
	struct pcm_channel *channel;
	struct snd_dbuf *buffer;
	struct pcmchan_caps caps;
	int dir, num, index;
	uint32_t fmt, blksz, blkcnt, bufsz;
	uint32_t ptr, prevptr;
	int active;
};

/*
 *     32bit Ensoniq Configuration (es->escfg).
 *     ----------------------------------------
 *
 *     +-------+--------+------+------+---------+--------+---------+---------+
 * len |  16   |    1   |  1   |  1   |    2    |   2    |    1    |    8    |
 *     +-------+--------+------+------+---------+--------+---------+---------+
 *     | fixed | single |      |      |         |        |   is    | general |
 *     | rate  |   pcm  | DACx | DACy | numplay | numrec | es1370? | purpose |
 *     |       |  mixer |      |      |         |        |         |         |
 *     +-------+--------+------+------+---------+--------+---------+---------+
 */
#define ES_FIXED_RATE(cfgv)	\
		(((cfgv) & 0xffff0000) >> 16)
#define ES_SET_FIXED_RATE(cfgv, nv)	\
		(((cfgv) & ~0xffff0000) | (((nv) & 0xffff) << 16))
#define ES_SINGLE_PCM_MIX(cfgv)	\
		(((cfgv) & 0x8000) >> 15)
#define ES_SET_SINGLE_PCM_MIX(cfgv, nv)	\
		(((cfgv) & ~0x8000) | (((nv) ? 1 : 0) << 15))
#define ES_DAC_FIRST(cfgv)	\
		(((cfgv) & 0x4000) >> 14)
#define ES_SET_DAC_FIRST(cfgv, nv)	\
		(((cfgv) & ~0x4000) | (((nv) & 0x1) << 14))
#define ES_DAC_SECOND(cfgv)	\
		(((cfgv) & 0x2000) >> 13)
#define ES_SET_DAC_SECOND(cfgv, nv)	\
		(((cfgv) & ~0x2000) | (((nv) & 0x1) << 13))
#define ES_NUMPLAY(cfgv)	\
		(((cfgv) & 0x1800) >> 11)
#define ES_SET_NUMPLAY(cfgv, nv)	\
		(((cfgv) & ~0x1800) | (((nv) & 0x3) << 11))
#define ES_NUMREC(cfgv)	\
		(((cfgv) & 0x600) >> 9)
#define ES_SET_NUMREC(cfgv, nv)	\
		(((cfgv) & ~0x600) | (((nv) & 0x3) << 9))
#define ES_IS_ES1370(cfgv)	\
		(((cfgv) & 0x100) >> 8)
#define ES_SET_IS_ES1370(cfgv, nv)	\
		(((cfgv) & ~0x100) | (((nv) ? 1 : 0) << 8))
#define ES_GP(cfgv)	\
		((cfgv) & 0xff)
#define ES_SET_GP(cfgv, nv)	\
		(((cfgv) & ~0xff) | ((nv) & 0xff))

#define ES_DAC1_ENABLED(cfgv)	\
		(ES_NUMPLAY(cfgv) > 1 || \
		(ES_NUMPLAY(cfgv) == 1 && ES_DAC_FIRST(cfgv) == ES_DAC1))
#define ES_DAC2_ENABLED(cfgv)	\
		(ES_NUMPLAY(cfgv) > 1 || \
		(ES_NUMPLAY(cfgv) == 1 && ES_DAC_FIRST(cfgv) == ES_DAC2))

/*
 * DAC 1/2 configuration through kernel hint - hint.pcm.<unit>.dac="val"
 *
 * 0 = Enable both DACs - Default
 * 1 = Enable single DAC (DAC1)
 * 2 = Enable single DAC (DAC2)
 * 3 = Enable both DACs, swap position (DAC2 comes first instead of DAC1)
 */
#define ES_DEFAULT_DAC_CFG	0

struct es_info {
	bus_space_tag_t st;
	bus_space_handle_t sh;
	bus_dma_tag_t	parent_dmat;

	struct resource *reg, *irq;
	int regtype, regid, irqid;
	void *ih;

	device_t dev;
	int num;
	unsigned int bufsz, blkcnt;

	/* Contents of board's registers */
	uint32_t	ctrl;
	uint32_t	sctrl;
	uint32_t	escfg;
	struct es_chinfo ch[ES_NCHANS];
	struct mtx	*lock;
	struct callout	poll_timer;
	int poll_ticks, polling;
};

#define ES_LOCK(sc)		snd_mtxlock((sc)->lock)
#define ES_UNLOCK(sc)		snd_mtxunlock((sc)->lock)
#define ES_LOCK_ASSERT(sc)	snd_mtxassert((sc)->lock)

/* prototypes */
static void     es_intr(void *);
static uint32_t	es1371_wait_src_ready(struct es_info *);
static void	es1371_src_write(struct es_info *,
					unsigned short, unsigned short);
static unsigned int	es1371_adc_rate(struct es_info *, unsigned int, int);
static unsigned int	es1371_dac_rate(struct es_info *, unsigned int, int);
static int	es1371_init(struct es_info *);
static int      es1370_init(struct es_info *);
static int      es1370_wrcodec(struct es_info *, unsigned char, unsigned char);

static uint32_t es_fmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};
static struct pcmchan_caps es_caps = {4000, 48000, es_fmt, 0};

static const struct {
	unsigned        volidx:4;
	unsigned        left:4;
	unsigned        right:4;
	unsigned        stereo:1;
	unsigned        recmask:13;
	unsigned        avail:1;
}       mixtable[SOUND_MIXER_NRDEVICES] = {
	[SOUND_MIXER_VOLUME]	= { 0, 0x0, 0x1, 1, 0x1f7f, 1 },
	[SOUND_MIXER_PCM] 	= { 1, 0x2, 0x3, 1, 0x0400, 1 },
	[SOUND_MIXER_SYNTH]	= { 2, 0x4, 0x5, 1, 0x0060, 1 },
	[SOUND_MIXER_CD]	= { 3, 0x6, 0x7, 1, 0x0006, 1 },
	[SOUND_MIXER_LINE]	= { 4, 0x8, 0x9, 1, 0x0018, 1 },
	[SOUND_MIXER_LINE1]	= { 5, 0xa, 0xb, 1, 0x1800, 1 },
	[SOUND_MIXER_LINE2]	= { 6, 0xc, 0x0, 0, 0x0100, 1 },
	[SOUND_MIXER_LINE3]	= { 7, 0xd, 0x0, 0, 0x0200, 1 },
	[SOUND_MIXER_MIC]	= { 8, 0xe, 0x0, 0, 0x0001, 1 },
	[SOUND_MIXER_OGAIN]	= { 9, 0xf, 0x0, 0, 0x0000, 1 }
};

static __inline uint32_t
es_rd(struct es_info *es, int regno, int size)
{
	switch (size) {
	case 1:
		return (bus_space_read_1(es->st, es->sh, regno));
	case 2:
		return (bus_space_read_2(es->st, es->sh, regno));
	case 4:
		return (bus_space_read_4(es->st, es->sh, regno));
	default:
		return (0xFFFFFFFF);
	}
}

static __inline void
es_wr(struct es_info *es, int regno, uint32_t data, int size)
{

	switch (size) {
	case 1:
		bus_space_write_1(es->st, es->sh, regno, data);
		break;
	case 2:
		bus_space_write_2(es->st, es->sh, regno, data);
		break;
	case 4:
		bus_space_write_4(es->st, es->sh, regno, data);
		break;
	}
}

/* -------------------------------------------------------------------- */
/* The es1370 mixer interface */

static int
es1370_mixinit(struct snd_mixer *m)
{
	struct es_info *es;
	int i;
	uint32_t v;

	es = mix_getdevinfo(m);
	v = 0;
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (mixtable[i].avail)
			v |= (1 << i);
	}
	/*
	 * Each DAC1/2 for ES1370 can be controlled independently
	 *   DAC1 = controlled by synth
	 *   DAC2 = controlled by pcm
	 * This is indeed can confuse user if DAC1 become primary playback
	 * channel. Try to be smart and combine both if necessary.
	 */
	if (ES_SINGLE_PCM_MIX(es->escfg))
		v &= ~(1 << SOUND_MIXER_SYNTH);
	mix_setdevs(m, v);
	v = 0;
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (mixtable[i].recmask)
			v |= (1 << i);
	}
	if (ES_SINGLE_PCM_MIX(es->escfg)) /* ditto */
		v &= ~(1 << SOUND_MIXER_SYNTH);
	mix_setrecdevs(m, v);
	return (0);
}

static int
es1370_mixset(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
	struct es_info *es;
	int l, r, rl, rr, set_dac1;

	if (!mixtable[dev].avail)
		return (-1);
	l = left;
	r = (mixtable[dev].stereo) ? right : l;
	if (mixtable[dev].left == 0xf)
		rl = (l < 2) ? 0x80 : 7 - (l - 2) / 14;
	else
		rl = (l < 7) ? 0x80 : 31 - (l - 7) / 3;
	es = mix_getdevinfo(m);
	ES_LOCK(es);
	if (dev == SOUND_MIXER_PCM && (ES_SINGLE_PCM_MIX(es->escfg)) &&
	    ES_DAC1_ENABLED(es->escfg))
		set_dac1 = 1;
	else
		set_dac1 = 0;
	if (mixtable[dev].stereo) {
		rr = (r < 7) ? 0x80 : 31 - (r - 7) / 3;
		es1370_wrcodec(es, mixtable[dev].right, rr);
		if (set_dac1 && mixtable[SOUND_MIXER_SYNTH].stereo)
			es1370_wrcodec(es,
			    mixtable[SOUND_MIXER_SYNTH].right, rr);
	}
	es1370_wrcodec(es, mixtable[dev].left, rl);
	if (set_dac1)
		es1370_wrcodec(es, mixtable[SOUND_MIXER_SYNTH].left, rl);
	ES_UNLOCK(es);

	return (l | (r << 8));
}

static uint32_t
es1370_mixsetrecsrc(struct snd_mixer *m, uint32_t src)
{
	struct es_info *es;
	int i, j = 0;

	es = mix_getdevinfo(m);
	if (src == 0) src = 1 << SOUND_MIXER_MIC;
	src &= mix_getrecdevs(m);
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		if ((src & (1 << i)) != 0) j |= mixtable[i].recmask;

	ES_LOCK(es);
	if ((src & (1 << SOUND_MIXER_PCM)) && ES_SINGLE_PCM_MIX(es->escfg) &&
	    ES_DAC1_ENABLED(es->escfg))
		j |= mixtable[SOUND_MIXER_SYNTH].recmask;
	es1370_wrcodec(es, CODEC_LIMIX1, j & 0x55);
	es1370_wrcodec(es, CODEC_RIMIX1, j & 0xaa);
	es1370_wrcodec(es, CODEC_LIMIX2, (j >> 8) & 0x17);
	es1370_wrcodec(es, CODEC_RIMIX2, (j >> 8) & 0x0f);
	es1370_wrcodec(es, CODEC_OMIX1, 0x7f);
	es1370_wrcodec(es, CODEC_OMIX2, 0x3f);
	ES_UNLOCK(es);

	return (src);
}

static kobj_method_t es1370_mixer_methods[] = {
	KOBJMETHOD(mixer_init,		es1370_mixinit),
	KOBJMETHOD(mixer_set,		es1370_mixset),
	KOBJMETHOD(mixer_setrecsrc,	es1370_mixsetrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(es1370_mixer);

/* -------------------------------------------------------------------- */

static int
es1370_wrcodec(struct es_info *es, unsigned char i, unsigned char data)
{
	unsigned int t;

	ES_LOCK_ASSERT(es);

	for (t = 0; t < 0x1000; t++) {
		if ((es_rd(es, ES1370_REG_STATUS, 4) &
		    STAT_CSTAT) == 0) {
			es_wr(es, ES1370_REG_CODEC,
			    ((unsigned short)i << CODEC_INDEX_SHIFT) | data, 2);
			return (0);
		}
		DELAY(1);
	}
	device_printf(es->dev, "%s: timed out\n", __func__);
	return (-1);
}

/* -------------------------------------------------------------------- */

/* channel interface */
static void *
eschan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
				struct pcm_channel *c, int dir)
{
	struct es_info *es = devinfo;
	struct es_chinfo *ch;
	uint32_t index;

	ES_LOCK(es);

	if (dir == PCMDIR_PLAY) {
		index = ES_GP(es->escfg);
		es->escfg = ES_SET_GP(es->escfg, index + 1);
		if (index == 0)
			index = ES_DAC_FIRST(es->escfg);
		else if (index == 1)
			index = ES_DAC_SECOND(es->escfg);
		else {
			device_printf(es->dev,
			    "Invalid ES_GP index: %d\n", index);
			ES_UNLOCK(es);
			return (NULL);
		}
		if (!(index == ES_DAC1 || index == ES_DAC2)) {
			device_printf(es->dev, "Unknown DAC: %d\n", index + 1);
			ES_UNLOCK(es);
			return (NULL);
		}
		if (es->ch[index].channel != NULL) {
			device_printf(es->dev, "DAC%d already initialized!\n",
			    index + 1);
			ES_UNLOCK(es);
			return (NULL);
		}
	} else
		index = ES_ADC;

	ch = &es->ch[index];
	ch->index = index;
	ch->num = es->num++;
	ch->caps = es_caps;
	if (ES_IS_ES1370(es->escfg)) {
		if (ch->index == ES_DAC1) {
			ch->caps.maxspeed = ES1370_DAC1_MAXSPEED;
			ch->caps.minspeed = ES1370_DAC1_MINSPEED;
		} else {
			uint32_t fixed_rate = ES_FIXED_RATE(es->escfg);
			if (!(fixed_rate < es_caps.minspeed ||
			    fixed_rate > es_caps.maxspeed)) {
				ch->caps.maxspeed = fixed_rate;
				ch->caps.minspeed = fixed_rate;
			}
		}
	}
	ch->parent = es;
	ch->channel = c;
	ch->buffer = b;
	ch->bufsz = es->bufsz;
	ch->blkcnt = es->blkcnt;
	ch->blksz = ch->bufsz / ch->blkcnt;
	ch->dir = dir;
	ES_UNLOCK(es);
	if (sndbuf_alloc(ch->buffer, es->parent_dmat, 0, ch->bufsz) != 0)
		return (NULL);
	ES_LOCK(es);
	if (dir == PCMDIR_PLAY) {
		if (ch->index == ES_DAC1) {
			es_wr(es, ES1370_REG_MEMPAGE,
			    ES1370_REG_DAC1_FRAMEADR >> 8, 1);
			es_wr(es, ES1370_REG_DAC1_FRAMEADR & 0xff,
			    sndbuf_getbufaddr(ch->buffer), 4);
			es_wr(es, ES1370_REG_DAC1_FRAMECNT & 0xff,
			    (ch->bufsz >> 2) - 1, 4);
		} else {
			es_wr(es, ES1370_REG_MEMPAGE,
			    ES1370_REG_DAC2_FRAMEADR >> 8, 1);
			es_wr(es, ES1370_REG_DAC2_FRAMEADR & 0xff,
			    sndbuf_getbufaddr(ch->buffer), 4);
			es_wr(es, ES1370_REG_DAC2_FRAMECNT & 0xff,
			    (ch->bufsz >> 2) - 1, 4);
		}
	} else {
		es_wr(es, ES1370_REG_MEMPAGE, ES1370_REG_ADC_FRAMEADR >> 8, 1);
		es_wr(es, ES1370_REG_ADC_FRAMEADR & 0xff,
		    sndbuf_getbufaddr(ch->buffer), 4);
		es_wr(es, ES1370_REG_ADC_FRAMECNT & 0xff,
		    (ch->bufsz >> 2) - 1, 4);
	}
	ES_UNLOCK(es);
	return (ch);
}

static int
eschan_setformat(kobj_t obj, void *data, uint32_t format)
{
	struct es_chinfo *ch = data;
	struct es_info *es = ch->parent;

	ES_LOCK(es);
	if (ch->dir == PCMDIR_PLAY) {
		if (ch->index == ES_DAC1) {
			es->sctrl &= ~SCTRL_P1FMT;
			if (format & AFMT_S16_LE)
				es->sctrl |= SCTRL_P1SEB;
			if (AFMT_CHANNEL(format) > 1)
				es->sctrl |= SCTRL_P1SMB;
		} else {
			es->sctrl &= ~SCTRL_P2FMT;
			if (format & AFMT_S16_LE)
				es->sctrl |= SCTRL_P2SEB;
			if (AFMT_CHANNEL(format) > 1)
				es->sctrl |= SCTRL_P2SMB;
		}
	} else {
		es->sctrl &= ~SCTRL_R1FMT;
		if (format & AFMT_S16_LE)
			es->sctrl |= SCTRL_R1SEB;
		if (AFMT_CHANNEL(format) > 1)
			es->sctrl |= SCTRL_R1SMB;
	}
	es_wr(es, ES1370_REG_SERIAL_CONTROL, es->sctrl, 4);
	ES_UNLOCK(es);
	ch->fmt = format;
	return (0);
}

static uint32_t
eschan1370_setspeed(kobj_t obj, void *data, uint32_t speed)
{
	struct es_chinfo *ch = data;
	struct es_info *es = ch->parent;

	ES_LOCK(es);
	/* Fixed rate , do nothing. */
	if (ch->caps.minspeed == ch->caps.maxspeed) {
		ES_UNLOCK(es);
		return (ch->caps.maxspeed);
	}
	if (speed < ch->caps.minspeed)
		speed = ch->caps.minspeed;
	if (speed > ch->caps.maxspeed)
		speed = ch->caps.maxspeed;
	if (ch->index == ES_DAC1) {
		/*
		 * DAC1 does not support continuous rate settings.
		 * Pick the nearest and use it since FEEDER_RATE will
		 * do the proper conversion for us.
		 */
		es->ctrl &= ~CTRL_WTSRSEL;
		if (speed < 8268) {
			speed = 5512;
			es->ctrl |= 0 << CTRL_SH_WTSRSEL;
		} else if (speed < 16537) {
			speed = 11025;
			es->ctrl |= 1 << CTRL_SH_WTSRSEL;
		} else if (speed < 33075) {
			speed = 22050;
			es->ctrl |= 2 << CTRL_SH_WTSRSEL;
		} else {
			speed = 44100;
			es->ctrl |= 3 << CTRL_SH_WTSRSEL;
		}
	} else {
		es->ctrl &= ~CTRL_PCLKDIV;
		es->ctrl |= DAC2_SRTODIV(speed) << CTRL_SH_PCLKDIV;
	}
	es_wr(es, ES1370_REG_CONTROL, es->ctrl, 4);
	ES_UNLOCK(es);
	return (speed);
}

static uint32_t
eschan1371_setspeed(kobj_t obj, void *data, uint32_t speed)
{
  	struct es_chinfo *ch = data;
  	struct es_info *es = ch->parent;
	uint32_t i;
	int delta;

	ES_LOCK(es);
	if (ch->dir == PCMDIR_PLAY)
  		i = es1371_dac_rate(es, speed, ch->index); /* play */
	else
  		i = es1371_adc_rate(es, speed, ch->index); /* record */
	ES_UNLOCK(es);
	delta = (speed > i) ? (speed - i) : (i - speed);
	if (delta < 2)
		return (speed);
	return (i);
}

static int
eschan_setfragments(kobj_t obj, void *data, uint32_t blksz, uint32_t blkcnt)
{
  	struct es_chinfo *ch = data;
  	struct es_info *es = ch->parent;

	blksz &= ES_BLK_ALIGN;

	if (blksz > (sndbuf_getmaxsize(ch->buffer) / ES_DMA_SEGS_MIN))
		blksz = sndbuf_getmaxsize(ch->buffer) / ES_DMA_SEGS_MIN;
	if (blksz < ES_BLK_MIN)
		blksz = ES_BLK_MIN;
	if (blkcnt > ES_DMA_SEGS_MAX)
		blkcnt = ES_DMA_SEGS_MAX;
	if (blkcnt < ES_DMA_SEGS_MIN)
		blkcnt = ES_DMA_SEGS_MIN;

	while ((blksz * blkcnt) > sndbuf_getmaxsize(ch->buffer)) {
		if ((blkcnt >> 1) >= ES_DMA_SEGS_MIN)
			blkcnt >>= 1;
		else if ((blksz >> 1) >= ES_BLK_MIN)
			blksz >>= 1;
		else
			break;
	}

	if ((sndbuf_getblksz(ch->buffer) != blksz ||
	    sndbuf_getblkcnt(ch->buffer) != blkcnt) &&
	    sndbuf_resize(ch->buffer, blkcnt, blksz) != 0)
		device_printf(es->dev, "%s: failed blksz=%u blkcnt=%u\n",
		    __func__, blksz, blkcnt);

	ch->bufsz = sndbuf_getsize(ch->buffer);
	ch->blksz = sndbuf_getblksz(ch->buffer);
	ch->blkcnt = sndbuf_getblkcnt(ch->buffer);

	return (0);
}

static uint32_t
eschan_setblocksize(kobj_t obj, void *data, uint32_t blksz)
{
  	struct es_chinfo *ch = data;
  	struct es_info *es = ch->parent;

	eschan_setfragments(obj, data, blksz, es->blkcnt);

	return (ch->blksz);
}

#define es_chan_active(es)	((es)->ch[ES_DAC1].active + \
				(es)->ch[ES_DAC2].active + \
				(es)->ch[ES_ADC].active)

static __inline int
es_poll_channel(struct es_chinfo *ch)
{
	struct es_info *es;
	uint32_t sz, delta;
	uint32_t reg, ptr;

	if (ch == NULL || ch->channel == NULL || ch->active == 0)
		return (0);

	es = ch->parent;
	if (ch->dir == PCMDIR_PLAY) {
		if (ch->index == ES_DAC1)
			reg = ES1370_REG_DAC1_FRAMECNT;
		else
			reg = ES1370_REG_DAC2_FRAMECNT;
	} else
		reg = ES1370_REG_ADC_FRAMECNT;
	sz = ch->blksz * ch->blkcnt;
	es_wr(es, ES1370_REG_MEMPAGE, reg >> 8, 4);
	ptr = es_rd(es, reg & 0x000000ff, 4) >> 16;
	ptr <<= 2;
	ch->ptr = ptr;
	ptr %= sz;
	ptr &= ~(ch->blksz - 1);
	delta = (sz + ptr - ch->prevptr) % sz;

	if (delta < ch->blksz)
		return (0);

	ch->prevptr = ptr;

	return (1);
}

static void
es_poll_callback(void *arg)
{
	struct es_info *es = arg;
	uint32_t trigger = 0;
	int i;

	if (es == NULL)
		return;

	ES_LOCK(es);
	if (es->polling == 0 || es_chan_active(es) == 0) {
		ES_UNLOCK(es);
		return;
	}

	for (i = 0; i < ES_NCHANS; i++) {
		if (es_poll_channel(&es->ch[i]) != 0)
			trigger |= 1 << i;
	}

	/* XXX */
	callout_reset(&es->poll_timer, 1/*es->poll_ticks*/,
	    es_poll_callback, es);

	ES_UNLOCK(es);

	for (i = 0; i < ES_NCHANS; i++) {
		if (trigger & (1 << i))
			chn_intr(es->ch[i].channel);
	}
}

static int
eschan_trigger(kobj_t obj, void *data, int go)
{
	struct es_chinfo *ch = data;
	struct es_info *es = ch->parent;
	uint32_t cnt, b = 0;

	if (!PCMTRIG_COMMON(go))
		return 0;

	ES_LOCK(es);
	cnt = (ch->blksz / sndbuf_getalign(ch->buffer)) - 1;
	if (ch->fmt & AFMT_16BIT)
		b |= 0x02;
	if (AFMT_CHANNEL(ch->fmt) > 1)
		b |= 0x01;
	if (ch->dir == PCMDIR_PLAY) {
		if (go == PCMTRIG_START) {
			if (ch->index == ES_DAC1) {
				es->ctrl |= CTRL_DAC1_EN;
				es->sctrl &= ~(SCTRL_P1LOOPSEL |
				    SCTRL_P1PAUSE | SCTRL_P1SCTRLD);
				if (es->polling == 0)
					es->sctrl |= SCTRL_P1INTEN;
				else
					es->sctrl &= ~SCTRL_P1INTEN;
				es->sctrl |= b;
				es_wr(es, ES1370_REG_DAC1_SCOUNT, cnt, 4);
				/* start at beginning of buffer */
				es_wr(es, ES1370_REG_MEMPAGE,
				    ES1370_REG_DAC1_FRAMECNT >> 8, 4);
				es_wr(es, ES1370_REG_DAC1_FRAMECNT & 0xff,
				    (ch->bufsz >> 2) - 1, 4);
			} else {
				es->ctrl |= CTRL_DAC2_EN;
				es->sctrl &= ~(SCTRL_P2ENDINC | SCTRL_P2STINC |
				    SCTRL_P2LOOPSEL | SCTRL_P2PAUSE |
				    SCTRL_P2DACSEN);
				if (es->polling == 0)
					es->sctrl |= SCTRL_P2INTEN;
				else
					es->sctrl &= ~SCTRL_P2INTEN;
				es->sctrl |= (b << 2) |
				    ((((b >> 1) & 1) + 1) << SCTRL_SH_P2ENDINC);
				es_wr(es, ES1370_REG_DAC2_SCOUNT, cnt, 4);
				/* start at beginning of buffer */
				es_wr(es, ES1370_REG_MEMPAGE,
				    ES1370_REG_DAC2_FRAMECNT >> 8, 4);
				es_wr(es, ES1370_REG_DAC2_FRAMECNT & 0xff,
				    (ch->bufsz >> 2) - 1, 4);
			}
		} else
			es->ctrl &= ~((ch->index == ES_DAC1) ?
			    CTRL_DAC1_EN : CTRL_DAC2_EN);
	} else {
		if (go == PCMTRIG_START) {
			es->ctrl |= CTRL_ADC_EN;
			es->sctrl &= ~SCTRL_R1LOOPSEL;
			if (es->polling == 0)
				es->sctrl |= SCTRL_R1INTEN;
			else
				es->sctrl &= ~SCTRL_R1INTEN;
			es->sctrl |= b << 4;
			es_wr(es, ES1370_REG_ADC_SCOUNT, cnt, 4);
			/* start at beginning of buffer */
			es_wr(es, ES1370_REG_MEMPAGE,
			    ES1370_REG_ADC_FRAMECNT >> 8, 4);
			es_wr(es, ES1370_REG_ADC_FRAMECNT & 0xff,
			    (ch->bufsz >> 2) - 1, 4);
		} else
			es->ctrl &= ~CTRL_ADC_EN;
	}
	es_wr(es, ES1370_REG_SERIAL_CONTROL, es->sctrl, 4);
	es_wr(es, ES1370_REG_CONTROL, es->ctrl, 4);
	if (go == PCMTRIG_START) {
		if (es->polling != 0) {
			ch->ptr = 0;
			ch->prevptr = 0;
			if (es_chan_active(es) == 0) {
				es->poll_ticks = 1;
				callout_reset(&es->poll_timer, 1,
				    es_poll_callback, es);
			}
		}
		ch->active = 1;
	} else {
		ch->active = 0;
		if (es->polling != 0) {
			if (es_chan_active(es) == 0) {
				callout_stop(&es->poll_timer);
				es->poll_ticks = 1;
			}
		}
	}
	ES_UNLOCK(es);
	return (0);
}

static uint32_t
eschan_getptr(kobj_t obj, void *data)
{
	struct es_chinfo *ch = data;
	struct es_info *es = ch->parent;
	uint32_t reg, cnt;

	ES_LOCK(es);
	if (es->polling != 0)
		cnt = ch->ptr;
	else {
		if (ch->dir == PCMDIR_PLAY) {
			if (ch->index == ES_DAC1)
				reg = ES1370_REG_DAC1_FRAMECNT;
			else
				reg = ES1370_REG_DAC2_FRAMECNT;
		} else
			reg = ES1370_REG_ADC_FRAMECNT;
		es_wr(es, ES1370_REG_MEMPAGE, reg >> 8, 4);
		cnt = es_rd(es, reg & 0x000000ff, 4) >> 16;
		/* cnt is longwords */
		cnt <<= 2;
	}
	ES_UNLOCK(es);

	cnt &= ES_BLK_ALIGN;

	return (cnt);
}

static struct pcmchan_caps *
eschan_getcaps(kobj_t obj, void *data)
{
	struct es_chinfo *ch = data;

	return (&ch->caps);
}

static kobj_method_t eschan1370_methods[] = {
	KOBJMETHOD(channel_init,		eschan_init),
	KOBJMETHOD(channel_setformat,		eschan_setformat),
	KOBJMETHOD(channel_setspeed,		eschan1370_setspeed),
	KOBJMETHOD(channel_setblocksize,	eschan_setblocksize),
	KOBJMETHOD(channel_setfragments,	eschan_setfragments),
	KOBJMETHOD(channel_trigger,		eschan_trigger),
	KOBJMETHOD(channel_getptr,		eschan_getptr),
	KOBJMETHOD(channel_getcaps,		eschan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(eschan1370);

static kobj_method_t eschan1371_methods[] = {
	KOBJMETHOD(channel_init,		eschan_init),
	KOBJMETHOD(channel_setformat,		eschan_setformat),
	KOBJMETHOD(channel_setspeed,		eschan1371_setspeed),
	KOBJMETHOD(channel_setblocksize,	eschan_setblocksize),
	KOBJMETHOD(channel_setfragments,	eschan_setfragments),
	KOBJMETHOD(channel_trigger,		eschan_trigger),
	KOBJMETHOD(channel_getptr,		eschan_getptr),
	KOBJMETHOD(channel_getcaps,		eschan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(eschan1371);

/* -------------------------------------------------------------------- */
/* The interrupt handler */
static void
es_intr(void *p)
{
	struct es_info *es = p;
	uint32_t intsrc, sctrl;

	ES_LOCK(es);
	if (es->polling != 0) {
		ES_UNLOCK(es);
		return;
	}
	intsrc = es_rd(es, ES1370_REG_STATUS, 4);
	if ((intsrc & STAT_INTR) == 0) {
		ES_UNLOCK(es);
		return;
	}

	sctrl = es->sctrl;
	if (intsrc & STAT_ADC)
		sctrl &= ~SCTRL_R1INTEN;
	if (intsrc & STAT_DAC1)
		sctrl &= ~SCTRL_P1INTEN;
	if (intsrc & STAT_DAC2)
		sctrl &= ~SCTRL_P2INTEN;

	es_wr(es, ES1370_REG_SERIAL_CONTROL, sctrl, 4);
	es_wr(es, ES1370_REG_SERIAL_CONTROL, es->sctrl, 4);
	ES_UNLOCK(es);

	if (intsrc & STAT_ADC)
		chn_intr(es->ch[ES_ADC].channel);
	if (intsrc & STAT_DAC1)
		chn_intr(es->ch[ES_DAC1].channel);
	if (intsrc & STAT_DAC2)
		chn_intr(es->ch[ES_DAC2].channel);
}

/* ES1370 specific */
static int
es1370_init(struct es_info *es)
{
	uint32_t fixed_rate;
	int r, single_pcm;

	/* ES1370 default to fixed rate operation */
	if (resource_int_value(device_get_name(es->dev),
	    device_get_unit(es->dev), "fixed_rate", &r) == 0) {
		fixed_rate = r;
		if (fixed_rate) {
			if (fixed_rate < es_caps.minspeed)
				fixed_rate = es_caps.minspeed;
			if (fixed_rate > es_caps.maxspeed)
				fixed_rate = es_caps.maxspeed;
		}
	} else
		fixed_rate = es_caps.maxspeed;

	if (resource_int_value(device_get_name(es->dev),
	    device_get_unit(es->dev), "single_pcm_mixer", &r) == 0)
		single_pcm = (r != 0) ? 1 : 0;
	else
		single_pcm = 1;

	ES_LOCK(es);
	if (ES_NUMPLAY(es->escfg) == 1)
		single_pcm = 1;
	/* This is ES1370 */
	es->escfg = ES_SET_IS_ES1370(es->escfg, 1);
	if (fixed_rate)
		es->escfg = ES_SET_FIXED_RATE(es->escfg, fixed_rate);
	else {
		es->escfg = ES_SET_FIXED_RATE(es->escfg, 0);
		fixed_rate = DSP_DEFAULT_SPEED;
	}
	if (single_pcm)
		es->escfg = ES_SET_SINGLE_PCM_MIX(es->escfg, 1);
	else
		es->escfg = ES_SET_SINGLE_PCM_MIX(es->escfg, 0);
	es->ctrl = CTRL_CDC_EN | CTRL_JYSTK_EN | CTRL_SERR_DIS |
	    (DAC2_SRTODIV(fixed_rate) << CTRL_SH_PCLKDIV);
	es->ctrl |= 3 << CTRL_SH_WTSRSEL;
	es_wr(es, ES1370_REG_CONTROL, es->ctrl, 4);

	es->sctrl = 0;
	es_wr(es, ES1370_REG_SERIAL_CONTROL, es->sctrl, 4);

	/* No RST, PD */
	es1370_wrcodec(es, CODEC_RES_PD, 3);
	/*
	 * CODEC ADC and CODEC DAC use {LR,B}CLK2 and run off the LRCLK2 PLL;
	 * program DAC_SYNC=0!
	 */
	es1370_wrcodec(es, CODEC_CSEL, 0);
	/* Recording source is mixer */
	es1370_wrcodec(es, CODEC_ADSEL, 0);
	/* MIC amp is 0db */
	es1370_wrcodec(es, CODEC_MGAIN, 0);
	ES_UNLOCK(es);

	return (0);
}

/* ES1371 specific */
int
es1371_init(struct es_info *es)
{
	uint32_t cssr, devid, revid, subdev;
	int idx;

	ES_LOCK(es);
	/* This is NOT ES1370 */
	es->escfg = ES_SET_IS_ES1370(es->escfg, 0);
	es->num = 0;
	es->sctrl = 0;
	cssr = 0;
	devid = pci_get_devid(es->dev);
	revid = pci_get_revid(es->dev);
	subdev = (pci_get_subdevice(es->dev) << 16) |
	    pci_get_subvendor(es->dev);
	/*
	 * Joyport blacklist. Either we're facing with broken hardware
	 * or because this hardware need special (unknown) initialization
	 * procedures.
	 */
	switch (subdev) {
	case 0x20001274:	/* old Ensoniq */
		es->ctrl = 0;
		break;
	default:
		es->ctrl = CTRL_JYSTK_EN;
		break;
	}
	if (devid == CT4730_PCI_ID) {
		/* XXX amplifier hack? */
		es->ctrl |= (1 << 16);
	}
	/* initialize the chips */
	es_wr(es, ES1370_REG_CONTROL, es->ctrl, 4);
	es_wr(es, ES1370_REG_SERIAL_CONTROL, es->sctrl, 4);
	es_wr(es, ES1371_REG_LEGACY, 0, 4);
	if ((devid == ES1371_PCI_ID && revid == ES1371REV_ES1373_8) ||
	    (devid == ES1371_PCI_ID && revid == ES1371REV_CT5880_A) ||
	    (devid == CT5880_PCI_ID && revid == CT5880REV_CT5880_C) ||
	    (devid == CT5880_PCI_ID && revid == CT5880REV_CT5880_D) ||
	    (devid == CT5880_PCI_ID && revid == CT5880REV_CT5880_E)) {
		cssr = 1 << 29;
		es_wr(es, ES1370_REG_STATUS, cssr, 4);
		DELAY(20000);
	}
	/* AC'97 warm reset to start the bitclk */
	es_wr(es, ES1370_REG_CONTROL, es->ctrl, 4);
	es_wr(es, ES1371_REG_LEGACY, ES1371_SYNC_RES, 4);
	DELAY(2000);
	es_wr(es, ES1370_REG_CONTROL, es->sctrl, 4);
	es1371_wait_src_ready(es);
	/* Init the sample rate converter */
	es_wr(es, ES1371_REG_SMPRATE, ES1371_DIS_SRC, 4);
	for (idx = 0; idx < 0x80; idx++)
		es1371_src_write(es, idx, 0);
	es1371_src_write(es, ES_SMPREG_DAC1 + ES_SMPREG_TRUNC_N, 16 << 4);
	es1371_src_write(es, ES_SMPREG_DAC1 + ES_SMPREG_INT_REGS, 16 << 10);
	es1371_src_write(es, ES_SMPREG_DAC2 + ES_SMPREG_TRUNC_N, 16 << 4);
	es1371_src_write(es, ES_SMPREG_DAC2 + ES_SMPREG_INT_REGS, 16 << 10);
	es1371_src_write(es, ES_SMPREG_VOL_ADC, 1 << 12);
	es1371_src_write(es, ES_SMPREG_VOL_ADC + 1, 1 << 12);
	es1371_src_write(es, ES_SMPREG_VOL_DAC1, 1 << 12);
	es1371_src_write(es, ES_SMPREG_VOL_DAC1 + 1, 1 << 12);
	es1371_src_write(es, ES_SMPREG_VOL_DAC2, 1 << 12);
	es1371_src_write(es, ES_SMPREG_VOL_DAC2 + 1, 1 << 12);
	es1371_adc_rate(es, 22050, ES_ADC);
	es1371_dac_rate(es, 22050, ES_DAC1);
	es1371_dac_rate(es, 22050, ES_DAC2);
	/*
	 * WARNING:
	 * enabling the sample rate converter without properly programming
	 * its parameters causes the chip to lock up (the SRC busy bit will
	 * be stuck high, and I've found no way to rectify this other than
	 * power cycle)
	 */
	es1371_wait_src_ready(es);
	es_wr(es, ES1371_REG_SMPRATE, 0, 4);
	/* try to reset codec directly */
	es_wr(es, ES1371_REG_CODEC, 0, 4);
	es_wr(es, ES1370_REG_STATUS, cssr, 4);
	ES_UNLOCK(es);

	return (0);
}

/* -------------------------------------------------------------------- */

static int
es1371_wrcd(kobj_t obj, void *s, int addr, uint32_t data)
{
	uint32_t t, x, orig;
	struct es_info *es = (struct es_info*)s;

	for (t = 0; t < 0x1000; t++) {
	  	if (!es_rd(es, ES1371_REG_CODEC & CODEC_WIP, 4))
			break;
	}
	/* save the current state for later */
 	x = orig = es_rd(es, ES1371_REG_SMPRATE, 4);
	/* enable SRC state data in SRC mux */
	es_wr(es, ES1371_REG_SMPRATE, (x & (ES1371_DIS_SRC | ES1371_DIS_P1 |
	    ES1371_DIS_P2 | ES1371_DIS_R1)) | 0x00010000, 4);
	/* busy wait */
	for (t = 0; t < 0x1000; t++) {
	  	if ((es_rd(es, ES1371_REG_SMPRATE, 4) & 0x00870000) ==
		    0x00000000)
			break;
	}
	/* wait for a SAFE time to write addr/data and then do it, dammit */
	for (t = 0; t < 0x1000; t++) {
	  	if ((es_rd(es, ES1371_REG_SMPRATE, 4) & 0x00870000) ==
		    0x00010000)
			break;
	}

	es_wr(es, ES1371_REG_CODEC, ((addr << CODEC_POADD_SHIFT) &
	    CODEC_POADD_MASK) | ((data << CODEC_PODAT_SHIFT) &
	    CODEC_PODAT_MASK), 4);
	/* restore SRC reg */
	es1371_wait_src_ready(s);
	es_wr(es, ES1371_REG_SMPRATE, orig, 4);

	return (0);
}

static int
es1371_rdcd(kobj_t obj, void *s, int addr)
{
  	uint32_t t, x, orig;
  	struct es_info *es = (struct es_info *)s;

  	for (t = 0; t < 0x1000; t++) {
		if (!(x = es_rd(es, ES1371_REG_CODEC, 4) & CODEC_WIP))
	  		break;
	}

  	/* save the current state for later */
  	x = orig = es_rd(es, ES1371_REG_SMPRATE, 4);
  	/* enable SRC state data in SRC mux */
  	es_wr(es, ES1371_REG_SMPRATE, (x & (ES1371_DIS_SRC | ES1371_DIS_P1 |
	    ES1371_DIS_P2 | ES1371_DIS_R1)) | 0x00010000, 4);
	/* busy wait */
  	for (t = 0; t < 0x1000; t++) {
		if ((x = es_rd(es, ES1371_REG_SMPRATE, 4) & 0x00870000) ==
		    0x00000000)
	  		break;
	}
  	/* wait for a SAFE time to write addr/data and then do it, dammit */
  	for (t = 0; t < 0x1000; t++) {
		if ((x = es_rd(es, ES1371_REG_SMPRATE, 4) & 0x00870000) ==
		    0x00010000)
	  		break;
	}

  	es_wr(es, ES1371_REG_CODEC, ((addr << CODEC_POADD_SHIFT) &
	    CODEC_POADD_MASK) | CODEC_PORD, 4);

  	/* restore SRC reg */
  	es1371_wait_src_ready(s);
  	es_wr(es, ES1371_REG_SMPRATE, orig, 4);

  	/* now wait for the stinkin' data (RDY) */
  	for (t = 0; t < 0x1000; t++) {
		if ((x = es_rd(es, ES1371_REG_CODEC, 4)) & CODEC_RDY)
	  		break;
	}

  	return ((x & CODEC_PIDAT_MASK) >> CODEC_PIDAT_SHIFT);
}

static kobj_method_t es1371_ac97_methods[] = {
	KOBJMETHOD(ac97_read,		es1371_rdcd),
	KOBJMETHOD(ac97_write,		es1371_wrcd),
	KOBJMETHOD_END
};
AC97_DECLARE(es1371_ac97);

/* -------------------------------------------------------------------- */

static unsigned int
es1371_src_read(struct es_info *es, unsigned short reg)
{
  	uint32_t r;

  	r = es1371_wait_src_ready(es) & (ES1371_DIS_SRC | ES1371_DIS_P1 |
	    ES1371_DIS_P2 | ES1371_DIS_R1);
  	r |= ES1371_SRC_RAM_ADDRO(reg);
  	es_wr(es, ES1371_REG_SMPRATE, r, 4);
  	return (ES1371_SRC_RAM_DATAI(es1371_wait_src_ready(es)));
}

static void
es1371_src_write(struct es_info *es, unsigned short reg, unsigned short data)
{
	uint32_t r;

	r = es1371_wait_src_ready(es) & (ES1371_DIS_SRC | ES1371_DIS_P1 |
	    ES1371_DIS_P2 | ES1371_DIS_R1);
	r |= ES1371_SRC_RAM_ADDRO(reg) |  ES1371_SRC_RAM_DATAO(data);
	es_wr(es, ES1371_REG_SMPRATE, r | ES1371_SRC_RAM_WE, 4);
}

static unsigned int
es1371_adc_rate(struct es_info *es, unsigned int rate, int set)
{
  	unsigned int n, truncm, freq, result;

	ES_LOCK_ASSERT(es);

  	if (rate > 48000)
		rate = 48000;
  	if (rate < 4000)
		rate = 4000;
  	n = rate / 3000;
  	if ((1 << n) & ((1 << 15) | (1 << 13) | (1 << 11) | (1 << 9)))
		n--;
  	truncm = (21 * n - 1) | 1;
  	freq = ((48000UL << 15) / rate) * n;
  	result = (48000UL << 15) / (freq / n);
  	if (set) {
		if (rate >= 24000) {
	  		if (truncm > 239)
				truncm = 239;
	  		es1371_src_write(es, ES_SMPREG_ADC + ES_SMPREG_TRUNC_N,
			    (((239 - truncm) >> 1) << 9) | (n << 4));
		} else {
	  		if (truncm > 119)
				truncm = 119;
	  		es1371_src_write(es, ES_SMPREG_ADC + ES_SMPREG_TRUNC_N,
			    0x8000 | (((119 - truncm) >> 1) << 9) | (n << 4));
		}
		es1371_src_write(es, ES_SMPREG_ADC + ES_SMPREG_INT_REGS,
		    (es1371_src_read(es, ES_SMPREG_ADC + ES_SMPREG_INT_REGS) &
		    0x00ff) | ((freq >> 5) & 0xfc00));
		es1371_src_write(es, ES_SMPREG_ADC + ES_SMPREG_VFREQ_FRAC,
		    freq & 0x7fff);
		es1371_src_write(es, ES_SMPREG_VOL_ADC, n << 8);
		es1371_src_write(es, ES_SMPREG_VOL_ADC + 1, n << 8);
	}
	return (result);
}

static unsigned int
es1371_dac_rate(struct es_info *es, unsigned int rate, int set)
{
  	unsigned int freq, r, result, dac, dis;

	ES_LOCK_ASSERT(es);

  	if (rate > 48000)
		rate = 48000;
  	if (rate < 4000)
		rate = 4000;
  	freq = ((rate << 15) + 1500) / 3000;
  	result = (freq * 3000) >> 15;

	dac = (set == ES_DAC1) ? ES_SMPREG_DAC1 : ES_SMPREG_DAC2;
	dis = (set == ES_DAC1) ? ES1371_DIS_P2 : ES1371_DIS_P1;
	r = (es1371_wait_src_ready(es) & (ES1371_DIS_SRC | ES1371_DIS_P1 |
	    ES1371_DIS_P2 | ES1371_DIS_R1));
	es_wr(es, ES1371_REG_SMPRATE, r, 4);
	es1371_src_write(es, dac + ES_SMPREG_INT_REGS,
	    (es1371_src_read(es, dac + ES_SMPREG_INT_REGS) & 0x00ff) |
	    ((freq >> 5) & 0xfc00));
	es1371_src_write(es, dac + ES_SMPREG_VFREQ_FRAC, freq & 0x7fff);
	r = (es1371_wait_src_ready(es) &
	    (ES1371_DIS_SRC | dis | ES1371_DIS_R1));
	es_wr(es, ES1371_REG_SMPRATE, r, 4);
  	return (result);
}

static uint32_t
es1371_wait_src_ready(struct es_info *es)
{
  	uint32_t t, r;

  	for (t = 0; t < 0x1000; t++) {
		if (!((r = es_rd(es, ES1371_REG_SMPRATE, 4)) &
		    ES1371_SRC_RAM_BUSY))
	  		return (r);
		DELAY(1);
  	}
	device_printf(es->dev, "%s: timed out 0x%x [0x%x]\n", __func__,
		ES1371_REG_SMPRATE, r);
  	return (0);
}

/* -------------------------------------------------------------------- */

/*
 * Probe and attach the card
 */

static int
es_pci_probe(device_t dev)
{
	switch(pci_get_devid(dev)) {
	case ES1370_PCI_ID:
		device_set_desc(dev, "AudioPCI ES1370");
		return (BUS_PROBE_DEFAULT);
	case ES1371_PCI_ID:
		switch(pci_get_revid(dev)) {
		case ES1371REV_ES1371_A:
			device_set_desc(dev, "AudioPCI ES1371-A");
			return (BUS_PROBE_DEFAULT);
		case ES1371REV_ES1371_B:
			device_set_desc(dev, "AudioPCI ES1371-B");
			return (BUS_PROBE_DEFAULT);
		case ES1371REV_ES1373_A:
			device_set_desc(dev, "AudioPCI ES1373-A");
			return (BUS_PROBE_DEFAULT);
		case ES1371REV_ES1373_B:
			device_set_desc(dev, "AudioPCI ES1373-B");
			return (BUS_PROBE_DEFAULT);
		case ES1371REV_ES1373_8:
			device_set_desc(dev, "AudioPCI ES1373-8");
			return (BUS_PROBE_DEFAULT);
		case ES1371REV_CT5880_A:
			device_set_desc(dev, "Creative CT5880-A");
			return (BUS_PROBE_DEFAULT);
		default:
			device_set_desc(dev, "AudioPCI ES1371-?");
			device_printf(dev,
			    "unknown revision %d -- please report to "
			    "freebsd-multimedia@freebsd.org\n",
			    pci_get_revid(dev));
			return (BUS_PROBE_DEFAULT);
		}
	case ES1371_PCI_ID2:
		device_set_desc(dev, "Strange AudioPCI ES1371-? (vid=3274)");
		device_printf(dev,
		    "unknown revision %d -- please report to "
		    "freebsd-multimedia@freebsd.org\n", pci_get_revid(dev));
		return (BUS_PROBE_DEFAULT);
	case CT4730_PCI_ID:
		switch(pci_get_revid(dev)) {
		case CT4730REV_CT4730_A:
			device_set_desc(dev,
			    "Creative SB AudioPCI CT4730/EV1938");
			return (BUS_PROBE_DEFAULT);
		default:
			device_set_desc(dev, "Creative SB AudioPCI CT4730-?");
			device_printf(dev,
			    "unknown revision %d -- please report to "
			    "freebsd-multimedia@freebsd.org\n",
			    pci_get_revid(dev));
			return (BUS_PROBE_DEFAULT);
		}
	case CT5880_PCI_ID:
		switch(pci_get_revid(dev)) {
		case CT5880REV_CT5880_C:
			device_set_desc(dev, "Creative CT5880-C");
			return (BUS_PROBE_DEFAULT);
		case CT5880REV_CT5880_D:
			device_set_desc(dev, "Creative CT5880-D");
			return (BUS_PROBE_DEFAULT);
		case CT5880REV_CT5880_E:
			device_set_desc(dev, "Creative CT5880-E");
			return (BUS_PROBE_DEFAULT);
		default:
			device_set_desc(dev, "Creative CT5880-?");
			device_printf(dev,
			    "unknown revision %d -- please report to "
			    "freebsd-multimedia@freebsd.org\n",
			    pci_get_revid(dev));
			return (BUS_PROBE_DEFAULT);
		}
	default:
		return (ENXIO);
	}
}

static int
sysctl_es137x_spdif_enable(SYSCTL_HANDLER_ARGS)
{
	struct es_info *es;
	device_t dev;
	uint32_t r;
	int err, new_en;

	dev = oidp->oid_arg1;
	es = pcm_getdevinfo(dev);
	ES_LOCK(es);
	r = es_rd(es, ES1370_REG_STATUS, 4);
	ES_UNLOCK(es);
	new_en = (r & ENABLE_SPDIF) ? 1 : 0;
	err = sysctl_handle_int(oidp, &new_en, 0, req);

	if (err || req->newptr == NULL)
		return (err);
	if (new_en < 0 || new_en > 1)
		return (EINVAL);

	ES_LOCK(es);
	if (new_en) {
		r |= ENABLE_SPDIF;
		es->ctrl |= SPDIFEN_B;
		es->ctrl |= RECEN_B;
	} else {
		r &= ~ENABLE_SPDIF;
		es->ctrl &= ~SPDIFEN_B;
		es->ctrl &= ~RECEN_B;
	}
	es_wr(es, ES1370_REG_CONTROL, es->ctrl, 4);
	es_wr(es, ES1370_REG_STATUS, r, 4);
	ES_UNLOCK(es);

	return (0);
}

static int
sysctl_es137x_latency_timer(SYSCTL_HANDLER_ARGS)
{
	struct es_info *es;
	device_t dev;
	uint32_t val;
	int err;

	dev = oidp->oid_arg1;
	es = pcm_getdevinfo(dev);
	ES_LOCK(es);
	val = pci_read_config(dev, PCIR_LATTIMER, 1);
	ES_UNLOCK(es);
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err || req->newptr == NULL)
		return (err);
	if (val > 255)
		return (EINVAL);

	ES_LOCK(es);
	pci_write_config(dev, PCIR_LATTIMER, val, 1);
	ES_UNLOCK(es);

	return (0);
}

static int
sysctl_es137x_fixed_rate(SYSCTL_HANDLER_ARGS)
{
	struct es_info *es;
	device_t dev;
	uint32_t val;
	int err;

	dev = oidp->oid_arg1;
	es = pcm_getdevinfo(dev);
	ES_LOCK(es);
	val = ES_FIXED_RATE(es->escfg);
	if (val < es_caps.minspeed)
		val = 0;
	ES_UNLOCK(es);
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err || req->newptr == NULL)
		return (err);
	if (val != 0 && (val < es_caps.minspeed || val > es_caps.maxspeed))
		return (EINVAL);

	ES_LOCK(es);
	if (es->ctrl & (CTRL_DAC2_EN|CTRL_ADC_EN)) {
		ES_UNLOCK(es);
		return (EBUSY);
	}
	if (val) {
		if (val != ES_FIXED_RATE(es->escfg)) {
			es->escfg = ES_SET_FIXED_RATE(es->escfg, val);
			es->ch[ES_DAC2].caps.maxspeed = val;
			es->ch[ES_DAC2].caps.minspeed = val;
			es->ch[ES_ADC].caps.maxspeed = val;
			es->ch[ES_ADC].caps.minspeed = val;
			es->ctrl &= ~CTRL_PCLKDIV;
			es->ctrl |= DAC2_SRTODIV(val) << CTRL_SH_PCLKDIV;
			es_wr(es, ES1370_REG_CONTROL, es->ctrl, 4);
		}
	} else {
		es->escfg = ES_SET_FIXED_RATE(es->escfg, 0);
		es->ch[ES_DAC2].caps = es_caps;
		es->ch[ES_ADC].caps = es_caps;
	}
	ES_UNLOCK(es);

	return (0);
}

static int
sysctl_es137x_single_pcm_mixer(SYSCTL_HANDLER_ARGS)
{
	struct es_info *es;
	struct snddev_info *d;
	struct snd_mixer *m;
	device_t dev;
	uint32_t val, set;
	int recsrc, level, err;

	dev = oidp->oid_arg1;
	d = device_get_softc(dev);
	if (!PCM_REGISTERED(d) || d->mixer_dev == NULL ||
	    d->mixer_dev->si_drv1 == NULL)
		return (EINVAL);
	es = d->devinfo;
	if (es == NULL)
		return (EINVAL);
	ES_LOCK(es);
	set = ES_SINGLE_PCM_MIX(es->escfg);
	val = set;
	ES_UNLOCK(es);
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err || req->newptr == NULL)
		return (err);
	if (!(val == 0 || val == 1))
		return (EINVAL);
	if (val == set)
		return (0);
	PCM_ACQUIRE_QUICK(d);
	m = (d->mixer_dev != NULL) ? d->mixer_dev->si_drv1 : NULL;
	if (m == NULL) {
		PCM_RELEASE_QUICK(d);
		return (ENODEV);
	}
	if (mixer_busy(m) != 0) {
		PCM_RELEASE_QUICK(d);
		return (EBUSY);
	}
	level = mix_get(m, SOUND_MIXER_PCM);
	recsrc = mix_getrecsrc(m);
	if (level < 0 || recsrc < 0) {
		PCM_RELEASE_QUICK(d);
		return (ENXIO);
	}

	ES_LOCK(es);
	if (es->ctrl & (CTRL_ADC_EN | CTRL_DAC1_EN | CTRL_DAC2_EN)) {
		ES_UNLOCK(es);
		PCM_RELEASE_QUICK(d);
		return (EBUSY);
	}
	if (val)
		es->escfg = ES_SET_SINGLE_PCM_MIX(es->escfg, 1);
	else
		es->escfg = ES_SET_SINGLE_PCM_MIX(es->escfg, 0);
	ES_UNLOCK(es);
	if (!val) {
		mix_setdevs(m, mix_getdevs(m) | (1 << SOUND_MIXER_SYNTH));
		mix_setrecdevs(m, mix_getrecdevs(m) | (1 << SOUND_MIXER_SYNTH));
		err = mix_set(m, SOUND_MIXER_SYNTH, level & 0x7f,
		    (level >> 8) & 0x7f);
	} else {
		err = mix_set(m, SOUND_MIXER_SYNTH, level & 0x7f,
		    (level >> 8) & 0x7f);
		mix_setdevs(m, mix_getdevs(m) & ~(1 << SOUND_MIXER_SYNTH));
		mix_setrecdevs(m, mix_getrecdevs(m) &
		    ~(1 << SOUND_MIXER_SYNTH));
	}
	if (!err) {
		level = recsrc;
		if (recsrc & (1 << SOUND_MIXER_PCM))
			recsrc |= 1 << SOUND_MIXER_SYNTH;
		else if (recsrc & (1 << SOUND_MIXER_SYNTH))
			recsrc |= 1 << SOUND_MIXER_PCM;
		if (level != recsrc)
			err = mix_setrecsrc(m, recsrc);
	}

	PCM_RELEASE_QUICK(d);

	return (err);
}

static int
sysctl_es_polling(SYSCTL_HANDLER_ARGS)
{
	struct es_info *es;
	device_t dev;
	int err, val;

	dev = oidp->oid_arg1;
	es = pcm_getdevinfo(dev);
	if (es == NULL)
		return (EINVAL);
	ES_LOCK(es);
	val = es->polling;
	ES_UNLOCK(es);
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err || req->newptr == NULL)
		return (err);
	if (val < 0 || val > 1)
		return (EINVAL);

	ES_LOCK(es);
	if (val != es->polling) {
		if (es_chan_active(es) != 0)
			err = EBUSY;
		else if (val == 0)
			es->polling = 0;
		else
			es->polling = 1;
	}
	ES_UNLOCK(es);

	return (err);
}

static void
es_init_sysctls(device_t dev)
{
	struct es_info *es;
	int r, devid, revid;

	devid = pci_get_devid(dev);
	revid = pci_get_revid(dev);
	es = pcm_getdevinfo(dev);
	if ((devid == ES1371_PCI_ID && revid == ES1371REV_ES1373_8) ||
	    (devid == ES1371_PCI_ID && revid == ES1371REV_CT5880_A) ||
	    (devid == CT5880_PCI_ID && revid == CT5880REV_CT5880_C) ||
	    (devid == CT5880_PCI_ID && revid == CT5880REV_CT5880_D) ||
	    (devid == CT5880_PCI_ID && revid == CT5880REV_CT5880_E)) {
		/* XXX: an user should be able to set this with a control tool,
		   if not done before 7.0-RELEASE, this needs to be converted
		   to a device specific sysctl "dev.pcm.X.yyy" via
		   device_get_sysctl_*() as discussed on multimedia@ in msg-id
		   <861wujij2q.fsf@xps.des.no> */
		SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
		    "spdif_enabled", CTLTYPE_INT | CTLFLAG_RW, dev, sizeof(dev),
		    sysctl_es137x_spdif_enable, "I",
		    "Enable S/PDIF output on primary playback channel");
	} else if (devid == ES1370_PCI_ID) {
		/*
		 * Enable fixed rate sysctl if both DAC2 / ADC enabled.
		 */
		if (es->ch[ES_DAC2].channel != NULL &&
		    es->ch[ES_ADC].channel != NULL) {
		/* XXX: an user should be able to set this with a control tool,
		   if not done before 7.0-RELEASE, this needs to be converted
		   to a device specific sysctl "dev.pcm.X.yyy" via
		   device_get_sysctl_*() as discussed on multimedia@ in msg-id
		   <861wujij2q.fsf@xps.des.no> */
			SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
			    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
			    OID_AUTO, "fixed_rate", CTLTYPE_INT | CTLFLAG_RW,
			    dev, sizeof(dev), sysctl_es137x_fixed_rate, "I",
			    "Enable fixed rate playback/recording");
		}
		/*
		 * Enable single pcm mixer sysctl if both DAC1/2 enabled.
		 */
		if (es->ch[ES_DAC1].channel != NULL &&
		    es->ch[ES_DAC2].channel != NULL) {
		/* XXX: an user should be able to set this with a control tool,
		   if not done before 7.0-RELEASE, this needs to be converted
		   to a device specific sysctl "dev.pcm.X.yyy" via
		   device_get_sysctl_*() as discussed on multimedia@ in msg-id
		   <861wujij2q.fsf@xps.des.no> */
			SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
			    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
			    OID_AUTO, "single_pcm_mixer",
			    CTLTYPE_INT | CTLFLAG_RW, dev, sizeof(dev),
			    sysctl_es137x_single_pcm_mixer, "I",
			    "Single PCM mixer controller for both DAC1/DAC2");
		}
	}
	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "latency_timer", &r) == 0 && !(r < 0 || r > 255))
		pci_write_config(dev, PCIR_LATTIMER, r, 1);
	/* XXX: this needs to be converted to a device specific sysctl
	   "dev.pcm.X.yyy" via device_get_sysctl_*() as discussed on
	   multimedia@ in msg-id <861wujij2q.fsf@xps.des.no> */
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "latency_timer", CTLTYPE_INT | CTLFLAG_RW, dev, sizeof(dev),
	    sysctl_es137x_latency_timer, "I",
	    "PCI Latency Timer configuration");
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "polling", CTLTYPE_INT | CTLFLAG_RW, dev, sizeof(dev),
	    sysctl_es_polling, "I",
	    "Enable polling mode");
}

static int
es_pci_attach(device_t dev)
{
	struct es_info *es = NULL;
	int		mapped, i, numplay, dac_cfg;
	char		status[SND_STATUSLEN];
	struct ac97_info *codec = NULL;
	kobj_class_t    ct = NULL;
	uint32_t devid;

	es = malloc(sizeof *es, M_DEVBUF, M_WAITOK | M_ZERO);
	es->lock = snd_mtxcreate(device_get_nameunit(dev), "snd_es137x softc");
	es->dev = dev;
	es->escfg = 0;
	mapped = 0;

	pci_enable_busmaster(dev);
	if (mapped == 0) {
		es->regid = MEM_MAP_REG;
		es->regtype = SYS_RES_MEMORY;
		es->reg = bus_alloc_resource_any(dev, es->regtype, &es->regid,
		    RF_ACTIVE);
		if (es->reg)
			mapped++;
	}
	if (mapped == 0) {
		es->regid = PCIR_BAR(0);
		es->regtype = SYS_RES_IOPORT;
		es->reg = bus_alloc_resource_any(dev, es->regtype, &es->regid,
		    RF_ACTIVE);
		if (es->reg)
			mapped++;
	}
	if (mapped == 0) {
		device_printf(dev, "unable to map register space\n");
		goto bad;
	}

	es->st = rman_get_bustag(es->reg);
	es->sh = rman_get_bushandle(es->reg);
	callout_init(&es->poll_timer, 1);
	es->poll_ticks = 1;

	if (resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "polling", &i) == 0 && i != 0)
		es->polling = 1;
	else
		es->polling = 0;

	es->bufsz = pcm_getbuffersize(dev, 4096, ES_DEFAULT_BUFSZ, 65536);
	if (resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "blocksize", &i) == 0 && i > 0) {
		i &= ES_BLK_ALIGN;
		if (i < ES_BLK_MIN)
			i = ES_BLK_MIN;
		es->blkcnt = es->bufsz / i;
		i = 0;
		while (es->blkcnt >> i)
			i++;
		es->blkcnt = 1 << (i - 1);
		if (es->blkcnt < ES_DMA_SEGS_MIN)
			es->blkcnt = ES_DMA_SEGS_MIN;
		else if (es->blkcnt > ES_DMA_SEGS_MAX)
			es->blkcnt = ES_DMA_SEGS_MAX;

	} else
		es->blkcnt = 2;

	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "dac", &dac_cfg) == 0) {
		if (dac_cfg < 0 || dac_cfg > 3)
			dac_cfg = ES_DEFAULT_DAC_CFG;
	} else
		dac_cfg = ES_DEFAULT_DAC_CFG;

	switch (dac_cfg) {
	case 0:	/* Enable all DAC: DAC1, DAC2 */
		numplay = 2;
		es->escfg = ES_SET_DAC_FIRST(es->escfg, ES_DAC1);
		es->escfg = ES_SET_DAC_SECOND(es->escfg, ES_DAC2);
		break;
	case 1: /* Only DAC1 */
		numplay = 1;
		es->escfg = ES_SET_DAC_FIRST(es->escfg, ES_DAC1);
		break;
	case 3: /* Enable all DAC / swap position: DAC2, DAC1 */
		numplay = 2;
		es->escfg = ES_SET_DAC_FIRST(es->escfg, ES_DAC2);
		es->escfg = ES_SET_DAC_SECOND(es->escfg, ES_DAC1);
		break;
	case 2: /* Only DAC2 */
	default:
		numplay = 1;
		es->escfg = ES_SET_DAC_FIRST(es->escfg, ES_DAC2);
		break;
	}
	es->escfg = ES_SET_NUMPLAY(es->escfg, numplay);
	es->escfg = ES_SET_NUMREC(es->escfg, 1);

	devid = pci_get_devid(dev);
	switch (devid) {
	case ES1371_PCI_ID:
	case ES1371_PCI_ID2:
	case CT5880_PCI_ID:
	case CT4730_PCI_ID:
		es1371_init(es);
		codec = AC97_CREATE(dev, es, es1371_ac97);
	  	if (codec == NULL)
			goto bad;
	  	/* our init routine does everything for us */
	  	/* set to NULL; flag mixer_init not to run the ac97_init */
	  	/*	  ac97_mixer.init = NULL;  */
		if (mixer_init(dev, ac97_getmixerclass(), codec))
			goto bad;
		ct = &eschan1371_class;
		break;
	case ES1370_PCI_ID:
	  	es1370_init(es);
		/* 
		 * Disable fixed rate operation if DAC2 disabled.
		 * This is a special case for es1370 only, where the
		 * speed of both ADC and DAC2 locked together.
		 */
		if (!ES_DAC2_ENABLED(es->escfg))
			es->escfg = ES_SET_FIXED_RATE(es->escfg, 0);
	  	if (mixer_init(dev, &es1370_mixer_class, es))
			goto bad;
		ct = &eschan1370_class;
		break;
	default:
		goto bad;
		/* NOTREACHED */
	}

	es->irqid = 0;
	es->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &es->irqid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (!es->irq || snd_setup_intr(dev, es->irq, INTR_MPSAFE, es_intr,
	    es, &es->ih)) {
		device_printf(dev, "unable to map interrupt\n");
		goto bad;
	}

	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev),
		/*alignment*/2, /*boundary*/0,
		/*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
		/*highaddr*/BUS_SPACE_MAXADDR,
		/*filter*/NULL, /*filterarg*/NULL,
		/*maxsize*/es->bufsz, /*nsegments*/1, /*maxsegz*/0x3ffff,
		/*flags*/0, /*lockfunc*/NULL,
		/*lockarg*/NULL, &es->parent_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		goto bad;
	}

	snprintf(status, SND_STATUSLEN, "at %s 0x%jx irq %jd %s",
	    (es->regtype == SYS_RES_IOPORT)? "io" : "memory",
	    rman_get_start(es->reg), rman_get_start(es->irq),
	    PCM_KLDSTRING(snd_es137x));

	if (pcm_register(dev, es, numplay, 1))
		goto bad;
	for (i = 0; i < numplay; i++)
		pcm_addchan(dev, PCMDIR_PLAY, ct, es);
	pcm_addchan(dev, PCMDIR_REC, ct, es);
	es_init_sysctls(dev);
	pcm_setstatus(dev, status);
	es->escfg = ES_SET_GP(es->escfg, 0);
	if (numplay == 1)
		device_printf(dev, "<Playback: DAC%d / Record: ADC>\n",
		    ES_DAC_FIRST(es->escfg) + 1);
	else if (numplay == 2)
		device_printf(dev, "<Playback: DAC%d,DAC%d / Record: ADC>\n",
		    ES_DAC_FIRST(es->escfg) + 1, ES_DAC_SECOND(es->escfg) + 1);
	return (0);

bad:
	if (es->parent_dmat)
		bus_dma_tag_destroy(es->parent_dmat);
	if (es->ih)
		bus_teardown_intr(dev, es->irq, es->ih);
	if (es->irq)
		bus_release_resource(dev, SYS_RES_IRQ, es->irqid, es->irq);
	if (codec)
		ac97_destroy(codec);
	if (es->reg)
		bus_release_resource(dev, es->regtype, es->regid, es->reg);
	if (es->lock)
		snd_mtxfree(es->lock);
	if (es)
		free(es, M_DEVBUF);
	return (ENXIO);
}

static int
es_pci_detach(device_t dev)
{
	int r;
	struct es_info *es;

	r = pcm_unregister(dev);
	if (r)
		return (r);

	es = pcm_getdevinfo(dev);

	if (es != NULL && es->num != 0) {
		ES_LOCK(es);
		es->polling = 0;
		callout_stop(&es->poll_timer);
		ES_UNLOCK(es);
		callout_drain(&es->poll_timer);
	}

	bus_teardown_intr(dev, es->irq, es->ih);
	bus_release_resource(dev, SYS_RES_IRQ, es->irqid, es->irq);
	bus_release_resource(dev, es->regtype, es->regid, es->reg);
	bus_dma_tag_destroy(es->parent_dmat);
	snd_mtxfree(es->lock);
	free(es, M_DEVBUF);

	return (0);
}

static device_method_t es_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		es_pci_probe),
	DEVMETHOD(device_attach,	es_pci_attach),
	DEVMETHOD(device_detach,	es_pci_detach),

	{ 0, 0 }
};

static driver_t es_driver = {
	"pcm",
	es_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_es137x, pci, es_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_es137x, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_es137x, 1);
