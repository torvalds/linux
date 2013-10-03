/*
 * File:         sound/soc/blackfin/bf5xx-ad1980.c
 * Author:       Cliff Cai <Cliff.Cai@analog.com>
 *
 * Created:      Tue June 06 2008
 * Description:  Board driver for AD1980/1 audio codec
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

/*
 * WARNING:
 *
 * Because Analog Devices Inc. discontinued the ad1980 sound chip since
 * Sep. 2009, this ad1980 driver is not maintained, tested and supported
 * by ADI now.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <asm/dma.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <linux/gpio.h>
#include <asm/portmux.h>

#include "../codecs/ad1980.h"

#include "bf5xx-ac97.h"

static struct snd_soc_card bf5xx_board;

static struct snd_soc_dai_link bf5xx_board_dai[] = {
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		.cpu_dai_name = "bfin-ac97.0",
		.codec_dai_name = "ad1980-hifi",
		.platform_name = "bfin-ac97-pcm-audio",
		.codec_name = "ad1980",
	},
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		.cpu_dai_name = "bfin-ac97.1",
		.codec_dai_name = "ad1980-hifi",
		.platform_name = "bfin-ac97-pcm-audio",
		.codec_name = "ad1980",
	},
};

static struct snd_soc_card bf5xx_board = {
	.name = "bfin-ad1980",
	.owner = THIS_MODULE,
	.dai_link = &bf5xx_board_dai[CONFIG_SND_BF5XX_SPORT_NUM],
	.num_links = 1,
};

static struct platform_device *bf5xx_board_snd_device;

static int __init bf5xx_board_init(void)
{
	int ret;

	bf5xx_board_snd_device = platform_device_alloc("soc-audio", -1);
	if (!bf5xx_board_snd_device)
		return -ENOMEM;

	platform_set_drvdata(bf5xx_board_snd_device, &bf5xx_board);
	ret = platform_device_add(bf5xx_board_snd_device);

	if (ret)
		platform_device_put(bf5xx_board_snd_device);

	return ret;
}

static void __exit bf5xx_board_exit(void)
{
	platform_device_unregister(bf5xx_board_snd_device);
}

module_init(bf5xx_board_init);
module_exit(bf5xx_board_exit);

/* Module information */
MODULE_AUTHOR("Cliff Cai");
MODULE_DESCRIPTION("ALSA SoC AD1980/1 BF5xx board (Obsolete)");
MODULE_LICENSE("GPL");
