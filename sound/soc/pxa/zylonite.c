/*
 * zylonite.c  --  SoC audio for Zylonite
 *
 * Copyright 2008 Wolfson Microelectronics PLC.
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "../codecs/wm9713.h"
#include "pxa2xx-pcm.h"
#include "pxa2xx-ac97.h"
#include "pxa-ssp.h"

static struct snd_soc_card zylonite;

static const struct snd_soc_dapm_widget zylonite_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Microphone", NULL),
	SND_SOC_DAPM_MIC("Handset Microphone", NULL),
	SND_SOC_DAPM_SPK("Multiactor", NULL),
	SND_SOC_DAPM_SPK("Headset Earpiece", NULL),
};

/* Currently supported audio map */
static const struct snd_soc_dapm_route audio_map[] = {

	/* Headphone output connected to HPL/HPR */
	{ "Headphone", NULL,  "HPL" },
	{ "Headphone", NULL,  "HPR" },

	/* On-board earpiece */
	{ "Headset Earpiece", NULL, "OUT3" },

	/* Headphone mic */
	{ "MIC2A", NULL, "Mic Bias" },
	{ "Mic Bias", NULL, "Headset Microphone" },

	/* On-board mic */
	{ "MIC1", NULL, "Mic Bias" },
	{ "Mic Bias", NULL, "Handset Microphone" },

	/* Multiactor differentially connected over SPKL/SPKR */
	{ "Multiactor", NULL, "SPKL" },
	{ "Multiactor", NULL, "SPKR" },
};

static int zylonite_wm9713_init(struct snd_soc_codec *codec)
{
	/* Currently we only support use of the AC97 clock here.  If
	 * CLK_POUT is selected by SW15 then the clock API will need
	 * to be used to request and enable it here.
	 */

	snd_soc_dapm_new_controls(codec, zylonite_dapm_widgets,
				  ARRAY_SIZE(zylonite_dapm_widgets));

	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	/* Static setup for now */
	snd_soc_dapm_enable_pin(codec, "Headphone");
	snd_soc_dapm_enable_pin(codec, "Headset Earpiece");

	snd_soc_dapm_sync(codec);
	return 0;
}

static int zylonite_voice_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	unsigned int pll_out = 0;
	unsigned int acds = 0;
	unsigned int wm9713_div = 0;
	int ret = 0;

	switch (params_rate(params)) {
	case 8000:
		wm9713_div = 12;
		pll_out = 2048000;
		break;
	case 16000:
		wm9713_div = 6;
		pll_out = 4096000;
		break;
	case 48000:
	default:
		wm9713_div = 2;
		pll_out = 12288000;
		acds = 1;
		break;
	}

	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_tdm_slot(cpu_dai,
				       params_channels(params),
				       params_channels(params));
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_pll(cpu_dai, 0, 0, pll_out);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, PXA_SSP_AUDIO_DIV_ACDS, acds);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(cpu_dai, PXA_SSP_CLK_AUDIO, 0, 1);
	if (ret < 0)
		return ret;

	/* Note that if the PLL is in use the WM9713_PCMCLK_PLL_DIV needs
	 * to be set instead.
	 */
	ret = snd_soc_dai_set_clkdiv(codec_dai, WM9713_PCMCLK_DIV,
				     WM9713_PCMDIV(wm9713_div));
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops zylonite_voice_ops = {
	.hw_params = zylonite_voice_hw_params,
};

static struct snd_soc_dai_link zylonite_dai[] = {
{
	.name = "AC97",
	.stream_name = "AC97 HiFi",
	.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_HIFI],
	.codec_dai = &wm9713_dai[WM9713_DAI_AC97_HIFI],
	.init = zylonite_wm9713_init,
},
{
	.name = "AC97 Aux",
	.stream_name = "AC97 Aux",
	.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_AUX],
	.codec_dai = &wm9713_dai[WM9713_DAI_AC97_AUX],
},
{
	.name = "WM9713 Voice",
	.stream_name = "WM9713 Voice",
	.cpu_dai = &pxa_ssp_dai[PXA_DAI_SSP3],
	.codec_dai = &wm9713_dai[WM9713_DAI_PCM_VOICE],
	.ops = &zylonite_voice_ops,
},
};

static struct snd_soc_card zylonite = {
	.name = "Zylonite",
	.platform = &pxa2xx_soc_platform,
	.dai_link = zylonite_dai,
	.num_links = ARRAY_SIZE(zylonite_dai),
};

static struct snd_soc_device zylonite_snd_ac97_devdata = {
	.card = &zylonite,
	.codec_dev = &soc_codec_dev_wm9713,
};

static struct platform_device *zylonite_snd_ac97_device;

static int __init zylonite_init(void)
{
	int ret;

	zylonite_snd_ac97_device = platform_device_alloc("soc-audio", -1);
	if (!zylonite_snd_ac97_device)
		return -ENOMEM;

	platform_set_drvdata(zylonite_snd_ac97_device,
			     &zylonite_snd_ac97_devdata);
	zylonite_snd_ac97_devdata.dev = &zylonite_snd_ac97_device->dev;

	ret = platform_device_add(zylonite_snd_ac97_device);
	if (ret != 0)
		platform_device_put(zylonite_snd_ac97_device);

	return ret;
}

static void __exit zylonite_exit(void)
{
	platform_device_unregister(zylonite_snd_ac97_device);
}

module_init(zylonite_init);
module_exit(zylonite_exit);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("ALSA SoC WM9713 Zylonite");
MODULE_LICENSE("GPL");
