/*
 *  sound/soc/samsung/smdk_wm8580pcm.c
 *
 *  Copyright (c) 2011 Samsung Electronics Co. Ltd
 *
 *  This program is free software; you can redistribute  it and/or  modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/pcm.h>

#include <asm/mach-types.h>

#include "../codecs/wm8580.h"
#include "dma.h"
#include "pcm.h"

/*
 * Board Settings:
 *  o '1' means 'ON'
 *  o '0' means 'OFF'
 *  o 'X' means 'Don't care'
 *
 * SMDK6410 Base B/D: CFG1-0000, CFG2-1111
 * SMDKC110, SMDKV210: CFGB11-100100, CFGB12-0000
 */

#define SMDK_WM8580_EXT_OSC 12000000
#define SMDK_WM8580_EXT_MCLK 4096000
#define SMDK_WM8580_EXT_VOICE 2048000

static unsigned long mclk_freq;
static unsigned long xtal_freq;

/*
 * If MCLK clock directly gets from XTAL, we don't have to use PLL
 * to make MCLK, but if XTAL clock source connects with other codec
 * pin (like XTI), we should have to set codec's PLL to make MCLK.
 * Because Samsung SoC does not support pcmcdclk output like I2S.
 */

static int smdk_wm8580_pcm_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int rfs, ret;

	switch (params_rate(params)) {
	case 8000:
		break;
	default:
		printk(KERN_ERR "%s:%d Sampling Rate %u not supported!\n",
		__func__, __LINE__, params_rate(params));
		return -EINVAL;
	}

	rfs = mclk_freq / params_rate(params) / 2;

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

	if (mclk_freq == xtal_freq) {
		ret = snd_soc_dai_set_sysclk(codec_dai, WM8580_CLKSRC_MCLK,
						mclk_freq, SND_SOC_CLOCK_IN);
		if (ret < 0)
			return ret;

		ret = snd_soc_dai_set_clkdiv(codec_dai, WM8580_MCLK,
						WM8580_CLKSRC_MCLK);
		if (ret < 0)
			return ret;
	} else {
		ret = snd_soc_dai_set_sysclk(codec_dai, WM8580_CLKSRC_PLLA,
						mclk_freq, SND_SOC_CLOCK_IN);
		if (ret < 0)
			return ret;

		ret = snd_soc_dai_set_clkdiv(codec_dai, WM8580_MCLK,
						WM8580_CLKSRC_PLLA);
		if (ret < 0)
			return ret;

		ret = snd_soc_dai_set_pll(codec_dai, WM8580_PLLA, 0,
						xtal_freq, mclk_freq);
		if (ret < 0)
			return ret;
	}

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

static struct snd_soc_ops smdk_wm8580_pcm_ops = {
	.hw_params = smdk_wm8580_pcm_hw_params,
};

static struct snd_soc_dai_link smdk_dai[] = {
	{
		.name = "WM8580 PAIF PCM RX",
		.stream_name = "Playback",
		.cpu_dai_name = "samsung-pcm.0",
		.codec_dai_name = "wm8580-hifi-playback",
		.platform_name = "samsung-audio",
		.codec_name = "wm8580.0-001b",
		.ops = &smdk_wm8580_pcm_ops,
	}, {
		.name = "WM8580 PAIF PCM TX",
		.stream_name = "Capture",
		.cpu_dai_name = "samsung-pcm.0",
		.codec_dai_name = "wm8580-hifi-capture",
		.platform_name = "samsung-pcm.0",
		.codec_name = "wm8580.0-001b",
		.ops = &smdk_wm8580_pcm_ops,
	},
};

static struct snd_soc_card smdk_pcm = {
	.name = "SMDK-PCM",
	.owner = THIS_MODULE,
	.dai_link = smdk_dai,
	.num_links = 2,
};

/*
 * After SMDKC110 Base Board's Rev is '0.1', 12MHz External OSC(X1)
 * is absent (or not connected), so we connect EXT_VOICE_CLK(OSC4),
 * 2.0484Mhz, directly with MCLK both Codec and SoC.
 */
static int snd_smdk_probe(struct platform_device *pdev)
{
	int ret = 0;

	xtal_freq = SMDK_WM8580_EXT_OSC;
	mclk_freq = SMDK_WM8580_EXT_MCLK;

	if (machine_is_smdkc110() || machine_is_smdkv210())
		xtal_freq = mclk_freq = SMDK_WM8580_EXT_VOICE;

	smdk_pcm.dev = &pdev->dev;
	ret = devm_snd_soc_register_card(&pdev->dev, &smdk_pcm);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card failed %d\n", ret);

	return ret;
}

static struct platform_driver snd_smdk_driver = {
	.driver = {
		.name = "samsung-smdk-pcm",
	},
	.probe = snd_smdk_probe,
};

module_platform_driver(snd_smdk_driver);

MODULE_AUTHOR("Sangbeom Kim, <sbkim73@samsung.com>");
MODULE_DESCRIPTION("ALSA SoC SMDK WM8580 for PCM");
MODULE_LICENSE("GPL");
