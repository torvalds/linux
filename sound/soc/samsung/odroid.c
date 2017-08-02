/*
 * Copyright (C) 2017 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include "i2s.h"
#include "i2s-regs.h"

struct odroid_priv {
	struct snd_soc_card card;
	struct snd_soc_dai_link dai_link;

	struct clk *pll;
	struct clk *rclk;
};

static int odroid_card_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_pcm_hw_constraint_single(runtime, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	return 0;
}

static int odroid_card_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct odroid_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	unsigned int pll_freq, rclk_freq;
	int ret;

	switch (params_rate(params)) {
	case 32000:
	case 64000:
		pll_freq = 131072006U;
		break;
	case 44100:
	case 88200:
	case 176400:
		pll_freq = 180633609U;
		break;
	case 48000:
	case 96000:
	case 192000:
		pll_freq = 196608001U;
		break;
	default:
		return -EINVAL;
	}

	ret = clk_set_rate(priv->pll, pll_freq + 1);
	if (ret < 0)
		return ret;

	rclk_freq = params_rate(params) * 256 * 4;

	ret = clk_set_rate(priv->rclk, rclk_freq);
	if (ret < 0)
		return ret;

	if (rtd->num_codecs > 1) {
		struct snd_soc_dai *codec_dai = rtd->codec_dais[1];

		ret = snd_soc_dai_set_sysclk(codec_dai, 0, rclk_freq,
					     SND_SOC_CLOCK_IN);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static const struct snd_soc_ops odroid_card_ops = {
	.startup = odroid_card_startup,
	.hw_params = odroid_card_hw_params,
};

static void odroid_put_codec_of_nodes(struct snd_soc_dai_link *link)
{
	struct snd_soc_dai_link_component *component = link->codecs;
	int i;

	for (i = 0; i < link->num_codecs; i++, component++) {
		if (!component->of_node)
			break;
		of_node_put(component->of_node);
	}
}

static int odroid_audio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *cpu, *codec;
	struct odroid_priv *priv;
	struct snd_soc_dai_link *link;
	struct snd_soc_card *card;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	card = &priv->card;
	card->dev = dev;

	card->owner = THIS_MODULE;
	card->fully_routed = true;

	snd_soc_card_set_drvdata(card, priv);

	priv->pll = devm_clk_get(dev, "epll");
	if (IS_ERR(priv->pll))
		return PTR_ERR(priv->pll);

	priv->rclk = devm_clk_get(dev, "i2s_rclk");
	if (IS_ERR(priv->rclk))
		return PTR_ERR(priv->rclk);

	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret < 0)
		return ret;

	if (of_property_read_bool(dev->of_node, "samsung,audio-widgets")) {
		ret = snd_soc_of_parse_audio_simple_widgets(card,
						"samsung,audio-widgets");
		if (ret < 0)
			return ret;
	}

	if (of_property_read_bool(dev->of_node, "samsung,audio-routing")) {
		ret = snd_soc_of_parse_audio_routing(card,
						"samsung,audio-routing");
		if (ret < 0)
			return ret;
	}

	link = &priv->dai_link;

	link->ops = &odroid_card_ops;
	link->dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS;

	card->dai_link = &priv->dai_link;
	card->num_links = 1;

	cpu = of_get_child_by_name(dev->of_node, "cpu");
	codec = of_get_child_by_name(dev->of_node, "codec");

	link->cpu_of_node = of_parse_phandle(cpu, "sound-dai", 0);
	if (!link->cpu_of_node) {
		dev_err(dev, "Failed parsing cpu/sound-dai property\n");
		return -EINVAL;
	}

	ret = snd_soc_of_get_dai_link_codecs(dev, codec, link);
	if (ret < 0)
		goto err_put_codec_n;

	link->platform_of_node = link->cpu_of_node;

	link->name = "Primary";
	link->stream_name = link->name;

	ret = devm_snd_soc_register_card(dev, card);
	if (ret < 0) {
		dev_err(dev, "snd_soc_register_card() failed: %d\n", ret);
		goto err_put_i2s_n;
	}

	return 0;

err_put_i2s_n:
	of_node_put(link->cpu_of_node);
err_put_codec_n:
	odroid_put_codec_of_nodes(link);
	return ret;
}

static int odroid_audio_remove(struct platform_device *pdev)
{
	struct odroid_priv *priv = platform_get_drvdata(pdev);

	of_node_put(priv->dai_link.cpu_of_node);
	odroid_put_codec_of_nodes(&priv->dai_link);

	return 0;
}

static const struct of_device_id odroid_audio_of_match[] = {
	{ .compatible	= "samsung,odroid-xu3-audio" },
	{ .compatible	= "samsung,odroid-xu4-audio"},
	{ },
};
MODULE_DEVICE_TABLE(of, odroid_audio_of_match);

static struct platform_driver odroid_audio_driver = {
	.driver = {
		.name		= "odroid-audio",
		.of_match_table	= odroid_audio_of_match,
		.pm		= &snd_soc_pm_ops,
	},
	.probe	= odroid_audio_probe,
	.remove	= odroid_audio_remove,
};
module_platform_driver(odroid_audio_driver);

MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_DESCRIPTION("Odroid XU3/XU4 audio support");
MODULE_LICENSE("GPL v2");
