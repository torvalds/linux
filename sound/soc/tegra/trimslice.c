// SPDX-License-Identifier: GPL-2.0-only
/*
 * trimslice.c - TrimSlice machine ASoC driver
 *
 * Copyright (C) 2011 - CompuLab, Ltd.
 * Author: Mike Rapoport <mike@compulab.co.il>
 *
 * Based on code copyright/by:
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2010-2011 - NVIDIA, Inc.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "../codecs/tlv320aic23.h"

#include "tegra_asoc_utils.h"

#define DRV_NAME "tegra-snd-trimslice"

struct tegra_trimslice {
	struct tegra_asoc_utils_data util_data;
};

static int trimslice_asoc_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_card *card = rtd->card;
	struct tegra_trimslice *trimslice = snd_soc_card_get_drvdata(card);
	int srate, mclk;
	int err;

	srate = params_rate(params);
	mclk = 128 * srate;

	err = tegra_asoc_utils_set_rate(&trimslice->util_data, srate, mclk);
	if (err < 0) {
		dev_err(card->dev, "Can't configure clocks\n");
		return err;
	}

	err = snd_soc_dai_set_sysclk(codec_dai, 0, mclk,
					SND_SOC_CLOCK_IN);
	if (err < 0) {
		dev_err(card->dev, "codec_dai clock not set\n");
		return err;
	}

	return 0;
}

static const struct snd_soc_ops trimslice_asoc_ops = {
	.hw_params = trimslice_asoc_hw_params,
};

static const struct snd_soc_dapm_widget trimslice_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Line Out", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
};

static const struct snd_soc_dapm_route trimslice_audio_map[] = {
	{"Line Out", NULL, "LOUT"},
	{"Line Out", NULL, "ROUT"},

	{"LLINEIN", NULL, "Line In"},
	{"RLINEIN", NULL, "Line In"},
};

static struct snd_soc_dai_link trimslice_tlv320aic23_dai = {
	.name = "TLV320AIC23",
	.stream_name = "AIC23",
	.codec_dai_name = "tlv320aic23-hifi",
	.ops = &trimslice_asoc_ops,
	.dai_fmt = SND_SOC_DAIFMT_I2S |
		   SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBS_CFS,
};

static struct snd_soc_card snd_soc_trimslice = {
	.name = "tegra-trimslice",
	.owner = THIS_MODULE,
	.dai_link = &trimslice_tlv320aic23_dai,
	.num_links = 1,

	.dapm_widgets = trimslice_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(trimslice_dapm_widgets),
	.dapm_routes = trimslice_audio_map,
	.num_dapm_routes = ARRAY_SIZE(trimslice_audio_map),
	.fully_routed = true,
};

static int tegra_snd_trimslice_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_card *card = &snd_soc_trimslice;
	struct tegra_trimslice *trimslice;
	int ret;

	trimslice = devm_kzalloc(&pdev->dev, sizeof(struct tegra_trimslice),
				 GFP_KERNEL);
	if (!trimslice)
		return -ENOMEM;

	card->dev = &pdev->dev;
	snd_soc_card_set_drvdata(card, trimslice);

	trimslice_tlv320aic23_dai.codec_of_node = of_parse_phandle(np,
			"nvidia,audio-codec", 0);
	if (!trimslice_tlv320aic23_dai.codec_of_node) {
		dev_err(&pdev->dev,
			"Property 'nvidia,audio-codec' missing or invalid\n");
		ret = -EINVAL;
		goto err;
	}

	trimslice_tlv320aic23_dai.cpu_of_node = of_parse_phandle(np,
			"nvidia,i2s-controller", 0);
	if (!trimslice_tlv320aic23_dai.cpu_of_node) {
		dev_err(&pdev->dev,
			"Property 'nvidia,i2s-controller' missing or invalid\n");
		ret = -EINVAL;
		goto err;
	}

	trimslice_tlv320aic23_dai.platform_of_node =
			trimslice_tlv320aic23_dai.cpu_of_node;

	ret = tegra_asoc_utils_init(&trimslice->util_data, &pdev->dev);
	if (ret)
		goto err;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		goto err_fini_utils;
	}

	return 0;

err_fini_utils:
	tegra_asoc_utils_fini(&trimslice->util_data);
err:
	return ret;
}

static int tegra_snd_trimslice_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct tegra_trimslice *trimslice = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);

	tegra_asoc_utils_fini(&trimslice->util_data);

	return 0;
}

static const struct of_device_id trimslice_of_match[] = {
	{ .compatible = "nvidia,tegra-audio-trimslice", },
	{},
};
MODULE_DEVICE_TABLE(of, trimslice_of_match);

static struct platform_driver tegra_snd_trimslice_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = trimslice_of_match,
	},
	.probe = tegra_snd_trimslice_probe,
	.remove = tegra_snd_trimslice_remove,
};
module_platform_driver(tegra_snd_trimslice_driver);

MODULE_AUTHOR("Mike Rapoport <mike@compulab.co.il>");
MODULE_DESCRIPTION("Trimslice machine ASoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
