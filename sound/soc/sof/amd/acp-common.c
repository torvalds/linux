// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Advanced Micro Devices, Inc.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>
//	    V sujith kumar Reddy <Vsujithkumar.Reddy@amd.com>

/* ACP-specific Common code */

#include "../sof-priv.h"
#include "../sof-audio.h"
#include "../ops.h"
#include "../sof-audio.h"
#include "acp.h"
#include "acp-dsp-offset.h"

int acp_dai_probe(struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(dai->component);
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);
	unsigned int val;

	val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, desc->i2s_pin_config_offset);
	if (val != desc->i2s_mode) {
		dev_err(sdev->dev, "I2S Mode is not supported (I2S_PIN_CONFIG: %#x)\n", val);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_NS(acp_dai_probe, SND_SOC_SOF_AMD_COMMON);

struct snd_soc_acpi_mach *amd_sof_machine_select(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *sof_pdata = sdev->pdata;
	const struct sof_dev_desc *desc = sof_pdata->desc;
	struct snd_soc_acpi_mach *mach;

	mach = snd_soc_acpi_find_machine(desc->machines);
	if (!mach) {
		dev_warn(sdev->dev, "No matching ASoC machine driver found\n");
		return NULL;
	}

	sof_pdata->tplg_filename = mach->sof_tplg_filename;
	sof_pdata->fw_filename = mach->fw_filename;

	return mach;
}

/* AMD Common DSP ops */
struct snd_sof_dsp_ops sof_acp_common_ops = {
	/* probe and remove */
	.probe			= amd_sof_acp_probe,
	.remove			= amd_sof_acp_remove,

	/* Register IO */
	.write			= sof_io_write,
	.read			= sof_io_read,

	/* Block IO */
	.block_read		= acp_dsp_block_read,
	.block_write		= acp_dsp_block_write,

	/*Firmware loading */
	.load_firmware		= snd_sof_load_firmware_memcpy,
	.pre_fw_run		= acp_dsp_pre_fw_run,
	.get_bar_index		= acp_get_bar_index,

	/* DSP core boot */
	.run			= acp_sof_dsp_run,

	/*IPC */
	.send_msg		= acp_sof_ipc_send_msg,
	.ipc_msg_data		= acp_sof_ipc_msg_data,
	.get_mailbox_offset	= acp_sof_ipc_get_mailbox_offset,
	.get_window_offset      = acp_sof_ipc_get_window_offset,
	.irq_thread		= acp_sof_ipc_irq_thread,

	/* stream callbacks */
	.pcm_open		= acp_pcm_open,
	.pcm_close		= acp_pcm_close,
	.pcm_hw_params		= acp_pcm_hw_params,

	.hw_info		= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_PAUSE |
				  SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,

	/* Machine driver callbacks */
	.machine_select		= amd_sof_machine_select,
	.machine_register	= sof_machine_register,
	.machine_unregister	= sof_machine_unregister,

	/* Trace Logger */
	.trace_init		= acp_sof_trace_init,
	.trace_release		= acp_sof_trace_release,

	/* PM */
	.suspend                = amd_sof_acp_suspend,
	.resume                 = amd_sof_acp_resume,
};
EXPORT_SYMBOL_NS(sof_acp_common_ops, SND_SOC_SOF_AMD_COMMON);

MODULE_IMPORT_NS(SND_SOC_SOF_AMD_COMMON);
MODULE_DESCRIPTION("ACP SOF COMMON Driver");
MODULE_LICENSE("Dual BSD/GPL");
