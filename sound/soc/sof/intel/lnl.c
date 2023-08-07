// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright(c) 2023 Intel Corporation. All rights reserved.

/*
 * Hardware interface for audio DSP on LunarLake.
 */

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
#include "hda.h"
#include <sound/hda-mlink.h>

/* Check if an SDW IRQ occurred */
static bool lnl_dsp_check_sdw_irq(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);

	return hdac_bus_eml_check_interrupt(bus, true,  AZX_REG_ML_LEPTR_ID_SDW);
}

static void lnl_enable_sdw_irq(struct snd_sof_dev *sdev, bool enable)
{
	struct hdac_bus *bus = sof_to_bus(sdev);

	hdac_bus_eml_enable_interrupt(bus, true,  AZX_REG_ML_LEPTR_ID_SDW, enable);
}

static int lnl_dsp_disable_interrupts(struct snd_sof_dev *sdev)
{
	lnl_enable_sdw_irq(sdev, false);
	mtl_disable_ipc_interrupts(sdev);
	return mtl_enable_interrupts(sdev, false);
}

const struct sof_intel_dsp_desc lnl_chip_info = {
	.cores_num = 5,
	.init_core_mask = BIT(0),
	.host_managed_cores_mask = BIT(0),
	.ipc_req = MTL_DSP_REG_HFIPCXIDR,
	.ipc_req_mask = MTL_DSP_REG_HFIPCXIDR_BUSY,
	.ipc_ack = MTL_DSP_REG_HFIPCXIDA,
	.ipc_ack_mask = MTL_DSP_REG_HFIPCXIDA_DONE,
	.ipc_ctl = MTL_DSP_REG_HFIPCXCTL,
	.rom_status_reg = MTL_DSP_ROM_STS,
	.rom_init_timeout = 300,
	.ssp_count = MTL_SSP_COUNT,
	.d0i3_offset = MTL_HDA_VS_D0I3C,
	.read_sdw_lcount =  hda_sdw_check_lcount_ext,
	.enable_sdw_irq = lnl_enable_sdw_irq,
	.check_sdw_irq = lnl_dsp_check_sdw_irq,
	.check_ipc_irq = mtl_dsp_check_ipc_irq,
	.cl_init = mtl_dsp_cl_init,
	.power_down_dsp = mtl_power_down_dsp,
	.disable_interrupts = lnl_dsp_disable_interrupts,
	.hw_ip_version = SOF_INTEL_ACE_2_0,
};
EXPORT_SYMBOL_NS(lnl_chip_info, SND_SOC_SOF_INTEL_HDA_COMMON);
