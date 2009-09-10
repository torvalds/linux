/*
 * File:         sound/soc/blackfin/bf5xx-ssm2602.c
 * Author:       Cliff Cai <Cliff.Cai@analog.com>
 *
 * Created:      Tue June 06 2008
 * Description:  board driver for SSM2602 sound chip
 *
 * Modified:
 *               Copyright 2008 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm_params.h>

#include <asm/dma.h>
#include <asm/portmux.h>
#include <linux/gpio.h>
#include "../codecs/ssm2602.h"
#include "bf5xx-sport.h"
#include "bf5xx-i2s-pcm.h"
#include "bf5xx-i2s.h"

static struct snd_soc_card bf5xx_ssm2602;

static int bf5xx_ssm2602_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;

	pr_debug("%s enter\n", __func__);
	cpu_dai->private_data = sport_handle;
	return 0;
}

static int bf5xx_ssm2602_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	unsigned int clk = 0;
	int ret = 0;

	pr_debug("%s rate %d format %x\n", __func__, params_rate(params),
		params_format(params));
	/*
	 * If you are using a crystal source which frequency is not 12MHz
	 * then modify the below case statement with frequency of the crystal.
	 *
	 * If you are using the SPORT to generate clocking then this is
	 * where to do it.
	 */

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 48000:
	case 96000:
	case 11025:
	case 22050:
	case 44100:
		clk = 12000000;
		break;
	}

	/*
	 * CODEC is master for BCLK and LRC in this configuration.
	 */

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;
	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, SSM2602_SYSCLK, clk,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops bf5xx_ssm2602_ops = {
	.startup = bf5xx_ssm2602_startup,
	.hw_params = bf5xx_ssm2602_hw_params,
};

static struct snd_soc_dai_link bf5xx_ssm2602_dai = {
	.name = "ssm2602",
	.stream_name = "SSM2602",
	.cpu_dai = &bf5xx_i2s_dai,
	.codec_dai = &ssm2602_dai,
	.ops = &bf5xx_ssm2602_ops,
};

/*
 * SSM2602 2 wire address is determined by CSB
 * state during powerup.
 *    low  = 0x1a
 *    high = 0x1b
 */

static struct ssm2602_setup_data bf5xx_ssm2602_setup = {
	.i2c_bus = 0,
	.i2c_address = 0x1b,
};

static struct snd_soc_card bf5xx_ssm2602 = {
	.name = "bf5xx_ssm2602",
	.platform = &bf5xx_i2s_soc_platform,
	.dai_link = &bf5xx_ssm2602_dai,
	.num_links = 1,
};

static struct snd_soc_device bf5xx_ssm2602_snd_devdata = {
	.card = &bf5xx_ssm2602,
	.codec_dev = &soc_codec_dev_ssm2602,
	.codec_data = &bf5xx_ssm2602_setup,
};

static struct platform_device *bf5xx_ssm2602_snd_device;

static int __init bf5xx_ssm2602_init(void)
{
	int ret;

	pr_debug("%s enter\n", __func__);
	bf5xx_ssm2602_snd_device = platform_device_alloc("soc-audio", -1);
	if (!bf5xx_ssm2602_snd_device)
		return -ENOMEM;

	platform_set_drvdata(bf5xx_ssm2602_snd_device,
				&bf5xx_ssm2602_snd_devdata);
	bf5xx_ssm2602_snd_devdata.dev = &bf5xx_ssm2602_snd_device->dev;
	ret = platform_device_add(bf5xx_ssm2602_snd_device);

	if (ret)
		platform_device_put(bf5xx_ssm2602_snd_device);

	return ret;
}

static void __exit bf5xx_ssm2602_exit(void)
{
	pr_debug("%s enter\n", __func__);
	platform_device_unregister(bf5xx_ssm2602_snd_device);
}

module_init(bf5xx_ssm2602_init);
module_exit(bf5xx_ssm2602_exit);

/* Module information */
MODULE_AUTHOR("Cliff Cai");
MODULE_DESCRIPTION("ALSA SoC SSM2602 BF527-EZKIT");
MODULE_LICENSE("GPL");

