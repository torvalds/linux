/*
 * skl-sst-dsp.c - SKL SST library generic function
 *
 * Copyright (C) 2014-15, Intel Corporation.
 * Author:Rafal Redzimski <rafal.f.redzimski@intel.com>
 *	Jeeja KP <jeeja.kp@intel.com>
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <sound/pcm.h>

#include "../common/sst-dsp.h"
#include "../common/sst-ipc.h"
#include "../common/sst-dsp-priv.h"
#include "skl-sst-ipc.h"

/* various timeout values */
#define SKL_DSP_PU_TO		50
#define SKL_DSP_PD_TO		50
#define SKL_DSP_RESET_TO	50

void skl_dsp_set_state_locked(struct sst_dsp *ctx, int state)
{
	mutex_lock(&ctx->mutex);
	ctx->sst_state = state;
	mutex_unlock(&ctx->mutex);
}

static int skl_dsp_core_set_reset_state(struct sst_dsp *ctx)
{
	int ret;

	/* update bits */
	sst_dsp_shim_update_bits_unlocked(ctx,
			SKL_ADSP_REG_ADSPCS, SKL_ADSPCS_CRST_MASK,
			SKL_ADSPCS_CRST(SKL_DSP_CORES_MASK));

	/* poll with timeout to check if operation successful */
	ret = sst_dsp_register_poll(ctx,
			SKL_ADSP_REG_ADSPCS,
			SKL_ADSPCS_CRST_MASK,
			SKL_ADSPCS_CRST(SKL_DSP_CORES_MASK),
			SKL_DSP_RESET_TO,
			"Set reset");
	if ((sst_dsp_shim_read_unlocked(ctx, SKL_ADSP_REG_ADSPCS) &
				SKL_ADSPCS_CRST(SKL_DSP_CORES_MASK)) !=
				SKL_ADSPCS_CRST(SKL_DSP_CORES_MASK)) {
		dev_err(ctx->dev, "Set reset state failed\n");
		ret = -EIO;
	}

	return ret;
}

static int skl_dsp_core_unset_reset_state(struct sst_dsp *ctx)
{
	int ret;

	dev_dbg(ctx->dev, "In %s\n", __func__);

	/* update bits */
	sst_dsp_shim_update_bits_unlocked(ctx, SKL_ADSP_REG_ADSPCS,
					SKL_ADSPCS_CRST_MASK, 0);

	/* poll with timeout to check if operation successful */
	ret = sst_dsp_register_poll(ctx,
			SKL_ADSP_REG_ADSPCS,
			SKL_ADSPCS_CRST_MASK,
			0,
			SKL_DSP_RESET_TO,
			"Unset reset");

	if ((sst_dsp_shim_read_unlocked(ctx, SKL_ADSP_REG_ADSPCS) &
				 SKL_ADSPCS_CRST(SKL_DSP_CORES_MASK)) != 0) {
		dev_err(ctx->dev, "Unset reset state failed\n");
		ret = -EIO;
	}

	return ret;
}

static bool is_skl_dsp_core_enable(struct sst_dsp *ctx)
{
	int val;
	bool is_enable;

	val = sst_dsp_shim_read_unlocked(ctx, SKL_ADSP_REG_ADSPCS);

	is_enable = ((val & SKL_ADSPCS_CPA(SKL_DSP_CORES_MASK)) &&
			(val & SKL_ADSPCS_SPA(SKL_DSP_CORES_MASK)) &&
			!(val & SKL_ADSPCS_CRST(SKL_DSP_CORES_MASK)) &&
			!(val & SKL_ADSPCS_CSTALL(SKL_DSP_CORES_MASK)));

	dev_dbg(ctx->dev, "DSP core is enabled=%d\n", is_enable);
	return is_enable;
}

static int skl_dsp_reset_core(struct sst_dsp *ctx)
{
	/* stall core */
	sst_dsp_shim_write_unlocked(ctx, SKL_ADSP_REG_ADSPCS,
			 sst_dsp_shim_read_unlocked(ctx, SKL_ADSP_REG_ADSPCS) &
				SKL_ADSPCS_CSTALL(SKL_DSP_CORES_MASK));

	/* set reset state */
	return skl_dsp_core_set_reset_state(ctx);
}

static int skl_dsp_start_core(struct sst_dsp *ctx)
{
	int ret;

	/* unset reset state */
	ret = skl_dsp_core_unset_reset_state(ctx);
	if (ret < 0) {
		dev_dbg(ctx->dev, "dsp unset reset fails\n");
		return ret;
	}

	/* run core */
	dev_dbg(ctx->dev, "run core...\n");
	sst_dsp_shim_write_unlocked(ctx, SKL_ADSP_REG_ADSPCS,
			 sst_dsp_shim_read_unlocked(ctx, SKL_ADSP_REG_ADSPCS) &
				~SKL_ADSPCS_CSTALL(SKL_DSP_CORES_MASK));

	if (!is_skl_dsp_core_enable(ctx)) {
		skl_dsp_reset_core(ctx);
		dev_err(ctx->dev, "DSP core enable failed\n");
		ret = -EIO;
	}

	return ret;
}

static int skl_dsp_core_power_up(struct sst_dsp *ctx)
{
	int ret;

	/* update bits */
	sst_dsp_shim_update_bits_unlocked(ctx, SKL_ADSP_REG_ADSPCS,
			SKL_ADSPCS_SPA_MASK, SKL_ADSPCS_SPA(SKL_DSP_CORES_MASK));

	/* poll with timeout to check if operation successful */
	ret = sst_dsp_register_poll(ctx,
			SKL_ADSP_REG_ADSPCS,
			SKL_ADSPCS_CPA_MASK,
			SKL_ADSPCS_CPA(SKL_DSP_CORES_MASK),
			SKL_DSP_PU_TO,
			"Power up");

	if ((sst_dsp_shim_read_unlocked(ctx, SKL_ADSP_REG_ADSPCS) &
			SKL_ADSPCS_CPA(SKL_DSP_CORES_MASK)) !=
			SKL_ADSPCS_CPA(SKL_DSP_CORES_MASK)) {
		dev_err(ctx->dev, "DSP core power up failed\n");
		ret = -EIO;
	}

	return ret;
}

static int skl_dsp_core_power_down(struct sst_dsp *ctx)
{
	/* update bits */
	sst_dsp_shim_update_bits_unlocked(ctx, SKL_ADSP_REG_ADSPCS,
					SKL_ADSPCS_SPA_MASK, 0);

	/* poll with timeout to check if operation successful */
	return sst_dsp_register_poll(ctx,
			SKL_ADSP_REG_ADSPCS,
			SKL_ADSPCS_CPA_MASK,
			0,
			SKL_DSP_PD_TO,
			"Power down");
}

int skl_dsp_enable_core(struct sst_dsp *ctx)
{
	int ret;

	/* power up */
	ret = skl_dsp_core_power_up(ctx);
	if (ret < 0) {
		dev_dbg(ctx->dev, "dsp core power up failed\n");
		return ret;
	}

	return skl_dsp_start_core(ctx);
}

int skl_dsp_disable_core(struct sst_dsp *ctx)
{
	int ret;

	ret = skl_dsp_reset_core(ctx);
	if (ret < 0) {
		dev_err(ctx->dev, "dsp core reset failed\n");
		return ret;
	}

	/* power down core*/
	ret = skl_dsp_core_power_down(ctx);
	if (ret < 0) {
		dev_err(ctx->dev, "dsp core power down failed\n");
		return ret;
	}

	if (is_skl_dsp_core_enable(ctx)) {
		dev_err(ctx->dev, "DSP core disable failed\n");
		ret = -EIO;
	}

	return ret;
}

int skl_dsp_boot(struct sst_dsp *ctx)
{
	int ret;

	if (is_skl_dsp_core_enable(ctx)) {
		dev_dbg(ctx->dev, "dsp core is already enabled, so reset the dap core\n");
		ret = skl_dsp_reset_core(ctx);
		if (ret < 0) {
			dev_err(ctx->dev, "dsp reset failed\n");
			return ret;
		}

		ret = skl_dsp_start_core(ctx);
		if (ret < 0) {
			dev_err(ctx->dev, "dsp start failed\n");
			return ret;
		}
	} else {
		dev_dbg(ctx->dev, "disable and enable to make sure DSP is invalid state\n");
		ret = skl_dsp_disable_core(ctx);

		if (ret < 0) {
			dev_err(ctx->dev, "dsp disable core failes\n");
			return ret;
		}
		ret = skl_dsp_enable_core(ctx);
	}

	return ret;
}

irqreturn_t skl_dsp_sst_interrupt(int irq, void *dev_id)
{
	struct sst_dsp *ctx = dev_id;
	u32 val;
	irqreturn_t result = IRQ_NONE;

	spin_lock(&ctx->spinlock);

	val = sst_dsp_shim_read_unlocked(ctx, SKL_ADSP_REG_ADSPIS);
	ctx->intr_status = val;

	if (val == 0xffffffff) {
		spin_unlock(&ctx->spinlock);
		return IRQ_NONE;
	}

	if (val & SKL_ADSPIS_IPC) {
		skl_ipc_int_disable(ctx);
		result = IRQ_WAKE_THREAD;
	}

	if (val & SKL_ADSPIS_CL_DMA) {
		skl_cldma_int_disable(ctx);
		result = IRQ_WAKE_THREAD;
	}

	spin_unlock(&ctx->spinlock);

	return result;
}

int skl_dsp_wake(struct sst_dsp *ctx)
{
	return ctx->fw_ops.set_state_D0(ctx);
}
EXPORT_SYMBOL_GPL(skl_dsp_wake);

int skl_dsp_sleep(struct sst_dsp *ctx)
{
	return ctx->fw_ops.set_state_D3(ctx);
}
EXPORT_SYMBOL_GPL(skl_dsp_sleep);

struct sst_dsp *skl_dsp_ctx_init(struct device *dev,
		struct sst_dsp_device *sst_dev, int irq)
{
	int ret;
	struct sst_dsp *sst;

	sst = devm_kzalloc(dev, sizeof(*sst), GFP_KERNEL);
	if (sst == NULL)
		return NULL;

	spin_lock_init(&sst->spinlock);
	mutex_init(&sst->mutex);
	sst->dev = dev;
	sst->sst_dev = sst_dev;
	sst->irq = irq;
	sst->ops = sst_dev->ops;
	sst->thread_context = sst_dev->thread_context;

	/* Initialise SST Audio DSP */
	if (sst->ops->init) {
		ret = sst->ops->init(sst, NULL);
		if (ret < 0)
			return NULL;
	}

	/* Register the ISR */
	ret = request_threaded_irq(sst->irq, sst->ops->irq_handler,
		sst_dev->thread, IRQF_SHARED, "AudioDSP", sst);
	if (ret) {
		dev_err(sst->dev, "unable to grab threaded IRQ %d, disabling device\n",
			       sst->irq);
		return NULL;
	}

	return sst;
}

void skl_dsp_free(struct sst_dsp *dsp)
{
	skl_ipc_int_disable(dsp);

	free_irq(dsp->irq, dsp);
	skl_ipc_op_int_disable(dsp);
	skl_ipc_int_disable(dsp);

	skl_dsp_disable_core(dsp);
}
EXPORT_SYMBOL_GPL(skl_dsp_free);

bool is_skl_dsp_running(struct sst_dsp *ctx)
{
	return (ctx->sst_state == SKL_DSP_RUNNING);
}
EXPORT_SYMBOL_GPL(is_skl_dsp_running);
