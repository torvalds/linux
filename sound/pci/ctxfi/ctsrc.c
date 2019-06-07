/**
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * This source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution for the license terms and conditions.
 *
 * @File	ctsrc.c
 *
 * @Brief
 * This file contains the implementation of the Sample Rate Convertor
 * resource management object.
 *
 * @Author	Liu Chun
 * @Date 	May 13 2008
 *
 */

#include "ctsrc.h"
#include "cthardware.h"
#include <linux/slab.h>

#define SRC_RESOURCE_NUM	256
#define SRCIMP_RESOURCE_NUM	256

static unsigned int conj_mask;

static int src_default_config_memrd(struct src *src);
static int src_default_config_memwr(struct src *src);
static int src_default_config_arcrw(struct src *src);

static int (*src_default_config[3])(struct src *) = {
	[MEMRD] = src_default_config_memrd,
	[MEMWR] = src_default_config_memwr,
	[ARCRW] = src_default_config_arcrw
};

static int src_set_state(struct src *src, unsigned int state)
{
	struct hw *hw;

	hw = src->rsc.hw;
	hw->src_set_state(src->rsc.ctrl_blk, state);

	return 0;
}

static int src_set_bm(struct src *src, unsigned int bm)
{
	struct hw *hw;

	hw = src->rsc.hw;
	hw->src_set_bm(src->rsc.ctrl_blk, bm);

	return 0;
}

static int src_set_sf(struct src *src, unsigned int sf)
{
	struct hw *hw;

	hw = src->rsc.hw;
	hw->src_set_sf(src->rsc.ctrl_blk, sf);

	return 0;
}

static int src_set_pm(struct src *src, unsigned int pm)
{
	struct hw *hw;

	hw = src->rsc.hw;
	hw->src_set_pm(src->rsc.ctrl_blk, pm);

	return 0;
}

static int src_set_rom(struct src *src, unsigned int rom)
{
	struct hw *hw;

	hw = src->rsc.hw;
	hw->src_set_rom(src->rsc.ctrl_blk, rom);

	return 0;
}

static int src_set_vo(struct src *src, unsigned int vo)
{
	struct hw *hw;

	hw = src->rsc.hw;
	hw->src_set_vo(src->rsc.ctrl_blk, vo);

	return 0;
}

static int src_set_st(struct src *src, unsigned int st)
{
	struct hw *hw;

	hw = src->rsc.hw;
	hw->src_set_st(src->rsc.ctrl_blk, st);

	return 0;
}

static int src_set_bp(struct src *src, unsigned int bp)
{
	struct hw *hw;

	hw = src->rsc.hw;
	hw->src_set_bp(src->rsc.ctrl_blk, bp);

	return 0;
}

static int src_set_cisz(struct src *src, unsigned int cisz)
{
	struct hw *hw;

	hw = src->rsc.hw;
	hw->src_set_cisz(src->rsc.ctrl_blk, cisz);

	return 0;
}

static int src_set_ca(struct src *src, unsigned int ca)
{
	struct hw *hw;

	hw = src->rsc.hw;
	hw->src_set_ca(src->rsc.ctrl_blk, ca);

	return 0;
}

static int src_set_sa(struct src *src, unsigned int sa)
{
	struct hw *hw;

	hw = src->rsc.hw;
	hw->src_set_sa(src->rsc.ctrl_blk, sa);

	return 0;
}

static int src_set_la(struct src *src, unsigned int la)
{
	struct hw *hw;

	hw = src->rsc.hw;
	hw->src_set_la(src->rsc.ctrl_blk, la);

	return 0;
}

static int src_set_pitch(struct src *src, unsigned int pitch)
{
	struct hw *hw;

	hw = src->rsc.hw;
	hw->src_set_pitch(src->rsc.ctrl_blk, pitch);

	return 0;
}

static int src_set_clear_zbufs(struct src *src)
{
	struct hw *hw;

	hw = src->rsc.hw;
	hw->src_set_clear_zbufs(src->rsc.ctrl_blk, 1);

	return 0;
}

static int src_commit_write(struct src *src)
{
	struct hw *hw;
	int i;
	unsigned int dirty = 0;

	hw = src->rsc.hw;
	src->rsc.ops->master(&src->rsc);
	if (src->rsc.msr > 1) {
		/* Save dirty flags for conjugate resource programming */
		dirty = hw->src_get_dirty(src->rsc.ctrl_blk) & conj_mask;
	}
	hw->src_commit_write(hw, src->rsc.ops->index(&src->rsc),
						src->rsc.ctrl_blk);

	/* Program conjugate parameter mixer resources */
	if (MEMWR == src->mode)
		return 0;

	for (i = 1; i < src->rsc.msr; i++) {
		src->rsc.ops->next_conj(&src->rsc);
		hw->src_set_dirty(src->rsc.ctrl_blk, dirty);
		hw->src_commit_write(hw, src->rsc.ops->index(&src->rsc),
							src->rsc.ctrl_blk);
	}
	src->rsc.ops->master(&src->rsc);

	return 0;
}

static int src_get_ca(struct src *src)
{
	struct hw *hw;

	hw = src->rsc.hw;
	return hw->src_get_ca(hw, src->rsc.ops->index(&src->rsc),
						src->rsc.ctrl_blk);
}

static int src_init(struct src *src)
{
	src_default_config[src->mode](src);

	return 0;
}

static struct src *src_next_interleave(struct src *src)
{
	return src->intlv;
}

static int src_default_config_memrd(struct src *src)
{
	struct hw *hw = src->rsc.hw;
	unsigned int rsr, msr;

	hw->src_set_state(src->rsc.ctrl_blk, SRC_STATE_OFF);
	hw->src_set_bm(src->rsc.ctrl_blk, 1);
	for (rsr = 0, msr = src->rsc.msr; msr > 1; msr >>= 1)
		rsr++;

	hw->src_set_rsr(src->rsc.ctrl_blk, rsr);
	hw->src_set_sf(src->rsc.ctrl_blk, SRC_SF_S16);
	hw->src_set_wr(src->rsc.ctrl_blk, 0);
	hw->src_set_pm(src->rsc.ctrl_blk, 0);
	hw->src_set_rom(src->rsc.ctrl_blk, 0);
	hw->src_set_vo(src->rsc.ctrl_blk, 0);
	hw->src_set_st(src->rsc.ctrl_blk, 0);
	hw->src_set_ilsz(src->rsc.ctrl_blk, src->multi - 1);
	hw->src_set_cisz(src->rsc.ctrl_blk, 0x80);
	hw->src_set_sa(src->rsc.ctrl_blk, 0x0);
	hw->src_set_la(src->rsc.ctrl_blk, 0x1000);
	hw->src_set_ca(src->rsc.ctrl_blk, 0x80);
	hw->src_set_pitch(src->rsc.ctrl_blk, 0x1000000);
	hw->src_set_clear_zbufs(src->rsc.ctrl_blk, 1);

	src->rsc.ops->master(&src->rsc);
	hw->src_commit_write(hw, src->rsc.ops->index(&src->rsc),
						src->rsc.ctrl_blk);

	for (msr = 1; msr < src->rsc.msr; msr++) {
		src->rsc.ops->next_conj(&src->rsc);
		hw->src_set_pitch(src->rsc.ctrl_blk, 0x1000000);
		hw->src_commit_write(hw, src->rsc.ops->index(&src->rsc),
							src->rsc.ctrl_blk);
	}
	src->rsc.ops->master(&src->rsc);

	return 0;
}

static int src_default_config_memwr(struct src *src)
{
	struct hw *hw = src->rsc.hw;

	hw->src_set_state(src->rsc.ctrl_blk, SRC_STATE_OFF);
	hw->src_set_bm(src->rsc.ctrl_blk, 1);
	hw->src_set_rsr(src->rsc.ctrl_blk, 0);
	hw->src_set_sf(src->rsc.ctrl_blk, SRC_SF_S16);
	hw->src_set_wr(src->rsc.ctrl_blk, 1);
	hw->src_set_pm(src->rsc.ctrl_blk, 0);
	hw->src_set_rom(src->rsc.ctrl_blk, 0);
	hw->src_set_vo(src->rsc.ctrl_blk, 0);
	hw->src_set_st(src->rsc.ctrl_blk, 0);
	hw->src_set_ilsz(src->rsc.ctrl_blk, 0);
	hw->src_set_cisz(src->rsc.ctrl_blk, 0x80);
	hw->src_set_sa(src->rsc.ctrl_blk, 0x0);
	hw->src_set_la(src->rsc.ctrl_blk, 0x1000);
	hw->src_set_ca(src->rsc.ctrl_blk, 0x80);
	hw->src_set_pitch(src->rsc.ctrl_blk, 0x1000000);
	hw->src_set_clear_zbufs(src->rsc.ctrl_blk, 1);

	src->rsc.ops->master(&src->rsc);
	hw->src_commit_write(hw, src->rsc.ops->index(&src->rsc),
						src->rsc.ctrl_blk);

	return 0;
}

static int src_default_config_arcrw(struct src *src)
{
	struct hw *hw = src->rsc.hw;
	unsigned int rsr, msr;
	unsigned int dirty;

	hw->src_set_state(src->rsc.ctrl_blk, SRC_STATE_OFF);
	hw->src_set_bm(src->rsc.ctrl_blk, 0);
	for (rsr = 0, msr = src->rsc.msr; msr > 1; msr >>= 1)
		rsr++;

	hw->src_set_rsr(src->rsc.ctrl_blk, rsr);
	hw->src_set_sf(src->rsc.ctrl_blk, SRC_SF_F32);
	hw->src_set_wr(src->rsc.ctrl_blk, 0);
	hw->src_set_pm(src->rsc.ctrl_blk, 0);
	hw->src_set_rom(src->rsc.ctrl_blk, 0);
	hw->src_set_vo(src->rsc.ctrl_blk, 0);
	hw->src_set_st(src->rsc.ctrl_blk, 0);
	hw->src_set_ilsz(src->rsc.ctrl_blk, 0);
	hw->src_set_cisz(src->rsc.ctrl_blk, 0x80);
	hw->src_set_sa(src->rsc.ctrl_blk, 0x0);
	/*hw->src_set_sa(src->rsc.ctrl_blk, 0x100);*/
	hw->src_set_la(src->rsc.ctrl_blk, 0x1000);
	/*hw->src_set_la(src->rsc.ctrl_blk, 0x03ffffe0);*/
	hw->src_set_ca(src->rsc.ctrl_blk, 0x80);
	hw->src_set_pitch(src->rsc.ctrl_blk, 0x1000000);
	hw->src_set_clear_zbufs(src->rsc.ctrl_blk, 1);

	dirty = hw->src_get_dirty(src->rsc.ctrl_blk);
	src->rsc.ops->master(&src->rsc);
	for (msr = 0; msr < src->rsc.msr; msr++) {
		hw->src_set_dirty(src->rsc.ctrl_blk, dirty);
		hw->src_commit_write(hw, src->rsc.ops->index(&src->rsc),
							src->rsc.ctrl_blk);
		src->rsc.ops->next_conj(&src->rsc);
	}
	src->rsc.ops->master(&src->rsc);

	return 0;
}

static const struct src_rsc_ops src_rsc_ops = {
	.set_state		= src_set_state,
	.set_bm			= src_set_bm,
	.set_sf			= src_set_sf,
	.set_pm			= src_set_pm,
	.set_rom		= src_set_rom,
	.set_vo			= src_set_vo,
	.set_st			= src_set_st,
	.set_bp			= src_set_bp,
	.set_cisz		= src_set_cisz,
	.set_ca			= src_set_ca,
	.set_sa			= src_set_sa,
	.set_la			= src_set_la,
	.set_pitch		= src_set_pitch,
	.set_clr_zbufs		= src_set_clear_zbufs,
	.commit_write		= src_commit_write,
	.get_ca			= src_get_ca,
	.init			= src_init,
	.next_interleave	= src_next_interleave,
};

static int
src_rsc_init(struct src *src, u32 idx,
	     const struct src_desc *desc, struct src_mgr *mgr)
{
	int err;
	int i, n;
	struct src *p;

	n = (MEMRD == desc->mode) ? desc->multi : 1;
	for (i = 0, p = src; i < n; i++, p++) {
		err = rsc_init(&p->rsc, idx + i, SRC, desc->msr, mgr->mgr.hw);
		if (err)
			goto error1;

		/* Initialize src specific rsc operations */
		p->ops = &src_rsc_ops;
		p->multi = (0 == i) ? desc->multi : 1;
		p->mode = desc->mode;
		src_default_config[desc->mode](p);
		mgr->src_enable(mgr, p);
		p->intlv = p + 1;
	}
	(--p)->intlv = NULL;	/* Set @intlv of the last SRC to NULL */

	mgr->commit_write(mgr);

	return 0;

error1:
	for (i--, p--; i >= 0; i--, p--) {
		mgr->src_disable(mgr, p);
		rsc_uninit(&p->rsc);
	}
	mgr->commit_write(mgr);
	return err;
}

static int src_rsc_uninit(struct src *src, struct src_mgr *mgr)
{
	int i, n;
	struct src *p;

	n = (MEMRD == src->mode) ? src->multi : 1;
	for (i = 0, p = src; i < n; i++, p++) {
		mgr->src_disable(mgr, p);
		rsc_uninit(&p->rsc);
		p->multi = 0;
		p->ops = NULL;
		p->mode = NUM_SRCMODES;
		p->intlv = NULL;
	}
	mgr->commit_write(mgr);

	return 0;
}

static int
get_src_rsc(struct src_mgr *mgr, const struct src_desc *desc, struct src **rsrc)
{
	unsigned int idx = SRC_RESOURCE_NUM;
	int err;
	struct src *src;
	unsigned long flags;

	*rsrc = NULL;

	/* Check whether there are sufficient src resources to meet request. */
	spin_lock_irqsave(&mgr->mgr_lock, flags);
	if (MEMRD == desc->mode)
		err = mgr_get_resource(&mgr->mgr, desc->multi, &idx);
	else
		err = mgr_get_resource(&mgr->mgr, 1, &idx);

	spin_unlock_irqrestore(&mgr->mgr_lock, flags);
	if (err) {
		dev_err(mgr->card->dev,
			"Can't meet SRC resource request!\n");
		return err;
	}

	/* Allocate mem for master src resource */
	if (MEMRD == desc->mode)
		src = kcalloc(desc->multi, sizeof(*src), GFP_KERNEL);
	else
		src = kzalloc(sizeof(*src), GFP_KERNEL);

	if (!src) {
		err = -ENOMEM;
		goto error1;
	}

	err = src_rsc_init(src, idx, desc, mgr);
	if (err)
		goto error2;

	*rsrc = src;

	return 0;

error2:
	kfree(src);
error1:
	spin_lock_irqsave(&mgr->mgr_lock, flags);
	if (MEMRD == desc->mode)
		mgr_put_resource(&mgr->mgr, desc->multi, idx);
	else
		mgr_put_resource(&mgr->mgr, 1, idx);

	spin_unlock_irqrestore(&mgr->mgr_lock, flags);
	return err;
}

static int put_src_rsc(struct src_mgr *mgr, struct src *src)
{
	unsigned long flags;

	spin_lock_irqsave(&mgr->mgr_lock, flags);
	src->rsc.ops->master(&src->rsc);
	if (MEMRD == src->mode)
		mgr_put_resource(&mgr->mgr, src->multi,
				 src->rsc.ops->index(&src->rsc));
	else
		mgr_put_resource(&mgr->mgr, 1, src->rsc.ops->index(&src->rsc));

	spin_unlock_irqrestore(&mgr->mgr_lock, flags);
	src_rsc_uninit(src, mgr);
	kfree(src);

	return 0;
}

static int src_enable_s(struct src_mgr *mgr, struct src *src)
{
	struct hw *hw = mgr->mgr.hw;
	int i;

	src->rsc.ops->master(&src->rsc);
	for (i = 0; i < src->rsc.msr; i++) {
		hw->src_mgr_enbs_src(mgr->mgr.ctrl_blk,
				     src->rsc.ops->index(&src->rsc));
		src->rsc.ops->next_conj(&src->rsc);
	}
	src->rsc.ops->master(&src->rsc);

	return 0;
}

static int src_enable(struct src_mgr *mgr, struct src *src)
{
	struct hw *hw = mgr->mgr.hw;
	int i;

	src->rsc.ops->master(&src->rsc);
	for (i = 0; i < src->rsc.msr; i++) {
		hw->src_mgr_enb_src(mgr->mgr.ctrl_blk,
				    src->rsc.ops->index(&src->rsc));
		src->rsc.ops->next_conj(&src->rsc);
	}
	src->rsc.ops->master(&src->rsc);

	return 0;
}

static int src_disable(struct src_mgr *mgr, struct src *src)
{
	struct hw *hw = mgr->mgr.hw;
	int i;

	src->rsc.ops->master(&src->rsc);
	for (i = 0; i < src->rsc.msr; i++) {
		hw->src_mgr_dsb_src(mgr->mgr.ctrl_blk,
				    src->rsc.ops->index(&src->rsc));
		src->rsc.ops->next_conj(&src->rsc);
	}
	src->rsc.ops->master(&src->rsc);

	return 0;
}

static int src_mgr_commit_write(struct src_mgr *mgr)
{
	struct hw *hw = mgr->mgr.hw;

	hw->src_mgr_commit_write(hw, mgr->mgr.ctrl_blk);

	return 0;
}

int src_mgr_create(struct hw *hw, struct src_mgr **rsrc_mgr)
{
	int err, i;
	struct src_mgr *src_mgr;

	*rsrc_mgr = NULL;
	src_mgr = kzalloc(sizeof(*src_mgr), GFP_KERNEL);
	if (!src_mgr)
		return -ENOMEM;

	err = rsc_mgr_init(&src_mgr->mgr, SRC, SRC_RESOURCE_NUM, hw);
	if (err)
		goto error1;

	spin_lock_init(&src_mgr->mgr_lock);
	conj_mask = hw->src_dirty_conj_mask();

	src_mgr->get_src = get_src_rsc;
	src_mgr->put_src = put_src_rsc;
	src_mgr->src_enable_s = src_enable_s;
	src_mgr->src_enable = src_enable;
	src_mgr->src_disable = src_disable;
	src_mgr->commit_write = src_mgr_commit_write;
	src_mgr->card = hw->card;

	/* Disable all SRC resources. */
	for (i = 0; i < 256; i++)
		hw->src_mgr_dsb_src(src_mgr->mgr.ctrl_blk, i);

	hw->src_mgr_commit_write(hw, src_mgr->mgr.ctrl_blk);

	*rsrc_mgr = src_mgr;

	return 0;

error1:
	kfree(src_mgr);
	return err;
}

int src_mgr_destroy(struct src_mgr *src_mgr)
{
	rsc_mgr_uninit(&src_mgr->mgr);
	kfree(src_mgr);

	return 0;
}

/* SRCIMP resource manager operations */

static int srcimp_master(struct rsc *rsc)
{
	rsc->conj = 0;
	return rsc->idx = container_of(rsc, struct srcimp, rsc)->idx[0];
}

static int srcimp_next_conj(struct rsc *rsc)
{
	rsc->conj++;
	return container_of(rsc, struct srcimp, rsc)->idx[rsc->conj];
}

static int srcimp_index(const struct rsc *rsc)
{
	return container_of(rsc, struct srcimp, rsc)->idx[rsc->conj];
}

static const struct rsc_ops srcimp_basic_rsc_ops = {
	.master		= srcimp_master,
	.next_conj	= srcimp_next_conj,
	.index		= srcimp_index,
	.output_slot	= NULL,
};

static int srcimp_map(struct srcimp *srcimp, struct src *src, struct rsc *input)
{
	struct imapper *entry;
	int i;

	srcimp->rsc.ops->master(&srcimp->rsc);
	src->rsc.ops->master(&src->rsc);
	input->ops->master(input);

	/* Program master and conjugate resources */
	for (i = 0; i < srcimp->rsc.msr; i++) {
		entry = &srcimp->imappers[i];
		entry->slot = input->ops->output_slot(input);
		entry->user = src->rsc.ops->index(&src->rsc);
		entry->addr = srcimp->rsc.ops->index(&srcimp->rsc);
		srcimp->mgr->imap_add(srcimp->mgr, entry);
		srcimp->mapped |= (0x1 << i);

		srcimp->rsc.ops->next_conj(&srcimp->rsc);
		input->ops->next_conj(input);
	}

	srcimp->rsc.ops->master(&srcimp->rsc);
	input->ops->master(input);

	return 0;
}

static int srcimp_unmap(struct srcimp *srcimp)
{
	int i;

	/* Program master and conjugate resources */
	for (i = 0; i < srcimp->rsc.msr; i++) {
		if (srcimp->mapped & (0x1 << i)) {
			srcimp->mgr->imap_delete(srcimp->mgr,
						 &srcimp->imappers[i]);
			srcimp->mapped &= ~(0x1 << i);
		}
	}

	return 0;
}

static const struct srcimp_rsc_ops srcimp_ops = {
	.map = srcimp_map,
	.unmap = srcimp_unmap
};

static int srcimp_rsc_init(struct srcimp *srcimp,
			   const struct srcimp_desc *desc,
			   struct srcimp_mgr *mgr)
{
	int err;

	err = rsc_init(&srcimp->rsc, srcimp->idx[0],
		       SRCIMP, desc->msr, mgr->mgr.hw);
	if (err)
		return err;

	/* Reserve memory for imapper nodes */
	srcimp->imappers = kcalloc(desc->msr, sizeof(struct imapper),
				   GFP_KERNEL);
	if (!srcimp->imappers) {
		err = -ENOMEM;
		goto error1;
	}

	/* Set srcimp specific operations */
	srcimp->rsc.ops = &srcimp_basic_rsc_ops;
	srcimp->ops = &srcimp_ops;
	srcimp->mgr = mgr;

	srcimp->rsc.ops->master(&srcimp->rsc);

	return 0;

error1:
	rsc_uninit(&srcimp->rsc);
	return err;
}

static int srcimp_rsc_uninit(struct srcimp *srcimp)
{
	kfree(srcimp->imappers);
	srcimp->imappers = NULL;
	srcimp->ops = NULL;
	srcimp->mgr = NULL;
	rsc_uninit(&srcimp->rsc);

	return 0;
}

static int get_srcimp_rsc(struct srcimp_mgr *mgr,
			  const struct srcimp_desc *desc,
			  struct srcimp **rsrcimp)
{
	int err, i;
	unsigned int idx;
	struct srcimp *srcimp;
	unsigned long flags;

	*rsrcimp = NULL;

	/* Allocate mem for SRCIMP resource */
	srcimp = kzalloc(sizeof(*srcimp), GFP_KERNEL);
	if (!srcimp)
		return -ENOMEM;

	/* Check whether there are sufficient SRCIMP resources. */
	err = 0;
	spin_lock_irqsave(&mgr->mgr_lock, flags);
	for (i = 0; i < desc->msr; i++) {
		err = mgr_get_resource(&mgr->mgr, 1, &idx);
		if (err)
			break;

		srcimp->idx[i] = idx;
	}
	spin_unlock_irqrestore(&mgr->mgr_lock, flags);
	if (err) {
		dev_err(mgr->card->dev,
			"Can't meet SRCIMP resource request!\n");
		goto error1;
	}

	err = srcimp_rsc_init(srcimp, desc, mgr);
	if (err)
		goto error1;

	*rsrcimp = srcimp;

	return 0;

error1:
	spin_lock_irqsave(&mgr->mgr_lock, flags);
	for (i--; i >= 0; i--)
		mgr_put_resource(&mgr->mgr, 1, srcimp->idx[i]);

	spin_unlock_irqrestore(&mgr->mgr_lock, flags);
	kfree(srcimp);
	return err;
}

static int put_srcimp_rsc(struct srcimp_mgr *mgr, struct srcimp *srcimp)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&mgr->mgr_lock, flags);
	for (i = 0; i < srcimp->rsc.msr; i++)
		mgr_put_resource(&mgr->mgr, 1, srcimp->idx[i]);

	spin_unlock_irqrestore(&mgr->mgr_lock, flags);
	srcimp_rsc_uninit(srcimp);
	kfree(srcimp);

	return 0;
}

static int srcimp_map_op(void *data, struct imapper *entry)
{
	struct rsc_mgr *mgr = &((struct srcimp_mgr *)data)->mgr;
	struct hw *hw = mgr->hw;

	hw->srcimp_mgr_set_imaparc(mgr->ctrl_blk, entry->slot);
	hw->srcimp_mgr_set_imapuser(mgr->ctrl_blk, entry->user);
	hw->srcimp_mgr_set_imapnxt(mgr->ctrl_blk, entry->next);
	hw->srcimp_mgr_set_imapaddr(mgr->ctrl_blk, entry->addr);
	hw->srcimp_mgr_commit_write(mgr->hw, mgr->ctrl_blk);

	return 0;
}

static int srcimp_imap_add(struct srcimp_mgr *mgr, struct imapper *entry)
{
	unsigned long flags;
	int err;

	spin_lock_irqsave(&mgr->imap_lock, flags);
	if ((0 == entry->addr) && (mgr->init_imap_added)) {
		input_mapper_delete(&mgr->imappers,
				    mgr->init_imap, srcimp_map_op, mgr);
		mgr->init_imap_added = 0;
	}
	err = input_mapper_add(&mgr->imappers, entry, srcimp_map_op, mgr);
	spin_unlock_irqrestore(&mgr->imap_lock, flags);

	return err;
}

static int srcimp_imap_delete(struct srcimp_mgr *mgr, struct imapper *entry)
{
	unsigned long flags;
	int err;

	spin_lock_irqsave(&mgr->imap_lock, flags);
	err = input_mapper_delete(&mgr->imappers, entry, srcimp_map_op, mgr);
	if (list_empty(&mgr->imappers)) {
		input_mapper_add(&mgr->imappers, mgr->init_imap,
				 srcimp_map_op, mgr);
		mgr->init_imap_added = 1;
	}
	spin_unlock_irqrestore(&mgr->imap_lock, flags);

	return err;
}

int srcimp_mgr_create(struct hw *hw, struct srcimp_mgr **rsrcimp_mgr)
{
	int err;
	struct srcimp_mgr *srcimp_mgr;
	struct imapper *entry;

	*rsrcimp_mgr = NULL;
	srcimp_mgr = kzalloc(sizeof(*srcimp_mgr), GFP_KERNEL);
	if (!srcimp_mgr)
		return -ENOMEM;

	err = rsc_mgr_init(&srcimp_mgr->mgr, SRCIMP, SRCIMP_RESOURCE_NUM, hw);
	if (err)
		goto error1;

	spin_lock_init(&srcimp_mgr->mgr_lock);
	spin_lock_init(&srcimp_mgr->imap_lock);
	INIT_LIST_HEAD(&srcimp_mgr->imappers);
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		err = -ENOMEM;
		goto error2;
	}
	entry->slot = entry->addr = entry->next = entry->user = 0;
	list_add(&entry->list, &srcimp_mgr->imappers);
	srcimp_mgr->init_imap = entry;
	srcimp_mgr->init_imap_added = 1;

	srcimp_mgr->get_srcimp = get_srcimp_rsc;
	srcimp_mgr->put_srcimp = put_srcimp_rsc;
	srcimp_mgr->imap_add = srcimp_imap_add;
	srcimp_mgr->imap_delete = srcimp_imap_delete;
	srcimp_mgr->card = hw->card;

	*rsrcimp_mgr = srcimp_mgr;

	return 0;

error2:
	rsc_mgr_uninit(&srcimp_mgr->mgr);
error1:
	kfree(srcimp_mgr);
	return err;
}

int srcimp_mgr_destroy(struct srcimp_mgr *srcimp_mgr)
{
	unsigned long flags;

	/* free src input mapper list */
	spin_lock_irqsave(&srcimp_mgr->imap_lock, flags);
	free_input_mapper_list(&srcimp_mgr->imappers);
	spin_unlock_irqrestore(&srcimp_mgr->imap_lock, flags);

	rsc_mgr_uninit(&srcimp_mgr->mgr);
	kfree(srcimp_mgr);

	return 0;
}
