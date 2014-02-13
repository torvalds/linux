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
#include <linux/of.h>
#include <linux/of_gpio.h>

#include "../codecs/rt3261.h"
#include "rk_pcm.h"
#include "rk_i2s.h"

#if 0
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif

static int rockchip_rt3261_hifi_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0;
	int ret;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);    

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
	snd_soc_dai_set_pll(codec_dai, 0, RT3261_PLL1_S_MCLK, pll_out, pll_out*2); //bard 8-29
	ret = snd_soc_dai_set_sysclk(codec_dai, RT3261_SCLK_S_PLL1, pll_out*2, SND_SOC_CLOCK_IN); //bard 8-29
	if (ret < 0)
	{
		DBG("rk29_hw_params_rt3261:failed to set the sysclk for codec side\n"); 
		return ret;
	}

	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, (pll_out/4)/params_rate(params)-1);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);// 256k = 48-1  3M=3

	DBG("Enter:%s, %d, pll_out/4/params_rate(params) = %d \n", __FUNCTION__, __LINE__, (pll_out/4)/params_rate(params));
 
	return 0;
}

static int rockchip_rt3261_voice_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0;
	int ret;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);    
       
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_DSP_A |
		SND_SOC_DAIFMT_IB_NF | SND_SOC_DAIFMT_CBS_CFS ); 

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
	snd_soc_dai_set_pll(codec_dai, 0, RT3261_PLL1_S_MCLK, pll_out, pll_out*2); //bard 8-29
	ret = snd_soc_dai_set_sysclk(codec_dai, RT3261_SCLK_S_PLL1, pll_out*2, SND_SOC_CLOCK_IN); //bard 8-29


	if (ret < 0) {
		printk("rk29_hw_params_rt3261:failed to set the sysclk for codec side\n"); 
		return ret;
	}

	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	//snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, (pll_out/4)/params_rate(params)-1);
	//snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);

	DBG("Enter:%s, %d, pll_out/4/params_rate(params) = %d \n", __FUNCTION__, __LINE__, (pll_out/4)/params_rate(params));
 
	return 0;
}

static const struct snd_soc_dapm_widget rockchip_rt3261_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Jack", NULL),	
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
};

static const struct snd_soc_dapm_route audio_map[]={

	/* Mic Jack --> MIC_IN*/
	{"micbias1", NULL, "Mic Jack"},
	{"MIC1", NULL, "micbias1"},
	
	// HP MIC
	{"micbias1", NULL, "Headset Jack"},
	{"MIC3", NULL, "micbias1"},

	{"Ext Spk", NULL, "SPOLP"},
   	{"Ext Spk", NULL, "SPOLN"},
   	{"Ext Spk", NULL, "SPORP"},
      	{"Ext Spk", NULL, "SPORN"},

	{"Headphone Jack", NULL, "HPOL"},
	{"Headphone Jack", NULL, "HPOR"},
} ;

static const struct snd_kcontrol_new rockchip_rt3261_controls[] = {
	SOC_DAPM_PIN_SWITCH("Mic Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Jack"),
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
};

/*
 * Logic for a rt3261 as connected on a rockchip board.
 */
static int rockchip_rt3261_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	snd_soc_add_codec_controls(codec, rockchip_rt3261_controls,
			ARRAY_SIZE(rockchip_rt3261_controls));

	/* Add specific widgets */
	snd_soc_dapm_new_controls(dapm, rockchip_rt3261_dapm_widgets,
				  ARRAY_SIZE(rockchip_rt3261_dapm_widgets));
	/* Set up specific audio path audio_mapnects */
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_enable_pin(dapm, "Mic Jack");
	snd_soc_dapm_enable_pin(dapm, "Headset Jack");
	snd_soc_dapm_enable_pin(dapm, "Ext Spk");
	snd_soc_dapm_enable_pin(dapm, "Headphone Jack");
#ifdef CONFIG_HDMI
	extern int hdmi_is_insert(void);
	extern void codec_set_spk(bool on);         
	if(hdmi_is_insert())                 
		rt3261_codec_set_spk(false);
#endif

#ifdef CONFIG_HDMI_RK30
	extern int hdmi_get_hotplug(void);
	if(hdmi_get_hotplug() == 2/*HDMI_HPD_ACTIVED*/)
		rt3261_codec_set_spk(false);
#endif

	snd_soc_dapm_sync(dapm);

	return 0;
}

static struct snd_soc_ops rockchip_rt3261_hifi_ops = {
	.hw_params = rockchip_rt3261_hifi_hw_params,
};

static struct snd_soc_ops rockchip_rt3261_voice_ops = {
	.hw_params = rockchip_rt3261_voice_hw_params,
};

static struct snd_soc_dai_link rockchip_rt3261_dai[] = {
	{
		.name = "RT3261 I2S1",
		.stream_name = "RT3261 PCM",
		.codec_name = "rt3261.0-001c",
		#if defined(CONFIG_SND_RK_SOC_I2S_8CH)
			.cpu_dai_name = "rockchip-i2s.0",
		#elif defined(CONFIG_SND_RK_SOC_I2S_2CH)
			.cpu_dai_name = "rockchip-i2s.1",
		#endif
		.codec_dai_name = "rt3261-aif1",
		.init = rockchip_rt3261_init,
		.ops = &rockchip_rt3261_hifi_ops,
#if defined (CONFIG_SND_RK_CODEC_SOC_MASTER)
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBM_CFM,
#else
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
#endif
	},
	{
		.name = "RT3261 I2S2",
		.stream_name = "RT3261 PCM",
		.codec_name = "rt3261.0-001c",
		#if defined(CONFIG_SND_RK_SOC_I2S_8CH)
			.cpu_dai_name = "rockchip-i2s.0",
		#elif defined(CONFIG_SND_RK_SOC_I2S_2CH)
			.cpu_dai_name = "rockchip-i2s.1",
		#endif 
		.codec_dai_name = "rt3261-aif2",
		.ops = &rockchip_rt3261_voice_ops,
		.no_pcm = 1;
	},
};

static struct snd_soc_card rockchip_rt3261_snd_card = {
	#if defined (CONFIG_SND_SOC_RT3224)
	.name = "RK_RT3224",
	#else
	.name = "RK_RT3261",
	#endif
	.owner = THIS_MODULE,
	.dai_link = rockchip_rt3261_dai,
	.num_links = ARRAY_SIZE(rockchip_rt3261_dai),
};

static int rockchip_rt3261_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_rt3261_snd_card;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);

	if (ret)
		printk("%s() register card failed:%d\n", __FUNCTION__, ret);

	return ret;
}

static int rockchip_rt3261_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_rt3261_of_match[] = {
        { .compatible = "rockchip-rt3261", },
        {},
};
MODULE_DEVICE_TABLE(of, rockchip_rt3261_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_rt3261_audio_driver = {
        .driver         = {
                .name   = "rockchip-rt3261",
                .owner  = THIS_MODULE,
                .of_match_table = of_match_ptr(rockchip_rt3261_of_match),
        },
        .probe          = rockchip_rt3261_audio_probe,
        .remove         = rockchip_rt3261_audio_remove,
};

module_platform_driver(rockchip_rt3261_audio_driver);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");
