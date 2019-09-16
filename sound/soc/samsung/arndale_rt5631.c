// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (c) 2014, Insignal Co., Ltd.
//
//  Author: Claude <claude@insginal.co.kr>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "i2s.h"

static int arndale_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
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

static struct snd_soc_ops arndale_ops = {
	.hw_params = arndale_hw_params,
};

SND_SOC_DAILINK_DEFS(rt5631_hifi,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "rt5631-hifi")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link arndale_rt5631_dai[] = {
	{
		.name = "RT5631 HiFi",
		.stream_name = "Primary",
		.dai_fmt = SND_SOC_DAIFMT_I2S
			| SND_SOC_DAIFMT_NB_NF
			| SND_SOC_DAIFMT_CBS_CFS,
		.ops = &arndale_ops,
		SND_SOC_DAILINK_REG(rt5631_hifi),
	},
};

static struct snd_soc_card arndale_rt5631 = {
	.name = "Arndale RT5631",
	.owner = THIS_MODULE,
	.dai_link = arndale_rt5631_dai,
	.num_links = ARRAY_SIZE(arndale_rt5631_dai),
};

static int arndale_audio_probe(struct platform_device *pdev)
{
	int n, ret;
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_card *card = &arndale_rt5631;

	card->dev = &pdev->dev;

	for (n = 0; np && n < ARRAY_SIZE(arndale_rt5631_dai); n++) {
		if (!arndale_rt5631_dai[n].cpus->dai_name) {
			arndale_rt5631_dai[n].cpus->of_node = of_parse_phandle(np,
					"samsung,audio-cpu", n);

			if (!arndale_rt5631_dai[n].cpus->of_node) {
				dev_err(&pdev->dev,
				"Property 'samsung,audio-cpu' missing or invalid\n");
				return -EINVAL;
			}
		}
		if (!arndale_rt5631_dai[n].platforms->name)
			arndale_rt5631_dai[n].platforms->of_node =
					arndale_rt5631_dai[n].cpus->of_node;

		arndale_rt5631_dai[n].codecs->name = NULL;
		arndale_rt5631_dai[n].codecs->of_node = of_parse_phandle(np,
					"samsung,audio-codec", n);
		if (!arndale_rt5631_dai[0].codecs->of_node) {
			dev_err(&pdev->dev,
			"Property 'samsung,audio-codec' missing or invalid\n");
			return -EINVAL;
		}
	}

	ret = devm_snd_soc_register_card(card->dev, card);

	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed:%d\n", ret);

	return ret;
}

static const struct of_device_id samsung_arndale_rt5631_of_match[] __maybe_unused = {
	{ .compatible = "samsung,arndale-rt5631", },
	{ .compatible = "samsung,arndale-alc5631", },
	{},
};
MODULE_DEVICE_TABLE(of, samsung_arndale_rt5631_of_match);

static struct platform_driver arndale_audio_driver = {
	.driver = {
		.name   = "arndale-audio",
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(samsung_arndale_rt5631_of_match),
	},
	.probe = arndale_audio_probe,
};

module_platform_driver(arndale_audio_driver);

MODULE_AUTHOR("Claude <claude@insignal.co.kr>");
MODULE_DESCRIPTION("ALSA SoC Driver for Arndale Board");
MODULE_LICENSE("GPL");
