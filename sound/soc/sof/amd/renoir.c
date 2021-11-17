// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Advanced Micro Devices, Inc.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>

/*
 * Hardware interface for Audio DSP on Renoir platform
 */

#include <linux/platform_device.h>
#include <linux/module.h>

#include "../ops.h"
#include "acp.h"

/* AMD Renoir DSP ops */
const struct snd_sof_dsp_ops sof_renoir_ops = {
	/* probe and remove */
	.probe			= amd_sof_acp_probe,
	.remove			= amd_sof_acp_remove,

	/* Register IO */
	.write			= sof_io_write,
	.read			= sof_io_read,

	/* Block IO */
	.block_read		= acp_dsp_block_read,
	.block_write		= acp_dsp_block_write,

	/* Module loading */
	.load_module		= snd_sof_parse_module_memcpy,

	/*Firmware loading */
	.load_firmware		= snd_sof_load_firmware_memcpy,
	.pre_fw_run		= acp_dsp_pre_fw_run,
	.get_bar_index		= acp_get_bar_index,

	/* DSP core boot */
	.run			= acp_sof_dsp_run,

	/*IPC */
	.send_msg		= acp_sof_ipc_send_msg,
	.ipc_msg_data		= acp_sof_ipc_msg_data,
	.ipc_pcm_params		= acp_sof_ipc_pcm_params,
	.get_mailbox_offset	= acp_sof_ipc_get_mailbox_offset,
	.irq_thread		= acp_sof_ipc_irq_thread,
	.fw_ready		= sof_fw_ready,
};
EXPORT_SYMBOL(sof_renoir_ops);

MODULE_IMPORT_NS(SND_SOC_SOF_AMD_COMMON);
MODULE_DESCRIPTION("RENOIR SOF Driver");
MODULE_LICENSE("Dual BSD/GPL");
