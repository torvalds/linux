// SPDX-License-Identifier: GPL-2.0-or-later
//
// sma1303.c -- sma1303 ALSA SoC Audio driver
//
// Copyright 2023 Iron Device Corporation
//
// Auther: Gyuhwa Park <gyuhwa.park@irondevice.com>
//         Kiseok Jo <kiseok.jo@irondevice.com>

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <asm/div64.h>

#include "sma1303.h"

#define CHECK_PERIOD_TIME 1 /* sec per HZ */
#define MAX_CONTROL_NAME 48

#define PLL_MATCH(_input_clk_name, _output_clk_name, _input_clk,\
		_post_n, _n, _vco,  _p_cp)\
{\
	.input_clk_name		= _input_clk_name,\
	.output_clk_name	= _output_clk_name,\
	.input_clk		= _input_clk,\
	.post_n			= _post_n,\
	.n			= _n,\
	.vco			= _vco,\
	.p_cp		= _p_cp,\
}

enum sma1303_type {
	SMA1303,
};

struct sma1303_pll_match {
	char *input_clk_name;
	char *output_clk_name;
	unsigned int input_clk;
	unsigned int post_n;
	unsigned int n;
	unsigned int vco;
	unsigned int p_cp;
};

struct sma1303_priv {
	enum sma1303_type devtype;
	struct attribute_group *attr_grp;
	struct delayed_work check_fault_work;
	struct device *dev;
	struct kobject *kobj;
	struct regmap *regmap;
	struct sma1303_pll_match *pll_matches;
	bool amp_power_status;
	bool force_mute_status;
	int num_of_pll_matches;
	int retry_cnt;
	unsigned int amp_mode;
	unsigned int cur_vol;
	unsigned int format;
	unsigned int frame_size;
	unsigned int init_vol;
	unsigned int last_bclk;
	unsigned int last_ocp_val;
	unsigned int last_over_temp;
	unsigned int rev_num;
	unsigned int sys_clk_id;
	unsigned int tdm_slot_rx;
	unsigned int tdm_slot_tx;
	unsigned int tsdw_cnt;
	long check_fault_period;
	long check_fault_status;
};

static struct sma1303_pll_match sma1303_pll_matches[] = {
PLL_MATCH("1.411MHz",  "24.595MHz", 1411200,  0x07, 0xF4, 0x8B, 0x03),
PLL_MATCH("1.536MHz",  "24.576MHz", 1536000,  0x07, 0xE0, 0x8B, 0x03),
PLL_MATCH("3.072MHz",  "24.576MHz", 3072000,  0x07, 0x70, 0x8B, 0x03),
PLL_MATCH("6.144MHz",  "24.576MHz", 6144000,  0x07, 0x70, 0x8B, 0x07),
PLL_MATCH("12.288MHz", "24.576MHz", 12288000, 0x07, 0x70, 0x8B, 0x0B),
PLL_MATCH("19.2MHz",   "24.343MHz", 19200000, 0x07, 0x47, 0x8B, 0x0A),
PLL_MATCH("24.576MHz", "24.576MHz", 24576000, 0x07, 0x70, 0x8B, 0x0F),
};

static int sma1303_startup(struct snd_soc_component *);
static int sma1303_shutdown(struct snd_soc_component *);

static const struct reg_default sma1303_reg_def[] = {
	{ 0x00, 0x80 },
	{ 0x01, 0x00 },
	{ 0x02, 0x00 },
	{ 0x03, 0x11 },
	{ 0x04, 0x17 },
	{ 0x09, 0x00 },
	{ 0x0A, 0x31 },
	{ 0x0B, 0x98 },
	{ 0x0C, 0x84 },
	{ 0x0D, 0x07 },
	{ 0x0E, 0x3F },
	{ 0x10, 0x00 },
	{ 0x11, 0x00 },
	{ 0x12, 0x00 },
	{ 0x14, 0x5C },
	{ 0x15, 0x01 },
	{ 0x16, 0x0F },
	{ 0x17, 0x0F },
	{ 0x18, 0x0F },
	{ 0x19, 0x00 },
	{ 0x1A, 0x00 },
	{ 0x1B, 0x00 },
	{ 0x23, 0x19 },
	{ 0x24, 0x00 },
	{ 0x25, 0x00 },
	{ 0x26, 0x04 },
	{ 0x33, 0x00 },
	{ 0x36, 0x92 },
	{ 0x37, 0x27 },
	{ 0x3B, 0x5A },
	{ 0x3C, 0x20 },
	{ 0x3D, 0x00 },
	{ 0x3E, 0x03 },
	{ 0x3F, 0x0C },
	{ 0x8B, 0x07 },
	{ 0x8C, 0x70 },
	{ 0x8D, 0x8B },
	{ 0x8E, 0x6F },
	{ 0x8F, 0x03 },
	{ 0x90, 0x26 },
	{ 0x91, 0x42 },
	{ 0x92, 0xE0 },
	{ 0x94, 0x35 },
	{ 0x95, 0x0C },
	{ 0x96, 0x42 },
	{ 0x97, 0x95 },
	{ 0xA0, 0x00 },
	{ 0xA1, 0x3B },
	{ 0xA2, 0xC8 },
	{ 0xA3, 0x28 },
	{ 0xA4, 0x40 },
	{ 0xA5, 0x01 },
	{ 0xA6, 0x41 },
	{ 0xA7, 0x00 },
};

static bool sma1303_readable_register(struct device *dev, unsigned int reg)
{
	bool result;

	if (reg > SMA1303_FF_DEVICE_INDEX)
		return false;

	switch (reg) {
	case SMA1303_00_SYSTEM_CTRL ... SMA1303_04_INPUT1_CTRL4:
	case SMA1303_09_OUTPUT_CTRL ... SMA1303_0E_MUTE_VOL_CTRL:
	case SMA1303_10_SYSTEM_CTRL1 ... SMA1303_12_SYSTEM_CTRL3:
	case SMA1303_14_MODULATOR ... SMA1303_1B_BASS_SPK7:
	case SMA1303_23_COMP_LIM1 ... SMA1303_26_COMP_LIM4:
	case SMA1303_33_SDM_CTRL ... SMA1303_34_OTP_DATA1:
	case SMA1303_36_PROTECTION  ... SMA1303_38_OTP_TRM0:
	case SMA1303_3B_TEST1  ... SMA1303_3F_ATEST2:
	case SMA1303_8B_PLL_POST_N ... SMA1303_92_FDPEC_CTRL:
	case SMA1303_94_BOOST_CTRL1 ... SMA1303_97_BOOST_CTRL4:
	case SMA1303_A0_PAD_CTRL0 ... SMA1303_A7_CLK_MON:
	case SMA1303_FA_STATUS1 ... SMA1303_FB_STATUS2:
		result = true;
		break;
	case SMA1303_FF_DEVICE_INDEX:
		result = true;
		break;
	default:
		result = false;
		break;
	}
	return result;
}

static bool sma1303_writeable_register(struct device *dev, unsigned int reg)
{
	bool result;

	if (reg > SMA1303_FF_DEVICE_INDEX)
		return false;

	switch (reg) {
	case SMA1303_00_SYSTEM_CTRL ... SMA1303_04_INPUT1_CTRL4:
	case SMA1303_09_OUTPUT_CTRL ... SMA1303_0E_MUTE_VOL_CTRL:
	case SMA1303_10_SYSTEM_CTRL1 ... SMA1303_12_SYSTEM_CTRL3:
	case SMA1303_14_MODULATOR ... SMA1303_1B_BASS_SPK7:
	case SMA1303_23_COMP_LIM1 ... SMA1303_26_COMP_LIM4:
	case SMA1303_33_SDM_CTRL:
	case SMA1303_36_PROTECTION  ... SMA1303_37_SLOPE_CTRL:
	case SMA1303_3B_TEST1  ... SMA1303_3F_ATEST2:
	case SMA1303_8B_PLL_POST_N ... SMA1303_92_FDPEC_CTRL:
	case SMA1303_94_BOOST_CTRL1 ... SMA1303_97_BOOST_CTRL4:
	case SMA1303_A0_PAD_CTRL0 ... SMA1303_A7_CLK_MON:
		result = true;
		break;
	default:
		result = false;
		break;
	}
	return result;
}

static bool sma1303_volatile_register(struct device *dev, unsigned int reg)
{
	bool result;

	switch (reg) {
	case SMA1303_FA_STATUS1 ... SMA1303_FB_STATUS2:
		result = true;
		break;
	case SMA1303_FF_DEVICE_INDEX:
		result = true;
		break;
	default:
		result = false;
		break;
	}
	return result;
}

static const DECLARE_TLV_DB_SCALE(sma1303_spk_tlv, -6000, 50, 0);

static int sma1303_regmap_write(struct sma1303_priv *sma1303,
				unsigned int reg, unsigned int val)
{
	int ret = 0;
	int cnt = sma1303->retry_cnt;

	while (cnt--) {
		ret = regmap_write(sma1303->regmap, reg, val);
		if (ret < 0) {
			dev_err(sma1303->dev,
					"Failed to write [0x%02X]\n", reg);
		} else
			break;
	}
	return ret;
}

static int sma1303_regmap_update_bits(struct sma1303_priv *sma1303,
	unsigned int reg, unsigned int mask, unsigned int val, bool *change)
{
	int ret = 0;
	int cnt = sma1303->retry_cnt;

	while (cnt--) {
		ret = regmap_update_bits_check(sma1303->regmap, reg,
				mask, val, change);
		if (ret < 0) {
			dev_err(sma1303->dev,
					"Failed to update [0x%02X]\n", reg);
		} else
			break;
	}
	return ret;
}

static int sma1303_regmap_read(struct sma1303_priv *sma1303,
			unsigned int reg, unsigned int *val)
{
	int ret = 0;
	int cnt = sma1303->retry_cnt;

	while (cnt--) {
		ret = regmap_read(sma1303->regmap, reg, val);
		if (ret < 0) {
			dev_err(sma1303->dev,
					"Failed to read [0x%02X]\n", reg);
		} else
			break;
	}
	return ret;
}

static const char * const sma1303_aif_in_source_text[] = {
	"Mono", "Left", "Right"};
static const char * const sma1303_aif_out_source_text[] = {
	"Disable", "After_FmtC", "After_Mixer", "After_DSP", "After_Post",
		"Clk_PLL", "Clk_OSC"};
static const char * const sma1303_tdm_slot_text[] = {
	"Slot0", "Slot1", "Slot2", "Slot3",
	"Slot4", "Slot5", "Slot6", "Slot7"};

static const struct soc_enum sma1303_aif_in_source_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_aif_in_source_text),
			sma1303_aif_in_source_text);
static const struct soc_enum sma1303_aif_out_source_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_aif_out_source_text),
			sma1303_aif_out_source_text);
static const struct soc_enum sma1303_tdm_slot_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1303_tdm_slot_text),
			sma1303_tdm_slot_text);

static int sma1303_force_mute_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = (int)sma1303->force_mute_status;
	dev_dbg(sma1303->dev, "%s : Force Mute %s\n", __func__,
			sma1303->force_mute_status ? "ON" : "OFF");

	return 0;
}

static int sma1303_force_mute_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	bool change = false, val = (bool)ucontrol->value.integer.value[0];

	if (sma1303->force_mute_status == val)
		change = false;
	else {
		change = true;
		sma1303->force_mute_status = val;
	}
	dev_dbg(sma1303->dev, "%s : Force Mute %s\n", __func__,
			sma1303->force_mute_status ? "ON" : "OFF");

	return change;
}

static int sma1303_postscaler_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val, ret;

	ret = sma1303_regmap_read(sma1303, SMA1303_90_POSTSCALER, &val);
	if (ret < 0)
		return -EINVAL;

	ucontrol->value.integer.value[0] = (val & 0x7E) >> 1;

	return 0;
}

static int sma1303_postscaler_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int ret, val = (int)ucontrol->value.integer.value[0];
	bool change;

	ret = sma1303_regmap_update_bits(sma1303,
			SMA1303_90_POSTSCALER, 0x7E, (val << 1), &change);
	if (ret < 0)
		return -EINVAL;

	return change;
}

static int sma1303_tdm_slot_rx_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val, ret;

	ret = sma1303_regmap_read(sma1303, SMA1303_A5_TDM1, &val);
	if (ret < 0)
		return -EINVAL;

	ucontrol->value.integer.value[0] = (val & 0x38) >> 3;
	sma1303->tdm_slot_rx = ucontrol->value.integer.value[0];

	return 0;
}

static int sma1303_tdm_slot_rx_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int ret, val = (int)ucontrol->value.integer.value[0];
	bool change;

	ret = sma1303_regmap_update_bits(sma1303,
			SMA1303_A5_TDM1, 0x38, (val << 3), &change);
	if (ret < 0)
		return -EINVAL;

	return change;
}

static int sma1303_tdm_slot_tx_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int val, ret;

	ret = sma1303_regmap_read(sma1303, SMA1303_A6_TDM2, &val);
	if (ret < 0)
		return -EINVAL;

	ucontrol->value.integer.value[0] = (val & 0x38) >> 3;
	sma1303->tdm_slot_tx = ucontrol->value.integer.value[0];

	return 0;
}

static int sma1303_tdm_slot_tx_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int ret, val = (int)ucontrol->value.integer.value[0];
	bool change;

	ret = sma1303_regmap_update_bits(sma1303,
			SMA1303_A6_TDM2, 0x38, (val << 3), &change);
	if (ret < 0)
		return -EINVAL;

	return change;
}

static int sma1303_startup(struct snd_soc_component *component)
{
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	bool change = false, temp = false;

	sma1303_regmap_update_bits(sma1303, SMA1303_8E_PLL_CTRL,
			SMA1303_PLL_PD2_MASK, SMA1303_PLL_OPERATION2, &temp);
	if (temp == true)
		change = true;

	sma1303_regmap_update_bits(sma1303, SMA1303_00_SYSTEM_CTRL,
			SMA1303_POWER_MASK, SMA1303_POWER_ON, &temp);
	if (temp == true)
		change = true;

	if (sma1303->amp_mode == SMA1303_MONO) {
		sma1303_regmap_update_bits(sma1303,
				SMA1303_10_SYSTEM_CTRL1,
				SMA1303_SPK_MODE_MASK,
				SMA1303_SPK_MONO,
				&temp);
		if (temp == true)
			change = true;

	} else {
		sma1303_regmap_update_bits(sma1303,
				SMA1303_10_SYSTEM_CTRL1,
				SMA1303_SPK_MODE_MASK,
				SMA1303_SPK_STEREO,
				&temp);
		if (temp == true)
			change = true;
	}

	if (sma1303->check_fault_status) {
		if (sma1303->check_fault_period > 0)
			queue_delayed_work(system_freezable_wq,
				&sma1303->check_fault_work,
					sma1303->check_fault_period * HZ);
		else
			queue_delayed_work(system_freezable_wq,
				&sma1303->check_fault_work,
					CHECK_PERIOD_TIME * HZ);
	}

	sma1303->amp_power_status = true;

	return change;
}

static int sma1303_shutdown(struct snd_soc_component *component)
{
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	bool change = false, temp = false;

	cancel_delayed_work_sync(&sma1303->check_fault_work);

	sma1303_regmap_update_bits(sma1303, SMA1303_10_SYSTEM_CTRL1,
			SMA1303_SPK_MODE_MASK, SMA1303_SPK_OFF, &temp);
	if (temp == true)
		change = true;

	sma1303_regmap_update_bits(sma1303, SMA1303_00_SYSTEM_CTRL,
			SMA1303_POWER_MASK, SMA1303_POWER_OFF, &temp);
	if (temp == true)
		change = true;
	sma1303_regmap_update_bits(sma1303, SMA1303_8E_PLL_CTRL,
			SMA1303_PLL_PD2_MASK, SMA1303_PLL_PD2, &temp);
	if (temp == true)
		change = true;

	sma1303->amp_power_status = false;

	return change;
}

static int sma1303_aif_in_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
			snd_soc_dapm_to_component(w->dapm);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	unsigned int mux = dapm_kcontrol_get_value(w->kcontrols[0]);
	int ret = 0;
	bool change = false, temp = false;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		switch (mux) {
		case 0:
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_11_SYSTEM_CTRL2,
					SMA1303_MONOMIX_MASK,
					SMA1303_MONOMIX_ON,
					&change);
			sma1303->amp_mode = SMA1303_MONO;
			break;
		case 1:
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_11_SYSTEM_CTRL2,
					SMA1303_MONOMIX_MASK,
					SMA1303_MONOMIX_OFF,
					&temp);
			if (temp == true)
				change = true;
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_11_SYSTEM_CTRL2,
					SMA1303_LR_DATA_SW_MASK,
					SMA1303_LR_DATA_SW_NORMAL,
					&temp);
			if (temp == true)
				change = true;
			sma1303->amp_mode = SMA1303_STEREO;
			break;
		case 2:
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_11_SYSTEM_CTRL2,
					SMA1303_MONOMIX_MASK,
					SMA1303_MONOMIX_OFF,
					&temp);
			if (temp == true)
				change = true;
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_11_SYSTEM_CTRL2,
					SMA1303_LR_DATA_SW_MASK,
					SMA1303_LR_DATA_SW_SWAP,
					&temp);
			if (temp == true)
				change = true;
			sma1303->amp_mode = SMA1303_STEREO;
			break;
		default:
			dev_err(sma1303->dev, "%s : Invalid value (%d)\n",
								__func__, mux);
			return -EINVAL;
		}

		dev_dbg(sma1303->dev, "%s : Source : %s\n", __func__,
					sma1303_aif_in_source_text[mux]);
		break;
	}
	if (ret < 0)
		return -EINVAL;
	return change;
}

static int sma1303_aif_out_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
			snd_soc_dapm_to_component(w->dapm);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	unsigned int mux = dapm_kcontrol_get_value(w->kcontrols[0]);
	int ret = 0;
	bool change = false, temp = false;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		switch (mux) {
		case 0:
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A3_TOP_MAN2,
					SMA1303_TEST_CLKO_EN_MASK,
					SMA1303_NORMAL_SDO,
					&temp);
			if (temp == true)
				change = true;
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_09_OUTPUT_CTRL,
					SMA1303_PORT_OUT_SEL_MASK,
					SMA1303_OUT_SEL_DISABLE,
					&temp);
			if (temp == true)
				change = true;
			break;
		case 1:
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A3_TOP_MAN2,
					SMA1303_TEST_CLKO_EN_MASK,
					SMA1303_NORMAL_SDO,
					&temp);
			if (temp == true)
				change = true;
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_09_OUTPUT_CTRL,
					SMA1303_PORT_OUT_SEL_MASK,
					SMA1303_FORMAT_CONVERTER,
					&temp);
			if (temp == true)
				change = true;
			break;
		case 2:
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A3_TOP_MAN2,
					SMA1303_TEST_CLKO_EN_MASK,
					SMA1303_NORMAL_SDO,
					&temp);
			if (temp == true)
				change = true;
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_09_OUTPUT_CTRL,
					SMA1303_PORT_OUT_SEL_MASK,
					SMA1303_MIXER_OUTPUT,
					&temp);
			if (temp == true)
				change = true;
			break;
		case 3:
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A3_TOP_MAN2,
					SMA1303_TEST_CLKO_EN_MASK,
					SMA1303_NORMAL_SDO,
					&temp);
			if (temp == true)
				change = true;
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_09_OUTPUT_CTRL,
					SMA1303_PORT_OUT_SEL_MASK,
					SMA1303_SPEAKER_PATH,
					&temp);
			if (temp == true)
				change = true;
			break;
		case 4:
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A3_TOP_MAN2,
					SMA1303_TEST_CLKO_EN_MASK,
					SMA1303_NORMAL_SDO,
					&temp);
			if (temp == true)
				change = true;
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_09_OUTPUT_CTRL,
					SMA1303_PORT_OUT_SEL_MASK,
					SMA1303_POSTSCALER_OUTPUT,
					&temp);
			if (temp == true)
				change = true;
			break;
		case 5:
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A3_TOP_MAN2,
					SMA1303_TEST_CLKO_EN_MASK,
					SMA1303_CLK_OUT_SDO,
					&temp);
			if (temp == true)
				change = true;
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A3_TOP_MAN2,
					SMA1303_MON_OSC_PLL_MASK,
					SMA1303_PLL_SDO,
					&temp);
			if (temp == true)
				change = true;
			break;
		case 6:
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A3_TOP_MAN2,
					SMA1303_TEST_CLKO_EN_MASK,
					SMA1303_CLK_OUT_SDO,
					&temp);
			if (temp == true)
				change = true;
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A3_TOP_MAN2,
					SMA1303_MON_OSC_PLL_MASK,
					SMA1303_OSC_SDO,
					&temp);
			if (temp == true)
				change = true;
			break;
		default:
			dev_err(sma1303->dev, "%s : Invalid value (%d)\n",
								__func__, mux);
			return -EINVAL;
		}

		dev_dbg(sma1303->dev, "%s : Source : %s\n", __func__,
					sma1303_aif_out_source_text[mux]);
		break;
	}
	if (ret < 0)
		return -EINVAL;
	return change;
}

static int sma1303_sdo_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int ret = 0;
	bool change = false, temp = false;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		dev_dbg(sma1303->dev,
			"%s : SND_SOC_DAPM_PRE_PMU\n", __func__);
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_09_OUTPUT_CTRL,
				SMA1303_PORT_CONFIG_MASK,
				SMA1303_OUTPUT_PORT_ENABLE,
				&temp);
		if (temp == true)
			change = true;
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_A3_TOP_MAN2,
				SMA1303_SDO_OUTPUT_MASK,
				SMA1303_NORMAL_OUT,
				&temp);
		if (temp == true)
			change = true;
		break;
	case SND_SOC_DAPM_POST_PMD:
		dev_dbg(sma1303->dev,
			"%s : SND_SOC_DAPM_POST_PMD\n", __func__);
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_09_OUTPUT_CTRL,
				SMA1303_PORT_CONFIG_MASK,
				SMA1303_INPUT_PORT_ONLY,
				&temp);
		if (temp == true)
			change = true;
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_A3_TOP_MAN2,
				SMA1303_SDO_OUTPUT_MASK,
				SMA1303_HIGH_Z_OUT,
				&temp);
		if (temp == true)
			change = true;
		break;
	}
	if (ret < 0)
		return -EINVAL;
	return change;
}

static int sma1303_post_scaler_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int ret = 0;
	bool change = false;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		dev_dbg(sma1303->dev,
				"%s : SND_SOC_DAPM_PRE_PMU\n", __func__);
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_90_POSTSCALER,
				SMA1303_BYP_POST_MASK,
				SMA1303_EN_POST_SCALER,
				&change);
		break;
	case SND_SOC_DAPM_POST_PMD:
		dev_dbg(sma1303->dev,
				"%s : SND_SOC_DAPM_POST_PMD\n", __func__);
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_90_POSTSCALER,
				SMA1303_BYP_POST_MASK,
				SMA1303_BYP_POST_SCALER,
				&change);
		break;
	}
	if (ret < 0)
		return -EINVAL;
	return change;
}

static int sma1303_power_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		dev_dbg(sma1303->dev,
			"%s : SND_SOC_DAPM_POST_PMU\n", __func__);
		ret = sma1303_startup(component);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		dev_dbg(sma1303->dev,
			"%s : SND_SOC_DAPM_PRE_PMD\n", __func__);
		ret = sma1303_shutdown(component);
		break;
	}
	return ret;
}

static const struct snd_kcontrol_new sma1303_aif_in_source_control =
	SOC_DAPM_ENUM("AIF IN Source", sma1303_aif_in_source_enum);
static const struct snd_kcontrol_new sma1303_aif_out_source_control =
	SOC_DAPM_ENUM("AIF OUT Source", sma1303_aif_out_source_enum);
static const struct snd_kcontrol_new sma1303_sdo_control =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);
static const struct snd_kcontrol_new sma1303_post_scaler_control =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);
static const struct snd_kcontrol_new sma1303_enable_control =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);

static const struct snd_kcontrol_new sma1303_snd_controls[] = {
	SOC_SINGLE_TLV("Speaker Volume", SMA1303_0A_SPK_VOL,
		0, 167, 1, sma1303_spk_tlv),
	SOC_SINGLE_BOOL_EXT("Force Mute Switch", 0,
		sma1303_force_mute_get, sma1303_force_mute_put),
	SOC_SINGLE_EXT("Postscaler Gain", SMA1303_90_POSTSCALER, 1, 0x30, 0,
		sma1303_postscaler_get, sma1303_postscaler_put),
	SOC_ENUM_EXT("TDM RX Slot Position", sma1303_tdm_slot_enum,
			sma1303_tdm_slot_rx_get, sma1303_tdm_slot_rx_put),
	SOC_ENUM_EXT("TDM TX Slot Position", sma1303_tdm_slot_enum,
			sma1303_tdm_slot_tx_get, sma1303_tdm_slot_tx_put),
};

static const struct snd_soc_dapm_widget sma1303_dapm_widgets[] = {
	/* platform domain */
	SND_SOC_DAPM_OUTPUT("SPK"),
	SND_SOC_DAPM_INPUT("SDO"),

	/* path domain */
	SND_SOC_DAPM_MUX_E("AIF IN Source", SND_SOC_NOPM, 0, 0,
			&sma1303_aif_in_source_control,
			sma1303_aif_in_event,
			SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_MUX_E("AIF OUT Source", SND_SOC_NOPM, 0, 0,
			&sma1303_aif_out_source_control,
			sma1303_aif_out_event,
			SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SWITCH_E("SDO Enable", SND_SOC_NOPM, 0, 0,
			&sma1303_sdo_control,
			sma1303_sdo_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER("Entry", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SWITCH_E("Post Scaler", SND_SOC_NOPM, 0, 1,
			&sma1303_post_scaler_control,
			sma1303_post_scaler_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUT_DRV_E("AMP Power", SND_SOC_NOPM, 0, 0, NULL, 0,
			sma1303_power_event,
			SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SWITCH("AMP Enable", SND_SOC_NOPM, 0, 1,
			&sma1303_enable_control),

	/* stream domain */
	SND_SOC_DAPM_AIF_IN("AIF IN", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF OUT", "Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route sma1303_audio_map[] = {
	/* Playback */
	{"AIF IN Source", "Mono", "AIF IN"},
	{"AIF IN Source", "Left", "AIF IN"},
	{"AIF IN Source", "Right", "AIF IN"},

	{"SDO Enable", "Switch", "AIF IN"},
	{"AIF OUT Source", "Disable", "SDO Enable"},
	{"AIF OUT Source", "After_FmtC", "SDO Enable"},
	{"AIF OUT Source", "After_Mixer", "SDO Enable"},
	{"AIF OUT Source", "After_DSP", "SDO Enable"},
	{"AIF OUT Source", "After_Post", "SDO Enable"},
	{"AIF OUT Source", "Clk_PLL", "SDO Enable"},
	{"AIF OUT Source", "Clk_OSC", "SDO Enable"},

	{"Entry", NULL, "AIF OUT Source"},
	{"Entry", NULL, "AIF IN Source"},

	{"Post Scaler", "Switch", "Entry"},
	{"AMP Power", NULL, "Entry"},
	{"AMP Power", NULL, "Entry"},

	{"AMP Enable", "Switch", "AMP Power"},
	{"SPK", NULL, "AMP Enable"},

	/* Capture */
	{"AIF OUT", NULL, "AMP Enable"},
};

static int sma1303_setup_pll(struct snd_soc_component *component,
		unsigned int bclk)
{
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	int i = 0, ret = 0;

	dev_dbg(component->dev, "%s : BCLK = %dHz\n",
		__func__, bclk);

	if (sma1303->sys_clk_id == SMA1303_PLL_CLKIN_MCLK) {
		dev_dbg(component->dev, "%s : MCLK is not supported\n",
		__func__);
	} else if (sma1303->sys_clk_id == SMA1303_PLL_CLKIN_BCLK) {
		for (i = 0; i < sma1303->num_of_pll_matches; i++) {
			if (sma1303->pll_matches[i].input_clk == bclk)
				break;
		}
		if (i == sma1303->num_of_pll_matches) {
			dev_dbg(component->dev, "%s : No matching value between pll table and SCK\n",
					__func__);
			return -EINVAL;
		}

		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_A2_TOP_MAN1,
				SMA1303_PLL_PD_MASK|SMA1303_PLL_REF_CLK_MASK,
				SMA1303_PLL_OPERATION|SMA1303_PLL_SCK,
				NULL);
	}

	ret += sma1303_regmap_write(sma1303,
			SMA1303_8B_PLL_POST_N,
			sma1303->pll_matches[i].post_n);

	ret += sma1303_regmap_write(sma1303,
			SMA1303_8C_PLL_N,
			sma1303->pll_matches[i].n);

	ret += sma1303_regmap_write(sma1303,
			SMA1303_8D_PLL_A_SETTING,
			sma1303->pll_matches[i].vco);

	ret += sma1303_regmap_write(sma1303,
			SMA1303_8F_PLL_P_CP,
			sma1303->pll_matches[i].p_cp);
	if (ret < 0)
		return -EINVAL;

	return 0;
}

static int sma1303_dai_hw_params_amp(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	unsigned int bclk = 0;
	int ret = 0;

	if (sma1303->format == SND_SOC_DAIFMT_DSP_A)
		bclk = params_rate(params) * sma1303->frame_size;
	else
		bclk = params_rate(params) * params_physical_width(params)
			* params_channels(params);

	dev_dbg(component->dev,
			"%s : rate = %d : bit size = %d : channel = %d\n",
			__func__, params_rate(params), params_width(params),
			params_channels(params));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (sma1303->sys_clk_id == SMA1303_PLL_CLKIN_BCLK) {
			if (sma1303->last_bclk != bclk) {
				sma1303_setup_pll(component, bclk);
				sma1303->last_bclk = bclk;
			}
		}

		switch (params_rate(params)) {
		case 8000:
		case 12000:
		case 16000:
		case 24000:
		case 32000:
		case 44100:
		case 48000:
		case 96000:
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A2_TOP_MAN1,
					SMA1303_DAC_DN_CONV_MASK,
					SMA1303_DAC_DN_CONV_DISABLE,
					NULL);

			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_01_INPUT1_CTRL1,
					SMA1303_LEFTPOL_MASK,
					SMA1303_LOW_FIRST_CH,
					NULL);
			break;

		case 192000:
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A2_TOP_MAN1,
					SMA1303_DAC_DN_CONV_MASK,
					SMA1303_DAC_DN_CONV_ENABLE,
					NULL);

			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_01_INPUT1_CTRL1,
					SMA1303_LEFTPOL_MASK,
					SMA1303_HIGH_FIRST_CH,
					NULL);
			break;

		default:
			dev_err(component->dev, "%s not support rate : %d\n",
				__func__, params_rate(params));

			return -EINVAL;
		}

	} else {

		switch (params_format(params)) {

		case SNDRV_PCM_FORMAT_S16_LE:
			dev_dbg(component->dev,
				"%s set format SNDRV_PCM_FORMAT_S16_LE\n",
				__func__);
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A4_TOP_MAN3,
					SMA1303_SCK_RATE_MASK,
					SMA1303_SCK_32FS,
					NULL);
			break;

		case SNDRV_PCM_FORMAT_S24_LE:
			dev_dbg(component->dev,
				"%s set format SNDRV_PCM_FORMAT_S24_LE\n",
				__func__);
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A4_TOP_MAN3,
					SMA1303_SCK_RATE_MASK,
					SMA1303_SCK_64FS,
					NULL);
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
			dev_dbg(component->dev,
				"%s set format SNDRV_PCM_FORMAT_S32_LE\n",
				__func__);
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A4_TOP_MAN3,
					SMA1303_SCK_RATE_MASK,
					SMA1303_SCK_64FS,
					NULL);
			break;
		default:
			dev_err(component->dev,
				"%s not support data bit : %d\n", __func__,
						params_format(params));
			return -EINVAL;
		}
	}

	switch (sma1303->format) {
	case SND_SOC_DAIFMT_I2S:
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_01_INPUT1_CTRL1,
				SMA1303_I2S_MODE_MASK,
				SMA1303_STANDARD_I2S,
				NULL);
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_A4_TOP_MAN3,
				SMA1303_O_FORMAT_MASK,
				SMA1303_O_FMT_I2S,
				NULL);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_01_INPUT1_CTRL1,
				SMA1303_I2S_MODE_MASK,
				SMA1303_LJ,
				NULL);
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_A4_TOP_MAN3,
				SMA1303_O_FORMAT_MASK,
				SMA1303_O_FMT_LJ,
				NULL);
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		switch (params_width(params)) {
		case 16:
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_01_INPUT1_CTRL1,
					SMA1303_I2S_MODE_MASK,
					SMA1303_RJ_16BIT,
					NULL);
			break;
		case 24:
		case 32:
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_01_INPUT1_CTRL1,
					SMA1303_I2S_MODE_MASK,
					SMA1303_RJ_24BIT,
					NULL);
			break;
		}
		break;
	case SND_SOC_DAIFMT_DSP_A:
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_01_INPUT1_CTRL1,
				SMA1303_I2S_MODE_MASK,
				SMA1303_STANDARD_I2S,
				NULL);
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_A4_TOP_MAN3,
				SMA1303_O_FORMAT_MASK,
				SMA1303_O_FMT_TDM,
				NULL);
		break;
	}

	switch (params_width(params)) {
	case 16:
	case 24:
	case 32:
		break;
	default:
		dev_err(component->dev,
			"%s not support data bit : %d\n", __func__,
					params_format(params));
		return -EINVAL;
	}
	if (ret < 0)
		return -EINVAL;

	return 0;
}

static int sma1303_dai_set_sysclk_amp(struct snd_soc_dai *dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	switch (clk_id) {
	case SMA1303_EXTERNAL_CLOCK_19_2:
		break;
	case SMA1303_EXTERNAL_CLOCK_24_576:
		break;
	case SMA1303_PLL_CLKIN_MCLK:
		break;
	case SMA1303_PLL_CLKIN_BCLK:
		break;
	default:
		dev_err(component->dev, "Invalid clk id: %d\n", clk_id);
		return -EINVAL;
	}
	sma1303->sys_clk_id = clk_id;
	return 0;
}

static int sma1303_dai_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *component = dai->component;
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	if (stream == SNDRV_PCM_STREAM_CAPTURE)
		return ret;

	if (mute) {
		dev_dbg(component->dev, "%s : %s\n", __func__, "MUTE");

		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_0E_MUTE_VOL_CTRL,
				SMA1303_SPK_MUTE_MASK,
				SMA1303_SPK_MUTE,
				NULL);

		/* Need to wait time for mute slope */
		msleep(55);
	} else {
		if (!sma1303->force_mute_status) {
			dev_dbg(component->dev, "%s : %s\n",
					__func__, "UNMUTE");
			ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_0E_MUTE_VOL_CTRL,
					SMA1303_SPK_MUTE_MASK,
					SMA1303_SPK_UNMUTE,
					NULL);
		} else {
			dev_dbg(sma1303->dev,
					"%s : FORCE MUTE!!!\n", __func__);
		}
	}

	if (ret < 0)
		return -EINVAL;
	return 0;
}

static int sma1303_dai_set_fmt_amp(struct snd_soc_dai *dai,
					unsigned int fmt)
{
	struct snd_soc_component *component  = dai->component;
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {

	case SND_SOC_DAIFMT_CBC_CFC:
		dev_dbg(component->dev,
				"%s : %s\n", __func__, "I2S/TDM Device mode");
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_01_INPUT1_CTRL1,
				SMA1303_CONTROLLER_DEVICE_MASK,
				SMA1303_DEVICE_MODE,
				NULL);
		break;

	case SND_SOC_DAIFMT_CBP_CFP:
		dev_dbg(component->dev,
			"%s : %s\n", __func__, "I2S/TDM Controller mode");
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_01_INPUT1_CTRL1,
				SMA1303_CONTROLLER_DEVICE_MASK,
				SMA1303_CONTROLLER_MODE,
				NULL);
		break;

	default:
		dev_err(component->dev,
			"Unsupported Controller/Device : 0x%x\n", fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {

	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		sma1303->format = fmt & SND_SOC_DAIFMT_FORMAT_MASK;
		break;
	default:
		dev_err(component->dev,
			"Unsupported Audio Interface Format : 0x%x\n", fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {

	case SND_SOC_DAIFMT_IB_NF:
		dev_dbg(component->dev, "%s : %s\n",
			__func__, "Invert BCLK + Normal Frame");
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_01_INPUT1_CTRL1,
				SMA1303_SCK_RISING_MASK,
				SMA1303_SCK_RISING_EDGE,
				NULL);
		break;
	case SND_SOC_DAIFMT_IB_IF:
		dev_dbg(component->dev, "%s : %s\n",
			__func__, "Invert BCLK + Invert Frame");
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_01_INPUT1_CTRL1,
				SMA1303_LEFTPOL_MASK|SMA1303_SCK_RISING_MASK,
				SMA1303_HIGH_FIRST_CH|SMA1303_SCK_RISING_EDGE,
				NULL);
		break;
	case SND_SOC_DAIFMT_NB_IF:
		dev_dbg(component->dev, "%s : %s\n",
			__func__, "Normal BCLK + Invert Frame");
		ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_01_INPUT1_CTRL1,
				SMA1303_LEFTPOL_MASK,
				SMA1303_HIGH_FIRST_CH,
				NULL);
		break;
	case SND_SOC_DAIFMT_NB_NF:
		dev_dbg(component->dev, "%s : %s\n",
			__func__, "Normal BCLK + Normal Frame");
		break;
	default:
		dev_err(component->dev,
				"Unsupported Bit & Frameclock : 0x%x\n", fmt);
		return -EINVAL;
	}

	if (ret < 0)
		return -EINVAL;
	return 0;
}

static int sma1303_dai_set_tdm_slot(struct snd_soc_dai *dai,
				unsigned int tx_mask, unsigned int rx_mask,
				int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	dev_dbg(component->dev, "%s : slots = %d, slot_width - %d\n",
			__func__, slots, slot_width);

	sma1303->frame_size = slot_width * slots;

	ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_A4_TOP_MAN3,
				SMA1303_O_FORMAT_MASK,
				SMA1303_O_FMT_TDM,
				NULL);

	switch (slot_width) {
	case 16:
		ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A6_TDM2,
					SMA1303_TDM_DL_MASK,
					SMA1303_TDM_DL_16,
					NULL);
		break;
	case 32:
		ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A6_TDM2,
					SMA1303_TDM_DL_MASK,
					SMA1303_TDM_DL_32,
					NULL);
		break;
	default:
		dev_err(component->dev, "%s not support TDM %d slot_width\n",
					__func__, slot_width);
		break;
	}

	switch (slots) {
	case 4:
		ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A6_TDM2,
					SMA1303_TDM_N_SLOT_MASK,
					SMA1303_TDM_N_SLOT_4,
					NULL);
		break;
	case 8:
		ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A6_TDM2,
					SMA1303_TDM_N_SLOT_MASK,
					SMA1303_TDM_N_SLOT_8,
					NULL);
		break;
	default:
		dev_err(component->dev, "%s not support TDM %d slots\n",
				__func__, slots);
		break;
	}

	if (sma1303->tdm_slot_rx < slots)
		ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A5_TDM1,
					SMA1303_TDM_SLOT1_RX_POS_MASK,
					(sma1303->tdm_slot_rx) << 3,
					NULL);
	else
		dev_err(component->dev, "%s Incorrect tdm-slot-rx %d set\n",
					__func__, sma1303->tdm_slot_rx);

	ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_A5_TDM1,
				SMA1303_TDM_CLK_POL_MASK,
				SMA1303_TDM_CLK_POL_RISE,
				NULL);

	ret += sma1303_regmap_update_bits(sma1303,
				SMA1303_A5_TDM1,
				SMA1303_TDM_TX_MODE_MASK,
				SMA1303_TDM_TX_MONO,
				NULL);

	if (sma1303->tdm_slot_tx < slots)
		ret += sma1303_regmap_update_bits(sma1303,
					SMA1303_A6_TDM2,
					SMA1303_TDM_SLOT1_TX_POS_MASK,
					(sma1303->tdm_slot_tx) << 3,
					NULL);
	else
		dev_err(component->dev, "%s Incorrect tdm-slot-tx %d set\n",
				__func__, sma1303->tdm_slot_tx);

	if (ret < 0)
		return -EINVAL;
	return 0;
}

static const struct snd_soc_dai_ops sma1303_dai_ops_amp = {
	.set_sysclk = sma1303_dai_set_sysclk_amp,
	.set_fmt = sma1303_dai_set_fmt_amp,
	.hw_params = sma1303_dai_hw_params_amp,
	.mute_stream = sma1303_dai_mute,
	.set_tdm_slot = sma1303_dai_set_tdm_slot,
};

#define SMA1303_RATES SNDRV_PCM_RATE_8000_192000
#define SMA1303_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | \
		SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver sma1303_dai[] = {
	{
		.name = "sma1303-amplifier",
		.id = 0,
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SMA1303_RATES,
			.formats = SMA1303_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SMA1303_RATES,
			.formats = SMA1303_FORMATS,
		},
		.ops = &sma1303_dai_ops_amp,
	},
};

static void sma1303_check_fault_worker(struct work_struct *work)
{
	struct sma1303_priv *sma1303 =
		container_of(work, struct sma1303_priv, check_fault_work.work);
	int ret = 0;
	unsigned int over_temp, ocp_val, uvlo_val;

	if (sma1303->tsdw_cnt)
		ret = sma1303_regmap_read(sma1303,
			SMA1303_0A_SPK_VOL, &sma1303->cur_vol);
	else
		ret = sma1303_regmap_read(sma1303,
			SMA1303_0A_SPK_VOL, &sma1303->init_vol);

	if (ret != 0) {
		dev_err(sma1303->dev,
			"failed to read SMA1303_0A_SPK_VOL : %d\n", ret);
		return;
	}

	ret = sma1303_regmap_read(sma1303, SMA1303_FA_STATUS1, &over_temp);
	if (ret != 0) {
		dev_err(sma1303->dev,
			"failed to read SMA1303_FA_STATUS1 : %d\n", ret);
		return;
	}

	ret = sma1303_regmap_read(sma1303, SMA1303_FB_STATUS2, &ocp_val);
	if (ret != 0) {
		dev_err(sma1303->dev,
			"failed to read SMA1303_FB_STATUS2 : %d\n", ret);
		return;
	}

	ret = sma1303_regmap_read(sma1303, SMA1303_FF_DEVICE_INDEX, &uvlo_val);
	if (ret != 0) {
		dev_err(sma1303->dev,
			"failed to read SMA1303_FF_DEVICE_INDEX : %d\n", ret);
		return;
	}

	if (~over_temp & SMA1303_OT1_OK_STATUS) {
		dev_crit(sma1303->dev,
			"%s : OT1(Over Temperature Level 1)\n", __func__);

		if ((sma1303->cur_vol + 6) <= 0xFF)
			sma1303_regmap_write(sma1303,
				SMA1303_0A_SPK_VOL, sma1303->cur_vol + 6);

		sma1303->tsdw_cnt++;
	} else if (sma1303->tsdw_cnt) {
		sma1303_regmap_write(sma1303,
				SMA1303_0A_SPK_VOL, sma1303->init_vol);
		sma1303->tsdw_cnt = 0;
		sma1303->cur_vol = sma1303->init_vol;
	}

	if (~over_temp & SMA1303_OT2_OK_STATUS) {
		dev_crit(sma1303->dev,
			"%s : OT2(Over Temperature Level 2)\n", __func__);
	}
	if (ocp_val & SMA1303_OCP_SPK_STATUS) {
		dev_crit(sma1303->dev,
			"%s : OCP_SPK(Over Current Protect SPK)\n", __func__);
	}
	if (ocp_val & SMA1303_OCP_BST_STATUS) {
		dev_crit(sma1303->dev,
			"%s : OCP_BST(Over Current Protect Boost)\n", __func__);
	}
	if ((ocp_val & SMA1303_CLK_MON_STATUS) && (sma1303->amp_power_status)) {
		dev_crit(sma1303->dev,
			"%s : CLK_FAULT(No clock input)\n", __func__);
	}
	if (uvlo_val & SMA1303_UVLO_BST_STATUS) {
		dev_crit(sma1303->dev,
			"%s : UVLO(Under Voltage Lock Out)\n", __func__);
	}

	if ((over_temp != sma1303->last_over_temp) ||
		(ocp_val != sma1303->last_ocp_val)) {

		dev_crit(sma1303->dev, "Please check AMP status");
		dev_dbg(sma1303->dev, "STATUS1=0x%02X : STATUS2=0x%02X\n",
				over_temp, ocp_val);
		sma1303->last_over_temp = over_temp;
		sma1303->last_ocp_val = ocp_val;
	}

	if (sma1303->check_fault_status) {
		if (sma1303->check_fault_period > 0)
			queue_delayed_work(system_freezable_wq,
				&sma1303->check_fault_work,
					sma1303->check_fault_period * HZ);
		else
			queue_delayed_work(system_freezable_wq,
				&sma1303->check_fault_work,
					CHECK_PERIOD_TIME * HZ);
	}

	if (!(~over_temp & SMA1303_OT1_OK_STATUS)
			&& !(~over_temp & SMA1303_OT2_OK_STATUS)
			&& !(ocp_val & SMA1303_OCP_SPK_STATUS)
			&& !(ocp_val & SMA1303_OCP_BST_STATUS)
			&& !(ocp_val & SMA1303_CLK_MON_STATUS)
			&& !(uvlo_val & SMA1303_UVLO_BST_STATUS)) {
	}
}

static int sma1303_probe(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);

	snd_soc_dapm_sync(dapm);

	return 0;
}

static void sma1303_remove(struct snd_soc_component *component)
{
	struct sma1303_priv *sma1303 = snd_soc_component_get_drvdata(component);

	cancel_delayed_work_sync(&sma1303->check_fault_work);
}

static const struct snd_soc_component_driver sma1303_component = {
	.probe = sma1303_probe,
	.remove = sma1303_remove,
	.controls = sma1303_snd_controls,
	.num_controls = ARRAY_SIZE(sma1303_snd_controls),
	.dapm_widgets = sma1303_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sma1303_dapm_widgets),
	.dapm_routes = sma1303_audio_map,
	.num_dapm_routes = ARRAY_SIZE(sma1303_audio_map),
};

const struct regmap_config sma_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = SMA1303_FF_DEVICE_INDEX,
	.readable_reg = sma1303_readable_register,
	.writeable_reg = sma1303_writeable_register,
	.volatile_reg = sma1303_volatile_register,

	.cache_type = REGCACHE_NONE,
	.reg_defaults = sma1303_reg_def,
	.num_reg_defaults = ARRAY_SIZE(sma1303_reg_def),
};

static ssize_t check_fault_period_show(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sma1303_priv *sma1303 = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%ld\n", sma1303->check_fault_period);
}

static ssize_t check_fault_period_store(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count)
{
	struct sma1303_priv *sma1303 = dev_get_drvdata(dev);
	int ret;

	ret = kstrtol(buf, 10, &sma1303->check_fault_period);

	if (ret)
		return -EINVAL;

	return (ssize_t)count;
}

static DEVICE_ATTR_RW(check_fault_period);

static ssize_t check_fault_status_show(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sma1303_priv *sma1303 = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%ld\n", sma1303->check_fault_status);
}

static ssize_t check_fault_status_store(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count)
{
	struct sma1303_priv *sma1303 = dev_get_drvdata(dev);
	int ret;

	ret = kstrtol(buf, 10, &sma1303->check_fault_status);

	if (ret)
		return -EINVAL;

	if (sma1303->check_fault_status) {
		if (sma1303->check_fault_period > 0)
			queue_delayed_work(system_freezable_wq,
				&sma1303->check_fault_work,
					sma1303->check_fault_period * HZ);
		else
			queue_delayed_work(system_freezable_wq,
				&sma1303->check_fault_work,
					CHECK_PERIOD_TIME * HZ);
	}

	return (ssize_t)count;
}

static DEVICE_ATTR_RW(check_fault_status);

static struct attribute *sma1303_attr[] = {
	&dev_attr_check_fault_period.attr,
	&dev_attr_check_fault_status.attr,
	NULL,
};

static struct attribute_group sma1303_attr_group = {
	.attrs = sma1303_attr,
};

static int sma1303_i2c_probe(struct i2c_client *client)
{
	struct sma1303_priv *sma1303;
	int ret, i = 0;
	unsigned int device_info, status, otp_stat;

	sma1303 = devm_kzalloc(&client->dev,
				sizeof(struct sma1303_priv), GFP_KERNEL);
	if (!sma1303)
		return -ENOMEM;
	sma1303->dev = &client->dev;

	sma1303->regmap = devm_regmap_init_i2c(client, &sma_i2c_regmap);
	if (IS_ERR(sma1303->regmap)) {
		ret = PTR_ERR(sma1303->regmap);
		dev_err(&client->dev,
			"Failed to allocate register map: %d\n", ret);

		return ret;
	}

	ret = sma1303_regmap_read(sma1303,
			SMA1303_FF_DEVICE_INDEX, &device_info);

	if ((ret != 0) || ((device_info & 0xF8) != SMA1303_DEVICE_ID)) {
		dev_err(&client->dev, "device initialization error (%d 0x%02X)",
				ret, device_info);
	}
	dev_dbg(&client->dev, "chip version 0x%02X\n", device_info);

	ret += sma1303_regmap_update_bits(sma1303,
			SMA1303_00_SYSTEM_CTRL,
			SMA1303_RESETBYI2C_MASK, SMA1303_RESETBYI2C_RESET,
			NULL);

	ret += sma1303_regmap_read(sma1303, SMA1303_FF_DEVICE_INDEX, &status);
	sma1303->rev_num = status & SMA1303_REV_NUM_STATUS;
	if (sma1303->rev_num == SMA1303_REV_NUM_TV0)
		dev_dbg(&client->dev, "SMA1303 Trimming Version 0\n");
	else if (sma1303->rev_num == SMA1303_REV_NUM_TV1)
		dev_dbg(&client->dev, "SMA1303 Trimming Version 1\n");

	ret += sma1303_regmap_read(sma1303, SMA1303_FB_STATUS2, &otp_stat);
	if (ret < 0)
		dev_err(&client->dev,
			"failed to read, register: %02X, ret: %d\n",
				SMA1303_FF_DEVICE_INDEX, ret);

	if (((sma1303->rev_num == SMA1303_REV_NUM_TV0) &&
		((otp_stat & 0x0E) == SMA1303_OTP_STAT_OK_0)) ||
		((sma1303->rev_num != SMA1303_REV_NUM_TV0) &&
		((otp_stat & 0x0C) == SMA1303_OTP_STAT_OK_1)))
		dev_dbg(&client->dev, "SMA1303 OTP Status Successful\n");
	else
		dev_dbg(&client->dev, "SMA1303 OTP Status Fail\n");

	for (i = 0; i < (unsigned int)ARRAY_SIZE(sma1303_reg_def); i++)
		ret += sma1303_regmap_write(sma1303,
				sma1303_reg_def[i].reg,
				sma1303_reg_def[i].def);

	sma1303->amp_mode = SMA1303_MONO;
	sma1303->amp_power_status = false;
	sma1303->check_fault_period = CHECK_PERIOD_TIME;
	sma1303->check_fault_status = true;
	sma1303->force_mute_status = false;
	sma1303->init_vol = 0x31;
	sma1303->cur_vol = sma1303->init_vol;
	sma1303->last_bclk = 0;
	sma1303->last_ocp_val = 0x08;
	sma1303->last_over_temp = 0xC0;
	sma1303->tsdw_cnt = 0;
	sma1303->retry_cnt = SMA1303_I2C_RETRY_COUNT;
	sma1303->tdm_slot_rx = 0;
	sma1303->tdm_slot_tx = 0;
	sma1303->sys_clk_id = SMA1303_PLL_CLKIN_BCLK;

	sma1303->dev = &client->dev;
	sma1303->kobj = &client->dev.kobj;

	INIT_DELAYED_WORK(&sma1303->check_fault_work,
		sma1303_check_fault_worker);

	i2c_set_clientdata(client, sma1303);

	sma1303->pll_matches = sma1303_pll_matches;
	sma1303->num_of_pll_matches =
		ARRAY_SIZE(sma1303_pll_matches);

	ret = devm_snd_soc_register_component(&client->dev,
			&sma1303_component, sma1303_dai, 1);
	if (ret) {
		dev_err(&client->dev, "Failed to register component");

		return ret;
	}

	sma1303->attr_grp = &sma1303_attr_group;
	ret = sysfs_create_group(sma1303->kobj, sma1303->attr_grp);
	if (ret) {
		dev_err(&client->dev,
			"failed to create attribute group [%d]\n", ret);
		sma1303->attr_grp = NULL;
	}

	return ret;
}

static void sma1303_i2c_remove(struct i2c_client *client)
{
	struct sma1303_priv *sma1303 =
		(struct sma1303_priv *) i2c_get_clientdata(client);

	cancel_delayed_work_sync(&sma1303->check_fault_work);
}

static const struct i2c_device_id sma1303_i2c_id[] = {
	{"sma1303", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, sma1303_i2c_id);

static const struct of_device_id sma1303_of_match[] = {
	{ .compatible = "irondevice,sma1303", },
	{ }
};
MODULE_DEVICE_TABLE(of, sma1303_of_match);

static struct i2c_driver sma1303_i2c_driver = {
	.driver = {
		.name = "sma1303",
		.of_match_table = sma1303_of_match,
	},
	.probe_new = sma1303_i2c_probe,
	.remove = sma1303_i2c_remove,
	.id_table = sma1303_i2c_id,
};

module_i2c_driver(sma1303_i2c_driver);

MODULE_DESCRIPTION("ALSA SoC SMA1303 driver");
MODULE_AUTHOR("Gyuhwa Park, <gyuhwa.park@irondevice.com>");
MODULE_AUTHOR("Kiseok Jo, <kiseok.jo@irondevice.com>");
MODULE_LICENSE("GPL v2");
