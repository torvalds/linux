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

#if 1
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
	int div_bclk,div_mclk;
	int ret;
	struct clk	*general_pll;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	if ((params->flags == HW_PARAMS_FLAG_EQVOL_ON)||(params->flags == HW_PARAMS_FLAG_EQVOL_OFF))
	{
		DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

		if (codec_dai->driver->ops->hw_params)
			ret = codec_dai->driver->ops->hw_params(substream, params, codec_dai); //by Vincent

		return 0;
	}

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
	DBG("Set cpu_dai slave\n");
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
#endif	
#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER)  
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);	
	DBG("Set cpu_dai master\n"); 
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

#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 	
	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
#endif

#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE)
	general_pll=clk_get(NULL, "general_pll");

	if(clk_get_rate(general_pll)>260000000) {
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
	DBG("func is%s,gpll=%ld,pll_out=%d,div_mclk=%d\n",
		__FUNCTION__,clk_get_rate(general_pll),pll_out,div_mclk);
	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK,div_bclk);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, div_mclk);
	
	if(div_mclk == 3)
		snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_MCLK1, pll_out, 0);
	else
		snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_FLL1, pll_out, 0);	
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

#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 	
	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
#endif

#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE)
	general_pll=clk_get(NULL, "general_pll");

	if(clk_get_rate(general_pll)>260000000) {
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
	DBG("func is%s,gpll=%ld,pll_out=%d,div_mclk=%d\n",
		__FUNCTION__,clk_get_rate(general_pll),pll_out,div_mclk);
	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	
	if(div_mclk == 3)
		snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_MCLK1, pll_out, 0);
	else
		snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_FLL1, pll_out, 0);
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

	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);

	general_pll=clk_get(NULL, "general_pll");

	if(clk_get_rate(general_pll)>260000000) {
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
	
	if(div_mclk == 3)
		snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_MCLK1, pll_out, 0);
	else
		snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_FLL1, pll_out, 0);

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
/*
 * Logic for a wm8994 as connected on a rockchip board.
 */
static int rk29_wm8994_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
    
	ret = snd_soc_dai_set_sysclk(codec_dai, 0,
		12000000, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		printk(KERN_ERR "Failed to set WM8994 SYSCLK: %d\n", ret);
		return ret;
	}
	
	/* Add specific widgets */
//	snd_soc_dapm_new_controls(dapm, rk2818_dapm_widgets,
//		ARRAY_SIZE(rk2818_dapm_widgets));	
	/* Set up specific audio path audio_mapnects */
//	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));       
//	snd_soc_dapm_sync(codec);
 
    return 0;
}

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
		.name = "WM8994",
		.stream_name = "WM8994 I2S1",
		.codec_name = "WM8994.0-001a",
		.platform_name = "rockchip-audio",
        	.cpu_dai_name = "rk29_i2s.0",
		.codec_dai_name = "wm8994-aif1",
		.init = rk29_wm8994_init,
		.ops = &rk29_aif1_ops,
	},
	{
		.name = "WM8994",
		.stream_name = "WM8994 I2S2",
		.codec_name = "WM8994.1-001a",
		.platform_name = "rockchip-audio",
        	.cpu_dai_name = "rk29_i2s.0",
		.codec_dai_name = "wm8994-aif2",
		.init = rk29_wm8994_init,
		.ops = &rk29_aif2_ops,
	},
	{
		.name = "WM8994",
		.stream_name = "WM8994 I2S3",
		.codec_name = "WM8994.2-001a",
		.platform_name = "rockchip-audio",
        	.cpu_dai_name = "rk29_i2s.0",
		.codec_dai_name = "wm8994-aif3",
		.init = rk29_wm8994_init,
		.ops = &rk29_aif3_ops,
	},
};

static struct snd_soc_card snd_soc_card_rk29 = {
	.name = "RK29_WM8994",
	.dai_link = rk29_dai,
	.num_links = 3,
};

static struct platform_device *rk29_snd_device;

static int __init audio_card_init(void)
{
	int ret =0;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	rk29_snd_device = platform_device_alloc("soc-audio", -1);
	if (!rk29_snd_device) {
		  DBG("platform device allocation failed\n");
		  ret = -ENOMEM;
		  return ret;
	}

	platform_set_drvdata(rk29_snd_device, &snd_soc_card_rk29);
	ret = platform_device_add(rk29_snd_device);
	if (ret) {
		DBG("platform device add failed\n");

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
