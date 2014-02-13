/*
 * rk_rt5631.c  --  SoC audio for rockchip
 *
 * Driver for rockchip rt5631 audio
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
#include "../codecs/rt5631_phone.h"
#include "rk_pcm.h"
#include "rk_i2s.h"

#if 1
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
	int ret;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);    

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
	}
	DBG("Enter:%s, %d, rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
	
	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	#if defined (CONFIG_SND_RK_CODEC_SOC_SLAVE)
	/*Set the system clk for codec*/
	ret=snd_soc_dai_set_sysclk(codec_dai, 0,pll_out,SND_SOC_CLOCK_IN);
	if (ret < 0)
	{
		DBG("rk29_hw_params_rt5631:failed to set the sysclk for codec side\n"); 
		return ret;
	}	    
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, (pll_out/4)/params_rate(params)-1);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);	
	#endif
  

	#if defined (CONFIG_SND_RK_CODEC_SOC_MASTER) 
	snd_soc_dai_set_sysclk(codec_dai,0,pll_out, SND_SOC_CLOCK_IN);						   
	#endif

	DBG("Enter:%s, %d, LRCK=%d\n",__FUNCTION__,__LINE__,(pll_out/4)/params_rate(params));
        
	return 0;
}

static int rk29_hw_params_voice(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0; 
	int ret;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);    
	//change to 8Khz
	params->intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = 8000;	
	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_DSP_A |
                                SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);

	switch(params_rate(params)) {
        case 8000:
        case 16000:
        case 24000:
        case 32000:
        case 48000:
	//		pll_out = 12288000;
	//		break;
        case 11025:
        case 22050:
        case 44100:
	//		pll_out = 11289600;
			pll_out = 2048000;
			break;
        default:
			DBG("Enter:%s, %d, Error rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
			return -EINVAL;
	}
	DBG("Enter:%s, %d, rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
	
	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	#if defined (CONFIG_SND_RK_CODEC_SOC_SLAVE)
	/*Set the system clk for codec*/
	ret=snd_soc_dai_set_sysclk(codec_dai, 0,pll_out,SND_SOC_CLOCK_IN);
	if (ret < 0)
	{
		DBG("rk29_hw_params_rt5631:failed to set the sysclk for codec side\n"); 
		return ret;
	}	    
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, (pll_out/4)/params_rate(params)-1);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);	
	#endif
  

	#if defined (CONFIG_SND_RK_CODEC_SOC_MASTER) 
	snd_soc_dai_set_sysclk(codec_dai,0,pll_out, SND_SOC_CLOCK_IN);
	#endif

	DBG("Enter:%s, %d, LRCK=%d\n",__FUNCTION__,__LINE__,(pll_out/4)/params_rate(params));
        
	return 0;
}

static const struct snd_soc_dapm_widget rt5631_dapm_widgets[] = {
	
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),

};

static const struct snd_soc_dapm_route audio_map[]={
	{"Headphone Jack", NULL, "HPOL"},
	{"Headphone Jack", NULL, "HPOR"},
	{"Ext Spk", NULL, "SPOL"},
	{"Ext Spk", NULL, "SPOR"},
	{"MIC1", NULL, "MIC Bias1"},
	{"MIC Bias1", NULL, "Mic Jack"},
} ;
//bard 7-5 s
static const struct snd_kcontrol_new rk29_controls[] = {
	SOC_DAPM_PIN_SWITCH("Mic Jack"),
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
};
//bard 7-5 e
/*
 * Logic for a rt5631 as connected on a rockchip board.
 */
static int rk29_rt5631_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
//bard 7-5 s
	snd_soc_add_codec_controls(codec, rk29_controls,
			ARRAY_SIZE(rk29_controls));
//bard 7-5 e
	/* Add specific widgets */
	snd_soc_dapm_new_controls(dapm, rt5631_dapm_widgets,
				  ARRAY_SIZE(rt5631_dapm_widgets));
	/* Set up specific audio path audio_mapnects */
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));
//	snd_soc_dapm_nc_pin(dapm, "MONO");
//	snd_soc_dapm_nc_pin(dapm, "MONOIN_RXN");
//	snd_soc_dapm_nc_pin(dapm, "MONOIN_RXP");
	snd_soc_dapm_nc_pin(dapm, "DMIC");
	snd_soc_dapm_sync(dapm);

	return 0;
}

static struct snd_soc_ops rk29_ops = {
	  .hw_params = rk29_hw_params,
};
static struct snd_soc_ops rk29_ops_voice = {
	  .hw_params = rk29_hw_params_voice,
};

static struct snd_soc_dai_link rk29_dai[] = {
	{
		.name = "RT5631 hifi",
		.stream_name = "RT5631 hifi stream",
		.codec_name = "RT5631.0-001a",
	#if defined(CONFIG_SND_RK_SOC_I2S_8CH)	
		.cpu_dai_name = "rockchip-i2s.0",
	#elif defined(CONFIG_SND_RK_SOC_I2S_2CH)
		.cpu_dai_name = "rockchip-i2s.1",
	#else
		.cpu_dai_name = "rockchip-i2s.2",
	#endif
		.codec_dai_name = "RT5631 HiFi",
		.init = rk29_rt5631_init,
		.ops = &rk29_ops,
#if defined (CONFIG_SND_RK_CODEC_SOC_MASTER)
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBM_CFM,
#else
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
#endif
	},
	{
		.name = "RT5631 voice",
		.stream_name = "RT5631 voice stream",
		.codec_name = "RT5631.0-001a",
	#if defined(CONFIG_SND_RK_SOC_I2S_8CH)	
		.cpu_dai_name = "rockchip-i2s.0",
	#elif defined(CONFIG_SND_RK_SOC_I2S_2CH)
		.cpu_dai_name = "rockchip-i2s.1",
	#else
		.cpu_dai_name = "rockchip-i2s.2",
	#endif
		.codec_dai_name = "rt5631-voice",
		.ops = &rk29_ops_voice,
	},	
};

static struct snd_soc_card rockchip_rt5631_snd_card = {
	.name = "RK_RT5631",
	.dai_link = rk29_dai,
	.num_links = 2,
};

static int rockchip_rt5631_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_rt5631_snd_card;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);

	if (ret)
		printk("%s() register card failed:%d\n", __FUNCTION__, ret);

	return ret;
}

static int rockchip_rt5631_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_rt5631_of_match[] = {
        { .compatible = "rockchip-rt5631-phone", },
        {},
};
MODULE_DEVICE_TABLE(of, rockchip_rt5631_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_rt5631_audio_driver = {
        .driver         = {
                .name   = "rockchip-rt5631-phone",
                .owner  = THIS_MODULE,
                .of_match_table = of_match_ptr(rockchip_rt5631_of_match),
        },
        .probe          = rockchip_rt5631_audio_probe,
        .remove         = rockchip_rt5631_audio_remove,
};

module_platform_driver(rockchip_rt5631_audio_driver);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");

