/*
 * s3c24xx-i2s.c  --  ALSA Soc Audio Layer
 *
 * (c) 2006 Wolfson Microelectronics PLC.
 * Graeme Gregory graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 * Copyright 2004-2005 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/module.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

#include <mach/gpio-samsung.h>
#include <plat/gpio-cfg.h>
#include "regs-iis.h"

#include "dma.h"
#include "s3c24xx-i2s.h"

#include <linux/platform_data/asoc-s3c.h>

static struct s3c_dma_params s3c24xx_i2s_pcm_stereo_out = {
	.ch_name	= "tx",
	.dma_size	= 2,
};

static struct s3c_dma_params s3c24xx_i2s_pcm_stereo_in = {
	.ch_name	= "rx",
	.dma_size	= 2,
};

struct s3c24xx_i2s_info {
	void __iomem	*regs;
	struct clk	*iis_clk;
	u32		iiscon;
	u32		iismod;
	u32		iisfcon;
	u32		iispsr;
};
static struct s3c24xx_i2s_info s3c24xx_i2s;

static void s3c24xx_snd_txctrl(int on)
{
	u32 iisfcon;
	u32 iiscon;
	u32 iismod;

	pr_debug("Entered %s\n", __func__);

	iisfcon = readl(s3c24xx_i2s.regs + S3C2410_IISFCON);
	iiscon  = readl(s3c24xx_i2s.regs + S3C2410_IISCON);
	iismod  = readl(s3c24xx_i2s.regs + S3C2410_IISMOD);

	pr_debug("r: IISCON: %x IISMOD: %x IISFCON: %x\n", iiscon, iismod, iisfcon);

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

	pr_debug("w: IISCON: %x IISMOD: %x IISFCON: %x\n", iiscon, iismod, iisfcon);
}

static void s3c24xx_snd_rxctrl(int on)
{
	u32 iisfcon;
	u32 iiscon;
	u32 iismod;

	pr_debug("Entered %s\n", __func__);

	iisfcon = readl(s3c24xx_i2s.regs + S3C2410_IISFCON);
	iiscon  = readl(s3c24xx_i2s.regs + S3C2410_IISCON);
	iismod  = readl(s3c24xx_i2s.regs + S3C2410_IISMOD);

	pr_debug("r: IISCON: %x IISMOD: %x IISFCON: %x\n", iiscon, iismod, iisfcon);

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

	pr_debug("w: IISCON: %x IISMOD: %x IISFCON: %x\n", iiscon, iismod, iisfcon);
}

/*
 * Wait for the LR signal to allow synchronisation to the L/R clock
 * from the codec. May only be needed for slave mode.
 */
static int s3c24xx_snd_lrsync(void)
{
	u32 iiscon;
	int timeout = 50; /* 5ms */

	pr_debug("Entered %s\n", __func__);

	while (1) {
		iiscon = readl(s3c24xx_i2s.regs + S3C2410_IISCON);
		if (iiscon & S3C2410_IISCON_LRINDEX)
			break;

		if (!timeout--)
			return -ETIMEDOUT;
		udelay(100);
	}

	return 0;
}

/*
 * Check whether CPU is the master or slave
 */
static inline int s3c24xx_snd_is_clkmaster(void)
{
	pr_debug("Entered %s\n", __func__);

	return (readl(s3c24xx_i2s.regs + S3C2410_IISMOD) & S3C2410_IISMOD_SLAVE) ? 0:1;
}

/*
 * Set S3C24xx I2S DAI format
 */
static int s3c24xx_i2s_set_fmt(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
	u32 iismod;

	pr_debug("Entered %s\n", __func__);

	iismod = readl(s3c24xx_i2s.regs + S3C2410_IISMOD);
	pr_debug("hw_params r: IISMOD: %x \n", iismod);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iismod |= S3C2410_IISMOD_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		iismod &= ~S3C2410_IISMOD_SLAVE;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_LEFT_J:
		iismod |= S3C2410_IISMOD_MSB;
		break;
	case SND_SOC_DAIFMT_I2S:
		iismod &= ~S3C2410_IISMOD_MSB;
		break;
	default:
		return -EINVAL;
	}

	writel(iismod, s3c24xx_i2s.regs + S3C2410_IISMOD);
	pr_debug("hw_params w: IISMOD: %x \n", iismod);
	return 0;
}

static int s3c24xx_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_dmaengine_dai_dma_data *dma_data;
	u32 iismod;

	pr_debug("Entered %s\n", __func__);

	dma_data = snd_soc_dai_get_dma_data(dai, substream);

	/* Working copies of register */
	iismod = readl(s3c24xx_i2s.regs + S3C2410_IISMOD);
	pr_debug("hw_params r: IISMOD: %x\n", iismod);

	switch (params_width(params)) {
	case 8:
		iismod &= ~S3C2410_IISMOD_16BIT;
		dma_data->addr_width = 1;
		break;
	case 16:
		iismod |= S3C2410_IISMOD_16BIT;
		dma_data->addr_width = 2;
		break;
	default:
		return -EINVAL;
	}

	writel(iismod, s3c24xx_i2s.regs + S3C2410_IISMOD);
	pr_debug("hw_params w: IISMOD: %x\n", iismod);
	return 0;
}

static int s3c24xx_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *dai)
{
	int ret = 0;

	pr_debug("Entered %s\n", __func__);

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
static int s3c24xx_i2s_set_sysclk(struct snd_soc_dai *cpu_dai,
	int clk_id, unsigned int freq, int dir)
{
	u32 iismod = readl(s3c24xx_i2s.regs + S3C2410_IISMOD);

	pr_debug("Entered %s\n", __func__);

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
static int s3c24xx_i2s_set_clkdiv(struct snd_soc_dai *cpu_dai,
	int div_id, int div)
{
	u32 reg;

	pr_debug("Entered %s\n", __func__);

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

static int s3c24xx_i2s_probe(struct snd_soc_dai *dai)
{
	pr_debug("Entered %s\n", __func__);

	samsung_asoc_init_dma_data(dai, &s3c24xx_i2s_pcm_stereo_out,
		&s3c24xx_i2s_pcm_stereo_in);

	s3c24xx_i2s.iis_clk = devm_clk_get(dai->dev, "iis");
	if (IS_ERR(s3c24xx_i2s.iis_clk)) {
		pr_err("failed to get iis_clock\n");
		return PTR_ERR(s3c24xx_i2s.iis_clk);
	}
	clk_prepare_enable(s3c24xx_i2s.iis_clk);

	/* Configure the I2S pins (GPE0...GPE4) in correct mode */
	s3c_gpio_cfgall_range(S3C2410_GPE(0), 5, S3C_GPIO_SFN(2),
			      S3C_GPIO_PULL_NONE);

	writel(S3C2410_IISCON_IISEN, s3c24xx_i2s.regs + S3C2410_IISCON);

	s3c24xx_snd_txctrl(0);
	s3c24xx_snd_rxctrl(0);

	return 0;
}

#ifdef CONFIG_PM
static int s3c24xx_i2s_suspend(struct snd_soc_dai *cpu_dai)
{
	pr_debug("Entered %s\n", __func__);

	s3c24xx_i2s.iiscon = readl(s3c24xx_i2s.regs + S3C2410_IISCON);
	s3c24xx_i2s.iismod = readl(s3c24xx_i2s.regs + S3C2410_IISMOD);
	s3c24xx_i2s.iisfcon = readl(s3c24xx_i2s.regs + S3C2410_IISFCON);
	s3c24xx_i2s.iispsr = readl(s3c24xx_i2s.regs + S3C2410_IISPSR);

	clk_disable_unprepare(s3c24xx_i2s.iis_clk);

	return 0;
}

static int s3c24xx_i2s_resume(struct snd_soc_dai *cpu_dai)
{
	pr_debug("Entered %s\n", __func__);
	clk_prepare_enable(s3c24xx_i2s.iis_clk);

	writel(s3c24xx_i2s.iiscon, s3c24xx_i2s.regs + S3C2410_IISCON);
	writel(s3c24xx_i2s.iismod, s3c24xx_i2s.regs + S3C2410_IISMOD);
	writel(s3c24xx_i2s.iisfcon, s3c24xx_i2s.regs + S3C2410_IISFCON);
	writel(s3c24xx_i2s.iispsr, s3c24xx_i2s.regs + S3C2410_IISPSR);

	return 0;
}
#else
#define s3c24xx_i2s_suspend NULL
#define s3c24xx_i2s_resume NULL
#endif


#define S3C24XX_I2S_RATES \
	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_16000 | \
	SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

static const struct snd_soc_dai_ops s3c24xx_i2s_dai_ops = {
	.trigger	= s3c24xx_i2s_trigger,
	.hw_params	= s3c24xx_i2s_hw_params,
	.set_fmt	= s3c24xx_i2s_set_fmt,
	.set_clkdiv	= s3c24xx_i2s_set_clkdiv,
	.set_sysclk	= s3c24xx_i2s_set_sysclk,
};

static struct snd_soc_dai_driver s3c24xx_i2s_dai = {
	.probe = s3c24xx_i2s_probe,
	.suspend = s3c24xx_i2s_suspend,
	.resume = s3c24xx_i2s_resume,
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
	.ops = &s3c24xx_i2s_dai_ops,
};

static const struct snd_soc_component_driver s3c24xx_i2s_component = {
	.name		= "s3c24xx-i2s",
};

static int s3c24xx_iis_dev_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	struct s3c_audio_pdata *pdata = dev_get_platdata(&pdev->dev);

	if (!pdata) {
		dev_err(&pdev->dev, "missing platform data");
		return -ENXIO;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Can't get IO resource.\n");
		return -ENOENT;
	}
	s3c24xx_i2s.regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(s3c24xx_i2s.regs))
		return PTR_ERR(s3c24xx_i2s.regs);

	s3c24xx_i2s_pcm_stereo_out.dma_addr = res->start + S3C2410_IISFIFO;
	s3c24xx_i2s_pcm_stereo_out.slave = pdata->dma_playback;
	s3c24xx_i2s_pcm_stereo_in.dma_addr = res->start + S3C2410_IISFIFO;
	s3c24xx_i2s_pcm_stereo_in.slave = pdata->dma_capture;

	ret = devm_snd_soc_register_component(&pdev->dev,
			&s3c24xx_i2s_component, &s3c24xx_i2s_dai, 1);
	if (ret) {
		pr_err("failed to register the dai\n");
		return ret;
	}

	ret = samsung_asoc_dma_platform_register(&pdev->dev,
						 pdata->dma_filter,
						 NULL, NULL);
	if (ret)
		pr_err("failed to register the dma: %d\n", ret);

	return ret;
}

static struct platform_driver s3c24xx_iis_driver = {
	.probe  = s3c24xx_iis_dev_probe,
	.driver = {
		.name = "s3c24xx-iis",
	},
};

module_platform_driver(s3c24xx_iis_driver);

/* Module information */
MODULE_AUTHOR("Ben Dooks, <ben@simtec.co.uk>");
MODULE_DESCRIPTION("s3c24xx I2S SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:s3c24xx-iis");
