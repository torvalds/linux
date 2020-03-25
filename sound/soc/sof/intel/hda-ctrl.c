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

#include <linux/module.h>
#include <sound/hdaudio_ext.h>
#include <sound/hda_register.h>
#include <sound/hda_component.h>
#include "../ops.h"
#include "hda.h"

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
static int hda_codec_mask = -1;
module_param_named(codec_mask, hda_codec_mask, int, 0444);
MODULE_PARM_DESC(codec_mask, "SOF HDA codec mask for probing");
#endif

/*
 * HDA Operations.
 */

int hda_dsp_ctrl_link_reset(struct snd_sof_dev *sdev, bool reset)
{
	unsigned long timeout;
	u32 gctl = 0;
	u32 val;

	/* 0 to enter reset and 1 to exit reset */
	val = reset ? 0 : SOF_HDA_GCTL_RESET;

	/* enter/exit HDA controller reset */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, SOF_HDA_GCTL,
				SOF_HDA_GCTL_RESET, val);

	/* wait to enter/exit reset */
	timeout = jiffies + msecs_to_jiffies(HDA_DSP_CTRL_RESET_TIMEOUT);
	while (time_before(jiffies, timeout)) {
		gctl = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, SOF_HDA_GCTL);
		if ((gctl & SOF_HDA_GCTL_RESET) == val)
			return 0;
		usleep_range(500, 1000);
	}

	/* enter/exit reset failed */
	dev_err(sdev->dev, "error: failed to %s HDA controller gctl 0x%x\n",
		reset ? "reset" : "ready", gctl);
	return -EIO;
}

int hda_dsp_ctrl_get_caps(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	u32 cap, offset, feature;
	int count = 0;

	offset = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, SOF_HDA_LLCH);

	do {
		cap = snd_sof_dsp_read(sdev, HDA_DSP_HDA_BAR, offset);

		dev_dbg(sdev->dev, "checking for capabilities at offset 0x%x\n",
			offset & SOF_HDA_CAP_NEXT_MASK);

		feature = (cap & SOF_HDA_CAP_ID_MASK) >> SOF_HDA_CAP_ID_OFF;

		switch (feature) {
		case SOF_HDA_PP_CAP_ID:
			dev_dbg(sdev->dev, "found DSP capability at 0x%x\n",
				offset);
			bus->ppcap = bus->remap_addr + offset;
			sdev->bar[HDA_DSP_PP_BAR] = bus->ppcap;
			break;
		case SOF_HDA_SPIB_CAP_ID:
			dev_dbg(sdev->dev, "found SPIB capability at 0x%x\n",
				offset);
			bus->spbcap = bus->remap_addr + offset;
			sdev->bar[HDA_DSP_SPIB_BAR] = bus->spbcap;
			break;
		case SOF_HDA_DRSM_CAP_ID:
			dev_dbg(sdev->dev, "found DRSM capability at 0x%x\n",
				offset);
			bus->drsmcap = bus->remap_addr + offset;
			sdev->bar[HDA_DSP_DRSM_BAR] = bus->drsmcap;
			break;
		case SOF_HDA_GTS_CAP_ID:
			dev_dbg(sdev->dev, "found GTS capability at 0x%x\n",
				offset);
			bus->gtscap = bus->remap_addr + offset;
			break;
		case SOF_HDA_ML_CAP_ID:
			dev_dbg(sdev->dev, "found ML capability at 0x%x\n",
				offset);
			bus->mlcap = bus->remap_addr + offset;
			break;
		default:
			dev_vdbg(sdev->dev, "found capability %d at 0x%x\n",
				 feature, offset);
			break;
		}

		offset = cap & SOF_HDA_CAP_NEXT_MASK;
	} while (count++ <= SOF_HDA_MAX_CAPS && offset);

	return 0;
}

void hda_dsp_ctrl_ppcap_enable(struct snd_sof_dev *sdev, bool enable)
{
	u32 val = enable ? SOF_HDA_PPCTL_GPROCEN : 0;

	snd_sof_dsp_update_bits(sdev, HDA_DSP_PP_BAR, SOF_HDA_REG_PP_PPCTL,
				SOF_HDA_PPCTL_GPROCEN, val);
}

void hda_dsp_ctrl_ppcap_int_enable(struct snd_sof_dev *sdev, bool enable)
{
	u32 val	= enable ? SOF_HDA_PPCTL_PIE : 0;

	snd_sof_dsp_update_bits(sdev, HDA_DSP_PP_BAR, SOF_HDA_REG_PP_PPCTL,
				SOF_HDA_PPCTL_PIE, val);
}

void hda_dsp_ctrl_misc_clock_gating(struct snd_sof_dev *sdev, bool enable)
{
	u32 val = enable ? PCI_CGCTL_MISCBDCGE_MASK : 0;

	snd_sof_pci_update_bits(sdev, PCI_CGCTL, PCI_CGCTL_MISCBDCGE_MASK, val);
}

/*
 * enable/disable audio dsp clock gating and power gating bits.
 * This allows the HW to opportunistically power and clock gate
 * the audio dsp when it is idle
 */
int hda_dsp_ctrl_clock_power_gating(struct snd_sof_dev *sdev, bool enable)
{
	u32 val;

	/* enable/disable audio dsp clock gating */
	val = enable ? PCI_CGCTL_ADSPDCGE : 0;
	snd_sof_pci_update_bits(sdev, PCI_CGCTL, PCI_CGCTL_ADSPDCGE, val);

	/* enable/disable DMI Link L1 support */
	val = enable ? HDA_VS_INTEL_EM2_L1SEN : 0;
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, HDA_VS_INTEL_EM2,
				HDA_VS_INTEL_EM2_L1SEN, val);

	/* enable/disable audio dsp power gating */
	val = enable ? 0 : PCI_PGCTL_ADSPPGD;
	snd_sof_pci_update_bits(sdev, PCI_PGCTL, PCI_PGCTL_ADSPPGD, val);

	return 0;
}

int hda_dsp_ctrl_init_chip(struct snd_sof_dev *sdev, bool full_reset)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	struct hdac_ext_link *hlink;
#endif
	struct hdac_stream *stream;
	int sd_offset, ret = 0;

	if (bus->chip_init)
		return 0;

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	snd_hdac_set_codec_wakeup(bus, true);
#endif
	hda_dsp_ctrl_misc_clock_gating(sdev, false);

	if (full_reset) {
		/* reset HDA controller */
		ret = hda_dsp_ctrl_link_reset(sdev, true);
		if (ret < 0) {
			dev_err(sdev->dev, "error: failed to reset HDA controller\n");
			goto err;
		}

		usleep_range(500, 1000);

		/* exit HDA controller reset */
		ret = hda_dsp_ctrl_link_reset(sdev, false);
		if (ret < 0) {
			dev_err(sdev->dev, "error: failed to exit HDA controller reset\n");
			goto err;
		}

		usleep_range(1000, 1200);
	}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* check to see if controller is ready */
	if (!snd_hdac_chip_readb(bus, GCTL)) {
		dev_dbg(bus->dev, "controller not ready!\n");
		ret = -EBUSY;
		goto err;
	}

	/* Accept unsolicited responses */
	snd_hdac_chip_updatel(bus, GCTL, AZX_GCTL_UNSOL, AZX_GCTL_UNSOL);

	/* detect codecs */
	if (!bus->codec_mask) {
		bus->codec_mask = snd_hdac_chip_readw(bus, STATESTS);
		dev_dbg(bus->dev, "codec_mask = 0x%lx\n", bus->codec_mask);
	}

	if (hda_codec_mask != -1) {
		bus->codec_mask &= hda_codec_mask;
		dev_dbg(bus->dev, "filtered codec_mask = 0x%lx\n",
			bus->codec_mask);
	}
#endif

	/* clear stream status */
	list_for_each_entry(stream, &bus->stream_list, list) {
		sd_offset = SOF_STREAM_SD_OFFSET(stream);
		snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
				  sd_offset + SOF_HDA_ADSP_REG_CL_SD_STS,
				  SOF_HDA_CL_DMA_SD_INT_MASK);
	}

	/* clear WAKESTS */
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR, SOF_HDA_WAKESTS,
			  SOF_HDA_WAKESTS_INT_MASK);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* clear rirb status */
	snd_hdac_chip_writeb(bus, RIRBSTS, RIRB_INT_MASK);
#endif

	/* clear interrupt status register */
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR, SOF_HDA_INTSTS,
			  SOF_HDA_INT_CTRL_EN | SOF_HDA_INT_ALL_STREAM);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* initialize the codec command I/O */
	snd_hdac_bus_init_cmd_io(bus);
#endif

	/* enable CIE and GIE interrupts */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, SOF_HDA_INTCTL,
				SOF_HDA_INT_CTRL_EN | SOF_HDA_INT_GLOBAL_EN,
				SOF_HDA_INT_CTRL_EN | SOF_HDA_INT_GLOBAL_EN);

	/* program the position buffer */
	if (bus->use_posbuf && bus->posbuf.addr) {
		snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR, SOF_HDA_ADSP_DPLBASE,
				  (u32)bus->posbuf.addr);
		snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR, SOF_HDA_ADSP_DPUBASE,
				  upper_32_bits(bus->posbuf.addr));
	}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* Reset stream-to-link mapping */
	list_for_each_entry(hlink, &bus->hlink_list, list)
		writel(0, hlink->ml_addr + AZX_REG_ML_LOSIDV);
#endif

	bus->chip_init = true;

err:
	hda_dsp_ctrl_misc_clock_gating(sdev, true);
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	snd_hdac_set_codec_wakeup(bus, false);
#endif

	return ret;
}

void hda_dsp_ctrl_stop_chip(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct hdac_stream *stream;
	int sd_offset;

	if (!bus->chip_init)
		return;

	/* disable interrupts in stream descriptor */
	list_for_each_entry(stream, &bus->stream_list, list) {
		sd_offset = SOF_STREAM_SD_OFFSET(stream);
		snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR,
					sd_offset +
					SOF_HDA_ADSP_REG_CL_SD_CTL,
					SOF_HDA_CL_DMA_SD_INT_MASK,
					0);
	}

	/* disable SIE for all streams */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, SOF_HDA_INTCTL,
				SOF_HDA_INT_ALL_STREAM,	0);

	/* disable controller CIE and GIE */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_HDA_BAR, SOF_HDA_INTCTL,
				SOF_HDA_INT_CTRL_EN | SOF_HDA_INT_GLOBAL_EN,
				0);

	/* clear stream status */
	list_for_each_entry(stream, &bus->stream_list, list) {
		sd_offset = SOF_STREAM_SD_OFFSET(stream);
		snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
				  sd_offset + SOF_HDA_ADSP_REG_CL_SD_STS,
				  SOF_HDA_CL_DMA_SD_INT_MASK);
	}

	/* clear WAKESTS */
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR, SOF_HDA_WAKESTS,
			  SOF_HDA_WAKESTS_INT_MASK);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* clear rirb status */
	snd_hdac_chip_writeb(bus, RIRBSTS, RIRB_INT_MASK);
#endif

	/* clear interrupt status register */
	snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR, SOF_HDA_INTSTS,
			  SOF_HDA_INT_CTRL_EN | SOF_HDA_INT_ALL_STREAM);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	/* disable CORB/RIRB */
	snd_hdac_bus_stop_cmd_io(bus);
#endif
	/* disable position buffer */
	if (bus->posbuf.addr) {
		snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
				  SOF_HDA_ADSP_DPLBASE, 0);
		snd_sof_dsp_write(sdev, HDA_DSP_HDA_BAR,
				  SOF_HDA_ADSP_DPUBASE, 0);
	}

	bus->chip_init = false;
}
