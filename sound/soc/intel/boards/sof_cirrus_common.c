// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file defines data structures and functions used in Machine
 * Driver for Intel platforms with Cirrus Logic Codecs.
 *
 * Copyright 2022 Intel Corporation.
 */
#include <linux/module.h>
#include <sound/sof.h>
#include "../../codecs/cs35l41.h"
#include "sof_cirrus_common.h"

/*
 * Cirrus Logic CS35L41/CS35L53
 */
static const struct snd_kcontrol_new cs35l41_kcontrols[] = {
	SOC_DAPM_PIN_SWITCH("WL Spk"),
	SOC_DAPM_PIN_SWITCH("WR Spk"),
	SOC_DAPM_PIN_SWITCH("TL Spk"),
	SOC_DAPM_PIN_SWITCH("TR Spk"),
};

static const struct snd_soc_dapm_widget cs35l41_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("WL Spk", NULL),
	SND_SOC_DAPM_SPK("WR Spk", NULL),
	SND_SOC_DAPM_SPK("TL Spk", NULL),
	SND_SOC_DAPM_SPK("TR Spk", NULL),
};

static const struct snd_soc_dapm_route cs35l41_dapm_routes[] = {
	/* speaker */
	{"WL Spk", NULL, "WL SPK"},
	{"WR Spk", NULL, "WR SPK"},
	{"TL Spk", NULL, "TL SPK"},
	{"TR Spk", NULL, "TR SPK"},
};

static struct snd_soc_dai_link_component cs35l41_components[] = {
	{
		.name = CS35L41_DEV0_NAME,
		.dai_name = CS35L41_CODEC_DAI,
	},
	{
		.name = CS35L41_DEV1_NAME,
		.dai_name = CS35L41_CODEC_DAI,
	},
	{
		.name = CS35L41_DEV2_NAME,
		.dai_name = CS35L41_CODEC_DAI,
	},
	{
		.name = CS35L41_DEV3_NAME,
		.dai_name = CS35L41_CODEC_DAI,
	},
};

/*
 * Mapping between ACPI instance id and speaker position.
 *
 * Four speakers:
 *         0: Tweeter left, 1: Woofer left
 *         2: Tweeter right, 3: Woofer right
 */
static struct snd_soc_codec_conf cs35l41_codec_conf[] = {
	{
		.dlc = COMP_CODEC_CONF(CS35L41_DEV0_NAME),
		.name_prefix = "TL",
	},
	{
		.dlc = COMP_CODEC_CONF(CS35L41_DEV1_NAME),
		.name_prefix = "WL",
	},
	{
		.dlc = COMP_CODEC_CONF(CS35L41_DEV2_NAME),
		.name_prefix = "TR",
	},
	{
		.dlc = COMP_CODEC_CONF(CS35L41_DEV3_NAME),
		.name_prefix = "WR",
	},
};

static int cs35l41_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dapm_new_controls(&card->dapm, cs35l41_dapm_widgets,
					ARRAY_SIZE(cs35l41_dapm_widgets));
	if (ret) {
		dev_err(rtd->dev, "fail to add dapm controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, cs35l41_kcontrols,
					ARRAY_SIZE(cs35l41_kcontrols));
	if (ret) {
		dev_err(rtd->dev, "fail to add card controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, cs35l41_dapm_routes,
				      ARRAY_SIZE(cs35l41_dapm_routes));

	if (ret)
		dev_err(rtd->dev, "fail to add dapm routes, ret %d\n", ret);

	return ret;
}

/*
 * Channel map:
 *
 * TL/WL: ASPRX1 on slot 0, ASPRX2 on slot 1 (default)
 * TR/WR: ASPRX1 on slot 1, ASPRX2 on slot 0
 */
static const struct {
	unsigned int rx[2];
} cs35l41_channel_map[] = {
	{.rx = {0, 1}}, /* TL */
	{.rx = {0, 1}}, /* WL */
	{.rx = {1, 0}}, /* TR */
	{.rx = {1, 0}}, /* WR */
};

static int cs35l41_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai;
	int clk_freq, i, ret;

	clk_freq = sof_dai_get_bclk(rtd); /* BCLK freq */

	if (clk_freq <= 0) {
		dev_err(rtd->dev, "fail to get bclk freq, ret %d\n", clk_freq);
		return -EINVAL;
	}

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		/* call dai driver's set_sysclk() callback */
		ret = snd_soc_dai_set_sysclk(codec_dai, CS35L41_CLKID_SCLK,
					     clk_freq, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(codec_dai->dev, "fail to set sysclk, ret %d\n",
				ret);
			return ret;
		}

		/* call component driver's set_sysclk() callback */
		ret = snd_soc_component_set_sysclk(codec_dai->component,
						   CS35L41_CLKID_SCLK, 0,
						   clk_freq, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(codec_dai->dev, "fail to set component sysclk, ret %d\n",
				ret);
			return ret;
		}

		/* setup channel map */
		ret = snd_soc_dai_set_channel_map(codec_dai, 0, NULL,
						  ARRAY_SIZE(cs35l41_channel_map[i].rx),
						  (unsigned int *)cs35l41_channel_map[i].rx);
		if (ret < 0) {
			dev_err(codec_dai->dev, "fail to set channel map, ret %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static const struct snd_soc_ops cs35l41_ops = {
	.hw_params = cs35l41_hw_params,
};

void cs35l41_set_dai_link(struct snd_soc_dai_link *link)
{
	link->codecs = cs35l41_components;
	link->num_codecs = ARRAY_SIZE(cs35l41_components);
	link->init = cs35l41_init;
	link->ops = &cs35l41_ops;
}
EXPORT_SYMBOL_NS(cs35l41_set_dai_link, SND_SOC_INTEL_SOF_CIRRUS_COMMON);

void cs35l41_set_codec_conf(struct snd_soc_card *card)
{
	card->codec_conf = cs35l41_codec_conf;
	card->num_configs = ARRAY_SIZE(cs35l41_codec_conf);
}
EXPORT_SYMBOL_NS(cs35l41_set_codec_conf, SND_SOC_INTEL_SOF_CIRRUS_COMMON);

MODULE_DESCRIPTION("ASoC Intel SOF Cirrus Logic helpers");
MODULE_LICENSE("GPL");
