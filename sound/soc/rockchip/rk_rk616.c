/*
 * rk_rk616.c  --  SoC audio for rockchip
 *
 * Driver for rockchip rk616 audio
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

#include "../codecs/rk616_codec.h"
#include "card_info.h"
#include "rk_pcm.h"
#include "rk_i2s.h"

#if 1
#define	DBG(x...)	pr_info(x)
#else
#define	DBG(x...)
#endif

static bool get_hdmi_state(void)
{
#ifdef CONFIG_HDMI
	if (hdmi_is_insert())
		return true;
#endif

#ifdef CONFIG_HDMI_RK30
	/*HDMI_HPD_ACTIVED*/
	if (hdmi_get_hotplug() == 2)
		return true;
#endif

			return false;
}

static const struct snd_soc_dapm_widget rk_rk616_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Jack", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
};

static const struct snd_soc_dapm_route rk_rk616_audio_map[] = {

	/* Mic Jack --> MIC_IN*/
	{"Mic1 Bias", NULL, "Mic Jack"},
	{"MIC1P", NULL, "Mic1 Bias"},
	{"MIC1N", NULL, "Mic1 Bias"},

	/* HP MIC */
	{"Mic2 Bias", NULL, "Headset Jack"},
	{"MIC2P", NULL, "Mic2 Bias"},
	{"MIC2N", NULL, "Mic2 Bias"},

	{"Ext Spk", NULL, "SPKOUTR"},
	{"Ext Spk", NULL, "SPKOUTL"},

	{"Headphone Jack", NULL, "HPOUTR"},
	{"Headphone Jack", NULL, "HPOUTL"},
};

static const struct snd_kcontrol_new rk_rk616_controls[] = {
	SOC_DAPM_PIN_SWITCH("Mic Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Jack"),
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
};

static int rk616_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	/*
	* if is for mid that using tiny alsa,
	* it don't need this controls and route, so return.
	*/
	if (rk616_get_for_mid())
		return 0;

	DBG("%s() %d\n", __func__, __LINE__);

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
	unsigned int pll_out = 0, div = 4, dai_fmt = rtd->dai_link->dai_fmt;
	int ret;

	DBG("%s() %d\n", __func__, __LINE__);

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, dai_fmt);
	if (ret < 0) {
		pr_err("%s():failed to set the format for codec side\n",
			__func__);
		return ret;
	}

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, dai_fmt);
	if (ret < 0) {
		pr_err("%s():failed to set the format for cpu side\n",
			__func__);
		return ret;
	}

	switch (params_rate(params)) {
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
	case 8000:
		pll_out = 12000000;
		div = 6;
		break;
	default:
		DBG("Enter:%s, %d, Error rate=%d\n",
			__func__, __LINE__,
			params_rate(params));
		return -EINVAL;
		break;
	}

	DBG("Enter:%s, %d, rate=%d\n",
		__func__, __LINE__,
		params_rate(params));

	#if defined(CONFIG_RK616_USE_MCLK_12M)
	/* MCLK must be 12M when RK616 HDMI is in */
	if (get_hdmi_state() && pll_out != 12000000) {
		DBG("%s : HDMI is in, don't set sys clk %u\n",
			__func__, pll_out);
		goto __setdiv;
	}
	#endif

	/* Set the system clk for codec
	   mclk will be setted in set_sysclk of codec_dai*/
	ret = snd_soc_dai_set_sysclk(codec_dai, 0,
		pll_out, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		DBG("%s : failed to set the sysclk for codec side\n",
			__func__);
		return ret;
	}
#if defined(CONFIG_RK616_USE_MCLK_12M)
__setdiv:
#endif
	snd_soc_dai_set_clkdiv(cpu_dai,
		ROCKCHIP_DIV_BCLK,
		(pll_out / div)/params_rate(params)-1);
	snd_soc_dai_set_clkdiv(cpu_dai,
		ROCKCHIP_DIV_MCLK, div - 1);

	DBG("Enter:%s, %d, pll_out/div/params_rate(params) = %d\n",
		__func__, __LINE__, (pll_out/div)/params_rate(params));

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

	DBG("%s() %d\n", __func__, __LINE__);

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, dai_fmt);
	if (ret < 0) {
		pr_err("rk_voice_hw_params:failed to set the format for codec side\n");
		return ret;
	}

	switch (params_rate(params)) {
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
		DBG("Enter:%s, %d, Error rate=%d\n",
			__func__, __LINE__,
			params_rate(params));
		return -EINVAL;
		break;
	}

	/* MCLK must be 12M when RK616 HDMI is in */
	#if defined(CONFIG_RK616_USE_MCLK_12M)
	if (get_hdmi_state() && pll_out != 12000000) {
		DBG("%s : HDMI is in, set mclk to 12Mn", __func__);
		pll_out = 12000000;
	}
	#endif

	/*Set the system clk for codec*/
	ret = snd_soc_dai_set_sysclk(codec_dai, 0,
		pll_out, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		pr_err("rk_voice_hw_params:failed to set the sysclk for codec side\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	if (ret < 0) {
		pr_err("rk_voice_hw_params:failed to set the sysclk for cpu side\n");
		return ret;
	}

	return 0;
}

static struct snd_soc_ops rk616_hifi_ops = {
	.hw_params = rk_hifi_hw_params,
};

static struct snd_soc_ops rk616_voice_ops = {
	.hw_params = rk_voice_hw_params,
};

static struct snd_soc_dai_link rk_dai[] = {
	{
		.name = "RK616 I2S1",
		.stream_name = "RK616 PCM",
		.codec_dai_name = "rk616-hifi",
		.init = rk616_init,
		.ops = &rk616_hifi_ops,
	},
	{
		.name = "RK616 I2S2",
		.stream_name = "RK616 PCM",
		.codec_dai_name = "rk616-voice",
		.ops = &rk616_voice_ops,
	},
};

static struct snd_soc_card rockchip_rk616_snd_card = {
	.name = "RK_RK616",
	.dai_link = rk_dai,
	.num_links = 2,
	.controls = rk_rk616_controls,
	.num_controls = ARRAY_SIZE(rk_rk616_controls),
	.dapm_widgets    = rk_rk616_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rk_rk616_dapm_widgets),
	.dapm_routes    = rk_rk616_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rk_rk616_audio_map),
};
/*
* dts:
* rockchip-rk616 {
*	compatible = "rockchip-rk616";
*	dais {
*		dai0 {
*			audio-codec = <&rk616>;
*			audio-controller = <&i2s0>;
*			format = "i2s";
*			//continuous-clock;
*			//bitclock-inversion;
*			//frame-inversion;
*			//bitclock-master;
*			//frame-master;
*		};
*
*		dai1 {
*			audio-codec = <&rk616>;
*			audio-controller = <&i2s0>;
*			format = "dsp_a";
*			//continuous-clock;
*			bitclock-inversion;
*			//frame-inversion;
*			//bitclock-master;
*			//frame-master;
*		};
*	};
* };
*/
static int rockchip_rk616_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_rk616_snd_card;

	card->dev = &pdev->dev;

	ret = rockchip_of_get_sound_card_info(card);
	if (ret) {
		pr_err("%s() get sound card info failed:%d\n",
			__func__, ret);
		return ret;
	}

	ret = snd_soc_register_card(card);
	if (ret)
		pr_err("%s() register card failed:%d\n",
			__func__, ret);

	return ret;
}

static int rockchip_rk616_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_rk616_of_match[] = {
	{ .compatible = "rockchip-rk616", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_rk616_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_rk616_audio_driver = {
	.driver         = {
		.name   = "rockchip-rk616",
		.owner  = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(rockchip_rk616_of_match),
	},
	.probe          = rockchip_rk616_audio_probe,
	.remove         = rockchip_rk616_audio_remove,
};

module_platform_driver(rockchip_rk616_audio_driver);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");
