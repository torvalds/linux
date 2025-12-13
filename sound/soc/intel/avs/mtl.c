// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2021-2025 Intel Corporation
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
#define MTL_DSPCCTL_SPA		BIT(0)
#define MTL_DSPCCTL_CPA		BIT(8)
#define MTL_DSPCCTL_OSEL	GENMASK(25, 24)
#define MTL_DSPCCTL_OSEL_HOST	BIT(25)

#define MTL_HfINT_BASE		0x1100
#define MTL_REG_HfINTIPPTR	(MTL_HfINT_BASE + 0x8)
#define MTL_REG_HfHIPCIE	(MTL_HfINT_BASE + 0x40)
#define MTL_HfINTIPPTR_PTR	GENMASK(20, 0)
#define MTL_HfHIPCIE_IE		BIT(0)

#define MTL_DWICTL_INTENL_IE		BIT(0)
#define MTL_DWICTL_FINALSTATUSL_IPC	BIT(0) /* same as ADSPIS_IPC */

static int avs_mtl_core_power_on(struct avs_dev *adev)
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
	snd_hdac_adsp_updatel(adev, MTL_REG_HfPWRCTL, MTL_HfPWRCTL_WPDSPHPxPG,
			      MTL_HfPWRCTL_WPDSPHPxPG);
	trace_avs_dsp_core_op(1, AVS_MAIN_CORE_MASK, "prevent dsp PG", true);

	ret = snd_hdac_adsp_readl_poll(adev, MTL_REG_HfPWRSTS, reg,
				       (reg & MTL_HfPWRSTS_DSPHPxPGS) == MTL_HfPWRSTS_DSPHPxPGS,
				       AVS_ADSPCS_INTERVAL_US, AVS_ADSPCS_TIMEOUT_US);

	/* Set ownership to HOST. */
	snd_hdac_adsp_updatel(adev, MTL_REG_DSPCCTL, MTL_DSPCCTL_OSEL, MTL_DSPCCTL_OSEL_HOST);
	return ret;
}

static int avs_mtl_core_power_off(struct avs_dev *adev)
{
	u32 reg;

	/* Allow power gating of DSP domain. No STS polling as HOST is only one of its users. */
	snd_hdac_adsp_updatel(adev, MTL_REG_HfPWRCTL, MTL_HfPWRCTL_WPDSPHPxPG, 0);
	trace_avs_dsp_core_op(0, AVS_MAIN_CORE_MASK, "allow dsp pg", false);

	/* Power down DSP domain. */
	snd_hdac_adsp_updatel(adev, MTL_REG_HfDSSCS, MTL_HfDSSCS_SPA, 0);
	trace_avs_dsp_core_op(0, AVS_MAIN_CORE_MASK, "power dsp", false);

	return snd_hdac_adsp_readl_poll(adev, MTL_REG_HfDSSCS, reg,
					(reg & MTL_HfDSSCS_CPA) == 0,
					AVS_ADSPCS_INTERVAL_US, AVS_ADSPCS_TIMEOUT_US);
}

int avs_mtl_core_power(struct avs_dev *adev, u32 core_mask, bool power)
{
	core_mask &= AVS_MAIN_CORE_MASK;
	if (!core_mask)
		return 0;

	if (power)
		return avs_mtl_core_power_on(adev);
	return avs_mtl_core_power_off(adev);
}

int avs_mtl_core_reset(struct avs_dev *adev, u32 core_mask, bool power)
{
	/* No logical equivalent on ACE 1.x. */
	return 0;
}

int avs_mtl_core_stall(struct avs_dev *adev, u32 core_mask, bool stall)
{
	u32 value, reg;
	int ret;

	core_mask &= AVS_MAIN_CORE_MASK;
	if (!core_mask)
		return 0;

	value = snd_hdac_adsp_readl(adev, MTL_REG_DSPCCTL);
	trace_avs_dsp_core_op(value, core_mask, "stall", stall);
	if (value == UINT_MAX)
		return 0;

	value = stall ? 0 : MTL_DSPCCTL_SPA;
	snd_hdac_adsp_updatel(adev, MTL_REG_DSPCCTL, MTL_DSPCCTL_SPA, value);

	value = stall ? 0 : MTL_DSPCCTL_CPA;
	ret = snd_hdac_adsp_readl_poll(adev, MTL_REG_DSPCCTL,
				       reg, (reg & MTL_DSPCCTL_CPA) == value,
				       AVS_ADSPCS_INTERVAL_US, AVS_ADSPCS_TIMEOUT_US);
	if (ret)
		dev_err(adev->dev, "core_mask %d %sstall failed: %d\n",
			core_mask, stall ? "" : "un", ret);
	return ret;
}

static void avs_mtl_ipc_interrupt(struct avs_dev *adev)
{
	const struct avs_spec *spec = adev->spec;
	u32 hipc_ack, hipc_rsp;

	snd_hdac_adsp_updatel(adev, spec->hipc->ctl_offset,
			      AVS_ADSP_HIPCCTL_DONE | AVS_ADSP_HIPCCTL_BUSY, 0);

	hipc_ack = snd_hdac_adsp_readl(adev, spec->hipc->ack_offset);
	hipc_rsp = snd_hdac_adsp_readl(adev, spec->hipc->rsp_offset);

	/* DSP acked host's request. */
	if (hipc_ack & spec->hipc->ack_done_mask) {
		complete(&adev->ipc->done_completion);

		/* Tell DSP it has our attention. */
		snd_hdac_adsp_updatel(adev, spec->hipc->ack_offset, spec->hipc->ack_done_mask,
				      spec->hipc->ack_done_mask);
	}

	/* DSP sent new response to process. */
	if (hipc_rsp & spec->hipc->rsp_busy_mask) {
		union avs_reply_msg msg;

		msg.primary = snd_hdac_adsp_readl(adev, MTL_REG_HfIPCxTDR);
		msg.ext.val = snd_hdac_adsp_readl(adev, MTL_REG_HfIPCxTDD);

		avs_dsp_process_response(adev, msg.val);

		/* Tell DSP we accepted its message. */
		snd_hdac_adsp_updatel(adev, MTL_REG_HfIPCxTDR,
				      MTL_HfIPCxTDR_BUSY, MTL_HfIPCxTDR_BUSY);
		/* Ack this response. */
		snd_hdac_adsp_updatel(adev, MTL_REG_HfIPCxTDA, MTL_HfIPCxTDA_BUSY, 0);
	}

	snd_hdac_adsp_updatel(adev, spec->hipc->ctl_offset,
			      AVS_ADSP_HIPCCTL_DONE | AVS_ADSP_HIPCCTL_BUSY,
			      AVS_ADSP_HIPCCTL_DONE | AVS_ADSP_HIPCCTL_BUSY);
}

irqreturn_t avs_mtl_dsp_interrupt(struct avs_dev *adev)
{
	u32 adspis = snd_hdac_adsp_readl(adev, MTL_DWICTL_REG_FINALSTATUSL);
	irqreturn_t ret = IRQ_NONE;

	if (adspis == UINT_MAX)
		return ret;

	if (adspis & MTL_DWICTL_FINALSTATUSL_IPC) {
		avs_mtl_ipc_interrupt(adev);
		ret = IRQ_HANDLED;
	}

	return ret;
}

void avs_mtl_interrupt_control(struct avs_dev *adev, bool enable)
{
	if (enable) {
		snd_hdac_adsp_updatel(adev, MTL_DWICTL_REG_INTENL, MTL_DWICTL_INTENL_IE,
				      MTL_DWICTL_INTENL_IE);
		snd_hdac_adsp_updatew(adev, MTL_REG_HfHIPCIE, MTL_HfHIPCIE_IE, MTL_HfHIPCIE_IE);
		snd_hdac_adsp_updatel(adev, MTL_REG_HfIPCxCTL, AVS_ADSP_HIPCCTL_DONE,
				      AVS_ADSP_HIPCCTL_DONE);
		snd_hdac_adsp_updatel(adev, MTL_REG_HfIPCxCTL, AVS_ADSP_HIPCCTL_BUSY,
				      AVS_ADSP_HIPCCTL_BUSY);
	} else {
		snd_hdac_adsp_updatel(adev, MTL_REG_HfIPCxCTL, AVS_ADSP_HIPCCTL_BUSY, 0);
		snd_hdac_adsp_updatel(adev, MTL_REG_HfIPCxCTL, AVS_ADSP_HIPCCTL_DONE, 0);
		snd_hdac_adsp_updatew(adev, MTL_REG_HfHIPCIE, MTL_HfHIPCIE_IE, 0);
		snd_hdac_adsp_updatel(adev, MTL_DWICTL_REG_INTENL, MTL_DWICTL_INTENL_IE, 0);
	}
}
