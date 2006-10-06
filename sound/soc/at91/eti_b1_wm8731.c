/*
 * eti_b1_wm8731  --  SoC audio for Endrelia ETI_B1.
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
 *  Revision history
 *    30th Nov 2005   Initial version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/arch/at91rm9200.h>
#include <asm/arch/gpio.h>
#include <asm/arch/hardware.h>

#include "../codecs/wm8731.h"
#include "at91rm9200-pcm.h"

#if 0
#define	DBG(x...)	printk(KERN_INFO "eti_b1_wm8731:" x)
#else
#define	DBG(x...)
#endif

static struct clk *pck1_clk;
static struct clk *pllb_clk;

static int eti_b1_startup(snd_pcm_substream_t *substream)
{
	/* Start PCK1 clock. */
	clk_enable(pck1_clk);
	DBG("pck1 started\n");

	return 0;
}

static void eti_b1_shutdown(snd_pcm_substream_t *substream)
{
	/* Stop PCK1 clock. */
	clk_disable(pck1_clk);
	DBG("pck1 stopped\n");
}

static struct snd_soc_ops eti_b1_ops = {
	.startup = eti_b1_startup,
	.shutdown = eti_b1_shutdown,
};


static const struct snd_soc_dapm_widget eti_b1_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static const char *intercon[][3] = {

	/* speaker connected to LHPOUT */
	{"Ext Spk", NULL, "LHPOUT"},

	/* mic is connected to Mic Jack, with WM8731 Mic Bias */
	{"MICIN", NULL, "Mic Bias"},
	{"Mic Bias", NULL, "Int Mic"},

	/* terminator */
	{NULL, NULL, NULL},
};

/*
 * Logic for a wm8731 as connected on a Endrelia ETI-B1 board.
 */
static int eti_b1_wm8731_init(struct snd_soc_codec *codec)
{
	int i;

	DBG("eti_b1_wm8731_init() called\n");

	/* Add specific widgets */
	for(i = 0; i < ARRAY_SIZE(eti_b1_dapm_widgets); i++) {
		snd_soc_dapm_new_control(codec, &eti_b1_dapm_widgets[i]);
	}

	/* Set up specific audio path interconnects */
	for(i = 0; intercon[i][0] != NULL; i++) {
		snd_soc_dapm_connect_input(codec, intercon[i][0],
			intercon[i][1], intercon[i][2]);
	}

	/* not connected */
	snd_soc_dapm_set_endpoint(codec, "RLINEIN", 0);
	snd_soc_dapm_set_endpoint(codec, "LLINEIN", 0);

	/* always connected */
	snd_soc_dapm_set_endpoint(codec, "Int Mic", 1);
	snd_soc_dapm_set_endpoint(codec, "Ext Spk", 1);

	snd_soc_dapm_sync_endpoints(codec);

	return 0;
}

unsigned int eti_b1_config_sysclk(struct snd_soc_pcm_runtime *rtd,
	struct snd_soc_clock_info *info)
{
	if(info->bclk_master & SND_SOC_DAIFMT_CBS_CFS) {
		return rtd->codec_dai->config_sysclk(rtd->codec_dai, info, 12000000);
	}
	return 0;
}

static struct snd_soc_dai_link eti_b1_dai = {
	.name = "WM8731",
	.stream_name = "WM8731",
	.cpu_dai = &at91rm9200_i2s_dai[1],
	.codec_dai = &wm8731_dai,
	.init = eti_b1_wm8731_init,
	.config_sysclk = eti_b1_config_sysclk,
};

static struct snd_soc_machine snd_soc_machine_eti_b1 = {
	.name = "ETI_B1",
	.dai_link = &eti_b1_dai,
	.num_links = 1,
	.ops = &eti_b1_ops,
};

static struct wm8731_setup_data eti_b1_wm8731_setup = {
	.i2c_address = 0x1a,
};

static struct snd_soc_device eti_b1_snd_devdata = {
	.machine = &snd_soc_machine_eti_b1,
	.platform = &at91rm9200_soc_platform,
	.codec_dev = &soc_codec_dev_wm8731,
	.codec_data = &eti_b1_wm8731_setup,
};

static struct platform_device *eti_b1_snd_device;

static int __init eti_b1_init(void)
{
	int ret;
	u32 ssc_pio_lines;

	eti_b1_snd_device = platform_device_alloc("soc-audio", -1);
	if (!eti_b1_snd_device)
		return -ENOMEM;

	platform_set_drvdata(eti_b1_snd_device, &eti_b1_snd_devdata);
	eti_b1_snd_devdata.dev = &eti_b1_snd_device->dev;

	ret = platform_device_add(eti_b1_snd_device);
	if (ret) {
		platform_device_put(eti_b1_snd_device);
		return ret;
	}

 	ssc_pio_lines = AT91_PB6_TF1 | AT91_PB7_TK1 | AT91_PB8_TD1
			| AT91_PB9_RD1 /* | AT91_PB10_RK1 | AT91_PB11_RF1 */;

	/* Reset all PIO registers and assign lines to peripheral A */
 	at91_sys_write(AT91_PIOB + PIO_PDR,  ssc_pio_lines);
 	at91_sys_write(AT91_PIOB + PIO_ODR,  ssc_pio_lines);
 	at91_sys_write(AT91_PIOB + PIO_IFDR, ssc_pio_lines);
 	at91_sys_write(AT91_PIOB + PIO_CODR, ssc_pio_lines);
 	at91_sys_write(AT91_PIOB + PIO_IDR,  ssc_pio_lines);
 	at91_sys_write(AT91_PIOB + PIO_MDDR, ssc_pio_lines);
 	at91_sys_write(AT91_PIOB + PIO_PUDR, ssc_pio_lines);
 	at91_sys_write(AT91_PIOB + PIO_ASR,  ssc_pio_lines);
 	at91_sys_write(AT91_PIOB + PIO_OWDR, ssc_pio_lines);

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

	return ret;
}

static void __exit eti_b1_exit(void)
{
	clk_put(pck1_clk);
	clk_put(pllb_clk);

	platform_device_unregister(eti_b1_snd_device);
}

module_init(eti_b1_init);
module_exit(eti_b1_exit);

/* Module information */
MODULE_AUTHOR("Frank Mandarino <fmandarino@endrelia.com>");
MODULE_DESCRIPTION("ALSA SoC ETI-B1-WM8731");
MODULE_LICENSE("GPL");
