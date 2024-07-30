// SPDX-License-Identifier: GPL-2.0-or-later
//
// Author: Kevin Wells <kevin.wells@nxp.com>
//
// Copyright (C) 2008 NXP Semiconductors
// Copyright 2023 Timesys Corporation <piotr.wojtaszczyk@timesys.com>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/amba/pl08x.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>
#include <sound/soc.h>

#include "lpc3xxx-i2s.h"

#define STUB_FORMATS	(SNDRV_PCM_FMTBIT_S8 | \
			SNDRV_PCM_FMTBIT_U8 | \
			SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_U16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_U24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE | \
			SNDRV_PCM_FMTBIT_U32_LE | \
			SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE)

static const struct snd_pcm_hardware lpc3xxx_pcm_hardware = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_PAUSE |
		 SNDRV_PCM_INFO_RESUME),
	.formats = STUB_FORMATS,
	.period_bytes_min = 128,
	.period_bytes_max = 2048,
	.periods_min = 2,
	.periods_max = 1024,
	.buffer_bytes_max = 128 * 1024
};

static const struct snd_dmaengine_pcm_config lpc3xxx_dmaengine_pcm_config = {
	.pcm_hardware = &lpc3xxx_pcm_hardware,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.compat_filter_fn = pl08x_filter_id,
	.prealloc_buffer_size = 128 * 1024,
};

const struct snd_soc_component_driver lpc3xxx_soc_platform_driver = {
	.name = "lpc32xx-pcm",
};

int lpc3xxx_pcm_register(struct platform_device *pdev)
{
	int ret;

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, &lpc3xxx_dmaengine_pcm_config, 0);
	if (ret) {
		dev_err(&pdev->dev, "failed to register dmaengine: %d\n", ret);
		return ret;
	}

	return devm_snd_soc_register_component(&pdev->dev, &lpc3xxx_soc_platform_driver,
					       NULL, 0);
}
EXPORT_SYMBOL(lpc3xxx_pcm_register);
