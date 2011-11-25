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
#include "../codecs/wm8988.h"
#include "rk29_pcm.h"
#include "rk29_i2s.h"

#include <mach/gpio.h>

#if 0
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif

//static void *rk29_speaker = NULL;

static int rk29_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;
	  
    DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);    
	/*by Vincent Hsiung for EQ Vol Change*/
	#define HW_PARAMS_FLAG_EQVOL_ON 0x21
	#define HW_PARAMS_FLAG_EQVOL_OFF 0x22
    if ((params->flags == HW_PARAMS_FLAG_EQVOL_ON)||(params->flags == HW_PARAMS_FLAG_EQVOL_OFF))
    {
		ret = codec_dai->driver->ops->hw_params(substream, params, codec_dai); //by Vincent
    	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
    }
    else
    {
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
	{"Micn", NULL, "RINPUT2"},
	{"Micp", NULL, "LINPUT2"},
};

/*
 * Logic for a wm8988 as connected on a rockchip board.
 */
static int rk29_wm8988_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret;
	  
    DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
    
    ret = snd_soc_dai_set_sysclk(codec_dai, 0,
		/*12000000*/11289600, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		printk(KERN_ERR "Failed to set WM8988 SYSCLK: %d\n", ret);
		return ret;
	}
	
    /* Add specific widgets */
	snd_soc_dapm_new_controls(dapm, rk29_dapm_widgets,
				  ARRAY_SIZE(rk29_dapm_widgets));
  	//snd_soc_dapm_nc_pin(codec, "LOUT2");
	//snd_soc_dapm_nc_pin(codec, "ROUT2");
	
    /* Set up specific audio path audio_mapnects */
        snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));
       
        snd_soc_dapm_sync(dapm);
 
    return 0;
}

static struct snd_soc_ops rk29_ops = {
	  .hw_params = rk29_hw_params,
};

static struct snd_soc_dai_link rk29_dai = {
	.name = "WM8988",
	.stream_name = "WM8988 PCM",
	.codec_name = "WM8988.0-001a",
	.platform_name = "rockchip-audio",
	.cpu_dai_name = "rk29_i2s.0",
	.codec_dai_name = "WM8988 HiFi",
	.init = rk29_wm8988_init,
	.ops = &rk29_ops,
};

static struct snd_soc_card snd_soc_card_rk29 = {
	.name = "RK29_WM8988",
	.dai_link = &rk29_dai,
	.num_links = 1,
};

static struct platform_device *rk29_snd_device;

static int __init audio_card_init(void)
{
    int ret =0;	
	
    //rk29_speaker = rk29_speaker_init(RK29_PIN6_PB6, GPIO_HIGH, 2, (200*1000*1000));

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
    //rk29_speaker_deinit(rk29_speaker);	
}

module_init(audio_card_init);
module_exit(audio_card_exit);
/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");
