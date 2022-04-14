// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Authors: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//

/*
 * Hardware interface for audio DSP on Tigerlake.
 */

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
	struct sof_ipc_pm_core_config pm_core_config = {
		.hdr = {
			.cmd = SOF_IPC_GLB_PM_MSG | SOF_IPC_PM_CORE_ENABLE,
			.size = sizeof(pm_core_config),
		},
		.enable_mask = sdev->enabled_cores_mask | BIT(core),
	};

	/* power up primary core if not already powered up and return */
	if (core == SOF_DSP_PRIMARY_CORE)
		return hda_dsp_enable_core(sdev, BIT(core));

	/* notify DSP for secondary cores */
	return sof_ipc_tx_message(sdev->ipc, &pm_core_config, sizeof(pm_core_config),
				 &pm_core_config, sizeof(pm_core_config));
}

static int tgl_dsp_core_put(struct snd_sof_dev *sdev, int core)
{
	struct sof_ipc_pm_core_config pm_core_config = {
		.hdr = {
			.cmd = SOF_IPC_GLB_PM_MSG | SOF_IPC_PM_CORE_ENABLE,
			.size = sizeof(pm_core_config),
		},
		.enable_mask = sdev->enabled_cores_mask & ~BIT(core),
	};

	/* power down primary core and return */
	if (core == SOF_DSP_PRIMARY_CORE)
		return hda_dsp_core_reset_power_down(sdev, BIT(core));

	/* notify DSP for secondary cores */
	return sof_ipc_tx_message(sdev->ipc, &pm_core_config, sizeof(pm_core_config),
				 &pm_core_config, sizeof(pm_core_config));
}

/* Tigerlake ops */
struct snd_sof_dsp_ops sof_tgl_ops;
EXPORT_SYMBOL_NS(sof_tgl_ops, SND_SOC_SOF_INTEL_HDA_COMMON);

int sof_tgl_ops_init(struct snd_sof_dev *sdev)
{
	/* common defaults */
	memcpy(&sof_tgl_ops, &sof_hda_common_ops, sizeof(struct snd_sof_dsp_ops));

	/* probe/remove/shutdown */
	sof_tgl_ops.shutdown	= hda_dsp_shutdown;

	/* doorbell */
	sof_tgl_ops.irq_thread	= cnl_ipc_irq_thread;

	/* ipc */
	sof_tgl_ops.send_msg	= cnl_ipc_send_msg;

	/* debug */
	sof_tgl_ops.debug_map	= tgl_dsp_debugfs;
	sof_tgl_ops.debug_map_count	= ARRAY_SIZE(tgl_dsp_debugfs);
	sof_tgl_ops.ipc_dump	= cnl_ipc_dump;

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
	.ssp_count = ICL_SSP_COUNT,
	.ssp_base_offset = CNL_SSP_BASE_OFFSET,
	.sdw_shim_base = SDW_SHIM_BASE,
	.sdw_alh_base = SDW_ALH_BASE,
	.check_sdw_irq	= hda_common_check_sdw_irq,
	.check_ipc_irq	= hda_dsp_check_ipc_irq,
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
	.ssp_count = ICL_SSP_COUNT,
	.ssp_base_offset = CNL_SSP_BASE_OFFSET,
	.sdw_shim_base = SDW_SHIM_BASE,
	.sdw_alh_base = SDW_ALH_BASE,
	.check_sdw_irq	= hda_common_check_sdw_irq,
	.check_ipc_irq	= hda_dsp_check_ipc_irq,
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
	.ssp_count = ICL_SSP_COUNT,
	.ssp_base_offset = CNL_SSP_BASE_OFFSET,
	.sdw_shim_base = SDW_SHIM_BASE,
	.sdw_alh_base = SDW_ALH_BASE,
	.check_sdw_irq	= hda_common_check_sdw_irq,
	.check_ipc_irq	= hda_dsp_check_ipc_irq,
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
	.ssp_count = ICL_SSP_COUNT,
	.ssp_base_offset = CNL_SSP_BASE_OFFSET,
	.sdw_shim_base = SDW_SHIM_BASE,
	.sdw_alh_base = SDW_ALH_BASE,
	.check_sdw_irq	= hda_common_check_sdw_irq,
	.check_ipc_irq	= hda_dsp_check_ipc_irq,
};
EXPORT_SYMBOL_NS(adls_chip_info, SND_SOC_SOF_INTEL_HDA_COMMON);
