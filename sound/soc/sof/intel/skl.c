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
#include <sound/hdaudio_ext.h>
#include <sound/pcm_params.h>
#include <sound/sof.h>
#include <sound/sof/ext_manifest4.h>

#include "../sof-priv.h"
#include "../ipc4-priv.h"
#include "../ops.h"
#include "hda.h"
#include "../sof-audio.h"

#define SRAM_MEMORY_WINDOW_BASE 0x8000

static const __maybe_unused struct snd_sof_debugfs_map skl_dsp_debugfs[] = {
	{"hda", HDA_DSP_HDA_BAR, 0, 0x4000},
	{"pp", HDA_DSP_PP_BAR,  0, 0x1000},
	{"dsp", HDA_DSP_BAR,  0, 0x10000},
};

static int skl_dsp_ipc_get_window_offset(struct snd_sof_dev *sdev, u32 id)
{
	return SRAM_MEMORY_WINDOW_BASE + (0x2000 * id);
}

static int skl_dsp_ipc_get_mailbox_offset(struct snd_sof_dev *sdev)
{
	return SRAM_MEMORY_WINDOW_BASE + 0x1000;
}

/* skylake ops */
struct snd_sof_dsp_ops sof_skl_ops;
EXPORT_SYMBOL_NS(sof_skl_ops, SND_SOC_SOF_INTEL_HDA_COMMON);

int sof_skl_ops_init(struct snd_sof_dev *sdev)
{
	struct sof_ipc4_fw_data *ipc4_data;

	/* common defaults */
	memcpy(&sof_skl_ops, &sof_hda_common_ops, sizeof(struct snd_sof_dsp_ops));

	/* probe/remove/shutdown */
	sof_skl_ops.shutdown	= hda_dsp_shutdown;

	sdev->private = kzalloc(sizeof(*ipc4_data), GFP_KERNEL);
	if (!sdev->private)
		return -ENOMEM;

	ipc4_data = sdev->private;
	ipc4_data->manifest_fw_hdr_offset = SOF_MAN4_FW_HDR_OFFSET_CAVS_1_5;

	ipc4_data->mtrace_type = SOF_IPC4_MTRACE_INTEL_CAVS_1_5;

	sof_skl_ops.get_window_offset = skl_dsp_ipc_get_window_offset;
	sof_skl_ops.get_mailbox_offset = skl_dsp_ipc_get_mailbox_offset;

	/* doorbell */
	sof_skl_ops.irq_thread	= hda_dsp_ipc4_irq_thread;

	/* ipc */
	sof_skl_ops.send_msg	= hda_dsp_ipc4_send_msg;

	/* set DAI driver ops */
	hda_set_dai_drv_ops(sdev, &sof_skl_ops);

	/* debug */
	sof_skl_ops.debug_map	= skl_dsp_debugfs;
	sof_skl_ops.debug_map_count	= ARRAY_SIZE(skl_dsp_debugfs);
	sof_skl_ops.ipc_dump	= hda_ipc4_dump;

	/* firmware run */
	sof_skl_ops.run = hda_dsp_cl_boot_firmware_skl;

	/* pre/post fw run */
	sof_skl_ops.post_fw_run = hda_dsp_post_fw_run;

	return 0;
};
EXPORT_SYMBOL_NS(sof_skl_ops_init, SND_SOC_SOF_INTEL_HDA_COMMON);

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
	.power_down_dsp = hda_power_down_dsp,
	.disable_interrupts = hda_dsp_disable_interrupts,
	.hw_ip_version = SOF_INTEL_CAVS_1_5,
};
EXPORT_SYMBOL_NS(skl_chip_info, SND_SOC_SOF_INTEL_HDA_COMMON);
