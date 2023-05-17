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
	if (!codec_node)
		return -EINVAL;

	/* set card codec info */
	ret = snd_soc_of_get_dai_link_codecs(dev, codec_node, dai_link);

	of_node_put(codec_node);

	if (ret < 0)
		return dev_err_probe(dev, ret, "%s: codec dai not found\n",
				     dai_link->name);

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
