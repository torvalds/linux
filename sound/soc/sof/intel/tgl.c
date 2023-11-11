// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Authors: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//

/*
 * Hardware interface for audio DSP on Tigerlake.
 */

#include <sound/sof/ext_manifest4.h>
#include "../ipc4-priv.h"
#include "../ops.h"
#include "hda.h"
#include "hda-ipc.h"
#include "../sof-audio.h"

static const struct snd_sof_debugfs_map tgl_dsp_debugfs[] = {
	{"hda", HDA_DSP_HDA_BAR, 0, 0x4000, SOF_DEBUGFS_ACCESS_ALWAYS},
	{"pp", HDA_DSP_PP_BAR,  0, 0x1000, SOF_DEBUGFS_ACCESS_ALWAYS},
	{"dsp", HDA_DSP_BAR,  0, 0x10000, SOF_DEBUGFS_ACCESS_ALWAYS},
};

static int tgl_dsp_core_get(struct snd_sof_dev *sdev, int core)
{
	const struct sof_ipc_pm_ops *pm_ops = sdev->ipc->ops->pm;

	/* power up primary core if not already powered up and return */
	if (core == SOF_DSP_PRIMARY_CORE)
		return hda_dsp_enable_core(sdev, BIT(core));

	if (pm_ops->set_core_state)
		return pm_ops->set_core_state(sdev, core, true);

	return 0;
}

static int tgl_dsp_core_put(struct snd_sof_dev *sdev, int core)
{
	const struct sof_ipc_pm_ops *pm_ops = sdev->ipc->ops->pm;

	/* power down primary core and return */
	if (core == SOF_DSP_PRIMARY_CORE)
		return hda_dsp_core_reset_power_down(sdev, BIT(core));

	if (pm_ops->set_core_state)
		return pm_ops->set_core_state(sdev, core, false);

	return 0;
}

/* Tigerlake ops */
struct snd_sof_dsp_ops sof_tgl_ops;
EXPORT_SYMBOL_NS(sof_tgl_ops, SND_SOC_SOF_INTEL_HDA_COMMON);

int sof_tgl_ops_init(struct snd_sof_dev *sdev)
{
	/* common defaults */
	memcpy(&sof_tgl_ops, &sof_hda_common_ops, sizeof(struct snd_sof_dsp_ops));

	/* probe/remove/shutdown */
	sof_tgl_ops.shutdown	= hda_dsp_shutdown_dma_flush;

	if (sdev->pdata->ipc_type == SOF_IPC) {
		/* doorbell */
		sof_tgl_ops.irq_thread	= cnl_ipc_irq_thread;

		/* ipc */
		sof_tgl_ops.send_msg	= cnl_ipc_send_msg;

		/* debug */
		sof_tgl_ops.ipc_dump	= cnl_ipc_dump;
	}

	if (sdev->pdata->ipc_type == SOF_INTEL_IPC4) {
		struct sof_ipc4_fw_data *ipc4_data;

		sdev->private = devm_kzalloc(sdev->dev, sizeof(*ipc4_data), GFP_KERNEL);
		if (!sdev->private)
			return -ENOMEM;

		ipc4_data = sdev->private;
		ipc4_data->manifest_fw_hdr_offset = SOF_MAN4_FW_HDR_OFFSET;

		ipc4_data->mtrace_type = SOF_IPC4_MTRACE_INTEL_CAVS_2;

		/* doorbell */
		sof_tgl_ops.irq_thread	= cnl_ipc4_irq_thread;

		/* ipc */
		sof_tgl_ops.send_msg	= cnl_ipc4_send_msg;

		/* debug */
		sof_tgl_ops.ipc_dump	= cnl_ipc4_dump;
	}

	/* set DAI driver ops */
	hda_set_dai_drv_ops(sdev, &sof_tgl_ops);

	/* debug */
	sof_tgl_ops.debug_map	= tgl_dsp_debugfs;
	sof_tgl_ops.debug_map_count	= ARRAY_SIZE(tgl_dsp_debugfs);

	/* pre/post fw run */
	sof_tgl_ops.post_fw_run = hda_dsp_post_fw_run;

	/* firmware run */
	sof_tgl_ops.run = hda_dsp_cl_boot_firmware_iccmax;

	/* dsp core get/put */
	sof_tgl_ops.core_get = tgl_dsp_core_get;
	sof_tgl_ops.core_put = tgl_dsp_core_put;

	return 0;
};
EXPORT_SYMBOL_NS(sof_tgl_ops_init, SND_SOC_SOF_INTEL_HDA_COMMON);

const struct sof_intel_dsp_desc tgl_chip_info = {
	/* Tigerlake , Alderlake */
	.cores_num = 4,
	.init_core_mask = 1,
	.host_managed_cores_mask = BIT(0),
	.ipc_req = CNL_DSP_REG_HIPCIDR,
	.ipc_req_mask = CNL_DSP_REG_HIPCIDR_BUSY,
	.ipc_ack = CNL_DSP_REG_HIPCIDA,
	.ipc_ack_mask = CNL_DSP_REG_HIPCIDA_DONE,
	.ipc_ctl = CNL_DSP_REG_HIPCCTL,
	.rom_status_reg = HDA_DSP_SRAM_REG_ROM_STATUS,
	.rom_init_timeout	= 300,
	.ssp_count = TGL_SSP_COUNT,
	.ssp_base_offset = CNL_SSP_BASE_OFFSET,
	.sdw_shim_base = SDW_SHIM_BASE,
	.sdw_alh_base = SDW_ALH_BASE,
	.check_sdw_irq	= hda_common_check_sdw_irq,
	.check_ipc_irq	= hda_dsp_check_ipc_irq,
	.cl_init = cl_dsp_init,
	.power_down_dsp = hda_power_down_dsp,
	.disable_interrupts = hda_dsp_disable_interrupts,
	.hw_ip_version = SOF_INTEL_CAVS_2_5,
};
EXPORT_SYMBOL_NS(tgl_chip_info, SND_SOC_SOF_INTEL_HDA_COMMON);

const struct sof_intel_dsp_desc tglh_chip_info = {
	/* Tigerlake-H */
	.cores_num = 2,
	.init_core_mask = 1,
	.host_managed_cores_mask = BIT(0),
	.ipc_req = CNL_DSP_REG_HIPCIDR,
	.ipc_req_mask = CNL_DSP_REG_HIPCIDR_BUSY,
	.ipc_ack = CNL_DSP_REG_HIPCIDA,
	.ipc_ack_mask = CNL_DSP_REG_HIPCIDA_DONE,
	.ipc_ctl = CNL_DSP_REG_HIPCCTL,
	.rom_status_reg = HDA_DSP_SRAM_REG_ROM_STATUS,
	.rom_init_timeout	= 300,
	.ssp_count = TGL_SSP_COUNT,
	.ssp_base_offset = CNL_SSP_BASE_OFFSET,
	.sdw_shim_base = SDW_SHIM_BASE,
	.sdw_alh_base = SDW_ALH_BASE,
	.check_sdw_irq	= hda_common_check_sdw_irq,
	.check_ipc_irq	= hda_dsp_check_ipc_irq,
	.cl_init = cl_dsp_init,
	.power_down_dsp = hda_power_down_dsp,
	.disable_interrupts = hda_dsp_disable_interrupts,
	.hw_ip_version = SOF_INTEL_CAVS_2_5,
};
EXPORT_SYMBOL_NS(tglh_chip_info, SND_SOC_SOF_INTEL_HDA_COMMON);

const struct sof_intel_dsp_desc ehl_chip_info = {
	/* Elkhartlake */
	.cores_num = 4,
	.init_core_mask = 1,
	.host_managed_cores_mask = BIT(0),
	.ipc_req = CNL_DSP_REG_HIPCIDR,
	.ipc_req_mask = CNL_DSP_REG_HIPCIDR_BUSY,
	.ipc_ack = CNL_DSP_REG_HIPCIDA,
	.ipc_ack_mask = CNL_DSP_REG_HIPCIDA_DONE,
	.ipc_ctl = CNL_DSP_REG_HIPCCTL,
	.rom_status_reg = HDA_DSP_SRAM_REG_ROM_STATUS,
	.rom_init_timeout	= 300,
	.ssp_count = TGL_SSP_COUNT,
	.ssp_base_offset = CNL_SSP_BASE_OFFSET,
	.sdw_shim_base = SDW_SHIM_BASE,
	.sdw_alh_base = SDW_ALH_BASE,
	.check_sdw_irq	= hda_common_check_sdw_irq,
	.check_ipc_irq	= hda_dsp_check_ipc_irq,
	.cl_init = cl_dsp_init,
	.power_down_dsp = hda_power_down_dsp,
	.disable_interrupts = hda_dsp_disable_interrupts,
	.hw_ip_version = SOF_INTEL_CAVS_2_5,
};
EXPORT_SYMBOL_NS(ehl_chip_info, SND_SOC_SOF_INTEL_HDA_COMMON);

const struct sof_intel_dsp_desc adls_chip_info = {
	/* Alderlake-S */
	.cores_num = 2,
	.init_core_mask = BIT(0),
	.host_managed_cores_mask = BIT(0),
	.ipc_req = CNL_DSP_REG_HIPCIDR,
	.ipc_req_mask = CNL_DSP_REG_HIPCIDR_BUSY,
	.ipc_ack = CNL_DSP_REG_HIPCIDA,
	.ipc_ack_mask = CNL_DSP_REG_HIPCIDA_DONE,
	.ipc_ctl = CNL_DSP_REG_HIPCCTL,
	.rom_status_reg = HDA_DSP_SRAM_REG_ROM_STATUS,
	.rom_init_timeout	= 300,
	.ssp_count = TGL_SSP_COUNT,
	.ssp_base_offset = CNL_SSP_BASE_OFFSET,
	.sdw_shim_base = SDW_SHIM_BASE,
	.sdw_alh_base = SDW_ALH_BASE,
	.check_sdw_irq	= hda_common_check_sdw_irq,
	.check_ipc_irq	= hda_dsp_check_ipc_irq,
	.cl_init = cl_dsp_init,
	.power_down_dsp = hda_power_down_dsp,
	.disable_interrupts = hda_dsp_disable_interrupts,
	.hw_ip_version = SOF_INTEL_CAVS_2_5,
};
EXPORT_SYMBOL_NS(adls_chip_info, SND_SOC_SOF_INTEL_HDA_COMMON);
