/*
 * rk29_wm8900.c  --  SoC audio for rockchip
 *
 * Driver for rockchip wm8900 audio
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

#include "../codecs/wm8900.h"
#include "card_info.h"
#include "rk_pcm.h"
#include "rk_i2s.h"
#include <linux/clk.h>

#if 0
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif

static int rk29_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0, dai_fmt = rtd->dai_link->dai_fmt;
	int div_bclk,div_mclk;
	int ret;
	struct clk	*general_pll;

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
                DBG("Enter:%s, %d, Error rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
                return -EINVAL;
                break;
        }
        DBG("Enter:%s, %d, rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));

        //pll_out = 12000000;
        //snd_soc_dai_set_pll(codec_dai, NULL, 12000000, pll_out);
        snd_soc_dai_set_clkdiv(codec_dai, WM8900_LRCLK_MODE, 0x000);

	if ((dai_fmt & SND_SOC_DAIFMT_MASTER_MASK) == SND_SOC_DAIFMT_CBM_CFM) {
		snd_soc_dai_set_clkdiv(codec_dai, WM8900_BCLK_DIV, WM8900_BCLK_DIV_4);
		snd_soc_dai_set_clkdiv(codec_dai, WM8900_DAC_LRCLK,(pll_out/4)/params_rate(params));
		snd_soc_dai_set_clkdiv(codec_dai, WM8900_ADC_LRCLK,(pll_out/4)/params_rate(params));
	} else {
		general_pll=clk_get(NULL, "general_pll");
		if(clk_get_rate(general_pll)>260000000)
		{
			div_bclk=(pll_out/4)/params_rate(params)-1;
			div_mclk=3;
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
		DBG("func is%s,gpll=%ld,pll_out=%u,div_mclk=%d\n",
			__FUNCTION__,clk_get_rate(general_pll),pll_out,div_mclk);
		snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
        snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK,div_bclk);
        snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, div_mclk);
	}
        DBG("Enter:%s, %d, LRCK=%d\n",__FUNCTION__,__LINE__,(pll_out/4)/params_rate(params));
        
        return 0;
}

static const struct snd_soc_dapm_widget wm8900_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Audio Out", NULL),
	SND_SOC_DAPM_LINE("Line in", NULL),
	SND_SOC_DAPM_MIC("Micn", NULL),
	SND_SOC_DAPM_MIC("Micp", NULL),
};

static const struct snd_soc_dapm_route audio_map[]= {
	
	{"Audio Out", NULL, "HP_L"},
	{"Audio Out", NULL, "HP_R"},
	{"Line in", NULL, "RINPUT1"},
	{"Line in", NULL, "LINPUT1"},
	{"Micn", NULL, "RINPUT2"},
	{"Micp", NULL, "LINPUT2"},
};

/*
 * Logic for a wm8900 as connected on a rockchip board.
 */
static int rk29_wm8900_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

        DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

        /* Add specific widgets */
	snd_soc_dapm_new_controls(dapm, wm8900_dapm_widgets,
				  ARRAY_SIZE(wm8900_dapm_widgets));
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
        /* Set up specific audio path audio_mapnects */
        snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));
        DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
        snd_soc_dapm_nc_pin(dapm, "HP_L");
        DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	snd_soc_dapm_nc_pin(dapm, "HP_R");
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
        snd_soc_dapm_sync(dapm);
        DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	return 0;
}

static struct snd_soc_ops rk29_ops = {
	  .hw_params = rk29_hw_params,
};

static struct snd_soc_dai_link rk29_dai = {
	.name = "WM8900",
	.stream_name = "WM8900 PCM",
	.codec_dai_name = "WM8900 HiFi",
	.init = rk29_wm8900_init,
	.ops = &rk29_ops,
};

static struct snd_soc_card rockchip_wm8900_snd_card = {
	.name = "RK_WM8900",
	.dai_link = &rk29_dai,
	.num_links = 1,
};

static int rockchip_wm8900_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_wm8900_snd_card;

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

static int rockchip_wm8900_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_wm8900_of_match[] = {
	{ .compatible = "rockchip-wm8900", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_wm8900_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_wm8900_audio_driver = {
	.driver         = {
		.name   = "rockchip-wm8900",
		.owner  = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(rockchip_wm8900_of_match),
	},
	.probe          = rockchip_wm8900_audio_probe,
	.remove         = rockchip_wm8900_audio_remove,
};

module_platform_driver(rockchip_wm8900_audio_driver);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");
