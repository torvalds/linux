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

#define CS35L41_HID "CSC3541"
#define CS35L41_MAX_AMPS 4

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

static struct snd_soc_dai_link_component cs35l41_components[CS35L41_MAX_AMPS];

/*
 * Mapping between ACPI instance id and speaker position.
 */
static struct snd_soc_codec_conf cs35l41_codec_conf[CS35L41_MAX_AMPS];

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
	{.rx = {0, 1}}, /* WL */
	{.rx = {1, 0}}, /* WR */
	{.rx = {0, 1}}, /* TL */
	{.rx = {1, 0}}, /* TR */
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

static const char * const cs35l41_name_prefixes[] = { "WL", "WR", "TL", "TR" };

/*
 * Expected UIDs are integers (stored as strings).
 * UID Mapping is fixed:
 * UID 0x0 -> WL
 * UID 0x1 -> WR
 * UID 0x2 -> TL
 * UID 0x3 -> TR
 * Note: If there are less than 4 Amps, UIDs still map to WL/WR/TL/TR. Dynamic code will only create
 * dai links for UIDs which exist, and ignore non-existant ones. Only 2 or 4 amps are expected.
 * Return number of codecs found.
 */
static int cs35l41_compute_codec_conf(void)
{
	const char * const uid_strings[] = { "0", "1", "2", "3" };
	unsigned int uid, sz = 0;
	struct acpi_device *adev;
	struct device *physdev;

	for (uid = 0; uid < CS35L41_MAX_AMPS; uid++) {
		adev = acpi_dev_get_first_match_dev(CS35L41_HID, uid_strings[uid], -1);
		if (!adev) {
			pr_devel("Cannot find match for HID %s UID %u (%s)\n", CS35L41_HID, uid,
				 cs35l41_name_prefixes[uid]);
			continue;
		}
		physdev = get_device(acpi_get_first_physical_node(adev));
		cs35l41_components[sz].name = dev_name(physdev);
		cs35l41_components[sz].dai_name = CS35L41_CODEC_DAI;
		cs35l41_codec_conf[sz].dlc.name = dev_name(physdev);
		cs35l41_codec_conf[sz].name_prefix = cs35l41_name_prefixes[uid];
		acpi_dev_put(adev);
		sz++;
	}

	if (sz != 2 && sz != 4)
		pr_warn("Invalid number of cs35l41 amps found: %d, expected 2 or 4\n", sz);
	return sz;
}

void cs35l41_set_dai_link(struct snd_soc_dai_link *link)
{
	link->num_codecs = cs35l41_compute_codec_conf();
	link->codecs = cs35l41_components;
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
