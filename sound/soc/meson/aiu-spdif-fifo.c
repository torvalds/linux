// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2019 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "aiu-fifo.h"

#define AIU_IEC958_BPF				0x000
#define AIU_IEC958_DCU_FF_CTRL			0x01c
#define  AIU_IEC958_DCU_FF_CTRL_EN		BIT(0)
#define  AIU_IEC958_DCU_FF_CTRL_AUTO_DISABLE	BIT(1)
#define  AIU_IEC958_DCU_FF_CTRL_IRQ_MODE	GENMASK(3, 2)
#define  AIU_IEC958_DCU_FF_CTRL_IRQ_OUT_THD	BIT(2)
#define  AIU_IEC958_DCU_FF_CTRL_IRQ_FRAME_READ 	BIT(3)
#define  AIU_IEC958_DCU_FF_CTRL_SYNC_HEAD_EN	BIT(4)
#define  AIU_IEC958_DCU_FF_CTRL_BYTE_SEEK	BIT(5)
#define  AIU_IEC958_DCU_FF_CTRL_CONTINUE	BIT(6)
#define AIU_MEM_IEC958_START			0x194
#define AIU_MEM_IEC958_CONTROL			0x1a4
#define AIU_MEM_IEC958_CONTROL_ENDIAN		GENMASK(5, 3)
#define AIU_MEM_IEC958_CONTROL_RD_DDR		BIT(6)
#define AIU_MEM_IEC958_CONTROL_MODE_16BIT	BIT(7)
#define AIU_MEM_IEC958_CONTROL_MODE_LINEAR	BIT(8)
#define AIU_MEM_IEC958_BUF_CNTL			0x1fc
#define  AIU_MEM_IEC958_BUF_CNTL_INIT		BIT(0)

#define AIU_SPDIF_FIFO_BLOCK 			8
#define AIU_SPDIF_FIFO_FORMATS 			(SNDRV_PCM_FMTBIT_S16_LE | \
						 SNDRV_PCM_FMTBIT_S20_LE | \
						 SNDRV_PCM_FMTBIT_S24_LE)

static void spdif_fifo_dcu_enable(struct snd_soc_component *component,
				  bool enable)
{
	snd_soc_component_update_bits(component, AIU_IEC958_DCU_FF_CTRL,
				      AIU_IEC958_DCU_FF_CTRL_EN,
				      enable ? AIU_IEC958_DCU_FF_CTRL_EN : 0);
}

static int spdif_fifo_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	int ret;

	ret = aiu_fifo_trigger(substream, cmd, dai);
	if (ret)
		return ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	        spdif_fifo_dcu_enable(component, true);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		spdif_fifo_dcu_enable(component, false);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int spdif_fifo_prepare(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	int ret;

	ret = aiu_fifo_prepare(substream, dai);
	if (ret)
		return ret;

	snd_soc_component_update_bits(component,
				      AIU_MEM_IEC958_BUF_CNTL,
				      AIU_MEM_IEC958_BUF_CNTL_INIT,
				      AIU_MEM_IEC958_BUF_CNTL_INIT);
	snd_soc_component_update_bits(component,
				      AIU_MEM_IEC958_BUF_CNTL,
				      AIU_MEM_IEC958_BUF_CNTL_INIT, 0);

	return 0;
}

static int spdif_fifo_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	unsigned int val;
	int ret;

	ret = aiu_fifo_hw_params(substream, params, dai);
	if (ret)
		return ret;

	val = AIU_MEM_IEC958_CONTROL_RD_DDR |
	      AIU_MEM_IEC958_CONTROL_MODE_LINEAR;

	switch (params_physical_width(params)) {
	case 16:
		val |= AIU_MEM_IEC958_CONTROL_MODE_16BIT;
		break;
	case 32:
		break;
	default:
		dev_err(dai->dev, "Unsupported physical width %u\n",
			params_physical_width(params));
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, AIU_MEM_IEC958_CONTROL,
				      AIU_MEM_IEC958_CONTROL_ENDIAN |
				      AIU_MEM_IEC958_CONTROL_RD_DDR |
				      AIU_MEM_IEC958_CONTROL_MODE_LINEAR |
				      AIU_MEM_IEC958_CONTROL_MODE_16BIT,
				      val);

	/* Number bytes read by the FIFO between each IRQ */
	snd_soc_component_write(component, AIU_IEC958_BPF,
				params_period_bytes(params));

	/*
	 * AUTO_DISABLE and SYNC_HEAD are enabled by default but
	 * this should be disabled in PCM (uncompressed) mode
	 */
	snd_soc_component_update_bits(component, AIU_IEC958_DCU_FF_CTRL,
				      AIU_IEC958_DCU_FF_CTRL_AUTO_DISABLE |
				      AIU_IEC958_DCU_FF_CTRL_IRQ_MODE |
				      AIU_IEC958_DCU_FF_CTRL_SYNC_HEAD_EN,
				      AIU_IEC958_DCU_FF_CTRL_IRQ_FRAME_READ);

	return 0;
}

static const struct snd_soc_dai_ops spdif_fifo_dai_ops = {
	.trigger	= spdif_fifo_trigger,
	.prepare	= spdif_fifo_prepare,
	.hw_params	= spdif_fifo_hw_params,
	.hw_free	= aiu_fifo_hw_free,
	.startup	= aiu_fifo_startup,
	.shutdown	= aiu_fifo_shutdown,
};

static struct snd_soc_dai_driver spdif_fifo_dai_drv = {
	.name = "AIU SPDIF FIFO",
	.playback = {
		.stream_name	= "Playback",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min	= 5512,
		.rate_max	= 192000,
		.formats	= AIU_SPDIF_FIFO_FORMATS,
	},
	.ops		= &spdif_fifo_dai_ops,
	.pcm_new	= aiu_fifo_pcm_new,
};

static const struct snd_soc_component_driver spdif_fifo_component_drv = {
	.probe		= aiu_fifo_component_probe,
	.pointer	= aiu_fifo_pointer,
};

static struct snd_pcm_hardware spdif_fifo_pcm = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_PAUSE),
	.formats = AIU_SPDIF_FIFO_FORMATS,
	.rate_min = 5512,
	.rate_max = 192000,
	.channels_min = 2,
	.channels_max = 2,
	.period_bytes_min = AIU_SPDIF_FIFO_BLOCK,
	.period_bytes_max = AIU_SPDIF_FIFO_BLOCK * USHRT_MAX,
	.periods_min = 2,
	.periods_max = UINT_MAX,

	/* No real justification for this */
	.buffer_bytes_max = 1 * 1024 * 1024,
};

static const struct aiu_fifo_hw spdif_fifo_hw = {
	.fifo_block = /*AIU_SPDIF_FIFO_BLOCK*/ 1,
	.mem_offset = AIU_MEM_IEC958_START,
	.pcm = &spdif_fifo_pcm,
};

static const struct aiu_fifo_match_data spdif_fifo_data = {
	.component_drv = &spdif_fifo_component_drv,
	.dai_drv = &spdif_fifo_dai_drv,
	.hw = &spdif_fifo_hw,
};

static const struct of_device_id aiu_spdif_fifo_of_match[] = {
	{
		.compatible = "amlogic,aiu-spdif-fifo",
		.data = &spdif_fifo_data,
	}, {}
};
MODULE_DEVICE_TABLE(of, aiu_spdif_fifo_of_match);

static struct platform_driver aiu_spdif_fifo_pdrv = {
	.probe = aiu_fifo_probe,
	.driver = {
		.name = "meson-aiu-spdif-fifo",
		.of_match_table = aiu_spdif_fifo_of_match,
	},
};
module_platform_driver(aiu_spdif_fifo_pdrv);

MODULE_DESCRIPTION("Amlogic AIU SPDIF FIFO driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
