// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Authors: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//	    Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//	    Jeeja KP <jeeja.kp@intel.com>
//	    Rander Wang <rander.wang@intel.com>
//          Keyon Jie <yang.jie@linux.intel.com>
//

/*
 * Hardware interface for generic Intel audio DSP HDA IP
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/pci.h>
#include <sound/hdaudio_ext.h>
#include <sound/hda_i915.h>
#include <sound/hda_register.h>
#include <sound/sof.h>
#include <sound/pcm_params.h>
#include <linux/pm_runtime.h>
#include <uapi/sound/sof-ipc.h>
#include "../sof-priv.h"
#include "../ops.h"
#include "hda.h"

/*
 * DSP Core control.
 */

int hda_dsp_core_reset_enter(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	u32 adspcs;
	int ret;

	/* set reset bits for cores */
	snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR,
					 HDA_DSP_REG_ADSPCS,
					 HDA_DSP_ADSPCS_CRST_MASK(core_mask),
					 HDA_DSP_ADSPCS_CRST_MASK(core_mask));

	/* poll with timeout to check if operation successful */
	ret = snd_sof_dsp_register_poll(sdev, HDA_DSP_BAR,
					HDA_DSP_REG_ADSPCS,
					HDA_DSP_ADSPCS_CRST_MASK(core_mask),
					HDA_DSP_ADSPCS_CRST_MASK(core_mask),
					HDA_DSP_RESET_TIMEOUT);

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

int hda_dsp_core_reset_leave(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	u32 adspcs;
	int ret;

	/* clear reset bits for cores */
	snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR,
					 HDA_DSP_REG_ADSPCS,
					 HDA_DSP_ADSPCS_CRST_MASK(core_mask),
					 0);

	/* poll with timeout to check if operation successful */
	ret = snd_sof_dsp_register_poll(sdev, HDA_DSP_BAR,
					HDA_DSP_REG_ADSPCS,
					HDA_DSP_ADSPCS_CRST_MASK(core_mask), 0,
					HDA_DSP_RESET_TIMEOUT);

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
	snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_HDA_BAR,
					 HDA_DSP_REG_ADSPCS,
					 HDA_DSP_ADSPCS_CSTALL_MASK(core_mask),
					 HDA_DSP_ADSPCS_CSTALL_MASK(core_mask));

	/* set reset state */
	return hda_dsp_core_reset_enter(sdev, core_mask);
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

int hda_dsp_core_power_up(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	u32 adspcs;
	int ret;

	/* update bits */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPCS,
				HDA_DSP_ADSPCS_SPA_MASK(core_mask),
				HDA_DSP_ADSPCS_SPA_MASK(core_mask));

	/* poll with timeout to check if operation successful */
	ret = snd_sof_dsp_register_poll(sdev, HDA_DSP_BAR,
					HDA_DSP_REG_ADSPCS,
					HDA_DSP_ADSPCS_CPA_MASK(core_mask),
					HDA_DSP_ADSPCS_CPA_MASK(core_mask),
					HDA_DSP_PU_TIMEOUT);
	if (ret < 0)
		dev_err(sdev->dev, "error: timeout on core powerup\n");

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

int hda_dsp_core_power_down(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	/* update bits */
	snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR,
					 HDA_DSP_REG_ADSPCS,
					 HDA_DSP_ADSPCS_SPA_MASK(core_mask), 0);

	/* poll with timeout to check if operation successful */
	return snd_sof_dsp_register_poll(sdev, HDA_DSP_BAR,
		HDA_DSP_REG_ADSPCS, HDA_DSP_ADSPCS_CPA_MASK(core_mask), 0,
		HDA_DSP_PD_TIMEOUT);
}

bool hda_dsp_core_is_enabled(struct snd_sof_dev *sdev,
			     unsigned int core_mask)
{
	int val;
	bool is_enable;

	val = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPCS);

	is_enable = ((val & HDA_DSP_ADSPCS_CPA_MASK(core_mask)) &&
			(val & HDA_DSP_ADSPCS_SPA_MASK(core_mask)) &&
			!(val & HDA_DSP_ADSPCS_CRST_MASK(core_mask)) &&
			!(val & HDA_DSP_ADSPCS_CSTALL_MASK(core_mask)));

	dev_dbg(sdev->dev, "DSP core(s) enabled? %d : core_mask %x\n",
		is_enable, core_mask);

	return is_enable;
}

int hda_dsp_core_stall_reset_skl(struct snd_sof_dev *sdev,
				 unsigned int core_mask)
{
	/* stall core */
	snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR,
					 HDA_DSP_REG_ADSPCS,
					 HDA_DSP_ADSPCS_CSTALL_MASK(core_mask),
					 HDA_DSP_ADSPCS_CSTALL_MASK(core_mask));

	/* set reset state */
	return hda_dsp_core_reset_enter(sdev, core_mask);
}

int hda_dsp_enable_core(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	int ret;

	/* power up */
	ret = hda_dsp_core_power_up(sdev, core_mask);
	if (ret < 0) {
		dev_err(sdev->dev, "dsp core power up failed: core_mask %x\n",
			core_mask);
		return ret;
	}

	return hda_dsp_core_run(sdev, core_mask);
}

int hda_dsp_core_reset_power_down(struct snd_sof_dev *sdev,
				  unsigned int core_mask)
{
	int ret;

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

static int hda_suspend(struct snd_sof_dev *sdev, int state)
{
	const struct sof_intel_dsp_desc *chip = sdev->hda->desc;
	struct hdac_bus *bus = sof_to_bus(sdev);
	int ret = 0;

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* power down all hda link */
	snd_hdac_ext_bus_link_power_down_all(bus);
#endif

	/* power down DSP */
	ret = hda_dsp_core_reset_power_down(sdev, chip->cores_mask);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to power down core during suspend\n");
		return ret;
	}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* disable ppcap interrupt */
	snd_hdac_ext_bus_ppcap_int_enable(bus, false);
	snd_hdac_ext_bus_ppcap_enable(bus, false);

	/* disable hda bus irw and i/o */
	snd_hdac_bus_stop_chip(bus);
#endif

	/* disable LP retention mode */
	snd_sof_pci_update_bits(sdev, PCI_TCSEL,
				PCI_CGCTL_LSRMD_MASK, PCI_CGCTL_LSRMD_MASK);

	return 0;
}

static int hda_resume(struct snd_sof_dev *sdev)
{
	const struct sof_intel_dsp_desc *chip = sdev->hda->desc;
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct hdac_ext_link *hlink = NULL;
	int ret;

	/*
	 * clear TCSEL to clear playback on some HD Audio
	 * codecs. PCI TCSEL is defined in the Intel manuals.
	 */
	snd_sof_pci_update_bits(sdev, PCI_TCSEL, 0x07, 0);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* reset and start hda controller */
	ret = hda_dsp_ctrl_init_chip(sdev, true);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to start controller after resume\n");
		return ret;
	}

	hda_dsp_ctrl_misc_clock_gating(sdev, false);

	/* Reset stream-to-link mapping */
	list_for_each_entry(hlink, &bus->hlink_list, list)
		bus->io_ops->reg_writel(0, hlink->ml_addr + AZX_REG_ML_LOSIDV);

	hda_dsp_ctrl_misc_clock_gating(sdev, true);

	/* enable ppcap interrupt */
	snd_hdac_ext_bus_ppcap_enable(bus, true);
	snd_hdac_ext_bus_ppcap_int_enable(bus, true);
#endif

	/* power up the DSP */
	ret = hda_dsp_core_power_up(sdev, chip->cores_mask);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to power up core after resume\n");
		return ret;
	}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* turn off the links that were off before suspend */
	list_for_each_entry(hlink, &bus->hlink_list, list) {
		if (!hlink->ref_count)
			snd_hdac_ext_bus_link_power_down(hlink);
	}

	/* check dma status and clean up CORB/RIRB buffers */
	if (!bus->cmd_dma_state)
		snd_hdac_bus_stop_cmd_io(bus);
#endif

	return 0;
}

int hda_dsp_resume(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	int ret;

	/* turn display power on */
	if (IS_ENABLED(CONFIG_SND_SOC_HDAC_HDMI)) {
		ret = snd_hdac_display_power(bus, true);
		if (ret < 0) {
			dev_err(bus->dev, "Cannot turn on display power on i915 after resume\n");
			return ret;
		}
	}

	/* init hda controller and power dsp up */
	return hda_resume(sdev);
}

int hda_dsp_runtime_resume(struct snd_sof_dev *sdev)
{
	/* init hda controller and power dsp up */
	return hda_resume(sdev);
}

int hda_dsp_runtime_suspend(struct snd_sof_dev *sdev, int state)
{
	/* stop hda controller and power dsp off */
	return hda_suspend(sdev, state);
}

int hda_dsp_suspend(struct snd_sof_dev *sdev, int state)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	int ret;

	/* stop hda controller and power dsp off */
	ret = hda_suspend(sdev, state);
	if (ret < 0) {
		dev_err(bus->dev, "error: suspending dsp\n");
		return ret;
	}

	/* turn display power off */
	if (IS_ENABLED(CONFIG_SND_SOC_HDAC_HDMI)) {
		ret = snd_hdac_display_power(bus, false);
		if (ret < 0) {
			dev_err(bus->dev, "Cannot turn on display power on i915 after resume\n");
			return ret;
		}
	}

	return 0;
}
