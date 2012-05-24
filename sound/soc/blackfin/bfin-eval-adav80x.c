/*
 * Machine driver for EVAL-ADAV801 and EVAL-ADAV803 on Analog Devices bfin
 * evaluation boards.
 *
 * Copyright 2011 Analog Devices Inc.
 * Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include "../codecs/adav80x.h"

static const struct snd_soc_dapm_widget bfin_eval_adav80x_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Line Out", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
};

static const struct snd_soc_dapm_route bfin_eval_adav80x_dapm_routes[] = {
	{ "Line Out", NULL, "VOUTL" },
	{ "Line Out", NULL, "VOUTR" },

	{ "VINL", NULL, "Line In" },
	{ "VINR", NULL, "Line In" },
};

static int bfin_eval_adav80x_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_pll(codec_dai, ADAV80X_PLL1, ADAV80X_PLL_SRC_XTAL,
			27000000, params_rate(params) * 256);
	if (ret)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, ADAV80X_CLK_PLL1,
			params_rate(params) * 256, SND_SOC_CLOCK_IN);

	return ret;
}

static int bfin_eval_adav80x_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	snd_soc_dai_set_sysclk(codec_dai, ADAV80X_CLK_SYSCLK1, 0,
	    SND_SOC_CLOCK_OUT);
	snd_soc_dai_set_sysclk(codec_dai, ADAV80X_CLK_SYSCLK2, 0,
	    SND_SOC_CLOCK_OUT);
	snd_soc_dai_set_sysclk(codec_dai, ADAV80X_CLK_SYSCLK3, 0,
	    SND_SOC_CLOCK_OUT);

	snd_soc_dai_set_sysclk(codec_dai, ADAV80X_CLK_XTAL, 2700000, 0);

	return 0;
}

static struct snd_soc_ops bfin_eval_adav80x_ops = {
	.hw_params = bfin_eval_adav80x_hw_params,
};

static struct snd_soc_dai_link bfin_eval_adav80x_dais[] = {
	{
		.name = "adav80x",
		.stream_name = "ADAV80x HiFi",
		.cpu_dai_name = "bfin-i2s.0",
		.codec_dai_name = "adav80x-hifi",
		.platform_name = "bfin-i2s-pcm-audio",
		.init = bfin_eval_adav80x_codec_init,
		.ops = &bfin_eval_adav80x_ops,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBM_CFM,
	},
};

static struct snd_soc_card bfin_eval_adav80x = {
	.name = "bfin-eval-adav80x",
	.owner = THIS_MODULE,
	.dai_link = bfin_eval_adav80x_dais,
	.num_links = ARRAY_SIZE(bfin_eval_adav80x_dais),

	.dapm_widgets		= bfin_eval_adav80x_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(bfin_eval_adav80x_dapm_widgets),
	.dapm_routes		= bfin_eval_adav80x_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(bfin_eval_adav80x_dapm_routes),
};

enum bfin_eval_adav80x_type {
	BFIN_EVAL_ADAV801,
	BFIN_EVAL_ADAV803,
};

static int bfin_eval_adav80x_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &bfin_eval_adav80x;
	const char *codec_name;

	switch (platform_get_device_id(pdev)->driver_data) {
	case BFIN_EVAL_ADAV801:
		codec_name = "spi0.1";
		break;
	case BFIN_EVAL_ADAV803:
		codec_name = "adav803.0-0034";
		break;
	default:
		return -EINVAL;
	}

	bfin_eval_adav80x_dais[0].codec_name = codec_name;

	card->dev = &pdev->dev;

	return snd_soc_register_card(&bfin_eval_adav80x);
}

static int __devexit bfin_eval_adav80x_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static const struct platform_device_id bfin_eval_adav80x_ids[] = {
	{ "bfin-eval-adav801", BFIN_EVAL_ADAV801 },
	{ "bfin-eval-adav803", BFIN_EVAL_ADAV803 },
	{ },
};
MODULE_DEVICE_TABLE(platform, bfin_eval_adav80x_ids);

static struct platform_driver bfin_eval_adav80x_driver = {
	.driver = {
		.name = "bfin-eval-adav80x",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = bfin_eval_adav80x_probe,
	.remove = __devexit_p(bfin_eval_adav80x_remove),
	.id_table = bfin_eval_adav80x_ids,
};

module_platform_driver(bfin_eval_adav80x_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ALSA SoC bfin adav80x driver");
MODULE_LICENSE("GPL");
