/*
 *  odroid_rt5512.c
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/module.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <asm/io.h>
#include <mach/hardware.h>
#include <sound/jack.h>
#include <linux/delay.h>    
#include "rk29_pcm.h"
#include "rk29_i2s.h"
#if 1
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif

#include "../codecs/rt5512.h"

static struct platform_device *rk29_snd_device;


static int rk29_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
        unsigned int pll_out = 0;
        unsigned int pll_div;
        int ret;

        DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);    
        
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
        case 96000:
        case 192000:	
                pll_out = 12288000*2;
                break;		
        case 88200:
        case 176400:	
                pll_out = 11289600*2;
                break;		
        default:
                DBG("Enter:%s, %d, Error rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
                return -EINVAL;
                break;
        }
#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE)
	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, 64-1);//bclk = 2*32*lrck; 2*32fs
	switch(params_rate(params)) {
        case 176400:		
		case 192000:
			snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 1);	
        DBG("Enter:%s, %d, MCLK=%d BCLK=%d LRCK=%d\n",
		__FUNCTION__,__LINE__,pll_out,pll_out/2,params_rate(params));			
			break;
		default:
			snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);	
        DBG("default:%s, %d, MCLK=%d BCLK=%d LRCK=%d\n",
		__FUNCTION__,__LINE__,pll_out,pll_out/4,params_rate(params));			
			break;
	}

    /*Set the system clk for codec*/
	ret=snd_soc_dai_set_sysclk(codec_dai, 0,pll_out,SND_SOC_CLOCK_IN);
	if (ret < 0)
	{
		DBG("rk29_hw_params_rt5631:failed to set the sysclk for codec side\n"); 
		return ret;
	}	    

    switch (params_rate(params))
	{
		case 8000:
			pll_div = 12;
			break;
		case 16000:
			pll_div = 6;
			break;
		case 32000:
			pll_div = 3;
			break;
		case 48000:
			pll_div = 2;
			break;
		case 96000:
			pll_div = 1;
			break;
		case 11025:
			pll_div = 8;
			break;
		case 22050:
			pll_div = 4;
			break;
		case 44100:
			pll_div = 2;
			break;
		case 88200:
			pll_div = 1;
			break;
    		default:
			printk("Not yet supported!\n");
			return -EINVAL;
	}
	ret = snd_soc_dai_set_clkdiv(codec_dai, RT5512_CLK_DIV_ID, pll_div*4);
	if (ret < 0)
		return ret;	
#endif

#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
	//snd_soc_dai_set_pll(codec_dai,0,pll_out, 22579200);
	snd_soc_dai_set_sysclk(codec_dai,0,pll_out, SND_SOC_CLOCK_IN);						      
#endif       
	return 0;
}

//---------------------------------------------------------------------------------
/*
 * rt5512 DAI operations.
 */
static struct snd_soc_ops rk29_ops = {
	.hw_params = rk29_hw_params,
};

static const struct snd_soc_dapm_widget rt5512_dapm_widgets[] = {
	// Input
	SND_SOC_DAPM_MIC("Main Mic", NULL),
	SND_SOC_DAPM_LINE("LineIn", NULL),
	// Output
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
};

static const struct snd_soc_dapm_route rt5512_audio_map[] = {
	// Input
	{"MicBias1", NULL,"Main Mic"},
	{"Mic2", NULL, "MicBias1"},
    {"MicBias2", NULL, "LineIn"},
    {"Aux", NULL, "MicBias2"},
	// Output
	{"Ext Spk", NULL, "LSpeaker"},
	{"Ext Spk", NULL, "RSpeaker"},
	{"Headphone Jack", NULL, "LHeadphone"},
	{"Headphone Jack", NULL, "RHeadphone"},
};

#if 0

static struct snd_soc_jack rk29_soc_jack;



static struct snd_soc_jack_gpio odroid_soc_jack_gpio[] = {
	{
		.gpio = 28,
		.name "headset event",
		.report = SND_JACK_HEADSET,
		.debounce_time = 200,
	},
};
#endif

#if 0
static int rt5512_headset_keys(struct snd_soc_jack *jack)
{
	int err = 0;

	err = snd_jack_set_key(jack->jack, SND_JACK_BTN_0, 0x80);
	if (err)
		return err;

	err = snd_jack_set_key(jack->jack, SND_JACK_BTN_1, 0x81);
	if (err)
		return err;

	err = snd_jack_set_key(jack->jack, SND_JACK_BTN_2, 0x82);
	if (err)
		return err;

	return 0;
}
#endif

static int rt5512_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	//struct rt5512_codec_chip *chip = snd_soc_codec_get_drvdata(codec);
	//int err = 0;

	snd_soc_dapm_new_controls(dapm, rt5512_dapm_widgets,
				ARRAY_SIZE(rt5512_dapm_widgets));

	snd_soc_dapm_add_routes(dapm, rt5512_audio_map,
				ARRAY_SIZE(rt5512_audio_map));
#if FOR_MID
    snd_soc_dapm_disable_pin(dapm, "Main Mic");
	snd_soc_dapm_disable_pin(dapm, "LineIn");
	snd_soc_dapm_disable_pin(dapm, "Ext Spk");
	snd_soc_dapm_disable_pin(dapm, "Headphone Jack");
#endif

#if 0
	if (!chip->rt_jack)
	{
		err = snd_soc_jack_new(codec, "Headset Jack" , SND_JACK_HEADSET, &rk29_soc_jack);
		if (err)
			return err;

		#if 0
		// How-to use gpio, just declare snd_soc_jack_gpios, then it will
		// help you to register a interrupt and set wakeup, and delayed schedule
		// work
		err = snd_soc_jack_add_gpios(&odroid_soc_jack, gpio_count, odroid_soc_jack_gpios);
		if (err)
			return err;

		// If  use this, when trigger, just use snd_soc_jack_get_type
		// then snd_soc_jack_report to send the event to upper layer
		err = snd_soc_jack_add_zones(&odroid_soc_jack, zone_count, tcc_soc_zones);
		if (err)
			return err;
		#endif

		err = rt5512_headset_keys(&rk29_soc_jack);
		if (err)
			return err;

		chip->rt_jack = &rk29_soc_jack;
	}
#endif
	snd_soc_dapm_sync(dapm);
	return 0;
}

static struct snd_soc_dai_link rk29_dai[] = {
	{ /* Primary DAI i/f */
		.name = "RT5512 AIF1",
		.stream_name = "RT5512 PCM",
		.cpu_dai_name = "rk29_i2s.1",
		.codec_dai_name = "RT5512-aif1",
		.platform_name = "rockchip-audio",
		.codec_name = "rt5512.1-0018",
		.init = rt5512_init,
		.ops = &rk29_ops,
	},
};

static struct snd_soc_card snd_soc_card_rk29 = {
	.name = "RK29_RT5512",
	.dai_link = rk29_dai,

	/* If you want to use sec_fifo device,
	 * changes the num_link = 2 or ARRAY_SIZE(odroid_dai). */
	.num_links = ARRAY_SIZE(rk29_dai),
};

static int __init audio_card_init(void)
{
	int ret;
	rk29_snd_device = platform_device_alloc("soc-audio", -1);
	if (!rk29_snd_device)
		return -ENOMEM;

	platform_set_drvdata(rk29_snd_device, &snd_soc_card_rk29);

	ret = platform_device_add(rk29_snd_device);
	if (ret)
		platform_device_put(rk29_snd_device);

	return ret;
}
module_init(audio_card_init);

static void __exit audio_card_exit(void)
{
	platform_device_unregister(rk29_snd_device);
}
module_exit(audio_card_exit);

MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_AUTHOR("cy_huang <cy_huang@richtek.com>");
MODULE_LICENSE("GPL");
