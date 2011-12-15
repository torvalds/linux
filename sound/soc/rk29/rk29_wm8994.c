/*
 * rk29_wm8994.c  --  SoC audio for rockchip
 *
 * Driver for rockchip wm8994 audio
 *  Copyright (C) 2009 lhh
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
#include "../codecs/wm8994.h"
#include "rk29_pcm.h"
#include "rk29_i2s.h"
#include <linux/clk.h>

#if 0
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif

#define HW_PARAMS_FLAG_EQVOL_ON 0x21
#define HW_PARAMS_FLAG_EQVOL_OFF 0x22

static int rk29_aif1_hw_params(struct snd_pcm_substream *substream,
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
	DBG("Set codec_dai slave\n");
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
	 	SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
#endif	
#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 			   
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	DBG("Set codec_dai master\n");
#endif
	if (ret < 0)
		return ret; 

	/* set cpu DAI configuration */
#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
	DBG("Set cpu_dai master\n");
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
#endif	
#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER)  
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);	
	DBG("Set cpu_dai slave\n"); 
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

	DBG("Enter:%s, %d, rate=%d,pll_out = %d\n",__FUNCTION__,__LINE__,params_rate(params),pll_out);
#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE)	
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	if(ret < 0)
	{
		DBG("rk29_hw_params_wm8994:failed to set the cpu sysclk for codec side\n"); 
		return ret;
	}
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_MCLK1, pll_out, 0);
	if (ret < 0) {
		DBG("rk29_hw_params_wm8994:failed to set the sysclk for codec side\n"); 
		return ret;
	}
#elif defined (CONFIG_SND_RK29_CODEC_SOC_MASTER)
	
#endif

	return 0;
}

static int rk29_aif2_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0; 
	int div_bclk,div_mclk;
	int ret;
	struct clk	*general_pll;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	/* set codec DAI configuration */
#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
	DBG("Set codec_dai slave\n");
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
	 	SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
#endif	
#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 			   
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	DBG("Set codec_dai master\n");
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
			break;
	}

	DBG("Enter:%s, %d, rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_MCLK1, pll_out, 0);
	if (ret < 0) {
		DBG("rk29_hw_params_wm8994:failed to set the sysclk for codec side\n"); 
		return ret;
	}

	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);

#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE)
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, (pll_out/4)/params_rate(params)-1);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);
#endif

	return 0;
}

static int rk29_aif3_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0; 
	int div_bclk,div_mclk;
	int ret;
	struct clk	*general_pll;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	/* set codec DAI configuration */			   
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	DBG("Set codec_dai master\n");

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
			break;
	}

	DBG("Enter:%s, %d, rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_MCLK1, pll_out, 0);
	if (ret < 0) {
		DBG("rk29_hw_params_wm8994:failed to set the sysclk for codec side\n"); 
		return ret;
	}

	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);

#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE)
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, (pll_out/4)/params_rate(params)-1);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);
#endif

	return 0;
}

/*
static const struct snd_soc_dapm_widget rk2818_dapm_widgets[] = {
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
*/

static struct snd_soc_ops rk29_aif1_ops = {
	  .hw_params = rk29_aif1_hw_params,
};

static struct snd_soc_ops rk29_aif2_ops = {
	  .hw_params = rk29_aif2_hw_params,
};

static struct snd_soc_ops rk29_aif3_ops = {
	  .hw_params = rk29_aif3_hw_params,
};

static struct snd_soc_dai_link rk29_dai[] = {
	{
		.name = "WM8994 I2S1",
		.stream_name = "WM8994 PCM",
		.codec_name = "wm8994-codec",
		.platform_name = "rockchip-audio",
#if defined(CONFIG_SND_RK29_SOC_I2S_8CH)	
        	.cpu_dai_name = "rk29_i2s.0",
#elif defined(CONFIG_SND_RK29_SOC_I2S_2CH)
		.cpu_dai_name = "rk29_i2s.1",
#endif
		.codec_dai_name = "wm8994-aif1",
		.ops = &rk29_aif1_ops,
	},
	{
		.name = "WM8994 I2S2",
		.stream_name = "WM8994 PCM",
		.codec_name = "wm8994-codec",
		.platform_name = "rockchip-audio",
#if defined(CONFIG_SND_RK29_SOC_I2S_8CH)	
        	.cpu_dai_name = "rk29_i2s.0",
#elif defined(CONFIG_SND_RK29_SOC_I2S_2CH)
		.cpu_dai_name = "rk29_i2s.1",
#endif
		.codec_dai_name = "wm8994-aif2",
		.ops = &rk29_aif2_ops,
	},
	{
		.name = "WM8994 I2S3",
		.stream_name = "WM8994 PCM",
		.codec_name = "wm8994-codec",
		.platform_name = "rockchip-audio",
#if defined(CONFIG_SND_RK29_SOC_I2S_8CH)	
        	.cpu_dai_name = "rk29_i2s.0",
#elif defined(CONFIG_SND_RK29_SOC_I2S_2CH)
		.cpu_dai_name = "rk29_i2s.1",
#endif
		.codec_dai_name = "wm8994-aif3",
		.ops = &rk29_aif3_ops,
	},
};

static struct snd_soc_card snd_soc_card_rk29 = {
	.name = "RK29_WM8994",
	.dai_link = rk29_dai,
	.num_links = ARRAY_SIZE(rk29_dai),
};

static struct platform_device *rk29_snd_device;

static int __init audio_card_init(void)
{
	int ret =0;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	rk29_snd_device = platform_device_alloc("soc-audio", -1);
	if (!rk29_snd_device) {
		  printk("platform device allocation failed\n");
		  return -ENOMEM;
	}

	platform_set_drvdata(rk29_snd_device, &snd_soc_card_rk29);
	ret = platform_device_add(rk29_snd_device);
	if (ret) {
		printk("platform device add failed\n");

		platform_device_put(rk29_snd_device);
		return ret;
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
