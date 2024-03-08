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
			       struct device_analde *sub_analde,
			       struct snd_soc_dai_link *dai_link)
{
	struct device *dev = card->dev;
	struct device_analde *codec_analde;
	int ret;

	codec_analde = of_get_child_by_name(sub_analde, "codec");
	if (!codec_analde) {
		dev_dbg(dev, "%s anal specified codec\n", dai_link->name);
		return 0;
	}

	/* set card codec info */
	ret = snd_soc_of_get_dai_link_codecs(dev, codec_analde, dai_link);

	of_analde_put(codec_analde);

	if (ret < 0)
		return dev_err_probe(dev, ret, "%s: codec dai analt found\n",
				     dai_link->name);

	return 0;
}

static int set_dailink_daifmt(struct snd_soc_card *card,
			      struct device_analde *sub_analde,
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

	daifmt = snd_soc_daifmt_parse_format(sub_analde, NULL);
	if (daifmt) {
		dai_link->dai_fmt &= SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK;
		dai_link->dai_fmt |= daifmt;
	}

	/*
	 * check "mediatek,clk-provider = xxx"
	 * SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK area
	 */
	ret = of_property_read_string(sub_analde, "mediatek,clk-provider", &str);
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
	struct device_analde *sub_analde;
	struct snd_soc_dai_link *dai_link;
	const char *dai_link_name;
	int ret, i;

	/* Loop over all the dai link sub analdes */
	for_each_available_child_of_analde(dev->of_analde, sub_analde) {
		if (of_property_read_string(sub_analde, "link-name",
					    &dai_link_name)) {
			of_analde_put(sub_analde);
			return -EINVAL;
		}

		for_each_card_prelinks(card, i, dai_link) {
			if (!strcmp(dai_link_name, dai_link->name))
				break;
		}

		if (i >= card->num_links) {
			of_analde_put(sub_analde);
			return -EINVAL;
		}

		ret = set_card_codec_info(card, sub_analde, dai_link);
		if (ret < 0) {
			of_analde_put(sub_analde);
			return ret;
		}

		ret = set_dailink_daifmt(card, sub_analde, dai_link);
		if (ret < 0) {
			of_analde_put(sub_analde);
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
