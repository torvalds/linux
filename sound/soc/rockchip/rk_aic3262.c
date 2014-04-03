/*
 * rk29_aic3262.c  --  SoC audio for rockchip
 *
 * Driver for rockchip aic3262 audio
 *  Copyright (C) 2009 lhh
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *
 */

#define DEBUG 1
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/i2c/twl.h>
#include <linux/regulator/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>
#include <linux/switch.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#include <asm/mach-types.h>
#include <linux/module.h>
#include <linux/device.h>

#include "../codecs/wm8994.h"
#include "card_info.h"
#include "rk_pcm.h"
#include "rk_i2s.h"
#include <linux/clk.h>
#include <linux/mfd/tlv320aic3262-registers.h>
#include "../codecs/tlv320aic326x.h"

#if 0
#define	DBG_AIC3262(x...)	printk(KERN_INFO x)
#else
#define	DBG_AIC3262(x...)
#endif

//struct regulator *vddhf_reg=NULL;

/* Headset jack */
//static struct snd_soc_jack hs_jack;

/*Headset jack detection DAPM pins */
/*static struct snd_soc_jack_pin hs_jack_pins[] = {
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Headset Stereophone",
		.mask = SND_JACK_HEADPHONE,
	},
};

static int spk_event(struct snd_soc_dapm_widget *w,
                struct snd_kcontrol *kcontrol, int event)
{
        //struct snd_soc_codec *codec = w->codec;
        int ret;
        if (SND_SOC_DAPM_EVENT_ON(event)) {

			printk(" I am NULL is %d event is %d\n",vddhf_reg,event);

			if (vddhf_reg) {
			    ret = regulator_enable(vddhf_reg);
			    if(ret) {
			            printk("failed to enable vddhf \n");
			            return ret;
			    }
			}
        }
        else {

            if (vddhf_reg) {
                ret = regulator_disable(vddhf_reg);
                if (ret) {
                        printk("failed to disable "
                                "VDDHF regulator %d\n", ret);
                        return ret;
                }
            }
        }
        return 0;
}*/



/* rk29 machine DAPM */
static const struct snd_soc_dapm_widget rk29_aic3262_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Ext Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_HP("Headset Stereophone", NULL),
	SND_SOC_DAPM_SPK("Earphone Spk", NULL),
	SND_SOC_DAPM_INPUT("FM Stereo In"),
	SND_SOC_DAPM_LINE("FM Stereo Out",NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* External Mics: MAINMIC, SUBMIC with bias*/
	{"IN1L", NULL, "Mic Bias Int"},
	{"IN1R", NULL, "Mic Bias Int"},
	{"IN4L", NULL, "Mic Bias Int"},
	{"IN4R", NULL, "Mic Bias Int"},
	{"Mic Bias Int", NULL, "Ext Mic"},

	/* External Speakers: HFL, HFR */
	{"Ext Spk", NULL, "SPKL"},
	{"Ext Spk", NULL, "SPKR"},

	/* Headset Mic: HSMIC with bias */
	{"IN2L", NULL, "Mic Bias Ext"},
	{"IN2R", NULL, "Mic Bias Ext"},
	{"Mic Bias Ext", NULL, "Headset Mic"},

	/* Headset Stereophone (Headphone): HPL, HPR */
	{"Headset Stereophone", NULL, "HPL"},
	{"Headset Stereophone", NULL, "HPR"},

	/* Earphone speaker */
	{"Earphone Spk", NULL, "RECP"},
	{"Earphone Spk", NULL, "RECM"},

	/* Aux/FM Stereo In: IN4L, IN4R */
	{"IN3L", NULL, "FM Stereo In"},
	{"IN3R", NULL, "FM Stereo In"},
	
	/* Aux/FM Stereo Out: LOL, LOR */
	{"FM Stereo Out", NULL, "LOL"},
	{"FM Stereo Out", NULL, "LOR"},
};

static const struct snd_kcontrol_new rk29_aic326x_controls[] = {
	SOC_DAPM_PIN_SWITCH("Ext Mic"),
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Headset Stereophone"),
	SOC_DAPM_PIN_SWITCH("Earphone Spk"),
	SOC_DAPM_PIN_SWITCH("FM Stereo In"),
	SOC_DAPM_PIN_SWITCH("FM Stereo Out"),
};

static int rk29_aic3262_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret;

	DBG_AIC3262("rk29_aic3262_init\n");

	/* Headset jack detection */
	/*ret = snd_soc_jack_new(codec, "Headset Jack",
				SND_JACK_HEADSET, &hs_jack);
	if (ret)
		return ret;

    ret = snd_soc_jack_add_pins(&hs_jack, ARRAY_SIZE(hs_jack_pins),
                            hs_jack_pins);  
    aic3262_hs_jack_detect(codec, &hs_jack, SND_JACK_HEADSET);*/
       
    /* don't wait before switching of HS power */
	rtd->pmdown_time = 0;
	return ret;
}

static int rk29_aif1_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0, dai_fmt = rtd->dai_link->dai_fmt;
	int div_bclk,div_mclk;
	int ret;

	DBG("Enter::%s----%d\n", __FUNCTION__, __LINE__);

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, dai_fmt);
	if (ret < 0) {
		printk("%s():failed to set the format for codec side\n", __FUNCTION__);
		return ret;
	}

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, dai_fmt);
	if (ret < 0) {
		printk("%s():failed to set the format for cpu side\n", __FUNCTION__);
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
			DBG_AIC3262("Enter:%s, %d, Error rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
			return -EINVAL;
	}

 
	div_bclk=(pll_out/4)/params_rate(params)-1;
	div_mclk=3;
	
	DBG_AIC3262(" %s, pll_out=%d, div_bclk=%d, div_mclk=%d\n",__FUNCTION__,pll_out,div_bclk,div_mclk);
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	if(ret < 0)
	{
		DBG_AIC3262("rk29_hw_params_aic3262:failed to set the cpu sysclk for codec side\n"); 
		return ret;
	}
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, div_bclk);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, div_mclk);
	DBG_AIC3262("Enter:%s, %d, LRCK=%d\n",__FUNCTION__,__LINE__,(pll_out/4)/params_rate(params));

	//MCLK == 11289600 or 12288000
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, pll_out, 0);
	if (ret < 0) {
		DBG_AIC3262("rk29_hw_params_aic3262:failed to set the sysclk for codec side\n"); 
		return ret;
	}
	
	return ret;
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

	DBG("Enter:%s, %d, rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_LEFT_J |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
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
		
	div_bclk=(pll_out/4)/params_rate(params)-1;
	div_mclk=3;
	
	DBG_AIC3262(" %s, pll_out=%d, div_bclk=%d, div_mclk=%d\n",__FUNCTION__,pll_out,div_bclk,div_mclk);
	
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	if(ret < 0)
	{
		DBG("rk29_hw_params_aic3262:failed to set the cpu sysclk for codec side\n"); 
		return ret;
	}
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, div_bclk);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, div_mclk);
	DBG("Enter:%s, %d, LRCK=%d\n",__FUNCTION__,__LINE__,(pll_out/4)/params_rate(params));

	/* set the codec system clock */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, pll_out, 0);
	if (ret < 0)
	{
		printk("%s: snd_soc_dai_set_sysclk err =%d\n",__FUNCTION__,ret);
		return ret;
	}

	/* set the codec FLL */
	ret = snd_soc_dai_set_pll(codec_dai, 0, AIC3262_PLL_CLKIN_MCLK1 , pll_out, 8000*256);
	if (ret < 0)
	{
		printk("%s: snd_soc_dai_set_pll err =%d\n",__FUNCTION__,ret);
		return ret;
	}

	return ret;
}

static int rk29_aif3_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0; 
	int div_bclk,div_mclk;
	int ret;	

	DBG("Enter:%s, %d, rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_LEFT_J |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
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
		
	div_bclk=(pll_out/4)/params_rate(params)-1;
	div_mclk=3;
	
	DBG_AIC3262(" %s, pll_out=%d, div_bclk=%d, div_mclk=%d\n",__FUNCTION__,pll_out,div_bclk,div_mclk);
	
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	if(ret < 0)
	{
		DBG("rk29_hw_params_aic3262:failed to set the cpu sysclk for codec side\n"); 
		return ret;
	}
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, div_bclk);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, div_mclk);
	DBG("Enter:%s, %d, LRCK=%d\n",__FUNCTION__,__LINE__,(pll_out/4)/params_rate(params));

	/* set the codec system clock */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, pll_out, 0);
	if (ret < 0)
	{
		printk("%s: snd_soc_dai_set_sysclk err =%d\n",__FUNCTION__,ret);
		return ret;
	}

	/* set the codec FLL */
	ret = snd_soc_dai_set_pll(codec_dai, 0, AIC3262_PLL_CLKIN_MCLK1 , pll_out, 8000*256);
	if (ret < 0)
	{
		printk("%s: snd_soc_dai_set_pll err =%d\n",__FUNCTION__,ret);
		return ret;
	}

	return ret;
}

static struct snd_soc_ops rk29_aif1_ops = {
	  .hw_params = rk29_aif1_hw_params,
};

static struct snd_soc_ops rk29_aif2_ops = {
	  .hw_params = rk29_aif2_hw_params,
};

static struct snd_soc_ops rk29_aif3_ops = {
	  .hw_params = rk29_aif3_hw_params,
};

static struct snd_soc_dai_link rk29_dai[] = {

	{
		.name = "AIC3262 I2S1",
		.stream_name = "AIC3262 PCM",
		.codec_dai_name = "aic326x-asi1",
		.ops = &rk29_aif1_ops,
		.init = rk29_aic3262_init,
	},
	
	{
		.name = "AIC3262 I2S2",
		.stream_name = "AIC3262 PCM",
		.codec_dai_name = "aic326x-asi2",
		.ops = &rk29_aif2_ops,
	},

	
	{
		.name = "AIC3262 I2S3",
		.stream_name = "AIC3262 PCM",
		.codec_dai_name = "aic326x-asi3",
		.ops = &rk29_aif3_ops,
	},

};


static struct snd_soc_card rockchip_aic3262_snd_card = {
	.name = "RK_AIC3262",
	.dai_link = rk29_dai,
	.num_links = ARRAY_SIZE(rk29_dai),
	.controls = rk29_aic326x_controls,
	.num_controls = ARRAY_SIZE(rk29_aic326x_controls),
	.dapm_widgets    = rk29_aic3262_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rk29_aic3262_dapm_widgets),
	.dapm_routes    = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static int rockchip_aic3262_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_aic3262_snd_card;

	card->dev = &pdev->dev;

	ret = rockchip_of_get_sound_card_info(card);
	if (ret) {
		printk("%s() get sound card info failed:%d\n", __FUNCTION__, ret);
		return ret;
	}

	ret = snd_soc_register_card(card);
	if (ret)
		printk("%s() register card failed:%d\n", __FUNCTION__, ret);

	return ret;
}

static int rockchip_aic3262_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_aic3262_of_match[] = {
	{ .compatible = "rockchip-aic3262", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_aic3262_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_aic3262_audio_driver = {
	.driver         = {
		.name   = "rockchip-aic3262",
		.owner  = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(rockchip_aic3262_of_match),
	},
	.probe          = rockchip_aic3262_audio_probe,
	.remove         = rockchip_aic3262_audio_remove,
};

module_platform_driver(rockchip_aic3262_audio_driver);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");

