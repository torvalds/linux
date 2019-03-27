/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Orion Hodson <orion@freebsd.org>
 * Portions of this code derived from via82c686.c:
 * 	Copyright (c) 2000 David Jones <dej@ox.org>
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
 * Credits due to:
 *
 * Grzybowski Rafal, Russell Davies, Mark Handley, Daniel O'Connor for
 * comments, machine time, testing patches, and patience.  VIA for
 * providing specs.  ALSA for helpful comments and some register poke
 * ordering.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/ac97.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <sys/sysctl.h>

#include <dev/sound/pci/via8233.h>

SND_DECLARE_FILE("$FreeBSD$");

#define VIA8233_PCI_ID 0x30591106

#define VIA8233_REV_ID_8233PRE	0x10
#define VIA8233_REV_ID_8233C	0x20
#define VIA8233_REV_ID_8233	0x30
#define VIA8233_REV_ID_8233A	0x40
#define VIA8233_REV_ID_8235	0x50
#define VIA8233_REV_ID_8237	0x60
#define VIA8233_REV_ID_8251	0x70

#define SEGS_PER_CHAN	2			/* Segments per channel */
#define NDXSCHANS	4			/* No of DXS channels */
#define NMSGDCHANS	1			/* No of multichannel SGD */
#define NWRCHANS	1			/* No of write channels */
#define NCHANS		(NWRCHANS + NDXSCHANS + NMSGDCHANS)
#define	NSEGS		NCHANS * SEGS_PER_CHAN	/* Segments in SGD table */
#define VIA_SEGS_MIN		2
#define VIA_SEGS_MAX		64
#define VIA_SEGS_DEFAULT	2
#define VIA_BLK_MIN		32
#define VIA_BLK_ALIGN		(~(VIA_BLK_MIN - 1))

#define	VIA_DEFAULT_BUFSZ	0x1000

/* we rely on this struct being packed to 64 bits */
struct via_dma_op {
	volatile uint32_t ptr;
	volatile uint32_t flags;
#define VIA_DMAOP_EOL         0x80000000
#define VIA_DMAOP_FLAG        0x40000000
#define VIA_DMAOP_STOP        0x20000000
#define VIA_DMAOP_COUNT(x)    ((x)&0x00FFFFFF)
};

struct via_info;

struct via_chinfo {
	struct via_info *parent;
	struct pcm_channel *channel;
	struct snd_dbuf *buffer;
	struct via_dma_op *sgd_table;
	bus_addr_t sgd_addr;
	int dir, rbase, active;
	unsigned int blksz, blkcnt;
	unsigned int ptr, prevptr;
};

struct via_info {
	device_t dev;

	bus_space_tag_t st;
	bus_space_handle_t sh;
	bus_dma_tag_t parent_dmat;
	bus_dma_tag_t sgd_dmat;
	bus_dmamap_t sgd_dmamap;
	bus_addr_t sgd_addr;

	struct resource *reg, *irq;
	int regid, irqid;
	void *ih;
	struct ac97_info *codec;

	unsigned int bufsz, blkcnt;
	int dxs_src, dma_eol_wake;

	struct via_chinfo pch[NDXSCHANS + NMSGDCHANS];
	struct via_chinfo rch[NWRCHANS];
	struct via_dma_op *sgd_table;
	uint16_t codec_caps;
	uint16_t n_dxs_registered;
	int play_num, rec_num;
	struct mtx *lock;
	struct callout poll_timer;
	int poll_ticks, polling;
};

static uint32_t via_fmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};

static struct pcmchan_caps via_vracaps = { 4000, 48000, via_fmt, 0 };
static struct pcmchan_caps via_caps = { 48000, 48000, via_fmt, 0 };

static __inline int
via_chan_active(struct via_info *via)
{
	int i, ret = 0;

	if (via == NULL)
		return (0);

	for (i = 0; i < NDXSCHANS + NMSGDCHANS; i++)
		ret += via->pch[i].active;

	for (i = 0; i < NWRCHANS; i++)
		ret += via->rch[i].active;

	return (ret);
}

static int
sysctl_via8233_spdif_enable(SYSCTL_HANDLER_ARGS)
{
	struct via_info *via;
	device_t dev;
	uint32_t r;
	int err, new_en;

	dev = oidp->oid_arg1;
	via = pcm_getdevinfo(dev);
	snd_mtxlock(via->lock);
	r = pci_read_config(dev, VIA_PCI_SPDIF, 1);
	snd_mtxunlock(via->lock);
	new_en = (r & VIA_SPDIF_EN) ? 1 : 0;
	err = sysctl_handle_int(oidp, &new_en, 0, req);

	if (err || req->newptr == NULL)
		return (err);
	if (new_en < 0 || new_en > 1)
		return (EINVAL);

	if (new_en)
		r |= VIA_SPDIF_EN;
	else
		r &= ~VIA_SPDIF_EN;
	snd_mtxlock(via->lock);
	pci_write_config(dev, VIA_PCI_SPDIF, r, 1);
	snd_mtxunlock(via->lock);

	return (0);
}

static int
sysctl_via8233_dxs_src(SYSCTL_HANDLER_ARGS)
{
	struct via_info *via;
	device_t dev;
	int err, val;

	dev = oidp->oid_arg1;
	via = pcm_getdevinfo(dev);
	snd_mtxlock(via->lock);
	val = via->dxs_src;
	snd_mtxunlock(via->lock);
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err || req->newptr == NULL)
		return (err);
	if (val < 0 || val > 1)
		return (EINVAL);

	snd_mtxlock(via->lock);
	via->dxs_src = val;
	snd_mtxunlock(via->lock);

	return (0);
}

static int
sysctl_via_polling(SYSCTL_HANDLER_ARGS)
{
	struct via_info *via;
	device_t dev;
	int err, val;

	dev = oidp->oid_arg1;
	via = pcm_getdevinfo(dev);
	if (via == NULL)
		return (EINVAL);
	snd_mtxlock(via->lock);
	val = via->polling;
	snd_mtxunlock(via->lock);
	err = sysctl_handle_int(oidp, &val, 0, req);

	if (err || req->newptr == NULL)
		return (err);
	if (val < 0 || val > 1)
		return (EINVAL);

	snd_mtxlock(via->lock);
	if (val != via->polling) {
		if (via_chan_active(via) != 0)
			err = EBUSY;
		else if (val == 0)
			via->polling = 0;
		else
			via->polling = 1;
	}
	snd_mtxunlock(via->lock);

	return (err);
}

static void
via_init_sysctls(device_t dev)
{
	/* XXX: an user should be able to set this with a control tool,
	   if not done before 7.0-RELEASE, this needs to be converted to
	   a device specific sysctl "dev.pcm.X.yyy" via device_get_sysctl_*()
	   as discussed on multimedia@ in msg-id <861wujij2q.fsf@xps.des.no> */
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "spdif_enabled",  CTLTYPE_INT | CTLFLAG_RW, dev, sizeof(dev),
	    sysctl_via8233_spdif_enable, "I",
	    "Enable S/PDIF output on primary playback channel");
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "dxs_src", CTLTYPE_INT | CTLFLAG_RW, dev, sizeof(dev),
	    sysctl_via8233_dxs_src, "I",
	    "Enable VIA DXS Sample Rate Converter");
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "polling", CTLTYPE_INT | CTLFLAG_RW, dev, sizeof(dev),
	    sysctl_via_polling, "I",
	    "Enable polling mode");
}

static __inline uint32_t
via_rd(struct via_info *via, int regno, int size)
{
	switch (size) {
	case 1:
		return (bus_space_read_1(via->st, via->sh, regno));
	case 2:
		return (bus_space_read_2(via->st, via->sh, regno));
	case 4:
		return (bus_space_read_4(via->st, via->sh, regno));
	default:
		return (0xFFFFFFFF);
	}
}

static __inline void
via_wr(struct via_info *via, int regno, uint32_t data, int size)
{

	switch (size) {
	case 1:
		bus_space_write_1(via->st, via->sh, regno, data);
		break;
	case 2:
		bus_space_write_2(via->st, via->sh, regno, data);
		break;
	case 4:
		bus_space_write_4(via->st, via->sh, regno, data);
		break;
	}
}

/* -------------------------------------------------------------------- */
/* Codec interface */

static int
via_waitready_codec(struct via_info *via)
{
	int i;

	/* poll until codec not busy */
	for (i = 0; i < 1000; i++) {
		if ((via_rd(via, VIA_AC97_CONTROL, 4) & VIA_AC97_BUSY) == 0)
			return (0);
		DELAY(1);
	}
	device_printf(via->dev, "%s: codec busy\n", __func__);
	return (1);
}

static int
via_waitvalid_codec(struct via_info *via)
{
	int i;

	/* poll until codec valid */
	for (i = 0; i < 1000; i++) {
		if (via_rd(via, VIA_AC97_CONTROL, 4) & VIA_AC97_CODEC00_VALID)
			return (0);
		DELAY(1);
	}
	device_printf(via->dev, "%s: codec invalid\n", __func__);
	return (1);
}

static int
via_write_codec(kobj_t obj, void *addr, int reg, uint32_t val)
{
	struct via_info *via = addr;

	if (via_waitready_codec(via))
		return (-1);

	via_wr(via, VIA_AC97_CONTROL,
	       VIA_AC97_CODEC00_VALID | VIA_AC97_INDEX(reg) |
	       VIA_AC97_DATA(val), 4);

	return (0);
}

static int
via_read_codec(kobj_t obj, void *addr, int reg)
{
	struct via_info *via = addr;

	if (via_waitready_codec(via))
		return (-1);

	via_wr(via, VIA_AC97_CONTROL, VIA_AC97_CODEC00_VALID |
	    VIA_AC97_READ | VIA_AC97_INDEX(reg), 4);

	if (via_waitready_codec(via))
		return (-1);

	if (via_waitvalid_codec(via))
		return (-1);

	return (via_rd(via, VIA_AC97_CONTROL, 2));
}

static kobj_method_t via_ac97_methods[] = {
	KOBJMETHOD(ac97_read,		via_read_codec),
	KOBJMETHOD(ac97_write,		via_write_codec),
	KOBJMETHOD_END
};
AC97_DECLARE(via_ac97);

/* -------------------------------------------------------------------- */

static int
via_buildsgdt(struct via_chinfo *ch)
{
	uint32_t phys_addr, flag;
	int i;

	phys_addr = sndbuf_getbufaddr(ch->buffer);

	for (i = 0; i < ch->blkcnt; i++) {
		flag = (i == ch->blkcnt - 1) ? VIA_DMAOP_EOL : VIA_DMAOP_FLAG;
		ch->sgd_table[i].ptr = phys_addr + (i * ch->blksz);
		ch->sgd_table[i].flags = flag | ch->blksz;
	}

	return (0);
}

/* -------------------------------------------------------------------- */
/* Format setting functions */

static int
via8233wr_setformat(kobj_t obj, void *data, uint32_t format)
{
	struct via_chinfo *ch = data;
	struct via_info *via = ch->parent;

	uint32_t f = WR_FORMAT_STOP_INDEX;

	if (AFMT_CHANNEL(format) > 1)
		f |= WR_FORMAT_STEREO;
	if (format & AFMT_S16_LE)
		f |= WR_FORMAT_16BIT;
	snd_mtxlock(via->lock);
	via_wr(via, VIA_WR0_FORMAT, f, 4);
	snd_mtxunlock(via->lock);

	return (0);
}

static int
via8233dxs_setformat(kobj_t obj, void *data, uint32_t format)
{
	struct via_chinfo *ch = data;
	struct via_info *via = ch->parent;
	uint32_t r, v;

	r = ch->rbase + VIA8233_RP_DXS_RATEFMT;
	snd_mtxlock(via->lock);
	v = via_rd(via, r, 4);

	v &= ~(VIA8233_DXS_RATEFMT_STEREO | VIA8233_DXS_RATEFMT_16BIT);
	if (AFMT_CHANNEL(format) > 1)
		v |= VIA8233_DXS_RATEFMT_STEREO;
	if (format & AFMT_16BIT)
		v |= VIA8233_DXS_RATEFMT_16BIT;
	via_wr(via, r, v, 4);
	snd_mtxunlock(via->lock);

	return (0);
}

static int
via8233msgd_setformat(kobj_t obj, void *data, uint32_t format)
{
	struct via_chinfo *ch = data;
	struct via_info *via = ch->parent;

	uint32_t s = 0xff000000;
	uint8_t  v = (format & AFMT_S16_LE) ? MC_SGD_16BIT : MC_SGD_8BIT;

	if (AFMT_CHANNEL(format) > 1) {
		v |= MC_SGD_CHANNELS(2);
		s |= SLOT3(1) | SLOT4(2);
	} else {
		v |= MC_SGD_CHANNELS(1);
		s |= SLOT3(1) | SLOT4(1);
	}

	snd_mtxlock(via->lock);
	via_wr(via, VIA_MC_SLOT_SELECT, s, 4);
	via_wr(via, VIA_MC_SGD_FORMAT, v, 1);
	snd_mtxunlock(via->lock);

	return (0);
}

/* -------------------------------------------------------------------- */
/* Speed setting functions */

static uint32_t
via8233wr_setspeed(kobj_t obj, void *data, uint32_t speed)
{
	struct via_chinfo *ch = data;
	struct via_info *via = ch->parent;

	if (via->codec_caps & AC97_EXTCAP_VRA)
		return (ac97_setrate(via->codec, AC97_REGEXT_LADCRATE, speed));

	return (48000);
}

static uint32_t
via8233dxs_setspeed(kobj_t obj, void *data, uint32_t speed)
{
	struct via_chinfo *ch = data;
	struct via_info *via = ch->parent;
	uint32_t r, v;

	r = ch->rbase + VIA8233_RP_DXS_RATEFMT;
	snd_mtxlock(via->lock);
	v = via_rd(via, r, 4) & ~VIA8233_DXS_RATEFMT_48K;

	/* Careful to avoid overflow (divide by 48 per vt8233c docs) */

	v |= VIA8233_DXS_RATEFMT_48K * (speed / 48) / (48000 / 48);
	via_wr(via, r, v, 4);
	snd_mtxunlock(via->lock);

	return (speed);
}

static uint32_t
via8233msgd_setspeed(kobj_t obj, void *data, uint32_t speed)
{
	struct via_chinfo *ch = data;
	struct via_info *via = ch->parent;

	if (via->codec_caps & AC97_EXTCAP_VRA)
		return (ac97_setrate(via->codec, AC97_REGEXT_FDACRATE, speed));

	return (48000);
}

/* -------------------------------------------------------------------- */
/* Format probing functions */

static struct pcmchan_caps *
via8233wr_getcaps(kobj_t obj, void *data)
{
	struct via_chinfo *ch = data;
	struct via_info *via = ch->parent;

	/* Controlled by ac97 registers */
	if (via->codec_caps & AC97_EXTCAP_VRA)
		return (&via_vracaps);
	return (&via_caps);
}

static struct pcmchan_caps *
via8233dxs_getcaps(kobj_t obj, void *data)
{
	struct via_chinfo *ch = data;
	struct via_info *via = ch->parent;

	/*
	 * Controlled by onboard registers
	 *
	 * Apparently, few boards can do DXS sample rate
	 * conversion.
	 */
	if (via->dxs_src)
		return (&via_vracaps);
	return (&via_caps);
}

static struct pcmchan_caps *
via8233msgd_getcaps(kobj_t obj, void *data)
{
	struct via_chinfo *ch = data;
	struct via_info *via = ch->parent;

	/* Controlled by ac97 registers */
	if (via->codec_caps & AC97_EXTCAP_VRA)
		return (&via_vracaps);
	return (&via_caps);
}

/* -------------------------------------------------------------------- */
/* Common functions */

static int
via8233chan_setfragments(kobj_t obj, void *data,
					uint32_t blksz, uint32_t blkcnt)
{
	struct via_chinfo *ch = data;
	struct via_info *via = ch->parent;

	blksz &= VIA_BLK_ALIGN;

	if (blksz > (sndbuf_getmaxsize(ch->buffer) / VIA_SEGS_MIN))
		blksz = sndbuf_getmaxsize(ch->buffer) / VIA_SEGS_MIN;
	if (blksz < VIA_BLK_MIN)
		blksz = VIA_BLK_MIN;
	if (blkcnt > VIA_SEGS_MAX)
		blkcnt = VIA_SEGS_MAX;
	if (blkcnt < VIA_SEGS_MIN)
		blkcnt = VIA_SEGS_MIN;

	while ((blksz * blkcnt) > sndbuf_getmaxsize(ch->buffer)) {
		if ((blkcnt >> 1) >= VIA_SEGS_MIN)
			blkcnt >>= 1;
		else if ((blksz >> 1) >= VIA_BLK_MIN)
			blksz >>= 1;
		else
			break;
	}

	if ((sndbuf_getblksz(ch->buffer) != blksz ||
	    sndbuf_getblkcnt(ch->buffer) != blkcnt) &&
	    sndbuf_resize(ch->buffer, blkcnt, blksz) != 0)
		device_printf(via->dev, "%s: failed blksz=%u blkcnt=%u\n",
		    __func__, blksz, blkcnt);

	ch->blksz = sndbuf_getblksz(ch->buffer);
	ch->blkcnt = sndbuf_getblkcnt(ch->buffer);

	return (0);
}

static uint32_t
via8233chan_setblocksize(kobj_t obj, void *data, uint32_t blksz)
{
	struct via_chinfo *ch = data;
	struct via_info *via = ch->parent;

	via8233chan_setfragments(obj, data, blksz, via->blkcnt);

	return (ch->blksz);
}

static uint32_t
via8233chan_getptr(kobj_t obj, void *data)
{
	struct via_chinfo *ch = data;
	struct via_info *via = ch->parent;
	uint32_t v, index, count, ptr;

	snd_mtxlock(via->lock);
	if (via->polling != 0) {
		ptr = ch->ptr;
		snd_mtxunlock(via->lock);
	} else {
		v = via_rd(via, ch->rbase + VIA_RP_CURRENT_COUNT, 4);
		snd_mtxunlock(via->lock);
		index = v >> 24;		/* Last completed buffer */
		count = v & 0x00ffffff;	/* Bytes remaining */
		ptr = (index + 1) * ch->blksz - count;
		ptr %= ch->blkcnt * ch->blksz;	/* Wrap to available space */
	}

	return (ptr);
}

static void
via8233chan_reset(struct via_info *via, struct via_chinfo *ch)
{
	via_wr(via, ch->rbase + VIA_RP_CONTROL, SGD_CONTROL_STOP, 1);
	via_wr(via, ch->rbase + VIA_RP_CONTROL, 0x00, 1);
	via_wr(via, ch->rbase + VIA_RP_STATUS,
	    SGD_STATUS_EOL | SGD_STATUS_FLAG, 1);
}

/* -------------------------------------------------------------------- */
/* Channel initialization functions */

static void
via8233chan_sgdinit(struct via_info *via, struct via_chinfo *ch, int chnum)
{
	ch->sgd_table = &via->sgd_table[chnum * VIA_SEGS_MAX];
	ch->sgd_addr = via->sgd_addr + chnum * VIA_SEGS_MAX *
	    sizeof(struct via_dma_op);
}

static void*
via8233wr_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
						struct pcm_channel *c, int dir)
{
	struct via_info *via = devinfo;
	struct via_chinfo *ch;
	int num;

	snd_mtxlock(via->lock);
	num = via->rec_num++;
	ch = &via->rch[num];
	ch->parent = via;
	ch->channel = c;
	ch->buffer = b;
	ch->dir = dir;
	ch->blkcnt = via->blkcnt;
	ch->rbase = VIA_WR_BASE(num);
	via_wr(via, ch->rbase + VIA_WR_RP_SGD_FORMAT, WR_FIFO_ENABLE, 1);
	snd_mtxunlock(via->lock);

	if (sndbuf_alloc(ch->buffer, via->parent_dmat, 0, via->bufsz) != 0)
		return (NULL);

	snd_mtxlock(via->lock);
	via8233chan_sgdinit(via, ch, num);
	via8233chan_reset(via, ch);
	snd_mtxunlock(via->lock);

	return (ch);
}

static void*
via8233dxs_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
						struct pcm_channel *c, int dir)
{
	struct via_info *via = devinfo;
	struct via_chinfo *ch;
	int num;

	snd_mtxlock(via->lock);
	num = via->play_num++;
	ch = &via->pch[num];
	ch->parent = via;
	ch->channel = c;
	ch->buffer = b;
	ch->dir = dir;
	ch->blkcnt = via->blkcnt;

	/*
	 * All cards apparently support DXS3, but not other DXS
	 * channels.  We therefore want to align first DXS channel to
	 * DXS3.
	 */
	ch->rbase = VIA_DXS_BASE(NDXSCHANS - 1 - via->n_dxs_registered);
	via->n_dxs_registered++;
	snd_mtxunlock(via->lock);

	if (sndbuf_alloc(ch->buffer, via->parent_dmat, 0, via->bufsz) != 0)
		return (NULL);

	snd_mtxlock(via->lock);
	via8233chan_sgdinit(via, ch, NWRCHANS + num);
	via8233chan_reset(via, ch);
	snd_mtxunlock(via->lock);

	return (ch);
}

static void*
via8233msgd_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
						struct pcm_channel *c, int dir)
{
	struct via_info *via = devinfo;
	struct via_chinfo *ch;
	int num;

	snd_mtxlock(via->lock);
	num = via->play_num++;
	ch = &via->pch[num];
	ch->parent = via;
	ch->channel = c;
	ch->buffer = b;
	ch->dir = dir;
	ch->rbase = VIA_MC_SGD_STATUS;
	ch->blkcnt = via->blkcnt;
	snd_mtxunlock(via->lock);

	if (sndbuf_alloc(ch->buffer, via->parent_dmat, 0, via->bufsz) != 0)
		return (NULL);

	snd_mtxlock(via->lock);
	via8233chan_sgdinit(via, ch, NWRCHANS + num);
	via8233chan_reset(via, ch);
	snd_mtxunlock(via->lock);

	return (ch);
}

static void
via8233chan_mute(struct via_info *via, struct via_chinfo *ch, int muted)
{
	if (BASE_IS_VIA_DXS_REG(ch->rbase)) {
		int r;
		muted = (muted) ? VIA8233_DXS_MUTE : 0;
		via_wr(via, ch->rbase + VIA8233_RP_DXS_LVOL, muted, 1);
		via_wr(via, ch->rbase + VIA8233_RP_DXS_RVOL, muted, 1);
		r = via_rd(via, ch->rbase + VIA8233_RP_DXS_LVOL, 1) &
		    VIA8233_DXS_MUTE;
		if (r != muted)
			device_printf(via->dev,
			    "%s: failed to set dxs volume "
			    "(dxs base 0x%02x).\n", __func__, ch->rbase);
	}
}

static __inline int
via_poll_channel(struct via_chinfo *ch)
{
	struct via_info *via;
	uint32_t sz, delta;
	uint32_t v, index, count;
	int ptr;

	if (ch == NULL || ch->channel == NULL || ch->active == 0)
		return (0);

	via = ch->parent;
	sz = ch->blksz * ch->blkcnt;
	v = via_rd(via, ch->rbase + VIA_RP_CURRENT_COUNT, 4);
	index = v >> 24;
	count = v & 0x00ffffff;
	ptr = ((index + 1) * ch->blksz) - count;
	ptr %= sz;
	ptr &= ~(ch->blksz - 1);
	ch->ptr = ptr;
	delta = (sz + ptr - ch->prevptr) % sz;

	if (delta < ch->blksz)
		return (0);

	ch->prevptr = ptr;

	return (1);
}

static void
via_poll_callback(void *arg)
{
	struct via_info *via = arg;
	uint32_t ptrigger = 0, rtrigger = 0;
	int i;

	if (via == NULL)
		return;

	snd_mtxlock(via->lock);
	if (via->polling == 0 || via_chan_active(via) == 0) {
		snd_mtxunlock(via->lock);
		return;
	}

	for (i = 0; i < NDXSCHANS + NMSGDCHANS; i++)
		ptrigger |= (via_poll_channel(&via->pch[i]) != 0) ?
		    (1 << i) : 0;

	for (i = 0; i < NWRCHANS; i++)
		rtrigger |= (via_poll_channel(&via->rch[i]) != 0) ?
		    (1 << i) : 0;

	/* XXX */
	callout_reset(&via->poll_timer, 1/*via->poll_ticks*/,
	    via_poll_callback, via);

	snd_mtxunlock(via->lock);

	for (i = 0; i < NDXSCHANS + NMSGDCHANS; i++) {
		if (ptrigger & (1 << i))
			chn_intr(via->pch[i].channel);
	}
	for (i = 0; i < NWRCHANS; i++) {
		if (rtrigger & (1 << i))
			chn_intr(via->rch[i].channel);
	}
}

static int
via_poll_ticks(struct via_info *via)
{
	struct via_chinfo *ch;
	int i;
	int ret = hz;
	int pollticks;

	for (i = 0; i < NDXSCHANS + NMSGDCHANS; i++) {
		ch = &via->pch[i];
		if (ch->channel == NULL || ch->active == 0)
			continue;
		pollticks = ((uint64_t)hz * ch->blksz) /
		    ((uint64_t)sndbuf_getalign(ch->buffer) *
		    sndbuf_getspd(ch->buffer));
		pollticks >>= 2;
		if (pollticks > hz)
			pollticks = hz;
		if (pollticks < 1)
			pollticks = 1;
		if (pollticks < ret)
			ret = pollticks;
	}

	for (i = 0; i < NWRCHANS; i++) {
		ch = &via->rch[i];
		if (ch->channel == NULL || ch->active == 0)
			continue;
		pollticks = ((uint64_t)hz * ch->blksz) /
		    ((uint64_t)sndbuf_getalign(ch->buffer) *
		    sndbuf_getspd(ch->buffer));
		pollticks >>= 2;
		if (pollticks > hz)
			pollticks = hz;
		if (pollticks < 1)
			pollticks = 1;
		if (pollticks < ret)
			ret = pollticks;
	}

	return (ret);
}

static int
via8233chan_trigger(kobj_t obj, void* data, int go)
{
	struct via_chinfo *ch = data;
	struct via_info *via = ch->parent;
	int pollticks;

	if (!PCMTRIG_COMMON(go))
		return (0);

	snd_mtxlock(via->lock);
	switch(go) {
	case PCMTRIG_START:
		via_buildsgdt(ch);
		via8233chan_mute(via, ch, 0);
		via_wr(via, ch->rbase + VIA_RP_TABLE_PTR, ch->sgd_addr, 4);
		if (via->polling != 0) {
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
			if (via_chan_active(via) == 0 ||
			    pollticks < via->poll_ticks) {
			    	if (bootverbose) {
					if (via_chan_active(via) == 0)
						printf("%s: pollticks=%d\n",
						    __func__, pollticks);
					else
						printf("%s: "
						    "pollticks %d -> %d\n",
						    __func__, via->poll_ticks,
						    pollticks);
				}
				via->poll_ticks = pollticks;
				callout_reset(&via->poll_timer, 1,
				    via_poll_callback, via);
			}
		}
		via_wr(via, ch->rbase + VIA_RP_CONTROL,
		    SGD_CONTROL_START | SGD_CONTROL_AUTOSTART |
		    ((via->polling == 0) ?
		    (SGD_CONTROL_I_EOL | SGD_CONTROL_I_FLAG) : 0), 1);
		ch->active = 1;
		break;
	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		via_wr(via, ch->rbase + VIA_RP_CONTROL, SGD_CONTROL_STOP, 1);
		via8233chan_mute(via, ch, 1);
		via8233chan_reset(via, ch);
		ch->active = 0;
		if (via->polling != 0) {
			if (via_chan_active(via) == 0) {
				callout_stop(&via->poll_timer);
				via->poll_ticks = 1;
			} else {
				pollticks = via_poll_ticks(via);
				if (pollticks > via->poll_ticks) {
					if (bootverbose)
						printf("%s: pollticks "
						    "%d -> %d\n",
						    __func__, via->poll_ticks,
						    pollticks);
					via->poll_ticks = pollticks;
					callout_reset(&via->poll_timer,
					    1, via_poll_callback,
					    via);
				}
			}
		}
		break;
	default:
		break;
	}
	snd_mtxunlock(via->lock);
	return (0);
}

static kobj_method_t via8233wr_methods[] = {
	KOBJMETHOD(channel_init,		via8233wr_init),
	KOBJMETHOD(channel_setformat,		via8233wr_setformat),
	KOBJMETHOD(channel_setspeed,		via8233wr_setspeed),
	KOBJMETHOD(channel_getcaps,		via8233wr_getcaps),
	KOBJMETHOD(channel_setblocksize,	via8233chan_setblocksize),
	KOBJMETHOD(channel_setfragments,	via8233chan_setfragments),
	KOBJMETHOD(channel_trigger,		via8233chan_trigger),
	KOBJMETHOD(channel_getptr,		via8233chan_getptr),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(via8233wr);

static kobj_method_t via8233dxs_methods[] = {
	KOBJMETHOD(channel_init,		via8233dxs_init),
	KOBJMETHOD(channel_setformat,		via8233dxs_setformat),
	KOBJMETHOD(channel_setspeed,		via8233dxs_setspeed),
	KOBJMETHOD(channel_getcaps,		via8233dxs_getcaps),
	KOBJMETHOD(channel_setblocksize,	via8233chan_setblocksize),
	KOBJMETHOD(channel_setfragments,	via8233chan_setfragments),
	KOBJMETHOD(channel_trigger,		via8233chan_trigger),
	KOBJMETHOD(channel_getptr,		via8233chan_getptr),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(via8233dxs);

static kobj_method_t via8233msgd_methods[] = {
	KOBJMETHOD(channel_init,		via8233msgd_init),
	KOBJMETHOD(channel_setformat,		via8233msgd_setformat),
	KOBJMETHOD(channel_setspeed,		via8233msgd_setspeed),
	KOBJMETHOD(channel_getcaps,		via8233msgd_getcaps),
	KOBJMETHOD(channel_setblocksize,	via8233chan_setblocksize),
	KOBJMETHOD(channel_setfragments,	via8233chan_setfragments),
	KOBJMETHOD(channel_trigger,		via8233chan_trigger),
	KOBJMETHOD(channel_getptr,		via8233chan_getptr),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(via8233msgd);

/* -------------------------------------------------------------------- */

static void
via_intr(void *p)
{
	struct via_info *via = p;
	uint32_t ptrigger = 0, rtrigger = 0;
	int i, reg, stat;

	snd_mtxlock(via->lock);
	if (via->polling != 0) {
		snd_mtxunlock(via->lock);
		return;
	}
	/* Poll playback channels */
	for (i = 0; i < NDXSCHANS + NMSGDCHANS; i++) {
		if (via->pch[i].channel == NULL || via->pch[i].active == 0)
			continue;
		reg = via->pch[i].rbase + VIA_RP_STATUS;
		stat = via_rd(via, reg, 1);
		if (stat & SGD_STATUS_INTR) {
			if (via->dma_eol_wake && ((stat & SGD_STATUS_EOL) ||
			    !(stat & SGD_STATUS_ACTIVE)))
				via_wr(via, via->pch[i].rbase + VIA_RP_CONTROL,
				    SGD_CONTROL_START | SGD_CONTROL_AUTOSTART |
				    SGD_CONTROL_I_EOL | SGD_CONTROL_I_FLAG, 1);
			via_wr(via, reg, stat, 1);
			ptrigger |= 1 << i;
		}
	}
	/* Poll record channels */
	for (i = 0; i < NWRCHANS; i++) {
		if (via->rch[i].channel == NULL || via->rch[i].active == 0)
			continue;
		reg = via->rch[i].rbase + VIA_RP_STATUS;
		stat = via_rd(via, reg, 1);
		if (stat & SGD_STATUS_INTR) {
			if (via->dma_eol_wake && ((stat & SGD_STATUS_EOL) ||
			    !(stat & SGD_STATUS_ACTIVE)))
				via_wr(via, via->rch[i].rbase + VIA_RP_CONTROL,
				    SGD_CONTROL_START | SGD_CONTROL_AUTOSTART |
				    SGD_CONTROL_I_EOL | SGD_CONTROL_I_FLAG, 1);
			via_wr(via, reg, stat, 1);
			rtrigger |= 1 << i;
		}
	}
	snd_mtxunlock(via->lock);

	for (i = 0; i < NDXSCHANS + NMSGDCHANS; i++) {
		if (ptrigger & (1 << i))
			chn_intr(via->pch[i].channel);
	}
	for (i = 0; i < NWRCHANS; i++) {
		if (rtrigger & (1 << i))
			chn_intr(via->rch[i].channel);
	}
}

/*
 *  Probe and attach the card
 */
static int
via_probe(device_t dev)
{
	switch(pci_get_devid(dev)) {
	case VIA8233_PCI_ID:
		switch(pci_get_revid(dev)) {
		case VIA8233_REV_ID_8233PRE:
			device_set_desc(dev, "VIA VT8233 (pre)");
			return (BUS_PROBE_DEFAULT);
		case VIA8233_REV_ID_8233C:
			device_set_desc(dev, "VIA VT8233C");
			return (BUS_PROBE_DEFAULT);
		case VIA8233_REV_ID_8233:
			device_set_desc(dev, "VIA VT8233");
			return (BUS_PROBE_DEFAULT);
		case VIA8233_REV_ID_8233A:
			device_set_desc(dev, "VIA VT8233A");
			return (BUS_PROBE_DEFAULT);
		case VIA8233_REV_ID_8235:
			device_set_desc(dev, "VIA VT8235");
			return (BUS_PROBE_DEFAULT);
		case VIA8233_REV_ID_8237:
			device_set_desc(dev, "VIA VT8237");
			return (BUS_PROBE_DEFAULT);
		case VIA8233_REV_ID_8251:
			device_set_desc(dev, "VIA VT8251");
			return (BUS_PROBE_DEFAULT);
		default:
			device_set_desc(dev, "VIA VT8233X");	/* Unknown */
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static void
dma_cb(void *p, bus_dma_segment_t *bds, int a, int b)
{
	struct via_info *via = (struct via_info *)p;
	via->sgd_addr = bds->ds_addr;
}

static int
via_chip_init(device_t dev)
{
	uint32_t data, cnt;

	/* Wake up and reset AC97 if necessary */
	data = pci_read_config(dev, VIA_PCI_ACLINK_STAT, 1);

	if ((data & VIA_PCI_ACLINK_C00_READY) == 0) {
		/* Cold reset per ac97r2.3 spec (page 95) */
		/* Assert low */
		pci_write_config(dev, VIA_PCI_ACLINK_CTRL,
		    VIA_PCI_ACLINK_EN, 1);
		/* Wait T_rst_low */
		DELAY(100);
		/* Assert high */
		pci_write_config(dev, VIA_PCI_ACLINK_CTRL,
		    VIA_PCI_ACLINK_EN | VIA_PCI_ACLINK_NRST, 1);
		/* Wait T_rst2clk */
		DELAY(5);
		/* Assert low */
		pci_write_config(dev, VIA_PCI_ACLINK_CTRL,
		    VIA_PCI_ACLINK_EN, 1);
	} else {
		/* Warm reset */
		/* Force no sync */
		pci_write_config(dev, VIA_PCI_ACLINK_CTRL,
		    VIA_PCI_ACLINK_EN, 1);
		DELAY(100);
		/* Sync */
		pci_write_config(dev, VIA_PCI_ACLINK_CTRL,
		    VIA_PCI_ACLINK_EN | VIA_PCI_ACLINK_SYNC, 1);
		/* Wait T_sync_high */
		DELAY(5);
		/* Force no sync */
		pci_write_config(dev, VIA_PCI_ACLINK_CTRL,
		    VIA_PCI_ACLINK_EN, 1);
		/* Wait T_sync2clk */
		DELAY(5);
	}

	/* Power everything up */
	pci_write_config(dev, VIA_PCI_ACLINK_CTRL, VIA_PCI_ACLINK_DESIRED, 1);

	/* Wait for codec to become ready (largest reported delay 310ms) */
	for (cnt = 0; cnt < 2000; cnt++) {
		data = pci_read_config(dev, VIA_PCI_ACLINK_STAT, 1);
		if (data & VIA_PCI_ACLINK_C00_READY)
			return (0);
		DELAY(5000);
	}
	device_printf(dev, "primary codec not ready (cnt = 0x%02x)\n", cnt);
	return (ENXIO);
}

static int
via_attach(device_t dev)
{
	struct via_info *via = NULL;
	char status[SND_STATUSLEN];
	int i, via_dxs_disabled, via_dxs_src, via_dxs_chnum, via_sgd_chnum;
	int nsegs;
	uint32_t revid;

	via = malloc(sizeof *via, M_DEVBUF, M_WAITOK | M_ZERO);
	via->lock = snd_mtxcreate(device_get_nameunit(dev),
	    "snd_via8233 softc");
	via->dev = dev;

	callout_init(&via->poll_timer, 1);
	via->poll_ticks = 1;

	if (resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "polling", &i) == 0 && i != 0)
		via->polling = 1;
	else
		via->polling = 0;

	pci_set_powerstate(dev, PCI_POWERSTATE_D0);
	pci_enable_busmaster(dev);

	via->regid = PCIR_BAR(0);
	via->reg = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &via->regid,
					  RF_ACTIVE);
	if (!via->reg) {
		device_printf(dev, "cannot allocate bus resource.");
		goto bad;
	}
	via->st = rman_get_bustag(via->reg);
	via->sh = rman_get_bushandle(via->reg);

	via->irqid = 0;
	via->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &via->irqid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (!via->irq ||
	    snd_setup_intr(dev, via->irq, INTR_MPSAFE,
	    via_intr, via, &via->ih)) {
		device_printf(dev, "unable to map interrupt\n");
		goto bad;
	}

	via->bufsz = pcm_getbuffersize(dev, 4096, VIA_DEFAULT_BUFSZ, 65536);
	if (resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "blocksize", &i) == 0 && i > 0) {
		i &= VIA_BLK_ALIGN;
		if (i < VIA_BLK_MIN)
			i = VIA_BLK_MIN;
		via->blkcnt = via->bufsz / i;
		i = 0;
		while (via->blkcnt >> i)
			i++;
		via->blkcnt = 1 << (i - 1);
		if (via->blkcnt < VIA_SEGS_MIN)
			via->blkcnt = VIA_SEGS_MIN;
		else if (via->blkcnt > VIA_SEGS_MAX)
			via->blkcnt = VIA_SEGS_MAX;

	} else
		via->blkcnt = VIA_SEGS_DEFAULT;

	revid = pci_get_revid(dev);

	/*
	 * VIA8251 lost its interrupt after DMA EOL, and need
	 * a gentle spank on its face within interrupt handler.
	 */
	if (revid == VIA8233_REV_ID_8251)
		via->dma_eol_wake = 1;
	else
		via->dma_eol_wake = 0;

	/*
	 * Decide whether DXS had to be disabled or not
	 */
	if (revid == VIA8233_REV_ID_8233A) {
		/*
		 * DXS channel is disabled.  Reports from multiple users
		 * that it plays at half-speed.  Do not see this behaviour
		 * on available 8233C or when emulating 8233A register set
		 * on 8233C (either with or without ac97 VRA).
		 */
		via_dxs_disabled = 1;
	} else if (resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "via_dxs_disabled",
	    &via_dxs_disabled) == 0)
		via_dxs_disabled = (via_dxs_disabled > 0) ? 1 : 0;
	else
		via_dxs_disabled = 0;

	if (via_dxs_disabled) {
		via_dxs_chnum = 0;
		via_sgd_chnum = 1;
	} else {
		if (resource_int_value(device_get_name(dev),
		    device_get_unit(dev), "via_dxs_channels",
		    &via_dxs_chnum) != 0)
			via_dxs_chnum = NDXSCHANS;
		if (resource_int_value(device_get_name(dev),
		    device_get_unit(dev), "via_sgd_channels",
		    &via_sgd_chnum) != 0)
			via_sgd_chnum = NMSGDCHANS;
	}
	if (via_dxs_chnum > NDXSCHANS)
		via_dxs_chnum = NDXSCHANS;
	else if (via_dxs_chnum < 0)
		via_dxs_chnum = 0;
	if (via_sgd_chnum > NMSGDCHANS)
		via_sgd_chnum = NMSGDCHANS;
	else if (via_sgd_chnum < 0)
		via_sgd_chnum = 0;
	if (via_dxs_chnum + via_sgd_chnum < 1) {
		/* Minimalist ? */
		via_dxs_chnum = 1;
		via_sgd_chnum = 0;
	}
	if (via_dxs_chnum > 0 && resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "via_dxs_src", &via_dxs_src) == 0)
		via->dxs_src = (via_dxs_src > 0) ? 1 : 0;
	else
		via->dxs_src = 0;

	nsegs = (via_dxs_chnum + via_sgd_chnum + NWRCHANS) * VIA_SEGS_MAX;

	/* DMA tag for buffers */
	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev), /*alignment*/2,
		/*boundary*/0,
		/*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
		/*highaddr*/BUS_SPACE_MAXADDR,
		/*filter*/NULL, /*filterarg*/NULL,
		/*maxsize*/via->bufsz, /*nsegments*/1, /*maxsegz*/0x3ffff,
		/*flags*/0, /*lockfunc*/NULL,
		/*lockarg*/NULL, &via->parent_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		goto bad;
	}

	/*
	 *  DMA tag for SGD table.  The 686 uses scatter/gather DMA and
	 *  requires a list in memory of work to do.  We need only 16 bytes
	 *  for this list, and it is wasteful to allocate 16K.
	 */
	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev), /*alignment*/2,
		/*boundary*/0,
		/*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
		/*highaddr*/BUS_SPACE_MAXADDR,
		/*filter*/NULL, /*filterarg*/NULL,
		/*maxsize*/nsegs * sizeof(struct via_dma_op),
		/*nsegments*/1, /*maxsegz*/0x3ffff,
		/*flags*/0, /*lockfunc*/NULL,
		/*lockarg*/NULL, &via->sgd_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		goto bad;
	}

	if (bus_dmamem_alloc(via->sgd_dmat, (void **)&via->sgd_table,
	    BUS_DMA_NOWAIT, &via->sgd_dmamap) == -1)
		goto bad;
	if (bus_dmamap_load(via->sgd_dmat, via->sgd_dmamap, via->sgd_table,
	    nsegs * sizeof(struct via_dma_op), dma_cb, via, 0))
		goto bad;

	if (via_chip_init(dev))
		goto bad;

	via->codec = AC97_CREATE(dev, via, via_ac97);
	if (!via->codec)
		goto bad;

	mixer_init(dev, ac97_getmixerclass(), via->codec);

	via->codec_caps = ac97_getextcaps(via->codec);

	/* Try to set VRA without generating an error, VRM not reqrd yet */
	if (via->codec_caps &
	    (AC97_EXTCAP_VRA | AC97_EXTCAP_VRM | AC97_EXTCAP_DRA)) {
		uint16_t ext = ac97_getextmode(via->codec);
		ext |= (via->codec_caps &
		    (AC97_EXTCAP_VRA | AC97_EXTCAP_VRM));
		ext &= ~AC97_EXTCAP_DRA;
		ac97_setextmode(via->codec, ext);
	}

	snprintf(status, SND_STATUSLEN, "at io 0x%jx irq %jd %s",
	    rman_get_start(via->reg), rman_get_start(via->irq),
	    PCM_KLDSTRING(snd_via8233));

	/* Register */
	if (pcm_register(dev, via, via_dxs_chnum + via_sgd_chnum, NWRCHANS))
	      goto bad;
	for (i = 0; i < via_dxs_chnum; i++)
	      pcm_addchan(dev, PCMDIR_PLAY, &via8233dxs_class, via);
	for (i = 0; i < via_sgd_chnum; i++)
	      pcm_addchan(dev, PCMDIR_PLAY, &via8233msgd_class, via);
	for (i = 0; i < NWRCHANS; i++)
	      pcm_addchan(dev, PCMDIR_REC, &via8233wr_class, via);
	if (via_dxs_chnum > 0)
		via_init_sysctls(dev);
	device_printf(dev, "<VIA DXS %sabled: DXS%s %d / SGD %d / REC %d>\n",
	    (via_dxs_chnum > 0) ? "En" : "Dis", (via->dxs_src) ? "(SRC)" : "",
	    via_dxs_chnum, via_sgd_chnum, NWRCHANS);

	pcm_setstatus(dev, status);

	return (0);
bad:
	if (via->codec)
		ac97_destroy(via->codec);
	if (via->reg)
		bus_release_resource(dev, SYS_RES_IOPORT, via->regid, via->reg);
	if (via->ih)
		bus_teardown_intr(dev, via->irq, via->ih);
	if (via->irq)
		bus_release_resource(dev, SYS_RES_IRQ, via->irqid, via->irq);
	if (via->parent_dmat)
		bus_dma_tag_destroy(via->parent_dmat);
	if (via->sgd_addr)
		bus_dmamap_unload(via->sgd_dmat, via->sgd_dmamap);
	if (via->sgd_table)
		bus_dmamem_free(via->sgd_dmat, via->sgd_table, via->sgd_dmamap);
	if (via->sgd_dmat)
		bus_dma_tag_destroy(via->sgd_dmat);
	if (via->lock)
		snd_mtxfree(via->lock);
	if (via)
		free(via, M_DEVBUF);
	return (ENXIO);
}

static int
via_detach(device_t dev)
{
	int r;
	struct via_info *via;

	r = pcm_unregister(dev);
	if (r)
		return (r);

	via = pcm_getdevinfo(dev);

	if (via != NULL && (via->play_num != 0 || via->rec_num != 0)) {
		snd_mtxlock(via->lock);
		via->polling = 0;
		callout_stop(&via->poll_timer);
		snd_mtxunlock(via->lock);
		callout_drain(&via->poll_timer);
	}

	bus_release_resource(dev, SYS_RES_IOPORT, via->regid, via->reg);
	bus_teardown_intr(dev, via->irq, via->ih);
	bus_release_resource(dev, SYS_RES_IRQ, via->irqid, via->irq);
	bus_dma_tag_destroy(via->parent_dmat);
	bus_dmamap_unload(via->sgd_dmat, via->sgd_dmamap);
	bus_dmamem_free(via->sgd_dmat, via->sgd_table, via->sgd_dmamap);
	bus_dma_tag_destroy(via->sgd_dmat);
	snd_mtxfree(via->lock);
	free(via, M_DEVBUF);
	return (0);
}


static device_method_t via_methods[] = {
	DEVMETHOD(device_probe,		via_probe),
	DEVMETHOD(device_attach,	via_attach),
	DEVMETHOD(device_detach,	via_detach),
	{ 0, 0}
};

static driver_t via_driver = {
	"pcm",
	via_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_via8233, pci, via_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_via8233, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_via8233, 1);
