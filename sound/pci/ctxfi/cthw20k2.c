/**
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * This source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution for the license terms and conditions.
 *
 * @File	cthw20k2.c
 *
 * @Brief
 * This file contains the implementation of hardware access method for 20k2.
 *
 * @Author	Liu Chun
 * @Date 	May 14 2008
 *
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include "cthw20k2.h"
#include "ct20k2reg.h"

#if BITS_PER_LONG == 32
#define CT_XFI_DMA_MASK		DMA_BIT_MASK(32) /* 32 bit PTE */
#else
#define CT_XFI_DMA_MASK		DMA_BIT_MASK(64) /* 64 bit PTE */
#endif

struct hw20k2 {
	struct hw hw;
	/* for i2c */
	unsigned char dev_id;
	unsigned char addr_size;
	unsigned char data_size;

	int mic_source;
};

static u32 hw_read_20kx(struct hw *hw, u32 reg);
static void hw_write_20kx(struct hw *hw, u32 reg, u32 data);

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

#define SRCCA_CA	0x0FFFFFFF
#define SRCCA_RS	0xE0000000

#define SRCSA_SA	0x0FFFFFFF

#define SRCLA_LA	0x0FFFFFFF

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
	kfree(blk);

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
			hw_write_20kx(hw, SRC_UPZ+idx*0x100+i*0x4, 0);

		for (i = 0; i < 4; i++)
			hw_write_20kx(hw, SRC_DN0Z+idx*0x100+i*0x4, 0);

		for (i = 0; i < 8; i++)
			hw_write_20kx(hw, SRC_DN1Z+idx*0x100+i*0x4, 0);

		ctl->dirty.bf.czbfs = 0;
	}
	if (ctl->dirty.bf.mpr) {
		/* Take the parameter mixer resource in the same group as that
		 * the idx src is in for simplicity. Unlike src, all conjugate
		 * parameter mixer resources must be programmed for
		 * corresponding conjugate src resources. */
		unsigned int pm_idx = src_param_pitch_mixer(idx);
		hw_write_20kx(hw, MIXER_PRING_LO_HI+4*pm_idx, ctl->mpr);
		hw_write_20kx(hw, MIXER_PMOPLO+8*pm_idx, 0x3);
		hw_write_20kx(hw, MIXER_PMOPHI+8*pm_idx, 0x0);
		ctl->dirty.bf.mpr = 0;
	}
	if (ctl->dirty.bf.sa) {
		hw_write_20kx(hw, SRC_SA+idx*0x100, ctl->sa);
		ctl->dirty.bf.sa = 0;
	}
	if (ctl->dirty.bf.la) {
		hw_write_20kx(hw, SRC_LA+idx*0x100, ctl->la);
		ctl->dirty.bf.la = 0;
	}
	if (ctl->dirty.bf.ca) {
		hw_write_20kx(hw, SRC_CA+idx*0x100, ctl->ca);
		ctl->dirty.bf.ca = 0;
	}

	/* Write srccf register */
	hw_write_20kx(hw, SRC_CF+idx*0x100, 0x0);

	if (ctl->dirty.bf.ccr) {
		hw_write_20kx(hw, SRC_CCR+idx*0x100, ctl->ccr);
		ctl->dirty.bf.ccr = 0;
	}
	if (ctl->dirty.bf.ctl) {
		hw_write_20kx(hw, SRC_CTL+idx*0x100, ctl->ctl);
		ctl->dirty.bf.ctl = 0;
	}

	return 0;
}

static int src_get_ca(struct hw *hw, unsigned int idx, void *blk)
{
	struct src_rsc_ctrl_blk *ctl = blk;

	ctl->ca = hw_read_20kx(hw, SRC_CA+idx*0x100);
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
	((struct src_mgr_ctrl_blk *)blk)->enbsa |= (0x1 << ((idx%128)/4));
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
			ret = hw_read_20kx(hw, SRC_ENBSTAT);
		} while (ret & 0x1);
		hw_write_20kx(hw, SRC_ENBSA, ctl->enbsa);
		ctl->dirty.bf.enbsa = 0;
	}
	for (i = 0; i < 8; i++) {
		if ((ctl->dirty.data & (0x1 << i))) {
			hw_write_20kx(hw, SRC_ENB+(i*0x100), ctl->enb[i]);
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
	kfree(blk);

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
	kfree(blk);

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
	((struct srcimp_mgr_ctrl_blk *)blk)->srcimap.idx = addr;
	((struct srcimp_mgr_ctrl_blk *)blk)->dirty.bf.srcimap = 1;
	return 0;
}

static int srcimp_mgr_commit_write(struct hw *hw, void *blk)
{
	struct srcimp_mgr_ctrl_blk *ctl = blk;

	if (ctl->dirty.bf.srcimap) {
		hw_write_20kx(hw, SRC_IMAP+ctl->srcimap.idx*0x100,
						ctl->srcimap.srcaim);
		ctl->dirty.bf.srcimap = 0;
	}

	return 0;
}

/*
 * AMIXER control block definitions.
 */

#define AMOPLO_M	0x00000003
#define AMOPLO_IV	0x00000004
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
	struct amixer_rsc_ctrl_blk *ctl = blk;

	set_field(&ctl->amoplo, AMOPLO_IV, iv);
	ctl->dirty.bf.amoplo = 1;
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
		hw_write_20kx(hw, MIXER_AMOPLO+idx*8, ctl->amoplo);
		ctl->dirty.bf.amoplo = 0;
		hw_write_20kx(hw, MIXER_AMOPHI+idx*8, ctl->amophi);
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
	kfree(blk);

	return 0;
}

static int amixer_mgr_get_ctrl_blk(void **rblk)
{
	*rblk = NULL;

	return 0;
}

static int amixer_mgr_put_ctrl_blk(void *blk)
{
	return 0;
}

/*
 * DAIO control block definitions.
 */

/* Receiver Sample Rate Tracker Control register */
#define SRTCTL_SRCO	0x000000FF
#define SRTCTL_SRCM	0x0000FF00
#define SRTCTL_RSR	0x00030000
#define SRTCTL_DRAT	0x00300000
#define SRTCTL_EC	0x01000000
#define SRTCTL_ET	0x10000000

/* DAIO Receiver register dirty flags */
union dai_dirty {
	struct {
		u16 srt:1;
		u16 rsv:15;
	} bf;
	u16 data;
};

/* DAIO Receiver control block */
struct dai_ctrl_blk {
	unsigned int	srt;
	union dai_dirty	dirty;
};

/* Audio Input Mapper RAM */
#define AIM_ARC		0x00000FFF
#define AIM_NXT		0x007F0000

struct daoimap {
	unsigned int aim;
	unsigned int idx;
};

/* Audio Transmitter Control and Status register */
#define ATXCTL_EN	0x00000001
#define ATXCTL_MODE	0x00000010
#define ATXCTL_CD	0x00000020
#define ATXCTL_RAW	0x00000100
#define ATXCTL_MT	0x00000200
#define ATXCTL_NUC	0x00003000
#define ATXCTL_BEN	0x00010000
#define ATXCTL_BMUX	0x00700000
#define ATXCTL_B24	0x01000000
#define ATXCTL_CPF	0x02000000
#define ATXCTL_RIV	0x10000000
#define ATXCTL_LIV	0x20000000
#define ATXCTL_RSAT	0x40000000
#define ATXCTL_LSAT	0x80000000

/* XDIF Transmitter register dirty flags */
union dao_dirty {
	struct {
		u16 atxcsl:1;
		u16 rsv:15;
	} bf;
	u16 data;
};

/* XDIF Transmitter control block */
struct dao_ctrl_blk {
	/* XDIF Transmitter Channel Status Low Register */
	unsigned int	atxcsl;
	union dao_dirty	dirty;
};

/* Audio Receiver Control register */
#define ARXCTL_EN	0x00000001

/* DAIO manager register dirty flags */
union daio_mgr_dirty {
	struct {
		u32 atxctl:8;
		u32 arxctl:8;
		u32 daoimap:1;
		u32 rsv:15;
	} bf;
	u32 data;
};

/* DAIO manager control block */
struct daio_mgr_ctrl_blk {
	struct daoimap		daoimap;
	unsigned int		txctl[8];
	unsigned int		rxctl[8];
	union daio_mgr_dirty	dirty;
};

static int dai_srt_set_srco(void *blk, unsigned int src)
{
	struct dai_ctrl_blk *ctl = blk;

	set_field(&ctl->srt, SRTCTL_SRCO, src);
	ctl->dirty.bf.srt = 1;
	return 0;
}

static int dai_srt_set_srcm(void *blk, unsigned int src)
{
	struct dai_ctrl_blk *ctl = blk;

	set_field(&ctl->srt, SRTCTL_SRCM, src);
	ctl->dirty.bf.srt = 1;
	return 0;
}

static int dai_srt_set_rsr(void *blk, unsigned int rsr)
{
	struct dai_ctrl_blk *ctl = blk;

	set_field(&ctl->srt, SRTCTL_RSR, rsr);
	ctl->dirty.bf.srt = 1;
	return 0;
}

static int dai_srt_set_drat(void *blk, unsigned int drat)
{
	struct dai_ctrl_blk *ctl = blk;

	set_field(&ctl->srt, SRTCTL_DRAT, drat);
	ctl->dirty.bf.srt = 1;
	return 0;
}

static int dai_srt_set_ec(void *blk, unsigned int ec)
{
	struct dai_ctrl_blk *ctl = blk;

	set_field(&ctl->srt, SRTCTL_EC, ec ? 1 : 0);
	ctl->dirty.bf.srt = 1;
	return 0;
}

static int dai_srt_set_et(void *blk, unsigned int et)
{
	struct dai_ctrl_blk *ctl = blk;

	set_field(&ctl->srt, SRTCTL_ET, et ? 1 : 0);
	ctl->dirty.bf.srt = 1;
	return 0;
}

static int dai_commit_write(struct hw *hw, unsigned int idx, void *blk)
{
	struct dai_ctrl_blk *ctl = blk;

	if (ctl->dirty.bf.srt) {
		hw_write_20kx(hw, AUDIO_IO_RX_SRT_CTL+0x40*idx, ctl->srt);
		ctl->dirty.bf.srt = 0;
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
	kfree(blk);

	return 0;
}

static int dao_set_spos(void *blk, unsigned int spos)
{
	((struct dao_ctrl_blk *)blk)->atxcsl = spos;
	((struct dao_ctrl_blk *)blk)->dirty.bf.atxcsl = 1;
	return 0;
}

static int dao_commit_write(struct hw *hw, unsigned int idx, void *blk)
{
	struct dao_ctrl_blk *ctl = blk;

	if (ctl->dirty.bf.atxcsl) {
		if (idx < 4) {
			/* S/PDIF SPOSx */
			hw_write_20kx(hw, AUDIO_IO_TX_CSTAT_L+0x40*idx,
							ctl->atxcsl);
		}
		ctl->dirty.bf.atxcsl = 0;
	}

	return 0;
}

static int dao_get_spos(void *blk, unsigned int *spos)
{
	*spos = ((struct dao_ctrl_blk *)blk)->atxcsl;
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
	kfree(blk);

	return 0;
}

static int daio_mgr_enb_dai(void *blk, unsigned int idx)
{
	struct daio_mgr_ctrl_blk *ctl = blk;

	set_field(&ctl->rxctl[idx], ARXCTL_EN, 1);
	ctl->dirty.bf.arxctl |= (0x1 << idx);
	return 0;
}

static int daio_mgr_dsb_dai(void *blk, unsigned int idx)
{
	struct daio_mgr_ctrl_blk *ctl = blk;

	set_field(&ctl->rxctl[idx], ARXCTL_EN, 0);

	ctl->dirty.bf.arxctl |= (0x1 << idx);
	return 0;
}

static int daio_mgr_enb_dao(void *blk, unsigned int idx)
{
	struct daio_mgr_ctrl_blk *ctl = blk;

	set_field(&ctl->txctl[idx], ATXCTL_EN, 1);
	ctl->dirty.bf.atxctl |= (0x1 << idx);
	return 0;
}

static int daio_mgr_dsb_dao(void *blk, unsigned int idx)
{
	struct daio_mgr_ctrl_blk *ctl = blk;

	set_field(&ctl->txctl[idx], ATXCTL_EN, 0);
	ctl->dirty.bf.atxctl |= (0x1 << idx);
	return 0;
}

static int daio_mgr_dao_init(void *blk, unsigned int idx, unsigned int conf)
{
	struct daio_mgr_ctrl_blk *ctl = blk;

	if (idx < 4) {
		/* S/PDIF output */
		switch ((conf & 0x7)) {
		case 1:
			set_field(&ctl->txctl[idx], ATXCTL_NUC, 0);
			break;
		case 2:
			set_field(&ctl->txctl[idx], ATXCTL_NUC, 1);
			break;
		case 4:
			set_field(&ctl->txctl[idx], ATXCTL_NUC, 2);
			break;
		case 8:
			set_field(&ctl->txctl[idx], ATXCTL_NUC, 3);
			break;
		default:
			break;
		}
		/* CDIF */
		set_field(&ctl->txctl[idx], ATXCTL_CD, (!(conf & 0x7)));
		/* Non-audio */
		set_field(&ctl->txctl[idx], ATXCTL_LIV, (conf >> 4) & 0x1);
		/* Non-audio */
		set_field(&ctl->txctl[idx], ATXCTL_RIV, (conf >> 4) & 0x1);
		set_field(&ctl->txctl[idx], ATXCTL_RAW,
			  ((conf >> 3) & 0x1) ? 0 : 0);
		ctl->dirty.bf.atxctl |= (0x1 << idx);
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
	((struct daio_mgr_ctrl_blk *)blk)->daoimap.idx = addr;
	((struct daio_mgr_ctrl_blk *)blk)->dirty.bf.daoimap = 1;
	return 0;
}

static int daio_mgr_commit_write(struct hw *hw, void *blk)
{
	struct daio_mgr_ctrl_blk *ctl = blk;
	unsigned int data;
	int i;

	for (i = 0; i < 8; i++) {
		if ((ctl->dirty.bf.atxctl & (0x1 << i))) {
			data = ctl->txctl[i];
			hw_write_20kx(hw, (AUDIO_IO_TX_CTL+(0x40*i)), data);
			ctl->dirty.bf.atxctl &= ~(0x1 << i);
			mdelay(1);
		}
		if ((ctl->dirty.bf.arxctl & (0x1 << i))) {
			data = ctl->rxctl[i];
			hw_write_20kx(hw, (AUDIO_IO_RX_CTL+(0x40*i)), data);
			ctl->dirty.bf.arxctl &= ~(0x1 << i);
			mdelay(1);
		}
	}
	if (ctl->dirty.bf.daoimap) {
		hw_write_20kx(hw, AUDIO_IO_AIM+ctl->daoimap.idx*4,
						ctl->daoimap.aim);
		ctl->dirty.bf.daoimap = 0;
	}

	return 0;
}

static int daio_mgr_get_ctrl_blk(struct hw *hw, void **rblk)
{
	struct daio_mgr_ctrl_blk *blk;
	int i;

	*rblk = NULL;
	blk = kzalloc(sizeof(*blk), GFP_KERNEL);
	if (!blk)
		return -ENOMEM;

	for (i = 0; i < 8; i++) {
		blk->txctl[i] = hw_read_20kx(hw, AUDIO_IO_TX_CTL+(0x40*i));
		blk->rxctl[i] = hw_read_20kx(hw, AUDIO_IO_RX_CTL+(0x40*i));
	}

	*rblk = blk;

	return 0;
}

static int daio_mgr_put_ctrl_blk(void *blk)
{
	kfree(blk);

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
	u32 data;
	int i;

	/* Program I2S with proper sample rate and enable the correct I2S
	 * channel. ED(0/8/16/24): Enable all I2S/I2X master clock output */
	if (1 == info->msr) {
		hw_write_20kx(hw, AUDIO_IO_MCLK, 0x01010101);
		hw_write_20kx(hw, AUDIO_IO_TX_BLRCLK, 0x01010101);
		hw_write_20kx(hw, AUDIO_IO_RX_BLRCLK, 0);
	} else if (2 == info->msr) {
		if (hw->model != CTSB1270) {
			hw_write_20kx(hw, AUDIO_IO_MCLK, 0x11111111);
		} else {
			/* PCM4220 on Titanium HD is different. */
			hw_write_20kx(hw, AUDIO_IO_MCLK, 0x11011111);
		}
		/* Specify all playing 96khz
		 * EA [0]	- Enabled
		 * RTA [4:5]	- 96kHz
		 * EB [8]	- Enabled
		 * RTB [12:13]	- 96kHz
		 * EC [16]	- Enabled
		 * RTC [20:21]	- 96kHz
		 * ED [24]	- Enabled
		 * RTD [28:29]	- 96kHz */
		hw_write_20kx(hw, AUDIO_IO_TX_BLRCLK, 0x11111111);
		hw_write_20kx(hw, AUDIO_IO_RX_BLRCLK, 0);
	} else if ((4 == info->msr) && (hw->model == CTSB1270)) {
		hw_write_20kx(hw, AUDIO_IO_MCLK, 0x21011111);
		hw_write_20kx(hw, AUDIO_IO_TX_BLRCLK, 0x21212121);
		hw_write_20kx(hw, AUDIO_IO_RX_BLRCLK, 0);
	} else {
		dev_alert(hw->card->dev,
			  "ERROR!!! Invalid sampling rate!!!\n");
		return -EINVAL;
	}

	for (i = 0; i < 8; i++) {
		if (i <= 3) {
			/* This comment looks wrong since loop is over 4  */
			/* channels and emu20k2 supports 4 spdif IOs.     */
			/* 1st 3 channels are SPDIFs (SB0960) */
			if (i == 3)
				data = 0x1001001;
			else
				data = 0x1000001;

			hw_write_20kx(hw, (AUDIO_IO_TX_CTL+(0x40*i)), data);
			hw_write_20kx(hw, (AUDIO_IO_RX_CTL+(0x40*i)), data);

			/* Initialize the SPDIF Out Channel status registers.
			 * The value specified here is based on the typical
			 * values provided in the specification, namely: Clock
			 * Accuracy of 1000ppm, Sample Rate of 48KHz,
			 * unspecified source number, Generation status = 1,
			 * Category code = 0x12 (Digital Signal Mixer),
			 * Mode = 0, Emph = 0, Copy Permitted, AN = 0
			 * (indicating that we're transmitting digital audio,
			 * and the Professional Use bit is 0. */

			hw_write_20kx(hw, AUDIO_IO_TX_CSTAT_L+(0x40*i),
					0x02109204); /* Default to 48kHz */

			hw_write_20kx(hw, AUDIO_IO_TX_CSTAT_H+(0x40*i), 0x0B);
		} else {
			/* Again, loop is over 4 channels not 5. */
			/* Next 5 channels are I2S (SB0960) */
			data = 0x11;
			hw_write_20kx(hw, AUDIO_IO_RX_CTL+(0x40*i), data);
			if (2 == info->msr) {
				/* Four channels per sample period */
				data |= 0x1000;
			} else if (4 == info->msr) {
				/* FIXME: check this against the chip spec */
				data |= 0x2000;
			}
			hw_write_20kx(hw, AUDIO_IO_TX_CTL+(0x40*i), data);
		}
	}

	return 0;
}

/* TRANSPORT operations */
static int hw_trn_init(struct hw *hw, const struct trn_conf *info)
{
	u32 vmctl, data;
	u32 ptp_phys_low, ptp_phys_high;
	int i;

	/* Set up device page table */
	if ((~0UL) == info->vm_pgt_phys) {
		dev_alert(hw->card->dev,
			  "Wrong device page table page address!!!\n");
		return -1;
	}

	vmctl = 0x80000C0F;  /* 32-bit, 4k-size page */
	ptp_phys_low = (u32)info->vm_pgt_phys;
	ptp_phys_high = upper_32_bits(info->vm_pgt_phys);
	if (sizeof(void *) == 8) /* 64bit address */
		vmctl |= (3 << 8);
	/* Write page table physical address to all PTPAL registers */
	for (i = 0; i < 64; i++) {
		hw_write_20kx(hw, VMEM_PTPAL+(16*i), ptp_phys_low);
		hw_write_20kx(hw, VMEM_PTPAH+(16*i), ptp_phys_high);
	}
	/* Enable virtual memory transfer */
	hw_write_20kx(hw, VMEM_CTL, vmctl);
	/* Enable transport bus master and queueing of request */
	hw_write_20kx(hw, TRANSPORT_CTL, 0x03);
	hw_write_20kx(hw, TRANSPORT_INT, 0x200c01);
	/* Enable transport ring */
	data = hw_read_20kx(hw, TRANSPORT_ENB);
	hw_write_20kx(hw, TRANSPORT_ENB, (data | 0x03));

	return 0;
}

/* Card initialization */
#define GCTL_AIE	0x00000001
#define GCTL_UAA	0x00000002
#define GCTL_DPC	0x00000004
#define GCTL_DBP	0x00000008
#define GCTL_ABP	0x00000010
#define GCTL_TBP	0x00000020
#define GCTL_SBP	0x00000040
#define GCTL_FBP	0x00000080
#define GCTL_ME		0x00000100
#define GCTL_AID	0x00001000

#define PLLCTL_SRC	0x00000007
#define PLLCTL_SPE	0x00000008
#define PLLCTL_RD	0x000000F0
#define PLLCTL_FD	0x0001FF00
#define PLLCTL_OD	0x00060000
#define PLLCTL_B	0x00080000
#define PLLCTL_AS	0x00100000
#define PLLCTL_LF	0x03E00000
#define PLLCTL_SPS	0x1C000000
#define PLLCTL_AD	0x60000000

#define PLLSTAT_CCS	0x00000007
#define PLLSTAT_SPL	0x00000008
#define PLLSTAT_CRD	0x000000F0
#define PLLSTAT_CFD	0x0001FF00
#define PLLSTAT_SL	0x00020000
#define PLLSTAT_FAS	0x00040000
#define PLLSTAT_B	0x00080000
#define PLLSTAT_PD	0x00100000
#define PLLSTAT_OCA	0x00200000
#define PLLSTAT_NCA	0x00400000

static int hw_pll_init(struct hw *hw, unsigned int rsr)
{
	unsigned int pllenb;
	unsigned int pllctl;
	unsigned int pllstat;
	int i;

	pllenb = 0xB;
	hw_write_20kx(hw, PLL_ENB, pllenb);
	pllctl = 0x20C00000;
	set_field(&pllctl, PLLCTL_B, 0);
	set_field(&pllctl, PLLCTL_FD, 48000 == rsr ? 16 - 4 : 147 - 4);
	set_field(&pllctl, PLLCTL_RD, 48000 == rsr ? 1 - 1 : 10 - 1);
	hw_write_20kx(hw, PLL_CTL, pllctl);
	mdelay(40);

	pllctl = hw_read_20kx(hw, PLL_CTL);
	set_field(&pllctl, PLLCTL_FD, 48000 == rsr ? 16 - 2 : 147 - 2);
	hw_write_20kx(hw, PLL_CTL, pllctl);
	mdelay(40);

	for (i = 0; i < 1000; i++) {
		pllstat = hw_read_20kx(hw, PLL_STAT);
		if (get_field(pllstat, PLLSTAT_PD))
			continue;

		if (get_field(pllstat, PLLSTAT_B) !=
					get_field(pllctl, PLLCTL_B))
			continue;

		if (get_field(pllstat, PLLSTAT_CCS) !=
					get_field(pllctl, PLLCTL_SRC))
			continue;

		if (get_field(pllstat, PLLSTAT_CRD) !=
					get_field(pllctl, PLLCTL_RD))
			continue;

		if (get_field(pllstat, PLLSTAT_CFD) !=
					get_field(pllctl, PLLCTL_FD))
			continue;

		break;
	}
	if (i >= 1000) {
		dev_alert(hw->card->dev,
			  "PLL initialization failed!!!\n");
		return -EBUSY;
	}

	return 0;
}

static int hw_auto_init(struct hw *hw)
{
	unsigned int gctl;
	int i;

	gctl = hw_read_20kx(hw, GLOBAL_CNTL_GCTL);
	set_field(&gctl, GCTL_AIE, 0);
	hw_write_20kx(hw, GLOBAL_CNTL_GCTL, gctl);
	set_field(&gctl, GCTL_AIE, 1);
	hw_write_20kx(hw, GLOBAL_CNTL_GCTL, gctl);
	mdelay(10);
	for (i = 0; i < 400000; i++) {
		gctl = hw_read_20kx(hw, GLOBAL_CNTL_GCTL);
		if (get_field(gctl, GCTL_AID))
			break;
	}
	if (!get_field(gctl, GCTL_AID)) {
		dev_alert(hw->card->dev, "Card Auto-init failed!!!\n");
		return -EBUSY;
	}

	return 0;
}

/* DAC operations */

#define CS4382_MC1 		0x1
#define CS4382_MC2 		0x2
#define CS4382_MC3		0x3
#define CS4382_FC		0x4
#define CS4382_IC		0x5
#define CS4382_XC1		0x6
#define CS4382_VCA1 		0x7
#define CS4382_VCB1 		0x8
#define CS4382_XC2		0x9
#define CS4382_VCA2 		0xA
#define CS4382_VCB2 		0xB
#define CS4382_XC3		0xC
#define CS4382_VCA3		0xD
#define CS4382_VCB3		0xE
#define CS4382_XC4 		0xF
#define CS4382_VCA4 		0x10
#define CS4382_VCB4 		0x11
#define CS4382_CREV 		0x12

/* I2C status */
#define STATE_LOCKED		0x00
#define STATE_UNLOCKED		0xAA
#define DATA_READY		0x800000    /* Used with I2C_IF_STATUS */
#define DATA_ABORT		0x10000     /* Used with I2C_IF_STATUS */

#define I2C_STATUS_DCM	0x00000001
#define I2C_STATUS_BC	0x00000006
#define I2C_STATUS_APD	0x00000008
#define I2C_STATUS_AB	0x00010000
#define I2C_STATUS_DR	0x00800000

#define I2C_ADDRESS_PTAD	0x0000FFFF
#define I2C_ADDRESS_SLAD	0x007F0000

struct regs_cs4382 {
	u32 mode_control_1;
	u32 mode_control_2;
	u32 mode_control_3;

	u32 filter_control;
	u32 invert_control;

	u32 mix_control_P1;
	u32 vol_control_A1;
	u32 vol_control_B1;

	u32 mix_control_P2;
	u32 vol_control_A2;
	u32 vol_control_B2;

	u32 mix_control_P3;
	u32 vol_control_A3;
	u32 vol_control_B3;

	u32 mix_control_P4;
	u32 vol_control_A4;
	u32 vol_control_B4;
};

static int hw20k2_i2c_unlock_full_access(struct hw *hw)
{
	u8 UnlockKeySequence_FLASH_FULLACCESS_MODE[2] =  {0xB3, 0xD4};

	/* Send keys for forced BIOS mode */
	hw_write_20kx(hw, I2C_IF_WLOCK,
			UnlockKeySequence_FLASH_FULLACCESS_MODE[0]);
	hw_write_20kx(hw, I2C_IF_WLOCK,
			UnlockKeySequence_FLASH_FULLACCESS_MODE[1]);
	/* Check whether the chip is unlocked */
	if (hw_read_20kx(hw, I2C_IF_WLOCK) == STATE_UNLOCKED)
		return 0;

	return -1;
}

static int hw20k2_i2c_lock_chip(struct hw *hw)
{
	/* Write twice */
	hw_write_20kx(hw, I2C_IF_WLOCK, STATE_LOCKED);
	hw_write_20kx(hw, I2C_IF_WLOCK, STATE_LOCKED);
	if (hw_read_20kx(hw, I2C_IF_WLOCK) == STATE_LOCKED)
		return 0;

	return -1;
}

static int hw20k2_i2c_init(struct hw *hw, u8 dev_id, u8 addr_size, u8 data_size)
{
	struct hw20k2 *hw20k2 = (struct hw20k2 *)hw;
	int err;
	unsigned int i2c_status;
	unsigned int i2c_addr;

	err = hw20k2_i2c_unlock_full_access(hw);
	if (err < 0)
		return err;

	hw20k2->addr_size = addr_size;
	hw20k2->data_size = data_size;
	hw20k2->dev_id = dev_id;

	i2c_addr = 0;
	set_field(&i2c_addr, I2C_ADDRESS_SLAD, dev_id);

	hw_write_20kx(hw, I2C_IF_ADDRESS, i2c_addr);

	i2c_status = hw_read_20kx(hw, I2C_IF_STATUS);

	set_field(&i2c_status, I2C_STATUS_DCM, 1); /* Direct control mode */

	hw_write_20kx(hw, I2C_IF_STATUS, i2c_status);

	return 0;
}

static int hw20k2_i2c_uninit(struct hw *hw)
{
	unsigned int i2c_status;
	unsigned int i2c_addr;

	i2c_addr = 0;
	set_field(&i2c_addr, I2C_ADDRESS_SLAD, 0x57); /* I2C id */

	hw_write_20kx(hw, I2C_IF_ADDRESS, i2c_addr);

	i2c_status = hw_read_20kx(hw, I2C_IF_STATUS);

	set_field(&i2c_status, I2C_STATUS_DCM, 0); /* I2C mode */

	hw_write_20kx(hw, I2C_IF_STATUS, i2c_status);

	return hw20k2_i2c_lock_chip(hw);
}

static int hw20k2_i2c_wait_data_ready(struct hw *hw)
{
	int i = 0x400000;
	unsigned int ret;

	do {
		ret = hw_read_20kx(hw, I2C_IF_STATUS);
	} while ((!(ret & DATA_READY)) && --i);

	return i;
}

static int hw20k2_i2c_read(struct hw *hw, u16 addr, u32 *datap)
{
	struct hw20k2 *hw20k2 = (struct hw20k2 *)hw;
	unsigned int i2c_status;

	i2c_status = hw_read_20kx(hw, I2C_IF_STATUS);
	set_field(&i2c_status, I2C_STATUS_BC,
		  (4 == hw20k2->addr_size) ? 0 : hw20k2->addr_size);
	hw_write_20kx(hw, I2C_IF_STATUS, i2c_status);
	if (!hw20k2_i2c_wait_data_ready(hw))
		return -1;

	hw_write_20kx(hw, I2C_IF_WDATA, addr);
	if (!hw20k2_i2c_wait_data_ready(hw))
		return -1;

	/* Force a read operation */
	hw_write_20kx(hw, I2C_IF_RDATA, 0);
	if (!hw20k2_i2c_wait_data_ready(hw))
		return -1;

	*datap = hw_read_20kx(hw, I2C_IF_RDATA);

	return 0;
}

static int hw20k2_i2c_write(struct hw *hw, u16 addr, u32 data)
{
	struct hw20k2 *hw20k2 = (struct hw20k2 *)hw;
	unsigned int i2c_data = (data << (hw20k2->addr_size * 8)) | addr;
	unsigned int i2c_status;

	i2c_status = hw_read_20kx(hw, I2C_IF_STATUS);

	set_field(&i2c_status, I2C_STATUS_BC,
		  (4 == (hw20k2->addr_size + hw20k2->data_size)) ?
		  0 : (hw20k2->addr_size + hw20k2->data_size));

	hw_write_20kx(hw, I2C_IF_STATUS, i2c_status);
	hw20k2_i2c_wait_data_ready(hw);
	/* Dummy write to trigger the write operation */
	hw_write_20kx(hw, I2C_IF_WDATA, 0);
	hw20k2_i2c_wait_data_ready(hw);

	/* This is the real data */
	hw_write_20kx(hw, I2C_IF_WDATA, i2c_data);
	hw20k2_i2c_wait_data_ready(hw);

	return 0;
}

static void hw_dac_stop(struct hw *hw)
{
	u32 data;
	data = hw_read_20kx(hw, GPIO_DATA);
	data &= 0xFFFFFFFD;
	hw_write_20kx(hw, GPIO_DATA, data);
	mdelay(10);
}

static void hw_dac_start(struct hw *hw)
{
	u32 data;
	data = hw_read_20kx(hw, GPIO_DATA);
	data |= 0x2;
	hw_write_20kx(hw, GPIO_DATA, data);
	mdelay(50);
}

static void hw_dac_reset(struct hw *hw)
{
	hw_dac_stop(hw);
	hw_dac_start(hw);
}

static int hw_dac_init(struct hw *hw, const struct dac_conf *info)
{
	int err;
	u32 data;
	int i;
	struct regs_cs4382 cs_read = {0};
	struct regs_cs4382 cs_def = {
				   0x00000001,  /* Mode Control 1 */
				   0x00000000,  /* Mode Control 2 */
				   0x00000084,  /* Mode Control 3 */
				   0x00000000,  /* Filter Control */
				   0x00000000,  /* Invert Control */
				   0x00000024,  /* Mixing Control Pair 1 */
				   0x00000000,  /* Vol Control A1 */
				   0x00000000,  /* Vol Control B1 */
				   0x00000024,  /* Mixing Control Pair 2 */
				   0x00000000,  /* Vol Control A2 */
				   0x00000000,  /* Vol Control B2 */
				   0x00000024,  /* Mixing Control Pair 3 */
				   0x00000000,  /* Vol Control A3 */
				   0x00000000,  /* Vol Control B3 */
				   0x00000024,  /* Mixing Control Pair 4 */
				   0x00000000,  /* Vol Control A4 */
				   0x00000000   /* Vol Control B4 */
				 };

	if (hw->model == CTSB1270) {
		hw_dac_stop(hw);
		data = hw_read_20kx(hw, GPIO_DATA);
		data &= ~0x0600;
		if (1 == info->msr)
			data |= 0x0000; /* Single Speed Mode 0-50kHz */
		else if (2 == info->msr)
			data |= 0x0200; /* Double Speed Mode 50-100kHz */
		else
			data |= 0x0600; /* Quad Speed Mode 100-200kHz */
		hw_write_20kx(hw, GPIO_DATA, data);
		hw_dac_start(hw);
		return 0;
	}

	/* Set DAC reset bit as output */
	data = hw_read_20kx(hw, GPIO_CTRL);
	data |= 0x02;
	hw_write_20kx(hw, GPIO_CTRL, data);

	err = hw20k2_i2c_init(hw, 0x18, 1, 1);
	if (err < 0)
		goto End;

	for (i = 0; i < 2; i++) {
		/* Reset DAC twice just in-case the chip
		 * didn't initialized properly */
		hw_dac_reset(hw);
		hw_dac_reset(hw);

		if (hw20k2_i2c_read(hw, CS4382_MC1,  &cs_read.mode_control_1))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_MC2,  &cs_read.mode_control_2))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_MC3,  &cs_read.mode_control_3))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_FC,   &cs_read.filter_control))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_IC,   &cs_read.invert_control))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_XC1,  &cs_read.mix_control_P1))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_VCA1, &cs_read.vol_control_A1))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_VCB1, &cs_read.vol_control_B1))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_XC2,  &cs_read.mix_control_P2))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_VCA2, &cs_read.vol_control_A2))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_VCB2, &cs_read.vol_control_B2))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_XC3,  &cs_read.mix_control_P3))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_VCA3, &cs_read.vol_control_A3))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_VCB3, &cs_read.vol_control_B3))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_XC4,  &cs_read.mix_control_P4))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_VCA4, &cs_read.vol_control_A4))
			continue;

		if (hw20k2_i2c_read(hw, CS4382_VCB4, &cs_read.vol_control_B4))
			continue;

		if (memcmp(&cs_read, &cs_def, sizeof(cs_read)))
			continue;
		else
			break;
	}

	if (i >= 2)
		goto End;

	/* Note: Every I2C write must have some delay.
	 * This is not a requirement but the delay works here... */
	hw20k2_i2c_write(hw, CS4382_MC1, 0x80);
	hw20k2_i2c_write(hw, CS4382_MC2, 0x10);
	if (1 == info->msr) {
		hw20k2_i2c_write(hw, CS4382_XC1, 0x24);
		hw20k2_i2c_write(hw, CS4382_XC2, 0x24);
		hw20k2_i2c_write(hw, CS4382_XC3, 0x24);
		hw20k2_i2c_write(hw, CS4382_XC4, 0x24);
	} else if (2 == info->msr) {
		hw20k2_i2c_write(hw, CS4382_XC1, 0x25);
		hw20k2_i2c_write(hw, CS4382_XC2, 0x25);
		hw20k2_i2c_write(hw, CS4382_XC3, 0x25);
		hw20k2_i2c_write(hw, CS4382_XC4, 0x25);
	} else {
		hw20k2_i2c_write(hw, CS4382_XC1, 0x26);
		hw20k2_i2c_write(hw, CS4382_XC2, 0x26);
		hw20k2_i2c_write(hw, CS4382_XC3, 0x26);
		hw20k2_i2c_write(hw, CS4382_XC4, 0x26);
	}

	return 0;
End:

	hw20k2_i2c_uninit(hw);
	return -1;
}

/* ADC operations */
#define MAKE_WM8775_ADDR(addr, data)	(u32)(((addr<<1)&0xFE)|((data>>8)&0x1))
#define MAKE_WM8775_DATA(data)	(u32)(data&0xFF)

#define WM8775_IC       0x0B
#define WM8775_MMC      0x0C
#define WM8775_AADCL    0x0E
#define WM8775_AADCR    0x0F
#define WM8775_ADCMC    0x15
#define WM8775_RESET    0x17

static int hw_is_adc_input_selected(struct hw *hw, enum ADCSRC type)
{
	u32 data;
	if (hw->model == CTSB1270) {
		/* Titanium HD has two ADC chips, one for line in and one */
		/* for MIC. We don't need to switch the ADC input. */
		return 1;
	}
	data = hw_read_20kx(hw, GPIO_DATA);
	switch (type) {
	case ADC_MICIN:
		data = (data & (0x1 << 14)) ? 1 : 0;
		break;
	case ADC_LINEIN:
		data = (data & (0x1 << 14)) ? 0 : 1;
		break;
	default:
		data = 0;
	}
	return data;
}

#define MIC_BOOST_0DB 0xCF
#define MIC_BOOST_STEPS_PER_DB 2

static void hw_wm8775_input_select(struct hw *hw, u8 input, s8 gain_in_db)
{
	u32 adcmc, gain;

	if (input > 3)
		input = 3;

	adcmc = ((u32)1 << input) | 0x100; /* Link L+R gain... */

	hw20k2_i2c_write(hw, MAKE_WM8775_ADDR(WM8775_ADCMC, adcmc),
				MAKE_WM8775_DATA(adcmc));

	if (gain_in_db < -103)
		gain_in_db = -103;
	if (gain_in_db > 24)
		gain_in_db = 24;

	gain = gain_in_db * MIC_BOOST_STEPS_PER_DB + MIC_BOOST_0DB;

	hw20k2_i2c_write(hw, MAKE_WM8775_ADDR(WM8775_AADCL, gain),
				MAKE_WM8775_DATA(gain));
	/* ...so there should be no need for the following. */
	hw20k2_i2c_write(hw, MAKE_WM8775_ADDR(WM8775_AADCR, gain),
				MAKE_WM8775_DATA(gain));
}

static int hw_adc_input_select(struct hw *hw, enum ADCSRC type)
{
	u32 data;
	data = hw_read_20kx(hw, GPIO_DATA);
	switch (type) {
	case ADC_MICIN:
		data |= (0x1 << 14);
		hw_write_20kx(hw, GPIO_DATA, data);
		hw_wm8775_input_select(hw, 0, 20); /* Mic, 20dB */
		break;
	case ADC_LINEIN:
		data &= ~(0x1 << 14);
		hw_write_20kx(hw, GPIO_DATA, data);
		hw_wm8775_input_select(hw, 1, 0); /* Line-in, 0dB */
		break;
	default:
		break;
	}

	return 0;
}

static int hw_adc_init(struct hw *hw, const struct adc_conf *info)
{
	int err;
	u32 data, ctl;

	/*  Set ADC reset bit as output */
	data = hw_read_20kx(hw, GPIO_CTRL);
	data |= (0x1 << 15);
	hw_write_20kx(hw, GPIO_CTRL, data);

	/* Initialize I2C */
	err = hw20k2_i2c_init(hw, 0x1A, 1, 1);
	if (err < 0) {
		dev_alert(hw->card->dev, "Failure to acquire I2C!!!\n");
		goto error;
	}

	/* Reset the ADC (reset is active low). */
	data = hw_read_20kx(hw, GPIO_DATA);
	data &= ~(0x1 << 15);
	hw_write_20kx(hw, GPIO_DATA, data);

	if (hw->model == CTSB1270) {
		/* Set up the PCM4220 ADC on Titanium HD */
		data &= ~0x0C;
		if (1 == info->msr)
			data |= 0x00; /* Single Speed Mode 32-50kHz */
		else if (2 == info->msr)
			data |= 0x08; /* Double Speed Mode 50-108kHz */
		else
			data |= 0x04; /* Quad Speed Mode 108kHz-216kHz */
		hw_write_20kx(hw, GPIO_DATA, data);
	}

	mdelay(10);
	/* Return the ADC to normal operation. */
	data |= (0x1 << 15);
	hw_write_20kx(hw, GPIO_DATA, data);
	mdelay(50);

	/* I2C write to register offset 0x0B to set ADC LRCLK polarity */
	/* invert bit, interface format to I2S, word length to 24-bit, */
	/* enable ADC high pass filter. Fixes bug 5323?		*/
	hw20k2_i2c_write(hw, MAKE_WM8775_ADDR(WM8775_IC, 0x26),
			 MAKE_WM8775_DATA(0x26));

	/* Set the master mode (256fs) */
	if (1 == info->msr) {
		/* slave mode, 128x oversampling 256fs */
		hw20k2_i2c_write(hw, MAKE_WM8775_ADDR(WM8775_MMC, 0x02),
						MAKE_WM8775_DATA(0x02));
	} else if ((2 == info->msr) || (4 == info->msr)) {
		/* slave mode, 64x oversampling, 256fs */
		hw20k2_i2c_write(hw, MAKE_WM8775_ADDR(WM8775_MMC, 0x0A),
						MAKE_WM8775_DATA(0x0A));
	} else {
		dev_alert(hw->card->dev,
			  "Invalid master sampling rate (msr %d)!!!\n",
			  info->msr);
		err = -EINVAL;
		goto error;
	}

	if (hw->model != CTSB1270) {
		/* Configure GPIO bit 14 change to line-in/mic-in */
		ctl = hw_read_20kx(hw, GPIO_CTRL);
		ctl |= 0x1 << 14;
		hw_write_20kx(hw, GPIO_CTRL, ctl);
		hw_adc_input_select(hw, ADC_LINEIN);
	} else {
		hw_wm8775_input_select(hw, 0, 0);
	}

	return 0;
error:
	hw20k2_i2c_uninit(hw);
	return err;
}

static struct capabilities hw_capabilities(struct hw *hw)
{
	struct capabilities cap;

	cap.digit_io_switch = 0;
	cap.dedicated_mic = hw->model == CTSB1270;
	cap.output_switch = hw->model == CTSB1270;
	cap.mic_source_switch = hw->model == CTSB1270;

	return cap;
}

static int hw_output_switch_get(struct hw *hw)
{
	u32 data = hw_read_20kx(hw, GPIO_EXT_DATA);

	switch (data & 0x30) {
	case 0x00:
	     return 0;
	case 0x10:
	     return 1;
	case 0x20:
	     return 2;
	default:
	     return 3;
	}
}

static int hw_output_switch_put(struct hw *hw, int position)
{
	u32 data;

	if (position == hw_output_switch_get(hw))
		return 0;

	/* Mute line and headphones (intended for anti-pop). */
	data = hw_read_20kx(hw, GPIO_DATA);
	data |= (0x03 << 11);
	hw_write_20kx(hw, GPIO_DATA, data);

	data = hw_read_20kx(hw, GPIO_EXT_DATA) & ~0x30;
	switch (position) {
	case 0:
		break;
	case 1:
		data |= 0x10;
		break;
	default:
		data |= 0x20;
	}
	hw_write_20kx(hw, GPIO_EXT_DATA, data);

	/* Unmute line and headphones. */
	data = hw_read_20kx(hw, GPIO_DATA);
	data &= ~(0x03 << 11);
	hw_write_20kx(hw, GPIO_DATA, data);

	return 1;
}

static int hw_mic_source_switch_get(struct hw *hw)
{
	struct hw20k2 *hw20k2 = (struct hw20k2 *)hw;

	return hw20k2->mic_source;
}

static int hw_mic_source_switch_put(struct hw *hw, int position)
{
	struct hw20k2 *hw20k2 = (struct hw20k2 *)hw;

	if (position == hw20k2->mic_source)
		return 0;

	switch (position) {
	case 0:
		hw_wm8775_input_select(hw, 0, 0); /* Mic, 0dB */
		break;
	case 1:
		hw_wm8775_input_select(hw, 1, 0); /* FP Mic, 0dB */
		break;
	case 2:
		hw_wm8775_input_select(hw, 3, 0); /* Aux Ext, 0dB */
		break;
	default:
		return 0;
	}

	hw20k2->mic_source = position;

	return 1;
}

static irqreturn_t ct_20k2_interrupt(int irq, void *dev_id)
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
	int err = 0;
	struct pci_dev *pci = hw->pci;
	unsigned int gctl;

	err = pci_enable_device(pci);
	if (err < 0)
		return err;

	/* Set DMA transfer mask */
	if (pci_set_dma_mask(pci, CT_XFI_DMA_MASK) < 0 ||
	    pci_set_consistent_dma_mask(pci, CT_XFI_DMA_MASK) < 0) {
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

		hw->io_base = pci_resource_start(hw->pci, 2);
		hw->mem_base = ioremap(hw->io_base,
				       pci_resource_len(hw->pci, 2));
		if (!hw->mem_base) {
			err = -ENOENT;
			goto error2;
		}
	}

	/* Switch to 20k2 mode from UAA mode. */
	gctl = hw_read_20kx(hw, GLOBAL_CNTL_GCTL);
	set_field(&gctl, GCTL_UAA, 0);
	hw_write_20kx(hw, GLOBAL_CNTL_GCTL, gctl);

	if (hw->irq < 0) {
		err = request_irq(pci->irq, ct_20k2_interrupt, IRQF_SHARED,
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

/*error3:
	iounmap((void *)hw->mem_base);
	hw->mem_base = (unsigned long)NULL;*/
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
	hw_write_20kx(hw, TRANSPORT_CTL, 0x00);

	/* disable pll */
	data = hw_read_20kx(hw, PLL_ENB);
	hw_write_20kx(hw, PLL_ENB, (data & (~0x07)));

	/* TODO: Disable interrupt and so on... */
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
	u32 data = 0;
	struct dac_conf dac_info = {0};
	struct adc_conf adc_info = {0};
	struct daio_conf daio_info = {0};
	struct trn_conf trn_info = {0};

	/* Get PCI io port/memory base address and
	 * do 20kx core switch if needed. */
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

	gctl = hw_read_20kx(hw, GLOBAL_CNTL_GCTL);
	set_field(&gctl, GCTL_DBP, 1);
	set_field(&gctl, GCTL_TBP, 1);
	set_field(&gctl, GCTL_FBP, 1);
	set_field(&gctl, GCTL_DPC, 0);
	hw_write_20kx(hw, GLOBAL_CNTL_GCTL, gctl);

	/* Reset all global pending interrupts */
	hw_write_20kx(hw, GIE, 0);
	/* Reset all SRC pending interrupts */
	hw_write_20kx(hw, SRC_IP, 0);

	if (hw->model != CTSB1270) {
		/* TODO: detect the card ID and configure GPIO accordingly. */
		/* Configures GPIO (0xD802 0x98028) */
		/*hw_write_20kx(hw, GPIO_CTRL, 0x7F07);*/
		/* Configures GPIO (SB0880) */
		/*hw_write_20kx(hw, GPIO_CTRL, 0xFF07);*/
		hw_write_20kx(hw, GPIO_CTRL, 0xD802);
	} else {
		hw_write_20kx(hw, GPIO_CTRL, 0x9E5F);
	}
	/* Enable audio ring */
	hw_write_20kx(hw, MIXER_AR_ENABLE, 0x01);

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

	data = hw_read_20kx(hw, SRC_MCTL);
	data |= 0x1; /* Enables input from the audio ring */
	hw_write_20kx(hw, SRC_MCTL, data);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int hw_suspend(struct hw *hw)
{
	struct pci_dev *pci = hw->pci;

	hw_card_stop(hw);

	pci_disable_device(pci);
	pci_save_state(pci);
	pci_set_power_state(pci, PCI_D3hot);

	return 0;
}

static int hw_resume(struct hw *hw, struct card_conf *info)
{
	struct pci_dev *pci = hw->pci;

	pci_set_power_state(pci, PCI_D0);
	pci_restore_state(pci);

	/* Re-initialize card hardware. */
	return hw_card_init(hw, info);
}
#endif

static u32 hw_read_20kx(struct hw *hw, u32 reg)
{
	return readl(hw->mem_base + reg);
}

static void hw_write_20kx(struct hw *hw, u32 reg, u32 data)
{
	writel(data, hw->mem_base + reg);
}

static struct hw ct20k2_preset = {
	.irq = -1,

	.card_init = hw_card_init,
	.card_stop = hw_card_stop,
	.pll_init = hw_pll_init,
	.is_adc_source_selected = hw_is_adc_input_selected,
	.select_adc_source = hw_adc_input_select,
	.capabilities = hw_capabilities,
	.output_switch_get = hw_output_switch_get,
	.output_switch_put = hw_output_switch_put,
	.mic_source_switch_get = hw_mic_source_switch_get,
	.mic_source_switch_put = hw_mic_source_switch_put,
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
	.dai_srt_set_srco = dai_srt_set_srco,
	.dai_srt_set_srcm = dai_srt_set_srcm,
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

int create_20k2_hw_obj(struct hw **rhw)
{
	struct hw20k2 *hw20k2;

	*rhw = NULL;
	hw20k2 = kzalloc(sizeof(*hw20k2), GFP_KERNEL);
	if (!hw20k2)
		return -ENOMEM;

	hw20k2->hw = ct20k2_preset;
	*rhw = &hw20k2->hw;

	return 0;
}

int destroy_20k2_hw_obj(struct hw *hw)
{
	if (hw->io_base)
		hw_card_shutdown(hw);

	kfree(hw);
	return 0;
}
