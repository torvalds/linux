/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 George Reid <greid@ukug.uk.freebsd.org>
 * Copyright (c) 1999 Cameron Grant <cg@freebsd.org>
 * Copyright (c) 1997,1998 Luigi Rizzo
 * Copyright (c) 1994,1995 Hannu Savolainen
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

SND_DECLARE_FILE("$FreeBSD$");

/* board-specific include files */
#include <dev/sound/isa/mss.h>
#include <dev/sound/isa/sb.h>
#include <dev/sound/chip.h>

#include <isa/isavar.h>

#include "mixer_if.h"

#define MSS_DEFAULT_BUFSZ (4096)
#define MSS_INDEXED_REGS 0x20
#define OPL_INDEXED_REGS 0x19

struct mss_info;

struct mss_chinfo {
	struct mss_info *parent;
	struct pcm_channel *channel;
	struct snd_dbuf *buffer;
	int dir;
	u_int32_t fmt, blksz;
};

struct mss_info {
    struct resource *io_base;	/* primary I/O address for the board */
    int		     io_rid;
    struct resource *conf_base; /* and the opti931 also has a config space */
    int		     conf_rid;
    struct resource *irq;
    int		     irq_rid;
    struct resource *drq1; /* play */
    int		     drq1_rid;
    struct resource *drq2; /* rec */
    int		     drq2_rid;
    void 	    *ih;
    bus_dma_tag_t    parent_dmat;
    struct mtx	    *lock;

    char mss_indexed_regs[MSS_INDEXED_REGS];
    char opl_indexed_regs[OPL_INDEXED_REGS];
    int bd_id;      /* used to hold board-id info, eg. sb version,
		     * mss codec type, etc. etc.
		     */
    int opti_offset;		/* offset from config_base for opti931 */
    u_long  bd_flags;       /* board-specific flags */
    int optibase;		/* base address for OPTi9xx config */
    struct resource *indir;	/* Indirect register index address */
    int indir_rid;
    int password;		/* password for opti9xx cards */
    int passwdreg;		/* password register */
    unsigned int bufsize;
    struct mss_chinfo pch, rch;
};

static int 		mss_probe(device_t dev);
static int 		mss_attach(device_t dev);

static driver_intr_t 	mss_intr;

/* prototypes for local functions */
static int 		mss_detect(device_t dev, struct mss_info *mss);
static int		opti_detect(device_t dev, struct mss_info *mss);
static char 		*ymf_test(device_t dev, struct mss_info *mss);
static void		ad_unmute(struct mss_info *mss);

/* mixer set funcs */
static int 		mss_mixer_set(struct mss_info *mss, int dev, int left, int right);
static int 		mss_set_recsrc(struct mss_info *mss, int mask);

/* io funcs */
static int 		ad_wait_init(struct mss_info *mss, int x);
static int 		ad_read(struct mss_info *mss, int reg);
static void 		ad_write(struct mss_info *mss, int reg, u_char data);
static void 		ad_write_cnt(struct mss_info *mss, int reg, u_short data);
static void    		ad_enter_MCE(struct mss_info *mss);
static void             ad_leave_MCE(struct mss_info *mss);

/* OPTi-specific functions */
static void		opti_write(struct mss_info *mss, u_char reg,
				   u_char data);
static u_char		opti_read(struct mss_info *mss, u_char reg);
static int		opti_init(device_t dev, struct mss_info *mss);

/* io primitives */
static void 		conf_wr(struct mss_info *mss, u_char reg, u_char data);
static u_char 		conf_rd(struct mss_info *mss, u_char reg);

static int 		pnpmss_probe(device_t dev);
static int 		pnpmss_attach(device_t dev);

static driver_intr_t 	opti931_intr;

static u_int32_t mss_fmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	SND_FORMAT(AFMT_MU_LAW, 1, 0),
	SND_FORMAT(AFMT_MU_LAW, 2, 0),
	SND_FORMAT(AFMT_A_LAW, 1, 0),
	SND_FORMAT(AFMT_A_LAW, 2, 0),
	0
};
static struct pcmchan_caps mss_caps = {4000, 48000, mss_fmt, 0};

static u_int32_t guspnp_fmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	SND_FORMAT(AFMT_A_LAW, 1, 0),
	SND_FORMAT(AFMT_A_LAW, 2, 0),
	0
};
static struct pcmchan_caps guspnp_caps = {4000, 48000, guspnp_fmt, 0};

static u_int32_t opti931_fmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};
static struct pcmchan_caps opti931_caps = {4000, 48000, opti931_fmt, 0};

#define MD_AD1848	0x91
#define MD_AD1845	0x92
#define MD_CS42XX	0xA1
#define MD_CS423X	0xA2
#define MD_OPTI930	0xB0
#define	MD_OPTI931	0xB1
#define MD_OPTI925	0xB2
#define MD_OPTI924	0xB3
#define	MD_GUSPNP	0xB8
#define MD_GUSMAX	0xB9
#define	MD_YM0020	0xC1
#define	MD_VIVO		0xD1

#define	DV_F_TRUE_MSS	0x00010000	/* mss _with_ base regs */

#define FULL_DUPLEX(x) ((x)->bd_flags & BD_F_DUPLEX)

static void
mss_lock(struct mss_info *mss)
{
	snd_mtxlock(mss->lock);
}

static void
mss_unlock(struct mss_info *mss)
{
	snd_mtxunlock(mss->lock);
}

static int
port_rd(struct resource *port, int off)
{
	if (port)
		return bus_space_read_1(rman_get_bustag(port),
					rman_get_bushandle(port),
					off);
	else
		return -1;
}

static void
port_wr(struct resource *port, int off, u_int8_t data)
{
	if (port)
		bus_space_write_1(rman_get_bustag(port),
				  rman_get_bushandle(port),
				  off, data);
}

static int
io_rd(struct mss_info *mss, int reg)
{
	if (mss->bd_flags & BD_F_MSS_OFFSET) reg -= 4;
	return port_rd(mss->io_base, reg);
}

static void
io_wr(struct mss_info *mss, int reg, u_int8_t data)
{
	if (mss->bd_flags & BD_F_MSS_OFFSET) reg -= 4;
	port_wr(mss->io_base, reg, data);
}

static void
conf_wr(struct mss_info *mss, u_char reg, u_char value)
{
    	port_wr(mss->conf_base, 0, reg);
    	port_wr(mss->conf_base, 1, value);
}

static u_char
conf_rd(struct mss_info *mss, u_char reg)
{
	port_wr(mss->conf_base, 0, reg);
    	return port_rd(mss->conf_base, 1);
}

static void
opti_wr(struct mss_info *mss, u_char reg, u_char value)
{
    	port_wr(mss->conf_base, mss->opti_offset + 0, reg);
    	port_wr(mss->conf_base, mss->opti_offset + 1, value);
}

static u_char
opti_rd(struct mss_info *mss, u_char reg)
{
	port_wr(mss->conf_base, mss->opti_offset + 0, reg);
    	return port_rd(mss->conf_base, mss->opti_offset + 1);
}

static void
gus_wr(struct mss_info *mss, u_char reg, u_char value)
{
    	port_wr(mss->conf_base, 3, reg);
    	port_wr(mss->conf_base, 5, value);
}

static u_char
gus_rd(struct mss_info *mss, u_char reg)
{
    	port_wr(mss->conf_base, 3, reg);
    	return port_rd(mss->conf_base, 5);
}

static void
mss_release_resources(struct mss_info *mss, device_t dev)
{
    	if (mss->irq) {
    		if (mss->ih)
			bus_teardown_intr(dev, mss->irq, mss->ih);
 		bus_release_resource(dev, SYS_RES_IRQ, mss->irq_rid,
				     mss->irq);
		mss->irq = NULL;
    	}
    	if (mss->drq2) {
		if (mss->drq2 != mss->drq1) {
			isa_dma_release(rman_get_start(mss->drq2));
			bus_release_resource(dev, SYS_RES_DRQ, mss->drq2_rid,
				     	mss->drq2);
		}
		mss->drq2 = NULL;
    	}
     	if (mss->drq1) {
		isa_dma_release(rman_get_start(mss->drq1));
		bus_release_resource(dev, SYS_RES_DRQ, mss->drq1_rid,
				     mss->drq1);
		mss->drq1 = NULL;
    	}
   	if (mss->io_base) {
		bus_release_resource(dev, SYS_RES_IOPORT, mss->io_rid,
				     mss->io_base);
		mss->io_base = NULL;
    	}
    	if (mss->conf_base) {
		bus_release_resource(dev, SYS_RES_IOPORT, mss->conf_rid,
				     mss->conf_base);
		mss->conf_base = NULL;
    	}
	if (mss->indir) {
		bus_release_resource(dev, SYS_RES_IOPORT, mss->indir_rid,
				     mss->indir);
		mss->indir = NULL;
	}
    	if (mss->parent_dmat) {
		bus_dma_tag_destroy(mss->parent_dmat);
		mss->parent_dmat = 0;
    	}
	if (mss->lock) snd_mtxfree(mss->lock);

     	free(mss, M_DEVBUF);
}

static int
mss_alloc_resources(struct mss_info *mss, device_t dev)
{
    	int pdma, rdma, ok = 1;
	if (!mss->io_base)
    		mss->io_base = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
						      &mss->io_rid, RF_ACTIVE);
	if (!mss->irq)
    		mss->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
						  &mss->irq_rid, RF_ACTIVE);
	if (!mss->drq1)
    		mss->drq1 = bus_alloc_resource_any(dev, SYS_RES_DRQ,
						   &mss->drq1_rid,
						   RF_ACTIVE);
    	if (mss->conf_rid >= 0 && !mss->conf_base)
        	mss->conf_base = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
							&mss->conf_rid,
							RF_ACTIVE);
    	if (mss->drq2_rid >= 0 && !mss->drq2)
        	mss->drq2 = bus_alloc_resource_any(dev, SYS_RES_DRQ,
						   &mss->drq2_rid,
						   RF_ACTIVE);

	if (!mss->io_base || !mss->drq1 || !mss->irq) ok = 0;
	if (mss->conf_rid >= 0 && !mss->conf_base) ok = 0;
	if (mss->drq2_rid >= 0 && !mss->drq2) ok = 0;

	if (ok) {
		pdma = rman_get_start(mss->drq1);
		isa_dma_acquire(pdma);
		isa_dmainit(pdma, mss->bufsize);
		mss->bd_flags &= ~BD_F_DUPLEX;
		if (mss->drq2) {
			rdma = rman_get_start(mss->drq2);
			isa_dma_acquire(rdma);
			isa_dmainit(rdma, mss->bufsize);
			mss->bd_flags |= BD_F_DUPLEX;
		} else mss->drq2 = mss->drq1;
	}
    	return ok;
}

/*
 * The various mixers use a variety of bitmasks etc. The Voxware
 * driver had a very nice technique to describe a mixer and interface
 * to it. A table defines, for each channel, which register, bits,
 * offset, polarity to use. This procedure creates the new value
 * using the table and the old value.
 */

static void
change_bits(mixer_tab *t, u_char *regval, int dev, int chn, int newval)
{
    	u_char mask;
    	int shift;

    	DEB(printf("ch_bits dev %d ch %d val %d old 0x%02x "
		"r %d p %d bit %d off %d\n",
		dev, chn, newval, *regval,
		(*t)[dev][chn].regno, (*t)[dev][chn].polarity,
		(*t)[dev][chn].nbits, (*t)[dev][chn].bitoffs ) );

    	if ( (*t)[dev][chn].polarity == 1)	/* reverse */
		newval = 100 - newval ;

    	mask = (1 << (*t)[dev][chn].nbits) - 1;
    	newval = (int) ((newval * mask) + 50) / 100; /* Scale it */
    	shift = (*t)[dev][chn].bitoffs /*- (*t)[dev][LEFT_CHN].nbits + 1*/;

    	*regval &= ~(mask << shift);        /* Filter out the previous value */
    	*regval |= (newval & mask) << shift;        /* Set the new value */
}

/* -------------------------------------------------------------------- */
/* only one source can be set... */
static int
mss_set_recsrc(struct mss_info *mss, int mask)
{
    	u_char   recdev;

    	switch (mask) {
    	case SOUND_MASK_LINE:
    	case SOUND_MASK_LINE3:
		recdev = 0;
		break;

    	case SOUND_MASK_CD:
    	case SOUND_MASK_LINE1:
		recdev = 0x40;
		break;

    	case SOUND_MASK_IMIX:
		recdev = 0xc0;
		break;

    	case SOUND_MASK_MIC:
    	default:
		mask = SOUND_MASK_MIC;
		recdev = 0x80;
    	}
    	ad_write(mss, 0, (ad_read(mss, 0) & 0x3f) | recdev);
    	ad_write(mss, 1, (ad_read(mss, 1) & 0x3f) | recdev);
    	return mask;
}

/* there are differences in the mixer depending on the actual sound card. */
static int
mss_mixer_set(struct mss_info *mss, int dev, int left, int right)
{
    	int        regoffs;
    	mixer_tab *mix_d;
    	u_char     old, val;

	switch (mss->bd_id) {
		case MD_OPTI931:
			mix_d = &opti931_devices;
			break;
		case MD_OPTI930:
			mix_d = &opti930_devices;
			break;
		default:
			mix_d = &mix_devices;
	}

    	if ((*mix_d)[dev][LEFT_CHN].nbits == 0) {
		DEB(printf("nbits = 0 for dev %d\n", dev));
		return -1;
    	}

    	if ((*mix_d)[dev][RIGHT_CHN].nbits == 0) right = left; /* mono */

    	/* Set the left channel */

    	regoffs = (*mix_d)[dev][LEFT_CHN].regno;
    	old = val = ad_read(mss, regoffs);
    	/* if volume is 0, mute chan. Otherwise, unmute. */
    	if (regoffs != 0) val = (left == 0)? old | 0x80 : old & 0x7f;
    	change_bits(mix_d, &val, dev, LEFT_CHN, left);
    	ad_write(mss, regoffs, val);

    	DEB(printf("LEFT: dev %d reg %d old 0x%02x new 0x%02x\n",
		dev, regoffs, old, val));

    	if ((*mix_d)[dev][RIGHT_CHN].nbits != 0) { /* have stereo */
		/* Set the right channel */
		regoffs = (*mix_d)[dev][RIGHT_CHN].regno;
		old = val = ad_read(mss, regoffs);
		if (regoffs != 1) val = (right == 0)? old | 0x80 : old & 0x7f;
		change_bits(mix_d, &val, dev, RIGHT_CHN, right);
		ad_write(mss, regoffs, val);

		DEB(printf("RIGHT: dev %d reg %d old 0x%02x new 0x%02x\n",
	    	dev, regoffs, old, val));
    	}
    	return 0; /* success */
}

/* -------------------------------------------------------------------- */

static int
mssmix_init(struct snd_mixer *m)
{
	struct mss_info *mss = mix_getdevinfo(m);

	mix_setdevs(m, MODE2_MIXER_DEVICES);
	mix_setrecdevs(m, MSS_REC_DEVICES);
	switch(mss->bd_id) {
	case MD_OPTI930:
		mix_setdevs(m, OPTI930_MIXER_DEVICES);
		break;

	case MD_OPTI931:
		mix_setdevs(m, OPTI931_MIXER_DEVICES);
		mss_lock(mss);
		ad_write(mss, 20, 0x88);
		ad_write(mss, 21, 0x88);
		mss_unlock(mss);
		break;

	case MD_AD1848:
		mix_setdevs(m, MODE1_MIXER_DEVICES);
		break;

	case MD_GUSPNP:
	case MD_GUSMAX:
		/* this is only necessary in mode 3 ... */
		mss_lock(mss);
		ad_write(mss, 22, 0x88);
		ad_write(mss, 23, 0x88);
		mss_unlock(mss);
		break;
	}
	return 0;
}

static int
mssmix_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
	struct mss_info *mss = mix_getdevinfo(m);

	mss_lock(mss);
	mss_mixer_set(mss, dev, left, right);
	mss_unlock(mss);

	return left | (right << 8);
}

static u_int32_t
mssmix_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	struct mss_info *mss = mix_getdevinfo(m);

	mss_lock(mss);
	src = mss_set_recsrc(mss, src);
	mss_unlock(mss);
	return src;
}

static kobj_method_t mssmix_mixer_methods[] = {
    	KOBJMETHOD(mixer_init,		mssmix_init),
    	KOBJMETHOD(mixer_set,		mssmix_set),
    	KOBJMETHOD(mixer_setrecsrc,	mssmix_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(mssmix_mixer);

/* -------------------------------------------------------------------- */

static int
ymmix_init(struct snd_mixer *m)
{
	struct mss_info *mss = mix_getdevinfo(m);

	mssmix_init(m);
	mix_setdevs(m, mix_getdevs(m) | SOUND_MASK_VOLUME | SOUND_MASK_MIC
				      | SOUND_MASK_BASS | SOUND_MASK_TREBLE);
	/* Set master volume */
	mss_lock(mss);
	conf_wr(mss, OPL3SAx_VOLUMEL, 7);
	conf_wr(mss, OPL3SAx_VOLUMER, 7);
	mss_unlock(mss);

	return 0;
}

static int
ymmix_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
	struct mss_info *mss = mix_getdevinfo(m);
	int t, l, r;

	mss_lock(mss);
	switch (dev) {
	case SOUND_MIXER_VOLUME:
		if (left) t = 15 - (left * 15) / 100;
		else t = 0x80; /* mute */
		conf_wr(mss, OPL3SAx_VOLUMEL, t);
		if (right) t = 15 - (right * 15) / 100;
		else t = 0x80; /* mute */
		conf_wr(mss, OPL3SAx_VOLUMER, t);
		break;

	case SOUND_MIXER_MIC:
		t = left;
		if (left) t = 31 - (left * 31) / 100;
		else t = 0x80; /* mute */
		conf_wr(mss, OPL3SAx_MIC, t);
		break;

	case SOUND_MIXER_BASS:
		l = (left * 7) / 100;
		r = (right * 7) / 100;
		t = (r << 4) | l;
		conf_wr(mss, OPL3SAx_BASS, t);
		break;

	case SOUND_MIXER_TREBLE:
		l = (left * 7) / 100;
		r = (right * 7) / 100;
		t = (r << 4) | l;
		conf_wr(mss, OPL3SAx_TREBLE, t);
		break;

	default:
		mss_mixer_set(mss, dev, left, right);
	}
	mss_unlock(mss);

	return left | (right << 8);
}

static u_int32_t
ymmix_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	struct mss_info *mss = mix_getdevinfo(m);
	mss_lock(mss);
	src = mss_set_recsrc(mss, src);
	mss_unlock(mss);
	return src;
}

static kobj_method_t ymmix_mixer_methods[] = {
    	KOBJMETHOD(mixer_init,		ymmix_init),
    	KOBJMETHOD(mixer_set,		ymmix_set),
    	KOBJMETHOD(mixer_setrecsrc,	ymmix_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(ymmix_mixer);

/* -------------------------------------------------------------------- */
/*
 * XXX This might be better off in the gusc driver.
 */
static void
gusmax_setup(struct mss_info *mss, device_t dev, struct resource *alt)
{
	static const unsigned char irq_bits[16] = {
		0, 0, 0, 3, 0, 2, 0, 4, 0, 1, 0, 5, 6, 0, 0, 7
	};
	static const unsigned char dma_bits[8] = {
		0, 1, 0, 2, 0, 3, 4, 5
	};
	device_t parent = device_get_parent(dev);
	unsigned char irqctl, dmactl;
	int s;

	s = splhigh();

	port_wr(alt, 0x0f, 0x05);
	port_wr(alt, 0x00, 0x0c);
	port_wr(alt, 0x0b, 0x00);

	port_wr(alt, 0x0f, 0x00);

	irqctl = irq_bits[isa_get_irq(parent)];
	/* Share the IRQ with the MIDI driver.  */
	irqctl |= 0x40;
	dmactl = dma_bits[isa_get_drq(parent)];
	if (device_get_flags(parent) & DV_F_DUAL_DMA)
		dmactl |= dma_bits[device_get_flags(parent) & DV_F_DRQ_MASK]
		    << 3;

	/*
	 * Set the DMA and IRQ control latches.
	 */
	port_wr(alt, 0x00, 0x0c);
	port_wr(alt, 0x0b, dmactl | 0x80);
	port_wr(alt, 0x00, 0x4c);
	port_wr(alt, 0x0b, irqctl);

	port_wr(alt, 0x00, 0x0c);
	port_wr(alt, 0x0b, dmactl);
	port_wr(alt, 0x00, 0x4c);
	port_wr(alt, 0x0b, irqctl);

	port_wr(mss->conf_base, 2, 0);
	port_wr(alt, 0x00, 0x0c);
	port_wr(mss->conf_base, 2, 0);

	splx(s);
}

static int
mss_init(struct mss_info *mss, device_t dev)
{
       	u_char r6, r9;
	struct resource *alt;
	int rid, tmp;

	mss->bd_flags |= BD_F_MCE_BIT;
	switch(mss->bd_id) {
	case MD_OPTI931:
		/*
		 * The MED3931 v.1.0 allocates 3 bytes for the config
		 * space, whereas v.2.0 allocates 4 bytes. What I know
		 * for sure is that the upper two ports must be used,
		 * and they should end on a boundary of 4 bytes. So I
		 * need the following trick.
		 */
		mss->opti_offset =
			(rman_get_start(mss->conf_base) & ~3) + 2
			- rman_get_start(mss->conf_base);
		BVDDB(printf("mss_init: opti_offset=%d\n", mss->opti_offset));
    		opti_wr(mss, 4, 0xd6); /* fifo empty, OPL3, audio enable, SB3.2 */
    		ad_write(mss, 10, 2); /* enable interrupts */
    		opti_wr(mss, 6, 2);  /* MCIR6: mss enable, sb disable */
    		opti_wr(mss, 5, 0x28);  /* MCIR5: codec in exp. mode,fifo */
		break;

	case MD_GUSPNP:
	case MD_GUSMAX:
		gus_wr(mss, 0x4c /* _URSTI */, 0);/* Pull reset */
    		DELAY(1000 * 30);
    		/* release reset  and enable DAC */
    		gus_wr(mss, 0x4c /* _URSTI */, 3);
    		DELAY(1000 * 30);
    		/* end of reset */

		rid = 0;
    		alt = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
					     RF_ACTIVE);
		if (alt == NULL) {
			printf("XXX couldn't init GUS PnP/MAX\n");
			break;
		}
    		port_wr(alt, 0, 0xC); /* enable int and dma */
		if (mss->bd_id == MD_GUSMAX)
			gusmax_setup(mss, dev, alt);
		bus_release_resource(dev, SYS_RES_IOPORT, rid, alt);

    		/*
     		 * unmute left & right line. Need to go in mode3, unmute,
     		 * and back to mode 2
     		 */
    		tmp = ad_read(mss, 0x0c);
    		ad_write(mss, 0x0c, 0x6c); /* special value to enter mode 3 */
    		ad_write(mss, 0x19, 0); /* unmute left */
    		ad_write(mss, 0x1b, 0); /* unmute right */
    		ad_write(mss, 0x0c, tmp); /* restore old mode */

    		/* send codec interrupts on irq1 and only use that one */
    		gus_wr(mss, 0x5a, 0x4f);

    		/* enable access to hidden regs */
    		tmp = gus_rd(mss, 0x5b /* IVERI */);
    		gus_wr(mss, 0x5b, tmp | 1);
    		BVDDB(printf("GUS: silicon rev %c\n", 'A' + ((tmp & 0xf) >> 4)));
		break;

    	case MD_YM0020:
         	conf_wr(mss, OPL3SAx_DMACONF, 0xa9); /* dma-b rec, dma-a play */
        	r6 = conf_rd(mss, OPL3SAx_DMACONF);
        	r9 = conf_rd(mss, OPL3SAx_MISC); /* version */
        	BVDDB(printf("Yamaha: ver 0x%x DMA config 0x%x\n", r6, r9);)
		/* yamaha - set volume to max */
		conf_wr(mss, OPL3SAx_VOLUMEL, 0);
		conf_wr(mss, OPL3SAx_VOLUMER, 0);
		conf_wr(mss, OPL3SAx_DMACONF, FULL_DUPLEX(mss)? 0xa9 : 0x8b);
		break;
 	}
    	if (FULL_DUPLEX(mss) && mss->bd_id != MD_OPTI931)
    		ad_write(mss, 12, ad_read(mss, 12) | 0x40); /* mode 2 */
	ad_enter_MCE(mss);
    	ad_write(mss, 9, FULL_DUPLEX(mss)? 0 : 4);
    	ad_leave_MCE(mss);
	ad_write(mss, 10, 2); /* int enable */
    	io_wr(mss, MSS_STATUS, 0); /* Clear interrupt status */
    	/* the following seem required on the CS4232 */
    	ad_unmute(mss);
	return 0;
}


/*
 * main irq handler for the CS423x. The OPTi931 code is
 * a separate one.
 * The correct way to operate for a device with multiple internal
 * interrupt sources is to loop on the status register and ack
 * interrupts until all interrupts are served and none are reported. At
 * this point the IRQ line to the ISA IRQ controller should go low
 * and be raised at the next interrupt.
 *
 * Since the ISA IRQ controller is sent EOI _before_ passing control
 * to the isr, it might happen that we serve an interrupt early, in
 * which case the status register at the next interrupt should just
 * say that there are no more interrupts...
 */

static void
mss_intr(void *arg)
{
    	struct mss_info *mss = arg;
    	u_char c = 0, served = 0;
    	int i;

    	DEB(printf("mss_intr\n"));
	mss_lock(mss);
    	ad_read(mss, 11); /* fake read of status bits */

    	/* loop until there are interrupts, but no more than 10 times. */
    	for (i = 10; i > 0 && io_rd(mss, MSS_STATUS) & 1; i--) {
		/* get exact reason for full-duplex boards */
		c = FULL_DUPLEX(mss)? ad_read(mss, 24) : 0x30;
		c &= ~served;
		if (sndbuf_runsz(mss->pch.buffer) && (c & 0x10)) {
	    		served |= 0x10;
			mss_unlock(mss);
	    		chn_intr(mss->pch.channel);
			mss_lock(mss);
		}
		if (sndbuf_runsz(mss->rch.buffer) && (c & 0x20)) {
	    		served |= 0x20;
			mss_unlock(mss);
	    		chn_intr(mss->rch.channel);
			mss_lock(mss);
		}
		/* now ack the interrupt */
		if (FULL_DUPLEX(mss)) ad_write(mss, 24, ~c); /* ack selectively */
		else io_wr(mss, MSS_STATUS, 0);	/* Clear interrupt status */
    	}
    	if (i == 10) {
		BVDDB(printf("mss_intr: irq, but not from mss\n"));
	} else if (served == 0) {
		BVDDB(printf("mss_intr: unexpected irq with reason %x\n", c));
		/*
	 	* this should not happen... I have no idea what to do now.
	 	* maybe should do a sanity check and restart dmas ?
	 	*/
		io_wr(mss, MSS_STATUS, 0);	/* Clear interrupt status */
    	}
	mss_unlock(mss);
}

/*
 * AD_WAIT_INIT waits if we are initializing the board and
 * we cannot modify its settings
 */
static int
ad_wait_init(struct mss_info *mss, int x)
{
    	int arg = x, n = 0; /* to shut up the compiler... */
    	for (; x > 0; x--)
		if ((n = io_rd(mss, MSS_INDEX)) & MSS_IDXBUSY) DELAY(10);
		else return n;
    	printf("AD_WAIT_INIT FAILED %d 0x%02x\n", arg, n);
    	return n;
}

static int
ad_read(struct mss_info *mss, int reg)
{
    	int             x;

    	ad_wait_init(mss, 201000);
    	x = io_rd(mss, MSS_INDEX) & ~MSS_IDXMASK;
    	io_wr(mss, MSS_INDEX, (u_char)(reg & MSS_IDXMASK) | x);
    	x = io_rd(mss, MSS_IDATA);
	/* printf("ad_read %d, %x\n", reg, x); */
    	return x;
}

static void
ad_write(struct mss_info *mss, int reg, u_char data)
{
    	int x;

	/* printf("ad_write %d, %x\n", reg, data); */
    	ad_wait_init(mss, 1002000);
    	x = io_rd(mss, MSS_INDEX) & ~MSS_IDXMASK;
    	io_wr(mss, MSS_INDEX, (u_char)(reg & MSS_IDXMASK) | x);
    	io_wr(mss, MSS_IDATA, data);
}

static void
ad_write_cnt(struct mss_info *mss, int reg, u_short cnt)
{
    	ad_write(mss, reg+1, cnt & 0xff);
    	ad_write(mss, reg, cnt >> 8); /* upper base must be last */
}

static void
wait_for_calibration(struct mss_info *mss)
{
    	int t;

    	/*
     	 * Wait until the auto calibration process has finished.
     	 *
     	 * 1) Wait until the chip becomes ready (reads don't return 0x80).
     	 * 2) Wait until the ACI bit of I11 gets on
     	 * 3) Wait until the ACI bit of I11 gets off
     	 */

    	t = ad_wait_init(mss, 1000000);
    	if (t & MSS_IDXBUSY) printf("mss: Auto calibration timed out(1).\n");

	/*
	 * The calibration mode for chips that support it is set so that
	 * we never see ACI go on.
	 */
	if (mss->bd_id == MD_GUSMAX || mss->bd_id == MD_GUSPNP) {
		for (t = 100; t > 0 && (ad_read(mss, 11) & 0x20) == 0; t--);
	} else {
       		/*
		 * XXX This should only be enabled for cards that *really*
		 * need it.  Are there any?
		 */
  		for (t = 100; t > 0 && (ad_read(mss, 11) & 0x20) == 0; t--) DELAY(100);
	}
    	for (t = 100; t > 0 && ad_read(mss, 11) & 0x20; t--) DELAY(100);
}

static void
ad_unmute(struct mss_info *mss)
{
    	ad_write(mss, 6, ad_read(mss, 6) & ~I6_MUTE);
    	ad_write(mss, 7, ad_read(mss, 7) & ~I6_MUTE);
}

static void
ad_enter_MCE(struct mss_info *mss)
{
    	int prev;

    	mss->bd_flags |= BD_F_MCE_BIT;
    	ad_wait_init(mss, 203000);
    	prev = io_rd(mss, MSS_INDEX);
    	prev &= ~MSS_TRD;
    	io_wr(mss, MSS_INDEX, prev | MSS_MCE);
}

static void
ad_leave_MCE(struct mss_info *mss)
{
    	u_char   prev;

    	if ((mss->bd_flags & BD_F_MCE_BIT) == 0) {
		DEB(printf("--- hey, leave_MCE: MCE bit was not set!\n"));
		return;
    	}

    	ad_wait_init(mss, 1000000);

    	mss->bd_flags &= ~BD_F_MCE_BIT;

    	prev = io_rd(mss, MSS_INDEX);
    	prev &= ~MSS_TRD;
    	io_wr(mss, MSS_INDEX, prev & ~MSS_MCE); /* Clear the MCE bit */
    	wait_for_calibration(mss);
}

static int
mss_speed(struct mss_chinfo *ch, int speed)
{
    	struct mss_info *mss = ch->parent;
    	/*
     	* In the CS4231, the low 4 bits of I8 are used to hold the
     	* sample rate.  Only a fixed number of values is allowed. This
     	* table lists them. The speed-setting routines scans the table
     	* looking for the closest match. This is the only supported method.
     	*
     	* In the CS4236, there is an alternate metod (which we do not
     	* support yet) which provides almost arbitrary frequency setting.
     	* In the AD1845, it looks like the sample rate can be
     	* almost arbitrary, and written directly to a register.
     	* In the OPTi931, there is a SB command which provides for
     	* almost arbitrary frequency setting.
     	*
     	*/
    	ad_enter_MCE(mss);
    	if (mss->bd_id == MD_AD1845) { /* Use alternate speed select regs */
		ad_write(mss, 22, (speed >> 8) & 0xff);	/* Speed MSB */
		ad_write(mss, 23, speed & 0xff);	/* Speed LSB */
		/* XXX must also do something in I27 for the ad1845 */
    	} else {
        	int i, sel = 0; /* assume entry 0 does not contain -1 */
        	static int speeds[] =
      	    	{8000, 5512, 16000, 11025, 27429, 18900, 32000, 22050,
	    	-1, 37800, -1, 44100, 48000, 33075, 9600, 6615};

        	for (i = 1; i < 16; i++)
   		    	if (speeds[i] > 0 &&
			    abs(speed-speeds[i]) < abs(speed-speeds[sel])) sel = i;
        	speed = speeds[sel];
        	ad_write(mss, 8, (ad_read(mss, 8) & 0xf0) | sel);
		ad_wait_init(mss, 10000);
    	}
    	ad_leave_MCE(mss);

    	return speed;
}

/*
 * mss_format checks that the format is supported (or defaults to AFMT_U8)
 * and returns the bit setting for the 1848 register corresponding to
 * the desired format.
 *
 * fixed lr970724
 */

static int
mss_format(struct mss_chinfo *ch, u_int32_t format)
{
    	struct mss_info *mss = ch->parent;
    	int i, arg = AFMT_ENCODING(format);

    	/*
     	* The data format uses 3 bits (just 2 on the 1848). For each
     	* bit setting, the following array returns the corresponding format.
     	* The code scans the array looking for a suitable format. In
     	* case it is not found, default to AFMT_U8 (not such a good
     	* choice, but let's do it for compatibility...).
     	*/

    	static int fmts[] =
        	{AFMT_U8, AFMT_MU_LAW, AFMT_S16_LE, AFMT_A_LAW,
		-1, AFMT_IMA_ADPCM, AFMT_U16_BE, -1};

	ch->fmt = format;
    	for (i = 0; i < 8; i++) if (arg == fmts[i]) break;
    	arg = i << 1;
    	if (AFMT_CHANNEL(format) > 1) arg |= 1;
    	arg <<= 4;
    	ad_enter_MCE(mss);
    	ad_write(mss, 8, (ad_read(mss, 8) & 0x0f) | arg);
	ad_wait_init(mss, 10000);
    	if (ad_read(mss, 12) & 0x40) {	/* mode2? */
		ad_write(mss, 28, arg); /* capture mode */
		ad_wait_init(mss, 10000);
	}
    	ad_leave_MCE(mss);
    	return format;
}

static int
mss_trigger(struct mss_chinfo *ch, int go)
{
    	struct mss_info *mss = ch->parent;
    	u_char m;
    	int retry, wr, cnt, ss;

	ss = 1;
	ss <<= (AFMT_CHANNEL(ch->fmt) > 1)? 1 : 0;
	ss <<= (ch->fmt & AFMT_16BIT)? 1 : 0;

	wr = (ch->dir == PCMDIR_PLAY)? 1 : 0;
    	m = ad_read(mss, 9);
    	switch (go) {
    	case PCMTRIG_START:
		cnt = (ch->blksz / ss) - 1;

		DEB(if (m & 4) printf("OUCH! reg 9 0x%02x\n", m););
		m |= wr? I9_PEN : I9_CEN; /* enable DMA */
		ad_write_cnt(mss, (wr || !FULL_DUPLEX(mss))? 14 : 30, cnt);
		break;

    	case PCMTRIG_STOP:
    	case PCMTRIG_ABORT: /* XXX check this... */
		m &= ~(wr? I9_PEN : I9_CEN); /* Stop DMA */
#if 0
		/*
	 	* try to disable DMA by clearing count registers. Not sure it
	 	* is needed, and it might cause false interrupts when the
	 	* DMA is re-enabled later.
	 	*/
		ad_write_cnt(mss, (wr || !FULL_DUPLEX(mss))? 14 : 30, 0);
#endif
    	}
    	/* on the OPTi931 the enable bit seems hard to set... */
    	for (retry = 10; retry > 0; retry--) {
        	ad_write(mss, 9, m);
        	if (ad_read(mss, 9) == m) break;
    	}
    	if (retry == 0) BVDDB(printf("stop dma, failed to set bit 0x%02x 0x%02x\n", \
			       m, ad_read(mss, 9)));
    	return 0;
}


/*
 * the opti931 seems to miss interrupts when working in full
 * duplex, so we try some heuristics to catch them.
 */
static void
opti931_intr(void *arg)
{
    	struct mss_info *mss = (struct mss_info *)arg;
    	u_char masked = 0, i11, mc11, c = 0;
    	u_char reason; /* b0 = playback, b1 = capture, b2 = timer */
    	int loops = 10;

#if 0
    	reason = io_rd(mss, MSS_STATUS);
    	if (!(reason & 1)) {/* no int, maybe a shared line ? */
		DEB(printf("intr: flag 0, mcir11 0x%02x\n", ad_read(mss, 11)));
		return;
    	}
#endif
	mss_lock(mss);
    	i11 = ad_read(mss, 11); /* XXX what's for ? */
	again:

    	c = mc11 = FULL_DUPLEX(mss)? opti_rd(mss, 11) : 0xc;
    	mc11 &= 0x0c;
    	if (c & 0x10) {
		DEB(printf("Warning: CD interrupt\n");)
		mc11 |= 0x10;
    	}
    	if (c & 0x20) {
		DEB(printf("Warning: MPU interrupt\n");)
		mc11 |= 0x20;
    	}
    	if (mc11 & masked) BVDDB(printf("irq reset failed, mc11 0x%02x, 0x%02x\n",\
                              	  mc11, masked));
    	masked |= mc11;
    	/*
     	* the nice OPTi931 sets the IRQ line before setting the bits in
     	* mc11. So, on some occasions I have to retry (max 10 times).
     	*/
    	if (mc11 == 0) { /* perhaps can return ... */
		reason = io_rd(mss, MSS_STATUS);
		if (reason & 1) {
	    		DEB(printf("one more try...\n");)
	    		if (--loops) goto again;
	    		else BVDDB(printf("intr, but mc11 not set\n");)
		}
		if (loops == 0) BVDDB(printf("intr, nothing in mcir11 0x%02x\n", mc11));
		mss_unlock(mss);
		return;
    	}

    	if (sndbuf_runsz(mss->rch.buffer) && (mc11 & 8)) {
		mss_unlock(mss);
		chn_intr(mss->rch.channel);
		mss_lock(mss);
	}
    	if (sndbuf_runsz(mss->pch.buffer) && (mc11 & 4)) {
		mss_unlock(mss);
		chn_intr(mss->pch.channel);
		mss_lock(mss);
	}
    	opti_wr(mss, 11, ~mc11); /* ack */
    	if (--loops) goto again;
	mss_unlock(mss);
    	DEB(printf("xxx too many loops\n");)
}

/* -------------------------------------------------------------------- */
/* channel interface */
static void *
msschan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct mss_info *mss = devinfo;
	struct mss_chinfo *ch = (dir == PCMDIR_PLAY)? &mss->pch : &mss->rch;

	ch->parent = mss;
	ch->channel = c;
	ch->buffer = b;
	ch->dir = dir;
	if (sndbuf_alloc(ch->buffer, mss->parent_dmat, 0, mss->bufsize) != 0)
		return NULL;
	sndbuf_dmasetup(ch->buffer, (dir == PCMDIR_PLAY)? mss->drq1 : mss->drq2);
	return ch;
}

static int
msschan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct mss_chinfo *ch = data;
	struct mss_info *mss = ch->parent;

	mss_lock(mss);
	mss_format(ch, format);
	mss_unlock(mss);
	return 0;
}

static u_int32_t
msschan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct mss_chinfo *ch = data;
	struct mss_info *mss = ch->parent;
	u_int32_t r;

	mss_lock(mss);
	r = mss_speed(ch, speed);
	mss_unlock(mss);

	return r;
}

static u_int32_t
msschan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct mss_chinfo *ch = data;

	ch->blksz = blocksize;
	sndbuf_resize(ch->buffer, 2, ch->blksz);

	return ch->blksz;
}

static int
msschan_trigger(kobj_t obj, void *data, int go)
{
	struct mss_chinfo *ch = data;
	struct mss_info *mss = ch->parent;

	if (!PCMTRIG_COMMON(go))
		return 0;

	sndbuf_dma(ch->buffer, go);
	mss_lock(mss);
	mss_trigger(ch, go);
	mss_unlock(mss);
	return 0;
}

static u_int32_t
msschan_getptr(kobj_t obj, void *data)
{
	struct mss_chinfo *ch = data;
	return sndbuf_dmaptr(ch->buffer);
}

static struct pcmchan_caps *
msschan_getcaps(kobj_t obj, void *data)
{
	struct mss_chinfo *ch = data;

	switch(ch->parent->bd_id) {
	case MD_OPTI931:
		return &opti931_caps;
		break;

	case MD_GUSPNP:
	case MD_GUSMAX:
		return &guspnp_caps;
		break;

	default:
		return &mss_caps;
		break;
	}
}

static kobj_method_t msschan_methods[] = {
    	KOBJMETHOD(channel_init,		msschan_init),
    	KOBJMETHOD(channel_setformat,		msschan_setformat),
    	KOBJMETHOD(channel_setspeed,		msschan_setspeed),
    	KOBJMETHOD(channel_setblocksize,	msschan_setblocksize),
    	KOBJMETHOD(channel_trigger,		msschan_trigger),
    	KOBJMETHOD(channel_getptr,		msschan_getptr),
    	KOBJMETHOD(channel_getcaps,		msschan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(msschan);

/* -------------------------------------------------------------------- */

/*
 * mss_probe() is the probe routine. Note, it is not necessary to
 * go through this for PnP devices, since they are already
 * indentified precisely using their PnP id.
 *
 * The base address supplied in the device refers to the old MSS
 * specs where the four 4 registers in io space contain configuration
 * information. Some boards (as an example, early MSS boards)
 * has such a block of registers, whereas others (generally CS42xx)
 * do not.  In order to distinguish between the two and do not have
 * to supply two separate probe routines, the flags entry in isa_device
 * has a bit to mark this.
 *
 */

static int
mss_probe(device_t dev)
{
    	u_char tmp, tmpx;
    	int flags, irq, drq, result = ENXIO, setres = 0;
    	struct mss_info *mss;

    	if (isa_get_logicalid(dev)) return ENXIO; /* not yet */

    	mss = (struct mss_info *)malloc(sizeof *mss, M_DEVBUF, M_NOWAIT | M_ZERO);
    	if (!mss) return ENXIO;

    	mss->io_rid = 0;
    	mss->conf_rid = -1;
    	mss->irq_rid = 0;
    	mss->drq1_rid = 0;
    	mss->drq2_rid = -1;
    	mss->io_base = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT,
    						&mss->io_rid, 8, RF_ACTIVE);
    	if (!mss->io_base) {
        	BVDDB(printf("mss_probe: no address given, try 0x%x\n", 0x530));
		mss->io_rid = 0;
		/* XXX verify this */
		setres = 1;
		bus_set_resource(dev, SYS_RES_IOPORT, mss->io_rid,
    		         	0x530, 8);
		mss->io_base = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT,
							&mss->io_rid,
							8, RF_ACTIVE);
    	}
    	if (!mss->io_base) goto no;

    	/* got irq/dma regs? */
    	flags = device_get_flags(dev);
    	irq = isa_get_irq(dev);
    	drq = isa_get_drq(dev);

    	if (!(device_get_flags(dev) & DV_F_TRUE_MSS)) goto mss_probe_end;

    	/*
     	* Check if the IO port returns valid signature. The original MS
     	* Sound system returns 0x04 while some cards
     	* (AudioTriX Pro for example) return 0x00 or 0x0f.
     	*/

    	device_set_desc(dev, "MSS");
    	tmpx = tmp = io_rd(mss, 3);
    	if (tmp == 0xff) {	/* Bus float */
		BVDDB(printf("I/O addr inactive (%x), try pseudo_mss\n", tmp));
		device_set_flags(dev, flags & ~DV_F_TRUE_MSS);
		goto mss_probe_end;
    	}
    	tmp &= 0x3f;
    	if (!(tmp == 0x04 || tmp == 0x0f || tmp == 0x00 || tmp == 0x05)) {
		BVDDB(printf("No MSS signature detected on port 0x%jx (0x%x)\n",
		     	rman_get_start(mss->io_base), tmpx));
		goto no;
    	}
    	if (irq > 11) {
		printf("MSS: Bad IRQ %d\n", irq);
		goto no;
    	}
    	if (!(drq == 0 || drq == 1 || drq == 3)) {
		printf("MSS: Bad DMA %d\n", drq);
		goto no;
    	}
    	if (tmpx & 0x80) {
		/* 8-bit board: only drq1/3 and irq7/9 */
		if (drq == 0) {
		    	printf("MSS: Can't use DMA0 with a 8 bit card/slot\n");
		    	goto no;
		}
		if (!(irq == 7 || irq == 9)) {
		    	printf("MSS: Can't use IRQ%d with a 8 bit card/slot\n",
			       irq);
		    	goto no;
		}
    	}
	mss_probe_end:
    	result = mss_detect(dev, mss);
	no:
    	mss_release_resources(mss, dev);
#if 0
    	if (setres) ISA_DELETE_RESOURCE(device_get_parent(dev), dev,
    				    	SYS_RES_IOPORT, mss->io_rid); /* XXX ? */
#endif
    	return result;
}

static int
mss_detect(device_t dev, struct mss_info *mss)
{
    	int          i;
    	u_char       tmp = 0, tmp1, tmp2;
    	char        *name, *yamaha;

    	if (mss->bd_id != 0) {
		device_printf(dev, "presel bd_id 0x%04x -- %s\n", mss->bd_id,
		      	device_get_desc(dev));
		return 0;
    	}

    	name = "AD1848";
    	mss->bd_id = MD_AD1848; /* AD1848 or CS4248 */

	if (opti_detect(dev, mss)) {
		switch (mss->bd_id) {
			case MD_OPTI924:
				name = "OPTi924";
				break;
			case MD_OPTI930:
				name = "OPTi930";
				break;
		}
		printf("Found OPTi device %s\n", name);
		if (opti_init(dev, mss) == 0) goto gotit;
	}

   	/*
     	* Check that the I/O address is in use.
     	*
     	* bit 7 of the base I/O port is known to be 0 after the chip has
     	* performed its power on initialization. Just assume this has
     	* happened before the OS is starting.
     	*
     	* If the I/O address is unused, it typically returns 0xff.
     	*/

    	for (i = 0; i < 10; i++)
		if ((tmp = io_rd(mss, MSS_INDEX)) & MSS_IDXBUSY) DELAY(10000);
		else break;

    	if (i >= 10) {	/* Not an AD1848 */
		BVDDB(printf("mss_detect, busy still set (0x%02x)\n", tmp));
		goto no;
    	}
    	/*
     	* Test if it's possible to change contents of the indirect
     	* registers. Registers 0 and 1 are ADC volume registers. The bit
     	* 0x10 is read only so try to avoid using it.
     	*/

    	ad_write(mss, 0, 0xaa);
    	ad_write(mss, 1, 0x45);/* 0x55 with bit 0x10 clear */
    	tmp1 = ad_read(mss, 0);
    	tmp2 = ad_read(mss, 1);
    	if (tmp1 != 0xaa || tmp2 != 0x45) {
		BVDDB(printf("mss_detect error - IREG (%x/%x)\n", tmp1, tmp2));
		goto no;
    	}

    	ad_write(mss, 0, 0x45);
    	ad_write(mss, 1, 0xaa);
    	tmp1 = ad_read(mss, 0);
    	tmp2 = ad_read(mss, 1);
    	if (tmp1 != 0x45 || tmp2 != 0xaa) {
		BVDDB(printf("mss_detect error - IREG2 (%x/%x)\n", tmp1, tmp2));
		goto no;
    	}

    	/*
     	* The indirect register I12 has some read only bits. Lets try to
     	* change them.
     	*/

    	tmp = ad_read(mss, 12);
    	ad_write(mss, 12, (~tmp) & 0x0f);
    	tmp1 = ad_read(mss, 12);

    	if ((tmp & 0x0f) != (tmp1 & 0x0f)) {
		BVDDB(printf("mss_detect - I12 (0x%02x was 0x%02x)\n", tmp1, tmp));
		goto no;
    	}

    	/*
     	* NOTE! Last 4 bits of the reg I12 tell the chip revision.
     	*	0x01=RevB
     	*  0x0A=RevC. also CS4231/CS4231A and OPTi931
     	*/

    	BVDDB(printf("mss_detect - chip revision 0x%02x\n", tmp & 0x0f);)

    	/*
     	* The original AD1848/CS4248 has just 16 indirect registers. This
     	* means that I0 and I16 should return the same value (etc.). Ensure
     	* that the Mode2 enable bit of I12 is 0. Otherwise this test fails
     	* with new parts.
     	*/

    	ad_write(mss, 12, 0);	/* Mode2=disabled */
#if 0
    	for (i = 0; i < 16; i++) {
		if ((tmp1 = ad_read(mss, i)) != (tmp2 = ad_read(mss, i + 16))) {
	    	BVDDB(printf("mss_detect warning - I%d: 0x%02x/0x%02x\n",
			i, tmp1, tmp2));
	    	/*
	     	* note - this seems to fail on the 4232 on I11. So we just break
	     	* rather than fail.  (which makes this test pointless - cg)
	     	*/
	    	break; /* return 0; */
		}
    	}
#endif
    	/*
     	* Try to switch the chip to mode2 (CS4231) by setting the MODE2 bit
     	* (0x40). The bit 0x80 is always 1 in CS4248 and CS4231.
     	*
     	* On the OPTi931, however, I12 is readonly and only contains the
     	* chip revision ID (as in the CS4231A). The upper bits return 0.
     	*/

    	ad_write(mss, 12, 0x40);	/* Set mode2, clear 0x80 */

    	tmp1 = ad_read(mss, 12);
    	if (tmp1 & 0x80) name = "CS4248"; /* Our best knowledge just now */
    	if ((tmp1 & 0xf0) == 0x00) {
		BVDDB(printf("this should be an OPTi931\n");)
    	} else if ((tmp1 & 0xc0) != 0xC0) goto gotit;
	/*
	* The 4231 has bit7=1 always, and bit6 we just set to 1.
	* We want to check that this is really a CS4231
	* Verify that setting I0 doesn't change I16.
	*/
	ad_write(mss, 16, 0);	/* Set I16 to known value */
	ad_write(mss, 0, 0x45);
	if ((tmp1 = ad_read(mss, 16)) == 0x45) goto gotit;

	ad_write(mss, 0, 0xaa);
       	if ((tmp1 = ad_read(mss, 16)) == 0xaa) {	/* Rotten bits? */
       		BVDDB(printf("mss_detect error - step H(%x)\n", tmp1));
		goto no;
	}
	/* Verify that some bits of I25 are read only. */
	tmp1 = ad_read(mss, 25);	/* Original bits */
	ad_write(mss, 25, ~tmp1);	/* Invert all bits */
	if ((ad_read(mss, 25) & 0xe7) == (tmp1 & 0xe7)) {
		int id;

		/* It's at least CS4231 */
		name = "CS4231";
		mss->bd_id = MD_CS42XX;

		/*
		* It could be an AD1845 or CS4231A as well.
		* CS4231 and AD1845 report the same revision info in I25
		* while the CS4231A reports different.
		*/

		id = ad_read(mss, 25) & 0xe7;
		/*
		* b7-b5 = version number;
		*	100 : all CS4231
		*	101 : CS4231A
		*
		* b2-b0 = chip id;
		*/
		switch (id) {

		case 0xa0:
			name = "CS4231A";
			mss->bd_id = MD_CS42XX;
		break;

		case 0xa2:
			name = "CS4232";
			mss->bd_id = MD_CS42XX;
		break;

		case 0xb2:
		/* strange: the 4231 data sheet says b4-b3 are XX
		* so this should be the same as 0xa2
		*/
			name = "CS4232A";
			mss->bd_id = MD_CS42XX;
		break;

		case 0x80:
			/*
			* It must be a CS4231 or AD1845. The register I23
			* of CS4231 is undefined and it appears to be read
			* only. AD1845 uses I23 for setting sample rate.
			* Assume the chip is AD1845 if I23 is changeable.
			*/

			tmp = ad_read(mss, 23);

			ad_write(mss, 23, ~tmp);
			if (ad_read(mss, 23) != tmp) {	/* AD1845 ? */
				name = "AD1845";
				mss->bd_id = MD_AD1845;
			}
			ad_write(mss, 23, tmp);	/* Restore */

			yamaha = ymf_test(dev, mss);
			if (yamaha) {
				mss->bd_id = MD_YM0020;
				name = yamaha;
			}
			break;

		case 0x83:	/* CS4236 */
		case 0x03:      /* CS4236 on Intel PR440FX motherboard XXX */
			name = "CS4236";
			mss->bd_id = MD_CS42XX;
			break;

		default:	/* Assume CS4231 */
	 		BVDDB(printf("unknown id 0x%02x, assuming CS4231\n", id);)
			mss->bd_id = MD_CS42XX;
		}
	}
	ad_write(mss, 25, tmp1);	/* Restore bits */
gotit:
    	BVDDB(printf("mss_detect() - Detected %s\n", name));
    	device_set_desc(dev, name);
    	device_set_flags(dev,
			 ((device_get_flags(dev) & ~DV_F_DEV_MASK) |
			  ((mss->bd_id << DV_F_DEV_SHIFT) & DV_F_DEV_MASK)));
    	return 0;
no:
    	return ENXIO;
}

static int
opti_detect(device_t dev, struct mss_info *mss)
{
	int c;
	static const struct opticard {
		int boardid;
		int passwdreg;
		int password;
		int base;
		int indir_reg;
	} cards[] = {
		{ MD_OPTI930, 0, 0xe4, 0xf8f, 0xe0e },	/* 930 */
		{ MD_OPTI924, 3, 0xe5, 0xf8c, 0,    },	/* 924 */
		{ 0 },
	};
	mss->conf_rid = 3;
	mss->indir_rid = 4;
	for (c = 0; cards[c].base; c++) {
		mss->optibase = cards[c].base;
		mss->password = cards[c].password;
		mss->passwdreg = cards[c].passwdreg;
		mss->bd_id = cards[c].boardid;

		if (cards[c].indir_reg)
			mss->indir = bus_alloc_resource(dev, SYS_RES_IOPORT,
				&mss->indir_rid, cards[c].indir_reg,
				cards[c].indir_reg+1, 1, RF_ACTIVE);

		mss->conf_base = bus_alloc_resource(dev, SYS_RES_IOPORT,
			&mss->conf_rid, mss->optibase, mss->optibase+9,
			9, RF_ACTIVE);

		if (opti_read(mss, 1) != 0xff) {
			return 1;
		} else {
			if (mss->indir)
				bus_release_resource(dev, SYS_RES_IOPORT, mss->indir_rid, mss->indir);
			mss->indir = NULL;
			if (mss->conf_base)
				bus_release_resource(dev, SYS_RES_IOPORT, mss->conf_rid, mss->conf_base);
			mss->conf_base = NULL;
		}
	}
	return 0;
}

static char *
ymf_test(device_t dev, struct mss_info *mss)
{
    	static int ports[] = {0x370, 0x310, 0x538};
    	int p, i, j, version;
    	static char *chipset[] = {
		NULL,			/* 0 */
		"OPL3-SA2 (YMF711)",	/* 1 */
		"OPL3-SA3 (YMF715)",	/* 2 */
		"OPL3-SA3 (YMF715)",	/* 3 */
		"OPL3-SAx (YMF719)",	/* 4 */
		"OPL3-SAx (YMF719)",	/* 5 */
		"OPL3-SAx (YMF719)",	/* 6 */
		"OPL3-SAx (YMF719)",	/* 7 */
    	};

    	for (p = 0; p < 3; p++) {
		mss->conf_rid = 1;
		mss->conf_base = bus_alloc_resource(dev,
					  	SYS_RES_IOPORT,
					  	&mss->conf_rid,
					  	ports[p], ports[p] + 1, 2,
					  	RF_ACTIVE);
		if (!mss->conf_base) return 0;

		/* Test the index port of the config registers */
		i = port_rd(mss->conf_base, 0);
		port_wr(mss->conf_base, 0, OPL3SAx_DMACONF);
		j = (port_rd(mss->conf_base, 0) == OPL3SAx_DMACONF)? 1 : 0;
		port_wr(mss->conf_base, 0, i);
		if (!j) {
	    		bus_release_resource(dev, SYS_RES_IOPORT,
			 		     mss->conf_rid, mss->conf_base);
	    		mss->conf_base = NULL;
	    		continue;
		}
		version = conf_rd(mss, OPL3SAx_MISC) & 0x07;
		return chipset[version];
    	}
    	return NULL;
}

static int
mss_doattach(device_t dev, struct mss_info *mss)
{
    	int pdma, rdma, flags = device_get_flags(dev);
    	char status[SND_STATUSLEN], status2[SND_STATUSLEN];

	mss->lock = snd_mtxcreate(device_get_nameunit(dev), "snd_mss softc");
	mss->bufsize = pcm_getbuffersize(dev, 4096, MSS_DEFAULT_BUFSZ, 65536);
    	if (!mss_alloc_resources(mss, dev)) goto no;
    	mss_init(mss, dev);
	pdma = rman_get_start(mss->drq1);
	rdma = rman_get_start(mss->drq2);
    	if (flags & DV_F_TRUE_MSS) {
		/* has IRQ/DMA registers, set IRQ and DMA addr */
		static char     interrupt_bits[12] =
	    	{-1, -1, -1, -1, -1, 0x28, -1, 0x08, -1, 0x10, 0x18, 0x20};
		static char     pdma_bits[4] =  {1, 2, -1, 3};
		static char	valid_rdma[4] = {1, 0, -1, 0};
		char		bits;

		if (!mss->irq || (bits = interrupt_bits[rman_get_start(mss->irq)]) == -1)
			goto no;
		io_wr(mss, 0, bits | 0x40);	/* config port */
		if ((io_rd(mss, 3) & 0x40) == 0) device_printf(dev, "IRQ Conflict?\n");
		/* Write IRQ+DMA setup */
		if (pdma_bits[pdma] == -1) goto no;
		bits |= pdma_bits[pdma];
		if (pdma != rdma) {
	    		if (rdma == valid_rdma[pdma]) bits |= 4;
	    		else {
				printf("invalid dual dma config %d:%d\n", pdma, rdma);
				goto no;
	    		}
		}
		io_wr(mss, 0, bits);
		printf("drq/irq conf %x\n", io_rd(mss, 0));
    	}
    	mixer_init(dev, (mss->bd_id == MD_YM0020)? &ymmix_mixer_class : &mssmix_mixer_class, mss);
    	switch (mss->bd_id) {
    	case MD_OPTI931:
		snd_setup_intr(dev, mss->irq, 0, opti931_intr, mss, &mss->ih);
		break;
    	default:
		snd_setup_intr(dev, mss->irq, 0, mss_intr, mss, &mss->ih);
    	}
    	if (pdma == rdma)
		pcm_setflags(dev, pcm_getflags(dev) | SD_F_SIMPLEX);
    	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev), /*alignment*/2,
			/*boundary*/0,
			/*lowaddr*/BUS_SPACE_MAXADDR_24BIT,
			/*highaddr*/BUS_SPACE_MAXADDR,
			/*filter*/NULL, /*filterarg*/NULL,
			/*maxsize*/mss->bufsize, /*nsegments*/1,
			/*maxsegz*/0x3ffff, /*flags*/0,
			/*lockfunc*/busdma_lock_mutex, /*lockarg*/&Giant,
			&mss->parent_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		goto no;
    	}

    	if (pdma != rdma)
		snprintf(status2, SND_STATUSLEN, ":%d", rdma);
	else
		status2[0] = '\0';

    	snprintf(status, SND_STATUSLEN, "at io 0x%jx irq %jd drq %d%s bufsz %u",
    	     	rman_get_start(mss->io_base), rman_get_start(mss->irq), pdma, status2, mss->bufsize);

    	if (pcm_register(dev, mss, 1, 1)) goto no;
    	pcm_addchan(dev, PCMDIR_REC, &msschan_class, mss);
    	pcm_addchan(dev, PCMDIR_PLAY, &msschan_class, mss);
    	pcm_setstatus(dev, status);

    	return 0;
no:
    	mss_release_resources(mss, dev);
    	return ENXIO;
}

static int
mss_detach(device_t dev)
{
	int r;
    	struct mss_info *mss;

	r = pcm_unregister(dev);
	if (r)
		return r;

	mss = pcm_getdevinfo(dev);
    	mss_release_resources(mss, dev);

	return 0;
}

static int
mss_attach(device_t dev)
{
    	struct mss_info *mss;
    	int flags = device_get_flags(dev);

    	mss = (struct mss_info *)malloc(sizeof *mss, M_DEVBUF, M_NOWAIT | M_ZERO);
    	if (!mss) return ENXIO;

    	mss->io_rid = 0;
    	mss->conf_rid = -1;
    	mss->irq_rid = 0;
    	mss->drq1_rid = 0;
    	mss->drq2_rid = -1;
    	if (flags & DV_F_DUAL_DMA) {
        	bus_set_resource(dev, SYS_RES_DRQ, 1,
    		         	 flags & DV_F_DRQ_MASK, 1);
		mss->drq2_rid = 1;
    	}
    	mss->bd_id = (device_get_flags(dev) & DV_F_DEV_MASK) >> DV_F_DEV_SHIFT;
    	if (mss->bd_id == MD_YM0020) ymf_test(dev, mss);
    	return mss_doattach(dev, mss);
}

/*
 * mss_resume() is the code to allow a laptop to resume using the sound
 * card.
 *
 * This routine re-sets the state of the board to the state before going
 * to sleep.  According to the yamaha docs this is the right thing to do,
 * but getting DMA restarted appears to be a bit of a trick, so the device
 * has to be closed and re-opened to be re-used, but there is no skipping
 * problem, and volume, bass/treble and most other things are restored
 * properly.
 *
 */

static int
mss_resume(device_t dev)
{
    	/*
     	 * Restore the state taken below.
     	 */
    	struct mss_info *mss;
    	int i;

    	mss = pcm_getdevinfo(dev);

    	if(mss->bd_id == MD_YM0020 || mss->bd_id == MD_CS423X) {
		/* This works on a Toshiba Libretto 100CT. */
		for (i = 0; i < MSS_INDEXED_REGS; i++)
    			ad_write(mss, i, mss->mss_indexed_regs[i]);
		for (i = 0; i < OPL_INDEXED_REGS; i++)
    			conf_wr(mss, i, mss->opl_indexed_regs[i]);
		mss_intr(mss);
    	}

	if (mss->bd_id == MD_CS423X) {
		/* Needed on IBM Thinkpad 600E */
		mss_lock(mss);
		mss_format(&mss->pch, mss->pch.channel->format);
		mss_speed(&mss->pch, mss->pch.channel->speed);
		mss_unlock(mss);
	}

    	return 0;

}

/*
 * mss_suspend() is the code that gets called right before a laptop
 * suspends.
 *
 * This code saves the state of the sound card right before shutdown
 * so it can be restored above.
 *
 */

static int
mss_suspend(device_t dev)
{
    	int i;
    	struct mss_info *mss;

    	mss = pcm_getdevinfo(dev);

    	if(mss->bd_id == MD_YM0020 || mss->bd_id == MD_CS423X)
    	{
		/* this stops playback. */
		conf_wr(mss, 0x12, 0x0c);
		for(i = 0; i < MSS_INDEXED_REGS; i++)
    			mss->mss_indexed_regs[i] = ad_read(mss, i);
		for(i = 0; i < OPL_INDEXED_REGS; i++)
    			mss->opl_indexed_regs[i] = conf_rd(mss, i);
		mss->opl_indexed_regs[0x12] = 0x0;
    	}
    	return 0;
}

static device_method_t mss_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mss_probe),
	DEVMETHOD(device_attach,	mss_attach),
	DEVMETHOD(device_detach,	mss_detach),
	DEVMETHOD(device_suspend,       mss_suspend),
	DEVMETHOD(device_resume,        mss_resume),

	{ 0, 0 }
};

static driver_t mss_driver = {
	"pcm",
	mss_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_mss, isa, mss_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_mss, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_mss, 1);

static int
azt2320_mss_mode(struct mss_info *mss, device_t dev)
{
	struct resource *sbport;
	int		i, ret, rid;

	rid = 0;
	ret = -1;
	sbport = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid, RF_ACTIVE);
	if (sbport) {
		for (i = 0; i < 1000; i++) {
			if ((port_rd(sbport, SBDSP_STATUS) & 0x80))
				DELAY((i > 100) ? 1000 : 10);
			else {
				port_wr(sbport, SBDSP_CMD, 0x09);
				break;
			}
		}
		for (i = 0; i < 1000; i++) {
			if ((port_rd(sbport, SBDSP_STATUS) & 0x80))
				DELAY((i > 100) ? 1000 : 10);
			else {
				port_wr(sbport, SBDSP_CMD, 0x00);
				ret = 0;
				break;
			}
		}
		DELAY(1000);
		bus_release_resource(dev, SYS_RES_IOPORT, rid, sbport);
	}
	return ret;
}

static struct isa_pnp_id pnpmss_ids[] = {
	{0x0000630e, "CS423x"},				/* CSC0000 */
	{0x0001630e, "CS423x-PCI"},			/* CSC0100 */
    	{0x01000000, "CMI8330"},			/* @@@0001 */
	{0x2100a865, "Yamaha OPL-SAx"},			/* YMH0021 */
	{0x1110d315, "ENSONIQ SoundscapeVIVO"},		/* ENS1011 */
	{0x1093143e, "OPTi931"},			/* OPT9310 */
	{0x5092143e, "OPTi925"},			/* OPT9250 XXX guess */
	{0x0000143e, "OPTi924"},			/* OPT0924 */
	{0x1022b839, "Neomagic 256AV (non-ac97)"},	/* NMX2210 */
	{0x01005407, "Aztech 2320"},			/* AZT0001 */
#if 0
	{0x0000561e, "GusPnP"},				/* GRV0000 */
#endif
	{0},
};

static int
pnpmss_probe(device_t dev)
{
	u_int32_t lid, vid;

	lid = isa_get_logicalid(dev);
	vid = isa_get_vendorid(dev);
	if (lid == 0x01000000 && vid != 0x0100a90d) /* CMI0001 */
		return ENXIO;
	return ISA_PNP_PROBE(device_get_parent(dev), dev, pnpmss_ids);
}

static int
pnpmss_attach(device_t dev)
{
	struct mss_info *mss;

	mss = malloc(sizeof(*mss), M_DEVBUF, M_WAITOK | M_ZERO);
	mss->io_rid = 0;
	mss->conf_rid = -1;
	mss->irq_rid = 0;
	mss->drq1_rid = 0;
	mss->drq2_rid = 1;
	mss->bd_id = MD_CS42XX;

	switch (isa_get_logicalid(dev)) {
	case 0x0000630e:			/* CSC0000 */
	case 0x0001630e:			/* CSC0100 */
	    mss->bd_flags |= BD_F_MSS_OFFSET;
	    mss->bd_id = MD_CS423X;
	    break;

	case 0x2100a865:			/* YHM0021 */
	    mss->io_rid = 1;
	    mss->conf_rid = 4;
	    mss->bd_id = MD_YM0020;
	    break;

	case 0x1110d315:			/* ENS1011 */
	    mss->io_rid = 1;
	    mss->bd_id = MD_VIVO;
	    break;

	case 0x1093143e:			/* OPT9310 */
            mss->bd_flags |= BD_F_MSS_OFFSET;
    	    mss->conf_rid = 3;
            mss->bd_id = MD_OPTI931;
	    break;

	case 0x5092143e:			/* OPT9250 XXX guess */
            mss->io_rid = 1;
            mss->conf_rid = 3;
	    mss->bd_id = MD_OPTI925;
	    break;

	case 0x0000143e:			/* OPT0924 */
	    mss->password = 0xe5;
	    mss->passwdreg = 3;
	    mss->optibase = 0xf0c;
	    mss->io_rid = 2;
	    mss->conf_rid = 3;
	    mss->bd_id = MD_OPTI924;
	    mss->bd_flags |= BD_F_924PNP;
	    if(opti_init(dev, mss) != 0) {
		    free(mss, M_DEVBUF);
		    return ENXIO;
	    }
	    break;

	case 0x1022b839:			/* NMX2210 */
	    mss->io_rid = 1;
	    break;

	case 0x01005407:			/* AZT0001 */
	    /* put into MSS mode first (snatched from NetBSD) */
	    if (azt2320_mss_mode(mss, dev) == -1) {
		    free(mss, M_DEVBUF);
		    return ENXIO;
	    }

	    mss->bd_flags |= BD_F_MSS_OFFSET;
	    mss->io_rid = 2;
	    break;
	    
#if 0
	case 0x0000561e:			/* GRV0000 */
	    mss->bd_flags |= BD_F_MSS_OFFSET;
            mss->io_rid = 2;
            mss->conf_rid = 1;
	    mss->drq1_rid = 1;
	    mss->drq2_rid = 0;
            mss->bd_id = MD_GUSPNP;
	    break;
#endif
	case 0x01000000:			/* @@@0001 */
	    mss->drq2_rid = -1;
            break;

	/* Unknown MSS default.  We could let the CSC0000 stuff match too */
        default:
	    mss->bd_flags |= BD_F_MSS_OFFSET;
	    break;
	}
    	return mss_doattach(dev, mss);
}

static int
opti_init(device_t dev, struct mss_info *mss)
{
	int flags = device_get_flags(dev);
	int basebits = 0;

	if (!mss->conf_base) {
		bus_set_resource(dev, SYS_RES_IOPORT, mss->conf_rid,
			mss->optibase, 0x9);

		mss->conf_base = bus_alloc_resource(dev, SYS_RES_IOPORT,
			&mss->conf_rid, mss->optibase, mss->optibase+0x9,
			0x9, RF_ACTIVE);
	}

	if (!mss->conf_base)
		return ENXIO;

	if (!mss->io_base)
		mss->io_base = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT,
			&mss->io_rid, 8, RF_ACTIVE);

	if (!mss->io_base)	/* No hint specified, use 0x530 */
		mss->io_base = bus_alloc_resource(dev, SYS_RES_IOPORT,
			&mss->io_rid, 0x530, 0x537, 8, RF_ACTIVE);

	if (!mss->io_base)
		return ENXIO;

	switch (rman_get_start(mss->io_base)) {
		case 0x530:
			basebits = 0x0;
			break;
		case 0xe80:
			basebits = 0x10;
			break;
		case 0xf40:
			basebits = 0x20;
			break;
		case 0x604:
			basebits = 0x30;
			break;
		default:
			printf("opti_init: invalid MSS base address!\n");
			return ENXIO;
	}


	switch (mss->bd_id) {
	case MD_OPTI924:
		opti_write(mss, 1, 0x80 | basebits);	/* MSS mode */
		opti_write(mss, 2, 0x00);	/* Disable CD */
		opti_write(mss, 3, 0xf0);	/* Disable SB IRQ */
		opti_write(mss, 4, 0xf0);
		opti_write(mss, 5, 0x00);
		opti_write(mss, 6, 0x02);	/* MPU stuff */
		break;

	case MD_OPTI930:
		opti_write(mss, 1, 0x00 | basebits);
		opti_write(mss, 3, 0x00);	/* Disable SB IRQ/DMA */
		opti_write(mss, 4, 0x52);	/* Empty FIFO */
		opti_write(mss, 5, 0x3c);	/* Mode 2 */
		opti_write(mss, 6, 0x02);	/* Enable MSS */
		break;
	}

	if (mss->bd_flags & BD_F_924PNP) {
		u_int32_t irq = isa_get_irq(dev);
		u_int32_t drq = isa_get_drq(dev);
		bus_set_resource(dev, SYS_RES_IRQ, 0, irq, 1);
		bus_set_resource(dev, SYS_RES_DRQ, mss->drq1_rid, drq, 1);
		if (flags & DV_F_DUAL_DMA) {
			bus_set_resource(dev, SYS_RES_DRQ, 1,
				flags & DV_F_DRQ_MASK, 1);
			mss->drq2_rid = 1;
		}
	}

	/* OPTixxx has I/DRQ registers */

	device_set_flags(dev, device_get_flags(dev) | DV_F_TRUE_MSS);

	return 0;
}

static void
opti_write(struct mss_info *mss, u_char reg, u_char val)
{
	port_wr(mss->conf_base, mss->passwdreg, mss->password);

	switch(mss->bd_id) {
	case MD_OPTI924:
		if (reg > 7) {		/* Indirect register */
			port_wr(mss->conf_base, mss->passwdreg, reg);
			port_wr(mss->conf_base, mss->passwdreg,
				mss->password);
			port_wr(mss->conf_base, 9, val);
			return;
		}
		port_wr(mss->conf_base, reg, val);
		break;

	case MD_OPTI930:
		port_wr(mss->indir, 0, reg);
		port_wr(mss->conf_base, mss->passwdreg, mss->password);
		port_wr(mss->indir, 1, val);
		break;
	}
}

u_char
opti_read(struct mss_info *mss, u_char reg)
{
	port_wr(mss->conf_base, mss->passwdreg, mss->password);

	switch(mss->bd_id) {
	case MD_OPTI924:
		if (reg > 7) {		/* Indirect register */
			port_wr(mss->conf_base, mss->passwdreg, reg);
			port_wr(mss->conf_base, mss->passwdreg, mss->password);
			return(port_rd(mss->conf_base, 9));
		}
		return(port_rd(mss->conf_base, reg));
		break;

	case MD_OPTI930:
		port_wr(mss->indir, 0, reg);
		port_wr(mss->conf_base, mss->passwdreg, mss->password);
		return port_rd(mss->indir, 1);
		break;
	}
	return -1;
}

static device_method_t pnpmss_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pnpmss_probe),
	DEVMETHOD(device_attach,	pnpmss_attach),
	DEVMETHOD(device_detach,	mss_detach),
	DEVMETHOD(device_suspend,       mss_suspend),
	DEVMETHOD(device_resume,        mss_resume),

	{ 0, 0 }
};

static driver_t pnpmss_driver = {
	"pcm",
	pnpmss_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_pnpmss, isa, pnpmss_driver, pcm_devclass, 0, 0);
DRIVER_MODULE(snd_pnpmss, acpi, pnpmss_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_pnpmss, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_pnpmss, 1);

static int
guspcm_probe(device_t dev)
{
	struct sndcard_func *func;

	func = device_get_ivars(dev);
	if (func == NULL || func->func != SCF_PCM)
		return ENXIO;

	device_set_desc(dev, "GUS CS4231");
	return 0;
}

static int
guspcm_attach(device_t dev)
{
	device_t parent = device_get_parent(dev);
	struct mss_info *mss;
	int base, flags;
	unsigned char ctl;

	mss = (struct mss_info *)malloc(sizeof *mss, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (mss == NULL)
		return ENOMEM;

	mss->bd_flags = BD_F_MSS_OFFSET;
	mss->io_rid = 2;
	mss->conf_rid = 1;
	mss->irq_rid = 0;
	mss->drq1_rid = 1;
	mss->drq2_rid = -1;

	if (isa_get_logicalid(parent) == 0)
		mss->bd_id = MD_GUSMAX;
	else {
		mss->bd_id = MD_GUSPNP;
		mss->drq2_rid = 0;
		goto skip_setup;
	}

	flags = device_get_flags(parent);
	if (flags & DV_F_DUAL_DMA)
		mss->drq2_rid = 0;

	mss->conf_base = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT,
						     &mss->conf_rid,
						     8, RF_ACTIVE);

	if (mss->conf_base == NULL) {
		mss_release_resources(mss, dev);
		return ENXIO;
	}

	base = isa_get_port(parent);

	ctl = 0x40;			/* CS4231 enable */
	if (isa_get_drq(dev) > 3)
		ctl |= 0x10;		/* 16-bit dma channel 1 */
	if ((flags & DV_F_DUAL_DMA) != 0 && (flags & DV_F_DRQ_MASK) > 3)
		ctl |= 0x20;		/* 16-bit dma channel 2 */
	ctl |= (base >> 4) & 0x0f;	/* 2X0 -> 3XC */
	port_wr(mss->conf_base, 6, ctl);

skip_setup:
	return mss_doattach(dev, mss);
}

static device_method_t guspcm_methods[] = {
	DEVMETHOD(device_probe,		guspcm_probe),
	DEVMETHOD(device_attach,	guspcm_attach),
	DEVMETHOD(device_detach,	mss_detach),

	{ 0, 0 }
};

static driver_t guspcm_driver = {
	"pcm",
	guspcm_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_guspcm, gusc, guspcm_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_guspcm, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_guspcm, 1);
ISA_PNP_INFO(pnpmss_ids);
