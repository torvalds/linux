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
#include "../sof-audio.h"
#include "acp.h"
#include "acp-dsp-offset.h"

#define I2S_BT_INSTANCE		0
#define I2S_SP_INSTANCE		1
#define PDM_DMIC_INSTANCE	2

#define I2S_MODE		0x04

static int renoir_dai_probe(struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(dai->component);
	unsigned int val;

	val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, ACP_I2S_PIN_CONFIG);
	if (val != I2S_MODE) {
		dev_err(sdev->dev, "I2S Mode is not supported (I2S_PIN_CONFIG: %#x)\n", val);
		return -EINVAL;
	}

	return 0;
}

static struct snd_soc_dai_driver renoir_sof_dai[] = {
	[I2S_BT_INSTANCE] = {
		.id = I2S_BT_INSTANCE,
		.name = "acp-sof-bt",
		.playback = {
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
				   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 2,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 96000,
		},
		.capture = {
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
				   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
			/* Supporting only stereo for I2S BT controller capture */
			.channels_min = 2,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
		},
		.probe = &renoir_dai_probe,
	},

	[I2S_SP_INSTANCE] = {
		.id = I2S_SP_INSTANCE,
		.name = "acp-sof-sp",
		.playback = {
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
				   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 2,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 96000,
		},
		.capture = {
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
				   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
			/* Supporting only stereo for I2S SP controller capture */
			.channels_min = 2,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
		},
		.probe = &renoir_dai_probe,
	},

	[PDM_DMIC_INSTANCE] = {
		.id = PDM_DMIC_INSTANCE,
		.name = "acp-sof-dmic",
		.capture = {
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 2,
			.channels_max = 4,
			.rate_min = 8000,
			.rate_max = 48000,
		},
	},
};

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

	/* DAI drivers */
	.drv			= renoir_sof_dai,
	.num_drv		= ARRAY_SIZE(renoir_sof_dai),

	/* stream callbacks */
	.pcm_open		= acp_pcm_open,
	.pcm_close		= acp_pcm_close,
	.pcm_hw_params		= acp_pcm_hw_params,

	.hw_info		= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_PAUSE |
				  SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,
};
EXPORT_SYMBOL(sof_renoir_ops);

MODULE_IMPORT_NS(SND_SOC_SOF_AMD_COMMON);
MODULE_DESCRIPTION("RENOIR SOF Driver");
MODULE_LICENSE("Dual BSD/GPL");
