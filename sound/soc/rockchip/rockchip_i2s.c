// SPDX-License-Identifier: GPL-2.0-only
/* sound/soc/rockchip/rockchip_i2s.c
 *
 * ALSA SoC Audio Layer - Rockchip I2S Controller driver
 *
 * Copyright (c) 2014 Rockchip Electronics Co. Ltd.
 * Author: Jianqun <jay.xu@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>

#include "rockchip_i2s.h"

#define DRV_NAME "rockchip-i2s"

struct rk_i2s_pins {
	u32 reg_offset;
	u32 shift;
};

struct rk_i2s_dev {
	struct device *dev;

	struct clk *hclk;
	struct clk *mclk;

	struct snd_dmaengine_dai_dma_data capture_dma_data;
	struct snd_dmaengine_dai_dma_data playback_dma_data;

	struct regmap *regmap;
	struct regmap *grf;

	bool has_capture;
	bool has_playback;

/*
 * Used to indicate the tx/rx status.
 * I2S controller hopes to start the tx and rx together,
 * also to stop them when they are both try to stop.
*/
	bool tx_start;
	bool rx_start;
	bool is_master_mode;
	const struct rk_i2s_pins *pins;
	unsigned int bclk_ratio;
	spinlock_t lock; /* tx/rx lock */
	struct pinctrl *pinctrl;
	struct pinctrl_state *bclk_on;
	struct pinctrl_state *bclk_off;
};

static int i2s_pinctrl_select_bclk_on(struct rk_i2s_dev *i2s)
{
	int ret = 0;

	if (!IS_ERR(i2s->pinctrl) && !IS_ERR_OR_NULL(i2s->bclk_on))
		ret = pinctrl_select_state(i2s->pinctrl, i2s->bclk_on);

	if (ret)
		dev_err(i2s->dev, "bclk enable failed %d\n", ret);

	return ret;
}

static int i2s_pinctrl_select_bclk_off(struct rk_i2s_dev *i2s)
{

	int ret = 0;

	if (!IS_ERR(i2s->pinctrl) && !IS_ERR_OR_NULL(i2s->bclk_off))
		ret = pinctrl_select_state(i2s->pinctrl, i2s->bclk_off);

	if (ret)
		dev_err(i2s->dev, "bclk disable failed %d\n", ret);

	return ret;
}

static int i2s_runtime_suspend(struct device *dev)
{
	struct rk_i2s_dev *i2s = dev_get_drvdata(dev);

	regcache_cache_only(i2s->regmap, true);
	clk_disable_unprepare(i2s->mclk);

	return 0;
}

static int i2s_runtime_resume(struct device *dev)
{
	struct rk_i2s_dev *i2s = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(i2s->mclk);
	if (ret) {
		dev_err(i2s->dev, "clock enable failed %d\n", ret);
		return ret;
	}

	regcache_cache_only(i2s->regmap, false);
	regcache_mark_dirty(i2s->regmap);

	ret = regcache_sync(i2s->regmap);
	if (ret)
		clk_disable_unprepare(i2s->mclk);

	return ret;
}

static inline struct rk_i2s_dev *to_info(struct snd_soc_dai *dai)
{
	return snd_soc_dai_get_drvdata(dai);
}

static int rockchip_snd_txctrl(struct rk_i2s_dev *i2s, int on)
{
	unsigned int val = 0;
	int ret = 0;

	spin_lock(&i2s->lock);
	if (on) {
		ret = regmap_update_bits(i2s->regmap, I2S_DMACR,
					 I2S_DMACR_TDE_ENABLE,
					 I2S_DMACR_TDE_ENABLE);
		if (ret < 0)
			goto end;
		ret = regmap_update_bits(i2s->regmap, I2S_XFER,
					 I2S_XFER_TXS_START | I2S_XFER_RXS_START,
					 I2S_XFER_TXS_START | I2S_XFER_RXS_START);
		if (ret < 0)
			goto end;
		i2s->tx_start = true;
	} else {
		i2s->tx_start = false;

		ret = regmap_update_bits(i2s->regmap, I2S_DMACR,
					 I2S_DMACR_TDE_ENABLE,
					 I2S_DMACR_TDE_DISABLE);
		if (ret < 0)
			goto end;

		if (!i2s->rx_start) {
			ret = regmap_update_bits(i2s->regmap, I2S_XFER,
						 I2S_XFER_TXS_START | I2S_XFER_RXS_START,
						 I2S_XFER_TXS_STOP | I2S_XFER_RXS_STOP);
			if (ret < 0)
				goto end;
			udelay(150);
			ret = regmap_update_bits(i2s->regmap, I2S_CLR,
						 I2S_CLR_TXC | I2S_CLR_RXC,
						 I2S_CLR_TXC | I2S_CLR_RXC);
			if (ret < 0)
				goto end;
			ret = regmap_read_poll_timeout_atomic(i2s->regmap,
							      I2S_CLR,
							      val,
							      val == 0,
							      20,
							      200);
			if (ret < 0)
				dev_warn(i2s->dev, "fail to clear: %d\n", ret);
		}
	}
end:
	spin_unlock(&i2s->lock);
	if (ret < 0)
		dev_err(i2s->dev, "lrclk update failed\n");

	return ret;
}

static int rockchip_snd_rxctrl(struct rk_i2s_dev *i2s, int on)
{
	unsigned int val = 0;
	int ret = 0;

	spin_lock(&i2s->lock);
	if (on) {
		ret = regmap_update_bits(i2s->regmap, I2S_DMACR,
					 I2S_DMACR_RDE_ENABLE,
					 I2S_DMACR_RDE_ENABLE);
		if (ret < 0)
			goto end;

		ret = regmap_update_bits(i2s->regmap, I2S_XFER,
					 I2S_XFER_TXS_START | I2S_XFER_RXS_START,
					 I2S_XFER_TXS_START | I2S_XFER_RXS_START);
		if (ret < 0)
			goto end;
		i2s->rx_start = true;
	} else {
		i2s->rx_start = false;

		ret = regmap_update_bits(i2s->regmap, I2S_DMACR,
					 I2S_DMACR_RDE_ENABLE,
					 I2S_DMACR_RDE_DISABLE);
		if (ret < 0)
			goto end;

		if (!i2s->tx_start) {
			ret = regmap_update_bits(i2s->regmap, I2S_XFER,
						 I2S_XFER_TXS_START | I2S_XFER_RXS_START,
						 I2S_XFER_TXS_STOP | I2S_XFER_RXS_STOP);
			if (ret < 0)
				goto end;
			udelay(150);
			ret = regmap_update_bits(i2s->regmap, I2S_CLR,
						 I2S_CLR_TXC | I2S_CLR_RXC,
						 I2S_CLR_TXC | I2S_CLR_RXC);
			if (ret < 0)
				goto end;
			ret = regmap_read_poll_timeout_atomic(i2s->regmap,
							      I2S_CLR,
							      val,
							      val == 0,
							      20,
							      200);
			if (ret < 0)
				dev_warn(i2s->dev, "fail to clear: %d\n", ret);
		}
	}
end:
	spin_unlock(&i2s->lock);
	if (ret < 0)
		dev_err(i2s->dev, "lrclk update failed\n");

	return ret;
}

static int rockchip_i2s_set_fmt(struct snd_soc_dai *cpu_dai,
				unsigned int fmt)
{
	struct rk_i2s_dev *i2s = to_info(cpu_dai);
	unsigned int mask = 0, val = 0;
	int ret = 0;

	pm_runtime_get_sync(cpu_dai->dev);
	mask = I2S_CKR_MSS_MASK;
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		/* Set source clock in Master mode */
		val = I2S_CKR_MSS_MASTER;
		i2s->is_master_mode = true;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		val = I2S_CKR_MSS_SLAVE;
		i2s->is_master_mode = false;
		break;
	default:
		ret = -EINVAL;
		goto err_pm_put;
	}

	regmap_update_bits(i2s->regmap, I2S_CKR, mask, val);

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

	regmap_update_bits(i2s->regmap, I2S_CKR, mask, val);

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
	case SND_SOC_DAIFMT_DSP_A: /* PCM delay 1 bit mode */
		val = I2S_TXCR_TFS_PCM | I2S_TXCR_PBM_MODE(1);
		break;
	case SND_SOC_DAIFMT_DSP_B: /* PCM no delay mode */
		val = I2S_TXCR_TFS_PCM;
		break;
	default:
		ret = -EINVAL;
		goto err_pm_put;
	}

	regmap_update_bits(i2s->regmap, I2S_TXCR, mask, val);

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
	case SND_SOC_DAIFMT_DSP_A: /* PCM delay 1 bit mode */
		val = I2S_RXCR_TFS_PCM | I2S_RXCR_PBM_MODE(1);
		break;
	case SND_SOC_DAIFMT_DSP_B: /* PCM no delay mode */
		val = I2S_RXCR_TFS_PCM;
		break;
	default:
		ret = -EINVAL;
		goto err_pm_put;
	}

	regmap_update_bits(i2s->regmap, I2S_RXCR, mask, val);

err_pm_put:
	pm_runtime_put(cpu_dai->dev);

	return ret;
}

static int rockchip_i2s_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct rk_i2s_dev *i2s = to_info(dai);
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	unsigned int val = 0;
	unsigned int mclk_rate, bclk_rate, div_bclk, div_lrck;

	if (i2s->is_master_mode) {
		mclk_rate = clk_get_rate(i2s->mclk);
		bclk_rate = i2s->bclk_ratio * params_rate(params);
		if (!bclk_rate)
			return -EINVAL;

		div_bclk = DIV_ROUND_CLOSEST(mclk_rate, bclk_rate);
		div_lrck = bclk_rate / params_rate(params);
		regmap_update_bits(i2s->regmap, I2S_CKR,
				   I2S_CKR_MDIV_MASK,
				   I2S_CKR_MDIV(div_bclk));

		regmap_update_bits(i2s->regmap, I2S_CKR,
				   I2S_CKR_TSD_MASK |
				   I2S_CKR_RSD_MASK,
				   I2S_CKR_TSD(div_lrck) |
				   I2S_CKR_RSD(div_lrck));
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
		dev_err(i2s->dev, "invalid channel: %d\n",
			params_channels(params));
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		regmap_update_bits(i2s->regmap, I2S_RXCR,
				   I2S_RXCR_VDW_MASK | I2S_RXCR_CSR_MASK,
				   val);
	else
		regmap_update_bits(i2s->regmap, I2S_TXCR,
				   I2S_TXCR_VDW_MASK | I2S_TXCR_CSR_MASK,
				   val);

	if (!IS_ERR(i2s->grf) && i2s->pins) {
		regmap_read(i2s->regmap, I2S_TXCR, &val);
		val &= I2S_TXCR_CSR_MASK;

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

		val <<= i2s->pins->shift;
		val |= (I2S_IO_DIRECTION_MASK << i2s->pins->shift) << 16;
		regmap_write(i2s->grf, i2s->pins->reg_offset, val);
	}

	regmap_update_bits(i2s->regmap, I2S_DMACR, I2S_DMACR_TDL_MASK,
			   I2S_DMACR_TDL(16));
	regmap_update_bits(i2s->regmap, I2S_DMACR, I2S_DMACR_RDL_MASK,
			   I2S_DMACR_RDL(16));

	val = I2S_CKR_TRCM_TXRX;
	if (dai->driver->symmetric_rate && rtd->dai_link->symmetric_rate)
		val = I2S_CKR_TRCM_TXONLY;

	regmap_update_bits(i2s->regmap, I2S_CKR,
			   I2S_CKR_TRCM_MASK,
			   val);
	return 0;
}

static int rockchip_i2s_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct rk_i2s_dev *i2s = to_info(dai);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			ret = rockchip_snd_rxctrl(i2s, 1);
		else
			ret = rockchip_snd_txctrl(i2s, 1);
		if (ret < 0)
			return ret;
		i2s_pinctrl_select_bclk_on(i2s);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
			if (!i2s->tx_start)
				i2s_pinctrl_select_bclk_off(i2s);
			ret = rockchip_snd_rxctrl(i2s, 0);
		} else {
			if (!i2s->rx_start)
				i2s_pinctrl_select_bclk_off(i2s);
			ret = rockchip_snd_txctrl(i2s, 0);
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int rockchip_i2s_set_bclk_ratio(struct snd_soc_dai *dai,
				       unsigned int ratio)
{
	struct rk_i2s_dev *i2s = to_info(dai);

	i2s->bclk_ratio = ratio;

	return 0;
}

static int rockchip_i2s_set_sysclk(struct snd_soc_dai *cpu_dai, int clk_id,
				   unsigned int freq, int dir)
{
	struct rk_i2s_dev *i2s = to_info(cpu_dai);
	int ret;

	if (freq == 0)
		return 0;

	ret = clk_set_rate(i2s->mclk, freq);
	if (ret)
		dev_err(i2s->dev, "Fail to set mclk %d\n", ret);

	return ret;
}

static int rockchip_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct rk_i2s_dev *i2s = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai,
		i2s->has_playback ? &i2s->playback_dma_data : NULL,
		i2s->has_capture  ? &i2s->capture_dma_data  : NULL);

	return 0;
}

static const struct snd_soc_dai_ops rockchip_i2s_dai_ops = {
	.hw_params = rockchip_i2s_hw_params,
	.set_bclk_ratio	= rockchip_i2s_set_bclk_ratio,
	.set_sysclk = rockchip_i2s_set_sysclk,
	.set_fmt = rockchip_i2s_set_fmt,
	.trigger = rockchip_i2s_trigger,
};

static struct snd_soc_dai_driver rockchip_i2s_dai = {
	.probe = rockchip_i2s_dai_probe,
	.ops = &rockchip_i2s_dai_ops,
	.symmetric_rate = 1,
};

static const struct snd_soc_component_driver rockchip_i2s_component = {
	.name = DRV_NAME,
	.legacy_dai_naming = 1,
};

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
	case I2S_TXDR:
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
	case I2S_FIFOLR:
	case I2S_TXDR:
	case I2S_RXDR:
		return true;
	default:
		return false;
	}
}

static bool rockchip_i2s_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case I2S_RXDR:
		return true;
	default:
		return false;
	}
}

static const struct reg_default rockchip_i2s_reg_defaults[] = {
	{0x00, 0x0000000f},
	{0x04, 0x0000000f},
	{0x08, 0x00071f1f},
	{0x10, 0x001f0000},
	{0x14, 0x01f00000},
};

static const struct regmap_config rockchip_i2s_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = I2S_RXDR,
	.reg_defaults = rockchip_i2s_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rockchip_i2s_reg_defaults),
	.writeable_reg = rockchip_i2s_wr_reg,
	.readable_reg = rockchip_i2s_rd_reg,
	.volatile_reg = rockchip_i2s_volatile_reg,
	.precious_reg = rockchip_i2s_precious_reg,
	.cache_type = REGCACHE_FLAT,
};

static const struct rk_i2s_pins rk3399_i2s_pins = {
	.reg_offset = 0xe220,
	.shift = 11,
};

static const struct of_device_id rockchip_i2s_match[] __maybe_unused = {
	{ .compatible = "rockchip,px30-i2s", },
	{ .compatible = "rockchip,rk1808-i2s", },
	{ .compatible = "rockchip,rk3036-i2s", },
	{ .compatible = "rockchip,rk3066-i2s", },
	{ .compatible = "rockchip,rk3128-i2s", },
	{ .compatible = "rockchip,rk3188-i2s", },
	{ .compatible = "rockchip,rk3228-i2s", },
	{ .compatible = "rockchip,rk3288-i2s", },
	{ .compatible = "rockchip,rk3308-i2s", },
	{ .compatible = "rockchip,rk3328-i2s", },
	{ .compatible = "rockchip,rk3366-i2s", },
	{ .compatible = "rockchip,rk3368-i2s", },
	{ .compatible = "rockchip,rk3399-i2s", .data = &rk3399_i2s_pins },
	{ .compatible = "rockchip,rk3588-i2s", },
	{ .compatible = "rockchip,rv1126-i2s", },
	{},
};

static int rockchip_i2s_init_dai(struct rk_i2s_dev *i2s, struct resource *res,
				 struct snd_soc_dai_driver **dp)
{
	struct device_node *node = i2s->dev->of_node;
	struct snd_soc_dai_driver *dai;
	struct property *dma_names;
	const char *dma_name;
	unsigned int val;

	of_property_for_each_string(node, "dma-names", dma_names, dma_name) {
		if (!strcmp(dma_name, "tx"))
			i2s->has_playback = true;
		if (!strcmp(dma_name, "rx"))
			i2s->has_capture = true;
	}

	dai = devm_kmemdup(i2s->dev, &rockchip_i2s_dai,
			   sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	if (i2s->has_playback) {
		dai->playback.stream_name = "Playback";
		dai->playback.channels_min = 2;
		dai->playback.channels_max = 8;
		dai->playback.rates = SNDRV_PCM_RATE_8000_192000;
		dai->playback.formats = SNDRV_PCM_FMTBIT_S8 |
					SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_S20_3LE |
					SNDRV_PCM_FMTBIT_S24_LE |
					SNDRV_PCM_FMTBIT_S32_LE;

		i2s->playback_dma_data.addr = res->start + I2S_TXDR;
		i2s->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		i2s->playback_dma_data.maxburst = 8;

		if (!of_property_read_u32(node, "rockchip,playback-channels", &val)) {
			if (val >= 2 && val <= 8)
				dai->playback.channels_max = val;
		}
	}

	if (i2s->has_capture) {
		dai->capture.stream_name = "Capture";
		dai->capture.channels_min = 2;
		dai->capture.channels_max = 8;
		dai->capture.rates = SNDRV_PCM_RATE_8000_192000;
		dai->capture.formats = SNDRV_PCM_FMTBIT_S8 |
				       SNDRV_PCM_FMTBIT_S16_LE |
				       SNDRV_PCM_FMTBIT_S20_3LE |
				       SNDRV_PCM_FMTBIT_S24_LE |
				       SNDRV_PCM_FMTBIT_S32_LE;

		i2s->capture_dma_data.addr = res->start + I2S_RXDR;
		i2s->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		i2s->capture_dma_data.maxburst = 8;

		if (!of_property_read_u32(node, "rockchip,capture-channels", &val)) {
			if (val >= 2 && val <= 8)
				dai->capture.channels_max = val;
		}
	}

	if (dp)
		*dp = dai;

	return 0;
}

static int rockchip_i2s_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	const struct of_device_id *of_id;
	struct rk_i2s_dev *i2s;
	struct snd_soc_dai_driver *dai;
	struct resource *res;
	void __iomem *regs;
	int ret;

	i2s = devm_kzalloc(&pdev->dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	spin_lock_init(&i2s->lock);
	i2s->dev = &pdev->dev;

	i2s->grf = syscon_regmap_lookup_by_phandle(node, "rockchip,grf");
	if (!IS_ERR(i2s->grf)) {
		of_id = of_match_device(rockchip_i2s_match, &pdev->dev);
		if (!of_id || !of_id->data)
			return -EINVAL;

		i2s->pins = of_id->data;
	}

	/* try to prepare related clocks */
	i2s->hclk = devm_clk_get(&pdev->dev, "i2s_hclk");
	if (IS_ERR(i2s->hclk)) {
		dev_err(&pdev->dev, "Can't retrieve i2s bus clock\n");
		return PTR_ERR(i2s->hclk);
	}
	ret = clk_prepare_enable(i2s->hclk);
	if (ret) {
		dev_err(i2s->dev, "hclock enable failed %d\n", ret);
		return ret;
	}

	i2s->mclk = devm_clk_get(&pdev->dev, "i2s_clk");
	if (IS_ERR(i2s->mclk)) {
		dev_err(&pdev->dev, "Can't retrieve i2s master clock\n");
		ret = PTR_ERR(i2s->mclk);
		goto err_clk;
	}

	regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(regs)) {
		ret = PTR_ERR(regs);
		goto err_clk;
	}

	i2s->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					    &rockchip_i2s_regmap_config);
	if (IS_ERR(i2s->regmap)) {
		dev_err(&pdev->dev,
			"Failed to initialise managed register map\n");
		ret = PTR_ERR(i2s->regmap);
		goto err_clk;
	}

	i2s->bclk_ratio = 64;
	i2s->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (!IS_ERR(i2s->pinctrl)) {
		i2s->bclk_on = pinctrl_lookup_state(i2s->pinctrl, "bclk_on");
		if (!IS_ERR_OR_NULL(i2s->bclk_on)) {
			i2s->bclk_off = pinctrl_lookup_state(i2s->pinctrl, "bclk_off");
			if (IS_ERR_OR_NULL(i2s->bclk_off)) {
				dev_err(&pdev->dev, "failed to find i2s bclk_off\n");
				ret = -EINVAL;
				goto err_clk;
			}
		}
	} else {
		dev_dbg(&pdev->dev, "failed to find i2s pinctrl\n");
	}

	i2s_pinctrl_select_bclk_off(i2s);

	dev_set_drvdata(&pdev->dev, i2s);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = i2s_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	ret = rockchip_i2s_init_dai(i2s, res, &dai);
	if (ret)
		goto err_pm_disable;

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &rockchip_i2s_component,
					      dai, 1);

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
		i2s_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);
err_clk:
	clk_disable_unprepare(i2s->hclk);
	return ret;
}

static void rockchip_i2s_remove(struct platform_device *pdev)
{
	struct rk_i2s_dev *i2s = dev_get_drvdata(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		i2s_runtime_suspend(&pdev->dev);

	clk_disable_unprepare(i2s->hclk);
}

static const struct dev_pm_ops rockchip_i2s_pm_ops = {
	SET_RUNTIME_PM_OPS(i2s_runtime_suspend, i2s_runtime_resume,
			   NULL)
};

static struct platform_driver rockchip_i2s_driver = {
	.probe = rockchip_i2s_probe,
	.remove_new = rockchip_i2s_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(rockchip_i2s_match),
		.pm = &rockchip_i2s_pm_ops,
	},
};
module_platform_driver(rockchip_i2s_driver);

MODULE_DESCRIPTION("ROCKCHIP IIS ASoC Interface");
MODULE_AUTHOR("jianqun <jay.xu@rock-chips.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, rockchip_i2s_match);
