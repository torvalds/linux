// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 * lpass-sc7180.c -- ALSA SoC platform-machine driver for QTi LPASS
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <dt-bindings/sound/sc7180-lpass.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include "lpass-lpaif-reg.h"
#include "lpass.h"

static struct snd_soc_dai_driver sc7180_lpass_cpu_dai_driver[] = {
	[MI2S_PRIMARY] = {
		.id = MI2S_PRIMARY,
		.name = "Primary MI2S",
		.playback = {
			.stream_name = "Primary Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.capture = {
			.stream_name = "Primary Capture",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.probe	= &asoc_qcom_lpass_cpu_dai_probe,
		.ops    = &asoc_qcom_lpass_cpu_dai_ops,
	},

	[MI2S_SECONDARY] = {
		.id = MI2S_SECONDARY,
		.name = "Secondary MI2S",
		.playback = {
			.stream_name = "Secondary Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.probe	= &asoc_qcom_lpass_cpu_dai_probe,
		.ops    = &asoc_qcom_lpass_cpu_dai_ops,
	},
};

static int sc7180_lpass_alloc_dma_channel(struct lpass_data *drvdata,
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

static int sc7180_lpass_free_dma_channel(struct lpass_data *drvdata, int chan)
{
	clear_bit(chan, &drvdata->dma_ch_bit_map);

	return 0;
}

static int sc7180_lpass_init(struct platform_device *pdev)
{
	struct lpass_data *drvdata = platform_get_drvdata(pdev);
	struct lpass_variant *variant = drvdata->variant;
	struct device *dev = &pdev->dev;
	int ret, i;

	drvdata->clks = devm_kcalloc(dev, variant->num_clks,
				     sizeof(*drvdata->clks), GFP_KERNEL);
	drvdata->num_clks = variant->num_clks;

	for (i = 0; i < drvdata->num_clks; i++)
		drvdata->clks[i].id = variant->clk_name[i];

	ret = devm_clk_bulk_get(dev, drvdata->num_clks, drvdata->clks);
	if (ret) {
		dev_err(dev, "Failed to get clocks %d\n", ret);
		return ret;
	}

	ret = clk_bulk_prepare_enable(drvdata->num_clks, drvdata->clks);
	if (ret) {
		dev_err(dev, "sc7180 clk_enable failed\n");
		return ret;
	}

	return 0;
}

static int sc7180_lpass_exit(struct platform_device *pdev)
{
	struct lpass_data *drvdata = platform_get_drvdata(pdev);

	clk_bulk_disable_unprepare(drvdata->num_clks, drvdata->clks);

	return 0;
}

static struct lpass_variant sc7180_data = {
	.i2sctrl_reg_base	= 0x1000,
	.i2sctrl_reg_stride	= 0x1000,
	.i2s_ports		= 3,
	.irq_reg_base		= 0x9000,
	.irq_reg_stride		= 0x1000,
	.irq_ports		= 3,
	.rdma_reg_base		= 0xC000,
	.rdma_reg_stride	= 0x1000,
	.rdma_channels		= 5,
	.dmactl_audif_start	= 1,
	.wrdma_reg_base		= 0x18000,
	.wrdma_reg_stride	= 0x1000,
	.wrdma_channel_start	= 5,
	.wrdma_channels		= 4,

	.loopback		= REG_FIELD_ID(0x1000, 17, 17, 3, 0x1000),
	.spken			= REG_FIELD_ID(0x1000, 16, 16, 3, 0x1000),
	.spkmode		= REG_FIELD_ID(0x1000, 11, 15, 3, 0x1000),
	.spkmono		= REG_FIELD_ID(0x1000, 10, 10, 3, 0x1000),
	.micen			= REG_FIELD_ID(0x1000, 9, 9, 3, 0x1000),
	.micmode		= REG_FIELD_ID(0x1000, 4, 8, 3, 0x1000),
	.micmono		= REG_FIELD_ID(0x1000, 3, 3, 3, 0x1000),
	.wssrc			= REG_FIELD_ID(0x1000, 2, 2, 3, 0x1000),
	.bitwidth		= REG_FIELD_ID(0x1000, 0, 0, 3, 0x1000),

	.rdma_dyncclk		= REG_FIELD_ID(0xC000, 21, 21, 5, 0x1000),
	.rdma_bursten		= REG_FIELD_ID(0xC000, 20, 20, 5, 0x1000),
	.rdma_wpscnt		= REG_FIELD_ID(0xC000, 16, 19, 5, 0x1000),
	.rdma_intf		= REG_FIELD_ID(0xC000, 12, 15, 5, 0x1000),
	.rdma_fifowm		= REG_FIELD_ID(0xC000, 1, 5, 5, 0x1000),
	.rdma_enable		= REG_FIELD_ID(0xC000, 0, 0, 5, 0x1000),

	.wrdma_dyncclk		= REG_FIELD_ID(0x18000, 22, 22, 4, 0x1000),
	.wrdma_bursten		= REG_FIELD_ID(0x18000, 21, 21, 4, 0x1000),
	.wrdma_wpscnt		= REG_FIELD_ID(0x18000, 17, 20, 4, 0x1000),
	.wrdma_intf		= REG_FIELD_ID(0x18000, 12, 16, 4, 0x1000),
	.wrdma_fifowm		= REG_FIELD_ID(0x18000, 1, 5, 4, 0x1000),
	.wrdma_enable		= REG_FIELD_ID(0x18000, 0, 0, 4, 0x1000),

	.clk_name		= (const char*[]) {
				   "pcnoc-sway-clk",
				   "audio-core",
				   "pcnoc-mport-clk",
				},
	.num_clks		= 3,
	.dai_driver		= sc7180_lpass_cpu_dai_driver,
	.num_dai		= ARRAY_SIZE(sc7180_lpass_cpu_dai_driver),
	.dai_osr_clk_names      = (const char *[]) {
				   "mclk0",
				   "null",
				},
	.dai_bit_clk_names      = (const char *[]) {
				   "mi2s-bit-clk0",
				   "mi2s-bit-clk1",
				},
	.init			= sc7180_lpass_init,
	.exit			= sc7180_lpass_exit,
	.alloc_dma_channel	= sc7180_lpass_alloc_dma_channel,
	.free_dma_channel	= sc7180_lpass_free_dma_channel,
};

static const struct of_device_id sc7180_lpass_cpu_device_id[] = {
	{.compatible = "qcom,sc7180-lpass-cpu", .data = &sc7180_data},
	{}
};

static struct platform_driver sc7180_lpass_cpu_platform_driver = {
	.driver = {
		.name = "sc7180-lpass-cpu",
		.of_match_table = of_match_ptr(sc7180_lpass_cpu_device_id),
	},
	.probe = asoc_qcom_lpass_cpu_platform_probe,
	.remove = asoc_qcom_lpass_cpu_platform_remove,
};

module_platform_driver(sc7180_lpass_cpu_platform_driver);

MODULE_DESCRIPTION("SC7180 LPASS CPU DRIVER");
MODULE_LICENSE("GPL v2");
