/*
 * omap2evm.c  --  SoC audio machine driver for omap2evm board
 *
 * Author: Arun KS <arunks@mistralsolutions.com>
 *
 * Based on sound/soc/omap/overo.c by Steve Sakoman
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
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <mach/gpio.h>
#include <plat/mcbsp.h>

#include "omap-mcbsp.h"
#include "omap-pcm.h"

static int omap2evm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;

	/* Set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai,
				  SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0) {
		printk(KERN_ERR "can't set codec DAI configuration\n");
		return ret;
	}

	/* Set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai,
				  SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0) {
		printk(KERN_ERR "can't set cpu DAI configuration\n");
		return ret;
	}

	/* Set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, 26000000,
					    SND_SOC_CLOCK_IN);
	if (ret < 0) {
		printk(KERN_ERR "can't set codec system clock\n");
		return ret;
	}

	return 0;
}

static struct snd_soc_ops omap2evm_ops = {
	.hw_params = omap2evm_hw_params,
};

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link omap2evm_dai = {
	.name = "TWL4030",
	.stream_name = "TWL4030",
	.cpu_dai_name = "omap-mcbsp-dai.1",
	.codec_dai_name = "twl4030-hifi",
	.platform_name = "omap-pcm-audio",
	.codec_name = "twl4030-codec",
	.ops = &omap2evm_ops,
};

/* Audio machine driver */
static struct snd_soc_card snd_soc_omap2evm = {
	.name = "omap2evm",
	.dai_link = &omap2evm_dai,
	.num_links = 1,
};

static struct platform_device *omap2evm_snd_device;

static int __init omap2evm_soc_init(void)
{
	int ret;

	if (!machine_is_omap2evm()) {
		pr_debug("Not omap2evm!\n");
		return -ENODEV;
	}
	printk(KERN_INFO "omap2evm SoC init\n");

	omap2evm_snd_device = platform_device_alloc("soc-audio", -1);
	if (!omap2evm_snd_device) {
		printk(KERN_ERR "Platform device allocation failed\n");
		return -ENOMEM;
	}

	platform_set_drvdata(omap2evm_snd_device, &snd_soc_omap2evm);

	ret = platform_device_add(omap2evm_snd_device);
	if (ret)
		goto err1;

	return 0;

err1:
	printk(KERN_ERR "Unable to add platform device\n");
	platform_device_put(omap2evm_snd_device);

	return ret;
}
module_init(omap2evm_soc_init);

static void __exit omap2evm_soc_exit(void)
{
	platform_device_unregister(omap2evm_snd_device);
}
module_exit(omap2evm_soc_exit);

MODULE_AUTHOR("Arun KS <arunks@mistralsolutions.com>");
MODULE_DESCRIPTION("ALSA SoC omap2evm");
MODULE_LICENSE("GPL");
