// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <sound/soc.h>

#include "meson-card.h"

int meson_card_i2s_set_sysclk(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      unsigned int mclk_fs)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai;
	unsigned int mclk;
	int ret, i;

	if (!mclk_fs)
		return 0;

	mclk = params_rate(params) * mclk_fs;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk,
					     SND_SOC_CLOCK_IN);
		if (ret && ret != -ENOTSUPP)
			return ret;
	}

	ret = snd_soc_dai_set_sysclk(snd_soc_rtd_to_cpu(rtd, 0), 0, mclk,
				     SND_SOC_CLOCK_OUT);
	if (ret && ret != -ENOTSUPP)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(meson_card_i2s_set_sysclk);

int meson_card_reallocate_links(struct snd_soc_card *card,
				unsigned int num_links)
{
	struct meson_card *priv = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai_link *links;
	void **ldata;

	links = krealloc(priv->card.dai_link,
			 num_links * sizeof(*priv->card.dai_link),
			 GFP_KERNEL | __GFP_ZERO);
	if (!links)
		goto err_links;

	ldata = krealloc(priv->link_data,
			 num_links * sizeof(*priv->link_data),
			 GFP_KERNEL | __GFP_ZERO);
	if (!ldata)
		goto err_ldata;

	priv->card.dai_link = links;
	priv->link_data = ldata;
	priv->card.num_links = num_links;
	return 0;

err_ldata:
	kfree(links);
err_links:
	dev_err(priv->card.dev, "failed to allocate links\n");
	return -ENOMEM;

}
EXPORT_SYMBOL_GPL(meson_card_reallocate_links);

int meson_card_parse_dai(struct snd_soc_card *card,
			 struct device_node *node,
			 struct snd_soc_dai_link_component *dlc)
{
	int ret;

	if (!dlc || !node)
		return -EINVAL;

	ret = snd_soc_of_get_dlc(node, NULL, dlc, 0);
	if (ret)
		return dev_err_probe(card->dev, ret, "can't parse dai\n");

	return ret;
}
EXPORT_SYMBOL_GPL(meson_card_parse_dai);

static int meson_card_set_link_name(struct snd_soc_card *card,
				    struct snd_soc_dai_link *link,
				    struct device_node *node,
				    const char *prefix)
{
	char *name = devm_kasprintf(card->dev, GFP_KERNEL, "%s.%s",
				    prefix, node->full_name);
	if (!name)
		return -ENOMEM;

	link->name = name;
	link->stream_name = name;

	return 0;
}

unsigned int meson_card_parse_daifmt(struct device_node *node,
				     struct device_node *cpu_node)
{
	struct device_node *bitclkmaster = NULL;
	struct device_node *framemaster = NULL;
	unsigned int daifmt;

	daifmt = snd_soc_daifmt_parse_format(node, NULL);

	snd_soc_daifmt_parse_clock_provider_as_phandle(node, NULL, &bitclkmaster, &framemaster);

	/* If no master is provided, default to cpu master */
	if (!bitclkmaster || bitclkmaster == cpu_node) {
		daifmt |= (!framemaster || framemaster == cpu_node) ?
			SND_SOC_DAIFMT_CBS_CFS : SND_SOC_DAIFMT_CBS_CFM;
	} else {
		daifmt |= (!framemaster || framemaster == cpu_node) ?
			SND_SOC_DAIFMT_CBM_CFS : SND_SOC_DAIFMT_CBM_CFM;
	}

	of_node_put(bitclkmaster);
	of_node_put(framemaster);

	return daifmt;
}
EXPORT_SYMBOL_GPL(meson_card_parse_daifmt);

int meson_card_set_be_link(struct snd_soc_card *card,
			   struct snd_soc_dai_link *link,
			   struct device_node *node)
{
	struct snd_soc_dai_link_component *codec;
	struct device_node *np;
	int ret, num_codecs;

	num_codecs = of_get_child_count(node);
	if (!num_codecs) {
		dev_err(card->dev, "be link %s has no codec\n",
			node->full_name);
		return -EINVAL;
	}

	codec = devm_kcalloc(card->dev, num_codecs, sizeof(*codec), GFP_KERNEL);
	if (!codec)
		return -ENOMEM;

	link->codecs = codec;
	link->num_codecs = num_codecs;

	for_each_child_of_node(node, np) {
		ret = meson_card_parse_dai(card, np, codec);
		if (ret) {
			of_node_put(np);
			return ret;
		}

		codec++;
	}

	ret = meson_card_set_link_name(card, link, node, "be");
	if (ret)
		dev_err(card->dev, "error setting %pOFn link name\n", np);

	return ret;
}
EXPORT_SYMBOL_GPL(meson_card_set_be_link);

int meson_card_set_fe_link(struct snd_soc_card *card,
			   struct snd_soc_dai_link *link,
			   struct device_node *node,
			   bool is_playback)
{
	link->codecs = &snd_soc_dummy_dlc;
	link->num_codecs = 1;

	link->dynamic = 1;
	link->dpcm_merged_format = 1;
	link->dpcm_merged_chan = 1;
	link->dpcm_merged_rate = 1;

	if (is_playback)
		link->playback_only = 1;
	else
		link->capture_only = 1;

	return meson_card_set_link_name(card, link, node, "fe");
}
EXPORT_SYMBOL_GPL(meson_card_set_fe_link);

static int meson_card_add_links(struct snd_soc_card *card)
{
	struct meson_card *priv = snd_soc_card_get_drvdata(card);
	struct device_node *node = card->dev->of_node;
	struct device_node *np;
	int num, i, ret;

	num = of_get_child_count(node);
	if (!num) {
		dev_err(card->dev, "card has no links\n");
		return -EINVAL;
	}

	ret = meson_card_reallocate_links(card, num);
	if (ret)
		return ret;

	i = 0;
	for_each_child_of_node(node, np) {
		ret = priv->match_data->add_link(card, np, &i);
		if (ret) {
			of_node_put(np);
			return ret;
		}

		i++;
	}

	return 0;
}

static int meson_card_parse_of_optional(struct snd_soc_card *card,
					const char *propname,
					int (*func)(struct snd_soc_card *c,
						    const char *p))
{
	/* If property is not provided, don't fail ... */
	if (!of_property_read_bool(card->dev->of_node, propname))
		return 0;

	/* ... but do fail if it is provided and the parsing fails */
	return func(card, propname);
}

static void meson_card_clean_references(struct meson_card *priv)
{
	struct snd_soc_card *card = &priv->card;
	struct snd_soc_dai_link *link;
	struct snd_soc_dai_link_component *codec;
	struct snd_soc_aux_dev *aux;
	int i, j;

	if (card->dai_link) {
		for_each_card_prelinks(card, i, link) {
			if (link->cpus)
				of_node_put(link->cpus->of_node);
			for_each_link_codecs(link, j, codec)
				of_node_put(codec->of_node);
		}
	}

	if (card->aux_dev) {
		for_each_card_pre_auxs(card, i, aux)
			of_node_put(aux->dlc.of_node);
	}

	kfree(card->dai_link);
	kfree(priv->link_data);
}

int meson_card_probe(struct platform_device *pdev)
{
	const struct meson_card_match_data *data;
	struct device *dev = &pdev->dev;
	struct meson_card *priv;
	int ret;

	data = of_device_get_match_data(dev);
	if (!data) {
		dev_err(dev, "failed to match device\n");
		return -ENODEV;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);
	snd_soc_card_set_drvdata(&priv->card, priv);

	priv->card.owner = THIS_MODULE;
	priv->card.dev = dev;
	priv->card.driver_name = dev->driver->name;
	priv->match_data = data;

	ret = snd_soc_of_parse_card_name(&priv->card, "model");
	if (ret < 0)
		return ret;

	ret = meson_card_parse_of_optional(&priv->card, "audio-routing",
					   snd_soc_of_parse_audio_routing);
	if (ret) {
		dev_err(dev, "error while parsing routing\n");
		return ret;
	}

	ret = meson_card_parse_of_optional(&priv->card, "audio-widgets",
					   snd_soc_of_parse_audio_simple_widgets);
	if (ret) {
		dev_err(dev, "error while parsing widgets\n");
		return ret;
	}

	ret = meson_card_add_links(&priv->card);
	if (ret)
		goto out_err;

	ret = snd_soc_of_parse_aux_devs(&priv->card, "audio-aux-devs");
	if (ret)
		goto out_err;

	ret = devm_snd_soc_register_card(dev, &priv->card);
	if (ret)
		goto out_err;

	return 0;

out_err:
	meson_card_clean_references(priv);
	return ret;
}
EXPORT_SYMBOL_GPL(meson_card_probe);

void meson_card_remove(struct platform_device *pdev)
{
	struct meson_card *priv = platform_get_drvdata(pdev);

	meson_card_clean_references(priv);
}
EXPORT_SYMBOL_GPL(meson_card_remove);

MODULE_DESCRIPTION("Amlogic Sound Card Utils");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
