// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright(c) 2025 Intel Corporation

/*
 * Hardware interface for audio DSP on NovaLake.
 */

#include <sound/hda_register.h>
#include <sound/hda-mlink.h>
#include <sound/sof/ipc4/header.h>
#include "../ipc4-priv.h"
#include "../ops.h"
#include "hda.h"
#include "hda-ipc.h"
#include "../sof-audio.h"
#include "mtl.h"
#include "lnl.h"
#include "ptl.h"
#include "nvl.h"

int sof_nvl_set_ops(struct snd_sof_dev *sdev, struct snd_sof_dsp_ops *dsp_ops)
{
	/* Use PTL ops for NVL */
	return sof_ptl_set_ops(sdev, dsp_ops);
};
EXPORT_SYMBOL_NS(sof_nvl_set_ops, "SND_SOC_SOF_INTEL_NVL");

const struct sof_intel_dsp_desc nvl_chip_info = {
	.cores_num = 4,
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
	.hw_ip_version = SOF_INTEL_ACE_4_0,
};

const struct sof_intel_dsp_desc nvl_s_chip_info = {
	.cores_num = 2,
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
	.hw_ip_version = SOF_INTEL_ACE_4_0,
};

MODULE_IMPORT_NS("SND_SOC_SOF_INTEL_MTL");
MODULE_IMPORT_NS("SND_SOC_SOF_INTEL_LNL");
MODULE_IMPORT_NS("SND_SOC_SOF_INTEL_PTL");
