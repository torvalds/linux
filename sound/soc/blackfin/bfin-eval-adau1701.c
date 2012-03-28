/*
 * Machine driver for EVAL-ADAU1701MINIZ on Analog Devices bfin
 * evaluation boards.
 *
 * Copyright 2011 Analog Devices Inc.
 * Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

#include "../codecs/adau1701.h"

static const struct snd_soc_dapm_widget bfin_eval_adau1701_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
};

static const struct snd_soc_dapm_route bfin_eval_adau1701_dapm_routes[] = {
	{ "Speaker", NULL, "OUT0" },
	{ "Speaker", NULL, "OUT1" },
	{ "Line Out", NULL, "OUT2" },
	{ "Line Out", NULL, "OUT3" },

	{ "IN0", NULL, "Line In" },
	{ "IN1", NULL, "Line In" },
};

static int bfin_eval_adau1701_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, ADAU1701_CLK_SRC_OSC, 12288000,
			SND_SOC_CLOCK_IN);

	return ret;
}

static struct snd_soc_ops bfin_eval_adau1701_ops = {
	.hw_params = bfin_eval_adau1701_hw_params,
};

#define BFIN_EVAL_ADAU1701_DAI_FMT (SND_SOC_DAIFMT_I2S | \
				SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM)

static struct snd_soc_dai_link bfin_eval_adau1701_dai[] = {
	{
		.name = "adau1701",
		.stream_name = "adau1701",
		.cpu_dai_name = "bfin-i2s.0",
		.codec_dai_name = "adau1701",
		.platform_name = "bfin-i2s-pcm-audio",
		.codec_name = "adau1701.0-0034",
		.ops = &bfin_eval_adau1701_ops,
		.dai_fmt = BFIN_EVAL_ADAU1701_DAI_FMT,
	},
	{
		.name = "adau1701",
		.stream_name = "adau1701",
		.cpu_dai_name = "bfin-i2s.1",
		.codec_dai_name = "adau1701",
		.platform_name = "bfin-i2s-pcm-audio",
		.codec_name = "adau1701.0-0034",
		.ops = &bfin_eval_adau1701_ops,
		.dai_fmt = BFIN_EVAL_ADAU1701_DAI_FMT,
	},
};

static struct snd_soc_card bfin_eval_adau1701 = {
	.name = "bfin-eval-adau1701",
	.owner = THIS_MODULE,
	.dai_link = &bfin_eval_adau1701_dai[CONFIG_SND_BF5XX_SPORT_NUM],
	.num_links = 1,

	.dapm_widgets		= bfin_eval_adau1701_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(bfin_eval_adau1701_dapm_widgets),
	.dapm_routes		= bfin_eval_adau1701_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(bfin_eval_adau1701_dapm_routes),
};

static int bfin_eval_adau1701_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &bfin_eval_adau1701;

	card->dev = &pdev->dev;

	return snd_soc_register_card(&bfin_eval_adau1701);
}

static int __devexit bfin_eval_adau1701_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static struct platform_driver bfin_eval_adau1701_driver = {
	.driver = {
		.name = "bfin-eval-adau1701",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = bfin_eval_adau1701_probe,
	.remove = __devexit_p(bfin_eval_adau1701_remove),
};

module_platform_driver(bfin_eval_adau1701_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ALSA SoC bfin ADAU1701 driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:bfin-eval-adau1701");
