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

	struct clk *clk_i2s_bus;
	struct clk *sclk_i2s;
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
	unsigned int pll_freq, rclk_freq, rfs;
	int ret;

	switch (params_rate(params)) {
	case 64000:
		pll_freq = 196608001U;
		rfs = 384;
		break;
	case 44100:
	case 88200:
		pll_freq = 180633609U;
		rfs = 512;
		break;
	case 32000:
	case 48000:
	case 96000:
		pll_freq = 196608001U;
		rfs = 512;
		break;
	default:
		return -EINVAL;
	}

	ret = clk_set_rate(priv->clk_i2s_bus, pll_freq / 2 + 1);
	if (ret < 0)
		return ret;

	/*
	 *  We add 1 to the rclk_freq value in order to avoid too low clock
	 *  frequency values due to the EPLL output frequency not being exact
	 *  multiple of the audio sampling rate.
	 */
	rclk_freq = params_rate(params) * rfs + 1;

	ret = clk_set_rate(priv->sclk_i2s, rclk_freq);
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


	priv->sclk_i2s = of_clk_get_by_name(link->cpu_of_node, "i2s_opclk1");
	if (IS_ERR(priv->sclk_i2s)) {
		ret = PTR_ERR(priv->sclk_i2s);
		goto err_put_i2s_n;
	}

	priv->clk_i2s_bus = of_clk_get_by_name(link->cpu_of_node, "iis");
	if (IS_ERR(priv->clk_i2s_bus)) {
		ret = PTR_ERR(priv->clk_i2s_bus);
		goto err_put_sclk;
	}

	ret = devm_snd_soc_register_card(dev, card);
	if (ret < 0) {
		dev_err(dev, "snd_soc_register_card() failed: %d\n", ret);
		goto err_put_clk_i2s;
	}

	return 0;

err_put_clk_i2s:
	clk_put(priv->clk_i2s_bus);
err_put_sclk:
	clk_put(priv->sclk_i2s);
err_put_i2s_n:
	of_node_put(link->cpu_of_node);
err_put_codec_n:
	snd_soc_of_put_dai_link_codecs(link);
	return ret;
}

static int odroid_audio_remove(struct platform_device *pdev)
{
	struct odroid_priv *priv = platform_get_drvdata(pdev);

	of_node_put(priv->dai_link.cpu_of_node);
	snd_soc_of_put_dai_link_codecs(&priv->dai_link);
	clk_put(priv->sclk_i2s);
	clk_put(priv->clk_i2s_bus);

	return 0;
}

static const struct of_device_id odroid_audio_of_match[] = {
	{ .compatible	= "hardkernel,odroid-xu3-audio" },
	{ .compatible	= "hardkernel,odroid-xu4-audio" },
	{ .compatible	= "samsung,odroid-xu3-audio" },
	{ .compatible	= "samsung,odroid-xu4-audio" },
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
