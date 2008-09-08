/*
 * pxa2xx-i2s.c  --  ALSA Soc Audio Layer
 *
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Author: Liam Girdwood
 *         liam.girdwood@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include <mach/hardware.h>
#include <mach/pxa-regs.h>
#include <mach/pxa2xx-gpio.h>
#include <mach/audio.h>

#include "pxa2xx-pcm.h"
#include "pxa2xx-i2s.h"

struct pxa_i2s_port {
	u32 sadiv;
	u32 sacr0;
	u32 sacr1;
	u32 saimr;
	int master;
	u32 fmt;
};
static struct pxa_i2s_port pxa_i2s;
static struct clk *clk_i2s;

static struct pxa2xx_pcm_dma_params pxa2xx_i2s_pcm_stereo_out = {
	.name			= "I2S PCM Stereo out",
	.dev_addr		= __PREG(SADR),
	.drcmr			= &DRCMR(3),
	.dcmd			= DCMD_INCSRCADDR | DCMD_FLOWTRG |
				  DCMD_BURST32 | DCMD_WIDTH4,
};

static struct pxa2xx_pcm_dma_params pxa2xx_i2s_pcm_stereo_in = {
	.name			= "I2S PCM Stereo in",
	.dev_addr		= __PREG(SADR),
	.drcmr			= &DRCMR(2),
	.dcmd			= DCMD_INCTRGADDR | DCMD_FLOWSRC |
				  DCMD_BURST32 | DCMD_WIDTH4,
};

static struct pxa2xx_gpio gpio_bus[] = {
	{ /* I2S SoC Slave */
		.rx = GPIO29_SDATA_IN_I2S_MD,
		.tx = GPIO30_SDATA_OUT_I2S_MD,
		.clk = GPIO28_BITCLK_IN_I2S_MD,
		.frm = GPIO31_SYNC_I2S_MD,
	},
	{ /* I2S SoC Master */
#ifdef CONFIG_PXA27x
		.sys = GPIO113_I2S_SYSCLK_MD,
#else
		.sys = GPIO32_SYSCLK_I2S_MD,
#endif
		.rx = GPIO29_SDATA_IN_I2S_MD,
		.tx = GPIO30_SDATA_OUT_I2S_MD,
		.clk = GPIO28_BITCLK_OUT_I2S_MD,
		.frm = GPIO31_SYNC_I2S_MD,
	},
};

static int pxa2xx_i2s_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;

	if (IS_ERR(clk_i2s))
		return PTR_ERR(clk_i2s);

	if (!cpu_dai->active) {
		SACR0 |= SACR0_RST;
		SACR0 = 0;
	}

	return 0;
}

/* wait for I2S controller to be ready */
static int pxa_i2s_wait(void)
{
	int i;

	/* flush the Rx FIFO */
	for(i = 0; i < 16; i++)
		SADR;
	return 0;
}

static int pxa2xx_i2s_set_dai_fmt(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		pxa_i2s.fmt = 0;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		pxa_i2s.fmt = SACR1_AMSL;
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		pxa_i2s.master = 1;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		pxa_i2s.master = 0;
		break;
	default:
		break;
	}
	return 0;
}

static int pxa2xx_i2s_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
		int clk_id, unsigned int freq, int dir)
{
	if (clk_id != PXA2XX_I2S_SYSCLK)
		return -ENODEV;

	if (pxa_i2s.master && dir == SND_SOC_CLOCK_OUT)
		pxa_gpio_mode(gpio_bus[pxa_i2s.master].sys);

	return 0;
}

static int pxa2xx_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;

	pxa_gpio_mode(gpio_bus[pxa_i2s.master].rx);
	pxa_gpio_mode(gpio_bus[pxa_i2s.master].tx);
	pxa_gpio_mode(gpio_bus[pxa_i2s.master].frm);
	pxa_gpio_mode(gpio_bus[pxa_i2s.master].clk);
	BUG_ON(IS_ERR(clk_i2s));
	clk_enable(clk_i2s);
	pxa_i2s_wait();

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		cpu_dai->dma_data = &pxa2xx_i2s_pcm_stereo_out;
	else
		cpu_dai->dma_data = &pxa2xx_i2s_pcm_stereo_in;

	/* is port used by another stream */
	if (!(SACR0 & SACR0_ENB)) {

		SACR0 = 0;
		SACR1 = 0;
		if (pxa_i2s.master)
			SACR0 |= SACR0_BCKD;

		SACR0 |= SACR0_RFTH(14) | SACR0_TFTH(1);
		SACR1 |= pxa_i2s.fmt;
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		SAIMR |= SAIMR_TFS;
	else
		SAIMR |= SAIMR_RFS;

	switch (params_rate(params)) {
	case 8000:
		SADIV = 0x48;
		break;
	case 11025:
		SADIV = 0x34;
		break;
	case 16000:
		SADIV = 0x24;
		break;
	case 22050:
		SADIV = 0x1a;
		break;
	case 44100:
		SADIV = 0xd;
		break;
	case 48000:
		SADIV = 0xc;
		break;
	case 96000: /* not in manual and possibly slightly inaccurate */
		SADIV = 0x6;
		break;
	}

	return 0;
}

static int pxa2xx_i2s_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		SACR0 |= SACR0_ENB;
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static void pxa2xx_i2s_shutdown(struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		SACR1 |= SACR1_DRPL;
		SAIMR &= ~SAIMR_TFS;
	} else {
		SACR1 |= SACR1_DREC;
		SAIMR &= ~SAIMR_RFS;
	}

	if (SACR1 & (SACR1_DREC | SACR1_DRPL)) {
		SACR0 &= ~SACR0_ENB;
		pxa_i2s_wait();
		clk_disable(clk_i2s);
	}

	clk_put(clk_i2s);
}

#ifdef CONFIG_PM
static int pxa2xx_i2s_suspend(struct platform_device *dev,
	struct snd_soc_dai *dai)
{
	if (!dai->active)
		return 0;

	/* store registers */
	pxa_i2s.sacr0 = SACR0;
	pxa_i2s.sacr1 = SACR1;
	pxa_i2s.saimr = SAIMR;
	pxa_i2s.sadiv = SADIV;

	/* deactivate link */
	SACR0 &= ~SACR0_ENB;
	pxa_i2s_wait();
	return 0;
}

static int pxa2xx_i2s_resume(struct platform_device *pdev,
	struct snd_soc_dai *dai)
{
	if (!dai->active)
		return 0;

	pxa_i2s_wait();

	SACR0 = pxa_i2s.sacr0 &= ~SACR0_ENB;
	SACR1 = pxa_i2s.sacr1;
	SAIMR = pxa_i2s.saimr;
	SADIV = pxa_i2s.sadiv;
	SACR0 |= SACR0_ENB;

	return 0;
}

#else
#define pxa2xx_i2s_suspend	NULL
#define pxa2xx_i2s_resume	NULL
#endif

#define PXA2XX_I2S_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_44100 | \
		SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000)

struct snd_soc_dai pxa_i2s_dai = {
	.name = "pxa2xx-i2s",
	.id = 0,
	.type = SND_SOC_DAI_I2S,
	.suspend = pxa2xx_i2s_suspend,
	.resume = pxa2xx_i2s_resume,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = PXA2XX_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = PXA2XX_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.ops = {
		.startup = pxa2xx_i2s_startup,
		.shutdown = pxa2xx_i2s_shutdown,
		.trigger = pxa2xx_i2s_trigger,
		.hw_params = pxa2xx_i2s_hw_params,},
	.dai_ops = {
		.set_fmt = pxa2xx_i2s_set_dai_fmt,
		.set_sysclk = pxa2xx_i2s_set_dai_sysclk,
	},
};

EXPORT_SYMBOL_GPL(pxa_i2s_dai);

static int pxa2xx_i2s_probe(struct platform_device *dev)
{
	clk_i2s = clk_get(&dev->dev, "I2SCLK");
	return IS_ERR(clk_i2s) ? PTR_ERR(clk_i2s) : 0;
}

static int __devexit pxa2xx_i2s_remove(struct platform_device *dev)
{
	clk_put(clk_i2s);
	clk_i2s = ERR_PTR(-ENOENT);
	return 0;
}

static struct platform_driver pxa2xx_i2s_driver = {
	.probe = pxa2xx_i2s_probe,
	.remove = __devexit_p(pxa2xx_i2s_remove),

	.driver = {
		.name = "pxa2xx-i2s",
		.owner = THIS_MODULE,
	},
};

static int __init pxa2xx_i2s_init(void)
{
	clk_i2s = ERR_PTR(-ENOENT);
	return platform_driver_register(&pxa2xx_i2s_driver);
}

static void __exit pxa2xx_i2s_exit(void)
{
	platform_driver_unregister(&pxa2xx_i2s_driver);
}

module_init(pxa2xx_i2s_init);
module_exit(pxa2xx_i2s_exit);

/* Module information */
MODULE_AUTHOR("Liam Girdwood, liam.girdwood@wolfsonmicro.com, www.wolfsonmicro.com");
MODULE_DESCRIPTION("pxa2xx I2S SoC Interface");
MODULE_LICENSE("GPL");
