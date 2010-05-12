/*
 * raumfeld_audio.c  --  SoC audio for Raumfeld audio devices
 *
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 *
 * based on code from:
 *
 *    Wolfson Microelectronics PLC.
 *    Openedhand Ltd.
 *    Liam Girdwood <lrg@slimlogic.co.uk>
 *    Richard Purdie <richard@openedhand.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/mach-types.h>

#include "../codecs/cs4270.h"
#include "../codecs/ak4104.h"
#include "pxa2xx-pcm.h"
#include "pxa-ssp.h"

#define GPIO_SPDIF_RESET	(38)
#define GPIO_MCLK_RESET		(111)
#define GPIO_CODEC_RESET	(120)

static struct i2c_client *max9486_client;
static struct i2c_board_info max9486_hwmon_info = {
	I2C_BOARD_INFO("max9485", 0x63),
};

#define MAX9485_MCLK_FREQ_112896 0x22
#define MAX9485_MCLK_FREQ_122880 0x23
#define MAX9485_MCLK_FREQ_225792 0x32
#define MAX9485_MCLK_FREQ_245760 0x33

static void set_max9485_clk(char clk)
{
	i2c_master_send(max9486_client, &clk, 1);
}

static void raumfeld_enable_audio(bool en)
{
	if (en) {
		gpio_set_value(GPIO_MCLK_RESET, 1);

		/* wait some time to let the clocks become stable */
		msleep(100);

		gpio_set_value(GPIO_SPDIF_RESET, 1);
		gpio_set_value(GPIO_CODEC_RESET, 1);
	} else {
		gpio_set_value(GPIO_MCLK_RESET, 0);
		gpio_set_value(GPIO_SPDIF_RESET, 0);
		gpio_set_value(GPIO_CODEC_RESET, 0);
	}
}

/* CS4270 */
static int raumfeld_cs4270_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;

	/* set freq to 0 to enable all possible codec sample rates */
	return snd_soc_dai_set_sysclk(codec_dai, 0, 0, 0);
}

static void raumfeld_cs4270_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;

	/* set freq to 0 to enable all possible codec sample rates */
	snd_soc_dai_set_sysclk(codec_dai, 0, 0, 0);
}

static int raumfeld_cs4270_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	unsigned int fmt, clk = 0;
	int ret = 0;

	switch (params_rate(params)) {
	case 44100:
		set_max9485_clk(MAX9485_MCLK_FREQ_112896);
		clk = 11289600;
		break;
	case 48000:
		set_max9485_clk(MAX9485_MCLK_FREQ_122880);
		clk = 12288000;
		break;
	case 88200:
		set_max9485_clk(MAX9485_MCLK_FREQ_225792);
		clk = 22579200;
		break;
	case 96000:
		set_max9485_clk(MAX9485_MCLK_FREQ_245760);
		clk = 24576000;
		break;
	default:
		return -EINVAL;
	}

	fmt = SND_SOC_DAIFMT_I2S |
	      SND_SOC_DAIFMT_NB_NF |
	      SND_SOC_DAIFMT_CBS_CFS;

	/* setup the CODEC DAI */
	ret = snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, clk, 0);
	if (ret < 0)
		return ret;

	/* setup the CPU DAI */
	ret = snd_soc_dai_set_pll(cpu_dai, 0, 0, 0, clk);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, PXA_SSP_DIV_SCR, 4);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(cpu_dai, PXA_SSP_CLK_EXT, clk, 1);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops raumfeld_cs4270_ops = {
	.startup = raumfeld_cs4270_startup,
	.shutdown = raumfeld_cs4270_shutdown,
	.hw_params = raumfeld_cs4270_hw_params,
};

static int raumfeld_line_suspend(struct platform_device *pdev, pm_message_t state)
{
	raumfeld_enable_audio(false);
	return 0;
}

static int raumfeld_line_resume(struct platform_device *pdev)
{
	raumfeld_enable_audio(true);
	return 0;
}

static struct snd_soc_dai_link raumfeld_line_dai = {
	.name		= "CS4270",
	.stream_name	= "CS4270",
	.cpu_dai	= &pxa_ssp_dai[PXA_DAI_SSP1],
	.codec_dai	= &cs4270_dai,
	.ops		= &raumfeld_cs4270_ops,
};

static struct snd_soc_card snd_soc_line_raumfeld = {
	.name		= "Raumfeld analog",
	.platform	= &pxa2xx_soc_platform,
	.dai_link	= &raumfeld_line_dai,
	.suspend_post	= raumfeld_line_suspend,
	.resume_pre	= raumfeld_line_resume,
	.num_links	= 1,
};


/* AK4104 */

static int raumfeld_ak4104_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	int fmt, ret = 0, clk = 0;

	switch (params_rate(params)) {
	case 44100:
		set_max9485_clk(MAX9485_MCLK_FREQ_112896);
		clk = 11289600;
		break;
	case 48000:
		set_max9485_clk(MAX9485_MCLK_FREQ_122880);
		clk = 12288000;
		break;
	case 88200:
		set_max9485_clk(MAX9485_MCLK_FREQ_225792);
		clk = 22579200;
		break;
	case 96000:
		set_max9485_clk(MAX9485_MCLK_FREQ_245760);
		clk = 24576000;
		break;
	default:
		return -EINVAL;
	}

	fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF;

	/* setup the CODEC DAI */
	ret = snd_soc_dai_set_fmt(codec_dai, fmt | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* setup the CPU DAI */
	ret = snd_soc_dai_set_pll(cpu_dai, 0, 0, 0, clk);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_fmt(cpu_dai, fmt | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, PXA_SSP_DIV_SCR, 4);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(cpu_dai, PXA_SSP_CLK_EXT, clk, 1);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops raumfeld_ak4104_ops = {
	.hw_params = raumfeld_ak4104_hw_params,
};

static struct snd_soc_dai_link raumfeld_spdif_dai = {
	.name		= "ak4104",
	.stream_name	= "Playback",
	.cpu_dai	= &pxa_ssp_dai[PXA_DAI_SSP2],
	.codec_dai	= &ak4104_dai,
	.ops		= &raumfeld_ak4104_ops,
};

static struct snd_soc_card snd_soc_spdif_raumfeld = {
	.name		= "Raumfeld S/PDIF",
	.platform	= &pxa2xx_soc_platform,
	.dai_link	= &raumfeld_spdif_dai,
	.num_links	= 1
};

/* raumfeld_audio audio subsystem */
static struct snd_soc_device raumfeld_line_devdata = {
	.card = &snd_soc_line_raumfeld,
	.codec_dev = &soc_codec_device_cs4270,
};

static struct snd_soc_device raumfeld_spdif_devdata = {
	.card = &snd_soc_spdif_raumfeld,
	.codec_dev = &soc_codec_device_ak4104,
};

static struct platform_device *raumfeld_audio_line_device;
static struct platform_device *raumfeld_audio_spdif_device;

static int __init raumfeld_audio_init(void)
{
	int ret;

	if (!machine_is_raumfeld_speaker() &&
	    !machine_is_raumfeld_connector())
		return 0;

	max9486_client = i2c_new_device(i2c_get_adapter(0),
					&max9486_hwmon_info);

	if (!max9486_client)
		return -ENOMEM;

	set_max9485_clk(MAX9485_MCLK_FREQ_122880);

	/* LINE */
	raumfeld_audio_line_device = platform_device_alloc("soc-audio", 0);
	if (!raumfeld_audio_line_device)
		return -ENOMEM;

	platform_set_drvdata(raumfeld_audio_line_device,
			     &raumfeld_line_devdata);
	raumfeld_line_devdata.dev = &raumfeld_audio_line_device->dev;
	ret = platform_device_add(raumfeld_audio_line_device);
	if (ret)
		platform_device_put(raumfeld_audio_line_device);

	/* no S/PDIF on Speakers */
	if (machine_is_raumfeld_speaker())
		return ret;

	/* S/PDIF */
	raumfeld_audio_spdif_device = platform_device_alloc("soc-audio", 1);
	if (!raumfeld_audio_spdif_device) {
		platform_device_put(raumfeld_audio_line_device);
		return -ENOMEM;
	}

	platform_set_drvdata(raumfeld_audio_spdif_device,
			     &raumfeld_spdif_devdata);
	raumfeld_spdif_devdata.dev = &raumfeld_audio_spdif_device->dev;
	ret = platform_device_add(raumfeld_audio_spdif_device);
	if (ret) {
		platform_device_put(raumfeld_audio_line_device);
		platform_device_put(raumfeld_audio_spdif_device);
	}

	raumfeld_enable_audio(true);

	return ret;
}

static void __exit raumfeld_audio_exit(void)
{
	raumfeld_enable_audio(false);

	platform_device_unregister(raumfeld_audio_line_device);

	if (machine_is_raumfeld_connector())
		platform_device_unregister(raumfeld_audio_spdif_device);

	i2c_unregister_device(max9486_client);

	gpio_free(GPIO_MCLK_RESET);
	gpio_free(GPIO_CODEC_RESET);
	gpio_free(GPIO_SPDIF_RESET);
}

module_init(raumfeld_audio_init);
module_exit(raumfeld_audio_exit);

/* Module information */
MODULE_AUTHOR("Daniel Mack <daniel@caiaq.de>");
MODULE_DESCRIPTION("Raumfeld audio SoC");
MODULE_LICENSE("GPL");
