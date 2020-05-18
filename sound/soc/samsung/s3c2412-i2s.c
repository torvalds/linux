// SPDX-License-Identifier: GPL-2.0+
//
// ALSA Soc Audio Layer - S3C2412 I2S driver
//
// Copyright (c) 2006 Wolfson Microelectronics PLC.
//	Graeme Gregory graeme.gregory@wolfsonmicro.com
//	linux@wolfsonmicro.com
//
// Copyright (c) 2007, 2004-2005 Simtec Electronics
//	http://armlinux.simtec.co.uk/
//	Ben Dooks <ben@simtec.co.uk>

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

#include <mach/gpio-samsung.h>
#include <plat/gpio-cfg.h>

#include "dma.h"
#include "regs-i2s-v2.h"
#include "s3c2412-i2s.h"

#include <linux/platform_data/asoc-s3c.h>

static struct snd_dmaengine_dai_dma_data s3c2412_i2s_pcm_stereo_out = {
	.chan_name	= "tx",
	.addr_width	= 4,
};

static struct snd_dmaengine_dai_dma_data s3c2412_i2s_pcm_stereo_in = {
	.chan_name	= "rx",
	.addr_width	= 4,
};

static struct s3c_i2sv2_info s3c2412_i2s;

static int s3c2412_i2s_probe(struct snd_soc_dai *dai)
{
	int ret;

	pr_debug("Entered %s\n", __func__);

	snd_soc_dai_init_dma_data(dai, &s3c2412_i2s_pcm_stereo_out,
					&s3c2412_i2s_pcm_stereo_in);

	ret = s3c_i2sv2_probe(dai, &s3c2412_i2s, S3C2410_PA_IIS);
	if (ret)
		return ret;

	s3c2412_i2s.dma_capture = &s3c2412_i2s_pcm_stereo_in;
	s3c2412_i2s.dma_playback = &s3c2412_i2s_pcm_stereo_out;

	s3c2412_i2s.iis_cclk = devm_clk_get(dai->dev, "i2sclk");
	if (IS_ERR(s3c2412_i2s.iis_cclk)) {
		pr_err("failed to get i2sclk clock\n");
		ret = PTR_ERR(s3c2412_i2s.iis_cclk);
		goto err;
	}

	/* Set MPLL as the source for IIS CLK */

	clk_set_parent(s3c2412_i2s.iis_cclk, clk_get(NULL, "mpll"));
	ret = clk_prepare_enable(s3c2412_i2s.iis_cclk);
	if (ret)
		goto err;

	/* Configure the I2S pins (GPE0...GPE4) in correct mode */
	s3c_gpio_cfgall_range(S3C2410_GPE(0), 5, S3C_GPIO_SFN(2),
			      S3C_GPIO_PULL_NONE);

	return 0;

err:
	s3c_i2sv2_cleanup(dai, &s3c2412_i2s);

	return ret;
}

static int s3c2412_i2s_remove(struct snd_soc_dai *dai)
{
	clk_disable_unprepare(s3c2412_i2s.iis_cclk);
	s3c_i2sv2_cleanup(dai, &s3c2412_i2s);

	return 0;
}

static int s3c2412_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *cpu_dai)
{
	struct s3c_i2sv2_info *i2s = snd_soc_dai_get_drvdata(cpu_dai);
	u32 iismod;

	pr_debug("Entered %s\n", __func__);

	iismod = readl(i2s->regs + S3C2412_IISMOD);
	pr_debug("%s: r: IISMOD: %x\n", __func__, iismod);

	switch (params_width(params)) {
	case 8:
		iismod |= S3C2412_IISMOD_8BIT;
		break;
	case 16:
		iismod &= ~S3C2412_IISMOD_8BIT;
		break;
	}

	writel(iismod, i2s->regs + S3C2412_IISMOD);
	pr_debug("%s: w: IISMOD: %x\n", __func__, iismod);

	return 0;
}

#ifdef CONFIG_PM
static int s3c2412_i2s_suspend(struct snd_soc_component *component)
{
	struct s3c_i2sv2_info *i2s = snd_soc_component_get_drvdata(component);
	u32 iismod;

	if (component->active) {
		i2s->suspend_iismod = readl(i2s->regs + S3C2412_IISMOD);
		i2s->suspend_iiscon = readl(i2s->regs + S3C2412_IISCON);
		i2s->suspend_iispsr = readl(i2s->regs + S3C2412_IISPSR);

		/* some basic suspend checks */

		iismod = readl(i2s->regs + S3C2412_IISMOD);

		if (iismod & S3C2412_IISCON_RXDMA_ACTIVE)
			pr_warn("%s: RXDMA active?\n", __func__);

		if (iismod & S3C2412_IISCON_TXDMA_ACTIVE)
			pr_warn("%s: TXDMA active?\n", __func__);

		if (iismod & S3C2412_IISCON_IIS_ACTIVE)
			pr_warn("%s: IIS active\n", __func__);
	}

	return 0;
}

static int s3c2412_i2s_resume(struct snd_soc_component *component)
{
	struct s3c_i2sv2_info *i2s = snd_soc_component_get_drvdata(component);

	pr_info("component_active %d, IISMOD %08x, IISCON %08x\n",
		component->active, i2s->suspend_iismod, i2s->suspend_iiscon);

	if (component->active) {
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
#endif

#define S3C2412_I2S_RATES \
	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_16000 | \
	SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

static const struct snd_soc_dai_ops s3c2412_i2s_dai_ops = {
	.hw_params	= s3c2412_i2s_hw_params,
};

static struct snd_soc_dai_driver s3c2412_i2s_dai = {
	.probe		= s3c2412_i2s_probe,
	.remove	= s3c2412_i2s_remove,
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
	.ops = &s3c2412_i2s_dai_ops,
};

static const struct snd_soc_component_driver s3c2412_i2s_component = {
	.name		= "s3c2412-i2s",
	.suspend	= s3c2412_i2s_suspend,
	.resume		= s3c2412_i2s_resume,
};

static int s3c2412_iis_dev_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	struct s3c_audio_pdata *pdata = dev_get_platdata(&pdev->dev);

	if (!pdata) {
		dev_err(&pdev->dev, "missing platform data");
		return -ENXIO;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	s3c2412_i2s.regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(s3c2412_i2s.regs))
		return PTR_ERR(s3c2412_i2s.regs);

	s3c2412_i2s_pcm_stereo_out.addr = res->start + S3C2412_IISTXD;
	s3c2412_i2s_pcm_stereo_out.filter_data = pdata->dma_playback;
	s3c2412_i2s_pcm_stereo_in.addr = res->start + S3C2412_IISRXD;
	s3c2412_i2s_pcm_stereo_in.filter_data = pdata->dma_capture;

	ret = samsung_asoc_dma_platform_register(&pdev->dev,
						 pdata->dma_filter,
						 "tx", "rx", NULL);
	if (ret) {
		pr_err("failed to register the DMA: %d\n", ret);
		return ret;
	}

	ret = s3c_i2sv2_register_component(&pdev->dev, -1,
					   &s3c2412_i2s_component,
					   &s3c2412_i2s_dai);
	if (ret)
		pr_err("failed to register the dai\n");

	return ret;
}

static struct platform_driver s3c2412_iis_driver = {
	.probe  = s3c2412_iis_dev_probe,
	.driver = {
		.name = "s3c2412-iis",
	},
};

module_platform_driver(s3c2412_iis_driver);

/* Module information */
MODULE_AUTHOR("Ben Dooks, <ben@simtec.co.uk>");
MODULE_DESCRIPTION("S3C2412 I2S SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:s3c2412-iis");
