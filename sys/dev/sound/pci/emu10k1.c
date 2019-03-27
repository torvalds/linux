/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 David O'Brien <obrien@FreeBSD.org>
 * Copyright (c) 2003 Orlando Bassotto <orlando.bassotto@ieo-research.it>
 * Copyright (c) 1999 Cameron Grant <cg@freebsd.org>
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
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/ac97.h>
#include <dev/sound/pci/emuxkireg.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <sys/queue.h>

#include <dev/sound/midi/mpu401.h>
#include "mpufoi_if.h"

SND_DECLARE_FILE("$FreeBSD$");

/* -------------------------------------------------------------------- */

#define	NUM_G		64	/* use all channels */
#define	WAVEOUT_MAXBUFSIZE 32768
#define	EMUPAGESIZE	4096	/* don't change */
#define	EMUMAXPAGES	(WAVEOUT_MAXBUFSIZE * NUM_G / EMUPAGESIZE)
#define	EMU10K1_PCI_ID	0x00021102	/* 1102 => Creative Labs Vendor ID */
#define	EMU10K2_PCI_ID	0x00041102	
#define	EMU10K3_PCI_ID	0x00081102	
#define	EMU_DEFAULT_BUFSZ	4096
#define EMU_MAX_CHANS	8
#define	EMU_CHANS	4

#define	MAXREQVOICES	8
#define	RESERVED	0
#define	NUM_MIDI	16
#define	NUM_FXSENDS	4

#define	TMEMSIZE	256*1024
#define	TMEMSIZEREG	4

#define	ENABLE		0xffffffff
#define	DISABLE		0x00000000
#define	ENV_ON		EMU_CHAN_DCYSUSV_CHANNELENABLE_MASK
#define	ENV_OFF		0x00	/* XXX: should this be 1? */

#define	EMU_A_IOCFG_GPOUT_A	0x40
#define	EMU_A_IOCFG_GPOUT_D	0x04
#define	EMU_A_IOCFG_GPOUT_AD (EMU_A_IOCFG_GPOUT_A|EMU_A_IOCFG_GPOUT_D)  /* EMU_A_IOCFG_GPOUT0 */

#define	EMU_HCFG_GPOUT1		0x00000800

/* instruction set */
#define iACC3	 0x06
#define iMACINT0 0x04
#define iINTERP  0x0e

#define C_00000000	0x40
#define C_00000001	0x41
#define C_00000004	0x44
#define C_40000000	0x4d
/* Audigy constants */
#define A_C_00000000	0xc0
#define A_C_40000000	0xcd

/* GPRs */
#define FXBUS(x)	(0x00 + (x))
#define EXTIN(x)	(0x10 + (x))
#define EXTOUT(x)	(0x20 + (x))

#define GPR(x)		(EMU_FXGPREGBASE + (x))
#define A_EXTIN(x)	(0x40 + (x))
#define A_FXBUS(x)	(0x00 + (x))
#define A_EXTOUT(x)	(0x60 + (x))
#define A_GPR(x)	(EMU_A_FXGPREGBASE + (x))

/* FX buses */
#define FXBUS_PCM_LEFT		0x00
#define FXBUS_PCM_RIGHT		0x01
#define FXBUS_MIDI_LEFT		0x04
#define FXBUS_MIDI_RIGHT	0x05
#define FXBUS_MIDI_REVERB	0x0c
#define FXBUS_MIDI_CHORUS	0x0d

/* Inputs */
#define EXTIN_AC97_L		0x00
#define EXTIN_AC97_R		0x01
#define EXTIN_SPDIF_CD_L	0x02
#define EXTIN_SPDIF_CD_R	0x03
#define EXTIN_TOSLINK_L		0x06
#define EXTIN_TOSLINK_R		0x07
#define EXTIN_COAX_SPDIF_L	0x0a
#define EXTIN_COAX_SPDIF_R	0x0b
/* Audigy Inputs */
#define A_EXTIN_AC97_L		0x00
#define A_EXTIN_AC97_R		0x01

/* Outputs */
#define EXTOUT_AC97_L	   0x00
#define EXTOUT_AC97_R	   0x01
#define EXTOUT_TOSLINK_L   0x02
#define EXTOUT_TOSLINK_R   0x03
#define EXTOUT_AC97_CENTER 0x04
#define EXTOUT_AC97_LFE	   0x05
#define EXTOUT_HEADPHONE_L 0x06
#define EXTOUT_HEADPHONE_R 0x07
#define EXTOUT_REAR_L	   0x08
#define EXTOUT_REAR_R	   0x09
#define EXTOUT_ADC_CAP_L   0x0a
#define EXTOUT_ADC_CAP_R   0x0b
#define EXTOUT_ACENTER	   0x11
#define EXTOUT_ALFE	   0x12
/* Audigy Outputs */
#define A_EXTOUT_FRONT_L	0x00
#define A_EXTOUT_FRONT_R	0x01
#define A_EXTOUT_CENTER		0x02
#define A_EXTOUT_LFE		0x03
#define A_EXTOUT_HEADPHONE_L	0x04
#define A_EXTOUT_HEADPHONE_R	0x05
#define A_EXTOUT_REAR_L		0x06
#define A_EXTOUT_REAR_R		0x07
#define A_EXTOUT_AFRONT_L	0x08
#define A_EXTOUT_AFRONT_R	0x09
#define A_EXTOUT_ACENTER	0x0a
#define A_EXTOUT_ALFE		0x0b
#define A_EXTOUT_AREAR_L	0x0e
#define A_EXTOUT_AREAR_R	0x0f
#define A_EXTOUT_AC97_L		0x10
#define A_EXTOUT_AC97_R		0x11
#define A_EXTOUT_ADC_CAP_L	0x16
#define A_EXTOUT_ADC_CAP_R	0x17

struct emu_memblk {
	SLIST_ENTRY(emu_memblk) link;
	void *buf;
	bus_addr_t buf_addr;
	u_int32_t pte_start, pte_size;
	bus_dmamap_t buf_map;
};

struct emu_mem {
	u_int8_t bmap[EMUMAXPAGES / 8];
	u_int32_t *ptb_pages;
	void *silent_page;
	bus_addr_t silent_page_addr;
	bus_addr_t ptb_pages_addr;
	bus_dmamap_t ptb_map;
	bus_dmamap_t silent_map;
	SLIST_HEAD(, emu_memblk) blocks;
};

struct emu_voice {
	int vnum;
	unsigned int b16:1, stereo:1, busy:1, running:1, ismaster:1;
	int speed;
	int start, end, vol;
	int fxrt1;	/* FX routing */
	int fxrt2;	/* FX routing (only for audigy) */
	u_int32_t buf;
	struct emu_voice *slave;
	struct pcm_channel *channel;
};

struct sc_info;

/* channel registers */
struct sc_pchinfo {
	int spd, fmt, blksz, run;
	struct emu_voice *master, *slave;
	struct snd_dbuf *buffer;
	struct pcm_channel *channel;
	struct sc_info *parent;
};

struct sc_rchinfo {
	int spd, fmt, run, blksz, num;
	u_int32_t idxreg, basereg, sizereg, setupreg, irqmask;
	struct snd_dbuf *buffer;
	struct pcm_channel *channel;
	struct sc_info *parent;
};

/* device private data */
struct sc_info {
	device_t	dev;
	u_int32_t	type, rev;
	u_int32_t	tos_link:1, APS:1, audigy:1, audigy2:1;
	u_int32_t	addrmask;	/* wider if audigy */

	bus_space_tag_t st;
	bus_space_handle_t sh;
	bus_dma_tag_t parent_dmat;

	struct resource *reg, *irq;
	void		*ih;
	struct mtx	*lock;

	unsigned int bufsz;
	int timer, timerinterval;
	int pnum, rnum;
	int nchans;
	struct emu_mem mem;
	struct emu_voice voice[64];
	struct sc_pchinfo pch[EMU_MAX_CHANS];
	struct sc_rchinfo rch[3];
	struct mpu401   *mpu;
	mpu401_intr_t           *mpu_intr;
	int mputx;
};

/* -------------------------------------------------------------------- */

/*
 * prototypes
 */

/* stuff */
static int emu_init(struct sc_info *);
static void emu_intr(void *);
static void *emu_malloc(struct sc_info *sc, u_int32_t sz, bus_addr_t *addr, bus_dmamap_t *map);
static void *emu_memalloc(struct sc_info *sc, u_int32_t sz, bus_addr_t *addr);
static int emu_memfree(struct sc_info *sc, void *buf);
static int emu_memstart(struct sc_info *sc, void *buf);
#ifdef EMUDEBUG
static void emu_vdump(struct sc_info *sc, struct emu_voice *v);
#endif

/* talk to the card */
static u_int32_t emu_rd(struct sc_info *, int, int);
static void emu_wr(struct sc_info *, int, u_int32_t, int);

/* -------------------------------------------------------------------- */

static u_int32_t emu_rfmt_ac97[] = {
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};

static u_int32_t emu_rfmt_mic[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	0
};

static u_int32_t emu_rfmt_efx[] = {
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};

static struct pcmchan_caps emu_reccaps[3] = {
	{8000, 48000, emu_rfmt_ac97, 0},
	{8000, 8000, emu_rfmt_mic, 0},
	{48000, 48000, emu_rfmt_efx, 0},
};

static u_int32_t emu_pfmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};

static struct pcmchan_caps emu_playcaps = {4000, 48000, emu_pfmt, 0};

static int adcspeed[8] = {48000, 44100, 32000, 24000, 22050, 16000, 11025, 8000};
/* audigy supports 12kHz. */
static int audigy_adcspeed[9] = {
	48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000
};

/* -------------------------------------------------------------------- */
/* Hardware */
static u_int32_t
emu_rd(struct sc_info *sc, int regno, int size)
{
	switch (size) {
	case 1:
		return bus_space_read_1(sc->st, sc->sh, regno);
	case 2:
		return bus_space_read_2(sc->st, sc->sh, regno);
	case 4:
		return bus_space_read_4(sc->st, sc->sh, regno);
	default:
		return 0xffffffff;
	}
}

static void
emu_wr(struct sc_info *sc, int regno, u_int32_t data, int size)
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

static u_int32_t
emu_rdptr(struct sc_info *sc, int chn, int reg)
{
	u_int32_t ptr, val, mask, size, offset;

	ptr = ((reg << 16) & sc->addrmask) | (chn & EMU_PTR_CHNO_MASK);
	emu_wr(sc, EMU_PTR, ptr, 4);
	val = emu_rd(sc, EMU_DATA, 4);
	if (reg & 0xff000000) {
		size = (reg >> 24) & 0x3f;
		offset = (reg >> 16) & 0x1f;
		mask = ((1 << size) - 1) << offset;
		val &= mask;
		val >>= offset;
	}
	return val;
}

static void
emu_wrptr(struct sc_info *sc, int chn, int reg, u_int32_t data)
{
	u_int32_t ptr, mask, size, offset;

	ptr = ((reg << 16) & sc->addrmask) | (chn & EMU_PTR_CHNO_MASK);
	emu_wr(sc, EMU_PTR, ptr, 4);
	if (reg & 0xff000000) {
		size = (reg >> 24) & 0x3f;
		offset = (reg >> 16) & 0x1f;
		mask = ((1 << size) - 1) << offset;
		data <<= offset;
		data &= mask;
		data |= emu_rd(sc, EMU_DATA, 4) & ~mask;
	}
	emu_wr(sc, EMU_DATA, data, 4);
}

static void
emu_wrefx(struct sc_info *sc, unsigned int pc, unsigned int data)
{
	pc += sc->audigy ? EMU_A_MICROCODEBASE : EMU_MICROCODEBASE;
	emu_wrptr(sc, 0, pc, data);
}

/* -------------------------------------------------------------------- */
/* ac97 codec */
/* no locking needed */

static int
emu_rdcd(kobj_t obj, void *devinfo, int regno)
{
	struct sc_info *sc = (struct sc_info *)devinfo;

	emu_wr(sc, EMU_AC97ADDR, regno, 1);
	return emu_rd(sc, EMU_AC97DATA, 2);
}

static int
emu_wrcd(kobj_t obj, void *devinfo, int regno, u_int32_t data)
{
	struct sc_info *sc = (struct sc_info *)devinfo;

	emu_wr(sc, EMU_AC97ADDR, regno, 1);
	emu_wr(sc, EMU_AC97DATA, data, 2);
	return 0;
}

static kobj_method_t emu_ac97_methods[] = {
	KOBJMETHOD(ac97_read,		emu_rdcd),
	KOBJMETHOD(ac97_write,		emu_wrcd),
	KOBJMETHOD_END
};
AC97_DECLARE(emu_ac97);

/* -------------------------------------------------------------------- */
/* stuff */
static int
emu_settimer(struct sc_info *sc)
{
	struct sc_pchinfo *pch;
	struct sc_rchinfo *rch;
	int i, tmp, rate;

	rate = 0;
	for (i = 0; i < sc->nchans; i++) {
		pch = &sc->pch[i];
		if (pch->buffer) {
			tmp = (pch->spd * sndbuf_getalign(pch->buffer))
			    / pch->blksz;
			if (tmp > rate)
				rate = tmp;
		}
	}

	for (i = 0; i < 3; i++) {
		rch = &sc->rch[i];
		if (rch->buffer) {
			tmp = (rch->spd * sndbuf_getalign(rch->buffer))
			    / rch->blksz;
			if (tmp > rate)
				rate = tmp;
		}
	}
	RANGE(rate, 48, 9600);
	sc->timerinterval = 48000 / rate;
	emu_wr(sc, EMU_TIMER, sc->timerinterval & 0x03ff, 2);

	return sc->timerinterval;
}

static int
emu_enatimer(struct sc_info *sc, int go)
{
	u_int32_t x;
	if (go) {
		if (sc->timer++ == 0) {
			x = emu_rd(sc, EMU_INTE, 4);
			x |= EMU_INTE_INTERTIMERENB;
			emu_wr(sc, EMU_INTE, x, 4);
		}
	} else {
		sc->timer = 0;
		x = emu_rd(sc, EMU_INTE, 4);
		x &= ~EMU_INTE_INTERTIMERENB;
		emu_wr(sc, EMU_INTE, x, 4);
	}
	return 0;
}

static void
emu_enastop(struct sc_info *sc, char channel, int enable)
{
	int reg = (channel & 0x20) ? EMU_SOLEH : EMU_SOLEL;
	channel &= 0x1f;
	reg |= 1 << 24;
	reg |= channel << 16;
	emu_wrptr(sc, 0, reg, enable);
}

static int
emu_recval(int speed) {
	int val;

	val = 0;
	while (val < 7 && speed < adcspeed[val])
		val++;
	return val;
}

static int
audigy_recval(int speed) {
	int val;

	val = 0;
	while (val < 8 && speed < audigy_adcspeed[val])
		val++;
	return val;
}

static u_int32_t
emu_rate_to_pitch(u_int32_t rate)
{
	static u_int32_t logMagTable[128] = {
		0x00000, 0x02dfc, 0x05b9e, 0x088e6, 0x0b5d6, 0x0e26f, 0x10eb3, 0x13aa2,
		0x1663f, 0x1918a, 0x1bc84, 0x1e72e, 0x2118b, 0x23b9a, 0x2655d, 0x28ed5,
		0x2b803, 0x2e0e8, 0x30985, 0x331db, 0x359eb, 0x381b6, 0x3a93d, 0x3d081,
		0x3f782, 0x41e42, 0x444c1, 0x46b01, 0x49101, 0x4b6c4, 0x4dc49, 0x50191,
		0x5269e, 0x54b6f, 0x57006, 0x59463, 0x5b888, 0x5dc74, 0x60029, 0x623a7,
		0x646ee, 0x66a00, 0x68cdd, 0x6af86, 0x6d1fa, 0x6f43c, 0x7164b, 0x73829,
		0x759d4, 0x77b4f, 0x79c9a, 0x7bdb5, 0x7dea1, 0x7ff5e, 0x81fed, 0x8404e,
		0x86082, 0x88089, 0x8a064, 0x8c014, 0x8df98, 0x8fef1, 0x91e20, 0x93d26,
		0x95c01, 0x97ab4, 0x9993e, 0x9b79f, 0x9d5d9, 0x9f3ec, 0xa11d8, 0xa2f9d,
		0xa4d3c, 0xa6ab5, 0xa8808, 0xaa537, 0xac241, 0xadf26, 0xafbe7, 0xb1885,
		0xb3500, 0xb5157, 0xb6d8c, 0xb899f, 0xba58f, 0xbc15e, 0xbdd0c, 0xbf899,
		0xc1404, 0xc2f50, 0xc4a7b, 0xc6587, 0xc8073, 0xc9b3f, 0xcb5ed, 0xcd07c,
		0xceaec, 0xd053f, 0xd1f73, 0xd398a, 0xd5384, 0xd6d60, 0xd8720, 0xda0c3,
		0xdba4a, 0xdd3b4, 0xded03, 0xe0636, 0xe1f4e, 0xe384a, 0xe512c, 0xe69f3,
		0xe829f, 0xe9b31, 0xeb3a9, 0xecc08, 0xee44c, 0xefc78, 0xf148a, 0xf2c83,
		0xf4463, 0xf5c2a, 0xf73da, 0xf8b71, 0xfa2f0, 0xfba57, 0xfd1a7, 0xfe8df
	};
	static char logSlopeTable[128] = {
		0x5c, 0x5c, 0x5b, 0x5a, 0x5a, 0x59, 0x58, 0x58,
		0x57, 0x56, 0x56, 0x55, 0x55, 0x54, 0x53, 0x53,
		0x52, 0x52, 0x51, 0x51, 0x50, 0x50, 0x4f, 0x4f,
		0x4e, 0x4d, 0x4d, 0x4d, 0x4c, 0x4c, 0x4b, 0x4b,
		0x4a, 0x4a, 0x49, 0x49, 0x48, 0x48, 0x47, 0x47,
		0x47, 0x46, 0x46, 0x45, 0x45, 0x45, 0x44, 0x44,
		0x43, 0x43, 0x43, 0x42, 0x42, 0x42, 0x41, 0x41,
		0x41, 0x40, 0x40, 0x40, 0x3f, 0x3f, 0x3f, 0x3e,
		0x3e, 0x3e, 0x3d, 0x3d, 0x3d, 0x3c, 0x3c, 0x3c,
		0x3b, 0x3b, 0x3b, 0x3b, 0x3a, 0x3a, 0x3a, 0x39,
		0x39, 0x39, 0x39, 0x38, 0x38, 0x38, 0x38, 0x37,
		0x37, 0x37, 0x37, 0x36, 0x36, 0x36, 0x36, 0x35,
		0x35, 0x35, 0x35, 0x34, 0x34, 0x34, 0x34, 0x34,
		0x33, 0x33, 0x33, 0x33, 0x32, 0x32, 0x32, 0x32,
		0x32, 0x31, 0x31, 0x31, 0x31, 0x31, 0x30, 0x30,
		0x30, 0x30, 0x30, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f
	};
	int i;

	if (rate == 0)
		return 0;	/* Bail out if no leading "1" */
	rate *= 11185;	/* Scale 48000 to 0x20002380 */
	for (i = 31; i > 0; i--) {
		if (rate & 0x80000000) {	/* Detect leading "1" */
			return (((u_int32_t) (i - 15) << 20) +
			    logMagTable[0x7f & (rate >> 24)] +
			    (0x7f & (rate >> 17)) *
			    logSlopeTable[0x7f & (rate >> 24)]);
		}
		rate <<= 1;
	}

	return 0;		/* Should never reach this point */
}

static u_int32_t
emu_rate_to_linearpitch(u_int32_t rate)
{
	rate = (rate << 8) / 375;
	return (rate >> 1) + (rate & 1);
}

static struct emu_voice *
emu_valloc(struct sc_info *sc)
{
	struct emu_voice *v;
	int i;

	v = NULL;
	for (i = 0; i < 64 && sc->voice[i].busy; i++);
	if (i < 64) {
		v = &sc->voice[i];
		v->busy = 1;
	}
	return v;
}

static int
emu_vinit(struct sc_info *sc, struct emu_voice *m, struct emu_voice *s,
	  u_int32_t sz, struct snd_dbuf *b)
{
	void *buf;
	bus_addr_t tmp_addr;

	buf = emu_memalloc(sc, sz, &tmp_addr);
	if (buf == NULL)
		return -1;
	if (b != NULL)
		sndbuf_setup(b, buf, sz);
	m->start = emu_memstart(sc, buf) * EMUPAGESIZE;
	m->end = m->start + sz;
	m->channel = NULL;
	m->speed = 0;
	m->b16 = 0;
	m->stereo = 0;
	m->running = 0;
	m->ismaster = 1;
	m->vol = 0xff;
	m->buf = tmp_addr;
	m->slave = s;
	if (sc->audigy) {
		m->fxrt1 = FXBUS_MIDI_CHORUS | FXBUS_PCM_RIGHT << 8 |
		    FXBUS_PCM_LEFT << 16 | FXBUS_MIDI_REVERB << 24;
		m->fxrt2 = 0x3f3f3f3f;	/* No effects on second route */
	} else {
		m->fxrt1 = FXBUS_MIDI_CHORUS | FXBUS_PCM_RIGHT << 4 |
		    FXBUS_PCM_LEFT << 8 | FXBUS_MIDI_REVERB << 12;
		m->fxrt2 = 0;
	}

	if (s != NULL) {
		s->start = m->start;
		s->end = m->end;
		s->channel = NULL;
		s->speed = 0;
		s->b16 = 0;
		s->stereo = 0;
		s->running = 0;
		s->ismaster = 0;
		s->vol = m->vol;
		s->buf = m->buf;
		s->fxrt1 = m->fxrt1;
		s->fxrt2 = m->fxrt2;
		s->slave = NULL;
	}
	return 0;
}

static void
emu_vsetup(struct sc_pchinfo *ch)
{
	struct emu_voice *v = ch->master;

	if (ch->fmt) {
		v->b16 = (ch->fmt & AFMT_16BIT) ? 1 : 0;
		v->stereo = (AFMT_CHANNEL(ch->fmt) > 1) ? 1 : 0;
		if (v->slave != NULL) {
			v->slave->b16 = v->b16;
			v->slave->stereo = v->stereo;
		}
	}
	if (ch->spd) {
		v->speed = ch->spd;
		if (v->slave != NULL)
			v->slave->speed = v->speed;
	}
}

static void
emu_vwrite(struct sc_info *sc, struct emu_voice *v)
{
	int s;
	int l, r, x, y;
	u_int32_t sa, ea, start, val, silent_page;

	s = (v->stereo ? 1 : 0) + (v->b16 ? 1 : 0);

	sa = v->start >> s;
	ea = v->end >> s;

	l = r = x = y = v->vol;
	if (v->stereo) {
		l = v->ismaster ? l : 0;
		r = v->ismaster ? 0 : r;
	}

	emu_wrptr(sc, v->vnum, EMU_CHAN_CPF, v->stereo ? EMU_CHAN_CPF_STEREO_MASK : 0);
	val = v->stereo ? 28 : 30;
	val *= v->b16 ? 1 : 2;
	start = sa + val;

	if (sc->audigy) {
		emu_wrptr(sc, v->vnum, EMU_A_CHAN_FXRT1, v->fxrt1);
		emu_wrptr(sc, v->vnum, EMU_A_CHAN_FXRT2, v->fxrt2);
		emu_wrptr(sc, v->vnum, EMU_A_CHAN_SENDAMOUNTS, 0);
	}
	else
		emu_wrptr(sc, v->vnum, EMU_CHAN_FXRT, v->fxrt1 << 16);

	emu_wrptr(sc, v->vnum, EMU_CHAN_PTRX, (x << 8) | r);
	emu_wrptr(sc, v->vnum, EMU_CHAN_DSL, ea | (y << 24));
	emu_wrptr(sc, v->vnum, EMU_CHAN_PSST, sa | (l << 24));
	emu_wrptr(sc, v->vnum, EMU_CHAN_CCCA, start | (v->b16 ? 0 : EMU_CHAN_CCCA_8BITSELECT));

	emu_wrptr(sc, v->vnum, EMU_CHAN_Z1, 0);
	emu_wrptr(sc, v->vnum, EMU_CHAN_Z2, 0);

	silent_page = ((u_int32_t)(sc->mem.silent_page_addr) << 1)
	    | EMU_CHAN_MAP_PTI_MASK;
	emu_wrptr(sc, v->vnum, EMU_CHAN_MAPA, silent_page);
	emu_wrptr(sc, v->vnum, EMU_CHAN_MAPB, silent_page);

	emu_wrptr(sc, v->vnum, EMU_CHAN_CVCF, EMU_CHAN_CVCF_CURRFILTER_MASK);
	emu_wrptr(sc, v->vnum, EMU_CHAN_VTFT, EMU_CHAN_VTFT_FILTERTARGET_MASK);
	emu_wrptr(sc, v->vnum, EMU_CHAN_ATKHLDM, 0);
	emu_wrptr(sc, v->vnum, EMU_CHAN_DCYSUSM, EMU_CHAN_DCYSUSM_DECAYTIME_MASK);
	emu_wrptr(sc, v->vnum, EMU_CHAN_LFOVAL1, 0x8000);
	emu_wrptr(sc, v->vnum, EMU_CHAN_LFOVAL2, 0x8000);
	emu_wrptr(sc, v->vnum, EMU_CHAN_FMMOD, 0);
	emu_wrptr(sc, v->vnum, EMU_CHAN_TREMFRQ, 0);
	emu_wrptr(sc, v->vnum, EMU_CHAN_FM2FRQ2, 0);
	emu_wrptr(sc, v->vnum, EMU_CHAN_ENVVAL, 0x8000);

	emu_wrptr(sc, v->vnum, EMU_CHAN_ATKHLDV,
	    EMU_CHAN_ATKHLDV_HOLDTIME_MASK | EMU_CHAN_ATKHLDV_ATTACKTIME_MASK);
	emu_wrptr(sc, v->vnum, EMU_CHAN_ENVVOL, 0x8000);

	emu_wrptr(sc, v->vnum, EMU_CHAN_PEFE_FILTERAMOUNT, 0x7f);
	emu_wrptr(sc, v->vnum, EMU_CHAN_PEFE_PITCHAMOUNT, 0);

	if (v->slave != NULL)
		emu_vwrite(sc, v->slave);
}

static void
emu_vtrigger(struct sc_info *sc, struct emu_voice *v, int go)
{
	u_int32_t pitch_target, initial_pitch;
	u_int32_t cra, cs, ccis;
	u_int32_t sample, i;

	if (go) {
		cra = 64;
		cs = v->stereo ? 4 : 2;
		ccis = v->stereo ? 28 : 30;
		ccis *= v->b16 ? 1 : 2;
		sample = v->b16 ? 0x00000000 : 0x80808080;

		for (i = 0; i < cs; i++)
			emu_wrptr(sc, v->vnum, EMU_CHAN_CD0 + i, sample);
		emu_wrptr(sc, v->vnum, EMU_CHAN_CCR_CACHEINVALIDSIZE, 0);
		emu_wrptr(sc, v->vnum, EMU_CHAN_CCR_READADDRESS, cra);
		emu_wrptr(sc, v->vnum, EMU_CHAN_CCR_CACHEINVALIDSIZE, ccis);

		emu_wrptr(sc, v->vnum, EMU_CHAN_IFATN, 0xff00);
		emu_wrptr(sc, v->vnum, EMU_CHAN_VTFT, 0xffffffff);
		emu_wrptr(sc, v->vnum, EMU_CHAN_CVCF, 0xffffffff);
		emu_wrptr(sc, v->vnum, EMU_CHAN_DCYSUSV, 0x00007f7f);
		emu_enastop(sc, v->vnum, 0);

		pitch_target = emu_rate_to_linearpitch(v->speed);
		initial_pitch = emu_rate_to_pitch(v->speed) >> 8;
		emu_wrptr(sc, v->vnum, EMU_CHAN_PTRX_PITCHTARGET, pitch_target);
		emu_wrptr(sc, v->vnum, EMU_CHAN_CPF_PITCH, pitch_target);
		emu_wrptr(sc, v->vnum, EMU_CHAN_IP, initial_pitch);
	} else {
		emu_wrptr(sc, v->vnum, EMU_CHAN_PTRX_PITCHTARGET, 0);
		emu_wrptr(sc, v->vnum, EMU_CHAN_CPF_PITCH, 0);
		emu_wrptr(sc, v->vnum, EMU_CHAN_IFATN, 0xffff);
		emu_wrptr(sc, v->vnum, EMU_CHAN_VTFT, 0x0000ffff);
		emu_wrptr(sc, v->vnum, EMU_CHAN_CVCF, 0x0000ffff);
		emu_wrptr(sc, v->vnum, EMU_CHAN_IP, 0);
		emu_enastop(sc, v->vnum, 1);
	}
	if (v->slave != NULL)
		emu_vtrigger(sc, v->slave, go);
}

static int
emu_vpos(struct sc_info *sc, struct emu_voice *v)
{
	int s, ptr;

	s = (v->b16 ? 1 : 0) + (v->stereo ? 1 : 0);
	ptr = (emu_rdptr(sc, v->vnum, EMU_CHAN_CCCA_CURRADDR) - (v->start >> s)) << s;
	return ptr & ~0x0000001f;
}

#ifdef EMUDEBUG
static void
emu_vdump(struct sc_info *sc, struct emu_voice *v)
{
	char *regname[] = {
		"cpf", "ptrx", "cvcf", "vtft", "z2", "z1", "psst", "dsl",
		"ccca", "ccr", "clp", "fxrt", "mapa", "mapb", NULL, NULL,
		"envvol", "atkhldv", "dcysusv", "lfoval1",
		"envval", "atkhldm", "dcysusm", "lfoval2",
		"ip", "ifatn", "pefe", "fmmod", "tremfrq", "fmfrq2",
		"tempenv"
	};
	char *regname2[] = {
		"mudata1", "mustat1", "mudata2", "mustat2",
		"fxwc1", "fxwc2", "spdrate", NULL, NULL,
		NULL, NULL, NULL, "fxrt2", "sndamnt", "fxrt1",
		NULL, NULL
	};
	int i, x;

	printf("voice number %d\n", v->vnum);
	for (i = 0, x = 0; i <= 0x1e; i++) {
		if (regname[i] == NULL)
			continue;
		printf("%s\t[%08x]", regname[i], emu_rdptr(sc, v->vnum, i));
		printf("%s", (x == 2) ? "\n" : "\t");
		x++;
		if (x > 2)
			x = 0;
	}

	/* Print out audigy extra registers */
	if (sc->audigy) {
		for (i = 0; i <= 0xe; i++) {
			if (regname2[i] == NULL)
				continue;
			printf("%s\t[%08x]", regname2[i],
			    emu_rdptr(sc, v->vnum, i + 0x70));
			printf("%s", (x == 2)? "\n" : "\t");
			x++;
			if (x > 2)
				x = 0;
		}
	}
	printf("\n\n");
}
#endif

/* channel interface */
static void *
emupchan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
    struct pcm_channel *c, int dir)
{
	struct sc_info *sc = devinfo;
	struct sc_pchinfo *ch;
	void *r;

	KASSERT(dir == PCMDIR_PLAY, ("emupchan_init: bad direction"));
	ch = &sc->pch[sc->pnum++];
	ch->buffer = b;
	ch->parent = sc;
	ch->channel = c;
	ch->blksz = sc->bufsz / 2;
	ch->fmt = SND_FORMAT(AFMT_U8, 1, 0);
	ch->spd = 8000;
	snd_mtxlock(sc->lock);
	ch->master = emu_valloc(sc);
	ch->slave = emu_valloc(sc);
	snd_mtxunlock(sc->lock);
	r = (emu_vinit(sc, ch->master, ch->slave, sc->bufsz, ch->buffer))
	    ? NULL : ch;

	return r;
}

static int
emupchan_free(kobj_t obj, void *data)
{
	struct sc_pchinfo *ch = data;
	struct sc_info *sc = ch->parent;
	int r;

	snd_mtxlock(sc->lock);
	r = emu_memfree(sc, sndbuf_getbuf(ch->buffer));
	snd_mtxunlock(sc->lock);

	return r;
}

static int
emupchan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct sc_pchinfo *ch = data;

	ch->fmt = format;
	return 0;
}

static u_int32_t
emupchan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct sc_pchinfo *ch = data;

	ch->spd = speed;
	return ch->spd;
}

static u_int32_t
emupchan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct sc_pchinfo *ch = data;
	struct sc_info *sc = ch->parent;
	int irqrate, blksz;

	ch->blksz = blocksize;
	snd_mtxlock(sc->lock);
	emu_settimer(sc);
	irqrate = 48000 / sc->timerinterval;
	snd_mtxunlock(sc->lock);
	blksz = (ch->spd * sndbuf_getalign(ch->buffer)) / irqrate;
	return blocksize;
}

static int
emupchan_trigger(kobj_t obj, void *data, int go)
{
	struct sc_pchinfo *ch = data;
	struct sc_info *sc = ch->parent;

	if (!PCMTRIG_COMMON(go))
		return 0;

	snd_mtxlock(sc->lock);
	if (go == PCMTRIG_START) {
		emu_vsetup(ch);
		emu_vwrite(sc, ch->master);
		emu_settimer(sc);
		emu_enatimer(sc, 1);
#ifdef EMUDEBUG
		printf("start [%d bit, %s, %d hz]\n",
			ch->master->b16 ? 16 : 8,
			ch->master->stereo ? "stereo" : "mono",
			ch->master->speed);
		emu_vdump(sc, ch->master);
		emu_vdump(sc, ch->slave);
#endif
	}
	ch->run = (go == PCMTRIG_START) ? 1 : 0;
	emu_vtrigger(sc, ch->master, ch->run);
	snd_mtxunlock(sc->lock);
	return 0;
}

static u_int32_t
emupchan_getptr(kobj_t obj, void *data)
{
	struct sc_pchinfo *ch = data;
	struct sc_info *sc = ch->parent;
	int r;

	snd_mtxlock(sc->lock);
	r = emu_vpos(sc, ch->master);
	snd_mtxunlock(sc->lock);

	return r;
}

static struct pcmchan_caps *
emupchan_getcaps(kobj_t obj, void *data)
{
	return &emu_playcaps;
}

static kobj_method_t emupchan_methods[] = {
	KOBJMETHOD(channel_init,		emupchan_init),
	KOBJMETHOD(channel_free,		emupchan_free),
	KOBJMETHOD(channel_setformat,		emupchan_setformat),
	KOBJMETHOD(channel_setspeed,		emupchan_setspeed),
	KOBJMETHOD(channel_setblocksize,	emupchan_setblocksize),
	KOBJMETHOD(channel_trigger,		emupchan_trigger),
	KOBJMETHOD(channel_getptr,		emupchan_getptr),
	KOBJMETHOD(channel_getcaps,		emupchan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(emupchan);

/* channel interface */
static void *
emurchan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
    struct pcm_channel *c, int dir)
{
	struct sc_info *sc = devinfo;
	struct sc_rchinfo *ch;

	KASSERT(dir == PCMDIR_REC, ("emurchan_init: bad direction"));
	ch = &sc->rch[sc->rnum];
	ch->buffer = b;
	ch->parent = sc;
	ch->channel = c;
	ch->blksz = sc->bufsz / 2;
	ch->fmt = SND_FORMAT(AFMT_U8, 1, 0);
	ch->spd = 8000;
	ch->num = sc->rnum;
	switch(sc->rnum) {
	case 0:
		ch->idxreg = sc->audigy ? EMU_A_ADCIDX : EMU_ADCIDX;
		ch->basereg = EMU_ADCBA;
		ch->sizereg = EMU_ADCBS;
		ch->setupreg = EMU_ADCCR;
		ch->irqmask = EMU_INTE_ADCBUFENABLE;
		break;

	case 1:
		ch->idxreg = EMU_FXIDX;
		ch->basereg = EMU_FXBA;
		ch->sizereg = EMU_FXBS;
		ch->setupreg = EMU_FXWC;
		ch->irqmask = EMU_INTE_EFXBUFENABLE;
		break;

	case 2:
		ch->idxreg = EMU_MICIDX;
		ch->basereg = EMU_MICBA;
		ch->sizereg = EMU_MICBS;
		ch->setupreg = 0;
		ch->irqmask = EMU_INTE_MICBUFENABLE;
		break;
	}
	sc->rnum++;
	if (sndbuf_alloc(ch->buffer, sc->parent_dmat, 0, sc->bufsz) != 0)
		return NULL;
	else {
		snd_mtxlock(sc->lock);
		emu_wrptr(sc, 0, ch->basereg, sndbuf_getbufaddr(ch->buffer));
		emu_wrptr(sc, 0, ch->sizereg, 0); /* off */
		snd_mtxunlock(sc->lock);
		return ch;
	}
}

static int
emurchan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct sc_rchinfo *ch = data;

	ch->fmt = format;
	return 0;
}

static u_int32_t
emurchan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct sc_rchinfo *ch = data;

	if (ch->num == 0) {
		if (ch->parent->audigy)
			speed = audigy_adcspeed[audigy_recval(speed)];
		else
			speed = adcspeed[emu_recval(speed)];
	}
	if (ch->num == 1)
		speed = 48000;
	if (ch->num == 2)
		speed = 8000;
	ch->spd = speed;
	return ch->spd;
}

static u_int32_t
emurchan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct sc_rchinfo *ch = data;
	struct sc_info *sc = ch->parent;
	int irqrate, blksz;

	ch->blksz = blocksize;
	snd_mtxlock(sc->lock);
	emu_settimer(sc);
	irqrate = 48000 / sc->timerinterval;
	snd_mtxunlock(sc->lock);
	blksz = (ch->spd * sndbuf_getalign(ch->buffer)) / irqrate;
	return blocksize;
}

/* semantic note: must start at beginning of buffer */
static int
emurchan_trigger(kobj_t obj, void *data, int go)
{
	struct sc_rchinfo *ch = data;
	struct sc_info *sc = ch->parent;
	u_int32_t val, sz;

	if (!PCMTRIG_COMMON(go))
		return 0;

	switch(sc->bufsz) {
	case 4096:
		sz = EMU_RECBS_BUFSIZE_4096;
		break;

	case 8192:
		sz = EMU_RECBS_BUFSIZE_8192;
		break;

	case 16384:
		sz = EMU_RECBS_BUFSIZE_16384;
		break;

	case 32768:
		sz = EMU_RECBS_BUFSIZE_32768;
		break;

	case 65536:
		sz = EMU_RECBS_BUFSIZE_65536;
		break;

	default:
		sz = EMU_RECBS_BUFSIZE_4096;
	}

	snd_mtxlock(sc->lock);
	switch(go) {
	case PCMTRIG_START:
		ch->run = 1;
		emu_wrptr(sc, 0, ch->sizereg, sz);
		if (ch->num == 0) {
			if (sc->audigy) {
				val = EMU_A_ADCCR_LCHANENABLE;
				if (AFMT_CHANNEL(ch->fmt) > 1)
					val |= EMU_A_ADCCR_RCHANENABLE;
				val |= audigy_recval(ch->spd);
			} else {
				val = EMU_ADCCR_LCHANENABLE;
				if (AFMT_CHANNEL(ch->fmt) > 1)
					val |= EMU_ADCCR_RCHANENABLE;
				val |= emu_recval(ch->spd);
			}

			emu_wrptr(sc, 0, ch->setupreg, 0);
			emu_wrptr(sc, 0, ch->setupreg, val);
		}
		val = emu_rd(sc, EMU_INTE, 4);
		val |= ch->irqmask;
		emu_wr(sc, EMU_INTE, val, 4);
		break;

	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		ch->run = 0;
		emu_wrptr(sc, 0, ch->sizereg, 0);
		if (ch->setupreg)
			emu_wrptr(sc, 0, ch->setupreg, 0);
		val = emu_rd(sc, EMU_INTE, 4);
		val &= ~ch->irqmask;
		emu_wr(sc, EMU_INTE, val, 4);
		break;

	case PCMTRIG_EMLDMAWR:
	case PCMTRIG_EMLDMARD:
	default:
		break;
	}
	snd_mtxunlock(sc->lock);

	return 0;
}

static u_int32_t
emurchan_getptr(kobj_t obj, void *data)
{
	struct sc_rchinfo *ch = data;
	struct sc_info *sc = ch->parent;
	int r;

	snd_mtxlock(sc->lock);
	r = emu_rdptr(sc, 0, ch->idxreg) & 0x0000ffff;
	snd_mtxunlock(sc->lock);

	return r;
}

static struct pcmchan_caps *
emurchan_getcaps(kobj_t obj, void *data)
{
	struct sc_rchinfo *ch = data;

	return &emu_reccaps[ch->num];
}

static kobj_method_t emurchan_methods[] = {
	KOBJMETHOD(channel_init,		emurchan_init),
	KOBJMETHOD(channel_setformat,		emurchan_setformat),
	KOBJMETHOD(channel_setspeed,		emurchan_setspeed),
	KOBJMETHOD(channel_setblocksize,	emurchan_setblocksize),
	KOBJMETHOD(channel_trigger,		emurchan_trigger),
	KOBJMETHOD(channel_getptr,		emurchan_getptr),
	KOBJMETHOD(channel_getcaps,		emurchan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(emurchan);

static unsigned char
emu_mread(struct mpu401 *arg, void *sc, int reg)
{	
	unsigned int d;

	d = emu_rd((struct sc_info *)sc, 0x18 + reg, 1); 
	return d;
}

static void
emu_mwrite(struct mpu401 *arg, void *sc, int reg, unsigned char b)
{

	emu_wr((struct sc_info *)sc, 0x18 + reg, b, 1);
}

static int
emu_muninit(struct mpu401 *arg, void *cookie)
{
	struct sc_info *sc = cookie;

	snd_mtxlock(sc->lock);
	sc->mpu_intr = NULL;
	snd_mtxunlock(sc->lock);

	return 0;
}

static kobj_method_t emu_mpu_methods[] = {
    	KOBJMETHOD(mpufoi_read,		emu_mread),
    	KOBJMETHOD(mpufoi_write,	emu_mwrite),
    	KOBJMETHOD(mpufoi_uninit,	emu_muninit),
	KOBJMETHOD_END
};

static DEFINE_CLASS(emu_mpu, emu_mpu_methods, 0);

static void
emu_intr2(void *p)
{
	struct sc_info *sc = (struct sc_info *)p;

	if (sc->mpu_intr)
	    (sc->mpu_intr)(sc->mpu);
}

static void
emu_midiattach(struct sc_info *sc)
{
	int i;

	i = emu_rd(sc, EMU_INTE, 4);
	i |= EMU_INTE_MIDIRXENABLE;
	emu_wr(sc, EMU_INTE, i, 4);

	sc->mpu = mpu401_init(&emu_mpu_class, sc, emu_intr2, &sc->mpu_intr);
}
/* -------------------------------------------------------------------- */
/* The interrupt handler */

static void
emu_intr(void *data)
{
	struct sc_info *sc = data;
	u_int32_t stat, ack, i, x;

	snd_mtxlock(sc->lock);
	while (1) {
		stat = emu_rd(sc, EMU_IPR, 4);
		if (stat == 0)
			break;
		ack = 0;

		/* process irq */
		if (stat & EMU_IPR_INTERVALTIMER)
			ack |= EMU_IPR_INTERVALTIMER;

		if (stat & (EMU_IPR_ADCBUFFULL | EMU_IPR_ADCBUFHALFFULL))
			ack |= stat & (EMU_IPR_ADCBUFFULL | EMU_IPR_ADCBUFHALFFULL);

		if (stat & (EMU_IPR_EFXBUFFULL | EMU_IPR_EFXBUFHALFFULL))
			ack |= stat & (EMU_IPR_EFXBUFFULL | EMU_IPR_EFXBUFHALFFULL);

		if (stat & (EMU_IPR_MICBUFFULL | EMU_IPR_MICBUFHALFFULL))
			ack |= stat & (EMU_IPR_MICBUFFULL | EMU_IPR_MICBUFHALFFULL);

		if (stat & EMU_PCIERROR) {
			ack |= EMU_PCIERROR;
			device_printf(sc->dev, "pci error\n");
			/* we still get an nmi with ecc ram even if we ack this */
		}
		if (stat & EMU_IPR_RATETRCHANGE) {
			ack |= EMU_IPR_RATETRCHANGE;
#ifdef EMUDEBUG
			device_printf(sc->dev,
			    "sample rate tracker lock status change\n");
#endif
		}

	    if (stat & EMU_IPR_MIDIRECVBUFE)
		if (sc->mpu_intr) {
		    (sc->mpu_intr)(sc->mpu);
		    ack |= EMU_IPR_MIDIRECVBUFE | EMU_IPR_MIDITRANSBUFE;
 		}
		if (stat & ~ack)
			device_printf(sc->dev, "dodgy irq: %x (harmless)\n",
			    stat & ~ack);

		emu_wr(sc, EMU_IPR, stat, 4);

		if (ack) {
			snd_mtxunlock(sc->lock);

			if (ack & EMU_IPR_INTERVALTIMER) {
				x = 0;
				for (i = 0; i < sc->nchans; i++) {
					if (sc->pch[i].run) {
						x = 1;
						chn_intr(sc->pch[i].channel);
					}
				}
				if (x == 0)
					emu_enatimer(sc, 0);
			}


			if (ack & (EMU_IPR_ADCBUFFULL | EMU_IPR_ADCBUFHALFFULL)) {
				if (sc->rch[0].channel)
					chn_intr(sc->rch[0].channel);
			}
			if (ack & (EMU_IPR_EFXBUFFULL | EMU_IPR_EFXBUFHALFFULL)) {
				if (sc->rch[1].channel)
					chn_intr(sc->rch[1].channel);
			}
			if (ack & (EMU_IPR_MICBUFFULL | EMU_IPR_MICBUFHALFFULL)) {
				if (sc->rch[2].channel)
					chn_intr(sc->rch[2].channel);
			}

			snd_mtxlock(sc->lock);
		}
	}
	snd_mtxunlock(sc->lock);
}

/* -------------------------------------------------------------------- */

static void
emu_setmap(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *phys = arg;

	*phys = error ? 0 : (bus_addr_t)segs->ds_addr;

	if (bootverbose) {
		printf("emu: setmap (%lx, %lx), nseg=%d, error=%d\n",
		    (unsigned long)segs->ds_addr, (unsigned long)segs->ds_len,
		    nseg, error);
	}
}

static void *
emu_malloc(struct sc_info *sc, u_int32_t sz, bus_addr_t *addr,
    bus_dmamap_t *map)
{
	void *buf;

	*addr = 0;
	if (bus_dmamem_alloc(sc->parent_dmat, &buf, BUS_DMA_NOWAIT, map))
		return NULL;
	if (bus_dmamap_load(sc->parent_dmat, *map, buf, sz, emu_setmap, addr, 0)
	    || !*addr) {
		bus_dmamem_free(sc->parent_dmat, buf, *map);
		return NULL;
	}
	return buf;
}

static void
emu_free(struct sc_info *sc, void *buf, bus_dmamap_t map)
{
	bus_dmamap_unload(sc->parent_dmat, map);
	bus_dmamem_free(sc->parent_dmat, buf, map);
}

static void *
emu_memalloc(struct sc_info *sc, u_int32_t sz, bus_addr_t *addr)
{
	u_int32_t blksz, start, idx, ofs, tmp, found;
	struct emu_mem *mem = &sc->mem;
	struct emu_memblk *blk;
	void *buf;

	blksz = sz / EMUPAGESIZE;
	if (sz > (blksz * EMUPAGESIZE))
		blksz++;
	/* find a free block in the bitmap */
	found = 0;
	start = 1;
	while (!found && start + blksz < EMUMAXPAGES) {
		found = 1;
		for (idx = start; idx < start + blksz; idx++)
			if (mem->bmap[idx >> 3] & (1 << (idx & 7)))
				found = 0;
		if (!found)
			start++;
	}
	if (!found)
		return NULL;
	blk = malloc(sizeof(*blk), M_DEVBUF, M_NOWAIT);
	if (blk == NULL)
		return NULL;
	buf = emu_malloc(sc, sz, &blk->buf_addr, &blk->buf_map);
	*addr = blk->buf_addr;
	if (buf == NULL) {
		free(blk, M_DEVBUF);
		return NULL;
	}
	blk->buf = buf;
	blk->pte_start = start;
	blk->pte_size = blksz;
#ifdef EMUDEBUG
	printf("buf %p, pte_start %d, pte_size %d\n", blk->buf,
	    blk->pte_start, blk->pte_size);
#endif
	ofs = 0;
	for (idx = start; idx < start + blksz; idx++) {
		mem->bmap[idx >> 3] |= 1 << (idx & 7);
		tmp = (uint32_t)(blk->buf_addr + ofs);
#ifdef EMUDEBUG
		printf("pte[%d] -> %x phys, %x virt\n", idx, tmp,
		    ((u_int32_t)buf) + ofs);
#endif
		mem->ptb_pages[idx] = (tmp << 1) | idx;
		ofs += EMUPAGESIZE;
	}
	SLIST_INSERT_HEAD(&mem->blocks, blk, link);
	return buf;
}

static int
emu_memfree(struct sc_info *sc, void *buf)
{
	u_int32_t idx, tmp;
	struct emu_mem *mem = &sc->mem;
	struct emu_memblk *blk, *i;

	blk = NULL;
	SLIST_FOREACH(i, &mem->blocks, link) {
		if (i->buf == buf)
			blk = i;
	}
	if (blk == NULL)
		return EINVAL;
	SLIST_REMOVE(&mem->blocks, blk, emu_memblk, link);
	emu_free(sc, buf, blk->buf_map);
	tmp = (u_int32_t)(sc->mem.silent_page_addr) << 1;
	for (idx = blk->pte_start; idx < blk->pte_start + blk->pte_size; idx++) {
		mem->bmap[idx >> 3] &= ~(1 << (idx & 7));
		mem->ptb_pages[idx] = tmp | idx;
	}
	free(blk, M_DEVBUF);
	return 0;
}

static int
emu_memstart(struct sc_info *sc, void *buf)
{
	struct emu_mem *mem = &sc->mem;
	struct emu_memblk *blk, *i;

	blk = NULL;
	SLIST_FOREACH(i, &mem->blocks, link) {
		if (i->buf == buf)
			blk = i;
	}
	if (blk == NULL)
		return -EINVAL;
	return blk->pte_start;
}

static void
emu_addefxop(struct sc_info *sc, int op, int z, int w, int x, int y,
    u_int32_t *pc)
{
	emu_wrefx(sc, (*pc) * 2, (x << 10) | y);
	emu_wrefx(sc, (*pc) * 2 + 1, (op << 20) | (z << 10) | w);
	(*pc)++;
}

static void
audigy_addefxop(struct sc_info *sc, int op, int z, int w, int x, int y,
    u_int32_t *pc)
{
	emu_wrefx(sc, (*pc) * 2, (x << 12) | y);
	emu_wrefx(sc, (*pc) * 2 + 1, (op << 24) | (z << 12) | w);
	(*pc)++;
}

static void
audigy_initefx(struct sc_info *sc)
{
	int i;
	u_int32_t pc = 0;

	/* skip 0, 0, -1, 0 - NOPs */
	for (i = 0; i < 512; i++)
		audigy_addefxop(sc, 0x0f, 0x0c0, 0x0c0, 0x0cf, 0x0c0, &pc);

	for (i = 0; i < 512; i++)
		emu_wrptr(sc, 0, EMU_A_FXGPREGBASE + i, 0x0);

	pc = 16;

	/* stop fx processor */
	emu_wrptr(sc, 0, EMU_A_DBG, EMU_A_DBG_SINGLE_STEP);

	/* Audigy 2 (EMU10K2) DSP Registers:
	   FX Bus
		0x000-0x00f : 16 registers (?)
	   Input
		0x040/0x041 : AC97 Codec (l/r)
		0x042/0x043 : ADC, S/PDIF (l/r)
		0x044/0x045 : Optical S/PDIF in (l/r)
		0x046/0x047 : ?
		0x048/0x049 : Line/Mic 2 (l/r)
		0x04a/0x04b : RCA S/PDIF (l/r)
		0x04c/0x04d : Aux 2 (l/r)
	   Output
		0x060/0x061 : Digital Front (l/r)
		0x062/0x063 : Digital Center/LFE
		0x064/0x065 : AudigyDrive Heaphone (l/r)
		0x066/0x067 : Digital Rear (l/r)
		0x068/0x069 : Analog Front (l/r)
		0x06a/0x06b : Analog Center/LFE
		0x06c/0x06d : ?
		0x06e/0x06f : Analog Rear (l/r)
		0x070/0x071 : AC97 Output (l/r)
		0x072/0x073 : ?
		0x074/0x075 : ?
		0x076/0x077 : ADC Recording Buffer (l/r)
	   Constants
		0x0c0 - 0x0c4 = 0 - 4
		0x0c5 = 0x8, 0x0c6 = 0x10, 0x0c7 = 0x20
		0x0c8 = 0x100, 0x0c9 = 0x10000, 0x0ca = 0x80000
		0x0cb = 0x10000000, 0x0cc = 0x20000000, 0x0cd = 0x40000000
		0x0ce = 0x80000000, 0x0cf = 0x7fffffff, 0x0d0 = 0xffffffff
		0x0d1 = 0xfffffffe, 0x0d2 = 0xc0000000, 0x0d3 = 0x41fbbcdc
		0x0d4 = 0x5a7ef9db, 0x0d5 = 0x00100000, 0x0dc = 0x00000001 (?)
	   Temporary Values
		0x0d6 : Accumulator (?)
		0x0d7 : Condition Register
		0x0d8 : Noise source
		0x0d9 : Noise source
	   Tank Memory Data Registers
		0x200 - 0x2ff
	   Tank Memory Address Registers
		0x300 - 0x3ff
	   General Purpose Registers
		0x400 - 0x5ff
	 */

	/* AC97Output[l/r] = FXBus PCM[l/r] */
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_AC97_L), A_C_00000000,
			A_C_00000000, A_FXBUS(FXBUS_PCM_LEFT), &pc);
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_AC97_R), A_C_00000000,
			A_C_00000000, A_FXBUS(FXBUS_PCM_RIGHT), &pc);

	/* GPR[0/1] = RCA S/PDIF[l/r] -- Master volume */
	audigy_addefxop(sc, iACC3, A_GPR(0), A_C_00000000,
			A_C_00000000, A_EXTIN(EXTIN_COAX_SPDIF_L), &pc);
	audigy_addefxop(sc, iACC3, A_GPR(1), A_C_00000000,
			A_C_00000000, A_EXTIN(EXTIN_COAX_SPDIF_R), &pc);

	/* GPR[2] = GPR[0] (Left) / 2 + GPR[1] (Right) / 2 -- Central volume */
	audigy_addefxop(sc, iINTERP, A_GPR(2), A_GPR(1),
			A_C_40000000, A_GPR(0), &pc);

	/* Headphones[l/r] = GPR[0/1] */
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_HEADPHONE_L),
			A_C_00000000, A_C_00000000, A_GPR(0), &pc);
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_HEADPHONE_R),
			A_C_00000000, A_C_00000000, A_GPR(1), &pc);

	/* Analog Front[l/r] = GPR[0/1] */
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_AFRONT_L), A_C_00000000,
			A_C_00000000, A_GPR(0), &pc);
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_AFRONT_R), A_C_00000000,
			A_C_00000000, A_GPR(1), &pc);

	/* Digital Front[l/r] = GPR[0/1] */
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_FRONT_L), A_C_00000000,
			A_C_00000000, A_GPR(0), &pc);
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_FRONT_R), A_C_00000000,
			A_C_00000000, A_GPR(1), &pc);

	/* Center and Subwoofer configuration */
	/* Analog Center = GPR[0] + GPR[2] */
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_ACENTER), A_C_00000000,
			A_GPR(0), A_GPR(2), &pc);
	/* Analog Sub = GPR[1] + GPR[2] */
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_ALFE), A_C_00000000,
			A_GPR(1), A_GPR(2), &pc);

	/* Digital Center = GPR[0] + GPR[2] */
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_CENTER), A_C_00000000,
			A_GPR(0), A_GPR(2), &pc);
	/* Digital Sub = GPR[1] + GPR[2] */
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_LFE), A_C_00000000,
			A_GPR(1), A_GPR(2), &pc);

#if 0
	/* Analog Rear[l/r] = (GPR[0/1] * RearVolume[l/r]) >> 31 */
	/*   RearVolume = GPR[0x10/0x11] (Will this ever be implemented?) */
	audigy_addefxop(sc, iMAC0, A_EXTOUT(A_EXTOUT_AREAR_L), A_C_00000000,
			A_GPR(16), A_GPR(0), &pc);
	audigy_addefxop(sc, iMAC0, A_EXTOUT(A_EXTOUT_AREAR_R), A_C_00000000,
			A_GPR(17), A_GPR(1), &pc);

	/* Digital Rear[l/r] = (GPR[0/1] * RearVolume[l/r]) >> 31 */
	/*   RearVolume = GPR[0x10/0x11] (Will this ever be implemented?) */
	audigy_addefxop(sc, iMAC0, A_EXTOUT(A_EXTOUT_REAR_L), A_C_00000000,
			A_GPR(16), A_GPR(0), &pc);
	audigy_addefxop(sc, iMAC0, A_EXTOUT(A_EXTOUT_REAR_R), A_C_00000000,
			A_GPR(17), A_GPR(1), &pc);
#else
	/* XXX This is just a copy to the channel, since we do not have
	 *     a patch manager, it is useful for have another output enabled.
	 */

	/* Analog Rear[l/r] = GPR[0/1] */
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_AREAR_L), A_C_00000000,
			A_C_00000000, A_GPR(0), &pc);
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_AREAR_R), A_C_00000000,
			A_C_00000000, A_GPR(1), &pc);

	/* Digital Rear[l/r] = GPR[0/1] */
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_REAR_L), A_C_00000000,
			A_C_00000000, A_GPR(0), &pc);
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_REAR_R), A_C_00000000,
			A_C_00000000, A_GPR(1), &pc);
#endif

	/* ADC Recording buffer[l/r] = AC97Input[l/r] */
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_ADC_CAP_L), A_C_00000000,
			A_C_00000000, A_EXTIN(A_EXTIN_AC97_L), &pc);
	audigy_addefxop(sc, iACC3, A_EXTOUT(A_EXTOUT_ADC_CAP_R), A_C_00000000,
			A_C_00000000, A_EXTIN(A_EXTIN_AC97_R), &pc);

	/* resume normal operations */
	emu_wrptr(sc, 0, EMU_A_DBG, 0);
}

static void
emu_initefx(struct sc_info *sc)
{
	int i;
	u_int32_t pc = 16;

	/* acc3 0,0,0,0 - NOPs */
	for (i = 0; i < 512; i++) {
		emu_wrefx(sc, i * 2, 0x10040);
		emu_wrefx(sc, i * 2 + 1, 0x610040);
	}

	for (i = 0; i < 256; i++)
		emu_wrptr(sc, 0, EMU_FXGPREGBASE + i, 0);

	/* FX-8010 DSP Registers:
	   FX Bus
	     0x000-0x00f : 16 registers
	   Input
	     0x010/0x011 : AC97 Codec (l/r)
	     0x012/0x013 : ADC, S/PDIF (l/r)
	     0x014/0x015 : Mic(left), Zoom (l/r)
	     0x016/0x017 : TOS link in (l/r)
	     0x018/0x019 : Line/Mic 1 (l/r)
	     0x01a/0x01b : COAX S/PDIF (l/r)
	     0x01c/0x01d : Line/Mic 2 (l/r)
	   Output
	     0x020/0x021 : AC97 Output (l/r)
	     0x022/0x023 : TOS link out (l/r)
	     0x024/0x025 : Center/LFE
	     0x026/0x027 : LiveDrive Headphone (l/r)
	     0x028/0x029 : Rear Channel (l/r)
	     0x02a/0x02b : ADC Recording Buffer (l/r)
	     0x02c       : Mic Recording Buffer
	     0x031/0x032 : Analog Center/LFE
	   Constants
	     0x040 - 0x044 = 0 - 4
	     0x045 = 0x8, 0x046 = 0x10, 0x047 = 0x20
	     0x048 = 0x100, 0x049 = 0x10000, 0x04a = 0x80000
	     0x04b = 0x10000000, 0x04c = 0x20000000, 0x04d = 0x40000000
	     0x04e = 0x80000000, 0x04f = 0x7fffffff, 0x050 = 0xffffffff
	     0x051 = 0xfffffffe, 0x052 = 0xc0000000, 0x053 = 0x41fbbcdc
	     0x054 = 0x5a7ef9db, 0x055 = 0x00100000
	   Temporary Values
	     0x056 : Accumulator
	     0x057 : Condition Register
	     0x058 : Noise source
	     0x059 : Noise source
	     0x05a : IRQ Register
	     0x05b : TRAM Delay Base Address Count
	   General Purpose Registers
	     0x100 - 0x1ff
	   Tank Memory Data Registers
	     0x200 - 0x2ff
	   Tank Memory Address Registers
	     0x300 - 0x3ff
	     */

	/* Routing - this will be configurable in later version */

	/* GPR[0/1] = FX * 4 + SPDIF-in */
	emu_addefxop(sc, iMACINT0, GPR(0), EXTIN(EXTIN_SPDIF_CD_L),
			FXBUS(FXBUS_PCM_LEFT), C_00000004, &pc);
	emu_addefxop(sc, iMACINT0, GPR(1), EXTIN(EXTIN_SPDIF_CD_R),
			FXBUS(FXBUS_PCM_RIGHT), C_00000004, &pc);

	/* GPR[0/1] += APS-input */
	emu_addefxop(sc, iACC3, GPR(0), GPR(0), C_00000000,
			sc->APS ? EXTIN(EXTIN_TOSLINK_L) : C_00000000, &pc);
	emu_addefxop(sc, iACC3, GPR(1), GPR(1), C_00000000,
			sc->APS ? EXTIN(EXTIN_TOSLINK_R) : C_00000000, &pc);

	/* FrontOut (AC97) = GPR[0/1] */
	emu_addefxop(sc, iACC3, EXTOUT(EXTOUT_AC97_L), C_00000000,
			C_00000000, GPR(0), &pc);
	emu_addefxop(sc, iACC3, EXTOUT(EXTOUT_AC97_R), C_00000000,
			C_00000001, GPR(1), &pc);

	/* GPR[2] = GPR[0] (Left) / 2 + GPR[1] (Right) / 2 -- Central volume */
	emu_addefxop(sc, iINTERP, GPR(2), GPR(1), C_40000000, GPR(0), &pc);

#if 0
	/* RearOut = (GPR[0/1] * RearVolume) >> 31 */
	/*   RearVolume = GPR[0x10/0x11] */
	emu_addefxop(sc, iMAC0, EXTOUT(EXTOUT_REAR_L), C_00000000,
			GPR(16), GPR(0), &pc);
	emu_addefxop(sc, iMAC0, EXTOUT(EXTOUT_REAR_R), C_00000000,
			GPR(17), GPR(1), &pc);
#else
	/* XXX This is just a copy to the channel, since we do not have
	 *     a patch manager, it is useful for have another output enabled.
	 */

	/* Rear[l/r] = GPR[0/1] */
	emu_addefxop(sc, iACC3, EXTOUT(EXTOUT_REAR_L), C_00000000,
			C_00000000, GPR(0), &pc);
	emu_addefxop(sc, iACC3, EXTOUT(EXTOUT_REAR_R), C_00000000,
			C_00000000, GPR(1), &pc);
#endif

	/* TOS out[l/r] = GPR[0/1] */
	emu_addefxop(sc, iACC3, EXTOUT(EXTOUT_TOSLINK_L), C_00000000,
			C_00000000, GPR(0), &pc);
	emu_addefxop(sc, iACC3, EXTOUT(EXTOUT_TOSLINK_R), C_00000000,
			C_00000000, GPR(1), &pc);

	/* Center and Subwoofer configuration */
	/* Analog Center = GPR[0] + GPR[2] */
	emu_addefxop(sc, iACC3, EXTOUT(EXTOUT_ACENTER), C_00000000,
			GPR(0), GPR(2), &pc);
	/* Analog Sub = GPR[1] + GPR[2] */
	emu_addefxop(sc, iACC3, EXTOUT(EXTOUT_ALFE), C_00000000,
			GPR(1), GPR(2), &pc);
	/* Digital Center = GPR[0] + GPR[2] */
	emu_addefxop(sc, iACC3, EXTOUT(EXTOUT_AC97_CENTER), C_00000000,
			GPR(0), GPR(2), &pc);
	/* Digital Sub = GPR[1] + GPR[2] */
	emu_addefxop(sc, iACC3, EXTOUT(EXTOUT_AC97_LFE), C_00000000,
			GPR(1), GPR(2), &pc);

	/* Headphones[l/r] = GPR[0/1] */
	emu_addefxop(sc, iACC3, EXTOUT(EXTOUT_HEADPHONE_L), C_00000000,
			C_00000000, GPR(0), &pc);
	emu_addefxop(sc, iACC3, EXTOUT(EXTOUT_HEADPHONE_R), C_00000000,
			C_00000000, GPR(1), &pc);

	/* ADC Recording buffer[l/r] = AC97Input[l/r] */
	emu_addefxop(sc, iACC3, EXTOUT(EXTOUT_ADC_CAP_L), C_00000000,
			C_00000000, EXTIN(EXTIN_AC97_L), &pc);
	emu_addefxop(sc, iACC3, EXTOUT(EXTOUT_ADC_CAP_R), C_00000000,
			C_00000000, EXTIN(EXTIN_AC97_R), &pc);

	/* resume normal operations */
	emu_wrptr(sc, 0, EMU_DBG, 0);
}

/* Probe and attach the card */
static int
emu_init(struct sc_info *sc)
{
	u_int32_t spcs, ch, tmp, i;

	if (sc->audigy) {
		/* enable additional AC97 slots */
		emu_wrptr(sc, 0, EMU_AC97SLOT, EMU_AC97SLOT_CENTER | EMU_AC97SLOT_LFE);
	}

	/* disable audio and lock cache */
	emu_wr(sc, EMU_HCFG,
	    EMU_HCFG_LOCKSOUNDCACHE | EMU_HCFG_LOCKTANKCACHE_MASK | EMU_HCFG_MUTEBUTTONENABLE,
	    4);

	/* reset recording buffers */
	emu_wrptr(sc, 0, EMU_MICBS, EMU_RECBS_BUFSIZE_NONE);
	emu_wrptr(sc, 0, EMU_MICBA, 0);
	emu_wrptr(sc, 0, EMU_FXBS, EMU_RECBS_BUFSIZE_NONE);
	emu_wrptr(sc, 0, EMU_FXBA, 0);
	emu_wrptr(sc, 0, EMU_ADCBS, EMU_RECBS_BUFSIZE_NONE);
	emu_wrptr(sc, 0, EMU_ADCBA, 0);

	/* disable channel interrupt */
	emu_wr(sc, EMU_INTE,
	    EMU_INTE_INTERTIMERENB | EMU_INTE_SAMPLERATER | EMU_INTE_PCIERRENABLE,
	    4);
	emu_wrptr(sc, 0, EMU_CLIEL, 0);
	emu_wrptr(sc, 0, EMU_CLIEH, 0);
	emu_wrptr(sc, 0, EMU_SOLEL, 0);
	emu_wrptr(sc, 0, EMU_SOLEH, 0);

	/* wonder what these do... */
	if (sc->audigy) {
		emu_wrptr(sc, 0, EMU_SPBYPASS, 0xf00);
		emu_wrptr(sc, 0, EMU_AC97SLOT, 0x3);
	}

	/* init envelope engine */
	for (ch = 0; ch < NUM_G; ch++) {
		emu_wrptr(sc, ch, EMU_CHAN_DCYSUSV, ENV_OFF);
		emu_wrptr(sc, ch, EMU_CHAN_IP, 0);
		emu_wrptr(sc, ch, EMU_CHAN_VTFT, 0xffff);
		emu_wrptr(sc, ch, EMU_CHAN_CVCF, 0xffff);
		emu_wrptr(sc, ch, EMU_CHAN_PTRX, 0);
		emu_wrptr(sc, ch, EMU_CHAN_CPF, 0);
		emu_wrptr(sc, ch, EMU_CHAN_CCR, 0);

		emu_wrptr(sc, ch, EMU_CHAN_PSST, 0);
		emu_wrptr(sc, ch, EMU_CHAN_DSL, 0x10);
		emu_wrptr(sc, ch, EMU_CHAN_CCCA, 0);
		emu_wrptr(sc, ch, EMU_CHAN_Z1, 0);
		emu_wrptr(sc, ch, EMU_CHAN_Z2, 0);
		emu_wrptr(sc, ch, EMU_CHAN_FXRT, 0xd01c0000);

		emu_wrptr(sc, ch, EMU_CHAN_ATKHLDM, 0);
		emu_wrptr(sc, ch, EMU_CHAN_DCYSUSM, 0);
		emu_wrptr(sc, ch, EMU_CHAN_IFATN, 0xffff);
		emu_wrptr(sc, ch, EMU_CHAN_PEFE, 0);
		emu_wrptr(sc, ch, EMU_CHAN_FMMOD, 0);
		emu_wrptr(sc, ch, EMU_CHAN_TREMFRQ, 24);	/* 1 Hz */
		emu_wrptr(sc, ch, EMU_CHAN_FM2FRQ2, 24);	/* 1 Hz */
		emu_wrptr(sc, ch, EMU_CHAN_TEMPENV, 0);

		/*** these are last so OFF prevents writing ***/
		emu_wrptr(sc, ch, EMU_CHAN_LFOVAL2, 0);
		emu_wrptr(sc, ch, EMU_CHAN_LFOVAL1, 0);
		emu_wrptr(sc, ch, EMU_CHAN_ATKHLDV, 0);
		emu_wrptr(sc, ch, EMU_CHAN_ENVVOL, 0);
		emu_wrptr(sc, ch, EMU_CHAN_ENVVAL, 0);

		if (sc->audigy) {
			/* audigy cards need this to initialize correctly */
			emu_wrptr(sc, ch, 0x4c, 0);
			emu_wrptr(sc, ch, 0x4d, 0);
			emu_wrptr(sc, ch, 0x4e, 0);
			emu_wrptr(sc, ch, 0x4f, 0);
			/* set default routing */
			emu_wrptr(sc, ch, EMU_A_CHAN_FXRT1, 0x03020100);
			emu_wrptr(sc, ch, EMU_A_CHAN_FXRT2, 0x3f3f3f3f);
			emu_wrptr(sc, ch, EMU_A_CHAN_SENDAMOUNTS, 0);
		}

		sc->voice[ch].vnum = ch;
		sc->voice[ch].slave = NULL;
		sc->voice[ch].busy = 0;
		sc->voice[ch].ismaster = 0;
		sc->voice[ch].running = 0;
		sc->voice[ch].b16 = 0;
		sc->voice[ch].stereo = 0;
		sc->voice[ch].speed = 0;
		sc->voice[ch].start = 0;
		sc->voice[ch].end = 0;
		sc->voice[ch].channel = NULL;
	}
	sc->pnum = sc->rnum = 0;

	/*
	 *  Init to 0x02109204 :
	 *  Clock accuracy    = 0     (1000ppm)
	 *  Sample Rate       = 2     (48kHz)
	 *  Audio Channel     = 1     (Left of 2)
	 *  Source Number     = 0     (Unspecified)
	 *  Generation Status = 1     (Original for Cat Code 12)
	 *  Cat Code          = 12    (Digital Signal Mixer)
	 *  Mode              = 0     (Mode 0)
	 *  Emphasis          = 0     (None)
	 *  CP                = 1     (Copyright unasserted)
	 *  AN                = 0     (Audio data)
	 *  P                 = 0     (Consumer)
	 */
	spcs = EMU_SPCS_CLKACCY_1000PPM | EMU_SPCS_SAMPLERATE_48 |
	    EMU_SPCS_CHANNELNUM_LEFT | EMU_SPCS_SOURCENUM_UNSPEC |
	    EMU_SPCS_GENERATIONSTATUS | 0x00001200 | 0x00000000 |
	    EMU_SPCS_EMPHASIS_NONE | EMU_SPCS_COPYRIGHT;
	emu_wrptr(sc, 0, EMU_SPCS0, spcs);
	emu_wrptr(sc, 0, EMU_SPCS1, spcs);
	emu_wrptr(sc, 0, EMU_SPCS2, spcs);

	if (!sc->audigy)
		emu_initefx(sc);
	else if (sc->audigy2) {	/* Audigy 2 */
		/* from ALSA initialization code: */

		/* Hack for Alice3 to work independent of haP16V driver */
		u_int32_t tmp;

		/* Setup SRCMulti_I2S SamplingRate */
		tmp = emu_rdptr(sc, 0, EMU_A_SPDIF_SAMPLERATE) & 0xfffff1ff;
		emu_wrptr(sc, 0, EMU_A_SPDIF_SAMPLERATE, tmp | 0x400);

		/* Setup SRCSel (Enable SPDIF, I2S SRCMulti) */
		emu_wr(sc, 0x20, 0x00600000, 4);
		emu_wr(sc, 0x24, 0x00000014, 4);

		/* Setup SRCMulti Input Audio Enable */
		emu_wr(sc, 0x20, 0x006e0000, 4);
		emu_wr(sc, 0x24, 0xff00ff00, 4);
	}

	SLIST_INIT(&sc->mem.blocks);
	sc->mem.ptb_pages = emu_malloc(sc, EMUMAXPAGES * sizeof(u_int32_t),
	    &sc->mem.ptb_pages_addr, &sc->mem.ptb_map);
	if (sc->mem.ptb_pages == NULL)
		return -1;

	sc->mem.silent_page = emu_malloc(sc, EMUPAGESIZE,
	    &sc->mem.silent_page_addr, &sc->mem.silent_map);
	if (sc->mem.silent_page == NULL) {
		emu_free(sc, sc->mem.ptb_pages, sc->mem.ptb_map);
		return -1;
	}
	/* Clear page with silence & setup all pointers to this page */
	bzero(sc->mem.silent_page, EMUPAGESIZE);
	tmp = (u_int32_t)(sc->mem.silent_page_addr) << 1;
	for (i = 0; i < EMUMAXPAGES; i++)
		sc->mem.ptb_pages[i] = tmp | i;

	emu_wrptr(sc, 0, EMU_PTB, (sc->mem.ptb_pages_addr));
	emu_wrptr(sc, 0, EMU_TCB, 0);	/* taken from original driver */
	emu_wrptr(sc, 0, EMU_TCBS, 0);	/* taken from original driver */

	for (ch = 0; ch < NUM_G; ch++) {
		emu_wrptr(sc, ch, EMU_CHAN_MAPA, tmp | EMU_CHAN_MAP_PTI_MASK);
		emu_wrptr(sc, ch, EMU_CHAN_MAPB, tmp | EMU_CHAN_MAP_PTI_MASK);
	}

	/* emu_memalloc(sc, EMUPAGESIZE); */
	/*
	 *  Hokay, now enable the AUD bit
	 *
	 *  Audigy
	 *   Enable Audio = 0 (enabled after fx processor initialization)
	 *   Mute Disable Audio = 0
	 *   Joystick = 1
	 *
	 *  Audigy 2
	 *   Enable Audio = 1
	 *   Mute Disable Audio = 0
	 *   Joystick = 1
	 *   GP S/PDIF AC3 Enable = 1
	 *   CD S/PDIF AC3 Enable = 1
	 *
	 *  EMU10K1
	 *   Enable Audio = 1
	 *   Mute Disable Audio = 0
	 *   Lock Tank Memory = 1
	 *   Lock Sound Memory = 0
	 *   Auto Mute = 1
	 */

	if (sc->audigy) {
		tmp = EMU_HCFG_AUTOMUTE | EMU_HCFG_JOYENABLE;
		if (sc->audigy2)	/* Audigy 2 */
			tmp = EMU_HCFG_AUDIOENABLE | EMU_HCFG_AC3ENABLE_CDSPDIF |
			    EMU_HCFG_AC3ENABLE_GPSPDIF;
		emu_wr(sc, EMU_HCFG, tmp, 4);

		audigy_initefx(sc);

		/* from ALSA initialization code: */

		/* enable audio and disable both audio/digital outputs */
		emu_wr(sc, EMU_HCFG, emu_rd(sc, EMU_HCFG, 4) | EMU_HCFG_AUDIOENABLE, 4);
		emu_wr(sc, EMU_A_IOCFG, emu_rd(sc, EMU_A_IOCFG, 4) & ~EMU_A_IOCFG_GPOUT_AD,
		    4);
		if (sc->audigy2) {	/* Audigy 2 */
			/* Unmute Analog.
			 * Set GPO6 to 1 for Apollo. This has to be done after
			 * init Alice3 I2SOut beyond 48kHz.
			 * So, sequence is important.
			 */
			emu_wr(sc, EMU_A_IOCFG,
			    emu_rd(sc, EMU_A_IOCFG, 4) | EMU_A_IOCFG_GPOUT_A, 4);
		}
	} else {
		/* EMU10K1 initialization code */
		tmp = EMU_HCFG_AUDIOENABLE | EMU_HCFG_LOCKTANKCACHE_MASK 
		    | EMU_HCFG_AUTOMUTE;
		if (sc->rev >= 6)
			tmp |= EMU_HCFG_JOYENABLE;

		emu_wr(sc, EMU_HCFG, tmp, 4);

		/* TOSLink detection */
		sc->tos_link = 0;
		tmp = emu_rd(sc, EMU_HCFG, 4);
		if (tmp & (EMU_HCFG_GPINPUT0 | EMU_HCFG_GPINPUT1)) {
			emu_wr(sc, EMU_HCFG, tmp | EMU_HCFG_GPOUT1, 4);
			DELAY(50);
			if (tmp != (emu_rd(sc, EMU_HCFG, 4) & ~EMU_HCFG_GPOUT1)) {
				sc->tos_link = 1;
				emu_wr(sc, EMU_HCFG, tmp, 4);
			}
		}
	}

	return 0;
}

static int
emu_uninit(struct sc_info *sc)
{
	u_int32_t ch;

	emu_wr(sc, EMU_INTE, 0, 4);
	for (ch = 0; ch < NUM_G; ch++)
		emu_wrptr(sc, ch, EMU_CHAN_DCYSUSV, ENV_OFF);
	for (ch = 0; ch < NUM_G; ch++) {
		emu_wrptr(sc, ch, EMU_CHAN_VTFT, 0);
		emu_wrptr(sc, ch, EMU_CHAN_CVCF, 0);
		emu_wrptr(sc, ch, EMU_CHAN_PTRX, 0);
		emu_wrptr(sc, ch, EMU_CHAN_CPF, 0);
	}

	if (sc->audigy) {	/* stop fx processor */
		emu_wrptr(sc, 0, EMU_A_DBG, EMU_A_DBG_SINGLE_STEP);
	}

	/* disable audio and lock cache */
	emu_wr(sc, EMU_HCFG,
	    EMU_HCFG_LOCKSOUNDCACHE | EMU_HCFG_LOCKTANKCACHE_MASK | EMU_HCFG_MUTEBUTTONENABLE,
	    4);

	emu_wrptr(sc, 0, EMU_PTB, 0);
	/* reset recording buffers */
	emu_wrptr(sc, 0, EMU_MICBS, EMU_RECBS_BUFSIZE_NONE);
	emu_wrptr(sc, 0, EMU_MICBA, 0);
	emu_wrptr(sc, 0, EMU_FXBS, EMU_RECBS_BUFSIZE_NONE);
	emu_wrptr(sc, 0, EMU_FXBA, 0);
	emu_wrptr(sc, 0, EMU_FXWC, 0);
	emu_wrptr(sc, 0, EMU_ADCBS, EMU_RECBS_BUFSIZE_NONE);
	emu_wrptr(sc, 0, EMU_ADCBA, 0);
	emu_wrptr(sc, 0, EMU_TCB, 0);
	emu_wrptr(sc, 0, EMU_TCBS, 0);

	/* disable channel interrupt */
	emu_wrptr(sc, 0, EMU_CLIEL, 0);
	emu_wrptr(sc, 0, EMU_CLIEH, 0);
	emu_wrptr(sc, 0, EMU_SOLEL, 0);
	emu_wrptr(sc, 0, EMU_SOLEH, 0);

	/* init envelope engine */
	if (!SLIST_EMPTY(&sc->mem.blocks))
		device_printf(sc->dev, "warning: memblock list not empty\n");
	emu_free(sc, sc->mem.ptb_pages, sc->mem.ptb_map);
	emu_free(sc, sc->mem.silent_page, sc->mem.silent_map);

	if(sc->mpu)
	    mpu401_uninit(sc->mpu);
	return 0;
}

static int
emu_pci_probe(device_t dev)
{
	char *s = NULL;

	switch (pci_get_devid(dev)) {
	case EMU10K1_PCI_ID:
		s = "Creative EMU10K1";
		break;

	case EMU10K2_PCI_ID:
		if (pci_get_revid(dev) == 0x04)
			s = "Creative Audigy 2 (EMU10K2)";
		else
			s = "Creative Audigy (EMU10K2)";
		break;

	case EMU10K3_PCI_ID:
		s = "Creative Audigy 2 (EMU10K3)";
		break;

	default:
		return ENXIO;
	}

	device_set_desc(dev, s);
	return BUS_PROBE_LOW_PRIORITY;
}

static int
emu_pci_attach(device_t dev)
{
	struct ac97_info *codec = NULL;
	struct sc_info *sc;
	int i, gotmic;
	char status[SND_STATUSLEN];

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->lock = snd_mtxcreate(device_get_nameunit(dev), "snd_emu10k1 softc");
	sc->dev = dev;
	sc->type = pci_get_devid(dev);
	sc->rev = pci_get_revid(dev);
	sc->audigy = sc->type == EMU10K2_PCI_ID || sc->type == EMU10K3_PCI_ID;
	sc->audigy2 = (sc->audigy && sc->rev == 0x04);
	sc->nchans = sc->audigy ? 8 : 4;
	sc->addrmask = sc->audigy ? EMU_A_PTR_ADDR_MASK : EMU_PTR_ADDR_MASK;

	pci_enable_busmaster(dev);

	i = PCIR_BAR(0);
	sc->reg = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &i, RF_ACTIVE);
	if (sc->reg == NULL) {
		device_printf(dev, "unable to map register space\n");
		goto bad;
	}
	sc->st = rman_get_bustag(sc->reg);
	sc->sh = rman_get_bushandle(sc->reg);

	sc->bufsz = pcm_getbuffersize(dev, 4096, EMU_DEFAULT_BUFSZ, 65536);

	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev), /*alignment*/2,
		/*boundary*/0,
		/*lowaddr*/(1U << 31) - 1, /* can only access 0-2gb */
		/*highaddr*/BUS_SPACE_MAXADDR,
		/*filter*/NULL, /*filterarg*/NULL,
		/*maxsize*/sc->bufsz, /*nsegments*/1, /*maxsegz*/0x3ffff,
		/*flags*/0, /*lockfunc*/busdma_lock_mutex,
		/*lockarg*/&Giant, &sc->parent_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		goto bad;
	}

	if (emu_init(sc) == -1) {
		device_printf(dev, "unable to initialize the card\n");
		goto bad;
	}

	codec = AC97_CREATE(dev, sc, emu_ac97);
	if (codec == NULL) goto bad;
	gotmic = (ac97_getcaps(codec) & AC97_CAP_MICCHANNEL) ? 1 : 0;
	if (mixer_init(dev, ac97_getmixerclass(), codec) == -1) goto bad;

	emu_midiattach(sc);

	i = 0;
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &i,
	    RF_ACTIVE | RF_SHAREABLE);
	if (!sc->irq ||
	    snd_setup_intr(dev, sc->irq, INTR_MPSAFE, emu_intr, sc, &sc->ih)) {
		device_printf(dev, "unable to map interrupt\n");
		goto bad;
	}

	snprintf(status, SND_STATUSLEN, "at io 0x%jx irq %jd %s",
	    rman_get_start(sc->reg), rman_get_start(sc->irq),
	    PCM_KLDSTRING(snd_emu10k1));

	if (pcm_register(dev, sc, sc->nchans, gotmic ? 3 : 2)) goto bad;
	for (i = 0; i < sc->nchans; i++)
		pcm_addchan(dev, PCMDIR_PLAY, &emupchan_class, sc);
	for (i = 0; i < (gotmic ? 3 : 2); i++)
		pcm_addchan(dev, PCMDIR_REC, &emurchan_class, sc);

	pcm_setstatus(dev, status);

	return 0;

bad:
	if (codec) ac97_destroy(codec);
	if (sc->reg) bus_release_resource(dev, SYS_RES_IOPORT, PCIR_BAR(0), sc->reg);
	if (sc->ih) bus_teardown_intr(dev, sc->irq, sc->ih);
	if (sc->irq) bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq);
	if (sc->parent_dmat) bus_dma_tag_destroy(sc->parent_dmat);
	if (sc->lock) snd_mtxfree(sc->lock);
	free(sc, M_DEVBUF);
	return ENXIO;
}

static int
emu_pci_detach(device_t dev)
{
	int r;
	struct sc_info *sc;

	r = pcm_unregister(dev);
	if (r)
		return r;

	sc = pcm_getdevinfo(dev);
	/* shutdown chip */
	emu_uninit(sc);

	bus_release_resource(dev, SYS_RES_IOPORT, PCIR_BAR(0), sc->reg);
	bus_teardown_intr(dev, sc->irq, sc->ih);
	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq);
	bus_dma_tag_destroy(sc->parent_dmat);
	snd_mtxfree(sc->lock);
	free(sc, M_DEVBUF);

	return 0;
}

/* add suspend, resume */
static device_method_t emu_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		emu_pci_probe),
	DEVMETHOD(device_attach,	emu_pci_attach),
	DEVMETHOD(device_detach,	emu_pci_detach),

	DEVMETHOD_END
};

static driver_t emu_driver = {
	"pcm",
	emu_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_emu10k1, pci, emu_driver, pcm_devclass, NULL, NULL);
MODULE_DEPEND(snd_emu10k1, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_emu10k1, 1);
MODULE_DEPEND(snd_emu10k1, midi, 1, 1, 1);

/* dummy driver to silence the joystick device */
static int
emujoy_pci_probe(device_t dev)
{
	char *s = NULL;

	switch (pci_get_devid(dev)) {
	case 0x70021102:
		s = "Creative EMU10K1 Joystick";
		device_quiet(dev);
		break;
	case 0x70031102:
		s = "Creative EMU10K2 Joystick";
		device_quiet(dev);
		break;
	}

	if (s) device_set_desc(dev, s);
	return s ? -1000 : ENXIO;
}

static int
emujoy_pci_attach(device_t dev)
{

	return 0;
}

static int
emujoy_pci_detach(device_t dev)
{

	return 0;
}

static device_method_t emujoy_methods[] = {
	DEVMETHOD(device_probe,		emujoy_pci_probe),
	DEVMETHOD(device_attach,	emujoy_pci_attach),
	DEVMETHOD(device_detach,	emujoy_pci_detach),

	DEVMETHOD_END
};

static driver_t emujoy_driver = {
	"emujoy",
	emujoy_methods,
	1	/* no softc */
};

static devclass_t emujoy_devclass;

DRIVER_MODULE(emujoy, pci, emujoy_driver, emujoy_devclass, NULL, NULL);
