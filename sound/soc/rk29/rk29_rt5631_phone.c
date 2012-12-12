/*
 * rk29_rt5631.c  --  SoC audio for rockchip
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
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <asm/io.h>
#include <mach/hardware.h>
#include "../codecs/rt5631_phone.h"
#include "rk29_pcm.h"
#include "rk29_i2s.h"

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

	/* set codec DAI configuration */
	#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
                                SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	#endif	
	#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
                                SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM ); 
	#endif
	if (ret < 0)
		return ret; 

	/* set cpu DAI configuration */
	#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
                                SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	#endif	
	#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
                                SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);	
	#endif		
	if (ret < 0)
		return ret;


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
	#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE)
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
  

	#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
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
	#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_DSP_A |
                                SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	#endif	
	#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
                                SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM ); 
	#endif
	if (ret < 0)
		return ret; 

	/* set cpu DAI configuration */
	#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
                                SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	#endif	
	#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
                                SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);	
	#endif		
	if (ret < 0)
		return ret;


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
	#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE)
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
  

	#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
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
	snd_soc_add_controls(codec, rk29_controls,
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
		.platform_name = "rockchip-audio",
	#if defined(CONFIG_SND_RK29_SOC_I2S_8CH)	
		.cpu_dai_name = "rk29_i2s.0",
	#elif defined(CONFIG_SND_RK29_SOC_I2S_2CH)
		.cpu_dai_name = "rk29_i2s.1",
	#else
		.cpu_dai_name = "rk29_i2s.2",
	#endif
		.codec_dai_name = "RT5631 HiFi",
		.init = rk29_rt5631_init,
		.ops = &rk29_ops,
	},
	{
		.name = "RT5631 voice",
		.stream_name = "RT5631 voice stream",
		.codec_name = "RT5631.0-001a",
		.platform_name = "rockchip-audio",
	#if defined(CONFIG_SND_RK29_SOC_I2S_8CH)	
		.cpu_dai_name = "rk29_i2s.0",
	#elif defined(CONFIG_SND_RK29_SOC_I2S_2CH)
		.cpu_dai_name = "rk29_i2s.1",
	#else
		.cpu_dai_name = "rk29_i2s.2",
	#endif
		.codec_dai_name = "rt5631-voice",
		.ops = &rk29_ops_voice,
	},	
};

static struct snd_soc_card snd_soc_card_rk29 = {
	.name = "RK29_RT5631",
	.dai_link = rk29_dai,
	.num_links = 2,
};

static struct platform_device *rk29_snd_device;

static int __init audio_card_init(void)
{
	int ret =0;

        DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	rk29_snd_device = platform_device_alloc("soc-audio", -1);
	if (!rk29_snd_device) {
		  printk("platform device allocation failed\n");
		  ret = -ENOMEM;
		  return ret;
	}
	platform_set_drvdata(rk29_snd_device, &snd_soc_card_rk29);
	ret = platform_device_add(rk29_snd_device);
	if (ret) {
	        printk("platform device add failed\n");
	        platform_device_put(rk29_snd_device);
	}
	return ret;
}

static void __exit audio_card_exit(void)
{
	platform_device_unregister(rk29_snd_device);
}

module_init(audio_card_init);
module_exit(audio_card_exit);
/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");

