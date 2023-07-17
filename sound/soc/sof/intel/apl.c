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
 * Hardware interface for audio DSP on Apollolake and GeminiLake
 */

#include <sound/sof/ext_manifest4.h>
#include "../ipc4-priv.h"
#include "../sof-priv.h"
#include "hda.h"
#include "../sof-audio.h"

static const struct snd_sof_debugfs_map apl_dsp_debugfs[] = {
	{"hda", HDA_DSP_HDA_BAR, 0, 0x4000, SOF_DEBUGFS_ACCESS_ALWAYS},
	{"pp", HDA_DSP_PP_BAR,  0, 0x1000, SOF_DEBUGFS_ACCESS_ALWAYS},
	{"dsp", HDA_DSP_BAR,  0, 0x10000, SOF_DEBUGFS_ACCESS_ALWAYS},
};

/* apollolake ops */
struct snd_sof_dsp_ops sof_apl_ops;
EXPORT_SYMBOL_NS(sof_apl_ops, SND_SOC_SOF_INTEL_HDA_COMMON);

int sof_apl_ops_init(struct snd_sof_dev *sdev)
{
	/* common defaults */
	memcpy(&sof_apl_ops, &sof_hda_common_ops, sizeof(struct snd_sof_dsp_ops));

	/* probe/remove/shutdown */
	sof_apl_ops.shutdown	= hda_dsp_shutdown;

	if (sdev->pdata->ipc_type == SOF_IPC) {
		/* doorbell */
		sof_apl_ops.irq_thread	= hda_dsp_ipc_irq_thread;

		/* ipc */
		sof_apl_ops.send_msg	= hda_dsp_ipc_send_msg;

		/* debug */
		sof_apl_ops.ipc_dump	= hda_ipc_dump;

		sof_apl_ops.set_power_state = hda_dsp_set_power_state_ipc3;
	}

	if (sdev->pdata->ipc_type == SOF_INTEL_IPC4) {
		struct sof_ipc4_fw_data *ipc4_data;

		sdev->private = devm_kzalloc(sdev->dev, sizeof(*ipc4_data), GFP_KERNEL);
		if (!sdev->private)
			return -ENOMEM;

		ipc4_data = sdev->private;
		ipc4_data->manifest_fw_hdr_offset = SOF_MAN4_FW_HDR_OFFSET;

		ipc4_data->mtrace_type = SOF_IPC4_MTRACE_INTEL_CAVS_1_5;

		/* External library loading support */
		ipc4_data->load_library = hda_dsp_ipc4_load_library;

		/* doorbell */
		sof_apl_ops.irq_thread	= hda_dsp_ipc4_irq_thread;

		/* ipc */
		sof_apl_ops.send_msg	= hda_dsp_ipc4_send_msg;

		/* debug */
		sof_apl_ops.ipc_dump	= hda_ipc4_dump;

		sof_apl_ops.set_power_state = hda_dsp_set_power_state_ipc4;
	}

	/* set DAI driver ops */
	hda_set_dai_drv_ops(sdev, &sof_apl_ops);

	/* debug */
	sof_apl_ops.debug_map	= apl_dsp_debugfs;
	sof_apl_ops.debug_map_count	= ARRAY_SIZE(apl_dsp_debugfs);

	/* firmware run */
	sof_apl_ops.run = hda_dsp_cl_boot_firmware;

	/* pre/post fw run */
	sof_apl_ops.post_fw_run = hda_dsp_post_fw_run;

	/* dsp core get/put */
	sof_apl_ops.core_get = hda_dsp_core_get;

	return 0;
};
EXPORT_SYMBOL_NS(sof_apl_ops_init, SND_SOC_SOF_INTEL_HDA_COMMON);

const struct sof_intel_dsp_desc apl_chip_info = {
	/* Apollolake */
	.cores_num = 2,
	.init_core_mask = 1,
	.host_managed_cores_mask = GENMASK(1, 0),
	.ipc_req = HDA_DSP_REG_HIPCI,
	.ipc_req_mask = HDA_DSP_REG_HIPCI_BUSY,
	.ipc_ack = HDA_DSP_REG_HIPCIE,
	.ipc_ack_mask = HDA_DSP_REG_HIPCIE_DONE,
	.ipc_ctl = HDA_DSP_REG_HIPCCTL,
	.rom_status_reg = HDA_DSP_SRAM_REG_ROM_STATUS,
	.rom_init_timeout	= 150,
	.ssp_count = APL_SSP_COUNT,
	.ssp_base_offset = APL_SSP_BASE_OFFSET,
	.d0i3_offset = SOF_HDA_VS_D0I3C,
	.quirks = SOF_INTEL_PROCEN_FMT_QUIRK,
	.check_ipc_irq	= hda_dsp_check_ipc_irq,
	.cl_init = cl_dsp_init,
	.power_down_dsp = hda_power_down_dsp,
	.disable_interrupts = hda_dsp_disable_interrupts,
	.hw_ip_version = SOF_INTEL_CAVS_1_5_PLUS,
};
EXPORT_SYMBOL_NS(apl_chip_info, SND_SOC_SOF_INTEL_HDA_COMMON);
