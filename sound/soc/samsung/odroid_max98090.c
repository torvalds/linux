/*
 *  odroid_max98090.c
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include <mach/regs-clock.h>

#include "i2s.h"
#include "s3c-i2s-v2.h"
#include "../codecs/max98090.h"

static struct platform_device *odroid_snd_device;

#ifdef CONFIG_SND_SAMSUNG_I2S_MASTER
static int set_epll_rate(unsigned long rate)
{
	struct clk *fout_epll;

	fout_epll = clk_get(NULL, "fout_epll");
	if (IS_ERR(fout_epll)) {
		printk(KERN_ERR "%s: failed to get fout_epll\n", __func__);
		return PTR_ERR(fout_epll);
	}

	if (rate == clk_get_rate(fout_epll))
		goto out;

	clk_set_rate(fout_epll, rate);
out:
	clk_put(fout_epll);

	return 0;
}
#endif /* CONFIG_SND_SAMSUNG_I2S_MASTER */

static int odroid_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int bfs, psr, rfs, ret;
	unsigned long rclk;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_U24:
	case SNDRV_PCM_FORMAT_S24:
		bfs = 48;
		break;
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_S16_LE:
		bfs = 32;
		break;
	default:
		return -EINVAL;
	}

	switch (params_rate(params)) {
	case 16000:
	case 22050:
	case 24000:
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
		if (bfs == 48)
			rfs = 384;
		else
			rfs = 256;
		break;
	case 64000:
		rfs = 384;
		break;
	case 8000:
	case 11025:
	case 12000:
		if (bfs == 48)
			rfs = 768;
		else
			rfs = 512;
		break;
	default:
		return -EINVAL;
	}

	rclk = params_rate(params) * rfs;

	switch (rclk) {
	case 4096000:
	case 5644800:
	case 6144000:
	case 8467200:
	case 9216000:
		psr = 8;
		break;
	case 8192000:
	case 11289600:
	case 12288000:
	case 16934400:
	case 18432000:
		psr = 4;
		break;
	case 22579200:
	case 24576000:
	case 33868800:
	case 36864000:
		psr = 2;
		break;
	case 67737600:
	case 73728000:
		psr = 1;
		break;
	default:
		printk("Not yet supported!\n");
		return -EINVAL;
	}

	set_epll_rate(rclk * psr);

	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S
					| SND_SOC_DAIFMT_NB_NF
					| SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S
					| SND_SOC_DAIFMT_NB_NF
					| SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, 0,
					rclk, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_CDCLK,
					0, SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, SAMSUNG_I2S_DIV_BCLK, bfs);
	if (ret < 0)
		return ret;

	return 0;
}

//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
/*
 * ODROID max98090 DAI operations.
 */
static struct snd_soc_ops odroid_ops = {
	.hw_params = odroid_hw_params,
};

static int max98090_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_sync(dapm);
	return 0;
}

static struct snd_soc_dai_link odroid_dai[] = {
	{ /* Primary DAI i/f */
		.name = "MAX98090 AIF1",
		.stream_name = "Playback",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "max98090-aif1",
#if defined(CONFIG_SND_SAMSUNG_NORMAL) || defined(CONFIG_SND_SAMSUNG_RP)
		.platform_name = "samsung-audio",
#else
		.platform_name = "samsung-audio-idma",
#endif
		.codec_name = "max98090.1-0010",
		.init = max98090_init,
		.ops = &odroid_ops,
	},
	{ /* Sec_Fifo DAI i/f */
		.name = "MAX98090 AIF2",
		.stream_name = "Capture",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "max98090-aif1",
#if defined(CONFIG_SND_SAMSUNG_NORMAL) || defined(CONFIG_SND_SAMSUNG_RP)
		.platform_name = "samsung-audio",
#else
		.platform_name = "samsung-audio-idma",
#endif
		.codec_name = "max98090.1-0010",
		.init = max98090_init,
		.ops = &odroid_ops,
	},
};

static struct snd_soc_card odroid = {
	.name = "Ooroid-max98090",
	.dai_link = odroid_dai,

	/* If you want to use sec_fifo device,
	 * changes the num_link = 2 or ARRAY_SIZE(odroid_dai). */
	.num_links = ARRAY_SIZE(odroid_dai),
};

static int __init odroid_audio_init(void)
{
	int ret;
	odroid_snd_device = platform_device_alloc("soc-audio", -1);
	if (!odroid_snd_device)
		return -ENOMEM;

	platform_set_drvdata(odroid_snd_device, &odroid);

	ret = platform_device_add(odroid_snd_device);
	if (ret)
		platform_device_put(odroid_snd_device);

	return ret;
}
module_init(odroid_audio_init);

static void __exit odroid_audio_exit(void)
{
	platform_device_unregister(odroid_snd_device);
}
module_exit(odroid_audio_exit);

MODULE_DESCRIPTION("ALSA SoC ODROID max98090");
MODULE_LICENSE("GPL");
