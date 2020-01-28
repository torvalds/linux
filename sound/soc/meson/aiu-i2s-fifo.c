// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2019 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "aiu-fifo.h"

#define AIU_MEM_I2S_START		0x180
#define AIU_MEM_I2S_MASKS		0x18c
#define  AIU_MEM_I2S_MASKS_IRQ_BLOCK	GENMASK(31, 16)
#define AIU_MEM_I2S_CONTROL		0x190
#define  AIU_MEM_I2S_CONTROL_MODE_16BIT	BIT(6)
#define AIU_MEM_I2S_BUF_CNTL		0x1d8
#define  AIU_MEM_I2S_BUF_CNTL_INIT	BIT(0)

#define AIU_I2S_FIFO_BLOCK 		256
#define AIU_I2S_FIFO_FORMATS 		(SNDRV_PCM_FMTBIT_S16_LE | \
					 SNDRV_PCM_FMTBIT_S20_LE | \
					 SNDRV_PCM_FMTBIT_S24_LE | \
					 SNDRV_PCM_FMTBIT_S32_LE)

static int i2s_fifo_prepare(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	int ret;

	ret = aiu_fifo_prepare(substream, dai);
	if (ret)
		return ret;

	snd_soc_component_update_bits(component,
				      AIU_MEM_I2S_BUF_CNTL,
				      AIU_MEM_I2S_BUF_CNTL_INIT,
				      AIU_MEM_I2S_BUF_CNTL_INIT);
	snd_soc_component_update_bits(component,
				      AIU_MEM_I2S_BUF_CNTL,
				      AIU_MEM_I2S_BUF_CNTL_INIT, 0);

	return 0;
}

static int i2s_fifo_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct aiu_fifo *fifo = snd_soc_component_get_drvdata(component);
	unsigned int val;
	int ret;

	ret = aiu_fifo_hw_params(substream, params, dai);
	if (ret)
		return ret;

	switch (params_physical_width(params)) {
	case 16:
		val = AIU_MEM_I2S_CONTROL_MODE_16BIT;
		break;
	case 32:
		val = 0;
		break;
	default:
		dev_err(dai->dev, "Unsupported physical width %u\n",
			params_physical_width(params));
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, AIU_MEM_I2S_CONTROL,
				      AIU_MEM_I2S_CONTROL_MODE_16BIT,
				      val);

	/* Setup the irq periodicity */
	val = params_period_bytes(params) / fifo->hw->fifo_block;
	val = FIELD_PREP(AIU_MEM_I2S_MASKS_IRQ_BLOCK, val);
	snd_soc_component_update_bits(component, AIU_MEM_I2S_MASKS,
				      AIU_MEM_I2S_MASKS_IRQ_BLOCK, val);

	return 0;
}

static const struct snd_soc_dai_ops i2s_fifo_dai_ops = {
	.trigger	= aiu_fifo_trigger,
	.prepare	= i2s_fifo_prepare,
	.hw_params	= i2s_fifo_hw_params,
	.hw_free	= aiu_fifo_hw_free,
	.startup	= aiu_fifo_startup,
	.shutdown	= aiu_fifo_shutdown,
};

static struct snd_soc_dai_driver i2s_fifo_dai_drv = {
	.name = "AIU I2S FIFO",
	.playback = {
		.stream_name	= "Playback",
		.channels_min	= 2,
		.channels_max	= 8,
		.rates		= SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min	= 5512,
		.rate_max	= 192000,
		.formats	= AIU_I2S_FIFO_FORMATS,
	},
	.ops		= &i2s_fifo_dai_ops,
	.pcm_new	= aiu_fifo_pcm_new,
};

static const struct snd_soc_component_driver i2s_fifo_component_drv = {
	.probe		= aiu_fifo_component_probe,
	.pointer	= aiu_fifo_pointer,
};

static struct snd_pcm_hardware i2s_fifo_pcm = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_PAUSE),
	.formats = AIU_I2S_FIFO_FORMATS,
	.rate_min = 5512,
	.rate_max = 192000,
	.channels_min = 2,
	.channels_max = 8,
	.period_bytes_min = AIU_I2S_FIFO_BLOCK,
	.period_bytes_max = AIU_I2S_FIFO_BLOCK * USHRT_MAX,
	.periods_min = 2,
	.periods_max = UINT_MAX,

	/* No real justification for this */
	.buffer_bytes_max = 1 * 1024 * 1024,
};

static const struct aiu_fifo_hw i2s_fifo_hw = {
	.fifo_block = AIU_I2S_FIFO_BLOCK,
	.mem_offset = AIU_MEM_I2S_START,
	.pcm = &i2s_fifo_pcm,
};

static const struct aiu_fifo_match_data i2s_fifo_data = {
	.component_drv = &i2s_fifo_component_drv,
	.dai_drv = &i2s_fifo_dai_drv,
	.hw = &i2s_fifo_hw,
};

static const struct of_device_id aiu_i2s_fifo_of_match[] = {
	{
		.compatible = "amlogic,aiu-i2s-fifo",
		.data = &i2s_fifo_data,
	}, {}
};
MODULE_DEVICE_TABLE(of, aiu_i2s_fifo_of_match);

static struct platform_driver aiu_i2s_fifo_pdrv = {
	.probe = aiu_fifo_probe,
	.driver = {
		.name = "meson-aiu-i2s-fifo",
		.of_match_table = aiu_i2s_fifo_of_match,
	},
};
module_platform_driver(aiu_i2s_fifo_pdrv);

MODULE_DESCRIPTION("Amlogic AIU I2S FIFO driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
