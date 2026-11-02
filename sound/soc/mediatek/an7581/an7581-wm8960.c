// SPDX-License-Identifier: GPL-2.0
/*
 * Airoha ALSA SoC I2S platform driver for AN7581
 *
 */

#include <linux/module.h>
#include <sound/soc.h>

#include "an7581-afe-common.h"

SND_SOC_DAILINK_DEFS(playback,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY(), COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY(), COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(codec,
		     DAILINK_COMP_ARRAY(COMP_CPU("ETDM")),
		     DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "wm8960-hifi")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY(), COMP_EMPTY()));

static struct snd_soc_dai_link an7581_wm8960_dai_links[] = {
	/* FE */
	{
		.name = "wm8960-playback",
		.stream_name = "wm8960-playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback),
	},
	{
		.name = "wm8960-capture",
		.stream_name = "wm8960-capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture),
	},
	/* BE */
	{
		.name = "wm8960-codec",
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBP_CFP |
			SND_SOC_DAIFMT_GATED,
		SND_SOC_DAILINK_REG(codec),
	},
};

static struct snd_soc_card an7581_wm8960_card = {
	.name = "an7581-wm8960",
	.owner = THIS_MODULE,
	.dai_link = an7581_wm8960_dai_links,
	.num_links = ARRAY_SIZE(an7581_wm8960_dai_links),
};

static int an7581_wm8960_machine_probe(struct platform_device *pdev)
{
	struct device_node *platform_dai_node, *codec_dai_node;
	struct snd_soc_card *card = &an7581_wm8960_card;
	struct device_node *of_platform, *codec;
	struct snd_soc_dai_link *dai_link;
	int i, d, ret;

	card->dev = &pdev->dev;

	of_platform = of_get_child_by_name(pdev->dev.of_node, "platform");

	if (of_platform) {
		platform_dai_node = of_parse_phandle(of_platform, "sound-dai", 0);
		of_node_put(of_platform);

		if (!platform_dai_node) {
			dev_err(&pdev->dev, "Failed to parse platform/sound-dai property\n");
			return -EINVAL;
		}
	} else {
		dev_err(&pdev->dev, "Property 'platform' missing or invalid\n");
		return -EINVAL;
	}

	for_each_card_prelinks(card, i, dai_link) {
		struct snd_soc_dai_link_component *platform;

		dai_link->num_platforms = 2;
		for_each_link_platforms(dai_link, d, platform) {
			if (platform->name)
				continue;
			platform->of_node = platform_dai_node;
		}
	}

	codec = of_get_child_by_name(pdev->dev.of_node, "codec");

	if (codec) {
		codec_dai_node = of_parse_phandle(codec, "sound-dai", 0);
		of_node_put(codec);

		if (!codec_dai_node) {
			of_node_put(platform_dai_node);
			dev_err(&pdev->dev, "Failed to parse codec/sound-dai property\n");
			return -EINVAL;
		}
	} else {
		of_node_put(platform_dai_node);
		dev_err(&pdev->dev, "Property 'codec' missing or invalid\n");
		return -EINVAL;
	}

	for_each_card_prelinks(card, i, dai_link) {
		if (dai_link->codecs->name)
			continue;
		dai_link->codecs->of_node = codec_dai_node;
	}

	ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
	if (ret) {
		dev_err(&pdev->dev, "Failed to parse audio-routing: %d\n", ret);
		goto err_of_node_put;
	}

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err_probe(&pdev->dev, ret, "%s snd_soc_register_card fail\n", __func__);
		goto err_of_node_put;
	}

	return 0;

err_of_node_put:
	of_node_put(platform_dai_node);
	of_node_put(codec_dai_node);
	return ret;
}

static const struct of_device_id an7581_wm8960_machine_dt_match[] = {
	{ .compatible = "airoha,an7581-wm8960-sound" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, an7581_wm8960_machine_dt_match);

static struct platform_driver an7581_wm8960_driver = {
	.driver = {
		   .name = "an7581-wm8960",
		   .of_match_table = an7581_wm8960_machine_dt_match,
	},
	.probe = an7581_wm8960_machine_probe,
};
module_platform_driver(an7581_wm8960_driver);

MODULE_DESCRIPTION("Airoha SoC I2S platform driver for ALSA AN7581");
MODULE_LICENSE("GPL");
