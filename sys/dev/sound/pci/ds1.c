/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Cameron Grant <cg@freebsd.org>
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

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/ac97.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/sound/pci/ds1.h>
#include <dev/sound/pci/ds1-fw.h>

SND_DECLARE_FILE("$FreeBSD$");

/* -------------------------------------------------------------------- */

#define	DS1_CHANS 4
#define DS1_RECPRIMARY 0
#define DS1_IRQHZ ((48000 << 8) / 256)
#define DS1_BUFFSIZE 4096

struct pbank {
	volatile u_int32_t	Format;
	volatile u_int32_t	LoopDefault;
	volatile u_int32_t	PgBase;
	volatile u_int32_t	PgLoop;
	volatile u_int32_t	PgLoopEnd;
	volatile u_int32_t	PgLoopFrac;
	volatile u_int32_t	PgDeltaEnd;
	volatile u_int32_t	LpfKEnd;
	volatile u_int32_t	EgGainEnd;
	volatile u_int32_t	LchGainEnd;
	volatile u_int32_t	RchGainEnd;
	volatile u_int32_t	Effect1GainEnd;
	volatile u_int32_t	Effect2GainEnd;
	volatile u_int32_t	Effect3GainEnd;
	volatile u_int32_t	LpfQ;
	volatile u_int32_t	Status;
	volatile u_int32_t	NumOfFrames;
	volatile u_int32_t	LoopCount;
	volatile u_int32_t	PgStart;
	volatile u_int32_t	PgStartFrac;
	volatile u_int32_t	PgDelta;
	volatile u_int32_t	LpfK;
	volatile u_int32_t	EgGain;
	volatile u_int32_t	LchGain;
	volatile u_int32_t	RchGain;
	volatile u_int32_t	Effect1Gain;
	volatile u_int32_t	Effect2Gain;
	volatile u_int32_t	Effect3Gain;
	volatile u_int32_t	LpfD1;
	volatile u_int32_t	LpfD2;
};

struct rbank {
	volatile u_int32_t	PgBase;
	volatile u_int32_t	PgLoopEnd;
	volatile u_int32_t	PgStart;
	volatile u_int32_t	NumOfLoops;
};

struct sc_info;

/* channel registers */
struct sc_pchinfo {
	int run, spd, dir, fmt;
	struct snd_dbuf *buffer;
	struct pcm_channel *channel;
	volatile struct pbank *lslot, *rslot;
	int lsnum, rsnum;
	struct sc_info *parent;
};

struct sc_rchinfo {
	int run, spd, dir, fmt, num;
	struct snd_dbuf *buffer;
	struct pcm_channel *channel;
	volatile struct rbank *slot;
	struct sc_info *parent;
};

/* device private data */
struct sc_info {
	device_t	dev;
	u_int32_t 	type, rev;
	u_int32_t	cd2id, ctrlbase;

	bus_space_tag_t st;
	bus_space_handle_t sh;
	bus_dma_tag_t buffer_dmat, control_dmat;
	bus_dmamap_t map;

	struct resource *reg, *irq;
	int		regid, irqid;
	void		*ih;
	struct mtx	*lock;

	void *regbase;
	u_int32_t *pbase, pbankbase, pbanksize;
	volatile struct pbank *pbank[2 * 64];
	volatile struct rbank *rbank;
	int pslotfree, currbank, pchn, rchn;
	unsigned int bufsz;

	struct sc_pchinfo pch[DS1_CHANS];
	struct sc_rchinfo rch[2];
};

struct {
	u_int32_t dev, subdev;
	char *name;
	u_int32_t *mcode;
} ds_devs[] = {
	{0x00041073, 0, 		"Yamaha DS-1 (YMF724)", CntrlInst},
	{0x000d1073, 0, 		"Yamaha DS-1E (YMF724F)", CntrlInst1E},
	{0x00051073, 0, 		"Yamaha DS-1? (YMF734)", CntrlInst},
	{0x00081073, 0, 		"Yamaha DS-1? (YMF737)", CntrlInst},
	{0x00201073, 0, 		"Yamaha DS-1? (YMF738)", CntrlInst},
	{0x00061073, 0, 		"Yamaha DS-1? (YMF738_TEG)", CntrlInst},
	{0x000a1073, 0x00041073, 	"Yamaha DS-1 (YMF740)", CntrlInst},
	{0x000a1073, 0x000a1073,  	"Yamaha DS-1 (YMF740B)", CntrlInst},
	{0x000a1073, 0x53328086,	"Yamaha DS-1 (YMF740I)", CntrlInst},
	{0x000a1073, 0, 		"Yamaha DS-1 (YMF740?)", CntrlInst},
	{0x000c1073, 0, 		"Yamaha DS-1E (YMF740C)", CntrlInst1E},
	{0x00101073, 0, 		"Yamaha DS-1E (YMF744)", CntrlInst1E},
	{0x00121073, 0, 		"Yamaha DS-1E (YMF754)", CntrlInst1E},
	{0, 0, NULL, NULL}
};

/* -------------------------------------------------------------------- */

/*
 * prototypes
 */

/* stuff */
static int       ds_init(struct sc_info *);
static void      ds_intr(void *);

/* talk to the card */
static u_int32_t ds_rd(struct sc_info *, int, int);
static void 	 ds_wr(struct sc_info *, int, u_int32_t, int);

/* -------------------------------------------------------------------- */

static u_int32_t ds_recfmt[] = {
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
static struct pcmchan_caps ds_reccaps = {4000, 48000, ds_recfmt, 0};

static u_int32_t ds_playfmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	/* SND_FORMAT(AFMT_S16_LE, 1, 0), */
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};
static struct pcmchan_caps ds_playcaps = {4000, 96000, ds_playfmt, 0};

/* -------------------------------------------------------------------- */
/* Hardware */
static u_int32_t
ds_rd(struct sc_info *sc, int regno, int size)
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
ds_wr(struct sc_info *sc, int regno, u_int32_t data, int size)
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
wrl(struct sc_info *sc, u_int32_t *ptr, u_int32_t val)
{
	*(volatile u_int32_t *)ptr = val;
	bus_space_barrier(sc->st, sc->sh, 0, 0, BUS_SPACE_BARRIER_WRITE);
}

/* -------------------------------------------------------------------- */
/* ac97 codec */
static int
ds_cdbusy(struct sc_info *sc, int sec)
{
	int i, reg;

	reg = sec? YDSXGR_SECSTATUSADR : YDSXGR_PRISTATUSADR;
	i = YDSXG_AC97TIMEOUT;
	while (i > 0) {
		if (!(ds_rd(sc, reg, 2) & 0x8000))
			return 0;
		i--;
	}
	return ETIMEDOUT;
}

static u_int32_t
ds_initcd(kobj_t obj, void *devinfo)
{
	struct sc_info *sc = (struct sc_info *)devinfo;
	u_int32_t x;

	x = pci_read_config(sc->dev, PCIR_DSXGCTRL, 1);
	if (x & 0x03) {
		pci_write_config(sc->dev, PCIR_DSXGCTRL, x & ~0x03, 1);
		pci_write_config(sc->dev, PCIR_DSXGCTRL, x | 0x03, 1);
		pci_write_config(sc->dev, PCIR_DSXGCTRL, x & ~0x03, 1);
		/*
		 * The YMF740 on some Intel motherboards requires a pretty
		 * hefty delay after this reset for some reason...  Otherwise:
		 * "pcm0: ac97 codec init failed"
		 * Maybe this is needed for all YMF740's?
		 * 400ms and 500ms here seem to work, 300ms does not.
		 *
		 * do it for all chips -cg
		 */
		DELAY(500000);
	}

	return ds_cdbusy(sc, 0)? 0 : 1;
}

static int
ds_rdcd(kobj_t obj, void *devinfo, int regno)
{
	struct sc_info *sc = (struct sc_info *)devinfo;
	int sec, cid, i;
	u_int32_t cmd, reg;

	sec = regno & 0x100;
	regno &= 0xff;
	cid = sec? (sc->cd2id << 8) : 0;
	reg = sec? YDSXGR_SECSTATUSDATA : YDSXGR_PRISTATUSDATA;
	if (sec && cid == 0)
		return 0xffffffff;

	cmd = YDSXG_AC97READCMD | cid | regno;
	ds_wr(sc, YDSXGR_AC97CMDADR, cmd, 2);

	if (ds_cdbusy(sc, sec))
		return 0xffffffff;

	if (sc->type == 11 && sc->rev < 2)
		for (i = 0; i < 600; i++)
			ds_rd(sc, reg, 2);

	return ds_rd(sc, reg, 2);
}

static int
ds_wrcd(kobj_t obj, void *devinfo, int regno, u_int32_t data)
{
	struct sc_info *sc = (struct sc_info *)devinfo;
	int sec, cid;
	u_int32_t cmd;

	sec = regno & 0x100;
	regno &= 0xff;
	cid = sec? (sc->cd2id << 8) : 0;
	if (sec && cid == 0)
		return ENXIO;

	cmd = YDSXG_AC97WRITECMD | cid | regno;
	cmd <<= 16;
	cmd |= data;
	ds_wr(sc, YDSXGR_AC97CMDDATA, cmd, 4);

	return ds_cdbusy(sc, sec);
}

static kobj_method_t ds_ac97_methods[] = {
    	KOBJMETHOD(ac97_init,		ds_initcd),
    	KOBJMETHOD(ac97_read,		ds_rdcd),
    	KOBJMETHOD(ac97_write,		ds_wrcd),
	KOBJMETHOD_END
};
AC97_DECLARE(ds_ac97);

/* -------------------------------------------------------------------- */

static void
ds_enadsp(struct sc_info *sc, int on)
{
	u_int32_t v, i;

	v = on? 1 : 0;
	if (on) {
		ds_wr(sc, YDSXGR_CONFIG, 0x00000001, 4);
	} else {
		if (ds_rd(sc, YDSXGR_CONFIG, 4))
			ds_wr(sc, YDSXGR_CONFIG, 0x00000000, 4);
		i = YDSXG_WORKBITTIMEOUT;
		while (i > 0) {
			if (!(ds_rd(sc, YDSXGR_CONFIG, 4) & 0x00000002))
				break;
			i--;
		}
	}
}

static volatile struct pbank *
ds_allocpslot(struct sc_info *sc)
{
	int slot;

	if (sc->pslotfree > 63)
		return NULL;
	slot = sc->pslotfree++;
	return sc->pbank[slot * 2];
}

static int
ds_initpbank(volatile struct pbank *pb, int ch, int stereo, int b16, u_int32_t rate, bus_addr_t base, u_int32_t len)
{
	u_int32_t lv[] = {1, 1, 0, 0, 0};
	u_int32_t rv[] = {1, 0, 1, 0, 0};
	u_int32_t e1[] = {0, 0, 0, 0, 0};
	u_int32_t e2[] = {1, 0, 0, 1, 0};
	u_int32_t e3[] = {1, 0, 0, 0, 1};
	int ss, i;
	u_int32_t delta;

	struct {
		int rate, fK, fQ;
	} speedinfo[] = {
		{  100, 0x00570000, 0x35280000},
		{ 2000, 0x06aa0000, 0x34a70000},
		{ 8000, 0x18b20000, 0x32020000},
		{11025, 0x20930000, 0x31770000},
		{16000, 0x2b9a0000, 0x31390000},
		{22050, 0x35a10000, 0x31c90000},
		{32000, 0x3eaa0000, 0x33d00000},
/*		{44100, 0x04646000, 0x370a0000},
*/		{48000, 0x40000000, 0x40000000},
	};

	ss = b16? 1 : 0;
	ss += stereo? 1 : 0;
	delta = (65536 * rate) / 48000;
	i = 0;
	while (i < 7 && speedinfo[i].rate < rate)
		i++;

	pb->Format = stereo? 0x00010000 : 0;
	pb->Format |= b16? 0 : 0x80000000;
	pb->Format |= (stereo && (ch == 2 || ch == 4))? 0x00000001 : 0;
	pb->LoopDefault = 0;
	pb->PgBase = base;
	pb->PgLoop = 0;
	pb->PgLoopEnd = len >> ss;
	pb->PgLoopFrac = 0;
	pb->Status = 0;
	pb->NumOfFrames = 0;
	pb->LoopCount = 0;
	pb->PgStart = 0;
	pb->PgStartFrac = 0;
	pb->PgDelta = pb->PgDeltaEnd = delta << 12;
	pb->LpfQ = speedinfo[i].fQ;
	pb->LpfK = pb->LpfKEnd = speedinfo[i].fK;
	pb->LpfD1 = pb->LpfD2 = 0;
	pb->EgGain = pb->EgGainEnd = 0x40000000;
	pb->LchGain = pb->LchGainEnd = lv[ch] * 0x40000000;
	pb->RchGain = pb->RchGainEnd = rv[ch] * 0x40000000;
	pb->Effect1Gain = pb->Effect1GainEnd = e1[ch] * 0x40000000;
	pb->Effect2Gain = pb->Effect2GainEnd = e2[ch] * 0x40000000;
	pb->Effect3Gain = pb->Effect3GainEnd = e3[ch] * 0x40000000;

	return 0;
}

static void
ds_enapslot(struct sc_info *sc, int slot, int go)
{
	wrl(sc, &sc->pbase[slot + 1], go? (sc->pbankbase + 2 * slot * sc->pbanksize) : 0);
	/* printf("pbase[%d] = 0x%x\n", slot + 1, go? (sc->pbankbase + 2 * slot * sc->pbanksize) : 0); */
}

static void
ds_setuppch(struct sc_pchinfo *ch)
{
	int stereo, b16, c, sz;
	bus_addr_t addr;

	stereo = (AFMT_CHANNEL(ch->fmt) > 1)? 1 : 0;
	b16 = (ch->fmt & AFMT_16BIT)? 1 : 0;
	c = stereo? 1 : 0;
	addr = sndbuf_getbufaddr(ch->buffer);
	sz = sndbuf_getsize(ch->buffer);

	ds_initpbank(ch->lslot, c, stereo, b16, ch->spd, addr, sz);
	ds_initpbank(ch->lslot + 1, c, stereo, b16, ch->spd, addr, sz);
	ds_initpbank(ch->rslot, 2, stereo, b16, ch->spd, addr, sz);
	ds_initpbank(ch->rslot + 1, 2, stereo, b16, ch->spd, addr, sz);
}

static void
ds_setuprch(struct sc_rchinfo *ch)
{
	struct sc_info *sc = ch->parent;
	int stereo, b16, i, sz, pri;
	u_int32_t x, y;
	bus_addr_t addr;

	stereo = (AFMT_CHANNEL(ch->fmt) > 1)? 1 : 0;
	b16 = (ch->fmt & AFMT_16BIT)? 1 : 0;
	addr = sndbuf_getbufaddr(ch->buffer);
	sz = sndbuf_getsize(ch->buffer);
	pri = (ch->num == DS1_RECPRIMARY)? 1 : 0;

	for (i = 0; i < 2; i++) {
		ch->slot[i].PgBase = addr;
		ch->slot[i].PgLoopEnd = sz;
		ch->slot[i].PgStart = 0;
		ch->slot[i].NumOfLoops = 0;
	}
	x = (b16? 0x00 : 0x01) | (stereo? 0x02 : 0x00);
	y = (48000 * 4096) / ch->spd;
	y--;
	/* printf("pri = %d, x = %d, y = %d\n", pri, x, y); */
	ds_wr(sc, pri? YDSXGR_ADCFORMAT : YDSXGR_RECFORMAT, x, 4);
	ds_wr(sc, pri? YDSXGR_ADCSLOTSR : YDSXGR_RECSLOTSR, y, 4);
}

/* -------------------------------------------------------------------- */
/* play channel interface */
static void *
ds1pchan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct sc_info *sc = devinfo;
	struct sc_pchinfo *ch;

	KASSERT(dir == PCMDIR_PLAY, ("ds1pchan_init: bad direction"));

	ch = &sc->pch[sc->pchn++];
	ch->buffer = b;
	ch->parent = sc;
	ch->channel = c;
	ch->dir = dir;
	ch->fmt = SND_FORMAT(AFMT_U8, 1, 0);
	ch->spd = 8000;
	ch->run = 0;
	if (sndbuf_alloc(ch->buffer, sc->buffer_dmat, 0, sc->bufsz) != 0)
		return NULL;
	else {
		ch->lsnum = sc->pslotfree;
		ch->lslot = ds_allocpslot(sc);
		ch->rsnum = sc->pslotfree;
		ch->rslot = ds_allocpslot(sc);
		ds_setuppch(ch);
		return ch;
	}
}

static int
ds1pchan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct sc_pchinfo *ch = data;

	ch->fmt = format;

	return 0;
}

static u_int32_t
ds1pchan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct sc_pchinfo *ch = data;

	ch->spd = speed;

	return speed;
}

static u_int32_t
ds1pchan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct sc_pchinfo *ch = data;
	struct sc_info *sc = ch->parent;
	int drate;

	/* irq rate is fixed at 187.5hz */
	drate = ch->spd * sndbuf_getalign(ch->buffer);
	blocksize = roundup2((drate << 8) / DS1_IRQHZ, 4);
	sndbuf_resize(ch->buffer, sc->bufsz / blocksize, blocksize);

	return blocksize;
}

/* semantic note: must start at beginning of buffer */
static int
ds1pchan_trigger(kobj_t obj, void *data, int go)
{
	struct sc_pchinfo *ch = data;
	struct sc_info *sc = ch->parent;
	int stereo;

	if (!PCMTRIG_COMMON(go))
		return 0;
	stereo = (AFMT_CHANNEL(ch->fmt) > 1)? 1 : 0;
	if (go == PCMTRIG_START) {
		ch->run = 1;
		ds_setuppch(ch);
		ds_enapslot(sc, ch->lsnum, 1);
		ds_enapslot(sc, ch->rsnum, stereo);
		snd_mtxlock(sc->lock);
		ds_wr(sc, YDSXGR_MODE, 0x00000003, 4);
		snd_mtxunlock(sc->lock);
	} else {
		ch->run = 0;
		/* ds_setuppch(ch); */
		ds_enapslot(sc, ch->lsnum, 0);
		ds_enapslot(sc, ch->rsnum, 0);
	}

	return 0;
}

static u_int32_t
ds1pchan_getptr(kobj_t obj, void *data)
{
	struct sc_pchinfo *ch = data;
	struct sc_info *sc = ch->parent;
	volatile struct pbank *bank;
	int ss;
	u_int32_t ptr;

	ss = (AFMT_CHANNEL(ch->fmt) > 1)? 1 : 0;
	ss += (ch->fmt & AFMT_16BIT)? 1 : 0;

	bank = ch->lslot + sc->currbank;
	/* printf("getptr: %d\n", bank->PgStart << ss); */
	ptr = bank->PgStart;
	ptr <<= ss;
	return ptr;
}

static struct pcmchan_caps *
ds1pchan_getcaps(kobj_t obj, void *data)
{
	return &ds_playcaps;
}

static kobj_method_t ds1pchan_methods[] = {
    	KOBJMETHOD(channel_init,		ds1pchan_init),
    	KOBJMETHOD(channel_setformat,		ds1pchan_setformat),
    	KOBJMETHOD(channel_setspeed,		ds1pchan_setspeed),
    	KOBJMETHOD(channel_setblocksize,	ds1pchan_setblocksize),
    	KOBJMETHOD(channel_trigger,		ds1pchan_trigger),
    	KOBJMETHOD(channel_getptr,		ds1pchan_getptr),
    	KOBJMETHOD(channel_getcaps,		ds1pchan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(ds1pchan);

/* -------------------------------------------------------------------- */
/* record channel interface */
static void *
ds1rchan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct sc_info *sc = devinfo;
	struct sc_rchinfo *ch;

	KASSERT(dir == PCMDIR_REC, ("ds1rchan_init: bad direction"));

	ch = &sc->rch[sc->rchn];
	ch->num = sc->rchn++;
	ch->buffer = b;
	ch->parent = sc;
	ch->channel = c;
	ch->dir = dir;
	ch->fmt = SND_FORMAT(AFMT_U8, 1, 0);
	ch->spd = 8000;
	if (sndbuf_alloc(ch->buffer, sc->buffer_dmat, 0, sc->bufsz) != 0)
		return NULL;
	else {
		ch->slot = (ch->num == DS1_RECPRIMARY)? sc->rbank + 2: sc->rbank;
		ds_setuprch(ch);
		return ch;
	}
}

static int
ds1rchan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct sc_rchinfo *ch = data;

	ch->fmt = format;

	return 0;
}

static u_int32_t
ds1rchan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct sc_rchinfo *ch = data;

	ch->spd = speed;

	return speed;
}

static u_int32_t
ds1rchan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct sc_rchinfo *ch = data;
	struct sc_info *sc = ch->parent;
	int drate;

	/* irq rate is fixed at 187.5hz */
	drate = ch->spd * sndbuf_getalign(ch->buffer);
	blocksize = roundup2((drate << 8) / DS1_IRQHZ, 4);
	sndbuf_resize(ch->buffer, sc->bufsz / blocksize, blocksize);

	return blocksize;
}

/* semantic note: must start at beginning of buffer */
static int
ds1rchan_trigger(kobj_t obj, void *data, int go)
{
	struct sc_rchinfo *ch = data;
	struct sc_info *sc = ch->parent;
	u_int32_t x;

	if (!PCMTRIG_COMMON(go))
		return 0;
	if (go == PCMTRIG_START) {
		ch->run = 1;
		ds_setuprch(ch);
		snd_mtxlock(sc->lock);
		x = ds_rd(sc, YDSXGR_MAPOFREC, 4);
		x |= (ch->num == DS1_RECPRIMARY)? 0x02 : 0x01;
		ds_wr(sc, YDSXGR_MAPOFREC, x, 4);
		ds_wr(sc, YDSXGR_MODE, 0x00000003, 4);
		snd_mtxunlock(sc->lock);
	} else {
		ch->run = 0;
		snd_mtxlock(sc->lock);
		x = ds_rd(sc, YDSXGR_MAPOFREC, 4);
		x &= ~((ch->num == DS1_RECPRIMARY)? 0x02 : 0x01);
		ds_wr(sc, YDSXGR_MAPOFREC, x, 4);
		snd_mtxunlock(sc->lock);
	}

	return 0;
}

static u_int32_t
ds1rchan_getptr(kobj_t obj, void *data)
{
	struct sc_rchinfo *ch = data;
	struct sc_info *sc = ch->parent;

	return ch->slot[sc->currbank].PgStart;
}

static struct pcmchan_caps *
ds1rchan_getcaps(kobj_t obj, void *data)
{
	return &ds_reccaps;
}

static kobj_method_t ds1rchan_methods[] = {
    	KOBJMETHOD(channel_init,		ds1rchan_init),
    	KOBJMETHOD(channel_setformat,		ds1rchan_setformat),
    	KOBJMETHOD(channel_setspeed,		ds1rchan_setspeed),
    	KOBJMETHOD(channel_setblocksize,	ds1rchan_setblocksize),
    	KOBJMETHOD(channel_trigger,		ds1rchan_trigger),
    	KOBJMETHOD(channel_getptr,		ds1rchan_getptr),
    	KOBJMETHOD(channel_getcaps,		ds1rchan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(ds1rchan);

/* -------------------------------------------------------------------- */
/* The interrupt handler */
static void
ds_intr(void *p)
{
	struct sc_info *sc = (struct sc_info *)p;
	u_int32_t i, x;

	snd_mtxlock(sc->lock);
	i = ds_rd(sc, YDSXGR_STATUS, 4);
	if (i & 0x00008000)
		device_printf(sc->dev, "timeout irq\n");
	if (i & 0x80008000) {
		ds_wr(sc, YDSXGR_STATUS, i & 0x80008000, 4);
		sc->currbank = ds_rd(sc, YDSXGR_CTRLSELECT, 4) & 0x00000001;

		x = 0;
		for (i = 0; i < DS1_CHANS; i++) {
			if (sc->pch[i].run) {
				x = 1;
				snd_mtxunlock(sc->lock);
				chn_intr(sc->pch[i].channel);
				snd_mtxlock(sc->lock);
			}
		}
		for (i = 0; i < 2; i++) {
			if (sc->rch[i].run) {
				x = 1;
				snd_mtxunlock(sc->lock);
				chn_intr(sc->rch[i].channel);
				snd_mtxlock(sc->lock);
			}
		}
		i = ds_rd(sc, YDSXGR_MODE, 4);
		if (x)
			ds_wr(sc, YDSXGR_MODE, i | 0x00000002, 4);

	}
	snd_mtxunlock(sc->lock);
}

/* -------------------------------------------------------------------- */

/*
 * Probe and attach the card
 */

static void
ds_setmap(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct sc_info *sc = arg;

	sc->ctrlbase = error? 0 : (u_int32_t)segs->ds_addr;

	if (bootverbose) {
		printf("ds1: setmap (%lx, %lx), nseg=%d, error=%d\n",
		       (unsigned long)segs->ds_addr, (unsigned long)segs->ds_len,
		       nseg, error);
	}
}

static int
ds_init(struct sc_info *sc)
{
	int i;
	u_int32_t *ci, r, pcs, rcs, ecs, ws, memsz, cb;
	u_int8_t *t;
	void *buf;

	ci = ds_devs[sc->type].mcode;

	ds_wr(sc, YDSXGR_NATIVEDACOUTVOL, 0x00000000, 4);
	ds_enadsp(sc, 0);
	ds_wr(sc, YDSXGR_MODE, 0x00010000, 4);
	ds_wr(sc, YDSXGR_MODE, 0x00000000, 4);
	ds_wr(sc, YDSXGR_MAPOFREC, 0x00000000, 4);
	ds_wr(sc, YDSXGR_MAPOFEFFECT, 0x00000000, 4);
	ds_wr(sc, YDSXGR_PLAYCTRLBASE, 0x00000000, 4);
	ds_wr(sc, YDSXGR_RECCTRLBASE, 0x00000000, 4);
	ds_wr(sc, YDSXGR_EFFCTRLBASE, 0x00000000, 4);
	r = ds_rd(sc, YDSXGR_GLOBALCTRL, 2);
	ds_wr(sc, YDSXGR_GLOBALCTRL, r & ~0x0007, 2);

	for (i = 0; i < YDSXG_DSPLENGTH; i += 4)
		ds_wr(sc, YDSXGR_DSPINSTRAM + i, DspInst[i >> 2], 4);

	for (i = 0; i < YDSXG_CTRLLENGTH; i += 4)
		ds_wr(sc, YDSXGR_CTRLINSTRAM + i, ci[i >> 2], 4);

	ds_enadsp(sc, 1);

	pcs = 0;
	for (i = 100; i > 0; i--) {
		pcs = ds_rd(sc, YDSXGR_PLAYCTRLSIZE, 4) << 2;
		if (pcs == sizeof(struct pbank))
			break;
		DELAY(1000);
	}
	if (pcs != sizeof(struct pbank)) {
		device_printf(sc->dev, "preposterous playctrlsize (%d)\n", pcs);
		return -1;
	}
	rcs = ds_rd(sc, YDSXGR_RECCTRLSIZE, 4) << 2;
	ecs = ds_rd(sc, YDSXGR_EFFCTRLSIZE, 4) << 2;
	ws = ds_rd(sc, YDSXGR_WORKSIZE, 4) << 2;

	memsz = 64 * 2 * pcs + 2 * 2 * rcs + 5 * 2 * ecs + ws;
	memsz += (64 + 1) * 4;

	if (sc->regbase == NULL) {
		if (bus_dma_tag_create(bus_get_dma_tag(sc->dev), 2, 0,
				       BUS_SPACE_MAXADDR_32BIT,
				       BUS_SPACE_MAXADDR,
				       NULL, NULL, memsz, 1, memsz, 0, NULL,
				       NULL, &sc->control_dmat))
			return -1;
		if (bus_dmamem_alloc(sc->control_dmat, &buf, BUS_DMA_NOWAIT, &sc->map))
			return -1;
		if (bus_dmamap_load(sc->control_dmat, sc->map, buf, memsz, ds_setmap, sc, 0) || !sc->ctrlbase) {
			device_printf(sc->dev, "pcs=%d, rcs=%d, ecs=%d, ws=%d, memsz=%d\n",
			      	      pcs, rcs, ecs, ws, memsz);
			return -1;
		}
		sc->regbase = buf;
	} else
		buf = sc->regbase;

	cb = 0;
	t = buf;
	ds_wr(sc, YDSXGR_WORKBASE, sc->ctrlbase + cb, 4);
	cb += ws;
	sc->pbase = (u_int32_t *)(t + cb);
	/* printf("pbase = %p -> 0x%x\n", sc->pbase, sc->ctrlbase + cb); */
	ds_wr(sc, YDSXGR_PLAYCTRLBASE, sc->ctrlbase + cb, 4);
	cb += (64 + 1) * 4;
	sc->rbank = (struct rbank *)(t + cb);
	ds_wr(sc, YDSXGR_RECCTRLBASE, sc->ctrlbase + cb, 4);
	cb += 2 * 2 * rcs;
	ds_wr(sc, YDSXGR_EFFCTRLBASE, sc->ctrlbase + cb, 4);
	cb += 5 * 2 * ecs;

	sc->pbankbase = sc->ctrlbase + cb;
	sc->pbanksize = pcs;
	for (i = 0; i < 64; i++) {
		wrl(sc, &sc->pbase[i + 1], 0);
		sc->pbank[i * 2] = (struct pbank *)(t + cb);
		/* printf("pbank[%d] = %p -> 0x%x; ", i * 2, (struct pbank *)(t + cb), sc->ctrlbase + cb - vtophys(t + cb)); */
		cb += pcs;
		sc->pbank[i * 2 + 1] = (struct pbank *)(t + cb);
		/* printf("pbank[%d] = %p -> 0x%x\n", i * 2 + 1, (struct pbank *)(t + cb), sc->ctrlbase + cb - vtophys(t + cb)); */
		cb += pcs;
	}
	wrl(sc, &sc->pbase[0], DS1_CHANS * 2);

	sc->pchn = sc->rchn = 0;
	ds_wr(sc, YDSXGR_NATIVEDACOUTVOL, 0x3fff3fff, 4);
	ds_wr(sc, YDSXGR_NATIVEADCINVOL, 0x3fff3fff, 4);
	ds_wr(sc, YDSXGR_NATIVEDACINVOL, 0x3fff3fff, 4);

	return 0;
}

static int
ds_uninit(struct sc_info *sc)
{
	ds_wr(sc, YDSXGR_NATIVEDACOUTVOL, 0x00000000, 4);
	ds_wr(sc, YDSXGR_NATIVEADCINVOL, 0, 4);
	ds_wr(sc, YDSXGR_NATIVEDACINVOL, 0, 4);
	ds_enadsp(sc, 0);
	ds_wr(sc, YDSXGR_MODE, 0x00010000, 4);
	ds_wr(sc, YDSXGR_MAPOFREC, 0x00000000, 4);
	ds_wr(sc, YDSXGR_MAPOFEFFECT, 0x00000000, 4);
	ds_wr(sc, YDSXGR_PLAYCTRLBASE, 0x00000000, 4);
	ds_wr(sc, YDSXGR_RECCTRLBASE, 0x00000000, 4);
	ds_wr(sc, YDSXGR_EFFCTRLBASE, 0x00000000, 4);
	ds_wr(sc, YDSXGR_GLOBALCTRL, 0, 2);

	bus_dmamap_unload(sc->control_dmat, sc->map);
	bus_dmamem_free(sc->control_dmat, sc->regbase, sc->map);

	return 0;
}

static int
ds_finddev(u_int32_t dev, u_int32_t subdev)
{
	int i;

	for (i = 0; ds_devs[i].dev; i++) {
		if (ds_devs[i].dev == dev &&
		    (ds_devs[i].subdev == subdev || ds_devs[i].subdev == 0))
			return i;
	}
	return -1;
}

static int
ds_pci_probe(device_t dev)
{
	int i;
	u_int32_t subdev;

	subdev = (pci_get_subdevice(dev) << 16) | pci_get_subvendor(dev);
	i = ds_finddev(pci_get_devid(dev), subdev);
	if (i >= 0) {
		device_set_desc(dev, ds_devs[i].name);
		return BUS_PROBE_DEFAULT;
	} else
		return ENXIO;
}

static int
ds_pci_attach(device_t dev)
{
	u_int32_t subdev, i;
	struct sc_info *sc;
	struct ac97_info *codec = NULL;
	char 		status[SND_STATUSLEN];

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->lock = snd_mtxcreate(device_get_nameunit(dev), "snd_ds1 softc");
	sc->dev = dev;
	subdev = (pci_get_subdevice(dev) << 16) | pci_get_subvendor(dev);
	sc->type = ds_finddev(pci_get_devid(dev), subdev);
	sc->rev = pci_get_revid(dev);

	pci_enable_busmaster(dev);

	sc->regid = PCIR_BAR(0);
	sc->reg = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->regid,
					 RF_ACTIVE);
	if (!sc->reg) {
		device_printf(dev, "unable to map register space\n");
		goto bad;
	}

	sc->st = rman_get_bustag(sc->reg);
	sc->sh = rman_get_bushandle(sc->reg);

	sc->bufsz = pcm_getbuffersize(dev, 4096, DS1_BUFFSIZE, 65536);

	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev), /*alignment*/2,
		/*boundary*/0,
		/*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
		/*highaddr*/BUS_SPACE_MAXADDR,
		/*filter*/NULL, /*filterarg*/NULL,
		/*maxsize*/sc->bufsz, /*nsegments*/1, /*maxsegz*/0x3ffff,
		/*flags*/0, /*lockfunc*/NULL,
		/*lockarg*/NULL, &sc->buffer_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		goto bad;
	}

	sc->regbase = NULL;
	if (ds_init(sc) == -1) {
		device_printf(dev, "unable to initialize the card\n");
		goto bad;
	}

	codec = AC97_CREATE(dev, sc, ds_ac97);
	if (codec == NULL)
		goto bad;
	/*
	 * Turn on inverted external amplifier sense flags for few
	 * 'special' boards.
	 */
	switch (subdev) {
	case 0x81171033:	/* NEC ValueStar (VT550/0) */
		ac97_setflags(codec, ac97_getflags(codec) | AC97_F_EAPD_INV);
		break;
	default:
		break;
	}
	mixer_init(dev, ac97_getmixerclass(), codec);

	sc->irqid = 0;
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irqid,
					 RF_ACTIVE | RF_SHAREABLE);
	if (!sc->irq || snd_setup_intr(dev, sc->irq, INTR_MPSAFE, ds_intr, sc, &sc->ih)) {
		device_printf(dev, "unable to map interrupt\n");
		goto bad;
	}

	snprintf(status, SND_STATUSLEN, "at memory 0x%jx irq %jd %s",
		 rman_get_start(sc->reg), rman_get_start(sc->irq),PCM_KLDSTRING(snd_ds1));

	if (pcm_register(dev, sc, DS1_CHANS, 2))
		goto bad;
	for (i = 0; i < DS1_CHANS; i++)
		pcm_addchan(dev, PCMDIR_PLAY, &ds1pchan_class, sc);
	for (i = 0; i < 2; i++)
		pcm_addchan(dev, PCMDIR_REC, &ds1rchan_class, sc);
	pcm_setstatus(dev, status);

	return 0;

bad:
	if (codec)
		ac97_destroy(codec);
	if (sc->reg)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->regid, sc->reg);
	if (sc->ih)
		bus_teardown_intr(dev, sc->irq, sc->ih);
	if (sc->irq)
		bus_release_resource(dev, SYS_RES_IRQ, sc->irqid, sc->irq);
	if (sc->buffer_dmat)
		bus_dma_tag_destroy(sc->buffer_dmat);
	if (sc->control_dmat)
		bus_dma_tag_destroy(sc->control_dmat);
	if (sc->lock)
		snd_mtxfree(sc->lock);
	free(sc, M_DEVBUF);
	return ENXIO;
}

static int
ds_pci_resume(device_t dev)
{
       struct sc_info *sc;

       sc = pcm_getdevinfo(dev);

       if (ds_init(sc) == -1) {
           device_printf(dev, "unable to reinitialize the card\n");
           return ENXIO;
       }
       if (mixer_reinit(dev) == -1) {
               device_printf(dev, "unable to reinitialize the mixer\n");
               return ENXIO;
       }
       return 0;
}

static int
ds_pci_detach(device_t dev)
{
    	int r;
	struct sc_info *sc;

	r = pcm_unregister(dev);
	if (r)
    		return r;

	sc = pcm_getdevinfo(dev);
	ds_uninit(sc);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->regid, sc->reg);
	bus_teardown_intr(dev, sc->irq, sc->ih);
	bus_release_resource(dev, SYS_RES_IRQ, sc->irqid, sc->irq);
	bus_dma_tag_destroy(sc->buffer_dmat);
	bus_dma_tag_destroy(sc->control_dmat);
	snd_mtxfree(sc->lock);
	free(sc, M_DEVBUF);
       	return 0;
}

static device_method_t ds1_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ds_pci_probe),
	DEVMETHOD(device_attach,	ds_pci_attach),
	DEVMETHOD(device_detach,	ds_pci_detach),
	DEVMETHOD(device_resume,        ds_pci_resume),
	{ 0, 0 }
};

static driver_t ds1_driver = {
	"pcm",
	ds1_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_ds1, pci, ds1_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_ds1, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_ds1, 1);
