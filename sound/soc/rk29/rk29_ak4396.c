/*
 * rk29_ak4396.c  --  SoC audio for rockchip
 *
 * Driver for rockchip ak4396 audio
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
#include "rk29_pcm.h"
#include "rk29_i2s.h"

#include <mach/gpio.h>

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
    int ret=-1;
	
    DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);    

    /* set codec DAI configuration */
    #if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
    ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_RIGHT_J |
	    	SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS); 
    if (ret < 0)        return ret; 
    /* set cpu DAI configuration */
    ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_RIGHT_J |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
    if (ret < 0)        return ret;		
    #endif	

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
	case 88200:
	case 176400:
		pll_out = 11289600*2;
		break;
	case 96000:
	case 192000:
		pll_out = 12288000*2;
		break;
    default:
        DBG("Enter:%s, %d, Error rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
        return -EINVAL;
    }

    DBG("Enter:%s, %d, rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));

	#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE)
	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, (2 * 32 )-1); //bclk = 2 * 32 * lrck

	switch(params_rate(params)){
	case 192000:
	case 176400:
		snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK,1);
        DBG("Enter:%s, %d, MCLK=%d BCLK=%d LRCK=%d\n",
		__FUNCTION__,__LINE__,pll_out,pll_out/2,params_rate(params));
 		break;
    default :
        snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);
        DBG("default:%s, %d, MCLK=%d BCLK=%d LRCK=%d\n",
		__FUNCTION__,__LINE__,pll_out,pll_out/4,params_rate(params));			
		break;
    }
	snd_soc_dai_set_sysclk(codec_dai,0,pll_out,SND_SOC_CLOCK_IN);
	#endif
	return ret;
}

/*
 * Logic for a ak4396 as connected on a rockchip board.
 */
static int rk29_ak4396_init(struct snd_soc_pcm_runtime *rtd)
{
    DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

    return 0;
}

static struct snd_soc_ops rk29_ops = {
	  .hw_params = rk29_hw_params,
};

static struct snd_soc_dai_link rk29_dai = {
	.name = "AK4396",
	.stream_name = "AK4396 PCM",
	.codec_name = "spi1.0",
	.platform_name = "rockchip-audio",
#if defined(CONFIG_SND_RK29_SOC_I2S_8CH)	
	.cpu_dai_name = "rk29_i2s.0",
#elif defined(CONFIG_SND_RK29_SOC_I2S_2CH)
	.cpu_dai_name = "rk29_i2s.1",
#else
	.cpu_dai_name = "rk29_i2s.2",
#endif
	.codec_dai_name = "AK4396 HiFi",
	.init = rk29_ak4396_init,
	.ops = &rk29_ops,
};

static struct snd_soc_card snd_soc_card_rk29 = {
	.name = "RK29_AK4396",
	.dai_link = &rk29_dai,
	.num_links = 1,
};

static struct platform_device *rk29_snd_device;

static int __init audio_card_init(void)
{
    int ret =0;	

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
        return ret;
	}
		
	return ret;
}

static void __exit audio_card_exit(void)
{
    platform_device_unregister(rk29_snd_device);;	
}

module_init(audio_card_init);
module_exit(audio_card_exit);
/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");
