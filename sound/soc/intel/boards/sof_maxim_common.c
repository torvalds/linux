// SPDX-License-Identifier: GPL-2.0
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
#include <linux/string.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>
#include <uapi/sound/asound.h>
#include "sof_maxim_common.h"

static const struct snd_soc_dapm_route max_98373_dapm_routes[] = {
	/* speaker */
	{ "Left Spk", NULL, "Left BE_OUT" },
	{ "Right Spk", NULL, "Right BE_OUT" },
};

static struct snd_soc_codec_conf max_98373_codec_conf[] = {
	{
		.dlc = COMP_CODEC_CONF(MAX_98373_DEV0_NAME),
		.name_prefix = "Right",
	},
	{
		.dlc = COMP_CODEC_CONF(MAX_98373_DEV1_NAME),
		.name_prefix = "Left",
	},
};

struct snd_soc_dai_link_component max_98373_components[] = {
	{  /* For Left */
		.name = MAX_98373_DEV0_NAME,
		.dai_name = MAX_98373_CODEC_DAI,
	},
	{  /* For Right */
		.name = MAX_98373_DEV1_NAME,
		.dai_name = MAX_98373_CODEC_DAI,
	},
};

static int max98373_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai;
	int j;

	for_each_rtd_codec_dais(rtd, j, codec_dai) {
		if (!strcmp(codec_dai->component->name, MAX_98373_DEV0_NAME)) {
			/* DEV0 tdm slot configuration */
			snd_soc_dai_set_tdm_slot(codec_dai, 0x30, 3, 8, 16);
		}
		if (!strcmp(codec_dai->component->name, MAX_98373_DEV1_NAME)) {
			/* DEV1 tdm slot configuration */
			snd_soc_dai_set_tdm_slot(codec_dai, 0xC0, 3, 8, 16);
		}
	}
	return 0;
}

struct snd_soc_ops max_98373_ops = {
	.hw_params = max98373_hw_params,
};

int max98373_spk_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dapm_add_routes(&card->dapm, max_98373_dapm_routes,
				      ARRAY_SIZE(max_98373_dapm_routes));
	if (ret)
		dev_err(rtd->dev, "Speaker map addition failed: %d\n", ret);
	return ret;
}

void sof_max98373_codec_conf(struct snd_soc_card *card)
{
	card->codec_conf = max_98373_codec_conf;
	card->num_configs = ARRAY_SIZE(max_98373_codec_conf);
}
