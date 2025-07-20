// SPDX-License-Identifier: GPL-2.0-only
// This file incorporates work covered by the following copyright notice:
// Copyright (c) 2024 Intel Corporation
// Copyright (c) 2024 Advanced Micro Devices, Inc.

/*
 * soc_sdw_bridge_cs35l56 - codec helper functions for handling CS35L56 Smart AMP
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc_sdw_utils.h>

static const struct snd_soc_dapm_widget bridge_widgets[] = {
	SND_SOC_DAPM_SPK("Bridge Speaker", NULL),
};

static const struct snd_soc_dapm_route bridge_map[] = {
	{"Bridge Speaker", NULL, "AMPL SPK"},
	{"Bridge Speaker", NULL, "AMPR SPK"},
};

static const char * const bridge_cs35l56_name_prefixes[] = {
	"AMPL",
	"AMPR",
};

static int asoc_sdw_bridge_cs35l56_asp_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int i, ret;
	unsigned int rx_mask = 3; // ASP RX1, RX2
	unsigned int tx_mask = 3; // ASP TX1, TX2
	struct snd_soc_dai *codec_dai;
	struct snd_soc_dai *cpu_dai;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s spk:cs35l56-bridge",
					  card->components);
	if (!card->components)
		return -ENOMEM;

	ret = snd_soc_dapm_new_controls(&card->dapm, bridge_widgets,
					ARRAY_SIZE(bridge_widgets));
	if (ret) {
		dev_err(card->dev, "widgets addition failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, bridge_map, ARRAY_SIZE(bridge_map));
	if (ret) {
		dev_err(card->dev, "map addition failed: %d\n", ret);
		return ret;
	}

	/* 4 x 16-bit sample slots and FSYNC=48000, BCLK=3.072 MHz */
	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		ret = asoc_sdw_cs35l56_volume_limit(card, codec_dai->component->name_prefix);
		if (ret)
			return ret;

		ret = snd_soc_dai_set_tdm_slot(codec_dai, tx_mask, rx_mask, 4, 16);
		if (ret < 0)
			return ret;

		ret = snd_soc_dai_set_sysclk(codec_dai, 0, 3072000, SND_SOC_CLOCK_IN);
		if (ret < 0)
			return ret;
	}

	for_each_rtd_cpu_dais(rtd, i, cpu_dai) {
		ret = snd_soc_dai_set_tdm_slot(cpu_dai, tx_mask, rx_mask, 4, 16);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static const struct snd_soc_pcm_stream asoc_sdw_bridge_params = {
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
};

SND_SOC_DAILINK_DEFS(asoc_sdw_bridge_dai,
		     DAILINK_COMP_ARRAY(COMP_CODEC("cs42l43-codec", "cs42l43-asp")),
		     DAILINK_COMP_ARRAY(COMP_CODEC("spi-cs35l56-left", "cs35l56-asp1"),
					COMP_CODEC("spi-cs35l56-right", "cs35l56-asp1")),
		     DAILINK_COMP_ARRAY(COMP_PLATFORM("cs42l43-codec")));

static const struct snd_soc_dai_link bridge_dai_template = {
	.name = "cs42l43-cs35l56",
	.init = asoc_sdw_bridge_cs35l56_asp_init,
	.c2c_params = &asoc_sdw_bridge_params,
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_IB_IF | SND_SOC_DAIFMT_CBC_CFC,
	SND_SOC_DAILINK_REG(asoc_sdw_bridge_dai),
};

int asoc_sdw_bridge_cs35l56_count_sidecar(struct snd_soc_card *card,
					  int *num_dais, int *num_devs)
{
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);

	if (ctx->mc_quirk & SOC_SDW_SIDECAR_AMPS) {
		(*num_dais)++;
		(*num_devs) += ARRAY_SIZE(bridge_cs35l56_name_prefixes);
	}

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_bridge_cs35l56_count_sidecar, "SND_SOC_SDW_UTILS");

int asoc_sdw_bridge_cs35l56_add_sidecar(struct snd_soc_card *card,
					struct snd_soc_dai_link **dai_links,
					struct snd_soc_codec_conf **codec_conf)
{
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);

	if (ctx->mc_quirk & SOC_SDW_SIDECAR_AMPS) {
		**dai_links = bridge_dai_template;

		for (int i = 0; i < ARRAY_SIZE(bridge_cs35l56_name_prefixes); i++) {
			(*codec_conf)->dlc.name = (*dai_links)->codecs[i].name;
			(*codec_conf)->name_prefix = bridge_cs35l56_name_prefixes[i];
			(*codec_conf)++;
		}

		(*dai_links)++;
	}

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_bridge_cs35l56_add_sidecar, "SND_SOC_SDW_UTILS");

int asoc_sdw_bridge_cs35l56_spk_init(struct snd_soc_card *card,
				     struct snd_soc_dai_link *dai_links,
				     struct asoc_sdw_codec_info *info,
				     bool playback)
{
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);

	if (ctx->mc_quirk & SOC_SDW_SIDECAR_AMPS)
		info->amp_num += ARRAY_SIZE(bridge_cs35l56_name_prefixes);

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_bridge_cs35l56_spk_init, "SND_SOC_SDW_UTILS");
