/*
 * mt8173-max98090.c  --  MT8173 MAX98090 ALSA SoC machine driver
 *
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Koro Chen <koro.chen@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <linux/gpio.h>
#include "../codecs/max98090.h"

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
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	return snd_soc_dai_set_sysclk(codec_dai, 0, params_rate(params) * 256,
				      SND_SOC_CLOCK_IN);
}

static struct snd_soc_ops mt8173_max98090_ops = {
	.hw_params = mt8173_max98090_hw_params,
};

static int mt8173_max98090_init(struct snd_soc_pcm_runtime *runtime)
{
	int ret;
	struct snd_soc_card *card = runtime->card;
	struct snd_soc_codec *codec = runtime->codec;

	/* enable jack detection */
	ret = snd_soc_card_jack_new(card, "Headphone", SND_JACK_HEADPHONE,
				    &mt8173_max98090_jack, NULL, 0);
	if (ret) {
		dev_err(card->dev, "Can't snd_soc_jack_new %d\n", ret);
		return ret;
	}

	ret = snd_soc_jack_add_pins(&mt8173_max98090_jack,
				    ARRAY_SIZE(mt8173_max98090_jack_pins),
				    mt8173_max98090_jack_pins);
	if (ret) {
		dev_err(card->dev, "Can't snd_soc_jack_add_pins %d\n", ret);
		return ret;
	}

	return max98090_mic_detect(codec, &mt8173_max98090_jack);
}

/* Digital audio interface glue - connects codec <---> CPU */
static struct snd_soc_dai_link mt8173_max98090_dais[] = {
	/* Front End DAI links */
	{
		.name = "MAX98090 Playback",
		.stream_name = "MAX98090 Playback",
		.cpu_dai_name = "DL1",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	{
		.name = "MAX98090 Capture",
		.stream_name = "MAX98090 Capture",
		.cpu_dai_name = "VUL",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	/* Back End DAI links */
	{
		.name = "Codec",
		.cpu_dai_name = "I2S",
		.no_pcm = 1,
		.codec_dai_name = "HiFi",
		.init = mt8173_max98090_init,
		.ops = &mt8173_max98090_ops,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
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
	int ret, i;

	platform_node = of_parse_phandle(pdev->dev.of_node,
					 "mediatek,platform", 0);
	if (!platform_node) {
		dev_err(&pdev->dev, "Property 'platform' missing or invalid\n");
		return -EINVAL;
	}
	for (i = 0; i < card->num_links; i++) {
		if (mt8173_max98090_dais[i].platform_name)
			continue;
		mt8173_max98090_dais[i].platform_of_node = platform_node;
	}

	codec_node = of_parse_phandle(pdev->dev.of_node,
				      "mediatek,audio-codec", 0);
	if (!codec_node) {
		dev_err(&pdev->dev,
			"Property 'audio-codec' missing or invalid\n");
		return -EINVAL;
	}
	for (i = 0; i < card->num_links; i++) {
		if (mt8173_max98090_dais[i].codec_name)
			continue;
		mt8173_max98090_dais[i].codec_of_node = codec_node;
	}
	card->dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "%s snd_soc_register_card fail %d\n",
			__func__, ret);
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
#ifdef CONFIG_PM
		   .pm = &snd_soc_pm_ops,
#endif
	},
	.probe = mt8173_max98090_dev_probe,
};

module_platform_driver(mt8173_max98090_driver);

/* Module information */
MODULE_DESCRIPTION("MT8173 MAX98090 ALSA SoC machine driver");
MODULE_AUTHOR("Koro Chen <koro.chen@mediatek.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mt8173-max98090");

