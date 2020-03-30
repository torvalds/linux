// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * linux/sound/soc/pxa/brownstone.c
 *
 * Copyright (C) 2011 Marvell International Ltd.
 */

#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>

#include "../codecs/wm8994.h"
#include "mmp-sspa.h"

static const struct snd_kcontrol_new brownstone_dapm_control[] = {
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
};

static const struct snd_soc_dapm_widget brownstone_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_HP("Headset Stereophone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Main Mic", NULL),
};

static const struct snd_soc_dapm_route brownstone_audio_map[] = {
	{"Ext Spk", NULL, "SPKOUTLP"},
	{"Ext Spk", NULL, "SPKOUTLN"},
	{"Ext Spk", NULL, "SPKOUTRP"},
	{"Ext Spk", NULL, "SPKOUTRN"},

	{"Headset Stereophone", NULL, "HPOUT1L"},
	{"Headset Stereophone", NULL, "HPOUT1R"},

	{"IN1RN", NULL, "Headset Mic"},

	{"DMIC1DAT", NULL, "MICBIAS1"},
	{"MICBIAS1", NULL, "Main Mic"},
};

static int brownstone_wm8994_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	int freq_out, sspa_mclk, sysclk;

	if (params_rate(params) > 11025) {
		freq_out  = params_rate(params) * 512;
		sysclk    = params_rate(params) * 256;
		sspa_mclk = params_rate(params) * 64;
	} else {
		freq_out  = params_rate(params) * 1024;
		sysclk    = params_rate(params) * 512;
		sspa_mclk = params_rate(params) * 64;
	}

	snd_soc_dai_set_sysclk(cpu_dai, MMP_SSPA_CLK_AUDIO, freq_out, 0);
	snd_soc_dai_set_pll(cpu_dai, MMP_SYSCLK, 0, freq_out, sysclk);
	snd_soc_dai_set_pll(cpu_dai, MMP_SSPA_CLK, 0, freq_out, sspa_mclk);

	/* set wm8994 sysclk */
	snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_MCLK1, sysclk, 0);

	return 0;
}

/* machine stream operations */
static const struct snd_soc_ops brownstone_ops = {
	.hw_params = brownstone_wm8994_hw_params,
};

SND_SOC_DAILINK_DEFS(wm8994,
	DAILINK_COMP_ARRAY(COMP_CPU("mmp-sspa-dai.0")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm8994-codec", "wm8994-aif1")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("mmp-pcm-audio")));

static struct snd_soc_dai_link brownstone_wm8994_dai[] = {
{
	.name		= "WM8994",
	.stream_name	= "WM8994 HiFi",
	.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBS_CFS,
	.ops		= &brownstone_ops,
	SND_SOC_DAILINK_REG(wm8994),
},
};

/* audio machine driver */
static struct snd_soc_card brownstone = {
	.name         = "brownstone",
	.owner        = THIS_MODULE,
	.dai_link     = brownstone_wm8994_dai,
	.num_links    = ARRAY_SIZE(brownstone_wm8994_dai),

	.controls = brownstone_dapm_control,
	.num_controls = ARRAY_SIZE(brownstone_dapm_control),
	.dapm_widgets = brownstone_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(brownstone_dapm_widgets),
	.dapm_routes = brownstone_audio_map,
	.num_dapm_routes = ARRAY_SIZE(brownstone_audio_map),
	.fully_routed = true,
};

static int brownstone_probe(struct platform_device *pdev)
{
	int ret;

	brownstone.dev = &pdev->dev;
	ret = devm_snd_soc_register_card(&pdev->dev, &brownstone);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
				ret);
	return ret;
}

static struct platform_driver mmp_driver = {
	.driver		= {
		.name	= "brownstone-audio",
		.pm     = &snd_soc_pm_ops,
	},
	.probe		= brownstone_probe,
};

module_platform_driver(mmp_driver);

MODULE_AUTHOR("Leo Yan <leoy@marvell.com>");
MODULE_DESCRIPTION("ALSA SoC Brownstone");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:brownstone-audio");
