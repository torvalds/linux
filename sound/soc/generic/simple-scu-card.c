/*
 * ASoC simple SCU sound card support
 *
 * Copyright (C) 2015 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * based on ${LINUX}/sound/soc/generic/simple-card.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/simple_card_utils.h>

struct asoc_simple_card_priv {
	struct snd_soc_card snd_card;
	struct snd_soc_codec_conf codec_conf;
	struct asoc_simple_dai *dai_props;
	struct snd_soc_dai_link *dai_link;
	u32 convert_rate;
	u32 convert_channels;
};

#define simple_priv_to_dev(priv) ((priv)->snd_card.dev)
#define simple_priv_to_link(priv, i) ((priv)->snd_card.dai_link + (i))
#define simple_priv_to_props(priv, i) ((priv)->dai_props + (i))

#define DAI	"sound-dai"
#define CELL	"#sound-dai-cells"
#define PREFIX	"simple-audio-card,"

static int asoc_simple_card_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct asoc_simple_card_priv *priv =	snd_soc_card_get_drvdata(rtd->card);
	struct asoc_simple_dai *dai_props =
		simple_priv_to_props(priv, rtd->num);

	return clk_prepare_enable(dai_props->clk);
}

static void asoc_simple_card_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct asoc_simple_card_priv *priv =	snd_soc_card_get_drvdata(rtd->card);
	struct asoc_simple_dai *dai_props =
		simple_priv_to_props(priv, rtd->num);

	clk_disable_unprepare(dai_props->clk);
}

static struct snd_soc_ops asoc_simple_card_ops = {
	.startup = asoc_simple_card_startup,
	.shutdown = asoc_simple_card_shutdown,
};

static int asoc_simple_card_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct asoc_simple_card_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai;
	struct snd_soc_dai_link *dai_link;
	struct asoc_simple_dai *dai_props;
	int num = rtd->num;

	dai_link	= simple_priv_to_link(priv, num);
	dai_props	= simple_priv_to_props(priv, num);
	dai		= dai_link->dynamic ?
				rtd->cpu_dai :
				rtd->codec_dai;

	return asoc_simple_card_init_dai(dai, dai_props);
}

static int asoc_simple_card_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					struct snd_pcm_hw_params *params)
{
	struct asoc_simple_card_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct snd_interval *rate = hw_param_interval(params,
						      SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);

	if (priv->convert_rate)
		rate->min =
		rate->max = priv->convert_rate;

	if (priv->convert_channels)
		channels->min =
		channels->max = priv->convert_channels;

	return 0;
}

static int asoc_simple_card_parse_links(struct device_node *np,
					struct asoc_simple_card_priv *priv,
					unsigned int daifmt,
					int idx, bool is_fe)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, idx);
	struct asoc_simple_dai *dai_props = simple_priv_to_props(priv, idx);
	int ret;

	if (is_fe) {
		int is_single_links = 0;

		/* BE is dummy */
		dai_link->codec_of_node		= NULL;
		dai_link->codec_dai_name	= "snd-soc-dummy-dai";
		dai_link->codec_name		= "snd-soc-dummy";

		/* FE settings */
		dai_link->dynamic		= 1;
		dai_link->dpcm_merged_format	= 1;

		ret = asoc_simple_card_parse_cpu(np, dai_link, DAI, CELL,
						 &is_single_links);
		if (ret)
			return ret;

		ret = asoc_simple_card_parse_clk_cpu(np, dai_link, dai_props);
		if (ret < 0)
			return ret;

		ret = asoc_simple_card_set_dailink_name(dev, dai_link,
							"fe.%s",
							dai_link->cpu_dai_name);
		if (ret < 0)
			return ret;

		asoc_simple_card_canonicalize_cpu(dai_link, is_single_links);
	} else {
		/* FE is dummy */
		dai_link->cpu_of_node		= NULL;
		dai_link->cpu_dai_name		= "snd-soc-dummy-dai";
		dai_link->cpu_name		= "snd-soc-dummy";

		/* BE settings */
		dai_link->no_pcm		= 1;
		dai_link->be_hw_params_fixup	= asoc_simple_card_be_hw_params_fixup;

		ret = asoc_simple_card_parse_codec(np, dai_link, DAI, CELL);
		if (ret < 0)
			return ret;

		ret = asoc_simple_card_parse_clk_codec(np, dai_link, dai_props);
		if (ret < 0)
			return ret;

		ret = asoc_simple_card_set_dailink_name(dev, dai_link,
							"be.%s",
							dai_link->codec_dai_name);
		if (ret < 0)
			return ret;

		snd_soc_of_parse_audio_prefix(&priv->snd_card,
					      &priv->codec_conf,
					      dai_link->codec_of_node,
					      PREFIX "prefix");
	}

	ret = snd_soc_of_parse_tdm_slot(np,
					&dai_props->tx_slot_mask,
					&dai_props->rx_slot_mask,
					&dai_props->slots,
					&dai_props->slot_width);
	if (ret)
		return ret;

	ret = asoc_simple_card_canonicalize_dailink(dai_link);
	if (ret < 0)
		return ret;

	dai_link->dai_fmt		= daifmt;
	dai_link->dpcm_playback		= 1;
	dai_link->dpcm_capture		= 1;
	dai_link->ops			= &asoc_simple_card_ops;
	dai_link->init			= asoc_simple_card_dai_init;

	dev_dbg(dev, "\t%s / %04x / %d\n",
		dai_link->name,
		dai_link->dai_fmt,
		dai_props->sysclk);

	return 0;
}

static int asoc_simple_card_dai_link_of(struct device_node *node,
				 struct asoc_simple_card_priv *priv)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct device_node *np;
	unsigned int daifmt = 0;
	int ret, i;
	bool is_fe;

	/* find 1st codec */
	np = of_get_child_by_name(node, PREFIX "codec");
	if (!np)
		return -ENODEV;

	ret = asoc_simple_card_parse_daifmt(dev, node, np,
					    PREFIX, &daifmt);
	if (ret < 0)
		return ret;

	i = 0;
	for_each_child_of_node(node, np) {
		is_fe = false;
		if (strcmp(np->name, PREFIX "cpu") == 0)
			is_fe = true;

		ret = asoc_simple_card_parse_links(np, priv, daifmt, i, is_fe);
		if (ret < 0)
			return ret;
		i++;
	}

	return 0;
}

static int asoc_simple_card_parse_of(struct device_node *node,
			      struct asoc_simple_card_priv *priv,
			      struct device *dev)
{
	struct asoc_simple_dai *props;
	struct snd_soc_dai_link *links;
	int ret;
	int num;

	if (!node)
		return -EINVAL;

	num = of_get_child_count(node);
	props = devm_kzalloc(dev, sizeof(*props) * num, GFP_KERNEL);
	links = devm_kzalloc(dev, sizeof(*links) * num, GFP_KERNEL);
	if (!props || !links)
		return -ENOMEM;

	priv->dai_props	= props;
	priv->dai_link	= links;

	/* Init snd_soc_card */
	priv->snd_card.owner			= THIS_MODULE;
	priv->snd_card.dev			= dev;
	priv->snd_card.dai_link			= priv->dai_link;
	priv->snd_card.num_links		= num;
	priv->snd_card.codec_conf		= &priv->codec_conf;
	priv->snd_card.num_configs		= 1;

	ret = snd_soc_of_parse_audio_routing(&priv->snd_card, PREFIX "routing");
	if (ret < 0)
		return ret;

	/* sampling rate convert */
	of_property_read_u32(node, PREFIX "convert-rate", &priv->convert_rate);

	/* channels transfer */
	of_property_read_u32(node, PREFIX "convert-channels", &priv->convert_channels);

	ret = asoc_simple_card_dai_link_of(node, priv);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_parse_card_name(&priv->snd_card, PREFIX);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "New card: %s\n",
		priv->snd_card.name ? priv->snd_card.name : "");
	dev_dbg(dev, "convert_rate     %d\n", priv->convert_rate);
	dev_dbg(dev, "convert_channels %d\n", priv->convert_channels);

	return 0;
}

static int asoc_simple_card_probe(struct platform_device *pdev)
{
	struct asoc_simple_card_priv *priv;
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	int ret;

	/* Allocate the private data */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = asoc_simple_card_parse_of(np, priv, dev);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "parse error %d\n", ret);
		goto err;
	}

	snd_soc_card_set_drvdata(&priv->snd_card, priv);

	ret = devm_snd_soc_register_card(&pdev->dev, &priv->snd_card);
	if (ret >= 0)
		return ret;
err:
	asoc_simple_card_clean_reference(&priv->snd_card);

	return ret;
}

static int asoc_simple_card_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	return asoc_simple_card_clean_reference(card);
}

static const struct of_device_id asoc_simple_of_match[] = {
	{ .compatible = "renesas,rsrc-card", },
	{ .compatible = "simple-scu-audio-card", },
	{},
};
MODULE_DEVICE_TABLE(of, asoc_simple_of_match);

static struct platform_driver asoc_simple_card = {
	.driver = {
		.name = "simple-scu-audio-card",
		.of_match_table = asoc_simple_of_match,
	},
	.probe = asoc_simple_card_probe,
	.remove = asoc_simple_card_remove,
};

module_platform_driver(asoc_simple_card);

MODULE_ALIAS("platform:asoc-simple-scu-card");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC Simple SCU Sound Card");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
