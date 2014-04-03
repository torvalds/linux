/*
 * rk_rk3026.c  --  SoC audio for rockchip
 *
 * Driver for rockchip rk3026 audio
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "../codecs/rk3026_codec.h"
#include "card_info.h"
#include "rk_pcm.h"
#include "rk_i2s.h"

#if 0
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif

static const struct snd_soc_dapm_widget rk_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Jack", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
};

static const struct snd_soc_dapm_route rk_audio_map[]={

	/* Mic Jack --> MIC_IN*/
	{"Mic Bias", NULL, "Mic Jack"},
	{"MICP", NULL, "Mic Bias"},
	{"MICN", NULL, "Mic Bias"},

	// HP MIC
	{"Mic Bias", NULL, "Headset Jack"},

	{"Ext Spk", NULL, "HPOUTR"},
	{"Ext Spk", NULL, "HPOUTL"},

	{"Headphone Jack", NULL, "HPOUTR"},
	{"Headphone Jack", NULL, "HPOUTL"},
} ;

static const struct snd_kcontrol_new rk_controls[] = {
	SOC_DAPM_PIN_SWITCH("Mic Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Jack"),
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
};

static int rk3026_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	mutex_lock(&dapm->card->dapm_mutex);

	snd_soc_dapm_enable_pin(dapm, "Mic Jack");
	snd_soc_dapm_enable_pin(dapm, "Headset Jack");
	snd_soc_dapm_enable_pin(dapm, "Ext Spk");
	snd_soc_dapm_enable_pin(dapm, "Headphone Jack");

	mutex_unlock(&dapm->card->dapm_mutex);

	snd_soc_dapm_sync(dapm);

	return 0;
}

static int rk_hifi_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0, dai_fmt = rtd->dai_link->dai_fmt;
	int ret;

	DBG("Enter::%s----%d\n", __FUNCTION__, __LINE__);

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, dai_fmt);
	if (ret < 0) {
		printk("%s():failed to set the format for codec side\n", __FUNCTION__);
		return ret;
	}

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, dai_fmt);
	if (ret < 0) {
		printk("%s():failed to set the format for cpu side\n", __FUNCTION__);
		return ret;
	}

	switch(params_rate(params)) {
		case 8000:
		case 16000:
		case 24000:
		case 32000:
		case 48000:
			pll_out = 12288000;
			break;
		case 11025:
		case 22050:
		case 44100:
			pll_out = 11289600;
			break;
		default:
			DBG("Enter:%s, %d, Error rate=%d\n", __FUNCTION__, __LINE__, params_rate(params));
			return -EINVAL;
			break;
	}

	DBG("Enter:%s, %d, rate=%d\n", __FUNCTION__, __LINE__, params_rate(params));

	/*Set the system clk for codec*/
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, pll_out, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		DBG("rk_hifi_hw_params:failed to set the sysclk for codec side\n");
		return ret;
	}

	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, (pll_out/4)/params_rate(params)-1);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);

	DBG("Enter:%s, %d, pll_out/4/params_rate(params) = %d \n", __FUNCTION__, __LINE__, (pll_out/4)/params_rate(params));

	return 0;
}

static int rk_voice_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0, dai_fmt = rtd->dai_link->dai_fmt;
	int ret;

	DBG("Enter::%s----%d\n", __FUNCTION__, __LINE__);

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, dai_fmt);
	if (ret < 0) {
		printk("%s():failed to set the format for codec side\n", __FUNCTION__);
		return ret;
	}

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_DSP_A |
				SND_SOC_DAIFMT_IB_NF | SND_SOC_DAIFMT_CBS_CFS);

	switch(params_rate(params)) {
		case 8000:
		case 16000:
		case 24000:
		case 32000:
		case 48000:
			pll_out = 12288000;
			break;
		case 11025:
		case 22050:
		case 44100:
			pll_out = 11289600;
			break;
		default:
			DBG("Enter:%s, %d, Error rate=%d\n", __FUNCTION__, __LINE__, params_rate(params));
			return -EINVAL;
			break;
	}

	/*Set the system clk for codec*/
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, pll_out, SND_SOC_CLOCK_IN);

	if (ret < 0) {
		printk("rk_voice_hw_params:failed to set the sysclk for codec side\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);

	return 0;
}

static struct snd_soc_ops rk3026_hifi_ops = {
	.hw_params = rk_hifi_hw_params,
};

static struct snd_soc_ops rk3026_voice_ops = {
	.hw_params = rk_voice_hw_params,
};

static struct snd_soc_dai_link rk_dai[] = {
	{
		.name = "RK3026 I2S1",
		.stream_name = "RK3026 PCM",
		.codec_dai_name = "rk3026-hifi",
		.init = rk3026_init,
		.ops = &rk3026_hifi_ops,
	},
	{
		.name = "RK3026 I2S2",
		.stream_name = "RK3026 PCM",
		.codec_dai_name = "rk3026-voice",
		.ops = &rk3026_voice_ops,
	},
};

static struct snd_soc_card rockchip_rk3026_snd_card = {
	.name = "RK_RK3026",
	.dai_link = rk_dai,
	.num_links = 2,
	.controls = rk_controls,
	.num_controls = ARRAY_SIZE(rk_controls),
	.dapm_widgets    = rk_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rk_dapm_widgets),
	.dapm_routes    = rk_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rk_audio_map),
};

static int rockchip_rk3026_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_rk3026_snd_card;

	card->dev = &pdev->dev;

	ret = rockchip_of_get_sound_card_info(card);
	if (ret) {
		printk("%s() get sound card info failed:%d\n", __FUNCTION__, ret);
		return ret;
	}

	ret = snd_soc_register_card(card);
	if (ret)
		printk("%s() register card failed:%d\n", __FUNCTION__, ret);

	return ret;
}

static int rockchip_rk3026_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_rk3026_of_match[] = {
	{ .compatible = "rockchip-rk3026", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_rk3026_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_rk3026_audio_driver = {
	.driver         = {
		.name   = "rockchip-rk3026",
		.owner  = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(rockchip_rk3026_of_match),
	},
	.probe          = rockchip_rk3026_audio_probe,
	.remove         = rockchip_rk3026_audio_remove,
};

module_platform_driver(rockchip_rk3026_audio_driver);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");
