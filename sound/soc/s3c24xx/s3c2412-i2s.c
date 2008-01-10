/* sound/soc/s3c24xx/s3c2412-i2s.c
 *
 * ALSA Soc Audio Layer - S3C2412 I2S driver
 *
 * Copyright (c) 2006 Wolfson Microelectronics PLC.
 *	Graeme Gregory graeme.gregory@wolfsonmicro.com
 *	linux@wolfsonmicro.com
 *
 * Copyright (c) 2007, 2004-2005 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/kernel.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <asm/hardware.h>

#include <linux/io.h>
#include <asm/dma.h>

#include <asm/plat-s3c24xx/regs-s3c2412-iis.h>

#include <asm/arch/regs-gpio.h>
#include <asm/arch/audio.h>
#include <asm/arch/dma.h>

#include "s3c24xx-pcm.h"
#include "s3c2412-i2s.h"

#define S3C2412_I2S_DEBUG 0
#define S3C2412_I2S_DEBUG_CON 0

#if S3C2412_I2S_DEBUG
#define DBG(x...) printk(KERN_INFO x)
#else
#define DBG(x...) do { } while (0)
#endif

static struct s3c2410_dma_client s3c2412_dma_client_out = {
	.name		= "I2S PCM Stereo out"
};

static struct s3c2410_dma_client s3c2412_dma_client_in = {
	.name		= "I2S PCM Stereo in"
};

static struct s3c24xx_pcm_dma_params s3c2412_i2s_pcm_stereo_out = {
	.client		= &s3c2412_dma_client_out,
	.channel	= DMACH_I2S_OUT,
	.dma_addr	= S3C2410_PA_IIS + S3C2412_IISTXD,
	.dma_size	= 4,
};

static struct s3c24xx_pcm_dma_params s3c2412_i2s_pcm_stereo_in = {
	.client		= &s3c2412_dma_client_in,
	.channel	= DMACH_I2S_IN,
	.dma_addr	= S3C2410_PA_IIS + S3C2412_IISRXD,
	.dma_size	= 4,
};

struct s3c2412_i2s_info {
	struct device	*dev;
	void __iomem	*regs;
	struct clk	*iis_clk;
	struct clk	*iis_pclk;
	struct clk	*iis_cclk;

	u32		 suspend_iismod;
	u32		 suspend_iiscon;
	u32		 suspend_iispsr;
};

static struct s3c2412_i2s_info s3c2412_i2s;

#define bit_set(v, b) (((v) & (b)) ? 1 : 0)

#if S3C2412_I2S_DEBUG_CON
static void dbg_showcon(const char *fn, u32 con)
{
	printk(KERN_DEBUG "%s: LRI=%d, TXFEMPT=%d, RXFEMPT=%d, TXFFULL=%d, RXFFULL=%d\n", fn,
	       bit_set(con, S3C2412_IISCON_LRINDEX),
	       bit_set(con, S3C2412_IISCON_TXFIFO_EMPTY),
	       bit_set(con, S3C2412_IISCON_RXFIFO_EMPTY),
	       bit_set(con, S3C2412_IISCON_TXFIFO_FULL),
	       bit_set(con, S3C2412_IISCON_RXFIFO_FULL));

	printk(KERN_DEBUG "%s: PAUSE: TXDMA=%d, RXDMA=%d, TXCH=%d, RXCH=%d\n",
	       fn,
	       bit_set(con, S3C2412_IISCON_TXDMA_PAUSE),
	       bit_set(con, S3C2412_IISCON_RXDMA_PAUSE),
	       bit_set(con, S3C2412_IISCON_TXCH_PAUSE),
	       bit_set(con, S3C2412_IISCON_RXCH_PAUSE));
	printk(KERN_DEBUG "%s: ACTIVE: TXDMA=%d, RXDMA=%d, IIS=%d\n", fn,
	       bit_set(con, S3C2412_IISCON_TXDMA_ACTIVE),
	       bit_set(con, S3C2412_IISCON_RXDMA_ACTIVE),
	       bit_set(con, S3C2412_IISCON_IIS_ACTIVE));
}
#else
static inline void dbg_showcon(const char *fn, u32 con)
{
}
#endif

/* Turn on or off the transmission path. */
static void s3c2412_snd_txctrl(int on)
{
	struct s3c2412_i2s_info *i2s = &s3c2412_i2s;
	void __iomem *regs = i2s->regs;
	u32 fic, con, mod;

	DBG("%s(%d)\n", __func__, on);

	fic = readl(regs + S3C2412_IISFIC);
	con = readl(regs + S3C2412_IISCON);
	mod = readl(regs + S3C2412_IISMOD);

	DBG("%s: IIS: CON=%x MOD=%x FIC=%x\n", __func__, con, mod, fic);

	if (on) {
		con |= S3C2412_IISCON_TXDMA_ACTIVE | S3C2412_IISCON_IIS_ACTIVE;
		con &= ~S3C2412_IISCON_TXDMA_PAUSE;
		con &= ~S3C2412_IISCON_TXCH_PAUSE;

		switch (mod & S3C2412_IISMOD_MODE_MASK) {
		case S3C2412_IISMOD_MODE_TXONLY:
		case S3C2412_IISMOD_MODE_TXRX:
			/* do nothing, we are in the right mode */
			break;

		case S3C2412_IISMOD_MODE_RXONLY:
			mod &= ~S3C2412_IISMOD_MODE_MASK;
			mod |= S3C2412_IISMOD_MODE_TXRX;
			break;

		default:
			dev_err(i2s->dev, "TXEN: Invalid MODE in IISMOD\n");
		}

		writel(con, regs + S3C2412_IISCON);
		writel(mod, regs + S3C2412_IISMOD);
	} else {
		/* Note, we do not have any indication that the FIFO problems
		 * tha the S3C2410/2440 had apply here, so we should be able
		 * to disable the DMA and TX without resetting the FIFOS.
		 */

		con |=  S3C2412_IISCON_TXDMA_PAUSE;
		con |=  S3C2412_IISCON_TXCH_PAUSE;
		con &= ~S3C2412_IISCON_TXDMA_ACTIVE;

		switch (mod & S3C2412_IISMOD_MODE_MASK) {
		case S3C2412_IISMOD_MODE_TXRX:
			mod &= ~S3C2412_IISMOD_MODE_MASK;
			mod |= S3C2412_IISMOD_MODE_RXONLY;
			break;

		case S3C2412_IISMOD_MODE_TXONLY:
			mod &= ~S3C2412_IISMOD_MODE_MASK;
			con &= ~S3C2412_IISCON_IIS_ACTIVE;
			break;

		default:
			dev_err(i2s->dev, "TXDIS: Invalid MODE in IISMOD\n");
		}

		writel(mod, regs + S3C2412_IISMOD);
		writel(con, regs + S3C2412_IISCON);
	}

	fic = readl(regs + S3C2412_IISFIC);
	dbg_showcon(__func__, con);
	DBG("%s: IIS: CON=%x MOD=%x FIC=%x\n", __func__, con, mod, fic);
}

static void s3c2412_snd_rxctrl(int on)
{
	struct s3c2412_i2s_info *i2s = &s3c2412_i2s;
	void __iomem *regs = i2s->regs;
	u32 fic, con, mod;

	DBG("%s(%d)\n", __func__, on);

	fic = readl(regs + S3C2412_IISFIC);
	con = readl(regs + S3C2412_IISCON);
	mod = readl(regs + S3C2412_IISMOD);

	DBG("%s: IIS: CON=%x MOD=%x FIC=%x\n", __func__, con, mod, fic);

	if (on) {
		con |= S3C2412_IISCON_RXDMA_ACTIVE | S3C2412_IISCON_IIS_ACTIVE;
		con &= ~S3C2412_IISCON_RXDMA_PAUSE;
		con &= ~S3C2412_IISCON_RXCH_PAUSE;

		switch (mod & S3C2412_IISMOD_MODE_MASK) {
		case S3C2412_IISMOD_MODE_TXRX:
		case S3C2412_IISMOD_MODE_RXONLY:
			/* do nothing, we are in the right mode */
			break;

		case S3C2412_IISMOD_MODE_TXONLY:
			mod &= ~S3C2412_IISMOD_MODE_MASK;
			mod |= S3C2412_IISMOD_MODE_TXRX;
			break;

		default:
			dev_err(i2s->dev, "RXEN: Invalid MODE in IISMOD\n");
		}

		writel(mod, regs + S3C2412_IISMOD);
		writel(con, regs + S3C2412_IISCON);
	} else {
		/* See txctrl notes on FIFOs. */

		con &= ~S3C2412_IISCON_RXDMA_ACTIVE;
		con |=  S3C2412_IISCON_RXDMA_PAUSE;
		con |=  S3C2412_IISCON_RXCH_PAUSE;

		switch (mod & S3C2412_IISMOD_MODE_MASK) {
		case S3C2412_IISMOD_MODE_RXONLY:
			con &= ~S3C2412_IISCON_IIS_ACTIVE;
			mod &= ~S3C2412_IISMOD_MODE_MASK;
			break;

		case S3C2412_IISMOD_MODE_TXRX:
			mod &= ~S3C2412_IISMOD_MODE_MASK;
			mod |= S3C2412_IISMOD_MODE_TXONLY;
			break;

		default:
			dev_err(i2s->dev, "RXEN: Invalid MODE in IISMOD\n");
		}

		writel(con, regs + S3C2412_IISCON);
		writel(mod, regs + S3C2412_IISMOD);
	}

	fic = readl(regs + S3C2412_IISFIC);
	DBG("%s: IIS: CON=%x MOD=%x FIC=%x\n", __func__, con, mod, fic);
}


/*
 * Wait for the LR signal to allow synchronisation to the L/R clock
 * from the codec. May only be needed for slave mode.
 */
static int s3c2412_snd_lrsync(void)
{
	u32 iiscon;
	unsigned long timeout = jiffies + msecs_to_jiffies(5);

	DBG("Entered %s\n", __func__);

	while (1) {
		iiscon = readl(s3c2412_i2s.regs + S3C2412_IISCON);
		if (iiscon & S3C2412_IISCON_LRINDEX)
			break;

		if (timeout < jiffies) {
			printk(KERN_ERR "%s: timeout\n", __func__);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

/*
 * Check whether CPU is the master or slave
 */
static inline int s3c2412_snd_is_clkmaster(void)
{
	u32 iismod = readl(s3c2412_i2s.regs + S3C2412_IISMOD);

	DBG("Entered %s\n", __func__);

	iismod &= S3C2412_IISMOD_MASTER_MASK;
	return !(iismod == S3C2412_IISMOD_SLAVE);
}

/*
 * Set S3C2412 I2S DAI format
 */
static int s3c2412_i2s_set_fmt(struct snd_soc_cpu_dai *cpu_dai,
			       unsigned int fmt)
{
	u32 iismod;


	DBG("Entered %s\n", __func__);

	iismod = readl(s3c2412_i2s.regs + S3C2412_IISMOD);
	DBG("hw_params r: IISMOD: %x \n", iismod);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iismod &= ~S3C2412_IISMOD_MASTER_MASK;
		iismod |= S3C2412_IISMOD_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		iismod &= ~S3C2412_IISMOD_MASTER_MASK;
		iismod |= S3C2412_IISMOD_MASTER_INTERNAL;
		break;
	default:
		DBG("unknwon master/slave format\n");
		return -EINVAL;
	}

	iismod &= ~S3C2412_IISMOD_SDF_MASK;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		iismod |= S3C2412_IISMOD_SDF_MSB;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iismod |= S3C2412_IISMOD_SDF_LSB;
		break;
	case SND_SOC_DAIFMT_I2S:
		iismod |= S3C2412_IISMOD_SDF_IIS;
		break;
	default:
		DBG("Unknown data format\n");
		return -EINVAL;
	}

	writel(iismod, s3c2412_i2s.regs + S3C2412_IISMOD);
	DBG("hw_params w: IISMOD: %x \n", iismod);
	return 0;
}

static int s3c2412_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	u32 iismod;

	DBG("Entered %s\n", __func__);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		rtd->dai->cpu_dai->dma_data = &s3c2412_i2s_pcm_stereo_out;
	else
		rtd->dai->cpu_dai->dma_data = &s3c2412_i2s_pcm_stereo_in;

	/* Working copies of register */
	iismod = readl(s3c2412_i2s.regs + S3C2412_IISMOD);
	DBG("%s: r: IISMOD: %x\n", __func__, iismod);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		iismod |= S3C2412_IISMOD_8BIT;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		iismod &= ~S3C2412_IISMOD_8BIT;
		break;
	}

	writel(iismod, s3c2412_i2s.regs + S3C2412_IISMOD);
	DBG("%s: w: IISMOD: %x\n", __func__, iismod);
	return 0;
}

static int s3c2412_i2s_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int capture = (substream->stream == SNDRV_PCM_STREAM_CAPTURE);
	unsigned long irqs;
	int ret = 0;

	DBG("Entered %s\n", __func__);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* On start, ensure that the FIFOs are cleared and reset. */

		writel(capture ? S3C2412_IISFIC_RXFLUSH : S3C2412_IISFIC_TXFLUSH,
		       s3c2412_i2s.regs + S3C2412_IISFIC);

		/* clear again, just in case */
		writel(0x0, s3c2412_i2s.regs + S3C2412_IISFIC);

	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (!s3c2412_snd_is_clkmaster()) {
			ret = s3c2412_snd_lrsync();
			if (ret)
				goto exit_err;
		}

		local_irq_save(irqs);

		if (capture)
			s3c2412_snd_rxctrl(1);
		else
			s3c2412_snd_txctrl(1);

		local_irq_restore(irqs);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		local_irq_save(irqs);

		if (capture)
			s3c2412_snd_rxctrl(0);
		else
			s3c2412_snd_txctrl(0);

		local_irq_restore(irqs);
		break;
	default:
		ret = -EINVAL;
		break;
	}

exit_err:
	return ret;
}

/* default table of all avaialable root fs divisors */
static unsigned int s3c2412_iis_fs[] = { 256, 512, 384, 768, 0 };

int s3c2412_iis_calc_rate(struct s3c2412_rate_calc *info,
			  unsigned int *fstab,
			  unsigned int rate, struct clk *clk)
{
	unsigned long clkrate = clk_get_rate(clk);
	unsigned int div;
	unsigned int fsclk;
	unsigned int actual;
	unsigned int fs;
	unsigned int fsdiv;
	signed int deviation = 0;
	unsigned int best_fs = 0;
	unsigned int best_div = 0;
	unsigned int best_rate = 0;
	unsigned int best_deviation = INT_MAX;


	if (fstab == NULL)
		fstab = s3c2412_iis_fs;

	for (fs = 0;; fs++) {
		fsdiv = s3c2412_iis_fs[fs];

		if (fsdiv == 0)
			break;

		fsclk = clkrate / fsdiv;
		div = fsclk / rate;

		if ((fsclk % rate) > (rate / 2))
			div++;

		if (div <= 1)
			continue;

		actual = clkrate / (fsdiv * div);
		deviation = actual - rate;

		printk(KERN_DEBUG "%dfs: div %d => result %d, deviation %d\n",
		       fsdiv, div, actual, deviation);

		deviation = abs(deviation);

		if (deviation < best_deviation) {
			best_fs = fsdiv;
			best_div = div;
			best_rate = actual;
			best_deviation = deviation;
		}

		if (deviation == 0)
			break;
	}

	printk(KERN_DEBUG "best: fs=%d, div=%d, rate=%d\n",
	       best_fs, best_div, best_rate);

	info->fs_div = best_fs;
	info->clk_div = best_div;

	return 0;
}
EXPORT_SYMBOL_GPL(s3c2412_iis_calc_rate);

/*
 * Set S3C2412 Clock source
 */
static int s3c2412_i2s_set_sysclk(struct snd_soc_cpu_dai *cpu_dai,
				  int clk_id, unsigned int freq, int dir)
{
	u32 iismod = readl(s3c2412_i2s.regs + S3C2412_IISMOD);

	DBG("%s(%p, %d, %u, %d)\n", __func__, cpu_dai, clk_id,
	    freq, dir);

	switch (clk_id) {
	case S3C2412_CLKSRC_PCLK:
		iismod &= ~S3C2412_IISMOD_MASTER_MASK;
		iismod |= S3C2412_IISMOD_MASTER_INTERNAL;
		break;
	case S3C2412_CLKSRC_I2SCLK:
		iismod &= ~S3C2412_IISMOD_MASTER_MASK;
		iismod |= S3C2412_IISMOD_MASTER_EXTERNAL;
		break;
	default:
		return -EINVAL;
	}

	writel(iismod, s3c2412_i2s.regs + S3C2412_IISMOD);
	return 0;
}

/*
 * Set S3C2412 Clock dividers
 */
static int s3c2412_i2s_set_clkdiv(struct snd_soc_cpu_dai *cpu_dai,
				  int div_id, int div)
{
	struct s3c2412_i2s_info *i2s = &s3c2412_i2s;
	u32 reg;

	DBG("%s(%p, %d, %d)\n", __func__, cpu_dai, div_id, div);

	switch (div_id) {
	case S3C2412_DIV_BCLK:
		reg = readl(i2s->regs + S3C2412_IISMOD);
		reg &= ~S3C2412_IISMOD_BCLK_MASK;
		writel(reg | div, i2s->regs + S3C2412_IISMOD);

		DBG("%s: MOD=%08x\n", __func__, readl(i2s->regs + S3C2412_IISMOD));
		break;

	case S3C2412_DIV_RCLK:
		if (div > 3) {
			/* convert value to bit field */

			switch (div) {
			case 256:
				div = S3C2412_IISMOD_RCLK_256FS;
				break;

			case 384:
				div = S3C2412_IISMOD_RCLK_384FS;
				break;

			case 512:
				div = S3C2412_IISMOD_RCLK_512FS;
				break;

			case 768:
				div = S3C2412_IISMOD_RCLK_768FS;
				break;

			default:
				return -EINVAL;
			}
		}

		reg = readl(s3c2412_i2s.regs + S3C2412_IISMOD);
		reg &= ~S3C2412_IISMOD_RCLK_MASK;
		writel(reg | div, i2s->regs + S3C2412_IISMOD);
		DBG("%s: MOD=%08x\n", __func__, readl(i2s->regs + S3C2412_IISMOD));
		break;

	case S3C2412_DIV_PRESCALER:
		if (div >= 0) {
			writel((div << 8) | S3C2412_IISPSR_PSREN,
			       i2s->regs + S3C2412_IISPSR);
		} else {
			writel(0x0, i2s->regs + S3C2412_IISPSR);
		}
		DBG("%s: PSR=%08x\n", __func__, readl(i2s->regs + S3C2412_IISPSR));
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

struct clk *s3c2412_get_iisclk(void)
{
	return s3c2412_i2s.iis_clk;
}
EXPORT_SYMBOL_GPL(s3c2412_get_iisclk);


static int s3c2412_i2s_probe(struct platform_device *pdev)
{
	DBG("Entered %s\n", __func__);

	s3c2412_i2s.dev = &pdev->dev;

	s3c2412_i2s.regs = ioremap(S3C2410_PA_IIS, 0x100);
	if (s3c2412_i2s.regs == NULL)
		return -ENXIO;

	s3c2412_i2s.iis_pclk = clk_get(&pdev->dev, "iis");
	if (s3c2412_i2s.iis_pclk == NULL) {
		DBG("failed to get iis_clock\n");
		iounmap(s3c2412_i2s.regs);
		return -ENODEV;
	}

	s3c2412_i2s.iis_cclk = clk_get(&pdev->dev, "i2sclk");
	if (s3c2412_i2s.iis_cclk == NULL) {
		DBG("failed to get i2sclk clock\n");
		iounmap(s3c2412_i2s.regs);
		return -ENODEV;
	}

	clk_set_parent(s3c2412_i2s.iis_cclk, clk_get(NULL, "mpll"));

	clk_enable(s3c2412_i2s.iis_pclk);
	clk_enable(s3c2412_i2s.iis_cclk);

	s3c2412_i2s.iis_clk = s3c2412_i2s.iis_pclk;

	/* Configure the I2S pins in correct mode */
	s3c2410_gpio_cfgpin(S3C2410_GPE0, S3C2410_GPE0_I2SLRCK);
	s3c2410_gpio_cfgpin(S3C2410_GPE1, S3C2410_GPE1_I2SSCLK);
	s3c2410_gpio_cfgpin(S3C2410_GPE2, S3C2410_GPE2_CDCLK);
	s3c2410_gpio_cfgpin(S3C2410_GPE3, S3C2410_GPE3_I2SSDI);
	s3c2410_gpio_cfgpin(S3C2410_GPE4, S3C2410_GPE4_I2SSDO);

	s3c2412_snd_txctrl(0);
	s3c2412_snd_rxctrl(0);

	return 0;
}

#ifdef CONFIG_PM
static int s3c2412_i2s_suspend(struct platform_device *dev,
			      struct snd_soc_cpu_dai *dai)
{
	struct s3c2412_i2s_info *i2s = &s3c2412_i2s;
	u32 iismod;

	if (dai->active) {
		i2s->suspend_iismod = readl(i2s->regs + S3C2412_IISMOD);
		i2s->suspend_iiscon = readl(i2s->regs + S3C2412_IISCON);
		i2s->suspend_iispsr = readl(i2s->regs + S3C2412_IISPSR);

		/* some basic suspend checks */

		iismod = readl(i2s->regs + S3C2412_IISMOD);

		if (iismod & S3C2412_IISCON_RXDMA_ACTIVE)
			dev_warn(&dev->dev, "%s: RXDMA active?\n", __func__);

		if (iismod & S3C2412_IISCON_TXDMA_ACTIVE)
			dev_warn(&dev->dev, "%s: TXDMA active?\n", __func__);

		if (iismod & S3C2412_IISCON_IIS_ACTIVE)
			dev_warn(&dev->dev, "%s: IIS active\n", __func__);
	}

	return 0;
}

static int s3c2412_i2s_resume(struct platform_device *pdev,
			      struct snd_soc_cpu_dai *dai)
{
	struct s3c2412_i2s_info *i2s = &s3c2412_i2s;

	dev_info(&pdev->dev, "dai_active %d, IISMOD %08x, IISCON %08x\n",
		 dai->active, i2s->suspend_iismod, i2s->suspend_iiscon);

	if (dai->active) {
		writel(i2s->suspend_iiscon, i2s->regs + S3C2412_IISCON);
		writel(i2s->suspend_iismod, i2s->regs + S3C2412_IISMOD);
		writel(i2s->suspend_iispsr, i2s->regs + S3C2412_IISPSR);

		writel(S3C2412_IISFIC_RXFLUSH | S3C2412_IISFIC_TXFLUSH,
		       i2s->regs + S3C2412_IISFIC);

		ndelay(250);
		writel(0x0, i2s->regs + S3C2412_IISFIC);

	}

	return 0;
}
#else
#define s3c2412_i2s_suspend NULL
#define s3c2412_i2s_resume  NULL
#endif /* CONFIG_PM */

#define S3C2412_I2S_RATES \
	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_16000 | \
	SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

struct snd_soc_cpu_dai s3c2412_i2s_dai = {
	.name	= "s3c2412-i2s",
	.id	= 0,
	.type	= SND_SOC_DAI_I2S,
	.probe	= s3c2412_i2s_probe,
	.suspend = s3c2412_i2s_suspend,
	.resume = s3c2412_i2s_resume,
	.playback = {
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= S3C2412_I2S_RATES,
		.formats	= SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= S3C2412_I2S_RATES,
		.formats	= SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = {
		.trigger	= s3c2412_i2s_trigger,
		.hw_params	= s3c2412_i2s_hw_params,
	},
	.dai_ops = {
		.set_fmt	= s3c2412_i2s_set_fmt,
		.set_clkdiv	= s3c2412_i2s_set_clkdiv,
		.set_sysclk	= s3c2412_i2s_set_sysclk,
	},
};
EXPORT_SYMBOL_GPL(s3c2412_i2s_dai);

/* Module information */
MODULE_AUTHOR("Ben Dooks, <ben@simtec.co.uk>");
MODULE_DESCRIPTION("S3C2412 I2S SoC Interface");
MODULE_LICENSE("GPL");
