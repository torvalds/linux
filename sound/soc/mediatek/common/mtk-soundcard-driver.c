// SPDX-License-Identifier: GPL-2.0
/*
 * mtk-soundcard-driver.c  --  MediaTek soundcard driver common
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Trevor Wu <trevor.wu@mediatek.com>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <sound/soc.h>

#include "mtk-dsp-sof-common.h"
#include "mtk-soc-card.h"
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
	struct snd_soc_dai_link *dai_link;
	const char *dai_link_name;
	int ret, i;

	/* Loop over all the dai link sub nodes */
	for_each_available_child_of_node_scoped(dev->of_node, sub_node) {
		if (of_property_read_string(sub_node, "link-name",
					    &dai_link_name))
			return -EINVAL;

		for_each_card_prelinks(card, i, dai_link) {
			if (!strcmp(dai_link_name, dai_link->name))
				break;
		}

		if (i >= card->num_links)
			return -EINVAL;

		ret = set_card_codec_info(card, sub_node, dai_link);
		if (ret < 0)
			return ret;

		ret = set_dailink_daifmt(card, sub_node, dai_link);
		if (ret < 0)
			return ret;
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

int mtk_soundcard_startup(struct snd_pcm_substream *substream,
			  enum mtk_pcm_constraint_type ctype)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct mtk_soc_card_data *soc_card = snd_soc_card_get_drvdata(rtd->card);
	const struct mtk_pcm_constraints_data *mpc = &soc_card->card_data->pcm_constraints[ctype];
	int ret;

	if (unlikely(!mpc))
		return -EINVAL;

	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_RATE,
					 mpc->rates);
	if (ret < 0) {
		dev_err(rtd->dev, "hw_constraint_list rate failed\n");
		return ret;
	}

	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_CHANNELS,
					 mpc->channels);
	if (ret < 0) {
		dev_err(rtd->dev, "hw_constraint_list channel failed\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_soundcard_startup);

static int mtk_soundcard_playback_startup(struct snd_pcm_substream *substream)
{
	return mtk_soundcard_startup(substream, MTK_CONSTRAINT_PLAYBACK);
}

const struct snd_soc_ops mtk_soundcard_common_playback_ops = {
	.startup = mtk_soundcard_playback_startup,
};
EXPORT_SYMBOL_GPL(mtk_soundcard_common_playback_ops);

static int mtk_soundcard_capture_startup(struct snd_pcm_substream *substream)
{
	return mtk_soundcard_startup(substream, MTK_CONSTRAINT_CAPTURE);
}

const struct snd_soc_ops mtk_soundcard_common_capture_ops = {
	.startup = mtk_soundcard_capture_startup,
};
EXPORT_SYMBOL_GPL(mtk_soundcard_common_capture_ops);

int mtk_soundcard_common_probe(struct platform_device *pdev)
{
	struct device_node *platform_node, *adsp_node, *accdet_node;
	struct snd_soc_component *accdet_comp;
	struct platform_device *accdet_pdev;
	const struct mtk_soundcard_pdata *pdata;
	struct mtk_soc_card_data *soc_card_data;
	struct snd_soc_dai_link *orig_dai_link, *dai_link;
	struct snd_soc_jack *jacks;
	struct snd_soc_card *card;
	int i, orig_num_links, ret;
	bool needs_legacy_probe;

	pdata = device_get_match_data(&pdev->dev);
	if (!pdata)
		return -EINVAL;

	card = pdata->card_data->card;
	card->dev = &pdev->dev;
	orig_dai_link = card->dai_link;
	orig_num_links = card->num_links;

	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret)
		return ret;

	if (!card->name) {
		if (!pdata->card_name)
			return -EINVAL;

		card->name = pdata->card_name;
	}

	needs_legacy_probe = !of_property_present(pdev->dev.of_node, "audio-routing");
	if (needs_legacy_probe) {
		/*
		 * If we have no .soc_probe() callback there's no way of using
		 * any legacy probe mechanism, as that cannot not be generic.
		 */
		if (!pdata->soc_probe)
			return -EINVAL;

		dev_info_once(&pdev->dev, "audio-routing not found: using legacy probe\n");
	} else {
		ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
		if (ret)
			return ret;
	}

	soc_card_data = devm_kzalloc(&pdev->dev, sizeof(*soc_card_data), GFP_KERNEL);
	if (!soc_card_data)
		return -ENOMEM;

	soc_card_data->card_data = pdata->card_data;

	jacks = devm_kcalloc(card->dev, soc_card_data->card_data->num_jacks,
			     sizeof(*jacks), GFP_KERNEL);
	if (!jacks)
		return -ENOMEM;

	soc_card_data->card_data->jacks = jacks;

	accdet_node = of_parse_phandle(pdev->dev.of_node, "mediatek,accdet", 0);
	if (accdet_node) {
		accdet_pdev = of_find_device_by_node(accdet_node);
		if (accdet_pdev) {
			accdet_comp = snd_soc_lookup_component(&accdet_pdev->dev, NULL);
			if (accdet_comp)
				soc_card_data->accdet = accdet_comp;
			else
				dev_err(&pdev->dev, "No sound component found from mediatek,accdet property\n");

			put_device(&accdet_pdev->dev);
		} else {
			dev_err(&pdev->dev, "No device found from mediatek,accdet property\n");
		}

		of_node_put(accdet_node);
	}

	platform_node = of_parse_phandle(pdev->dev.of_node, "mediatek,platform", 0);
	if (!platform_node)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "Property mediatek,platform missing or invalid\n");

	/* Check if this SoC has an Audio DSP */
	if (pdata->sof_priv)
		adsp_node = of_parse_phandle(pdev->dev.of_node, "mediatek,adsp", 0);
	else
		adsp_node = NULL;

	if (adsp_node) {
		if (of_property_present(pdev->dev.of_node, "mediatek,dai-link")) {
			ret = mtk_sof_dailink_parse_of(card, pdev->dev.of_node,
						       "mediatek,dai-link",
						       card->dai_link, card->num_links);
			if (ret) {
				of_node_put(adsp_node);
				of_node_put(platform_node);
				return dev_err_probe(&pdev->dev, ret,
						     "Cannot parse mediatek,dai-link\n");
			}
		}

		soc_card_data->sof_priv = pdata->sof_priv;
		card->probe = mtk_sof_card_probe;
		card->late_probe = mtk_sof_card_late_probe;
		if (!card->topology_shortname_created) {
			snprintf(card->topology_shortname, 32, "sof-%s", card->name);
			card->topology_shortname_created = true;
		}
		card->name = card->topology_shortname;
	}

	/*
	 * Regardless of whether the ADSP is wanted and/or present in a machine
	 * specific device tree or not and regardless of whether any AFE_SOF
	 * link is present, we have to make sure that the platforms->of_node
	 * is not NULL, and set to either ADSP (adsp_node) or AFE (platform_node).
	 */
	for_each_card_prelinks(card, i, dai_link) {
		if (adsp_node && !strncmp(dai_link->name, "AFE_SOF", strlen("AFE_SOF")))
			dai_link->platforms->of_node = adsp_node;
		else if (!dai_link->platforms->name && !dai_link->platforms->of_node)
			dai_link->platforms->of_node = platform_node;
	}

	if (!needs_legacy_probe) {
		ret = parse_dai_link_info(card);
		if (ret)
			goto err_restore_dais;
	} else {
		if (adsp_node)
			of_node_put(adsp_node);
		of_node_put(platform_node);
	}

	if (pdata->soc_probe) {
		ret = pdata->soc_probe(soc_card_data, needs_legacy_probe);
		if (ret) {
			if (!needs_legacy_probe)
				clean_card_reference(card);
			goto err_restore_dais;
		}
	}
	snd_soc_card_set_drvdata(card, soc_card_data);

	ret = devm_snd_soc_register_card(&pdev->dev, card);

	if (!needs_legacy_probe)
		clean_card_reference(card);

	if (ret) {
		dev_err_probe(&pdev->dev, ret, "Cannot register card\n");
		goto err_restore_dais;
	}

	return 0;

err_restore_dais:
	card->dai_link = orig_dai_link;
	card->num_links = orig_num_links;
	return ret;
}
EXPORT_SYMBOL_GPL(mtk_soundcard_common_probe);
