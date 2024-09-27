// SPDX-License-Identifier: GPL-2.0-only
// ALSA SoC Audio Layer - Rockchip I2S/TDM Controller driver

// Copyright (c) 2018 Rockchip Electronics Co. Ltd.
// Author: Sugar Zhang <sugar.zhang@rock-chips.com>
// Author: Nicolas Frattaroli <frattaroli.nicolas@gmail.com>

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

#include "rockchip_i2s_tdm.h"

#define DRV_NAME "rockchip-i2s-tdm"

#define DEFAULT_MCLK_FS				256
#define CH_GRP_MAX				4  /* The max channel 8 / 2 */
#define MULTIPLEX_CH_MAX			10

#define TRCM_TXRX 0
#define TRCM_TX 1
#define TRCM_RX 2

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
	struct regmap *regmap;
	struct regmap *grf;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct reset_control *tx_reset;
	struct reset_control *rx_reset;
	const struct rk_i2s_soc_data *soc_data;
	bool is_master_mode;
	bool io_multiplex;
	bool tdm_mode;
	unsigned int frame_width;
	unsigned int clk_trcm;
	unsigned int i2s_sdis[CH_GRP_MAX];
	unsigned int i2s_sdos[CH_GRP_MAX];
	int refcount;
	spinlock_t lock; /* xfer lock */
	bool has_playback;
	bool has_capture;
	struct snd_soc_dai_driver *dai;
};

static int to_ch_num(unsigned int val)
{
	switch (val) {
	case I2S_CHN_4:
		return 4;
	case I2S_CHN_6:
		return 6;
	case I2S_CHN_8:
		return 8;
	default:
		return 2;
	}
}

static void i2s_tdm_disable_unprepare_mclk(struct rk_i2s_tdm_dev *i2s_tdm)
{
	clk_disable_unprepare(i2s_tdm->mclk_tx);
	clk_disable_unprepare(i2s_tdm->mclk_rx);
}

/**
 * i2s_tdm_prepare_enable_mclk - prepare to enable all mclks, disable them on
 *				 failure.
 * @i2s_tdm: rk_i2s_tdm_dev struct
 *
 * This function attempts to enable all mclk clocks, but cleans up after
 * itself on failure. Guarantees to balance its calls.
 *
 * Returns success (0) or negative errno.
 */
static int i2s_tdm_prepare_enable_mclk(struct rk_i2s_tdm_dev *i2s_tdm)
{
	int ret = 0;

	ret = clk_prepare_enable(i2s_tdm->mclk_tx);
	if (ret)
		goto err_mclk_tx;
	ret = clk_prepare_enable(i2s_tdm->mclk_rx);
	if (ret)
		goto err_mclk_rx;

	return 0;

err_mclk_rx:
	clk_disable_unprepare(i2s_tdm->mclk_tx);
err_mclk_tx:
	return ret;
}

static int __maybe_unused i2s_tdm_runtime_suspend(struct device *dev)
{
	struct rk_i2s_tdm_dev *i2s_tdm = dev_get_drvdata(dev);

	regcache_cache_only(i2s_tdm->regmap, true);
	i2s_tdm_disable_unprepare_mclk(i2s_tdm);

	clk_disable_unprepare(i2s_tdm->hclk);

	return 0;
}

static int __maybe_unused i2s_tdm_runtime_resume(struct device *dev)
{
	struct rk_i2s_tdm_dev *i2s_tdm = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(i2s_tdm->hclk);
	if (ret)
		goto err_hclk;

	ret = i2s_tdm_prepare_enable_mclk(i2s_tdm);
	if (ret)
		goto err_mclk;

	regcache_cache_only(i2s_tdm->regmap, false);
	regcache_mark_dirty(i2s_tdm->regmap);

	ret = regcache_sync(i2s_tdm->regmap);
	if (ret)
		goto err_regcache;

	return 0;

err_regcache:
	i2s_tdm_disable_unprepare_mclk(i2s_tdm);
err_mclk:
	clk_disable_unprepare(i2s_tdm->hclk);
err_hclk:
	return ret;
}

static inline struct rk_i2s_tdm_dev *to_info(struct snd_soc_dai *dai)
{
	return snd_soc_dai_get_drvdata(dai);
}

/*
 * Makes sure that both tx and rx are reset at the same time to sync lrck
 * when clk_trcm > 0.
 */
static void rockchip_snd_xfer_sync_reset(struct rk_i2s_tdm_dev *i2s_tdm)
{
	/* This is technically race-y.
	 *
	 * In an ideal world, we could atomically assert both resets at the
	 * same time, through an atomic bulk reset API. This API however does
	 * not exist, so what the downstream vendor code used to do was
	 * implement half a reset controller here and require the CRU to be
	 * passed to the driver as a device tree node. Violating abstractions
	 * like that is bad, especially when it influences something like the
	 * bindings which are supposed to describe the hardware, not whatever
	 * workarounds the driver needs, so it was dropped.
	 *
	 * In practice, asserting the resets one by one appears to work just
	 * fine for playback. During duplex (playback + capture) operation,
	 * this might become an issue, but that should be solved by the
	 * implementation of the aforementioned API, not by shoving a reset
	 * controller into an audio driver.
	 */

	reset_control_assert(i2s_tdm->tx_reset);
	reset_control_assert(i2s_tdm->rx_reset);
	udelay(10);
	reset_control_deassert(i2s_tdm->tx_reset);
	reset_control_deassert(i2s_tdm->rx_reset);
	udelay(10);
}

static void rockchip_snd_reset(struct reset_control *rc)
{
	reset_control_assert(rc);
	udelay(10);
	reset_control_deassert(rc);
	udelay(10);
}

static void rockchip_snd_xfer_clear(struct rk_i2s_tdm_dev *i2s_tdm,
				    unsigned int clr)
{
	unsigned int xfer_mask = 0;
	unsigned int xfer_val = 0;
	unsigned int val;
	int retry = 10;
	bool tx = clr & I2S_CLR_TXC;
	bool rx = clr & I2S_CLR_RXC;

	if (!(rx || tx))
		return;

	if (tx) {
		xfer_mask = I2S_XFER_TXS_START;
		xfer_val = I2S_XFER_TXS_STOP;
	}
	if (rx) {
		xfer_mask |= I2S_XFER_RXS_START;
		xfer_val |= I2S_XFER_RXS_STOP;
	}

	regmap_update_bits(i2s_tdm->regmap, I2S_XFER, xfer_mask, xfer_val);
	udelay(150);
	regmap_update_bits(i2s_tdm->regmap, I2S_CLR, clr, clr);

	regmap_read(i2s_tdm->regmap, I2S_CLR, &val);
	/* Wait on the clear operation to finish */
	while (val) {
		udelay(15);
		regmap_read(i2s_tdm->regmap, I2S_CLR, &val);
		retry--;
		if (!retry) {
			dev_warn(i2s_tdm->dev, "clear failed, reset %s%s\n",
				 tx ? "tx" : "", rx ? "rx" : "");
			if (rx && tx)
				rockchip_snd_xfer_sync_reset(i2s_tdm);
			else if (tx)
				rockchip_snd_reset(i2s_tdm->tx_reset);
			else if (rx)
				rockchip_snd_reset(i2s_tdm->rx_reset);
			break;
		}
	}
}

static inline void rockchip_enable_tde(struct regmap *regmap)
{
	regmap_update_bits(regmap, I2S_DMACR, I2S_DMACR_TDE_ENABLE,
			   I2S_DMACR_TDE_ENABLE);
}

static inline void rockchip_disable_tde(struct regmap *regmap)
{
	regmap_update_bits(regmap, I2S_DMACR, I2S_DMACR_TDE_ENABLE,
			   I2S_DMACR_TDE_DISABLE);
}

static inline void rockchip_enable_rde(struct regmap *regmap)
{
	regmap_update_bits(regmap, I2S_DMACR, I2S_DMACR_RDE_ENABLE,
			   I2S_DMACR_RDE_ENABLE);
}

static inline void rockchip_disable_rde(struct regmap *regmap)
{
	regmap_update_bits(regmap, I2S_DMACR, I2S_DMACR_RDE_ENABLE,
			   I2S_DMACR_RDE_DISABLE);
}

/* only used when clk_trcm > 0 */
static void rockchip_snd_txrxctrl(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai, int on)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(dai);
	unsigned long flags;

	spin_lock_irqsave(&i2s_tdm->lock, flags);
	if (on) {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			rockchip_enable_tde(i2s_tdm->regmap);
		else
			rockchip_enable_rde(i2s_tdm->regmap);

		if (++i2s_tdm->refcount == 1) {
			rockchip_snd_xfer_sync_reset(i2s_tdm);
			regmap_update_bits(i2s_tdm->regmap, I2S_XFER,
					   I2S_XFER_TXS_START |
					   I2S_XFER_RXS_START,
					   I2S_XFER_TXS_START |
					   I2S_XFER_RXS_START);
		}
	} else {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			rockchip_disable_tde(i2s_tdm->regmap);
		else
			rockchip_disable_rde(i2s_tdm->regmap);

		if (--i2s_tdm->refcount == 0) {
			rockchip_snd_xfer_clear(i2s_tdm,
						I2S_CLR_TXC | I2S_CLR_RXC);
		}
	}
	spin_unlock_irqrestore(&i2s_tdm->lock, flags);
}

static void rockchip_snd_txctrl(struct rk_i2s_tdm_dev *i2s_tdm, int on)
{
	if (on) {
		rockchip_enable_tde(i2s_tdm->regmap);

		regmap_update_bits(i2s_tdm->regmap, I2S_XFER,
				   I2S_XFER_TXS_START,
				   I2S_XFER_TXS_START);
	} else {
		rockchip_disable_tde(i2s_tdm->regmap);

		rockchip_snd_xfer_clear(i2s_tdm, I2S_CLR_TXC);
	}
}

static void rockchip_snd_rxctrl(struct rk_i2s_tdm_dev *i2s_tdm, int on)
{
	if (on) {
		rockchip_enable_rde(i2s_tdm->regmap);

		regmap_update_bits(i2s_tdm->regmap, I2S_XFER,
				   I2S_XFER_RXS_START,
				   I2S_XFER_RXS_START);
	} else {
		rockchip_disable_rde(i2s_tdm->regmap);

		rockchip_snd_xfer_clear(i2s_tdm, I2S_CLR_RXC);
	}
}

static int rockchip_i2s_tdm_set_fmt(struct snd_soc_dai *cpu_dai,
				    unsigned int fmt)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(cpu_dai);
	unsigned int mask, val, tdm_val, txcr_val, rxcr_val;
	int ret;
	bool is_tdm = i2s_tdm->tdm_mode;

	ret = pm_runtime_resume_and_get(cpu_dai->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	mask = I2S_CKR_MSS_MASK;
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		val = I2S_CKR_MSS_MASTER;
		i2s_tdm->is_master_mode = true;
		break;
	case SND_SOC_DAIFMT_BC_FC:
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

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		txcr_val = I2S_TXCR_IBM_RSJM;
		rxcr_val = I2S_RXCR_IBM_RSJM;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		txcr_val = I2S_TXCR_IBM_LSJM;
		rxcr_val = I2S_RXCR_IBM_LSJM;
		break;
	case SND_SOC_DAIFMT_I2S:
		txcr_val = I2S_TXCR_IBM_NORMAL;
		rxcr_val = I2S_RXCR_IBM_NORMAL;
		break;
	case SND_SOC_DAIFMT_DSP_A: /* PCM delay 1 mode */
		txcr_val = I2S_TXCR_TFS_PCM | I2S_TXCR_PBM_MODE(1);
		rxcr_val = I2S_RXCR_TFS_PCM | I2S_RXCR_PBM_MODE(1);
		break;
	case SND_SOC_DAIFMT_DSP_B: /* PCM no delay mode */
		txcr_val = I2S_TXCR_TFS_PCM;
		rxcr_val = I2S_RXCR_TFS_PCM;
		break;
	default:
		ret = -EINVAL;
		goto err_pm_put;
	}

	mask = I2S_TXCR_IBM_MASK | I2S_TXCR_TFS_MASK | I2S_TXCR_PBM_MASK;
	regmap_update_bits(i2s_tdm->regmap, I2S_TXCR, mask, txcr_val);

	mask = I2S_RXCR_IBM_MASK | I2S_RXCR_TFS_MASK | I2S_RXCR_PBM_MASK;
	regmap_update_bits(i2s_tdm->regmap, I2S_RXCR, mask, rxcr_val);

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
		tdm_val |= TDM_FSYNC_WIDTH_HALF_FRAME;

		mask = I2S_TXCR_TFS_MASK;
		regmap_update_bits(i2s_tdm->regmap, I2S_TXCR, mask, val);
		regmap_update_bits(i2s_tdm->regmap, I2S_RXCR, mask, val);

		mask = TDM_FSYNC_WIDTH_SEL1_MSK | TDM_FSYNC_WIDTH_SEL0_MSK |
		       TDM_SHIFT_CTRL_MSK;
		regmap_update_bits(i2s_tdm->regmap, I2S_TDM_TXCR,
				   mask, tdm_val);
		regmap_update_bits(i2s_tdm->regmap, I2S_TDM_RXCR,
				   mask, tdm_val);
	}

err_pm_put:
	pm_runtime_put(cpu_dai->dev);

	return ret;
}

static void rockchip_i2s_tdm_xfer_pause(struct snd_pcm_substream *substream,
					struct rk_i2s_tdm_dev *i2s_tdm)
{
	int stream;

	stream = SNDRV_PCM_STREAM_LAST - substream->stream;
	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		rockchip_disable_tde(i2s_tdm->regmap);
	else
		rockchip_disable_rde(i2s_tdm->regmap);

	rockchip_snd_xfer_clear(i2s_tdm, I2S_CLR_TXC | I2S_CLR_RXC);
}

static void rockchip_i2s_tdm_xfer_resume(struct snd_pcm_substream *substream,
					 struct rk_i2s_tdm_dev *i2s_tdm)
{
	int stream;

	stream = SNDRV_PCM_STREAM_LAST - substream->stream;
	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		rockchip_enable_tde(i2s_tdm->regmap);
	else
		rockchip_enable_rde(i2s_tdm->regmap);

	regmap_update_bits(i2s_tdm->regmap, I2S_XFER,
			   I2S_XFER_TXS_START |
			   I2S_XFER_RXS_START,
			   I2S_XFER_TXS_START |
			   I2S_XFER_RXS_START);
}

static int rockchip_i2s_ch_to_io(unsigned int ch, bool substream_capture)
{
	if (substream_capture) {
		switch (ch) {
		case I2S_CHN_4:
			return I2S_IO_6CH_OUT_4CH_IN;
		case I2S_CHN_6:
			return I2S_IO_4CH_OUT_6CH_IN;
		case I2S_CHN_8:
			return I2S_IO_2CH_OUT_8CH_IN;
		default:
			return I2S_IO_8CH_OUT_2CH_IN;
		}
	} else {
		switch (ch) {
		case I2S_CHN_4:
			return I2S_IO_4CH_OUT_6CH_IN;
		case I2S_CHN_6:
			return I2S_IO_6CH_OUT_4CH_IN;
		case I2S_CHN_8:
			return I2S_IO_8CH_OUT_2CH_IN;
		default:
			return I2S_IO_2CH_OUT_8CH_IN;
		}
	}
}

static int rockchip_i2s_io_multiplex(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(dai);
	int usable_chs = MULTIPLEX_CH_MAX;
	unsigned int val = 0;

	if (!i2s_tdm->io_multiplex)
		return 0;

	if (IS_ERR_OR_NULL(i2s_tdm->grf)) {
		dev_err(i2s_tdm->dev,
			"io multiplex not supported for this device\n");
		return -EINVAL;
	}

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
				"Capture channels (%d) > usable channels (%d)\n",
				to_ch_num(val), usable_chs);
			return -EINVAL;
		}

		rockchip_i2s_ch_to_io(val, true);
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
				"Playback channels (%d) > usable channels (%d)\n",
				to_ch_num(val), usable_chs);
			return -EINVAL;
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
	if (i2s_tdm->refcount)
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

	if (i2s_tdm->refcount)
		rockchip_i2s_tdm_xfer_resume(substream, i2s_tdm);
	spin_unlock_irqrestore(&i2s_tdm->lock, flags);

	return 0;
}

static int rockchip_i2s_tdm_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(dai);
	unsigned int val = 0;
	unsigned int mclk_rate, bclk_rate, div_bclk = 4, div_lrck = 64;
	int err;

	if (i2s_tdm->is_master_mode) {
		struct clk *mclk;

		if (i2s_tdm->clk_trcm == TRCM_TX) {
			mclk = i2s_tdm->mclk_tx;
		} else if (i2s_tdm->clk_trcm == TRCM_RX) {
			mclk = i2s_tdm->mclk_rx;
		} else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			mclk = i2s_tdm->mclk_tx;
		} else {
			mclk = i2s_tdm->mclk_rx;
		}

		err = clk_set_rate(mclk, DEFAULT_MCLK_FS * params_rate(params));
		if (err)
			return err;

		mclk_rate = clk_get_rate(mclk);
		bclk_rate = i2s_tdm->frame_width * params_rate(params);
		if (!bclk_rate)
			return -EINVAL;

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
		return -EINVAL;
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
		return -EINVAL;
	}

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

	return rockchip_i2s_io_multiplex(substream, dai);
}

static int rockchip_i2s_tdm_trigger(struct snd_pcm_substream *substream,
				    int cmd, struct snd_soc_dai *dai)
{
	struct rk_i2s_tdm_dev *i2s_tdm = to_info(dai);

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
		return -EINVAL;
	}

	return 0;
}

static int rockchip_i2s_tdm_dai_probe(struct snd_soc_dai *dai)
{
	struct rk_i2s_tdm_dev *i2s_tdm = snd_soc_dai_get_drvdata(dai);

	if (i2s_tdm->has_capture)
		snd_soc_dai_dma_data_set_capture(dai,  &i2s_tdm->capture_dma_data);
	if (i2s_tdm->has_playback)
		snd_soc_dai_dma_data_set_playback(dai, &i2s_tdm->playback_dma_data);

	return 0;
}

static int rockchip_dai_tdm_slot(struct snd_soc_dai *dai,
				 unsigned int tx_mask, unsigned int rx_mask,
				 int slots, int slot_width)
{
	struct rk_i2s_tdm_dev *i2s_tdm = snd_soc_dai_get_drvdata(dai);
	unsigned int mask, val;

	i2s_tdm->tdm_mode = true;
	i2s_tdm->frame_width = slots * slot_width;
	mask = TDM_SLOT_BIT_WIDTH_MSK | TDM_FRAME_WIDTH_MSK;
	val = TDM_SLOT_BIT_WIDTH(slot_width) |
	      TDM_FRAME_WIDTH(slots * slot_width);
	regmap_update_bits(i2s_tdm->regmap, I2S_TDM_TXCR,
			   mask, val);
	regmap_update_bits(i2s_tdm->regmap, I2S_TDM_RXCR,
			   mask, val);

	return 0;
}

static int rockchip_i2s_tdm_set_bclk_ratio(struct snd_soc_dai *dai,
					   unsigned int ratio)
{
	struct rk_i2s_tdm_dev *i2s_tdm = snd_soc_dai_get_drvdata(dai);

	if (ratio < 32 || ratio > 512 || ratio % 2 == 1)
		return -EINVAL;

	i2s_tdm->frame_width = ratio;

	return 0;
}

static const struct snd_soc_dai_ops rockchip_i2s_tdm_dai_ops = {
	.probe = rockchip_i2s_tdm_dai_probe,
	.hw_params = rockchip_i2s_tdm_hw_params,
	.set_bclk_ratio	= rockchip_i2s_tdm_set_bclk_ratio,
	.set_fmt = rockchip_i2s_tdm_set_fmt,
	.set_tdm_slot = rockchip_dai_tdm_slot,
	.trigger = rockchip_i2s_tdm_trigger,
};

static const struct snd_soc_component_driver rockchip_i2s_tdm_component = {
	.name = DRV_NAME,
	.legacy_dai_naming = 1,
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
	if (reg == I2S_RXDR)
		return true;
	return false;
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

	if (trcm == TRCM_TXRX)
		return 0;

	if (IS_ERR_OR_NULL(i2s_tdm->grf)) {
		dev_err(i2s_tdm->dev,
			"no grf present but non-txrx TRCM specified\n");
		return -EINVAL;
	}

	for (i = 0; i < i2s_tdm->soc_data->config_count; i++) {
		if (addr != configs[i].addr)
			continue;
		reg = configs[i].reg;
		if (trcm == TRCM_TX)
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
	{ 0xfe410000, 0x508, RK3568_I2S1_MCLK_TX_OE, RK3568_I2S1_MCLK_RX_OE },
	{ 0xfe420000, 0x508, RK3568_I2S2_MCLK_OE, RK3568_I2S2_MCLK_OE },
	{ 0xfe430000, 0x504, RK3568_I2S3_CLK_TXONLY, RK3568_I2S3_CLK_RXONLY },
	{ 0xfe430000, 0x508, RK3568_I2S3_MCLK_TXONLY, RK3568_I2S3_MCLK_RXONLY },
	{ 0xfe430000, 0x508, RK3568_I2S3_MCLK_OE, RK3568_I2S3_MCLK_OE },
};

static const struct txrx_config rv1126_txrx_config[] = {
	{ 0xff800000, 0x10260, RV1126_I2S0_CLK_TXONLY, RV1126_I2S0_CLK_RXONLY },
};

static const struct rk_i2s_soc_data px30_i2s_soc_data = {
	.softrst_offset = 0x0300,
	.configs = px30_txrx_config,
	.config_count = ARRAY_SIZE(px30_txrx_config),
	.init = common_soc_init,
};

static const struct rk_i2s_soc_data rk1808_i2s_soc_data = {
	.softrst_offset = 0x0300,
	.configs = rk1808_txrx_config,
	.config_count = ARRAY_SIZE(rk1808_txrx_config),
	.init = common_soc_init,
};

static const struct rk_i2s_soc_data rk3308_i2s_soc_data = {
	.softrst_offset = 0x0400,
	.grf_reg_offset = 0x0308,
	.grf_shift = 5,
	.configs = rk3308_txrx_config,
	.config_count = ARRAY_SIZE(rk3308_txrx_config),
	.init = common_soc_init,
};

static const struct rk_i2s_soc_data rk3568_i2s_soc_data = {
	.softrst_offset = 0x0400,
	.configs = rk3568_txrx_config,
	.config_count = ARRAY_SIZE(rk3568_txrx_config),
	.init = common_soc_init,
};

static const struct rk_i2s_soc_data rv1126_i2s_soc_data = {
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
	{ .compatible = "rockchip,rk3588-i2s-tdm" },
	{ .compatible = "rockchip,rv1126-i2s-tdm", .data = &rv1126_i2s_soc_data },
	{},
};

static const struct snd_soc_dai_driver i2s_tdm_dai = {
	.ops = &rockchip_i2s_tdm_dai_ops,
};

static int rockchip_i2s_tdm_init_dai(struct rk_i2s_tdm_dev *i2s_tdm)
{
	struct snd_soc_dai_driver *dai;
	struct property *dma_names;
	const char *dma_name;
	u64 formats = (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE |
		       SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE |
		       SNDRV_PCM_FMTBIT_S32_LE);
	struct device_node *node = i2s_tdm->dev->of_node;

	of_property_for_each_string(node, "dma-names", dma_names, dma_name) {
		if (!strcmp(dma_name, "tx"))
			i2s_tdm->has_playback = true;
		if (!strcmp(dma_name, "rx"))
			i2s_tdm->has_capture = true;
	}

	dai = devm_kmemdup(i2s_tdm->dev, &i2s_tdm_dai,
			   sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	if (i2s_tdm->has_playback) {
		dai->playback.stream_name  = "Playback";
		dai->playback.channels_min = 2;
		dai->playback.channels_max = 8;
		dai->playback.rates = SNDRV_PCM_RATE_8000_192000;
		dai->playback.formats = formats;
	}

	if (i2s_tdm->has_capture) {
		dai->capture.stream_name  = "Capture";
		dai->capture.channels_min = 2;
		dai->capture.channels_max = 8;
		dai->capture.rates = SNDRV_PCM_RATE_8000_192000;
		dai->capture.formats = formats;
	}

	if (i2s_tdm->clk_trcm != TRCM_TXRX)
		dai->symmetric_rate = 1;

	i2s_tdm->dai = dai;

	return 0;
}

static int rockchip_i2s_tdm_path_check(struct rk_i2s_tdm_dev *i2s_tdm,
				       int num,
				       bool is_rx_path)
{
	unsigned int *i2s_data;
	int i, j;

	if (is_rx_path)
		i2s_data = i2s_tdm->i2s_sdis;
	else
		i2s_data = i2s_tdm->i2s_sdos;

	for (i = 0; i < num; i++) {
		if (i2s_data[i] > CH_GRP_MAX - 1) {
			dev_err(i2s_tdm->dev,
				"%s path i2s_data[%d]: %d is too high, max is: %d\n",
				is_rx_path ? "RX" : "TX",
				i, i2s_data[i], CH_GRP_MAX);
			return -EINVAL;
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
				return -EINVAL;
			}
		}
	}

	return 0;
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
		return ret;
	} else if (num != CH_GRP_MAX) {
		dev_err(i2s_tdm->dev,
			"The num: %d should be: %d\n", num, CH_GRP_MAX);
		return -EINVAL;
	}

	ret = of_property_read_u32_array(np, i2s_path_prop,
					 i2s_data, num);
	if (ret < 0) {
		dev_err(i2s_tdm->dev,
			"Failed to read '%s': %d\n",
			i2s_path_prop, ret);
		return ret;
	}

	ret = rockchip_i2s_tdm_path_check(i2s_tdm, num, is_rx_path);
	if (ret < 0) {
		dev_err(i2s_tdm->dev,
			"Failed to check i2s data bus: %d\n", ret);
		return ret;
	}

	rockchip_i2s_tdm_path_config(i2s_tdm, num, is_rx_path);

	return 0;
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
	struct rk_i2s_tdm_dev *i2s_tdm;
	struct resource *res;
	void __iomem *regs;
	int ret;

	i2s_tdm = devm_kzalloc(&pdev->dev, sizeof(*i2s_tdm), GFP_KERNEL);
	if (!i2s_tdm)
		return -ENOMEM;

	i2s_tdm->dev = &pdev->dev;

	spin_lock_init(&i2s_tdm->lock);
	i2s_tdm->soc_data = device_get_match_data(&pdev->dev);
	i2s_tdm->frame_width = 64;

	i2s_tdm->clk_trcm = TRCM_TXRX;
	if (of_property_read_bool(node, "rockchip,trcm-sync-tx-only"))
		i2s_tdm->clk_trcm = TRCM_TX;
	if (of_property_read_bool(node, "rockchip,trcm-sync-rx-only")) {
		if (i2s_tdm->clk_trcm) {
			dev_err(i2s_tdm->dev, "invalid trcm-sync configuration\n");
			return -EINVAL;
		}
		i2s_tdm->clk_trcm = TRCM_RX;
	}

	ret = rockchip_i2s_tdm_init_dai(i2s_tdm);
	if (ret)
		return ret;

	i2s_tdm->grf = syscon_regmap_lookup_by_phandle(node, "rockchip,grf");
	i2s_tdm->tx_reset = devm_reset_control_get_optional_exclusive(&pdev->dev,
								      "tx-m");
	if (IS_ERR(i2s_tdm->tx_reset)) {
		ret = PTR_ERR(i2s_tdm->tx_reset);
		return dev_err_probe(i2s_tdm->dev, ret,
				     "Error in tx-m reset control\n");
	}

	i2s_tdm->rx_reset = devm_reset_control_get_optional_exclusive(&pdev->dev,
								      "rx-m");
	if (IS_ERR(i2s_tdm->rx_reset)) {
		ret = PTR_ERR(i2s_tdm->rx_reset);
		return dev_err_probe(i2s_tdm->dev, ret,
				     "Error in rx-m reset control\n");
	}

	i2s_tdm->hclk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(i2s_tdm->hclk)) {
		return dev_err_probe(i2s_tdm->dev, PTR_ERR(i2s_tdm->hclk),
				     "Failed to get clock hclk\n");
	}

	i2s_tdm->mclk_tx = devm_clk_get(&pdev->dev, "mclk_tx");
	if (IS_ERR(i2s_tdm->mclk_tx)) {
		return dev_err_probe(i2s_tdm->dev, PTR_ERR(i2s_tdm->mclk_tx),
				     "Failed to get clock mclk_tx\n");
	}

	i2s_tdm->mclk_rx = devm_clk_get(&pdev->dev, "mclk_rx");
	if (IS_ERR(i2s_tdm->mclk_rx)) {
		return dev_err_probe(i2s_tdm->dev, PTR_ERR(i2s_tdm->mclk_rx),
				     "Failed to get clock mclk_rx\n");
	}

	i2s_tdm->io_multiplex =
		of_property_read_bool(node, "rockchip,io-multiplex");

	regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(regs)) {
		return dev_err_probe(i2s_tdm->dev, PTR_ERR(regs),
				     "Failed to get resource IORESOURCE_MEM\n");
	}

	i2s_tdm->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
						&rockchip_i2s_tdm_regmap_config);
	if (IS_ERR(i2s_tdm->regmap)) {
		return dev_err_probe(i2s_tdm->dev, PTR_ERR(i2s_tdm->regmap),
				     "Failed to initialise regmap\n");
	}

	if (i2s_tdm->has_playback) {
		i2s_tdm->playback_dma_data.addr = res->start + I2S_TXDR;
		i2s_tdm->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		i2s_tdm->playback_dma_data.maxburst = 8;
	}

	if (i2s_tdm->has_capture) {
		i2s_tdm->capture_dma_data.addr = res->start + I2S_RXDR;
		i2s_tdm->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		i2s_tdm->capture_dma_data.maxburst = 8;
	}

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

	dev_set_drvdata(&pdev->dev, i2s_tdm);

	ret = clk_prepare_enable(i2s_tdm->hclk);
	if (ret) {
		return dev_err_probe(i2s_tdm->dev, ret,
				     "Failed to enable clock hclk\n");
	}

	ret = i2s_tdm_prepare_enable_mclk(i2s_tdm);
	if (ret) {
		ret = dev_err_probe(i2s_tdm->dev, ret,
				    "Failed to enable one or more mclks\n");
		goto err_disable_hclk;
	}

	pm_runtime_enable(&pdev->dev);

	regmap_update_bits(i2s_tdm->regmap, I2S_DMACR, I2S_DMACR_TDL_MASK,
			   I2S_DMACR_TDL(16));
	regmap_update_bits(i2s_tdm->regmap, I2S_DMACR, I2S_DMACR_RDL_MASK,
			   I2S_DMACR_RDL(16));
	regmap_update_bits(i2s_tdm->regmap, I2S_CKR, I2S_CKR_TRCM_MASK,
			   i2s_tdm->clk_trcm << I2S_CKR_TRCM_SHIFT);

	if (i2s_tdm->soc_data && i2s_tdm->soc_data->init)
		i2s_tdm->soc_data->init(&pdev->dev, res->start);

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &rockchip_i2s_tdm_component,
					      i2s_tdm->dai, 1);

	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI\n");
		goto err_suspend;
	}

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM\n");
		goto err_suspend;
	}

	return 0;

err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		i2s_tdm_runtime_suspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

err_disable_hclk:
	clk_disable_unprepare(i2s_tdm->hclk);

	return ret;
}

static void rockchip_i2s_tdm_remove(struct platform_device *pdev)
{
	if (!pm_runtime_status_suspended(&pdev->dev))
		i2s_tdm_runtime_suspend(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
}

static int __maybe_unused rockchip_i2s_tdm_suspend(struct device *dev)
{
	struct rk_i2s_tdm_dev *i2s_tdm = dev_get_drvdata(dev);

	regcache_mark_dirty(i2s_tdm->regmap);

	return 0;
}

static int __maybe_unused rockchip_i2s_tdm_resume(struct device *dev)
{
	struct rk_i2s_tdm_dev *i2s_tdm = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;
	ret = regcache_sync(i2s_tdm->regmap);
	pm_runtime_put(dev);

	return ret;
}

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
		.of_match_table = rockchip_i2s_tdm_match,
		.pm = &rockchip_i2s_tdm_pm_ops,
	},
};
module_platform_driver(rockchip_i2s_tdm_driver);

MODULE_DESCRIPTION("ROCKCHIP I2S/TDM ASoC Interface");
MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, rockchip_i2s_tdm_match);
