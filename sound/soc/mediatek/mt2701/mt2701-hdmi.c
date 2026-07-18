// SPDX-License-Identifier: GPL-2.0
/*
 * mt2701-hdmi.c -- MT2701 HDMI ALSA SoC machine driver
 *
 * Copyright (c) 2026 Daniel Golle <daniel@makrotopia.org>
 *
 * Based on mt2701-cs42448.c
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/soc.h>

enum {
	DAI_LINK_FE_HDMI_OUT,
	DAI_LINK_BE_HDMI_I2S,
};

SND_SOC_DAILINK_DEFS(fe_hdmi_out,
	DAILINK_COMP_ARRAY(COMP_CPU("PCM_HDMI")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(be_hdmi_i2s,
	DAILINK_COMP_ARRAY(COMP_CPU("HDMI I2S")),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "i2s-hifi")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link mt2701_hdmi_dai_links[] = {
	[DAI_LINK_FE_HDMI_OUT] = {
		.name = "HDMI Playback",
		.stream_name = "HDMI Playback",
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
			     SND_SOC_DPCM_TRIGGER_POST },
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(fe_hdmi_out),
	},
	[DAI_LINK_BE_HDMI_I2S] = {
		.name = "HDMI BE",
		.no_pcm = 1,
		.playback_only = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBC_CFC,
		SND_SOC_DAILINK_REG(be_hdmi_i2s),
	},
};

static struct snd_soc_card mt2701_hdmi_soc_card = {
	.name = "mt2701-hdmi",
	.owner = THIS_MODULE,
	.dai_link = mt2701_hdmi_dai_links,
	.num_links = ARRAY_SIZE(mt2701_hdmi_dai_links),
};

static int mt2701_hdmi_machine_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &mt2701_hdmi_soc_card;
	struct device *dev = &pdev->dev;
	struct device_node *platform_node;
	struct device_node *codec_node;
	struct snd_soc_dai_link *dai_link;
	int ret;
	int i;

	platform_node = of_parse_phandle(dev->of_node, "mediatek,platform", 0);
	if (!platform_node)
		return dev_err_probe(dev, -EINVAL,
				     "Property 'mediatek,platform' missing\n");

	for_each_card_prelinks(card, i, dai_link) {
		if (dai_link->platforms->name)
			continue;
		dai_link->platforms->of_node = platform_node;
	}

	codec_node = of_parse_phandle(dev->of_node, "mediatek,audio-codec", 0);
	if (!codec_node) {
		of_node_put(platform_node);
		return dev_err_probe(dev, -EINVAL,
				     "Property 'mediatek,audio-codec' missing\n");
	}
	mt2701_hdmi_dai_links[DAI_LINK_BE_HDMI_I2S].codecs->of_node = codec_node;

	card->dev = dev;

	ret = devm_snd_soc_register_card(dev, card);

	of_node_put(platform_node);
	of_node_put(codec_node);
	return ret;
}

static const struct of_device_id mt2701_hdmi_machine_dt_match[] = {
	{ .compatible = "mediatek,mt2701-hdmi-audio" },
	{ .compatible = "mediatek,mt7623n-hdmi-audio" },
	{}
};
MODULE_DEVICE_TABLE(of, mt2701_hdmi_machine_dt_match);

static struct platform_driver mt2701_hdmi_machine = {
	.driver = {
		.name = "mt2701-hdmi",
		.of_match_table = mt2701_hdmi_machine_dt_match,
	},
	.probe = mt2701_hdmi_machine_probe,
};
module_platform_driver(mt2701_hdmi_machine);

MODULE_DESCRIPTION("MT2701 HDMI ALSA SoC machine driver");
MODULE_AUTHOR("Daniel Golle <daniel@makrotopia.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mt2701-hdmi");
