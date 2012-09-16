/*
 * rk29_rt3261.c  --  SoC audio for rockchip
 *
 * Driver for rockchip rt3261 audio
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

#include "../codecs/rt3261.h"
#include "rk29_pcm.h"
#include "rk29_i2s.h"

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
	int ret;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);    
	/*by Vincent Hsiung for EQ Vol Change*/
	#define HW_PARAMS_FLAG_EQVOL_ON 0x21
	#define HW_PARAMS_FLAG_EQVOL_OFF 0x22
	if (codec_dai->driver->ops->hw_params && ((params->flags == HW_PARAMS_FLAG_EQVOL_ON) || (params->flags == HW_PARAMS_FLAG_EQVOL_OFF)))
	{
		ret = codec_dai->driver->ops->hw_params(substream, params, codec_dai); //by Vincent
		DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	} else {
                
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
			DBG("Enter:%s, %d, Error rate=%d\n", __FUNCTION__, __LINE__, params_rate(params));
			return -EINVAL;
			break;
	}

	DBG("Enter:%s, %d, rate=%d\n", __FUNCTION__, __LINE__, params_rate(params));

	/*Set the system clk for codec*/
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, pll_out, SND_SOC_CLOCK_IN);
	if (ret < 0)
	{
		DBG("rk29_hw_params_rt3261:failed to set the sysclk for codec side\n"); 
		return ret;
	}

	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, (pll_out/4)/params_rate(params)-1);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);

	DBG("Enter:%s, %d, pll_out/4/params_rate(params) = %d \n", __FUNCTION__, __LINE__, (pll_out/4)/params_rate(params));
 
	return 0;
}

static int rt3261_voice_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0;
	int ret;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);    
       
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_DSP_A |
		SND_SOC_DAIFMT_IB_NF | SND_SOC_DAIFMT_CBM_CFM ); 

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
			DBG("Enter:%s, %d, Error rate=%d\n", __FUNCTION__, __LINE__, params_rate(params));
			return -EINVAL;
			break;
	}

	DBG("Enter:%s, %d, rate=%d\n", __FUNCTION__, __LINE__, params_rate(params));

	/*Set the system clk for codec*/
	snd_soc_dai_set_pll(codec_dai, 0, RT3261_PLL1_S_MCLK, pll_out, 256 * 8000);

 	ret = snd_soc_dai_set_sysclk(codec_dai, RT3261_SCLK_S_PLL1, 256 * 8000, SND_SOC_CLOCK_IN);


	if (ret < 0) {
		printk("rk29_hw_params_rt3261:failed to set the sysclk for codec side\n"); 
		return ret;
	}

	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, (pll_out/4)/params_rate(params)-1);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);

	DBG("Enter:%s, %d, pll_out/4/params_rate(params) = %d \n", __FUNCTION__, __LINE__, (pll_out/4)/params_rate(params));
 
	return 0;
}

static const struct snd_soc_dapm_widget rt3261_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Jack", NULL),	
};

static const struct snd_soc_dapm_route audio_map[]={

	/* Mic Jack --> MIC_IN*/
	{"micbias1", NULL, "Mic Jack"},
	{"MIC1", NULL, "micbias1"},
	
	// HP MIC
	{"micbias1", NULL, "Headset Jack"},
	{"MIC3", NULL, "micbias1"},
} ;

static const struct snd_kcontrol_new rk_controls[] = {
	SOC_DAPM_PIN_SWITCH("Mic Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Jack"),
};

/*
 * Logic for a rt3261 as connected on a rockchip board.
 */
static int rk29_rt3261_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	snd_soc_add_controls(codec, rk_controls,
			ARRAY_SIZE(rk_controls));

	/* Add specific widgets */
	snd_soc_dapm_new_controls(dapm, rt3261_dapm_widgets,
				  ARRAY_SIZE(rt3261_dapm_widgets));
	/* Set up specific audio path audio_mapnects */
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_enable_pin(dapm, "Mic Jack");
	snd_soc_dapm_enable_pin(dapm, "Headset Jack");
	snd_soc_dapm_sync(dapm);

	return 0;
}

static struct snd_soc_ops rk29_ops = {
	.hw_params = rk29_hw_params,
};

static struct snd_soc_ops rt3261_voice_ops = {
	.hw_params = rt3261_voice_hw_params,
};

static struct snd_soc_dai_link rk29_dai[] = {
	{
		.name = "RT3261 I2S1",
		.stream_name = "RT3261 PCM",
		.codec_name = "rt3261.0-001c",
		.platform_name = "rockchip-audio",
		.cpu_dai_name = "rk29_i2s.0",
		.codec_dai_name = "rt3261-aif1",
		.init = rk29_rt3261_init,
		.ops = &rk29_ops,
	},
	{
		.name = "RT3261 I2S2",
		.stream_name = "RT3261 PCM",
		.codec_name = "rt3261.0-001c",
		.platform_name = "rockchip-audio",
		.cpu_dai_name = "rk29_i2s.0",
		.codec_dai_name = "rt3261-aif2",
		.init = rk29_rt3261_init,
		.ops = &rt3261_voice_ops,
	},
};

static struct snd_soc_card snd_soc_card_rk29 = {
	.name = "RK29_RT3261",
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
