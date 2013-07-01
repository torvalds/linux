/*$_FOR_ROCKCHIP_RBOX_$*/
/*$_rbox_$_modify_$_huangzhibao for spdif output*/

/*
 * smdk_spdif.c  --  S/PDIF audio for SMDK
 *
 * Copyright 2010 Samsung Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 */

#include <linux/clk.h>

#include <sound/soc.h>

#include <mach/iomux.h>

#if 0
#define RK_SPDIF_DBG(x...) printk(KERN_INFO "rk_hdmi_spdif:"x)
#else
#define RK_SPDIF_DBG(x...) do { } while (0)
#endif


static int set_audio_clock_rate(unsigned long pll_rate,
				unsigned long audio_rate)
{
	struct clk *hclk_spdif, *sclk_spdif;

#if defined (CONFIG_ARCH_RK30) || (CONFIG_ARCH_RK3188)	
	hclk_spdif = clk_get(NULL, "hclk_spdif");
	if (IS_ERR(hclk_spdif)) {
		printk(KERN_ERR "spdif:failed to get hclk_spdif\n");
		return -ENOENT;
	}

	clk_set_rate(hclk_spdif, pll_rate);
	clk_put(hclk_spdif);
#endif

	sclk_spdif = clk_get(NULL, "spdif");
	if (IS_ERR(sclk_spdif)) {
		printk(KERN_ERR "spdif:failed to get sclk_spdif\n");
		return -ENOENT;
	}

	clk_set_rate(sclk_spdif, audio_rate);
	clk_put(sclk_spdif);

	return 0;
}

static int rk_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned long pll_out, rclk_rate;
	int ret, ratio;

  RK_SPDIF_DBG("spdif:Entered %s\n", __func__);
  
	switch (params_rate(params)) {
	case 44100:
		pll_out = 11289600;
		break;
	case 32000:
		pll_out = 8192000;
		break;
	case 48000:
		pll_out = 12288000;
		break;
	case 96000:
		pll_out = 24576000;
		break;
	default:
		printk("rk_spdif: params not support\n");
		return -EINVAL;
	}

	ratio = 256;
	rclk_rate = params_rate(params) * ratio;

	/* Set audio source clock rates */
	ret = set_audio_clock_rate(pll_out, rclk_rate);
	if (ret < 0)
		return ret;

	/* Set S/PDIF uses internal source clock */
	//ret = snd_soc_dai_set_sysclk(cpu_dai, SND_SOC_SPDIF_INT_MCLK,
					//rclk_rate, SND_SOC_CLOCK_IN);
	//if (ret < 0)
		//return ret;

	return ret;
}

static struct snd_soc_ops rk_spdif_ops = {
	.hw_params = rk_hw_params,
};

static struct snd_soc_dai_link rk_dai = {
	.name = "SPDIF",
	.stream_name = "SPDIF PCM Playback",
	.platform_name = "rockchip-audio",
	.cpu_dai_name = "rk-spdif.0",
	.codec_dai_name = "dit-hifi",
	.codec_name = "spdif-dit",
	.ops = &rk_spdif_ops,
};

static struct snd_soc_card rk_spdif = {
	.name = "ROCKCHIP-SPDIF",
	.dai_link = &rk_dai,
	.num_links = 1,
};

static struct platform_device *rk_snd_spdif_dit_device;
static struct platform_device *rk_snd_spdif_device;

static int __init rk_spdif_init(void)
{
	int ret;
	
	RK_SPDIF_DBG("Entered %s\n", __func__);
	
	rk_snd_spdif_dit_device = platform_device_alloc("spdif-dit", -1);
	if (!rk_snd_spdif_dit_device){
		printk("spdif:platform_device_alloc spdif-dit\n");
		return -ENOMEM;
	}

	ret = platform_device_add(rk_snd_spdif_dit_device);
	if (ret)
		goto err1;

	rk_snd_spdif_device = platform_device_alloc("soc-audio", -3);
	if (!rk_snd_spdif_device) {
		printk("spdif:platform_device_alloc rk_soc-audio\n");
		ret = -ENOMEM;
		goto err2;
	}
	
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	platform_set_drvdata(rk_snd_spdif_device, &rk_spdif);
#else
	platform_set_drvdata(rk_snd_spdif_device, &rk_spdif);
	rk_spdif.dev = &rk_snd_spdif_device->dev;
#endif

	//platform_set_drvdata(rk_snd_spdif_device, &rk_spdif);

	ret = platform_device_add(rk_snd_spdif_device);
	if (ret)
		goto err3;
	
	RK_SPDIF_DBG("rk_spdif_init ok\n");
	return ret;
err3:
	platform_device_put(rk_snd_spdif_device);
err2:
	platform_device_del(rk_snd_spdif_dit_device);
err1:
	platform_device_put(rk_snd_spdif_dit_device);
	
	return ret;
}

static void __exit rk_spdif_exit(void)
{
	platform_device_unregister(rk_snd_spdif_device);
	platform_device_unregister(rk_snd_spdif_dit_device);
}

//using late_initcall to make sure spdif is after board codec. added by zxg.
//module_init(rk_spdif_init);
late_initcall(rk_spdif_init);
module_exit(rk_spdif_exit);

MODULE_AUTHOR("hzb, <hzb@rock-chips.com>");
MODULE_DESCRIPTION("ALSA SoC RK+S/PDIF");
MODULE_LICENSE("GPL");
