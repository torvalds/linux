/*
 * ASoC machine driver for Snow boards
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <sound/soc.h>

#include "i2s.h"

#define FIN_PLL_RATE		24000000

static struct snd_soc_dai_link snow_dai[] = {
	{
		.name = "Primary",
		.stream_name = "Primary",
		.codec_dai_name = "HiFi",
		.dai_fmt = SND_SOC_DAIFMT_I2S |
				SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBS_CFS,
	},
};

static int snow_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_dai *codec_dai = card->rtd[0].codec_dai;
	struct snd_soc_dai *cpu_dai = card->rtd[0].cpu_dai;
	int ret;

	/* Set the MCLK rate for the codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0,
					FIN_PLL_RATE, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* Select I2S Bus clock to set RCLK and BCLK */
	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_RCLKSRC_0,
					0, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_card snow_snd = {
	.name = "Snow-I2S",
	.dai_link = snow_dai,
	.num_links = ARRAY_SIZE(snow_dai),

	.late_probe = snow_late_probe,
};

static int snow_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snow_snd;
	struct device_node *i2s_node, *codec_node;
	int i, ret;

	i2s_node = of_parse_phandle(pdev->dev.of_node,
				    "samsung,i2s-controller", 0);
	if (!i2s_node) {
		dev_err(&pdev->dev,
			"Property 'i2s-controller' missing or invalid\n");
		return -EINVAL;
	}

	codec_node = of_parse_phandle(pdev->dev.of_node,
				      "samsung,audio-codec", 0);
	if (!codec_node) {
		dev_err(&pdev->dev,
			"Property 'audio-codec' missing or invalid\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(snow_dai); i++) {
		snow_dai[i].codec_of_node = codec_node;
		snow_dai[i].cpu_of_node = i2s_node;
		snow_dai[i].platform_of_node = i2s_node;
	}

	card->dev = &pdev->dev;

	/* Update card-name if provided through DT, else use default name */
	snd_soc_of_parse_card_name(card, "samsung,model");

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		return ret;
	}

	return ret;
}

static const struct of_device_id snow_of_match[] = {
	{ .compatible = "google,snow-audio-max98090", },
	{ .compatible = "google,snow-audio-max98091", },
	{ .compatible = "google,snow-audio-max98095", },
	{},
};

static struct platform_driver snow_driver = {
	.driver = {
		.name = "snow-audio",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = snow_of_match,
	},
	.probe = snow_probe,
};

module_platform_driver(snow_driver);

MODULE_DESCRIPTION("ALSA SoC Audio machine driver for Snow");
MODULE_LICENSE("GPL");
