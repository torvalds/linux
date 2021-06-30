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
#include <linux/clk-provider.h>
#include <linux/clk/rockchip.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>

#include "rockchip_i2s_tdm.h"
#include "rockchip_pcm.h"

#define DRV_NAME "rockchip-i2s-tdm"

#define DEFAULT_MCLK_FS				256
#define CH_GRP_MAX				4  /* The max channel 8 / 2 */
#define MULTIPLEX_CH_MAX			10
#define CLK_PPM_MIN				(-1000)
#define CLK_PPM_MAX				(1000)

struct txrx_config {
	u32 addr;
	u32 reg;
	u32 txonly;
	u32 rxonly;
};

struct rk_i2s_soc_data {
	u32 softrst_offset;
	u32 grf_reg_offset;
	u32 grf_shift;
	int config_count;
	const struct txrx_config *configs;
	int (*init)(struct device *dev, u32 addr);
};

struct rk_i2s_tdm_dev {
	struct device *dev;
	struct clk *hclk;
	struct clk *mclk_tx;
	struct clk *mclk_rx;
	/* The mclk_tx_src is parent of mclk_tx */
	struct clk *mclk_tx_src;
	/* The mclk_rx_src is parent of mclk_rx */
	struct clk *mclk_rx_src;
	/*
	 * The mclk_root0 and mclk_root1 are root parent and supplies for
	 * the different FS.
	 *
	 * e.g:
	 * mclk_root0 is VPLL0, used for FS=48000Hz
	 * mclk_root0 is VPLL1, used for FS=44100Hz
	 */
	struct clk *mclk_root0;
	struct clk *mclk_root1;
	struct regmap *regmap;
	struct regmap *grf;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct reset_control *tx_reset;
	struct reset_control *rx_reset;
	struct rk_i2s_soc_data *soc_data;
	void __iomem *cru_base;
	bool is_master_mode;
	bool io_multiplex;
	bool mclk_calibrate;
	bool tdm_mode;
	bool tdm_fsync_half_frame;
	unsigned int mclk_rx_freq;
	unsigned int mclk_tx_freq;
	unsigned int mclk_root0_freq;
	unsigned int mclk_root1_freq;
	unsigned int mclk_root0_initial_freq;
	unsigned int mclk_root1_initial_freq;
	unsigned int bclk_fs;
	unsigned int clk_trcm;
	unsigned int i2s_sdis[CH_GRP_MAX];
	unsigned int i2s_sdos[CH_GRP_MAX];
	int clk_ppm;
	int tx_reset_id;
	int rx_reset_id;
	atomic_t refcount;
	spinlock_t lock; /* xfer lock */
};

static int to_ch_num(unsigned int val)
{
	int chs;

	switch (val) {
	case I2S_CHN_4:
		chs = 4;
		break;
	case I2S_CHN_6:
		chs = 6;
		break;
	case I2S_CHN_8:
		chs = 8;
		break;
	default:
		chs = 2;
		break;
	}

	return chs;
}

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

#if defined(CONFIG_ARM) && !defined(writeq)
static inline void __raw_writeq(u64 val, volatile void __iomem *addr)
{
	asm volatile("strd %0, %H0, [%1]" : : "r" (val), "r" (addr));
}
#define writeq(v,c) ({ __iowmb(); __raw_writeq((__force u64) cpu_to_le64(v), c); })
#endif

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

		if (IS_ALIGNED((uintptr_t)addr, 8)) {
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
	/* delay for reset assert done */
	udelay(10);
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

		if (IS_ALIGNED((uintptr_t)addr, 8)) {
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
	/* delay for reset deassert done */
	udelay(10);
}

/*
 * make sure both tx and rx are reset at the same time for sync lrck
 * when clk_trcm > 0
 */
static void rockchip_snd_xfer_sync_reset(struct rk_i2s_tdm_dev *i2s_tdm)
{
	int tx_id, rx_id;
	int tx_bank, rx_bank, tx_offset, rx_offset;

	if (!i2s_tdm->cru_base || !i2s_tdm->soc_data)
		return;

	tx_id = i2s_tdm->tx_reset_id;
	rx_id = i2s_tdm->rx_reset_id;
	if (tx_id < 0 || rx_id < 0)
		return;

	tx_bank = tx_id / 16;
	tx_offset = tx_id % 16;
	rx_bank = rx_id / 16;
	rx_offset = rx_id % 16;
	dev_dbg(i2s_tdm->dev,
		"tx_bank: %d, rx_bank: %d,tx_offset: %d, rx_offset: %d\n",
		tx_bank, rx_bank, tx_offset, rx_offset);

	rockchip_snd_xfer_reset_assert(i2s_tdm, tx_bank, tx_offset,
				       rx_bank, rx_offset);
	rockchip_snd_xfer_reset_deassert(i2s_tdm, tx_bank, tx_offset,
					 rx_bank, rx_offset);
}

/* only used when clk_trcm > 0 */
static void rockchip_snd_txrxctrl(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai, int on)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(dai);
	unsigned int val = 0;
	unsigned long flags;
	int retry = 10;

	spin_lock_irqsave(&i2s_tdm->lock, flags);
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
					dev_info(i2s_tdm->dev, "reset txrx\n");
					rockchip_snd_xfer_sync_reset(i2s_tdm);
					break;
				}
			}
		}
	}
	spin_unlock_irqrestore(&i2s_tdm->lock, flags);
}

static void rockchip_snd_reset(struct reset_control *rc)
{
	if (IS_ERR(rc))
		return;

	reset_control_assert(rc);
	/* delay for reset assert done */
	udelay(10);
	reset_control_deassert(rc);
	/* delay for reset deassert done */
	udelay(10);
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
				dev_warn(i2s_tdm->dev, "reset tx\n");
				rockchip_snd_reset(i2s_tdm->tx_reset);
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
				dev_warn(i2s_tdm->dev, "reset rx\n");
				rockchip_snd_reset(i2s_tdm->rx_reset);
				break;
			}
		}
	}
}

static int rockchip_i2s_tdm_set_fmt(struct snd_soc_dai *cpu_dai,
				    unsigned int fmt)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(cpu_dai);
	unsigned int mask = 0, val = 0, tdm_val = 0;
	int ret = 0;
	bool is_tdm = i2s_tdm->tdm_mode;

	pm_runtime_get_sync(cpu_dai->dev);
	mask = I2S_CKR_MSS_MASK;
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
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

	mask = I2S_CKR_CKP_MASK | I2S_CKR_TLP_MASK | I2S_CKR_RLP_MASK;
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		val = I2S_CKR_CKP_NORMAL |
		      I2S_CKR_TLP_NORMAL |
		      I2S_CKR_RLP_NORMAL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		val = I2S_CKR_CKP_NORMAL |
		      I2S_CKR_TLP_INVERTED |
		      I2S_CKR_RLP_INVERTED;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		val = I2S_CKR_CKP_INVERTED |
		      I2S_CKR_TLP_NORMAL |
		      I2S_CKR_RLP_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		val = I2S_CKR_CKP_INVERTED |
		      I2S_CKR_TLP_INVERTED |
		      I2S_CKR_RLP_INVERTED;
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

	if (is_tdm) {
		switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_RIGHT_J:
			val = I2S_TXCR_TFS_TDM_I2S;
			tdm_val = TDM_SHIFT_CTRL(2);
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			val = I2S_TXCR_TFS_TDM_I2S;
			tdm_val = TDM_SHIFT_CTRL(1);
			break;
		case SND_SOC_DAIFMT_I2S:
			val = I2S_TXCR_TFS_TDM_I2S;
			tdm_val = TDM_SHIFT_CTRL(0);
			break;
		case SND_SOC_DAIFMT_DSP_A:
			val = I2S_TXCR_TFS_TDM_PCM;
			tdm_val = TDM_SHIFT_CTRL(0);
			break;
		case SND_SOC_DAIFMT_DSP_B:
			val = I2S_TXCR_TFS_TDM_PCM;
			tdm_val = TDM_SHIFT_CTRL(2);
			break;
		default:
			ret = -EINVAL;
			goto err_pm_put;
		}

		tdm_val |= TDM_FSYNC_WIDTH_SEL1(1);
		if (i2s_tdm->tdm_fsync_half_frame)
			tdm_val |= TDM_FSYNC_WIDTH_HALF_FRAME;
		else
			tdm_val |= TDM_FSYNC_WIDTH_ONE_FRAME;

		mask = I2S_TXCR_TFS_MASK;
		regmap_update_bits(i2s_tdm->regmap, I2S_TXCR, mask, val);
		regmap_update_bits(i2s_tdm->regmap, I2S_RXCR, mask, val);

		mask = TDM_FSYNC_WIDTH_SEL1_MSK | TDM_FSYNC_WIDTH_SEL0_MSK |
		       TDM_SHIFT_CTRL_MSK;
		regmap_update_bits(i2s_tdm->regmap, I2S_TDM_TXCR,
				   mask, tdm_val);
		regmap_update_bits(i2s_tdm->regmap, I2S_TDM_RXCR,
				   mask, tdm_val);

		if (val == I2S_TXCR_TFS_TDM_I2S && !i2s_tdm->tdm_fsync_half_frame) {
			/* refine frame width for TDM_I2S_ONE_FRAME */
			mask = TDM_FRAME_WIDTH_MSK;
			tdm_val = TDM_FRAME_WIDTH(i2s_tdm->bclk_fs >> 1);
			regmap_update_bits(i2s_tdm->regmap, I2S_TDM_TXCR,
					   mask, tdm_val);
			regmap_update_bits(i2s_tdm->regmap, I2S_TDM_RXCR,
					   mask, tdm_val);
		}
	}

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
			dev_info(i2s_tdm->dev, "reset txrx\n");
			rockchip_snd_xfer_sync_reset(i2s_tdm);
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

static int rockchip_i2s_tdm_clk_set_rate(struct rk_i2s_tdm_dev *i2s_tdm,
					 struct clk *clk, unsigned long rate,
					 int ppm)
{
	unsigned long rate_target;
	int delta, ret;

	if (ppm == i2s_tdm->clk_ppm)
		return 0;

	ret = rockchip_pll_clk_compensation(clk, ppm);
	if (ret != -ENOSYS)
		goto out;

	delta = (ppm < 0) ? -1 : 1;
	delta *= (int)div64_u64((uint64_t)rate * (uint64_t)abs(ppm) + 500000, 1000000);

	rate_target = rate + delta;

	if (!rate_target)
		return -EINVAL;

	ret = clk_set_rate(clk, rate_target);
	if (ret)
		return ret;
out:
	if (!ret)
		i2s_tdm->clk_ppm = ppm;

	return ret;
}

static int rockchip_i2s_tdm_calibrate_mclk(struct rk_i2s_tdm_dev *i2s_tdm,
					   struct snd_pcm_substream *substream,
					   unsigned int lrck_freq)
{
	struct clk *mclk_root;
	struct clk *mclk_parent;
	unsigned int mclk_root_freq;
	unsigned int mclk_root_initial_freq;
	unsigned int mclk_parent_freq;
	unsigned int div, delta;
	uint64_t ppm;
	int ret;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		mclk_parent = i2s_tdm->mclk_tx_src;
	else
		mclk_parent = i2s_tdm->mclk_rx_src;

	switch (lrck_freq) {
	case 8000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 64000:
	case 96000:
	case 192000:
		mclk_root = i2s_tdm->mclk_root0;
		mclk_root_freq = i2s_tdm->mclk_root0_freq;
		mclk_root_initial_freq = i2s_tdm->mclk_root0_initial_freq;
		mclk_parent_freq = DEFAULT_MCLK_FS * 192000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
	case 176400:
		mclk_root = i2s_tdm->mclk_root1;
		mclk_root_freq = i2s_tdm->mclk_root1_freq;
		mclk_root_initial_freq = i2s_tdm->mclk_root1_initial_freq;
		mclk_parent_freq = DEFAULT_MCLK_FS * 176400;
		break;
	default:
		dev_err(i2s_tdm->dev, "Invalid LRCK freq: %u Hz\n",
			lrck_freq);
		return -EINVAL;
	}

	ret = clk_set_parent(mclk_parent, mclk_root);
	if (ret)
		goto out;

	ret = rockchip_i2s_tdm_clk_set_rate(i2s_tdm, mclk_root,
					    mclk_root_freq, 0);
	if (ret)
		goto out;

	delta = abs(mclk_root_freq % mclk_parent_freq - mclk_parent_freq);
	ppm = div64_u64((uint64_t)delta * 1000000, (uint64_t)mclk_root_freq);

	if (ppm) {
		div = DIV_ROUND_CLOSEST(mclk_root_initial_freq, mclk_parent_freq);
		if (!div)
			return -EINVAL;

		mclk_root_freq = mclk_parent_freq * round_up(div, 2);

		ret = clk_set_rate(mclk_root, mclk_root_freq);
		if (ret)
			goto out;

		i2s_tdm->mclk_root0_freq = clk_get_rate(i2s_tdm->mclk_root0);
		i2s_tdm->mclk_root1_freq = clk_get_rate(i2s_tdm->mclk_root1);
	}

	ret = clk_set_rate(mclk_parent, mclk_parent_freq);
	if (ret)
		goto out;

out:
	return ret;
}

static int rockchip_i2s_tdm_set_mclk(struct rk_i2s_tdm_dev *i2s_tdm,
				     struct snd_pcm_substream *substream,
				     struct clk **mclk)
{
	unsigned int mclk_freq;
	int ret;

	if (i2s_tdm->clk_trcm) {
		if (i2s_tdm->mclk_tx_freq != i2s_tdm->mclk_rx_freq) {
			dev_err(i2s_tdm->dev,
				"clk_trcm, tx: %d and rx: %d should be same\n",
				i2s_tdm->mclk_tx_freq,
				i2s_tdm->mclk_rx_freq);
			ret = -EINVAL;
			goto err;
		}

		ret = clk_set_rate(i2s_tdm->mclk_tx, i2s_tdm->mclk_tx_freq);
		if (ret)
			goto err;

		ret = clk_set_rate(i2s_tdm->mclk_rx, i2s_tdm->mclk_rx_freq);
		if (ret)
			goto err;

		/* mclk_rx is also ok. */
		*mclk = i2s_tdm->mclk_tx;
	} else {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			*mclk = i2s_tdm->mclk_tx;
			mclk_freq = i2s_tdm->mclk_tx_freq;
		} else {
			*mclk = i2s_tdm->mclk_rx;
			mclk_freq = i2s_tdm->mclk_rx_freq;
		}

		ret = clk_set_rate(*mclk, mclk_freq);
		if (ret)
			goto err;
	}

	return 0;

err:
	return ret;
}

static int rockchip_i2s_io_multiplex(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(dai);
	int usable_chs = MULTIPLEX_CH_MAX;
	unsigned int val = 0;

	if (!i2s_tdm->io_multiplex)
		return 0;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		struct snd_pcm_str *playback_str =
			&substream->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK];

		if (playback_str->substream_opened) {
			regmap_read(i2s_tdm->regmap, I2S_TXCR, &val);
			val &= I2S_TXCR_CSR_MASK;
			usable_chs = MULTIPLEX_CH_MAX - to_ch_num(val);
		}

		regmap_read(i2s_tdm->regmap, I2S_RXCR, &val);
		val &= I2S_RXCR_CSR_MASK;

		if (to_ch_num(val) > usable_chs) {
			dev_err(i2s_tdm->dev,
				"Capture chs(%d) > usable chs(%d)\n",
				to_ch_num(val), usable_chs);
			return -EINVAL;
		}

		switch (val) {
		case I2S_CHN_4:
			val = I2S_IO_6CH_OUT_4CH_IN;
			break;
		case I2S_CHN_6:
			val = I2S_IO_4CH_OUT_6CH_IN;
			break;
		case I2S_CHN_8:
			val = I2S_IO_2CH_OUT_8CH_IN;
			break;
		default:
			val = I2S_IO_8CH_OUT_2CH_IN;
			break;
		}
	} else {
		struct snd_pcm_str *capture_str =
			&substream->pcm->streams[SNDRV_PCM_STREAM_CAPTURE];

		if (capture_str->substream_opened) {
			regmap_read(i2s_tdm->regmap, I2S_RXCR, &val);
			val &= I2S_RXCR_CSR_MASK;
			usable_chs = MULTIPLEX_CH_MAX - to_ch_num(val);
		}

		regmap_read(i2s_tdm->regmap, I2S_TXCR, &val);
		val &= I2S_TXCR_CSR_MASK;

		if (to_ch_num(val) > usable_chs) {
			dev_err(i2s_tdm->dev,
				"Playback chs(%d) > usable chs(%d)\n",
				to_ch_num(val), usable_chs);
			return -EINVAL;
		}

		switch (val) {
		case I2S_CHN_4:
			val = I2S_IO_4CH_OUT_6CH_IN;
			break;
		case I2S_CHN_6:
			val = I2S_IO_6CH_OUT_4CH_IN;
			break;
		case I2S_CHN_8:
			val = I2S_IO_8CH_OUT_2CH_IN;
			break;
		default:
			val = I2S_IO_2CH_OUT_8CH_IN;
			break;
		}
	}

	val <<= i2s_tdm->soc_data->grf_shift;
	val |= (I2S_IO_DIRECTION_MASK << i2s_tdm->soc_data->grf_shift) << 16;
	regmap_write(i2s_tdm->grf, i2s_tdm->soc_data->grf_reg_offset, val);

	return 0;
}

static int rockchip_i2s_trcm_mode(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai,
				  unsigned int div_bclk,
				  unsigned int div_lrck,
				  unsigned int fmt)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(dai);
	unsigned long flags;

	if (!i2s_tdm->clk_trcm)
		return 0;

	spin_lock_irqsave(&i2s_tdm->lock, flags);
	if (atomic_read(&i2s_tdm->refcount))
		rockchip_i2s_tdm_xfer_pause(substream, i2s_tdm);

	regmap_update_bits(i2s_tdm->regmap, I2S_CLKDIV,
			   I2S_CLKDIV_TXM_MASK | I2S_CLKDIV_RXM_MASK,
			   I2S_CLKDIV_TXM(div_bclk) | I2S_CLKDIV_RXM(div_bclk));
	regmap_update_bits(i2s_tdm->regmap, I2S_CKR,
			   I2S_CKR_TSD_MASK | I2S_CKR_RSD_MASK,
			   I2S_CKR_TSD(div_lrck) | I2S_CKR_RSD(div_lrck));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		regmap_update_bits(i2s_tdm->regmap, I2S_TXCR,
				   I2S_TXCR_VDW_MASK | I2S_TXCR_CSR_MASK,
				   fmt);
	else
		regmap_update_bits(i2s_tdm->regmap, I2S_RXCR,
				   I2S_RXCR_VDW_MASK | I2S_RXCR_CSR_MASK,
				   fmt);

	if (atomic_read(&i2s_tdm->refcount))
		rockchip_i2s_tdm_xfer_resume(substream, i2s_tdm);
	spin_unlock_irqrestore(&i2s_tdm->lock, flags);

	return 0;
}

static int rockchip_i2s_tdm_params_channels(struct snd_pcm_substream *substream,
					    struct snd_pcm_hw_params *params,
					    struct snd_soc_dai *dai)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(dai);
	unsigned int reg_fmt, fmt;
	int ret = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		reg_fmt = I2S_TXCR;
	else
		reg_fmt = I2S_RXCR;

	regmap_read(i2s_tdm->regmap, reg_fmt, &fmt);
	fmt &= I2S_TXCR_TFS_MASK;

	if (fmt == I2S_TXCR_TFS_TDM_I2S && !i2s_tdm->tdm_fsync_half_frame) {
		switch (params_channels(params)) {
		case 16:
			ret = I2S_CHN_8;
			break;
		case 12:
			ret = I2S_CHN_6;
			break;
		case 8:
			ret = I2S_CHN_4;
			break;
		case 4:
			ret = I2S_CHN_2;
			break;
		default:
			ret = -EINVAL;
			break;
		}
	} else {
		switch (params_channels(params)) {
		case 8:
			ret = I2S_CHN_8;
			break;
		case 6:
			ret = I2S_CHN_6;
			break;
		case 4:
			ret = I2S_CHN_4;
			break;
		case 2:
			ret = I2S_CHN_2;
			break;
		default:
			ret = -EINVAL;
			break;
		}
	}

	return ret;
}

static int rockchip_i2s_tdm_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(dai);
	struct clk *mclk;
	int ret = 0;
	unsigned int val = 0;
	unsigned int mclk_rate, bclk_rate, div_bclk = 4, div_lrck = 64;

	if (i2s_tdm->is_master_mode) {
		if (i2s_tdm->mclk_calibrate)
			rockchip_i2s_tdm_calibrate_mclk(i2s_tdm, substream,
							params_rate(params));

		ret = rockchip_i2s_tdm_set_mclk(i2s_tdm, substream, &mclk);
		if (ret)
			goto err;

		mclk_rate = clk_get_rate(mclk);
		bclk_rate = i2s_tdm->bclk_fs * params_rate(params);
		if (!bclk_rate) {
			ret = -EINVAL;
			goto err;
		}
		div_bclk = DIV_ROUND_CLOSEST(mclk_rate, bclk_rate);
		div_lrck = bclk_rate / params_rate(params);
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

	ret = rockchip_i2s_tdm_params_channels(substream, params, dai);
	if (ret < 0)
		goto err;

	val |= ret;
	if (i2s_tdm->clk_trcm) {
		rockchip_i2s_trcm_mode(substream, dai, div_bclk, div_lrck, val);
	} else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_update_bits(i2s_tdm->regmap, I2S_CLKDIV,
				   I2S_CLKDIV_TXM_MASK,
				   I2S_CLKDIV_TXM(div_bclk));
		regmap_update_bits(i2s_tdm->regmap, I2S_CKR,
				   I2S_CKR_TSD_MASK,
				   I2S_CKR_TSD(div_lrck));
		regmap_update_bits(i2s_tdm->regmap, I2S_TXCR,
				   I2S_TXCR_VDW_MASK | I2S_TXCR_CSR_MASK,
				   val);
	} else {
		regmap_update_bits(i2s_tdm->regmap, I2S_CLKDIV,
				   I2S_CLKDIV_RXM_MASK,
				   I2S_CLKDIV_RXM(div_bclk));
		regmap_update_bits(i2s_tdm->regmap, I2S_CKR,
				   I2S_CKR_RSD_MASK,
				   I2S_CKR_RSD(div_lrck));
		regmap_update_bits(i2s_tdm->regmap, I2S_RXCR,
				   I2S_RXCR_VDW_MASK | I2S_RXCR_CSR_MASK,
				   val);
	}

	ret = rockchip_i2s_io_multiplex(substream, dai);

err:
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

static int rockchip_i2s_tdm_set_sysclk(struct snd_soc_dai *cpu_dai, int stream,
				       unsigned int freq, int dir)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(cpu_dai);

	/* Put set mclk rate into rockchip_i2s_tdm_set_mclk() */
	if (i2s_tdm->clk_trcm) {
		i2s_tdm->mclk_tx_freq = freq;
		i2s_tdm->mclk_rx_freq = freq;
	} else {
		if (stream == SNDRV_PCM_STREAM_PLAYBACK)
			i2s_tdm->mclk_tx_freq = freq;
		else
			i2s_tdm->mclk_rx_freq = freq;
	}

	dev_dbg(i2s_tdm->dev, "The target mclk_%s freq is: %d\n",
		stream ? "rx" : "tx", freq);

	return 0;
}

static int rockchip_i2s_tdm_clk_compensation_info(struct snd_kcontrol *kcontrol,
						  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = CLK_PPM_MIN;
	uinfo->value.integer.max = CLK_PPM_MAX;
	uinfo->value.integer.step = 1;

	return 0;
}

static int rockchip_i2s_tdm_clk_compensation_get(struct snd_kcontrol *kcontrol,
						 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct rk_i2s_tdm_dev *i2s_tdm = snd_soc_dai_get_drvdata(dai);

	ucontrol->value.integer.value[0] = i2s_tdm->clk_ppm;

	return 0;
}

static int rockchip_i2s_tdm_clk_compensation_put(struct snd_kcontrol *kcontrol,
						 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct rk_i2s_tdm_dev *i2s_tdm = snd_soc_dai_get_drvdata(dai);
	int ret = 0, ppm = 0;

	if ((ucontrol->value.integer.value[0] < CLK_PPM_MIN) ||
	    (ucontrol->value.integer.value[0] > CLK_PPM_MAX))
		return -EINVAL;

	ppm = ucontrol->value.integer.value[0];

	ret = rockchip_i2s_tdm_clk_set_rate(i2s_tdm, i2s_tdm->mclk_root0,
					    i2s_tdm->mclk_root0_freq, ppm);
	if (ret)
		return ret;

	if (clk_is_match(i2s_tdm->mclk_root0, i2s_tdm->mclk_root1))
		return 0;

	ret = rockchip_i2s_tdm_clk_set_rate(i2s_tdm, i2s_tdm->mclk_root1,
					    i2s_tdm->mclk_root1_freq, ppm);

	return ret;
}

static struct snd_kcontrol_new rockchip_i2s_tdm_compensation_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.name = "PCM Clk Compensation In PPM",
	.info = rockchip_i2s_tdm_clk_compensation_info,
	.get = rockchip_i2s_tdm_clk_compensation_get,
	.put = rockchip_i2s_tdm_clk_compensation_put,
};

static int rockchip_i2s_tdm_dai_probe(struct snd_soc_dai *dai)
{
	struct rk_i2s_tdm_dev *i2s_tdm = snd_soc_dai_get_drvdata(dai);

	dai->capture_dma_data = &i2s_tdm->capture_dma_data;
	dai->playback_dma_data = &i2s_tdm->playback_dma_data;

	if (i2s_tdm->mclk_calibrate)
		snd_soc_add_dai_controls(dai, &rockchip_i2s_tdm_compensation_control, 1);

	return 0;
}

static int rockchip_dai_tdm_slot(struct snd_soc_dai *dai,
				 unsigned int tx_mask, unsigned int rx_mask,
				 int slots, int slot_width)
{
	struct rk_i2s_tdm_dev *i2s_tdm = snd_soc_dai_get_drvdata(dai);
	unsigned int mask, val;

	i2s_tdm->tdm_mode = true;
	i2s_tdm->bclk_fs = slots * slot_width;
	mask = TDM_SLOT_BIT_WIDTH_MSK | TDM_FRAME_WIDTH_MSK;
	val = TDM_SLOT_BIT_WIDTH(slot_width) |
	      TDM_FRAME_WIDTH(slots * slot_width);
	regmap_update_bits(i2s_tdm->regmap, I2S_TDM_TXCR,
			   mask, val);
	regmap_update_bits(i2s_tdm->regmap, I2S_TDM_RXCR,
			   mask, val);

	return 0;
}

static const struct snd_soc_dai_ops rockchip_i2s_tdm_dai_ops = {
	.hw_params = rockchip_i2s_tdm_hw_params,
	.set_sysclk = rockchip_i2s_tdm_set_sysclk,
	.set_fmt = rockchip_i2s_tdm_set_fmt,
	.set_tdm_slot = rockchip_dai_tdm_slot,
	.trigger = rockchip_i2s_tdm_trigger,
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
	case I2S_TXDR:
	case I2S_RXDR:
	case I2S_TXFIFOLR:
	case I2S_INTSR:
	case I2S_RXFIFOLR:
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
	case I2S_TXFIFOLR:
	case I2S_INTSR:
	case I2S_CLR:
	case I2S_TXDR:
	case I2S_RXDR:
	case I2S_RXFIFOLR:
		return true;
	default:
		return false;
	}
}

static bool rockchip_i2s_tdm_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case I2S_RXDR:
		return true;
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

static int common_soc_init(struct device *dev, u32 addr)
{
	struct rk_i2s_tdm_dev *i2s_tdm = dev_get_drvdata(dev);
	const struct txrx_config *configs = i2s_tdm->soc_data->configs;
	u32 reg = 0, val = 0, trcm = i2s_tdm->clk_trcm;
	int i;

	switch (trcm) {
	case I2S_CKR_TRCM_TXONLY:
		/* fall through */
	case I2S_CKR_TRCM_RXONLY:
		break;
	default:
		return 0;
	}

	for (i = 0; i < i2s_tdm->soc_data->config_count; i++) {
		if (addr != configs[i].addr)
			continue;
		reg = configs[i].reg;
		if (trcm == I2S_CKR_TRCM_TXONLY)
			val = configs[i].txonly;
		else
			val = configs[i].rxonly;

		if (reg)
			regmap_write(i2s_tdm->grf, reg, val);
	}

	return 0;
}

static const struct txrx_config px30_txrx_config[] = {
	{ 0xff060000, 0x184, PX30_I2S0_CLK_TXONLY, PX30_I2S0_CLK_RXONLY },
};

static const struct txrx_config rk1808_txrx_config[] = {
	{ 0xff7e0000, 0x190, RK1808_I2S0_CLK_TXONLY, RK1808_I2S0_CLK_RXONLY },
};

static const struct txrx_config rk3308_txrx_config[] = {
	{ 0xff300000, 0x308, RK3308_I2S0_CLK_TXONLY, RK3308_I2S0_CLK_RXONLY },
	{ 0xff310000, 0x308, RK3308_I2S1_CLK_TXONLY, RK3308_I2S1_CLK_RXONLY },
};

static const struct txrx_config rk3568_txrx_config[] = {
	{ 0xfe410000, 0x504, RK3568_I2S1_CLK_TXONLY, RK3568_I2S1_CLK_RXONLY },
	{ 0xfe430000, 0x504, RK3568_I2S3_CLK_TXONLY, RK3568_I2S3_CLK_RXONLY },
	{ 0xfe430000, 0x508, RK3568_I2S3_MCLK_TXONLY, RK3568_I2S3_MCLK_RXONLY },
};

static const struct txrx_config rv1126_txrx_config[] = {
	{ 0xff800000, 0x10260, RV1126_I2S0_CLK_TXONLY, RV1126_I2S0_CLK_RXONLY },
};

static struct rk_i2s_soc_data px30_i2s_soc_data = {
	.softrst_offset = 0x0300,
	.configs = px30_txrx_config,
	.config_count = ARRAY_SIZE(px30_txrx_config),
	.init = common_soc_init,
};

static struct rk_i2s_soc_data rk1808_i2s_soc_data = {
	.softrst_offset = 0x0300,
	.configs = rk1808_txrx_config,
	.config_count = ARRAY_SIZE(rk1808_txrx_config),
	.init = common_soc_init,
};

static struct rk_i2s_soc_data rk3308_i2s_soc_data = {
	.softrst_offset = 0x0400,
	.grf_reg_offset = 0x0308,
	.grf_shift = 5,
	.configs = rk3308_txrx_config,
	.config_count = ARRAY_SIZE(rk3308_txrx_config),
	.init = common_soc_init,
};

static struct rk_i2s_soc_data rk3568_i2s_soc_data = {
	.softrst_offset = 0x0400,
	.configs = rk3568_txrx_config,
	.config_count = ARRAY_SIZE(rk3568_txrx_config),
	.init = common_soc_init,
};

static struct rk_i2s_soc_data rv1126_i2s_soc_data = {
	.softrst_offset = 0x0300,
	.configs = rv1126_txrx_config,
	.config_count = ARRAY_SIZE(rv1126_txrx_config),
	.init = common_soc_init,
};

static const struct of_device_id rockchip_i2s_tdm_match[] = {
	{ .compatible = "rockchip,px30-i2s-tdm", .data = &px30_i2s_soc_data },
	{ .compatible = "rockchip,rk1808-i2s-tdm", .data = &rk1808_i2s_soc_data },
	{ .compatible = "rockchip,rk3308-i2s-tdm", .data = &rk3308_i2s_soc_data },
	{ .compatible = "rockchip,rk3568-i2s-tdm", .data = &rk3568_i2s_soc_data },
	{ .compatible = "rockchip,rv1126-i2s-tdm", .data = &rv1126_i2s_soc_data },
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

static int rockchip_i2s_tdm_dai_prepare(struct platform_device *pdev,
					struct snd_soc_dai_driver **soc_dai)
{
	struct snd_soc_dai_driver rockchip_i2s_tdm_dai = {
		.probe = rockchip_i2s_tdm_dai_probe,
		.playback = {
			.stream_name = "Playback",
			.channels_min = 2,
			.channels_max = 16,
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
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S8 |
				    SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S20_3LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
		.ops = &rockchip_i2s_tdm_dai_ops,
	};

	*soc_dai = devm_kmemdup(&pdev->dev, &rockchip_i2s_tdm_dai,
				sizeof(rockchip_i2s_tdm_dai), GFP_KERNEL);
	if (!(*soc_dai))
		return -ENOMEM;

	return 0;
}

static int rockchip_i2s_tdm_path_check(struct rk_i2s_tdm_dev *i2s_tdm,
				       int num,
				       bool is_rx_path)
{
	unsigned int *i2s_data;
	int i, j, ret = 0;

	if (is_rx_path)
		i2s_data = i2s_tdm->i2s_sdis;
	else
		i2s_data = i2s_tdm->i2s_sdos;

	for (i = 0; i < num; i++) {
		if (i2s_data[i] > CH_GRP_MAX - 1) {
			dev_err(i2s_tdm->dev,
				"%s path i2s_data[%d]: %d is overflow, max is: %d\n",
				is_rx_path ? "RX" : "TX",
				i, i2s_data[i], CH_GRP_MAX);
			ret = -EINVAL;
			goto err;
		}

		for (j = 0; j < num; j++) {
			if (i == j)
				continue;

			if (i2s_data[i] == i2s_data[j]) {
				dev_err(i2s_tdm->dev,
					"%s path invalid routed i2s_data: [%d]%d == [%d]%d\n",
					is_rx_path ? "RX" : "TX",
					i, i2s_data[i],
					j, i2s_data[j]);
				ret = -EINVAL;
				goto err;
			}
		}
	}

err:
	return ret;
}

static void rockchip_i2s_tdm_tx_path_config(struct rk_i2s_tdm_dev *i2s_tdm,
					    int num)
{
	int idx;

	for (idx = 0; idx < num; idx++) {
		regmap_update_bits(i2s_tdm->regmap, I2S_TXCR,
				   I2S_TXCR_PATH_MASK(idx),
				   I2S_TXCR_PATH(idx, i2s_tdm->i2s_sdos[idx]));
	}
}

static void rockchip_i2s_tdm_rx_path_config(struct rk_i2s_tdm_dev *i2s_tdm,
					    int num)
{
	int idx;

	for (idx = 0; idx < num; idx++) {
		regmap_update_bits(i2s_tdm->regmap, I2S_RXCR,
				   I2S_RXCR_PATH_MASK(idx),
				   I2S_RXCR_PATH(idx, i2s_tdm->i2s_sdis[idx]));
	}
}

static void rockchip_i2s_tdm_path_config(struct rk_i2s_tdm_dev *i2s_tdm,
					 int num, bool is_rx_path)
{
	if (is_rx_path)
		rockchip_i2s_tdm_rx_path_config(i2s_tdm, num);
	else
		rockchip_i2s_tdm_tx_path_config(i2s_tdm, num);
}

static int rockchip_i2s_tdm_path_prepare(struct rk_i2s_tdm_dev *i2s_tdm,
					 struct device_node *np,
					 bool is_rx_path)
{
	char *i2s_tx_path_prop = "rockchip,i2s-tx-route";
	char *i2s_rx_path_prop = "rockchip,i2s-rx-route";
	char *i2s_path_prop;
	unsigned int *i2s_data;
	int num, ret = 0;

	if (is_rx_path) {
		i2s_path_prop = i2s_rx_path_prop;
		i2s_data = i2s_tdm->i2s_sdis;
	} else {
		i2s_path_prop = i2s_tx_path_prop;
		i2s_data = i2s_tdm->i2s_sdos;
	}

	num = of_count_phandle_with_args(np, i2s_path_prop, NULL);
	if (num < 0) {
		if (num != -ENOENT) {
			dev_err(i2s_tdm->dev,
				"Failed to read '%s' num: %d\n",
				i2s_path_prop, num);
			ret = num;
		}
		goto out;
	} else if (num != CH_GRP_MAX) {
		dev_err(i2s_tdm->dev,
			"The num: %d should be: %d\n", num, CH_GRP_MAX);
		ret = -EINVAL;
		goto out;
	}

	ret = of_property_read_u32_array(np, i2s_path_prop,
					 i2s_data, num);
	if (ret < 0) {
		dev_err(i2s_tdm->dev,
			"Failed to read '%s': %d\n",
			i2s_path_prop, ret);
		goto out;
	}

	ret = rockchip_i2s_tdm_path_check(i2s_tdm, num, is_rx_path);
	if (ret < 0) {
		dev_err(i2s_tdm->dev,
			"Failed to check i2s data bus: %d\n", ret);
		goto out;
	}

	rockchip_i2s_tdm_path_config(i2s_tdm, num, is_rx_path);

out:
	return ret;
}

static int rockchip_i2s_tdm_tx_path_prepare(struct rk_i2s_tdm_dev *i2s_tdm,
					    struct device_node *np)
{
	return rockchip_i2s_tdm_path_prepare(i2s_tdm, np, 0);
}

static int rockchip_i2s_tdm_rx_path_prepare(struct rk_i2s_tdm_dev *i2s_tdm,
					    struct device_node *np)
{
	return rockchip_i2s_tdm_path_prepare(i2s_tdm, np, 1);
}

static int rockchip_i2s_tdm_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *cru_node;
	const struct of_device_id *of_id;
	struct rk_i2s_tdm_dev *i2s_tdm;
	struct snd_soc_dai_driver *soc_dai;
	struct resource *res;
	void __iomem *regs;
	bool sync;
	int ret;
	int val;

	ret = rockchip_i2s_tdm_dai_prepare(pdev, &soc_dai);
	if (ret)
		return ret;

	i2s_tdm = devm_kzalloc(&pdev->dev, sizeof(*i2s_tdm), GFP_KERNEL);
	if (!i2s_tdm)
		return -ENOMEM;

	i2s_tdm->dev = &pdev->dev;

	of_id = of_match_device(rockchip_i2s_tdm_match, &pdev->dev);
	if (!of_id || !of_id->data)
		return -EINVAL;

	spin_lock_init(&i2s_tdm->lock);
	i2s_tdm->soc_data = (struct rk_i2s_soc_data *)of_id->data;

	i2s_tdm->bclk_fs = 64;
	if (!of_property_read_u32(node, "rockchip,bclk-fs", &val)) {
		if ((val >= 32) && (val % 2 == 0))
			i2s_tdm->bclk_fs = val;
	}

	i2s_tdm->clk_trcm = I2S_CKR_TRCM_TXRX;
	if (!of_property_read_u32(node, "rockchip,clk-trcm", &val)) {
		if (val >= 0 && val <= 2) {
			i2s_tdm->clk_trcm = val << I2S_CKR_TRCM_SHIFT;
			if (i2s_tdm->clk_trcm)
				soc_dai->symmetric_rates = 1;
		}
	}

	i2s_tdm->tdm_fsync_half_frame =
		of_property_read_bool(node, "rockchip,tdm-fsync-half-frame");

	if (of_property_read_bool(node, "rockchip,playback-only"))
		soc_dai->capture.channels_min = 0;
	else if (of_property_read_bool(node, "rockchip,capture-only"))
		soc_dai->playback.channels_min = 0;

	i2s_tdm->grf = syscon_regmap_lookup_by_phandle(node, "rockchip,grf");
	if (IS_ERR(i2s_tdm->grf))
		return PTR_ERR(i2s_tdm->grf);

	sync = of_device_is_compatible(node, "rockchip,px30-i2s-tdm") ||
	       of_device_is_compatible(node, "rockchip,rk1808-i2s-tdm") ||
	       of_device_is_compatible(node, "rockchip,rk3308-i2s-tdm");

	if (i2s_tdm->clk_trcm && sync) {
		cru_node = of_parse_phandle(node, "rockchip,cru", 0);
		i2s_tdm->cru_base = of_iomap(cru_node, 0);
		if (!i2s_tdm->cru_base)
			return -ENOENT;

		i2s_tdm->tx_reset_id = of_i2s_resetid_get(node, "tx-m");
		i2s_tdm->rx_reset_id = of_i2s_resetid_get(node, "rx-m");
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

	i2s_tdm->io_multiplex =
		of_property_read_bool(node, "rockchip,io-multiplex");

	i2s_tdm->mclk_calibrate =
		of_property_read_bool(node, "rockchip,mclk-calibrate");
	if (i2s_tdm->mclk_calibrate) {
		i2s_tdm->mclk_tx_src = devm_clk_get(&pdev->dev, "mclk_tx_src");
		if (IS_ERR(i2s_tdm->mclk_tx_src))
			return PTR_ERR(i2s_tdm->mclk_tx_src);

		i2s_tdm->mclk_rx_src = devm_clk_get(&pdev->dev, "mclk_rx_src");
		if (IS_ERR(i2s_tdm->mclk_rx_src))
			return PTR_ERR(i2s_tdm->mclk_rx_src);

		i2s_tdm->mclk_root0 = devm_clk_get(&pdev->dev, "mclk_root0");
		if (IS_ERR(i2s_tdm->mclk_root0))
			return PTR_ERR(i2s_tdm->mclk_root0);

		i2s_tdm->mclk_root1 = devm_clk_get(&pdev->dev, "mclk_root1");
		if (IS_ERR(i2s_tdm->mclk_root1))
			return PTR_ERR(i2s_tdm->mclk_root1);

		i2s_tdm->mclk_root0_initial_freq = clk_get_rate(i2s_tdm->mclk_root0);
		i2s_tdm->mclk_root1_initial_freq = clk_get_rate(i2s_tdm->mclk_root1);
		i2s_tdm->mclk_root0_freq = i2s_tdm->mclk_root0_initial_freq;
		i2s_tdm->mclk_root1_freq = i2s_tdm->mclk_root1_initial_freq;
	}

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

	ret = rockchip_i2s_tdm_tx_path_prepare(i2s_tdm, node);
	if (ret < 0) {
		dev_err(&pdev->dev, "I2S TX path prepare failed: %d\n", ret);
		return ret;
	}

	ret = rockchip_i2s_tdm_rx_path_prepare(i2s_tdm, node);
	if (ret < 0) {
		dev_err(&pdev->dev, "I2S RX path prepare failed: %d\n", ret);
		return ret;
	}

	atomic_set(&i2s_tdm->refcount, 0);
	dev_set_drvdata(&pdev->dev, i2s_tdm);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = i2s_tdm_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	regmap_update_bits(i2s_tdm->regmap, I2S_DMACR, I2S_DMACR_TDL_MASK,
			   I2S_DMACR_TDL(16));
	regmap_update_bits(i2s_tdm->regmap, I2S_DMACR, I2S_DMACR_RDL_MASK,
			   I2S_DMACR_RDL(16));
	regmap_update_bits(i2s_tdm->regmap, I2S_CKR,
			   I2S_CKR_TRCM_MASK, i2s_tdm->clk_trcm);

	if (i2s_tdm->soc_data && i2s_tdm->soc_data->init)
		i2s_tdm->soc_data->init(&pdev->dev, res->start);

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &rockchip_i2s_tdm_component,
					      soc_dai, 1);

	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI\n");
		goto err_suspend;
	}

	if (of_property_read_bool(node, "rockchip,no-dmaengine"))
		return ret;
	ret = rockchip_pcm_platform_register(&pdev->dev);
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
