/*
 * rk29_tlv320dac3100.c  --  SoC audio for rockchip
 *
 * Driver for rockchip tlv320aic3100 audio
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

#include "../codecs/tlv320aic3111.h"
#include "card_info.h"
#include "rk_pcm.h"
#include "rk_i2s.h"

#if 0
#define	AIC_DBG(x...)	printk(KERN_INFO x)
#else
#define	AIC_DBG(x...)	do { } while (0)
#endif

#ifdef CODECHPDET
	#define HP_DET_PIN 		RK29_PIN6_PA0
#endif



static int rk29_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0, dai_fmt = rtd->dai_link->dai_fmt;
	int ret;

	AIC_DBG("Enter::%s----%d\n", __FUNCTION__, __LINE__);

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
			printk("Enter:%s, %d, Error rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
			return -EINVAL;
			break;
	}
	AIC_DBG("Enter:%s, %d, rate=%d, pll_out = %d\n",__FUNCTION__,__LINE__,params_rate(params), pll_out);
	//pll_out = 12000000;
	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	snd_soc_dai_set_sysclk(codec_dai, 0, pll_out, SND_SOC_CLOCK_IN);

	return 0;
}

static const struct snd_soc_dapm_widget dac3100_dapm_widgets[] = {
/*	SND_SOC_DAPM_LINE("Audio Out", NULL),
	SND_SOC_DAPM_LINE("Line in", NULL),
	SND_SOC_DAPM_MIC("Micn", NULL),
	SND_SOC_DAPM_MIC("Micp", NULL),*/
};

static const struct snd_soc_dapm_route audio_map[]= {
/*	{"Audio Out", NULL, "HPL"},
	{"Audio Out", NULL, "HPR"},
	{"Line in", NULL, "RINPUT1"},
	{"Line in", NULL, "LINPUT1"},
	{"Micn", NULL, "RINPUT2"},
	{"Micp", NULL, "LINPUT2"},*/
};

/*
 * Logic for a tlv320dac3100 as connected on a rockchip board.
 */
static int rk29_aic3111_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	AIC_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	/* Add specific widgets */
	snd_soc_dapm_new_controls(dapm, dac3100_dapm_widgets,
				  ARRAY_SIZE(dac3100_dapm_widgets));

	/* Set up specific audio path audio_mapnects */
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));
		AIC_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	snd_soc_dapm_nc_pin(dapm, "HPL");
		AIC_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	snd_soc_dapm_nc_pin(dapm, "HPR");
		AIC_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	snd_soc_dapm_sync(dapm);
		AIC_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	return 0;
}

static struct snd_soc_ops rk29_ops = {
	  .hw_params = rk29_hw_params,
};

static struct snd_soc_dai_link rk29_dai = {
	.name = "AIC3111",
	.stream_name = "AIC3111 PCM",
	.codec_dai_name = "AIC3111 HiFi",
	.init = rk29_aic3111_init,
	.ops = &rk29_ops,
};

static struct snd_soc_card rockchip_aic3111_snd_card = {
	.name = "RK_AIC3111",
	.dai_link = &rk29_dai,
	.num_links = 1,
};

static int rockchip_aic3111_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_aic3111_snd_card;

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

static int rockchip_aic3111_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_aic3111_of_match[] = {
	{ .compatible = "rockchip-aic3111", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_aic3111_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_aic3111_audio_driver = {
	.driver         = {
		.name   = "rockchip-aic3111",
		.owner  = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(rockchip_aic3111_of_match),
	},
	.probe          = rockchip_aic3111_audio_probe,
	.remove         = rockchip_aic3111_audio_remove,
};

module_platform_driver(rockchip_aic3111_audio_driver);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");
