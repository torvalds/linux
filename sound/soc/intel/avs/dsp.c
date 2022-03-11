// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/module.h>
#include <sound/hdaudio_ext.h>
#include "avs.h"
#include "registers.h"

#define AVS_ADSPCS_INTERVAL_US		500
#define AVS_ADSPCS_TIMEOUT_US		50000

int avs_dsp_core_power(struct avs_dev *adev, u32 core_mask, bool power)
{
	u32 value, mask, reg;
	int ret;

	mask = AVS_ADSPCS_SPA_MASK(core_mask);
	value = power ? mask : 0;

	snd_hdac_adsp_updatel(adev, AVS_ADSP_REG_ADSPCS, mask, value);

	mask = AVS_ADSPCS_CPA_MASK(core_mask);
	value = power ? mask : 0;

	ret = snd_hdac_adsp_readl_poll(adev, AVS_ADSP_REG_ADSPCS,
				       reg, (reg & mask) == value,
				       AVS_ADSPCS_INTERVAL_US,
				       AVS_ADSPCS_TIMEOUT_US);
	if (ret)
		dev_err(adev->dev, "core_mask %d power %s failed: %d\n",
			core_mask, power ? "on" : "off", ret);

	return ret;
}

int avs_dsp_core_reset(struct avs_dev *adev, u32 core_mask, bool reset)
{
	u32 value, mask, reg;
	int ret;

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

	mask = AVS_ADSPCS_CSTALL_MASK(core_mask);
	value = stall ? mask : 0;

	snd_hdac_adsp_updatel(adev, AVS_ADSP_REG_ADSPCS, mask, value);

	ret = snd_hdac_adsp_readl_poll(adev, AVS_ADSP_REG_ADSPCS,
				       reg, (reg & mask) == value,
				       AVS_ADSPCS_INTERVAL_US,
				       AVS_ADSPCS_TIMEOUT_US);
	if (ret)
		dev_err(adev->dev, "core_mask %d %sstall failed: %d\n",
			core_mask, stall ? "" : "un", ret);

	return ret;
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

MODULE_LICENSE("GPL");
