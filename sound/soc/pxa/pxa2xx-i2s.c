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
 *
 *  Revision history
 *    12th Aug 2005   Initial version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include <asm/hardware.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/audio.h>

#include "pxa2xx-pcm.h"

/* used to disable sysclk if external crystal is used */
static int extclk;
module_param(extclk, int, 0);
MODULE_PARM_DESC(extclk, "set to 1 to disable pxa2xx i2s sysclk");

struct pxa_i2s_port {
	u32 sadiv;
	u32 sacr0;
	u32 sacr1;
	u32 saimr;
	int master;
};
static struct pxa_i2s_port pxa_i2s;

#define PXA_I2S_DAIFMT \
	(SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_NB_NF)

#define PXA_I2S_DIR \
	(SND_SOC_DAIDIR_PLAYBACK | SND_SOC_DAIDIR_CAPTURE)

#define PXA_I2S_RATES \
	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_16000 | \
	SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

/* priv is divider */
static struct snd_soc_dai_mode pxa2xx_i2s_modes[] = {
	/* pxa2xx I2S frame and clock master modes */
	{PXA_I2S_DAIFMT | SND_SOC_DAIFMT_CBS_CFS, SND_SOC_DAITDM_LRDW(0,0),
		SNDRV_PCM_FMTBIT_S16_LE, SNDRV_PCM_RATE_8000, PXA_I2S_DIR,
		SND_SOC_DAI_BFS_DIV, 256, SND_SOC_FSBD(4), 0x48},
	{PXA_I2S_DAIFMT | SND_SOC_DAIFMT_CBS_CFS, SND_SOC_DAITDM_LRDW(0,0),
		SNDRV_PCM_FMTBIT_S16_LE, SNDRV_PCM_RATE_11025, PXA_I2S_DIR,
		SND_SOC_DAI_BFS_DIV, 256, SND_SOC_FSBD(4), 0x34},
	{PXA_I2S_DAIFMT | SND_SOC_DAIFMT_CBS_CFS, SND_SOC_DAITDM_LRDW(0,0),
		SNDRV_PCM_FMTBIT_S16_LE, SNDRV_PCM_RATE_16000, PXA_I2S_DIR,
		SND_SOC_DAI_BFS_DIV, 256, SND_SOC_FSBD(4), 0x24},
	{PXA_I2S_DAIFMT | SND_SOC_DAIFMT_CBS_CFS, SND_SOC_DAITDM_LRDW(0,0),
		SNDRV_PCM_FMTBIT_S16_LE, SNDRV_PCM_RATE_22050, PXA_I2S_DIR,
		SND_SOC_DAI_BFS_DIV, 256, SND_SOC_FSBD(4), 0x1a},
	{PXA_I2S_DAIFMT | SND_SOC_DAIFMT_CBS_CFS, SND_SOC_DAITDM_LRDW(0,0),
		SNDRV_PCM_FMTBIT_S16_LE, SNDRV_PCM_RATE_44100, PXA_I2S_DIR,
		SND_SOC_DAI_BFS_DIV, 256, SND_SOC_FSBD(4), 0xd},
	{PXA_I2S_DAIFMT | SND_SOC_DAIFMT_CBS_CFS, SND_SOC_DAITDM_LRDW(0,0),
		SNDRV_PCM_FMTBIT_S16_LE, SNDRV_PCM_RATE_48000, PXA_I2S_DIR,
		SND_SOC_DAI_BFS_DIV, 256, SND_SOC_FSBD(4), 0xc},

	/* pxa2xx I2S frame master and clock slave mode */
	{PXA_I2S_DAIFMT | SND_SOC_DAIFMT_CBM_CFS, SND_SOC_DAITDM_LRDW(0,0),
		SNDRV_PCM_FMTBIT_S16_LE, PXA_I2S_RATES, PXA_I2S_DIR, 0,
		SND_SOC_FS_ALL, SND_SOC_FSB(64), 0x48},

};

static struct pxa2xx_pcm_dma_params pxa2xx_i2s_pcm_stereo_out = {
	.name			= "I2S PCM Stereo out",
	.dev_addr		= __PREG(SADR),
	.drcmr			= &DRCMRTXSADR,
	.dcmd			= DCMD_INCSRCADDR | DCMD_FLOWTRG |
				  DCMD_BURST32 | DCMD_WIDTH4,
};

static struct pxa2xx_pcm_dma_params pxa2xx_i2s_pcm_stereo_in = {
	.name			= "I2S PCM Stereo in",
	.dev_addr		= __PREG(SADR),
	.drcmr			= &DRCMRRXSADR,
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

	if (!rtd->cpu_dai->active) {
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

static int pxa2xx_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	pxa_i2s.master = 0;
	if (rtd->cpu_dai->dai_runtime.fmt & SND_SOC_DAIFMT_CBS_CFS)
		pxa_i2s.master = 1;

	if (pxa_i2s.master && !extclk)
		pxa_gpio_mode(gpio_bus[pxa_i2s.master].sys);

	pxa_gpio_mode(gpio_bus[pxa_i2s.master].rx);
	pxa_gpio_mode(gpio_bus[pxa_i2s.master].tx);
	pxa_gpio_mode(gpio_bus[pxa_i2s.master].frm);
	pxa_gpio_mode(gpio_bus[pxa_i2s.master].clk);
	pxa_set_cken(CKEN8_I2S, 1);
	pxa_i2s_wait();

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		rtd->cpu_dai->dma_data = &pxa2xx_i2s_pcm_stereo_out;
	else
		rtd->cpu_dai->dma_data = &pxa2xx_i2s_pcm_stereo_in;

	/* is port used by another stream */
	if (!(SACR0 & SACR0_ENB)) {

		SACR0 = 0;
		SACR1 = 0;
		if (pxa_i2s.master)
			SACR0 |= SACR0_BCKD;

		SACR0 |= SACR0_RFTH(14) | SACR0_TFTH(1);

		if (rtd->cpu_dai->dai_runtime.fmt & SND_SOC_DAIFMT_LEFT_J)
			SACR1 |= SACR1_AMSL;
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		SAIMR |= SAIMR_TFS;
	else
		SAIMR |= SAIMR_RFS;

	SADIV = rtd->cpu_dai->dai_runtime.priv;
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
		pxa_set_cken(CKEN8_I2S, 0);
	}
}

#ifdef CONFIG_PM
static int pxa2xx_i2s_suspend(struct platform_device *dev,
	struct snd_soc_cpu_dai *dai)
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
	struct snd_soc_cpu_dai *dai)
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

/* pxa2xx I2S sysclock is always 256 FS */
static unsigned int pxa_i2s_config_sysclk(struct snd_soc_cpu_dai *iface,
	struct snd_soc_clock_info *info, unsigned int clk)
{
	return info->rate << 8;
}

struct snd_soc_cpu_dai pxa_i2s_dai = {
	.name = "pxa2xx-i2s",
	.id = 0,
	.type = SND_SOC_DAI_I2S,
	.suspend = pxa2xx_i2s_suspend,
	.resume = pxa2xx_i2s_resume,
	.config_sysclk = pxa_i2s_config_sysclk,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,},
	.ops = {
		.startup = pxa2xx_i2s_startup,
		.shutdown = pxa2xx_i2s_shutdown,
		.trigger = pxa2xx_i2s_trigger,
		.hw_params = pxa2xx_i2s_hw_params,},
	.caps = {
		.num_modes = ARRAY_SIZE(pxa2xx_i2s_modes),
		.mode = pxa2xx_i2s_modes,},
};

EXPORT_SYMBOL_GPL(pxa_i2s_dai);

/* Module information */
MODULE_AUTHOR("Liam Girdwood, liam.girdwood@wolfsonmicro.com, www.wolfsonmicro.com");
MODULE_DESCRIPTION("pxa2xx I2S SoC Interface");
MODULE_LICENSE("GPL");
