// SPDX-License-Identifier: GPL-2.0-only
/*
 * osk5912.c  --  SoC audio for OSK 5912
 *
 * Copyright (C) 2008 Mistral Solutions
 *
 * Contact: Arun KS  <arunks@mistralsolutions.com>
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/platform_data/asoc-ti-mcbsp.h>

#include "omap-mcbsp.h"
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
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	int err;

	/* Set the codec system clock for DAC and ADC */
	err =
	    snd_soc_dai_set_sysclk(codec_dai, 0, CODEC_CLOCK, SND_SOC_CLOCK_IN);

	if (err < 0) {
		printk(KERN_ERR "can't set codec system clock\n");
		return err;
	}

	return err;
}

static const struct snd_soc_ops osk_ops = {
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

/* Digital audio interface glue - connects codec <--> CPU */
SND_SOC_DAILINK_DEFS(aic23,
	DAILINK_COMP_ARRAY(COMP_CPU("omap-mcbsp.1")),
	DAILINK_COMP_ARRAY(COMP_CODEC("tlv320aic23-codec",
				      "tlv320aic23-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("omap-mcbsp.1")));

static struct snd_soc_dai_link osk_dai = {
	.name = "TLV320AIC23",
	.stream_name = "AIC23",
	.dai_fmt = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBM_CFM,
	.ops = &osk_ops,
	SND_SOC_DAILINK_REG(aic23),
};

/* Audio machine driver */
static struct snd_soc_card snd_soc_card_osk = {
	.name = "OSK5912",
	.owner = THIS_MODULE,
	.dai_link = &osk_dai,
	.num_links = 1,

	.dapm_widgets = tlv320aic23_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tlv320aic23_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
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
		err = PTR_ERR(tlv320aic23_mclk);
		goto err2;
	}

	/*
	 * Configure 12 MHz output on MCLK.
	 */
	curRate = (uint) clk_get_rate(tlv320aic23_mclk);
	if (curRate != CODEC_CLOCK) {
		if (clk_set_rate(tlv320aic23_mclk, CODEC_CLOCK)) {
			printk(KERN_ERR "Cannot set MCLK for AIC23 CODEC\n");
			err = -ECANCELED;
			goto err3;
		}
	}

	printk(KERN_INFO "MCLK = %d [%d]\n",
	       (uint) clk_get_rate(tlv320aic23_mclk), CODEC_CLOCK);

	return 0;

err3:
	clk_put(tlv320aic23_mclk);
err2:
	platform_device_del(osk_snd_device);
err1:
	platform_device_put(osk_snd_device);

	return err;

}

static void __exit osk_soc_exit(void)
{
	clk_put(tlv320aic23_mclk);
	platform_device_unregister(osk_snd_device);
}

module_init(osk_soc_init);
module_exit(osk_soc_exit);

MODULE_AUTHOR("Arun KS <arunks@mistralsolutions.com>");
MODULE_DESCRIPTION("ALSA SoC OSK 5912");
MODULE_LICENSE("GPL");
