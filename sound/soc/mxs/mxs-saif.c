/*
 * Copyright 2011 Freescale Semiconductor, Inc.
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
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/fsl/mxs-dma.h>
#include <linux/pinctrl/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/saif.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <mach/mxs.h>

#include "mxs-saif.h"

static struct mxs_saif *mxs_saif[2];

/*
 * SAIF is a little different with other normal SOC DAIs on clock using.
 *
 * For MXS, two SAIF modules are instantiated on-chip.
 * Each SAIF has a set of clock pins and can be operating in master
 * mode simultaneously if they are connected to different off-chip codecs.
 * Also, one of the two SAIFs can master or drive the clock pins while the
 * other SAIF, in slave mode, receives clocking from the master SAIF.
 * This also means that both SAIFs must operate at the same sample rate.
 *
 * We abstract this as each saif has a master, the master could be
 * himself or other saifs. In the generic saif driver, saif does not need
 * to know the different clkmux. Saif only needs to know who is his master
 * and operating his master to generate the proper clock rate for him.
 * The master id is provided in mach-specific layer according to different
 * clkmux setting.
 */

static int mxs_saif_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
			int clk_id, unsigned int freq, int dir)
{
	struct mxs_saif *saif = snd_soc_dai_get_drvdata(cpu_dai);

	switch (clk_id) {
	case MXS_SAIF_MCLK:
		saif->mclk = freq;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * Since SAIF may work on EXTMASTER mode, IOW, it's working BITCLK&LRCLK
 * is provided by other SAIF, we provide a interface here to get its master
 * from its master_id.
 * Note that the master could be himself.
 */
static inline struct mxs_saif *mxs_saif_get_master(struct mxs_saif * saif)
{
	return mxs_saif[saif->master_id];
}

/*
 * Set SAIF clock and MCLK
 */
static int mxs_saif_set_clk(struct mxs_saif *saif,
				  unsigned int mclk,
				  unsigned int rate)
{
	u32 scr;
	int ret;
	struct mxs_saif *master_saif;

	dev_dbg(saif->dev, "mclk %d rate %d\n", mclk, rate);

	/* Set master saif to generate proper clock */
	master_saif = mxs_saif_get_master(saif);
	if (!master_saif)
		return -EINVAL;

	dev_dbg(saif->dev, "master saif%d\n", master_saif->id);

	/* Checking if can playback and capture simutaneously */
	if (master_saif->ongoing && rate != master_saif->cur_rate) {
		dev_err(saif->dev,
			"can not change clock, master saif%d(rate %d) is ongoing\n",
			master_saif->id, master_saif->cur_rate);
		return -EINVAL;
	}

	scr = __raw_readl(master_saif->base + SAIF_CTRL);
	scr &= ~BM_SAIF_CTRL_BITCLK_MULT_RATE;
	scr &= ~BM_SAIF_CTRL_BITCLK_BASE_RATE;

	/*
	 * Set SAIF clock
	 *
	 * The SAIF clock should be either 384*fs or 512*fs.
	 * If MCLK is used, the SAIF clk ratio need to match mclk ratio.
	 *  For 32x mclk, set saif clk as 512*fs.
	 *  For 48x mclk, set saif clk as 384*fs.
	 *
	 * If MCLK is not used, we just set saif clk to 512*fs.
	 */
	clk_prepare_enable(master_saif->clk);

	if (master_saif->mclk_in_use) {
		if (mclk % 32 == 0) {
			scr &= ~BM_SAIF_CTRL_BITCLK_BASE_RATE;
			ret = clk_set_rate(master_saif->clk, 512 * rate);
		} else if (mclk % 48 == 0) {
			scr |= BM_SAIF_CTRL_BITCLK_BASE_RATE;
			ret = clk_set_rate(master_saif->clk, 384 * rate);
		} else {
			/* SAIF MCLK should be either 32x or 48x */
			clk_disable_unprepare(master_saif->clk);
			return -EINVAL;
		}
	} else {
		ret = clk_set_rate(master_saif->clk, 512 * rate);
		scr &= ~BM_SAIF_CTRL_BITCLK_BASE_RATE;
	}

	clk_disable_unprepare(master_saif->clk);

	if (ret)
		return ret;

	master_saif->cur_rate = rate;

	if (!master_saif->mclk_in_use) {
		__raw_writel(scr, master_saif->base + SAIF_CTRL);
		return 0;
	}

	/*
	 * Program the over-sample rate for MCLK output
	 *
	 * The available MCLK range is 32x, 48x... 512x. The rate
	 * could be from 8kHz to 192kH.
	 */
	switch (mclk / rate) {
	case 32:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(4);
		break;
	case 64:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(3);
		break;
	case 128:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(2);
		break;
	case 256:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(1);
		break;
	case 512:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(0);
		break;
	case 48:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(3);
		break;
	case 96:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(2);
		break;
	case 192:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(1);
		break;
	case 384:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(0);
		break;
	default:
		return -EINVAL;
	}

	__raw_writel(scr, master_saif->base + SAIF_CTRL);

	return 0;
}

/*
 * Put and disable MCLK.
 */
int mxs_saif_put_mclk(unsigned int saif_id)
{
	struct mxs_saif *saif = mxs_saif[saif_id];
	u32 stat;

	if (!saif)
		return -EINVAL;

	stat = __raw_readl(saif->base + SAIF_STAT);
	if (stat & BM_SAIF_STAT_BUSY) {
		dev_err(saif->dev, "error: busy\n");
		return -EBUSY;
	}

	clk_disable_unprepare(saif->clk);

	/* disable MCLK output */
	__raw_writel(BM_SAIF_CTRL_CLKGATE,
		saif->base + SAIF_CTRL + MXS_SET_ADDR);
	__raw_writel(BM_SAIF_CTRL_RUN,
		saif->base + SAIF_CTRL + MXS_CLR_ADDR);

	saif->mclk_in_use = 0;
	return 0;
}

/*
 * Get MCLK and set clock rate, then enable it
 *
 * This interface is used for codecs who are using MCLK provided
 * by saif.
 */
int mxs_saif_get_mclk(unsigned int saif_id, unsigned int mclk,
					unsigned int rate)
{
	struct mxs_saif *saif = mxs_saif[saif_id];
	u32 stat;
	int ret;
	struct mxs_saif *master_saif;

	if (!saif)
		return -EINVAL;

	/* Clear Reset */
	__raw_writel(BM_SAIF_CTRL_SFTRST,
		saif->base + SAIF_CTRL + MXS_CLR_ADDR);

	/* FIXME: need clear clk gate for register r/w */
	__raw_writel(BM_SAIF_CTRL_CLKGATE,
		saif->base + SAIF_CTRL + MXS_CLR_ADDR);

	master_saif = mxs_saif_get_master(saif);
	if (saif != master_saif) {
		dev_err(saif->dev, "can not get mclk from a non-master saif\n");
		return -EINVAL;
	}

	stat = __raw_readl(saif->base + SAIF_STAT);
	if (stat & BM_SAIF_STAT_BUSY) {
		dev_err(saif->dev, "error: busy\n");
		return -EBUSY;
	}

	saif->mclk_in_use = 1;
	ret = mxs_saif_set_clk(saif, mclk, rate);
	if (ret)
		return ret;

	ret = clk_prepare_enable(saif->clk);
	if (ret)
		return ret;

	/* enable MCLK output */
	__raw_writel(BM_SAIF_CTRL_RUN,
		saif->base + SAIF_CTRL + MXS_SET_ADDR);

	return 0;
}

/*
 * SAIF DAI format configuration.
 * Should only be called when port is inactive.
 */
static int mxs_saif_set_dai_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	u32 scr, stat;
	u32 scr0;
	struct mxs_saif *saif = snd_soc_dai_get_drvdata(cpu_dai);

	stat = __raw_readl(saif->base + SAIF_STAT);
	if (stat & BM_SAIF_STAT_BUSY) {
		dev_err(cpu_dai->dev, "error: busy\n");
		return -EBUSY;
	}

	scr0 = __raw_readl(saif->base + SAIF_CTRL);
	scr0 = scr0 & ~BM_SAIF_CTRL_BITCLK_EDGE & ~BM_SAIF_CTRL_LRCLK_POLARITY \
		& ~BM_SAIF_CTRL_JUSTIFY & ~BM_SAIF_CTRL_DELAY;
	scr = 0;

	/* DAI mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/* data frame low 1clk before data */
		scr |= BM_SAIF_CTRL_DELAY;
		scr &= ~BM_SAIF_CTRL_LRCLK_POLARITY;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		/* data frame high with data */
		scr &= ~BM_SAIF_CTRL_DELAY;
		scr &= ~BM_SAIF_CTRL_LRCLK_POLARITY;
		scr &= ~BM_SAIF_CTRL_JUSTIFY;
		break;
	default:
		return -EINVAL;
	}

	/* DAI clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		scr |= BM_SAIF_CTRL_BITCLK_EDGE;
		scr |= BM_SAIF_CTRL_LRCLK_POLARITY;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		scr |= BM_SAIF_CTRL_BITCLK_EDGE;
		scr &= ~BM_SAIF_CTRL_LRCLK_POLARITY;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		scr &= ~BM_SAIF_CTRL_BITCLK_EDGE;
		scr |= BM_SAIF_CTRL_LRCLK_POLARITY;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		scr &= ~BM_SAIF_CTRL_BITCLK_EDGE;
		scr &= ~BM_SAIF_CTRL_LRCLK_POLARITY;
		break;
	}

	/*
	 * Note: We simply just support master mode since SAIF TX can only
	 * work as master.
	 * Here the master is relative to codec side.
	 * Saif internally could be slave when working on EXTMASTER mode.
	 * We just hide this to machine driver.
	 */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		if (saif->id == saif->master_id)
			scr &= ~BM_SAIF_CTRL_SLAVE_MODE;
		else
			scr |= BM_SAIF_CTRL_SLAVE_MODE;

		__raw_writel(scr | scr0, saif->base + SAIF_CTRL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mxs_saif_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *cpu_dai)
{
	struct mxs_saif *saif = snd_soc_dai_get_drvdata(cpu_dai);
	snd_soc_dai_set_dma_data(cpu_dai, substream, &saif->dma_param);

	/* clear error status to 0 for each re-open */
	saif->fifo_underrun = 0;
	saif->fifo_overrun = 0;

	/* Clear Reset for normal operations */
	__raw_writel(BM_SAIF_CTRL_SFTRST,
		saif->base + SAIF_CTRL + MXS_CLR_ADDR);

	/* clear clock gate */
	__raw_writel(BM_SAIF_CTRL_CLKGATE,
		saif->base + SAIF_CTRL + MXS_CLR_ADDR);

	return 0;
}

/*
 * Should only be called when port is inactive.
 * although can be called multiple times by upper layers.
 */
static int mxs_saif_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *cpu_dai)
{
	struct mxs_saif *saif = snd_soc_dai_get_drvdata(cpu_dai);
	u32 scr, stat;
	int ret;

	/* mclk should already be set */
	if (!saif->mclk && saif->mclk_in_use) {
		dev_err(cpu_dai->dev, "set mclk first\n");
		return -EINVAL;
	}

	stat = __raw_readl(saif->base + SAIF_STAT);
	if (stat & BM_SAIF_STAT_BUSY) {
		dev_err(cpu_dai->dev, "error: busy\n");
		return -EBUSY;
	}

	/*
	 * Set saif clk based on sample rate.
	 * If mclk is used, we also set mclk, if not, saif->mclk is
	 * default 0, means not used.
	 */
	ret = mxs_saif_set_clk(saif, saif->mclk, params_rate(params));
	if (ret) {
		dev_err(cpu_dai->dev, "unable to get proper clk\n");
		return ret;
	}

	scr = __raw_readl(saif->base + SAIF_CTRL);

	scr &= ~BM_SAIF_CTRL_WORD_LENGTH;
	scr &= ~BM_SAIF_CTRL_BITCLK_48XFS_ENABLE;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		scr |= BF_SAIF_CTRL_WORD_LENGTH(0);
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		scr |= BF_SAIF_CTRL_WORD_LENGTH(4);
		scr |= BM_SAIF_CTRL_BITCLK_48XFS_ENABLE;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		scr |= BF_SAIF_CTRL_WORD_LENGTH(8);
		scr |= BM_SAIF_CTRL_BITCLK_48XFS_ENABLE;
		break;
	default:
		return -EINVAL;
	}

	/* Tx/Rx config */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* enable TX mode */
		scr &= ~BM_SAIF_CTRL_READ_MODE;
	} else {
		/* enable RX mode */
		scr |= BM_SAIF_CTRL_READ_MODE;
	}

	__raw_writel(scr, saif->base + SAIF_CTRL);
	return 0;
}

static int mxs_saif_prepare(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *cpu_dai)
{
	struct mxs_saif *saif = snd_soc_dai_get_drvdata(cpu_dai);

	/* enable FIFO error irqs */
	__raw_writel(BM_SAIF_CTRL_FIFO_ERROR_IRQ_EN,
		saif->base + SAIF_CTRL + MXS_SET_ADDR);

	return 0;
}

static int mxs_saif_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *cpu_dai)
{
	struct mxs_saif *saif = snd_soc_dai_get_drvdata(cpu_dai);
	struct mxs_saif *master_saif;
	u32 delay;

	master_saif = mxs_saif_get_master(saif);
	if (!master_saif)
		return -EINVAL;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dev_dbg(cpu_dai->dev, "start\n");

		clk_enable(master_saif->clk);
		if (!master_saif->mclk_in_use)
			__raw_writel(BM_SAIF_CTRL_RUN,
				master_saif->base + SAIF_CTRL + MXS_SET_ADDR);

		/*
		 * If the saif's master is not himself, we also need to enable
		 * itself clk for its internal basic logic to work.
		 */
		if (saif != master_saif) {
			clk_enable(saif->clk);
			__raw_writel(BM_SAIF_CTRL_RUN,
				saif->base + SAIF_CTRL + MXS_SET_ADDR);
		}

		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			/*
			 * write a data to saif data register to trigger
			 * the transfer
			 */
			__raw_writel(0, saif->base + SAIF_DATA);
		} else {
			/*
			 * read a data from saif data register to trigger
			 * the receive
			 */
			__raw_readl(saif->base + SAIF_DATA);
		}

		master_saif->ongoing = 1;

		dev_dbg(saif->dev, "CTRL 0x%x STAT 0x%x\n",
			__raw_readl(saif->base + SAIF_CTRL),
			__raw_readl(saif->base + SAIF_STAT));

		dev_dbg(master_saif->dev, "CTRL 0x%x STAT 0x%x\n",
			__raw_readl(master_saif->base + SAIF_CTRL),
			__raw_readl(master_saif->base + SAIF_STAT));
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dev_dbg(cpu_dai->dev, "stop\n");

		/* wait a while for the current sample to complete */
		delay = USEC_PER_SEC / master_saif->cur_rate;

		if (!master_saif->mclk_in_use) {
			__raw_writel(BM_SAIF_CTRL_RUN,
				master_saif->base + SAIF_CTRL + MXS_CLR_ADDR);
			udelay(delay);
		}
		clk_disable(master_saif->clk);

		if (saif != master_saif) {
			__raw_writel(BM_SAIF_CTRL_RUN,
				saif->base + SAIF_CTRL + MXS_CLR_ADDR);
			udelay(delay);
			clk_disable(saif->clk);
		}

		master_saif->ongoing = 0;

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#define MXS_SAIF_RATES		SNDRV_PCM_RATE_8000_192000
#define MXS_SAIF_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
	SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops mxs_saif_dai_ops = {
	.startup = mxs_saif_startup,
	.trigger = mxs_saif_trigger,
	.prepare = mxs_saif_prepare,
	.hw_params = mxs_saif_hw_params,
	.set_sysclk = mxs_saif_set_dai_sysclk,
	.set_fmt = mxs_saif_set_dai_fmt,
};

static int mxs_saif_dai_probe(struct snd_soc_dai *dai)
{
	struct mxs_saif *saif = dev_get_drvdata(dai->dev);

	snd_soc_dai_set_drvdata(dai, saif);

	return 0;
}

static struct snd_soc_dai_driver mxs_saif_dai = {
	.name = "mxs-saif",
	.probe = mxs_saif_dai_probe,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = MXS_SAIF_RATES,
		.formats = MXS_SAIF_FORMATS,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = MXS_SAIF_RATES,
		.formats = MXS_SAIF_FORMATS,
	},
	.ops = &mxs_saif_dai_ops,
};

static irqreturn_t mxs_saif_irq(int irq, void *dev_id)
{
	struct mxs_saif *saif = dev_id;
	unsigned int stat;

	stat = __raw_readl(saif->base + SAIF_STAT);
	if (!(stat & (BM_SAIF_STAT_FIFO_UNDERFLOW_IRQ |
			BM_SAIF_STAT_FIFO_OVERFLOW_IRQ)))
		return IRQ_NONE;

	if (stat & BM_SAIF_STAT_FIFO_UNDERFLOW_IRQ) {
		dev_dbg(saif->dev, "underrun!!! %d\n", ++saif->fifo_underrun);
		__raw_writel(BM_SAIF_STAT_FIFO_UNDERFLOW_IRQ,
				saif->base + SAIF_STAT + MXS_CLR_ADDR);
	}

	if (stat & BM_SAIF_STAT_FIFO_OVERFLOW_IRQ) {
		dev_dbg(saif->dev, "overrun!!! %d\n", ++saif->fifo_overrun);
		__raw_writel(BM_SAIF_STAT_FIFO_OVERFLOW_IRQ,
				saif->base + SAIF_STAT + MXS_CLR_ADDR);
	}

	dev_dbg(saif->dev, "SAIF_CTRL %x SAIF_STAT %x\n",
	       __raw_readl(saif->base + SAIF_CTRL),
	       __raw_readl(saif->base + SAIF_STAT));

	return IRQ_HANDLED;
}

static int mxs_saif_probe(struct platform_device *pdev)
{
	struct resource *iores, *dmares;
	struct mxs_saif *saif;
	struct mxs_saif_platform_data *pdata;
	struct pinctrl *pinctrl;
	int ret = 0;

	if (pdev->id >= ARRAY_SIZE(mxs_saif))
		return -EINVAL;

	saif = devm_kzalloc(&pdev->dev, sizeof(*saif), GFP_KERNEL);
	if (!saif)
		return -ENOMEM;

	mxs_saif[pdev->id] = saif;
	saif->id = pdev->id;

	pdata = pdev->dev.platform_data;
	if (pdata && !pdata->master_mode) {
		saif->master_id = pdata->master_id;
		if (saif->master_id < 0 ||
			saif->master_id >= ARRAY_SIZE(mxs_saif) ||
			saif->master_id == saif->id) {
			dev_err(&pdev->dev, "get wrong master id\n");
			return -EINVAL;
		}
	} else {
		saif->master_id = saif->id;
	}

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl)) {
		ret = PTR_ERR(pinctrl);
		return ret;
	}

	saif->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(saif->clk)) {
		ret = PTR_ERR(saif->clk);
		dev_err(&pdev->dev, "Cannot get the clock: %d\n",
			ret);
		return ret;
	}

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	saif->base = devm_request_and_ioremap(&pdev->dev, iores);
	if (!saif->base) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENODEV;
		goto failed_get_resource;
	}

	dmares = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!dmares) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "failed to get dma resource: %d\n",
			ret);
		goto failed_get_resource;
	}
	saif->dma_param.chan_num = dmares->start;

	saif->irq = platform_get_irq(pdev, 0);
	if (saif->irq < 0) {
		ret = saif->irq;
		dev_err(&pdev->dev, "failed to get irq resource: %d\n",
			ret);
		goto failed_get_resource;
	}

	saif->dev = &pdev->dev;
	ret = devm_request_irq(&pdev->dev, saif->irq, mxs_saif_irq, 0,
			       "mxs-saif", saif);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq\n");
		goto failed_get_resource;
	}

	saif->dma_param.chan_irq = platform_get_irq(pdev, 1);
	if (saif->dma_param.chan_irq < 0) {
		ret = saif->dma_param.chan_irq;
		dev_err(&pdev->dev, "failed to get dma irq resource: %d\n",
			ret);
		goto failed_get_resource;
	}

	platform_set_drvdata(pdev, saif);

	ret = snd_soc_register_dai(&pdev->dev, &mxs_saif_dai);
	if (ret) {
		dev_err(&pdev->dev, "register DAI failed\n");
		goto failed_get_resource;
	}

	saif->soc_platform_pdev = platform_device_alloc(
					"mxs-pcm-audio", pdev->id);
	if (!saif->soc_platform_pdev) {
		ret = -ENOMEM;
		goto failed_pdev_alloc;
	}

	platform_set_drvdata(saif->soc_platform_pdev, saif);
	ret = platform_device_add(saif->soc_platform_pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to add soc platform device\n");
		goto failed_pdev_add;
	}

	return 0;

failed_pdev_add:
	platform_device_put(saif->soc_platform_pdev);
failed_pdev_alloc:
	snd_soc_unregister_dai(&pdev->dev);
failed_get_resource:
	clk_put(saif->clk);

	return ret;
}

static int __devexit mxs_saif_remove(struct platform_device *pdev)
{
	struct mxs_saif *saif = platform_get_drvdata(pdev);

	platform_device_unregister(saif->soc_platform_pdev);
	snd_soc_unregister_dai(&pdev->dev);
	clk_put(saif->clk);

	return 0;
}

static struct platform_driver mxs_saif_driver = {
	.probe = mxs_saif_probe,
	.remove = __devexit_p(mxs_saif_remove),

	.driver = {
		.name = "mxs-saif",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(mxs_saif_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MXS ASoC SAIF driver");
MODULE_LICENSE("GPL");
