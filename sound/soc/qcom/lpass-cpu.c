// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2011,2013-2015 The Linux Foundation. All rights reserved.
 *
 * lpass-cpu.c -- ALSA SoC CPU DAI driver for QTi LPASS
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include "lpass-lpaif-reg.h"
#include "lpass.h"

#define LPASS_CPU_MAX_MI2S_LINES	4
#define LPASS_CPU_I2S_SD0_MASK		BIT(0)
#define LPASS_CPU_I2S_SD1_MASK		BIT(1)
#define LPASS_CPU_I2S_SD2_MASK		BIT(2)
#define LPASS_CPU_I2S_SD3_MASK		BIT(3)
#define LPASS_CPU_I2S_SD0_1_MASK	GENMASK(1, 0)
#define LPASS_CPU_I2S_SD2_3_MASK	GENMASK(3, 2)
#define LPASS_CPU_I2S_SD0_1_2_MASK	GENMASK(2, 0)
#define LPASS_CPU_I2S_SD0_1_2_3_MASK	GENMASK(3, 0)

static int lpass_cpu_init_i2sctl_bitfields(struct device *dev,
			struct lpaif_i2sctl *i2sctl, struct regmap *map)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);
	struct lpass_variant *v = drvdata->variant;

	i2sctl->loopback = devm_regmap_field_alloc(dev, map, v->loopback);
	i2sctl->spken = devm_regmap_field_alloc(dev, map, v->spken);
	i2sctl->spkmode = devm_regmap_field_alloc(dev, map, v->spkmode);
	i2sctl->spkmono = devm_regmap_field_alloc(dev, map, v->spkmono);
	i2sctl->micen = devm_regmap_field_alloc(dev, map, v->micen);
	i2sctl->micmode = devm_regmap_field_alloc(dev, map, v->micmode);
	i2sctl->micmono = devm_regmap_field_alloc(dev, map, v->micmono);
	i2sctl->wssrc = devm_regmap_field_alloc(dev, map, v->wssrc);
	i2sctl->bitwidth = devm_regmap_field_alloc(dev, map, v->bitwidth);

	if (IS_ERR(i2sctl->loopback) || IS_ERR(i2sctl->spken) ||
	    IS_ERR(i2sctl->spkmode) || IS_ERR(i2sctl->spkmono) ||
	    IS_ERR(i2sctl->micen) || IS_ERR(i2sctl->micmode) ||
	    IS_ERR(i2sctl->micmono) || IS_ERR(i2sctl->wssrc) ||
	    IS_ERR(i2sctl->bitwidth))
		return -EINVAL;

	return 0;
}

static int lpass_cpu_daiops_set_sysclk(struct snd_soc_dai *dai, int clk_id,
		unsigned int freq, int dir)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	int ret;

	ret = clk_set_rate(drvdata->mi2s_osr_clk[dai->driver->id], freq);
	if (ret)
		dev_err(dai->dev, "error setting mi2s osrclk to %u: %d\n",
			freq, ret);

	return ret;
}

static int lpass_cpu_daiops_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	int ret;

	ret = clk_prepare_enable(drvdata->mi2s_osr_clk[dai->driver->id]);
	if (ret) {
		dev_err(dai->dev, "error in enabling mi2s osr clk: %d\n", ret);
		return ret;
	}
	ret = clk_prepare(drvdata->mi2s_bit_clk[dai->driver->id]);
	if (ret) {
		dev_err(dai->dev, "error in enabling mi2s bit clk: %d\n", ret);
		clk_disable_unprepare(drvdata->mi2s_osr_clk[dai->driver->id]);
		return ret;
	}
	return 0;
}

static void lpass_cpu_daiops_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);

	clk_disable_unprepare(drvdata->mi2s_osr_clk[dai->driver->id]);
	clk_unprepare(drvdata->mi2s_bit_clk[dai->driver->id]);
}

static int lpass_cpu_daiops_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	struct lpaif_i2sctl *i2sctl = drvdata->i2sctl;
	unsigned int id = dai->driver->id;
	snd_pcm_format_t format = params_format(params);
	unsigned int channels = params_channels(params);
	unsigned int rate = params_rate(params);
	unsigned int mode;
	unsigned int regval;
	int bitwidth, ret;

	bitwidth = snd_pcm_format_width(format);
	if (bitwidth < 0) {
		dev_err(dai->dev, "invalid bit width given: %d\n", bitwidth);
		return bitwidth;
	}

	ret = regmap_fields_write(i2sctl->loopback, id,
				 LPAIF_I2SCTL_LOOPBACK_DISABLE);
	if (ret) {
		dev_err(dai->dev, "error updating loopback field: %d\n", ret);
		return ret;
	}

	ret = regmap_fields_write(i2sctl->wssrc, id,
				 LPAIF_I2SCTL_WSSRC_INTERNAL);
	if (ret) {
		dev_err(dai->dev, "error updating wssrc field: %d\n", ret);
		return ret;
	}

	switch (bitwidth) {
	case 16:
		regval = LPAIF_I2SCTL_BITWIDTH_16;
		break;
	case 24:
		regval = LPAIF_I2SCTL_BITWIDTH_24;
		break;
	case 32:
		regval = LPAIF_I2SCTL_BITWIDTH_32;
		break;
	default:
		dev_err(dai->dev, "invalid bitwidth given: %d\n", bitwidth);
		return -EINVAL;
	}

	ret = regmap_fields_write(i2sctl->bitwidth, id, regval);
	if (ret) {
		dev_err(dai->dev, "error updating bitwidth field: %d\n", ret);
		return ret;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		mode = drvdata->mi2s_playback_sd_mode[id];
	else
		mode = drvdata->mi2s_capture_sd_mode[id];

	if (!mode) {
		dev_err(dai->dev, "no line is assigned\n");
		return -EINVAL;
	}

	switch (channels) {
	case 1:
	case 2:
		switch (mode) {
		case LPAIF_I2SCTL_MODE_QUAD01:
		case LPAIF_I2SCTL_MODE_6CH:
		case LPAIF_I2SCTL_MODE_8CH:
			mode = LPAIF_I2SCTL_MODE_SD0;
			break;
		case LPAIF_I2SCTL_MODE_QUAD23:
			mode = LPAIF_I2SCTL_MODE_SD2;
			break;
		}

		break;
	case 4:
		if (mode < LPAIF_I2SCTL_MODE_QUAD01) {
			dev_err(dai->dev, "cannot configure 4 channels with mode %d\n",
				mode);
			return -EINVAL;
		}

		switch (mode) {
		case LPAIF_I2SCTL_MODE_6CH:
		case LPAIF_I2SCTL_MODE_8CH:
			mode = LPAIF_I2SCTL_MODE_QUAD01;
			break;
		}
		break;
	case 6:
		if (mode < LPAIF_I2SCTL_MODE_6CH) {
			dev_err(dai->dev, "cannot configure 6 channels with mode %d\n",
				mode);
			return -EINVAL;
		}

		switch (mode) {
		case LPAIF_I2SCTL_MODE_8CH:
			mode = LPAIF_I2SCTL_MODE_6CH;
			break;
		}
		break;
	case 8:
		if (mode < LPAIF_I2SCTL_MODE_8CH) {
			dev_err(dai->dev, "cannot configure 8 channels with mode %d\n",
				mode);
			return -EINVAL;
		}
		break;
	default:
		dev_err(dai->dev, "invalid channels given: %u\n", channels);
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = regmap_fields_write(i2sctl->spkmode, id,
					 LPAIF_I2SCTL_SPKMODE(mode));
		if (ret) {
			dev_err(dai->dev, "error writing to i2sctl spkr mode: %d\n",
				ret);
			return ret;
		}
		if (channels >= 2)
			ret = regmap_fields_write(i2sctl->spkmono, id,
						 LPAIF_I2SCTL_SPKMONO_STEREO);
		else
			ret = regmap_fields_write(i2sctl->spkmono, id,
						 LPAIF_I2SCTL_SPKMONO_MONO);
	} else {
		ret = regmap_fields_write(i2sctl->micmode, id,
					 LPAIF_I2SCTL_MICMODE(mode));
		if (ret) {
			dev_err(dai->dev, "error writing to i2sctl mic mode: %d\n",
				ret);
			return ret;
		}
		if (channels >= 2)
			ret = regmap_fields_write(i2sctl->micmono, id,
						 LPAIF_I2SCTL_MICMONO_STEREO);
		else
			ret = regmap_fields_write(i2sctl->micmono, id,
						 LPAIF_I2SCTL_MICMONO_MONO);
	}

	if (ret) {
		dev_err(dai->dev, "error writing to i2sctl channels mode: %d\n",
			ret);
		return ret;
	}

	ret = clk_set_rate(drvdata->mi2s_bit_clk[id],
			   rate * bitwidth * 2);
	if (ret) {
		dev_err(dai->dev, "error setting mi2s bitclk to %u: %d\n",
			rate * bitwidth * 2, ret);
		return ret;
	}

	return 0;
}

static int lpass_cpu_daiops_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	struct lpaif_i2sctl *i2sctl = drvdata->i2sctl;
	unsigned int id = dai->driver->id;
	int ret = -EINVAL;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			ret = regmap_fields_write(i2sctl->spken, id,
						 LPAIF_I2SCTL_SPKEN_ENABLE);
		} else  {
			ret = regmap_fields_write(i2sctl->micen, id,
						 LPAIF_I2SCTL_MICEN_ENABLE);
		}
		if (ret)
			dev_err(dai->dev, "error writing to i2sctl reg: %d\n",
				ret);

		if (drvdata->bit_clk_state[id] == LPAIF_BIT_CLK_DISABLE) {
			ret = clk_enable(drvdata->mi2s_bit_clk[id]);
			if (ret) {
				dev_err(dai->dev, "error in enabling mi2s bit clk: %d\n", ret);
				clk_disable(drvdata->mi2s_osr_clk[id]);
				return ret;
			}
			drvdata->bit_clk_state[id] = LPAIF_BIT_CLK_ENABLE;
		}

		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			ret = regmap_fields_write(i2sctl->spken, id,
						 LPAIF_I2SCTL_SPKEN_DISABLE);
		} else  {
			ret = regmap_fields_write(i2sctl->micen, id,
						 LPAIF_I2SCTL_MICEN_DISABLE);
		}
		if (ret)
			dev_err(dai->dev, "error writing to i2sctl reg: %d\n",
				ret);
		if (drvdata->bit_clk_state[id] == LPAIF_BIT_CLK_ENABLE) {
			clk_disable(drvdata->mi2s_bit_clk[dai->driver->id]);
			drvdata->bit_clk_state[id] = LPAIF_BIT_CLK_DISABLE;
		}
		break;
	}

	return ret;
}

const struct snd_soc_dai_ops asoc_qcom_lpass_cpu_dai_ops = {
	.set_sysclk	= lpass_cpu_daiops_set_sysclk,
	.startup	= lpass_cpu_daiops_startup,
	.shutdown	= lpass_cpu_daiops_shutdown,
	.hw_params	= lpass_cpu_daiops_hw_params,
	.trigger	= lpass_cpu_daiops_trigger,
};
EXPORT_SYMBOL_GPL(asoc_qcom_lpass_cpu_dai_ops);

int asoc_qcom_lpass_cpu_dai_probe(struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	int ret;

	/* ensure audio hardware is disabled */
	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_I2SCTL_REG(drvdata->variant, dai->driver->id), 0);
	if (ret)
		dev_err(dai->dev, "error writing to i2sctl reg: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(asoc_qcom_lpass_cpu_dai_probe);

static int asoc_qcom_of_xlate_dai_name(struct snd_soc_component *component,
				   struct of_phandle_args *args,
				   const char **dai_name)
{
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct lpass_variant *variant = drvdata->variant;
	int id = args->args[0];
	int ret = -EINVAL;
	int i;

	for (i = 0; i  < variant->num_dai; i++) {
		if (variant->dai_driver[i].id == id) {
			*dai_name = variant->dai_driver[i].name;
			ret = 0;
			break;
		}
	}

	return ret;
}

static const struct snd_soc_component_driver lpass_cpu_comp_driver = {
	.name = "lpass-cpu",
	.of_xlate_dai_name = asoc_qcom_of_xlate_dai_name,
};

static bool lpass_cpu_regmap_writeable(struct device *dev, unsigned int reg)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);
	struct lpass_variant *v = drvdata->variant;
	int i;

	for (i = 0; i < v->i2s_ports; ++i)
		if (reg == LPAIF_I2SCTL_REG(v, i))
			return true;

	for (i = 0; i < v->irq_ports; ++i) {
		if (reg == LPAIF_IRQEN_REG(v, i))
			return true;
		if (reg == LPAIF_IRQCLEAR_REG(v, i))
			return true;
	}

	for (i = 0; i < v->rdma_channels; ++i) {
		if (reg == LPAIF_RDMACTL_REG(v, i))
			return true;
		if (reg == LPAIF_RDMABASE_REG(v, i))
			return true;
		if (reg == LPAIF_RDMABUFF_REG(v, i))
			return true;
		if (reg == LPAIF_RDMAPER_REG(v, i))
			return true;
	}

	for (i = 0; i < v->wrdma_channels; ++i) {
		if (reg == LPAIF_WRDMACTL_REG(v, i + v->wrdma_channel_start))
			return true;
		if (reg == LPAIF_WRDMABASE_REG(v, i + v->wrdma_channel_start))
			return true;
		if (reg == LPAIF_WRDMABUFF_REG(v, i + v->wrdma_channel_start))
			return true;
		if (reg == LPAIF_WRDMAPER_REG(v, i + v->wrdma_channel_start))
			return true;
	}

	return false;
}

static bool lpass_cpu_regmap_readable(struct device *dev, unsigned int reg)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);
	struct lpass_variant *v = drvdata->variant;
	int i;

	for (i = 0; i < v->i2s_ports; ++i)
		if (reg == LPAIF_I2SCTL_REG(v, i))
			return true;

	for (i = 0; i < v->irq_ports; ++i) {
		if (reg == LPAIF_IRQEN_REG(v, i))
			return true;
		if (reg == LPAIF_IRQSTAT_REG(v, i))
			return true;
	}

	for (i = 0; i < v->rdma_channels; ++i) {
		if (reg == LPAIF_RDMACTL_REG(v, i))
			return true;
		if (reg == LPAIF_RDMABASE_REG(v, i))
			return true;
		if (reg == LPAIF_RDMABUFF_REG(v, i))
			return true;
		if (reg == LPAIF_RDMACURR_REG(v, i))
			return true;
		if (reg == LPAIF_RDMAPER_REG(v, i))
			return true;
	}

	for (i = 0; i < v->wrdma_channels; ++i) {
		if (reg == LPAIF_WRDMACTL_REG(v, i + v->wrdma_channel_start))
			return true;
		if (reg == LPAIF_WRDMABASE_REG(v, i + v->wrdma_channel_start))
			return true;
		if (reg == LPAIF_WRDMABUFF_REG(v, i + v->wrdma_channel_start))
			return true;
		if (reg == LPAIF_WRDMACURR_REG(v, i + v->wrdma_channel_start))
			return true;
		if (reg == LPAIF_WRDMAPER_REG(v, i + v->wrdma_channel_start))
			return true;
	}

	return false;
}

static bool lpass_cpu_regmap_volatile(struct device *dev, unsigned int reg)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);
	struct lpass_variant *v = drvdata->variant;
	int i;

	for (i = 0; i < v->irq_ports; ++i)
		if (reg == LPAIF_IRQSTAT_REG(v, i))
			return true;

	for (i = 0; i < v->rdma_channels; ++i)
		if (reg == LPAIF_RDMACURR_REG(v, i))
			return true;

	for (i = 0; i < v->wrdma_channels; ++i)
		if (reg == LPAIF_WRDMACURR_REG(v, i + v->wrdma_channel_start))
			return true;

	return false;
}

static struct regmap_config lpass_cpu_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.writeable_reg = lpass_cpu_regmap_writeable,
	.readable_reg = lpass_cpu_regmap_readable,
	.volatile_reg = lpass_cpu_regmap_volatile,
	.cache_type = REGCACHE_FLAT,
};

static int lpass_hdmi_init_bitfields(struct device *dev, struct regmap *map)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);
	struct lpass_variant *v = drvdata->variant;
	unsigned int i;
	struct lpass_hdmi_tx_ctl *tx_ctl;
	struct regmap_field *legacy_en;
	struct lpass_vbit_ctrl *vbit_ctl;
	struct regmap_field *tx_parity;
	struct lpass_dp_metadata_ctl *meta_ctl;
	struct lpass_sstream_ctl *sstream_ctl;
	struct regmap_field *ch_msb;
	struct regmap_field *ch_lsb;
	struct lpass_hdmitx_dmactl *tx_dmactl;
	int rval;

	tx_ctl = devm_kzalloc(dev, sizeof(*tx_ctl), GFP_KERNEL);
	if (!tx_ctl)
		return -ENOMEM;

	QCOM_REGMAP_FIELD_ALLOC(dev, map, v->soft_reset, tx_ctl->soft_reset);
	QCOM_REGMAP_FIELD_ALLOC(dev, map, v->force_reset, tx_ctl->force_reset);
	drvdata->tx_ctl = tx_ctl;

	QCOM_REGMAP_FIELD_ALLOC(dev, map, v->legacy_en, legacy_en);
	drvdata->hdmitx_legacy_en = legacy_en;

	vbit_ctl = devm_kzalloc(dev, sizeof(*vbit_ctl), GFP_KERNEL);
	if (!vbit_ctl)
		return -ENOMEM;

	QCOM_REGMAP_FIELD_ALLOC(dev, map, v->replace_vbit, vbit_ctl->replace_vbit);
	QCOM_REGMAP_FIELD_ALLOC(dev, map, v->vbit_stream, vbit_ctl->vbit_stream);
	drvdata->vbit_ctl = vbit_ctl;


	QCOM_REGMAP_FIELD_ALLOC(dev, map, v->calc_en, tx_parity);
	drvdata->hdmitx_parity_calc_en = tx_parity;

	meta_ctl = devm_kzalloc(dev, sizeof(*meta_ctl), GFP_KERNEL);
	if (!meta_ctl)
		return -ENOMEM;

	rval = devm_regmap_field_bulk_alloc(dev, map, &meta_ctl->mute, &v->mute, 7);
	if (rval)
		return rval;
	drvdata->meta_ctl = meta_ctl;

	sstream_ctl = devm_kzalloc(dev, sizeof(*sstream_ctl), GFP_KERNEL);
	if (!sstream_ctl)
		return -ENOMEM;

	rval = devm_regmap_field_bulk_alloc(dev, map, &sstream_ctl->sstream_en, &v->sstream_en, 9);
	if (rval)
		return rval;

	drvdata->sstream_ctl = sstream_ctl;

	for (i = 0; i < LPASS_MAX_HDMI_DMA_CHANNELS; i++) {
		QCOM_REGMAP_FIELD_ALLOC(dev, map, v->msb_bits, ch_msb);
		drvdata->hdmitx_ch_msb[i] = ch_msb;

		QCOM_REGMAP_FIELD_ALLOC(dev, map, v->lsb_bits, ch_lsb);
		drvdata->hdmitx_ch_lsb[i] = ch_lsb;

		tx_dmactl = devm_kzalloc(dev, sizeof(*tx_dmactl), GFP_KERNEL);
		if (!tx_dmactl)
			return -ENOMEM;

		QCOM_REGMAP_FIELD_ALLOC(dev, map, v->use_hw_chs, tx_dmactl->use_hw_chs);
		QCOM_REGMAP_FIELD_ALLOC(dev, map, v->use_hw_usr, tx_dmactl->use_hw_usr);
		QCOM_REGMAP_FIELD_ALLOC(dev, map, v->hw_chs_sel, tx_dmactl->hw_chs_sel);
		QCOM_REGMAP_FIELD_ALLOC(dev, map, v->hw_usr_sel, tx_dmactl->hw_usr_sel);
		drvdata->hdmi_tx_dmactl[i] = tx_dmactl;
	}
	return 0;
}

static bool lpass_hdmi_regmap_writeable(struct device *dev, unsigned int reg)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);
	struct lpass_variant *v = drvdata->variant;
	int i;

	if (reg == LPASS_HDMI_TX_CTL_ADDR(v))
		return true;
	if (reg == LPASS_HDMI_TX_LEGACY_ADDR(v))
		return true;
	if (reg == LPASS_HDMI_TX_VBIT_CTL_ADDR(v))
		return true;
	if (reg == LPASS_HDMI_TX_PARITY_ADDR(v))
		return true;
	if (reg == LPASS_HDMI_TX_DP_ADDR(v))
		return true;
	if (reg == LPASS_HDMI_TX_SSTREAM_ADDR(v))
		return true;
	if (reg == LPASS_HDMITX_APP_IRQEN_REG(v))
		return true;
	if (reg == LPASS_HDMITX_APP_IRQCLEAR_REG(v))
		return true;

	for (i = 0; i < v->hdmi_rdma_channels; i++) {
		if (reg == LPASS_HDMI_TX_CH_LSB_ADDR(v, i))
			return true;
		if (reg == LPASS_HDMI_TX_CH_MSB_ADDR(v, i))
			return true;
		if (reg == LPASS_HDMI_TX_DMA_ADDR(v, i))
			return true;
	}

	for (i = 0; i < v->rdma_channels; ++i) {
		if (reg == LPAIF_HDMI_RDMACTL_REG(v, i))
			return true;
		if (reg == LPAIF_HDMI_RDMABASE_REG(v, i))
			return true;
		if (reg == LPAIF_HDMI_RDMABUFF_REG(v, i))
			return true;
		if (reg == LPAIF_HDMI_RDMAPER_REG(v, i))
			return true;
	}
	return false;
}

static bool lpass_hdmi_regmap_readable(struct device *dev, unsigned int reg)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);
	struct lpass_variant *v = drvdata->variant;
	int i;

	if (reg == LPASS_HDMI_TX_CTL_ADDR(v))
		return true;
	if (reg == LPASS_HDMI_TX_LEGACY_ADDR(v))
		return true;
	if (reg == LPASS_HDMI_TX_VBIT_CTL_ADDR(v))
		return true;

	for (i = 0; i < v->hdmi_rdma_channels; i++) {
		if (reg == LPASS_HDMI_TX_CH_LSB_ADDR(v, i))
			return true;
		if (reg == LPASS_HDMI_TX_CH_MSB_ADDR(v, i))
			return true;
		if (reg == LPASS_HDMI_TX_DMA_ADDR(v, i))
			return true;
	}

	if (reg == LPASS_HDMI_TX_PARITY_ADDR(v))
		return true;
	if (reg == LPASS_HDMI_TX_DP_ADDR(v))
		return true;
	if (reg == LPASS_HDMI_TX_SSTREAM_ADDR(v))
		return true;
	if (reg == LPASS_HDMITX_APP_IRQEN_REG(v))
		return true;
	if (reg == LPASS_HDMITX_APP_IRQSTAT_REG(v))
		return true;

	for (i = 0; i < v->rdma_channels; ++i) {
		if (reg == LPAIF_HDMI_RDMACTL_REG(v, i))
			return true;
		if (reg == LPAIF_HDMI_RDMABASE_REG(v, i))
			return true;
		if (reg == LPAIF_HDMI_RDMABUFF_REG(v, i))
			return true;
		if (reg == LPAIF_HDMI_RDMAPER_REG(v, i))
			return true;
		if (reg == LPAIF_HDMI_RDMACURR_REG(v, i))
			return true;
	}

	return false;
}

static bool lpass_hdmi_regmap_volatile(struct device *dev, unsigned int reg)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);
	struct lpass_variant *v = drvdata->variant;
	int i;

	if (reg == LPASS_HDMITX_APP_IRQSTAT_REG(v))
		return true;
	if (reg == LPASS_HDMI_TX_LEGACY_ADDR(v))
		return true;

	for (i = 0; i < v->rdma_channels; ++i) {
		if (reg == LPAIF_HDMI_RDMACURR_REG(v, i))
			return true;
	}
	return false;
}

struct regmap_config lpass_hdmi_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.writeable_reg = lpass_hdmi_regmap_writeable,
	.readable_reg = lpass_hdmi_regmap_readable,
	.volatile_reg = lpass_hdmi_regmap_volatile,
	.cache_type = REGCACHE_FLAT,
};

static unsigned int of_lpass_cpu_parse_sd_lines(struct device *dev,
						struct device_node *node,
						const char *name)
{
	unsigned int lines[LPASS_CPU_MAX_MI2S_LINES];
	unsigned int sd_line_mask = 0;
	int num_lines, i;

	num_lines = of_property_read_variable_u32_array(node, name, lines, 0,
							LPASS_CPU_MAX_MI2S_LINES);
	if (num_lines < 0)
		return LPAIF_I2SCTL_MODE_NONE;

	for (i = 0; i < num_lines; i++)
		sd_line_mask |= BIT(lines[i]);

	switch (sd_line_mask) {
	case LPASS_CPU_I2S_SD0_MASK:
		return LPAIF_I2SCTL_MODE_SD0;
	case LPASS_CPU_I2S_SD1_MASK:
		return LPAIF_I2SCTL_MODE_SD1;
	case LPASS_CPU_I2S_SD2_MASK:
		return LPAIF_I2SCTL_MODE_SD2;
	case LPASS_CPU_I2S_SD3_MASK:
		return LPAIF_I2SCTL_MODE_SD3;
	case LPASS_CPU_I2S_SD0_1_MASK:
		return LPAIF_I2SCTL_MODE_QUAD01;
	case LPASS_CPU_I2S_SD2_3_MASK:
		return LPAIF_I2SCTL_MODE_QUAD23;
	case LPASS_CPU_I2S_SD0_1_2_MASK:
		return LPAIF_I2SCTL_MODE_6CH;
	case LPASS_CPU_I2S_SD0_1_2_3_MASK:
		return LPAIF_I2SCTL_MODE_8CH;
	default:
		dev_err(dev, "Unsupported SD line mask: %#x\n", sd_line_mask);
		return LPAIF_I2SCTL_MODE_NONE;
	}
}

static void of_lpass_cpu_parse_dai_data(struct device *dev,
					struct lpass_data *data)
{
	struct device_node *node;
	int ret, id;

	/* Allow all channels by default for backwards compatibility */
	for (id = 0; id < data->variant->num_dai; id++) {
		data->mi2s_playback_sd_mode[id] = LPAIF_I2SCTL_MODE_8CH;
		data->mi2s_capture_sd_mode[id] = LPAIF_I2SCTL_MODE_8CH;
	}

	for_each_child_of_node(dev->of_node, node) {
		ret = of_property_read_u32(node, "reg", &id);
		if (ret || id < 0 || id >= data->variant->num_dai) {
			dev_err(dev, "valid dai id not found: %d\n", ret);
			continue;
		}
		if (id == LPASS_DP_RX) {
			data->hdmi_port_enable = 1;
			dev_err(dev, "HDMI Port is enabled: %d\n", id);
		} else {
			data->mi2s_playback_sd_mode[id] =
				of_lpass_cpu_parse_sd_lines(dev, node,
							    "qcom,playback-sd-lines");
			data->mi2s_capture_sd_mode[id] =
				of_lpass_cpu_parse_sd_lines(dev, node,
						    "qcom,capture-sd-lines");
		}
	}
}

int asoc_qcom_lpass_cpu_platform_probe(struct platform_device *pdev)
{
	struct lpass_data *drvdata;
	struct device_node *dsp_of_node;
	struct resource *res;
	struct lpass_variant *variant;
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	int ret, i, dai_id;

	dsp_of_node = of_parse_phandle(pdev->dev.of_node, "qcom,adsp", 0);
	if (dsp_of_node) {
		dev_err(dev, "DSP exists and holds audio resources\n");
		return -EBUSY;
	}

	drvdata = devm_kzalloc(dev, sizeof(struct lpass_data), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;
	platform_set_drvdata(pdev, drvdata);

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data)
		return -EINVAL;

	drvdata->variant = (struct lpass_variant *)match->data;
	variant = drvdata->variant;

	of_lpass_cpu_parse_dai_data(dev, drvdata);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lpass-lpaif");

	drvdata->lpaif = devm_ioremap_resource(dev, res);
	if (IS_ERR((void const __force *)drvdata->lpaif)) {
		dev_err(dev, "error mapping reg resource: %ld\n",
				PTR_ERR((void const __force *)drvdata->lpaif));
		return PTR_ERR((void const __force *)drvdata->lpaif);
	}

	lpass_cpu_regmap_config.max_register = LPAIF_WRDMAPER_REG(variant,
						variant->wrdma_channels +
						variant->wrdma_channel_start);

	drvdata->lpaif_map = devm_regmap_init_mmio(dev, drvdata->lpaif,
			&lpass_cpu_regmap_config);
	if (IS_ERR(drvdata->lpaif_map)) {
		dev_err(dev, "error initializing regmap: %ld\n",
			PTR_ERR(drvdata->lpaif_map));
		return PTR_ERR(drvdata->lpaif_map);
	}

	if (drvdata->hdmi_port_enable) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lpass-hdmiif");

		drvdata->hdmiif = devm_ioremap_resource(dev, res);
		if (IS_ERR((void const __force *)drvdata->hdmiif)) {
			dev_err(dev, "error mapping reg resource: %ld\n",
					PTR_ERR((void const __force *)drvdata->hdmiif));
			return PTR_ERR((void const __force *)drvdata->hdmiif);
		}

		lpass_hdmi_regmap_config.max_register = LPAIF_HDMI_RDMAPER_REG(variant,
					variant->hdmi_rdma_channels);
		drvdata->hdmiif_map = devm_regmap_init_mmio(dev, drvdata->hdmiif,
					&lpass_hdmi_regmap_config);
		if (IS_ERR(drvdata->hdmiif_map)) {
			dev_err(dev, "error initializing regmap: %ld\n",
			PTR_ERR(drvdata->hdmiif_map));
			return PTR_ERR(drvdata->hdmiif_map);
		}
	}

	if (variant->init) {
		ret = variant->init(pdev);
		if (ret) {
			dev_err(dev, "error initializing variant: %d\n", ret);
			return ret;
		}
	}

	for (i = 0; i < variant->num_dai; i++) {
		dai_id = variant->dai_driver[i].id;
		if (dai_id == LPASS_DP_RX)
			continue;

		drvdata->mi2s_osr_clk[dai_id] = devm_clk_get(dev,
					     variant->dai_osr_clk_names[i]);
		if (IS_ERR(drvdata->mi2s_osr_clk[dai_id])) {
			dev_warn(dev,
				"%s() error getting optional %s: %ld\n",
				__func__,
				variant->dai_osr_clk_names[i],
				PTR_ERR(drvdata->mi2s_osr_clk[dai_id]));

			drvdata->mi2s_osr_clk[dai_id] = NULL;
		}

		drvdata->mi2s_bit_clk[dai_id] = devm_clk_get(dev,
						variant->dai_bit_clk_names[i]);
		if (IS_ERR(drvdata->mi2s_bit_clk[dai_id])) {
			dev_err(dev,
				"error getting %s: %ld\n",
				variant->dai_bit_clk_names[i],
				PTR_ERR(drvdata->mi2s_bit_clk[dai_id]));
			return PTR_ERR(drvdata->mi2s_bit_clk[dai_id]);
		}
		drvdata->bit_clk_state[dai_id] = LPAIF_BIT_CLK_DISABLE;
	}

	/* Allocation for i2sctl regmap fields */
	drvdata->i2sctl = devm_kzalloc(&pdev->dev, sizeof(struct lpaif_i2sctl),
					GFP_KERNEL);

	/* Initialize bitfields for dai I2SCTL register */
	ret = lpass_cpu_init_i2sctl_bitfields(dev, drvdata->i2sctl,
						drvdata->lpaif_map);
	if (ret) {
		dev_err(dev, "error init i2sctl field: %d\n", ret);
		return ret;
	}

	if (drvdata->hdmi_port_enable) {
		ret = lpass_hdmi_init_bitfields(dev, drvdata->hdmiif_map);
		if (ret) {
			dev_err(dev, "%s error  hdmi init failed\n", __func__);
			return ret;
		}
	}
	ret = devm_snd_soc_register_component(dev,
					      &lpass_cpu_comp_driver,
					      variant->dai_driver,
					      variant->num_dai);
	if (ret) {
		dev_err(dev, "error registering cpu driver: %d\n", ret);
		goto err;
	}

	ret = asoc_qcom_lpass_platform_register(pdev);
	if (ret) {
		dev_err(dev, "error registering platform driver: %d\n", ret);
		goto err;
	}

err:
	return ret;
}
EXPORT_SYMBOL_GPL(asoc_qcom_lpass_cpu_platform_probe);

int asoc_qcom_lpass_cpu_platform_remove(struct platform_device *pdev)
{
	struct lpass_data *drvdata = platform_get_drvdata(pdev);

	if (drvdata->variant->exit)
		drvdata->variant->exit(pdev);


	return 0;
}
EXPORT_SYMBOL_GPL(asoc_qcom_lpass_cpu_platform_remove);

MODULE_DESCRIPTION("QTi LPASS CPU Driver");
MODULE_LICENSE("GPL v2");
