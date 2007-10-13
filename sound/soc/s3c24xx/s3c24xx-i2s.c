/*
 * s3c24xx-i2s.c  --  ALSA Soc Audio Layer
 *
 * (c) 2006 Wolfson Microelectronics PLC.
 * Graeme Gregory graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 * (c) 2004-2005 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *
 *  Revision history
 *    11th Dec 2006   Merged with Simtec driver
 *    10th Nov 2006   Initial version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/arch/regs-iis.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-clock.h>
#include <asm/arch/audio.h>
#include <asm/dma.h>
#include <asm/arch/dma.h>

#include "s3c24xx-pcm.h"
#include "s3c24xx-i2s.h"

#define S3C24XX_I2S_DEBUG 0
#if S3C24XX_I2S_DEBUG
#define DBG(x...) printk(KERN_DEBUG x)
#else
#define DBG(x...)
#endif

static struct s3c2410_dma_client s3c24xx_dma_client_out = {
	.name = "I2S PCM Stereo out"
};

static struct s3c2410_dma_client s3c24xx_dma_client_in = {
	.name = "I2S PCM Stereo in"
};

static struct s3c24xx_pcm_dma_params s3c24xx_i2s_pcm_stereo_out = {
	.client		= &s3c24xx_dma_client_out,
	.channel	= DMACH_I2S_OUT,
	.dma_addr	= S3C2410_PA_IIS + S3C2410_IISFIFO,
	.dma_size	= 2,
};

static struct s3c24xx_pcm_dma_params s3c24xx_i2s_pcm_stereo_in = {
	.client		= &s3c24xx_dma_client_in,
	.channel	= DMACH_I2S_IN,
	.dma_addr	= S3C2410_PA_IIS + S3C2410_IISFIFO,
	.dma_size	= 2,
};

struct s3c24xx_i2s_info {
	void __iomem	*regs;
	struct clk	*iis_clk;
};
static struct s3c24xx_i2s_info s3c24xx_i2s;

static void s3c24xx_snd_txctrl(int on)
{
	u32 iisfcon;
	u32 iiscon;
	u32 iismod;

	DBG("Entered %s\n", __FUNCTION__);

	iisfcon = readl(s3c24xx_i2s.regs + S3C2410_IISFCON);
	iiscon  = readl(s3c24xx_i2s.regs + S3C2410_IISCON);
	iismod  = readl(s3c24xx_i2s.regs + S3C2410_IISMOD);

	DBG("r: IISCON: %lx IISMOD: %lx IISFCON: %lx\n", iiscon, iismod, iisfcon);

	if (on) {
		iisfcon |= S3C2410_IISFCON_TXDMA | S3C2410_IISFCON_TXENABLE;
		iiscon  |= S3C2410_IISCON_TXDMAEN | S3C2410_IISCON_IISEN;
		iiscon  &= ~S3C2410_IISCON_TXIDLE;
		iismod  |= S3C2410_IISMOD_TXMODE;

		writel(iismod,  s3c24xx_i2s.regs + S3C2410_IISMOD);
		writel(iisfcon, s3c24xx_i2s.regs + S3C2410_IISFCON);
		writel(iiscon,  s3c24xx_i2s.regs + S3C2410_IISCON);
	} else {
		/* note, we have to disable the FIFOs otherwise bad things
		 * seem to happen when the DMA stops. According to the
		 * Samsung supplied kernel, this should allow the DMA
		 * engine and FIFOs to reset. If this isn't allowed, the
		 * DMA engine will simply freeze randomly.
		 */

		iisfcon &= ~S3C2410_IISFCON_TXENABLE;
		iisfcon &= ~S3C2410_IISFCON_TXDMA;
		iiscon  |=  S3C2410_IISCON_TXIDLE;
		iiscon  &= ~S3C2410_IISCON_TXDMAEN;
		iismod  &= ~S3C2410_IISMOD_TXMODE;

		writel(iiscon,  s3c24xx_i2s.regs + S3C2410_IISCON);
		writel(iisfcon, s3c24xx_i2s.regs + S3C2410_IISFCON);
		writel(iismod,  s3c24xx_i2s.regs + S3C2410_IISMOD);
	}

	DBG("w: IISCON: %lx IISMOD: %lx IISFCON: %lx\n", iiscon, iismod, iisfcon);
}

static void s3c24xx_snd_rxctrl(int on)
{
	u32 iisfcon;
	u32 iiscon;
	u32 iismod;

	DBG("Entered %s\n", __FUNCTION__);

	iisfcon = readl(s3c24xx_i2s.regs + S3C2410_IISFCON);
	iiscon  = readl(s3c24xx_i2s.regs + S3C2410_IISCON);
	iismod  = readl(s3c24xx_i2s.regs + S3C2410_IISMOD);

	DBG("r: IISCON: %lx IISMOD: %lx IISFCON: %lx\n", iiscon, iismod, iisfcon);

	if (on) {
		iisfcon |= S3C2410_IISFCON_RXDMA | S3C2410_IISFCON_RXENABLE;
		iiscon  |= S3C2410_IISCON_RXDMAEN | S3C2410_IISCON_IISEN;
		iiscon  &= ~S3C2410_IISCON_RXIDLE;
		iismod  |= S3C2410_IISMOD_RXMODE;

		writel(iismod,  s3c24xx_i2s.regs + S3C2410_IISMOD);
		writel(iisfcon, s3c24xx_i2s.regs + S3C2410_IISFCON);
		writel(iiscon,  s3c24xx_i2s.regs + S3C2410_IISCON);
	} else {
		/* note, we have to disable the FIFOs otherwise bad things
		 * seem to happen when the DMA stops. According to the
		 * Samsung supplied kernel, this should allow the DMA
		 * engine and FIFOs to reset. If this isn't allowed, the
		 * DMA engine will simply freeze randomly.
		 */

        iisfcon &= ~S3C2410_IISFCON_RXENABLE;
        iisfcon &= ~S3C2410_IISFCON_RXDMA;
        iiscon  |= S3C2410_IISCON_RXIDLE;
        iiscon  &= ~S3C2410_IISCON_RXDMAEN;
		iismod  &= ~S3C2410_IISMOD_RXMODE;

		writel(iisfcon, s3c24xx_i2s.regs + S3C2410_IISFCON);
		writel(iiscon,  s3c24xx_i2s.regs + S3C2410_IISCON);
		writel(iismod,  s3c24xx_i2s.regs + S3C2410_IISMOD);
	}

	DBG("w: IISCON: %lx IISMOD: %lx IISFCON: %lx\n", iiscon, iismod, iisfcon);
}

/*
 * Wait for the LR signal to allow synchronisation to the L/R clock
 * from the codec. May only be needed for slave mode.
 */
static int s3c24xx_snd_lrsync(void)
{
	u32 iiscon;
	unsigned long timeout = jiffies + msecs_to_jiffies(5);

	DBG("Entered %s\n", __FUNCTION__);

	while (1) {
		iiscon = readl(s3c24xx_i2s.regs + S3C2410_IISCON);
		if (iiscon & S3C2410_IISCON_LRINDEX)
			break;

		if (timeout < jiffies)
			return -ETIMEDOUT;
	}

	return 0;
}

/*
 * Check whether CPU is the master or slave
 */
static inline int s3c24xx_snd_is_clkmaster(void)
{
	DBG("Entered %s\n", __FUNCTION__);

	return (readl(s3c24xx_i2s.regs + S3C2410_IISMOD) & S3C2410_IISMOD_SLAVE) ? 0:1;
}

/*
 * Set S3C24xx I2S DAI format
 */
static int s3c24xx_i2s_set_fmt(struct snd_soc_cpu_dai *cpu_dai,
		unsigned int fmt)
{
	u32 iismod;

	DBG("Entered %s\n", __FUNCTION__);

	iismod = readl(s3c24xx_i2s.regs + S3C2410_IISMOD);
	DBG("hw_params r: IISMOD: %lx \n", iismod);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iismod |= S3C2410_IISMOD_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_LEFT_J:
		iismod |= S3C2410_IISMOD_MSB;
		break;
	case SND_SOC_DAIFMT_I2S:
		break;
	default:
		return -EINVAL;
	}

	writel(iismod, s3c24xx_i2s.regs + S3C2410_IISMOD);
	DBG("hw_params w: IISMOD: %lx \n", iismod);
	return 0;
}

static int s3c24xx_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	u32 iismod;

	DBG("Entered %s\n", __FUNCTION__);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		rtd->dai->cpu_dai->dma_data = &s3c24xx_i2s_pcm_stereo_out;
	else
		rtd->dai->cpu_dai->dma_data = &s3c24xx_i2s_pcm_stereo_in;

	/* Working copies of register */
	iismod = readl(s3c24xx_i2s.regs + S3C2410_IISMOD);
	DBG("hw_params r: IISMOD: %lx\n", iismod);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		iismod |= S3C2410_IISMOD_16BIT;
		break;
	}

	writel(iismod, s3c24xx_i2s.regs + S3C2410_IISMOD);
	DBG("hw_params w: IISMOD: %lx\n", iismod);
	return 0;
}

static int s3c24xx_i2s_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;

	DBG("Entered %s\n", __FUNCTION__);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (!s3c24xx_snd_is_clkmaster()) {
			ret = s3c24xx_snd_lrsync();
			if (ret)
				goto exit_err;
		}

		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			s3c24xx_snd_rxctrl(1);
		else
			s3c24xx_snd_txctrl(1);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			s3c24xx_snd_rxctrl(0);
		else
			s3c24xx_snd_txctrl(0);
		break;
	default:
		ret = -EINVAL;
		break;
	}

exit_err:
	return ret;
}

/*
 * Set S3C24xx Clock source
 */
static int s3c24xx_i2s_set_sysclk(struct snd_soc_cpu_dai *cpu_dai,
	int clk_id, unsigned int freq, int dir)
{
	u32 iismod = readl(s3c24xx_i2s.regs + S3C2410_IISMOD);

	DBG("Entered %s\n", __FUNCTION__);

	iismod &= ~S3C2440_IISMOD_MPLL;

	switch (clk_id) {
	case S3C24XX_CLKSRC_PCLK:
		break;
	case S3C24XX_CLKSRC_MPLL:
		iismod |= S3C2440_IISMOD_MPLL;
		break;
	default:
		return -EINVAL;
	}

	writel(iismod, s3c24xx_i2s.regs + S3C2410_IISMOD);
	return 0;
}

/*
 * Set S3C24xx Clock dividers
 */
static int s3c24xx_i2s_set_clkdiv(struct snd_soc_cpu_dai *cpu_dai,
	int div_id, int div)
{
	u32 reg;

	DBG("Entered %s\n", __FUNCTION__);

	switch (div_id) {
	case S3C24XX_DIV_BCLK:
		reg = readl(s3c24xx_i2s.regs + S3C2410_IISMOD) & ~S3C2410_IISMOD_FS_MASK;
		writel(reg | div, s3c24xx_i2s.regs + S3C2410_IISMOD);
		break;
	case S3C24XX_DIV_MCLK:
		reg = readl(s3c24xx_i2s.regs + S3C2410_IISMOD) & ~(S3C2410_IISMOD_384FS);
		writel(reg | div, s3c24xx_i2s.regs + S3C2410_IISMOD);
		break;
	case S3C24XX_DIV_PRESCALER:
		writel(div, s3c24xx_i2s.regs + S3C2410_IISPSR);
		reg = readl(s3c24xx_i2s.regs + S3C2410_IISCON);
		writel(reg | S3C2410_IISCON_PSCEN, s3c24xx_i2s.regs + S3C2410_IISCON);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * To avoid duplicating clock code, allow machine driver to
 * get the clockrate from here.
 */
u32 s3c24xx_i2s_get_clockrate(void)
{
	return clk_get_rate(s3c24xx_i2s.iis_clk);
}
EXPORT_SYMBOL_GPL(s3c24xx_i2s_get_clockrate);

static int s3c24xx_i2s_probe(struct platform_device *pdev)
{
	DBG("Entered %s\n", __FUNCTION__);

	s3c24xx_i2s.regs = ioremap(S3C2410_PA_IIS, 0x100);
	if (s3c24xx_i2s.regs == NULL)
		return -ENXIO;

	s3c24xx_i2s.iis_clk=clk_get(&pdev->dev, "iis");
	if (s3c24xx_i2s.iis_clk == NULL) {
		DBG("failed to get iis_clock\n");
		return -ENODEV;
	}
	clk_enable(s3c24xx_i2s.iis_clk);

	/* Configure the I2S pins in correct mode */
	s3c2410_gpio_cfgpin(S3C2410_GPE0, S3C2410_GPE0_I2SLRCK);
	s3c2410_gpio_cfgpin(S3C2410_GPE1, S3C2410_GPE1_I2SSCLK);
	s3c2410_gpio_cfgpin(S3C2410_GPE2, S3C2410_GPE2_CDCLK);
	s3c2410_gpio_cfgpin(S3C2410_GPE3, S3C2410_GPE3_I2SSDI);
	s3c2410_gpio_cfgpin(S3C2410_GPE4, S3C2410_GPE4_I2SSDO);

	writel(S3C2410_IISCON_IISEN, s3c24xx_i2s.regs + S3C2410_IISCON);

	s3c24xx_snd_txctrl(0);
	s3c24xx_snd_rxctrl(0);

	return 0;
}

#define S3C24XX_I2S_RATES \
	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_16000 | \
	SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

struct snd_soc_cpu_dai s3c24xx_i2s_dai = {
	.name = "s3c24xx-i2s",
	.id = 0,
	.type = SND_SOC_DAI_I2S,
	.probe = s3c24xx_i2s_probe,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = S3C24XX_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = S3C24XX_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE,},
	.ops = {
		.trigger = s3c24xx_i2s_trigger,
		.hw_params = s3c24xx_i2s_hw_params,},
	.dai_ops = {
		.set_fmt = s3c24xx_i2s_set_fmt,
		.set_clkdiv = s3c24xx_i2s_set_clkdiv,
		.set_sysclk = s3c24xx_i2s_set_sysclk,
	},
};
EXPORT_SYMBOL_GPL(s3c24xx_i2s_dai);

/* Module information */
MODULE_AUTHOR("Ben Dooks, <ben@simtec.co.uk>");
MODULE_DESCRIPTION("s3c24xx I2S SoC Interface");
MODULE_LICENSE("GPL");
