// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2025 Advanced Micro Devices, Inc.
//
// Authors: Vijendar Mukunda <Vijendar.Mukunda@amd.com>

/*
 * Hardware interface for Audio DSP on ACP7.B/7.F platforms
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <sound/soc-acpi.h>

#include "../ops.h"
#include "../sof-audio.h"
#include "acp.h"
#include "acp-dsp-offset.h"

#define I2S_TDM0_INSTANCE	0
#define I2S_TDM1_INSTANCE	1
#define I2S_TDM2_INSTANCE	2
#define PDM0_DMIC_INSTANCE	3
#define PDM1_DMIC_INSTANCE	4

static struct snd_soc_dai_driver acp7x_sof_dai[] = {
	[I2S_TDM0_INSTANCE] = {
		.id = I2S_TDM0_INSTANCE,
		.name = "acp-sof-i2s0",
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
			/* Supporting only stereo for I2S HS controller capture */
			.channels_min = 2,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
		},
	},

	[I2S_TDM1_INSTANCE] = {
		.id = I2S_TDM1_INSTANCE,
		.name = "acp-sof-i2s1",
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
	},

	[I2S_TDM2_INSTANCE] = {
		.id = I2S_TDM2_INSTANCE,
		.name = "acp-sof-i2s2",
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
	},

	[PDM0_DMIC_INSTANCE] = {
		.id = PDM0_DMIC_INSTANCE,
		.name = "acp-sof-dmic0",
		.capture = {
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 2,
			.channels_max = 4,
			.rate_min = 8000,
			.rate_max = 48000,
		},
	},

	[PDM1_DMIC_INSTANCE] = {
		.id = PDM1_DMIC_INSTANCE,
		.name = "acp-sof-dmic1",
		.capture = {
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 2,
			.channels_max = 4,
			.rate_min = 8000,
			.rate_max = 96000,
		},
	},
};

static int sof_acp7x_post_fw_run_delay(struct snd_sof_dev *sdev)
{
	/*
	 * Resuming from suspend in some cases may cause the DSP firmware
	 * to enter an unrecoverable faulty state. Delaying a bit any host
	 * to DSP transmission right after firmware boot completion seems
	 * to resolve the issue.
	 */
	if (!sdev->first_boot)
		usleep_range(100, 150);

	return 0;
}

struct snd_sof_dsp_ops sof_acp7x_ops;
EXPORT_SYMBOL_NS(sof_acp7x_ops, "SND_SOC_SOF_AMD_COMMON");

int sof_acp7x_ops_init(struct snd_sof_dev *sdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&to_pci_dev(sdev->dev)->dev);
	const union acpi_object *obj;
	int acp_sof_signed_firmware_image = 0;
	int acp_sof_post_fw_run_delay = 0;

	/* common defaults */
	memcpy(&sof_acp7x_ops, &sof_acp_common_ops, sizeof(struct snd_sof_dsp_ops));

	sof_acp7x_ops.drv = acp7x_sof_dai;
	sof_acp7x_ops.num_drv = ARRAY_SIZE(acp7x_sof_dai);
	sof_acp7x_ops.probe = amd_sof_acp7x_probe;
	sof_acp7x_ops.remove = amd_sof_acp7x_remove;

	if (adev) {
		if (!acpi_dev_get_property(adev, "acp-sof-signed-firmware-image",
					   ACPI_TYPE_INTEGER, &obj))
			acp_sof_signed_firmware_image = obj->integer.value;

		if (!acpi_dev_get_property(adev, "acp-sof-post_fw_run_delay",
					   ACPI_TYPE_INTEGER, &obj))
			acp_sof_post_fw_run_delay = obj->integer.value;
	}

	if (acp_sof_signed_firmware_image)
		sof_acp7x_ops.load_firmware = acp_sof_load_signed_firmware;

	if (acp_sof_post_fw_run_delay)
		sof_acp7x_ops.post_fw_run = sof_acp7x_post_fw_run_delay;

	sof_acp7x_ops.suspend = amd_sof_acp7x_suspend;
	sof_acp7x_ops.resume = amd_sof_acp7x_resume;
	sof_acp7x_ops.runtime_suspend = amd_sof_acp7x_suspend_runtime;
	sof_acp7x_ops.runtime_resume = amd_sof_acp7x_resume_runtime;

	return 0;
}
