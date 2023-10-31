// SPDX-License-Identifier: GPL-2.0
//
// audio-graph-card2-custom-sample.c
//
// Copyright (C) 2020 Renesas Electronics Corp.
// Copyright (C) 2020 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <sound/graph_card.h>

/*
 * Custom driver can have own priv
 * which includes simple_util_priv.
 */
struct custom_priv {
	struct simple_util_priv simple_priv;

	/* custom driver's own params */
	int custom_params;
};

/* You can get custom_priv from simple_priv */
#define simple_to_custom(simple) container_of((simple), struct custom_priv, simple_priv)

static int custom_card_probe(struct snd_soc_card *card)
{
	struct simple_util_priv *simple_priv = snd_soc_card_get_drvdata(card);
	struct custom_priv *custom_priv = simple_to_custom(simple_priv);
	struct device *dev = simple_priv_to_dev(simple_priv);

	dev_info(dev, "custom probe\n");

	custom_priv->custom_params = 1;

	/* you can use generic probe function */
	return graph_util_card_probe(card);
}

static int custom_hook_pre(struct simple_util_priv *priv)
{
	struct device *dev = simple_priv_to_dev(priv);

	/* You can custom before parsing */
	dev_info(dev, "hook : %s\n", __func__);

	return 0;
}

static int custom_hook_post(struct simple_util_priv *priv)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct snd_soc_card *card;

	/* You can custom after parsing */
	dev_info(dev, "hook : %s\n", __func__);

	/* overwrite .probe sample */
	card = simple_priv_to_card(priv);
	card->probe = custom_card_probe;

	return 0;
}

static int custom_normal(struct simple_util_priv *priv,
			 struct device_node *lnk,
			 struct link_info *li)
{
	struct device *dev = simple_priv_to_dev(priv);

	/*
	 * You can custom Normal parsing
	 * before/affter audio_graph2_link_normal()
	 */
	dev_info(dev, "hook : %s\n", __func__);

	return audio_graph2_link_normal(priv, lnk, li);
}

static int custom_dpcm(struct simple_util_priv *priv,
		       struct device_node *lnk,
		       struct link_info *li)
{
	struct device *dev = simple_priv_to_dev(priv);

	/*
	 * You can custom DPCM parsing
	 * before/affter audio_graph2_link_dpcm()
	 */
	dev_info(dev, "hook : %s\n", __func__);

	return audio_graph2_link_dpcm(priv, lnk, li);
}

static int custom_c2c(struct simple_util_priv *priv,
		      struct device_node *lnk,
		      struct link_info *li)
{
	struct device *dev = simple_priv_to_dev(priv);

	/*
	 * You can custom Codec2Codec parsing
	 * before/affter audio_graph2_link_c2c()
	 */
	dev_info(dev, "hook : %s\n", __func__);

	return audio_graph2_link_c2c(priv, lnk, li);
}

/*
 * audio-graph-card2 has many hooks for your customizing.
 */
static struct graph2_custom_hooks custom_hooks = {
	.hook_pre	= custom_hook_pre,
	.hook_post	= custom_hook_post,
	.custom_normal	= custom_normal,
	.custom_dpcm	= custom_dpcm,
	.custom_c2c	= custom_c2c,
};

static int custom_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct simple_util_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct device *dev = simple_priv_to_dev(priv);

	dev_info(dev, "custom startup\n");

	return simple_util_startup(substream);
}

/* You can use custom ops */
static const struct snd_soc_ops custom_ops = {
	.startup	= custom_startup,
	.shutdown	= simple_util_shutdown,
	.hw_params	= simple_util_hw_params,
};

static int custom_probe(struct platform_device *pdev)
{
	struct custom_priv *custom_priv;
	struct simple_util_priv *simple_priv;
	struct device *dev = &pdev->dev;
	int ret;

	custom_priv = devm_kzalloc(dev, sizeof(*custom_priv), GFP_KERNEL);
	if (!custom_priv)
		return -ENOMEM;

	simple_priv		= &custom_priv->simple_priv;
	simple_priv->ops	= &custom_ops; /* customize dai_link ops */

	/* "audio-graph-card2-custom-sample" is too long */
	simple_priv->snd_card.name = "card2-custom";

	/* use audio-graph-card2 parsing with own custom hooks */
	ret = audio_graph2_parse_of(simple_priv, dev, &custom_hooks);
	if (ret < 0)
		return ret;

	/* customize more if needed */

	return 0;
}

static const struct of_device_id custom_of_match[] = {
	{ .compatible = "audio-graph-card2-custom-sample", },
	{},
};
MODULE_DEVICE_TABLE(of, custom_of_match);

static struct platform_driver custom_card = {
	.driver = {
		.name = "audio-graph-card2-custom-sample",
		.of_match_table = custom_of_match,
	},
	.probe	= custom_probe,
	.remove	= simple_util_remove,
};
module_platform_driver(custom_card);

MODULE_ALIAS("platform:asoc-audio-graph-card2-custom-sample");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC Audio Graph Card2 Custom Sample");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
