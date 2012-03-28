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
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <asm/io.h>
#include <mach/hardware.h>
#include <mach/rk29_iomap.h>
#include "../codecs/rk610_codec.h"
#include "rk29_pcm.h"
#include "rk29_i2s.h"

#if 0
#define	DBG(x...)	printk(KERN_ERR x)
#else
#define	DBG(x...)
#endif

static int rk29_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
#else
        struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
        struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
#endif
	int ret;
	unsigned int pll_out = 0; 
	int div_bclk,div_mclk;
//	struct clk	*general_pll;
	
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);    
	/*by Vincent Hsiung for EQ Vol Change*/
	#define HW_PARAMS_FLAG_EQVOL_ON 0x21
	#define HW_PARAMS_FLAG_EQVOL_OFF 0x22
	if ((params->flags == HW_PARAMS_FLAG_EQVOL_ON)||(params->flags == HW_PARAMS_FLAG_EQVOL_OFF))
	{
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
		ret = codec_dai->driver->ops->hw_params(substream, params, codec_dai); //by Vincent
		#else
		ret = codec_dai->ops->hw_params(substream, params, codec_dai); //by Vincent
		#endif
		DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	}
	else
	{
		/* set codec DAI configuration */
		DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
		#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
		ret = codec_dai->driver->ops->set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS); 
		#else
		ret = codec_dai->ops->set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS); 
		#endif
		#endif	
		#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
		ret = codec_dai->driver->ops->set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM ); 
		#else
		ret = codec_dai->ops->set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM ); 
		#endif
		#endif
		if (ret < 0)
			  return ret; 
		/* set cpu DAI configuration */
		DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
		#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
		ret = cpu_dai->driver->ops->set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
		#else
		ret = cpu_dai->ops->set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
		#endif
		#endif	
		#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
		ret = cpu_dai->driver->ops->set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);	
		#else
		ret = cpu_dai->ops->set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);	
		#endif
		#endif		
		if (ret < 0)
			  return ret;
	}

	switch(params_rate(params)) {
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
            DBG("Enter:%s, %d, Error rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
            return -EINVAL;
            break;
	}
	DBG("Enter:%s, %d, rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
	snd_soc_dai_set_sysclk(codec_dai, 0, pll_out, SND_SOC_CLOCK_IN);
	
//	#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 	
//		snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
//	#endif	
	#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE)
		div_bclk = 63;
		div_mclk = pll_out/(params_rate(params)*64) - 1;
		
		DBG("func is%s,pll_out=%ld,div_mclk=%ld div_bclk=%ld\n",
				__FUNCTION__,pll_out,div_mclk, div_bclk);
		snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
		snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK,div_bclk);
		snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, div_mclk);
//		DBG("Enter:%s, %d, LRCK=%d\n",__FUNCTION__,__LINE__,(pll_out/4)/params_rate(params));		
	#endif
    return 0;
}

static const struct snd_soc_dapm_widget rk29_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Audio Out", NULL),
	SND_SOC_DAPM_LINE("Line in", NULL),
	SND_SOC_DAPM_MIC("Micn", NULL),
	SND_SOC_DAPM_MIC("Micp", NULL),
};

static const struct snd_soc_dapm_route audio_map[]= {
	
	{"Audio Out", NULL, "LOUT1"},
	{"Audio Out", NULL, "ROUT1"},
	{"Line in", NULL, "RINPUT1"},
	{"Line in", NULL, "LINPUT1"},
//	{"Micn", NULL, "RINPUT2"},
//	{"Micp", NULL, "LINPUT2"},
};

/*
 * Logic for a RK610 codec as connected on a rockchip board.
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static int rk29_RK610_codec_init(struct snd_soc_pcm_runtime *rtd) {
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
        /* Add specific widgets */
	snd_soc_dapm_new_controls(dapm, rk29_dapm_widgets,
				  ARRAY_SIZE(rk29_dapm_widgets));
				  
	/* Set up specific audio path audio_mapnects */
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));
	snd_soc_dapm_sync(dapm);
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	
	return 0;
}
#else 
static int rk29_RK610_codec_init(struct snd_soc_codec *codec) {
//	struct snd_soc_dai *codec_dai = &codec->dai[0];
	int ret;
	
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	
//	ret = snd_soc_dai_set_sysclk(codec_dai, 0,
//	                                11289600, SND_SOC_CLOCK_IN);
//	if (ret < 0) {
//	        printk(KERN_ERR "Failed to set WM8988 SYSCLK: %d\n", ret);
//	        return ret;
//	}
	
	/* Add specific widgets */
	snd_soc_dapm_new_controls(codec, rk29_dapm_widgets,
			  ARRAY_SIZE(rk29_dapm_widgets));
	
	/* Set up specific audio path audio_mapnects */
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));
	
	snd_soc_dapm_sync(codec);
	
	return 0;
}
#endif
static struct snd_soc_ops rk29_ops = {
	.hw_params = rk29_hw_params,
};

static struct snd_soc_dai_link rk29_dai = {
	.name = "RK610",
	.stream_name = "RK610 CODEC PCM",
	.codec_name = "RK610_CODEC.1-0060",
	.platform_name = "rockchip-audio",
#if defined(CONFIG_SND_RK29_SOC_I2S_8CH)	
	.cpu_dai_name = "rk29_i2s.0",
#elif defined(CONFIG_SND_RK29_SOC_I2S_2CH)
	.cpu_dai_name = "rk29_i2s.1",
#endif
	.codec_dai_name = "rk610_codec_xx",
	.init = rk29_RK610_codec_init,
	.ops = &rk29_ops,
};
static struct snd_soc_card snd_soc_card_rk29 = {
	.name = "RK29_RK610",
	.dai_link = &rk29_dai,
	.num_links = 1,
};

static struct platform_device *rk29_snd_device;

static int __init audio_card_init(void)
{
	int ret =0;	
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	printk(KERN_ERR "[%s] start\n", __FUNCTION__);
	rk29_snd_device = platform_device_alloc("soc-audio", -1);
	if (!rk29_snd_device) {
		printk("[%s] platform device allocation failed\n", __FUNCTION__);
		ret = -ENOMEM;
		return ret;
	}
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	platform_set_drvdata(rk29_snd_device, &snd_soc_card_rk29);
	#else
	platform_set_drvdata(rk29_snd_device, &rk29_snd_devdata);
	rk29_snd_devdata.dev = &rk29_snd_device->dev;
	#endif
	ret = platform_device_add(rk29_snd_device);
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	if (ret) {
		DBG("platform device add failed\n");
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
