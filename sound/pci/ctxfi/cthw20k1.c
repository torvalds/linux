/**
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * This source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution for the license terms and conditions.
 *
 * @File	cthw20k1.c
 *
 * @Brief
 * This file contains the implementation of hardware access methord for 20k1.
 *
 * @Author	Liu Chun
 * @Date 	Jun 24 2008
 *
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include "cthw20k1.h"
#include "ct20k1reg.h"

#if BITS_PER_LONG == 32
#define CT_XFI_DMA_MASK		DMA_BIT_MASK(32) /* 32 bit PTE */
#else
#define CT_XFI_DMA_MASK		DMA_BIT_MASK(64) /* 64 bit PTE */
#endif

struct hw20k1 {
	struct hw hw;
	spinlock_t reg_20k1_lock;
	spinlock_t reg_pci_lock;
};

static u32 hw_read_20kx(struct hw *hw, u32 reg);
static void hw_write_20kx(struct hw *hw, u32 reg, u32 data);
static u32 hw_read_pci(struct hw *hw, u32 reg);
static void hw_write_pci(struct hw *hw, u32 reg, u32 data);

/*
 * Type definition block.
 * The layout of control structures can be directly applied on 20k2 chip.
 */

/*
 * SRC control block definitions.
 */

/* SRC resource control block */
#define SRCCTL_STATE	0x00000007
#define SRCCTL_BM	0x00000008
#define SRCCTL_RSR	0x00000030
#define SRCCTL_SF	0x000001C0
#define SRCCTL_WR	0x00000200
#define SRCCTL_PM	0x00000400
#define SRCCTL_ROM	0x00001800
#define SRCCTL_VO	0x00002000
#define SRCCTL_ST	0x00004000
#define SRCCTL_IE	0x00008000
#define SRCCTL_ILSZ	0x000F0000
#define SRCCTL_BP	0x00100000

#define SRCCCR_CISZ	0x000007FF
#define SRCCCR_CWA	0x001FF800
#define SRCCCR_D	0x00200000
#define SRCCCR_RS	0x01C00000
#define SRCCCR_NAL	0x3E000000
#define SRCCCR_RA	0xC0000000

#define SRCCA_CA	0x03FFFFFF
#define SRCCA_RS	0x1C000000
#define SRCCA_NAL	0xE0000000

#define SRCSA_SA	0x03FFFFFF

#define SRCLA_LA	0x03FFFFFF

/* Mixer Parameter Ring ram Low and Hight register.
 * Fixed-point value in 8.24 format for parameter channel */
#define MPRLH_PITCH	0xFFFFFFFF

/* SRC resource register dirty flags */
union src_dirty {
	struct {
		u16 ctl:1;
		u16 ccr:1;
		u16 sa:1;
		u16 la:1;
		u16 ca:1;
		u16 mpr:1;
		u16 czbfs:1;	/* Clear Z-Buffers */
		u16 rsv:9;
	} bf;
	u16 data;
};

struct src_rsc_ctrl_blk {
	unsigned int	ctl;
	unsigned int 	ccr;
	unsigned int	ca;
	unsigned int	sa;
	unsigned int	la;
	unsigned int	mpr;
	union src_dirty	dirty;
};

/* SRC manager control block */
union src_mgr_dirty {
	struct {
		u16 enb0:1;
		u16 enb1:1;
		u16 enb2:1;
		u16 enb3:1;
		u16 enb4:1;
		u16 enb5:1;
		u16 enb6:1;
		u16 enb7:1;
		u16 enbsa:1;
		u16 rsv:7;
	} bf;
	u16 data;
};

struct src_mgr_ctrl_blk {
	unsigned int		enbsa;
	unsigned int		enb[8];
	union src_mgr_dirty	dirty;
};

/* SRCIMP manager control block */
#define SRCAIM_ARC	0x00000FFF
#define SRCAIM_NXT	0x00FF0000
#define SRCAIM_SRC	0xFF000000

struct srcimap {
	unsigned int srcaim;
	unsigned int idx;
};

/* SRCIMP manager register dirty flags */
union srcimp_mgr_dirty {
	struct {
		u16 srcimap:1;
		u16 rsv:15;
	} bf;
	u16 data;
};

struct srcimp_mgr_ctrl_blk {
	struct srcimap		srcimap;
	union srcimp_mgr_dirty	dirty;
};

/*
 * Function implementation block.
 */

static int src_get_rsc_ctrl_blk(void **rblk)
{
	struct src_rsc_ctrl_blk *blk;

	*rblk = NULL;
	blk = kzalloc(sizeof(*blk), GFP_KERNEL);
	if (!blk)
		return -ENOMEM;

	*rblk = blk;

	return 0;
}

static int src_put_rsc_ctrl_blk(void *blk)
{
	kfree((struct src_rsc_ctrl_blk *)blk);

	return 0;
}

static int src_set_state(void *blk, unsigned int state)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->ctl, SRCCTL_STATE, state);
	ctl->dirty.bf.ctl = 1;
	return 0;
}

static int src_set_bm(void *blk, unsigned int bm)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->ctl, SRCCTL_BM, bm);
	ctl->dirty.bf.ctl = 1;
	return 0;
}

static int src_set_rsr(void *blk, unsigned int rsr)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->ctl, SRCCTL_RSR, rsr);
	ctl->dirty.bf.ctl = 1;
	return 0;
}

static int src_set_sf(void *blk, unsigned int sf)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->ctl, SRCCTL_SF, sf);
	ctl->dirty.bf.ctl = 1;
	return 0;
}

static int src_set_wr(void *blk, unsigned int wr)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->ctl, SRCCTL_WR, wr);
	ctl->dirty.bf.ctl = 1;
	return 0;
}

static int src_set_pm(void *blk, unsigned int pm)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->ctl, SRCCTL_PM, pm);
	ctl->dirty.bf.ctl = 1;
	return 0;
}

static int src_set_rom(void *blk, unsigned int rom)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->ctl, SRCCTL_ROM, rom);
	ctl->dirty.bf.ctl = 1;
	return 0;
}

static int src_set_vo(void *blk, unsigned int vo)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->ctl, SRCCTL_VO, vo);
	ctl->dirty.bf.ctl = 1;
	return 0;
}

static int src_set_st(void *blk, unsigned int st)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->ctl, SRCCTL_ST, st);
	ctl->dirty.bf.ctl = 1;
	return 0;
}

static int src_set_ie(void *blk, unsigned int ie)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->ctl, SRCCTL_IE, ie);
	ctl->dirty.bf.ctl = 1;
	return 0;
}

static int src_set_ilsz(void *blk, unsigned int ilsz)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->ctl, SRCCTL_ILSZ, ilsz);
	ctl->dirty.bf.ctl = 1;
	return 0;
}

static int src_set_bp(void *blk, unsigned int bp)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->ctl, SRCCTL_BP, bp);
	ctl->dirty.bf.ctl = 1;
	return 0;
}

static int src_set_cisz(void *blk, unsigned int cisz)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->ccr, SRCCCR_CISZ, cisz);
	ctl->dirty.bf.ccr = 1;
	return 0;
}

static int src_set_ca(void *blk, unsigned int ca)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->ca, SRCCA_CA, ca);
	ctl->dirty.bf.ca = 1;
	return 0;
}

static int src_set_sa(void *blk, unsigned int sa)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->sa, SRCSA_SA, sa);
	ctl->dirty.bf.sa = 1;
	return 0;
}

static int src_set_la(void *blk, unsigned int la)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->la, SRCLA_LA, la);
	ctl->dirty.bf.la = 1;
	return 0;
}

static int src_set_pitch(void *blk, unsigned int pitch)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->mpr, MPRLH_PITCH, pitch);
	ctl->dirty.bf.mpr = 1;
	return 0;
}

static int src_set_clear_zbufs(void *blk, unsigned int clear)
{
	((struct src_rsc_ctrl_blk *)blk)->dirty.bf.czbfs = (clear ? 1 : 0);
	return 0;
}

static int src_set_dirty(void *blk, unsigned int flags)
{
	((struct src_rsc_ctrl_blk *)blk)->dirty.data = (flags & 0xffff);
	return 0;
}

static int src_set_dirty_all(void *blk)
{
	((struct src_rsc_ctrl_blk *)blk)->dirty.data = ~(0x0);
	return 0;
}

#define AR_SLOT_SIZE		4096
#define AR_SLOT_BLOCK_SIZE	16
#define AR_PTS_PITCH		6
#define AR_PARAM_SRC_OFFSET	0x60

static unsigned int src_param_pitch_mixer(unsigned int src_idx)
{
	return ((src_idx << 4) + AR_PTS_PITCH + AR_SLOT_SIZE
			- AR_PARAM_SRC_OFFSET) % AR_SLOT_SIZE;

}

static int src_commit_write(struct hw *hw, unsigned int idx, void *blk)
{
	struct src_rsc_ctrl_blk *ctl = blk;
	int i;

	if (ctl->dirty.bf.czbfs) {
		/* Clear Z-Buffer registers */
		for (i = 0; i < 8; i++)
			hw_write_20kx(hw, SRCUPZ+idx*0x100+i*0x4, 0);

		for (i = 0; i < 4; i++)
			hw_write_20kx(hw, SRCDN0Z+idx*0x100+i*0x4, 0);

		for (i = 0; i < 8; i++)
			hw_write_20kx(hw, SRCDN1Z+idx*0x100+i*0x4, 0);

		ctl->dirty.bf.czbfs = 0;
	}
	if (ctl->dirty.bf.mpr) {
		/* Take the parameter mixer resource in the same group as that
		 * the idx src is in for simplicity. Unlike src, all conjugate
		 * parameter mixer resources must be programmed for
		 * corresponding conjugate src resources. */
		unsigned int pm_idx = src_param_pitch_mixer(idx);
		hw_write_20kx(hw, PRING_LO_HI+4*pm_idx, ctl->mpr);
		hw_write_20kx(hw, PMOPLO+8*pm_idx, 0x3);
		hw_write_20kx(hw, PMOPHI+8*pm_idx, 0x0);
		ctl->dirty.bf.mpr = 0;
	}
	if (ctl->dirty.bf.sa) {
		hw_write_20kx(hw, SRCSA+idx*0x100, ctl->sa);
		ctl->dirty.bf.sa = 0;
	}
	if (ctl->dirty.bf.la) {
		hw_write_20kx(hw, SRCLA+idx*0x100, ctl->la);
		ctl->dirty.bf.la = 0;
	}
	if (ctl->dirty.bf.ca) {
		hw_write_20kx(hw, SRCCA+idx*0x100, ctl->ca);
		ctl->dirty.bf.ca = 0;
	}

	/* Write srccf register */
	hw_write_20kx(hw, SRCCF+idx*0x100, 0x0);

	if (ctl->dirty.bf.ccr) {
		hw_write_20kx(hw, SRCCCR+idx*0x100, ctl->ccr);
		ctl->dirty.bf.ccr = 0;
	}
	if (ctl->dirty.bf.ctl) {
		hw_write_20kx(hw, SRCCTL+idx*0x100, ctl->ctl);
		ctl->dirty.bf.ctl = 0;
	}

	return 0;
}

static int src_get_ca(struct hw *hw, unsigned int idx, void *blk)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	ctl->ca = hw_read_20kx(hw, SRCCA+idx*0x100);
	ctl->dirty.bf.ca = 0;

	return get_field(ctl->ca, SRCCA_CA);
}

static unsigned int src_get_dirty(void *blk)
{
	return ((struct src_rsc_ctrl_blk *)blk)->dirty.data;
}

static unsigned int src_dirty_conj_mask(void)
{
	return 0x20;
}

static int src_mgr_enbs_src(void *blk, unsigned int idx)
{
	((struct src_mgr_ctrl_blk *)blk)->enbsa = ~(0x0);
	((struct src_mgr_ctrl_blk *)blk)->dirty.bf.enbsa = 1;
	((struct src_mgr_ctrl_blk *)blk)->enb[idx/32] |= (0x1 << (idx%32));
	return 0;
}

static int src_mgr_enb_src(void *blk, unsigned int idx)
{
	((struct src_mgr_ctrl_blk *)blk)->enb[idx/32] |= (0x1 << (idx%32));
	((struct src_mgr_ctrl_blk *)blk)->dirty.data |= (0x1 << (idx/32));
	return 0;
}

static int src_mgr_dsb_src(void *blk, unsigned int idx)
{
	((struct src_mgr_ctrl_blk *)blk)->enb[idx/32] &= ~(0x1 << (idx%32));
	((struct src_mgr_ctrl_blk *)blk)->dirty.data |= (0x1 << (idx/32));
	return 0;
}

static int src_mgr_commit_write(struct hw *hw, void *blk)
{
	struct src_mgr_ctrl_blk *ctl = blk;
	int i;
	unsigned int ret;

	if (ctl->dirty.bf.enbsa) {
		do {
			ret = hw_read_20kx(hw, SRCENBSTAT);
		} while (ret & 0x1);
		hw_write_20kx(hw, SRCENBS, ctl->enbsa);
		ctl->dirty.bf.enbsa = 0;
	}
	for (i = 0; i < 8; i++) {
		if ((ctl->dirty.data & (0x1 << i))) {
			hw_write_20kx(hw, SRCENB+(i*0x100), ctl->enb[i]);
			ctl->dirty.data &= ~(0x1 << i);
		}
	}

	return 0;
}

static int src_mgr_get_ctrl_blk(void **rblk)
{
	struct src_mgr_ctrl_blk *blk;

	*rblk = NULL;
	blk = kzalloc(sizeof(*blk), GFP_KERNEL);
	if (!blk)
		return -ENOMEM;

	*rblk = blk;

	return 0;
}

static int src_mgr_put_ctrl_blk(void *blk)
{
	kfree((struct src_mgr_ctrl_blk *)blk);

	return 0;
}

static int srcimp_mgr_get_ctrl_blk(void **rblk)
{
	struct srcimp_mgr_ctrl_blk *blk;

	*rblk = NULL;
	blk = kzalloc(sizeof(*blk), GFP_KERNEL);
	if (!blk)
		return -ENOMEM;

	*rblk = blk;

	return 0;
}

static int srcimp_mgr_put_ctrl_blk(void *blk)
{
	kfree((struct srcimp_mgr_ctrl_blk *)blk);

	return 0;
}

static int srcimp_mgr_set_imaparc(void *blk, unsigned int slot)
{
	struct srcimp_mgr_ctrl_blk *ctl = blk;

	set_field(&ctl->srcimap.srcaim, SRCAIM_ARC, slot);
	ctl->dirty.bf.srcimap = 1;
	return 0;
}

static int srcimp_mgr_set_imapuser(void *blk, unsigned int user)
{
	struct srcimp_mgr_ctrl_blk *ctl = blk;

	set_field(&ctl->srcimap.srcaim, SRCAIM_SRC, user);
	ctl->dirty.bf.srcimap = 1;
	return 0;
}

static int srcimp_mgr_set_imapnxt(void *blk, unsigned int next)
{
	struct srcimp_mgr_ctrl_blk *ctl = blk;

	set_field(&ctl->srcimap.srcaim, SRCAIM_NXT, next);
	ctl->dirty.bf.srcimap = 1;
	return 0;
}

static int srcimp_mgr_set_imapaddr(void *blk, unsigned int addr)
{
	struct srcimp_mgr_ctrl_blk *ctl = blk;

	ctl->srcimap.idx = addr;
	ctl->dirty.bf.srcimap = 1;
	return 0;
}

static int srcimp_mgr_commit_write(struct hw *hw, void *blk)
{
	struct srcimp_mgr_ctrl_blk *ctl = blk;

	if (ctl->dirty.bf.srcimap) {
		hw_write_20kx(hw, SRCIMAP+ctl->srcimap.idx*0x100,
						ctl->srcimap.srcaim);
		ctl->dirty.bf.srcimap = 0;
	}

	return 0;
}

/*
 * AMIXER control block definitions.
 */

#define AMOPLO_M	0x00000003
#define AMOPLO_X	0x0003FFF0
#define AMOPLO_Y	0xFFFC0000

#define AMOPHI_SADR	0x000000FF
#define AMOPHI_SE	0x80000000

/* AMIXER resource register dirty flags */
union amixer_dirty {
	struct {
		u16 amoplo:1;
		u16 amophi:1;
		u16 rsv:14;
	} bf;
	u16 data;
};

/* AMIXER resource control block */
struct amixer_rsc_ctrl_blk {
	unsigned int		amoplo;
	unsigned int		amophi;
	union amixer_dirty	dirty;
};

static int amixer_set_mode(void *blk, unsigned int mode)
{
	struct amixer_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->amoplo, AMOPLO_M, mode);
	ctl->dirty.bf.amoplo = 1;
	return 0;
}

static int amixer_set_iv(void *blk, unsigned int iv)
{
	/* 20k1 amixer does not have this field */
	return 0;
}

static int amixer_set_x(void *blk, unsigned int x)
{
	struct amixer_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->amoplo, AMOPLO_X, x);
	ctl->dirty.bf.amoplo = 1;
	return 0;
}

static int amixer_set_y(void *blk, unsigned int y)
{
	struct amixer_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->amoplo, AMOPLO_Y, y);
	ctl->dirty.bf.amoplo = 1;
	return 0;
}

static int amixer_set_sadr(void *blk, unsigned int sadr)
{
	struct amixer_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->amophi, AMOPHI_SADR, sadr);
	ctl->dirty.bf.amophi = 1;
	return 0;
}

static int amixer_set_se(void *blk, unsigned int se)
{
	struct amixer_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->amophi, AMOPHI_SE, se);
	ctl->dirty.bf.amophi = 1;
	return 0;
}

static int amixer_set_dirty(void *blk, unsigned int flags)
{
	((struct amixer_rsc_ctrl_blk *)blk)->dirty.data = (flags & 0xffff);
	return 0;
}

static int amixer_set_dirty_all(void *blk)
{
	((struct amixer_rsc_ctrl_blk *)blk)->dirty.data = ~(0x0);
	return 0;
}

static int amixer_commit_write(struct hw *hw, unsigned int idx, void *blk)
{
	struct amixer_rsc_ctrl_blk *ctl = blk;

	if (ctl->dirty.bf.amoplo || ctl->dirty.bf.amophi) {
		hw_write_20kx(hw, AMOPLO+idx*8, ctl->amoplo);
		ctl->dirty.bf.amoplo = 0;
		hw_write_20kx(hw, AMOPHI+idx*8, ctl->amophi);
		ctl->dirty.bf.amophi = 0;
	}

	return 0;
}

static int amixer_get_y(void *blk)
{
	struct amixer_rsc_ctrl_blk *ctl = blk;

	return get_field(ctl->amoplo, AMOPLO_Y);
}

static unsigned int amixer_get_dirty(void *blk)
{
	return ((struct amixer_rsc_ctrl_blk *)blk)->dirty.data;
}

static int amixer_rsc_get_ctrl_blk(void **rblk)
{
	struct amixer_rsc_ctrl_blk *blk;

	*rblk = NULL;
	blk = kzalloc(sizeof(*blk), GFP_KERNEL);
	if (!blk)
		return -ENOMEM;

	*rblk = blk;

	return 0;
}

static int amixer_rsc_put_ctrl_blk(void *blk)
{
	kfree((struct amixer_rsc_ctrl_blk *)blk);

	return 0;
}

static int amixer_mgr_get_ctrl_blk(void **rblk)
{
	/*amixer_mgr_ctrl_blk_t *blk;*/

	*rblk = NULL;
	/*blk = kzalloc(sizeof(*blk), GFP_KERNEL);
	if (!blk)
		return -ENOMEM;

	*rblk = blk;*/

	return 0;
}

static int amixer_mgr_put_ctrl_blk(void *blk)
{
	/*kfree((amixer_mgr_ctrl_blk_t *)blk);*/

	return 0;
}

/*
 * DAIO control block definitions.
 */

/* Receiver Sample Rate Tracker Control register */
#define SRTCTL_SRCR	0x000000FF
#define SRTCTL_SRCL	0x0000FF00
#define SRTCTL_RSR	0x00030000
#define SRTCTL_DRAT	0x000C0000
#define SRTCTL_RLE	0x10000000
#define SRTCTL_RLP	0x20000000
#define SRTCTL_EC	0x40000000
#define SRTCTL_ET	0x80000000

/* DAIO Receiver register dirty flags */
union dai_dirty {
	struct {
		u16 srtctl:1;
		u16 rsv:15;
	} bf;
	u16 data;
};

/* DAIO Receiver control block */
struct dai_ctrl_blk {
	unsigned int	srtctl;
	union dai_dirty	dirty;
};

/* S/PDIF Transmitter register dirty flags */
union dao_dirty {
	struct {
		u16 spos:1;
		u16 rsv:15;
	} bf;
	u16 data;
};

/* S/PDIF Transmitter control block */
struct dao_ctrl_blk {
	unsigned int 	spos; /* S/PDIF Output Channel Status Register */
	union dao_dirty	dirty;
};

/* Audio Input Mapper RAM */
#define AIM_ARC		0x00000FFF
#define AIM_NXT		0x007F0000

struct daoimap {
	unsigned int aim;
	unsigned int idx;
};

/* I2S Transmitter/Receiver Control register */
#define I2SCTL_EA	0x00000004
#define I2SCTL_EI	0x00000010

/* S/PDIF Transmitter Control register */
#define SPOCTL_OE	0x00000001
#define SPOCTL_OS	0x0000000E
#define SPOCTL_RIV	0x00000010
#define SPOCTL_LIV	0x00000020
#define SPOCTL_SR	0x000000C0

/* S/PDIF Receiver Control register */
#define SPICTL_EN	0x00000001
#define SPICTL_I24	0x00000002
#define SPICTL_IB	0x00000004
#define SPICTL_SM	0x00000008
#define SPICTL_VM	0x00000010

/* DAIO manager register dirty flags */
union daio_mgr_dirty {
	struct {
		u32 i2soctl:4;
		u32 i2sictl:4;
		u32 spoctl:4;
		u32 spictl:4;
		u32 daoimap:1;
		u32 rsv:15;
	} bf;
	u32 data;
};

/* DAIO manager control block */
struct daio_mgr_ctrl_blk {
	unsigned int		i2sctl;
	unsigned int		spoctl;
	unsigned int		spictl;
	struct daoimap		daoimap;
	union daio_mgr_dirty	dirty;
};

static int dai_srt_set_srcr(void *blk, unsigned int src)
{
	struct dai_ctrl_blk *ctl = blk;

	set_field(&ctl->srtctl, SRTCTL_SRCR, src);
	ctl->dirty.bf.srtctl = 1;
	return 0;
}

static int dai_srt_set_srcl(void *blk, unsigned int src)
{
	struct dai_ctrl_blk *ctl = blk;

	set_field(&ctl->srtctl, SRTCTL_SRCL, src);
	ctl->dirty.bf.srtctl = 1;
	return 0;
}

static int dai_srt_set_rsr(void *blk, unsigned int rsr)
{
	struct dai_ctrl_blk *ctl = blk;

	set_field(&ctl->srtctl, SRTCTL_RSR, rsr);
	ctl->dirty.bf.srtctl = 1;
	return 0;
}

static int dai_srt_set_drat(void *blk, unsigned int drat)
{
	struct dai_ctrl_blk *ctl = blk;

	set_field(&ctl->srtctl, SRTCTL_DRAT, drat);
	ctl->dirty.bf.srtctl = 1;
	return 0;
}

static int dai_srt_set_ec(void *blk, unsigned int ec)
{
	struct dai_ctrl_blk *ctl = blk;

	set_field(&ctl->srtctl, SRTCTL_EC, ec ? 1 : 0);
	ctl->dirty.bf.srtctl = 1;
	return 0;
}

static int dai_srt_set_et(void *blk, unsigned int et)
{
	struct dai_ctrl_blk *ctl = blk;

	set_field(&ctl->srtctl, SRTCTL_ET, et ? 1 : 0);
	ctl->dirty.bf.srtctl = 1;
	return 0;
}

static int dai_commit_write(struct hw *hw, unsigned int idx, void *blk)
{
	struct dai_ctrl_blk *ctl = blk;

	if (ctl->dirty.bf.srtctl) {
		if (idx < 4) {
			/* S/PDIF SRTs */
			hw_write_20kx(hw, SRTSCTL+0x4*idx, ctl->srtctl);
		} else {
			/* I2S SRT */
			hw_write_20kx(hw, SRTICTL, ctl->srtctl);
		}
		ctl->dirty.bf.srtctl = 0;
	}

	return 0;
}

static int dai_get_ctrl_blk(void **rblk)
{
	struct dai_ctrl_blk *blk;

	*rblk = NULL;
	blk = kzalloc(sizeof(*blk), GFP_KERNEL);
	if (!blk)
		return -ENOMEM;

	*rblk = blk;

	return 0;
}

static int dai_put_ctrl_blk(void *blk)
{
	kfree((struct dai_ctrl_blk *)blk);

	return 0;
}

static int dao_set_spos(void *blk, unsigned int spos)
{
	((struct dao_ctrl_blk *)blk)->spos = spos;
	((struct dao_ctrl_blk *)blk)->dirty.bf.spos = 1;
	return 0;
}

static int dao_commit_write(struct hw *hw, unsigned int idx, void *blk)
{
	struct dao_ctrl_blk *ctl = blk;

	if (ctl->dirty.bf.spos) {
		if (idx < 4) {
			/* S/PDIF SPOSx */
			hw_write_20kx(hw, SPOS+0x4*idx, ctl->spos);
		}
		ctl->dirty.bf.spos = 0;
	}

	return 0;
}

static int dao_get_spos(void *blk, unsigned int *spos)
{
	*spos = ((struct dao_ctrl_blk *)blk)->spos;
	return 0;
}

static int dao_get_ctrl_blk(void **rblk)
{
	struct dao_ctrl_blk *blk;

	*rblk = NULL;
	blk = kzalloc(sizeof(*blk), GFP_KERNEL);
	if (!blk)
		return -ENOMEM;

	*rblk = blk;

	return 0;
}

static int dao_put_ctrl_blk(void *blk)
{
	kfree((struct dao_ctrl_blk *)blk);

	return 0;
}

static int daio_mgr_enb_dai(void *blk, unsigned int idx)
{
	struct daio_mgr_ctrl_blk *ctl = blk;

	if (idx < 4) {
		/* S/PDIF input */
		set_field(&ctl->spictl, SPICTL_EN << (idx*8), 1);
		ctl->dirty.bf.spictl |= (0x1 << idx);
	} else {
		/* I2S input */
		idx %= 4;
		set_field(&ctl->i2sctl, I2SCTL_EI << (idx*8), 1);
		ctl->dirty.bf.i2sictl |= (0x1 << idx);
	}
	return 0;
}

static int daio_mgr_dsb_dai(void *blk, unsigned int idx)
{
	struct daio_mgr_ctrl_blk *ctl = blk;

	if (idx < 4) {
		/* S/PDIF input */
		set_field(&ctl->spictl, SPICTL_EN << (idx*8), 0);
		ctl->dirty.bf.spictl |= (0x1 << idx);
	} else {
		/* I2S input */
		idx %= 4;
		set_field(&ctl->i2sctl, I2SCTL_EI << (idx*8), 0);
		ctl->dirty.bf.i2sictl |= (0x1 << idx);
	}
	return 0;
}

static int daio_mgr_enb_dao(void *blk, unsigned int idx)
{
	struct daio_mgr_ctrl_blk *ctl = blk;

	if (idx < 4) {
		/* S/PDIF output */
		set_field(&ctl->spoctl, SPOCTL_OE << (idx*8), 1);
		ctl->dirty.bf.spoctl |= (0x1 << idx);
	} else {
		/* I2S output */
		idx %= 4;
		set_field(&ctl->i2sctl, I2SCTL_EA << (idx*8), 1);
		ctl->dirty.bf.i2soctl |= (0x1 << idx);
	}
	return 0;
}

static int daio_mgr_dsb_dao(void *blk, unsigned int idx)
{
	struct daio_mgr_ctrl_blk *ctl = blk;

	if (idx < 4) {
		/* S/PDIF output */
		set_field(&ctl->spoctl, SPOCTL_OE << (idx*8), 0);
		ctl->dirty.bf.spoctl |= (0x1 << idx);
	} else {
		/* I2S output */
		idx %= 4;
		set_field(&ctl->i2sctl, I2SCTL_EA << (idx*8), 0);
		ctl->dirty.bf.i2soctl |= (0x1 << idx);
	}
	return 0;
}

static int daio_mgr_dao_init(void *blk, unsigned int idx, unsigned int conf)
{
	struct daio_mgr_ctrl_blk *ctl = blk;

	if (idx < 4) {
		/* S/PDIF output */
		switch ((conf & 0x7)) {
		case 0:
			set_field(&ctl->spoctl, SPOCTL_SR << (idx*8), 3);
			break; /* CDIF */
		case 1:
			set_field(&ctl->spoctl, SPOCTL_SR << (idx*8), 0);
			break;
		case 2:
			set_field(&ctl->spoctl, SPOCTL_SR << (idx*8), 1);
			break;
		case 4:
			set_field(&ctl->spoctl, SPOCTL_SR << (idx*8), 2);
			break;
		default:
			break;
		}
		set_field(&ctl->spoctl, SPOCTL_LIV << (idx*8),
			  (conf >> 4) & 0x1); /* Non-audio */
		set_field(&ctl->spoctl, SPOCTL_RIV << (idx*8),
			  (conf >> 4) & 0x1); /* Non-audio */
		set_field(&ctl->spoctl, SPOCTL_OS << (idx*8),
			  ((conf >> 3) & 0x1) ? 2 : 2); /* Raw */

		ctl->dirty.bf.spoctl |= (0x1 << idx);
	} else {
		/* I2S output */
		/*idx %= 4; */
	}
	return 0;
}

static int daio_mgr_set_imaparc(void *blk, unsigned int slot)
{
	struct daio_mgr_ctrl_blk *ctl = blk;

	set_field(&ctl->daoimap.aim, AIM_ARC, slot);
	ctl->dirty.bf.daoimap = 1;
	return 0;
}

static int daio_mgr_set_imapnxt(void *blk, unsigned int next)
{
	struct daio_mgr_ctrl_blk *ctl = blk;

	set_field(&ctl->daoimap.aim, AIM_NXT, next);
	ctl->dirty.bf.daoimap = 1;
	return 0;
}

static int daio_mgr_set_imapaddr(void *blk, unsigned int addr)
{
	struct daio_mgr_ctrl_blk *ctl = blk;

	ctl->daoimap.idx = addr;
	ctl->dirty.bf.daoimap = 1;
	return 0;
}

static int daio_mgr_commit_write(struct hw *hw, void *blk)
{
	struct daio_mgr_ctrl_blk *ctl = blk;
	int i;

	if (ctl->dirty.bf.i2sictl || ctl->dirty.bf.i2soctl) {
		for (i = 0; i < 4; i++) {
			if ((ctl->dirty.bf.i2sictl & (0x1 << i)))
				ctl->dirty.bf.i2sictl &= ~(0x1 << i);

			if ((ctl->dirty.bf.i2soctl & (0x1 << i)))
				ctl->dirty.bf.i2soctl &= ~(0x1 << i);
		}
		hw_write_20kx(hw, I2SCTL, ctl->i2sctl);
		mdelay(1);
	}
	if (ctl->dirty.bf.spoctl) {
		for (i = 0; i < 4; i++) {
			if ((ctl->dirty.bf.spoctl & (0x1 << i)))
				ctl->dirty.bf.spoctl &= ~(0x1 << i);
		}
		hw_write_20kx(hw, SPOCTL, ctl->spoctl);
		mdelay(1);
	}
	if (ctl->dirty.bf.spictl) {
		for (i = 0; i < 4; i++) {
			if ((ctl->dirty.bf.spictl & (0x1 << i)))
				ctl->dirty.bf.spictl &= ~(0x1 << i);
		}
		hw_write_20kx(hw, SPICTL, ctl->spictl);
		mdelay(1);
	}
	if (ctl->dirty.bf.daoimap) {
		hw_write_20kx(hw, DAOIMAP+ctl->daoimap.idx*4,
					ctl->daoimap.aim);
		ctl->dirty.bf.daoimap = 0;
	}

	return 0;
}

static int daio_mgr_get_ctrl_blk(struct hw *hw, void **rblk)
{
	struct daio_mgr_ctrl_blk *blk;

	*rblk = NULL;
	blk = kzalloc(sizeof(*blk), GFP_KERNEL);
	if (!blk)
		return -ENOMEM;

	blk->i2sctl = hw_read_20kx(hw, I2SCTL);
	blk->spoctl = hw_read_20kx(hw, SPOCTL);
	blk->spictl = hw_read_20kx(hw, SPICTL);

	*rblk = blk;

	return 0;
}

static int daio_mgr_put_ctrl_blk(void *blk)
{
	kfree((struct daio_mgr_ctrl_blk *)blk);

	return 0;
}

/* Timer interrupt */
static int set_timer_irq(struct hw *hw, int enable)
{
	hw_write_20kx(hw, GIE, enable ? IT_INT : 0);
	return 0;
}

static int set_timer_tick(struct hw *hw, unsigned int ticks)
{
	if (ticks)
		ticks |= TIMR_IE | TIMR_IP;
	hw_write_20kx(hw, TIMR, ticks);
	return 0;
}

static unsigned int get_wc(struct hw *hw)
{
	return hw_read_20kx(hw, WC);
}

/* Card hardware initialization block */
struct dac_conf {
	unsigned int msr; /* master sample rate in rsrs */
};

struct adc_conf {
	unsigned int msr; 	/* master sample rate in rsrs */
	unsigned char input; 	/* the input source of ADC */
	unsigned char mic20db; 	/* boost mic by 20db if input is microphone */
};

struct daio_conf {
	unsigned int msr; /* master sample rate in rsrs */
};

struct trn_conf {
	unsigned long vm_pgt_phys;
};

static int hw_daio_init(struct hw *hw, const struct daio_conf *info)
{
	u32 i2sorg;
	u32 spdorg;

	/* Read I2S CTL.  Keep original value. */
	/*i2sorg = hw_read_20kx(hw, I2SCTL);*/
	i2sorg = 0x94040404; /* enable all audio out and I2S-D input */
	/* Program I2S with proper master sample rate and enable
	 * the correct I2S channel. */
	i2sorg &= 0xfffffffc;

	/* Enable S/PDIF-out-A in fixed 24-bit data
	 * format and default to 48kHz. */
	/* Disable all before doing any changes. */
	hw_write_20kx(hw, SPOCTL, 0x0);
	spdorg = 0x05;

	switch (info->msr) {
	case 1:
		i2sorg |= 1;
		spdorg |= (0x0 << 6);
		break;
	case 2:
		i2sorg |= 2;
		spdorg |= (0x1 << 6);
		break;
	case 4:
		i2sorg |= 3;
		spdorg |= (0x2 << 6);
		break;
	default:
		i2sorg |= 1;
		break;
	}

	hw_write_20kx(hw, I2SCTL, i2sorg);
	hw_write_20kx(hw, SPOCTL, spdorg);

	/* Enable S/PDIF-in-A in fixed 24-bit data format. */
	/* Disable all before doing any changes. */
	hw_write_20kx(hw, SPICTL, 0x0);
	mdelay(1);
	spdorg = 0x0a0a0a0a;
	hw_write_20kx(hw, SPICTL, spdorg);
	mdelay(1);

	return 0;
}

/* TRANSPORT operations */
static int hw_trn_init(struct hw *hw, const struct trn_conf *info)
{
	u32 trnctl;
	u32 ptp_phys_low, ptp_phys_high;

	/* Set up device page table */
	if ((~0UL) == info->vm_pgt_phys) {
		dev_err(hw->card->dev,
			"Wrong device page table page address!\n");
		return -1;
	}

	trnctl = 0x13;  /* 32-bit, 4k-size page */
	ptp_phys_low = (u32)info->vm_pgt_phys;
	ptp_phys_high = upper_32_bits(info->vm_pgt_phys);
	if (sizeof(void *) == 8) /* 64bit address */
		trnctl |= (1 << 2);
#if 0 /* Only 4k h/w pages for simplicitiy */
#if PAGE_SIZE == 8192
	trnctl |= (1<<5);
#endif
#endif
	hw_write_20kx(hw, PTPALX, ptp_phys_low);
	hw_write_20kx(hw, PTPAHX, ptp_phys_high);
	hw_write_20kx(hw, TRNCTL, trnctl);
	hw_write_20kx(hw, TRNIS, 0x200c01); /* really needed? */

	return 0;
}

/* Card initialization */
#define GCTL_EAC	0x00000001
#define GCTL_EAI	0x00000002
#define GCTL_BEP	0x00000004
#define GCTL_BES	0x00000008
#define GCTL_DSP	0x00000010
#define GCTL_DBP	0x00000020
#define GCTL_ABP	0x00000040
#define GCTL_TBP	0x00000080
#define GCTL_SBP	0x00000100
#define GCTL_FBP	0x00000200
#define GCTL_XA		0x00000400
#define GCTL_ET		0x00000800
#define GCTL_PR		0x00001000
#define GCTL_MRL	0x00002000
#define GCTL_SDE	0x00004000
#define GCTL_SDI	0x00008000
#define GCTL_SM		0x00010000
#define GCTL_SR		0x00020000
#define GCTL_SD		0x00040000
#define GCTL_SE		0x00080000
#define GCTL_AID	0x00100000

static int hw_pll_init(struct hw *hw, unsigned int rsr)
{
	unsigned int pllctl;
	int i;

	pllctl = (48000 == rsr) ? 0x1480a001 : 0x1480a731;
	for (i = 0; i < 3; i++) {
		if (hw_read_20kx(hw, PLLCTL) == pllctl)
			break;

		hw_write_20kx(hw, PLLCTL, pllctl);
		mdelay(40);
	}
	if (i >= 3) {
		dev_alert(hw->card->dev, "PLL initialization failed!!!\n");
		return -EBUSY;
	}

	return 0;
}

static int hw_auto_init(struct hw *hw)
{
	unsigned int gctl;
	int i;

	gctl = hw_read_20kx(hw, GCTL);
	set_field(&gctl, GCTL_EAI, 0);
	hw_write_20kx(hw, GCTL, gctl);
	set_field(&gctl, GCTL_EAI, 1);
	hw_write_20kx(hw, GCTL, gctl);
	mdelay(10);
	for (i = 0; i < 400000; i++) {
		gctl = hw_read_20kx(hw, GCTL);
		if (get_field(gctl, GCTL_AID))
			break;
	}
	if (!get_field(gctl, GCTL_AID)) {
		dev_alert(hw->card->dev, "Card Auto-init failed!!!\n");
		return -EBUSY;
	}

	return 0;
}

static int i2c_unlock(struct hw *hw)
{
	if ((hw_read_pci(hw, 0xcc) & 0xff) == 0xaa)
		return 0;

	hw_write_pci(hw, 0xcc, 0x8c);
	hw_write_pci(hw, 0xcc, 0x0e);
	if ((hw_read_pci(hw, 0xcc) & 0xff) == 0xaa)
		return 0;

	hw_write_pci(hw, 0xcc, 0xee);
	hw_write_pci(hw, 0xcc, 0xaa);
	if ((hw_read_pci(hw, 0xcc) & 0xff) == 0xaa)
		return 0;

	return -1;
}

static void i2c_lock(struct hw *hw)
{
	if ((hw_read_pci(hw, 0xcc) & 0xff) == 0xaa)
		hw_write_pci(hw, 0xcc, 0x00);
}

static void i2c_write(struct hw *hw, u32 device, u32 addr, u32 data)
{
	unsigned int ret;

	do {
		ret = hw_read_pci(hw, 0xEC);
	} while (!(ret & 0x800000));
	hw_write_pci(hw, 0xE0, device);
	hw_write_pci(hw, 0xE4, (data << 8) | (addr & 0xff));
}

/* DAC operations */

static int hw_reset_dac(struct hw *hw)
{
	u32 i;
	u16 gpioorg;
	unsigned int ret;

	if (i2c_unlock(hw))
		return -1;

	do {
		ret = hw_read_pci(hw, 0xEC);
	} while (!(ret & 0x800000));
	hw_write_pci(hw, 0xEC, 0x05);  /* write to i2c status control */

	/* To be effective, need to reset the DAC twice. */
	for (i = 0; i < 2;  i++) {
		/* set gpio */
		mdelay(100);
		gpioorg = (u16)hw_read_20kx(hw, GPIO);
		gpioorg &= 0xfffd;
		hw_write_20kx(hw, GPIO, gpioorg);
		mdelay(1);
		hw_write_20kx(hw, GPIO, gpioorg | 0x2);
	}

	i2c_write(hw, 0x00180080, 0x01, 0x80);
	i2c_write(hw, 0x00180080, 0x02, 0x10);

	i2c_lock(hw);

	return 0;
}

static int hw_dac_init(struct hw *hw, const struct dac_conf *info)
{
	u32 data;
	u16 gpioorg;
	unsigned int ret;

	if (hw->model == CTSB055X) {
		/* SB055x, unmute outputs */
		gpioorg = (u16)hw_read_20kx(hw, GPIO);
		gpioorg &= 0xffbf;	/* set GPIO6 to low */
		gpioorg |= 2;		/* set GPIO1 to high */
		hw_write_20kx(hw, GPIO, gpioorg);
		return 0;
	}

	/* mute outputs */
	gpioorg = (u16)hw_read_20kx(hw, GPIO);
	gpioorg &= 0xffbf;
	hw_write_20kx(hw, GPIO, gpioorg);

	hw_reset_dac(hw);

	if (i2c_unlock(hw))
		return -1;

	hw_write_pci(hw, 0xEC, 0x05);  /* write to i2c status control */
	do {
		ret = hw_read_pci(hw, 0xEC);
	} while (!(ret & 0x800000));

	switch (info->msr) {
	case 1:
		data = 0x24;
		break;
	case 2:
		data = 0x25;
		break;
	case 4:
		data = 0x26;
		break;
	default:
		data = 0x24;
		break;
	}

	i2c_write(hw, 0x00180080, 0x06, data);
	i2c_write(hw, 0x00180080, 0x09, data);
	i2c_write(hw, 0x00180080, 0x0c, data);
	i2c_write(hw, 0x00180080, 0x0f, data);

	i2c_lock(hw);

	/* unmute outputs */
	gpioorg = (u16)hw_read_20kx(hw, GPIO);
	gpioorg = gpioorg | 0x40;
	hw_write_20kx(hw, GPIO, gpioorg);

	return 0;
}

/* ADC operations */

static int is_adc_input_selected_SB055x(struct hw *hw, enum ADCSRC type)
{
	return 0;
}

static int is_adc_input_selected_SBx(struct hw *hw, enum ADCSRC type)
{
	u32 data;

	data = hw_read_20kx(hw, GPIO);
	switch (type) {
	case ADC_MICIN:
		data = ((data & (0x1<<7)) && (data & (0x1<<8)));
		break;
	case ADC_LINEIN:
		data = (!(data & (0x1<<7)) && (data & (0x1<<8)));
		break;
	case ADC_NONE: /* Digital I/O */
		data = (!(data & (0x1<<8)));
		break;
	default:
		data = 0;
	}
	return data;
}

static int is_adc_input_selected_hendrix(struct hw *hw, enum ADCSRC type)
{
	u32 data;

	data = hw_read_20kx(hw, GPIO);
	switch (type) {
	case ADC_MICIN:
		data = (data & (0x1 << 7)) ? 1 : 0;
		break;
	case ADC_LINEIN:
		data = (data & (0x1 << 7)) ? 0 : 1;
		break;
	default:
		data = 0;
	}
	return data;
}

static int hw_is_adc_input_selected(struct hw *hw, enum ADCSRC type)
{
	switch (hw->model) {
	case CTSB055X:
		return is_adc_input_selected_SB055x(hw, type);
	case CTSB073X:
		return is_adc_input_selected_hendrix(hw, type);
	case CTUAA:
		return is_adc_input_selected_hendrix(hw, type);
	default:
		return is_adc_input_selected_SBx(hw, type);
	}
}

static int
adc_input_select_SB055x(struct hw *hw, enum ADCSRC type, unsigned char boost)
{
	u32 data;

	/*
	 * check and set the following GPIO bits accordingly
	 * ADC_Gain		= GPIO2
	 * DRM_off		= GPIO3
	 * Mic_Pwr_on		= GPIO7
	 * Digital_IO_Sel	= GPIO8
	 * Mic_Sw		= GPIO9
	 * Aux/MicLine_Sw	= GPIO12
	 */
	data = hw_read_20kx(hw, GPIO);
	data &= 0xec73;
	switch (type) {
	case ADC_MICIN:
		data |= (0x1<<7) | (0x1<<8) | (0x1<<9) ;
		data |= boost ? (0x1<<2) : 0;
		break;
	case ADC_LINEIN:
		data |= (0x1<<8);
		break;
	case ADC_AUX:
		data |= (0x1<<8) | (0x1<<12);
		break;
	case ADC_NONE:
		data |= (0x1<<12);  /* set to digital */
		break;
	default:
		return -1;
	}

	hw_write_20kx(hw, GPIO, data);

	return 0;
}


static int
adc_input_select_SBx(struct hw *hw, enum ADCSRC type, unsigned char boost)
{
	u32 data;
	u32 i2c_data;
	unsigned int ret;

	if (i2c_unlock(hw))
		return -1;

	do {
		ret = hw_read_pci(hw, 0xEC);
	} while (!(ret & 0x800000)); /* i2c ready poll */
	/* set i2c access mode as Direct Control */
	hw_write_pci(hw, 0xEC, 0x05);

	data = hw_read_20kx(hw, GPIO);
	switch (type) {
	case ADC_MICIN:
		data |= ((0x1 << 7) | (0x1 << 8));
		i2c_data = 0x1;  /* Mic-in */
		break;
	case ADC_LINEIN:
		data &= ~(0x1 << 7);
		data |= (0x1 << 8);
		i2c_data = 0x2; /* Line-in */
		break;
	case ADC_NONE:
		data &= ~(0x1 << 8);
		i2c_data = 0x0; /* set to Digital */
		break;
	default:
		i2c_lock(hw);
		return -1;
	}
	hw_write_20kx(hw, GPIO, data);
	i2c_write(hw, 0x001a0080, 0x2a, i2c_data);
	if (boost) {
		i2c_write(hw, 0x001a0080, 0x1c, 0xe7); /* +12dB boost */
		i2c_write(hw, 0x001a0080, 0x1e, 0xe7); /* +12dB boost */
	} else {
		i2c_write(hw, 0x001a0080, 0x1c, 0xcf); /* No boost */
		i2c_write(hw, 0x001a0080, 0x1e, 0xcf); /* No boost */
	}

	i2c_lock(hw);

	return 0;
}

static int
adc_input_select_hendrix(struct hw *hw, enum ADCSRC type, unsigned char boost)
{
	u32 data;
	u32 i2c_data;
	unsigned int ret;

	if (i2c_unlock(hw))
		return -1;

	do {
		ret = hw_read_pci(hw, 0xEC);
	} while (!(ret & 0x800000)); /* i2c ready poll */
	/* set i2c access mode as Direct Control */
	hw_write_pci(hw, 0xEC, 0x05);

	data = hw_read_20kx(hw, GPIO);
	switch (type) {
	case ADC_MICIN:
		data |= (0x1 << 7);
		i2c_data = 0x1;  /* Mic-in */
		break;
	case ADC_LINEIN:
		data &= ~(0x1 << 7);
		i2c_data = 0x2; /* Line-in */
		break;
	default:
		i2c_lock(hw);
		return -1;
	}
	hw_write_20kx(hw, GPIO, data);
	i2c_write(hw, 0x001a0080, 0x2a, i2c_data);
	if (boost) {
		i2c_write(hw, 0x001a0080, 0x1c, 0xe7); /* +12dB boost */
		i2c_write(hw, 0x001a0080, 0x1e, 0xe7); /* +12dB boost */
	} else {
		i2c_write(hw, 0x001a0080, 0x1c, 0xcf); /* No boost */
		i2c_write(hw, 0x001a0080, 0x1e, 0xcf); /* No boost */
	}

	i2c_lock(hw);

	return 0;
}

static int hw_adc_input_select(struct hw *hw, enum ADCSRC type)
{
	int state = type == ADC_MICIN;

	switch (hw->model) {
	case CTSB055X:
		return adc_input_select_SB055x(hw, type, state);
	case CTSB073X:
		return adc_input_select_hendrix(hw, type, state);
	case CTUAA:
		return adc_input_select_hendrix(hw, type, state);
	default:
		return adc_input_select_SBx(hw, type, state);
	}
}

static int adc_init_SB055x(struct hw *hw, int input, int mic20db)
{
	return adc_input_select_SB055x(hw, input, mic20db);
}

static int adc_init_SBx(struct hw *hw, int input, int mic20db)
{
	u16 gpioorg;
	u16 input_source;
	u32 adcdata;
	unsigned int ret;

	input_source = 0x100;  /* default to analog */
	switch (input) {
	case ADC_MICIN:
		adcdata = 0x1;
		input_source = 0x180;  /* set GPIO7 to select Mic */
		break;
	case ADC_LINEIN:
		adcdata = 0x2;
		break;
	case ADC_VIDEO:
		adcdata = 0x4;
		break;
	case ADC_AUX:
		adcdata = 0x8;
		break;
	case ADC_NONE:
		adcdata = 0x0;
		input_source = 0x0;  /* set to Digital */
		break;
	default:
		adcdata = 0x0;
		break;
	}

	if (i2c_unlock(hw))
		return -1;

	do {
		ret = hw_read_pci(hw, 0xEC);
	} while (!(ret & 0x800000)); /* i2c ready poll */
	hw_write_pci(hw, 0xEC, 0x05);  /* write to i2c status control */

	i2c_write(hw, 0x001a0080, 0x0e, 0x08);
	i2c_write(hw, 0x001a0080, 0x18, 0x0a);
	i2c_write(hw, 0x001a0080, 0x28, 0x86);
	i2c_write(hw, 0x001a0080, 0x2a, adcdata);

	if (mic20db) {
		i2c_write(hw, 0x001a0080, 0x1c, 0xf7);
		i2c_write(hw, 0x001a0080, 0x1e, 0xf7);
	} else {
		i2c_write(hw, 0x001a0080, 0x1c, 0xcf);
		i2c_write(hw, 0x001a0080, 0x1e, 0xcf);
	}

	if (!(hw_read_20kx(hw, ID0) & 0x100))
		i2c_write(hw, 0x001a0080, 0x16, 0x26);

	i2c_lock(hw);

	gpioorg = (u16)hw_read_20kx(hw,  GPIO);
	gpioorg &= 0xfe7f;
	gpioorg |= input_source;
	hw_write_20kx(hw, GPIO, gpioorg);

	return 0;
}

static int hw_adc_init(struct hw *hw, const struct adc_conf *info)
{
	if (hw->model == CTSB055X)
		return adc_init_SB055x(hw, info->input, info->mic20db);
	else
		return adc_init_SBx(hw, info->input, info->mic20db);
}

static struct capabilities hw_capabilities(struct hw *hw)
{
	struct capabilities cap;

	/* SB073x and Vista compatible cards have no digit IO switch */
	cap.digit_io_switch = !(hw->model == CTSB073X || hw->model == CTUAA);
	cap.dedicated_mic = 0;
	cap.output_switch = 0;
	cap.mic_source_switch = 0;

	return cap;
}

#define CTLBITS(a, b, c, d)	(((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

#define UAA_CFG_PWRSTATUS	0x44
#define UAA_CFG_SPACE_FLAG	0xA0
#define UAA_CORE_CHANGE		0x3FFC
static int uaa_to_xfi(struct pci_dev *pci)
{
	unsigned int bar0, bar1, bar2, bar3, bar4, bar5;
	unsigned int cmd, irq, cl_size, l_timer, pwr;
	unsigned int is_uaa;
	unsigned int data[4] = {0};
	unsigned int io_base;
	void __iomem *mem_base;
	int i;
	const u32 CTLX = CTLBITS('C', 'T', 'L', 'X');
	const u32 CTL_ = CTLBITS('C', 'T', 'L', '-');
	const u32 CTLF = CTLBITS('C', 'T', 'L', 'F');
	const u32 CTLi = CTLBITS('C', 'T', 'L', 'i');
	const u32 CTLA = CTLBITS('C', 'T', 'L', 'A');
	const u32 CTLZ = CTLBITS('C', 'T', 'L', 'Z');
	const u32 CTLL = CTLBITS('C', 'T', 'L', 'L');

	/* By default, Hendrix card UAA Bar0 should be using memory... */
	io_base = pci_resource_start(pci, 0);
	mem_base = ioremap(io_base, pci_resource_len(pci, 0));
	if (!mem_base)
		return -ENOENT;

	/* Read current mode from Mode Change Register */
	for (i = 0; i < 4; i++)
		data[i] = readl(mem_base + UAA_CORE_CHANGE);

	/* Determine current mode... */
	if (data[0] == CTLA) {
		is_uaa = ((data[1] == CTLZ && data[2] == CTLL
			  && data[3] == CTLA) || (data[1] == CTLA
			  && data[2] == CTLZ && data[3] == CTLL));
	} else if (data[0] == CTLZ) {
		is_uaa = (data[1] == CTLL
				&& data[2] == CTLA && data[3] == CTLA);
	} else if (data[0] == CTLL) {
		is_uaa = (data[1] == CTLA
				&& data[2] == CTLA && data[3] == CTLZ);
	} else {
		is_uaa = 0;
	}

	if (!is_uaa) {
		/* Not in UAA mode currently. Return directly. */
		iounmap(mem_base);
		return 0;
	}

	pci_read_config_dword(pci, PCI_BASE_ADDRESS_0, &bar0);
	pci_read_config_dword(pci, PCI_BASE_ADDRESS_1, &bar1);
	pci_read_config_dword(pci, PCI_BASE_ADDRESS_2, &bar2);
	pci_read_config_dword(pci, PCI_BASE_ADDRESS_3, &bar3);
	pci_read_config_dword(pci, PCI_BASE_ADDRESS_4, &bar4);
	pci_read_config_dword(pci, PCI_BASE_ADDRESS_5, &bar5);
	pci_read_config_dword(pci, PCI_INTERRUPT_LINE, &irq);
	pci_read_config_dword(pci, PCI_CACHE_LINE_SIZE, &cl_size);
	pci_read_config_dword(pci, PCI_LATENCY_TIMER, &l_timer);
	pci_read_config_dword(pci, UAA_CFG_PWRSTATUS, &pwr);
	pci_read_config_dword(pci, PCI_COMMAND, &cmd);

	/* Set up X-Fi core PCI configuration space. */
	/* Switch to X-Fi config space with BAR0 exposed. */
	pci_write_config_dword(pci, UAA_CFG_SPACE_FLAG, 0x87654321);
	/* Copy UAA's BAR5 into X-Fi BAR0 */
	pci_write_config_dword(pci, PCI_BASE_ADDRESS_0, bar5);
	/* Switch to X-Fi config space without BAR0 exposed. */
	pci_write_config_dword(pci, UAA_CFG_SPACE_FLAG, 0x12345678);
	pci_write_config_dword(pci, PCI_BASE_ADDRESS_1, bar1);
	pci_write_config_dword(pci, PCI_BASE_ADDRESS_2, bar2);
	pci_write_config_dword(pci, PCI_BASE_ADDRESS_3, bar3);
	pci_write_config_dword(pci, PCI_BASE_ADDRESS_4, bar4);
	pci_write_config_dword(pci, PCI_INTERRUPT_LINE, irq);
	pci_write_config_dword(pci, PCI_CACHE_LINE_SIZE, cl_size);
	pci_write_config_dword(pci, PCI_LATENCY_TIMER, l_timer);
	pci_write_config_dword(pci, UAA_CFG_PWRSTATUS, pwr);
	pci_write_config_dword(pci, PCI_COMMAND, cmd);

	/* Switch to X-Fi mode */
	writel(CTLX, (mem_base + UAA_CORE_CHANGE));
	writel(CTL_, (mem_base + UAA_CORE_CHANGE));
	writel(CTLF, (mem_base + UAA_CORE_CHANGE));
	writel(CTLi, (mem_base + UAA_CORE_CHANGE));

	iounmap(mem_base);

	return 0;
}

static irqreturn_t ct_20k1_interrupt(int irq, void *dev_id)
{
	struct hw *hw = dev_id;
	unsigned int status;

	status = hw_read_20kx(hw, GIP);
	if (!status)
		return IRQ_NONE;

	if (hw->irq_callback)
		hw->irq_callback(hw->irq_callback_data, status);

	hw_write_20kx(hw, GIP, status);
	return IRQ_HANDLED;
}

static int hw_card_start(struct hw *hw)
{
	int err;
	struct pci_dev *pci = hw->pci;

	err = pci_enable_device(pci);
	if (err < 0)
		return err;

	/* Set DMA transfer mask */
	if (dma_set_mask(&pci->dev, CT_XFI_DMA_MASK) < 0 ||
	    dma_set_coherent_mask(&pci->dev, CT_XFI_DMA_MASK) < 0) {
		dev_err(hw->card->dev,
			"architecture does not support PCI busmaster DMA with mask 0x%llx\n",
			CT_XFI_DMA_MASK);
		err = -ENXIO;
		goto error1;
	}

	if (!hw->io_base) {
		err = pci_request_regions(pci, "XFi");
		if (err < 0)
			goto error1;

		if (hw->model == CTUAA)
			hw->io_base = pci_resource_start(pci, 5);
		else
			hw->io_base = pci_resource_start(pci, 0);

	}

	/* Switch to X-Fi mode from UAA mode if neeeded */
	if (hw->model == CTUAA) {
		err = uaa_to_xfi(pci);
		if (err)
			goto error2;

	}

	if (hw->irq < 0) {
		err = request_irq(pci->irq, ct_20k1_interrupt, IRQF_SHARED,
				  KBUILD_MODNAME, hw);
		if (err < 0) {
			dev_err(hw->card->dev,
				"XFi: Cannot get irq %d\n", pci->irq);
			goto error2;
		}
		hw->irq = pci->irq;
	}

	pci_set_master(pci);

	return 0;

error2:
	pci_release_regions(pci);
	hw->io_base = 0;
error1:
	pci_disable_device(pci);
	return err;
}

static int hw_card_stop(struct hw *hw)
{
	unsigned int data;

	/* disable transport bus master and queueing of request */
	hw_write_20kx(hw, TRNCTL, 0x00);

	/* disable pll */
	data = hw_read_20kx(hw, PLLCTL);
	hw_write_20kx(hw, PLLCTL, (data & (~(0x0F<<12))));

	/* TODO: Disable interrupt and so on... */
	if (hw->irq >= 0)
		synchronize_irq(hw->irq);
	return 0;
}

static int hw_card_shutdown(struct hw *hw)
{
	if (hw->irq >= 0)
		free_irq(hw->irq, hw);

	hw->irq	= -1;
	iounmap(hw->mem_base);
	hw->mem_base = NULL;

	if (hw->io_base)
		pci_release_regions(hw->pci);

	hw->io_base = 0;

	pci_disable_device(hw->pci);

	return 0;
}

static int hw_card_init(struct hw *hw, struct card_conf *info)
{
	int err;
	unsigned int gctl;
	u32 data;
	struct dac_conf dac_info = {0};
	struct adc_conf adc_info = {0};
	struct daio_conf daio_info = {0};
	struct trn_conf trn_info = {0};

	/* Get PCI io port base address and do Hendrix switch if needed. */
	err = hw_card_start(hw);
	if (err)
		return err;

	/* PLL init */
	err = hw_pll_init(hw, info->rsr);
	if (err < 0)
		return err;

	/* kick off auto-init */
	err = hw_auto_init(hw);
	if (err < 0)
		return err;

	/* Enable audio ring */
	gctl = hw_read_20kx(hw, GCTL);
	set_field(&gctl, GCTL_EAC, 1);
	set_field(&gctl, GCTL_DBP, 1);
	set_field(&gctl, GCTL_TBP, 1);
	set_field(&gctl, GCTL_FBP, 1);
	set_field(&gctl, GCTL_ET, 1);
	hw_write_20kx(hw, GCTL, gctl);
	mdelay(10);

	/* Reset all global pending interrupts */
	hw_write_20kx(hw, GIE, 0);
	/* Reset all SRC pending interrupts */
	hw_write_20kx(hw, SRCIP, 0);
	mdelay(30);

	/* Detect the card ID and configure GPIO accordingly. */
	switch (hw->model) {
	case CTSB055X:
		hw_write_20kx(hw, GPIOCTL, 0x13fe);
		break;
	case CTSB073X:
		hw_write_20kx(hw, GPIOCTL, 0x00e6);
		break;
	case CTUAA:
		hw_write_20kx(hw, GPIOCTL, 0x00c2);
		break;
	default:
		hw_write_20kx(hw, GPIOCTL, 0x01e6);
		break;
	}

	trn_info.vm_pgt_phys = info->vm_pgt_phys;
	err = hw_trn_init(hw, &trn_info);
	if (err < 0)
		return err;

	daio_info.msr = info->msr;
	err = hw_daio_init(hw, &daio_info);
	if (err < 0)
		return err;

	dac_info.msr = info->msr;
	err = hw_dac_init(hw, &dac_info);
	if (err < 0)
		return err;

	adc_info.msr = info->msr;
	adc_info.input = ADC_LINEIN;
	adc_info.mic20db = 0;
	err = hw_adc_init(hw, &adc_info);
	if (err < 0)
		return err;

	data = hw_read_20kx(hw, SRCMCTL);
	data |= 0x1; /* Enables input from the audio ring */
	hw_write_20kx(hw, SRCMCTL, data);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int hw_suspend(struct hw *hw)
{
	struct pci_dev *pci = hw->pci;

	hw_card_stop(hw);

	if (hw->model == CTUAA) {
		/* Switch to UAA config space. */
		pci_write_config_dword(pci, UAA_CFG_SPACE_FLAG, 0x0);
	}

	return 0;
}

static int hw_resume(struct hw *hw, struct card_conf *info)
{
	/* Re-initialize card hardware. */
	return hw_card_init(hw, info);
}
#endif

static u32 hw_read_20kx(struct hw *hw, u32 reg)
{
	u32 value;
	unsigned long flags;

	spin_lock_irqsave(
		&container_of(hw, struct hw20k1, hw)->reg_20k1_lock, flags);
	outl(reg, hw->io_base + 0x0);
	value = inl(hw->io_base + 0x4);
	spin_unlock_irqrestore(
		&container_of(hw, struct hw20k1, hw)->reg_20k1_lock, flags);

	return value;
}

static void hw_write_20kx(struct hw *hw, u32 reg, u32 data)
{
	unsigned long flags;

	spin_lock_irqsave(
		&container_of(hw, struct hw20k1, hw)->reg_20k1_lock, flags);
	outl(reg, hw->io_base + 0x0);
	outl(data, hw->io_base + 0x4);
	spin_unlock_irqrestore(
		&container_of(hw, struct hw20k1, hw)->reg_20k1_lock, flags);

}

static u32 hw_read_pci(struct hw *hw, u32 reg)
{
	u32 value;
	unsigned long flags;

	spin_lock_irqsave(
		&container_of(hw, struct hw20k1, hw)->reg_pci_lock, flags);
	outl(reg, hw->io_base + 0x10);
	value = inl(hw->io_base + 0x14);
	spin_unlock_irqrestore(
		&container_of(hw, struct hw20k1, hw)->reg_pci_lock, flags);

	return value;
}

static void hw_write_pci(struct hw *hw, u32 reg, u32 data)
{
	unsigned long flags;

	spin_lock_irqsave(
		&container_of(hw, struct hw20k1, hw)->reg_pci_lock, flags);
	outl(reg, hw->io_base + 0x10);
	outl(data, hw->io_base + 0x14);
	spin_unlock_irqrestore(
		&container_of(hw, struct hw20k1, hw)->reg_pci_lock, flags);
}

static struct hw ct20k1_preset = {
	.irq = -1,

	.card_init = hw_card_init,
	.card_stop = hw_card_stop,
	.pll_init = hw_pll_init,
	.is_adc_source_selected = hw_is_adc_input_selected,
	.select_adc_source = hw_adc_input_select,
	.capabilities = hw_capabilities,
#ifdef CONFIG_PM_SLEEP
	.suspend = hw_suspend,
	.resume = hw_resume,
#endif

	.src_rsc_get_ctrl_blk = src_get_rsc_ctrl_blk,
	.src_rsc_put_ctrl_blk = src_put_rsc_ctrl_blk,
	.src_mgr_get_ctrl_blk = src_mgr_get_ctrl_blk,
	.src_mgr_put_ctrl_blk = src_mgr_put_ctrl_blk,
	.src_set_state = src_set_state,
	.src_set_bm = src_set_bm,
	.src_set_rsr = src_set_rsr,
	.src_set_sf = src_set_sf,
	.src_set_wr = src_set_wr,
	.src_set_pm = src_set_pm,
	.src_set_rom = src_set_rom,
	.src_set_vo = src_set_vo,
	.src_set_st = src_set_st,
	.src_set_ie = src_set_ie,
	.src_set_ilsz = src_set_ilsz,
	.src_set_bp = src_set_bp,
	.src_set_cisz = src_set_cisz,
	.src_set_ca = src_set_ca,
	.src_set_sa = src_set_sa,
	.src_set_la = src_set_la,
	.src_set_pitch = src_set_pitch,
	.src_set_dirty = src_set_dirty,
	.src_set_clear_zbufs = src_set_clear_zbufs,
	.src_set_dirty_all = src_set_dirty_all,
	.src_commit_write = src_commit_write,
	.src_get_ca = src_get_ca,
	.src_get_dirty = src_get_dirty,
	.src_dirty_conj_mask = src_dirty_conj_mask,
	.src_mgr_enbs_src = src_mgr_enbs_src,
	.src_mgr_enb_src = src_mgr_enb_src,
	.src_mgr_dsb_src = src_mgr_dsb_src,
	.src_mgr_commit_write = src_mgr_commit_write,

	.srcimp_mgr_get_ctrl_blk = srcimp_mgr_get_ctrl_blk,
	.srcimp_mgr_put_ctrl_blk = srcimp_mgr_put_ctrl_blk,
	.srcimp_mgr_set_imaparc = srcimp_mgr_set_imaparc,
	.srcimp_mgr_set_imapuser = srcimp_mgr_set_imapuser,
	.srcimp_mgr_set_imapnxt = srcimp_mgr_set_imapnxt,
	.srcimp_mgr_set_imapaddr = srcimp_mgr_set_imapaddr,
	.srcimp_mgr_commit_write = srcimp_mgr_commit_write,

	.amixer_rsc_get_ctrl_blk = amixer_rsc_get_ctrl_blk,
	.amixer_rsc_put_ctrl_blk = amixer_rsc_put_ctrl_blk,
	.amixer_mgr_get_ctrl_blk = amixer_mgr_get_ctrl_blk,
	.amixer_mgr_put_ctrl_blk = amixer_mgr_put_ctrl_blk,
	.amixer_set_mode = amixer_set_mode,
	.amixer_set_iv = amixer_set_iv,
	.amixer_set_x = amixer_set_x,
	.amixer_set_y = amixer_set_y,
	.amixer_set_sadr = amixer_set_sadr,
	.amixer_set_se = amixer_set_se,
	.amixer_set_dirty = amixer_set_dirty,
	.amixer_set_dirty_all = amixer_set_dirty_all,
	.amixer_commit_write = amixer_commit_write,
	.amixer_get_y = amixer_get_y,
	.amixer_get_dirty = amixer_get_dirty,

	.dai_get_ctrl_blk = dai_get_ctrl_blk,
	.dai_put_ctrl_blk = dai_put_ctrl_blk,
	.dai_srt_set_srco = dai_srt_set_srcr,
	.dai_srt_set_srcm = dai_srt_set_srcl,
	.dai_srt_set_rsr = dai_srt_set_rsr,
	.dai_srt_set_drat = dai_srt_set_drat,
	.dai_srt_set_ec = dai_srt_set_ec,
	.dai_srt_set_et = dai_srt_set_et,
	.dai_commit_write = dai_commit_write,

	.dao_get_ctrl_blk = dao_get_ctrl_blk,
	.dao_put_ctrl_blk = dao_put_ctrl_blk,
	.dao_set_spos = dao_set_spos,
	.dao_commit_write = dao_commit_write,
	.dao_get_spos = dao_get_spos,

	.daio_mgr_get_ctrl_blk = daio_mgr_get_ctrl_blk,
	.daio_mgr_put_ctrl_blk = daio_mgr_put_ctrl_blk,
	.daio_mgr_enb_dai = daio_mgr_enb_dai,
	.daio_mgr_dsb_dai = daio_mgr_dsb_dai,
	.daio_mgr_enb_dao = daio_mgr_enb_dao,
	.daio_mgr_dsb_dao = daio_mgr_dsb_dao,
	.daio_mgr_dao_init = daio_mgr_dao_init,
	.daio_mgr_set_imaparc = daio_mgr_set_imaparc,
	.daio_mgr_set_imapnxt = daio_mgr_set_imapnxt,
	.daio_mgr_set_imapaddr = daio_mgr_set_imapaddr,
	.daio_mgr_commit_write = daio_mgr_commit_write,

	.set_timer_irq = set_timer_irq,
	.set_timer_tick = set_timer_tick,
	.get_wc = get_wc,
};

int create_20k1_hw_obj(struct hw **rhw)
{
	struct hw20k1 *hw20k1;

	*rhw = NULL;
	hw20k1 = kzalloc(sizeof(*hw20k1), GFP_KERNEL);
	if (!hw20k1)
		return -ENOMEM;

	spin_lock_init(&hw20k1->reg_20k1_lock);
	spin_lock_init(&hw20k1->reg_pci_lock);

	hw20k1->hw = ct20k1_preset;

	*rhw = &hw20k1->hw;

	return 0;
}

int destroy_20k1_hw_obj(struct hw *hw)
{
	if (hw->io_base)
		hw_card_shutdown(hw);

	kfree(container_of(hw, struct hw20k1, hw));
	return 0;
}
