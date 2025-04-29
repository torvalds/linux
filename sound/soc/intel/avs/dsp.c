// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/string_choices.h>
#include <sound/hdaudio_ext.h>
#include "avs.h"
#include "registers.h"
#include "trace.h"

#define AVS_ADSPCS_INTERVAL_US		500
#define AVS_ADSPCS_TIMEOUT_US		50000
#define AVS_ADSPCS_DELAY_US		1000

int avs_dsp_core_power(struct avs_dev *adev, u32 core_mask, bool power)
{
	u32 value, mask, reg;
	int ret;

	value = snd_hdac_adsp_readl(adev, AVS_ADSP_REG_ADSPCS);
	trace_avs_dsp_core_op(value, core_mask, "power", power);

	mask = AVS_ADSPCS_SPA_MASK(core_mask);
	value = power ? mask : 0;

	snd_hdac_adsp_updatel(adev, AVS_ADSP_REG_ADSPCS, mask, value);
	/* Delay the polling to avoid false positives. */
	usleep_range(AVS_ADSPCS_DELAY_US, 2 * AVS_ADSPCS_DELAY_US);

	mask = AVS_ADSPCS_CPA_MASK(core_mask);
	value = power ? mask : 0;

	ret = snd_hdac_adsp_readl_poll(adev, AVS_ADSP_REG_ADSPCS,
				       reg, (reg & mask) == value,
				       AVS_ADSPCS_INTERVAL_US,
				       AVS_ADSPCS_TIMEOUT_US);
	if (ret)
		dev_err(adev->dev, "core_mask %d power %s failed: %d\n",
			core_mask, str_on_off(power), ret);

	return ret;
}

int avs_dsp_core_reset(struct avs_dev *adev, u32 core_mask, bool reset)
{
	u32 value, mask, reg;
	int ret;

	value = snd_hdac_adsp_readl(adev, AVS_ADSP_REG_ADSPCS);
	trace_avs_dsp_core_op(value, core_mask, "reset", reset);

	mask = AVS_ADSPCS_CRST_MASK(core_mask);
	value = reset ? mask : 0;

	snd_hdac_adsp_updatel(adev, AVS_ADSP_REG_ADSPCS, mask, value);

	ret = snd_hdac_adsp_readl_poll(adev, AVS_ADSP_REG_ADSPCS,
				       reg, (reg & mask) == value,
				       AVS_ADSPCS_INTERVAL_US,
				       AVS_ADSPCS_TIMEOUT_US);
	if (ret)
		dev_err(adev->dev, "core_mask %d %s reset failed: %d\n",
			core_mask, reset ? "enter" : "exit", ret);

	return ret;
}

int avs_dsp_core_stall(struct avs_dev *adev, u32 core_mask, bool stall)
{
	u32 value, mask, reg;
	int ret;

	value = snd_hdac_adsp_readl(adev, AVS_ADSP_REG_ADSPCS);
	trace_avs_dsp_core_op(value, core_mask, "stall", stall);

	mask = AVS_ADSPCS_CSTALL_MASK(core_mask);
	value = stall ? mask : 0;

	snd_hdac_adsp_updatel(adev, AVS_ADSP_REG_ADSPCS, mask, value);

	ret = snd_hdac_adsp_readl_poll(adev, AVS_ADSP_REG_ADSPCS,
				       reg, (reg & mask) == value,
				       AVS_ADSPCS_INTERVAL_US,
				       AVS_ADSPCS_TIMEOUT_US);
	if (ret) {
		dev_err(adev->dev, "core_mask %d %sstall failed: %d\n",
			core_mask, stall ? "" : "un", ret);
		return ret;
	}

	/* Give HW time to propagate the change. */
	usleep_range(AVS_ADSPCS_DELAY_US, 2 * AVS_ADSPCS_DELAY_US);
	return 0;
}

int avs_dsp_core_enable(struct avs_dev *adev, u32 core_mask)
{
	int ret;

	ret = avs_dsp_op(adev, power, core_mask, true);
	if (ret)
		return ret;

	ret = avs_dsp_op(adev, reset, core_mask, false);
	if (ret)
		return ret;

	return avs_dsp_op(adev, stall, core_mask, false);
}

int avs_dsp_core_disable(struct avs_dev *adev, u32 core_mask)
{
	/* No error checks to allow for complete DSP shutdown. */
	avs_dsp_op(adev, stall, core_mask, true);
	avs_dsp_op(adev, reset, core_mask, true);

	return avs_dsp_op(adev, power, core_mask, false);
}

static int avs_dsp_enable(struct avs_dev *adev, u32 core_mask)
{
	u32 mask;
	int ret;

	ret = avs_dsp_core_enable(adev, core_mask);
	if (ret < 0)
		return ret;

	mask = core_mask & ~AVS_MAIN_CORE_MASK;
	if (!mask)
		/*
		 * without main core, fw is dead anyway
		 * so setting D0 for it is futile.
		 */
		return 0;

	ret = avs_ipc_set_dx(adev, mask, true);
	return AVS_IPC_RET(ret);
}

static int avs_dsp_disable(struct avs_dev *adev, u32 core_mask)
{
	int ret;

	ret = avs_ipc_set_dx(adev, core_mask, false);
	if (ret)
		return AVS_IPC_RET(ret);

	return avs_dsp_core_disable(adev, core_mask);
}

static int avs_dsp_get_core(struct avs_dev *adev, u32 core_id)
{
	u32 mask;
	int ret;

	mask = BIT_MASK(core_id);
	if (mask == AVS_MAIN_CORE_MASK)
		/* nothing to do for main core */
		return 0;
	if (core_id >= adev->hw_cfg.dsp_cores) {
		ret = -EINVAL;
		goto err;
	}

	adev->core_refs[core_id]++;
	if (adev->core_refs[core_id] == 1) {
		/*
		 * No cores other than main-core can be running for DSP
		 * to achieve d0ix. Conscious SET_D0IX IPC failure is permitted,
		 * simply d0ix power state will no longer be attempted.
		 */
		ret = avs_dsp_disable_d0ix(adev);
		if (ret && ret != -AVS_EIPC)
			goto err_disable_d0ix;

		ret = avs_dsp_enable(adev, mask);
		if (ret)
			goto err_enable_dsp;
	}

	return 0;

err_enable_dsp:
	avs_dsp_enable_d0ix(adev);
err_disable_d0ix:
	adev->core_refs[core_id]--;
err:
	dev_err(adev->dev, "get core %d failed: %d\n", core_id, ret);
	return ret;
}

static int avs_dsp_put_core(struct avs_dev *adev, u32 core_id)
{
	u32 mask;
	int ret;

	mask = BIT_MASK(core_id);
	if (mask == AVS_MAIN_CORE_MASK)
		/* nothing to do for main core */
		return 0;
	if (core_id >= adev->hw_cfg.dsp_cores) {
		ret = -EINVAL;
		goto err;
	}

	adev->core_refs[core_id]--;
	if (!adev->core_refs[core_id]) {
		ret = avs_dsp_disable(adev, mask);
		if (ret)
			goto err;

		/* Match disable_d0ix in avs_dsp_get_core(). */
		avs_dsp_enable_d0ix(adev);
	}

	return 0;
err:
	dev_err(adev->dev, "put core %d failed: %d\n", core_id, ret);
	return ret;
}

int avs_dsp_init_module(struct avs_dev *adev, u16 module_id, u8 ppl_instance_id,
			u8 core_id, u8 domain, void *param, u32 param_size,
			u8 *instance_id)
{
	struct avs_module_entry mentry;
	bool was_loaded = false;
	int ret, id;

	id = avs_module_id_alloc(adev, module_id);
	if (id < 0)
		return id;

	ret = avs_get_module_id_entry(adev, module_id, &mentry);
	if (ret)
		goto err_mod_entry;

	ret = avs_dsp_get_core(adev, core_id);
	if (ret)
		goto err_mod_entry;

	/* Load code into memory if this is the first instance. */
	if (!id && !avs_module_entry_is_loaded(&mentry)) {
		ret = avs_dsp_op(adev, transfer_mods, true, &mentry, 1);
		if (ret) {
			dev_err(adev->dev, "load modules failed: %d\n", ret);
			goto err_mod_entry;
		}
		was_loaded = true;
	}

	ret = avs_ipc_init_instance(adev, module_id, id, ppl_instance_id,
				    core_id, domain, param, param_size);
	if (ret) {
		ret = AVS_IPC_RET(ret);
		goto err_ipc;
	}

	*instance_id = id;
	return 0;

err_ipc:
	if (was_loaded)
		avs_dsp_op(adev, transfer_mods, false, &mentry, 1);
	avs_dsp_put_core(adev, core_id);
err_mod_entry:
	avs_module_id_free(adev, module_id, id);
	return ret;
}

void avs_dsp_delete_module(struct avs_dev *adev, u16 module_id, u8 instance_id,
			   u8 ppl_instance_id, u8 core_id)
{
	struct avs_module_entry mentry;
	int ret;

	/* Modules not owned by any pipeline need to be freed explicitly. */
	if (ppl_instance_id == INVALID_PIPELINE_ID)
		avs_ipc_delete_instance(adev, module_id, instance_id);

	avs_module_id_free(adev, module_id, instance_id);

	ret = avs_get_module_id_entry(adev, module_id, &mentry);
	/* Unload occupied memory if this was the last instance. */
	if (!ret && mentry.type.load_type == AVS_MODULE_LOAD_TYPE_LOADABLE) {
		if (avs_is_module_ida_empty(adev, module_id)) {
			ret = avs_dsp_op(adev, transfer_mods, false, &mentry, 1);
			if (ret)
				dev_err(adev->dev, "unload modules failed: %d\n", ret);
		}
	}

	avs_dsp_put_core(adev, core_id);
}

int avs_dsp_create_pipeline(struct avs_dev *adev, u16 req_size, u8 priority,
			    bool lp, u16 attributes, u8 *instance_id)
{
	struct avs_fw_cfg *fw_cfg = &adev->fw_cfg;
	int ret, id;

	id = ida_alloc_max(&adev->ppl_ida, fw_cfg->max_ppl_count - 1, GFP_KERNEL);
	if (id < 0)
		return id;

	ret = avs_ipc_create_pipeline(adev, req_size, priority, id, lp, attributes);
	if (ret) {
		ida_free(&adev->ppl_ida, id);
		return AVS_IPC_RET(ret);
	}

	*instance_id = id;
	return 0;
}

int avs_dsp_delete_pipeline(struct avs_dev *adev, u8 instance_id)
{
	int ret;

	ret = avs_ipc_delete_pipeline(adev, instance_id);
	if (ret)
		ret = AVS_IPC_RET(ret);

	ida_free(&adev->ppl_ida, instance_id);
	return ret;
}
