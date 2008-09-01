/*
 * eti_b1_wm8731  --  SoC audio for AT91RM9200-based Endrelia ETI_B1 board.
 *
 * Author:	Frank Mandarino <fmandarino@endrelia.com>
 *		Endrelia Technologies Inc.
 * Created:	Mar 29, 2006
 *
 * Based on corgi.c by:
 *
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Copyright 2005 Openedhand Ltd.
 *
 * Authors: Liam Girdwood <liam.girdwood@wolfsonmicro.com>
 *          Richard Purdie <richard@openedhand.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <mach/hardware.h>
#include <mach/gpio.h>

#include "../codecs/wm8731.h"
#include "at91-pcm.h"
#include "at91-ssc.h"

#if 0
#define	DBG(x...)	printk(KERN_INFO "eti_b1_wm8731: " x)
#else
#define	DBG(x...)
#endif

static struct clk *pck1_clk;
static struct clk *pllb_clk;


static int eti_b1_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	int ret;

	/* cpu clock is the AT91 master clock sent to the SSC */
	ret = snd_soc_dai_set_sysclk(cpu_dai, AT91_SYSCLK_MCK,
		60000000, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* codec system clock is supplied by PCK1, set to 12MHz */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8731_SYSCLK,
		12000000, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* Start PCK1 clock. */
	clk_enable(pck1_clk);
	DBG("pck1 started\n");

	return 0;
}

static void eti_b1_shutdown(struct snd_pcm_substream *substream)
{
	/* Stop PCK1 clock. */
	clk_disable(pck1_clk);
	DBG("pck1 stopped\n");
}

static int eti_b1_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	int ret;

#ifdef CONFIG_SND_AT91_SOC_ETI_SLAVE
	unsigned int rate;
	int cmr_div, period;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/*
	 * The SSC clock dividers depend on the sample rate.  The CMR.DIV
	 * field divides the system master clock MCK to drive the SSC TK
	 * signal which provides the codec BCLK.  The TCMR.PERIOD and
	 * RCMR.PERIOD fields further divide the BCLK signal to drive
	 * the SSC TF and RF signals which provide the codec DACLRC and
	 * ADCLRC clocks.
	 *
	 * The dividers were determined through trial and error, where a
	 * CMR.DIV value is chosen such that the resulting BCLK value is
	 * divisible, or almost divisible, by (2 * sample rate), and then
	 * the TCMR.PERIOD or RCMR.PERIOD is BCLK / (2 * sample rate) - 1.
	 */
	rate = params_rate(params);

	switch (rate) {
	case 8000:
		cmr_div = 25;	/* BCLK = 60MHz/(2*25) = 1.2MHz */
		period = 74;	/* LRC = BCLK/(2*(74+1)) = 8000Hz */
		break;
	case 32000:
		cmr_div = 7;	/* BCLK = 60MHz/(2*7) ~= 4.28571428MHz */
		period = 66;	/* LRC = BCLK/(2*(66+1)) = 31982.942Hz */
		break;
	case 48000:
		cmr_div = 13;	/* BCLK = 60MHz/(2*13) ~= 2.3076923MHz */
		period = 23;	/* LRC = BCLK/(2*(23+1)) = 48076.923Hz */
		break;
	default:
		printk(KERN_WARNING "unsupported rate %d on ETI-B1 board\n", rate);
		return -EINVAL;
	}

	/* set the MCK divider for BCLK */
	ret = snd_soc_dai_set_clkdiv(cpu_dai, AT91SSC_CMR_DIV, cmr_div);
	if (ret < 0)
		return ret;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* set the BCLK divider for DACLRC */
		ret = snd_soc_dai_set_clkdiv(cpu_dai,
						AT91SSC_TCMR_PERIOD, period);
	} else {
		/* set the BCLK divider for ADCLRC */
		ret = snd_soc_dai_set_clkdiv(cpu_dai,
						AT91SSC_RCMR_PERIOD, period);
	}
	if (ret < 0)
		return ret;

#else /* CONFIG_SND_AT91_SOC_ETI_SLAVE */
	/*
	 * Codec in Master Mode.
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

#endif /* CONFIG_SND_AT91_SOC_ETI_SLAVE */

	return 0;
}

static struct snd_soc_ops eti_b1_ops = {
	.startup = eti_b1_startup,
	.hw_params = eti_b1_hw_params,
	.shutdown = eti_b1_shutdown,
};


static const struct snd_soc_dapm_widget eti_b1_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static const struct snd_soc_dapm_route intercon[] = {

	/* speaker connected to LHPOUT */
	{"Ext Spk", NULL, "LHPOUT"},

	/* mic is connected to Mic Jack, with WM8731 Mic Bias */
	{"MICIN", NULL, "Mic Bias"},
	{"Mic Bias", NULL, "Int Mic"},
};

/*
 * Logic for a wm8731 as connected on a Endrelia ETI-B1 board.
 */
static int eti_b1_wm8731_init(struct snd_soc_codec *codec)
{
	DBG("eti_b1_wm8731_init() called\n");

	/* Add specific widgets */
	snd_soc_dapm_new_controls(codec, eti_b1_dapm_widgets,
				  ARRAY_SIZE(eti_b1_dapm_widgets));

	/* Set up specific audio path interconnects */
	snd_soc_dapm_add_route(codec, intercon, ARRAY_SIZE(intercon));

	/* not connected */
	snd_soc_dapm_disable_pin(codec, "RLINEIN");
	snd_soc_dapm_disable_pin(codec, "LLINEIN");

	/* always connected */
	snd_soc_dapm_enable_pin(codec, "Int Mic");
	snd_soc_dapm_enable_pin(codec, "Ext Spk");

	snd_soc_dapm_sync(codec);

	return 0;
}

static struct snd_soc_dai_link eti_b1_dai = {
	.name = "WM8731",
	.stream_name = "WM8731 PCM",
	.cpu_dai = &at91_ssc_dai[1],
	.codec_dai = &wm8731_dai,
	.init = eti_b1_wm8731_init,
	.ops = &eti_b1_ops,
};

static struct snd_soc_machine snd_soc_machine_eti_b1 = {
	.name = "ETI_B1_WM8731",
	.dai_link = &eti_b1_dai,
	.num_links = 1,
};

static struct wm8731_setup_data eti_b1_wm8731_setup = {
	.i2c_bus = 0,
	.i2c_address = 0x1a,
};

static struct snd_soc_device eti_b1_snd_devdata = {
	.machine = &snd_soc_machine_eti_b1,
	.platform = &at91_soc_platform,
	.codec_dev = &soc_codec_dev_wm8731,
	.codec_data = &eti_b1_wm8731_setup,
};

static struct platform_device *eti_b1_snd_device;

static int __init eti_b1_init(void)
{
	int ret;
	struct at91_ssc_periph *ssc = eti_b1_dai.cpu_dai->private_data;

	if (!request_mem_region(AT91RM9200_BASE_SSC1, SZ_16K, "soc-audio")) {
		DBG("SSC1 memory region is busy\n");
		return -EBUSY;
	}

	ssc->base = ioremap(AT91RM9200_BASE_SSC1, SZ_16K);
	if (!ssc->base) {
		DBG("SSC1 memory ioremap failed\n");
		ret = -ENOMEM;
		goto fail_release_mem;
	}

	ssc->pid = AT91RM9200_ID_SSC1;

	eti_b1_snd_device = platform_device_alloc("soc-audio", -1);
	if (!eti_b1_snd_device) {
		DBG("platform device allocation failed\n");
		ret = -ENOMEM;
		goto fail_io_unmap;
	}

	platform_set_drvdata(eti_b1_snd_device, &eti_b1_snd_devdata);
	eti_b1_snd_devdata.dev = &eti_b1_snd_device->dev;

	ret = platform_device_add(eti_b1_snd_device);
	if (ret) {
		DBG("platform device add failed\n");
		platform_device_put(eti_b1_snd_device);
		goto fail_io_unmap;
	}

	at91_set_A_periph(AT91_PIN_PB6, 0);	/* TF1 */
	at91_set_A_periph(AT91_PIN_PB7, 0);	/* TK1 */
	at91_set_A_periph(AT91_PIN_PB8, 0);	/* TD1 */
	at91_set_A_periph(AT91_PIN_PB9, 0);	/* RD1 */
/*	at91_set_A_periph(AT91_PIN_PB10, 0);*/	/* RK1 */
	at91_set_A_periph(AT91_PIN_PB11, 0);	/* RF1 */

	/*
	 * Set PCK1 parent to PLLB and its rate to 12 Mhz.
	 */
	pllb_clk = clk_get(NULL, "pllb");
	pck1_clk = clk_get(NULL, "pck1");

	clk_set_parent(pck1_clk, pllb_clk);
	clk_set_rate(pck1_clk, 12000000);

	DBG("MCLK rate %luHz\n", clk_get_rate(pck1_clk));

	/* assign the GPIO pin to PCK1 */
	at91_set_B_periph(AT91_PIN_PA24, 0);

#ifdef CONFIG_SND_AT91_SOC_ETI_SLAVE
	printk(KERN_INFO "eti_b1_wm8731: Codec in Slave Mode\n");
#else
	printk(KERN_INFO "eti_b1_wm8731: Codec in Master Mode\n");
#endif
	return ret;

fail_io_unmap:
	iounmap(ssc->base);
fail_release_mem:
	release_mem_region(AT91RM9200_BASE_SSC1, SZ_16K);
	return ret;
}

static void __exit eti_b1_exit(void)
{
	struct at91_ssc_periph *ssc = eti_b1_dai.cpu_dai->private_data;

	clk_put(pck1_clk);
	clk_put(pllb_clk);

	platform_device_unregister(eti_b1_snd_device);

	iounmap(ssc->base);
	release_mem_region(AT91RM9200_BASE_SSC1, SZ_16K);
}

module_init(eti_b1_init);
module_exit(eti_b1_exit);

/* Module information */
MODULE_AUTHOR("Frank Mandarino <fmandarino@endrelia.com>");
MODULE_DESCRIPTION("ALSA SoC ETI-B1-WM8731");
MODULE_LICENSE("GPL");
