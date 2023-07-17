// SPDX-License-Identifier: GPL-2.0-only
//
// tegra210_i2s.c - Tegra210 I2S driver
//
// Copyright (c) 2020 NVIDIA CORPORATION.  All rights reserved.

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm_params.h>
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

static void tegra210_i2s_set_slot_ctrl(struct regmap *regmap,
				       unsigned int total_slots,
				       unsigned int tx_slot_mask,
				       unsigned int rx_slot_mask)
{
	regmap_write(regmap, TEGRA210_I2S_SLOT_CTRL, total_slots - 1);
	regmap_write(regmap, TEGRA210_I2S_TX_SLOT_CTRL, tx_slot_mask);
	regmap_write(regmap, TEGRA210_I2S_RX_SLOT_CTRL, rx_slot_mask);
}

static int tegra210_i2s_set_clock_rate(struct device *dev,
				       unsigned int clock_rate)
{
	struct tegra210_i2s *i2s = dev_get_drvdata(dev);
	unsigned int val;
	int err;

	regmap_read(i2s->regmap, TEGRA210_I2S_CTRL, &val);

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
				 bool is_playback)
{
	struct device *dev = compnt->dev;
	struct tegra210_i2s *i2s = dev_get_drvdata(dev);
	unsigned int reset_mask = I2S_SOFT_RESET_MASK;
	unsigned int reset_en = I2S_SOFT_RESET_EN;
	unsigned int reset_reg, cif_reg, stream_reg;
	unsigned int cif_ctrl, stream_ctrl, i2s_ctrl, val;
	int err;

	if (is_playback) {
		reset_reg = TEGRA210_I2S_RX_SOFT_RESET;
		cif_reg = TEGRA210_I2S_RX_CIF_CTRL;
		stream_reg = TEGRA210_I2S_RX_CTRL;
	} else {
		reset_reg = TEGRA210_I2S_TX_SOFT_RESET;
		cif_reg = TEGRA210_I2S_TX_CIF_CTRL;
		stream_reg = TEGRA210_I2S_TX_CTRL;
	}

	/* Store CIF and I2S control values */
	regmap_read(i2s->regmap, cif_reg, &cif_ctrl);
	regmap_read(i2s->regmap, stream_reg, &stream_ctrl);
	regmap_read(i2s->regmap, TEGRA210_I2S_CTRL, &i2s_ctrl);

	/* Reset to make sure the previous transactions are clean */
	regmap_update_bits(i2s->regmap, reset_reg, reset_mask, reset_en);

	err = regmap_read_poll_timeout(i2s->regmap, reset_reg, val,
				       !(val & reset_mask & reset_en),
				       10, 10000);
	if (err) {
		dev_err(dev, "timeout: failed to reset I2S for %s\n",
			is_playback ? "playback" : "capture");
		return err;
	}

	/* Restore CIF and I2S control values */
	regmap_write(i2s->regmap, cif_reg, cif_ctrl);
	regmap_write(i2s->regmap, stream_reg, stream_ctrl);
	regmap_write(i2s->regmap, TEGRA210_I2S_CTRL, i2s_ctrl);

	return 0;
}

static int tegra210_i2s_init(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *compnt = snd_soc_dapm_to_component(w->dapm);
	struct device *dev = compnt->dev;
	struct tegra210_i2s *i2s = dev_get_drvdata(dev);
	unsigned int val, status_reg;
	bool is_playback;
	int err;

	switch (w->reg) {
	case TEGRA210_I2S_RX_ENABLE:
		is_playback = true;
		status_reg = TEGRA210_I2S_RX_STATUS;
		break;
	case TEGRA210_I2S_TX_ENABLE:
		is_playback = false;
		status_reg = TEGRA210_I2S_TX_STATUS;
		break;
	default:
		return -EINVAL;
	}

	/* Ensure I2S is in disabled state before new session */
	err = regmap_read_poll_timeout(i2s->regmap, status_reg, val,
				       !(val & I2S_EN_MASK & I2S_EN),
				       10, 10000);
	if (err) {
		dev_err(dev, "timeout: previous I2S %s is still active\n",
			is_playback ? "playback" : "capture");
		return err;
	}

	return tegra210_i2s_sw_reset(compnt, is_playback);
}

static int __maybe_unused tegra210_i2s_runtime_suspend(struct device *dev)
{
	struct tegra210_i2s *i2s = dev_get_drvdata(dev);

	regcache_cache_only(i2s->regmap, true);
	regcache_mark_dirty(i2s->regmap);

	clk_disable_unprepare(i2s->clk_i2s);

	return 0;
}

static int __maybe_unused tegra210_i2s_runtime_resume(struct device *dev)
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
	regmap_update_bits(i2s->regmap, TEGRA210_I2S_TX_CTRL,
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

	regmap_update_bits(i2s->regmap, TEGRA210_I2S_CTRL, mask, val);

	i2s->dai_fmt = fmt & SND_SOC_DAIFMT_FORMAT_MASK;

	return 0;
}

static int tegra210_i2s_set_tdm_slot(struct snd_soc_dai *dai,
				     unsigned int tx_mask, unsigned int rx_mask,
				     int slots, int slot_width)
{
	struct tegra210_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	/* Copy the required tx and rx mask */
	i2s->tx_mask = (tx_mask > DEFAULT_I2S_SLOT_MASK) ?
		       DEFAULT_I2S_SLOT_MASK : tx_mask;
	i2s->rx_mask = (rx_mask > DEFAULT_I2S_SLOT_MASK) ?
		       DEFAULT_I2S_SLOT_MASK : rx_mask;

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

	regmap_update_bits(i2s->regmap, TEGRA210_I2S_CTRL, I2S_CTRL_LPBK_MASK,
			   i2s->loopback << I2S_CTRL_LPBK_SHIFT);

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
	regmap_update_bits(i2s->regmap, TEGRA210_I2S_CTRL,
			   I2S_CTRL_FSYNC_WIDTH_MASK,
			   i2s->fsync_width << I2S_FSYNC_WIDTH_SHIFT);

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

	regmap_read(i2s->regmap, TEGRA210_I2S_CTRL, &val);

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

		tegra210_i2s_set_slot_ctrl(i2s->regmap, channels,
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

	regmap_write(i2s->regmap, TEGRA210_I2S_TIMING,
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

	memset(&cif_conf, 0, sizeof(struct tegra_cif_conf));

	channels = params_channels(params);
	if (channels < 1) {
		dev_err(dev, "invalid I2S %d channel configuration\n",
			channels);
		return -EINVAL;
	}

	cif_conf.audio_ch = channels;
	cif_conf.client_ch = channels;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		val = I2S_BITS_8;
		sample_size = 8;
		cif_conf.audio_bits = TEGRA_ACIF_BITS_8;
		cif_conf.client_bits = TEGRA_ACIF_BITS_8;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		val = I2S_BITS_16;
		sample_size = 16;
		cif_conf.audio_bits = TEGRA_ACIF_BITS_16;
		cif_conf.client_bits = TEGRA_ACIF_BITS_16;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		val = I2S_BITS_32;
		sample_size = 32;
		cif_conf.audio_bits = TEGRA_ACIF_BITS_32;
		cif_conf.client_bits = TEGRA_ACIF_BITS_32;
		break;
	default:
		dev_err(dev, "unsupported format!\n");
		return -EOPNOTSUPP;
	}

	/* Program sample size */
	regmap_update_bits(i2s->regmap, TEGRA210_I2S_CTRL,
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
		reg = TEGRA210_I2S_TX_CIF_CTRL;
	}

	cif_conf.mono_conv = i2s->mono_to_stereo[path];
	cif_conf.stereo_conv = i2s->stereo_to_mono[path];

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
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "CIF-Capture",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
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
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "DAP-Capture",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
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

static const struct snd_soc_dapm_widget tegra210_i2s_widgets[] = {
	SND_SOC_DAPM_AIF_IN_E("RX", NULL, 0, TEGRA210_I2S_RX_ENABLE,
			      0, 0, tegra210_i2s_init, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_AIF_OUT_E("TX", NULL, 0, TEGRA210_I2S_TX_ENABLE,
			       0, 0, tegra210_i2s_init, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_MIC("MIC", NULL),
	SND_SOC_DAPM_SPK("SPK", NULL),
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

static const struct regmap_config tegra210_i2s_regmap_config = {
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

static int tegra210_i2s_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra210_i2s *i2s;
	void __iomem *regs;
	int err;

	i2s = devm_kzalloc(dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	i2s->rx_fifo_th = DEFAULT_I2S_RX_FIFO_THRESHOLD;
	i2s->tx_mask = DEFAULT_I2S_SLOT_MASK;
	i2s->rx_mask = DEFAULT_I2S_SLOT_MASK;
	i2s->loopback = false;

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
					    &tegra210_i2s_regmap_config);
	if (IS_ERR(i2s->regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(i2s->regmap);
	}

	regcache_cache_only(i2s->regmap, true);

	err = devm_snd_soc_register_component(dev, &tegra210_i2s_cmpnt,
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
	SET_RUNTIME_PM_OPS(tegra210_i2s_runtime_suspend,
			   tegra210_i2s_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static const struct of_device_id tegra210_i2s_of_match[] = {
	{ .compatible = "nvidia,tegra210-i2s" },
	{},
};
MODULE_DEVICE_TABLE(of, tegra210_i2s_of_match);

static struct platform_driver tegra210_i2s_driver = {
	.driver = {
		.name = "tegra210-i2s",
		.of_match_table = tegra210_i2s_of_match,
		.pm = &tegra210_i2s_pm_ops,
	},
	.probe = tegra210_i2s_probe,
	.remove_new = tegra210_i2s_remove,
};
module_platform_driver(tegra210_i2s_driver)

MODULE_AUTHOR("Songhee Baek <sbaek@nvidia.com>");
MODULE_DESCRIPTION("Tegra210 ASoC I2S driver");
MODULE_LICENSE("GPL v2");
