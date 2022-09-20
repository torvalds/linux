// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018-2022 Intel Corporation. All rights reserved.
//

/*
 * Hardware interface for audio DSP on Skylake and Kabylake.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <sound/hdaudio_ext.h>
#include <sound/pcm_params.h>
#include <sound/sof.h>

#include "../sof-priv.h"
#include "../ops.h"
#include "hda.h"
#include "../sof-audio.h"

#define SRAM_MEMORY_WINDOW_BASE 0x8000

static const __maybe_unused struct snd_sof_debugfs_map skl_dsp_debugfs[] = {
	{"hda", HDA_DSP_HDA_BAR, 0, 0x4000},
	{"pp", HDA_DSP_PP_BAR,  0, 0x1000},
	{"dsp", HDA_DSP_BAR,  0, 0x10000},
};

static int __maybe_unused skl_dsp_ipc_get_window_offset(struct snd_sof_dev *sdev, u32 id)
{
	return SRAM_MEMORY_WINDOW_BASE + (0x2000 * id);
}

/* skylake ops */
struct snd_sof_dsp_ops sof_skl_ops = {
	/*
	 * the ops are left empty at this stage since the SOF releases do not
	 * support SKL/KBL.
	 * The ops will be populated when support for the Intel IPC4 is added
	 * to the SOF driver
	 */
};
EXPORT_SYMBOL(sof_skl_ops);

const struct sof_intel_dsp_desc skl_chip_info = {
	.cores_num = 2,
	.init_core_mask = 1,
	.host_managed_cores_mask = GENMASK(1, 0),
	.ipc_req = HDA_DSP_REG_HIPCI,
	.ipc_req_mask = HDA_DSP_REG_HIPCI_BUSY,
	.ipc_ack = HDA_DSP_REG_HIPCIE,
	.ipc_ack_mask = HDA_DSP_REG_HIPCIE_DONE,
	.ipc_ctl = HDA_DSP_REG_HIPCCTL,
	.rom_status_reg = HDA_DSP_SRAM_REG_ROM_STATUS_SKL,
	.rom_init_timeout	= 300,
	.check_ipc_irq	= hda_dsp_check_ipc_irq,
	.hw_ip_version = SOF_INTEL_CAVS_1_5,
};
EXPORT_SYMBOL_NS(skl_chip_info, SND_SOC_SOF_INTEL_HDA_COMMON);
