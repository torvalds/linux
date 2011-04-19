/*
 * rk29_wm8900.c  --  SoC audio for rockchip
 *
 * Driver for rockchip alc5623 audio
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
#include "../codecs/alc5621.h"
#include "rk29_pcm.h"
#include "rk29_i2s.h"

#if 1
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif



static int rk29_hw_params_alc5623(struct snd_pcm_substream *substream,	struct snd_pcm_hw_params *params)
{
        struct snd_soc_pcm_runtime *rtd = substream->private_data;
        struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
        struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
        unsigned int pll_out = 0i,sys_clk; 
        int ret;
          
        DBG("rk29_hw_params for rk29_alc5623\n");    
        /*by Vincent Hsiung for EQ Vol Change*/
        #define HW_PARAMS_FLAG_EQVOL_ON 0x21
        #define HW_PARAMS_FLAG_EQVOL_OFF 0x22
        if ((params->flags == HW_PARAMS_FLAG_EQVOL_ON)||(params->flags == HW_PARAMS_FLAG_EQVOL_OFF))
        {
        	ret = codec_dai->ops->hw_params(substream, params, codec_dai); //by Vincent
        	DBG("rk29_hw_params set EQ vol for rk29_alc5623\n");
        }
        else
        {
                
            /* set codec DAI configuration for codec side */
            #if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
                DBG("rk29_hw_params for rk29_alc5623  codec as slave\n");    
                ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
            #endif	

		    #if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
                ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM ); 
            #endif
                if (ret < 0)return ret; 

            /* set cpu DAI configuration */
            #if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
                DBG("rk29_hw_params for rk29_alc5623  cpu as master\n");    
                ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
            #endif	

			#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
                ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);	
             #endif		
                if (ret < 0)return ret;

        }


        switch(params_rate(params)) {
        case 8000:
              sys_clk=  12288000;
              pll_out = 12288000;
             break; 
        case 16000:
             sys_clk=  11289600;
             pll_out = 12288000;
             break;
        case 24000:
             sys_clk = 24576000;
             pll_out = 12288000;
             break;
        case 32000:
             sys_clk=  12288000;
             pll_out = 12288000;
        case 48000:
             sys_clk =  12288000;
              pll_out = 12288000;
                break;
        /*------------------------------*/
        case 11025:
             sys_clk = 11289600; 
             pll_out = 11289600;
             break;
        case 22050:
             sys_clk = 11289600;
             pll_out = 11289600;
             break;

        case 44100:
             sys_clk = 11289600;
             pll_out = 11289600;
             break;
        default:
                DBG("rk29_hw_params for rk29_alc5623,invalid sapmleRate:%d\n",params_rate(params));
                return -EINVAL;
                break;
        }
        DBG("rk29_hw_params for rk29_alc5623, sapmleRate:%d\n",params_rate(params));

		    
	   /*Set the system clk for codec*/
	    ret=snd_soc_dai_set_sysclk(codec_dai, 0,sys_clk,SND_SOC_CLOCK_IN);//ALC5621 system clk from MCLK or PLL
	    if (ret < 0)
	    {
	       DBG("rk29_hw_params_alc5623:failed to set the sysclk for codec side\n"); 
	   	   return ret;
	   	}

            /*Set the pll of alc5621,the Pll source from MCLK no matter slave or master mode*/
	    ret=snd_soc_dai_set_pll(codec_dai,RT5621_PLL_FR_BCLK,params_rate(params)*64,sys_clk);
	    if (ret < 0)
	    { 
	       DBG("rk29_hw_params_alc5623:failed to set the pll for codec side\n"); 
	  	   return ret;
	    }
	 

        #if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
        snd_soc_dai_set_clkdiv(codec_dai, ALC5623_BCLK_DIV, ALC5623_BCLK_DIV_4);        
       snd_soc_dai_set_clkdiv(codec_dai, ALC5623_DAC_LRCLK,(pll_out/4)/params_rate(params));
        snd_soc_dai_set_clkdiv(codec_dai, ALC5623_ADC_LRCLK,(pll_out/4)/params_rate(params));
        #endif

        #if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE)
        snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
        snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, (pll_out/4)/params_rate(params)-1);
        snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);
        #endif
        DBG("rk29_hw_params_alc5623:,LRCK=%d\n",(pll_out/4)/params_rate(params));        
        return 0;
}

static const struct snd_soc_dapm_widget alc5623_dapm_widgets[] = {
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

/*
 * Logic for a wm8900 as connected on a rockchip board.
 */
static int rk29_alc5623_init(struct snd_soc_codec *codec)
{
	  
    DBG("rk29_alc5623_init\n");

    /* Add specific widgets */
	snd_soc_dapm_new_controls(codec, alc5623_dapm_widgets, ARRAY_SIZE(alc5623_dapm_widgets));
		
    /* Set up specific audio path audio_mapnects */
    snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));
    snd_soc_dapm_nc_pin(codec, "HP_L");
    snd_soc_dapm_nc_pin(codec, "HP_R");
	snd_soc_dapm_sync(codec);
    DBG("rk29_alc5623_init   end\n");
    return 0;
}

static struct snd_soc_ops rk29_ops = {
	.hw_params = rk29_hw_params_alc5623,
};

static struct snd_soc_dai_link rk29_dai_alc5623 = {
	.name = "ALC5623",
	.stream_name = "ALC5623 PCM",
	.cpu_dai = &rk29_i2s_dai[0],
	.codec_dai = &rt5621_dai,
	.init = rk29_alc5623_init,
	.ops = &rk29_ops,
};

static struct snd_soc_card snd_soc_card_rk29_alc5623 = {
	  .name = "RK29_ALC5623",
	  .platform = &rk29_soc_platform,
	  .dai_link = &rk29_dai_alc5623,
	  .num_links = 1,
};


static struct snd_soc_device rk29_snd_devdata_alc5623 = {
	  .card = &snd_soc_card_rk29_alc5623,
	  .codec_dev = &soc_codec_dev_rt5621,
};

static struct platform_device *rk29_snd_device_alc5623;

static int __init audio_card_init_alc5623(void)
{
	int ret =0;	
        DBG("audio_card_init_alc5623\n");
	rk29_snd_device_alc5623 = platform_device_alloc("soc-audio", -1);
	if (!rk29_snd_device_alc5623) {
           DBG("audio_card_init_alc5623:platform device allocation failed\n");
	   ret = -ENOMEM;
	   return ret;
	}
	platform_set_drvdata(rk29_snd_device_alc5623, &rk29_snd_devdata_alc5623);
	rk29_snd_devdata_alc5623.dev = &rk29_snd_device_alc5623->dev;
	ret = platform_device_add(rk29_snd_device_alc5623);
	if (ret) {
	        DBG("audio_card_init_alc5623:platform device add failed\n");
	        platform_device_put(rk29_snd_device_alc5623);
	}
	return ret;
}

static void __exit audio_card_exit_alc5623(void)
{
	platform_device_unregister(rk29_snd_device_alc5623);
}

module_init(audio_card_init_alc5623);
module_exit(audio_card_exit_alc5623);
/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");
