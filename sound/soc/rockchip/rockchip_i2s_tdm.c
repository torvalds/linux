/* sound/soc/rockchip/rockchip_i2s_tdm.c
 *
 * ALSA SoC Audio Layer - Rockchip I2S/TDM Controller driver
 *
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * Author: Sugar Zhang <sugar.zhang@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>

#include "rockchip_i2s_tdm.h"

#define DRV_NAME "rockchip-i2s-tdm"

struct rk_i2s_soc_data {
	u32 softrst_offset;
	int tx_reset_id;
	int rx_reset_id;
};

struct rk_i2s_tdm_dev {
	struct device *dev;
	struct clk *hclk;
	struct clk *mclk_tx;
	struct clk *mclk_rx;
	struct regmap *regmap;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct reset_control *tx_reset;
	struct reset_control *rx_reset;
	struct rk_i2s_soc_data *soc_data;
	void __iomem *cru_base;
	bool is_master_mode;
	unsigned int bclk_fs;
	unsigned int clk_trcm;
	atomic_t refcount;
	spinlock_t lock; /* xfer lock */
};

static int i2s_tdm_runtime_suspend(struct device *dev)
{
	struct rk_i2s_tdm_dev *i2s_tdm = dev_get_drvdata(dev);

	regcache_cache_only(i2s_tdm->regmap, true);
	if (!IS_ERR(i2s_tdm->mclk_tx))
		clk_disable_unprepare(i2s_tdm->mclk_tx);
	if (!IS_ERR(i2s_tdm->mclk_rx))
		clk_disable_unprepare(i2s_tdm->mclk_rx);

	return 0;
}

static int i2s_tdm_runtime_resume(struct device *dev)
{
	struct rk_i2s_tdm_dev *i2s_tdm = dev_get_drvdata(dev);
	int ret;

	if (!IS_ERR(i2s_tdm->mclk_tx))
		clk_prepare_enable(i2s_tdm->mclk_tx);
	if (!IS_ERR(i2s_tdm->mclk_rx))
		clk_prepare_enable(i2s_tdm->mclk_rx);

	regcache_cache_only(i2s_tdm->regmap, false);
	regcache_mark_dirty(i2s_tdm->regmap);

	ret = regcache_sync(i2s_tdm->regmap);
	if (ret) {
		if (!IS_ERR(i2s_tdm->mclk_tx))
			clk_disable_unprepare(i2s_tdm->mclk_tx);
		if (!IS_ERR(i2s_tdm->mclk_rx))
			clk_disable_unprepare(i2s_tdm->mclk_rx);
	}

	return ret;
}

static inline struct rk_i2s_tdm_dev *to_info(struct snd_soc_dai *dai)
{
	return snd_soc_dai_get_drvdata(dai);
}

static void rockchip_snd_xfer_reset_assert(struct rk_i2s_tdm_dev *i2s_tdm,
					   int tx_bank, int tx_offset,
					   int rx_bank, int rx_offset)
{
	void __iomem *cru_reset, *addr;
	unsigned long flags;
	u64 val;

	cru_reset = i2s_tdm->cru_base + i2s_tdm->soc_data->softrst_offset;

	switch (abs(tx_bank - rx_bank)) {
	case 0:
		writel(BIT(tx_offset) | BIT(rx_offset) |
		       (BIT(tx_offset) << 16) | (BIT(rx_offset) << 16),
		       cru_reset + (tx_bank * 4));
		break;
	case 1:
		if (tx_bank < rx_bank) {
			val = BIT(rx_offset) | (BIT(rx_offset) << 16);
			val <<= 32;
			val |= BIT(tx_offset) | (BIT(tx_offset) << 16);
			addr = cru_reset + (tx_bank * 4);
		} else {
			val = BIT(tx_offset) | (BIT(tx_offset) << 16);
			val <<= 32;
			val |= BIT(rx_offset) | (BIT(rx_offset) << 16);
			addr = cru_reset + (rx_bank * 4);
		}

		if (IS_ALIGNED((u64)addr, 8)) {
			writeq(val, addr);
			break;
		}
		/* fall through */
	default:
		local_irq_save(flags);
		writel(BIT(tx_offset) | (BIT(tx_offset) << 16),
		       cru_reset + (tx_bank * 4));
		writel(BIT(rx_offset) | (BIT(rx_offset) << 16),
		       cru_reset + (rx_bank * 4));
		local_irq_restore(flags);
		break;
	}
}

static void rockchip_snd_xfer_reset_deassert(struct rk_i2s_tdm_dev *i2s_tdm,
					     int tx_bank, int tx_offset,
					     int rx_bank, int rx_offset)
{
	void __iomem *cru_reset, *addr;
	unsigned long flags;
	u64 val;

	cru_reset = i2s_tdm->cru_base + i2s_tdm->soc_data->softrst_offset;

	switch (abs(tx_bank - rx_bank)) {
	case 0:
		writel((BIT(tx_offset) << 16) | (BIT(rx_offset) << 16),
		       cru_reset + (tx_bank * 4));
		break;
	case 1:
		if (tx_bank < rx_bank) {
			val = (BIT(rx_offset) << 16);
			val <<= 32;
			val |= (BIT(tx_offset) << 16);
			addr = cru_reset + (tx_bank * 4);
		} else {
			val = (BIT(tx_offset) << 16);
			val <<= 32;
			val |= (BIT(rx_offset) << 16);
			addr = cru_reset + (rx_bank * 4);
		}

		if (IS_ALIGNED((u64)addr, 8)) {
			writeq(val, addr);
			break;
		}
		/* fall through */
	default:
		local_irq_save(flags);
		writel((BIT(tx_offset) << 16),
		       cru_reset + (tx_bank * 4));
		writel((BIT(rx_offset) << 16),
		       cru_reset + (rx_bank * 4));
		local_irq_restore(flags);
		break;
	}
}

/*
 * to make sure tx/rx reset at the same time when clk_trcm > 0
 * if not, will lead lrck is abnormal.
 */
static void rockchip_snd_xfer_sync_reset(struct rk_i2s_tdm_dev *i2s_tdm)
{
	int tx_id, rx_id;
	int tx_bank, rx_bank, tx_offset, rx_offset;

	if (!i2s_tdm->cru_base || !i2s_tdm->soc_data)
		return;

	tx_id = i2s_tdm->soc_data->tx_reset_id;
	rx_id = i2s_tdm->soc_data->rx_reset_id;
	if (tx_id < 0 || rx_id < 0) {
		dev_err(i2s_tdm->dev, "invalid reset id\n");
		return;
	}

	tx_bank = tx_id / 16;
	tx_offset = tx_id % 16;
	rx_bank = rx_id / 16;
	rx_offset = rx_id % 16;
	dev_dbg(i2s_tdm->dev,
		"tx_bank: %d, rx_bank: %d,tx_offset: %d, rx_offset: %d\n",
		tx_bank, rx_bank, tx_offset, rx_offset);

	rockchip_snd_xfer_reset_assert(i2s_tdm, tx_bank, tx_offset,
				       rx_bank, rx_offset);

	udelay(150);

	rockchip_snd_xfer_reset_deassert(i2s_tdm, tx_bank, tx_offset,
					 rx_bank, rx_offset);
}

/* only used when clk_trcm > 0 */
static void rockchip_snd_txrxctrl(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai, int on)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(dai);
	unsigned int val = 0;
	int retry = 10;

	spin_lock(&i2s_tdm->lock);
	if (on) {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			regmap_update_bits(i2s_tdm->regmap, I2S_DMACR,
					   I2S_DMACR_TDE_ENABLE,
					   I2S_DMACR_TDE_ENABLE);
		else
			regmap_update_bits(i2s_tdm->regmap, I2S_DMACR,
					   I2S_DMACR_RDE_ENABLE,
					   I2S_DMACR_RDE_ENABLE);

		if (atomic_inc_return(&i2s_tdm->refcount) == 1) {
			rockchip_snd_xfer_sync_reset(i2s_tdm);
			regmap_update_bits(i2s_tdm->regmap, I2S_XFER,
					   I2S_XFER_TXS_START |
					   I2S_XFER_RXS_START,
					   I2S_XFER_TXS_START |
					   I2S_XFER_RXS_START);
		}
	} else {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			regmap_update_bits(i2s_tdm->regmap, I2S_DMACR,
					   I2S_DMACR_TDE_ENABLE,
					   I2S_DMACR_TDE_DISABLE);
		else
			regmap_update_bits(i2s_tdm->regmap, I2S_DMACR,
					   I2S_DMACR_RDE_ENABLE,
					   I2S_DMACR_RDE_DISABLE);

		if (atomic_dec_and_test(&i2s_tdm->refcount)) {
			regmap_update_bits(i2s_tdm->regmap, I2S_XFER,
					   I2S_XFER_TXS_START |
					   I2S_XFER_RXS_START,
					   I2S_XFER_TXS_STOP |
					   I2S_XFER_RXS_STOP);

			udelay(150);
			regmap_update_bits(i2s_tdm->regmap, I2S_CLR,
					   I2S_CLR_TXC | I2S_CLR_RXC,
					   I2S_CLR_TXC | I2S_CLR_RXC);

			regmap_read(i2s_tdm->regmap, I2S_CLR, &val);

			/* Should wait for clear operation to finish */
			while (val) {
				regmap_read(i2s_tdm->regmap, I2S_CLR, &val);
				retry--;
				if (!retry) {
					dev_info(i2s_tdm->dev, "fail to clear\n");
					break;
				}
			}
		}
	}
	spin_unlock(&i2s_tdm->lock);
}

static void rockchip_snd_txctrl(struct rk_i2s_tdm_dev *i2s_tdm, int on)
{
	unsigned int val = 0;
	int retry = 10;

	if (on) {
		regmap_update_bits(i2s_tdm->regmap, I2S_DMACR,
				   I2S_DMACR_TDE_ENABLE, I2S_DMACR_TDE_ENABLE);

		regmap_update_bits(i2s_tdm->regmap, I2S_XFER,
				   I2S_XFER_TXS_START,
				   I2S_XFER_TXS_START);
	} else {
		regmap_update_bits(i2s_tdm->regmap, I2S_DMACR,
				   I2S_DMACR_TDE_ENABLE, I2S_DMACR_TDE_DISABLE);

		regmap_update_bits(i2s_tdm->regmap, I2S_XFER,
				   I2S_XFER_TXS_START,
				   I2S_XFER_TXS_STOP);

		udelay(150);
		regmap_update_bits(i2s_tdm->regmap, I2S_CLR,
				   I2S_CLR_TXC,
				   I2S_CLR_TXC);

		regmap_read(i2s_tdm->regmap, I2S_CLR, &val);

		/* Should wait for clear operation to finish */
		while (val) {
			regmap_read(i2s_tdm->regmap, I2S_CLR, &val);
			retry--;
			if (!retry) {
				dev_warn(i2s_tdm->dev, "fail to clear\n");
				break;
			}
		}
	}
}

static void rockchip_snd_rxctrl(struct rk_i2s_tdm_dev *i2s_tdm, int on)
{
	unsigned int val = 0;
	int retry = 10;

	if (on) {
		regmap_update_bits(i2s_tdm->regmap, I2S_DMACR,
				   I2S_DMACR_RDE_ENABLE, I2S_DMACR_RDE_ENABLE);

		regmap_update_bits(i2s_tdm->regmap, I2S_XFER,
				   I2S_XFER_RXS_START,
				   I2S_XFER_RXS_START);
	} else {
		regmap_update_bits(i2s_tdm->regmap, I2S_DMACR,
				   I2S_DMACR_RDE_ENABLE, I2S_DMACR_RDE_DISABLE);

		regmap_update_bits(i2s_tdm->regmap, I2S_XFER,
				   I2S_XFER_RXS_START,
				   I2S_XFER_RXS_STOP);

		udelay(150);
		regmap_update_bits(i2s_tdm->regmap, I2S_CLR,
				   I2S_CLR_RXC,
				   I2S_CLR_RXC);

		regmap_read(i2s_tdm->regmap, I2S_CLR, &val);

		/* Should wait for clear operation to finish */
		while (val) {
			regmap_read(i2s_tdm->regmap, I2S_CLR, &val);
			retry--;
			if (!retry) {
				dev_warn(i2s_tdm->dev, "fail to clear\n");
				break;
			}
		}
	}
}

static int rockchip_i2s_tdm_set_fmt(struct snd_soc_dai *cpu_dai,
				    unsigned int fmt)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(cpu_dai);
	unsigned int mask = 0, val = 0;
	int ret = 0;

	pm_runtime_get_sync(cpu_dai->dev);
	mask = I2S_CKR_MSS_MASK;
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		/* Set source clock in Master mode */
		val = I2S_CKR_MSS_MASTER;
		i2s_tdm->is_master_mode = true;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		val = I2S_CKR_MSS_SLAVE;
		i2s_tdm->is_master_mode = false;
		break;
	default:
		ret = -EINVAL;
		goto err_pm_put;
	}

	regmap_update_bits(i2s_tdm->regmap, I2S_CKR, mask, val);

	mask = I2S_CKR_CKP_MASK;
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		val = I2S_CKR_CKP_NEG;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		val = I2S_CKR_CKP_POS;
		break;
	default:
		ret = -EINVAL;
		goto err_pm_put;
	}

	regmap_update_bits(i2s_tdm->regmap, I2S_CKR, mask, val);

	mask = I2S_TXCR_IBM_MASK | I2S_TXCR_TFS_MASK | I2S_TXCR_PBM_MASK;
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
	case SND_SOC_DAIFMT_DSP_A: /* PCM no delay mode */
		val = I2S_TXCR_TFS_PCM;
		break;
	case SND_SOC_DAIFMT_DSP_B: /* PCM delay 1 mode */
		val = I2S_TXCR_TFS_PCM | I2S_TXCR_PBM_MODE(1);
		break;
	default:
		ret = -EINVAL;
		goto err_pm_put;
	}

	regmap_update_bits(i2s_tdm->regmap, I2S_TXCR, mask, val);

	mask = I2S_RXCR_IBM_MASK | I2S_RXCR_TFS_MASK | I2S_RXCR_PBM_MASK;
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
	case SND_SOC_DAIFMT_DSP_A: /* PCM no delay mode */
		val = I2S_RXCR_TFS_PCM;
		break;
	case SND_SOC_DAIFMT_DSP_B: /* PCM delay 1 mode */
		val = I2S_RXCR_TFS_PCM | I2S_RXCR_PBM_MODE(1);
		break;
	default:
		ret = -EINVAL;
		goto err_pm_put;
	}

	regmap_update_bits(i2s_tdm->regmap, I2S_RXCR, mask, val);

err_pm_put:
	pm_runtime_put(cpu_dai->dev);

	return ret;
}

static void rockchip_i2s_tdm_xfer_pause(struct snd_pcm_substream *substream,
					struct rk_i2s_tdm_dev *i2s_tdm)
{
	int stream;
	unsigned int val = 0;
	int retry = 10;

	stream = SNDRV_PCM_STREAM_LAST - substream->stream;
	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		regmap_update_bits(i2s_tdm->regmap, I2S_DMACR,
				   I2S_DMACR_TDE_ENABLE,
				   I2S_DMACR_TDE_DISABLE);
	else
		regmap_update_bits(i2s_tdm->regmap, I2S_DMACR,
				   I2S_DMACR_RDE_ENABLE,
				   I2S_DMACR_RDE_DISABLE);

	regmap_update_bits(i2s_tdm->regmap, I2S_XFER,
			   I2S_XFER_TXS_START |
			   I2S_XFER_RXS_START,
			   I2S_XFER_TXS_STOP |
			   I2S_XFER_RXS_STOP);

	udelay(150);
	regmap_update_bits(i2s_tdm->regmap, I2S_CLR,
			   I2S_CLR_TXC | I2S_CLR_RXC,
			   I2S_CLR_TXC | I2S_CLR_RXC);

	regmap_read(i2s_tdm->regmap, I2S_CLR, &val);

	/* Should wait for clear operation to finish */
	while (val) {
		regmap_read(i2s_tdm->regmap, I2S_CLR, &val);
		retry--;
		if (!retry) {
			dev_info(i2s_tdm->dev, "fail to clear\n");
			break;
		}
	}
}

static void rockchip_i2s_tdm_xfer_resume(struct snd_pcm_substream *substream,
					 struct rk_i2s_tdm_dev *i2s_tdm)
{
	int stream;

	stream = SNDRV_PCM_STREAM_LAST - substream->stream;
	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		regmap_update_bits(i2s_tdm->regmap, I2S_DMACR,
				   I2S_DMACR_TDE_ENABLE,
				   I2S_DMACR_TDE_ENABLE);
	else
		regmap_update_bits(i2s_tdm->regmap, I2S_DMACR,
				   I2S_DMACR_RDE_ENABLE,
				   I2S_DMACR_RDE_ENABLE);

	regmap_update_bits(i2s_tdm->regmap, I2S_XFER,
			   I2S_XFER_TXS_START |
			   I2S_XFER_RXS_START,
			   I2S_XFER_TXS_START |
			   I2S_XFER_RXS_START);
}

static int rockchip_i2s_tdm_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(dai);
	struct clk *mclk;
	int ret = 0;
	unsigned int val = 0;
	unsigned int mclk_rate, bclk_rate, div_bclk, div_lrck;

	if (i2s_tdm->clk_trcm) {
		spin_lock(&i2s_tdm->lock);
		if (atomic_read(&i2s_tdm->refcount))
			rockchip_i2s_tdm_xfer_pause(substream, i2s_tdm);
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		mclk = i2s_tdm->mclk_tx;
	else
		mclk = i2s_tdm->mclk_rx;

	if (i2s_tdm->is_master_mode) {
		mclk_rate = clk_get_rate(mclk);
		bclk_rate = i2s_tdm->bclk_fs * params_rate(params);
		if (!bclk_rate) {
			ret = -EINVAL;
			goto err;
		}
		div_bclk = DIV_ROUND_CLOSEST(mclk_rate, bclk_rate);
		div_lrck = bclk_rate / params_rate(params);
		if (i2s_tdm->clk_trcm) {
			regmap_update_bits(i2s_tdm->regmap, I2S_CLKDIV,
					   I2S_CLKDIV_TXM_MASK | I2S_CLKDIV_RXM_MASK,
					   I2S_CLKDIV_TXM(div_bclk) | I2S_CLKDIV_RXM(div_bclk));
			regmap_update_bits(i2s_tdm->regmap, I2S_CKR,
					   I2S_CKR_TSD_MASK | I2S_CKR_RSD_MASK,
					   I2S_CKR_TSD(div_lrck) | I2S_CKR_RSD(div_lrck));
		} else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			regmap_update_bits(i2s_tdm->regmap, I2S_CLKDIV,
					   I2S_CLKDIV_TXM_MASK,
					   I2S_CLKDIV_TXM(div_bclk));
			regmap_update_bits(i2s_tdm->regmap, I2S_CKR,
					   I2S_CKR_TSD_MASK,
					   I2S_CKR_TSD(div_lrck));
		} else {
			regmap_update_bits(i2s_tdm->regmap, I2S_CLKDIV,
					   I2S_CLKDIV_RXM_MASK,
					   I2S_CLKDIV_RXM(div_bclk));
			regmap_update_bits(i2s_tdm->regmap, I2S_CKR,
					   I2S_CKR_RSD_MASK,
					   I2S_CKR_RSD(div_lrck));
		}
	}

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
		val |= I2S_TXCR_VDW(24);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		val |= I2S_TXCR_VDW(32);
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	switch (params_channels(params)) {
	case 8:
		val |= I2S_CHN_8;
		break;
	case 6:
		val |= I2S_CHN_6;
		break;
	case 4:
		val |= I2S_CHN_4;
		break;
	case 2:
		val |= I2S_CHN_2;
		break;
	default:
		dev_err(i2s_tdm->dev, "invalid channel: %d\n",
			params_channels(params));
		ret = -EINVAL;
		goto err;
	}

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		regmap_update_bits(i2s_tdm->regmap, I2S_RXCR,
				   I2S_RXCR_VDW_MASK | I2S_RXCR_CSR_MASK,
				   val);
	else
		regmap_update_bits(i2s_tdm->regmap, I2S_TXCR,
				   I2S_TXCR_VDW_MASK | I2S_TXCR_CSR_MASK,
				   val);

	regmap_update_bits(i2s_tdm->regmap, I2S_DMACR, I2S_DMACR_TDL_MASK,
			   I2S_DMACR_TDL(16));
	regmap_update_bits(i2s_tdm->regmap, I2S_DMACR, I2S_DMACR_RDL_MASK,
			   I2S_DMACR_RDL(16));

	if (i2s_tdm->clk_trcm) {
		if (atomic_read(&i2s_tdm->refcount))
			rockchip_i2s_tdm_xfer_resume(substream, i2s_tdm);
		spin_unlock(&i2s_tdm->lock);
	}

	return 0;

err:
	if (i2s_tdm->clk_trcm)
		spin_unlock(&i2s_tdm->lock);
	return ret;
}

static int rockchip_i2s_tdm_trigger(struct snd_pcm_substream *substream,
				    int cmd, struct snd_soc_dai *dai)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(dai);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (i2s_tdm->clk_trcm)
			rockchip_snd_txrxctrl(substream, dai, 1);
		else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			rockchip_snd_rxctrl(i2s_tdm, 1);
		else
			rockchip_snd_txctrl(i2s_tdm, 1);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (i2s_tdm->clk_trcm)
			rockchip_snd_txrxctrl(substream, dai, 0);
		else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			rockchip_snd_rxctrl(i2s_tdm, 0);
		else
			rockchip_snd_txctrl(i2s_tdm, 0);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int rockchip_i2s_tdm_set_sysclk(struct snd_soc_dai *cpu_dai, int clk_id,
				       unsigned int freq, int dir)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(cpu_dai);
	int ret;

	ret = clk_set_rate(i2s_tdm->mclk_tx, freq);
	if (ret)
		dev_err(i2s_tdm->dev, "Fail to set mclk_tx %d\n", ret);

	if (!IS_ERR(i2s_tdm->mclk_rx))
		ret = clk_set_rate(i2s_tdm->mclk_rx, freq);
	return ret;
}

static int rockchip_i2s_tdm_dai_probe(struct snd_soc_dai *dai)
{
	struct rk_i2s_tdm_dev *i2s_tdm = snd_soc_dai_get_drvdata(dai);

	dai->capture_dma_data = &i2s_tdm->capture_dma_data;
	dai->playback_dma_data = &i2s_tdm->playback_dma_data;

	return 0;
}

static const struct snd_soc_dai_ops rockchip_i2s_tdm_dai_ops = {
	.hw_params = rockchip_i2s_tdm_hw_params,
	.set_sysclk = rockchip_i2s_tdm_set_sysclk,
	.set_fmt = rockchip_i2s_tdm_set_fmt,
	.trigger = rockchip_i2s_tdm_trigger,
};

static struct snd_soc_dai_driver rockchip_i2s_tdm_dai = {
	.probe = rockchip_i2s_tdm_dai_probe,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = (SNDRV_PCM_FMTBIT_S8 |
			    SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE |
			    SNDRV_PCM_FMTBIT_S32_LE),
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = (SNDRV_PCM_FMTBIT_S8 |
			    SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE |
			    SNDRV_PCM_FMTBIT_S32_LE),
	},
	.ops = &rockchip_i2s_tdm_dai_ops,
	.symmetric_rates = 1,
};

static const struct snd_soc_component_driver rockchip_i2s_tdm_component = {
	.name = DRV_NAME,
};

static bool rockchip_i2s_tdm_wr_reg(struct device *dev, unsigned int reg)
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
	case I2S_TDM_TXCR:
	case I2S_TDM_RXCR:
	case I2S_CLKDIV:
		return true;
	default:
		return false;
	}
}

static bool rockchip_i2s_tdm_rd_reg(struct device *dev, unsigned int reg)
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
	case I2S_TDM_TXCR:
	case I2S_TDM_RXCR:
	case I2S_CLKDIV:
		return true;
	default:
		return false;
	}
}

static bool rockchip_i2s_tdm_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case I2S_INTSR:
	case I2S_CLR:
		return true;
	default:
		return false;
	}
}

static bool rockchip_i2s_tdm_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	default:
		return false;
	}
}

static const struct reg_default rockchip_i2s_tdm_reg_defaults[] = {
	{0x00, 0x7200000f},
	{0x04, 0x01c8000f},
	{0x08, 0x00001f1f},
	{0x10, 0x001f0000},
	{0x14, 0x01f00000},
	{0x30, 0x00003eff},
	{0x34, 0x00003eff},
	{0x38, 0x00000707},
};

static const struct regmap_config rockchip_i2s_tdm_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = I2S_CLKDIV,
	.reg_defaults = rockchip_i2s_tdm_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rockchip_i2s_tdm_reg_defaults),
	.writeable_reg = rockchip_i2s_tdm_wr_reg,
	.readable_reg = rockchip_i2s_tdm_rd_reg,
	.volatile_reg = rockchip_i2s_tdm_volatile_reg,
	.precious_reg = rockchip_i2s_tdm_precious_reg,
	.cache_type = REGCACHE_FLAT,
};

static struct rk_i2s_soc_data rk3308_i2s_soc_data = {
	.softrst_offset = 0x0400,
};

static const struct of_device_id rockchip_i2s_tdm_match[] = {
	{ .compatible = "rockchip,rk3308-i2s-tdm", .data = &rk3308_i2s_soc_data },
	{},
};

static int of_i2s_resetid_get(struct device_node *node,
			      const char *id)
{
	struct of_phandle_args args;
	int index = 0;
	int ret;

	if (id)
		index = of_property_match_string(node,
						 "reset-names", id);
	ret = of_parse_phandle_with_args(node, "resets", "#reset-cells",
					 index, &args);
	if (ret)
		return ret;

	return args.args[0];
}

static int rockchip_i2s_tdm_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *cru_node;
	const struct of_device_id *of_id;
	struct rk_i2s_tdm_dev *i2s_tdm;
	struct resource *res;
	void __iomem *regs;
	int ret;
	int val;

	i2s_tdm = devm_kzalloc(&pdev->dev, sizeof(*i2s_tdm), GFP_KERNEL);
	if (!i2s_tdm)
		return -ENOMEM;

	i2s_tdm->dev = &pdev->dev;

	spin_lock_init(&i2s_tdm->lock);
	i2s_tdm->bclk_fs = 64;
	if (!of_property_read_u32(node, "rockchip,bclk-fs", &val)) {
		if ((val >= 32) && (val % 2 == 0))
			i2s_tdm->bclk_fs = val;
	}

	i2s_tdm->clk_trcm = I2S_CKR_TRCM_TXRX;
	if (!of_property_read_u32(node, "rockchip,clk-trcm", &val)) {
		if (val >= 0 && val <= 2)
			i2s_tdm->clk_trcm = val << I2S_CKR_TRCM_SHIFT;
	}

	if (i2s_tdm->clk_trcm) {
		cru_node = of_parse_phandle(node, "rockchip,cru", 0);
		i2s_tdm->cru_base = of_iomap(cru_node, 0);
		if (!i2s_tdm->cru_base)
			return -ENOENT;
		of_id = of_match_device(rockchip_i2s_tdm_match, &pdev->dev);
		if (!of_id || !of_id->data)
			return -EINVAL;

		i2s_tdm->soc_data = (struct rk_i2s_soc_data *)of_id->data;
		i2s_tdm->soc_data->tx_reset_id = of_i2s_resetid_get(node, "tx-m");
		if (i2s_tdm->soc_data->tx_reset_id < 0)
			return -EINVAL;
		i2s_tdm->soc_data->rx_reset_id = of_i2s_resetid_get(node, "rx-m");
		if (i2s_tdm->soc_data->rx_reset_id < 0)
			return -EINVAL;
	}

	i2s_tdm->tx_reset = devm_reset_control_get(&pdev->dev, "tx-m");
	if (IS_ERR(i2s_tdm->tx_reset)) {
		ret = PTR_ERR(i2s_tdm->tx_reset);
		if (ret != -ENOENT)
			return ret;
	}

	i2s_tdm->rx_reset = devm_reset_control_get(&pdev->dev, "rx-m");
	if (IS_ERR(i2s_tdm->rx_reset)) {
		ret = PTR_ERR(i2s_tdm->rx_reset);
		if (ret != -ENOENT)
			return ret;
	}

	i2s_tdm->hclk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(i2s_tdm->hclk))
		return PTR_ERR(i2s_tdm->hclk);

	ret = clk_prepare_enable(i2s_tdm->hclk);
	if (ret)
		return ret;

	i2s_tdm->mclk_tx = devm_clk_get(&pdev->dev, "mclk_tx");
	if (IS_ERR(i2s_tdm->mclk_tx))
		return PTR_ERR(i2s_tdm->mclk_tx);

	i2s_tdm->mclk_rx = devm_clk_get(&pdev->dev, "mclk_rx");
	if (IS_ERR(i2s_tdm->mclk_rx))
		return PTR_ERR(i2s_tdm->mclk_rx);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	i2s_tdm->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					    &rockchip_i2s_tdm_regmap_config);
	if (IS_ERR(i2s_tdm->regmap))
		return PTR_ERR(i2s_tdm->regmap);

	i2s_tdm->playback_dma_data.addr = res->start + I2S_TXDR;
	i2s_tdm->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s_tdm->playback_dma_data.maxburst = 8;

	i2s_tdm->capture_dma_data.addr = res->start + I2S_RXDR;
	i2s_tdm->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s_tdm->capture_dma_data.maxburst = 8;

	atomic_set(&i2s_tdm->refcount, 0);
	dev_set_drvdata(&pdev->dev, i2s_tdm);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = i2s_tdm_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	regmap_update_bits(i2s_tdm->regmap, I2S_CKR,
			   I2S_CKR_TRCM_MASK, i2s_tdm->clk_trcm);

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &rockchip_i2s_tdm_component,
					      &rockchip_i2s_tdm_dai, 1);

	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI\n");
		goto err_suspend;
	}

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM\n");
		return ret;
	}

	return 0;

err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		i2s_tdm_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int rockchip_i2s_tdm_remove(struct platform_device *pdev)
{
	struct rk_i2s_tdm_dev *i2s_tdm = dev_get_drvdata(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		i2s_tdm_runtime_suspend(&pdev->dev);

	if (!IS_ERR(i2s_tdm->mclk_tx))
		clk_prepare_enable(i2s_tdm->mclk_tx);
	if (!IS_ERR(i2s_tdm->mclk_rx))
		clk_prepare_enable(i2s_tdm->mclk_rx);
	if (!IS_ERR(i2s_tdm->hclk))
		clk_disable_unprepare(i2s_tdm->hclk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rockchip_i2s_tdm_suspend(struct device *dev)
{
	struct rk_i2s_tdm_dev *i2s_tdm = dev_get_drvdata(dev);

	regcache_mark_dirty(i2s_tdm->regmap);

	return 0;
}

static int rockchip_i2s_tdm_resume(struct device *dev)
{
	struct rk_i2s_tdm_dev *i2s_tdm = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		return ret;
	ret = regcache_sync(i2s_tdm->regmap);
	pm_runtime_put(dev);

	return ret;
}
#endif

static const struct dev_pm_ops rockchip_i2s_tdm_pm_ops = {
	SET_RUNTIME_PM_OPS(i2s_tdm_runtime_suspend, i2s_tdm_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_i2s_tdm_suspend,
				rockchip_i2s_tdm_resume)
};

static struct platform_driver rockchip_i2s_tdm_driver = {
	.probe = rockchip_i2s_tdm_probe,
	.remove = rockchip_i2s_tdm_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(rockchip_i2s_tdm_match),
		.pm = &rockchip_i2s_tdm_pm_ops,
	},
};
module_platform_driver(rockchip_i2s_tdm_driver);

MODULE_DESCRIPTION("ROCKCHIP I2S/TDM ASoC Interface");
MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, rockchip_i2s_tdm_match);
