/*
 * Rockchip I2S ALSA SoC Digital Audio Interface(DAI)  driver
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
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/cru.h>
#include <linux/rockchip/grf.h>
#include <linux/slab.h>
#include <asm/dma.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "rk_pcm.h"
#include "rk_i2s.h"

#define CLK_SET_LATER
#define I2S_DEFAULT_FREQ	(11289600)
#define I2S_DMA_BURST_SIZE	(16) /* size * width: 16*4 = 64 bytes */
static DEFINE_SPINLOCK(lock);

#if defined(CONFIG_RK_HDMI) && defined(CONFIG_SND_RK_SOC_HDMI_I2S)
extern int snd_config_hdmi_audio(struct snd_pcm_hw_params *params);
#endif

struct rk_i2s_dev {
	struct device *dev;
	struct clk *clk; /* bclk */
	struct clk *mclk; /*mclk output only */
	struct clk *hclk; /*ahb clk */
	struct snd_dmaengine_dai_dma_data capture_dma_data;
	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct regmap *regmap;
	bool tx_start;
	bool rx_start;
#ifdef CLK_SET_LATER
	struct delayed_work clk_delayed_work;
#endif
};

static inline struct rk_i2s_dev *to_info(struct snd_soc_dai *dai)
{
	return snd_soc_dai_get_drvdata(dai);
}

static void rockchip_snd_txctrl(struct rk_i2s_dev *i2s, int on)
{
	unsigned long flags;
	unsigned int val = 0;
	int retry = 10;

	spin_lock_irqsave(&lock, flags);

	dev_dbg(i2s->dev, "%s: %d: on: %d\n", __func__, __LINE__, on);

	if (on) {
		regmap_update_bits(i2s->regmap, I2S_DMACR,
				   I2S_DMACR_TDE_MASK, I2S_DMACR_TDE_ENABLE);

		regmap_update_bits(i2s->regmap, I2S_XFER,
				   I2S_XFER_TXS_MASK | I2S_XFER_RXS_MASK,
				   I2S_XFER_TXS_START | I2S_XFER_RXS_START);

		i2s->tx_start = true;
	} else {
		i2s->tx_start = false;

		regmap_update_bits(i2s->regmap, I2S_DMACR,
				   I2S_DMACR_TDE_MASK, I2S_DMACR_TDE_DISABLE);


		if (!i2s->rx_start) {
			regmap_update_bits(i2s->regmap, I2S_XFER,
					   I2S_XFER_TXS_MASK |
					   I2S_XFER_RXS_MASK,
					   I2S_XFER_TXS_STOP |
					   I2S_XFER_RXS_STOP);

			regmap_update_bits(i2s->regmap, I2S_CLR,
					   I2S_CLR_TXC_MASK | I2S_CLR_RXC_MASK,
					   I2S_CLR_TXC | I2S_CLR_RXC);

			regmap_read(i2s->regmap, I2S_CLR, &val);

			/* Should wait for clear operation to finish */
			while (val) {
				regmap_read(i2s->regmap, I2S_CLR, &val);
				retry--;
				if (!retry) {
					dev_warn(i2s->dev, "fail to clear\n");
					break;
				}
			}
			dev_dbg(i2s->dev, "%s: %d: stop xfer\n",
				__func__, __LINE__);
		}
	}

	spin_unlock_irqrestore(&lock, flags);
}

static void rockchip_snd_rxctrl(struct rk_i2s_dev *i2s, int on)
{
	unsigned long flags;
	unsigned int val = 0;
	int retry = 10;

	spin_lock_irqsave(&lock, flags);

	dev_dbg(i2s->dev, "%s: %d: on: %d\n", __func__, __LINE__, on);

	if (on) {
		regmap_update_bits(i2s->regmap, I2S_DMACR,
				   I2S_DMACR_RDE_MASK, I2S_DMACR_RDE_ENABLE);

		regmap_update_bits(i2s->regmap, I2S_XFER,
				   I2S_XFER_TXS_MASK | I2S_XFER_RXS_MASK,
				   I2S_XFER_TXS_START | I2S_XFER_RXS_START);

		i2s->rx_start = true;
	} else {
		i2s->rx_start = false;

		regmap_update_bits(i2s->regmap, I2S_DMACR,
				   I2S_DMACR_RDE_MASK, I2S_DMACR_RDE_DISABLE);

		if (!i2s->tx_start) {
			regmap_update_bits(i2s->regmap, I2S_XFER,
					   I2S_XFER_TXS_MASK |
					   I2S_XFER_RXS_MASK,
					   I2S_XFER_TXS_STOP |
					   I2S_XFER_RXS_STOP);

			regmap_update_bits(i2s->regmap, I2S_CLR,
					   I2S_CLR_TXC_MASK | I2S_CLR_RXC_MASK,
					   I2S_CLR_TXC | I2S_CLR_RXC);

			regmap_read(i2s->regmap, I2S_CLR, &val);

			/* Should wait for clear operation to finish */
			while (val) {
				regmap_read(i2s->regmap, I2S_CLR, &val);
				retry--;
				if (!retry) {
					dev_warn(i2s->dev, "fail to clear\n");
					break;
				}
			}
			dev_dbg(i2s->dev, "%s: %d: stop xfer\n",
				__func__, __LINE__);
		}
	}

	spin_unlock_irqrestore(&lock, flags);
}

static int rockchip_i2s_set_fmt(struct snd_soc_dai *cpu_dai,
				unsigned int fmt)
{
	struct rk_i2s_dev *i2s = to_info(cpu_dai);
	unsigned int mask = 0, val = 0;
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);

	mask = I2S_CKR_MSS_MASK;
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		/* Codec is slave, so set cpu master */
		val = I2S_CKR_MSS_MASTER;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		/* Codec is master, so set cpu slave */
		val = I2S_CKR_MSS_SLAVE;
		break;
	default:
		ret = -EINVAL;
		goto err_fmt;
	}

	regmap_update_bits(i2s->regmap, I2S_CKR, mask, val);

	mask = I2S_TXCR_IBM_MASK;
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		val = I2S_TXCR_IBM_RSJM;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val = I2S_TXCR_IBM_LSJM;
		break;
	case SND_SOC_DAIFMT_I2S:
		val = I2S_TXCR_IBM_NORMAL;
		break;
	default:
		ret = -EINVAL;
		goto err_fmt;
	}

	regmap_update_bits(i2s->regmap, I2S_TXCR, mask, val);

	mask = I2S_RXCR_IBM_MASK;
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		val = I2S_RXCR_IBM_RSJM;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val = I2S_RXCR_IBM_LSJM;
		break;
	case SND_SOC_DAIFMT_I2S:
		val = I2S_RXCR_IBM_NORMAL;
		break;
	default:
		ret = -EINVAL;
		goto err_fmt;
	}

	regmap_update_bits(i2s->regmap, I2S_RXCR, mask, val);

err_fmt:

	spin_unlock_irqrestore(&lock, flags);
	return ret;
}

static int rockchip_i2s_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct rk_i2s_dev *i2s = to_info(dai);
	unsigned int val = 0;
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);

	dev_dbg(i2s->dev, "%s: %d\n", __func__, __LINE__);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		val |= I2S_TXCR_VDW(8);
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		val |= I2S_TXCR_VDW(16);
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		val |= I2S_TXCR_VDW(20);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
		val |= I2S_TXCR_VDW(24);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		val |= I2S_TXCR_VDW(32);
		break;
	default:
		dev_err(i2s->dev, "invalid fmt: %d\n", params_format(params));
		spin_unlock_irqrestore(&lock, flags);
		return -EINVAL;
	}

	switch (params_channels(params)) {
	case I2S_CHANNEL_8:
		val |= I2S_TXCR_CHN_8;
		break;
	case I2S_CHANNEL_6:
		val |= I2S_TXCR_CHN_6;
		break;
	case I2S_CHANNEL_4:
		val |= I2S_TXCR_CHN_4;
		break;
	case I2S_CHANNEL_2:
		val |= I2S_TXCR_CHN_2;
		break;
	default:
		dev_err(i2s->dev, "invalid channel: %d\n",
			params_channels(params));
		spin_unlock_irqrestore(&lock, flags);
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_update_bits(i2s->regmap, I2S_TXCR,
				   I2S_TXCR_VDW_MASK |
				   I2S_TXCR_CSR_MASK,
				   val);
	} else {
		regmap_update_bits(i2s->regmap, I2S_RXCR,
				   I2S_RXCR_VDW_MASK, val);
	}

	regmap_update_bits(i2s->regmap, I2S_DMACR,
			   I2S_DMACR_TDL_MASK | I2S_DMACR_RDL_MASK,
			   I2S_DMACR_TDL(16) | I2S_DMACR_RDL(16));

#if defined(CONFIG_RK_HDMI) && defined(CONFIG_SND_RK_SOC_HDMI_I2S)
	snd_config_hdmi_audio(params);
#endif
	spin_unlock_irqrestore(&lock, flags);

	return 0;
}

static int rockchip_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct rk_i2s_dev *i2s = to_info(dai);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			rockchip_snd_rxctrl(i2s, 1);
		else
			rockchip_snd_txctrl(i2s, 1);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			rockchip_snd_rxctrl(i2s, 0);
		else
			rockchip_snd_txctrl(i2s, 0);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int rockchip_i2s_set_sysclk(struct snd_soc_dai *cpu_dai,
				   int clk_id, unsigned int freq, int dir)
{
	struct rk_i2s_dev *i2s = to_info(cpu_dai);
	int ret;

	ret = clk_set_rate(i2s->clk, freq);
	if (ret)
		dev_err(i2s->dev, "fail set clk: freq: %d\n", freq);

	return ret;
}

static int rockchip_i2s_set_clkdiv(struct snd_soc_dai *cpu_dai,
				   int div_id, int div)
{
	struct rk_i2s_dev *i2s = to_info(cpu_dai);
	unsigned int val = 0;
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);

	dev_dbg(i2s->dev, "%s: div_id=%d, div=%d\n", __func__, div_id, div);

	switch (div_id) {
	case ROCKCHIP_DIV_BCLK:
		val |= I2S_CKR_TSD(div);
		val |= I2S_CKR_RSD(div);
		regmap_update_bits(i2s->regmap, I2S_CKR,
				   I2S_CKR_TSD_MASK | I2S_CKR_RSD_MASK,
				   val);
		break;
	case ROCKCHIP_DIV_MCLK:
		val |= I2S_CKR_MDIV(div);
		regmap_update_bits(i2s->regmap, I2S_CKR,
				   I2S_CKR_MDIV_MASK, val);
		break;
	default:
		spin_unlock_irqrestore(&lock, flags);
		return -EINVAL;
	}

	spin_unlock_irqrestore(&lock, flags);

	return 0;
}

static int rockchip_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct rk_i2s_dev *i2s = to_info(dai);

	dai->capture_dma_data = &i2s->capture_dma_data;
	dai->playback_dma_data = &i2s->playback_dma_data;

	return 0;
}

static struct snd_soc_dai_ops rockchip_i2s_dai_ops = {
	.trigger = rockchip_i2s_trigger,
	.hw_params = rockchip_i2s_hw_params,
	.set_fmt = rockchip_i2s_set_fmt,
	.set_clkdiv = rockchip_i2s_set_clkdiv,
	.set_sysclk = rockchip_i2s_set_sysclk,
};

#define ROCKCHIP_I2S_RATES SNDRV_PCM_RATE_8000_192000
#define ROCKCHIP_I2S_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
			      SNDRV_PCM_FMTBIT_S20_3LE | \
			      SNDRV_PCM_FMTBIT_S24_LE | \
			      SNDRV_PCM_FORMAT_S32_LE)

struct snd_soc_dai_driver rockchip_i2s_dai[] = {
	{
		.probe = rockchip_i2s_dai_probe,
		.name = "rockchip-i2s.0",
		.id = 0,
		.playback = {
			.channels_min = 2,
			.channels_max = 8,
			.rates = ROCKCHIP_I2S_RATES,
			.formats = ROCKCHIP_I2S_FORMATS,
		},
		.capture = {
			.channels_min = 2,
			.channels_max = 2,
			.rates = ROCKCHIP_I2S_RATES,
			.formats = ROCKCHIP_I2S_FORMATS,
		},
		.ops = &rockchip_i2s_dai_ops,
		.symmetric_rates = 1,
	},
	{
		.probe = rockchip_i2s_dai_probe,
		.name = "rockchip-i2s.1",
		.id = 1,
		.playback = {
			.channels_min = 2,
			.channels_max = 2,
			.rates = ROCKCHIP_I2S_RATES,
			.formats = ROCKCHIP_I2S_FORMATS,
		},
		.capture = {
			.channels_min = 2,
			.channels_max = 2,
			.rates = ROCKCHIP_I2S_RATES,
			.formats = ROCKCHIP_I2S_FORMATS,
		},
		.ops = &rockchip_i2s_dai_ops,
		.symmetric_rates = 1,
	},
};

static const struct snd_soc_component_driver rockchip_i2s_component = {
	.name = "rockchip-i2s",
};

#ifdef CONFIG_PM
static int rockchip_i2s_runtime_suspend(struct device *dev)
{
	struct rk_i2s_dev *i2s = dev_get_drvdata(dev);

	dev_dbg(i2s->dev, "%s\n", __func__);
	return 0;
}

static int rockchip_i2s_runtime_resume(struct device *dev)
{
	struct rk_i2s_dev *i2s = dev_get_drvdata(dev);

	dev_dbg(i2s->dev, "%s\n", __func__);
	return 0;
}
#else
#define i2s_runtime_suspend NULL
#define i2s_runtime_resume NULL
#endif

#ifdef CLK_SET_LATER
static void set_clk_later_work(struct work_struct *work)
{
	struct rk_i2s_dev *i2s = container_of(work, struct rk_i2s_dev,
						 clk_delayed_work.work);

	clk_set_rate(i2s->clk, I2S_DEFAULT_FREQ);
	if (!IS_ERR(i2s->mclk))
		clk_set_rate(i2s->mclk, I2S_DEFAULT_FREQ);
}
#endif

static bool rockchip_i2s_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case I2S_TXCR:
	case I2S_RXCR:
	case I2S_CKR:
	case I2S_DMACR:
	case I2S_INTCR:
	case I2S_XFER:
	case I2S_CLR:
	case I2S_TXDR:
		return true;
	default:
		return false;
	}
}

static bool rockchip_i2s_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case I2S_TXCR:
	case I2S_RXCR:
	case I2S_CKR:
	case I2S_DMACR:
	case I2S_INTCR:
	case I2S_XFER:
	case I2S_CLR:
	case I2S_RXDR:
	case I2S_FIFOLR:
	case I2S_INTSR:
		return true;
	default:
		return false;
	}
}

static bool rockchip_i2s_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case I2S_INTSR:
	case I2S_CLR:
		return true;
	default:
		return false;
	}
}

static bool rockchip_i2s_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	default:
		return false;
	}
}

static const struct regmap_config rockchip_i2s_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = I2S_RXDR,
	.writeable_reg = rockchip_i2s_wr_reg,
	.readable_reg = rockchip_i2s_rd_reg,
	.volatile_reg = rockchip_i2s_volatile_reg,
	.precious_reg = rockchip_i2s_precious_reg,
	.cache_type = REGCACHE_FLAT,
};

static int rockchip_i2s_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct rk_i2s_dev *i2s;
	struct resource *res;
	void __iomem *regs;
	int ret;

	ret = of_property_read_u32(node, "i2s-id", &pdev->id);
	if (ret < 0) {
		dev_err(&pdev->dev, "Property 'i2s-id' missing or invalid\n");
		ret = -EINVAL;
		goto err;
	}

	if (soc_is_rk3126b()) {
		int sdi_src = 0;

		/* rk3126b has no i2s1 controller(i2s_8ch) */
		if (1 == pdev->id) {
			pr_info("rk3126b has no i2s1 controller\n");
			ret = -ENODEV;
			goto err;
		}

		ret = of_property_read_u32(node, "sdi_source",
					   &sdi_src);
		if (ret < 0)
			sdi_src = 0;

		if (1 == sdi_src) {
			int val;

			/*GRF_SOC_CON*/
			val = readl_relaxed(RK_GRF_VIRT + 0x0140);
			val = val | 0x04000400;
			writel_relaxed(val, RK_GRF_VIRT + 0x0140);
		}
	}

	i2s = devm_kzalloc(&pdev->dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s) {
		dev_err(&pdev->dev, "Can't allocate rk_i2s_dev\n");
		ret = -ENOMEM;
		goto err;
	}

	i2s->hclk = devm_clk_get(&pdev->dev, "i2s_hclk");
	if (IS_ERR(i2s->hclk)) {
		dev_err(&pdev->dev, "Can't retrieve i2s bus clock\n");
		ret = PTR_ERR(i2s->hclk);
		goto err;
	} else {
		clk_prepare_enable(i2s->hclk);
	}

	i2s->clk = devm_clk_get(&pdev->dev, "i2s_clk");
	if (IS_ERR(i2s->clk)) {
		dev_err(&pdev->dev, "Can't retrieve i2s clock\n");
		ret = PTR_ERR(i2s->clk);
		goto err;
	}
#ifdef CLK_SET_LATER
	INIT_DELAYED_WORK(&i2s->clk_delayed_work, set_clk_later_work);
	schedule_delayed_work(&i2s->clk_delayed_work, msecs_to_jiffies(10));
#else
	clk_set_rate(i2s->clk, I2S_DEFAULT_FREQ);
#endif
	clk_prepare_enable(i2s->clk);

	i2s->mclk = devm_clk_get(&pdev->dev, "i2s_mclk");
	if (IS_ERR(i2s->mclk)) {
		dev_info(&pdev->dev, "i2s%d has no mclk\n", pdev->id);
	} else {
	#ifndef CLK_SET_LATER
		clk_set_rate(i2s->mclk, I2S_DEFAULT_FREQ);
	#endif
		clk_prepare_enable(i2s->mclk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs)) {
		ret = PTR_ERR(regs);
		goto err;
	}

	i2s->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					    &rockchip_i2s_regmap_config);
	if (IS_ERR(i2s->regmap)) {
		dev_err(&pdev->dev,
			"Failed to initialise managed register map\n");
		ret = PTR_ERR(i2s->regmap);
		goto err;
	}

	i2s->playback_dma_data.addr = res->start + I2S_TXDR;
	i2s->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s->playback_dma_data.maxburst = I2S_DMA_BURST_SIZE;

	i2s->capture_dma_data.addr = res->start + I2S_RXDR;
	i2s->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s->capture_dma_data.maxburst = I2S_DMA_BURST_SIZE;

	i2s->tx_start = false;
	i2s->rx_start = false;

	i2s->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, i2s);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = rockchip_i2s_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	ret = snd_soc_register_component(&pdev->dev, &rockchip_i2s_component,
					 &rockchip_i2s_dai[pdev->id], 1);

	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI: %d\n", ret);
		ret = -ENOMEM;
		goto err_suspend;
	}

	ret = rockchip_pcm_platform_register(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM: %d\n", ret);
		goto err_unregister_component;
	}

	rockchip_snd_txctrl(i2s, 0);
	rockchip_snd_rxctrl(i2s, 0);

	return 0;

err_unregister_component:
	snd_soc_unregister_component(&pdev->dev);
err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		rockchip_i2s_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);
err:
	return ret;
}

static int rockchip_i2s_remove(struct platform_device *pdev)
{
	struct rk_i2s_dev *i2s = dev_get_drvdata(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		rockchip_i2s_runtime_suspend(&pdev->dev);

	if (!IS_ERR(i2s->mclk))
		clk_disable_unprepare(i2s->mclk);

	clk_disable_unprepare(i2s->clk);
	clk_disable_unprepare(i2s->hclk);
	rockchip_pcm_platform_unregister(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_i2s_match[] = {
	{ .compatible = "rockchip-i2s", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_i2s_match);
#endif

#ifdef CONFIG_PM_SLEEP
static int rockchip_i2s_suspend(struct device *dev)
{
	struct rk_i2s_dev *i2s = dev_get_drvdata(dev);

	dev_dbg(i2s->dev, "%s\n", __func__);
	return pinctrl_pm_select_sleep_state(dev);
}

static int rockchip_i2s_resume(struct device *dev)
{
	struct rk_i2s_dev *i2s = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		return ret;
	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0)
		return ret;
	ret = regmap_reinit_cache(i2s->regmap, &rockchip_i2s_regmap_config);

	pm_runtime_put(dev);

	dev_dbg(i2s->dev, "%s\n", __func__);
	return ret;
}
#endif

static const struct dev_pm_ops rockchip_i2s_pm_ops = {
	SET_RUNTIME_PM_OPS(rockchip_i2s_runtime_suspend, rockchip_i2s_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_i2s_suspend, rockchip_i2s_resume)
};

static struct platform_driver rockchip_i2s_driver = {
	.probe  = rockchip_i2s_probe,
	.remove = rockchip_i2s_remove,
	.driver = {
		.name   = "rockchip-i2s",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(rockchip_i2s_match),
		.pm	= &rockchip_i2s_pm_ops,
	},
};

static int __init rockchip_i2s_init(void)
{
	return platform_driver_register(&rockchip_i2s_driver);
}
subsys_initcall_sync(rockchip_i2s_init);

static void __exit rockchip_i2s_exit(void)
{
	platform_driver_unregister(&rockchip_i2s_driver);
}
module_exit(rockchip_i2s_exit);

MODULE_AUTHOR("Sugar <sugar.zhang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip I2S Controller Driver");
MODULE_LICENSE("GPL v2");
