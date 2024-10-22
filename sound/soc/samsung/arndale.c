// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (c) 2014, Insignal Co., Ltd.
//
//  Author: Claude <claude@insginal.co.kr>

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "../codecs/wm8994.h"
#include "i2s.h"

static int arndale_rt5631_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int rfs, ret;
	unsigned long rclk;

	rfs = 256;

	rclk = params_rate(params) * rfs;

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_CDCLK,
					0, SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_RCLKSRC_0,
					0, SND_SOC_CLOCK_OUT);

	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, rclk, SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_soc_ops arndale_rt5631_ops = {
	.hw_params = arndale_rt5631_hw_params,
};

static int arndale_wm1811_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int rfs, rclk;

	/* Ensure AIF1CLK is >= 3 MHz for optimal performance */
	if (params_width(params) == 24)
		rfs = 384;
	else if (params_rate(params) == 8000 || params_rate(params) == 11025)
		rfs = 512;
	else
		rfs = 256;

	rclk = params_rate(params) * rfs;

	/*
	 * We add 1 to the frequency value to ensure proper EPLL setting
	 * for each audio sampling rate (see epll_24mhz_tbl in drivers/clk/
	 * samsung/clk-exynos5250.c for list of available EPLL rates).
	 * The CODEC uses clk API and the value will be rounded hence the MCLK1
	 * clock's frequency will still be exact multiple of the sample rate.
	 */
	return snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_MCLK1,
					rclk + 1, SND_SOC_CLOCK_IN);
}

static const struct snd_soc_ops arndale_wm1811_ops = {
	.hw_params = arndale_wm1811_hw_params,
};

SND_SOC_DAILINK_DEFS(rt5631_hifi,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "rt5631-aif1")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link arndale_rt5631_dai[] = {
	{
		.name = "RT5631 HiFi",
		.stream_name = "Primary",
		.dai_fmt = SND_SOC_DAIFMT_I2S
			| SND_SOC_DAIFMT_NB_NF
			| SND_SOC_DAIFMT_CBS_CFS,
		.ops = &arndale_rt5631_ops,
		SND_SOC_DAILINK_REG(rt5631_hifi),
	},
};

SND_SOC_DAILINK_DEFS(wm1811_hifi,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "wm8994-aif1")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link arndale_wm1811_dai[] = {
	{
		.name = "WM1811 HiFi",
		.stream_name = "Primary",
		.dai_fmt = SND_SOC_DAIFMT_I2S
			| SND_SOC_DAIFMT_NB_NF
			| SND_SOC_DAIFMT_CBM_CFM,
		.ops = &arndale_wm1811_ops,
		SND_SOC_DAILINK_REG(wm1811_hifi),
	},
};

static struct snd_soc_card arndale_rt5631 = {
	.name = "Arndale RT5631",
	.owner = THIS_MODULE,
	.dai_link = arndale_rt5631_dai,
	.num_links = ARRAY_SIZE(arndale_rt5631_dai),
};

static struct snd_soc_card arndale_wm1811 = {
	.name = "Arndale WM1811",
	.owner = THIS_MODULE,
	.dai_link = arndale_wm1811_dai,
	.num_links = ARRAY_SIZE(arndale_wm1811_dai),
};

static void arndale_put_of_nodes(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *dai_link;
	int i;

	for_each_card_prelinks(card, i, dai_link) {
		of_node_put(dai_link->cpus->of_node);
		of_node_put(dai_link->codecs->of_node);
	}
}

static int arndale_audio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_card *card;
	struct snd_soc_dai_link *dai_link;
	int ret;

	card = (struct snd_soc_card *)of_device_get_match_data(&pdev->dev);
	card->dev = &pdev->dev;
	dai_link = card->dai_link;

	dai_link->cpus->of_node = of_parse_phandle(np, "samsung,audio-cpu", 0);
	if (!dai_link->cpus->of_node) {
		dev_err(&pdev->dev,
			"Property 'samsung,audio-cpu' missing or invalid\n");
		return -EINVAL;
	}

	if (!dai_link->platforms->name)
		dai_link->platforms->of_node = dai_link->cpus->of_node;

	dai_link->codecs->of_node = of_parse_phandle(np, "samsung,audio-codec", 0);
	if (!dai_link->codecs->of_node) {
		dev_err(&pdev->dev,
			"Property 'samsung,audio-codec' missing or invalid\n");
		ret = -EINVAL;
		goto err_put_of_nodes;
	}

	ret = devm_snd_soc_register_card(card->dev, card);
	if (ret) {
		dev_err_probe(&pdev->dev, ret,
			      "snd_soc_register_card() failed\n");
		goto err_put_of_nodes;
	}
	return 0;

err_put_of_nodes:
	arndale_put_of_nodes(card);
	return ret;
}

static void arndale_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	arndale_put_of_nodes(card);
}

static const struct of_device_id arndale_audio_of_match[] = {
	{ .compatible = "samsung,arndale-rt5631",  .data = &arndale_rt5631 },
	{ .compatible = "samsung,arndale-alc5631", .data = &arndale_rt5631 },
	{ .compatible = "samsung,arndale-wm1811",  .data = &arndale_wm1811 },
	{},
};
MODULE_DEVICE_TABLE(of, arndale_audio_of_match);

static struct platform_driver arndale_audio_driver = {
	.driver = {
		.name = "arndale-audio",
		.pm = &snd_soc_pm_ops,
		.of_match_table = arndale_audio_of_match,
	},
	.probe = arndale_audio_probe,
	.remove = arndale_audio_remove,
};

module_platform_driver(arndale_audio_driver);

MODULE_AUTHOR("Claude <claude@insignal.co.kr>");
MODULE_DESCRIPTION("ALSA SoC Driver for Arndale Board");
MODULE_LICENSE("GPL");
