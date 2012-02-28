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
#include "../codecs/rk1000_codec.h"
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

//设置分频部分，暂时未设置
	return 0;
}

/*
 * Logic for a rk1000 codec as connected on a rockchip board.
 */
static int rk29_rk1000_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	return 0;
}

static struct snd_soc_ops rk29_ops = {
	  .hw_params = rk29_hw_params,
};

static struct snd_soc_dai_link rk29_dai[] = {
	{
	  .name = "RK1000",
	  .stream_name = "RK1000 CODEC PCM",
	  .platform_name = "rockchip-audio",
	  .codec_name = "RK1000_CODEC.0-0060",
	  .codec_dai_name = "rk1000_codec",
	  .cpu_dai_name = "rk29_i2s.1",
	  .init = rk29_rk1000_codec_init,
	  .ops = &rk29_ops,
	}
};

static struct snd_soc_card snd_soc_card_rk29 = {
	.name = "RK29_RK1000",
	.dai_link = rk29_dai,
	.num_links = 1,
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
	printk("audio_card_init end....\n");
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
