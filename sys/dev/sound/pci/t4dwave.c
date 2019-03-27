/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THEPOSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/ac97.h>
#include <dev/sound/pci/t4dwave.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

SND_DECLARE_FILE("$FreeBSD$");

/* -------------------------------------------------------------------- */

#define TDX_PCI_ID 	0x20001023
#define TNX_PCI_ID 	0x20011023
#define ALI_PCI_ID	0x545110b9
#define SPA_PCI_ID	0x70181039

#define TR_DEFAULT_BUFSZ 	0x1000
/* For ALi M5451 the DMA transfer size appears to be fixed to 64k. */
#define ALI_BUFSZ	0x10000
#define TR_BUFALGN	0x8
#define TR_TIMEOUT_CDC	0xffff
#define TR_MAXHWCH	64
#define ALI_MAXHWCH	32
#define TR_MAXPLAYCH	4
#define ALI_MAXPLAYCH	1
/*
 * Though, it's not clearly documented in the 4DWAVE datasheet, the
 * DX and NX chips can't handle DMA addresses located above 1GB as the
 * LBA (loop begin address) register which holds the DMA base address
 * is 32-bit, but the two MSBs are used for other purposes.
 */
#define TR_MAXADDR	((1U << 30) - 1)
#define ALI_MAXADDR	((1U << 31) - 1)

struct tr_info;

/* channel registers */
struct tr_chinfo {
	u_int32_t cso, alpha, fms, fmc, ec;
	u_int32_t lba;
	u_int32_t eso, delta;
	u_int32_t rvol, cvol;
	u_int32_t gvsel, pan, vol, ctrl;
	u_int32_t active:1, was_active:1;
	int index, bufhalf;
	struct snd_dbuf *buffer;
	struct pcm_channel *channel;
	struct tr_info *parent;
};

struct tr_rchinfo {
	u_int32_t delta;
	u_int32_t active:1, was_active:1;
	struct snd_dbuf *buffer;
	struct pcm_channel *channel;
	struct tr_info *parent;
};

/* device private data */
struct tr_info {
	u_int32_t type;
	u_int32_t rev;

	bus_space_tag_t st;
	bus_space_handle_t sh;
	bus_dma_tag_t parent_dmat;

	struct resource *reg, *irq;
	int regtype, regid, irqid;
	void *ih;

	struct mtx *lock;

	u_int32_t hwchns;
	u_int32_t playchns;
	unsigned int bufsz;

	struct tr_chinfo chinfo[TR_MAXPLAYCH];
	struct tr_rchinfo recchinfo;
};

/* -------------------------------------------------------------------- */

static u_int32_t tr_recfmt[] = {
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
static struct pcmchan_caps tr_reccaps = {4000, 48000, tr_recfmt, 0};

static u_int32_t tr_playfmt[] = {
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
static struct pcmchan_caps tr_playcaps = {4000, 48000, tr_playfmt, 0};

/* -------------------------------------------------------------------- */

/* Hardware */

static u_int32_t
tr_rd(struct tr_info *tr, int regno, int size)
{
	switch(size) {
	case 1:
		return bus_space_read_1(tr->st, tr->sh, regno);
	case 2:
		return bus_space_read_2(tr->st, tr->sh, regno);
	case 4:
		return bus_space_read_4(tr->st, tr->sh, regno);
	default:
		return 0xffffffff;
	}
}

static void
tr_wr(struct tr_info *tr, int regno, u_int32_t data, int size)
{
	switch(size) {
	case 1:
		bus_space_write_1(tr->st, tr->sh, regno, data);
		break;
	case 2:
		bus_space_write_2(tr->st, tr->sh, regno, data);
		break;
	case 4:
		bus_space_write_4(tr->st, tr->sh, regno, data);
		break;
	}
}

/* -------------------------------------------------------------------- */
/* ac97 codec */

static int
tr_rdcd(kobj_t obj, void *devinfo, int regno)
{
	struct tr_info *tr = (struct tr_info *)devinfo;
	int i, j, treg, trw;

	switch (tr->type) {
	case SPA_PCI_ID:
		treg=SPA_REG_CODECRD;
		trw=SPA_CDC_RWSTAT;
		break;
	case ALI_PCI_ID:
		if (tr->rev > 0x01)
		  treg=TDX_REG_CODECWR;
		else
		  treg=TDX_REG_CODECRD;
		trw=TDX_CDC_RWSTAT;
		break;
	case TDX_PCI_ID:
		treg=TDX_REG_CODECRD;
		trw=TDX_CDC_RWSTAT;
		break;
	case TNX_PCI_ID:
		treg=(regno & 0x100)? TNX_REG_CODEC2RD : TNX_REG_CODEC1RD;
		trw=TNX_CDC_RWSTAT;
		break;
	default:
		printf("!!! tr_rdcd defaulted !!!\n");
		return -1;
	}

	i = j = 0;

	regno &= 0x7f;
	snd_mtxlock(tr->lock);
	if (tr->type == ALI_PCI_ID) {
		u_int32_t chk1, chk2;
		j = trw;
		for (i = TR_TIMEOUT_CDC; (i > 0) && (j & trw); i--)
			j = tr_rd(tr, treg, 4);
		if (i > 0) {
			chk1 = tr_rd(tr, 0xc8, 4);
			chk2 = tr_rd(tr, 0xc8, 4);
			for (i = TR_TIMEOUT_CDC; (i > 0) && (chk1 == chk2);
					i--)
				chk2 = tr_rd(tr, 0xc8, 4);
		}
	}
	if (tr->type != ALI_PCI_ID || i > 0) {
		tr_wr(tr, treg, regno | trw, 4);
		j=trw;
		for (i=TR_TIMEOUT_CDC; (i > 0) && (j & trw); i--)
		       	j=tr_rd(tr, treg, 4);
	}
	snd_mtxunlock(tr->lock);
	if (i == 0) printf("codec timeout during read of register %x\n", regno);
	return (j >> TR_CDC_DATA) & 0xffff;
}

static int
tr_wrcd(kobj_t obj, void *devinfo, int regno, u_int32_t data)
{
	struct tr_info *tr = (struct tr_info *)devinfo;
	int i, j, treg, trw;

	switch (tr->type) {
	case SPA_PCI_ID:
		treg=SPA_REG_CODECWR;
		trw=SPA_CDC_RWSTAT;
		break;
	case ALI_PCI_ID:
	case TDX_PCI_ID:
		treg=TDX_REG_CODECWR;
		trw=TDX_CDC_RWSTAT;
		break;
	case TNX_PCI_ID:
		treg=TNX_REG_CODECWR;
		trw=TNX_CDC_RWSTAT | ((regno & 0x100)? TNX_CDC_SEC : 0);
		break;
	default:
		printf("!!! tr_wrcd defaulted !!!");
		return -1;
	}

	i = 0;

	regno &= 0x7f;
#if 0
	printf("tr_wrcd: reg %x was %x", regno, tr_rdcd(devinfo, regno));
#endif
	j=trw;
	snd_mtxlock(tr->lock);
	if (tr->type == ALI_PCI_ID) {
		j = trw;
		for (i = TR_TIMEOUT_CDC; (i > 0) && (j & trw); i--)
			j = tr_rd(tr, treg, 4);
		if (i > 0) {
			u_int32_t chk1, chk2;
			chk1 = tr_rd(tr, 0xc8, 4);
			chk2 = tr_rd(tr, 0xc8, 4);
			for (i = TR_TIMEOUT_CDC; (i > 0) && (chk1 == chk2);
					i--)
				chk2 = tr_rd(tr, 0xc8, 4);
		}
	}
	if (tr->type != ALI_PCI_ID || i > 0) {
		for (i=TR_TIMEOUT_CDC; (i>0) && (j & trw); i--)
			j=tr_rd(tr, treg, 4);
		if (tr->type == ALI_PCI_ID && tr->rev > 0x01)
		      	trw |= 0x0100;
		tr_wr(tr, treg, (data << TR_CDC_DATA) | regno | trw, 4);
	}
#if 0
	printf(" - wrote %x, now %x\n", data, tr_rdcd(devinfo, regno));
#endif
	snd_mtxunlock(tr->lock);
	if (i==0) printf("codec timeout writing %x, data %x\n", regno, data);
	return (i > 0)? 0 : -1;
}

static kobj_method_t tr_ac97_methods[] = {
    	KOBJMETHOD(ac97_read,		tr_rdcd),
    	KOBJMETHOD(ac97_write,		tr_wrcd),
	KOBJMETHOD_END
};
AC97_DECLARE(tr_ac97);

/* -------------------------------------------------------------------- */
/* playback channel interrupts */

#if 0
static u_int32_t
tr_testint(struct tr_chinfo *ch)
{
	struct tr_info *tr = ch->parent;
	int bank, chan;

	bank = (ch->index & 0x20) ? 1 : 0;
	chan = ch->index & 0x1f;
	return tr_rd(tr, bank? TR_REG_ADDRINTB : TR_REG_ADDRINTA, 4) & (1 << chan);
}
#endif

static void
tr_clrint(struct tr_chinfo *ch)
{
	struct tr_info *tr = ch->parent;
	int bank, chan;

	bank = (ch->index & 0x20) ? 1 : 0;
	chan = ch->index & 0x1f;
	tr_wr(tr, bank? TR_REG_ADDRINTB : TR_REG_ADDRINTA, 1 << chan, 4);
}

static void
tr_enaint(struct tr_chinfo *ch, int enable)
{
	struct tr_info *tr = ch->parent;
       	u_int32_t i, reg;
	int bank, chan;

	snd_mtxlock(tr->lock);
	bank = (ch->index & 0x20) ? 1 : 0;
	chan = ch->index & 0x1f;
	reg = bank? TR_REG_INTENB : TR_REG_INTENA;

	i = tr_rd(tr, reg, 4);
	i &= ~(1 << chan);
	i |= (enable? 1 : 0) << chan;

	tr_clrint(ch);
	tr_wr(tr, reg, i, 4);
	snd_mtxunlock(tr->lock);
}

/* playback channels */

static void
tr_selch(struct tr_chinfo *ch)
{
	struct tr_info *tr = ch->parent;
	int i;

	i = tr_rd(tr, TR_REG_CIR, 4);
	i &= ~TR_CIR_MASK;
	i |= ch->index & 0x3f;
	tr_wr(tr, TR_REG_CIR, i, 4);
}

static void
tr_startch(struct tr_chinfo *ch)
{
	struct tr_info *tr = ch->parent;
	int bank, chan;

	bank = (ch->index & 0x20) ? 1 : 0;
	chan = ch->index & 0x1f;
	tr_wr(tr, bank? TR_REG_STARTB : TR_REG_STARTA, 1 << chan, 4);
}

static void
tr_stopch(struct tr_chinfo *ch)
{
	struct tr_info *tr = ch->parent;
	int bank, chan;

	bank = (ch->index & 0x20) ? 1 : 0;
	chan = ch->index & 0x1f;
	tr_wr(tr, bank? TR_REG_STOPB : TR_REG_STOPA, 1 << chan, 4);
}

static void
tr_wrch(struct tr_chinfo *ch)
{
	struct tr_info *tr = ch->parent;
	u_int32_t cr[TR_CHN_REGS], i;

	ch->gvsel 	&= 0x00000001;
	ch->fmc		&= 0x00000003;
	ch->fms		&= 0x0000000f;
	ch->ctrl	&= 0x0000000f;
	ch->pan 	&= 0x0000007f;
	ch->rvol	&= 0x0000007f;
	ch->cvol 	&= 0x0000007f;
	ch->vol		&= 0x000000ff;
	ch->ec		&= 0x00000fff;
	ch->alpha	&= 0x00000fff;
	ch->delta	&= 0x0000ffff;
	if (tr->type == ALI_PCI_ID)
		ch->lba &= ALI_MAXADDR;
	else
		ch->lba &= TR_MAXADDR;

	cr[1]=ch->lba;
	cr[3]=(ch->fmc<<14) | (ch->rvol<<7) | (ch->cvol);
	cr[4]=(ch->gvsel<<31) | (ch->pan<<24) | (ch->vol<<16) | (ch->ctrl<<12) | (ch->ec);

	switch (tr->type) {
	case SPA_PCI_ID:
	case ALI_PCI_ID:
	case TDX_PCI_ID:
		ch->cso &= 0x0000ffff;
		ch->eso &= 0x0000ffff;
		cr[0]=(ch->cso<<16) | (ch->alpha<<4) | (ch->fms);
		cr[2]=(ch->eso<<16) | (ch->delta);
		break;
	case TNX_PCI_ID:
		ch->cso &= 0x00ffffff;
		ch->eso &= 0x00ffffff;
		cr[0]=((ch->delta & 0xff)<<24) | (ch->cso);
		cr[2]=((ch->delta>>8)<<24) | (ch->eso);
		cr[3]|=(ch->alpha<<20) | (ch->fms<<16) | (ch->fmc<<14);
		break;
	}
	snd_mtxlock(tr->lock);
	tr_selch(ch);
	for (i=0; i<TR_CHN_REGS; i++)
		tr_wr(tr, TR_REG_CHNBASE+(i<<2), cr[i], 4);
	snd_mtxunlock(tr->lock);
}

static void
tr_rdch(struct tr_chinfo *ch)
{
	struct tr_info *tr = ch->parent;
	u_int32_t cr[5], i;

	snd_mtxlock(tr->lock);
	tr_selch(ch);
	for (i=0; i<5; i++)
		cr[i]=tr_rd(tr, TR_REG_CHNBASE+(i<<2), 4);
	snd_mtxunlock(tr->lock);


	if (tr->type == ALI_PCI_ID)
		ch->lba=(cr[1] & ALI_MAXADDR);
	else
		ch->lba=(cr[1] & TR_MAXADDR);
	ch->fmc=	(cr[3] & 0x0000c000) >> 14;
	ch->rvol=	(cr[3] & 0x00003f80) >> 7;
	ch->cvol=	(cr[3] & 0x0000007f);
	ch->gvsel=	(cr[4] & 0x80000000) >> 31;
	ch->pan=	(cr[4] & 0x7f000000) >> 24;
	ch->vol=	(cr[4] & 0x00ff0000) >> 16;
	ch->ctrl=	(cr[4] & 0x0000f000) >> 12;
	ch->ec=		(cr[4] & 0x00000fff);
	switch(tr->type) {
	case SPA_PCI_ID:
	case ALI_PCI_ID:
	case TDX_PCI_ID:
		ch->cso=	(cr[0] & 0xffff0000) >> 16;
		ch->alpha=	(cr[0] & 0x0000fff0) >> 4;
		ch->fms=	(cr[0] & 0x0000000f);
		ch->eso=	(cr[2] & 0xffff0000) >> 16;
		ch->delta=	(cr[2] & 0x0000ffff);
		break;
	case TNX_PCI_ID:
		ch->cso=	(cr[0] & 0x00ffffff);
		ch->eso=	(cr[2] & 0x00ffffff);
		ch->delta=	((cr[2] & 0xff000000) >> 16) | ((cr[0] & 0xff000000) >> 24);
		ch->alpha=	(cr[3] & 0xfff00000) >> 20;
		ch->fms=	(cr[3] & 0x000f0000) >> 16;
		break;
	}
}

static u_int32_t
tr_fmttobits(u_int32_t fmt)
{
	u_int32_t bits;

	bits = 0;
	bits |= (fmt & AFMT_SIGNED)? 0x2 : 0;
	bits |= (AFMT_CHANNEL(fmt) > 1)? 0x4 : 0;
	bits |= (fmt & AFMT_16BIT)? 0x8 : 0;

	return bits;
}

/* -------------------------------------------------------------------- */
/* channel interface */

static void *
trpchan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct tr_info *tr = devinfo;
	struct tr_chinfo *ch;

	KASSERT(dir == PCMDIR_PLAY, ("trpchan_init: bad direction"));
	ch = &tr->chinfo[tr->playchns];
	ch->index = tr->playchns++;
	ch->buffer = b;
	ch->parent = tr;
	ch->channel = c;
	if (sndbuf_alloc(ch->buffer, tr->parent_dmat, 0, tr->bufsz) != 0)
		return NULL;

	return ch;
}

static int
trpchan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct tr_chinfo *ch = data;

	ch->ctrl = tr_fmttobits(format) | 0x01;

	return 0;
}

static u_int32_t
trpchan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct tr_chinfo *ch = data;

	ch->delta = (speed << 12) / 48000;
	return (ch->delta * 48000) >> 12;
}

static u_int32_t
trpchan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct tr_chinfo *ch = data;

	sndbuf_resize(ch->buffer, 2, blocksize);
	return blocksize;
}

static int
trpchan_trigger(kobj_t obj, void *data, int go)
{
	struct tr_chinfo *ch = data;

	if (!PCMTRIG_COMMON(go))
		return 0;

	if (go == PCMTRIG_START) {
		ch->fmc = 3;
		ch->fms = 0;
		ch->ec = 0;
		ch->alpha = 0;
		ch->lba = sndbuf_getbufaddr(ch->buffer);
		ch->cso = 0;
		ch->eso = (sndbuf_getsize(ch->buffer) / sndbuf_getalign(ch->buffer)) - 1;
		ch->rvol = ch->cvol = 0x7f;
		ch->gvsel = 0;
		ch->pan = 0;
		ch->vol = 0;
		ch->bufhalf = 0;
   		tr_wrch(ch);
		tr_enaint(ch, 1);
		tr_startch(ch);
		ch->active = 1;
	} else {
		tr_stopch(ch);
		ch->active = 0;
	}

	return 0;
}

static u_int32_t
trpchan_getptr(kobj_t obj, void *data)
{
	struct tr_chinfo *ch = data;

	tr_rdch(ch);
	return ch->cso * sndbuf_getalign(ch->buffer);
}

static struct pcmchan_caps *
trpchan_getcaps(kobj_t obj, void *data)
{
	return &tr_playcaps;
}

static kobj_method_t trpchan_methods[] = {
    	KOBJMETHOD(channel_init,		trpchan_init),
    	KOBJMETHOD(channel_setformat,		trpchan_setformat),
    	KOBJMETHOD(channel_setspeed,		trpchan_setspeed),
    	KOBJMETHOD(channel_setblocksize,	trpchan_setblocksize),
    	KOBJMETHOD(channel_trigger,		trpchan_trigger),
    	KOBJMETHOD(channel_getptr,		trpchan_getptr),
    	KOBJMETHOD(channel_getcaps,		trpchan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(trpchan);

/* -------------------------------------------------------------------- */
/* rec channel interface */

static void *
trrchan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct tr_info *tr = devinfo;
	struct tr_rchinfo *ch;

	KASSERT(dir == PCMDIR_REC, ("trrchan_init: bad direction"));
	ch = &tr->recchinfo;
	ch->buffer = b;
	ch->parent = tr;
	ch->channel = c;
	if (sndbuf_alloc(ch->buffer, tr->parent_dmat, 0, tr->bufsz) != 0)
		return NULL;

	return ch;
}

static int
trrchan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct tr_rchinfo *ch = data;
	struct tr_info *tr = ch->parent;
	u_int32_t i, bits;

	bits = tr_fmttobits(format);
	/* set # of samples between interrupts */
	i = (sndbuf_runsz(ch->buffer) >> ((bits & 0x08)? 1 : 0)) - 1;
	tr_wr(tr, TR_REG_SBBL, i | (i << 16), 4);
	/* set sample format */
	i = 0x18 | (bits << 4);
	tr_wr(tr, TR_REG_SBCTRL, i, 1);

	return 0;
}

static u_int32_t
trrchan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct tr_rchinfo *ch = data;
	struct tr_info *tr = ch->parent;

	/* setup speed */
	ch->delta = (48000 << 12) / speed;
	tr_wr(tr, TR_REG_SBDELTA, ch->delta, 2);

	/* return closest possible speed */
	return (48000 << 12) / ch->delta;
}

static u_int32_t
trrchan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct tr_rchinfo *ch = data;

	sndbuf_resize(ch->buffer, 2, blocksize);

	return blocksize;
}

static int
trrchan_trigger(kobj_t obj, void *data, int go)
{
	struct tr_rchinfo *ch = data;
	struct tr_info *tr = ch->parent;
	u_int32_t i;

	if (!PCMTRIG_COMMON(go))
		return 0;

	if (go == PCMTRIG_START) {
		/* set up dma mode regs */
		tr_wr(tr, TR_REG_DMAR15, 0, 1);
		i = tr_rd(tr, TR_REG_DMAR11, 1) & 0x03;
		tr_wr(tr, TR_REG_DMAR11, i | 0x54, 1);
		/* set up base address */
	   	tr_wr(tr, TR_REG_DMAR0, sndbuf_getbufaddr(ch->buffer), 4);
		/* set up buffer size */
		i = tr_rd(tr, TR_REG_DMAR4, 4) & ~0x00ffffff;
		tr_wr(tr, TR_REG_DMAR4, i | (sndbuf_runsz(ch->buffer) - 1), 4);
		/* start */
		tr_wr(tr, TR_REG_SBCTRL, tr_rd(tr, TR_REG_SBCTRL, 1) | 1, 1);
		ch->active = 1;
	} else {
		tr_wr(tr, TR_REG_SBCTRL, tr_rd(tr, TR_REG_SBCTRL, 1) & ~7, 1);
		ch->active = 0;
	}

	/* return 0 if ok */
	return 0;
}

static u_int32_t
trrchan_getptr(kobj_t obj, void *data)
{
 	struct tr_rchinfo *ch = data;
	struct tr_info *tr = ch->parent;

	/* return current byte offset of channel */
	return tr_rd(tr, TR_REG_DMAR0, 4) - sndbuf_getbufaddr(ch->buffer);
}

static struct pcmchan_caps *
trrchan_getcaps(kobj_t obj, void *data)
{
	return &tr_reccaps;
}

static kobj_method_t trrchan_methods[] = {
    	KOBJMETHOD(channel_init,		trrchan_init),
    	KOBJMETHOD(channel_setformat,		trrchan_setformat),
    	KOBJMETHOD(channel_setspeed,		trrchan_setspeed),
    	KOBJMETHOD(channel_setblocksize,	trrchan_setblocksize),
    	KOBJMETHOD(channel_trigger,		trrchan_trigger),
    	KOBJMETHOD(channel_getptr,		trrchan_getptr),
    	KOBJMETHOD(channel_getcaps,		trrchan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(trrchan);

/* -------------------------------------------------------------------- */
/* The interrupt handler */

static void
tr_intr(void *p)
{
	struct tr_info *tr = (struct tr_info *)p;
	struct tr_chinfo *ch;
	u_int32_t active, mask, bufhalf, chnum, intsrc;
	int tmp;

	intsrc = tr_rd(tr, TR_REG_MISCINT, 4);
	if (intsrc & TR_INT_ADDR) {
		chnum = 0;
		while (chnum < tr->hwchns) {
			mask = 0x00000001;
			active = tr_rd(tr, (chnum < 32)? TR_REG_ADDRINTA : TR_REG_ADDRINTB, 4);
			bufhalf = tr_rd(tr, (chnum < 32)? TR_REG_CSPF_A : TR_REG_CSPF_B, 4);
			if (active) {
				do {
					if (active & mask) {
						tmp = (bufhalf & mask)? 1 : 0;
						if (chnum < tr->playchns) {
							ch = &tr->chinfo[chnum];
							/* printf("%d @ %d, ", chnum, trpchan_getptr(NULL, ch)); */
							if (ch->bufhalf != tmp) {
								chn_intr(ch->channel);
								ch->bufhalf = tmp;
							}
						}
					}
					chnum++;
					mask <<= 1;
				} while (chnum & 31);
			} else
				chnum += 32;

			tr_wr(tr, (chnum <= 32)? TR_REG_ADDRINTA : TR_REG_ADDRINTB, active, 4);
		}
	}
	if (intsrc & TR_INT_SB) {
		chn_intr(tr->recchinfo.channel);
		tr_rd(tr, TR_REG_SBR9, 1);
		tr_rd(tr, TR_REG_SBR10, 1);
	}
}

/* -------------------------------------------------------------------- */

/*
 * Probe and attach the card
 */

static int
tr_init(struct tr_info *tr)
{
	switch (tr->type) {
	case SPA_PCI_ID:
		tr_wr(tr, SPA_REG_GPIO, 0, 4);
		tr_wr(tr, SPA_REG_CODECST, SPA_RST_OFF, 4);
		break;
	case TDX_PCI_ID:
		tr_wr(tr, TDX_REG_CODECST, TDX_CDC_ON, 4);
		break;
	case TNX_PCI_ID:
		tr_wr(tr, TNX_REG_CODECST, TNX_CDC_ON, 4);
		break;
	}

	tr_wr(tr, TR_REG_CIR, TR_CIR_MIDENA | TR_CIR_ADDRENA, 4);
	return 0;
}

static int
tr_pci_probe(device_t dev)
{
	switch (pci_get_devid(dev)) {
		case SPA_PCI_ID:
			device_set_desc(dev, "SiS 7018");
			return BUS_PROBE_DEFAULT;
		case ALI_PCI_ID:
			device_set_desc(dev, "Acer Labs M5451");
			return BUS_PROBE_DEFAULT;
		case TDX_PCI_ID:
			device_set_desc(dev, "Trident 4DWave DX");
			return BUS_PROBE_DEFAULT;
		case TNX_PCI_ID:
			device_set_desc(dev, "Trident 4DWave NX");
			return BUS_PROBE_DEFAULT;
	}

	return ENXIO;
}

static int
tr_pci_attach(device_t dev)
{
	struct tr_info *tr;
	struct ac97_info *codec = NULL;
	bus_addr_t	lowaddr;
	int		i, dacn;
	char 		status[SND_STATUSLEN];
#ifdef __sparc64__
	device_t	*children;
	int		nchildren;
	u_int32_t	data;
#endif

	tr = malloc(sizeof(*tr), M_DEVBUF, M_WAITOK | M_ZERO);
	tr->type = pci_get_devid(dev);
	tr->rev = pci_get_revid(dev);
	tr->lock = snd_mtxcreate(device_get_nameunit(dev), "snd_t4dwave softc");

	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "dac", &i) == 0) {
	    	if (i < 1)
			dacn = 1;
		else if (i > TR_MAXPLAYCH)
			dacn = TR_MAXPLAYCH;
		else
			dacn = i;
	} else {
		switch (tr->type) {
		case ALI_PCI_ID:
			dacn = ALI_MAXPLAYCH;
			break;
		default:
			dacn = TR_MAXPLAYCH;
			break;
		}
	}

	pci_enable_busmaster(dev);

	tr->regid = PCIR_BAR(0);
	tr->regtype = SYS_RES_IOPORT;
	tr->reg = bus_alloc_resource_any(dev, tr->regtype, &tr->regid,
		RF_ACTIVE);
	if (tr->reg) {
		tr->st = rman_get_bustag(tr->reg);
		tr->sh = rman_get_bushandle(tr->reg);
	} else {
		device_printf(dev, "unable to map register space\n");
		goto bad;
	}

	if (tr_init(tr) == -1) {
		device_printf(dev, "unable to initialize the card\n");
		goto bad;
	}
	tr->playchns = 0;

	codec = AC97_CREATE(dev, tr, tr_ac97);
	if (codec == NULL) goto bad;
	if (mixer_init(dev, ac97_getmixerclass(), codec) == -1) goto bad;

	tr->irqid = 0;
	tr->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &tr->irqid,
				 RF_ACTIVE | RF_SHAREABLE);
	if (!tr->irq || snd_setup_intr(dev, tr->irq, 0, tr_intr, tr, &tr->ih)) {
		device_printf(dev, "unable to map interrupt\n");
		goto bad;
	}

	if (tr->type == ALI_PCI_ID) {
		/*
		 * The M5451 generates 31 bit of DMA and in order to do
		 * 32-bit DMA, the 31st bit can be set via its accompanying
		 * ISA bridge.  Note that we can't predict whether bus_dma(9)
		 * will actually supply us with a 32-bit buffer and even when
		 * using a low address of BUS_SPACE_MAXADDR_32BIT for both
		 * we might end up with the play buffer being in the 32-bit
		 * range while the record buffer isn't or vice versa. So we
		 * limit enabling the 31st bit to sparc64, where the IOMMU
		 * guarantees that we're using a 32-bit address (and in turn
		 * requires it).
		 */
		lowaddr = ALI_MAXADDR;
#ifdef __sparc64__
		if (device_get_children(device_get_parent(dev), &children,
		    &nchildren) == 0) {
			for (i = 0; i < nchildren; i++) {
				if (pci_get_devid(children[i]) == 0x153310b9) {
					lowaddr = BUS_SPACE_MAXADDR_32BIT;
					data = pci_read_config(children[i],
					    0x7e, 1);
					if (bootverbose)
						device_printf(dev,
						    "M1533 0x7e: 0x%x -> ",
						    data);
					data |= 0x1;
					if (bootverbose)
						printf("0x%x\n", data);
					pci_write_config(children[i], 0x7e,
					    data, 1);
					break;
				}
			}
		}
		free(children, M_TEMP);
#endif
		tr->hwchns = ALI_MAXHWCH;
		tr->bufsz = ALI_BUFSZ;
	} else {
		lowaddr = TR_MAXADDR;
		tr->hwchns = TR_MAXHWCH;
		tr->bufsz = pcm_getbuffersize(dev, 4096, TR_DEFAULT_BUFSZ,
		    65536);
	}

	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev),
		/*alignment*/TR_BUFALGN,
		/*boundary*/0,
		/*lowaddr*/lowaddr,
		/*highaddr*/BUS_SPACE_MAXADDR,
		/*filter*/NULL, /*filterarg*/NULL,
		/*maxsize*/tr->bufsz, /*nsegments*/1, /*maxsegz*/tr->bufsz,
		/*flags*/0, /*lockfunc*/busdma_lock_mutex,
		/*lockarg*/&Giant, &tr->parent_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		goto bad;
	}

	snprintf(status, 64, "at io 0x%jx irq %jd %s",
		 rman_get_start(tr->reg), rman_get_start(tr->irq),PCM_KLDSTRING(snd_t4dwave));

	if (pcm_register(dev, tr, dacn, 1))
		goto bad;
	pcm_addchan(dev, PCMDIR_REC, &trrchan_class, tr);
	for (i = 0; i < dacn; i++)
		pcm_addchan(dev, PCMDIR_PLAY, &trpchan_class, tr);
	pcm_setstatus(dev, status);

	return 0;

bad:
	if (codec) ac97_destroy(codec);
	if (tr->reg) bus_release_resource(dev, tr->regtype, tr->regid, tr->reg);
	if (tr->ih) bus_teardown_intr(dev, tr->irq, tr->ih);
	if (tr->irq) bus_release_resource(dev, SYS_RES_IRQ, tr->irqid, tr->irq);
	if (tr->parent_dmat) bus_dma_tag_destroy(tr->parent_dmat);
	if (tr->lock) snd_mtxfree(tr->lock);
	free(tr, M_DEVBUF);
	return ENXIO;
}

static int
tr_pci_detach(device_t dev)
{
	int r;
	struct tr_info *tr;

	r = pcm_unregister(dev);
	if (r)
		return r;

	tr = pcm_getdevinfo(dev);
	bus_release_resource(dev, tr->regtype, tr->regid, tr->reg);
	bus_teardown_intr(dev, tr->irq, tr->ih);
	bus_release_resource(dev, SYS_RES_IRQ, tr->irqid, tr->irq);
	bus_dma_tag_destroy(tr->parent_dmat);
	snd_mtxfree(tr->lock);
	free(tr, M_DEVBUF);

	return 0;
}

static int
tr_pci_suspend(device_t dev)
{
	int i;
	struct tr_info *tr;

	tr = pcm_getdevinfo(dev);

	for (i = 0; i < tr->playchns; i++) {
		tr->chinfo[i].was_active = tr->chinfo[i].active;
		if (tr->chinfo[i].active) {
			trpchan_trigger(NULL, &tr->chinfo[i], PCMTRIG_STOP);
		}
	}

	tr->recchinfo.was_active = tr->recchinfo.active;
	if (tr->recchinfo.active) {
		trrchan_trigger(NULL, &tr->recchinfo, PCMTRIG_STOP);
	}

	return 0;
}

static int
tr_pci_resume(device_t dev)
{
	int i;
	struct tr_info *tr;

	tr = pcm_getdevinfo(dev);

	if (tr_init(tr) == -1) {
		device_printf(dev, "unable to initialize the card\n");
		return ENXIO;
	}

	if (mixer_reinit(dev) == -1) {
		device_printf(dev, "unable to initialize the mixer\n");
		return ENXIO;
	}

	for (i = 0; i < tr->playchns; i++) {
		if (tr->chinfo[i].was_active) {
			trpchan_trigger(NULL, &tr->chinfo[i], PCMTRIG_START);
		}
	}

	if (tr->recchinfo.was_active) {
		trrchan_trigger(NULL, &tr->recchinfo, PCMTRIG_START);
	}

	return 0;
}

static device_method_t tr_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		tr_pci_probe),
	DEVMETHOD(device_attach,	tr_pci_attach),
	DEVMETHOD(device_detach,	tr_pci_detach),
	DEVMETHOD(device_suspend,	tr_pci_suspend),
	DEVMETHOD(device_resume,	tr_pci_resume),
	{ 0, 0 }
};

static driver_t tr_driver = {
	"pcm",
	tr_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_t4dwave, pci, tr_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_t4dwave, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_t4dwave, 1);
