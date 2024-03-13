// SPDX-License-Identifier: GPL-2.0
/*
 * mtk-soundcard-driver.c  --  MediaTek soundcard driver common
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Trevor Wu <trevor.wu@mediatek.com>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <sound/soc.h>

#include "mtk-soundcard-driver.h"

static int set_card_codec_info(struct snd_soc_card *card,
			       struct device_node *sub_node,
			       struct snd_soc_dai_link *dai_link)
{
	struct device *dev = card->dev;
	struct device_node *codec_node;
	int ret;

	codec_node = of_get_child_by_name(sub_node, "codec");
	if (!codec_node) {
		dev_dbg(dev, "%s no specified codec: setting dummy.\n", dai_link->name);

		dai_link->codecs = &snd_soc_dummy_dlc;
		dai_link->num_codecs = 1;
		dai_link->dynamic = 1;
		return 0;
	}

	/* set card codec info */
	ret = snd_soc_of_get_dai_link_codecs(dev, codec_node, dai_link);

	of_node_put(codec_node);

	if (ret < 0)
		return dev_err_probe(dev, ret, "%s: codec dai not found\n",
				     dai_link->name);

	return 0;
}

static int set_dailink_daifmt(struct snd_soc_card *card,
			      struct device_node *sub_node,
			      struct snd_soc_dai_link *dai_link)
{
	unsigned int daifmt;
	const char *str;
	int ret;
	struct {
		char *name;
		unsigned int val;
	} of_clk_table[] = {
		{ "cpu",	SND_SOC_DAIFMT_CBC_CFC },
		{ "codec",	SND_SOC_DAIFMT_CBP_CFP },
	};

	daifmt = snd_soc_daifmt_parse_format(sub_node, NULL);
	if (daifmt) {
		dai_link->dai_fmt &= SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK;
		dai_link->dai_fmt |= daifmt;
	}

	/*
	 * check "mediatek,clk-provider = xxx"
	 * SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK area
	 */
	ret = of_property_read_string(sub_node, "mediatek,clk-provider", &str);
	if (ret == 0) {
		int i;

		for (i = 0; i < ARRAY_SIZE(of_clk_table); i++) {
			if (strcmp(str, of_clk_table[i].name) == 0) {
				dai_link->dai_fmt &= ~SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK;
				dai_link->dai_fmt |= of_clk_table[i].val;
				break;
			}
		}
	}

	return 0;
}

int parse_dai_link_info(struct snd_soc_card *card)
{
	struct device *dev = card->dev;
	struct device_node *sub_node;
	struct snd_soc_dai_link *dai_link;
	const char *dai_link_name;
	int ret, i;

	/* Loop over all the dai link sub nodes */
	for_each_available_child_of_node(dev->of_node, sub_node) {
		if (of_property_read_string(sub_node, "link-name",
					    &dai_link_name)) {
			of_node_put(sub_node);
			return -EINVAL;
		}

		for_each_card_prelinks(card, i, dai_link) {
			if (!strcmp(dai_link_name, dai_link->name))
				break;
		}

		if (i >= card->num_links) {
			of_node_put(sub_node);
			return -EINVAL;
		}

		ret = set_card_codec_info(card, sub_node, dai_link);
		if (ret < 0) {
			of_node_put(sub_node);
			return ret;
		}

		ret = set_dailink_daifmt(card, sub_node, dai_link);
		if (ret < 0) {
			of_node_put(sub_node);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(parse_dai_link_info);

void clean_card_reference(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *dai_link;
	int i;

	/* release codec reference gotten by set_card_codec_info */
	for_each_card_prelinks(card, i, dai_link)
		snd_soc_of_put_dai_link_codecs(dai_link);
}
EXPORT_SYMBOL_GPL(clean_card_reference);
