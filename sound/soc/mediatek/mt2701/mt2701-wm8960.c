// SPDX-License-Identifier: GPL-2.0
/*
 * mt2701-wm8960.c  --  MT2701 WM8960 ALSA SoC machine driver
 *
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 */

#include <linux/module.h>
#include <sound/soc.h>

#include "mt2701-afe-common.h"

static const struct snd_soc_dapm_widget mt2701_wm8960_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("AMIC", NULL),
};

static const struct snd_kcontrol_new mt2701_wm8960_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("AMIC"),
};

static int mt2701_wm8960_be_ops_hw_params(struct snd_pcm_substream *substream,
					  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int mclk_rate;
	unsigned int rate = params_rate(params);
	unsigned int div_mclk_over_bck = rate > 192000 ? 2 : 4;
	unsigned int div_bck_over_lrck = 64;

	mclk_rate = rate * div_bck_over_lrck * div_mclk_over_bck;

	snd_soc_dai_set_sysclk(cpu_dai, 0, mclk_rate, SND_SOC_CLOCK_OUT);
	snd_soc_dai_set_sysclk(codec_dai, 0, mclk_rate, SND_SOC_CLOCK_IN);

	return 0;
}

static struct snd_soc_ops mt2701_wm8960_be_ops = {
	.hw_params = mt2701_wm8960_be_ops_hw_params
};

static struct snd_soc_dai_link mt2701_wm8960_dai_links[] = {
	/* FE */
	{
		.name = "wm8960-playback",
		.stream_name = "wm8960-playback",
		.cpu_dai_name = "PCMO0",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	{
		.name = "wm8960-capture",
		.stream_name = "wm8960-capture",
		.cpu_dai_name = "PCM0",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	/* BE */
	{
		.name = "wm8960-codec",
		.cpu_dai_name = "I2S0",
		.no_pcm = 1,
		.codec_dai_name = "wm8960-hifi",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS
			| SND_SOC_DAIFMT_GATED,
		.ops = &mt2701_wm8960_be_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
};

static struct snd_soc_card mt2701_wm8960_card = {
	.name = "mt2701-wm8960",
	.owner = THIS_MODULE,
	.dai_link = mt2701_wm8960_dai_links,
	.num_links = ARRAY_SIZE(mt2701_wm8960_dai_links),
	.controls = mt2701_wm8960_controls,
	.num_controls = ARRAY_SIZE(mt2701_wm8960_controls),
	.dapm_widgets = mt2701_wm8960_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt2701_wm8960_widgets),
};

static int mt2701_wm8960_machine_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &mt2701_wm8960_card;
	struct device_node *platform_node, *codec_node;
	struct snd_soc_dai_link *dai_link;
	int ret, i;

	platform_node = of_parse_phandle(pdev->dev.of_node,
					 "mediatek,platform", 0);
	if (!platform_node) {
		dev_err(&pdev->dev, "Property 'platform' missing or invalid\n");
		return -EINVAL;
	}
	for_each_card_prelinks(card, i, dai_link) {
		if (dai_link->platform_name)
			continue;
		dai_link->platform_of_node = platform_node;
	}

	card->dev = &pdev->dev;

	codec_node = of_parse_phandle(pdev->dev.of_node,
				      "mediatek,audio-codec", 0);
	if (!codec_node) {
		dev_err(&pdev->dev,
			"Property 'audio-codec' missing or invalid\n");
		return -EINVAL;
	}
	for_each_card_prelinks(card, i, dai_link) {
		if (dai_link->codec_name)
			continue;
		dai_link->codec_of_node = codec_node;
	}

	ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
	if (ret) {
		dev_err(&pdev->dev, "failed to parse audio-routing: %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "%s snd_soc_register_card fail %d\n",
			__func__, ret);

	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id mt2701_wm8960_machine_dt_match[] = {
	{.compatible = "mediatek,mt2701-wm8960-machine",},
	{}
};
#endif

static struct platform_driver mt2701_wm8960_machine = {
	.driver = {
		.name = "mt2701-wm8960",
#ifdef CONFIG_OF
		.of_match_table = mt2701_wm8960_machine_dt_match,
#endif
	},
	.probe = mt2701_wm8960_machine_probe,
};

module_platform_driver(mt2701_wm8960_machine);

/* Module information */
MODULE_DESCRIPTION("MT2701 WM8960 ALSA SoC machine driver");
MODULE_AUTHOR("Ryder Lee <ryder.lee@mediatek.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("mt2701 wm8960 soc card");

