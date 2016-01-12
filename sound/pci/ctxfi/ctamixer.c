/**
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * This source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution for the license terms and conditions.
 *
 * @File	ctamixer.c
 *
 * @Brief
 * This file contains the implementation of the Audio Mixer
 * resource management object.
 *
 * @Author	Liu Chun
 * @Date 	May 21 2008
 *
 */

#include "ctamixer.h"
#include "cthardware.h"
#include <linux/slab.h>

#define AMIXER_RESOURCE_NUM	256
#define SUM_RESOURCE_NUM	256

#define AMIXER_Y_IMMEDIATE	1

#define BLANK_SLOT		4094

static int amixer_master(struct rsc *rsc)
{
	rsc->conj = 0;
	return rsc->idx = container_of(rsc, struct amixer, rsc)->idx[0];
}

static int amixer_next_conj(struct rsc *rsc)
{
	rsc->conj++;
	return container_of(rsc, struct amixer, rsc)->idx[rsc->conj];
}

static int amixer_index(const struct rsc *rsc)
{
	return container_of(rsc, struct amixer, rsc)->idx[rsc->conj];
}

static int amixer_output_slot(const struct rsc *rsc)
{
	return (amixer_index(rsc) << 4) + 0x4;
}

static const struct rsc_ops amixer_basic_rsc_ops = {
	.master		= amixer_master,
	.next_conj	= amixer_next_conj,
	.index		= amixer_index,
	.output_slot	= amixer_output_slot,
};

static int amixer_set_input(struct amixer *amixer, struct rsc *rsc)
{
	struct hw *hw;

	hw = amixer->rsc.hw;
	hw->amixer_set_mode(amixer->rsc.ctrl_blk, AMIXER_Y_IMMEDIATE);
	amixer->input = rsc;
	if (!rsc)
		hw->amixer_set_x(amixer->rsc.ctrl_blk, BLANK_SLOT);
	else
		hw->amixer_set_x(amixer->rsc.ctrl_blk,
					rsc->ops->output_slot(rsc));

	return 0;
}

/* y is a 14-bit immediate constant */
static int amixer_set_y(struct amixer *amixer, unsigned int y)
{
	struct hw *hw;

	hw = amixer->rsc.hw;
	hw->amixer_set_y(amixer->rsc.ctrl_blk, y);

	return 0;
}

static int amixer_set_invalid_squash(struct amixer *amixer, unsigned int iv)
{
	struct hw *hw;

	hw = amixer->rsc.hw;
	hw->amixer_set_iv(amixer->rsc.ctrl_blk, iv);

	return 0;
}

static int amixer_set_sum(struct amixer *amixer, struct sum *sum)
{
	struct hw *hw;

	hw = amixer->rsc.hw;
	amixer->sum = sum;
	if (!sum) {
		hw->amixer_set_se(amixer->rsc.ctrl_blk, 0);
	} else {
		hw->amixer_set_se(amixer->rsc.ctrl_blk, 1);
		hw->amixer_set_sadr(amixer->rsc.ctrl_blk,
					sum->rsc.ops->index(&sum->rsc));
	}

	return 0;
}

static int amixer_commit_write(struct amixer *amixer)
{
	struct hw *hw;
	unsigned int index;
	int i;
	struct rsc *input;
	struct sum *sum;

	hw = amixer->rsc.hw;
	input = amixer->input;
	sum = amixer->sum;

	/* Program master and conjugate resources */
	amixer->rsc.ops->master(&amixer->rsc);
	if (input)
		input->ops->master(input);

	if (sum)
		sum->rsc.ops->master(&sum->rsc);

	for (i = 0; i < amixer->rsc.msr; i++) {
		hw->amixer_set_dirty_all(amixer->rsc.ctrl_blk);
		if (input) {
			hw->amixer_set_x(amixer->rsc.ctrl_blk,
						input->ops->output_slot(input));
			input->ops->next_conj(input);
		}
		if (sum) {
			hw->amixer_set_sadr(amixer->rsc.ctrl_blk,
						sum->rsc.ops->index(&sum->rsc));
			sum->rsc.ops->next_conj(&sum->rsc);
		}
		index = amixer->rsc.ops->output_slot(&amixer->rsc);
		hw->amixer_commit_write(hw, index, amixer->rsc.ctrl_blk);
		amixer->rsc.ops->next_conj(&amixer->rsc);
	}
	amixer->rsc.ops->master(&amixer->rsc);
	if (input)
		input->ops->master(input);

	if (sum)
		sum->rsc.ops->master(&sum->rsc);

	return 0;
}

static int amixer_commit_raw_write(struct amixer *amixer)
{
	struct hw *hw;
	unsigned int index;

	hw = amixer->rsc.hw;
	index = amixer->rsc.ops->output_slot(&amixer->rsc);
	hw->amixer_commit_write(hw, index, amixer->rsc.ctrl_blk);

	return 0;
}

static int amixer_get_y(struct amixer *amixer)
{
	struct hw *hw;

	hw = amixer->rsc.hw;
	return hw->amixer_get_y(amixer->rsc.ctrl_blk);
}

static int amixer_setup(struct amixer *amixer, struct rsc *input,
			unsigned int scale, struct sum *sum)
{
	amixer_set_input(amixer, input);
	amixer_set_y(amixer, scale);
	amixer_set_sum(amixer, sum);
	amixer_commit_write(amixer);
	return 0;
}

static const struct amixer_rsc_ops amixer_ops = {
	.set_input		= amixer_set_input,
	.set_invalid_squash	= amixer_set_invalid_squash,
	.set_scale		= amixer_set_y,
	.set_sum		= amixer_set_sum,
	.commit_write		= amixer_commit_write,
	.commit_raw_write	= amixer_commit_raw_write,
	.setup			= amixer_setup,
	.get_scale		= amixer_get_y,
};

static int amixer_rsc_init(struct amixer *amixer,
			   const struct amixer_desc *desc,
			   struct amixer_mgr *mgr)
{
	int err;

	err = rsc_init(&amixer->rsc, amixer->idx[0],
			AMIXER, desc->msr, mgr->mgr.hw);
	if (err)
		return err;

	/* Set amixer specific operations */
	amixer->rsc.ops = &amixer_basic_rsc_ops;
	amixer->ops = &amixer_ops;
	amixer->input = NULL;
	amixer->sum = NULL;

	amixer_setup(amixer, NULL, 0, NULL);

	return 0;
}

static int amixer_rsc_uninit(struct amixer *amixer)
{
	amixer_setup(amixer, NULL, 0, NULL);
	rsc_uninit(&amixer->rsc);
	amixer->ops = NULL;
	amixer->input = NULL;
	amixer->sum = NULL;
	return 0;
}

static int get_amixer_rsc(struct amixer_mgr *mgr,
			  const struct amixer_desc *desc,
			  struct amixer **ramixer)
{
	int err, i;
	unsigned int idx;
	struct amixer *amixer;
	unsigned long flags;

	*ramixer = NULL;

	/* Allocate mem for amixer resource */
	amixer = kzalloc(sizeof(*amixer), GFP_KERNEL);
	if (!amixer)
		return -ENOMEM;

	/* Check whether there are sufficient
	 * amixer resources to meet request. */
	err = 0;
	spin_lock_irqsave(&mgr->mgr_lock, flags);
	for (i = 0; i < desc->msr; i++) {
		err = mgr_get_resource(&mgr->mgr, 1, &idx);
		if (err)
			break;

		amixer->idx[i] = idx;
	}
	spin_unlock_irqrestore(&mgr->mgr_lock, flags);
	if (err) {
		dev_err(mgr->card->dev,
			"Can't meet AMIXER resource request!\n");
		goto error;
	}

	err = amixer_rsc_init(amixer, desc, mgr);
	if (err)
		goto error;

	*ramixer = amixer;

	return 0;

error:
	spin_lock_irqsave(&mgr->mgr_lock, flags);
	for (i--; i >= 0; i--)
		mgr_put_resource(&mgr->mgr, 1, amixer->idx[i]);

	spin_unlock_irqrestore(&mgr->mgr_lock, flags);
	kfree(amixer);
	return err;
}

static int put_amixer_rsc(struct amixer_mgr *mgr, struct amixer *amixer)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&mgr->mgr_lock, flags);
	for (i = 0; i < amixer->rsc.msr; i++)
		mgr_put_resource(&mgr->mgr, 1, amixer->idx[i]);

	spin_unlock_irqrestore(&mgr->mgr_lock, flags);
	amixer_rsc_uninit(amixer);
	kfree(amixer);

	return 0;
}

int amixer_mgr_create(struct hw *hw, struct amixer_mgr **ramixer_mgr)
{
	int err;
	struct amixer_mgr *amixer_mgr;

	*ramixer_mgr = NULL;
	amixer_mgr = kzalloc(sizeof(*amixer_mgr), GFP_KERNEL);
	if (!amixer_mgr)
		return -ENOMEM;

	err = rsc_mgr_init(&amixer_mgr->mgr, AMIXER, AMIXER_RESOURCE_NUM, hw);
	if (err)
		goto error;

	spin_lock_init(&amixer_mgr->mgr_lock);

	amixer_mgr->get_amixer = get_amixer_rsc;
	amixer_mgr->put_amixer = put_amixer_rsc;
	amixer_mgr->card = hw->card;

	*ramixer_mgr = amixer_mgr;

	return 0;

error:
	kfree(amixer_mgr);
	return err;
}

int amixer_mgr_destroy(struct amixer_mgr *amixer_mgr)
{
	rsc_mgr_uninit(&amixer_mgr->mgr);
	kfree(amixer_mgr);
	return 0;
}

/* SUM resource management */

static int sum_master(struct rsc *rsc)
{
	rsc->conj = 0;
	return rsc->idx = container_of(rsc, struct sum, rsc)->idx[0];
}

static int sum_next_conj(struct rsc *rsc)
{
	rsc->conj++;
	return container_of(rsc, struct sum, rsc)->idx[rsc->conj];
}

static int sum_index(const struct rsc *rsc)
{
	return container_of(rsc, struct sum, rsc)->idx[rsc->conj];
}

static int sum_output_slot(const struct rsc *rsc)
{
	return (sum_index(rsc) << 4) + 0xc;
}

static const struct rsc_ops sum_basic_rsc_ops = {
	.master		= sum_master,
	.next_conj	= sum_next_conj,
	.index		= sum_index,
	.output_slot	= sum_output_slot,
};

static int sum_rsc_init(struct sum *sum,
			const struct sum_desc *desc,
			struct sum_mgr *mgr)
{
	int err;

	err = rsc_init(&sum->rsc, sum->idx[0], SUM, desc->msr, mgr->mgr.hw);
	if (err)
		return err;

	sum->rsc.ops = &sum_basic_rsc_ops;

	return 0;
}

static int sum_rsc_uninit(struct sum *sum)
{
	rsc_uninit(&sum->rsc);
	return 0;
}

static int get_sum_rsc(struct sum_mgr *mgr,
		       const struct sum_desc *desc,
		       struct sum **rsum)
{
	int err, i;
	unsigned int idx;
	struct sum *sum;
	unsigned long flags;

	*rsum = NULL;

	/* Allocate mem for sum resource */
	sum = kzalloc(sizeof(*sum), GFP_KERNEL);
	if (!sum)
		return -ENOMEM;

	/* Check whether there are sufficient sum resources to meet request. */
	err = 0;
	spin_lock_irqsave(&mgr->mgr_lock, flags);
	for (i = 0; i < desc->msr; i++) {
		err = mgr_get_resource(&mgr->mgr, 1, &idx);
		if (err)
			break;

		sum->idx[i] = idx;
	}
	spin_unlock_irqrestore(&mgr->mgr_lock, flags);
	if (err) {
		dev_err(mgr->card->dev,
			"Can't meet SUM resource request!\n");
		goto error;
	}

	err = sum_rsc_init(sum, desc, mgr);
	if (err)
		goto error;

	*rsum = sum;

	return 0;

error:
	spin_lock_irqsave(&mgr->mgr_lock, flags);
	for (i--; i >= 0; i--)
		mgr_put_resource(&mgr->mgr, 1, sum->idx[i]);

	spin_unlock_irqrestore(&mgr->mgr_lock, flags);
	kfree(sum);
	return err;
}

static int put_sum_rsc(struct sum_mgr *mgr, struct sum *sum)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&mgr->mgr_lock, flags);
	for (i = 0; i < sum->rsc.msr; i++)
		mgr_put_resource(&mgr->mgr, 1, sum->idx[i]);

	spin_unlock_irqrestore(&mgr->mgr_lock, flags);
	sum_rsc_uninit(sum);
	kfree(sum);

	return 0;
}

int sum_mgr_create(struct hw *hw, struct sum_mgr **rsum_mgr)
{
	int err;
	struct sum_mgr *sum_mgr;

	*rsum_mgr = NULL;
	sum_mgr = kzalloc(sizeof(*sum_mgr), GFP_KERNEL);
	if (!sum_mgr)
		return -ENOMEM;

	err = rsc_mgr_init(&sum_mgr->mgr, SUM, SUM_RESOURCE_NUM, hw);
	if (err)
		goto error;

	spin_lock_init(&sum_mgr->mgr_lock);

	sum_mgr->get_sum = get_sum_rsc;
	sum_mgr->put_sum = put_sum_rsc;
	sum_mgr->card = hw->card;

	*rsum_mgr = sum_mgr;

	return 0;

error:
	kfree(sum_mgr);
	return err;
}

int sum_mgr_destroy(struct sum_mgr *sum_mgr)
{
	rsc_mgr_uninit(&sum_mgr->mgr);
	kfree(sum_mgr);
	return 0;
}

