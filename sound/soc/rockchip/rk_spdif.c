/*
 * Rockchip S/PDIF ALSA SoC Digital Audio Interface(DAI)  driver
 *
 * Copyright (C) 2015 Fuzhou Rockchip Electronics Co., Ltd
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/version.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <linux/rockchip/iomap.h>
#include <linux/rockchip/grf.h>

#include <asm/dma.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include <linux/spinlock.h>
#include "rk_pcm.h"

/*
 * channel status register
 * 192 frame channel status bits: include 384 subframe bits
 */
#define SPDIF_CHNSR00_ADDR	0xC0
#define SPDIF_CHNSR01_ADDR	0xC4
#define SPDIF_CHNSR02_ADDR	0xC8
#define SPDIF_CHNSR03_ADDR	0xCC
#define SPDIF_CHNSR04_ADDR	0xD0
#define SPDIF_CHNSR05_ADDR	0xD4
#define SPDIF_CHNSR06_ADDR	0xD8
#define SPDIF_CHNSR07_ADDR	0xDC
#define SPDIF_CHNSR08_ADDR	0xE0
#define SPDIF_CHNSR09_ADDR	0xE4
#define SPDIF_CHNSR10_ADDR	0xE8
#define SPDIF_CHNSR11_ADDR	0xEC

/*
 * according to iec958, we only care about
 * the first meaningful 5 bytes(40 bits)
 */
#define CHNSTA_BYTES		(5)
#define BIT_1_LPCM		(0X0<<1)
#define BIT_1_NLPCM		(0x1<<1)

/* sample word length bit 32~35 */
#define CHNS_SAMPLE_WORD_LEN_16 (0x2)
#define CHNS_SAMPLE_WORD_LEN_24	(0xb)

/* sample frequency bit 24~27 */
#define CHNS_SAMPLE_FREQ_22P05K	(0X4)
#define CHNS_SAMPLE_FREQ_44P1K	(0X0)
#define CHNS_SAMPLE_FREQ_88P2K	(0X8)
#define CHNS_SAMPLE_FREQ_176P4K	(0Xc)
#define CHNS_SAMPLE_FREQ_24K	(0X6)
#define CHNS_SAMPLE_FREQ_48K	(0X2)
#define CHNS_SAMPLE_FREQ_96K	(0Xa)
#define CHNS_SAMPLE_FREQ_192K	(0Xe)
#define CHNS_SAMPLE_FREQ_32K	(0X3)
#define CHNS_SAMPLE_FREQ_768K	(0X9)

/* Registers */
#define CFGR			0x00
#define SDBLR			0x04
#define DMACR			0x08
#define INTCR			0x0C
#define INTSR			0x10
#define XFER			0x18
#define SMPDR			0x20

/* transfer configuration register */
#define CFGR_VALID_DATA_16bit		(0x0 << 0)
#define CFGR_VALID_DATA_20bit		(0x1 << 0)
#define CFGR_VALID_DATA_24bit		(0x2 << 0)
#define CFGR_VALID_DATA_MASK		(0x3 << 0)
#define CFGR_HALFWORD_TX_ENABLE		(0x1 << 2)
#define CFGR_HALFWORD_TX_DISABLE	(0x0 << 2)
#define CFGR_HALFWORD_TX_MASK		(0x1 << 2)
#define CFGR_JUSTIFIED_RIGHT		(0x0 << 3)
#define CFGR_JUSTIFIED_LEFT		(0x1 << 3)
#define CFGR_JUSTIFIED_MASK		(0x1 << 3)
#define CFGR_CSE_DISABLE		(0x0 << 6)
#define CFGR_CSE_ENABLE			(0x1 << 6)
#define CFGR_CSE_MASK			(0x1 << 6)
#define CFGR_MCLK_CLR			(0x1 << 7)
#define CFGR_LINEAR_PCM			(0x0 << 8)
#define CFGR_NON_LINEAR_PCM		(0x1 << 8)
#define CFGR_LINEAR_MASK		(0x1 << 8)
#define CFGR_PRE_CHANGE_ENALBLE		(0x1 << 9)
#define CFGR_PRE_CHANGE_DISABLE		(0x0 << 9)
#define CFGR_PRE_CHANGE_MASK		(0x1 << 9)
#define CFGR_CLK_RATE_MASK		(0xFF << 16)

/* transfer start register */
#define XFER_TRAN_STOP			(0x0 << 0)
#define XFER_TRAN_START			(0x1 << 0)
#define XFER_MASK			(0x1 << 0)

/* dma control register */
#define DMACR_TRAN_DMA_DISABLE		(0x0 << 5)
#define DMACR_TRAN_DMA_ENABLE		(0x1 << 5)
#define DMACR_TRAN_DMA_CTL_MASK		(0x1 << 5)
#define DMACR_TRAN_DATA_LEVEL		(0x10)
#define DMACR_TRAN_DATA_LEVEL_MASK	(0x1F)
#define DMACR_TRAN_DMA_MASK		(0x3F)
#define DMA_DATA_LEVEL_16		(0x10)

/* interrupt control register */
#define INTCR_SDBEIE_DISABLE		(0x0 << 4)
#define INTCR_SDBEIE_ENABLE		(0x1 << 4)
#define INTCR_SDBEIE_MASK		(0x1 << 4)

/* size * width: 16*4 = 64 bytes */
#define SPDIF_DMA_BURST_SIZE		(16)

struct rockchip_spdif_info {
	spinlock_t lock;/*lock parmeter setting.*/
	void __iomem *regs;
	unsigned long clk_rate;
	struct clk *hclk;
	struct clk *clk;
	struct device *dev;
	struct snd_dmaengine_dai_dma_data dma_playback;
	u32 cfgr;
	u32 dmac;
};

static inline struct rockchip_spdif_info *to_info(struct snd_soc_dai *cpu_dai)
{
	return snd_soc_dai_get_drvdata(cpu_dai);
}

static void spdif_snd_txctrl(struct rockchip_spdif_info *spdif, int on)
{
	void __iomem *regs = spdif->regs;
	u32 dmacr, xfer;

	xfer = readl(regs + XFER) & (~XFER_MASK);
	dmacr = readl(regs + DMACR) & (~DMACR_TRAN_DMA_CTL_MASK);

	if (on) {
		xfer |= XFER_TRAN_START;
		dmacr |= DMACR_TRAN_DMA_ENABLE;
		dmacr |= spdif->dmac;
		writel(spdif->cfgr, regs + CFGR);
		writel(dmacr, regs + DMACR);
		writel(xfer, regs + XFER);
	} else {
		xfer &= XFER_TRAN_STOP;
		dmacr &= DMACR_TRAN_DMA_DISABLE;
		writel(xfer, regs + XFER);
		writel(dmacr, regs + DMACR);
		writel(CFGR_MCLK_CLR, regs + CFGR);
	}

	dev_dbg(spdif->dev, "on: %d, xfer = 0x%x, dmacr = 0x%x\n",
		on, readl(regs + XFER), readl(regs + DMACR));
}

static int spdif_set_syclk(struct snd_soc_dai *cpu_dai, int clk_id,
			   unsigned int freq, int dir)
{
	struct rockchip_spdif_info *spdif = to_info(cpu_dai);

	dev_dbg(spdif->dev, "%s: sysclk = %d\n", __func__, freq);

	spdif->clk_rate = freq;
	clk_set_rate(spdif->clk, freq);

	return 0;
}

static int spdif_trigger(struct snd_pcm_substream *substream, int cmd,
			 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct rockchip_spdif_info *spdif = to_info(rtd->cpu_dai);
	unsigned long flags;

	dev_dbg(spdif->dev, "%s: cmd: %d\n", __func__, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		spin_lock_irqsave(&spdif->lock, flags);
		spdif_snd_txctrl(spdif, 1);
		spin_unlock_irqrestore(&spdif->lock, flags);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		spin_lock_irqsave(&spdif->lock, flags);
		spdif_snd_txctrl(spdif, 0);
		spin_unlock_irqrestore(&spdif->lock, flags);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int spdif_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct snd_soc_dai *dai)
{
	struct rockchip_spdif_info *spdif = to_info(dai);
	void __iomem *regs = spdif->regs;
	unsigned long flags;
	unsigned int val;
	u32 cfgr, dmac, intcr, chnregval;
	char chnsta[CHNSTA_BYTES];

	dev_dbg(spdif->dev, "%s\n", __func__);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dai->playback_dma_data = &spdif->dma_playback;
	} else {
		dev_err(spdif->dev, "capture is not supported\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&spdif->lock, flags);

	cfgr = readl(regs + CFGR);

	cfgr &= ~CFGR_VALID_DATA_MASK;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		cfgr |= CFGR_VALID_DATA_16bit;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		cfgr |= CFGR_VALID_DATA_20bit;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		cfgr |= CFGR_VALID_DATA_24bit;
		break;
	default:
		goto err;
	}

	cfgr &= ~CFGR_HALFWORD_TX_MASK;
	cfgr |= CFGR_HALFWORD_TX_ENABLE;

	/* no need divder, let set_syclk care about this */
	cfgr &= ~CFGR_CLK_RATE_MASK;
	cfgr |= (0x0<<16);

	cfgr &= ~CFGR_JUSTIFIED_MASK;
	cfgr |= CFGR_JUSTIFIED_RIGHT;

	cfgr &= ~CFGR_CSE_MASK;
	cfgr |= CFGR_CSE_ENABLE;

	cfgr &= ~CFGR_LINEAR_MASK;
	cfgr |= CFGR_LINEAR_PCM;

	cfgr &= ~CFGR_PRE_CHANGE_MASK;
	cfgr |= CFGR_PRE_CHANGE_ENALBLE;

	spdif->cfgr = cfgr;
	writel(cfgr, regs + CFGR);

	intcr = readl(regs + INTCR) & (~INTCR_SDBEIE_MASK);
	intcr |= INTCR_SDBEIE_DISABLE;
	writel(intcr, regs + INTCR);

	dmac = readl(regs + DMACR) & (~DMACR_TRAN_DATA_LEVEL_MASK);
	dmac |= DMA_DATA_LEVEL_16;
	spdif->dmac = dmac;
	writel(dmac, regs + DMACR);

	/* channel status bit */
	memset(chnsta, 0x0, CHNSTA_BYTES);
	switch (params_rate(params)) {
	case 44100:
		val = CHNS_SAMPLE_FREQ_44P1K;
		break;
	case 48000:
		val = CHNS_SAMPLE_FREQ_48K;
		break;
	case 88200:
		val = CHNS_SAMPLE_FREQ_88P2K;
		break;
	case 96000:
		val = CHNS_SAMPLE_FREQ_96K;
		break;
	case 176400:
		val = CHNS_SAMPLE_FREQ_176P4K;
		break;
	case 192000:
		val = CHNS_SAMPLE_FREQ_192K;
		break;
	default:
		val = CHNS_SAMPLE_FREQ_44P1K;
		break;
	}

	chnsta[0] |= BIT_1_LPCM;
	chnsta[3] |= val;
	chnsta[4] |= ((~val)<<4 | CHNS_SAMPLE_WORD_LEN_16);

	chnregval = (chnsta[4] << 16) | (chnsta[4]);
	writel(chnregval, regs + SPDIF_CHNSR02_ADDR);

	chnregval = (chnsta[3] << 24) | (chnsta[3] << 8);
	writel(chnregval, regs + SPDIF_CHNSR01_ADDR);

	chnregval = (chnsta[1] << 24) | (chnsta[0] << 16) |
				(chnsta[1] << 8) | (chnsta[0]);
	writel(chnregval, regs + SPDIF_CHNSR00_ADDR);

	spin_unlock_irqrestore(&spdif->lock, flags);

	return 0;
err:
	spin_unlock_irqrestore(&spdif->lock, flags);
	return -EINVAL;
}

#ifdef CONFIG_PM
static int spdif_suspend(struct snd_soc_dai *cpu_dai)
{
	struct rockchip_spdif_info *spdif = to_info(cpu_dai);

	dev_dbg(spdif->dev, "%s\n", __func__);
	return 0;
}

static int spdif_resume(struct snd_soc_dai *cpu_dai)
{
	struct rockchip_spdif_info *spdif = to_info(cpu_dai);

	dev_dbg(spdif->dev, "%s\n", __func__);
	return 0;
}
#else
#define spdif_suspend NULL
#define spdif_resume NULL
#endif

static struct snd_soc_dai_ops spdif_dai_ops = {
	.set_sysclk	= spdif_set_syclk,
	.trigger	= spdif_trigger,
	.hw_params	= spdif_hw_params,
};

struct snd_soc_dai_driver rockchip_spdif_dai = {
	.name = "rockchip-spdif",
	.playback = {
		.stream_name = "SPDIF Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S20_3LE |
			   SNDRV_PCM_FMTBIT_S24_LE, },
	.ops = &spdif_dai_ops,
	.suspend = spdif_suspend,
	.resume = spdif_resume,
};

static const struct snd_soc_component_driver rockchip_spdif_component = {
	.name = "rockchip-spdif",
};

static int spdif_probe(struct platform_device *pdev)
{
	struct resource *memregion;
	struct resource *mem_res;
	struct rockchip_spdif_info *spdif;
	int ret;

	spdif = devm_kzalloc(&pdev->dev, sizeof(
		struct rockchip_spdif_info), GFP_KERNEL);
	if (!spdif) {
		dev_err(&pdev->dev, "Can't allocate spdif info\n");
		return -ENOMEM;
	}

	spdif->dev = &pdev->dev;
	platform_set_drvdata(pdev, spdif);

	spin_lock_init(&spdif->lock);

	/* get spdif register region. */
	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "No memory resource\n");
		ret = -ENOENT;
		goto err_;
	}
	memregion = devm_request_mem_region(&pdev->dev,
					    mem_res->start,
					    resource_size(mem_res),
					    "rockchip-spdif");
	if (!memregion) {
		dev_err(&pdev->dev, "Memory region already claimed\n");
		ret = -EBUSY;
		goto err_;
	}
	spdif->regs = devm_ioremap(&pdev->dev,
				   memregion->start,
				   resource_size(memregion));
	if (!spdif->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto err_;
	}

	/* get spdif clock and init. */
	spdif->hclk = devm_clk_get(&pdev->dev, "spdif_hclk");
	if (IS_ERR(spdif->hclk)) {
		dev_err(&pdev->dev, "Can't retrieve spdif hclk\n");
		spdif->hclk = NULL;
	}
	clk_prepare_enable(spdif->hclk);

	spdif->clk = devm_clk_get(&pdev->dev, "spdif_mclk");
	if (IS_ERR(spdif->clk)) {
		dev_err(&pdev->dev, "Can't retrieve spdif mclk\n");
		ret = -ENOMEM;
		goto err_;
	}
	/* init freq */
	clk_set_rate(spdif->clk, 11289600);
	clk_prepare_enable(spdif->clk);

	spdif->dma_playback.addr = mem_res->start + SMPDR;
	spdif->dma_playback.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	spdif->dma_playback.maxburst = SPDIF_DMA_BURST_SIZE;

	ret = snd_soc_register_component(&pdev->dev,
					 &rockchip_spdif_component,
					 &rockchip_spdif_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI: %d\n", ret);
		ret = -ENOMEM;
		goto err_;
	}

	ret = rockchip_pcm_platform_register(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM: %d\n", ret);
		goto err_;
	}

	dev_info(&pdev->dev, "spdif ready.\n");

	return 0;

err_:
	platform_set_drvdata(pdev, NULL);

	return ret;
}

static int spdif_remove(struct platform_device *pdev)
{
	rockchip_pcm_platform_unregister(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_spdif_match[] = {
	{ .compatible = "rockchip-spdif", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_spdif_match);
#endif

static struct platform_driver rockchip_spdif_driver = {
	.probe	= spdif_probe,
	.remove	= spdif_remove,
	.driver	= {
		.name	= "rockchip-spdif",
		.of_match_table = of_match_ptr(rockchip_spdif_match),
	},
};
module_platform_driver(rockchip_spdif_driver);

MODULE_AUTHOR("Sugar <sugar.zhang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip S/PDIF Controller Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:rockchip-spdif");
