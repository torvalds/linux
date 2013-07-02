/*
 * rk_hdmi_i2s.c  --  HDMI i2s audio for rockchip
 *
 * Copyright 2013 Rockship
 * Author: chenjq <chenjq@rock-chips.com>
 */

#include <linux/clk.h>
#include <sound/soc.h>
#include <mach/iomux.h>

#include "rk29_pcm.h"
#include "rk29_i2s.h"

#if 0
#define DBG(x...) printk(KERN_INFO "rk_hdmi_i2s:"x)
#else
#define DBG(x...) do { } while (0)
#endif


static int hdmi_i2s_hifi_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0;
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
			printk("Enter:%s, %d, Error rate=%d\n", __FUNCTION__, __LINE__, params_rate(params));
			return -EINVAL;
			break;
	}

	DBG("Enter:%s, %d, rate=%d\n", __FUNCTION__, __LINE__, params_rate(params));

	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, (pll_out/4)/params_rate(params)-1);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);

	DBG("Enter:%s, %d, pll_out/4/params_rate(params) = %d \n", __FUNCTION__, __LINE__, (pll_out/4)/params_rate(params));

	return 0;
}



static struct snd_soc_ops hdmi_i2s_hifi_ops = {
	.hw_params = hdmi_i2s_hifi_hw_params,
};

static struct snd_soc_dai_link hdmi_i2s_dai = {
	.name = "HDMI I2S",
	.stream_name = "HDMI PCM",
	.codec_name = "hdmi-i2s",
	.platform_name = "rockchip-audio",
#if defined(CONFIG_SND_RK29_SOC_I2S_8CH)
	.cpu_dai_name = "rk29_i2s.0",
#elif defined(CONFIG_SND_RK29_SOC_I2S_2CH)
	.cpu_dai_name = "rk29_i2s.1",
#endif
	.codec_dai_name = "rk-hdmi-i2s-hifi",
	.ops = &hdmi_i2s_hifi_ops,
};

static struct snd_soc_card snd_soc_card_hdmi_i2s = {
	.name = "RK-HDMI-I2S",
	.dai_link = &hdmi_i2s_dai,
	.num_links = 1,
};

static struct platform_device *hdmi_i2s_snd_device;
static struct platform_device *hdmi_i2s_device;

static int __init audio_card_init(void)
{
	int ret =0;

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	hdmi_i2s_device = platform_device_alloc("hdmi-i2s", -1);

	if (!hdmi_i2s_device){
		printk("spdif:platform_device_alloc hdmi-i2s\n");
		return -ENOMEM;
	}

	ret = platform_device_add(hdmi_i2s_device);
	if (ret) {
		printk("platform device add hdmi-i2s failed\n");

		platform_device_put(hdmi_i2s_device);
		return ret;
	}

	hdmi_i2s_snd_device = platform_device_alloc("soc-audio", -3);
	if (!hdmi_i2s_snd_device) {
		printk("platform device allocation failed\n");

		platform_device_put(hdmi_i2s_device);
		return -ENOMEM;
	}

	platform_set_drvdata(hdmi_i2s_snd_device, &snd_soc_card_hdmi_i2s);
	ret = platform_device_add(hdmi_i2s_snd_device);
	if (ret) {
		printk("platform device add soc-audio failed\n");

		platform_device_put(hdmi_i2s_device);
		platform_device_put(hdmi_i2s_snd_device);
		return ret;
	}

        return ret;
}

static void __exit audio_card_exit(void)
{
	platform_device_unregister(hdmi_i2s_snd_device);
}

late_initcall(audio_card_init);
module_exit(audio_card_exit);
/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP hdmi i2s ASoC Interface");
MODULE_LICENSE("GPL");
