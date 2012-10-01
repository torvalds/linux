/*
 * overo.c  --  SoC audio for Gumstix Overo
 *
 * Author: Steve Sakoman <steve@sakoman.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <mach/gpio.h>
#include <linux/platform_data/asoc-ti-mcbsp.h>

#include "omap-mcbsp.h"
#include "omap-pcm.h"

static int overo_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	/* Set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, 26000000,
					    SND_SOC_CLOCK_IN);
	if (ret < 0) {
		printk(KERN_ERR "can't set codec system clock\n");
		return ret;
	}

	return 0;
}

static struct snd_soc_ops overo_ops = {
	.hw_params = overo_hw_params,
};

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link overo_dai = {
	.name = "TWL4030",
	.stream_name = "TWL4030",
	.cpu_dai_name = "omap-mcbsp.2",
	.codec_dai_name = "twl4030-hifi",
	.platform_name = "omap-pcm-audio",
	.codec_name = "twl4030-codec",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBM_CFM,
	.ops = &overo_ops,
};

/* Audio machine driver */
static struct snd_soc_card snd_soc_card_overo = {
	.name = "overo",
	.owner = THIS_MODULE,
	.dai_link = &overo_dai,
	.num_links = 1,
};

static struct platform_device *overo_snd_device;

static int __init overo_soc_init(void)
{
	int ret;

	if (!(machine_is_overo() || machine_is_cm_t35())) {
		pr_debug("Incomatible machine!\n");
		return -ENODEV;
	}
	printk(KERN_INFO "overo SoC init\n");

	overo_snd_device = platform_device_alloc("soc-audio", -1);
	if (!overo_snd_device) {
		printk(KERN_ERR "Platform device allocation failed\n");
		return -ENOMEM;
	}

	platform_set_drvdata(overo_snd_device, &snd_soc_card_overo);

	ret = platform_device_add(overo_snd_device);
	if (ret)
		goto err1;

	return 0;

err1:
	printk(KERN_ERR "Unable to add platform device\n");
	platform_device_put(overo_snd_device);

	return ret;
}
module_init(overo_soc_init);

static void __exit overo_soc_exit(void)
{
	platform_device_unregister(overo_snd_device);
}
module_exit(overo_soc_exit);

MODULE_AUTHOR("Steve Sakoman <steve@sakoman.com>");
MODULE_DESCRIPTION("ALSA SoC overo");
MODULE_LICENSE("GPL");
