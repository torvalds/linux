/*
 * rk29_wm8988.c  --  SoC audio for rockchip
 *
 * Driver for rockchip wm8988 audio
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
#include "../codecs/rk1000_codec.h"
#include "card_info.h"
#include "rk_pcm.h"
#include "rk_i2s.h"

#define RK1000_CARD_DBG 0

#if RK1000_CARD_DBG
#define	DBG(x...)	pr_info(x)
#else
#define	DBG(x...)
#endif

static int rk29_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int dai_fmt = rtd->dai_link->dai_fmt;
	int ret;
	unsigned int pll_out = 0;
	int div_bclk, div_mclk;

	DBG("Enter::%s----%d\n",  __func__, __LINE__);
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
	case 8000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 96000:
		pll_out = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		pll_out = 11289600;
		break;
	case 176400:
		pll_out = 11289600*2;
		break;
	case 192000:
		pll_out = 12288000*2;
		break;
	default:
		DBG("Enter:%s, %d, Error rate=%d\n", __func__,
		    __LINE__, params_rate(params));
		return -EINVAL;
		break;
	}
	DBG("Enter:%s, %d, rate=%d\n", __func__, __LINE__, params_rate(params));
	snd_soc_dai_set_sysclk(codec_dai, 0, pll_out, SND_SOC_CLOCK_IN);

	div_bclk = 127;
	div_mclk = pll_out/(params_rate(params)*128) - 1;

	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, div_bclk);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, div_mclk);
	return 0;
}

/*
 * Logic for a rk1000 codec as connected on a rockchip board.
 */
static int rk29_rk1000_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	return 0;
}

static struct snd_soc_ops rk29_ops = {
	  .hw_params = rk29_hw_params,
};

static struct snd_soc_dai_link rk29_dai[] = {
	{
		.name = "RK1000",
		.stream_name = "RK1000 CODEC PCM",
		.codec_dai_name = "rk1000_codec",
		.init = rk29_rk1000_codec_init,
		.ops = &rk29_ops,
	}
};

static struct snd_soc_card rockchip_rk1000_snd_card = {
	.name = "RK_RK1000",
	.dai_link = rk29_dai,
	.num_links = 1,
};

static int rockchip_rk1000_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_rk1000_snd_card;

	card->dev = &pdev->dev;
	ret = rockchip_of_get_sound_card_info(card);
	if (ret) {
		pr_err("%s() get sound card info failed:%d\n", __func__, ret);
		return ret;
	}
	ret = snd_soc_register_card(card);
	if (ret)
		pr_err("%s() register card failed:%d\n", __func__, ret);
	return ret;
}

static int rockchip_rk1000_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_rk1000_of_match[] = {
	{ .compatible = "rockchip-rk1000", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_rk1000_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_rk1000_audio_driver = {
	.driver         = {
		.name   = "rockchip-rk1000",
		.owner  = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(rockchip_rk1000_of_match),
	},
	.probe          = rockchip_rk1000_audio_probe,
	.remove         = rockchip_rk1000_audio_remove,
};

module_platform_driver(rockchip_rk1000_audio_driver);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");
