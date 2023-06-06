// SPDX-License-Identifier: GPL-2.0-only
//
// tegra210_mvc.c - Tegra210 MVC driver
//
// Copyright (c) 2021 NVIDIA CORPORATION.  All rights reserved.

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra210_mvc.h"
#include "tegra_cif.h"

static const struct reg_default tegra210_mvc_reg_defaults[] = {
	{ TEGRA210_MVC_RX_INT_MASK, 0x00000001},
	{ TEGRA210_MVC_RX_CIF_CTRL, 0x00007700},
	{ TEGRA210_MVC_TX_INT_MASK, 0x00000001},
	{ TEGRA210_MVC_TX_CIF_CTRL, 0x00007700},
	{ TEGRA210_MVC_CG, 0x1},
	{ TEGRA210_MVC_CTRL, TEGRA210_MVC_CTRL_DEFAULT},
	{ TEGRA210_MVC_INIT_VOL, 0x00800000},
	{ TEGRA210_MVC_TARGET_VOL, 0x00800000},
	{ TEGRA210_MVC_DURATION, 0x000012c0},
	{ TEGRA210_MVC_DURATION_INV, 0x0006d3a0},
	{ TEGRA210_MVC_POLY_N1, 0x0000007d},
	{ TEGRA210_MVC_POLY_N2, 0x00000271},
	{ TEGRA210_MVC_PEAK_CTRL, 0x000012c0},
	{ TEGRA210_MVC_CFG_RAM_CTRL, 0x00004000},
};

static const struct tegra210_mvc_gain_params gain_params = {
	.poly_coeff = { 23738319, 659403, -3680,
			15546680, 2530732, -120985,
			12048422, 5527252, -785042 },
	.poly_n1 = 16,
	.poly_n2 = 63,
	.duration = 150,
	.duration_inv = 14316558,
};

static int __maybe_unused tegra210_mvc_runtime_suspend(struct device *dev)
{
	struct tegra210_mvc *mvc = dev_get_drvdata(dev);

	regmap_read(mvc->regmap, TEGRA210_MVC_CTRL, &(mvc->ctrl_value));

	regcache_cache_only(mvc->regmap, true);
	regcache_mark_dirty(mvc->regmap);

	return 0;
}

static int __maybe_unused tegra210_mvc_runtime_resume(struct device *dev)
{
	struct tegra210_mvc *mvc = dev_get_drvdata(dev);

	regcache_cache_only(mvc->regmap, false);
	regcache_sync(mvc->regmap);

	regmap_write(mvc->regmap, TEGRA210_MVC_CTRL, mvc->ctrl_value);
	regmap_update_bits(mvc->regmap,
			   TEGRA210_MVC_SWITCH,
			   TEGRA210_MVC_VOLUME_SWITCH_MASK,
			   TEGRA210_MVC_VOLUME_SWITCH_TRIGGER);

	return 0;
}

static void tegra210_mvc_write_ram(struct regmap *regmap)
{
	int i;

	regmap_write(regmap, TEGRA210_MVC_CFG_RAM_CTRL,
		     TEGRA210_MVC_CFG_RAM_CTRL_SEQ_ACCESS_EN |
		     TEGRA210_MVC_CFG_RAM_CTRL_ADDR_INIT_EN |
		     TEGRA210_MVC_CFG_RAM_CTRL_RW_WRITE);

	for (i = 0; i < NUM_GAIN_POLY_COEFFS; i++)
		regmap_write(regmap, TEGRA210_MVC_CFG_RAM_DATA,
			     gain_params.poly_coeff[i]);
}

static void tegra210_mvc_conv_vol(struct tegra210_mvc *mvc, u8 chan, s32 val)
{
	/*
	 * Volume control read from mixer control is with
	 * 100x scaling; for CURVE_POLY the reg range
	 * is 0-100 (linear, Q24) and for CURVE_LINEAR
	 * it is -120dB to +40dB (Q8)
	 */
	if (mvc->curve_type == CURVE_POLY) {
		if (val > 10000)
			val = 10000;
		mvc->volume[chan] = ((val * (1<<8)) / 100) << 16;
	} else {
		val -= 12000;
		mvc->volume[chan] = (val * (1<<8)) / 100;
	}
}

static u32 tegra210_mvc_get_ctrl_reg(struct snd_kcontrol *kcontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_mvc *mvc = snd_soc_component_get_drvdata(cmpnt);
	u32 val;

	pm_runtime_get_sync(cmpnt->dev);
	regmap_read(mvc->regmap, TEGRA210_MVC_CTRL, &val);
	pm_runtime_put(cmpnt->dev);

	return val;
}

static int tegra210_mvc_get_mute(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	u32 val = tegra210_mvc_get_ctrl_reg(kcontrol);
	u8 mute_mask = TEGRA210_GET_MUTE_VAL(val);

	/*
	 * If per channel control is enabled, then return
	 * exact mute/unmute setting of all channels.
	 *
	 * Else report setting based on CH0 bit to reflect
	 * the correct HW state.
	 */
	if (val & TEGRA210_MVC_PER_CHAN_CTRL_EN) {
		ucontrol->value.integer.value[0] = mute_mask;
	} else {
		if (mute_mask & TEGRA210_MVC_CH0_MUTE_EN)
			ucontrol->value.integer.value[0] =
				TEGRA210_MUTE_MASK_EN;
		else
			ucontrol->value.integer.value[0] = 0;
	}

	return 0;
}

static int tegra210_mvc_get_master_mute(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u32 val = tegra210_mvc_get_ctrl_reg(kcontrol);
	u8 mute_mask = TEGRA210_GET_MUTE_VAL(val);

	/*
	 * If per channel control is disabled, then return
	 * master mute/unmute setting based on CH0 bit.
	 *
	 * Else report settings based on state of all
	 * channels.
	 */
	if (!(val & TEGRA210_MVC_PER_CHAN_CTRL_EN)) {
		ucontrol->value.integer.value[0] =
			mute_mask & TEGRA210_MVC_CH0_MUTE_EN;
	} else {
		if (mute_mask == TEGRA210_MUTE_MASK_EN)
			ucontrol->value.integer.value[0] =
				TEGRA210_MVC_CH0_MUTE_EN;
		else
			ucontrol->value.integer.value[0] = 0;
	}

	return 0;
}

static int tegra210_mvc_volume_switch_timeout(struct snd_soc_component *cmpnt)
{
	struct tegra210_mvc *mvc = snd_soc_component_get_drvdata(cmpnt);
	u32 value;
	int err;

	err = regmap_read_poll_timeout(mvc->regmap, TEGRA210_MVC_SWITCH,
			value, !(value & TEGRA210_MVC_VOLUME_SWITCH_MASK),
			10, 10000);
	if (err < 0)
		dev_err(cmpnt->dev,
			"Volume switch trigger is still active, err = %d\n",
			err);

	return err;
}

static int tegra210_mvc_update_mute(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol,
				    bool per_chan_ctrl)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_mvc *mvc = snd_soc_component_get_drvdata(cmpnt);
	u32 mute_val = ucontrol->value.integer.value[0];
	u32 per_ch_ctrl_val;
	bool change = false;
	int err;

	pm_runtime_get_sync(cmpnt->dev);

	err = tegra210_mvc_volume_switch_timeout(cmpnt);
	if (err < 0)
		goto end;

	if (per_chan_ctrl) {
		per_ch_ctrl_val = TEGRA210_MVC_PER_CHAN_CTRL_EN;
	} else {
		per_ch_ctrl_val = 0;

		if (mute_val)
			mute_val = TEGRA210_MUTE_MASK_EN;
	}

	regmap_update_bits_check(mvc->regmap, TEGRA210_MVC_CTRL,
				 TEGRA210_MVC_MUTE_MASK,
				 mute_val << TEGRA210_MVC_MUTE_SHIFT,
				 &change);

	if (change) {
		regmap_update_bits(mvc->regmap, TEGRA210_MVC_CTRL,
				   TEGRA210_MVC_PER_CHAN_CTRL_EN_MASK,
				   per_ch_ctrl_val);

		regmap_update_bits(mvc->regmap, TEGRA210_MVC_SWITCH,
				   TEGRA210_MVC_VOLUME_SWITCH_MASK,
				   TEGRA210_MVC_VOLUME_SWITCH_TRIGGER);
	}

end:
	pm_runtime_put(cmpnt->dev);

	if (err < 0)
		return err;

	if (change)
		return 1;

	return 0;
}

static int tegra210_mvc_put_mute(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	return tegra210_mvc_update_mute(kcontrol, ucontrol, true);
}

static int tegra210_mvc_put_master_mute(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	return tegra210_mvc_update_mute(kcontrol, ucontrol, false);
}

static int tegra210_mvc_get_vol(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_mvc *mvc = snd_soc_component_get_drvdata(cmpnt);
	u8 chan = TEGRA210_MVC_GET_CHAN(mc->reg, TEGRA210_MVC_TARGET_VOL);
	s32 val = mvc->volume[chan];

	if (mvc->curve_type == CURVE_POLY) {
		val = ((val >> 16) * 100) >> 8;
	} else {
		val = (val * 100) >> 8;
		val += 12000;
	}

	ucontrol->value.integer.value[0] = val;

	return 0;
}

static int tegra210_mvc_get_master_vol(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	return tegra210_mvc_get_vol(kcontrol, ucontrol);
}

static int tegra210_mvc_update_vol(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol,
				   bool per_ch_enable)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_mvc *mvc = snd_soc_component_get_drvdata(cmpnt);
	u8 chan = TEGRA210_MVC_GET_CHAN(mc->reg, TEGRA210_MVC_TARGET_VOL);
	int old_volume = mvc->volume[chan];
	int err, i;

	pm_runtime_get_sync(cmpnt->dev);

	err = tegra210_mvc_volume_switch_timeout(cmpnt);
	if (err < 0)
		goto end;

	tegra210_mvc_conv_vol(mvc, chan, ucontrol->value.integer.value[0]);

	if (mvc->volume[chan] == old_volume) {
		err = 0;
		goto end;
	}

	if (per_ch_enable) {
		regmap_update_bits(mvc->regmap, TEGRA210_MVC_CTRL,
				   TEGRA210_MVC_PER_CHAN_CTRL_EN_MASK,
				   TEGRA210_MVC_PER_CHAN_CTRL_EN);
	} else {
		regmap_update_bits(mvc->regmap, TEGRA210_MVC_CTRL,
				   TEGRA210_MVC_PER_CHAN_CTRL_EN_MASK, 0);

		for (i = 1; i < TEGRA210_MVC_MAX_CHAN_COUNT; i++)
			mvc->volume[i] = mvc->volume[chan];
	}

	/* Configure init volume same as target volume */
	regmap_write(mvc->regmap,
		TEGRA210_MVC_REG_OFFSET(TEGRA210_MVC_INIT_VOL, chan),
		mvc->volume[chan]);

	regmap_write(mvc->regmap, mc->reg, mvc->volume[chan]);

	regmap_update_bits(mvc->regmap, TEGRA210_MVC_SWITCH,
			   TEGRA210_MVC_VOLUME_SWITCH_MASK,
			   TEGRA210_MVC_VOLUME_SWITCH_TRIGGER);

	err = 1;

end:
	pm_runtime_put(cmpnt->dev);

	return err;
}

static int tegra210_mvc_put_vol(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return tegra210_mvc_update_vol(kcontrol, ucontrol, true);
}

static int tegra210_mvc_put_master_vol(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	return tegra210_mvc_update_vol(kcontrol, ucontrol, false);
}

static void tegra210_mvc_reset_vol_settings(struct tegra210_mvc *mvc,
					    struct device *dev)
{
	int i;

	/* Change volume to default init for new curve type */
	if (mvc->curve_type == CURVE_POLY) {
		for (i = 0; i < TEGRA210_MVC_MAX_CHAN_COUNT; i++)
			mvc->volume[i] = TEGRA210_MVC_INIT_VOL_DEFAULT_POLY;
	} else {
		for (i = 0; i < TEGRA210_MVC_MAX_CHAN_COUNT; i++)
			mvc->volume[i] = TEGRA210_MVC_INIT_VOL_DEFAULT_LINEAR;
	}

	pm_runtime_get_sync(dev);

	/* Program curve type */
	regmap_update_bits(mvc->regmap, TEGRA210_MVC_CTRL,
			   TEGRA210_MVC_CURVE_TYPE_MASK,
			   mvc->curve_type <<
			   TEGRA210_MVC_CURVE_TYPE_SHIFT);

	/* Init volume for all channels */
	for (i = 0; i < TEGRA210_MVC_MAX_CHAN_COUNT; i++) {
		regmap_write(mvc->regmap,
			TEGRA210_MVC_REG_OFFSET(TEGRA210_MVC_INIT_VOL, i),
			mvc->volume[i]);
		regmap_write(mvc->regmap,
			TEGRA210_MVC_REG_OFFSET(TEGRA210_MVC_TARGET_VOL, i),
			mvc->volume[i]);
	}

	/* Trigger volume switch */
	regmap_update_bits(mvc->regmap, TEGRA210_MVC_SWITCH,
			   TEGRA210_MVC_VOLUME_SWITCH_MASK,
			   TEGRA210_MVC_VOLUME_SWITCH_TRIGGER);

	pm_runtime_put(dev);
}

static int tegra210_mvc_get_curve_type(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_mvc *mvc = snd_soc_component_get_drvdata(cmpnt);

	ucontrol->value.enumerated.item[0] = mvc->curve_type;

	return 0;
}

static int tegra210_mvc_put_curve_type(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_mvc *mvc = snd_soc_component_get_drvdata(cmpnt);
	unsigned int value;

	regmap_read(mvc->regmap, TEGRA210_MVC_ENABLE, &value);
	if (value & TEGRA210_MVC_EN) {
		dev_err(cmpnt->dev,
			"Curve type can't be set when MVC is running\n");
		return -EINVAL;
	}

	if (mvc->curve_type == ucontrol->value.enumerated.item[0])
		return 0;

	mvc->curve_type = ucontrol->value.enumerated.item[0];

	tegra210_mvc_reset_vol_settings(mvc, cmpnt->dev);

	return 1;
}

static int tegra210_mvc_set_audio_cif(struct tegra210_mvc *mvc,
				      struct snd_pcm_hw_params *params,
				      unsigned int reg)
{
	unsigned int channels, audio_bits;
	struct tegra_cif_conf cif_conf;

	memset(&cif_conf, 0, sizeof(struct tegra_cif_conf));

	channels = params_channels(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		audio_bits = TEGRA_ACIF_BITS_16;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		audio_bits = TEGRA_ACIF_BITS_32;
		break;
	default:
		return -EINVAL;
	}

	cif_conf.audio_ch = channels;
	cif_conf.client_ch = channels;
	cif_conf.audio_bits = audio_bits;
	cif_conf.client_bits = audio_bits;

	tegra_set_cif(mvc->regmap, reg, &cif_conf);

	return 0;
}

static int tegra210_mvc_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct device *dev = dai->dev;
	struct tegra210_mvc *mvc = snd_soc_dai_get_drvdata(dai);
	int err, val;

	/*
	 * Soft Reset: Below performs module soft reset which clears
	 * all FSM logic, flushes flow control of FIFO and resets the
	 * state register. It also brings module back to disabled
	 * state (without flushing the data in the pipe).
	 */
	regmap_write(mvc->regmap, TEGRA210_MVC_SOFT_RESET, 1);

	err = regmap_read_poll_timeout(mvc->regmap, TEGRA210_MVC_SOFT_RESET,
				       val, !val, 10, 10000);
	if (err < 0) {
		dev_err(dev, "SW reset failed, err = %d\n", err);
		return err;
	}

	/* Set RX CIF */
	err = tegra210_mvc_set_audio_cif(mvc, params, TEGRA210_MVC_RX_CIF_CTRL);
	if (err) {
		dev_err(dev, "Can't set MVC RX CIF: %d\n", err);
		return err;
	}

	/* Set TX CIF */
	err = tegra210_mvc_set_audio_cif(mvc, params, TEGRA210_MVC_TX_CIF_CTRL);
	if (err) {
		dev_err(dev, "Can't set MVC TX CIF: %d\n", err);
		return err;
	}

	tegra210_mvc_write_ram(mvc->regmap);

	/* Program poly_n1, poly_n2, duration */
	regmap_write(mvc->regmap, TEGRA210_MVC_POLY_N1, gain_params.poly_n1);
	regmap_write(mvc->regmap, TEGRA210_MVC_POLY_N2, gain_params.poly_n2);
	regmap_write(mvc->regmap, TEGRA210_MVC_DURATION, gain_params.duration);

	/* Program duration_inv */
	regmap_write(mvc->regmap, TEGRA210_MVC_DURATION_INV,
		     gain_params.duration_inv);

	return 0;
}

static const struct snd_soc_dai_ops tegra210_mvc_dai_ops = {
	.hw_params	= tegra210_mvc_hw_params,
};

static const char * const tegra210_mvc_curve_type_text[] = {
	"Poly",
	"Linear",
};

static const struct soc_enum tegra210_mvc_curve_type_ctrl =
	SOC_ENUM_SINGLE_EXT(2, tegra210_mvc_curve_type_text);

#define TEGRA210_MVC_VOL_CTRL(chan)					\
	SOC_SINGLE_EXT("Channel" #chan " Volume",			\
		       TEGRA210_MVC_REG_OFFSET(TEGRA210_MVC_TARGET_VOL, \
					       (chan - 1)),		\
		       0, 16000, 0, tegra210_mvc_get_vol,		\
		       tegra210_mvc_put_vol)

static const struct snd_kcontrol_new tegra210_mvc_vol_ctrl[] = {
	/* Per channel volume control */
	TEGRA210_MVC_VOL_CTRL(1),
	TEGRA210_MVC_VOL_CTRL(2),
	TEGRA210_MVC_VOL_CTRL(3),
	TEGRA210_MVC_VOL_CTRL(4),
	TEGRA210_MVC_VOL_CTRL(5),
	TEGRA210_MVC_VOL_CTRL(6),
	TEGRA210_MVC_VOL_CTRL(7),
	TEGRA210_MVC_VOL_CTRL(8),

	/* Per channel mute */
	SOC_SINGLE_EXT("Per Chan Mute Mask",
		       TEGRA210_MVC_CTRL, 0, TEGRA210_MUTE_MASK_EN, 0,
		       tegra210_mvc_get_mute, tegra210_mvc_put_mute),

	/* Master volume */
	SOC_SINGLE_EXT("Volume", TEGRA210_MVC_TARGET_VOL, 0, 16000, 0,
		       tegra210_mvc_get_master_vol,
		       tegra210_mvc_put_master_vol),

	/* Master mute */
	SOC_SINGLE_EXT("Mute", TEGRA210_MVC_CTRL, 0, 1, 0,
		       tegra210_mvc_get_master_mute,
		       tegra210_mvc_put_master_mute),

	SOC_ENUM_EXT("Curve Type", tegra210_mvc_curve_type_ctrl,
		     tegra210_mvc_get_curve_type, tegra210_mvc_put_curve_type),
};

static struct snd_soc_dai_driver tegra210_mvc_dais[] = {
	/* Input */
	{
		.name = "MVC-RX-CIF",
		.playback = {
			.stream_name = "RX-CIF-Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "RX-CIF-Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
	},

	/* Output */
	{
		.name = "MVC-TX-CIF",
		.playback = {
			.stream_name = "TX-CIF-Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "TX-CIF-Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &tegra210_mvc_dai_ops,
	}
};

static const struct snd_soc_dapm_widget tegra210_mvc_widgets[] = {
	SND_SOC_DAPM_AIF_IN("RX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TX", NULL, 0, TEGRA210_MVC_ENABLE,
			     TEGRA210_MVC_EN_SHIFT, 0),
};

#define MVC_ROUTES(sname)					\
	{ "RX XBAR-" sname,	NULL,	"XBAR-TX" },		\
	{ "RX-CIF-" sname,	NULL,	"RX XBAR-" sname },	\
	{ "RX",			NULL,	"RX-CIF-" sname },	\
	{ "TX-CIF-" sname,	NULL,	"TX" },			\
	{ "TX XBAR-" sname,	NULL,	"TX-CIF-" sname },	\
	{ "XBAR-RX",            NULL,   "TX XBAR-" sname }

static const struct snd_soc_dapm_route tegra210_mvc_routes[] = {
	{ "TX", NULL, "RX" },
	MVC_ROUTES("Playback"),
	MVC_ROUTES("Capture"),
};

static const struct snd_soc_component_driver tegra210_mvc_cmpnt = {
	.dapm_widgets		= tegra210_mvc_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tegra210_mvc_widgets),
	.dapm_routes		= tegra210_mvc_routes,
	.num_dapm_routes	= ARRAY_SIZE(tegra210_mvc_routes),
	.controls		= tegra210_mvc_vol_ctrl,
	.num_controls		= ARRAY_SIZE(tegra210_mvc_vol_ctrl),
};

static bool tegra210_mvc_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_MVC_RX_STATUS ... TEGRA210_MVC_CONFIG_ERR_TYPE:
		return true;
	default:
		return false;
	};
}

static bool tegra210_mvc_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_MVC_RX_INT_MASK ... TEGRA210_MVC_RX_CIF_CTRL:
	case TEGRA210_MVC_TX_INT_MASK ... TEGRA210_MVC_TX_CIF_CTRL:
	case TEGRA210_MVC_ENABLE ... TEGRA210_MVC_CG:
	case TEGRA210_MVC_CTRL ... TEGRA210_MVC_CFG_RAM_DATA:
		return true;
	default:
		return false;
	}
}

static bool tegra210_mvc_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_MVC_RX_STATUS:
	case TEGRA210_MVC_RX_INT_STATUS:
	case TEGRA210_MVC_RX_INT_SET:

	case TEGRA210_MVC_TX_STATUS:
	case TEGRA210_MVC_TX_INT_STATUS:
	case TEGRA210_MVC_TX_INT_SET:

	case TEGRA210_MVC_SOFT_RESET:
	case TEGRA210_MVC_STATUS:
	case TEGRA210_MVC_INT_STATUS:
	case TEGRA210_MVC_SWITCH:
	case TEGRA210_MVC_CFG_RAM_CTRL:
	case TEGRA210_MVC_CFG_RAM_DATA:
	case TEGRA210_MVC_PEAK_VALUE:
	case TEGRA210_MVC_CTRL:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tegra210_mvc_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= TEGRA210_MVC_CONFIG_ERR_TYPE,
	.writeable_reg		= tegra210_mvc_wr_reg,
	.readable_reg		= tegra210_mvc_rd_reg,
	.volatile_reg		= tegra210_mvc_volatile_reg,
	.reg_defaults		= tegra210_mvc_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(tegra210_mvc_reg_defaults),
	.cache_type		= REGCACHE_FLAT,
};

static const struct of_device_id tegra210_mvc_of_match[] = {
	{ .compatible = "nvidia,tegra210-mvc" },
	{},
};
MODULE_DEVICE_TABLE(of, tegra210_mvc_of_match);

static int tegra210_mvc_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra210_mvc *mvc;
	void __iomem *regs;
	int err;

	mvc = devm_kzalloc(dev, sizeof(*mvc), GFP_KERNEL);
	if (!mvc)
		return -ENOMEM;

	dev_set_drvdata(dev, mvc);

	mvc->curve_type = CURVE_LINEAR;
	mvc->ctrl_value = TEGRA210_MVC_CTRL_DEFAULT;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	mvc->regmap = devm_regmap_init_mmio(dev, regs,
					    &tegra210_mvc_regmap_config);
	if (IS_ERR(mvc->regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(mvc->regmap);
	}

	regcache_cache_only(mvc->regmap, true);

	err = devm_snd_soc_register_component(dev, &tegra210_mvc_cmpnt,
					      tegra210_mvc_dais,
					      ARRAY_SIZE(tegra210_mvc_dais));
	if (err) {
		dev_err(dev, "can't register MVC component, err: %d\n", err);
		return err;
	}

	pm_runtime_enable(dev);

	tegra210_mvc_reset_vol_settings(mvc, &pdev->dev);

	return 0;
}

static void tegra210_mvc_platform_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static const struct dev_pm_ops tegra210_mvc_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra210_mvc_runtime_suspend,
			   tegra210_mvc_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver tegra210_mvc_driver = {
	.driver = {
		.name = "tegra210-mvc",
		.of_match_table = tegra210_mvc_of_match,
		.pm = &tegra210_mvc_pm_ops,
	},
	.probe = tegra210_mvc_platform_probe,
	.remove_new = tegra210_mvc_platform_remove,
};
module_platform_driver(tegra210_mvc_driver)

MODULE_AUTHOR("Arun Shamanna Lakshmi <aruns@nvidia.com>");
MODULE_DESCRIPTION("Tegra210 MVC ASoC driver");
MODULE_LICENSE("GPL v2");
