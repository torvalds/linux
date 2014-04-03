/*
 * rk29_rt5621.c  --  SoC audio for rockchip
 *
 * Driver for rockchip rt5621 audio
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

#include "../codecs/rt5621.h"
#include "card_info.h"
#include "rk_pcm.h"
#include "rk_i2s.h"

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
	unsigned int pll_out = 0;
	unsigned int lrclk = 0, dai_fmt = rtd->dai_link->dai_fmt;
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
		DBG("Enter:%s, %d, Error rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
		return -EINVAL;
		break;
	}
	DBG("Enter:%s, %d, rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));

	if ((dai_fmt & SND_SOC_DAIFMT_MASTER_MASK) == SND_SOC_DAIFMT_CBM_CFM) {

		if((24576000%params_rate(params))==0)	//for 8k,16k,32k,48k
		{
			snd_soc_dai_set_pll(codec_dai,0,RT5621_PLL_FR_MCLK,pll_out, 24576000);
			snd_soc_dai_set_sysclk(codec_dai,0, 24576000, SND_SOC_CLOCK_IN);
		}
		else if((22579200%params_rate(params))==0)	//for 11k,22k,44k
		{
			snd_soc_dai_set_pll(codec_dai,0,RT5621_PLL_FR_MCLK,pll_out, 22579200);
			snd_soc_dai_set_sysclk(codec_dai,0, 22579200, SND_SOC_CLOCK_IN);
		}
	}


	if ((dai_fmt & SND_SOC_DAIFMT_MASTER_MASK) == SND_SOC_DAIFMT_CBS_CFS) {
		/*Set the system clk for codec*/
		ret=snd_soc_dai_set_sysclk(codec_dai, 0,pll_out,SND_SOC_CLOCK_IN);
		if (ret < 0) {
			DBG("rk29_hw_params_rt5621:failed to set the sysclk for codec side\n");
			return ret;
		}

		snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
		snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, (pll_out/4)/params_rate(params)-1);
		snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);
	}

	DBG("Enter:%s, %d, LRCK=%d\n",__FUNCTION__,__LINE__,(pll_out/4)/params_rate(params));
        
	return 0;
}

static const struct snd_soc_dapm_widget rt5621_dapm_widgets[] = {
	
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),

};

static const struct snd_soc_dapm_route audio_map[]={

	/* Mic Jack --> MIC_IN*/
	{"Mic Bias1", NULL, "Mic Jack"},
	{"MIC1", NULL, "Mic Bias1"},
	/* HP_OUT --> Headphone Jack */
	{"Headphone Jack", NULL, "HPOL"},
	{"Headphone Jack", NULL, "HPOR"},
	/* LINE_OUT --> Ext Speaker */
	{"Ext Spk", NULL, "SPOL"},
	{"Ext Spk", NULL, "SPOR"},

} ;

/*
 * Logic for a rt5621 as connected on a rockchip board.
 */
static int rk29_rt5621_init(struct snd_soc_pcm_runtime *rtd)
{
        return 0;
}

static struct snd_soc_ops rk29_ops = {
	  .hw_params = rk29_hw_params,
};

static struct snd_soc_dai_link rk29_dai = {
	.name = "RT5621",
	.stream_name = "RT5621 PCM",
	.codec_dai_name = "RT5621 HiFi",
	.init = rk29_rt5621_init,
	.ops = &rk29_ops,
};

static struct snd_soc_card rockchip_rt5621_snd_card = {
	.name = "RK_RT5621",
	.dai_link = &rk29_dai,
	.num_links = 1,
};

static int rockchip_rt5621_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_rt5621_snd_card;

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

static int rockchip_rt5621_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_rt5621_of_match[] = {
	{ .compatible = "rockchip-rt5621", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_rt5621_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_rt5621_audio_driver = {
	.driver         = {
		.name   = "rockchip-rt5621",
		.owner  = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(rockchip_rt5621_of_match),
	},
	.probe          = rockchip_rt5621_audio_probe,
	.remove         = rockchip_rt5621_audio_remove,
};

module_platform_driver(rockchip_rt5621_audio_driver);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");
