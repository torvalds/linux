/*
* rk_rk3036codec.c-- SoC audio for rockchip
*
* Driver for rockchip internal rk_rk3036codec audio
*
* This program is free software; you can redistribute  it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
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
#include "../codecs/rk3036_codec.h"
#include "card_info.h"
#include "rk_pcm.h"
#include "rk_i2s.h"

#if 0
#define	DBG(x...)	pr_info("rk_rk3036" x)
#else
#define	DBG(x...)
#endif
static int rk30_hw_params(struct snd_pcm_substream *
	substream, struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0, dai_fmt = rtd->dai_link->dai_fmt;
	int ret;

	DBG("Enter::%s----%d\n", __func__, __LINE__);

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, dai_fmt);
	if (ret < 0) {
		DBG("%s():failed to set the format for codec side\n",
		    __func__);
		return ret;
	}

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, dai_fmt);
	if (ret < 0) {
		DBG("%s():failed to set the format for cpu side\n",
		    __func__);
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
		    __func__, __LINE__, params_rate(params));
		return -EINVAL;
		break;
	}

	DBG("Enter:%s, %d, rate=%d\n",
	    __func__, __LINE__, params_rate(params));

	/*Set the system clk for codec*/
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, pll_out, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		DBG("rk_hifi_hw_params:failed to set the sysclk for codec\n");
		return ret;
	}

	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK,
			       (pll_out/4)/params_rate(params)-1);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);
	DBG("Enter:%s, %d, pll_out/4/params_rate(params) = %d \n",
	    __func__, __LINE__, (pll_out/4)/params_rate(params));

	return 0;
}

/*
 * Logic for a rk3036 codec as connected on a rockchip board.
 */
static int rk30_rk3036_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	return 0;
}

static struct snd_soc_ops rk30_ops = {
	  .hw_params = rk30_hw_params,
};

static struct snd_soc_dai_link rk30_dai[] = {
	{
		.name = "RK3036",
		.stream_name = "RK3036 CODEC PCM",
		.codec_dai_name = "rk3036-voice",
		.init = rk30_rk3036_codec_init,
		.ops = &rk30_ops,
	}
};

static struct snd_soc_card rockchip_rk3036_snd_card = {
	.name = "RK_RK3036",
	.dai_link = rk30_dai,
	.num_links = 1,
};

static int rockchip_rk3036_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_rk3036_snd_card;

	card->dev = &pdev->dev;

	ret = rockchip_of_get_sound_card_info(card);
	if (ret < 0)
		return ret;

	ret = snd_soc_register_card(card);

	return ret;
}

static int rockchip_rk3036_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_rk3036_of_match[] = {
		{ .compatible = "rk3036-audio", },
		{},
};
MODULE_DEVICE_TABLE(of, rockchip_rk3036_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_rk3036_audio_driver = {
	.driver = {
		.name   = "rk3036-audio",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(rockchip_rk3036_of_match),
	},
	.probe      = rockchip_rk3036_audio_probe,
	.remove     = rockchip_rk3036_audio_remove,
};
module_platform_driver(rockchip_rk3036_audio_driver);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");
