/*
 * Speyside audio support
 *
 * Copyright 2011 Wolfson Microelectronics
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "../codecs/wm8915.h"

static int speyside_set_bias_level(struct snd_soc_card *card,
				   enum snd_soc_bias_level level)
{
	struct snd_soc_dai *codec_dai = card->rtd[0].codec_dai;
	int ret;

	switch (level) {
	case SND_SOC_BIAS_STANDBY:
		ret = snd_soc_dai_set_sysclk(codec_dai, WM8915_SYSCLK_MCLK1,
					     32768, SND_SOC_CLOCK_IN);
		if (ret < 0)
			return ret;

		ret = snd_soc_dai_set_pll(codec_dai, WM8915_FLL_MCLK1,
					  0, 0, 0);
		if (ret < 0) {
			pr_err("Failed to stop FLL\n");
			return ret;
		}

	default:
		break;
	}

	return 0;
}

static int speyside_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S
					 | SND_SOC_DAIFMT_NB_NF
					 | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S
					 | SND_SOC_DAIFMT_NB_NF
					 | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_pll(codec_dai, 0, WM8915_FLL_MCLK1,
				  32768, 256 * 48000);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8915_SYSCLK_FLL,
				     256 * 48000, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops speyside_ops = {
	.hw_params = speyside_hw_params,
};

static int speyside_wm8915_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *dai = rtd->codec_dai;

	return snd_soc_dai_set_sysclk(dai, WM8915_SYSCLK_MCLK1, 32768, 0);
}

static struct snd_soc_dai_link speyside_dai[] = {
	{
		.name = "CPU",
		.stream_name = "CPU",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "wm8915-aif1",
		.platform_name = "samsung-audio",
		.codec_name = "wm8915.1-001a",
		.init = speyside_wm8915_init,
		.ops = &speyside_ops,
	},
};

static struct snd_soc_dapm_widget widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),

	SND_SOC_DAPM_SPK("Main Speaker", NULL),

	SND_SOC_DAPM_MIC("Main AMIC", NULL),
	SND_SOC_DAPM_MIC("Main DMIC", NULL),
};

static struct snd_soc_dapm_route audio_paths[] = {
	{ "IN1LP", NULL, "MICB2" },
	{ "MICB2", NULL, "Main AMIC" },

	{ "DMIC1DAT", NULL, "MICB1" },
	{ "DMIC2DAT", NULL, "MICB1" },
	{ "MICB1", NULL, "Main DMIC" },

	{ "Headphone", NULL, "HPOUT1L" },
	{ "Headphone", NULL, "HPOUT1R" },

	{ "Main Speaker", NULL, "SPKDAT" },
};

static struct snd_soc_card speyside = {
	.name = "Speyside",
	.dai_link = speyside_dai,
	.num_links = ARRAY_SIZE(speyside_dai),

	.set_bias_level = speyside_set_bias_level,

	.dapm_widgets = widgets,
	.num_dapm_widgets = ARRAY_SIZE(widgets),
	.dapm_routes = audio_paths,
	.num_dapm_routes = ARRAY_SIZE(audio_paths),
};

static __devinit int speyside_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &speyside;
	int ret;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int __devexit speyside_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static struct platform_driver speyside_driver = {
	.driver = {
		.name = "speyside",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = speyside_probe,
	.remove = __devexit_p(speyside_remove),
};

static int __init speyside_audio_init(void)
{
	return platform_driver_register(&speyside_driver);
}
module_init(speyside_audio_init);

static void __exit speyside_audio_exit(void)
{
	platform_driver_unregister(&speyside_driver);
}
module_exit(speyside_audio_exit);

MODULE_DESCRIPTION("Speyside audio support");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:speyside");
