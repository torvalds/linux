/*
 * rk29_cs42l52.c  --  SoC audio for rockchip
 *
 * Driver for rockchip cs42l52 audio
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
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "../codecs/cs42l52.h"
#include "card_info.h"
#include "rk_pcm.h"
#include "rk_i2s.h"


#define HW_PARAMS_FLAG_EQVOL_ON 0x21
#define HW_PARAMS_FLAG_EQVOL_OFF 0x22
static const struct snd_soc_dapm_widget cs42l52_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Audio Out", NULL),
	SND_SOC_DAPM_LINE("Line in", NULL),
	SND_SOC_DAPM_MIC("Micn", NULL),
	SND_SOC_DAPM_MIC("Micp", NULL),
};

static const struct snd_soc_dapm_route audio_map[]= {
	
	{"Audio Out", NULL, "HPA"},
	{"Audio Out", NULL, "HPB"},
	{"Line in", NULL, "INPUT1A"},
	{"Line in", NULL, "INPUT1B"},
	{"Micn", NULL, "INPUT2A"},
	{"Micp", NULL, "INPUT2B"},
};

static int rk29_cs42l52_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
        struct snd_soc_pcm_runtime *rtd = substream->private_data;
        struct snd_soc_dai *codec_dai = rtd->codec_dai;
        struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
        unsigned int pll_out = 0, dai_fmt = rtd->dai_link->dai_fmt;
        unsigned int lrclk = 0;
		int div_bclk,div_mclk;
		struct clk	*general_pll;
        int ret;

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
                return -EINVAL;
                break;
        }

	if ((dai_fmt & SND_SOC_DAIFMT_MASTER_MASK) == SND_SOC_DAIFMT_CBS_CFS) {
		general_pll=clk_get(NULL, "general_pll");
		if(clk_get_rate(general_pll)>260000000)
		{
			div_bclk=(pll_out/4)/params_rate(params)-1;
			//div_bclk= 63;
			div_mclk= 3;
		}
		else if(clk_get_rate(general_pll)>130000000)
		{
			div_bclk=(pll_out/2)/params_rate(params)-1;
			div_mclk=1;
		}
		else
		{
			pll_out=pll_out/4;
			div_bclk=(pll_out)/params_rate(params)-1;
			div_mclk=0;
		}

		//snd_soc_dai_set_sysclk(codec_dai, 0, pll_out, 0);
		snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
        snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK,div_bclk);
        snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, div_mclk);
	}

        
        return 0;
}

static int rk29_cs42l52_dai_init(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	  
	snd_soc_dapm_nc_pin(dapm, "INPUT1A");
	snd_soc_dapm_nc_pin(dapm, "INPUT2A");
	snd_soc_dapm_nc_pin(dapm, "INPUT3A");
	snd_soc_dapm_nc_pin(dapm, "INPUT4A");
	snd_soc_dapm_nc_pin(dapm, "INPUT1B");
	snd_soc_dapm_nc_pin(dapm, "INPUT2B");
	snd_soc_dapm_nc_pin(dapm, "INPUT3B");
	snd_soc_dapm_nc_pin(dapm, "INPUT4B");
	snd_soc_dapm_nc_pin(dapm, "MICB");
	snd_soc_dapm_sync(dapm);
	return 0;
}

static struct snd_soc_ops rk29_cs42l52_ops = {
	  .hw_params = rk29_cs42l52_hw_params,
};

static struct snd_soc_dai_link rk29_cs42l52_dai_link = {
	.name = "CS42L52",
	.stream_name = "CS42L52 PCM",
	.codec_dai_name = "cs42l52-hifi",
	.init = rk29_cs42l52_dai_init,
	.ops = &rk29_cs42l52_ops,
};

static struct snd_soc_card rockchip_cs42l52_snd_card = {
	  .name = "RK_CS42L52",
	  .dai_link = &rk29_cs42l52_dai_link,
	  .num_links = 1,
};

static int rockchip_cs42l52_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_cs42l52_snd_card;

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

static int rockchip_cs42l52_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_cs42l52_of_match[] = {
	{ .compatible = "rockchip-cs42l52", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_cs42l52_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_cs42l52_audio_driver = {
	.driver         = {
		.name   = "rockchip-cs42l52",
		.owner  = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(rockchip_cs42l52_of_match),
	},
	.probe          = rockchip_cs42l52_audio_probe,
	.remove         = rockchip_cs42l52_audio_remove,
};

module_platform_driver(rockchip_cs42l52_audio_driver);

MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");

