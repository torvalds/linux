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
#include <sound/soc-dapm.h>
#include <sound/pcm_params.h>

#include <asm/blackfin.h>
#include <asm/cacheflush.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/portmux.h>

#include "../codecs/ad193x.h"
#include "bf5xx-sport.h"

#include "bf5xx-tdm-pcm.h"
#include "bf5xx-tdm.h"

static struct snd_soc_card bf5xx_ad193x;

static int bf5xx_ad193x_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;

	cpu_dai->private_data = sport_handle;
	return 0;
}

static int bf5xx_ad193x_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	unsigned int channel_map[] = {0, 1, 2, 3, 4, 5, 6, 7};
	int ret = 0;
	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_DSP_A |
		SND_SOC_DAIFMT_IB_IF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_DSP_A |
		SND_SOC_DAIFMT_IB_IF | SND_SOC_DAIFMT_CBM_CFM);
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

static struct snd_soc_ops bf5xx_ad193x_ops = {
	.startup = bf5xx_ad193x_startup,
	.hw_params = bf5xx_ad193x_hw_params,
};

static struct snd_soc_dai_link bf5xx_ad193x_dai = {
	.name = "ad193x",
	.stream_name = "AD193X",
	.cpu_dai = &bf5xx_tdm_dai,
	.codec_dai = &ad193x_dai,
	.ops = &bf5xx_ad193x_ops,
};

static struct snd_soc_card bf5xx_ad193x = {
	.name = "bf5xx_ad193x",
	.platform = &bf5xx_tdm_soc_platform,
	.dai_link = &bf5xx_ad193x_dai,
	.num_links = 1,
};

static struct snd_soc_device bf5xx_ad193x_snd_devdata = {
	.card = &bf5xx_ad193x,
	.codec_dev = &soc_codec_dev_ad193x,
};

static struct platform_device *bfxx_ad193x_snd_device;

static int __init bf5xx_ad193x_init(void)
{
	int ret;

	bfxx_ad193x_snd_device = platform_device_alloc("soc-audio", -1);
	if (!bfxx_ad193x_snd_device)
		return -ENOMEM;

	platform_set_drvdata(bfxx_ad193x_snd_device, &bf5xx_ad193x_snd_devdata);
	bf5xx_ad193x_snd_devdata.dev = &bfxx_ad193x_snd_device->dev;
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

