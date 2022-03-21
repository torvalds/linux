// SPDX-License-Identifier: GPL-2.0-only
/**
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * @File	ctresource.c
 *
 * @Brief
 * This file contains the implementation of some generic helper functions.
 *
 * @Author	Liu Chun
 * @Date 	May 15 2008
 */

#include "ctresource.h"
#include "cthardware.h"
#include <linux/err.h>
#include <linux/slab.h>

#define AUDIO_SLOT_BLOCK_NUM 	256

/* Resource allocation based on bit-map management mechanism */
static int
get_resource(u8 *rscs, unsigned int amount,
	     unsigned int multi, unsigned int *ridx)
{
	int i, j, k, n;

	/* Check whether there are sufficient resources to meet request. */
	for (i = 0, n = multi; i < amount; i++) {
		j = i / 8;
		k = i % 8;
		if (rscs[j] & ((u8)1 << k)) {
			n = multi;
			continue;
		}
		if (!(--n))
			break; /* found sufficient contiguous resources */
	}

	if (i >= amount) {
		/* Can not find sufficient contiguous resources */
		return -ENOENT;
	}

	/* Mark the contiguous bits in resource bit-map as used */
	for (n = multi; n > 0; n--) {
		j = i / 8;
		k = i % 8;
		rscs[j] |= ((u8)1 << k);
		i--;
	}

	*ridx = i + 1;

	return 0;
}

static int put_resource(u8 *rscs, unsigned int multi, unsigned int idx)
{
	unsigned int i, j, k, n;

	/* Mark the contiguous bits in resource bit-map as used */
	for (n = multi, i = idx; n > 0; n--) {
		j = i / 8;
		k = i % 8;
		rscs[j] &= ~((u8)1 << k);
		i++;
	}

	return 0;
}

int mgr_get_resource(struct rsc_mgr *mgr, unsigned int n, unsigned int *ridx)
{
	int err;

	if (n > mgr->avail)
		return -ENOENT;

	err = get_resource(mgr->rscs, mgr->amount, n, ridx);
	if (!err)
		mgr->avail -= n;

	return err;
}

int mgr_put_resource(struct rsc_mgr *mgr, unsigned int n, unsigned int idx)
{
	put_resource(mgr->rscs, n, idx);
	mgr->avail += n;

	return 0;
}

static const unsigned char offset_in_audio_slot_block[NUM_RSCTYP] = {
	/* SRC channel is at Audio Ring slot 1 every 16 slots. */
	[SRC]		= 0x1,
	[AMIXER]	= 0x4,
	[SUM]		= 0xc,
};

static int rsc_index(const struct rsc *rsc)
{
    return rsc->conj;
}

static int audio_ring_slot(const struct rsc *rsc)
{
    return (rsc->conj << 4) + offset_in_audio_slot_block[rsc->type];
}

static void rsc_next_conj(struct rsc *rsc)
{
	unsigned int i;
	for (i = 0; (i < 8) && (!(rsc->msr & (0x1 << i))); )
		i++;
	rsc->conj += (AUDIO_SLOT_BLOCK_NUM >> i);
}

static void rsc_master(struct rsc *rsc)
{
	rsc->conj = rsc->idx;
}

static const struct rsc_ops rsc_generic_ops = {
	.index		= rsc_index,
	.output_slot	= audio_ring_slot,
	.master		= rsc_master,
	.next_conj	= rsc_next_conj,
};

int
rsc_init(struct rsc *rsc, u32 idx, enum RSCTYP type, u32 msr, struct hw *hw)
{
	int err = 0;

	rsc->idx = idx;
	rsc->conj = idx;
	rsc->type = type;
	rsc->msr = msr;
	rsc->hw = hw;
	rsc->ops = &rsc_generic_ops;
	if (!hw) {
		rsc->ctrl_blk = NULL;
		return 0;
	}

	switch (type) {
	case SRC:
		err = hw->src_rsc_get_ctrl_blk(&rsc->ctrl_blk);
		break;
	case AMIXER:
		err = hw->amixer_rsc_get_ctrl_blk(&rsc->ctrl_blk);
		break;
	case SRCIMP:
	case SUM:
	case DAIO:
		break;
	default:
		dev_err(((struct hw *)hw)->card->dev,
			"Invalid resource type value %d!\n", type);
		return -EINVAL;
	}

	if (err) {
		dev_err(((struct hw *)hw)->card->dev,
			"Failed to get resource control block!\n");
		return err;
	}

	return 0;
}

int rsc_uninit(struct rsc *rsc)
{
	if ((NULL != rsc->hw) && (NULL != rsc->ctrl_blk)) {
		switch (rsc->type) {
		case SRC:
			rsc->hw->src_rsc_put_ctrl_blk(rsc->ctrl_blk);
			break;
		case AMIXER:
			rsc->hw->amixer_rsc_put_ctrl_blk(rsc->ctrl_blk);
			break;
		case SUM:
		case DAIO:
			break;
		default:
			dev_err(((struct hw *)rsc->hw)->card->dev,
				"Invalid resource type value %d!\n",
				rsc->type);
			break;
		}

		rsc->hw = rsc->ctrl_blk = NULL;
	}

	rsc->idx = rsc->conj = 0;
	rsc->type = NUM_RSCTYP;
	rsc->msr = 0;

	return 0;
}

int rsc_mgr_init(struct rsc_mgr *mgr, enum RSCTYP type,
		 unsigned int amount, struct hw *hw)
{
	int err = 0;

	mgr->type = NUM_RSCTYP;

	mgr->rscs = kzalloc(((amount + 8 - 1) / 8), GFP_KERNEL);
	if (!mgr->rscs)
		return -ENOMEM;

	switch (type) {
	case SRC:
		err = hw->src_mgr_get_ctrl_blk(&mgr->ctrl_blk);
		break;
	case SRCIMP:
		err = hw->srcimp_mgr_get_ctrl_blk(&mgr->ctrl_blk);
		break;
	case AMIXER:
		err = hw->amixer_mgr_get_ctrl_blk(&mgr->ctrl_blk);
		break;
	case DAIO:
		err = hw->daio_mgr_get_ctrl_blk(hw, &mgr->ctrl_blk);
		break;
	case SUM:
		break;
	default:
		dev_err(hw->card->dev,
			"Invalid resource type value %d!\n", type);
		err = -EINVAL;
		goto error;
	}

	if (err) {
		dev_err(hw->card->dev,
			"Failed to get manager control block!\n");
		goto error;
	}

	mgr->type = type;
	mgr->avail = mgr->amount = amount;
	mgr->hw = hw;

	return 0;

error:
	kfree(mgr->rscs);
	return err;
}

int rsc_mgr_uninit(struct rsc_mgr *mgr)
{
	kfree(mgr->rscs);
	mgr->rscs = NULL;

	if ((NULL != mgr->hw) && (NULL != mgr->ctrl_blk)) {
		switch (mgr->type) {
		case SRC:
			mgr->hw->src_mgr_put_ctrl_blk(mgr->ctrl_blk);
			break;
		case SRCIMP:
			mgr->hw->srcimp_mgr_put_ctrl_blk(mgr->ctrl_blk);
			break;
		case AMIXER:
			mgr->hw->amixer_mgr_put_ctrl_blk(mgr->ctrl_blk);
			break;
		case DAIO:
			mgr->hw->daio_mgr_put_ctrl_blk(mgr->ctrl_blk);
			break;
		case SUM:
			break;
		default:
			dev_err(((struct hw *)mgr->hw)->card->dev,
				"Invalid resource type value %d!\n",
				mgr->type);
			break;
		}

		mgr->hw = mgr->ctrl_blk = NULL;
	}

	mgr->type = NUM_RSCTYP;
	mgr->avail = mgr->amount = 0;

	return 0;
}
