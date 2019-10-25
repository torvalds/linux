// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
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

#include <sound/hdaudio_ext.h>
#include <sound/hda_register.h>
#include "../ops.h"
#include "hda.h"

/*
 * DSP Core control.
 */

int hda_dsp_core_reset_enter(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	u32 adspcs;
	u32 reset;
	int ret;

	/* set reset bits for cores */
	reset = HDA_DSP_ADSPCS_CRST_MASK(core_mask);
	snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR,
					 HDA_DSP_REG_ADSPCS,
					 reset, reset),

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

int hda_dsp_core_reset_leave(struct snd_sof_dev *sdev, unsigned int core_mask)
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

int hda_dsp_core_power_down(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	u32 adspcs;
	int ret;

	/* update bits */
	snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR,
					 HDA_DSP_REG_ADSPCS,
					 HDA_DSP_ADSPCS_SPA_MASK(core_mask), 0);

	ret = snd_sof_dsp_read_poll_timeout(sdev, HDA_DSP_BAR,
				HDA_DSP_REG_ADSPCS, adspcs,
				!(adspcs & HDA_DSP_ADSPCS_SPA_MASK(core_mask)),
				HDA_DSP_REG_POLL_INTERVAL_US,
				HDA_DSP_PD_TIMEOUT * USEC_PER_MSEC);
	if (ret < 0)
		dev_err(sdev->dev,
			"error: %s: timeout on HDA_DSP_REG_ADSPCS read\n",
			__func__);

	return ret;
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

int hda_dsp_enable_core(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	int ret;

	/* return if core is already enabled */
	if (hda_dsp_core_is_enabled(sdev, core_mask))
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

static int hda_dsp_wait_d0i3c_done(struct snd_sof_dev *sdev, int retry)
{
	struct hdac_bus *bus = sof_to_bus(sdev);

	while (snd_hdac_chip_readb(bus, VS_D0I3C) & SOF_HDA_VS_D0I3C_CIP) {
		if (!retry--)
			return -ETIMEDOUT;
		usleep_range(10, 15);
	}

	return 0;
}

int hda_dsp_set_power_state(struct snd_sof_dev *sdev,
			    enum sof_d0_substate d0_substate)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	int retry = 50;
	int ret;
	u8 value;

	/* Write to D0I3C after Command-In-Progress bit is cleared */
	ret = hda_dsp_wait_d0i3c_done(sdev, retry);
	if (ret < 0) {
		dev_err(bus->dev, "CIP timeout before update D0I3C!\n");
		return ret;
	}

	/* Update D0I3C register */
	value = d0_substate == SOF_DSP_D0I3 ? SOF_HDA_VS_D0I3C_I3 : 0;
	snd_hdac_chip_updateb(bus, VS_D0I3C, SOF_HDA_VS_D0I3C_I3, value);

	/* Wait for cmd in progress to be cleared before exiting the function */
	retry = 50;
	ret = hda_dsp_wait_d0i3c_done(sdev, retry);
	if (ret < 0) {
		dev_err(bus->dev, "CIP timeout after D0I3C updated!\n");
		return ret;
	}

	dev_vdbg(bus->dev, "D0I3C updated, register = 0x%x\n",
		 snd_hdac_chip_readb(bus, VS_D0I3C));

	return 0;
}

static int hda_suspend(struct snd_sof_dev *sdev, bool runtime_suspend)
{
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	const struct sof_intel_dsp_desc *chip = hda->desc;
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	struct hdac_bus *bus = sof_to_bus(sdev);
#endif
	int ret;

	/* disable IPC interrupts */
	hda_dsp_ipc_int_disable(sdev);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	if (runtime_suspend)
		hda_codec_jack_wake_enable(sdev);

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

	return 0;
}

static int hda_resume(struct snd_sof_dev *sdev, bool runtime_resume)
{
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct hdac_ext_link *hlink = NULL;
#endif
	int ret;

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
		return ret;
	}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* check jack status */
	if (runtime_resume)
		hda_codec_jack_check(sdev);

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

	return 0;
}

int hda_dsp_resume(struct snd_sof_dev *sdev)
{
	/* init hda controller. DSP cores will be powered up during fw boot */
	return hda_resume(sdev, false);
}

int hda_dsp_runtime_resume(struct snd_sof_dev *sdev)
{
	/* init hda controller. DSP cores will be powered up during fw boot */
	return hda_resume(sdev, true);
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
	/* stop hda controller and power dsp off */
	return hda_suspend(sdev, true);
}

int hda_dsp_suspend(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	int ret;

	/* stop hda controller and power dsp off */
	ret = hda_suspend(sdev, false);
	if (ret < 0) {
		dev_err(bus->dev, "error: suspending dsp\n");
		return ret;
	}

	return 0;
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
			rtd = snd_pcm_substream_chip(stream->link_substream);
			name = rtd->codec_dai->component->name;
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
