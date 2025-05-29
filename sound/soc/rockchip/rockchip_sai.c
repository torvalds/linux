// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ALSA SoC Audio Layer - Rockchip SAI Controller driver
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 * Copyright (c) 2025 Collabora Ltd.
 */

#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>
#include <sound/tlv.h>

#include "rockchip_sai.h"

#define DRV_NAME		"rockchip-sai"

#define CLK_SHIFT_RATE_HZ_MAX	5
#define FW_RATIO_MAX		8
#define FW_RATIO_MIN		1
#define MAXBURST_PER_FIFO	8

#define TIMEOUT_US		1000
#define WAIT_TIME_MS_MAX	10000

#define MAX_LANES		4

enum fpw_mode {
	FPW_ONE_BCLK_WIDTH,
	FPW_ONE_SLOT_WIDTH,
	FPW_HALF_FRAME_WIDTH,
};

struct rk_sai_dev {
	struct device *dev;
	struct clk *hclk;
	struct clk *mclk;
	struct regmap *regmap;
	struct reset_control *rst_h;
	struct reset_control *rst_m;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct snd_pcm_substream *substreams[SNDRV_PCM_STREAM_LAST + 1];
	unsigned int mclk_rate;
	unsigned int wait_time[SNDRV_PCM_STREAM_LAST + 1];
	unsigned int tx_lanes;
	unsigned int rx_lanes;
	unsigned int sdi[MAX_LANES];
	unsigned int sdo[MAX_LANES];
	unsigned int version;
	enum fpw_mode fpw;
	int  fw_ratio;
	bool has_capture;
	bool has_playback;
	bool is_master_mode;
	bool is_tdm;
	bool initialized;
	/* protects register writes that depend on the state of XFER[1:0] */
	spinlock_t xfer_lock;
};

static bool rockchip_sai_stream_valid(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	struct rk_sai_dev *sai = snd_soc_dai_get_drvdata(dai);

	if (!substream)
		return false;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    sai->has_playback)
		return true;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE &&
	    sai->has_capture)
		return true;

	return false;
}

static int rockchip_sai_fsync_lost_detect(struct rk_sai_dev *sai, bool en)
{
	unsigned int fw, cnt;

	if (sai->is_master_mode || sai->version < SAI_VER_2311)
		return 0;

	regmap_read(sai->regmap, SAI_FSCR, &fw);
	cnt = SAI_FSCR_FW_V(fw) << 1; /* two fsync lost */

	regmap_update_bits(sai->regmap, SAI_INTCR,
			   SAI_INTCR_FSLOSTC, SAI_INTCR_FSLOSTC);
	regmap_update_bits(sai->regmap, SAI_INTCR,
			   SAI_INTCR_FSLOST_MASK,
			   SAI_INTCR_FSLOST(en));
	/*
	 * The `cnt` is the number of SCLK cycles of the CRU's SCLK signal that
	 * should be used as timeout. Consequently, in slave mode, this value
	 * is only correct if the CRU SCLK is equal to the external SCLK.
	 */
	regmap_update_bits(sai->regmap, SAI_FS_TIMEOUT,
			   SAI_FS_TIMEOUT_VAL_MASK | SAI_FS_TIMEOUT_EN_MASK,
			   SAI_FS_TIMEOUT_VAL(cnt) | SAI_FS_TIMEOUT_EN(en));

	return 0;
}

static int rockchip_sai_fsync_err_detect(struct rk_sai_dev *sai,
					 bool en)
{
	if (sai->is_master_mode || sai->version < SAI_VER_2311)
		return 0;

	regmap_update_bits(sai->regmap, SAI_INTCR,
			   SAI_INTCR_FSERRC, SAI_INTCR_FSERRC);
	regmap_update_bits(sai->regmap, SAI_INTCR,
			   SAI_INTCR_FSERR_MASK,
			   SAI_INTCR_FSERR(en));

	return 0;
}

static int rockchip_sai_poll_clk_idle(struct rk_sai_dev *sai)
{
	unsigned int reg, idle, val;
	int ret;

	if (sai->version >= SAI_VER_2307) {
		reg = SAI_STATUS;
		idle = SAI_STATUS_FS_IDLE;
		idle = sai->version >= SAI_VER_2311 ? idle >> 1 : idle;
	} else {
		reg = SAI_XFER;
		idle = SAI_XFER_FS_IDLE;
	}

	ret = regmap_read_poll_timeout_atomic(sai->regmap, reg, val,
					      (val & idle), 10, TIMEOUT_US);
	if (ret < 0)
		dev_warn(sai->dev, "Failed to idle FS\n");

	return ret;
}

static int rockchip_sai_poll_stream_idle(struct rk_sai_dev *sai, bool playback, bool capture)
{
	unsigned int reg, val;
	unsigned int idle = 0;
	int ret;

	if (sai->version >= SAI_VER_2307) {
		reg = SAI_STATUS;
		if (playback)
			idle |= SAI_STATUS_TX_IDLE;
		if (capture)
			idle |= SAI_STATUS_RX_IDLE;
		idle = sai->version >= SAI_VER_2311 ? idle >> 1 : idle;
	} else {
		reg = SAI_XFER;
		if (playback)
			idle |= SAI_XFER_TX_IDLE;
		if (capture)
			idle |= SAI_XFER_RX_IDLE;
	}

	ret = regmap_read_poll_timeout_atomic(sai->regmap, reg, val,
					      (val & idle), 10, TIMEOUT_US);
	if (ret < 0)
		dev_warn(sai->dev, "Failed to idle stream\n");

	return ret;
}

/**
 * rockchip_sai_xfer_clk_stop_and_wait() - stop the xfer clock and wait for it to be idle
 * @sai: pointer to the driver instance's rk_sai_dev struct
 * @to_restore: pointer to store the CLK/FSS register values in as they were
 *              found before they were cleared, or NULL.
 *
 * Clear the XFER_CLK and XFER_FSS registers if needed, then busy-waits for the
 * XFER clocks to be idle. Before clearing the bits, it stores the state of the
 * registers as it encountered them in to_restore if it isn't NULL.
 *
 * Context: Any context. Expects sai->xfer_lock to be held by caller.
 */
static void rockchip_sai_xfer_clk_stop_and_wait(struct rk_sai_dev *sai, unsigned int *to_restore)
{
	unsigned int mask = SAI_XFER_CLK_MASK | SAI_XFER_FSS_MASK;
	unsigned int disable = SAI_XFER_CLK_DIS | SAI_XFER_FSS_DIS;
	unsigned int val;

	assert_spin_locked(&sai->xfer_lock);

	regmap_read(sai->regmap, SAI_XFER, &val);
	if ((val & mask) == disable)
		goto wait_for_idle;

	if (sai->is_master_mode)
		regmap_update_bits(sai->regmap, SAI_XFER, mask, disable);

wait_for_idle:
	rockchip_sai_poll_clk_idle(sai);

	if (to_restore)
		*to_restore = val;
}

static int rockchip_sai_runtime_suspend(struct device *dev)
{
	struct rk_sai_dev *sai = dev_get_drvdata(dev);
	unsigned long flags;

	rockchip_sai_fsync_lost_detect(sai, 0);
	rockchip_sai_fsync_err_detect(sai, 0);

	spin_lock_irqsave(&sai->xfer_lock, flags);
	rockchip_sai_xfer_clk_stop_and_wait(sai, NULL);
	spin_unlock_irqrestore(&sai->xfer_lock, flags);

	regcache_cache_only(sai->regmap, true);
	/*
	 * After FS is idle, we should wait at least 2 BCLK cycles to make sure
	 * the CLK gate operation has completed, and only then disable mclk.
	 *
	 * Otherwise, the BCLK is still ungated, and once the mclk is enabled,
	 * there is a risk that a few BCLK cycles leak. This is true especially
	 * at low speeds, such as with a samplerate of 8k.
	 *
	 * Ideally we'd adjust the delay based on the samplerate, but it's such
	 * a tiny value that we can just delay for the maximum clock period
	 * for the sake of simplicity.
	 *
	 * The maximum BCLK period is 31us @ 8K-8Bit (64kHz BCLK). We wait for
	 * 40us to give ourselves a safety margin in case udelay falls short.
	 */
	udelay(40);
	clk_disable_unprepare(sai->mclk);
	clk_disable_unprepare(sai->hclk);

	return 0;
}

static int rockchip_sai_runtime_resume(struct device *dev)
{
	struct rk_sai_dev *sai = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(sai->hclk);
	if (ret)
		goto err_hclk;

	ret = clk_prepare_enable(sai->mclk);
	if (ret)
		goto err_mclk;

	regcache_cache_only(sai->regmap, false);
	regcache_mark_dirty(sai->regmap);
	ret = regcache_sync(sai->regmap);
	if (ret)
		goto err_regmap;

	return 0;

err_regmap:
	clk_disable_unprepare(sai->mclk);
err_mclk:
	clk_disable_unprepare(sai->hclk);
err_hclk:
	return ret;
}

static void rockchip_sai_fifo_xrun_detect(struct rk_sai_dev *sai,
					  int stream, bool en)
{
	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* clear irq status which was asserted before TXUIE enabled */
		regmap_update_bits(sai->regmap, SAI_INTCR,
				   SAI_INTCR_TXUIC, SAI_INTCR_TXUIC);
		regmap_update_bits(sai->regmap, SAI_INTCR,
				   SAI_INTCR_TXUIE_MASK,
				   SAI_INTCR_TXUIE(en));
	} else {
		/* clear irq status which was asserted before RXOIE enabled */
		regmap_update_bits(sai->regmap, SAI_INTCR,
				   SAI_INTCR_RXOIC, SAI_INTCR_RXOIC);
		regmap_update_bits(sai->regmap, SAI_INTCR,
				   SAI_INTCR_RXOIE_MASK,
				   SAI_INTCR_RXOIE(en));
	}
}

static void rockchip_sai_dma_ctrl(struct rk_sai_dev *sai,
				  int stream, bool en)
{
	if (!en)
		rockchip_sai_fifo_xrun_detect(sai, stream, 0);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_update_bits(sai->regmap, SAI_DMACR,
				   SAI_DMACR_TDE_MASK,
				   SAI_DMACR_TDE(en));
	} else {
		regmap_update_bits(sai->regmap, SAI_DMACR,
				   SAI_DMACR_RDE_MASK,
				   SAI_DMACR_RDE(en));
	}

	if (en)
		rockchip_sai_fifo_xrun_detect(sai, stream, 1);
}

static void rockchip_sai_reset(struct rk_sai_dev *sai)
{
	/*
	 * It is advised to reset the hclk domain before resetting the mclk
	 * domain, especially in slave mode without a clock input.
	 *
	 * To deal with the aforementioned case of slave mode without a clock
	 * input, we work around a potential issue by resetting the whole
	 * controller, bringing it back into master mode, and then recovering
	 * the controller configuration in the regmap.
	 */
	reset_control_assert(sai->rst_h);
	udelay(10);
	reset_control_deassert(sai->rst_h);
	udelay(10);
	reset_control_assert(sai->rst_m);
	udelay(10);
	reset_control_deassert(sai->rst_m);
	udelay(10);

	/* recover regmap config */
	regcache_mark_dirty(sai->regmap);
	regcache_sync(sai->regmap);
}

static int rockchip_sai_clear(struct rk_sai_dev *sai, unsigned int clr)
{
	unsigned int val = 0;
	int ret = 0;

	regmap_update_bits(sai->regmap, SAI_CLR, clr, clr);
	ret = regmap_read_poll_timeout_atomic(sai->regmap, SAI_CLR, val,
					      !(val & clr), 10, TIMEOUT_US);
	if (ret < 0) {
		dev_warn(sai->dev, "Failed to clear %u\n", clr);
		rockchip_sai_reset(sai);
	}

	return ret;
}

static void rockchip_sai_xfer_start(struct rk_sai_dev *sai, int stream)
{
	unsigned int msk, val;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		msk = SAI_XFER_TXS_MASK;
		val = SAI_XFER_TXS_EN;

	} else {
		msk = SAI_XFER_RXS_MASK;
		val = SAI_XFER_RXS_EN;
	}

	regmap_update_bits(sai->regmap, SAI_XFER, msk, val);
}

static void rockchip_sai_xfer_stop(struct rk_sai_dev *sai, int stream)
{
	unsigned int msk = 0, val = 0, clr = 0;
	bool playback;
	bool capture;

	if (stream < 0) {
		playback = true;
		capture = true;
	} else if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		playback = true;
		capture = false;
	} else {
		playback = true;
		capture = false;
	}

	if (playback) {
		msk |= SAI_XFER_TXS_MASK;
		val |= SAI_XFER_TXS_DIS;
		clr |= SAI_CLR_TXC;
	}
	if (capture) {
		msk |= SAI_XFER_RXS_MASK;
		val |= SAI_XFER_RXS_DIS;
		clr |= SAI_CLR_RXC;
	}

	regmap_update_bits(sai->regmap, SAI_XFER, msk, val);
	rockchip_sai_poll_stream_idle(sai, playback, capture);

	rockchip_sai_clear(sai, clr);
}

static void rockchip_sai_start(struct rk_sai_dev *sai, int stream)
{
	rockchip_sai_dma_ctrl(sai, stream, 1);
	rockchip_sai_xfer_start(sai, stream);
}

static void rockchip_sai_stop(struct rk_sai_dev *sai, int stream)
{
	rockchip_sai_dma_ctrl(sai, stream, 0);
	rockchip_sai_xfer_stop(sai, stream);
}

static void rockchip_sai_fmt_create(struct rk_sai_dev *sai, unsigned int fmt)
{
	unsigned int xcr_mask = 0, xcr_val = 0, xsft_mask = 0, xsft_val = 0;
	unsigned int fscr_mask = 0, fscr_val = 0;

	assert_spin_locked(&sai->xfer_lock);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		xcr_mask = SAI_XCR_VDJ_MASK | SAI_XCR_EDGE_SHIFT_MASK;
		xcr_val = SAI_XCR_VDJ_R | SAI_XCR_EDGE_SHIFT_0;
		xsft_mask = SAI_XSHIFT_RIGHT_MASK;
		xsft_val = SAI_XSHIFT_RIGHT(0);
		fscr_mask = SAI_FSCR_EDGE_MASK;
		fscr_val = SAI_FSCR_EDGE_DUAL;
		sai->fpw = FPW_HALF_FRAME_WIDTH;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		xcr_mask = SAI_XCR_VDJ_MASK | SAI_XCR_EDGE_SHIFT_MASK;
		xcr_val = SAI_XCR_VDJ_L | SAI_XCR_EDGE_SHIFT_0;
		xsft_mask = SAI_XSHIFT_RIGHT_MASK;
		xsft_val = SAI_XSHIFT_RIGHT(0);
		fscr_mask = SAI_FSCR_EDGE_MASK;
		fscr_val = SAI_FSCR_EDGE_DUAL;
		sai->fpw = FPW_HALF_FRAME_WIDTH;
		break;
	case SND_SOC_DAIFMT_I2S:
		xcr_mask = SAI_XCR_VDJ_MASK | SAI_XCR_EDGE_SHIFT_MASK;
		xcr_val = SAI_XCR_VDJ_L | SAI_XCR_EDGE_SHIFT_1;
		xsft_mask = SAI_XSHIFT_RIGHT_MASK;
		if (sai->is_tdm)
			xsft_val = SAI_XSHIFT_RIGHT(1);
		else
			xsft_val = SAI_XSHIFT_RIGHT(2);
		fscr_mask = SAI_FSCR_EDGE_MASK;
		fscr_val = SAI_FSCR_EDGE_DUAL;
		sai->fpw = FPW_HALF_FRAME_WIDTH;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		xcr_mask = SAI_XCR_VDJ_MASK | SAI_XCR_EDGE_SHIFT_MASK;
		xcr_val = SAI_XCR_VDJ_L | SAI_XCR_EDGE_SHIFT_0;
		xsft_mask = SAI_XSHIFT_RIGHT_MASK;
		xsft_val = SAI_XSHIFT_RIGHT(2);
		fscr_mask = SAI_FSCR_EDGE_MASK;
		fscr_val = SAI_FSCR_EDGE_RISING;
		sai->fpw = FPW_ONE_BCLK_WIDTH;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		xcr_mask = SAI_XCR_VDJ_MASK | SAI_XCR_EDGE_SHIFT_MASK;
		xcr_val = SAI_XCR_VDJ_L | SAI_XCR_EDGE_SHIFT_0;
		xsft_mask = SAI_XSHIFT_RIGHT_MASK;
		xsft_val = SAI_XSHIFT_RIGHT(0);
		fscr_mask = SAI_FSCR_EDGE_MASK;
		fscr_val = SAI_FSCR_EDGE_RISING;
		sai->fpw = FPW_ONE_BCLK_WIDTH;
		break;
	default:
		dev_err(sai->dev, "Unsupported fmt %u\n", fmt);
		break;
	}

	regmap_update_bits(sai->regmap, SAI_TXCR, xcr_mask, xcr_val);
	regmap_update_bits(sai->regmap, SAI_RXCR, xcr_mask, xcr_val);
	regmap_update_bits(sai->regmap, SAI_TX_SHIFT, xsft_mask, xsft_val);
	regmap_update_bits(sai->regmap, SAI_RX_SHIFT, xsft_mask, xsft_val);
	regmap_update_bits(sai->regmap, SAI_FSCR, fscr_mask, fscr_val);
}

static int rockchip_sai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct rk_sai_dev *sai = snd_soc_dai_get_drvdata(dai);
	unsigned int mask = 0, val = 0;
	unsigned int clk_gates;
	unsigned long flags;
	int ret = 0;

	pm_runtime_get_sync(dai->dev);

	mask = SAI_CKR_MSS_MASK;
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		val = SAI_CKR_MSS_MASTER;
		sai->is_master_mode = true;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		val = SAI_CKR_MSS_SLAVE;
		sai->is_master_mode = false;
		break;
	default:
		ret = -EINVAL;
		goto err_pm_put;
	}

	spin_lock_irqsave(&sai->xfer_lock, flags);
	rockchip_sai_xfer_clk_stop_and_wait(sai, &clk_gates);
	if (sai->initialized) {
		if (sai->has_capture && sai->has_playback)
			rockchip_sai_xfer_stop(sai, -1);
		else if (sai->has_capture)
			rockchip_sai_xfer_stop(sai, SNDRV_PCM_STREAM_CAPTURE);
		else
			rockchip_sai_xfer_stop(sai, SNDRV_PCM_STREAM_PLAYBACK);
	} else {
		rockchip_sai_clear(sai, 0);
		sai->initialized = true;
	}

	regmap_update_bits(sai->regmap, SAI_CKR, mask, val);

	mask = SAI_CKR_CKP_MASK | SAI_CKR_FSP_MASK;
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		val = SAI_CKR_CKP_NORMAL | SAI_CKR_FSP_NORMAL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		val = SAI_CKR_CKP_NORMAL | SAI_CKR_FSP_INVERTED;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		val = SAI_CKR_CKP_INVERTED | SAI_CKR_FSP_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		val = SAI_CKR_CKP_INVERTED | SAI_CKR_FSP_INVERTED;
		break;
	default:
		ret = -EINVAL;
		goto err_xfer_unlock;
	}

	regmap_update_bits(sai->regmap, SAI_CKR, mask, val);

	rockchip_sai_fmt_create(sai, fmt);

err_xfer_unlock:
	if (clk_gates)
		regmap_update_bits(sai->regmap, SAI_XFER,
				SAI_XFER_CLK_MASK | SAI_XFER_FSS_MASK,
				clk_gates);
	spin_unlock_irqrestore(&sai->xfer_lock, flags);
err_pm_put:
	pm_runtime_put(dai->dev);

	return ret;
}

static int rockchip_sai_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct rk_sai_dev *sai = snd_soc_dai_get_drvdata(dai);
	struct snd_dmaengine_dai_dma_data *dma_data;
	unsigned int mclk_rate, mclk_req_rate, bclk_rate, div_bclk;
	unsigned int ch_per_lane, slot_width;
	unsigned int val, fscr, reg;
	unsigned int lanes, req_lanes;
	unsigned long flags;
	int ret = 0;

	if (!rockchip_sai_stream_valid(substream, dai))
		return 0;

	dma_data = snd_soc_dai_get_dma_data(dai, substream);
	dma_data->maxburst = MAXBURST_PER_FIFO * params_channels(params) / 2;

	pm_runtime_get_sync(sai->dev);

	regmap_read(sai->regmap, SAI_DMACR, &val);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		reg = SAI_TXCR;
		lanes = sai->tx_lanes;
	} else {
		reg = SAI_RXCR;
		lanes = sai->rx_lanes;
	}

	if (!sai->is_tdm) {
		req_lanes = DIV_ROUND_UP(params_channels(params), 2);
		if (lanes < req_lanes) {
			dev_err(sai->dev, "not enough lanes (%d) for requested number of %s channels (%d)\n",
				lanes, reg == SAI_TXCR ? "playback" : "capture",
				params_channels(params));
			ret = -EINVAL;
			goto err_pm_put;
		} else {
			lanes = req_lanes;
		}
	}

	dev_dbg(sai->dev, "using %d lanes totalling %d%s channels for %s\n",
		lanes, params_channels(params), sai->is_tdm ? " (TDM)" : "",
		reg == SAI_TXCR ? "playback" : "capture");

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
	case SNDRV_PCM_FORMAT_U8:
		val = SAI_XCR_VDW(8);
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		val = SAI_XCR_VDW(16);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val = SAI_XCR_VDW(24);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE:
		val = SAI_XCR_VDW(32);
		break;
	default:
		ret = -EINVAL;
		goto err_pm_put;
	}

	val |= SAI_XCR_CSR(lanes);

	spin_lock_irqsave(&sai->xfer_lock, flags);

	regmap_update_bits(sai->regmap, reg, SAI_XCR_VDW_MASK | SAI_XCR_CSR_MASK, val);

	regmap_read(sai->regmap, reg, &val);

	slot_width = SAI_XCR_SBW_V(val);
	ch_per_lane = params_channels(params) / lanes;

	regmap_update_bits(sai->regmap, reg, SAI_XCR_SNB_MASK,
			   SAI_XCR_SNB(ch_per_lane));

	fscr = SAI_FSCR_FW(sai->fw_ratio * slot_width * ch_per_lane);

	switch (sai->fpw) {
	case FPW_ONE_BCLK_WIDTH:
		fscr |= SAI_FSCR_FPW(1);
		break;
	case FPW_ONE_SLOT_WIDTH:
		fscr |= SAI_FSCR_FPW(slot_width);
		break;
	case FPW_HALF_FRAME_WIDTH:
		fscr |= SAI_FSCR_FPW(sai->fw_ratio * slot_width * ch_per_lane / 2);
		break;
	default:
		dev_err(sai->dev, "Invalid Frame Pulse Width %d\n", sai->fpw);
		ret = -EINVAL;
		goto err_xfer_unlock;
	}

	regmap_update_bits(sai->regmap, SAI_FSCR,
			   SAI_FSCR_FW_MASK | SAI_FSCR_FPW_MASK, fscr);

	if (sai->is_master_mode) {
		bclk_rate = sai->fw_ratio * slot_width * ch_per_lane * params_rate(params);
		ret = clk_set_rate(sai->mclk, sai->mclk_rate);
		if (ret) {
			dev_err(sai->dev, "Failed to set mclk to %u: %pe\n",
				sai->mclk_rate, ERR_PTR(ret));
			goto err_xfer_unlock;
		}

		mclk_rate = clk_get_rate(sai->mclk);
		if (mclk_rate < bclk_rate) {
			dev_err(sai->dev, "Mismatch mclk: %u, at least %u\n",
				mclk_rate, bclk_rate);
			ret = -EINVAL;
			goto err_xfer_unlock;
		}

		div_bclk = DIV_ROUND_CLOSEST(mclk_rate, bclk_rate);
		mclk_req_rate = bclk_rate * div_bclk;

		if (mclk_rate < mclk_req_rate - CLK_SHIFT_RATE_HZ_MAX ||
		    mclk_rate > mclk_req_rate + CLK_SHIFT_RATE_HZ_MAX) {
			dev_err(sai->dev, "Mismatch mclk: %u, expected %u (+/- %dHz)\n",
				mclk_rate, mclk_req_rate, CLK_SHIFT_RATE_HZ_MAX);
			ret = -EINVAL;
			goto err_xfer_unlock;
		}

		regmap_update_bits(sai->regmap, SAI_CKR, SAI_CKR_MDIV_MASK,
				   SAI_CKR_MDIV(div_bclk));
	}

err_xfer_unlock:
	spin_unlock_irqrestore(&sai->xfer_lock, flags);
err_pm_put:
	pm_runtime_put(sai->dev);

	return ret;
}

static int rockchip_sai_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct rk_sai_dev *sai = snd_soc_dai_get_drvdata(dai);
	unsigned long flags;

	if (!rockchip_sai_stream_valid(substream, dai))
		return 0;

	if (sai->is_master_mode) {
		/*
		 * We should wait for the first BCLK pulse to have definitely
		 * occurred after any DIV settings have potentially been
		 * changed in order to guarantee a clean clock signal once we
		 * ungate the clock.
		 *
		 * Ideally, this would be done depending on the samplerate, but
		 * for the sake of simplicity, we'll just delay for the maximum
		 * possible clock offset time, which is quite a small value.
		 *
		 * The maximum BCLK offset is 15.6us @ 8K-8Bit (64kHz BCLK). We
		 * wait for 20us in order to give us a safety margin in case
		 * udelay falls short.
		 */
		udelay(20);
		spin_lock_irqsave(&sai->xfer_lock, flags);
		regmap_update_bits(sai->regmap, SAI_XFER,
				   SAI_XFER_CLK_MASK |
				   SAI_XFER_FSS_MASK,
				   SAI_XFER_CLK_EN |
				   SAI_XFER_FSS_EN);
		spin_unlock_irqrestore(&sai->xfer_lock, flags);
	}

	rockchip_sai_fsync_lost_detect(sai, 1);
	rockchip_sai_fsync_err_detect(sai, 1);

	return 0;
}

static void rockchip_sai_path_config(struct rk_sai_dev *sai,
				     int num, bool is_rx)
{
	int i;

	if (is_rx)
		for (i = 0; i < num; i++)
			regmap_update_bits(sai->regmap, SAI_PATH_SEL,
					   SAI_RX_PATH_MASK(i),
					   SAI_RX_PATH(i, sai->sdi[i]));
	else
		for (i = 0; i < num; i++)
			regmap_update_bits(sai->regmap, SAI_PATH_SEL,
					   SAI_TX_PATH_MASK(i),
					   SAI_TX_PATH(i, sai->sdo[i]));
}

static int rockchip_sai_path_prepare(struct rk_sai_dev *sai,
				     struct device_node *np,
				     bool is_rx)
{
	const char *path_prop;
	unsigned int *data;
	unsigned int *lanes;
	int i, num, ret;

	if (is_rx) {
		path_prop = "rockchip,sai-rx-route";
		data = sai->sdi;
		lanes = &sai->rx_lanes;
	} else {
		path_prop = "rockchip,sai-tx-route";
		data = sai->sdo;
		lanes = &sai->tx_lanes;
	}

	num = of_count_phandle_with_args(np, path_prop, NULL);
	if (num == -ENOENT) {
		return 0;
	} else if (num > MAX_LANES || num == 0) {
		dev_err(sai->dev, "found %d entries in %s, outside of range 1 to %d\n",
			num, path_prop, MAX_LANES);
		return -EINVAL;
	} else if (num < 0) {
		dev_err(sai->dev, "error in %s property: %pe\n", path_prop,
			ERR_PTR(num));
		return num;
	}

	*lanes = num;
	ret = device_property_read_u32_array(sai->dev, path_prop, data, num);
	if (ret < 0) {
		dev_err(sai->dev, "failed to read property '%s': %pe\n",
			path_prop, ERR_PTR(ret));
		return ret;
	}

	for (i = 0; i < num; i++) {
		if (data[i] >= MAX_LANES) {
			dev_err(sai->dev, "%s[%d] is %d, should be less than %d\n",
				path_prop, i, data[i], MAX_LANES);
			return -EINVAL;
		}
	}

	rockchip_sai_path_config(sai, num, is_rx);

	return 0;
}

static int rockchip_sai_parse_paths(struct rk_sai_dev *sai,
				    struct device_node *np)
{
	int ret;

	if (sai->has_playback) {
		sai->tx_lanes = 1;
		ret = rockchip_sai_path_prepare(sai, np, false);
		if (ret < 0) {
			dev_err(sai->dev, "Failed to prepare TX path: %pe\n",
				ERR_PTR(ret));
			return ret;
		}
	}

	if (sai->has_capture) {
		sai->rx_lanes = 1;
		ret = rockchip_sai_path_prepare(sai, np, true);
		if (ret < 0) {
			dev_err(sai->dev, "Failed to prepare RX path: %pe\n",
				ERR_PTR(ret));
			return ret;
		}
	}

	return 0;
}

static int rockchip_sai_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct rk_sai_dev *sai = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	if (!rockchip_sai_stream_valid(substream, dai))
		return 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		rockchip_sai_start(sai, substream->stream);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		rockchip_sai_stop(sai, substream->stream);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}


static int rockchip_sai_dai_probe(struct snd_soc_dai *dai)
{
	struct rk_sai_dev *sai = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai,
		sai->has_playback ? &sai->playback_dma_data : NULL,
		sai->has_capture  ? &sai->capture_dma_data  : NULL);

	return 0;
}

static int rockchip_sai_startup(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	struct rk_sai_dev *sai = snd_soc_dai_get_drvdata(dai);
	int stream = substream->stream;

	if (!rockchip_sai_stream_valid(substream, dai))
		return 0;

	if (sai->substreams[stream])
		return -EBUSY;

	if (sai->wait_time[stream])
		substream->wait_time = sai->wait_time[stream];

	sai->substreams[stream] = substream;

	return 0;
}

static void rockchip_sai_shutdown(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	struct rk_sai_dev *sai = snd_soc_dai_get_drvdata(dai);

	if (!rockchip_sai_stream_valid(substream, dai))
		return;

	sai->substreams[substream->stream] = NULL;
}

static int rockchip_sai_set_tdm_slot(struct snd_soc_dai *dai,
				     unsigned int tx_mask, unsigned int rx_mask,
				     int slots, int slot_width)
{
	struct rk_sai_dev *sai = snd_soc_dai_get_drvdata(dai);
	unsigned long flags;
	unsigned int clk_gates;
	int sw = slot_width;

	if (!slots) {
		/* Disabling TDM, set slot width back to 32 bits */
		sai->is_tdm = false;
		sw = 32;
	} else {
		sai->is_tdm = true;
	}

	if (sw < 16 || sw > 32)
		return -EINVAL;

	pm_runtime_get_sync(dai->dev);
	spin_lock_irqsave(&sai->xfer_lock, flags);
	rockchip_sai_xfer_clk_stop_and_wait(sai, &clk_gates);
	regmap_update_bits(sai->regmap, SAI_TXCR, SAI_XCR_SBW_MASK,
			   SAI_XCR_SBW(sw));
	regmap_update_bits(sai->regmap, SAI_RXCR, SAI_XCR_SBW_MASK,
			   SAI_XCR_SBW(sw));
	regmap_update_bits(sai->regmap, SAI_XFER,
			   SAI_XFER_CLK_MASK | SAI_XFER_FSS_MASK,
			   clk_gates);
	spin_unlock_irqrestore(&sai->xfer_lock, flags);
	pm_runtime_put(dai->dev);

	return 0;
}

static int rockchip_sai_set_sysclk(struct snd_soc_dai *dai, int stream,
				   unsigned int freq, int dir)
{
	struct rk_sai_dev *sai = snd_soc_dai_get_drvdata(dai);

	sai->mclk_rate = freq;

	return 0;
}

static const struct snd_soc_dai_ops rockchip_sai_dai_ops = {
	.probe = rockchip_sai_dai_probe,
	.startup = rockchip_sai_startup,
	.shutdown = rockchip_sai_shutdown,
	.hw_params = rockchip_sai_hw_params,
	.set_fmt = rockchip_sai_set_fmt,
	.set_sysclk = rockchip_sai_set_sysclk,
	.prepare = rockchip_sai_prepare,
	.trigger = rockchip_sai_trigger,
	.set_tdm_slot = rockchip_sai_set_tdm_slot,
};

static const struct snd_soc_dai_driver rockchip_sai_dai = {
	.ops = &rockchip_sai_dai_ops,
	.symmetric_rate = 1,
};

static bool rockchip_sai_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SAI_TXCR:
	case SAI_FSCR:
	case SAI_RXCR:
	case SAI_MONO_CR:
	case SAI_XFER:
	case SAI_CLR:
	case SAI_CKR:
	case SAI_DMACR:
	case SAI_INTCR:
	case SAI_TXDR:
	case SAI_PATH_SEL:
	case SAI_TX_SLOT_MASK0:
	case SAI_TX_SLOT_MASK1:
	case SAI_TX_SLOT_MASK2:
	case SAI_TX_SLOT_MASK3:
	case SAI_RX_SLOT_MASK0:
	case SAI_RX_SLOT_MASK1:
	case SAI_RX_SLOT_MASK2:
	case SAI_RX_SLOT_MASK3:
	case SAI_TX_SHIFT:
	case SAI_RX_SHIFT:
	case SAI_FSXN:
	case SAI_FS_TIMEOUT:
	case SAI_LOOPBACK_LR:
		return true;
	default:
		return false;
	}
}

static bool rockchip_sai_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SAI_TXCR:
	case SAI_FSCR:
	case SAI_RXCR:
	case SAI_MONO_CR:
	case SAI_XFER:
	case SAI_CLR:
	case SAI_CKR:
	case SAI_TXFIFOLR:
	case SAI_RXFIFOLR:
	case SAI_DMACR:
	case SAI_INTCR:
	case SAI_INTSR:
	case SAI_TXDR:
	case SAI_RXDR:
	case SAI_PATH_SEL:
	case SAI_TX_SLOT_MASK0:
	case SAI_TX_SLOT_MASK1:
	case SAI_TX_SLOT_MASK2:
	case SAI_TX_SLOT_MASK3:
	case SAI_RX_SLOT_MASK0:
	case SAI_RX_SLOT_MASK1:
	case SAI_RX_SLOT_MASK2:
	case SAI_RX_SLOT_MASK3:
	case SAI_TX_DATA_CNT:
	case SAI_RX_DATA_CNT:
	case SAI_TX_SHIFT:
	case SAI_RX_SHIFT:
	case SAI_STATUS:
	case SAI_VERSION:
	case SAI_FSXN:
	case SAI_FS_TIMEOUT:
	case SAI_LOOPBACK_LR:
		return true;
	default:
		return false;
	}
}

static bool rockchip_sai_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SAI_XFER:
	case SAI_INTCR:
	case SAI_INTSR:
	case SAI_CLR:
	case SAI_TXFIFOLR:
	case SAI_RXFIFOLR:
	case SAI_TXDR:
	case SAI_RXDR:
	case SAI_TX_DATA_CNT:
	case SAI_RX_DATA_CNT:
	case SAI_STATUS:
	case SAI_VERSION:
		return true;
	default:
		return false;
	}
}

static bool rockchip_sai_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SAI_RXDR:
		return true;
	default:
		return false;
	}
}

static const struct reg_default rockchip_sai_reg_defaults[] = {
	{ SAI_TXCR, 0x00000bff },
	{ SAI_FSCR, 0x0001f03f },
	{ SAI_RXCR, 0x00000bff },
	{ SAI_PATH_SEL, 0x0000e4e4 },
};

static const struct regmap_config rockchip_sai_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = SAI_LOOPBACK_LR,
	.reg_defaults = rockchip_sai_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rockchip_sai_reg_defaults),
	.writeable_reg = rockchip_sai_wr_reg,
	.readable_reg = rockchip_sai_rd_reg,
	.volatile_reg = rockchip_sai_volatile_reg,
	.precious_reg = rockchip_sai_precious_reg,
	.cache_type = REGCACHE_FLAT,
};

static int rockchip_sai_init_dai(struct rk_sai_dev *sai, struct resource *res,
				 struct snd_soc_dai_driver **dp)
{
	struct device_node *node = sai->dev->of_node;
	struct snd_soc_dai_driver *dai;
	struct property *dma_names;
	const char *dma_name;

	of_property_for_each_string(node, "dma-names", dma_names, dma_name) {
		if (!strcmp(dma_name, "tx"))
			sai->has_playback = true;
		if (!strcmp(dma_name, "rx"))
			sai->has_capture = true;
	}

	dai = devm_kmemdup(sai->dev, &rockchip_sai_dai,
			   sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	if (sai->has_playback) {
		dai->playback.stream_name = "Playback";
		dai->playback.channels_min = 1;
		dai->playback.channels_max = 512;
		dai->playback.rates = SNDRV_PCM_RATE_8000_384000;
		dai->playback.formats = SNDRV_PCM_FMTBIT_S8 |
					SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_S24_LE |
					SNDRV_PCM_FMTBIT_S32_LE |
					SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE;

		sai->playback_dma_data.addr = res->start + SAI_TXDR;
		sai->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		sai->playback_dma_data.maxburst = MAXBURST_PER_FIFO;
	}

	if (sai->has_capture) {
		dai->capture.stream_name = "Capture";
		dai->capture.channels_min = 1;
		dai->capture.channels_max = 512;
		dai->capture.rates = SNDRV_PCM_RATE_8000_384000;
		dai->capture.formats = SNDRV_PCM_FMTBIT_S8 |
				       SNDRV_PCM_FMTBIT_S16_LE |
				       SNDRV_PCM_FMTBIT_S24_LE |
				       SNDRV_PCM_FMTBIT_S32_LE |
				       SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE;

		sai->capture_dma_data.addr = res->start + SAI_RXDR;
		sai->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		sai->capture_dma_data.maxburst = MAXBURST_PER_FIFO;
	}

	regmap_update_bits(sai->regmap, SAI_DMACR, SAI_DMACR_TDL_MASK,
			   SAI_DMACR_TDL(16));
	regmap_update_bits(sai->regmap, SAI_DMACR, SAI_DMACR_RDL_MASK,
			   SAI_DMACR_RDL(16));

	if (dp)
		*dp = dai;

	return 0;
}

static const char * const mono_text[] = { "Disable", "Enable" };

static DECLARE_TLV_DB_SCALE(rmss_tlv, 0, 128, 0);

static const char * const lplrc_text[] = { "L:MIC R:LP", "L:LP R:MIC" };
static const char * const lplr_text[] = { "Disable", "Enable" };

static const char * const lpx_text[] = {
	"From SDO0", "From SDO1", "From SDO2", "From SDO3" };

static const char * const lps_text[] = { "Disable", "Enable" };
static const char * const sync_out_text[] = { "From CRU", "From IO" };
static const char * const sync_in_text[] = { "From IO", "From Sync Port" };

static const char * const rpaths_text[] = {
	"From SDI0", "From SDI1", "From SDI2", "From SDI3" };

static const char * const tpaths_text[] = {
	"From PATH0", "From PATH1", "From PATH2", "From PATH3" };

/* MONO_CR */
static SOC_ENUM_SINGLE_DECL(rmono_switch, SAI_MONO_CR, 1, mono_text);
static SOC_ENUM_SINGLE_DECL(tmono_switch, SAI_MONO_CR, 0, mono_text);

/* PATH_SEL */
static SOC_ENUM_SINGLE_DECL(lp3_enum, SAI_PATH_SEL, 28, lpx_text);
static SOC_ENUM_SINGLE_DECL(lp2_enum, SAI_PATH_SEL, 26, lpx_text);
static SOC_ENUM_SINGLE_DECL(lp1_enum, SAI_PATH_SEL, 24, lpx_text);
static SOC_ENUM_SINGLE_DECL(lp0_enum, SAI_PATH_SEL, 22, lpx_text);
static SOC_ENUM_SINGLE_DECL(lp3_switch, SAI_PATH_SEL, 21, lps_text);
static SOC_ENUM_SINGLE_DECL(lp2_switch, SAI_PATH_SEL, 20, lps_text);
static SOC_ENUM_SINGLE_DECL(lp1_switch, SAI_PATH_SEL, 19, lps_text);
static SOC_ENUM_SINGLE_DECL(lp0_switch, SAI_PATH_SEL, 18, lps_text);
static SOC_ENUM_SINGLE_DECL(sync_out_switch, SAI_PATH_SEL, 17, sync_out_text);
static SOC_ENUM_SINGLE_DECL(sync_in_switch, SAI_PATH_SEL, 16, sync_in_text);
static SOC_ENUM_SINGLE_DECL(rpath3_enum, SAI_PATH_SEL, 14, rpaths_text);
static SOC_ENUM_SINGLE_DECL(rpath2_enum, SAI_PATH_SEL, 12, rpaths_text);
static SOC_ENUM_SINGLE_DECL(rpath1_enum, SAI_PATH_SEL, 10, rpaths_text);
static SOC_ENUM_SINGLE_DECL(rpath0_enum, SAI_PATH_SEL, 8, rpaths_text);
static SOC_ENUM_SINGLE_DECL(tpath3_enum, SAI_PATH_SEL, 6, tpaths_text);
static SOC_ENUM_SINGLE_DECL(tpath2_enum, SAI_PATH_SEL, 4, tpaths_text);
static SOC_ENUM_SINGLE_DECL(tpath1_enum, SAI_PATH_SEL, 2, tpaths_text);
static SOC_ENUM_SINGLE_DECL(tpath0_enum, SAI_PATH_SEL, 0, tpaths_text);

/* LOOPBACK_LR */
static SOC_ENUM_SINGLE_DECL(lp3lrc_enum, SAI_LOOPBACK_LR, 7, lplrc_text);
static SOC_ENUM_SINGLE_DECL(lp2lrc_enum, SAI_LOOPBACK_LR, 6, lplrc_text);
static SOC_ENUM_SINGLE_DECL(lp1lrc_enum, SAI_LOOPBACK_LR, 5, lplrc_text);
static SOC_ENUM_SINGLE_DECL(lp0lrc_enum, SAI_LOOPBACK_LR, 4, lplrc_text);
static SOC_ENUM_SINGLE_DECL(lp3lr_switch, SAI_LOOPBACK_LR, 3, lplr_text);
static SOC_ENUM_SINGLE_DECL(lp2lr_switch, SAI_LOOPBACK_LR, 2, lplr_text);
static SOC_ENUM_SINGLE_DECL(lp1lr_switch, SAI_LOOPBACK_LR, 1, lplr_text);
static SOC_ENUM_SINGLE_DECL(lp0lr_switch, SAI_LOOPBACK_LR, 0, lplr_text);

static int rockchip_sai_wait_time_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = WAIT_TIME_MS_MAX;
	uinfo->value.integer.step = 1;

	return 0;
}

static int rockchip_sai_rd_wait_time_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rk_sai_dev *sai = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = sai->wait_time[SNDRV_PCM_STREAM_CAPTURE];

	return 0;
}

static int rockchip_sai_rd_wait_time_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rk_sai_dev *sai = snd_soc_component_get_drvdata(component);

	if (ucontrol->value.integer.value[0] > WAIT_TIME_MS_MAX)
		return -EINVAL;

	sai->wait_time[SNDRV_PCM_STREAM_CAPTURE] = ucontrol->value.integer.value[0];

	return 1;
}

static int rockchip_sai_wr_wait_time_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rk_sai_dev *sai = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = sai->wait_time[SNDRV_PCM_STREAM_PLAYBACK];

	return 0;
}

static int rockchip_sai_wr_wait_time_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rk_sai_dev *sai = snd_soc_component_get_drvdata(component);

	if (ucontrol->value.integer.value[0] > WAIT_TIME_MS_MAX)
		return -EINVAL;

	sai->wait_time[SNDRV_PCM_STREAM_PLAYBACK] = ucontrol->value.integer.value[0];

	return 1;
}

#define SAI_PCM_WAIT_TIME(xname, xhandler_get, xhandler_put)	\
{	.iface = SNDRV_CTL_ELEM_IFACE_PCM, .name = xname,	\
	.info = rockchip_sai_wait_time_info,			\
	.get = xhandler_get, .put = xhandler_put }

static const struct snd_kcontrol_new rockchip_sai_controls[] = {
	SOC_SINGLE_TLV("Receive Mono Slot Select", SAI_MONO_CR,
		       2, 128, 0, rmss_tlv),
	SOC_ENUM("Receive Mono Switch", rmono_switch),
	SOC_ENUM("Transmit Mono Switch", tmono_switch),

	SOC_ENUM("SDI3 Loopback I2S LR Channel Sel", lp3lrc_enum),
	SOC_ENUM("SDI2 Loopback I2S LR Channel Sel", lp2lrc_enum),
	SOC_ENUM("SDI1 Loopback I2S LR Channel Sel", lp1lrc_enum),
	SOC_ENUM("SDI0 Loopback I2S LR Channel Sel", lp0lrc_enum),
	SOC_ENUM("SDI3 Loopback I2S LR Switch", lp3lr_switch),
	SOC_ENUM("SDI2 Loopback I2S LR Switch", lp2lr_switch),
	SOC_ENUM("SDI1 Loopback I2S LR Switch", lp1lr_switch),
	SOC_ENUM("SDI0 Loopback I2S LR Switch", lp0lr_switch),

	SOC_ENUM("SDI3 Loopback Src Select", lp3_enum),
	SOC_ENUM("SDI2 Loopback Src Select", lp2_enum),
	SOC_ENUM("SDI1 Loopback Src Select", lp1_enum),
	SOC_ENUM("SDI0 Loopback Src Select", lp0_enum),
	SOC_ENUM("SDI3 Loopback Switch", lp3_switch),
	SOC_ENUM("SDI2 Loopback Switch", lp2_switch),
	SOC_ENUM("SDI1 Loopback Switch", lp1_switch),
	SOC_ENUM("SDI0 Loopback Switch", lp0_switch),
	SOC_ENUM("Sync Out Switch", sync_out_switch),
	SOC_ENUM("Sync In Switch", sync_in_switch),
	SOC_ENUM("Receive PATH3 Source Select", rpath3_enum),
	SOC_ENUM("Receive PATH2 Source Select", rpath2_enum),
	SOC_ENUM("Receive PATH1 Source Select", rpath1_enum),
	SOC_ENUM("Receive PATH0 Source Select", rpath0_enum),
	SOC_ENUM("Transmit SDO3 Source Select", tpath3_enum),
	SOC_ENUM("Transmit SDO2 Source Select", tpath2_enum),
	SOC_ENUM("Transmit SDO1 Source Select", tpath1_enum),
	SOC_ENUM("Transmit SDO0 Source Select", tpath0_enum),

	SAI_PCM_WAIT_TIME("PCM Read Wait Time MS",
			  rockchip_sai_rd_wait_time_get,
			  rockchip_sai_rd_wait_time_put),
	SAI_PCM_WAIT_TIME("PCM Write Wait Time MS",
			  rockchip_sai_wr_wait_time_get,
			  rockchip_sai_wr_wait_time_put),
};

static const struct snd_soc_component_driver rockchip_sai_component = {
	.name = DRV_NAME,
	.controls = rockchip_sai_controls,
	.num_controls = ARRAY_SIZE(rockchip_sai_controls),
	.legacy_dai_naming = 1,
};

static irqreturn_t rockchip_sai_isr(int irq, void *devid)
{
	struct rk_sai_dev *sai = (struct rk_sai_dev *)devid;
	struct snd_pcm_substream *substream;
	u32 val;

	regmap_read(sai->regmap, SAI_INTSR, &val);
	if (val & SAI_INTSR_TXUI_ACT) {
		dev_warn_ratelimited(sai->dev, "TX FIFO Underrun\n");
		regmap_update_bits(sai->regmap, SAI_INTCR,
				   SAI_INTCR_TXUIC, SAI_INTCR_TXUIC);
		regmap_update_bits(sai->regmap, SAI_INTCR,
				   SAI_INTCR_TXUIE_MASK,
				   SAI_INTCR_TXUIE(0));
		substream = sai->substreams[SNDRV_PCM_STREAM_PLAYBACK];
		if (substream)
			snd_pcm_stop_xrun(substream);
	}

	if (val & SAI_INTSR_RXOI_ACT) {
		dev_warn_ratelimited(sai->dev, "RX FIFO Overrun\n");
		regmap_update_bits(sai->regmap, SAI_INTCR,
				   SAI_INTCR_RXOIC, SAI_INTCR_RXOIC);
		regmap_update_bits(sai->regmap, SAI_INTCR,
				   SAI_INTCR_RXOIE_MASK,
				   SAI_INTCR_RXOIE(0));
		substream = sai->substreams[SNDRV_PCM_STREAM_CAPTURE];
		if (substream)
			snd_pcm_stop_xrun(substream);
	}

	if (val & SAI_INTSR_FSERRI_ACT) {
		dev_warn_ratelimited(sai->dev, "Frame Sync Error\n");
		regmap_update_bits(sai->regmap, SAI_INTCR,
				   SAI_INTCR_FSERRC, SAI_INTCR_FSERRC);
		regmap_update_bits(sai->regmap, SAI_INTCR,
				   SAI_INTCR_FSERR_MASK,
				   SAI_INTCR_FSERR(0));
	}

	if (val & SAI_INTSR_FSLOSTI_ACT) {
		dev_warn_ratelimited(sai->dev, "Frame Sync Lost\n");
		regmap_update_bits(sai->regmap, SAI_INTCR,
				   SAI_INTCR_FSLOSTC, SAI_INTCR_FSLOSTC);
		regmap_update_bits(sai->regmap, SAI_INTCR,
				   SAI_INTCR_FSLOST_MASK,
				   SAI_INTCR_FSLOST(0));
	}

	return IRQ_HANDLED;
}

static int rockchip_sai_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct rk_sai_dev *sai;
	struct snd_soc_dai_driver *dai;
	struct resource *res;
	void __iomem *regs;
	int ret, irq;

	sai = devm_kzalloc(&pdev->dev, sizeof(*sai), GFP_KERNEL);
	if (!sai)
		return -ENOMEM;

	sai->dev = &pdev->dev;
	sai->fw_ratio = 1;
	/* match to register default */
	sai->is_master_mode = true;
	dev_set_drvdata(&pdev->dev, sai);

	spin_lock_init(&sai->xfer_lock);

	sai->rst_h = devm_reset_control_get_optional_exclusive(&pdev->dev, "h");
	if (IS_ERR(sai->rst_h))
		return dev_err_probe(&pdev->dev, PTR_ERR(sai->rst_h),
				     "Error in 'h' reset control\n");

	sai->rst_m = devm_reset_control_get_optional_exclusive(&pdev->dev, "m");
	if (IS_ERR(sai->rst_m))
		return dev_err_probe(&pdev->dev, PTR_ERR(sai->rst_m),
				     "Error in 'm' reset control\n");

	regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(regs))
		return dev_err_probe(&pdev->dev, PTR_ERR(regs),
				     "Failed to get and ioremap resource\n");

	sai->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					    &rockchip_sai_regmap_config);
	if (IS_ERR(sai->regmap))
		return dev_err_probe(&pdev->dev, PTR_ERR(sai->regmap),
				     "Failed to initialize regmap\n");

	irq = platform_get_irq_optional(pdev, 0);
	if (irq > 0) {
		ret = devm_request_irq(&pdev->dev, irq, rockchip_sai_isr,
				       IRQF_SHARED, node->name, sai);
		if (ret) {
			return dev_err_probe(&pdev->dev, ret,
					     "Failed to request irq %d\n", irq);
		}
	} else {
		dev_dbg(&pdev->dev, "Asked for an IRQ but got %d\n", irq);
	}

	sai->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(sai->mclk)) {
		return dev_err_probe(&pdev->dev, PTR_ERR(sai->mclk),
				     "Failed to get mclk\n");
	}

	sai->hclk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(sai->hclk)) {
		return dev_err_probe(&pdev->dev, PTR_ERR(sai->hclk),
				     "Failed to get hclk\n");
	}

	ret = clk_prepare_enable(sai->hclk);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to enable hclk\n");

	regmap_read(sai->regmap, SAI_VERSION, &sai->version);

	ret = rockchip_sai_init_dai(sai, res, &dai);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize DAI: %d\n", ret);
		goto err_disable_hclk;
	}

	ret = rockchip_sai_parse_paths(sai, node);
	if (ret) {
		dev_err(&pdev->dev, "Failed to parse paths: %d\n", ret);
		goto err_disable_hclk;
	}

	/*
	 * From here on, all register accesses need to be wrapped in
	 * pm_runtime_get_sync/pm_runtime_put calls
	 *
	 * NB: we don't rely on _resume_and_get in case of !CONFIG_PM
	 */
	devm_pm_runtime_enable(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);
	ret = rockchip_sai_runtime_resume(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to resume device: %pe\n", ERR_PTR(ret));
		goto err_disable_hclk;
	}

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register PCM: %d\n", ret);
		goto err_runtime_suspend;
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &rockchip_sai_component,
					      dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register component: %d\n", ret);
		goto err_runtime_suspend;
	}

	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_put(&pdev->dev);

	clk_disable_unprepare(sai->hclk);

	return 0;

err_runtime_suspend:
	/* If we're !CONFIG_PM, we get -ENOSYS and disable manually */
	if (pm_runtime_put(&pdev->dev))
		rockchip_sai_runtime_suspend(&pdev->dev);
err_disable_hclk:
	clk_disable_unprepare(sai->hclk);

	return ret;
}

static void rockchip_sai_remove(struct platform_device *pdev)
{
#ifndef CONFIG_PM
	rockchip_sai_runtime_suspend(&pdev->dev);
#endif
}

static const struct dev_pm_ops rockchip_sai_pm_ops = {
	SET_RUNTIME_PM_OPS(rockchip_sai_runtime_suspend, rockchip_sai_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
};

static const struct of_device_id rockchip_sai_match[] = {
	{ .compatible = "rockchip,rk3576-sai", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_sai_match);

static struct platform_driver rockchip_sai_driver = {
	.probe = rockchip_sai_probe,
	.remove = rockchip_sai_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = rockchip_sai_match,
		.pm = &rockchip_sai_pm_ops,
	},
};
module_platform_driver(rockchip_sai_driver);

MODULE_DESCRIPTION("Rockchip SAI ASoC Interface");
MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_AUTHOR("Nicolas Frattaroli <nicolas.frattaroli@collabora.com>");
MODULE_LICENSE("GPL");
