/*$_FOR_ROCKCHIP_RBOX_$*/
/*$_rbox_$_modify_$_huangzhibao for spdif output*/

/* sound/soc/rockchip/rk_spdif.c
 *
 * ALSA SoC Audio Layer - rockchip S/PDIF Controller driver
 *
 * Copyright (c) 2010 rockchip Electronics Co. Ltd
 *		http://www.rockchip.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#undef  DEBUG_SPDIF
#define DEBUG_SPDIF 0

#if DEBUG_SPDIF
#define RK_SPDIF_DBG(x...) pr_info("rk_spdif:"x)
#else
#define RK_SPDIF_DBG(x...) do { } while (0)
#endif

/* Registers */
#define CFGR                  0x00
#define SDBLR                 0x04
#define DMACR                 0x08
#define INTCR                 0x0C
#define INTSR                 0x10
#define XFER                  0x18
#define SMPDR                 0x20

#define SPDIF_CHNSR00_ADDR    0xC0
#define SPDIF_CHNSR01_ADDR    0xC4
#define SPDIF_CHNSR02_ADDR    0xC8
#define SPDIF_CHNSR03_ADDR    0xCC
#define SPDIF_CHNSR04_ADDR    0xD0
#define SPDIF_CHNSR05_ADDR    0xD4
#define SPDIF_CHNSR06_ADDR    0xD8
#define SPDIF_CHNSR07_ADDR    0xDC
#define SPDIF_CHNSR08_ADDR    0xE0
#define SPDIF_CHNSR09_ADDR    0xE4
#define SPDIF_CHNSR10_ADDR    0xE8
#define SPDIF_CHNSR11_ADDR    0xEC

#define SPDIF_BURST_INFO       0x100
#define SPDIF_REPETTION       0x104

#define DATA_OUTBUF           0x20

#define SPDIF_CHANNEL_SEL_8CH	((0x2<<16)|(0x0<<0))
#define SPDIF_CHANNEL_SEL_2CH	((0x2<<16)|(0x2<<0))

/* burst_info bit0:6 AC-3:0x01, DTS-I -II -III:11,12,13 */
#define burst_info_DATA_TYPE_AC3     0x01
#define burst_info_DATA_TYPE_EAC3    0x15
#define BURST_INFO_DATA_TYPE_DTS_I   0x0b

#define CFGR_MASK                   0x0ffffff
#define CFGR_VALID_DATA_16bit       (00)
#define CFGR_VALID_DATA_20bit       (01)
#define CFGR_VALID_DATA_24bit       (10)
#define CFGR_VALID_DATA_MASK        (11)

#define CFGR_HALFWORD_TX_ENABLE     (0x1<<2)
#define CFGR_HALFWORD_TX_DISABLE    (0x0<<2)
#define CFGR_HALFWORD_TX_MASK       (0x1<<2)

#define CFGR_CLK_RATE_MASK          (0xFF<<16)

#define CFGR_JUSTIFIED_RIGHT        (0<<3)
#define CFGR_JUSTIFIED_LEFT         (1<<3)
#define CFGR_JUSTIFIED_MASK         (1<<3)

/* CSE:channel status enable */
/* The bit should be set to 1 when the channel conveys non-linear PCM */
#define CFGR_CSE_DISABLE            (0<<6)
#define CFGR_CSE_ENABLE             (1<<6)
#define CFGR_CSE_MASK               (1<<6)

#define CFGR_MCLK_CLR               (1<<7)

#define CFGR_LINEAR_PCM             (0<<8)
#define CFGR_NON_LINEAR_PCM         (1<<8)
#define CFGR_LINEAR_MASK            (1<<8)

/* support 7.1 amplifier,new */
#define CFGR_PRE_CHANGE_ENALBLE     (1<<9)
#define CFGR_PRE_CHANGE_DISABLE     (0<<9)
#define CFGR_PRE_CHANGE_MASK        (1<<9)

#define XFER_TRAN_STOP              (0)
#define XFER_TRAN_START             (1)
#define XFER_MASK                   (1)

#define DMACR_TRAN_DMA_DISABLE      (0<<5)
#define DMACR_TRAN_DMA_ENABLE       (1<<5)
#define DMACR_TRAN_DMA_CTL_MASK     (1<<5)

#define DMACR_TRAN_DATA_LEVEL       0x10
#define DMACR_TRAN_DATA_LEVEL_MASK  0x1F
#define DMACR_TRAN_DMA_MASK         0x3F

/* Sample Date Buffer empty interrupt enable, new */
#define INTCR_SDBEIE_DISABLE        (0<<4)
#define INTCR_SDBEIE_ENABLE         (1<<4)
#define INTCR_SDBEIE_MASK           (1<<4)

struct rockchip_spdif_info {
	spinlock_t	lock;/*lock parmeter setting.*/
	void __iomem	*regs;
	unsigned long	clk_rate;
	struct clk	*hclk;
	struct clk	*clk;
	struct snd_dmaengine_dai_dma_data	dma_playback;
};

static inline struct rockchip_spdif_info *to_info(struct snd_soc_dai *cpu_dai)
{
	return snd_soc_dai_get_drvdata(cpu_dai);
}

static void spdif_snd_txctrl(struct rockchip_spdif_info *spdif, int on)
{
	void __iomem *regs = spdif->regs;
	u32 opr, xfer;

	RK_SPDIF_DBG("Entered %s\n", __func__);

	xfer = readl(regs + XFER) & XFER_MASK;
	opr = readl(regs + DMACR) & DMACR_TRAN_DMA_MASK
		& (~DMACR_TRAN_DMA_CTL_MASK);

	if (on) {
		xfer |= XFER_TRAN_START;
		opr |= DMACR_TRAN_DMA_ENABLE;
		writel(xfer, regs + XFER);
		writel(opr, regs + DMACR);
		RK_SPDIF_DBG("on xfer=0x%x,opr=0x%x\n", readl(
			regs + XFER), readl(regs + DMACR));
	} else {
		xfer &= ~XFER_TRAN_START;
		opr &= ~DMACR_TRAN_DMA_ENABLE;
		writel(xfer, regs + XFER);
		writel(opr, regs + DMACR);
		writel(1<<7, regs + CFGR);
		RK_SPDIF_DBG("off xfer=0x%x,opr=0x%x\n", readl(
			regs + XFER), readl(regs + DMACR));
	}
}

static int spdif_set_syclk(struct snd_soc_dai *
	cpu_dai, int clk_id, unsigned int freq, int dir)
{
	struct rockchip_spdif_info *spdif = to_info(cpu_dai);

	RK_SPDIF_DBG("Entered %s sysclk=%d\n", __func__, freq);

	spdif->clk_rate = freq;
	clk_set_rate(spdif->clk, freq);

	return 0;
}

static int spdif_trigger(struct snd_pcm_substream *
		substream, int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct rockchip_spdif_info *spdif = to_info(rtd->cpu_dai);
	unsigned long flags;

	RK_SPDIF_DBG("Entered %s\n", __func__);

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

static int spdif_hw_params(struct snd_pcm_substream *
		substream, struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct rockchip_spdif_info *spdif = to_info(dai);
	void __iomem *regs = spdif->regs;
	unsigned long flags;
	int cfgr, dmac, intcr, chnsr_byte[5] = {0};
	int data_type, err_flag, data_len, data_info;
	int bs_num, repetition, burst_info;

	RK_SPDIF_DBG("Entered %s\n", __func__);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dai->playback_dma_data = &spdif->dma_playback;
	} else {
		pr_err("spdif:Capture is not supported\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&spdif->lock, flags);

	cfgr = readl(regs + CFGR) & CFGR_VALID_DATA_MASK;

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

	/* set most MCLK:192kHz */
	cfgr &= ~CFGR_CLK_RATE_MASK;
	cfgr |= (1<<16);

	cfgr &= ~CFGR_JUSTIFIED_MASK;
	cfgr |= CFGR_JUSTIFIED_RIGHT;

	cfgr &= ~CFGR_CSE_MASK;
	cfgr |= CFGR_CSE_DISABLE;

	cfgr &= ~CFGR_LINEAR_MASK;
	cfgr |= CFGR_LINEAR_PCM;

	/* stream type*/
	if (!snd_pcm_format_linear(params_format(params)))
		cfgr |= CFGR_NON_LINEAR_PCM;

	cfgr &= ~CFGR_PRE_CHANGE_MASK;
	cfgr |= CFGR_PRE_CHANGE_ENALBLE;

	writel(cfgr, regs + CFGR);

	intcr = readl(regs + INTCR) & INTCR_SDBEIE_MASK;
	intcr |= INTCR_SDBEIE_ENABLE;
	writel(intcr, regs + INTCR);

	dmac = readl(regs + DMACR) & DMACR_TRAN_DMA_MASK &
		(~DMACR_TRAN_DATA_LEVEL_MASK);
	dmac |= 0x10;
	writel(dmac, regs + DMACR);

	/*
	* channel byte 0:
	* Bit 1
	* 1 Main data field represents linear PCM samples.
	* 0 Main data field used for purposes other purposes.
	*/
	chnsr_byte[0] = (0x0) | (0x0 << 1) |
		(0x0 << 2) | (0x0 << 3) |
		(0x00 << 6);
	chnsr_byte[1] = (0x0);
	chnsr_byte[2] = (0x0) | (0x0 << 4) | (0x0 << 6);
	chnsr_byte[3] = (0x00) | (0x00);
	chnsr_byte[4] = (0x0 << 4) | (0x01 << 1 | 0x0);

	/* set stream type */
	if (!snd_pcm_format_linear(params_format(params))) {
		chnsr_byte[0] |= (0x1<<1);
		chnsr_byte[4] = (0x0<<4)|(0x00<<1|0x0);
	}
	writel((chnsr_byte[4] << 16)
			| (chnsr_byte[4]),
			regs + SPDIF_CHNSR02_ADDR);
	writel((chnsr_byte[3] << 24) | (chnsr_byte[2] << 16) |
		(chnsr_byte[3] << 8) | (chnsr_byte[2]),
		regs + SPDIF_CHNSR01_ADDR);
	writel((chnsr_byte[1] << 24) | (chnsr_byte[0] << 16) |
		(chnsr_byte[1] << 8) | (chnsr_byte[0]),
		regs + SPDIF_CHNSR00_ADDR);

	/* set non-linear params */
	if (!snd_pcm_format_linear(params_format(params))) {
		switch (params_format(params)) {
		case SNDRV_NON_LINEAR_PCM_FORMAT_AC3:
			/* bit0:6 AC-3:0x01, DTS-I -II -III:11,12,13 */
			data_type = burst_info_DATA_TYPE_AC3;
			/*
			* repetition:AC-3:1536
			* DTS-I -II -III:512,1024,2048 EAC3:6144
			*/
			repetition = 1536;
			break;
		case SNDRV_NON_LINEAR_PCM_FORMAT_DTS_I:
			data_type = BURST_INFO_DATA_TYPE_DTS_I;
			repetition = 512;
			break;
		case SNDRV_NON_LINEAR_PCM_FORMAT_EAC3:
			data_type = burst_info_DATA_TYPE_EAC3;
			repetition = 6144;
			break;
		default:
			return -EINVAL;
		}
		err_flag = 0x0;
		data_len = params_period_size(params) * 2 * 16;
		data_info = 0;
		bs_num = 0x0;
		burst_info = (data_len << 16) | (bs_num << 13) |
			(data_info << 8) | (err_flag << 7) | data_type;
		writel(burst_info, regs + SPDIF_BURST_INFO);
		writel(repetition, regs + SPDIF_REPETTION);
	}
	spin_unlock_irqrestore(&spdif->lock, flags);

	return 0;
err:
	spin_unlock_irqrestore(&spdif->lock, flags);
	return -EINVAL;
}

#ifdef CONFIG_PM
static int spdif_suspend(struct snd_soc_dai *cpu_dai)
{
	RK_SPDIF_DBG("spdif:Entered %s\n", __func__);

	return 0;
}

static int spdif_resume(struct snd_soc_dai *cpu_dai)
{
	RK_SPDIF_DBG("spdif:Entered %s\n", __func__);

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
		.rates = (SNDRV_PCM_RATE_32000 |
				SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_96000),
		.formats = SNDRV_PCM_FMTBIT_S16_LE|
		SNDRV_PCM_FMTBIT_S20_3LE|
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
	/*struct device_node *spdif_np = pdev->dev.of_node;*/
	struct resource *memregion;
	struct resource *mem_res;
	struct rockchip_spdif_info *spdif;
	int ret;

	RK_SPDIF_DBG("Entered %s\n", __func__);

	spdif = devm_kzalloc(&pdev->dev, sizeof(
		struct rockchip_spdif_info), GFP_KERNEL);
	if (!spdif) {
		dev_err(&pdev->dev, "Can't allocate spdif info\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, spdif);

	spin_lock_init(&spdif->lock);

	/* get spdif register regoin. */
	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "No memory resource\n");
		ret = -ENOENT;
		goto err_;
	}
	memregion = devm_request_mem_region(&pdev->
			dev, mem_res->start,
			resource_size(mem_res), "rockchip-spdif");
	if (!memregion) {
		dev_err(&pdev->dev, "Memory region already claimed\n");
		ret = -EBUSY;
		goto err_;
	}
	spdif->regs = devm_ioremap(&pdev->dev, memregion->
			start, resource_size(memregion));
	if (!spdif->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto err_;
	}

	/* get spdif clock and init. */
	spdif->hclk = devm_clk_get(&pdev->dev, "spdif_hclk");
	if (IS_ERR(spdif->hclk)) {
		dev_err(&pdev->dev, "Can't retrieve spdif hclock\n");
		spdif->hclk = NULL;
	}
	clk_prepare_enable(spdif->hclk);

	/* get spdif clock and init. */
	spdif->clk = devm_clk_get(&pdev->dev, "spdif_mclk");
	if (IS_ERR(spdif->clk)) {
		dev_err(&pdev->dev, "Can't retrieve spdif clock\n");
		ret = -ENOMEM;
		goto err_;
	}
	clk_set_rate(spdif->clk, 12288000);
	clk_set_rate(spdif->clk, 11289600);
	clk_prepare_enable(spdif->clk);

	spdif->dma_playback.addr = mem_res->start + DATA_OUTBUF;
	spdif->dma_playback.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	spdif->dma_playback.maxburst = 4;

	/* set dev name to driver->name for sound card register */
	dev_set_name(&pdev->dev, "%s", pdev->dev.driver->name);

	ret = snd_soc_register_component(&pdev->
		dev, &rockchip_spdif_component,	&rockchip_spdif_dai, 1);
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

	RK_SPDIF_DBG("spdif:spdif probe ok!\n");

	return 0;

err_:
	platform_set_drvdata(pdev, NULL);

	return ret;
}

static int spdif_remove(struct platform_device *pdev)
{
	RK_SPDIF_DBG("Entered %s\n", __func__);

	rockchip_pcm_platform_unregister(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id exynos_spdif_match[] = {
	{ .compatible = "rockchip-spdif"},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_spdif_match);
#endif

static struct platform_driver rockchip_spdif_driver = {
	.probe	= spdif_probe,
	.remove	= spdif_remove,
	.driver	= {
		.name	= "rockchip-spdif",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(exynos_spdif_match),
	},
};
module_platform_driver(rockchip_spdif_driver);

MODULE_AUTHOR("Seungwhan Youn, <sw.youn@rockchip.com>");
MODULE_DESCRIPTION("rockchip S/PDIF Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rockchip-spdif");
