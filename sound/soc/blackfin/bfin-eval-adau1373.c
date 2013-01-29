/*
 * Machine driver for EVAL-ADAU1373 on Analog Devices bfin
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

#include "../codecs/adau1373.h"

static const struct snd_soc_dapm_widget bfin_eval_adau1373_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Line In1", NULL),
	SND_SOC_DAPM_LINE("Line In2", NULL),
	SND_SOC_DAPM_LINE("Line In3", NULL),
	SND_SOC_DAPM_LINE("Line In4", NULL),

	SND_SOC_DAPM_LINE("Line Out1", NULL),
	SND_SOC_DAPM_LINE("Line Out2", NULL),
	SND_SOC_DAPM_LINE("Stereo Out", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_HP("Earpiece", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static const struct snd_soc_dapm_route bfin_eval_adau1373_dapm_routes[] = {
	{ "AIN1L", NULL, "Line In1" },
	{ "AIN1R", NULL, "Line In1" },
	{ "AIN2L", NULL, "Line In2" },
	{ "AIN2R", NULL, "Line In2" },
	{ "AIN3L", NULL, "Line In3" },
	{ "AIN3R", NULL, "Line In3" },
	{ "AIN4L", NULL, "Line In4" },
	{ "AIN4R", NULL, "Line In4" },

	/* MICBIAS can be connected via a jumper to the line-in jack, since w
	   don't know which one is going to be used, just power both. */
	{ "Line In1", NULL, "MICBIAS1" },
	{ "Line In2", NULL, "MICBIAS1" },
	{ "Line In3", NULL, "MICBIAS1" },
	{ "Line In4", NULL, "MICBIAS1" },
	{ "Line In1", NULL, "MICBIAS2" },
	{ "Line In2", NULL, "MICBIAS2" },
	{ "Line In3", NULL, "MICBIAS2" },
	{ "Line In4", NULL, "MICBIAS2" },

	{ "Line Out1", NULL, "LOUT1L" },
	{ "Line Out1", NULL, "LOUT1R" },
	{ "Line Out2", NULL, "LOUT2L" },
	{ "Line Out2", NULL, "LOUT2R" },
	{ "Headphone", NULL, "HPL" },
	{ "Headphone", NULL, "HPR" },
	{ "Earpiece", NULL, "EP" },
	{ "Speaker", NULL, "SPKL" },
	{ "Stereo Out", NULL, "SPKR" },
};

static int bfin_eval_adau1373_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;
	int pll_rate;

	switch (params_rate(params)) {
	case 48000:
	case 8000:
	case 12000:
	case 16000:
	case 24000:
	case 32000:
		pll_rate = 48000 * 1024;
		break;
	case 44100:
	case 7350:
	case 11025:
	case 14700:
	case 22050:
	case 29400:
		pll_rate = 44100 * 1024;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_dai_set_pll(codec_dai, ADAU1373_PLL1,
			ADAU1373_PLL_SRC_MCLK1, 12288000, pll_rate);
	if (ret)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, ADAU1373_CLK_SRC_PLL1, pll_rate,
			SND_SOC_CLOCK_IN);

	return ret;
}

static int bfin_eval_adau1373_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	unsigned int pll_rate = 48000 * 1024;
	int ret;

	ret = snd_soc_dai_set_pll(codec_dai, ADAU1373_PLL1,
			ADAU1373_PLL_SRC_MCLK1, 12288000, pll_rate);
	if (ret)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, ADAU1373_CLK_SRC_PLL1, pll_rate,
			SND_SOC_CLOCK_IN);

	return ret;
}
static struct snd_soc_ops bfin_eval_adau1373_ops = {
	.hw_params = bfin_eval_adau1373_hw_params,
};

static struct snd_soc_dai_link bfin_eval_adau1373_dai = {
	.name = "adau1373",
	.stream_name = "adau1373",
	.cpu_dai_name = "bfin-i2s.0",
	.codec_dai_name = "adau1373-aif1",
	.platform_name = "bfin-i2s-pcm-audio",
	.codec_name = "adau1373.0-001a",
	.ops = &bfin_eval_adau1373_ops,
	.init = bfin_eval_adau1373_codec_init,
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBM_CFM,
};

static struct snd_soc_card bfin_eval_adau1373 = {
	.name = "bfin-eval-adau1373",
	.owner = THIS_MODULE,
	.dai_link = &bfin_eval_adau1373_dai,
	.num_links = 1,

	.dapm_widgets		= bfin_eval_adau1373_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(bfin_eval_adau1373_dapm_widgets),
	.dapm_routes		= bfin_eval_adau1373_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(bfin_eval_adau1373_dapm_routes),
};

static int bfin_eval_adau1373_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &bfin_eval_adau1373;

	card->dev = &pdev->dev;

	return snd_soc_register_card(&bfin_eval_adau1373);
}

static int bfin_eval_adau1373_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static struct platform_driver bfin_eval_adau1373_driver = {
	.driver = {
		.name = "bfin-eval-adau1373",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = bfin_eval_adau1373_probe,
	.remove = bfin_eval_adau1373_remove,
};

module_platform_driver(bfin_eval_adau1373_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ALSA SoC bfin adau1373 driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:bfin-eval-adau1373");
