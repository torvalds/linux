// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
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
#include "../sof-audio.h"
#include "../ops.h"
#include "hda.h"
#include "hda-ipc.h"

static bool hda_enable_trace_D0I3_S0;
#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG)
module_param_named(enable_trace_D0I3_S0, hda_enable_trace_D0I3_S0, bool, 0444);
MODULE_PARM_DESC(enable_trace_D0I3_S0,
		 "SOF HDA enable trace when the DSP is in D0I3 in S0");
#endif

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

static int hda_dsp_core_stall_reset(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	/* stall core */
	snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR,
					 HDA_DSP_REG_ADSPCS,
					 HDA_DSP_ADSPCS_CSTALL_MASK(core_mask),
					 HDA_DSP_ADSPCS_CSTALL_MASK(core_mask));

	/* set reset state */
	return hda_dsp_core_reset_enter(sdev, core_mask);
}

static bool hda_dsp_core_is_enabled(struct snd_sof_dev *sdev, unsigned int core_mask)
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

/*
 * Power Management.
 */

static int hda_dsp_core_power_up(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	unsigned int cpa;
	u32 adspcs;
	int ret;

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

void hda_dsp_ipc_int_enable(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;

	/* enable IPC DONE and BUSY interrupts */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, chip->ipc_ctl,
			HDA_DSP_REG_HIPCCTL_DONE | HDA_DSP_REG_HIPCCTL_BUSY,
			HDA_DSP_REG_HIPCCTL_DONE | HDA_DSP_REG_HIPCCTL_BUSY);

	/* enable IPC interrupt */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPIC,
				HDA_DSP_ADSPIC_IPC, HDA_DSP_ADSPIC_IPC);
}

void hda_dsp_ipc_int_disable(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;

	/* disable IPC interrupt */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPIC,
				HDA_DSP_ADSPIC_IPC, 0);

	/* disable IPC BUSY and DONE interrupt */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, chip->ipc_ctl,
			HDA_DSP_REG_HIPCCTL_BUSY | HDA_DSP_REG_HIPCCTL_DONE, 0);
}

static int hda_dsp_wait_d0i3c_done(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	int retry = HDA_DSP_REG_POLL_RETRY_COUNT;

	while (snd_hdac_chip_readb(bus, VS_D0I3C) & SOF_HDA_VS_D0I3C_CIP) {
		if (!retry--)
			return -ETIMEDOUT;
		usleep_range(10, 15);
	}

	return 0;
}

static int hda_dsp_send_pm_gate_ipc(struct snd_sof_dev *sdev, u32 flags)
{
	struct sof_ipc_pm_gate pm_gate;
	struct sof_ipc_reply reply;

	memset(&pm_gate, 0, sizeof(pm_gate));

	/* configure pm_gate ipc message */
	pm_gate.hdr.size = sizeof(pm_gate);
	pm_gate.hdr.cmd = SOF_IPC_GLB_PM_MSG | SOF_IPC_PM_GATE;
	pm_gate.flags = flags;

	/* send pm_gate ipc to dsp */
	return sof_ipc_tx_message_no_pm(sdev->ipc, pm_gate.hdr.cmd,
					&pm_gate, sizeof(pm_gate), &reply,
					sizeof(reply));
}

static int hda_dsp_update_d0i3c_register(struct snd_sof_dev *sdev, u8 value)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	int ret;

	/* Write to D0I3C after Command-In-Progress bit is cleared */
	ret = hda_dsp_wait_d0i3c_done(sdev);
	if (ret < 0) {
		dev_err(bus->dev, "CIP timeout before D0I3C update!\n");
		return ret;
	}

	/* Update D0I3C register */
	snd_hdac_chip_updateb(bus, VS_D0I3C, SOF_HDA_VS_D0I3C_I3, value);

	/* Wait for cmd in progress to be cleared before exiting the function */
	ret = hda_dsp_wait_d0i3c_done(sdev);
	if (ret < 0) {
		dev_err(bus->dev, "CIP timeout after D0I3C update!\n");
		return ret;
	}

	dev_vdbg(bus->dev, "D0I3C updated, register = 0x%x\n",
		 snd_hdac_chip_readb(bus, VS_D0I3C));

	return 0;
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
		if (!sdev->dtrace_is_supported ||
		    !hda_enable_trace_D0I3_S0 ||
		    sdev->system_suspend_target != SOF_SUSPEND_NONE)
			flags = HDA_PM_NO_DMA_TRACE;
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
	case SOF_DSP_PM_D3_HOT:
		dev_dbg(sdev->dev, "Current DSP power state: D3_HOT\n");
		break;
	case SOF_DSP_PM_D3:
		dev_dbg(sdev->dev, "Current DSP power state: D3\n");
		break;
	case SOF_DSP_PM_D3_COLD:
		dev_dbg(sdev->dev, "Current DSP power state: D3_COLD\n");
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
int hda_dsp_set_power_state(struct snd_sof_dev *sdev,
			    const struct sof_dsp_power_state *target_state)
{
	int ret = 0;

	/*
	 * When the DSP is already in D0I3 and the target state is D0I3,
	 * it could be the case that the DSP is in D0I3 during S0
	 * and the system is suspending to S0Ix. Therefore,
	 * hda_dsp_set_D0_state() must be called to disable trace DMA
	 * by sending the PM_GATE IPC to the FW.
	 */
	if (target_state->substate == SOF_HDA_DSP_PM_D0I3 &&
	    sdev->system_suspend_target == SOF_SUSPEND_S0IX)
		goto set_state;

	/*
	 * For all other cases, return without doing anything if
	 * the DSP is already in the target state.
	 */
	if (target_state->state == sdev->dsp_power_state.state &&
	    target_state->substate == sdev->dsp_power_state.substate)
		return 0;

set_state:
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
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	struct hdac_bus *bus = sof_to_bus(sdev);
#endif
	int ret;

	hda_sdw_int_enable(sdev, false);

	/* disable IPC interrupts */
	hda_dsp_ipc_int_disable(sdev);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	hda_codec_jack_wake_enable(sdev, runtime_suspend);

	/* power down all hda link */
	snd_hdac_ext_bus_link_power_down_all(bus);
#endif

	/* power down DSP */
	ret = snd_sof_dsp_core_power_down(sdev, chip->host_managed_cores_mask);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to power down core during suspend\n");
		return ret;
	}

	/* disable ppcap interrupt */
	hda_dsp_ctrl_ppcap_enable(sdev, false);
	hda_dsp_ctrl_ppcap_int_enable(sdev, false);

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
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct hdac_ext_link *hlink = NULL;
#endif
	int ret;

	/* display codec must be powered before link reset */
	hda_codec_i915_display_power(sdev, true);

	/*
	 * clear TCSEL to clear playback on some HD Audio
	 * codecs. PCI TCSEL is defined in the Intel manuals.
	 */
	snd_sof_pci_update_bits(sdev, PCI_TCSEL, 0x07, 0);

	/* reset and start hda controller */
	ret = hda_dsp_ctrl_init_chip(sdev, true);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to start controller after resume\n");
		goto cleanup;
	}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* check jack status */
	if (runtime_resume) {
		hda_codec_jack_wake_enable(sdev, false);
		if (sdev->system_suspend_target == SOF_SUSPEND_NONE)
			hda_codec_jack_check(sdev);
	}

	/* turn off the links that were off before suspend */
	list_for_each_entry(hlink, &bus->hlink_list, list) {
		if (!hlink->ref_count)
			snd_hdac_ext_bus_link_power_down(hlink);
	}

	/* check dma status and clean up CORB/RIRB buffers */
	if (!bus->cmd_dma_state)
		snd_hdac_bus_stop_cmd_io(bus);
#endif

	/* enable ppcap interrupt */
	hda_dsp_ctrl_ppcap_enable(sdev, true);
	hda_dsp_ctrl_ppcap_int_enable(sdev, true);

cleanup:
	/* display codec can powered off after controller init */
	hda_codec_i915_display_power(sdev, false);

	return 0;
}

int hda_dsp_resume(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	const struct sof_dsp_power_state target_state = {
		.state = SOF_DSP_PM_D0,
		.substate = SOF_HDA_DSP_PM_D0I0,
	};
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct hdac_ext_link *hlink = NULL;
#endif
	int ret;

	/* resume from D0I3 */
	if (sdev->dsp_power_state.state == SOF_DSP_PM_D0) {
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
		/* power up links that were active before suspend */
		list_for_each_entry(hlink, &bus->hlink_list, list) {
			if (hlink->ref_count) {
				ret = snd_hdac_ext_bus_link_power_up(hlink);
				if (ret < 0) {
					dev_dbg(sdev->dev,
						"error %d in %s: failed to power up links",
						ret, __func__);
					return ret;
				}
			}
		}

		/* set up CORB/RIRB buffers if was on before suspend */
		if (bus->cmd_dma_state)
			snd_hdac_bus_init_cmd_io(bus);
#endif

		/* Set DSP power state */
		ret = snd_sof_dsp_set_power_state(sdev, &target_state);
		if (ret < 0) {
			dev_err(sdev->dev, "error: setting dsp state %d substate %d\n",
				target_state.state, target_state.substate);
			return ret;
		}

		/* restore L1SEN bit */
		if (hda->l1_support_changed)
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

int hda_dsp_runtime_suspend(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_dsp_power_state target_state = {
		.state = SOF_DSP_PM_D3,
	};
	int ret;

	/* cancel any attempt for DSP D0I3 */
	cancel_delayed_work_sync(&hda->d0i3_work);

	/* stop hda controller and power dsp off */
	ret = hda_suspend(sdev, true);
	if (ret < 0)
		return ret;

	return snd_sof_dsp_set_power_state(sdev, &target_state);
}

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

	/* cancel any attempt for DSP D0I3 */
	cancel_delayed_work_sync(&hda->d0i3_work);

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
		hda->l1_support_changed =
			snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
						HDA_VS_INTEL_EM2,
						HDA_VS_INTEL_EM2_L1SEN,
						HDA_VS_INTEL_EM2_L1SEN);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
		/* stop the CORB/RIRB DMA if it is On */
		if (bus->cmd_dma_state)
			snd_hdac_bus_stop_cmd_io(bus);

		/* no link can be powered in s0ix state */
		ret = snd_hdac_ext_bus_link_power_down_all(bus);
		if (ret < 0) {
			dev_dbg(sdev->dev,
				"error %d in %s: failed to power down links",
				ret, __func__);
			return ret;
		}
#endif

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

int hda_dsp_shutdown(struct snd_sof_dev *sdev)
{
	sdev->system_suspend_target = SOF_SUSPEND_S3;
	return snd_sof_suspend(sdev->dev);
}

int hda_dsp_set_hw_params_upon_resume(struct snd_sof_dev *sdev)
{
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct snd_soc_pcm_runtime *rtd;
	struct hdac_ext_stream *stream;
	struct hdac_ext_link *link;
	struct hdac_stream *s;
	const char *name;
	int stream_tag;

	/* set internal flag for BE */
	list_for_each_entry(s, &bus->stream_list, list) {
		stream = stream_to_hdac_ext_stream(s);

		/*
		 * clear stream. This should already be taken care for running
		 * streams when the SUSPEND trigger is called. But paused
		 * streams do not get suspended, so this needs to be done
		 * explicitly during suspend.
		 */
		if (stream->link_substream) {
			rtd = asoc_substream_to_rtd(stream->link_substream);
			name = asoc_rtd_to_codec(rtd, 0)->component->name;
			link = snd_hdac_ext_bus_get_link(bus, name);
			if (!link)
				return -EINVAL;

			stream->link_prepared = 0;

			if (hdac_stream(stream)->direction ==
				SNDRV_PCM_STREAM_CAPTURE)
				continue;

			stream_tag = hdac_stream(stream)->stream_tag;
			snd_hdac_ext_link_clear_stream_id(link, stream_tag);
		}
	}
#endif
	return 0;
}

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

int hda_dsp_core_get(struct snd_sof_dev *sdev, int core)
{
	struct sof_ipc_pm_core_config pm_core_config = {
		.hdr = {
			.cmd = SOF_IPC_GLB_PM_MSG | SOF_IPC_PM_CORE_ENABLE,
			.size = sizeof(pm_core_config),
		},
		.enable_mask = sdev->enabled_cores_mask | BIT(core),
	};
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

	/* Now notify DSP for secondary cores */
	ret = sof_ipc_tx_message(sdev->ipc, pm_core_config.hdr.cmd,
				 &pm_core_config, sizeof(pm_core_config),
				 &pm_core_config, sizeof(pm_core_config));
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
