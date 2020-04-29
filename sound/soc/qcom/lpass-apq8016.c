// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2011,2013-2015 The Linux Foundation. All rights reserved.
 *
 * lpass-apq8016.c -- ALSA SoC CPU DAI driver for APQ8016 LPASS
 */


#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include <dt-bindings/sound/apq8016-lpass.h>
#include "lpass-lpaif-reg.h"
#include "lpass.h"

static struct snd_soc_dai_driver apq8016_lpass_cpu_dai_driver[] = {
	[MI2S_PRIMARY] =  {
		.id = MI2S_PRIMARY,
		.name = "Primary MI2S",
		.playback = {
			.stream_name	= "Primary Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
						SNDRV_PCM_FMTBIT_S24 |
						SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000 |
						SNDRV_PCM_RATE_16000 |
						SNDRV_PCM_RATE_32000 |
						SNDRV_PCM_RATE_48000 |
						SNDRV_PCM_RATE_96000,
			.rate_min	= 8000,
			.rate_max	= 96000,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.probe	= &asoc_qcom_lpass_cpu_dai_probe,
		.ops    = &asoc_qcom_lpass_cpu_dai_ops,
	},
	[MI2S_SECONDARY] =  {
		.id = MI2S_SECONDARY,
		.name = "Secondary MI2S",
		.playback = {
			.stream_name	= "Secondary Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
						SNDRV_PCM_FMTBIT_S24 |
						SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000 |
						SNDRV_PCM_RATE_16000 |
						SNDRV_PCM_RATE_32000 |
						SNDRV_PCM_RATE_48000 |
						SNDRV_PCM_RATE_96000,
			.rate_min	= 8000,
			.rate_max	= 96000,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.probe	= &asoc_qcom_lpass_cpu_dai_probe,
		.ops    = &asoc_qcom_lpass_cpu_dai_ops,
	},
	[MI2S_TERTIARY] =  {
		.id = MI2S_TERTIARY,
		.name = "Tertiary MI2S",
		.capture = {
			.stream_name	= "Tertiary Capture",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
						SNDRV_PCM_FMTBIT_S24 |
						SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000 |
						SNDRV_PCM_RATE_16000 |
						SNDRV_PCM_RATE_32000 |
						SNDRV_PCM_RATE_48000 |
						SNDRV_PCM_RATE_96000,
			.rate_min	= 8000,
			.rate_max	= 96000,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.probe	= &asoc_qcom_lpass_cpu_dai_probe,
		.ops    = &asoc_qcom_lpass_cpu_dai_ops,
	},
	[MI2S_QUATERNARY] =  {
		.id = MI2S_QUATERNARY,
		.name = "Quatenary MI2S",
		.playback = {
			.stream_name	= "Quatenary Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
						SNDRV_PCM_FMTBIT_S24 |
						SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000 |
						SNDRV_PCM_RATE_16000 |
						SNDRV_PCM_RATE_32000 |
						SNDRV_PCM_RATE_48000 |
						SNDRV_PCM_RATE_96000,
			.rate_min	= 8000,
			.rate_max	= 96000,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.capture = {
			.stream_name	= "Quatenary Capture",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
						SNDRV_PCM_FMTBIT_S24 |
						SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000 |
						SNDRV_PCM_RATE_16000 |
						SNDRV_PCM_RATE_32000 |
						SNDRV_PCM_RATE_48000 |
						SNDRV_PCM_RATE_96000,
			.rate_min	= 8000,
			.rate_max	= 96000,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.probe	= &asoc_qcom_lpass_cpu_dai_probe,
		.ops    = &asoc_qcom_lpass_cpu_dai_ops,
	},
};

static int apq8016_lpass_alloc_dma_channel(struct lpass_data *drvdata,
					   int direction)
{
	struct lpass_variant *v = drvdata->variant;
	int chan = 0;

	if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
		chan = find_first_zero_bit(&drvdata->dma_ch_bit_map,
					v->rdma_channels);

		if (chan >= v->rdma_channels)
			return -EBUSY;
	} else {
		chan = find_next_zero_bit(&drvdata->dma_ch_bit_map,
					v->wrdma_channel_start +
					v->wrdma_channels,
					v->wrdma_channel_start);

		if (chan >=  v->wrdma_channel_start + v->wrdma_channels)
			return -EBUSY;
	}

	set_bit(chan, &drvdata->dma_ch_bit_map);

	return chan;
}

static int apq8016_lpass_free_dma_channel(struct lpass_data *drvdata, int chan)
{
	clear_bit(chan, &drvdata->dma_ch_bit_map);

	return 0;
}

static int apq8016_lpass_init(struct platform_device *pdev)
{
	struct lpass_data *drvdata = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int ret;

	drvdata->pcnoc_mport_clk = devm_clk_get(dev, "pcnoc-mport-clk");
	if (IS_ERR(drvdata->pcnoc_mport_clk)) {
		dev_err(&pdev->dev, "error getting pcnoc-mport-clk: %ld\n",
			PTR_ERR(drvdata->pcnoc_mport_clk));
		return PTR_ERR(drvdata->pcnoc_mport_clk);
	}

	ret = clk_prepare_enable(drvdata->pcnoc_mport_clk);
	if (ret) {
		dev_err(&pdev->dev, "Error enabling pcnoc-mport-clk: %d\n",
			ret);
		return ret;
	}

	drvdata->pcnoc_sway_clk = devm_clk_get(dev, "pcnoc-sway-clk");
	if (IS_ERR(drvdata->pcnoc_sway_clk)) {
		dev_err(&pdev->dev, "error getting pcnoc-sway-clk: %ld\n",
			PTR_ERR(drvdata->pcnoc_sway_clk));
		return PTR_ERR(drvdata->pcnoc_sway_clk);
	}

	ret = clk_prepare_enable(drvdata->pcnoc_sway_clk);
	if (ret) {
		dev_err(&pdev->dev, "Error enabling pcnoc_sway_clk: %d\n", ret);
		return ret;
	}

	return 0;
}

static int apq8016_lpass_exit(struct platform_device *pdev)
{
	struct lpass_data *drvdata = platform_get_drvdata(pdev);

	clk_disable_unprepare(drvdata->pcnoc_mport_clk);
	clk_disable_unprepare(drvdata->pcnoc_sway_clk);

	return 0;
}


static struct lpass_variant apq8016_data = {
	.i2sctrl_reg_base	= 0x1000,
	.i2sctrl_reg_stride	= 0x1000,
	.i2s_ports		= 4,
	.irq_reg_base		= 0x6000,
	.irq_reg_stride		= 0x1000,
	.irq_ports		= 3,
	.rdma_reg_base		= 0x8400,
	.rdma_reg_stride	= 0x1000,
	.rdma_channels		= 2,
	.dmactl_audif_start	= 1,
	.wrdma_reg_base		= 0xB000,
	.wrdma_reg_stride	= 0x1000,
	.wrdma_channel_start	= 5,
	.wrdma_channels		= 2,
	.dai_driver		= apq8016_lpass_cpu_dai_driver,
	.num_dai		= ARRAY_SIZE(apq8016_lpass_cpu_dai_driver),
	.dai_osr_clk_names	= (const char *[]) {
				"mi2s-osr-clk0",
				"mi2s-osr-clk1",
				"mi2s-osr-clk2",
				"mi2s-osr-clk3",
				},
	.dai_bit_clk_names	= (const char *[]) {
				"mi2s-bit-clk0",
				"mi2s-bit-clk1",
				"mi2s-bit-clk2",
				"mi2s-bit-clk3",
				},
	.init			= apq8016_lpass_init,
	.exit			= apq8016_lpass_exit,
	.alloc_dma_channel	= apq8016_lpass_alloc_dma_channel,
	.free_dma_channel	= apq8016_lpass_free_dma_channel,
};

static const struct of_device_id apq8016_lpass_cpu_device_id[] = {
	{ .compatible = "qcom,lpass-cpu-apq8016", .data = &apq8016_data },
	{}
};
MODULE_DEVICE_TABLE(of, apq8016_lpass_cpu_device_id);

static struct platform_driver apq8016_lpass_cpu_platform_driver = {
	.driver	= {
		.name		= "apq8016-lpass-cpu",
		.of_match_table	= of_match_ptr(apq8016_lpass_cpu_device_id),
	},
	.probe	= asoc_qcom_lpass_cpu_platform_probe,
	.remove	= asoc_qcom_lpass_cpu_platform_remove,
};
module_platform_driver(apq8016_lpass_cpu_platform_driver);

MODULE_DESCRIPTION("APQ8016 LPASS CPU Driver");
MODULE_LICENSE("GPL v2");

