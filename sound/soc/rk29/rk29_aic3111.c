/*
 * rk29_tlv320dac3100.c  --  SoC audio for rockchip
 *
 * Driver for rockchip tlv320aic3100 audio
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
#include <mach/gpio.h>
#include "../codecs/tlv320aic3111.h"
#include "rk29_pcm.h"
#include "rk29_i2s.h"

#if 0
#define	AIC_DBG(x...)	printk(KERN_INFO x)
#else
#define	AIC_DBG(x...)	do { } while (0)
#endif

#ifdef CODECHPDET
	#define HP_DET_PIN 		RK29_PIN6_PA0
#endif



static int rk29_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0; 
	int ret;

	AIC_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	/*by Vincent Hsiung for EQ Vol Change*/
	#define HW_PARAMS_FLAG_EQVOL_ON 0x21
	#define HW_PARAMS_FLAG_EQVOL_OFF 0x22
	if ((params->flags == HW_PARAMS_FLAG_EQVOL_ON)||(params->flags == HW_PARAMS_FLAG_EQVOL_OFF))
	{
		ret = codec_dai->driver->ops->hw_params(substream, params, codec_dai);
		AIC_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	}
	else
	{   
		/* set codec DAI configuration */
		#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
			ret = snd_soc_dai_set_fmt(codec_dai, 
							SND_SOC_DAIFMT_I2S |
							SND_SOC_DAIFMT_NB_NF | 
							SND_SOC_DAIFMT_CBS_CFS);
		#endif

		#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
				ret = snd_soc_dai_set_fmt(codec_dai, 
							SND_SOC_DAIFMT_I2S |
							SND_SOC_DAIFMT_NB_NF |
							SND_SOC_DAIFMT_CBM_CFM ); 
		#endif

		if (ret < 0)
			return ret; 

		/* set cpu DAI configuration */
		#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
				ret = snd_soc_dai_set_fmt(cpu_dai, 
							SND_SOC_DAIFMT_I2S |
							SND_SOC_DAIFMT_NB_NF | 
							SND_SOC_DAIFMT_CBM_CFM);
		#endif
		
		#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
				ret = snd_soc_dai_set_fmt(cpu_dai, 
							SND_SOC_DAIFMT_I2S |
							SND_SOC_DAIFMT_NB_NF | 
							SND_SOC_DAIFMT_CBS_CFS);	
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
			printk("Enter:%s, %d, Error rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
			return -EINVAL;
			break;
	}
	AIC_DBG("Enter:%s, %d, rate=%d, pll_out = %d\n",__FUNCTION__,__LINE__,params_rate(params), pll_out);
	//pll_out = 12000000;
	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	snd_soc_dai_set_sysclk(codec_dai, 0, pll_out, SND_SOC_CLOCK_IN);

	return 0;
}

static const struct snd_soc_dapm_widget dac3100_dapm_widgets[] = {
/*	SND_SOC_DAPM_LINE("Audio Out", NULL),
	SND_SOC_DAPM_LINE("Line in", NULL),
	SND_SOC_DAPM_MIC("Micn", NULL),
	SND_SOC_DAPM_MIC("Micp", NULL),*/
};

static const struct snd_soc_dapm_route audio_map[]= {
/*	{"Audio Out", NULL, "HPL"},
	{"Audio Out", NULL, "HPR"},
	{"Line in", NULL, "RINPUT1"},
	{"Line in", NULL, "LINPUT1"},
	{"Micn", NULL, "RINPUT2"},
	{"Micp", NULL, "LINPUT2"},*/
};

/*
 * Logic for a tlv320dac3100 as connected on a rockchip board.
 */
static int rk29_aic3111_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	AIC_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	/* Add specific widgets */
	snd_soc_dapm_new_controls(dapm, dac3100_dapm_widgets,
				  ARRAY_SIZE(dac3100_dapm_widgets));

	/* Set up specific audio path audio_mapnects */
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));
		AIC_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	snd_soc_dapm_nc_pin(dapm, "HPL");
		AIC_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	snd_soc_dapm_nc_pin(dapm, "HPR");
		AIC_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	snd_soc_dapm_sync(dapm);
		AIC_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	return 0;
}

static struct snd_soc_ops rk29_ops = {
	  .hw_params = rk29_hw_params,
};

static struct snd_soc_dai_link rk29_dai = {
	.name = "AIC3111",
	.stream_name = "AIC3111 PCM",
	.codec_name = "AIC3111.0-0018",
	.platform_name = "rockchip-audio",
	.cpu_dai_name = "rk29_i2s.0",
	.codec_dai_name = "AIC3111 HiFi",
	.init = rk29_aic3111_init,
	.ops = &rk29_ops,
};

static struct snd_soc_card snd_soc_card_rk29 = {
	.name = "RK29_AIC3111",
	.dai_link = &rk29_dai,
	.num_links = 1,
};

static struct platform_device *rk29_snd_device;

static int __init audio_card_init(void)
{
	int ret =0;

        AIC_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	rk29_snd_device = platform_device_alloc("soc-audio", -1);
	if (!rk29_snd_device) {
		  AIC_DBG("platform device allocation failed\n");
		  ret = -ENOMEM;
		  return ret;
	}
	platform_set_drvdata(rk29_snd_device, &snd_soc_card_rk29);
	ret = platform_device_add(rk29_snd_device);
	if (ret) {
	        AIC_DBG("platform device add failed\n");
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
