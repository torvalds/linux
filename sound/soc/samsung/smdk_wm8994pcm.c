/*
 *  sound/soc/samsung/smdk_wm8994pcm.c
 *
 *  Copyright (c) 2011 Samsung Electronics Co., Ltd
 *		http://www.samsung.com
 *
 *  This program is free software; you can redistribute  it and/or  modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "../codecs/wm8994.h"
#include "dma.h"
#include "pcm.h"

/*
 * Board Settings:
 *  o '1' means 'ON'
 *  o '0' means 'OFF'
 *  o 'X' means 'Don't care'
 *
 * SMDKC210, SMDKV310: CFG3- 1001, CFG5-1000, CFG7-111111
 */

/*
 * Configure audio route as :-
 * $ amixer sset 'DAC1' on,on
 * $ amixer sset 'Right Headphone Mux' 'DAC'
 * $ amixer sset 'Left Headphone Mux' 'DAC'
 * $ amixer sset 'DAC1R Mixer AIF1.1' on
 * $ amixer sset 'DAC1L Mixer AIF1.1' on
 * $ amixer sset 'IN2L' on
 * $ amixer sset 'IN2L PGA IN2LN' on
 * $ amixer sset 'MIXINL IN2L' on
 * $ amixer sset 'AIF1ADC1L Mixer ADC/DMIC' on
 * $ amixer sset 'IN2R' on
 * $ amixer sset 'IN2R PGA IN2RN' on
 * $ amixer sset 'MIXINR IN2R' on
 * $ amixer sset 'AIF1ADC1R Mixer ADC/DMIC' on
 */

/* SMDK has a 16.9344MHZ crystal attached to WM8994 */
#define SMDK_WM8994_FREQ 16934400

static int smdk_wm8994_pcm_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned long mclk_freq;
	int rfs, ret;

	switch(params_rate(params)) {
	case 8000:
		rfs = 512;
		break;
	default:
		dev_err(cpu_dai->dev, "%s:%d Sampling Rate %u not supported!\n",
		__func__, __LINE__, params_rate(params));
		return -EINVAL;
	}

	mclk_freq = params_rate(params) * rfs;

	/* Set the codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_DSP_B
				| SND_SOC_DAIFMT_IB_NF
				| SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* Set the cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_DSP_B
				| SND_SOC_DAIFMT_IB_NF
				| SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_FLL1,
					mclk_freq, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_pll(codec_dai, WM8994_FLL1, WM8994_FLL_SRC_MCLK1,
					SMDK_WM8994_FREQ, mclk_freq);
	if (ret < 0)
		return ret;

	/* Set PCM source clock on CPU */
	ret = snd_soc_dai_set_sysclk(cpu_dai, S3C_PCM_CLKSRC_MUX,
					mclk_freq, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* Set SCLK_DIV for making bclk */
	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C_PCM_SCLK_PER_FS, rfs);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops smdk_wm8994_pcm_ops = {
	.hw_params = smdk_wm8994_pcm_hw_params,
};

static struct snd_soc_dai_link smdk_dai[] = {
	{
		.name = "WM8994 PAIF PCM",
		.stream_name = "Primary PCM",
		.cpu_dai_name = "samsung-pcm.0",
		.codec_dai_name = "wm8994-aif1",
		.platform_name = "samsung-audio",
		.codec_name = "wm8994-codec",
		.ops = &smdk_wm8994_pcm_ops,
	},
};

static struct snd_soc_card smdk_pcm = {
	.name = "SMDK-PCM",
	.dai_link = smdk_dai,
	.num_links = 1,
};

static int __devinit snd_smdk_probe(struct platform_device *pdev)
{
	int ret = 0;

	smdk_pcm.dev = &pdev->dev;
	ret = snd_soc_register_card(&smdk_pcm);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed %d\n", ret);
		return ret;
	}

	return 0;
}

static int __devexit snd_smdk_remove(struct platform_device *pdev)
{
	snd_soc_unregister_card(&smdk_pcm);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_driver snd_smdk_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "samsung-smdk-pcm",
	},
	.probe = snd_smdk_probe,
	.remove = __devexit_p(snd_smdk_remove),
};

static int __init smdk_audio_init(void)
{
	return platform_driver_register(&snd_smdk_driver);
}

module_init(smdk_audio_init);

static void __exit smdk_audio_exit(void)
{
	platform_driver_unregister(&snd_smdk_driver);
}

module_exit(smdk_audio_exit);

MODULE_AUTHOR("Sangbeom Kim, <sbkim73@samsung.com>");
MODULE_DESCRIPTION("ALSA SoC SMDK WM8994 for PCM");
MODULE_LICENSE("GPL");
