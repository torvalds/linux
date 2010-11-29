/*
 * rk2818_wm8988.c  --  SoC audio for rockchip
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

#if 0
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif

static int rk2818_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
    struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	int ret;
	  
    DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);    
	/*by Vincent Hsiung for EQ Vol Change*/
	#define HW_PARAMS_FLAG_EQVOL_ON 0x21
	#define HW_PARAMS_FLAG_EQVOL_OFF 0x22
    if ((params->flags == HW_PARAMS_FLAG_EQVOL_ON)||(params->flags == HW_PARAMS_FLAG_EQVOL_OFF))
    {
    	ret = codec_dai->ops->hw_params(substream, params, codec_dai); //by Vincent
    	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
    }
    else
    {
	    /* set codec DAI configuration */
	    #if defined (CONFIG_SND_CODEC_SOC_SLAVE) 
	    ret = codec_dai->ops->set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
	    	SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS); 
	    #endif	
	    #if defined (CONFIG_SND_CODEC_SOC_MASTER) 
	    ret = codec_dai->ops->set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
	    	SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM ); 
	    #endif
	    if (ret < 0)
	    	  return ret; 
	    /* set cpu DAI configuration */
	    #if defined (CONFIG_SND_CODEC_SOC_SLAVE) 
	    ret = cpu_dai->ops->set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
	    	SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	    #endif	
	    #if defined (CONFIG_SND_CODEC_SOC_MASTER) 
	    ret = cpu_dai->ops->set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
	    	SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);	
	    #endif		
	    if (ret < 0)
	    	  return ret;
	  }
    
	  return 0;
}

static const struct snd_soc_dapm_widget rk2818_dapm_widgets[] = {
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
static int rk2818_wm8988_init(struct snd_soc_codec *codec)
{
	struct snd_soc_dai *codec_dai = &codec->dai[0];
	int ret;
	  
    DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
    
    ret = snd_soc_dai_set_sysclk(codec_dai, 0,
		12000000, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		printk(KERN_ERR "Failed to set WM8988 SYSCLK: %d\n", ret);
		return ret;
	}
	
    /* Add specific widgets */
	snd_soc_dapm_new_controls(codec, rk2818_dapm_widgets,
				  ARRAY_SIZE(rk2818_dapm_widgets));
  	snd_soc_dapm_nc_pin(codec, "LOUT2");
	snd_soc_dapm_nc_pin(codec, "ROUT2");
	
    /* Set up specific audio path audio_mapnects */
    snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));
       
    snd_soc_dapm_sync(codec);
 
    return 0;
}

static struct snd_soc_ops rk2818_ops = {
	  .hw_params = rk2818_hw_params,
};

static struct snd_soc_dai_link rk2818_dai = {
	  .name = "WM8988",
	  .stream_name = "WM8988 PCM",
	  .cpu_dai = &rk2818_i2s_dai,
	  .codec_dai = &wm8988_dai,
	  .init = rk2818_wm8988_init,
	  .ops = &rk2818_ops,
};

static struct snd_soc_card snd_soc_card_rk2818 = {
	  .name = "RK2818_WM8988",
	  .platform = &rk2818_soc_platform,
	  .dai_link = &rk2818_dai,
	  .num_links = 1,
};


static struct snd_soc_device rk2818_snd_devdata = {
	  .card = &snd_soc_card_rk2818,
	  .codec_dev = &soc_codec_dev_wm8988,
};

static struct platform_device *rk2818_snd_device;

static int __init audio_card_init(void)
{
	int ret =0;	
    DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	rk2818_snd_device = platform_device_alloc("soc-audio", -1);
	if (!rk2818_snd_device) {
		  DBG("platform device allocation failed\n");
		  ret = -ENOMEM;
		  return ret;
	}
	platform_set_drvdata(rk2818_snd_device, &rk2818_snd_devdata);
	rk2818_snd_devdata.dev = &rk2818_snd_device->dev;
	ret = platform_device_add(rk2818_snd_device);
	if (ret) {
	    DBG("platform device add failed\n");
	    platform_device_put(rk2818_snd_device);
	}
	return ret;
}
static void __exit audio_card_exit(void)
{
	platform_device_unregister(rk2818_snd_device);
}

module_init(audio_card_init);
module_exit(audio_card_exit);
/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");
