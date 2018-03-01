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

/*
 * Initialize core power state and usage count. To be called after
 * successful first boot. Hence core 0 will be running and other cores
 * will be reset
 */
void skl_dsp_init_core_state(struct sst_dsp *ctx)
{
	struct skl_sst *skl = ctx->thread_context;
	int i;

	skl->cores.state[SKL_DSP_CORE0_ID] = SKL_DSP_RUNNING;
	skl->cores.usage_count[SKL_DSP_CORE0_ID] = 1;

	for (i = SKL_DSP_CORE0_ID + 1; i < skl->cores.count; i++) {
		skl->cores.state[i] = SKL_DSP_RESET;
		skl->cores.usage_count[i] = 0;
	}
}

/* Get the mask for all enabled cores */
unsigned int skl_dsp_get_enabled_cores(struct sst_dsp *ctx)
{
	struct skl_sst *skl = ctx->thread_context;
	unsigned int core_mask, en_cores_mask;
	u32 val;

	core_mask = SKL_DSP_CORES_MASK(skl->cores.count);

	val = sst_dsp_shim_read_unlocked(ctx, SKL_ADSP_REG_ADSPCS);

	/* Cores having CPA bit set */
	en_cores_mask = (val & SKL_ADSPCS_CPA_MASK(core_mask)) >>
			SKL_ADSPCS_CPA_SHIFT;

	/* And cores having CRST bit cleared */
	en_cores_mask &= (~val & SKL_ADSPCS_CRST_MASK(core_mask)) >>
			SKL_ADSPCS_CRST_SHIFT;

	/* And cores having CSTALL bit cleared */
	en_cores_mask &= (~val & SKL_ADSPCS_CSTALL_MASK(core_mask)) >>
			SKL_ADSPCS_CSTALL_SHIFT;
	en_cores_mask &= core_mask;

	dev_dbg(ctx->dev, "DSP enabled cores mask = %x\n", en_cores_mask);

	return en_cores_mask;
}

static int
skl_dsp_core_set_reset_state(struct sst_dsp *ctx, unsigned int core_mask)
{
	int ret;

	/* update bits */
	sst_dsp_shim_update_bits_unlocked(ctx,
			SKL_ADSP_REG_ADSPCS, SKL_ADSPCS_CRST_MASK(core_mask),
			SKL_ADSPCS_CRST_MASK(core_mask));

	/* poll with timeout to check if operation successful */
	ret = sst_dsp_register_poll(ctx,
			SKL_ADSP_REG_ADSPCS,
			SKL_ADSPCS_CRST_MASK(core_mask),
			SKL_ADSPCS_CRST_MASK(core_mask),
			SKL_DSP_RESET_TO,
			"Set reset");
	if ((sst_dsp_shim_read_unlocked(ctx, SKL_ADSP_REG_ADSPCS) &
				SKL_ADSPCS_CRST_MASK(core_mask)) !=
				SKL_ADSPCS_CRST_MASK(core_mask)) {
		dev_err(ctx->dev, "Set reset state failed: core_mask %x\n",
							core_mask);
		ret = -EIO;
	}

	return ret;
}

int skl_dsp_core_unset_reset_state(
		struct sst_dsp *ctx, unsigned int core_mask)
{
	int ret;

	dev_dbg(ctx->dev, "In %s\n", __func__);

	/* update bits */
	sst_dsp_shim_update_bits_unlocked(ctx, SKL_ADSP_REG_ADSPCS,
				SKL_ADSPCS_CRST_MASK(core_mask), 0);

	/* poll with timeout to check if operation successful */
	ret = sst_dsp_register_poll(ctx,
			SKL_ADSP_REG_ADSPCS,
			SKL_ADSPCS_CRST_MASK(core_mask),
			0,
			SKL_DSP_RESET_TO,
			"Unset reset");

	if ((sst_dsp_shim_read_unlocked(ctx, SKL_ADSP_REG_ADSPCS) &
				SKL_ADSPCS_CRST_MASK(core_mask)) != 0) {
		dev_err(ctx->dev, "Unset reset state failed: core_mask %x\n",
				core_mask);
		ret = -EIO;
	}

	return ret;
}

static bool
is_skl_dsp_core_enable(struct sst_dsp *ctx, unsigned int core_mask)
{
	int val;
	bool is_enable;

	val = sst_dsp_shim_read_unlocked(ctx, SKL_ADSP_REG_ADSPCS);

	is_enable = ((val & SKL_ADSPCS_CPA_MASK(core_mask)) &&
			(val & SKL_ADSPCS_SPA_MASK(core_mask)) &&
			!(val & SKL_ADSPCS_CRST_MASK(core_mask)) &&
			!(val & SKL_ADSPCS_CSTALL_MASK(core_mask)));

	dev_dbg(ctx->dev, "DSP core(s) enabled? %d : core_mask %x\n",
						is_enable, core_mask);

	return is_enable;
}

static int skl_dsp_reset_core(struct sst_dsp *ctx, unsigned int core_mask)
{
	/* stall core */
	sst_dsp_shim_update_bits_unlocked(ctx, SKL_ADSP_REG_ADSPCS,
			SKL_ADSPCS_CSTALL_MASK(core_mask),
			SKL_ADSPCS_CSTALL_MASK(core_mask));

	/* set reset state */
	return skl_dsp_core_set_reset_state(ctx, core_mask);
}

int skl_dsp_start_core(struct sst_dsp *ctx, unsigned int core_mask)
{
	int ret;

	/* unset reset state */
	ret = skl_dsp_core_unset_reset_state(ctx, core_mask);
	if (ret < 0)
		return ret;

	/* run core */
	dev_dbg(ctx->dev, "unstall/run core: core_mask = %x\n", core_mask);
	sst_dsp_shim_update_bits_unlocked(ctx, SKL_ADSP_REG_ADSPCS,
			SKL_ADSPCS_CSTALL_MASK(core_mask), 0);

	if (!is_skl_dsp_core_enable(ctx, core_mask)) {
		skl_dsp_reset_core(ctx, core_mask);
		dev_err(ctx->dev, "DSP start core failed: core_mask %x\n",
							core_mask);
		ret = -EIO;
	}

	return ret;
}

int skl_dsp_core_power_up(struct sst_dsp *ctx, unsigned int core_mask)
{
	int ret;

	/* update bits */
	sst_dsp_shim_update_bits_unlocked(ctx, SKL_ADSP_REG_ADSPCS,
			SKL_ADSPCS_SPA_MASK(core_mask),
			SKL_ADSPCS_SPA_MASK(core_mask));

	/* poll with timeout to check if operation successful */
	ret = sst_dsp_register_poll(ctx,
			SKL_ADSP_REG_ADSPCS,
			SKL_ADSPCS_CPA_MASK(core_mask),
			SKL_ADSPCS_CPA_MASK(core_mask),
			SKL_DSP_PU_TO,
			"Power up");

	if ((sst_dsp_shim_read_unlocked(ctx, SKL_ADSP_REG_ADSPCS) &
			SKL_ADSPCS_CPA_MASK(core_mask)) !=
			SKL_ADSPCS_CPA_MASK(core_mask)) {
		dev_err(ctx->dev, "DSP core power up failed: core_mask %x\n",
				core_mask);
		ret = -EIO;
	}

	return ret;
}

int skl_dsp_core_power_down(struct sst_dsp  *ctx, unsigned int core_mask)
{
	/* update bits */
	sst_dsp_shim_update_bits_unlocked(ctx, SKL_ADSP_REG_ADSPCS,
				SKL_ADSPCS_SPA_MASK(core_mask), 0);

	/* poll with timeout to check if operation successful */
	return sst_dsp_register_poll(ctx,
			SKL_ADSP_REG_ADSPCS,
			SKL_ADSPCS_CPA_MASK(core_mask),
			0,
			SKL_DSP_PD_TO,
			"Power down");
}

int skl_dsp_enable_core(struct sst_dsp  *ctx, unsigned int core_mask)
{
	int ret;

	/* power up */
	ret = skl_dsp_core_power_up(ctx, core_mask);
	if (ret < 0) {
		dev_err(ctx->dev, "dsp core power up failed: core_mask %x\n",
							core_mask);
		return ret;
	}

	return skl_dsp_start_core(ctx, core_mask);
}

int skl_dsp_disable_core(struct sst_dsp *ctx, unsigned int core_mask)
{
	int ret;

	ret = skl_dsp_reset_core(ctx, core_mask);
	if (ret < 0) {
		dev_err(ctx->dev, "dsp core reset failed: core_mask %x\n",
							core_mask);
		return ret;
	}

	/* power down core*/
	ret = skl_dsp_core_power_down(ctx, core_mask);
	if (ret < 0) {
		dev_err(ctx->dev, "dsp core power down fail mask %x: %d\n",
							core_mask, ret);
		return ret;
	}

	if (is_skl_dsp_core_enable(ctx, core_mask)) {
		dev_err(ctx->dev, "dsp core disable fail mask %x: %d\n",
							core_mask, ret);
		ret = -EIO;
	}

	return ret;
}

int skl_dsp_boot(struct sst_dsp *ctx)
{
	int ret;

	if (is_skl_dsp_core_enable(ctx, SKL_DSP_CORE0_MASK)) {
		ret = skl_dsp_reset_core(ctx, SKL_DSP_CORE0_MASK);
		if (ret < 0) {
			dev_err(ctx->dev, "dsp core0 reset fail: %d\n", ret);
			return ret;
		}

		ret = skl_dsp_start_core(ctx, SKL_DSP_CORE0_MASK);
		if (ret < 0) {
			dev_err(ctx->dev, "dsp core0 start fail: %d\n", ret);
			return ret;
		}
	} else {
		ret = skl_dsp_disable_core(ctx, SKL_DSP_CORE0_MASK);
		if (ret < 0) {
			dev_err(ctx->dev, "dsp core0 disable fail: %d\n", ret);
			return ret;
		}
		ret = skl_dsp_enable_core(ctx, SKL_DSP_CORE0_MASK);
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
/*
 * skl_dsp_get_core/skl_dsp_put_core will be called inside DAPM context
 * within the dapm mutex. Hence no separate lock is used.
 */
int skl_dsp_get_core(struct sst_dsp *ctx, unsigned int core_id)
{
	struct skl_sst *skl = ctx->thread_context;
	int ret = 0;

	if (core_id >= skl->cores.count) {
		dev_err(ctx->dev, "invalid core id: %d\n", core_id);
		return -EINVAL;
	}

	skl->cores.usage_count[core_id]++;

	if (skl->cores.state[core_id] == SKL_DSP_RESET) {
		ret = ctx->fw_ops.set_state_D0(ctx, core_id);
		if (ret < 0) {
			dev_err(ctx->dev, "unable to get core%d\n", core_id);
			goto out;
		}
	}

out:
	dev_dbg(ctx->dev, "core id %d state %d usage_count %d\n",
			core_id, skl->cores.state[core_id],
			skl->cores.usage_count[core_id]);

	return ret;
}
EXPORT_SYMBOL_GPL(skl_dsp_get_core);

int skl_dsp_put_core(struct sst_dsp *ctx, unsigned int core_id)
{
	struct skl_sst *skl = ctx->thread_context;
	int ret = 0;

	if (core_id >= skl->cores.count) {
		dev_err(ctx->dev, "invalid core id: %d\n", core_id);
		return -EINVAL;
	}

	if ((--skl->cores.usage_count[core_id] == 0) &&
		(skl->cores.state[core_id] != SKL_DSP_RESET)) {
		ret = ctx->fw_ops.set_state_D3(ctx, core_id);
		if (ret < 0) {
			dev_err(ctx->dev, "unable to put core %d: %d\n",
					core_id, ret);
			skl->cores.usage_count[core_id]++;
		}
	}

	dev_dbg(ctx->dev, "core id %d state %d usage_count %d\n",
			core_id, skl->cores.state[core_id],
			skl->cores.usage_count[core_id]);

	return ret;
}
EXPORT_SYMBOL_GPL(skl_dsp_put_core);

int skl_dsp_wake(struct sst_dsp *ctx)
{
	return skl_dsp_get_core(ctx, SKL_DSP_CORE0_ID);
}
EXPORT_SYMBOL_GPL(skl_dsp_wake);

int skl_dsp_sleep(struct sst_dsp *ctx)
{
	return skl_dsp_put_core(ctx, SKL_DSP_CORE0_ID);
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

	return sst;
}

int skl_dsp_acquire_irq(struct sst_dsp *sst)
{
	struct sst_dsp_device *sst_dev = sst->sst_dev;
	int ret;

	/* Register the ISR */
	ret = request_threaded_irq(sst->irq, sst->ops->irq_handler,
		sst_dev->thread, IRQF_SHARED, "AudioDSP", sst);
	if (ret)
		dev_err(sst->dev, "unable to grab threaded IRQ %d, disabling device\n",
			       sst->irq);

	return ret;
}

void skl_dsp_free(struct sst_dsp *dsp)
{
	skl_ipc_int_disable(dsp);

	free_irq(dsp->irq, dsp);
	skl_ipc_op_int_disable(dsp);
	skl_dsp_disable_core(dsp, SKL_DSP_CORE0_MASK);
}
EXPORT_SYMBOL_GPL(skl_dsp_free);

bool is_skl_dsp_running(struct sst_dsp *ctx)
{
	return (ctx->sst_state == SKL_DSP_RUNNING);
}
EXPORT_SYMBOL_GPL(is_skl_dsp_running);
