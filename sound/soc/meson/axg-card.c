// SPDX-License-Identifier: (GPL-2.0 OR MIT)
//
// Copyright (c) 2018 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "axg-tdm.h"
#include "meson-card.h"

struct axg_dai_link_tdm_mask {
	u32 tx;
	u32 rx;
};

struct axg_dai_link_tdm_data {
	unsigned int mclk_fs;
	unsigned int slots;
	unsigned int slot_width;
	u32 *tx_mask;
	u32 *rx_mask;
	struct axg_dai_link_tdm_mask *codec_masks;
};

/*
 * Base params for the codec to codec links
 * Those will be over-written by the CPU side of the link
 */
static const struct snd_soc_pcm_stream codec_params = {
	.formats = SNDRV_PCM_FMTBIT_S24_LE,
	.rate_min = 5525,
	.rate_max = 192000,
	.channels_min = 1,
	.channels_max = 8,
};

static int axg_card_tdm_be_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct meson_card *priv = snd_soc_card_get_drvdata(rtd->card);
	struct axg_dai_link_tdm_data *be =
		(struct axg_dai_link_tdm_data *)priv->link_data[rtd->num];

	return meson_card_i2s_set_sysclk(substream, params, be->mclk_fs);
}

static const struct snd_soc_ops axg_card_tdm_be_ops = {
	.hw_params = axg_card_tdm_be_hw_params,
};

static int axg_card_tdm_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct meson_card *priv = snd_soc_card_get_drvdata(rtd->card);
	struct axg_dai_link_tdm_data *be =
		(struct axg_dai_link_tdm_data *)priv->link_data[rtd->num];
	struct snd_soc_dai *codec_dai;
	int ret, i;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		ret = snd_soc_dai_set_tdm_slot(codec_dai,
					       be->codec_masks[i].tx,
					       be->codec_masks[i].rx,
					       be->slots, be->slot_width);
		if (ret && ret != -ENOTSUPP) {
			dev_err(codec_dai->dev,
				"setting tdm link slots failed\n");
			return ret;
		}
	}

	ret = axg_tdm_set_tdm_slots(asoc_rtd_to_cpu(rtd, 0), be->tx_mask, be->rx_mask,
				    be->slots, be->slot_width);
	if (ret) {
		dev_err(asoc_rtd_to_cpu(rtd, 0)->dev, "setting tdm link slots failed\n");
		return ret;
	}

	return 0;
}

static int axg_card_tdm_dai_lb_init(struct snd_soc_pcm_runtime *rtd)
{
	struct meson_card *priv = snd_soc_card_get_drvdata(rtd->card);
	struct axg_dai_link_tdm_data *be =
		(struct axg_dai_link_tdm_data *)priv->link_data[rtd->num];
	int ret;

	/* The loopback rx_mask is the pad tx_mask */
	ret = axg_tdm_set_tdm_slots(asoc_rtd_to_cpu(rtd, 0), NULL, be->tx_mask,
				    be->slots, be->slot_width);
	if (ret) {
		dev_err(asoc_rtd_to_cpu(rtd, 0)->dev, "setting tdm link slots failed\n");
		return ret;
	}

	return 0;
}

static int axg_card_add_tdm_loopback(struct snd_soc_card *card,
				     int *index)
{
	struct meson_card *priv = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai_link *pad;
	struct snd_soc_dai_link *lb;
	struct snd_soc_dai_link_component *dlc;
	int ret;

	/* extend links */
	ret = meson_card_reallocate_links(card, card->num_links + 1);
	if (ret)
		return ret;

	pad = &card->dai_link[*index];
	lb = &card->dai_link[*index + 1];

	lb->name = devm_kasprintf(card->dev, GFP_KERNEL, "%s-lb", pad->name);
	if (!lb->name)
		return -ENOMEM;

	dlc = devm_kzalloc(card->dev, 2 * sizeof(*dlc), GFP_KERNEL);
	if (!dlc)
		return -ENOMEM;

	lb->cpus = &dlc[0];
	lb->codecs = &dlc[1];
	lb->num_cpus = 1;
	lb->num_codecs = 1;

	lb->stream_name = lb->name;
	lb->cpus->of_node = pad->cpus->of_node;
	lb->cpus->dai_name = "TDM Loopback";
	lb->codecs->name = "snd-soc-dummy";
	lb->codecs->dai_name = "snd-soc-dummy-dai";
	lb->dpcm_capture = 1;
	lb->no_pcm = 1;
	lb->ops = &axg_card_tdm_be_ops;
	lb->init = axg_card_tdm_dai_lb_init;

	/* Provide the same link data to the loopback */
	priv->link_data[*index + 1] = priv->link_data[*index];

	/*
	 * axg_card_clean_references() will iterate over this link,
	 * make sure the node count is balanced
	 */
	of_node_get(lb->cpus->of_node);

	/* Let add_links continue where it should */
	*index += 1;

	return 0;
}

static int axg_card_parse_cpu_tdm_slots(struct snd_soc_card *card,
					struct snd_soc_dai_link *link,
					struct device_node *node,
					struct axg_dai_link_tdm_data *be)
{
	char propname[32];
	u32 tx, rx;
	int i;

	be->tx_mask = devm_kcalloc(card->dev, AXG_TDM_NUM_LANES,
				   sizeof(*be->tx_mask), GFP_KERNEL);
	be->rx_mask = devm_kcalloc(card->dev, AXG_TDM_NUM_LANES,
				   sizeof(*be->rx_mask), GFP_KERNEL);
	if (!be->tx_mask || !be->rx_mask)
		return -ENOMEM;

	for (i = 0, tx = 0; i < AXG_TDM_NUM_LANES; i++) {
		snprintf(propname, 32, "dai-tdm-slot-tx-mask-%d", i);
		snd_soc_of_get_slot_mask(node, propname, &be->tx_mask[i]);
		tx = max(tx, be->tx_mask[i]);
	}

	/* Disable playback is the interface has no tx slots */
	if (!tx)
		link->dpcm_playback = 0;

	for (i = 0, rx = 0; i < AXG_TDM_NUM_LANES; i++) {
		snprintf(propname, 32, "dai-tdm-slot-rx-mask-%d", i);
		snd_soc_of_get_slot_mask(node, propname, &be->rx_mask[i]);
		rx = max(rx, be->rx_mask[i]);
	}

	/* Disable capture is the interface has no rx slots */
	if (!rx)
		link->dpcm_capture = 0;

	/* ... but the interface should at least have one of them */
	if (!tx && !rx) {
		dev_err(card->dev, "tdm link has no cpu slots\n");
		return -EINVAL;
	}

	of_property_read_u32(node, "dai-tdm-slot-num", &be->slots);
	if (!be->slots) {
		/*
		 * If the slot number is not provided, set it such as it
		 * accommodates the largest mask
		 */
		be->slots = fls(max(tx, rx));
	} else if (be->slots < fls(max(tx, rx)) || be->slots > 32) {
		/*
		 * Error if the slots can't accommodate the largest mask or
		 * if it is just too big
		 */
		dev_err(card->dev, "bad slot number\n");
		return -EINVAL;
	}

	of_property_read_u32(node, "dai-tdm-slot-width", &be->slot_width);

	return 0;
}

static int axg_card_parse_codecs_masks(struct snd_soc_card *card,
				       struct snd_soc_dai_link *link,
				       struct device_node *node,
				       struct axg_dai_link_tdm_data *be)
{
	struct axg_dai_link_tdm_mask *codec_mask;
	struct device_node *np;

	codec_mask = devm_kcalloc(card->dev, link->num_codecs,
				  sizeof(*codec_mask), GFP_KERNEL);
	if (!codec_mask)
		return -ENOMEM;

	be->codec_masks = codec_mask;

	for_each_child_of_node(node, np) {
		snd_soc_of_get_slot_mask(np, "dai-tdm-slot-rx-mask",
					 &codec_mask->rx);
		snd_soc_of_get_slot_mask(np, "dai-tdm-slot-tx-mask",
					 &codec_mask->tx);

		codec_mask++;
	}

	return 0;
}

static int axg_card_parse_tdm(struct snd_soc_card *card,
			      struct device_node *node,
			      int *index)
{
	struct meson_card *priv = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai_link *link = &card->dai_link[*index];
	struct axg_dai_link_tdm_data *be;
	int ret;

	/* Allocate tdm link parameters */
	be = devm_kzalloc(card->dev, sizeof(*be), GFP_KERNEL);
	if (!be)
		return -ENOMEM;
	priv->link_data[*index] = be;

	/* Setup tdm link */
	link->ops = &axg_card_tdm_be_ops;
	link->init = axg_card_tdm_dai_init;
	link->dai_fmt = meson_card_parse_daifmt(node, link->cpus->of_node);

	of_property_read_u32(node, "mclk-fs", &be->mclk_fs);

	ret = axg_card_parse_cpu_tdm_slots(card, link, node, be);
	if (ret) {
		dev_err(card->dev, "error parsing tdm link slots\n");
		return ret;
	}

	ret = axg_card_parse_codecs_masks(card, link, node, be);
	if (ret)
		return ret;

	/* Add loopback if the pad dai has playback */
	if (link->dpcm_playback) {
		ret = axg_card_add_tdm_loopback(card, index);
		if (ret)
			return ret;
	}

	return 0;
}

static int axg_card_cpu_is_capture_fe(struct device_node *np)
{
	return of_device_is_compatible(np, DT_PREFIX "axg-toddr");
}

static int axg_card_cpu_is_playback_fe(struct device_node *np)
{
	return of_device_is_compatible(np, DT_PREFIX "axg-frddr");
}

static int axg_card_cpu_is_tdm_iface(struct device_node *np)
{
	return of_device_is_compatible(np, DT_PREFIX "axg-tdm-iface");
}

static int axg_card_cpu_is_codec(struct device_node *np)
{
	return of_device_is_compatible(np, DT_PREFIX "g12a-tohdmitx") ||
		of_device_is_compatible(np, DT_PREFIX "g12a-toacodec");
}

static int axg_card_add_link(struct snd_soc_card *card, struct device_node *np,
			     int *index)
{
	struct snd_soc_dai_link *dai_link = &card->dai_link[*index];
	struct snd_soc_dai_link_component *cpu;
	int ret;

	cpu = devm_kzalloc(card->dev, sizeof(*cpu), GFP_KERNEL);
	if (!cpu)
		return -ENOMEM;

	dai_link->cpus = cpu;
	dai_link->num_cpus = 1;
	dai_link->nonatomic = true;

	ret = meson_card_parse_dai(card, np, &dai_link->cpus->of_node,
				   &dai_link->cpus->dai_name);
	if (ret)
		return ret;

	if (axg_card_cpu_is_playback_fe(dai_link->cpus->of_node))
		return meson_card_set_fe_link(card, dai_link, np, true);
	else if (axg_card_cpu_is_capture_fe(dai_link->cpus->of_node))
		return meson_card_set_fe_link(card, dai_link, np, false);


	ret = meson_card_set_be_link(card, dai_link, np);
	if (ret)
		return ret;

	if (axg_card_cpu_is_codec(dai_link->cpus->of_node)) {
		dai_link->params = &codec_params;
	} else {
		dai_link->no_pcm = 1;
		snd_soc_dai_link_set_capabilities(dai_link);
		if (axg_card_cpu_is_tdm_iface(dai_link->cpus->of_node))
			ret = axg_card_parse_tdm(card, np, index);
	}

	return ret;
}

static const struct meson_card_match_data axg_card_match_data = {
	.add_link = axg_card_add_link,
};

static const struct of_device_id axg_card_of_match[] = {
	{
		.compatible = "amlogic,axg-sound-card",
		.data = &axg_card_match_data,
	}, {}
};
MODULE_DEVICE_TABLE(of, axg_card_of_match);

static struct platform_driver axg_card_pdrv = {
	.probe = meson_card_probe,
	.remove = meson_card_remove,
	.driver = {
		.name = "axg-sound-card",
		.of_match_table = axg_card_of_match,
	},
};
module_platform_driver(axg_card_pdrv);

MODULE_DESCRIPTION("Amlogic AXG ALSA machine driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
