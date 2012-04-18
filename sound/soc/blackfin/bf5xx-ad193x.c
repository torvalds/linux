/*
 * File:         sound/soc/blackfin/bf5xx-ad193x.c
 * Author:       Barry Song <Barry.Song@analog.com>
 *
 * Created:      Thur June 4 2009
 * Description:  Board driver for ad193x sound chip
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
#include <sound/pcm_params.h>

#include <asm/blackfin.h>
#include <asm/cacheflush.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/portmux.h>

#include "../codecs/ad193x.h"

#include "bf5xx-tdm-pcm.h"
#include "bf5xx-tdm.h"

static struct snd_soc_card bf5xx_ad193x;

static int bf5xx_ad193x_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	unsigned int clk = 0;
	unsigned int channel_map[] = {0, 1, 2, 3, 4, 5, 6, 7};
	int ret = 0;

	switch (params_rate(params)) {
	case 48000:
		clk = 24576000;
		break;
	}

	/* set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, clk,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* set codec DAI slots, 8 channels, all channels are enabled */
	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0xFF, 0xFF, 8, 32);
	if (ret < 0)
		return ret;

	/* set cpu DAI channel mapping */
	ret = snd_soc_dai_set_channel_map(cpu_dai, ARRAY_SIZE(channel_map),
		channel_map, ARRAY_SIZE(channel_map), channel_map);
	if (ret < 0)
		return ret;

	return 0;
}

#define BF5XX_AD193X_DAIFMT (SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_IB_IF | \
				SND_SOC_DAIFMT_CBM_CFM)

static struct snd_soc_ops bf5xx_ad193x_ops = {
	.hw_params = bf5xx_ad193x_hw_params,
};

static struct snd_soc_dai_link bf5xx_ad193x_dai[] = {
	{
		.name = "ad193x",
		.stream_name = "AD193X",
		.cpu_dai_name = "bfin-tdm.0",
		.codec_dai_name ="ad193x-hifi",
		.platform_name = "bfin-tdm-pcm-audio",
		.codec_name = "spi0.5",
		.ops = &bf5xx_ad193x_ops,
		.dai_fmt = BF5XX_AD193X_DAIFMT,
	},
	{
		.name = "ad193x",
		.stream_name = "AD193X",
		.cpu_dai_name = "bfin-tdm.1",
		.codec_dai_name ="ad193x-hifi",
		.platform_name = "bfin-tdm-pcm-audio",
		.codec_name = "spi0.5",
		.ops = &bf5xx_ad193x_ops,
		.dai_fmt = BF5XX_AD193X_DAIFMT,
	},
};

static struct snd_soc_card bf5xx_ad193x = {
	.name = "bfin-ad193x",
	.owner = THIS_MODULE,
	.dai_link = &bf5xx_ad193x_dai[CONFIG_SND_BF5XX_SPORT_NUM],
	.num_links = 1,
};

static struct platform_device *bfxx_ad193x_snd_device;

static int __init bf5xx_ad193x_init(void)
{
	int ret;

	bfxx_ad193x_snd_device = platform_device_alloc("soc-audio", -1);
	if (!bfxx_ad193x_snd_device)
		return -ENOMEM;

	platform_set_drvdata(bfxx_ad193x_snd_device, &bf5xx_ad193x);
	ret = platform_device_add(bfxx_ad193x_snd_device);

	if (ret)
		platform_device_put(bfxx_ad193x_snd_device);

	return ret;
}

static void __exit bf5xx_ad193x_exit(void)
{
	platform_device_unregister(bfxx_ad193x_snd_device);
}

module_init(bf5xx_ad193x_init);
module_exit(bf5xx_ad193x_exit);

/* Module information */
MODULE_AUTHOR("Barry Song");
MODULE_DESCRIPTION("ALSA SoC AD193X board driver");
MODULE_LICENSE("GPL");

