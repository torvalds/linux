// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation
//
// Authors: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//	    Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//	    Rander Wang <rander.wang@intel.com>
//          Keyon Jie <yang.jie@linux.intel.com>
//

/*
 * Hardware interface for generic Intel audio DSP HDA IP
 */

#include <linux/module.h>
#include <sound/hdaudio_ext.h>
#include <sound/hda_register.h>
#include <sound/hda-mlink.h>
#include <trace/events/sof_intel.h>
#include <sound/sof/xtensa.h>
#include "../sof-audio.h"
#include "../ops.h"
#include "hda.h"
#include "mtl.h"
#include "hda-ipc.h"

#define EXCEPT_MAX_HDR_SIZE	0x400
#define HDA_EXT_ROM_STATUS_SIZE 8

struct hda_dsp_msg_code {
	u32 code;
	const char *text;
};

static bool hda_enable_trace_D0I3_S0;
#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG)
module_param_named(enable_trace_D0I3_S0, hda_enable_trace_D0I3_S0, bool, 0444);
MODULE_PARM_DESC(enable_trace_D0I3_S0,
		 "SOF HDA enable trace when the DSP is in D0I3 in S0");
#endif

static void hda_get_interfaces(struct snd_sof_dev *sdev, u32 *interface_mask)
{
	const struct sof_intel_dsp_desc *chip;

	chip = get_chip_info(sdev->pdata);
	switch (chip->hw_ip_version) {
	case SOF_INTEL_TANGIER:
	case SOF_INTEL_BAYTRAIL:
	case SOF_INTEL_BROADWELL:
		interface_mask[SOF_DAI_DSP_ACCESS] =  BIT(SOF_DAI_INTEL_SSP);
		break;
	case SOF_INTEL_CAVS_1_5:
	case SOF_INTEL_CAVS_1_5_PLUS:
		interface_mask[SOF_DAI_DSP_ACCESS] =
			BIT(SOF_DAI_INTEL_SSP) | BIT(SOF_DAI_INTEL_DMIC) | BIT(SOF_DAI_INTEL_HDA);
		interface_mask[SOF_DAI_HOST_ACCESS] = BIT(SOF_DAI_INTEL_HDA);
		break;
	case SOF_INTEL_CAVS_1_8:
	case SOF_INTEL_CAVS_2_0:
	case SOF_INTEL_CAVS_2_5:
	case SOF_INTEL_ACE_1_0:
		interface_mask[SOF_DAI_DSP_ACCESS] =
			BIT(SOF_DAI_INTEL_SSP) | BIT(SOF_DAI_INTEL_DMIC) |
			BIT(SOF_DAI_INTEL_HDA) | BIT(SOF_DAI_INTEL_ALH);
		interface_mask[SOF_DAI_HOST_ACCESS] = BIT(SOF_DAI_INTEL_HDA);
		break;
	case SOF_INTEL_ACE_2_0:
		interface_mask[SOF_DAI_DSP_ACCESS] =
			BIT(SOF_DAI_INTEL_SSP) | BIT(SOF_DAI_INTEL_DMIC) |
			BIT(SOF_DAI_INTEL_HDA) | BIT(SOF_DAI_INTEL_ALH);
		 /* all interfaces accessible without DSP */
		interface_mask[SOF_DAI_HOST_ACCESS] =
			interface_mask[SOF_DAI_DSP_ACCESS];
		break;
	default:
		break;
	}
}

u32 hda_get_interface_mask(struct snd_sof_dev *sdev)
{
	u32 interface_mask[SOF_DAI_ACCESS_NUM] = { 0 };

	hda_get_interfaces(sdev, interface_mask);

	return interface_mask[sdev->dspless_mode_selected];
}
EXPORT_SYMBOL_NS(hda_get_interface_mask, SND_SOC_SOF_INTEL_HDA_COMMON);

bool hda_is_chain_dma_supported(struct snd_sof_dev *sdev, u32 dai_type)
{
	u32 interface_mask[SOF_DAI_ACCESS_NUM] = { 0 };
	const struct sof_intel_dsp_desc *chip;

	if (sdev->dspless_mode_selected)
		return false;

	hda_get_interfaces(sdev, interface_mask);

	if (!(interface_mask[SOF_DAI_DSP_ACCESS] & BIT(dai_type)))
		return false;

	if (dai_type == SOF_DAI_INTEL_HDA)
		return true;

	switch (dai_type) {
	case SOF_DAI_INTEL_SSP:
	case SOF_DAI_INTEL_DMIC:
	case SOF_DAI_INTEL_ALH:
		chip = get_chip_info(sdev->pdata);
		if (chip->hw_ip_version < SOF_INTEL_ACE_2_0)
			return false;
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_NS(hda_is_chain_dma_supported, SND_SOC_SOF_INTEL_HDA_COMMON);

/*
 * DSP Core control.
 */

static int hda_dsp_core_reset_enter(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	u32 adspcs;
	u32 reset;
	int ret;

	/* set reset bits for cores */
	reset = HDA_DSP_ADSPCS_CRST_MASK(core_mask);
	snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR,
					 HDA_DSP_REG_ADSPCS,
					 reset, reset);

	/* poll with timeout to check if operation successful */
	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR,
					HDA_DSP_REG_ADSPCS, adspcs,
					((adspcs & reset) == reset),
					HDA_DSP_REG_POLL_INTERVAL_US,
					HDA_DSP_RESET_TIMEOUT_US);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: %s: timeout on HDA_DSP_REG_ADSPCS read\n",
			__func__);
		return ret;
	}

	/* has core entered reset ? */
	adspcs = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
				  HDA_DSP_REG_ADSPCS);
	if ((adspcs & HDA_DSP_ADSPCS_CRST_MASK(core_mask)) !=
		HDA_DSP_ADSPCS_CRST_MASK(core_mask)) {
		dev_err(sdev->dev,
			"error: reset enter failed: core_mask %x adspcs 0x%x\n",
			core_mask, adspcs);
		ret = -EIO;
	}

	return ret;
}

static int hda_dsp_core_reset_leave(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	unsigned int crst;
	u32 adspcs;
	int ret;

	/* clear reset bits for cores */
	snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR,
					 HDA_DSP_REG_ADSPCS,
					 HDA_DSP_ADSPCS_CRST_MASK(core_mask),
					 0);

	/* poll with timeout to check if operation successful */
	crst = HDA_DSP_ADSPCS_CRST_MASK(core_mask);
	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR,
					    HDA_DSP_REG_ADSPCS, adspcs,
					    !(adspcs & crst),
					    HDA_DSP_REG_POLL_INTERVAL_US,
					    HDA_DSP_RESET_TIMEOUT_US);

	if (ret < 0) {
		dev_err(sdev->dev,
			"error: %s: timeout on HDA_DSP_REG_ADSPCS read\n",
			__func__);
		return ret;
	}

	/* has core left reset ? */
	adspcs = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
				  HDA_DSP_REG_ADSPCS);
	if ((adspcs & HDA_DSP_ADSPCS_CRST_MASK(core_mask)) != 0) {
		dev_err(sdev->dev,
			"error: reset leave failed: core_mask %x adspcs 0x%x\n",
			core_mask, adspcs);
		ret = -EIO;
	}

	return ret;
}

int hda_dsp_core_stall_reset(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	/* stall core */
	snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR,
					 HDA_DSP_REG_ADSPCS,
					 HDA_DSP_ADSPCS_CSTALL_MASK(core_mask),
					 HDA_DSP_ADSPCS_CSTALL_MASK(core_mask));

	/* set reset state */
	return hda_dsp_core_reset_enter(sdev, core_mask);
}
EXPORT_SYMBOL_NS(hda_dsp_core_stall_reset, SND_SOC_SOF_INTEL_HDA_COMMON);

bool hda_dsp_core_is_enabled(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	int val;
	bool is_enable;

	val = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPCS);

#define MASK_IS_EQUAL(v, m, field) ({	\
	u32 _m = field(m);		\
	((v) & _m) == _m;		\
})

	is_enable = MASK_IS_EQUAL(val, core_mask, HDA_DSP_ADSPCS_CPA_MASK) &&
		MASK_IS_EQUAL(val, core_mask, HDA_DSP_ADSPCS_SPA_MASK) &&
		!(val & HDA_DSP_ADSPCS_CRST_MASK(core_mask)) &&
		!(val & HDA_DSP_ADSPCS_CSTALL_MASK(core_mask));

#undef MASK_IS_EQUAL

	dev_dbg(sdev->dev, "DSP core(s) enabled? %d : core_mask %x\n",
		is_enable, core_mask);

	return is_enable;
}
EXPORT_SYMBOL_NS(hda_dsp_core_is_enabled, SND_SOC_SOF_INTEL_HDA_COMMON);

int hda_dsp_core_run(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	int ret;

	/* leave reset state */
	ret = hda_dsp_core_reset_leave(sdev, core_mask);
	if (ret < 0)
		return ret;

	/* run core */
	dev_dbg(sdev->dev, "unstall/run core: core_mask = %x\n", core_mask);
	snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR,
					 HDA_DSP_REG_ADSPCS,
					 HDA_DSP_ADSPCS_CSTALL_MASK(core_mask),
					 0);

	/* is core now running ? */
	if (!hda_dsp_core_is_enabled(sdev, core_mask)) {
		hda_dsp_core_stall_reset(sdev, core_mask);
		dev_err(sdev->dev, "error: DSP start core failed: core_mask %x\n",
			core_mask);
		ret = -EIO;
	}

	return ret;
}
EXPORT_SYMBOL_NS(hda_dsp_core_run, SND_SOC_SOF_INTEL_HDA_COMMON);

/*
 * Power Management.
 */

int hda_dsp_core_power_up(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;
	unsigned int cpa;
	u32 adspcs;
	int ret;

	/* restrict core_mask to host managed cores mask */
	core_mask &= chip->host_managed_cores_mask;
	/* return if core_mask is not valid */
	if (!core_mask)
		return 0;

	/* update bits */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPCS,
				HDA_DSP_ADSPCS_SPA_MASK(core_mask),
				HDA_DSP_ADSPCS_SPA_MASK(core_mask));

	/* poll with timeout to check if operation successful */
	cpa = HDA_DSP_ADSPCS_CPA_MASK(core_mask);
	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR,
					    HDA_DSP_REG_ADSPCS, adspcs,
					    (adspcs & cpa) == cpa,
					    HDA_DSP_REG_POLL_INTERVAL_US,
					    HDA_DSP_RESET_TIMEOUT_US);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: %s: timeout on HDA_DSP_REG_ADSPCS read\n",
			__func__);
		return ret;
	}

	/* did core power up ? */
	adspcs = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
				  HDA_DSP_REG_ADSPCS);
	if ((adspcs & HDA_DSP_ADSPCS_CPA_MASK(core_mask)) !=
		HDA_DSP_ADSPCS_CPA_MASK(core_mask)) {
		dev_err(sdev->dev,
			"error: power up core failed core_mask %xadspcs 0x%x\n",
			core_mask, adspcs);
		ret = -EIO;
	}

	return ret;
}
EXPORT_SYMBOL_NS(hda_dsp_core_power_up, SND_SOC_SOF_INTEL_HDA_COMMON);

static int hda_dsp_core_power_down(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	u32 adspcs;
	int ret;

	/* update bits */
	snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR,
					 HDA_DSP_REG_ADSPCS,
					 HDA_DSP_ADSPCS_SPA_MASK(core_mask), 0);

	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR,
				HDA_DSP_REG_ADSPCS, adspcs,
				!(adspcs & HDA_DSP_ADSPCS_CPA_MASK(core_mask)),
				HDA_DSP_REG_POLL_INTERVAL_US,
				HDA_DSP_PD_TIMEOUT * USEC_PER_MSEC);
	if (ret < 0)
		dev_err(sdev->dev,
			"error: %s: timeout on HDA_DSP_REG_ADSPCS read\n",
			__func__);

	return ret;
}

int hda_dsp_enable_core(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;
	int ret;

	/* restrict core_mask to host managed cores mask */
	core_mask &= chip->host_managed_cores_mask;

	/* return if core_mask is not valid or cores are already enabled */
	if (!core_mask || hda_dsp_core_is_enabled(sdev, core_mask))
		return 0;

	/* power up */
	ret = hda_dsp_core_power_up(sdev, core_mask);
	if (ret < 0) {
		dev_err(sdev->dev, "error: dsp core power up failed: core_mask %x\n",
			core_mask);
		return ret;
	}

	return hda_dsp_core_run(sdev, core_mask);
}
EXPORT_SYMBOL_NS(hda_dsp_enable_core, SND_SOC_SOF_INTEL_HDA_COMMON);

int hda_dsp_core_reset_power_down(struct snd_sof_dev *sdev,
				  unsigned int core_mask)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;
	int ret;

	/* restrict core_mask to host managed cores mask */
	core_mask &= chip->host_managed_cores_mask;

	/* return if core_mask is not valid */
	if (!core_mask)
		return 0;

	/* place core in reset prior to power down */
	ret = hda_dsp_core_stall_reset(sdev, core_mask);
	if (ret < 0) {
		dev_err(sdev->dev, "error: dsp core reset failed: core_mask %x\n",
			core_mask);
		return ret;
	}

	/* power down core */
	ret = hda_dsp_core_power_down(sdev, core_mask);
	if (ret < 0) {
		dev_err(sdev->dev, "error: dsp core power down fail mask %x: %d\n",
			core_mask, ret);
		return ret;
	}

	/* make sure we are in OFF state */
	if (hda_dsp_core_is_enabled(sdev, core_mask)) {
		dev_err(sdev->dev, "error: dsp core disable fail mask %x: %d\n",
			core_mask, ret);
		ret = -EIO;
	}

	return ret;
}
EXPORT_SYMBOL_NS(hda_dsp_core_reset_power_down, SND_SOC_SOF_INTEL_HDA_COMMON);

void hda_dsp_ipc_int_enable(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;

	if (sdev->dspless_mode_selected)
		return;

	/* enable IPC DONE and BUSY interrupts */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, chip->ipc_ctl,
			HDA_DSP_REG_HIPCCTL_DONE | HDA_DSP_REG_HIPCCTL_BUSY,
			HDA_DSP_REG_HIPCCTL_DONE | HDA_DSP_REG_HIPCCTL_BUSY);

	/* enable IPC interrupt */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPIC,
				HDA_DSP_ADSPIC_IPC, HDA_DSP_ADSPIC_IPC);
}
EXPORT_SYMBOL_NS(hda_dsp_ipc_int_enable, SND_SOC_SOF_INTEL_HDA_COMMON);

void hda_dsp_ipc_int_disable(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;

	if (sdev->dspless_mode_selected)
		return;

	/* disable IPC interrupt */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPIC,
				HDA_DSP_ADSPIC_IPC, 0);

	/* disable IPC BUSY and DONE interrupt */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, chip->ipc_ctl,
			HDA_DSP_REG_HIPCCTL_BUSY | HDA_DSP_REG_HIPCCTL_DONE, 0);
}
EXPORT_SYMBOL_NS(hda_dsp_ipc_int_disable, SND_SOC_SOF_INTEL_HDA_COMMON);

static int hda_dsp_wait_d0i3c_done(struct snd_sof_dev *sdev)
{
	int retry = HDA_DSP_REG_POLL_RETRY_COUNT;
	struct snd_sof_pdata *pdata = sdev->pdata;
	const struct sof_intel_dsp_desc *chip;

	chip = get_chip_info(pdata);
	while (snd_sof_dsp_read8(sdev, HDA_DSP_HDA_BAR, chip->d0i3_offset) &
		SOF_HDA_VS_D0I3C_CIP) {
		if (!retry--)
			return -ETIMEDOUT;
		usleep_range(10, 15);
	}

	return 0;
}

static int hda_dsp_send_pm_gate_ipc(struct snd_sof_dev *sdev, u32 flags)
{
	const struct sof_ipc_pm_ops *pm_ops = sof_ipc_get_ops(sdev, pm);

	if (pm_ops && pm_ops->set_pm_gate)
		return pm_ops->set_pm_gate(sdev, flags);

	return 0;
}

static int hda_dsp_update_d0i3c_register(struct snd_sof_dev *sdev, u8 value)
{
	struct snd_sof_pdata *pdata = sdev->pdata;
	const struct sof_intel_dsp_desc *chip;
	int ret;
	u8 reg;

	chip = get_chip_info(pdata);

	/* Write to D0I3C after Command-In-Progress bit is cleared */
	ret = hda_dsp_wait_d0i3c_done(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "CIP timeout before D0I3C update!\n");
		return ret;
	}

	/* Update D0I3C register */
	snd_sof_dsp_update8(sdev, HDA_DSP_HDA_BAR, chip->d0i3_offset,
			    SOF_HDA_VS_D0I3C_I3, value);

	/*
	 * The value written to the D0I3C::I3 bit may not be taken into account immediately.
	 * A delay is recommended before checking if D0I3C::CIP is cleared
	 */
	usleep_range(30, 40);

	/* Wait for cmd in progress to be cleared before exiting the function */
	ret = hda_dsp_wait_d0i3c_done(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "CIP timeout after D0I3C update!\n");
		return ret;
	}

	reg = snd_sof_dsp_read8(sdev, HDA_DSP_HDA_BAR, chip->d0i3_offset);
	/* Confirm d0i3 state changed with paranoia check */
	if ((reg ^ value) & SOF_HDA_VS_D0I3C_I3) {
		dev_err(sdev->dev, "failed to update D0I3C!\n");
		return -EIO;
	}

	trace_sof_intel_D0I3C_updated(sdev, reg);

	return 0;
}

/*
 * d0i3 streaming is enabled if all the active streams can
 * work in d0i3 state and playback is enabled
 */
static bool hda_dsp_d0i3_streaming_applicable(struct snd_sof_dev *sdev)
{
	struct snd_pcm_substream *substream;
	struct snd_sof_pcm *spcm;
	bool playback_active = false;
	int dir;

	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		for_each_pcm_streams(dir) {
			substream = spcm->stream[dir].substream;
			if (!substream || !substream->runtime)
				continue;

			if (!spcm->stream[dir].d0i3_compatible)
				return false;

			if (dir == SNDRV_PCM_STREAM_PLAYBACK)
				playback_active = true;
		}
	}

	return playback_active;
}

static int hda_dsp_set_D0_state(struct snd_sof_dev *sdev,
				const struct sof_dsp_power_state *target_state)
{
	u32 flags = 0;
	int ret;
	u8 value = 0;

	/*
	 * Sanity check for illegal state transitions
	 * The only allowed transitions are:
	 * 1. D3 -> D0I0
	 * 2. D0I0 -> D0I3
	 * 3. D0I3 -> D0I0
	 */
	switch (sdev->dsp_power_state.state) {
	case SOF_DSP_PM_D0:
		/* Follow the sequence below for D0 substate transitions */
		break;
	case SOF_DSP_PM_D3:
		/* Follow regular flow for D3 -> D0 transition */
		return 0;
	default:
		dev_err(sdev->dev, "error: transition from %d to %d not allowed\n",
			sdev->dsp_power_state.state, target_state->state);
		return -EINVAL;
	}

	/* Set flags and register value for D0 target substate */
	if (target_state->substate == SOF_HDA_DSP_PM_D0I3) {
		value = SOF_HDA_VS_D0I3C_I3;

		/*
		 * Trace DMA need to be disabled when the DSP enters
		 * D0I3 for S0Ix suspend, but it can be kept enabled
		 * when the DSP enters D0I3 while the system is in S0
		 * for debug purpose.
		 */
		if (!sdev->fw_trace_is_supported ||
		    !hda_enable_trace_D0I3_S0 ||
		    sdev->system_suspend_target != SOF_SUSPEND_NONE)
			flags = HDA_PM_NO_DMA_TRACE;

		if (hda_dsp_d0i3_streaming_applicable(sdev))
			flags |= HDA_PM_PG_STREAMING;
	} else {
		/* prevent power gating in D0I0 */
		flags = HDA_PM_PPG;
	}

	/* update D0I3C register */
	ret = hda_dsp_update_d0i3c_register(sdev, value);
	if (ret < 0)
		return ret;

	/*
	 * Notify the DSP of the state change.
	 * If this IPC fails, revert the D0I3C register update in order
	 * to prevent partial state change.
	 */
	ret = hda_dsp_send_pm_gate_ipc(sdev, flags);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: PM_GATE ipc error %d\n", ret);
		goto revert;
	}

	return ret;

revert:
	/* fallback to the previous register value */
	value = value ? 0 : SOF_HDA_VS_D0I3C_I3;

	/*
	 * This can fail but return the IPC error to signal that
	 * the state change failed.
	 */
	hda_dsp_update_d0i3c_register(sdev, value);

	return ret;
}

/* helper to log DSP state */
static void hda_dsp_state_log(struct snd_sof_dev *sdev)
{
	switch (sdev->dsp_power_state.state) {
	case SOF_DSP_PM_D0:
		switch (sdev->dsp_power_state.substate) {
		case SOF_HDA_DSP_PM_D0I0:
			dev_dbg(sdev->dev, "Current DSP power state: D0I0\n");
			break;
		case SOF_HDA_DSP_PM_D0I3:
			dev_dbg(sdev->dev, "Current DSP power state: D0I3\n");
			break;
		default:
			dev_dbg(sdev->dev, "Unknown DSP D0 substate: %d\n",
				sdev->dsp_power_state.substate);
			break;
		}
		break;
	case SOF_DSP_PM_D1:
		dev_dbg(sdev->dev, "Current DSP power state: D1\n");
		break;
	case SOF_DSP_PM_D2:
		dev_dbg(sdev->dev, "Current DSP power state: D2\n");
		break;
	case SOF_DSP_PM_D3:
		dev_dbg(sdev->dev, "Current DSP power state: D3\n");
		break;
	default:
		dev_dbg(sdev->dev, "Unknown DSP power state: %d\n",
			sdev->dsp_power_state.state);
		break;
	}
}

/*
 * All DSP power state transitions are initiated by the driver.
 * If the requested state change fails, the error is simply returned.
 * Further state transitions are attempted only when the set_power_save() op
 * is called again either because of a new IPC sent to the DSP or
 * during system suspend/resume.
 */
static int hda_dsp_set_power_state(struct snd_sof_dev *sdev,
				   const struct sof_dsp_power_state *target_state)
{
	int ret = 0;

	switch (target_state->state) {
	case SOF_DSP_PM_D0:
		ret = hda_dsp_set_D0_state(sdev, target_state);
		break;
	case SOF_DSP_PM_D3:
		/* The only allowed transition is: D0I0 -> D3 */
		if (sdev->dsp_power_state.state == SOF_DSP_PM_D0 &&
		    sdev->dsp_power_state.substate == SOF_HDA_DSP_PM_D0I0)
			break;

		dev_err(sdev->dev,
			"error: transition from %d to %d not allowed\n",
			sdev->dsp_power_state.state, target_state->state);
		return -EINVAL;
	default:
		dev_err(sdev->dev, "error: target state unsupported %d\n",
			target_state->state);
		return -EINVAL;
	}
	if (ret < 0) {
		dev_err(sdev->dev,
			"failed to set requested target DSP state %d substate %d\n",
			target_state->state, target_state->substate);
		return ret;
	}

	sdev->dsp_power_state = *target_state;
	hda_dsp_state_log(sdev);
	return ret;
}

int hda_dsp_set_power_state_ipc3(struct snd_sof_dev *sdev,
				 const struct sof_dsp_power_state *target_state)
{
	/*
	 * When the DSP is already in D0I3 and the target state is D0I3,
	 * it could be the case that the DSP is in D0I3 during S0
	 * and the system is suspending to S0Ix. Therefore,
	 * hda_dsp_set_D0_state() must be called to disable trace DMA
	 * by sending the PM_GATE IPC to the FW.
	 */
	if (target_state->substate == SOF_HDA_DSP_PM_D0I3 &&
	    sdev->system_suspend_target == SOF_SUSPEND_S0IX)
		return hda_dsp_set_power_state(sdev, target_state);

	/*
	 * For all other cases, return without doing anything if
	 * the DSP is already in the target state.
	 */
	if (target_state->state == sdev->dsp_power_state.state &&
	    target_state->substate == sdev->dsp_power_state.substate)
		return 0;

	return hda_dsp_set_power_state(sdev, target_state);
}
EXPORT_SYMBOL_NS(hda_dsp_set_power_state_ipc3, SND_SOC_SOF_INTEL_HDA_COMMON);

int hda_dsp_set_power_state_ipc4(struct snd_sof_dev *sdev,
				 const struct sof_dsp_power_state *target_state)
{
	/* Return without doing anything if the DSP is already in the target state */
	if (target_state->state == sdev->dsp_power_state.state &&
	    target_state->substate == sdev->dsp_power_state.substate)
		return 0;

	return hda_dsp_set_power_state(sdev, target_state);
}
EXPORT_SYMBOL_NS(hda_dsp_set_power_state_ipc4, SND_SOC_SOF_INTEL_HDA_COMMON);

/*
 * Audio DSP states may transform as below:-
 *
 *                                         Opportunistic D0I3 in S0
 *     Runtime    +---------------------+  Delayed D0i3 work timeout
 *     suspend    |                     +--------------------+
 *   +------------+       D0I0(active)  |                    |
 *   |            |                     <---------------+    |
 *   |   +-------->                     |    New IPC	|    |
 *   |   |Runtime +--^--+---------^--+--+ (via mailbox)	|    |
 *   |   |resume     |  |         |  |			|    |
 *   |   |           |  |         |  |			|    |
 *   |   |     System|  |         |  |			|    |
 *   |   |     resume|  | S3/S0IX |  |                  |    |
 *   |   |	     |  | suspend |  | S0IX             |    |
 *   |   |           |  |         |  |suspend           |    |
 *   |   |           |  |         |  |                  |    |
 *   |   |           |  |         |  |                  |    |
 * +-v---+-----------+--v-------+ |  |           +------+----v----+
 * |                            | |  +----------->                |
 * |       D3 (suspended)       | |              |      D0I3      |
 * |                            | +--------------+                |
 * |                            |  System resume |                |
 * +----------------------------+		 +----------------+
 *
 * S0IX suspend: The DSP is in D0I3 if any D0I3-compatible streams
 *		 ignored the suspend trigger. Otherwise the DSP
 *		 is in D3.
 */

static int hda_suspend(struct snd_sof_dev *sdev, bool runtime_suspend)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;
	struct hdac_bus *bus = sof_to_bus(sdev);
	bool imr_lost = false;
	int ret, j;

	/*
	 * The memory used for IMR boot loses its content in deeper than S3
	 * state on CAVS platforms.
	 * On ACE platforms due to the system architecture the IMR content is
	 * lost at S3 state already, they are tailored for s2idle use.
	 * We must not try IMR boot on next power up in these cases as it will
	 * fail.
	 */
	if (sdev->system_suspend_target > SOF_SUSPEND_S3 ||
	    (chip->hw_ip_version >= SOF_INTEL_ACE_1_0 &&
	     sdev->system_suspend_target == SOF_SUSPEND_S3))
		imr_lost = true;

	/*
	 * In case of firmware crash or boot failure set the skip_imr_boot to true
	 * as well in order to try to re-load the firmware to do a 'cold' boot.
	 */
	if (imr_lost || sdev->fw_state == SOF_FW_CRASHED ||
	    sdev->fw_state == SOF_FW_BOOT_FAILED)
		hda->skip_imr_boot = true;

	ret = chip->disable_interrupts(sdev);
	if (ret < 0)
		return ret;

	/* make sure that no irq handler is pending before shutdown */
	synchronize_irq(sdev->ipc_irq);

	hda_codec_jack_wake_enable(sdev, runtime_suspend);

	/* power down all hda links */
	hda_bus_ml_suspend(bus);

	if (sdev->dspless_mode_selected)
		goto skip_dsp;

	ret = chip->power_down_dsp(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to power down DSP during suspend\n");
		return ret;
	}

	/* reset ref counts for all cores */
	for (j = 0; j < chip->cores_num; j++)
		sdev->dsp_core_ref_count[j] = 0;

	/* disable ppcap interrupt */
	hda_dsp_ctrl_ppcap_enable(sdev, false);
	hda_dsp_ctrl_ppcap_int_enable(sdev, false);
skip_dsp:

	/* disable hda bus irq and streams */
	hda_dsp_ctrl_stop_chip(sdev);

	/* disable LP retention mode */
	snd_sof_pci_update_bits(sdev, PCI_PGCTL,
				PCI_PGCTL_LSRMD_MASK, PCI_PGCTL_LSRMD_MASK);

	/* reset controller */
	ret = hda_dsp_ctrl_link_reset(sdev, true);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to reset controller during suspend\n");
		return ret;
	}

	/* display codec can powered off after link reset */
	hda_codec_i915_display_power(sdev, false);

	return 0;
}

static int hda_resume(struct snd_sof_dev *sdev, bool runtime_resume)
{
	const struct sof_intel_dsp_desc *chip;
	int ret;

	/* display codec must be powered before link reset */
	hda_codec_i915_display_power(sdev, true);

	/*
	 * clear TCSEL to clear playback on some HD Audio
	 * codecs. PCI TCSEL is defined in the Intel manuals.
	 */
	snd_sof_pci_update_bits(sdev, PCI_TCSEL, 0x07, 0);

	/* reset and start hda controller */
	ret = hda_dsp_ctrl_init_chip(sdev);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to start controller after resume\n");
		goto cleanup;
	}

	/* check jack status */
	if (runtime_resume) {
		hda_codec_jack_wake_enable(sdev, false);
		if (sdev->system_suspend_target == SOF_SUSPEND_NONE)
			hda_codec_jack_check(sdev);
	}

	if (!sdev->dspless_mode_selected) {
		/* enable ppcap interrupt */
		hda_dsp_ctrl_ppcap_enable(sdev, true);
		hda_dsp_ctrl_ppcap_int_enable(sdev, true);
	}

	chip = get_chip_info(sdev->pdata);
	if (chip && chip->hw_ip_version >= SOF_INTEL_ACE_2_0)
		hda_sdw_int_enable(sdev, true);

cleanup:
	/* display codec can powered off after controller init */
	hda_codec_i915_display_power(sdev, false);

	return 0;
}

int hda_dsp_resume(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	const struct sof_dsp_power_state target_state = {
		.state = SOF_DSP_PM_D0,
		.substate = SOF_HDA_DSP_PM_D0I0,
	};
	int ret;

	/* resume from D0I3 */
	if (sdev->dsp_power_state.state == SOF_DSP_PM_D0) {
		ret = hda_bus_ml_resume(bus);
		if (ret < 0) {
			dev_err(sdev->dev,
				"error %d in %s: failed to power up links",
				ret, __func__);
			return ret;
		}

		/* set up CORB/RIRB buffers if was on before suspend */
		hda_codec_resume_cmd_io(sdev);

		/* Set DSP power state */
		ret = snd_sof_dsp_set_power_state(sdev, &target_state);
		if (ret < 0) {
			dev_err(sdev->dev, "error: setting dsp state %d substate %d\n",
				target_state.state, target_state.substate);
			return ret;
		}

		/* restore L1SEN bit */
		if (hda->l1_disabled)
			snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
						HDA_VS_INTEL_EM2,
						HDA_VS_INTEL_EM2_L1SEN, 0);

		/* restore and disable the system wakeup */
		pci_restore_state(pci);
		disable_irq_wake(pci->irq);
		return 0;
	}

	/* init hda controller. DSP cores will be powered up during fw boot */
	ret = hda_resume(sdev, false);
	if (ret < 0)
		return ret;

	return snd_sof_dsp_set_power_state(sdev, &target_state);
}
EXPORT_SYMBOL_NS(hda_dsp_resume, SND_SOC_SOF_INTEL_HDA_COMMON);

int hda_dsp_runtime_resume(struct snd_sof_dev *sdev)
{
	const struct sof_dsp_power_state target_state = {
		.state = SOF_DSP_PM_D0,
	};
	int ret;

	/* init hda controller. DSP cores will be powered up during fw boot */
	ret = hda_resume(sdev, true);
	if (ret < 0)
		return ret;

	return snd_sof_dsp_set_power_state(sdev, &target_state);
}
EXPORT_SYMBOL_NS(hda_dsp_runtime_resume, SND_SOC_SOF_INTEL_HDA_COMMON);

int hda_dsp_runtime_idle(struct snd_sof_dev *sdev)
{
	struct hdac_bus *hbus = sof_to_bus(sdev);

	if (hbus->codec_powered) {
		dev_dbg(sdev->dev, "some codecs still powered (%08X), not idle\n",
			(unsigned int)hbus->codec_powered);
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL_NS(hda_dsp_runtime_idle, SND_SOC_SOF_INTEL_HDA_COMMON);

int hda_dsp_runtime_suspend(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_dsp_power_state target_state = {
		.state = SOF_DSP_PM_D3,
	};
	int ret;

	if (!sdev->dspless_mode_selected) {
		/* cancel any attempt for DSP D0I3 */
		cancel_delayed_work_sync(&hda->d0i3_work);
	}

	/* stop hda controller and power dsp off */
	ret = hda_suspend(sdev, true);
	if (ret < 0)
		return ret;

	return snd_sof_dsp_set_power_state(sdev, &target_state);
}
EXPORT_SYMBOL_NS(hda_dsp_runtime_suspend, SND_SOC_SOF_INTEL_HDA_COMMON);

int hda_dsp_suspend(struct snd_sof_dev *sdev, u32 target_state)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	const struct sof_dsp_power_state target_dsp_state = {
		.state = target_state,
		.substate = target_state == SOF_DSP_PM_D0 ?
				SOF_HDA_DSP_PM_D0I3 : 0,
	};
	int ret;

	if (!sdev->dspless_mode_selected) {
		/* cancel any attempt for DSP D0I3 */
		cancel_delayed_work_sync(&hda->d0i3_work);
	}

	if (target_state == SOF_DSP_PM_D0) {
		/* Set DSP power state */
		ret = snd_sof_dsp_set_power_state(sdev, &target_dsp_state);
		if (ret < 0) {
			dev_err(sdev->dev, "error: setting dsp state %d substate %d\n",
				target_dsp_state.state,
				target_dsp_state.substate);
			return ret;
		}

		/* enable L1SEN to make sure the system can enter S0Ix */
		if (hda->l1_disabled)
			snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, HDA_VS_INTEL_EM2,
						HDA_VS_INTEL_EM2_L1SEN, HDA_VS_INTEL_EM2_L1SEN);

		/* stop the CORB/RIRB DMA if it is On */
		hda_codec_suspend_cmd_io(sdev);

		/* no link can be powered in s0ix state */
		ret = hda_bus_ml_suspend(bus);
		if (ret < 0) {
			dev_err(sdev->dev,
				"error %d in %s: failed to power down links",
				ret, __func__);
			return ret;
		}

		/* enable the system waking up via IPC IRQ */
		enable_irq_wake(pci->irq);
		pci_save_state(pci);
		return 0;
	}

	/* stop hda controller and power dsp off */
	ret = hda_suspend(sdev, false);
	if (ret < 0) {
		dev_err(bus->dev, "error: suspending dsp\n");
		return ret;
	}

	return snd_sof_dsp_set_power_state(sdev, &target_dsp_state);
}
EXPORT_SYMBOL_NS(hda_dsp_suspend, SND_SOC_SOF_INTEL_HDA_COMMON);

static unsigned int hda_dsp_check_for_dma_streams(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct hdac_stream *s;
	unsigned int active_streams = 0;
	int sd_offset;
	u32 val;

	list_for_each_entry(s, &bus->stream_list, list) {
		sd_offset = SOF_STREAM_SD_OFFSET(s);
		val = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR,
				       sd_offset);
		if (val & SOF_HDA_SD_CTL_DMA_START)
			active_streams |= BIT(s->index);
	}

	return active_streams;
}

static int hda_dsp_s5_quirk(struct snd_sof_dev *sdev)
{
	int ret;

	/*
	 * Do not assume a certain timing between the prior
	 * suspend flow, and running of this quirk function.
	 * This is needed if the controller was just put
	 * to reset before calling this function.
	 */
	usleep_range(500, 1000);

	/*
	 * Take controller out of reset to flush DMA
	 * transactions.
	 */
	ret = hda_dsp_ctrl_link_reset(sdev, false);
	if (ret < 0)
		return ret;

	usleep_range(500, 1000);

	/* Restore state for shutdown, back to reset */
	ret = hda_dsp_ctrl_link_reset(sdev, true);
	if (ret < 0)
		return ret;

	return ret;
}

int hda_dsp_shutdown_dma_flush(struct snd_sof_dev *sdev)
{
	unsigned int active_streams;
	int ret, ret2;

	/* check if DMA cleanup has been successful */
	active_streams = hda_dsp_check_for_dma_streams(sdev);

	sdev->system_suspend_target = SOF_SUSPEND_S3;
	ret = snd_sof_suspend(sdev->dev);

	if (active_streams) {
		dev_warn(sdev->dev,
			 "There were active DSP streams (%#x) at shutdown, trying to recover\n",
			 active_streams);
		ret2 = hda_dsp_s5_quirk(sdev);
		if (ret2 < 0)
			dev_err(sdev->dev, "shutdown recovery failed (%d)\n", ret2);
	}

	return ret;
}
EXPORT_SYMBOL_NS(hda_dsp_shutdown_dma_flush, SND_SOC_SOF_INTEL_HDA_COMMON);

int hda_dsp_shutdown(struct snd_sof_dev *sdev)
{
	sdev->system_suspend_target = SOF_SUSPEND_S3;
	return snd_sof_suspend(sdev->dev);
}
EXPORT_SYMBOL_NS(hda_dsp_shutdown, SND_SOC_SOF_INTEL_HDA_COMMON);

int hda_dsp_set_hw_params_upon_resume(struct snd_sof_dev *sdev)
{
	int ret;

	/* make sure all DAI resources are freed */
	ret = hda_dsp_dais_suspend(sdev);
	if (ret < 0)
		dev_warn(sdev->dev, "%s: failure in hda_dsp_dais_suspend\n", __func__);

	return ret;
}
EXPORT_SYMBOL_NS(hda_dsp_set_hw_params_upon_resume, SND_SOC_SOF_INTEL_HDA_COMMON);

void hda_dsp_d0i3_work(struct work_struct *work)
{
	struct sof_intel_hda_dev *hdev = container_of(work,
						      struct sof_intel_hda_dev,
						      d0i3_work.work);
	struct hdac_bus *bus = &hdev->hbus.core;
	struct snd_sof_dev *sdev = dev_get_drvdata(bus->dev);
	struct sof_dsp_power_state target_state = {
		.state = SOF_DSP_PM_D0,
		.substate = SOF_HDA_DSP_PM_D0I3,
	};
	int ret;

	/* DSP can enter D0I3 iff only D0I3-compatible streams are active */
	if (!snd_sof_dsp_only_d0i3_compatible_stream_active(sdev))
		/* remain in D0I0 */
		return;

	/* This can fail but error cannot be propagated */
	ret = snd_sof_dsp_set_power_state(sdev, &target_state);
	if (ret < 0)
		dev_err_ratelimited(sdev->dev,
				    "error: failed to set DSP state %d substate %d\n",
				    target_state.state, target_state.substate);
}
EXPORT_SYMBOL_NS(hda_dsp_d0i3_work, SND_SOC_SOF_INTEL_HDA_COMMON);

int hda_dsp_core_get(struct snd_sof_dev *sdev, int core)
{
	const struct sof_ipc_pm_ops *pm_ops = sdev->ipc->ops->pm;
	int ret, ret1;

	/* power up core */
	ret = hda_dsp_enable_core(sdev, BIT(core));
	if (ret < 0) {
		dev_err(sdev->dev, "failed to power up core %d with err: %d\n",
			core, ret);
		return ret;
	}

	/* No need to send IPC for primary core or if FW boot is not complete */
	if (sdev->fw_state != SOF_FW_BOOT_COMPLETE || core == SOF_DSP_PRIMARY_CORE)
		return 0;

	/* No need to continue the set_core_state ops is not available */
	if (!pm_ops->set_core_state)
		return 0;

	/* Now notify DSP for secondary cores */
	ret = pm_ops->set_core_state(sdev, core, true);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to enable secondary core '%d' failed with %d\n",
			core, ret);
		goto power_down;
	}

	return ret;

power_down:
	/* power down core if it is host managed and return the original error if this fails too */
	ret1 = hda_dsp_core_reset_power_down(sdev, BIT(core));
	if (ret1 < 0)
		dev_err(sdev->dev, "failed to power down core: %d with err: %d\n", core, ret1);

	return ret;
}
EXPORT_SYMBOL_NS(hda_dsp_core_get, SND_SOC_SOF_INTEL_HDA_COMMON);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL_SOUNDWIRE)
void hda_common_enable_sdw_irq(struct snd_sof_dev *sdev, bool enable)
{
	struct sof_intel_hda_dev *hdev;

	hdev = sdev->pdata->hw_pdata;

	if (!hdev->sdw)
		return;

	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPIC2,
				HDA_DSP_REG_ADSPIC2_SNDW,
				enable ? HDA_DSP_REG_ADSPIC2_SNDW : 0);
}
EXPORT_SYMBOL_NS(hda_common_enable_sdw_irq, SND_SOC_SOF_INTEL_HDA_COMMON);

void hda_sdw_int_enable(struct snd_sof_dev *sdev, bool enable)
{
	u32 interface_mask = hda_get_interface_mask(sdev);
	const struct sof_intel_dsp_desc *chip;

	if (!(interface_mask & BIT(SOF_DAI_INTEL_ALH)))
		return;

	chip = get_chip_info(sdev->pdata);
	if (chip && chip->enable_sdw_irq)
		chip->enable_sdw_irq(sdev, enable);
}
EXPORT_SYMBOL_NS(hda_sdw_int_enable, SND_SOC_SOF_INTEL_HDA_COMMON);

int hda_sdw_check_lcount_common(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hdev;
	struct sdw_intel_ctx *ctx;
	u32 caps;

	hdev = sdev->pdata->hw_pdata;
	ctx = hdev->sdw;

	caps = snd_sof_dsp_read(sdev, HDA_DSP_BAR, ctx->shim_base + SDW_SHIM_LCAP);
	caps &= SDW_SHIM_LCAP_LCOUNT_MASK;

	/* Check HW supported vs property value */
	if (caps < ctx->count) {
		dev_err(sdev->dev,
			"%s: BIOS master count %d is larger than hardware capabilities %d\n",
			__func__, ctx->count, caps);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_NS(hda_sdw_check_lcount_common, SND_SOC_SOF_INTEL_HDA_COMMON);

int hda_sdw_check_lcount_ext(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hdev;
	struct sdw_intel_ctx *ctx;
	struct hdac_bus *bus;
	u32 slcount;

	bus = sof_to_bus(sdev);

	hdev = sdev->pdata->hw_pdata;
	ctx = hdev->sdw;

	slcount = hdac_bus_eml_get_count(bus, true, AZX_REG_ML_LEPTR_ID_SDW);

	/* Check HW supported vs property value */
	if (slcount < ctx->count) {
		dev_err(sdev->dev,
			"%s: BIOS master count %d is larger than hardware capabilities %d\n",
			__func__, ctx->count, slcount);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_NS(hda_sdw_check_lcount_ext, SND_SOC_SOF_INTEL_HDA_COMMON);

int hda_sdw_check_lcount(struct snd_sof_dev *sdev)
{
	const struct sof_intel_dsp_desc *chip;

	chip = get_chip_info(sdev->pdata);
	if (chip && chip->read_sdw_lcount)
		return chip->read_sdw_lcount(sdev);

	return 0;
}
EXPORT_SYMBOL_NS(hda_sdw_check_lcount, SND_SOC_SOF_INTEL_HDA_COMMON);

void hda_sdw_process_wakeen(struct snd_sof_dev *sdev)
{
	u32 interface_mask = hda_get_interface_mask(sdev);
	const struct sof_intel_dsp_desc *chip;

	if (!(interface_mask & BIT(SOF_DAI_INTEL_ALH)))
		return;

	chip = get_chip_info(sdev->pdata);
	if (chip && chip->sdw_process_wakeen)
		chip->sdw_process_wakeen(sdev);
}
EXPORT_SYMBOL_NS(hda_sdw_process_wakeen, SND_SOC_SOF_INTEL_HDA_COMMON);

#endif

int hda_dsp_disable_interrupts(struct snd_sof_dev *sdev)
{
	hda_sdw_int_enable(sdev, false);
	hda_dsp_ipc_int_disable(sdev);

	return 0;
}
EXPORT_SYMBOL_NS(hda_dsp_disable_interrupts, SND_SOC_SOF_INTEL_HDA_COMMON);

static const struct hda_dsp_msg_code hda_dsp_rom_fw_error_texts[] = {
	{HDA_DSP_ROM_CSE_ERROR, "error: cse error"},
	{HDA_DSP_ROM_CSE_WRONG_RESPONSE, "error: cse wrong response"},
	{HDA_DSP_ROM_IMR_TO_SMALL, "error: IMR too small"},
	{HDA_DSP_ROM_BASE_FW_NOT_FOUND, "error: base fw not found"},
	{HDA_DSP_ROM_CSE_VALIDATION_FAILED, "error: signature verification failed"},
	{HDA_DSP_ROM_IPC_FATAL_ERROR, "error: ipc fatal error"},
	{HDA_DSP_ROM_L2_CACHE_ERROR, "error: L2 cache error"},
	{HDA_DSP_ROM_LOAD_OFFSET_TO_SMALL, "error: load offset too small"},
	{HDA_DSP_ROM_API_PTR_INVALID, "error: API ptr invalid"},
	{HDA_DSP_ROM_BASEFW_INCOMPAT, "error: base fw incompatible"},
	{HDA_DSP_ROM_UNHANDLED_INTERRUPT, "error: unhandled interrupt"},
	{HDA_DSP_ROM_MEMORY_HOLE_ECC, "error: ECC memory hole"},
	{HDA_DSP_ROM_KERNEL_EXCEPTION, "error: kernel exception"},
	{HDA_DSP_ROM_USER_EXCEPTION, "error: user exception"},
	{HDA_DSP_ROM_UNEXPECTED_RESET, "error: unexpected reset"},
	{HDA_DSP_ROM_NULL_FW_ENTRY,	"error: null FW entry point"},
};

#define FSR_ROM_STATE_ENTRY(state)	{FSR_STATE_ROM_##state, #state}
static const struct hda_dsp_msg_code cavs_fsr_rom_state_names[] = {
	FSR_ROM_STATE_ENTRY(INIT),
	FSR_ROM_STATE_ENTRY(INIT_DONE),
	FSR_ROM_STATE_ENTRY(CSE_MANIFEST_LOADED),
	FSR_ROM_STATE_ENTRY(FW_MANIFEST_LOADED),
	FSR_ROM_STATE_ENTRY(FW_FW_LOADED),
	FSR_ROM_STATE_ENTRY(FW_ENTERED),
	FSR_ROM_STATE_ENTRY(VERIFY_FEATURE_MASK),
	FSR_ROM_STATE_ENTRY(GET_LOAD_OFFSET),
	FSR_ROM_STATE_ENTRY(FETCH_ROM_EXT),
	FSR_ROM_STATE_ENTRY(FETCH_ROM_EXT_DONE),
	/* CSE states */
	FSR_ROM_STATE_ENTRY(CSE_IMR_REQUEST),
	FSR_ROM_STATE_ENTRY(CSE_IMR_GRANTED),
	FSR_ROM_STATE_ENTRY(CSE_VALIDATE_IMAGE_REQUEST),
	FSR_ROM_STATE_ENTRY(CSE_IMAGE_VALIDATED),
	FSR_ROM_STATE_ENTRY(CSE_IPC_IFACE_INIT),
	FSR_ROM_STATE_ENTRY(CSE_IPC_RESET_PHASE_1),
	FSR_ROM_STATE_ENTRY(CSE_IPC_OPERATIONAL_ENTRY),
	FSR_ROM_STATE_ENTRY(CSE_IPC_OPERATIONAL),
	FSR_ROM_STATE_ENTRY(CSE_IPC_DOWN),
};

static const struct hda_dsp_msg_code ace_fsr_rom_state_names[] = {
	FSR_ROM_STATE_ENTRY(INIT),
	FSR_ROM_STATE_ENTRY(INIT_DONE),
	FSR_ROM_STATE_ENTRY(CSE_MANIFEST_LOADED),
	FSR_ROM_STATE_ENTRY(FW_MANIFEST_LOADED),
	FSR_ROM_STATE_ENTRY(FW_FW_LOADED),
	FSR_ROM_STATE_ENTRY(FW_ENTERED),
	FSR_ROM_STATE_ENTRY(VERIFY_FEATURE_MASK),
	FSR_ROM_STATE_ENTRY(GET_LOAD_OFFSET),
	FSR_ROM_STATE_ENTRY(RESET_VECTOR_DONE),
	FSR_ROM_STATE_ENTRY(PURGE_BOOT),
	FSR_ROM_STATE_ENTRY(RESTORE_BOOT),
	FSR_ROM_STATE_ENTRY(FW_ENTRY_POINT),
	FSR_ROM_STATE_ENTRY(VALIDATE_PUB_KEY),
	FSR_ROM_STATE_ENTRY(POWER_DOWN_HPSRAM),
	FSR_ROM_STATE_ENTRY(POWER_DOWN_ULPSRAM),
	FSR_ROM_STATE_ENTRY(POWER_UP_ULPSRAM_STACK),
	FSR_ROM_STATE_ENTRY(POWER_UP_HPSRAM_DMA),
	FSR_ROM_STATE_ENTRY(BEFORE_EP_POINTER_READ),
	FSR_ROM_STATE_ENTRY(VALIDATE_MANIFEST),
	FSR_ROM_STATE_ENTRY(VALIDATE_FW_MODULE),
	FSR_ROM_STATE_ENTRY(PROTECT_IMR_REGION),
	FSR_ROM_STATE_ENTRY(PUSH_MODEL_ROUTINE),
	FSR_ROM_STATE_ENTRY(PULL_MODEL_ROUTINE),
	FSR_ROM_STATE_ENTRY(VALIDATE_PKG_DIR),
	FSR_ROM_STATE_ENTRY(VALIDATE_CPD),
	FSR_ROM_STATE_ENTRY(VALIDATE_CSS_MAN_HEADER),
	FSR_ROM_STATE_ENTRY(VALIDATE_BLOB_SVN),
	FSR_ROM_STATE_ENTRY(VERIFY_IFWI_PARTITION),
	FSR_ROM_STATE_ENTRY(REMOVE_ACCESS_CONTROL),
	FSR_ROM_STATE_ENTRY(AUTH_BYPASS),
	FSR_ROM_STATE_ENTRY(AUTH_ENABLED),
	FSR_ROM_STATE_ENTRY(INIT_DMA),
	FSR_ROM_STATE_ENTRY(PURGE_FW_ENTRY),
	FSR_ROM_STATE_ENTRY(PURGE_FW_END),
	FSR_ROM_STATE_ENTRY(CLEAN_UP_BSS_DONE),
	FSR_ROM_STATE_ENTRY(IMR_RESTORE_ENTRY),
	FSR_ROM_STATE_ENTRY(IMR_RESTORE_END),
	FSR_ROM_STATE_ENTRY(FW_MANIFEST_IN_DMA_BUFF),
	FSR_ROM_STATE_ENTRY(LOAD_CSE_MAN_TO_IMR),
	FSR_ROM_STATE_ENTRY(LOAD_FW_MAN_TO_IMR),
	FSR_ROM_STATE_ENTRY(LOAD_FW_CODE_TO_IMR),
	FSR_ROM_STATE_ENTRY(FW_LOADING_DONE),
	FSR_ROM_STATE_ENTRY(FW_CODE_LOADED),
	FSR_ROM_STATE_ENTRY(VERIFY_IMAGE_TYPE),
	FSR_ROM_STATE_ENTRY(AUTH_API_INIT),
	FSR_ROM_STATE_ENTRY(AUTH_API_PROC),
	FSR_ROM_STATE_ENTRY(AUTH_API_FIRST_BUSY),
	FSR_ROM_STATE_ENTRY(AUTH_API_FIRST_RESULT),
	FSR_ROM_STATE_ENTRY(AUTH_API_CLEANUP),
};

#define FSR_BRINGUP_STATE_ENTRY(state)	{FSR_STATE_BRINGUP_##state, #state}
static const struct hda_dsp_msg_code fsr_bringup_state_names[] = {
	FSR_BRINGUP_STATE_ENTRY(INIT),
	FSR_BRINGUP_STATE_ENTRY(INIT_DONE),
	FSR_BRINGUP_STATE_ENTRY(HPSRAM_LOAD),
	FSR_BRINGUP_STATE_ENTRY(UNPACK_START),
	FSR_BRINGUP_STATE_ENTRY(IMR_RESTORE),
	FSR_BRINGUP_STATE_ENTRY(FW_ENTERED),
};

#define FSR_WAIT_STATE_ENTRY(state)	{FSR_WAIT_FOR_##state, #state}
static const struct hda_dsp_msg_code fsr_wait_state_names[] = {
	FSR_WAIT_STATE_ENTRY(IPC_BUSY),
	FSR_WAIT_STATE_ENTRY(IPC_DONE),
	FSR_WAIT_STATE_ENTRY(CACHE_INVALIDATION),
	FSR_WAIT_STATE_ENTRY(LP_SRAM_OFF),
	FSR_WAIT_STATE_ENTRY(DMA_BUFFER_FULL),
	FSR_WAIT_STATE_ENTRY(CSE_CSR),
};

#define FSR_MODULE_NAME_ENTRY(mod)	[FSR_MOD_##mod] = #mod
static const char * const fsr_module_names[] = {
	FSR_MODULE_NAME_ENTRY(ROM),
	FSR_MODULE_NAME_ENTRY(ROM_BYP),
	FSR_MODULE_NAME_ENTRY(BASE_FW),
	FSR_MODULE_NAME_ENTRY(LP_BOOT),
	FSR_MODULE_NAME_ENTRY(BRNGUP),
	FSR_MODULE_NAME_ENTRY(ROM_EXT),
};

static const char *
hda_dsp_get_state_text(u32 code, const struct hda_dsp_msg_code *msg_code,
		       size_t array_size)
{
	int i;

	for (i = 0; i < array_size; i++) {
		if (code == msg_code[i].code)
			return msg_code[i].text;
	}

	return NULL;
}

void hda_dsp_get_state(struct snd_sof_dev *sdev, const char *level)
{
	const struct sof_intel_dsp_desc *chip = get_chip_info(sdev->pdata);
	const char *state_text, *error_text, *module_text;
	u32 fsr, state, wait_state, module, error_code;

	fsr = snd_sof_dsp_read(sdev, HDA_DSP_BAR, chip->rom_status_reg);
	state = FSR_TO_STATE_CODE(fsr);
	wait_state = FSR_TO_WAIT_STATE_CODE(fsr);
	module = FSR_TO_MODULE_CODE(fsr);

	if (module > FSR_MOD_ROM_EXT)
		module_text = "unknown";
	else
		module_text = fsr_module_names[module];

	if (module == FSR_MOD_BRNGUP) {
		state_text = hda_dsp_get_state_text(state, fsr_bringup_state_names,
						    ARRAY_SIZE(fsr_bringup_state_names));
	} else {
		if (chip->hw_ip_version < SOF_INTEL_ACE_1_0)
			state_text = hda_dsp_get_state_text(state,
							cavs_fsr_rom_state_names,
							ARRAY_SIZE(cavs_fsr_rom_state_names));
		else
			state_text = hda_dsp_get_state_text(state,
							ace_fsr_rom_state_names,
							ARRAY_SIZE(ace_fsr_rom_state_names));
	}

	/* not for us, must be generic sof message */
	if (!state_text) {
		dev_printk(level, sdev->dev, "%#010x: unknown ROM status value\n", fsr);
		return;
	}

	if (wait_state) {
		const char *wait_state_text;

		wait_state_text = hda_dsp_get_state_text(wait_state, fsr_wait_state_names,
							 ARRAY_SIZE(fsr_wait_state_names));
		if (!wait_state_text)
			wait_state_text = "unknown";

		dev_printk(level, sdev->dev,
			   "%#010x: module: %s, state: %s, waiting for: %s, %s\n",
			   fsr, module_text, state_text, wait_state_text,
			   fsr & FSR_HALTED ? "not running" : "running");
	} else {
		dev_printk(level, sdev->dev, "%#010x: module: %s, state: %s, %s\n",
			   fsr, module_text, state_text,
			   fsr & FSR_HALTED ? "not running" : "running");
	}

	error_code = snd_sof_dsp_read(sdev, HDA_DSP_BAR, chip->rom_status_reg + 4);
	if (!error_code)
		return;

	error_text = hda_dsp_get_state_text(error_code, hda_dsp_rom_fw_error_texts,
					    ARRAY_SIZE(hda_dsp_rom_fw_error_texts));
	if (!error_text)
		error_text = "unknown";

	if (state == FSR_STATE_FW_ENTERED)
		dev_printk(level, sdev->dev, "status code: %#x (%s)\n", error_code,
			   error_text);
	else
		dev_printk(level, sdev->dev, "error code: %#x (%s)\n", error_code,
			   error_text);
}
EXPORT_SYMBOL_NS(hda_dsp_get_state, SND_SOC_SOF_INTEL_HDA_COMMON);

static void hda_dsp_get_registers(struct snd_sof_dev *sdev,
				  struct sof_ipc_dsp_oops_xtensa *xoops,
				  struct sof_ipc_panic_info *panic_info,
				  u32 *stack, size_t stack_words)
{
	u32 offset = sdev->dsp_oops_offset;

	/* first read registers */
	sof_mailbox_read(sdev, offset, xoops, sizeof(*xoops));

	/* note: variable AR register array is not read */

	/* then get panic info */
	if (xoops->arch_hdr.totalsize > EXCEPT_MAX_HDR_SIZE) {
		dev_err(sdev->dev, "invalid header size 0x%x. FW oops is bogus\n",
			xoops->arch_hdr.totalsize);
		return;
	}
	offset += xoops->arch_hdr.totalsize;
	sof_block_read(sdev, sdev->mmio_bar, offset,
		       panic_info, sizeof(*panic_info));

	/* then get the stack */
	offset += sizeof(*panic_info);
	sof_block_read(sdev, sdev->mmio_bar, offset, stack,
		       stack_words * sizeof(u32));
}

/* dump the first 8 dwords representing the extended ROM status */
void hda_dsp_dump_ext_rom_status(struct snd_sof_dev *sdev, const char *level,
				 u32 flags)
{
	const struct sof_intel_dsp_desc *chip;
	char msg[128];
	int len = 0;
	u32 value;
	int i;

	chip = get_chip_info(sdev->pdata);
	for (i = 0; i < HDA_EXT_ROM_STATUS_SIZE; i++) {
		value = snd_sof_dsp_read(sdev, HDA_DSP_BAR, chip->rom_status_reg + i * 0x4);
		len += scnprintf(msg + len, sizeof(msg) - len, " 0x%x", value);
	}

	dev_printk(level, sdev->dev, "extended rom status: %s", msg);

}

void hda_dsp_dump(struct snd_sof_dev *sdev, u32 flags)
{
	char *level = (flags & SOF_DBG_DUMP_OPTIONAL) ? KERN_DEBUG : KERN_ERR;
	struct sof_ipc_dsp_oops_xtensa xoops;
	struct sof_ipc_panic_info panic_info;
	u32 stack[HDA_DSP_STACK_DUMP_SIZE];

	/* print ROM/FW status */
	hda_dsp_get_state(sdev, level);

	/* The firmware register dump only available with IPC3 */
	if (flags & SOF_DBG_DUMP_REGS && sdev->pdata->ipc_type == SOF_IPC_TYPE_3) {
		u32 status = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_SRAM_REG_FW_STATUS);
		u32 panic = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_SRAM_REG_FW_TRACEP);

		hda_dsp_get_registers(sdev, &xoops, &panic_info, stack,
				      HDA_DSP_STACK_DUMP_SIZE);
		sof_print_oops_and_stack(sdev, level, status, panic, &xoops,
					 &panic_info, stack, HDA_DSP_STACK_DUMP_SIZE);
	} else {
		hda_dsp_dump_ext_rom_status(sdev, level, flags);
	}
}
EXPORT_SYMBOL_NS(hda_dsp_dump, SND_SOC_SOF_INTEL_HDA_COMMON);
