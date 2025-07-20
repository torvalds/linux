// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2020-2025 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.
//
// tegra210_i2s.c - Tegra210 I2S driver

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm_params.h>
#include <sound/simple_card_utils.h>
#include <sound/soc.h>
#include "tegra210_i2s.h"
#include "tegra_cif.h"

static const struct reg_default tegra210_i2s_reg_defaults[] = {
	{ TEGRA210_I2S_RX_INT_MASK, 0x00000003 },
	{ TEGRA210_I2S_RX_CIF_CTRL, 0x00007700 },
	{ TEGRA210_I2S_TX_INT_MASK, 0x00000003 },
	{ TEGRA210_I2S_TX_CIF_CTRL, 0x00007700 },
	{ TEGRA210_I2S_CG, 0x1 },
	{ TEGRA210_I2S_TIMING, 0x0000001f },
	{ TEGRA210_I2S_ENABLE, 0x1 },
	/*
	 * Below update does not have any effect on Tegra186 and Tegra194.
	 * On Tegra210, I2S4 has "i2s4a" and "i2s4b" pins and below update
	 * is required to select i2s4b for it to be functional for I2S
	 * operation.
	 */
	{ TEGRA210_I2S_CYA, 0x1 },
};

static const struct reg_default tegra264_i2s_reg_defaults[] = {
	{ TEGRA210_I2S_RX_INT_MASK, 0x00000003 },
	{ TEGRA210_I2S_RX_CIF_CTRL, 0x00003f00 },
	{ TEGRA264_I2S_TX_INT_MASK, 0x00000003 },
	{ TEGRA264_I2S_TX_CIF_CTRL, 0x00003f00 },
	{ TEGRA264_I2S_CG, 0x1 },
	{ TEGRA264_I2S_TIMING, 0x0000001f },
	{ TEGRA264_I2S_ENABLE, 0x1 },
	{ TEGRA264_I2S_RX_FIFO_WR_ACCESS_MODE, 0x1 },
	{ TEGRA264_I2S_TX_FIFO_RD_ACCESS_MODE, 0x1 },
};

static void tegra210_i2s_set_slot_ctrl(struct tegra210_i2s *i2s,
				       unsigned int total_slots,
				       unsigned int tx_slot_mask,
				       unsigned int rx_slot_mask)
{
	regmap_write(i2s->regmap, TEGRA210_I2S_SLOT_CTRL + i2s->soc_data->i2s_ctrl_offset,
		     total_slots - 1);
	regmap_write(i2s->regmap, TEGRA210_I2S_TX_SLOT_CTRL + i2s->soc_data->tx_offset,
		     tx_slot_mask);
	regmap_write(i2s->regmap, TEGRA210_I2S_RX_SLOT_CTRL, rx_slot_mask);
}

static int tegra210_i2s_set_clock_rate(struct device *dev,
				       unsigned int clock_rate)
{
	struct tegra210_i2s *i2s = dev_get_drvdata(dev);
	unsigned int val;
	int err;

	regmap_read(i2s->regmap, TEGRA210_I2S_CTRL + i2s->soc_data->i2s_ctrl_offset, &val);

	/* No need to set rates if I2S is being operated in slave */
	if (!(val & I2S_CTRL_MASTER_EN))
		return 0;

	err = clk_set_rate(i2s->clk_i2s, clock_rate);
	if (err) {
		dev_err(dev, "can't set I2S bit clock rate %u, err: %d\n",
			clock_rate, err);
		return err;
	}

	if (!IS_ERR(i2s->clk_sync_input)) {
		/*
		 * Other I/O modules in AHUB can use i2s bclk as reference
		 * clock. Below sets sync input clock rate as per bclk,
		 * which can be used as input to other I/O modules.
		 */
		err = clk_set_rate(i2s->clk_sync_input, clock_rate);
		if (err) {
			dev_err(dev,
				"can't set I2S sync input rate %u, err = %d\n",
				clock_rate, err);
			return err;
		}
	}

	return 0;
}

static int tegra210_i2s_sw_reset(struct snd_soc_component *compnt,
				 int stream)
{
	struct device *dev = compnt->dev;
	struct tegra210_i2s *i2s = dev_get_drvdata(dev);
	unsigned int reset_mask = I2S_SOFT_RESET_MASK;
	unsigned int reset_en = I2S_SOFT_RESET_EN;
	unsigned int reset_reg, cif_reg, stream_reg;
	unsigned int cif_ctrl, stream_ctrl, i2s_ctrl, val;
	int err;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		reset_reg = TEGRA210_I2S_RX_SOFT_RESET;
		cif_reg = TEGRA210_I2S_RX_CIF_CTRL;
		stream_reg = TEGRA210_I2S_RX_CTRL;
	} else {
		reset_reg = TEGRA210_I2S_TX_SOFT_RESET + i2s->soc_data->tx_offset;
		cif_reg = TEGRA210_I2S_TX_CIF_CTRL + i2s->soc_data->tx_offset;
		stream_reg = TEGRA210_I2S_TX_CTRL + i2s->soc_data->tx_offset;
	}

	/* Store CIF and I2S control values */
	regmap_read(i2s->regmap, cif_reg, &cif_ctrl);
	regmap_read(i2s->regmap, stream_reg, &stream_ctrl);
	regmap_read(i2s->regmap, TEGRA210_I2S_CTRL + i2s->soc_data->i2s_ctrl_offset, &i2s_ctrl);

	/* Reset to make sure the previous transactions are clean */
	regmap_update_bits(i2s->regmap, reset_reg, reset_mask, reset_en);

	err = regmap_read_poll_timeout(i2s->regmap, reset_reg, val,
				       !(val & reset_mask & reset_en),
				       10, 10000);
	if (err) {
		dev_err(dev, "timeout: failed to reset I2S for %s\n",
			snd_pcm_direction_name(stream));
		return err;
	}

	/* Restore CIF and I2S control values */
	regmap_write(i2s->regmap, cif_reg, cif_ctrl);
	regmap_write(i2s->regmap, stream_reg, stream_ctrl);
	regmap_write(i2s->regmap, TEGRA210_I2S_CTRL + i2s->soc_data->i2s_ctrl_offset, i2s_ctrl);

	return 0;
}

static int tegra210_i2s_init(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *compnt = snd_soc_dapm_to_component(w->dapm);
	struct device *dev = compnt->dev;
	struct tegra210_i2s *i2s = dev_get_drvdata(dev);
	unsigned int val, status_reg;
	int stream;
	int err;

	if (w->reg == TEGRA210_I2S_RX_ENABLE) {
		stream = SNDRV_PCM_STREAM_PLAYBACK;
		status_reg = TEGRA210_I2S_RX_STATUS;
	} else if (w->reg == (TEGRA210_I2S_TX_ENABLE + i2s->soc_data->tx_offset)) {
		stream = SNDRV_PCM_STREAM_CAPTURE;
		status_reg = TEGRA210_I2S_TX_STATUS + i2s->soc_data->tx_offset;
	} else {
		return -EINVAL;
	}

	/* Ensure I2S is in disabled state before new session */
	err = regmap_read_poll_timeout(i2s->regmap, status_reg, val,
				       !(val & I2S_EN_MASK & I2S_EN),
				       10, 10000);
	if (err) {
		dev_err(dev, "timeout: previous I2S %s is still active\n",
			snd_pcm_direction_name(stream));
		return err;
	}

	return tegra210_i2s_sw_reset(compnt, stream);
}

static int tegra210_i2s_runtime_suspend(struct device *dev)
{
	struct tegra210_i2s *i2s = dev_get_drvdata(dev);

	regcache_cache_only(i2s->regmap, true);
	regcache_mark_dirty(i2s->regmap);

	clk_disable_unprepare(i2s->clk_i2s);

	return 0;
}

static int tegra210_i2s_runtime_resume(struct device *dev)
{
	struct tegra210_i2s *i2s = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(i2s->clk_i2s);
	if (err) {
		dev_err(dev, "failed to enable I2S bit clock, err: %d\n", err);
		return err;
	}

	regcache_cache_only(i2s->regmap, false);
	regcache_sync(i2s->regmap);

	return 0;
}

static void tegra210_i2s_set_data_offset(struct tegra210_i2s *i2s,
					 unsigned int data_offset)
{
	/* Capture path */
	regmap_update_bits(i2s->regmap, TEGRA210_I2S_TX_CTRL + i2s->soc_data->tx_offset,
			   I2S_CTRL_DATA_OFFSET_MASK,
			   data_offset << I2S_DATA_SHIFT);

	/* Playback path */
	regmap_update_bits(i2s->regmap, TEGRA210_I2S_RX_CTRL,
			   I2S_CTRL_DATA_OFFSET_MASK,
			   data_offset << I2S_DATA_SHIFT);
}

static int tegra210_i2s_set_fmt(struct snd_soc_dai *dai,
				unsigned int fmt)
{
	struct tegra210_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned int mask, val;

	mask = I2S_CTRL_MASTER_EN_MASK;
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BC_FC:
		val = 0;
		break;
	case SND_SOC_DAIFMT_BP_FP:
		val = I2S_CTRL_MASTER_EN;
		break;
	default:
		return -EINVAL;
	}

	mask |= I2S_CTRL_FRAME_FMT_MASK | I2S_CTRL_LRCK_POL_MASK;
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		val |= I2S_CTRL_FRAME_FMT_FSYNC_MODE;
		val |= I2S_CTRL_LRCK_POL_HIGH;
		tegra210_i2s_set_data_offset(i2s, 1);
		break;
	case SND_SOC_DAIFMT_DSP_B:
		val |= I2S_CTRL_FRAME_FMT_FSYNC_MODE;
		val |= I2S_CTRL_LRCK_POL_HIGH;
		tegra210_i2s_set_data_offset(i2s, 0);
		break;
	/* I2S mode has data offset of 1 */
	case SND_SOC_DAIFMT_I2S:
		val |= I2S_CTRL_FRAME_FMT_LRCK_MODE;
		val |= I2S_CTRL_LRCK_POL_LOW;
		tegra210_i2s_set_data_offset(i2s, 1);
		break;
	/*
	 * For RJ mode data offset is dependent on the sample size
	 * and the bclk ratio, and so is set when hw_params is called.
	 */
	case SND_SOC_DAIFMT_RIGHT_J:
		val |= I2S_CTRL_FRAME_FMT_LRCK_MODE;
		val |= I2S_CTRL_LRCK_POL_HIGH;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val |= I2S_CTRL_FRAME_FMT_LRCK_MODE;
		val |= I2S_CTRL_LRCK_POL_HIGH;
		tegra210_i2s_set_data_offset(i2s, 0);
		break;
	default:
		return -EINVAL;
	}

	mask |= I2S_CTRL_EDGE_CTRL_MASK;
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		val |= I2S_CTRL_EDGE_CTRL_POS_EDGE;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		val |= I2S_CTRL_EDGE_CTRL_POS_EDGE;
		val ^= I2S_CTRL_LRCK_POL_MASK;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		val |= I2S_CTRL_EDGE_CTRL_NEG_EDGE;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		val |= I2S_CTRL_EDGE_CTRL_NEG_EDGE;
		val ^= I2S_CTRL_LRCK_POL_MASK;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, TEGRA210_I2S_CTRL + i2s->soc_data->i2s_ctrl_offset,
			   mask, val);

	i2s->dai_fmt = fmt & SND_SOC_DAIFMT_FORMAT_MASK;

	return 0;
}

static int tegra210_i2s_set_tdm_slot(struct snd_soc_dai *dai,
				     unsigned int tx_mask, unsigned int rx_mask,
				     int slots, int slot_width)
{
	struct tegra210_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	/* Copy the required tx and rx mask */
	i2s->tx_mask = (tx_mask > i2s->soc_data->slot_mask) ?
		       i2s->soc_data->slot_mask : tx_mask;
	i2s->rx_mask = (rx_mask > i2s->soc_data->slot_mask) ?
		       i2s->soc_data->slot_mask : rx_mask;

	return 0;
}

static int tegra210_i2s_get_loopback(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);

	ucontrol->value.integer.value[0] = i2s->loopback;

	return 0;
}

static int tegra210_i2s_put_loopback(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);
	int value = ucontrol->value.integer.value[0];

	if (value == i2s->loopback)
		return 0;

	i2s->loopback = value;

	regmap_update_bits(i2s->regmap, TEGRA210_I2S_CTRL + i2s->soc_data->i2s_ctrl_offset,
			   I2S_CTRL_LPBK_MASK, i2s->loopback << I2S_CTRL_LPBK_SHIFT);

	return 1;
}

static int tegra210_i2s_get_fsync_width(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);

	ucontrol->value.integer.value[0] = i2s->fsync_width;

	return 0;
}

static int tegra210_i2s_put_fsync_width(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);
	int value = ucontrol->value.integer.value[0];

	if (value == i2s->fsync_width)
		return 0;

	i2s->fsync_width = value;

	/*
	 * Frame sync width is used only for FSYNC modes and not
	 * applicable for LRCK modes. Reset value for this field is "0",
	 * which means the width is one bit clock wide.
	 * The width requirement may depend on the codec and in such
	 * cases mixer control is used to update custom values. A value
	 * of "N" here means, width is "N + 1" bit clock wide.
	 */
	regmap_update_bits(i2s->regmap, TEGRA210_I2S_CTRL + i2s->soc_data->i2s_ctrl_offset,
			   i2s->soc_data->fsync_width_mask,
			   i2s->fsync_width << i2s->soc_data->fsync_width_shift);

	return 1;
}

static int tegra210_i2s_cget_stereo_to_mono(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);

	ucontrol->value.enumerated.item[0] = i2s->stereo_to_mono[I2S_TX_PATH];

	return 0;
}

static int tegra210_i2s_cput_stereo_to_mono(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);
	unsigned int value = ucontrol->value.enumerated.item[0];

	if (value == i2s->stereo_to_mono[I2S_TX_PATH])
		return 0;

	i2s->stereo_to_mono[I2S_TX_PATH] = value;

	return 1;
}

static int tegra210_i2s_cget_mono_to_stereo(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);

	ucontrol->value.enumerated.item[0] = i2s->mono_to_stereo[I2S_TX_PATH];

	return 0;
}

static int tegra210_i2s_cput_mono_to_stereo(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);
	unsigned int value = ucontrol->value.enumerated.item[0];

	if (value == i2s->mono_to_stereo[I2S_TX_PATH])
		return 0;

	i2s->mono_to_stereo[I2S_TX_PATH] = value;

	return 1;
}

static int tegra210_i2s_pget_stereo_to_mono(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);

	ucontrol->value.enumerated.item[0] = i2s->stereo_to_mono[I2S_RX_PATH];

	return 0;
}

static int tegra210_i2s_pput_stereo_to_mono(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);
	unsigned int value = ucontrol->value.enumerated.item[0];

	if (value == i2s->stereo_to_mono[I2S_RX_PATH])
		return 0;

	i2s->stereo_to_mono[I2S_RX_PATH] = value;

	return 1;
}

static int tegra210_i2s_pget_mono_to_stereo(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);

	ucontrol->value.enumerated.item[0] = i2s->mono_to_stereo[I2S_RX_PATH];

	return 0;
}

static int tegra210_i2s_pput_mono_to_stereo(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);
	unsigned int value = ucontrol->value.enumerated.item[0];

	if (value == i2s->mono_to_stereo[I2S_RX_PATH])
		return 0;

	i2s->mono_to_stereo[I2S_RX_PATH] = value;

	return 1;
}

static int tegra210_i2s_pget_fifo_th(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);

	ucontrol->value.integer.value[0] = i2s->rx_fifo_th;

	return 0;
}

static int tegra210_i2s_pput_fifo_th(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);
	int value = ucontrol->value.integer.value[0];

	if (value == i2s->rx_fifo_th)
		return 0;

	i2s->rx_fifo_th = value;

	return 1;
}

static int tegra210_i2s_get_bclk_ratio(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);

	ucontrol->value.integer.value[0] = i2s->bclk_ratio;

	return 0;
}

static int tegra210_i2s_put_bclk_ratio(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *compnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_i2s *i2s = snd_soc_component_get_drvdata(compnt);
	int value = ucontrol->value.integer.value[0];

	if (value == i2s->bclk_ratio)
		return 0;

	i2s->bclk_ratio = value;

	return 1;
}

static int tegra210_i2s_set_dai_bclk_ratio(struct snd_soc_dai *dai,
					   unsigned int ratio)
{
	struct tegra210_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	i2s->bclk_ratio = ratio;

	return 0;
}

static int tegra210_i2s_set_timing_params(struct device *dev,
					  unsigned int sample_size,
					  unsigned int srate,
					  unsigned int channels)
{
	struct tegra210_i2s *i2s = dev_get_drvdata(dev);
	unsigned int val, bit_count, bclk_rate, num_bclk = sample_size;
	int err;

	if (i2s->bclk_ratio)
		num_bclk *= i2s->bclk_ratio;

	if (i2s->dai_fmt == SND_SOC_DAIFMT_RIGHT_J)
		tegra210_i2s_set_data_offset(i2s, num_bclk - sample_size);

	/* I2S bit clock rate */
	bclk_rate = srate * channels * num_bclk;

	err = tegra210_i2s_set_clock_rate(dev, bclk_rate);
	if (err) {
		dev_err(dev, "can't set I2S bit clock rate %u, err: %d\n",
			bclk_rate, err);
		return err;
	}

	regmap_read(i2s->regmap, TEGRA210_I2S_CTRL + i2s->soc_data->i2s_ctrl_offset, &val);

	/*
	 * For LRCK mode, channel bit count depends on number of bit clocks
	 * on the left channel, where as for FSYNC mode bit count depends on
	 * the number of bit clocks in both left and right channels for DSP
	 * mode or the number of bit clocks in one TDM frame.
	 *
	 */
	switch (val & I2S_CTRL_FRAME_FMT_MASK) {
	case I2S_CTRL_FRAME_FMT_LRCK_MODE:
		bit_count = (bclk_rate / (srate * 2)) - 1;
		break;
	case I2S_CTRL_FRAME_FMT_FSYNC_MODE:
		bit_count = (bclk_rate / srate) - 1;

		tegra210_i2s_set_slot_ctrl(i2s, channels,
					   i2s->tx_mask, i2s->rx_mask);
		break;
	default:
		dev_err(dev, "invalid I2S frame format\n");
		return -EINVAL;
	}

	if (bit_count > I2S_TIMING_CH_BIT_CNT_MASK) {
		dev_err(dev, "invalid I2S channel bit count %u\n", bit_count);
		return -EINVAL;
	}

	regmap_write(i2s->regmap, TEGRA210_I2S_TIMING + i2s->soc_data->i2s_ctrl_offset,
		     bit_count << I2S_TIMING_CH_BIT_CNT_SHIFT);

	return 0;
}

static int tegra210_i2s_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct device *dev = dai->dev;
	struct tegra210_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned int sample_size, channels, srate, val, reg, path;
	struct tegra_cif_conf cif_conf;
	snd_pcm_format_t sample_format;

	memset(&cif_conf, 0, sizeof(struct tegra_cif_conf));

	channels = params_channels(params);
	if (channels < 1) {
		dev_err(dev, "invalid I2S %d channel configuration\n",
			channels);
		return -EINVAL;
	}

	cif_conf.audio_ch = channels;
	cif_conf.client_ch = channels;
	if (i2s->client_channels)
		cif_conf.client_ch = i2s->client_channels;

	/* AHUB CIF Audio bits configs */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		cif_conf.audio_bits = TEGRA_ACIF_BITS_8;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		cif_conf.audio_bits = TEGRA_ACIF_BITS_16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		cif_conf.audio_bits = TEGRA_ACIF_BITS_32;
		break;
	default:
		dev_err(dev, "unsupported params audio bit format!\n");
		return -EOPNOTSUPP;
	}

	sample_format = params_format(params);
	if (i2s->client_sample_format >= 0)
		sample_format = (snd_pcm_format_t)i2s->client_sample_format;

	/*
	 * Format of the I2S for sending/receiving the audio
	 * to/from external device.
	 */
	switch (sample_format) {
	case SNDRV_PCM_FORMAT_S8:
		val = I2S_BITS_8;
		sample_size = 8;
		cif_conf.client_bits = TEGRA_ACIF_BITS_8;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		val = I2S_BITS_16;
		sample_size = 16;
		cif_conf.client_bits = TEGRA_ACIF_BITS_16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val = I2S_BITS_24;
		sample_size = 32;
		cif_conf.client_bits = TEGRA_ACIF_BITS_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		val = I2S_BITS_32;
		sample_size = 32;
		cif_conf.client_bits = TEGRA_ACIF_BITS_32;
		break;
	default:
		dev_err(dev, "unsupported client bit format!\n");
		return -EOPNOTSUPP;
	}

	/* Program sample size */
	regmap_update_bits(i2s->regmap, TEGRA210_I2S_CTRL + i2s->soc_data->i2s_ctrl_offset,
			   I2S_CTRL_BIT_SIZE_MASK, val);

	srate = params_rate(params);

	/* For playback I2S RX-CIF and for capture TX-CIF is used */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		path = I2S_RX_PATH;
	else
		path = I2S_TX_PATH;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		unsigned int max_th;

		/* FIFO threshold in terms of frames */
		max_th = (I2S_RX_FIFO_DEPTH / cif_conf.audio_ch) - 1;

		if (i2s->rx_fifo_th > max_th)
			i2s->rx_fifo_th = max_th;

		cif_conf.threshold = i2s->rx_fifo_th;

		reg = TEGRA210_I2S_RX_CIF_CTRL;
	} else {
		reg = TEGRA210_I2S_TX_CIF_CTRL + i2s->soc_data->tx_offset;
	}

	cif_conf.mono_conv = i2s->mono_to_stereo[path];
	cif_conf.stereo_conv = i2s->stereo_to_mono[path];

	if (i2s->soc_data->max_ch == TEGRA264_I2S_MAX_CHANNEL)
		tegra264_set_cif(i2s->regmap, reg, &cif_conf);
	else
		tegra_set_cif(i2s->regmap, reg, &cif_conf);

	return tegra210_i2s_set_timing_params(dev, sample_size, srate,
					      cif_conf.client_ch);
}

static const struct snd_soc_dai_ops tegra210_i2s_dai_ops = {
	.set_fmt	= tegra210_i2s_set_fmt,
	.hw_params	= tegra210_i2s_hw_params,
	.set_bclk_ratio	= tegra210_i2s_set_dai_bclk_ratio,
	.set_tdm_slot	= tegra210_i2s_set_tdm_slot,
};

static struct snd_soc_dai_driver tegra210_i2s_dais[] = {
	{
		.name = "I2S-CIF",
		.playback = {
			.stream_name = "CIF-Playback",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "CIF-Capture",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
	},
	{
		.name = "I2S-DAP",
		.playback = {
			.stream_name = "DAP-Playback",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "DAP-Capture",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &tegra210_i2s_dai_ops,
		.symmetric_rate = 1,
	},
};

static const char * const tegra210_i2s_stereo_conv_text[] = {
	"CH0", "CH1", "AVG",
};

static const char * const tegra210_i2s_mono_conv_text[] = {
	"Zero", "Copy",
};

static const struct soc_enum tegra210_i2s_mono_conv_enum =
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(tegra210_i2s_mono_conv_text),
			tegra210_i2s_mono_conv_text);

static const struct soc_enum tegra210_i2s_stereo_conv_enum =
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(tegra210_i2s_stereo_conv_text),
			tegra210_i2s_stereo_conv_text);

static const struct snd_kcontrol_new tegra210_i2s_controls[] = {
	SOC_SINGLE_EXT("Loopback", 0, 0, 1, 0, tegra210_i2s_get_loopback,
		       tegra210_i2s_put_loopback),
	SOC_SINGLE_EXT("FSYNC Width", 0, 0, 255, 0,
		       tegra210_i2s_get_fsync_width,
		       tegra210_i2s_put_fsync_width),
	SOC_ENUM_EXT("Capture Stereo To Mono", tegra210_i2s_stereo_conv_enum,
		     tegra210_i2s_cget_stereo_to_mono,
		     tegra210_i2s_cput_stereo_to_mono),
	SOC_ENUM_EXT("Capture Mono To Stereo", tegra210_i2s_mono_conv_enum,
		     tegra210_i2s_cget_mono_to_stereo,
		     tegra210_i2s_cput_mono_to_stereo),
	SOC_ENUM_EXT("Playback Stereo To Mono", tegra210_i2s_stereo_conv_enum,
		     tegra210_i2s_pget_mono_to_stereo,
		     tegra210_i2s_pput_mono_to_stereo),
	SOC_ENUM_EXT("Playback Mono To Stereo", tegra210_i2s_mono_conv_enum,
		     tegra210_i2s_pget_stereo_to_mono,
		     tegra210_i2s_pput_stereo_to_mono),
	SOC_SINGLE_EXT("Playback FIFO Threshold", 0, 0, I2S_RX_FIFO_DEPTH - 1,
		       0, tegra210_i2s_pget_fifo_th, tegra210_i2s_pput_fifo_th),
	SOC_SINGLE_EXT("BCLK Ratio", 0, 0, INT_MAX, 0,
		       tegra210_i2s_get_bclk_ratio,
		       tegra210_i2s_put_bclk_ratio),
};

#define TEGRA_I2S_WIDGETS(tx_enable_reg) \
	SND_SOC_DAPM_AIF_IN_E("RX", NULL, 0, TEGRA210_I2S_RX_ENABLE, \
			      0, 0, tegra210_i2s_init, SND_SOC_DAPM_PRE_PMU), \
	SND_SOC_DAPM_AIF_OUT_E("TX", NULL, 0, tx_enable_reg, \
			       0, 0, tegra210_i2s_init, SND_SOC_DAPM_PRE_PMU), \
	SND_SOC_DAPM_MIC("MIC", NULL), \
	SND_SOC_DAPM_SPK("SPK", NULL),

static const struct snd_soc_dapm_widget tegra210_i2s_widgets[] = {
	TEGRA_I2S_WIDGETS(TEGRA210_I2S_TX_ENABLE)
};

static const struct snd_soc_dapm_widget tegra264_i2s_widgets[] = {
	TEGRA_I2S_WIDGETS(TEGRA264_I2S_TX_ENABLE)
};

static const struct snd_soc_dapm_route tegra210_i2s_routes[] = {
	/* Playback route from XBAR */
	{ "XBAR-Playback",	NULL,	"XBAR-TX" },
	{ "CIF-Playback",	NULL,	"XBAR-Playback" },
	{ "RX",			NULL,	"CIF-Playback" },
	{ "DAP-Playback",	NULL,	"RX" },
	{ "SPK",		NULL,	"DAP-Playback" },
	/* Capture route to XBAR */
	{ "XBAR-RX",		NULL,	"XBAR-Capture" },
	{ "XBAR-Capture",	NULL,	"CIF-Capture" },
	{ "CIF-Capture",	NULL,	"TX" },
	{ "TX",			NULL,	"DAP-Capture" },
	{ "DAP-Capture",	NULL,	"MIC" },
};

static const struct snd_soc_component_driver tegra210_i2s_cmpnt = {
	.dapm_widgets		= tegra210_i2s_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tegra210_i2s_widgets),
	.dapm_routes		= tegra210_i2s_routes,
	.num_dapm_routes	= ARRAY_SIZE(tegra210_i2s_routes),
	.controls		= tegra210_i2s_controls,
	.num_controls		= ARRAY_SIZE(tegra210_i2s_controls),
};

static const struct snd_soc_component_driver tegra264_i2s_cmpnt = {
	.dapm_widgets		= tegra264_i2s_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tegra264_i2s_widgets),
	.dapm_routes		= tegra210_i2s_routes,
	.num_dapm_routes	= ARRAY_SIZE(tegra210_i2s_routes),
	.controls		= tegra210_i2s_controls,
	.num_controls		= ARRAY_SIZE(tegra210_i2s_controls),
};

static bool tegra210_i2s_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_I2S_RX_ENABLE ... TEGRA210_I2S_RX_SOFT_RESET:
	case TEGRA210_I2S_RX_INT_MASK ... TEGRA210_I2S_RX_CLK_TRIM:
	case TEGRA210_I2S_TX_ENABLE ... TEGRA210_I2S_TX_SOFT_RESET:
	case TEGRA210_I2S_TX_INT_MASK ... TEGRA210_I2S_TX_CLK_TRIM:
	case TEGRA210_I2S_ENABLE ... TEGRA210_I2S_CG:
	case TEGRA210_I2S_CTRL ... TEGRA210_I2S_CYA:
		return true;
	default:
		return false;
	}
}

static bool tegra210_i2s_rd_reg(struct device *dev, unsigned int reg)
{
	if (tegra210_i2s_wr_reg(dev, reg))
		return true;

	switch (reg) {
	case TEGRA210_I2S_RX_STATUS:
	case TEGRA210_I2S_RX_INT_STATUS:
	case TEGRA210_I2S_RX_CIF_FIFO_STATUS:
	case TEGRA210_I2S_TX_STATUS:
	case TEGRA210_I2S_TX_INT_STATUS:
	case TEGRA210_I2S_TX_CIF_FIFO_STATUS:
	case TEGRA210_I2S_STATUS:
	case TEGRA210_I2S_INT_STATUS:
		return true;
	default:
		return false;
	}
}

static bool tegra210_i2s_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_I2S_RX_STATUS:
	case TEGRA210_I2S_RX_INT_STATUS:
	case TEGRA210_I2S_RX_CIF_FIFO_STATUS:
	case TEGRA210_I2S_TX_STATUS:
	case TEGRA210_I2S_TX_INT_STATUS:
	case TEGRA210_I2S_TX_CIF_FIFO_STATUS:
	case TEGRA210_I2S_STATUS:
	case TEGRA210_I2S_INT_STATUS:
	case TEGRA210_I2S_RX_SOFT_RESET:
	case TEGRA210_I2S_TX_SOFT_RESET:
		return true;
	default:
		return false;
	}
}

static bool tegra264_i2s_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_I2S_RX_ENABLE ... TEGRA210_I2S_RX_SOFT_RESET:
	case TEGRA210_I2S_RX_INT_MASK ... TEGRA264_I2S_RX_CYA:
	case TEGRA264_I2S_TX_ENABLE ... TEGRA264_I2S_TX_SOFT_RESET:
	case TEGRA264_I2S_TX_INT_MASK ... TEGRA264_I2S_TX_FIFO_RD_ACCESS_MODE:
	case TEGRA264_I2S_TX_FIFO_THRESHOLD ... TEGRA264_I2S_TX_CYA:
	case TEGRA264_I2S_ENABLE ... TEGRA264_I2S_CG:
	case TEGRA264_I2S_INT_SET ... TEGRA264_I2S_INT_MASK:
	case TEGRA264_I2S_CTRL ... TEGRA264_I2S_CYA:
		return true;
	default:
		return false;
	};
}

static bool tegra264_i2s_rd_reg(struct device *dev, unsigned int reg)
{
	if (tegra264_i2s_wr_reg(dev, reg))
		return true;

	switch (reg) {
	case TEGRA210_I2S_RX_STATUS:
	case TEGRA210_I2S_RX_INT_STATUS:
	case TEGRA264_I2S_RX_CIF_FIFO_STATUS:
	case TEGRA264_I2S_TX_STATUS:
	case TEGRA264_I2S_TX_INT_STATUS:
	case TEGRA264_I2S_TX_FIFO_RD_DATA:
	case TEGRA264_I2S_TX_CIF_FIFO_STATUS:
	case TEGRA264_I2S_STATUS:
	case TEGRA264_I2S_INT_STATUS:
	case TEGRA264_I2S_PIO_MODE_ENABLE:
	case TEGRA264_I2S_PAD_MACRO_STATUS:
		return true;
	default:
		return false;
	};
}

static bool tegra264_i2s_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_I2S_RX_SOFT_RESET:
	case TEGRA210_I2S_RX_STATUS:
	case TEGRA210_I2S_RX_INT_STATUS:
	case TEGRA264_I2S_RX_CIF_FIFO_STATUS:
	case TEGRA264_I2S_TX_STATUS:
	case TEGRA264_I2S_TX_INT_STATUS:
	case TEGRA264_I2S_TX_FIFO_RD_DATA:
	case TEGRA264_I2S_TX_CIF_FIFO_STATUS:
	case TEGRA264_I2S_STATUS:
	case TEGRA264_I2S_INT_STATUS:
	case TEGRA264_I2S_TX_SOFT_RESET:
	case TEGRA264_I2S_PAD_MACRO_STATUS:
		return true;
	default:
		return false;
	};
}

static const struct regmap_config tegra210_regmap_conf = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= TEGRA210_I2S_CYA,
	.writeable_reg		= tegra210_i2s_wr_reg,
	.readable_reg		= tegra210_i2s_rd_reg,
	.volatile_reg		= tegra210_i2s_volatile_reg,
	.reg_defaults		= tegra210_i2s_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(tegra210_i2s_reg_defaults),
	.cache_type		= REGCACHE_FLAT,
};

/*
 * The AHUB HW modules are interconnected with CIF which are capable of
 * supporting Channel and Sample bit format conversion. This needs different
 * CIF Audio and client configuration. As one of the config comes from
 * params_channels() or params_format(), the extra configuration is passed from
 * CIF Port of DT I2S node which can help to perform this conversion.
 *
 *    4ch          audio = 4ch      client = 2ch       2ch
 *   -----> ADMAIF -----------> CIF -------------> I2S ---->
 */
static void tegra210_parse_client_convert(struct device *dev)
{
	struct tegra210_i2s *i2s = dev_get_drvdata(dev);
	struct device_node *ports, *ep;
	struct simple_util_data data = {};
	int cif_port = 0;

	ports = of_get_child_by_name(dev->of_node, "ports");
	if (ports) {
		ep = of_graph_get_endpoint_by_regs(ports, cif_port, -1);
		if (ep) {
			simple_util_parse_convert(ep, NULL, &data);
			of_node_put(ep);
		}
		of_node_put(ports);
	}

	if (data.convert_channels)
		i2s->client_channels = data.convert_channels;

	if (data.convert_sample_format)
		i2s->client_sample_format = simple_util_get_sample_fmt(&data);
}

static const struct regmap_config tegra264_regmap_conf = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= TEGRA264_I2S_PAD_MACRO_STATUS,
	.writeable_reg		= tegra264_i2s_wr_reg,
	.readable_reg		= tegra264_i2s_rd_reg,
	.volatile_reg		= tegra264_i2s_volatile_reg,
	.reg_defaults		= tegra264_i2s_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(tegra264_i2s_reg_defaults),
	.cache_type		= REGCACHE_FLAT,
};

static int tegra210_i2s_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra210_i2s *i2s;
	void __iomem *regs;
	int err, id;

	i2s = devm_kzalloc(dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	i2s->soc_data = of_device_get_match_data(&pdev->dev);
	i2s->rx_fifo_th = DEFAULT_I2S_RX_FIFO_THRESHOLD;
	i2s->tx_mask = i2s->soc_data->slot_mask;
	i2s->rx_mask = i2s->soc_data->slot_mask;
	i2s->loopback = false;
	i2s->client_sample_format = -EINVAL;

	dev_set_drvdata(dev, i2s);

	i2s->clk_i2s = devm_clk_get(dev, "i2s");
	if (IS_ERR(i2s->clk_i2s)) {
		dev_err(dev, "can't retrieve I2S bit clock\n");
		return PTR_ERR(i2s->clk_i2s);
	}

	/*
	 * Not an error, as this clock is needed only when some other I/O
	 * requires input clock from current I2S instance, which is
	 * configurable from DT.
	 */
	i2s->clk_sync_input = devm_clk_get(dev, "sync_input");
	if (IS_ERR(i2s->clk_sync_input))
		dev_dbg(dev, "can't retrieve I2S sync input clock\n");

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	i2s->regmap = devm_regmap_init_mmio(dev, regs,
					    i2s->soc_data->regmap_conf);
	if (IS_ERR(i2s->regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(i2s->regmap);
	}

	tegra210_parse_client_convert(dev);

	regcache_cache_only(i2s->regmap, true);

	/* Update the dais max channel as per soc */
	for (id = 0; id < ARRAY_SIZE(tegra210_i2s_dais); id++) {
		tegra210_i2s_dais[id].playback.channels_max = i2s->soc_data->max_ch;
		tegra210_i2s_dais[id].capture.channels_max = i2s->soc_data->max_ch;
	}

	err = devm_snd_soc_register_component(dev, i2s->soc_data->i2s_cmpnt,
					      tegra210_i2s_dais,
					      ARRAY_SIZE(tegra210_i2s_dais));
	if (err) {
		dev_err(dev, "can't register I2S component, err: %d\n", err);
		return err;
	}

	pm_runtime_enable(dev);

	return 0;
}

static void tegra210_i2s_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static const struct dev_pm_ops tegra210_i2s_pm_ops = {
	RUNTIME_PM_OPS(tegra210_i2s_runtime_suspend,
		       tegra210_i2s_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
};

static const struct tegra_i2s_soc_data soc_data_tegra210 = {
	.regmap_conf		= &tegra210_regmap_conf,
	.i2s_cmpnt		= &tegra210_i2s_cmpnt,
	.max_ch			= TEGRA210_I2S_MAX_CHANNEL,
	.tx_offset		= TEGRA210_I2S_TX_OFFSET,
	.i2s_ctrl_offset	= TEGRA210_I2S_CTRL_OFFSET,
	.fsync_width_mask	= I2S_CTRL_FSYNC_WIDTH_MASK,
	.fsync_width_shift	= I2S_FSYNC_WIDTH_SHIFT,
	.slot_mask		= DEFAULT_I2S_SLOT_MASK,
};

static const struct tegra_i2s_soc_data soc_data_tegra264 = {
	.regmap_conf		= &tegra264_regmap_conf,
	.i2s_cmpnt		= &tegra264_i2s_cmpnt,
	.max_ch			= TEGRA264_I2S_MAX_CHANNEL,
	.tx_offset		= TEGRA264_I2S_TX_OFFSET,
	.i2s_ctrl_offset	= TEGRA264_I2S_CTRL_OFFSET,
	.fsync_width_mask	= TEGRA264_I2S_CTRL_FSYNC_WIDTH_MASK,
	.fsync_width_shift	= TEGRA264_I2S_FSYNC_WIDTH_SHIFT,
	.slot_mask		= TEGRA264_DEFAULT_I2S_SLOT_MASK,
};

static const struct of_device_id tegra210_i2s_of_match[] = {
	{ .compatible = "nvidia,tegra210-i2s", .data = &soc_data_tegra210 },
	{ .compatible = "nvidia,tegra264-i2s", .data = &soc_data_tegra264 },
	{},
};
MODULE_DEVICE_TABLE(of, tegra210_i2s_of_match);

static struct platform_driver tegra210_i2s_driver = {
	.driver = {
		.name = "tegra210-i2s",
		.of_match_table = tegra210_i2s_of_match,
		.pm = pm_ptr(&tegra210_i2s_pm_ops),
	},
	.probe = tegra210_i2s_probe,
	.remove = tegra210_i2s_remove,
};
module_platform_driver(tegra210_i2s_driver)

MODULE_AUTHOR("Songhee Baek <sbaek@nvidia.com>");
MODULE_DESCRIPTION("Tegra210 ASoC I2S driver");
MODULE_LICENSE("GPL v2");
