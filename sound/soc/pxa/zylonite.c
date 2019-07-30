// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * zylonite.c  --  SoC audio for Zylonite
 *
 * Copyright 2008 Wolfson Microelectronics PLC.
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "../codecs/wm9713.h"
#include "pxa-ssp.h"

/*
 * There is a physical switch SW15 on the board which changes the MCLK
 * for the WM9713 between the standard AC97 master clock and the
 * output of the CLK_POUT signal from the PXA.
 */
static int clk_pout;
module_param(clk_pout, int, 0);
MODULE_PARM_DESC(clk_pout, "Use CLK_POUT as WM9713 MCLK (SW15 on board).");

static struct clk *pout;

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

static int zylonite_wm9713_init(struct snd_soc_pcm_runtime *rtd)
{
	if (clk_pout)
		snd_soc_dai_set_pll(rtd->codec_dai, 0, 0,
				    clk_get_rate(pout), 0);

	return 0;
}

static int zylonite_voice_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int wm9713_div = 0;
	int ret = 0;
	int rate = params_rate(params);

	/* Only support ratios that we can generate neatly from the AC97
	 * based master clock - in particular, this excludes 44.1kHz.
	 * In most applications the voice DAC will be used for telephony
	 * data so multiples of 8kHz will be the common case.
	 */
	switch (rate) {
	case 8000:
		wm9713_div = 12;
		break;
	case 16000:
		wm9713_div = 6;
		break;
	case 48000:
		wm9713_div = 2;
		break;
	default:
		/* Don't support OSS emulation */
		return -EINVAL;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, PXA_SSP_CLK_AUDIO, 0, 1);
	if (ret < 0)
		return ret;

	if (clk_pout)
		ret = snd_soc_dai_set_clkdiv(codec_dai, WM9713_PCMCLK_PLL_DIV,
					     WM9713_PCMDIV(wm9713_div));
	else
		ret = snd_soc_dai_set_clkdiv(codec_dai, WM9713_PCMCLK_DIV,
					     WM9713_PCMDIV(wm9713_div));
	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_soc_ops zylonite_voice_ops = {
	.hw_params = zylonite_voice_hw_params,
};

static struct snd_soc_dai_link zylonite_dai[] = {
{
	.name = "AC97",
	.stream_name = "AC97 HiFi",
	.codec_name = "wm9713-codec",
	.platform_name = "pxa-pcm-audio",
	.cpu_dai_name = "pxa2xx-ac97",
	.codec_dai_name = "wm9713-hifi",
	.init = zylonite_wm9713_init,
},
{
	.name = "AC97 Aux",
	.stream_name = "AC97 Aux",
	.codec_name = "wm9713-codec",
	.platform_name = "pxa-pcm-audio",
	.cpu_dai_name = "pxa2xx-ac97-aux",
	.codec_dai_name = "wm9713-aux",
},
{
	.name = "WM9713 Voice",
	.stream_name = "WM9713 Voice",
	.codec_name = "wm9713-codec",
	.platform_name = "pxa-pcm-audio",
	.cpu_dai_name = "pxa-ssp-dai.2",
	.codec_dai_name = "wm9713-voice",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBS_CFS,
	.ops = &zylonite_voice_ops,
},
};

static int zylonite_probe(struct snd_soc_card *card)
{
	int ret;

	if (clk_pout) {
		pout = clk_get(NULL, "CLK_POUT");
		if (IS_ERR(pout)) {
			dev_err(card->dev, "Unable to obtain CLK_POUT: %ld\n",
				PTR_ERR(pout));
			return PTR_ERR(pout);
		}

		ret = clk_enable(pout);
		if (ret != 0) {
			dev_err(card->dev, "Unable to enable CLK_POUT: %d\n",
				ret);
			clk_put(pout);
			return ret;
		}

		dev_dbg(card->dev, "MCLK enabled at %luHz\n",
			clk_get_rate(pout));
	}

	return 0;
}

static int zylonite_remove(struct snd_soc_card *card)
{
	if (clk_pout) {
		clk_disable(pout);
		clk_put(pout);
	}

	return 0;
}

static int zylonite_suspend_post(struct snd_soc_card *card)
{
	if (clk_pout)
		clk_disable(pout);

	return 0;
}

static int zylonite_resume_pre(struct snd_soc_card *card)
{
	int ret = 0;

	if (clk_pout) {
		ret = clk_enable(pout);
		if (ret != 0)
			dev_err(card->dev, "Unable to enable CLK_POUT: %d\n",
				ret);
	}

	return ret;
}

static struct snd_soc_card zylonite = {
	.name = "Zylonite",
	.owner = THIS_MODULE,
	.probe = &zylonite_probe,
	.remove = &zylonite_remove,
	.suspend_post = &zylonite_suspend_post,
	.resume_pre = &zylonite_resume_pre,
	.dai_link = zylonite_dai,
	.num_links = ARRAY_SIZE(zylonite_dai),

	.dapm_widgets = zylonite_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(zylonite_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static struct platform_device *zylonite_snd_ac97_device;

static int __init zylonite_init(void)
{
	int ret;

	zylonite_snd_ac97_device = platform_device_alloc("soc-audio", -1);
	if (!zylonite_snd_ac97_device)
		return -ENOMEM;

	platform_set_drvdata(zylonite_snd_ac97_device, &zylonite);

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
