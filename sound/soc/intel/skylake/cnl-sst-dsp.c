/*
 * cnl-sst-dsp.c - CNL SST library generic function
 *
 * Copyright (C) 2016-17, Intel Corporation.
 * Author: Guneshwor Singh <guneshwor.o.singh@intel.com>
 *
 * Modified from:
 *	SKL SST library generic function
 *	Copyright (C) 2014-15, Intel Corporation.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/device.h>
#include "../common/sst-dsp.h"
#include "../common/sst-ipc.h"
#include "../common/sst-dsp-priv.h"
#include "cnl-sst-dsp.h"

/* various timeout values */
#define CNL_DSP_PU_TO		50
#define CNL_DSP_PD_TO		50
#define CNL_DSP_RESET_TO	50

static int
cnl_dsp_core_set_reset_state(struct sst_dsp *ctx, unsigned int core_mask)
{
	/* update bits */
	sst_dsp_shim_update_bits_unlocked(ctx,
			CNL_ADSP_REG_ADSPCS, CNL_ADSPCS_CRST(core_mask),
			CNL_ADSPCS_CRST(core_mask));

	/* poll with timeout to check if operation successful */
	return sst_dsp_register_poll(ctx,
			CNL_ADSP_REG_ADSPCS,
			CNL_ADSPCS_CRST(core_mask),
			CNL_ADSPCS_CRST(core_mask),
			CNL_DSP_RESET_TO,
			"Set reset");
}

static int
cnl_dsp_core_unset_reset_state(struct sst_dsp *ctx, unsigned int core_mask)
{
	/* update bits */
	sst_dsp_shim_update_bits_unlocked(ctx, CNL_ADSP_REG_ADSPCS,
					CNL_ADSPCS_CRST(core_mask), 0);

	/* poll with timeout to check if operation successful */
	return sst_dsp_register_poll(ctx,
			CNL_ADSP_REG_ADSPCS,
			CNL_ADSPCS_CRST(core_mask),
			0,
			CNL_DSP_RESET_TO,
			"Unset reset");
}

static bool is_cnl_dsp_core_enable(struct sst_dsp *ctx, unsigned int core_mask)
{
	int val;
	bool is_enable;

	val = sst_dsp_shim_read_unlocked(ctx, CNL_ADSP_REG_ADSPCS);

	is_enable = (val & CNL_ADSPCS_CPA(core_mask)) &&
			(val & CNL_ADSPCS_SPA(core_mask)) &&
			!(val & CNL_ADSPCS_CRST(core_mask)) &&
			!(val & CNL_ADSPCS_CSTALL(core_mask));

	dev_dbg(ctx->dev, "DSP core(s) enabled? %d: core_mask %#x\n",
		is_enable, core_mask);

	return is_enable;
}

static int cnl_dsp_reset_core(struct sst_dsp *ctx, unsigned int core_mask)
{
	/* stall core */
	sst_dsp_shim_update_bits_unlocked(ctx, CNL_ADSP_REG_ADSPCS,
			CNL_ADSPCS_CSTALL(core_mask),
			CNL_ADSPCS_CSTALL(core_mask));

	/* set reset state */
	return cnl_dsp_core_set_reset_state(ctx, core_mask);
}

static int cnl_dsp_start_core(struct sst_dsp *ctx, unsigned int core_mask)
{
	int ret;

	/* unset reset state */
	ret = cnl_dsp_core_unset_reset_state(ctx, core_mask);
	if (ret < 0)
		return ret;

	/* run core */
	sst_dsp_shim_update_bits_unlocked(ctx, CNL_ADSP_REG_ADSPCS,
				CNL_ADSPCS_CSTALL(core_mask), 0);

	if (!is_cnl_dsp_core_enable(ctx, core_mask)) {
		cnl_dsp_reset_core(ctx, core_mask);
		dev_err(ctx->dev, "DSP core mask %#x enable failed\n",
			core_mask);
		ret = -EIO;
	}

	return ret;
}

static int cnl_dsp_core_power_up(struct sst_dsp *ctx, unsigned int core_mask)
{
	/* update bits */
	sst_dsp_shim_update_bits_unlocked(ctx, CNL_ADSP_REG_ADSPCS,
					  CNL_ADSPCS_SPA(core_mask),
					  CNL_ADSPCS_SPA(core_mask));

	/* poll with timeout to check if operation successful */
	return sst_dsp_register_poll(ctx, CNL_ADSP_REG_ADSPCS,
				    CNL_ADSPCS_CPA(core_mask),
				    CNL_ADSPCS_CPA(core_mask),
				    CNL_DSP_PU_TO,
				    "Power up");
}

static int cnl_dsp_core_power_down(struct sst_dsp *ctx, unsigned int core_mask)
{
	/* update bits */
	sst_dsp_shim_update_bits_unlocked(ctx, CNL_ADSP_REG_ADSPCS,
					CNL_ADSPCS_SPA(core_mask), 0);

	/* poll with timeout to check if operation successful */
	return sst_dsp_register_poll(ctx,
			CNL_ADSP_REG_ADSPCS,
			CNL_ADSPCS_CPA(core_mask),
			0,
			CNL_DSP_PD_TO,
			"Power down");
}

int cnl_dsp_enable_core(struct sst_dsp *ctx, unsigned int core_mask)
{
	int ret;

	/* power up */
	ret = cnl_dsp_core_power_up(ctx, core_mask);
	if (ret < 0) {
		dev_dbg(ctx->dev, "DSP core mask %#x power up failed",
			core_mask);
		return ret;
	}

	return cnl_dsp_start_core(ctx, core_mask);
}

int cnl_dsp_disable_core(struct sst_dsp *ctx, unsigned int core_mask)
{
	int ret;

	ret = cnl_dsp_reset_core(ctx, core_mask);
	if (ret < 0) {
		dev_err(ctx->dev, "DSP core mask %#x reset failed\n",
			core_mask);
		return ret;
	}

	/* power down core*/
	ret = cnl_dsp_core_power_down(ctx, core_mask);
	if (ret < 0) {
		dev_err(ctx->dev, "DSP core mask %#x power down failed\n",
			core_mask);
		return ret;
	}

	if (is_cnl_dsp_core_enable(ctx, core_mask)) {
		dev_err(ctx->dev, "DSP core mask %#x disable failed\n",
			core_mask);
		ret = -EIO;
	}

	return ret;
}

irqreturn_t cnl_dsp_sst_interrupt(int irq, void *dev_id)
{
	struct sst_dsp *ctx = dev_id;
	u32 val;
	irqreturn_t ret = IRQ_NONE;

	spin_lock(&ctx->spinlock);

	val = sst_dsp_shim_read_unlocked(ctx, CNL_ADSP_REG_ADSPIS);
	ctx->intr_status = val;

	if (val == 0xffffffff) {
		spin_unlock(&ctx->spinlock);
		return IRQ_NONE;
	}

	if (val & CNL_ADSPIS_IPC) {
		cnl_ipc_int_disable(ctx);
		ret = IRQ_WAKE_THREAD;
	}

	spin_unlock(&ctx->spinlock);

	return ret;
}

void cnl_dsp_free(struct sst_dsp *dsp)
{
	cnl_ipc_int_disable(dsp);

	free_irq(dsp->irq, dsp);
	cnl_ipc_op_int_disable(dsp);
	cnl_dsp_disable_core(dsp, SKL_DSP_CORE0_MASK);
}
EXPORT_SYMBOL_GPL(cnl_dsp_free);

void cnl_ipc_int_enable(struct sst_dsp *ctx)
{
	sst_dsp_shim_update_bits(ctx, CNL_ADSP_REG_ADSPIC,
				 CNL_ADSPIC_IPC, CNL_ADSPIC_IPC);
}

void cnl_ipc_int_disable(struct sst_dsp *ctx)
{
	sst_dsp_shim_update_bits_unlocked(ctx, CNL_ADSP_REG_ADSPIC,
					  CNL_ADSPIC_IPC, 0);
}

void cnl_ipc_op_int_enable(struct sst_dsp *ctx)
{
	/* enable IPC DONE interrupt */
	sst_dsp_shim_update_bits(ctx, CNL_ADSP_REG_HIPCCTL,
				 CNL_ADSP_REG_HIPCCTL_DONE,
				 CNL_ADSP_REG_HIPCCTL_DONE);

	/* enable IPC BUSY interrupt */
	sst_dsp_shim_update_bits(ctx, CNL_ADSP_REG_HIPCCTL,
				 CNL_ADSP_REG_HIPCCTL_BUSY,
				 CNL_ADSP_REG_HIPCCTL_BUSY);
}

void cnl_ipc_op_int_disable(struct sst_dsp *ctx)
{
	/* disable IPC DONE interrupt */
	sst_dsp_shim_update_bits(ctx, CNL_ADSP_REG_HIPCCTL,
				 CNL_ADSP_REG_HIPCCTL_DONE, 0);

	/* disable IPC BUSY interrupt */
	sst_dsp_shim_update_bits(ctx, CNL_ADSP_REG_HIPCCTL,
				 CNL_ADSP_REG_HIPCCTL_BUSY, 0);
}

bool cnl_ipc_int_status(struct sst_dsp *ctx)
{
	return sst_dsp_shim_read_unlocked(ctx, CNL_ADSP_REG_ADSPIS) &
							CNL_ADSPIS_IPC;
}

void cnl_ipc_free(struct sst_generic_ipc *ipc)
{
	cnl_ipc_op_int_disable(ipc->dsp);
	sst_ipc_fini(ipc);
}
