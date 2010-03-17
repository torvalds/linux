/*
 * osk5912.c  --  SoC audio for OSK 5912
 *
 * Copyright (C) 2008 Mistral Solutions
 *
 * Contact: Arun KS  <arunks@mistralsolutions.com>
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
#include <linux/gpio.h>
#include <plat/mcbsp.h>

#include "omap-mcbsp.h"
#include "omap-pcm.h"
#include "../codecs/tlv320aic23.h"

#define CODEC_CLOCK 	12000000

static struct clk *tlv320aic23_mclk;

static int osk_startup(struct snd_pcm_substream *substream)
{
	return clk_enable(tlv320aic23_mclk);
}

static void osk_shutdown(struct snd_pcm_substream *substream)
{
	clk_disable(tlv320aic23_mclk);
}

static int osk_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int err;

	/* Set codec DAI configuration */
	err = snd_soc_dai_set_fmt(codec_dai,
				  SND_SOC_DAIFMT_DSP_B |
				  SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM);
	if (err < 0) {
		printk(KERN_ERR "can't set codec DAI configuration\n");
		return err;
	}

	/* Set cpu DAI configuration */
	err = snd_soc_dai_set_fmt(cpu_dai,
				  SND_SOC_DAIFMT_DSP_B |
				  SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM);
	if (err < 0) {
		printk(KERN_ERR "can't set cpu DAI configuration\n");
		return err;
	}

	/* Set the codec system clock for DAC and ADC */
	err =
	    snd_soc_dai_set_sysclk(codec_dai, 0, CODEC_CLOCK, SND_SOC_CLOCK_IN);

	if (err < 0) {
		printk(KERN_ERR "can't set codec system clock\n");
		return err;
	}

	return err;
}

static struct snd_soc_ops osk_ops = {
	.startup = osk_startup,
	.hw_params = osk_hw_params,
	.shutdown = osk_shutdown,
};

static const struct snd_soc_dapm_widget tlv320aic23_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Headphone Jack", NULL, "LHPOUT"},
	{"Headphone Jack", NULL, "RHPOUT"},

	{"LLINEIN", NULL, "Line In"},
	{"RLINEIN", NULL, "Line In"},

	{"MICIN", NULL, "Mic Jack"},
};

static int osk_tlv320aic23_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;

	/* Add osk5912 specific widgets */
	snd_soc_dapm_new_controls(codec, tlv320aic23_dapm_widgets,
				  ARRAY_SIZE(tlv320aic23_dapm_widgets));

	/* Set up osk5912 specific audio path audio_map */
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_enable_pin(codec, "Headphone Jack");
	snd_soc_dapm_enable_pin(codec, "Line In");
	snd_soc_dapm_enable_pin(codec, "Mic Jack");

	snd_soc_dapm_sync(codec);

	return 0;
}

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link osk_dai = {
	.name = "TLV320AIC23",
	.stream_name = "AIC23",
	.cpu_dai_name = "omap-mcbsp-dai.0",
	.codec_dai_name = "tlv320aic23-hifi",
	.platform_name = "omap-pcm-audio",
	.codec_name = "tlv320aic23-codec",
	.init = osk_tlv320aic23_init,
	.ops = &osk_ops,
};

/* Audio machine driver */
static struct snd_soc_card snd_soc_card_osk = {
	.name = "OSK5912",
	.dai_link = &osk_dai,
	.num_links = 1,
};

static struct platform_device *osk_snd_device;

static int __init osk_soc_init(void)
{
	int err;
	u32 curRate;
	struct device *dev;

	if (!(machine_is_omap_osk()))
		return -ENODEV;

	osk_snd_device = platform_device_alloc("soc-audio", -1);
	if (!osk_snd_device)
		return -ENOMEM;

	platform_set_drvdata(osk_snd_device, &snd_soc_card_osk);
	err = platform_device_add(osk_snd_device);
	if (err)
		goto err1;

	dev = &osk_snd_device->dev;

	tlv320aic23_mclk = clk_get(dev, "mclk");
	if (IS_ERR(tlv320aic23_mclk)) {
		printk(KERN_ERR "Could not get mclk clock\n");
		return -ENODEV;
	}

	/*
	 * Configure 12 MHz output on MCLK.
	 */
	curRate = (uint) clk_get_rate(tlv320aic23_mclk);
	if (curRate != CODEC_CLOCK) {
		if (clk_set_rate(tlv320aic23_mclk, CODEC_CLOCK)) {
			printk(KERN_ERR "Cannot set MCLK for AIC23 CODEC\n");
			err = -ECANCELED;
			goto err1;
		}
	}

	printk(KERN_INFO "MCLK = %d [%d]\n",
	       (uint) clk_get_rate(tlv320aic23_mclk), CODEC_CLOCK);

	return 0;
err1:
	clk_put(tlv320aic23_mclk);
	platform_device_del(osk_snd_device);
	platform_device_put(osk_snd_device);

	return err;

}

static void __exit osk_soc_exit(void)
{
	platform_device_unregister(osk_snd_device);
}

module_init(osk_soc_init);
module_exit(osk_soc_exit);

MODULE_AUTHOR("Arun KS <arunks@mistralsolutions.com>");
MODULE_DESCRIPTION("ALSA SoC OSK 5912");
MODULE_LICENSE("GPL");
