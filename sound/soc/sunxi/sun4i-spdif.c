/*
 * ALSA SoC SPDIF Audio Layer
 *
 * Copyright 2015 Andrea Venturi <be17068@iperbole.bo.it>
 * Copyright 2015 Marcus Cooper <codekipper@gmail.com>
 *
 * Based on the Allwinner SDK driver, released under the GPL.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/regmap.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#define	SUN4I_SPDIF_CTL		(0x00)
	#define SUN4I_SPDIF_CTL_MCLKDIV(v)		((v) << 4) /* v even */
	#define SUN4I_SPDIF_CTL_MCLKOUTEN		BIT(2)
	#define SUN4I_SPDIF_CTL_GEN			BIT(1)
	#define SUN4I_SPDIF_CTL_RESET			BIT(0)

#define SUN4I_SPDIF_TXCFG	(0x04)
	#define SUN4I_SPDIF_TXCFG_SINGLEMOD		BIT(31)
	#define SUN4I_SPDIF_TXCFG_ASS			BIT(17)
	#define SUN4I_SPDIF_TXCFG_NONAUDIO		BIT(16)
	#define SUN4I_SPDIF_TXCFG_TXRATIO(v)		((v) << 4)
	#define SUN4I_SPDIF_TXCFG_TXRATIO_MASK		GENMASK(8, 4)
	#define SUN4I_SPDIF_TXCFG_FMTRVD		GENMASK(3, 2)
	#define SUN4I_SPDIF_TXCFG_FMT16BIT		(0 << 2)
	#define SUN4I_SPDIF_TXCFG_FMT20BIT		(1 << 2)
	#define SUN4I_SPDIF_TXCFG_FMT24BIT		(2 << 2)
	#define SUN4I_SPDIF_TXCFG_CHSTMODE		BIT(1)
	#define SUN4I_SPDIF_TXCFG_TXEN			BIT(0)

#define SUN4I_SPDIF_RXCFG	(0x08)
	#define SUN4I_SPDIF_RXCFG_LOCKFLAG		BIT(4)
	#define SUN4I_SPDIF_RXCFG_CHSTSRC		BIT(3)
	#define SUN4I_SPDIF_RXCFG_CHSTCP		BIT(1)
	#define SUN4I_SPDIF_RXCFG_RXEN			BIT(0)

#define SUN4I_SPDIF_TXFIFO	(0x0C)

#define SUN4I_SPDIF_RXFIFO	(0x10)

#define SUN4I_SPDIF_FCTL	(0x14)
	#define SUN4I_SPDIF_FCTL_FIFOSRC		BIT(31)
	#define SUN4I_SPDIF_FCTL_FTX			BIT(17)
	#define SUN4I_SPDIF_FCTL_FRX			BIT(16)
	#define SUN4I_SPDIF_FCTL_TXTL(v)		((v) << 8)
	#define SUN4I_SPDIF_FCTL_TXTL_MASK		GENMASK(12, 8)
	#define SUN4I_SPDIF_FCTL_RXTL(v)		((v) << 3)
	#define SUN4I_SPDIF_FCTL_RXTL_MASK		GENMASK(7, 3)
	#define SUN4I_SPDIF_FCTL_TXIM			BIT(2)
	#define SUN4I_SPDIF_FCTL_RXOM(v)		((v) << 0)
	#define SUN4I_SPDIF_FCTL_RXOM_MASK		GENMASK(1, 0)

#define SUN4I_SPDIF_FSTA	(0x18)
	#define SUN4I_SPDIF_FSTA_TXE			BIT(14)
	#define SUN4I_SPDIF_FSTA_TXECNTSHT		(8)
	#define SUN4I_SPDIF_FSTA_RXA			BIT(6)
	#define SUN4I_SPDIF_FSTA_RXACNTSHT		(0)

#define SUN4I_SPDIF_INT		(0x1C)
	#define SUN4I_SPDIF_INT_RXLOCKEN		BIT(18)
	#define SUN4I_SPDIF_INT_RXUNLOCKEN		BIT(17)
	#define SUN4I_SPDIF_INT_RXPARERREN		BIT(16)
	#define SUN4I_SPDIF_INT_TXDRQEN			BIT(7)
	#define SUN4I_SPDIF_INT_TXUIEN			BIT(6)
	#define SUN4I_SPDIF_INT_TXOIEN			BIT(5)
	#define SUN4I_SPDIF_INT_TXEIEN			BIT(4)
	#define SUN4I_SPDIF_INT_RXDRQEN			BIT(2)
	#define SUN4I_SPDIF_INT_RXOIEN			BIT(1)
	#define SUN4I_SPDIF_INT_RXAIEN			BIT(0)

#define SUN4I_SPDIF_ISTA	(0x20)
	#define SUN4I_SPDIF_ISTA_RXLOCKSTA		BIT(18)
	#define SUN4I_SPDIF_ISTA_RXUNLOCKSTA		BIT(17)
	#define SUN4I_SPDIF_ISTA_RXPARERRSTA		BIT(16)
	#define SUN4I_SPDIF_ISTA_TXUSTA			BIT(6)
	#define SUN4I_SPDIF_ISTA_TXOSTA			BIT(5)
	#define SUN4I_SPDIF_ISTA_TXESTA			BIT(4)
	#define SUN4I_SPDIF_ISTA_RXOSTA			BIT(1)
	#define SUN4I_SPDIF_ISTA_RXASTA			BIT(0)

#define SUN4I_SPDIF_TXCNT	(0x24)

#define SUN4I_SPDIF_RXCNT	(0x28)

#define SUN4I_SPDIF_TXCHSTA0	(0x2C)
	#define SUN4I_SPDIF_TXCHSTA0_CLK(v)		((v) << 28)
	#define SUN4I_SPDIF_TXCHSTA0_SAMFREQ(v)		((v) << 24)
	#define SUN4I_SPDIF_TXCHSTA0_SAMFREQ_MASK	GENMASK(27, 24)
	#define SUN4I_SPDIF_TXCHSTA0_CHNUM(v)		((v) << 20)
	#define SUN4I_SPDIF_TXCHSTA0_CHNUM_MASK		GENMASK(23, 20)
	#define SUN4I_SPDIF_TXCHSTA0_SRCNUM(v)		((v) << 16)
	#define SUN4I_SPDIF_TXCHSTA0_CATACOD(v)		((v) << 8)
	#define SUN4I_SPDIF_TXCHSTA0_MODE(v)		((v) << 6)
	#define SUN4I_SPDIF_TXCHSTA0_EMPHASIS(v)	((v) << 3)
	#define SUN4I_SPDIF_TXCHSTA0_CP			BIT(2)
	#define SUN4I_SPDIF_TXCHSTA0_AUDIO		BIT(1)
	#define SUN4I_SPDIF_TXCHSTA0_PRO		BIT(0)

#define SUN4I_SPDIF_TXCHSTA1	(0x30)
	#define SUN4I_SPDIF_TXCHSTA1_CGMSA(v)		((v) << 8)
	#define SUN4I_SPDIF_TXCHSTA1_ORISAMFREQ(v)	((v) << 4)
	#define SUN4I_SPDIF_TXCHSTA1_ORISAMFREQ_MASK	GENMASK(7, 4)
	#define SUN4I_SPDIF_TXCHSTA1_SAMWORDLEN(v)	((v) << 1)
	#define SUN4I_SPDIF_TXCHSTA1_MAXWORDLEN		BIT(0)

#define SUN4I_SPDIF_RXCHSTA0	(0x34)
	#define SUN4I_SPDIF_RXCHSTA0_CLK(v)		((v) << 28)
	#define SUN4I_SPDIF_RXCHSTA0_SAMFREQ(v)		((v) << 24)
	#define SUN4I_SPDIF_RXCHSTA0_CHNUM(v)		((v) << 20)
	#define SUN4I_SPDIF_RXCHSTA0_SRCNUM(v)		((v) << 16)
	#define SUN4I_SPDIF_RXCHSTA0_CATACOD(v)		((v) << 8)
	#define SUN4I_SPDIF_RXCHSTA0_MODE(v)		((v) << 6)
	#define SUN4I_SPDIF_RXCHSTA0_EMPHASIS(v)	((v) << 3)
	#define SUN4I_SPDIF_RXCHSTA0_CP			BIT(2)
	#define SUN4I_SPDIF_RXCHSTA0_AUDIO		BIT(1)
	#define SUN4I_SPDIF_RXCHSTA0_PRO		BIT(0)

#define SUN4I_SPDIF_RXCHSTA1	(0x38)
	#define SUN4I_SPDIF_RXCHSTA1_CGMSA(v)		((v) << 8)
	#define SUN4I_SPDIF_RXCHSTA1_ORISAMFREQ(v)	((v) << 4)
	#define SUN4I_SPDIF_RXCHSTA1_SAMWORDLEN(v)	((v) << 1)
	#define SUN4I_SPDIF_RXCHSTA1_MAXWORDLEN		BIT(0)

/* Defines for Sampling Frequency */
#define SUN4I_SPDIF_SAMFREQ_44_1KHZ		0x0
#define SUN4I_SPDIF_SAMFREQ_NOT_INDICATED	0x1
#define SUN4I_SPDIF_SAMFREQ_48KHZ		0x2
#define SUN4I_SPDIF_SAMFREQ_32KHZ		0x3
#define SUN4I_SPDIF_SAMFREQ_22_05KHZ		0x4
#define SUN4I_SPDIF_SAMFREQ_24KHZ		0x6
#define SUN4I_SPDIF_SAMFREQ_88_2KHZ		0x8
#define SUN4I_SPDIF_SAMFREQ_76_8KHZ		0x9
#define SUN4I_SPDIF_SAMFREQ_96KHZ		0xa
#define SUN4I_SPDIF_SAMFREQ_176_4KHZ		0xc
#define SUN4I_SPDIF_SAMFREQ_192KHZ		0xe

struct sun4i_spdif_dev {
	struct platform_device *pdev;
	struct clk *spdif_clk;
	struct clk *apb_clk;
	struct reset_control *rst;
	struct snd_soc_dai_driver cpu_dai_drv;
	struct regmap *regmap;
	struct snd_dmaengine_dai_dma_data dma_params_tx;
};

static void sun4i_spdif_configure(struct sun4i_spdif_dev *host)
{
	/* soft reset SPDIF */
	regmap_write(host->regmap, SUN4I_SPDIF_CTL, SUN4I_SPDIF_CTL_RESET);

	/* flush TX FIFO */
	regmap_update_bits(host->regmap, SUN4I_SPDIF_FCTL,
			   SUN4I_SPDIF_FCTL_FTX, SUN4I_SPDIF_FCTL_FTX);

	/* clear TX counter */
	regmap_write(host->regmap, SUN4I_SPDIF_TXCNT, 0);
}

static void sun4i_snd_txctrl_on(struct snd_pcm_substream *substream,
				struct sun4i_spdif_dev *host)
{
	if (substream->runtime->channels == 1)
		regmap_update_bits(host->regmap, SUN4I_SPDIF_TXCFG,
				   SUN4I_SPDIF_TXCFG_SINGLEMOD,
				   SUN4I_SPDIF_TXCFG_SINGLEMOD);

	/* SPDIF TX ENABLE */
	regmap_update_bits(host->regmap, SUN4I_SPDIF_TXCFG,
			   SUN4I_SPDIF_TXCFG_TXEN, SUN4I_SPDIF_TXCFG_TXEN);

	/* DRQ ENABLE */
	regmap_update_bits(host->regmap, SUN4I_SPDIF_INT,
			   SUN4I_SPDIF_INT_TXDRQEN, SUN4I_SPDIF_INT_TXDRQEN);

	/* Global enable */
	regmap_update_bits(host->regmap, SUN4I_SPDIF_CTL,
			   SUN4I_SPDIF_CTL_GEN, SUN4I_SPDIF_CTL_GEN);
}

static void sun4i_snd_txctrl_off(struct snd_pcm_substream *substream,
				 struct sun4i_spdif_dev *host)
{
	/* SPDIF TX DISABLE */
	regmap_update_bits(host->regmap, SUN4I_SPDIF_TXCFG,
			   SUN4I_SPDIF_TXCFG_TXEN, 0);

	/* DRQ DISABLE */
	regmap_update_bits(host->regmap, SUN4I_SPDIF_INT,
			   SUN4I_SPDIF_INT_TXDRQEN, 0);

	/* Global disable */
	regmap_update_bits(host->regmap, SUN4I_SPDIF_CTL,
			   SUN4I_SPDIF_CTL_GEN, 0);
}

static int sun4i_spdif_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *cpu_dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sun4i_spdif_dev *host = snd_soc_dai_get_drvdata(rtd->cpu_dai);

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return -EINVAL;

	sun4i_spdif_configure(host);

	return 0;
}

static int sun4i_spdif_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *cpu_dai)
{
	int ret = 0;
	int fmt;
	unsigned long rate = params_rate(params);
	u32 mclk_div = 0;
	unsigned int mclk = 0;
	u32 reg_val;
	struct sun4i_spdif_dev *host = snd_soc_dai_get_drvdata(cpu_dai);
	struct platform_device *pdev = host->pdev;

	/* Add the PCM and raw data select interface */
	switch (params_channels(params)) {
	case 1: /* PCM mode */
	case 2:
		fmt = 0;
		break;
	case 4: /* raw data mode */
		fmt = SUN4I_SPDIF_TXCFG_NONAUDIO;
		break;
	default:
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		fmt |= SUN4I_SPDIF_TXCFG_FMT16BIT;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		fmt |= SUN4I_SPDIF_TXCFG_FMT20BIT;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		fmt |= SUN4I_SPDIF_TXCFG_FMT24BIT;
		break;
	default:
		return -EINVAL;
	}

	switch (rate) {
	case 22050:
	case 44100:
	case 88200:
	case 176400:
		mclk = 22579200;
		break;
	case 24000:
	case 32000:
	case 48000:
	case 96000:
	case 192000:
		mclk = 24576000;
		break;
	default:
		return -EINVAL;
	}

	ret = clk_set_rate(host->spdif_clk, mclk);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Setting SPDIF clock rate for %d Hz failed!\n", mclk);
		return ret;
	}

	regmap_update_bits(host->regmap, SUN4I_SPDIF_FCTL,
			   SUN4I_SPDIF_FCTL_TXIM, SUN4I_SPDIF_FCTL_TXIM);

	switch (rate) {
	case 22050:
	case 24000:
		mclk_div = 8;
		break;
	case 32000:
		mclk_div = 6;
		break;
	case 44100:
	case 48000:
		mclk_div = 4;
		break;
	case 88200:
	case 96000:
		mclk_div = 2;
		break;
	case 176400:
	case 192000:
		mclk_div = 1;
		break;
	default:
		return -EINVAL;
	}

	reg_val = 0;
	reg_val |= SUN4I_SPDIF_TXCFG_ASS;
	reg_val |= fmt; /* set non audio and bit depth */
	reg_val |= SUN4I_SPDIF_TXCFG_CHSTMODE;
	reg_val |= SUN4I_SPDIF_TXCFG_TXRATIO(mclk_div - 1);
	regmap_write(host->regmap, SUN4I_SPDIF_TXCFG, reg_val);

	return 0;
}

static int sun4i_spdif_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *dai)
{
	int ret = 0;
	struct sun4i_spdif_dev *host = snd_soc_dai_get_drvdata(dai);

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return -EINVAL;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		sun4i_snd_txctrl_on(substream, host);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		sun4i_snd_txctrl_off(substream, host);
		break;

	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int sun4i_spdif_soc_dai_probe(struct snd_soc_dai *dai)
{
	struct sun4i_spdif_dev *host = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, &host->dma_params_tx, NULL);
	return 0;
}

static const struct snd_soc_dai_ops sun4i_spdif_dai_ops = {
	.startup	= sun4i_spdif_startup,
	.trigger	= sun4i_spdif_trigger,
	.hw_params	= sun4i_spdif_hw_params,
};

static const struct regmap_config sun4i_spdif_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = SUN4I_SPDIF_RXCHSTA1,
};

#define SUN4I_RATES	SNDRV_PCM_RATE_8000_192000

#define SUN4I_FORMATS	(SNDRV_PCM_FORMAT_S16_LE | \
				SNDRV_PCM_FORMAT_S20_3LE | \
				SNDRV_PCM_FORMAT_S24_LE)

static struct snd_soc_dai_driver sun4i_spdif_dai = {
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SUN4I_RATES,
		.formats = SUN4I_FORMATS,
	},
	.probe = sun4i_spdif_soc_dai_probe,
	.ops = &sun4i_spdif_dai_ops,
	.name = "spdif",
};

static const struct snd_soc_dapm_widget dit_widgets[] = {
	SND_SOC_DAPM_OUTPUT("spdif-out"),
};

static const struct snd_soc_dapm_route dit_routes[] = {
	{ "spdif-out", NULL, "Playback" },
};

static const struct of_device_id sun4i_spdif_of_match[] = {
	{ .compatible = "allwinner,sun4i-a10-spdif", },
	{ .compatible = "allwinner,sun6i-a31-spdif", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sun4i_spdif_of_match);

static const struct snd_soc_component_driver sun4i_spdif_component = {
	.name		= "sun4i-spdif",
};

static int sun4i_spdif_runtime_suspend(struct device *dev)
{
	struct sun4i_spdif_dev *host  = dev_get_drvdata(dev);

	clk_disable_unprepare(host->spdif_clk);
	clk_disable_unprepare(host->apb_clk);

	return 0;
}

static int sun4i_spdif_runtime_resume(struct device *dev)
{
	struct sun4i_spdif_dev *host  = dev_get_drvdata(dev);

	clk_prepare_enable(host->spdif_clk);
	clk_prepare_enable(host->apb_clk);

	return 0;
}

static int sun4i_spdif_probe(struct platform_device *pdev)
{
	struct sun4i_spdif_dev *host;
	struct resource *res;
	int ret;
	void __iomem *base;

	dev_dbg(&pdev->dev, "Entered %s\n", __func__);

	host = devm_kzalloc(&pdev->dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->pdev = pdev;

	/* Initialize this copy of the CPU DAI driver structure */
	memcpy(&host->cpu_dai_drv, &sun4i_spdif_dai, sizeof(sun4i_spdif_dai));
	host->cpu_dai_drv.name = dev_name(&pdev->dev);

	/* Get the addresses */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	host->regmap = devm_regmap_init_mmio(&pdev->dev, base,
						&sun4i_spdif_regmap_config);

	/* Clocks */
	host->apb_clk = devm_clk_get(&pdev->dev, "apb");
	if (IS_ERR(host->apb_clk)) {
		dev_err(&pdev->dev, "failed to get a apb clock.\n");
		return PTR_ERR(host->apb_clk);
	}

	host->spdif_clk = devm_clk_get(&pdev->dev, "spdif");
	if (IS_ERR(host->spdif_clk)) {
		dev_err(&pdev->dev, "failed to get a spdif clock.\n");
		ret = PTR_ERR(host->spdif_clk);
		goto err_disable_apb_clk;
	}

	host->dma_params_tx.addr = res->start + SUN4I_SPDIF_TXFIFO;
	host->dma_params_tx.maxburst = 8;
	host->dma_params_tx.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;

	platform_set_drvdata(pdev, host);

	if (of_device_is_compatible(pdev->dev.of_node,
				    "allwinner,sun6i-a31-spdif")) {
		host->rst = devm_reset_control_get_optional(&pdev->dev, NULL);
		if (IS_ERR(host->rst) && PTR_ERR(host->rst) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			dev_err(&pdev->dev, "Failed to get reset: %d\n", ret);
			goto err_disable_apb_clk;
		}
		if (!IS_ERR(host->rst))
			reset_control_deassert(host->rst);
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
				&sun4i_spdif_component, &sun4i_spdif_dai, 1);
	if (ret)
		goto err_disable_apb_clk;

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = sun4i_spdif_runtime_resume(&pdev->dev);
		if (ret)
			goto err_unregister;
	}

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret)
		goto err_suspend;
	return 0;
err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		sun4i_spdif_runtime_suspend(&pdev->dev);
err_unregister:
	pm_runtime_disable(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);
err_disable_apb_clk:
	clk_disable_unprepare(host->apb_clk);
	return ret;
}

static int sun4i_spdif_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		sun4i_spdif_runtime_suspend(&pdev->dev);

	snd_soc_unregister_platform(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops sun4i_spdif_pm = {
	SET_RUNTIME_PM_OPS(sun4i_spdif_runtime_suspend,
			   sun4i_spdif_runtime_resume, NULL)
};

static struct platform_driver sun4i_spdif_driver = {
	.driver		= {
		.name	= "sun4i-spdif",
		.of_match_table = of_match_ptr(sun4i_spdif_of_match),
		.pm	= &sun4i_spdif_pm,
	},
	.probe		= sun4i_spdif_probe,
	.remove		= sun4i_spdif_remove,
};

module_platform_driver(sun4i_spdif_driver);

MODULE_AUTHOR("Marcus Cooper <codekipper@gmail.com>");
MODULE_AUTHOR("Andrea Venturi <be17068@iperbole.bo.it>");
MODULE_DESCRIPTION("Allwinner sun4i SPDIF SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sun4i-spdif");
