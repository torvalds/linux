/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Ariff Abdullah <ariff@FreeBSD.org>
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
 * FreeBSD pcm driver for ATI IXP 150/200/250/300 AC97 controllers
 *
 * Features
 *	* 16bit playback / recording
 *	* 32bit native playback - yay!
 *	* 32bit native recording (seems broken on few hardwares)
 *
 * Issues / TODO:
 *	* SPDIF
 *	* Support for more than 2 channels.
 *	* VRA ? VRM ? DRA ?
 *	* 32bit native recording seems broken on few hardwares, most
 *	  probably because of incomplete VRA/DRA cleanup.
 *
 *
 * Thanks goes to:
 *
 *   Shaharil @ SCAN Associates whom relentlessly providing me the
 *   mind blowing Acer Ferrari 4002 WLMi with this ATI IXP hardware.
 *
 *   Reinoud Zandijk <reinoud@NetBSD.org> (auixp), which this driver is
 *   largely based upon although large part of it has been reworked. His
 *   driver is the primary reference and pretty much well documented.
 *
 *   Takashi Iwai (ALSA snd-atiixp), for register definitions and some
 *   random ninja hackery.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/ac97.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <sys/sysctl.h>
#include <sys/endian.h>

#include <dev/sound/pci/atiixp.h>

SND_DECLARE_FILE("$FreeBSD$");

#define ATI_IXP_DMA_RETRY_MAX	100

#define ATI_IXP_BUFSZ_MIN	4096
#define ATI_IXP_BUFSZ_MAX	65536
#define ATI_IXP_BUFSZ_DEFAULT	16384

#define ATI_IXP_BLK_MIN		32
#define ATI_IXP_BLK_ALIGN	(~(ATI_IXP_BLK_MIN - 1))

#define ATI_IXP_CHN_RUNNING	0x00000001
#define ATI_IXP_CHN_SUSPEND	0x00000002

struct atiixp_dma_op {
	volatile uint32_t addr;
	volatile uint16_t status;
	volatile uint16_t size;
	volatile uint32_t next;
};

struct atiixp_info;

struct atiixp_chinfo {
	struct snd_dbuf *buffer;
	struct pcm_channel *channel;
	struct atiixp_info *parent;
	struct atiixp_dma_op *sgd_table;
	bus_addr_t sgd_addr;
	uint32_t enable_bit, flush_bit, linkptr_bit, dt_cur_bit;
	uint32_t blksz, blkcnt;
	uint32_t ptr, prevptr;
	uint32_t fmt;
	uint32_t flags;
	int caps_32bit, dir;
};

struct atiixp_info {
	device_t dev;

	bus_space_tag_t st;
	bus_space_handle_t sh;
	bus_dma_tag_t parent_dmat;
	bus_dma_tag_t sgd_dmat;
	bus_dmamap_t sgd_dmamap;
	bus_addr_t sgd_addr;

	struct resource *reg, *irq;
	int regtype, regid, irqid;
	void *ih;
	struct ac97_info *codec;

	struct atiixp_chinfo pch;
	struct atiixp_chinfo rch;
	struct atiixp_dma_op *sgd_table;
	struct intr_config_hook delayed_attach;

	uint32_t bufsz;
	uint32_t codec_not_ready_bits, codec_idx, codec_found;
	uint32_t blkcnt;
	int registered_channels;

	struct mtx *lock;
	struct callout poll_timer;
	int poll_ticks, polling;
};

#define atiixp_rd(_sc, _reg)	\
		bus_space_read_4((_sc)->st, (_sc)->sh, _reg)
#define atiixp_wr(_sc, _reg, _val)	\
		bus_space_write_4((_sc)->st, (_sc)->sh, _reg, _val)

#define atiixp_lock(_sc)	snd_mtxlock((_sc)->lock)
#define atiixp_unlock(_sc)	snd_mtxunlock((_sc)->lock)
#define atiixp_assert(_sc)	snd_mtxassert((_sc)->lock)

static uint32_t atiixp_fmt_32bit[] = {
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	SND_FORMAT(AFMT_S32_LE, 2, 0),
	0
};

static uint32_t atiixp_fmt[] = {
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};

static struct pcmchan_caps atiixp_caps_32bit = {
	ATI_IXP_BASE_RATE,
	ATI_IXP_BASE_RATE,
	atiixp_fmt_32bit, 0
};

static struct pcmchan_caps atiixp_caps = {
	ATI_IXP_BASE_RATE,
	ATI_IXP_BASE_RATE,
	atiixp_fmt, 0
};

static const struct {
	uint16_t vendor;
	uint16_t devid;
	char	 *desc;
} atiixp_hw[] = {
	{ ATI_VENDOR_ID, ATI_IXP_200_ID, "ATI IXP 200" },
	{ ATI_VENDOR_ID, ATI_IXP_300_ID, "ATI IXP 300" },
	{ ATI_VENDOR_ID, ATI_IXP_400_ID, "ATI IXP 400" },
	{ ATI_VENDOR_ID, ATI_IXP_SB600_ID, "ATI IXP SB600" },
};

static void atiixp_enable_interrupts(struct atiixp_info *);
static void atiixp_disable_interrupts(struct atiixp_info *);
static void atiixp_reset_aclink(struct atiixp_info *);
static void atiixp_flush_dma(struct atiixp_chinfo *);
static void atiixp_enable_dma(struct atiixp_chinfo *);
static void atiixp_disable_dma(struct atiixp_chinfo *);

static int atiixp_waitready_codec(struct atiixp_info *);
static int atiixp_rdcd(kobj_t, void *, int);
static int atiixp_wrcd(kobj_t, void *, int, uint32_t);

static void  *atiixp_chan_init(kobj_t, void *, struct snd_dbuf *,
						struct pcm_channel *, int);
static int    atiixp_chan_setformat(kobj_t, void *, uint32_t);
static uint32_t    atiixp_chan_setspeed(kobj_t, void *, uint32_t);
static int         atiixp_chan_setfragments(kobj_t, void *, uint32_t, uint32_t);
static uint32_t    atiixp_chan_setblocksize(kobj_t, void *, uint32_t);
static void   atiixp_buildsgdt(struct atiixp_chinfo *);
static int    atiixp_chan_trigger(kobj_t, void *, int);
static __inline uint32_t atiixp_dmapos(struct atiixp_chinfo *);
static uint32_t          atiixp_chan_getptr(kobj_t, void *);
static struct pcmchan_caps *atiixp_chan_getcaps(kobj_t, void *);

static void atiixp_intr(void *);
static void atiixp_dma_cb(void *, bus_dma_segment_t *, int, int);
static void atiixp_chip_pre_init(struct atiixp_info *);
static void atiixp_chip_post_init(void *);
static void atiixp_release_resource(struct atiixp_info *);
static int  atiixp_pci_probe(device_t);
static int  atiixp_pci_attach(device_t);
static int  atiixp_pci_detach(device_t);
static int  atiixp_pci_suspend(device_t);
static int  atiixp_pci_resume(device_t);

/*
 * ATI IXP helper functions
 */
static void
atiixp_enable_interrupts(struct atiixp_info *sc)
{
	uint32_t value;

	/* clear all pending */
	atiixp_wr(sc, ATI_REG_ISR, 0xffffffff);

	/* enable all relevant interrupt sources we can handle */
	value = atiixp_rd(sc, ATI_REG_IER);

	value |= ATI_REG_IER_IO_STATUS_EN;

	/*
	 * Disable / ignore internal xrun/spdf interrupt flags
	 * since it doesn't interest us (for now).
	 */
#if 1
	value &= ~(ATI_REG_IER_IN_XRUN_EN | ATI_REG_IER_OUT_XRUN_EN |
	    ATI_REG_IER_SPDF_XRUN_EN | ATI_REG_IER_SPDF_STATUS_EN);
#else
	value |= ATI_REG_IER_IN_XRUN_EN;
	value |= ATI_REG_IER_OUT_XRUN_EN;

	value |= ATI_REG_IER_SPDF_XRUN_EN;
	value |= ATI_REG_IER_SPDF_STATUS_EN;
#endif

	atiixp_wr(sc, ATI_REG_IER, value);
}

static void
atiixp_disable_interrupts(struct atiixp_info *sc)
{
	/* disable all interrupt sources */
	atiixp_wr(sc, ATI_REG_IER, 0);

	/* clear all pending */
	atiixp_wr(sc, ATI_REG_ISR, 0xffffffff);
}

static void
atiixp_reset_aclink(struct atiixp_info *sc)
{
	uint32_t value, timeout;

	/* if power is down, power it up */
	value = atiixp_rd(sc, ATI_REG_CMD);
	if (value & ATI_REG_CMD_POWERDOWN) {
		/* explicitly enable power */
		value &= ~ATI_REG_CMD_POWERDOWN;
		atiixp_wr(sc, ATI_REG_CMD, value);

		/* have to wait at least 10 usec for it to initialise */
		DELAY(20);
	}

	/* perform a soft reset */
	value  = atiixp_rd(sc, ATI_REG_CMD);
	value |= ATI_REG_CMD_AC_SOFT_RESET;
	atiixp_wr(sc, ATI_REG_CMD, value);

	/* need to read the CMD reg and wait aprox. 10 usec to init */
	value  = atiixp_rd(sc, ATI_REG_CMD);
	DELAY(20);

	/* clear soft reset flag again */
	value  = atiixp_rd(sc, ATI_REG_CMD);
	value &= ~ATI_REG_CMD_AC_SOFT_RESET;
	atiixp_wr(sc, ATI_REG_CMD, value);

	/* check if the ac-link is working; reset device otherwise */
	timeout = 10;
	value = atiixp_rd(sc, ATI_REG_CMD);
	while (!(value & ATI_REG_CMD_ACLINK_ACTIVE) && --timeout) {
#if 0
		device_printf(sc->dev, "not up; resetting aclink hardware\n");
#endif

		/* dip aclink reset but keep the acsync */
		value &= ~ATI_REG_CMD_AC_RESET;
		value |=  ATI_REG_CMD_AC_SYNC;
		atiixp_wr(sc, ATI_REG_CMD, value);

		/* need to read CMD again and wait again (clocking in issue?) */
		value = atiixp_rd(sc, ATI_REG_CMD);
		DELAY(20);

		/* assert aclink reset again */
		value = atiixp_rd(sc, ATI_REG_CMD);
		value |=  ATI_REG_CMD_AC_RESET;
		atiixp_wr(sc, ATI_REG_CMD, value);

		/* check if its active now */
		value = atiixp_rd(sc, ATI_REG_CMD);
	}

	if (timeout == 0)
		device_printf(sc->dev, "giving up aclink reset\n");
#if 0
	if (timeout != 10)
		device_printf(sc->dev, "aclink hardware reset successful\n");
#endif

	/* assert reset and sync for safety */
	value  = atiixp_rd(sc, ATI_REG_CMD);
	value |= ATI_REG_CMD_AC_SYNC | ATI_REG_CMD_AC_RESET;
	atiixp_wr(sc, ATI_REG_CMD, value);
}

static void
atiixp_flush_dma(struct atiixp_chinfo *ch)
{
	atiixp_wr(ch->parent, ATI_REG_FIFO_FLUSH, ch->flush_bit);
}

static void
atiixp_enable_dma(struct atiixp_chinfo *ch)
{
	uint32_t value;

	value = atiixp_rd(ch->parent, ATI_REG_CMD);
	if (!(value & ch->enable_bit)) {
		value |= ch->enable_bit;
		atiixp_wr(ch->parent, ATI_REG_CMD, value);
	}
}

static void
atiixp_disable_dma(struct atiixp_chinfo *ch)
{
	uint32_t value;

	value = atiixp_rd(ch->parent, ATI_REG_CMD);
	if (value & ch->enable_bit) {
		value &= ~ch->enable_bit;
		atiixp_wr(ch->parent, ATI_REG_CMD, value);
	}
}

/*
 * AC97 interface
 */
static int
atiixp_waitready_codec(struct atiixp_info *sc)
{
	int timeout = 500;

	do {
		if ((atiixp_rd(sc, ATI_REG_PHYS_OUT_ADDR) &
		    ATI_REG_PHYS_OUT_ADDR_EN) == 0)
			return (0);
		DELAY(1);
	} while (--timeout);

	return (-1);
}

static int
atiixp_rdcd(kobj_t obj, void *devinfo, int reg)
{
	struct atiixp_info *sc = devinfo;
	uint32_t data;
	int timeout;

	if (atiixp_waitready_codec(sc))
		return (-1);

	data = (reg << ATI_REG_PHYS_OUT_ADDR_SHIFT) |
	    ATI_REG_PHYS_OUT_ADDR_EN | ATI_REG_PHYS_OUT_RW | sc->codec_idx;

	atiixp_wr(sc, ATI_REG_PHYS_OUT_ADDR, data);

	if (atiixp_waitready_codec(sc))
		return (-1);

	timeout = 500;
	do {
		data = atiixp_rd(sc, ATI_REG_PHYS_IN_ADDR);
		if (data & ATI_REG_PHYS_IN_READ_FLAG)
			return (data >> ATI_REG_PHYS_IN_DATA_SHIFT);
		DELAY(1);
	} while (--timeout);

	if (reg < 0x7c)
		device_printf(sc->dev, "codec read timeout! (reg 0x%x)\n", reg);

	return (-1);
}

static int
atiixp_wrcd(kobj_t obj, void *devinfo, int reg, uint32_t data)
{
	struct atiixp_info *sc = devinfo;

	if (atiixp_waitready_codec(sc))
		return (-1);

	data = (data << ATI_REG_PHYS_OUT_DATA_SHIFT) |
	    (((uint32_t)reg) << ATI_REG_PHYS_OUT_ADDR_SHIFT) |
	    ATI_REG_PHYS_OUT_ADDR_EN | sc->codec_idx;

	atiixp_wr(sc, ATI_REG_PHYS_OUT_ADDR, data);

	return (0);
}

static kobj_method_t atiixp_ac97_methods[] = {
	KOBJMETHOD(ac97_read,		atiixp_rdcd),
	KOBJMETHOD(ac97_write,		atiixp_wrcd),
	KOBJMETHOD_END
};
AC97_DECLARE(atiixp_ac97);

/*
 * Playback / Record channel interface
 */
static void *
atiixp_chan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
					struct pcm_channel *c, int dir)
{
	struct atiixp_info *sc = devinfo;
	struct atiixp_chinfo *ch;
	int num;

	atiixp_lock(sc);

	if (dir == PCMDIR_PLAY) {
		ch = &sc->pch;
		ch->linkptr_bit = ATI_REG_OUT_DMA_LINKPTR;
		ch->enable_bit = ATI_REG_CMD_OUT_DMA_EN | ATI_REG_CMD_SEND_EN;
		ch->flush_bit = ATI_REG_FIFO_OUT_FLUSH;
		ch->dt_cur_bit = ATI_REG_OUT_DMA_DT_CUR;
		/* Native 32bit playback working properly */
		ch->caps_32bit = 1;
	} else {
		ch = &sc->rch;
		ch->linkptr_bit = ATI_REG_IN_DMA_LINKPTR;
		ch->enable_bit = ATI_REG_CMD_IN_DMA_EN | ATI_REG_CMD_RECEIVE_EN;
		ch->flush_bit = ATI_REG_FIFO_IN_FLUSH;
		ch->dt_cur_bit = ATI_REG_IN_DMA_DT_CUR;
		/* XXX Native 32bit recording appear to be broken */
		ch->caps_32bit = 1;
	}

	ch->buffer = b;
	ch->parent = sc;
	ch->channel = c;
	ch->dir = dir;
	ch->blkcnt = sc->blkcnt;
	ch->blksz = sc->bufsz / ch->blkcnt;

	atiixp_unlock(sc);

	if (sndbuf_alloc(ch->buffer, sc->parent_dmat, 0, sc->bufsz) == -1)
		return (NULL);

	atiixp_lock(sc);
	num = sc->registered_channels++;
	ch->sgd_table = &sc->sgd_table[num * ATI_IXP_DMA_CHSEGS_MAX];
	ch->sgd_addr = sc->sgd_addr + (num * ATI_IXP_DMA_CHSEGS_MAX *
	    sizeof(struct atiixp_dma_op));
	atiixp_disable_dma(ch);
	atiixp_unlock(sc);

	return (ch);
}

static int
atiixp_chan_setformat(kobj_t obj, void *data, uint32_t format)
{
	struct atiixp_chinfo *ch = data;
	struct atiixp_info *sc = ch->parent;
	uint32_t value;

	atiixp_lock(sc);
	if (ch->dir == PCMDIR_REC) {
		value = atiixp_rd(sc, ATI_REG_CMD);
		value &= ~ATI_REG_CMD_INTERLEAVE_IN;
		if ((format & AFMT_32BIT) == 0)
			value |= ATI_REG_CMD_INTERLEAVE_IN;
		atiixp_wr(sc, ATI_REG_CMD, value);
	} else {
		value = atiixp_rd(sc, ATI_REG_OUT_DMA_SLOT);
		value &= ~ATI_REG_OUT_DMA_SLOT_MASK;
		/* We do not have support for more than 2 channels, _yet_. */
		value |= ATI_REG_OUT_DMA_SLOT_BIT(3) |
		    ATI_REG_OUT_DMA_SLOT_BIT(4);
		value |= 0x04 << ATI_REG_OUT_DMA_THRESHOLD_SHIFT;
		atiixp_wr(sc, ATI_REG_OUT_DMA_SLOT, value);
		value = atiixp_rd(sc, ATI_REG_CMD);
		value &= ~ATI_REG_CMD_INTERLEAVE_OUT;
		if ((format & AFMT_32BIT) == 0)
			value |= ATI_REG_CMD_INTERLEAVE_OUT;
		atiixp_wr(sc, ATI_REG_CMD, value);
		value = atiixp_rd(sc, ATI_REG_6CH_REORDER);
		value &= ~ATI_REG_6CH_REORDER_EN;
		atiixp_wr(sc, ATI_REG_6CH_REORDER, value);
	}
	ch->fmt = format;
	atiixp_unlock(sc);

	return (0);
}

static uint32_t
atiixp_chan_setspeed(kobj_t obj, void *data, uint32_t spd)
{
	/* XXX We're supposed to do VRA/DRA processing right here */
	return (ATI_IXP_BASE_RATE);
}

static int
atiixp_chan_setfragments(kobj_t obj, void *data,
					uint32_t blksz, uint32_t blkcnt)
{
	struct atiixp_chinfo *ch = data;
	struct atiixp_info *sc = ch->parent;

	blksz &= ATI_IXP_BLK_ALIGN;

	if (blksz > (sndbuf_getmaxsize(ch->buffer) / ATI_IXP_DMA_CHSEGS_MIN))
		blksz = sndbuf_getmaxsize(ch->buffer) / ATI_IXP_DMA_CHSEGS_MIN;
	if (blksz < ATI_IXP_BLK_MIN)
		blksz = ATI_IXP_BLK_MIN;
	if (blkcnt > ATI_IXP_DMA_CHSEGS_MAX)
		blkcnt = ATI_IXP_DMA_CHSEGS_MAX;
	if (blkcnt < ATI_IXP_DMA_CHSEGS_MIN)
		blkcnt = ATI_IXP_DMA_CHSEGS_MIN;

	while ((blksz * blkcnt) > sndbuf_getmaxsize(ch->buffer)) {
		if ((blkcnt >> 1) >= ATI_IXP_DMA_CHSEGS_MIN)
			blkcnt >>= 1;
		else if ((blksz >> 1) >= ATI_IXP_BLK_MIN)
			blksz >>= 1;
		else
			break;
	}

	if ((sndbuf_getblksz(ch->buffer) != blksz ||
	    sndbuf_getblkcnt(ch->buffer) != blkcnt) &&
	    sndbuf_resize(ch->buffer, blkcnt, blksz) != 0)
		device_printf(sc->dev, "%s: failed blksz=%u blkcnt=%u\n",
		    __func__, blksz, blkcnt);

	ch->blksz = sndbuf_getblksz(ch->buffer);
	ch->blkcnt = sndbuf_getblkcnt(ch->buffer);

	return (0);
}

static uint32_t
atiixp_chan_setblocksize(kobj_t obj, void *data, uint32_t blksz)
{
	struct atiixp_chinfo *ch = data;
	struct atiixp_info *sc = ch->parent;

	atiixp_chan_setfragments(obj, data, blksz, sc->blkcnt);

	return (ch->blksz);
}

static void
atiixp_buildsgdt(struct atiixp_chinfo *ch)
{
	struct atiixp_info *sc = ch->parent;
	uint32_t addr, blksz, blkcnt;
	int i;

	addr = sndbuf_getbufaddr(ch->buffer);

	if (sc->polling != 0) {
		blksz = ch->blksz * ch->blkcnt;
		blkcnt = 1;
	} else {
		blksz = ch->blksz;
		blkcnt = ch->blkcnt;
	}

	for (i = 0; i < blkcnt; i++) {
		ch->sgd_table[i].addr = htole32(addr + (i * blksz));
		ch->sgd_table[i].status = htole16(0);
		ch->sgd_table[i].size = htole16(blksz >> 2);
		ch->sgd_table[i].next = htole32((uint32_t)ch->sgd_addr +
		    (((i + 1) % blkcnt) * sizeof(struct atiixp_dma_op)));
	}
}

static __inline uint32_t
atiixp_dmapos(struct atiixp_chinfo *ch)
{
	struct atiixp_info *sc = ch->parent;
	uint32_t reg, addr, sz, retry;
	volatile uint32_t ptr;

	reg = ch->dt_cur_bit;
	addr = sndbuf_getbufaddr(ch->buffer);
	sz = ch->blkcnt * ch->blksz;
	retry = ATI_IXP_DMA_RETRY_MAX;

	do {
		ptr = atiixp_rd(sc, reg);
		if (ptr < addr)
			continue;
		ptr -= addr;
		if (ptr < sz) {
#if 0
#ifdef ATI_IXP_DEBUG
			if ((ptr & ~(ch->blksz - 1)) != ch->ptr) {
				uint32_t delta;

				delta = (sz + ptr - ch->prevptr) % sz;
#ifndef ATI_IXP_DEBUG_VERBOSE
				if (delta < ch->blksz)
#endif
					device_printf(sc->dev,
						"PCMDIR_%s: incoherent DMA "
						"prevptr=%u ptr=%u "
						"ptr=%u blkcnt=%u "
						"[delta=%u != blksz=%u] "
						"(%s)\n",
						(ch->dir == PCMDIR_PLAY) ?
						"PLAY" : "REC",
						ch->prevptr, ptr,
						ch->ptr, ch->blkcnt,
						delta, ch->blksz,
						(delta < ch->blksz) ?
						"OVERLAPPED!" : "Ok");
				ch->ptr = ptr & ~(ch->blksz - 1);
			}
			ch->prevptr = ptr;
#endif
#endif
			return (ptr);
		}
	} while (--retry);

	device_printf(sc->dev, "PCMDIR_%s: invalid DMA pointer ptr=%u\n",
	    (ch->dir == PCMDIR_PLAY) ? "PLAY" : "REC", ptr);

	return (0);
}

static __inline int
atiixp_poll_channel(struct atiixp_chinfo *ch)
{
	uint32_t sz, delta;
	volatile uint32_t ptr;

	if (!(ch->flags & ATI_IXP_CHN_RUNNING))
		return (0);

	sz = ch->blksz * ch->blkcnt;
	ptr = atiixp_dmapos(ch);
	ch->ptr = ptr;
	ptr %= sz;
	ptr &= ~(ch->blksz - 1);
	delta = (sz + ptr - ch->prevptr) % sz;

	if (delta < ch->blksz)
		return (0);

	ch->prevptr = ptr;

	return (1);
}

#define atiixp_chan_active(sc)	(((sc)->pch.flags | (sc)->rch.flags) &	\
				 ATI_IXP_CHN_RUNNING)

static void
atiixp_poll_callback(void *arg)
{
	struct atiixp_info *sc = arg;
	uint32_t trigger = 0;

	if (sc == NULL)
		return;

	atiixp_lock(sc);
	if (sc->polling == 0 || atiixp_chan_active(sc) == 0) {
		atiixp_unlock(sc);
		return;
	}

	trigger |= (atiixp_poll_channel(&sc->pch) != 0) ? 1 : 0;
	trigger |= (atiixp_poll_channel(&sc->rch) != 0) ? 2 : 0;

	/* XXX */
	callout_reset(&sc->poll_timer, 1/*sc->poll_ticks*/,
	    atiixp_poll_callback, sc);

	atiixp_unlock(sc);

	if (trigger & 1)
		chn_intr(sc->pch.channel);
	if (trigger & 2)
		chn_intr(sc->rch.channel);
}

static int
atiixp_chan_trigger(kobj_t obj, void *data, int go)
{
	struct atiixp_chinfo *ch = data;
	struct atiixp_info *sc = ch->parent;
	uint32_t value;
	int pollticks;

	if (!PCMTRIG_COMMON(go))
		return (0);

	atiixp_lock(sc);

	switch (go) {
	case PCMTRIG_START:
		atiixp_flush_dma(ch);
		atiixp_buildsgdt(ch);
		atiixp_wr(sc, ch->linkptr_bit, 0);
		atiixp_enable_dma(ch);
		atiixp_wr(sc, ch->linkptr_bit,
		    (uint32_t)ch->sgd_addr | ATI_REG_LINKPTR_EN);
		if (sc->polling != 0) {
			ch->ptr = 0;
			ch->prevptr = 0;
			pollticks = ((uint64_t)hz * ch->blksz) /
			    ((uint64_t)sndbuf_getalign(ch->buffer) *
			    sndbuf_getspd(ch->buffer));
			pollticks >>= 2;
			if (pollticks > hz)
				pollticks = hz;
			if (pollticks < 1)
				pollticks = 1;
			if (atiixp_chan_active(sc) == 0 ||
			    pollticks < sc->poll_ticks) {
			    	if (bootverbose) {
					if (atiixp_chan_active(sc) == 0)
						device_printf(sc->dev,
						    "%s: pollticks=%d\n",
						    __func__, pollticks);
					else
						device_printf(sc->dev,
						    "%s: pollticks %d -> %d\n",
						    __func__, sc->poll_ticks,
						    pollticks);
				}
				sc->poll_ticks = pollticks;
				callout_reset(&sc->poll_timer, 1,
				    atiixp_poll_callback, sc);
			}
		}
		ch->flags |= ATI_IXP_CHN_RUNNING;
		break;
	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		atiixp_disable_dma(ch);
		atiixp_flush_dma(ch);
		ch->flags &= ~ATI_IXP_CHN_RUNNING;
		if (sc->polling != 0) {
			if (atiixp_chan_active(sc) == 0) {
				callout_stop(&sc->poll_timer);
				sc->poll_ticks = 1;
			} else {
				if (sc->pch.flags & ATI_IXP_CHN_RUNNING)
					ch = &sc->pch;
				else
					ch = &sc->rch;
				pollticks = ((uint64_t)hz * ch->blksz) /
				    ((uint64_t)sndbuf_getalign(ch->buffer) *
				    sndbuf_getspd(ch->buffer));
				pollticks >>= 2;
				if (pollticks > hz)
					pollticks = hz;
				if (pollticks < 1)
					pollticks = 1;
				if (pollticks > sc->poll_ticks) {
					if (bootverbose)
						device_printf(sc->dev,
						    "%s: pollticks %d -> %d\n",
						    __func__, sc->poll_ticks,
						    pollticks);
					sc->poll_ticks = pollticks;
					callout_reset(&sc->poll_timer,
					    1, atiixp_poll_callback,
					    sc);
				}
			}
		}
		break;
	default:
		atiixp_unlock(sc);
		return (0);
		break;
	}

	/* Update bus busy status */
	value = atiixp_rd(sc, ATI_REG_IER);
	if (atiixp_rd(sc, ATI_REG_CMD) & (ATI_REG_CMD_SEND_EN |
	    ATI_REG_CMD_RECEIVE_EN | ATI_REG_CMD_SPDF_OUT_EN))
		value |= ATI_REG_IER_SET_BUS_BUSY;
	else
		value &= ~ATI_REG_IER_SET_BUS_BUSY;
	atiixp_wr(sc, ATI_REG_IER, value);

	atiixp_unlock(sc);

	return (0);
}

static uint32_t
atiixp_chan_getptr(kobj_t obj, void *data)
{
	struct atiixp_chinfo *ch = data;
	struct atiixp_info *sc = ch->parent;
	uint32_t ptr;

	atiixp_lock(sc);
	if (sc->polling != 0)
		ptr = ch->ptr;
	else
		ptr = atiixp_dmapos(ch);
	atiixp_unlock(sc);

	return (ptr);
}

static struct pcmchan_caps *
atiixp_chan_getcaps(kobj_t obj, void *data)
{
	struct atiixp_chinfo *ch = data;

	if (ch->caps_32bit)
		return (&atiixp_caps_32bit);
	return (&atiixp_caps);
}

static kobj_method_t atiixp_chan_methods[] = {
	KOBJMETHOD(channel_init,		atiixp_chan_init),
	KOBJMETHOD(channel_setformat,		atiixp_chan_setformat),
	KOBJMETHOD(channel_setspeed,		atiixp_chan_setspeed),
	KOBJMETHOD(channel_setblocksize,	atiixp_chan_setblocksize),
	KOBJMETHOD(channel_setfragments,	atiixp_chan_setfragments),
	KOBJMETHOD(channel_trigger,		atiixp_chan_trigger),
	KOBJMETHOD(channel_getptr,		atiixp_chan_getptr),
	KOBJMETHOD(channel_getcaps,		atiixp_chan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(atiixp_chan);

/*
 * PCI driver interface
 */
static void
atiixp_intr(void *p)
{
	struct atiixp_info *sc = p;
	uint32_t status, enable, detected_codecs;
	uint32_t trigger = 0;

	atiixp_lock(sc);
	if (sc->polling != 0) {
		atiixp_unlock(sc);
		return;
	}
	status = atiixp_rd(sc, ATI_REG_ISR);

	if (status == 0) {
		atiixp_unlock(sc);
		return;
	}

	if ((status & ATI_REG_ISR_OUT_STATUS) &&
	    (sc->pch.flags & ATI_IXP_CHN_RUNNING))
		trigger |= 1;
	if ((status & ATI_REG_ISR_IN_STATUS) &&
	    (sc->rch.flags & ATI_IXP_CHN_RUNNING))
		trigger |= 2;

#if 0
	if (status & ATI_REG_ISR_IN_XRUN) {
		device_printf(sc->dev,
			"Recieve IN XRUN interrupt\n");
	}
	if (status & ATI_REG_ISR_OUT_XRUN) {
		device_printf(sc->dev,
			"Recieve OUT XRUN interrupt\n");
	}
#endif

	if (status & CODEC_CHECK_BITS) {
		/* mark missing codecs as not ready */
		detected_codecs = status & CODEC_CHECK_BITS;
		sc->codec_not_ready_bits |= detected_codecs;

		/* disable detected interrupt sources */
		enable  = atiixp_rd(sc, ATI_REG_IER);
		enable &= ~detected_codecs;
		atiixp_wr(sc, ATI_REG_IER, enable);
		wakeup(sc);
	}

	/* acknowledge */
	atiixp_wr(sc, ATI_REG_ISR, status);
	atiixp_unlock(sc);

	if (trigger & 1)
		chn_intr(sc->pch.channel);
	if (trigger & 2)
		chn_intr(sc->rch.channel);
}

static void
atiixp_dma_cb(void *p, bus_dma_segment_t *bds, int a, int b)
{
	struct atiixp_info *sc = (struct atiixp_info *)p;
	sc->sgd_addr = bds->ds_addr;
}

static void
atiixp_chip_pre_init(struct atiixp_info *sc)
{
	uint32_t value;

	atiixp_lock(sc);

	/* disable interrupts */
	atiixp_disable_interrupts(sc);

	/* clear all DMA enables (preserving rest of settings) */
	value = atiixp_rd(sc, ATI_REG_CMD);
	value &= ~(ATI_REG_CMD_IN_DMA_EN | ATI_REG_CMD_OUT_DMA_EN |
	    ATI_REG_CMD_SPDF_OUT_EN );
	atiixp_wr(sc, ATI_REG_CMD, value);

	/* reset aclink */
	atiixp_reset_aclink(sc);

	sc->codec_not_ready_bits = 0;

	/* enable all codecs to interrupt as well as the new frame interrupt */
	atiixp_wr(sc, ATI_REG_IER, CODEC_CHECK_BITS);

	atiixp_unlock(sc);
}

static int
sysctl_atiixp_polling(SYSCTL_HANDLER_ARGS)
{
	struct atiixp_info *sc;
	device_t dev;
	int err, val;

	dev = oidp->oid_arg1;
	sc = pcm_getdevinfo(dev);
	if (sc == NULL)
		return (EINVAL);
	atiixp_lock(sc);
	val = sc->polling;
	atiixp_unlock(sc);
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err || req->newptr == NULL)
		return (err);
	if (val < 0 || val > 1)
		return (EINVAL);

	atiixp_lock(sc);
	if (val != sc->polling) {
		if (atiixp_chan_active(sc) != 0)
			err = EBUSY;
		else if (val == 0) {
			atiixp_enable_interrupts(sc);
			sc->polling = 0;
			DELAY(1000);
		} else {
			atiixp_disable_interrupts(sc);
			sc->polling = 1;
			DELAY(1000);
		}
	}
	atiixp_unlock(sc);

	return (err);
}

static void
atiixp_chip_post_init(void *arg)
{
	struct atiixp_info *sc = (struct atiixp_info *)arg;
	uint32_t subdev;
	int i, timeout, found, polling;
	char status[SND_STATUSLEN];

	atiixp_lock(sc);

	if (sc->delayed_attach.ich_func) {
		config_intrhook_disestablish(&sc->delayed_attach);
		sc->delayed_attach.ich_func = NULL;
	}

	polling = sc->polling;
	sc->polling = 0;

	timeout = 10;
	if (sc->codec_not_ready_bits == 0) {
		/* wait for the interrupts to happen */
		do {
			msleep(sc, sc->lock, PWAIT, "ixpslp", max(hz / 10, 1));
			if (sc->codec_not_ready_bits != 0)
				break;
		} while (--timeout);
	}

	sc->polling = polling;
	atiixp_disable_interrupts(sc);

	if (sc->codec_not_ready_bits == 0 && timeout == 0) {
		device_printf(sc->dev,
			"WARNING: timeout during codec detection; "
			"codecs might be present but haven't interrupted\n");
		atiixp_unlock(sc);
		goto postinitbad;
	}

	found = 0;

	/*
	 * ATI IXP can have upto 3 codecs, but single codec should be
	 * suffice for now.
	 */
	if (!(sc->codec_not_ready_bits & ATI_REG_ISR_CODEC0_NOT_READY)) {
		/* codec 0 present */
		sc->codec_found++;
		sc->codec_idx = 0;
		found++;
	}

	if (!(sc->codec_not_ready_bits & ATI_REG_ISR_CODEC1_NOT_READY)) {
		/* codec 1 present */
		sc->codec_found++;
	}

	if (!(sc->codec_not_ready_bits & ATI_REG_ISR_CODEC2_NOT_READY)) {
		/* codec 2 present */
		sc->codec_found++;
	}

	atiixp_unlock(sc);

	if (found == 0)
		goto postinitbad;

	/* create/init mixer */
	sc->codec = AC97_CREATE(sc->dev, sc, atiixp_ac97);
	if (sc->codec == NULL)
		goto postinitbad;

	subdev = (pci_get_subdevice(sc->dev) << 16) |
	    pci_get_subvendor(sc->dev);
	switch (subdev) {
	case 0x11831043:	/* ASUS A6R */
	case 0x2043161f:	/* Maxselect x710s - http://maxselect.ru/ */
		ac97_setflags(sc->codec, ac97_getflags(sc->codec) |
		    AC97_F_EAPD_INV);
		break;
	default:
		break;
	}

	mixer_init(sc->dev, ac97_getmixerclass(), sc->codec);

	if (pcm_register(sc->dev, sc, ATI_IXP_NPCHAN, ATI_IXP_NRCHAN))
		goto postinitbad;

	for (i = 0; i < ATI_IXP_NPCHAN; i++)
		pcm_addchan(sc->dev, PCMDIR_PLAY, &atiixp_chan_class, sc);
	for (i = 0; i < ATI_IXP_NRCHAN; i++)
		pcm_addchan(sc->dev, PCMDIR_REC, &atiixp_chan_class, sc);

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(sc->dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev)), OID_AUTO,
	    "polling", CTLTYPE_INT | CTLFLAG_RW, sc->dev, sizeof(sc->dev),
	    sysctl_atiixp_polling, "I", "Enable polling mode");

	snprintf(status, SND_STATUSLEN, "at memory 0x%jx irq %jd %s",
	    rman_get_start(sc->reg), rman_get_start(sc->irq),
	    PCM_KLDSTRING(snd_atiixp));

	pcm_setstatus(sc->dev, status);

	atiixp_lock(sc);
	if (sc->polling == 0)
		atiixp_enable_interrupts(sc);
	atiixp_unlock(sc);

	return;

postinitbad:
	atiixp_release_resource(sc);
}

static void
atiixp_release_resource(struct atiixp_info *sc)
{
	if (sc == NULL)
		return;
	if (sc->registered_channels != 0) {
		atiixp_lock(sc);
		sc->polling = 0;
		callout_stop(&sc->poll_timer);
		atiixp_unlock(sc);
		callout_drain(&sc->poll_timer);
	}
	if (sc->codec) {
		ac97_destroy(sc->codec);
		sc->codec = NULL;
	}
	if (sc->ih) {
		bus_teardown_intr(sc->dev, sc->irq, sc->ih);
		sc->ih = NULL;
	}
	if (sc->reg) {
		bus_release_resource(sc->dev, sc->regtype, sc->regid, sc->reg);
		sc->reg = NULL;
	}
	if (sc->irq) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, sc->irqid, sc->irq);
		sc->irq = NULL;
	}
	if (sc->parent_dmat) {
		bus_dma_tag_destroy(sc->parent_dmat);
		sc->parent_dmat = NULL;
	}
	if (sc->sgd_addr) {
		bus_dmamap_unload(sc->sgd_dmat, sc->sgd_dmamap);
		sc->sgd_addr = 0;
	}
	if (sc->sgd_table) {
		bus_dmamem_free(sc->sgd_dmat, sc->sgd_table, sc->sgd_dmamap);
		sc->sgd_table = NULL;
	}
	if (sc->sgd_dmat) {
		bus_dma_tag_destroy(sc->sgd_dmat);
		sc->sgd_dmat = NULL;
	}
	if (sc->lock) {
		snd_mtxfree(sc->lock);
		sc->lock = NULL;
	}
	free(sc, M_DEVBUF);
}

static int
atiixp_pci_probe(device_t dev)
{
	int i;
	uint16_t devid, vendor;

	vendor = pci_get_vendor(dev);
	devid = pci_get_device(dev);
	for (i = 0; i < sizeof(atiixp_hw) / sizeof(atiixp_hw[0]); i++) {
		if (vendor == atiixp_hw[i].vendor &&
		    devid == atiixp_hw[i].devid) {
			device_set_desc(dev, atiixp_hw[i].desc);
			return (BUS_PROBE_DEFAULT);
		}
	}

	return (ENXIO);
}

static int
atiixp_pci_attach(device_t dev)
{
	struct atiixp_info *sc;
	int i;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->lock = snd_mtxcreate(device_get_nameunit(dev), "snd_atiixp softc");
	sc->dev = dev;

	callout_init(&sc->poll_timer, 1);
	sc->poll_ticks = 1;

	if (resource_int_value(device_get_name(sc->dev),
	    device_get_unit(sc->dev), "polling", &i) == 0 && i != 0)
		sc->polling = 1;
	else
		sc->polling = 0;

	pci_enable_busmaster(dev);

	sc->regid = PCIR_BAR(0);
	sc->regtype = SYS_RES_MEMORY;
	sc->reg = bus_alloc_resource_any(dev, sc->regtype,
	    &sc->regid, RF_ACTIVE);

	if (!sc->reg) {
		device_printf(dev, "unable to allocate register space\n");
		goto bad;
	}

	sc->st = rman_get_bustag(sc->reg);
	sc->sh = rman_get_bushandle(sc->reg);

	sc->bufsz = pcm_getbuffersize(dev, ATI_IXP_BUFSZ_MIN,
	    ATI_IXP_BUFSZ_DEFAULT, ATI_IXP_BUFSZ_MAX);

	sc->irqid = 0;
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irqid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (!sc->irq || snd_setup_intr(dev, sc->irq, INTR_MPSAFE,
	    atiixp_intr, sc, &sc->ih)) {
		device_printf(dev, "unable to map interrupt\n");
		goto bad;
	}

	/*
	 * Let the user choose the best DMA segments.
	 */
	if (resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "blocksize", &i) == 0 && i > 0) {
		i &= ATI_IXP_BLK_ALIGN;
		if (i < ATI_IXP_BLK_MIN)
			i = ATI_IXP_BLK_MIN;
		sc->blkcnt = sc->bufsz / i;
		i = 0;
		while (sc->blkcnt >> i)
			i++;
		sc->blkcnt = 1 << (i - 1);
		if (sc->blkcnt < ATI_IXP_DMA_CHSEGS_MIN)
			sc->blkcnt = ATI_IXP_DMA_CHSEGS_MIN;
		else if (sc->blkcnt > ATI_IXP_DMA_CHSEGS_MAX)
			sc->blkcnt = ATI_IXP_DMA_CHSEGS_MAX;

	} else
		sc->blkcnt = ATI_IXP_DMA_CHSEGS;

	/*
	 * DMA tag for scatter-gather buffers and link pointers
	 */
	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev), /*alignment*/2,
		/*boundary*/0,
		/*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
		/*highaddr*/BUS_SPACE_MAXADDR,
		/*filter*/NULL, /*filterarg*/NULL,
		/*maxsize*/sc->bufsz, /*nsegments*/1, /*maxsegz*/0x3ffff,
		/*flags*/0, /*lockfunc*/NULL,
		/*lockarg*/NULL, &sc->parent_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		goto bad;
	}

	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev), /*alignment*/2,
		/*boundary*/0,
		/*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
		/*highaddr*/BUS_SPACE_MAXADDR,
		/*filter*/NULL, /*filterarg*/NULL,
		/*maxsize*/ATI_IXP_DMA_CHSEGS_MAX * ATI_IXP_NCHANS *
		sizeof(struct atiixp_dma_op),
		/*nsegments*/1, /*maxsegz*/0x3ffff,
		/*flags*/0, /*lockfunc*/NULL,
		/*lockarg*/NULL, &sc->sgd_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		goto bad;
	}

	if (bus_dmamem_alloc(sc->sgd_dmat, (void **)&sc->sgd_table,
	    BUS_DMA_NOWAIT, &sc->sgd_dmamap) == -1)
		goto bad;

	if (bus_dmamap_load(sc->sgd_dmat, sc->sgd_dmamap, sc->sgd_table,
	    ATI_IXP_DMA_CHSEGS_MAX * ATI_IXP_NCHANS *
	    sizeof(struct atiixp_dma_op), atiixp_dma_cb, sc, 0))
		goto bad;


	atiixp_chip_pre_init(sc);

	sc->delayed_attach.ich_func = atiixp_chip_post_init;
	sc->delayed_attach.ich_arg = sc;
	if (cold == 0 ||
	    config_intrhook_establish(&sc->delayed_attach) != 0) {
		sc->delayed_attach.ich_func = NULL;
		atiixp_chip_post_init(sc);
	}

	return (0);

bad:
	atiixp_release_resource(sc);
	return (ENXIO);
}

static int
atiixp_pci_detach(device_t dev)
{
	int r;
	struct atiixp_info *sc;

	sc = pcm_getdevinfo(dev);
	if (sc != NULL) {
		if (sc->codec != NULL) {
			r = pcm_unregister(dev);
			if (r)
				return (r);
		}
		sc->codec = NULL;
		if (sc->st != 0 && sc->sh != 0)
			atiixp_disable_interrupts(sc);
		atiixp_release_resource(sc);
	}
	return (0);
}

static int
atiixp_pci_suspend(device_t dev)
{
	struct atiixp_info *sc = pcm_getdevinfo(dev);
	uint32_t value;

	/* quickly disable interrupts and save channels active state */
	atiixp_lock(sc);
	atiixp_disable_interrupts(sc);
	atiixp_unlock(sc);

	/* stop everything */
	if (sc->pch.flags & ATI_IXP_CHN_RUNNING) {
		atiixp_chan_trigger(NULL, &sc->pch, PCMTRIG_STOP);
		sc->pch.flags |= ATI_IXP_CHN_SUSPEND;
	}
	if (sc->rch.flags & ATI_IXP_CHN_RUNNING) {
		atiixp_chan_trigger(NULL, &sc->rch, PCMTRIG_STOP);
		sc->rch.flags |= ATI_IXP_CHN_SUSPEND;
	}

	/* power down aclink and pci bus */
	atiixp_lock(sc);
	value = atiixp_rd(sc, ATI_REG_CMD);
	value |= ATI_REG_CMD_POWERDOWN | ATI_REG_CMD_AC_RESET;
	atiixp_wr(sc, ATI_REG_CMD, ATI_REG_CMD_POWERDOWN);
	atiixp_unlock(sc);

	return (0);
}

static int
atiixp_pci_resume(device_t dev)
{
	struct atiixp_info *sc = pcm_getdevinfo(dev);

	atiixp_lock(sc);
	/* reset / power up aclink */
	atiixp_reset_aclink(sc);
	atiixp_unlock(sc);

	if (mixer_reinit(dev) == -1) {
		device_printf(dev, "unable to reinitialize the mixer\n");
		return (ENXIO);
	}

	/*
	 * Resume channel activities. Reset channel format regardless
	 * of its previous state.
	 */
	if (sc->pch.channel != NULL) {
		if (sc->pch.fmt != 0)
			atiixp_chan_setformat(NULL, &sc->pch, sc->pch.fmt);
		if (sc->pch.flags & ATI_IXP_CHN_SUSPEND) {
			sc->pch.flags &= ~ATI_IXP_CHN_SUSPEND;
			atiixp_chan_trigger(NULL, &sc->pch, PCMTRIG_START);
		}
	}
	if (sc->rch.channel != NULL) {
		if (sc->rch.fmt != 0)
			atiixp_chan_setformat(NULL, &sc->rch, sc->rch.fmt);
		if (sc->rch.flags & ATI_IXP_CHN_SUSPEND) {
			sc->rch.flags &= ~ATI_IXP_CHN_SUSPEND;
			atiixp_chan_trigger(NULL, &sc->rch, PCMTRIG_START);
		}
	}

	/* enable interrupts */
	atiixp_lock(sc);
	if (sc->polling == 0)
		atiixp_enable_interrupts(sc);
	atiixp_unlock(sc);

	return (0);
}

static device_method_t atiixp_methods[] = {
	DEVMETHOD(device_probe,		atiixp_pci_probe),
	DEVMETHOD(device_attach,	atiixp_pci_attach),
	DEVMETHOD(device_detach,	atiixp_pci_detach),
	DEVMETHOD(device_suspend,	atiixp_pci_suspend),
	DEVMETHOD(device_resume,	atiixp_pci_resume),
	{ 0, 0 }
};

static driver_t atiixp_driver = {
	"pcm",
	atiixp_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_atiixp, pci, atiixp_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_atiixp, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_atiixp, 1);
