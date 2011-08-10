/*
 * rk29_cs42l52.c  --  SoC audio for rockchip
 *
 * Driver for rockchip cs42l52 audio
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
#include <linux/clk.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <asm/io.h>
#include <mach/hardware.h>
#include <mach/rk29_iomap.h>
#include "../codecs/cs42l52.h"
#include "rk29_pcm.h"
#include "rk29_i2s.h"


#define HW_PARAMS_FLAG_EQVOL_ON 0x21
#define HW_PARAMS_FLAG_EQVOL_OFF 0x22
static const struct snd_soc_dapm_widget cs42l52_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Audio Out", NULL),
	SND_SOC_DAPM_LINE("Line in", NULL),
	SND_SOC_DAPM_MIC("Micn", NULL),
	SND_SOC_DAPM_MIC("Micp", NULL),
};

static const struct snd_soc_dapm_route audio_map[]= {
	
	{"Audio Out", NULL, "HPA"},
	{"Audio Out", NULL, "HPB"},
	{"Line in", NULL, "INPUT1A"},
	{"Line in", NULL, "INPUT1B"},
	{"Micn", NULL, "INPUT2A"},
	{"Micp", NULL, "INPUT2B"},
};

static int rk29_cs42l52_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
        struct snd_soc_pcm_runtime *rtd = substream->private_data;
        struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
        struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
        unsigned int pll_out = 0; 
        unsigned int lrclk = 0;
		int div_bclk,div_mclk;
		struct clk	*general_pll;
        int ret;
          
        if ((params->flags == HW_PARAMS_FLAG_EQVOL_ON)||(params->flags == HW_PARAMS_FLAG_EQVOL_OFF))
        {
        	ret = codec_dai->ops->hw_params(substream, params, codec_dai); //by Vincent
        }
        else
    	{       
            /* set codec DAI configuration */
            #if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
            ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
            #endif	
            #if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
            ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
            #endif
            if (ret < 0)
              return ret; 

            /* set cpu DAI configuration */
            #if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
            ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
            #endif	
            #if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
            ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
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
                return -EINVAL;
                break;
        }

        #if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE)
		general_pll=clk_get(NULL, "general_pll");
		if(clk_get_rate(general_pll)>260000000)
		{
			div_bclk=(pll_out/4)/params_rate(params)-1;
			//div_bclk= 63;
			div_mclk= 3;
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

		//snd_soc_dai_set_sysclk(codec_dai, 0, pll_out, 0);
		snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
        snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK,div_bclk);
        snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, div_mclk);
        #endif

        
        return 0;
}

static int rk29_cs42l52_dai_init(struct snd_soc_codec *codec)
{
	struct snd_soc_dai *codec_dai = &codec->dai[0];
	int ret;
	  
	snd_soc_dapm_nc_pin(codec, "INPUT1A");
	snd_soc_dapm_nc_pin(codec, "INPUT2A");
	snd_soc_dapm_nc_pin(codec, "INPUT3A");
	snd_soc_dapm_nc_pin(codec, "INPUT4A");
	snd_soc_dapm_nc_pin(codec, "INPUT1B");
	snd_soc_dapm_nc_pin(codec, "INPUT2B");
	snd_soc_dapm_nc_pin(codec, "INPUT3B");
	snd_soc_dapm_nc_pin(codec, "INPUT4B");
	snd_soc_dapm_nc_pin(codec, "MICB");
    snd_soc_dapm_sync(codec);
	return 0;
}

static struct snd_soc_ops rk29_cs42l52_ops = {
	  .hw_params = rk29_cs42l52_hw_params,
};

static struct snd_soc_dai_link rk29_cs42l52_dai_link = {
	  .name = "CS42L52",
	  .stream_name = "CS42L52 PCM",
	  .cpu_dai = &rk29_i2s_dai[0],
	  .codec_dai = &soc_cs42l52_dai,
	  .init = rk29_cs42l52_dai_init,
	  .ops = &rk29_cs42l52_ops,
};

static struct snd_soc_card snd_soc_card_rk29_cs42l52 = {
	  .name = "RK29_CS42L52",
	  .platform = &rk29_soc_platform,
	  .dai_link = &rk29_cs42l52_dai_link,
	  .num_links = 1,
};


static struct snd_soc_device rk29_cs42l52_snd_devdata = {
	  .card = &snd_soc_card_rk29_cs42l52,
	  .codec_dev = &soc_codec_dev_cs42l52,
};

static struct platform_device *rk29_cs42l52_snd_device;

static int rk29_cs42l52_probe(struct platform_device *pdev)
{
	int ret =0;	
	printk("RK29 CS42L52 SoC Audio driver\n");
	rk29_cs42l52_snd_device = platform_device_alloc("soc-audio", -1);
	if (!rk29_cs42l52_snd_device) {
		  ret = -ENOMEM;
		  printk("%s:platform device alloc fail\n",__FUNCTION__);
		  return ret;
	}
	platform_set_drvdata(rk29_cs42l52_snd_device, &rk29_cs42l52_snd_devdata);
	rk29_cs42l52_snd_devdata.dev = &rk29_cs42l52_snd_device->dev;
	ret = platform_device_add(rk29_cs42l52_snd_device);
	if (ret) {
        platform_device_put(rk29_cs42l52_snd_device);
		printk("%s:platform device add fail,ret = %d\n",__FUNCTION__,ret);
	}
	return ret;
}

static int rk29_cs42l52_remove(struct platform_device *pdev)
{
	platform_device_unregister(rk29_cs42l52_snd_device);
	return 0;
}

static struct platform_driver rk29_cs42l52_driver = {
	.probe  = rk29_cs42l52_probe,
	.remove = rk29_cs42l52_remove,
	.driver = {
		.name = "rk29_cs42l52",
		.owner = THIS_MODULE,
	},
};

static int __init rk29_cs42l52_init(void)
{
	return platform_driver_register(&rk29_cs42l52_driver);
}

static void __exit rk29_cs42l52_exit(void)
{
	platform_driver_unregister(&rk29_cs42l52_driver);
}

module_init(rk29_cs42l52_init);
module_exit(rk29_cs42l52_exit);
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");

