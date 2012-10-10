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
#include "../codecs/wm8994.h"
#include "rk29_pcm.h"
#include "rk29_i2s.h"
#include <linux/clk.h>

#if 0
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif

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

//	DBG("Enter:%s, %d, rate=%d,pll_out = %d\n",__FUNCTION__,__LINE__,params_rate(params),pll_out);	
#ifdef CONFIG_ARCH_RK29
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
	{//96M
		pll_out=pll_out/4;
		div_bclk=(pll_out)/params_rate(params)-1;
		div_mclk=0;
	}
#else
	div_bclk=(pll_out/4)/params_rate(params)-1;
	div_mclk=3;
#endif

	DBG("func is%s,gpll=%ld,pll_out=%d,div_mclk=%d\n",__FUNCTION__,clk_get_rate(general_pll),pll_out,div_mclk);
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	if(ret < 0)
	{
		DBG("rk29_hw_params_wm8994:failed to set the cpu sysclk for codec side\n"); 
		return ret;
	}
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK,div_bclk);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, div_mclk);
	DBG("Enter:%s, %d, LRCK=%d\n",__FUNCTION__,__LINE__,(pll_out/4)/params_rate(params));

	if(div_mclk== 3)
	{//MCLK == 11289600 or 12288000
		ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_MCLK1, pll_out, 0);
		if (ret < 0) {
			DBG("rk29_hw_params_wm8994:failed to set the sysclk for codec side\n"); 
			return ret;
		}
	}
	else
	{
		/* set the codec FLL */
		ret = snd_soc_dai_set_pll(codec_dai, WM8994_FLL1, WM8994_FLL_SRC_MCLK1, pll_out,
				params_rate(params) * 256);
		if (ret < 0)
		{
			printk("%s: snd_soc_dai_set_pll err =%d\n",__FUNCTION__,ret);
			return ret;
		}
		/* set the codec system clock */
		ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_FLL1,
				params_rate(params) * 256, SND_SOC_CLOCK_IN);
		if (ret < 0)
		{
			printk("%s: snd_soc_dai_set_sysclk err =%d\n",__FUNCTION__,ret);
			return ret;
		}
	}

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

	//change to 8Khz
//	params->intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = 8000;	

	DBG("Enter:%s, %d, rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
	
//	if (params_rate(params) != 8000)
//		return -EINVAL;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_DSP_A |
			SND_SOC_DAIFMT_IB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
	{
		printk("%s: snd_soc_dai_set_fmt err =%d\n",__FUNCTION__,ret);
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
	}
	
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
	{//96M
		pll_out=pll_out/4;
		div_bclk=(pll_out)/params_rate(params)-1;
		div_mclk=0;
	}

	DBG("func is%s,gpll=%ld,pll_out=%d,div_mclk=%d\n",
			__FUNCTION__,clk_get_rate(general_pll),pll_out,div_mclk);
	
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	if(ret < 0)
	{
		DBG("rk29_hw_params_wm8994:failed to set the cpu sysclk for codec side\n"); 
		return ret;
	}
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK,div_bclk);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, div_mclk);
	DBG("Enter:%s, %d, LRCK=%d\n",__FUNCTION__,__LINE__,(pll_out/4)/params_rate(params));

	/* set the codec FLL */
	ret = snd_soc_dai_set_pll(codec_dai, WM8994_FLL2, WM8994_FLL_SRC_MCLK1, pll_out,
			8000 * 256);
	if (ret < 0)
	{
		printk("%s: snd_soc_dai_set_pll err =%d\n",__FUNCTION__,ret);
		return ret;
	}
	/* set the codec system clock */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_FLL2,
			8000 * 256, SND_SOC_CLOCK_IN);
	if (ret < 0)
	{
		printk("%s: snd_soc_dai_set_sysclk err =%d\n",__FUNCTION__,ret);
		return ret;
	}

	return ret;
}


static const struct snd_soc_dapm_widget rk29_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Left Spk", NULL),
	SND_SOC_DAPM_SPK("Ext Right Spk", NULL),
	SND_SOC_DAPM_SPK("Ext Rcv", NULL),
	SND_SOC_DAPM_HP("Headset Stereophone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Main Mic", NULL),
	SND_SOC_DAPM_MIC("2nd Mic", NULL),
//	SND_SOC_DAPM_LINE("Radio In", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),	

};

static const struct snd_soc_dapm_route rk29_dapm_routes[] = {
	{"Ext Left Spk", NULL, "SPKOUTLP"},
	{"Ext Left Spk", NULL, "SPKOUTLN"},

	{"Ext Right Spk", NULL, "SPKOUTRP"},
	{"Ext Right Spk", NULL, "SPKOUTRN"},

	{"Ext Rcv", NULL, "HPOUT2N"},
	{"Ext Rcv", NULL, "HPOUT2P"},

	{"Headset Stereophone", NULL, "HPOUT1L"},
	{"Headset Stereophone", NULL, "HPOUT1R"},

	{"IN1LN", NULL, "Headset Mic"},
	{"IN1LP", NULL, "Headset Mic"},

	{"IN1LN", NULL, "2nd Mic"},
	{"IN1LP", NULL, "2nd Mic"},

	{"IN1RN", NULL, "Main Mic"},
	{"IN1RP", NULL, "Main Mic"},

//	{"IN2LN", NULL, "Radio In"},
//	{"IN2RN", NULL, "Radio In"},

	{"IN2LP:VXRN", NULL, "Line In"},
	{"IN2RP:VXRP", NULL, "Line In"},
	
	{"Line Out", NULL, "LINEOUT1N"},
	{"Line Out", NULL, "LINEOUT1P"},

};

static int rk29_wm8994_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
//	int ret;
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	/* add goni specific widgets */
	snd_soc_dapm_new_controls(dapm, rk29_dapm_widgets,
			ARRAY_SIZE(rk29_dapm_widgets));

	/* set up goni specific audio routes */
	snd_soc_dapm_add_routes(dapm, rk29_dapm_routes,
			ARRAY_SIZE(rk29_dapm_routes));

	/* set endpoints to not connected */
//	snd_soc_dapm_nc_pin(dapm, "IN2LP:VXRN");
//	snd_soc_dapm_nc_pin(dapm, "IN2RP:VXRP");
	snd_soc_dapm_nc_pin(dapm, "IN2LN");
	snd_soc_dapm_nc_pin(dapm, "IN2RN");
//	snd_soc_dapm_nc_pin(dapm, "LINEOUT1N");
//	snd_soc_dapm_nc_pin(dapm, "LINEOUT1P");
	snd_soc_dapm_nc_pin(dapm, "LINEOUT2N");
	snd_soc_dapm_nc_pin(dapm, "LINEOUT2P");
#ifdef CONFIG_HDMI
extern int hdmi_is_insert(void);
extern void codec_set_spk(bool on);
	if(hdmi_is_insert())
		codec_set_spk(false);
#endif
	snd_soc_dapm_sync(dapm);

	/* Headset jack detection */
/*	ret = snd_soc_jack_new(codec, "Headset Jack",
			SND_JACK_HEADSET | SND_JACK_MECHANICAL | SND_JACK_AVOUT,
			&jack);
	if (ret)
		return ret;

	ret = snd_soc_jack_add_pins(&jack, ARRAY_SIZE(jack_pins), jack_pins);
	if (ret)
		return ret;

	ret = snd_soc_jack_add_gpios(&jack, ARRAY_SIZE(jack_gpios), jack_gpios);
	if (ret)
		return ret;
*/
	return 0;
}


static struct snd_soc_ops rk29_aif1_ops = {
	  .hw_params = rk29_aif1_hw_params,
};

static struct snd_soc_ops rk29_aif2_ops = {
	  .hw_params = rk29_aif2_hw_params,
};

static struct snd_soc_dai_driver voice_dai = {
	.name = "rk29-voice-dai",
	.id = 0,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
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
		.init = rk29_wm8994_init,
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
	//	snd_soc_unregister_dai(&rk29_snd_device->dev);
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
