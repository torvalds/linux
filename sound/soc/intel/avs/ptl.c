// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2024-2025 Intel Corporation
 *
 * Authors: Cezary Rojewski <cezary.rojewski@intel.com>
 *          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
 */

#include <sound/hdaudio_ext.h>
#include "avs.h"
#include "debug.h"
#include "registers.h"
#include "trace.h"

#define MTL_HfDSSGBL_BASE	0x1000
#define MTL_REG_HfDSSCS		(MTL_HfDSSGBL_BASE + 0x0)
#define MTL_HfDSSCS_SPA		BIT(16)
#define MTL_HfDSSCS_CPA		BIT(24)

#define MTL_DSPCS_BASE		0x178D00
#define MTL_REG_DSPCCTL		(MTL_DSPCS_BASE + 0x4)
#define MTL_DSPCCTL_OSEL	GENMASK(25, 24)
#define MTL_DSPCCTL_OSEL_HOST	BIT(25)

static int avs_ptl_core_power_on(struct avs_dev *adev)
{
	u32 reg;
	int ret;

	/* Power up DSP domain. */
	snd_hdac_adsp_updatel(adev, MTL_REG_HfDSSCS, MTL_HfDSSCS_SPA, MTL_HfDSSCS_SPA);
	trace_avs_dsp_core_op(1, AVS_MAIN_CORE_MASK, "power dsp", true);

	ret = snd_hdac_adsp_readl_poll(adev, MTL_REG_HfDSSCS, reg,
				       (reg & MTL_HfDSSCS_CPA) == MTL_HfDSSCS_CPA,
				       AVS_ADSPCS_INTERVAL_US, AVS_ADSPCS_TIMEOUT_US);
	if (ret) {
		dev_err(adev->dev, "power on domain dsp failed: %d\n", ret);
		return ret;
	}

	/* Prevent power gating of DSP domain. */
	snd_hdac_adsp_updatel(adev, MTL_REG_HfPWRCTL2, MTL_HfPWRCTL2_WPDSPHPxPG,
			      MTL_HfPWRCTL2_WPDSPHPxPG);
	trace_avs_dsp_core_op(1, AVS_MAIN_CORE_MASK, "prevent dsp PG", true);

	ret = snd_hdac_adsp_readl_poll(adev, MTL_REG_HfPWRSTS2, reg,
				       (reg & MTL_HfPWRSTS2_DSPHPxPGS) == MTL_HfPWRSTS2_DSPHPxPGS,
				       AVS_ADSPCS_INTERVAL_US, AVS_ADSPCS_TIMEOUT_US);

	/* Set ownership to HOST. */
	snd_hdac_adsp_updatel(adev, MTL_REG_DSPCCTL, MTL_DSPCCTL_OSEL, MTL_DSPCCTL_OSEL_HOST);
	return ret;
}

static int avs_ptl_core_power_off(struct avs_dev *adev)
{
	u32 reg;

	/* Allow power gating of DSP domain. No STS polling as HOST is only one of its users. */
	snd_hdac_adsp_updatel(adev, MTL_REG_HfPWRCTL2, MTL_HfPWRCTL2_WPDSPHPxPG, 0);
	trace_avs_dsp_core_op(0, AVS_MAIN_CORE_MASK, "allow dsp pg", false);

	/* Power down DSP domain. */
	snd_hdac_adsp_updatel(adev, MTL_REG_HfDSSCS, MTL_HfDSSCS_SPA, 0);
	trace_avs_dsp_core_op(0, AVS_MAIN_CORE_MASK, "power dsp", false);

	return snd_hdac_adsp_readl_poll(adev, MTL_REG_HfDSSCS, reg,
					(reg & MTL_HfDSSCS_CPA) == 0,
					AVS_ADSPCS_INTERVAL_US, AVS_ADSPCS_TIMEOUT_US);
}

static int avs_ptl_core_power(struct avs_dev *adev, u32 core_mask, bool power)
{
	core_mask &= AVS_MAIN_CORE_MASK;
	if (!core_mask)
		return 0;

	if (power)
		return avs_ptl_core_power_on(adev);
	return avs_ptl_core_power_off(adev);
}

const struct avs_dsp_ops avs_ptl_dsp_ops = {
	.power = avs_ptl_core_power,
	.reset = avs_mtl_core_reset,
	.stall = avs_lnl_core_stall,
	.dsp_interrupt = avs_mtl_dsp_interrupt,
	.int_control = avs_mtl_interrupt_control,
	.load_basefw = avs_hda_load_basefw,
	.load_lib = avs_hda_load_library,
	.transfer_mods = avs_hda_transfer_modules,
	.log_buffer_offset = avs_icl_log_buffer_offset,
	.log_buffer_status = avs_apl_log_buffer_status,
	.coredump = avs_apl_coredump,
	.d0ix_toggle = avs_icl_d0ix_toggle,
	.set_d0ix = avs_icl_set_d0ix,
	AVS_SET_ENABLE_LOGS_OP(icl)
};
