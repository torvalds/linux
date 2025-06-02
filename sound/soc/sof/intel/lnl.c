// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright(c) 2023 Intel Corporation

/*
 * Hardware interface for audio DSP on LunarLake.
 */

#include <linux/debugfs.h>
#include <linux/firmware.h>
#include <sound/hda_register.h>
#include <sound/sof/ipc4/header.h>
#include <trace/events/sof_intel.h>
#include "../ipc4-priv.h"
#include "../ops.h"
#include "hda.h"
#include "hda-ipc.h"
#include "../sof-audio.h"
#include "mtl.h"
#include "lnl.h"
#include <sound/hda-mlink.h>

/* this helps allows the DSP to setup DMIC/SSP */
static int hdac_bus_offload_dmic_ssp(struct hdac_bus *bus, bool enable)
{
	int ret;

	ret = hdac_bus_eml_enable_offload(bus, true,
					  AZX_REG_ML_LEPTR_ID_INTEL_SSP, enable);
	if (ret < 0)
		return ret;

	ret = hdac_bus_eml_enable_offload(bus, true,
					  AZX_REG_ML_LEPTR_ID_INTEL_DMIC, enable);
	if (ret < 0)
		return ret;

	return 0;
}

static int lnl_hda_dsp_probe(struct snd_sof_dev *sdev)
{
	int ret;

	ret = hda_dsp_probe(sdev);
	if (ret < 0)
		return ret;

	return hdac_bus_offload_dmic_ssp(sof_to_bus(sdev), true);
}

static void lnl_hda_dsp_remove(struct snd_sof_dev *sdev)
{
	int ret;

	ret = hdac_bus_offload_dmic_ssp(sof_to_bus(sdev), false);
	if (ret < 0)
		dev_warn(sdev->dev,
			 "Failed to disable offload for DMIC/SSP: %d\n", ret);

	hda_dsp_remove(sdev);
}

static int lnl_hda_dsp_resume(struct snd_sof_dev *sdev)
{
	int ret;

	ret = hda_dsp_resume(sdev);
	if (ret < 0)
		return ret;

	return hdac_bus_offload_dmic_ssp(sof_to_bus(sdev), true);
}

static int lnl_hda_dsp_runtime_resume(struct snd_sof_dev *sdev)
{
	int ret;

	ret = hda_dsp_runtime_resume(sdev);
	if (ret < 0)
		return ret;

	return hdac_bus_offload_dmic_ssp(sof_to_bus(sdev), true);
}

static int lnl_dsp_post_fw_run(struct snd_sof_dev *sdev)
{
	if (sdev->first_boot) {
		struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;

		/* Check if IMR boot is usable */
		if (!sof_debug_check_flag(SOF_DBG_IGNORE_D3_PERSISTENT)) {
			hda->imrboot_supported = true;
			debugfs_create_bool("skip_imr_boot",
					    0644, sdev->debugfs_root,
					    &hda->skip_imr_boot);
		}
	}

	return 0;
}

int sof_lnl_set_ops(struct snd_sof_dev *sdev, struct snd_sof_dsp_ops *dsp_ops)
{
	int ret;

	ret = sof_mtl_set_ops(sdev, dsp_ops);
	if (ret)
		return ret;

	/* probe/remove */
	if (!sdev->dspless_mode_selected) {
		dsp_ops->probe = lnl_hda_dsp_probe;
		dsp_ops->remove = lnl_hda_dsp_remove;
	}

	/* post fw run */
	dsp_ops->post_fw_run = lnl_dsp_post_fw_run;

	/* PM */
	if (!sdev->dspless_mode_selected) {
		dsp_ops->resume = lnl_hda_dsp_resume;
		dsp_ops->runtime_resume = lnl_hda_dsp_runtime_resume;
	}

	return 0;
}
EXPORT_SYMBOL_NS(sof_lnl_set_ops, "SND_SOC_SOF_INTEL_LNL");

/* Check if an SDW IRQ occurred */
bool lnl_dsp_check_sdw_irq(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);

	return hdac_bus_eml_check_interrupt(bus, true,  AZX_REG_ML_LEPTR_ID_SDW);
}
EXPORT_SYMBOL_NS(lnl_dsp_check_sdw_irq, "SND_SOC_SOF_INTEL_LNL");

int lnl_dsp_disable_interrupts(struct snd_sof_dev *sdev)
{
	mtl_disable_ipc_interrupts(sdev);
	return mtl_enable_interrupts(sdev, false);
}
EXPORT_SYMBOL_NS(lnl_dsp_disable_interrupts, "SND_SOC_SOF_INTEL_LNL");

bool lnl_sdw_check_wakeen_irq(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	u16 wake_sts;

	/*
	 * we need to use the global HDaudio WAKEEN/STS to be able to
	 * detect wakes in low-power modes. The link-specific information
	 * is handled in the process_wakeen() helper, this helper only
	 * detects a SoundWire wake without identifying the link.
	 */
	wake_sts = snd_hdac_chip_readw(bus, STATESTS);

	/* filter out the range of SDIs that can be set for SoundWire */
	return wake_sts & GENMASK(SDW_MAX_DEVICES, SDW_INTEL_DEV_NUM_IDA_MIN);
}
EXPORT_SYMBOL_NS(lnl_sdw_check_wakeen_irq, "SND_SOC_SOF_INTEL_LNL");

const struct sof_intel_dsp_desc lnl_chip_info = {
	.cores_num = 5,
	.init_core_mask = BIT(0),
	.host_managed_cores_mask = BIT(0),
	.ipc_req = MTL_DSP_REG_HFIPCXIDR,
	.ipc_req_mask = MTL_DSP_REG_HFIPCXIDR_BUSY,
	.ipc_ack = MTL_DSP_REG_HFIPCXIDA,
	.ipc_ack_mask = MTL_DSP_REG_HFIPCXIDA_DONE,
	.ipc_ctl = MTL_DSP_REG_HFIPCXCTL,
	.rom_status_reg = LNL_DSP_REG_HFDSC,
	.rom_init_timeout = 300,
	.ssp_count = MTL_SSP_COUNT,
	.d0i3_offset = MTL_HDA_VS_D0I3C,
	.read_sdw_lcount =  hda_sdw_check_lcount_ext,
	.check_sdw_irq = lnl_dsp_check_sdw_irq,
	.check_sdw_wakeen_irq = lnl_sdw_check_wakeen_irq,
	.sdw_process_wakeen = hda_sdw_process_wakeen_common,
	.check_ipc_irq = mtl_dsp_check_ipc_irq,
	.cl_init = mtl_dsp_cl_init,
	.power_down_dsp = mtl_power_down_dsp,
	.disable_interrupts = lnl_dsp_disable_interrupts,
	.hw_ip_version = SOF_INTEL_ACE_2_0,
};

MODULE_IMPORT_NS("SND_SOC_SOF_INTEL_MTL");
MODULE_IMPORT_NS("SND_SOC_SOF_HDA_MLINK");
