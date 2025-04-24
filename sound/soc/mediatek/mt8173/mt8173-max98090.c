// SPDX-License-Identifier: GPL-2.0
/*
 * mt8173-max98090.c  --  MT8173 MAX98090 ALSA SoC machine driver
 *
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Koro Chen <koro.chen@mediatek.com>
 */

#include <linux/module.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include "../../codecs/max98090.h"

static struct snd_soc_jack mt8173_max98090_jack;

static struct snd_soc_jack_pin mt8173_max98090_jack_pins[] = {
	{
		.pin	= "Headphone",
		.mask	= SND_JACK_HEADPHONE,
	},
	{
		.pin	= "Headset Mic",
		.mask	= SND_JACK_MICROPHONE,
	},
};

static const struct snd_soc_dapm_widget mt8173_max98090_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route mt8173_max98090_routes[] = {
	{"Speaker", NULL, "SPKL"},
	{"Speaker", NULL, "SPKR"},
	{"DMICL", NULL, "Int Mic"},
	{"Headphone", NULL, "HPL"},
	{"Headphone", NULL, "HPR"},
	{"Headset Mic", NULL, "MICBIAS"},
	{"IN34", NULL, "Headset Mic"},
};

static const struct snd_kcontrol_new mt8173_max98090_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static int mt8173_max98090_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);

	return snd_soc_dai_set_sysclk(codec_dai, 0, params_rate(params) * 256,
				      SND_SOC_CLOCK_IN);
}

static const struct snd_soc_ops mt8173_max98090_ops = {
	.hw_params = mt8173_max98090_hw_params,
};

static int mt8173_max98090_init(struct snd_soc_pcm_runtime *runtime)
{
	int ret;
	struct snd_soc_card *card = runtime->card;
	struct snd_soc_component *component = snd_soc_rtd_to_codec(runtime, 0)->component;

	/* enable jack detection */
	ret = snd_soc_card_jack_new_pins(card, "Headphone", SND_JACK_HEADSET,
					 &mt8173_max98090_jack,
					 mt8173_max98090_jack_pins,
					 ARRAY_SIZE(mt8173_max98090_jack_pins));
	if (ret) {
		dev_err(card->dev, "Can't create a new Jack %d\n", ret);
		return ret;
	}

	return max98090_mic_detect(component, &mt8173_max98090_jack);
}

SND_SOC_DAILINK_DEFS(playback,
	DAILINK_COMP_ARRAY(COMP_CPU("DL1")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture,
	DAILINK_COMP_ARRAY(COMP_CPU("VUL")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(hifi,
	DAILINK_COMP_ARRAY(COMP_CPU("I2S")),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "HiFi")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

/* Digital audio interface glue - connects codec <---> CPU */
static struct snd_soc_dai_link mt8173_max98090_dais[] = {
	/* Front End DAI links */
	{
		.name = "MAX98090 Playback",
		.stream_name = "MAX98090 Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback),
	},
	{
		.name = "MAX98090 Capture",
		.stream_name = "MAX98090 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture),
	},
	/* Back End DAI links */
	{
		.name = "Codec",
		.no_pcm = 1,
		.init = mt8173_max98090_init,
		.ops = &mt8173_max98090_ops,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBC_CFC,
		SND_SOC_DAILINK_REG(hifi),
	},
};

static struct snd_soc_card mt8173_max98090_card = {
	.name = "mt8173-max98090",
	.owner = THIS_MODULE,
	.dai_link = mt8173_max98090_dais,
	.num_links = ARRAY_SIZE(mt8173_max98090_dais),
	.controls = mt8173_max98090_controls,
	.num_controls = ARRAY_SIZE(mt8173_max98090_controls),
	.dapm_widgets = mt8173_max98090_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt8173_max98090_widgets),
	.dapm_routes = mt8173_max98090_routes,
	.num_dapm_routes = ARRAY_SIZE(mt8173_max98090_routes),
};

static int mt8173_max98090_dev_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &mt8173_max98090_card;
	struct device_node *codec_node, *platform_node;
	struct snd_soc_dai_link *dai_link;
	int ret, i;

	platform_node = of_parse_phandle(pdev->dev.of_node,
					 "mediatek,platform", 0);
	if (!platform_node) {
		dev_err(&pdev->dev, "Property 'platform' missing or invalid\n");
		return -EINVAL;
	}
	for_each_card_prelinks(card, i, dai_link) {
		if (dai_link->platforms->name)
			continue;
		dai_link->platforms->of_node = platform_node;
	}

	codec_node = of_parse_phandle(pdev->dev.of_node,
				      "mediatek,audio-codec", 0);
	if (!codec_node) {
		dev_err(&pdev->dev,
			"Property 'audio-codec' missing or invalid\n");
		ret = -EINVAL;
		goto put_platform_node;
	}
	for_each_card_prelinks(card, i, dai_link) {
		if (dai_link->codecs->name)
			continue;
		dai_link->codecs->of_node = codec_node;
	}
	card->dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, card);

	of_node_put(codec_node);

put_platform_node:
	of_node_put(platform_node);
	return ret;
}

static const struct of_device_id mt8173_max98090_dt_match[] = {
	{ .compatible = "mediatek,mt8173-max98090", },
	{ }
};
MODULE_DEVICE_TABLE(of, mt8173_max98090_dt_match);

static struct platform_driver mt8173_max98090_driver = {
	.driver = {
		   .name = "mt8173-max98090",
		   .of_match_table = mt8173_max98090_dt_match,
		   .pm = &snd_soc_pm_ops,
	},
	.probe = mt8173_max98090_dev_probe,
};

module_platform_driver(mt8173_max98090_driver);

/* Module information */
MODULE_DESCRIPTION("MT8173 MAX98090 ALSA SoC machine driver");
MODULE_AUTHOR("Koro Chen <koro.chen@mediatek.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mt8173-max98090");

