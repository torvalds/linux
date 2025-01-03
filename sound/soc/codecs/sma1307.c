// SPDX-License-Identifier: GPL-2.0-or-later
// sma1307.c -- sma1307 ALSA SoC Audio driver
//
// Copyright 2024 Iron Device Corporation
//
// Auther: Gyuhwa Park <gyuwha.park@irondevice.com>
// Auther: Kiseok Jo <kiseok.jo@irondevice.com>

#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include "sma1307.h"

#define CHECK_PERIOD_TIME 1	/* sec per HZ */
#define PLL_MATCH(_input_clk_name, _output_clk_name, _input_clk,\
		_post_n, _n, _vco,  _p_cp)\
{\
	.input_clk_name		= _input_clk_name,\
	.output_clk_name	= _output_clk_name,\
	.input_clk		= _input_clk,\
	.post_n			= _post_n,\
	.n			= _n,\
	.vco			= _vco,\
	.p_cp			= _p_cp,\
}

static const char *setting_file = "sma1307_setting.bin";
#define SMA1307_SETTING_CHECKSUM 0x100000

/* PLL clock setting Table */
struct sma1307_pll_match {
	char *input_clk_name;
	char *output_clk_name;
	unsigned int input_clk;
	unsigned int post_n;
	unsigned int n;
	unsigned int vco;
	unsigned int p_cp;
};

struct sma1307_data {
	char *name;
	void (*init)(struct regmap *regmap);
};

struct sma1307_priv {
	bool check_fault_status;
	bool force_mute_status;
	bool sw_ot1_prot;
	char *name;
	enum sma1307_mode amp_mode;
	int binary_mode;
	int dapm_aif_in;
	int dapm_aif_out0;
	int dapm_aif_out1;
	int dapm_sdo_en;
	int dapm_sdo_setting;
	int num_of_pll_matches;
	int check_fault_period;
	struct delayed_work check_fault_work;
	struct device *dev;
	struct kobject *kobj;
	struct mutex default_lock;
	struct regmap *regmap;
	struct sma1307_setting_file set;
	const struct sma1307_pll_match *pll_matches;
	const struct sma1307_data *data;
	unsigned int cur_vol;
	unsigned int format;
	unsigned int frame_size;
	unsigned int init_vol;
	unsigned int last_bclk;
	unsigned int otp_trm2;
	unsigned int otp_trm3;
	unsigned int rev_num;
	unsigned int sys_clk_id;
	unsigned int tdm_slot0_rx;
	unsigned int tdm_slot1_rx;
	unsigned int tdm_slot0_tx;
	unsigned int tdm_slot1_tx;
	unsigned int tsdw_cnt;
};

static const struct sma1307_pll_match sma1307_pll_matches[] = {
	/* in_clk_name, out_clk_name, input_clk post_n, n, vco, p_cp */
	PLL_MATCH("1.411MHz", "24.554MHz",
		  1411200, 0x06, 0xD1, 0x88, 0x00),
	PLL_MATCH("1.536MHz", "24.576MHz",
		  1536000, 0x06, 0xC0, 0x88, 0x00),
	PLL_MATCH("2.822MHz", "24.554MHz",
		  2822400, 0x06, 0xD1, 0x88, 0x04),
	PLL_MATCH("3.072MHz", "24.576MHz",
		  3072000, 0x06, 0x60, 0x88, 0x00),
	PLL_MATCH("6.144MHz", "24.576MHz",
		  6144000, 0x06, 0x60, 0x88, 0x04),
	PLL_MATCH("12.288MHz", "24.576MHz",
		  12288000, 0x06, 0x60, 0x88, 0x08),
	PLL_MATCH("19.2MHz", "24.48MHz",
		  19200000, 0x06, 0x7B, 0x88, 0x0C),
	PLL_MATCH("24.576MHz", "24.576MHz",
		  24576000, 0x06, 0x60, 0x88, 0x0C),
};

static struct snd_soc_component *sma1307_amp_component;

static void sma1307_startup(struct snd_soc_component *);
static void sma1307_shutdown(struct snd_soc_component *);
static void sma1307_reset(struct snd_soc_component *);
static void sma1307_set_binary(struct snd_soc_component *);
static void sma1307_set_default(struct snd_soc_component *);

/* Initial register value - 6.0W SPK (8ohm load)  */
static const struct reg_default sma1307_reg_def[] = {
	{ 0x00, 0x80 },
	{ 0x01, 0x00 },
	{ 0x02, 0x52 },
	{ 0x03, 0x4C },
	{ 0x04, 0x47 },
	{ 0x05, 0x42 },
	{ 0x06, 0x40 },
	{ 0x07, 0x40 },
	{ 0x08, 0x3C },
	{ 0x09, 0x2F },
	{ 0x0A, 0x32 },
	{ 0x0B, 0x50 },
	{ 0x0C, 0x8C },
	{ 0x0D, 0x00 },
	{ 0x0E, 0x3F },
	{ 0x0F, 0x00 },
	{ 0x10, 0x00 },
	{ 0x11, 0x00 },
	{ 0x12, 0x00 },
	{ 0x13, 0x09 },
	{ 0x14, 0x12 },
	{ 0x1C, 0x00 },
	{ 0x1D, 0x85 },
	{ 0x1E, 0xA1 },
	{ 0x1F, 0x67 },
	{ 0x22, 0x00 },
	{ 0x23, 0x1F },
	{ 0x24, 0x7A },
	{ 0x25, 0x00 },
	{ 0x26, 0xFF },
	{ 0x27, 0x39 },
	{ 0x28, 0x54 },
	{ 0x29, 0x92 },
	{ 0x2A, 0xB0 },
	{ 0x2B, 0xED },
	{ 0x2C, 0xED },
	{ 0x2D, 0xFF },
	{ 0x2E, 0xFF },
	{ 0x2F, 0xFF },
	{ 0x30, 0xFF },
	{ 0x31, 0xFF },
	{ 0x32, 0xFF },
	{ 0x34, 0x01 },
	{ 0x35, 0x17 },
	{ 0x36, 0x92 },
	{ 0x37, 0x00 },
	{ 0x38, 0x01 },
	{ 0x39, 0x10 },
	{ 0x3E, 0x01 },
	{ 0x3F, 0x08 },
	{ 0x8B, 0x05 },
	{ 0x8C, 0x50 },
	{ 0x8D, 0x80 },
	{ 0x8E, 0x10 },
	{ 0x8F, 0x02 },
	{ 0x90, 0x02 },
	{ 0x91, 0x83 },
	{ 0x92, 0xC0 },
	{ 0x93, 0x00 },
	{ 0x94, 0xA4 },
	{ 0x95, 0x74 },
	{ 0x96, 0x57 },
	{ 0xA2, 0xCC },
	{ 0xA3, 0x28 },
	{ 0xA4, 0x40 },
	{ 0xA5, 0x01 },
	{ 0xA6, 0x41 },
	{ 0xA7, 0x08 },
	{ 0xA8, 0x04 },
	{ 0xA9, 0x27 },
	{ 0xAA, 0x10 },
	{ 0xAB, 0x10 },
	{ 0xAC, 0x10 },
	{ 0xAD, 0x0F },
	{ 0xAE, 0xCD },
	{ 0xAF, 0x70 },
	{ 0xB0, 0x03 },
	{ 0xB1, 0xEF },
	{ 0xB2, 0x03 },
	{ 0xB3, 0xEF },
	{ 0xB4, 0xF3 },
	{ 0xB5, 0x3D },
};

static bool sma1307_readable_register(struct device *dev, unsigned int reg)
{
	if (reg > SMA1307_FF_DEVICE_INDEX)
		return false;

	switch (reg) {
	case SMA1307_00_SYSTEM_CTRL ... SMA1307_1F_TONE_FINE_VOLUME:
	case SMA1307_22_COMP_HYS_SEL ... SMA1307_32_BROWN_OUT_PROT19:
	case SMA1307_34_OCP_SPK ... SMA1307_39_PMT_NZ_VAL:
	case SMA1307_3B_TEST1 ... SMA1307_3F_ATEST2:
	case SMA1307_8B_PLL_POST_N ... SMA1307_9A_OTP_TRM3:
	case SMA1307_A0_PAD_CTRL0 ... SMA1307_BE_MCBS_CTRL2:
	case SMA1307_F5_READY_FOR_V_SAR:
	case SMA1307_F7_READY_FOR_T_SAR ... SMA1307_FF_DEVICE_INDEX:
		break;
	default:
		return false;
	}
	return true;
}

static bool sma1307_writeable_register(struct device *dev, unsigned int reg)
{
	if (reg > SMA1307_FF_DEVICE_INDEX)
		return false;

	switch (reg) {
	case SMA1307_00_SYSTEM_CTRL ... SMA1307_1F_TONE_FINE_VOLUME:
	case SMA1307_22_COMP_HYS_SEL ... SMA1307_32_BROWN_OUT_PROT19:
	case SMA1307_34_OCP_SPK ... SMA1307_39_PMT_NZ_VAL:
	case SMA1307_3B_TEST1 ... SMA1307_3F_ATEST2:
	case SMA1307_8B_PLL_POST_N ... SMA1307_9A_OTP_TRM3:
	case SMA1307_A0_PAD_CTRL0 ... SMA1307_BE_MCBS_CTRL2:
		break;
	default:
		return false;
	}
	return true;
}

static bool sma1307_volatile_register(struct device *dev, unsigned int reg)
{
	if (reg > SMA1307_FF_DEVICE_INDEX)
		return false;

	switch (reg) {
	case SMA1307_F8_STATUS_T1 ... SMA1307_FF_DEVICE_INDEX:
		break;
	default:
		return false;
	}
	return true;
}

/* DB scale conversion of speaker volume */
static const DECLARE_TLV_DB_SCALE(sma1307_spk_tlv, -6000, 50, 0);

static const char *const sma1307_aif_in_source_text[] = {
	"Mono", "Left", "Right"
};

static const char *const sma1307_sdo_setting_text[] = {
	"Data_One_48k", "Data_Two_48k", "Data_Two_24k",
	"Clk_PLL", "Clk_OSC"
};

static const char *const sma1307_aif_out_source_text[] = {
	"Disable", "After_FmtC", "After_Mixer", "After_DSP",
	"Vrms2_Avg", "Battery", "Temperature", "After_Delay"
};

static const char *const sma1307_tdm_slot_text[] = {
	"Slot0", "Slot1", "Slot2", "Slot3",
	"Slot4", "Slot5", "Slot6", "Slot7"
};

static const char *const sma1307_binary_mode_text[] = {
	"Mode0", "Mode1", "Mode2", "Mode3", "Mode4"
};

static const char *const sma1307_reset_text[] = {
	"Reset"
};

static const struct soc_enum sma1307_aif_in_source_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1307_aif_in_source_text),
		    sma1307_aif_in_source_text);
static const struct soc_enum sma1307_sdo_setting_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1307_sdo_setting_text),
		    sma1307_sdo_setting_text);
static const struct soc_enum sma1307_aif_out_source_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1307_aif_out_source_text),
		    sma1307_aif_out_source_text);
static const struct soc_enum sma1307_tdm_slot_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1307_tdm_slot_text),
		    sma1307_tdm_slot_text);
static const struct soc_enum sma1307_binary_mode_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1307_binary_mode_text),
		    sma1307_binary_mode_text);
static const struct soc_enum sma1307_reset_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(sma1307_reset_text),
		    sma1307_reset_text);

static int sma1307_force_mute_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
	    snd_soc_kcontrol_component(kcontrol);
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = (int)sma1307->force_mute_status;

	return 0;
}

static int sma1307_force_mute_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
	    snd_soc_kcontrol_component(kcontrol);
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);
	bool change = false, val = (bool)ucontrol->value.integer.value[0];

	if (sma1307->force_mute_status == val) {
		change = false;
	} else {
		change = true;
		sma1307->force_mute_status = val;
	}

	return change;
}

static int sma1307_tdm_slot_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
	    snd_soc_kcontrol_component(kcontrol);
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);
	int val1, val2;

	regmap_read(sma1307->regmap, SMA1307_A5_TDM1, &val1);
	regmap_read(sma1307->regmap, SMA1307_A6_TDM2, &val2);

	if (!strcmp(kcontrol->id.name, SMA1307_TDM_RX0_POS_NAME)) {
		ucontrol->value.integer.value[0]
		    = (val1 & SMA1307_TDM_SLOT0_RX_POS_MASK) >> 3;
		sma1307->tdm_slot0_rx = ucontrol->value.integer.value[0];
	} else if (!strcmp(kcontrol->id.name, SMA1307_TDM_RX1_POS_NAME)) {
		ucontrol->value.integer.value[0]
		    = val1 & SMA1307_TDM_SLOT1_RX_POS_MASK;
		sma1307->tdm_slot1_rx = ucontrol->value.integer.value[0];
	} else if (!strcmp(kcontrol->id.name, SMA1307_TDM_TX0_POS_NAME)) {
		ucontrol->value.integer.value[0]
		    = (val2 & SMA1307_TDM_SLOT0_TX_POS_MASK) >> 3;
		sma1307->tdm_slot0_tx = ucontrol->value.integer.value[0];
	} else if (!strcmp(kcontrol->id.name, SMA1307_TDM_TX1_POS_NAME)) {
		ucontrol->value.integer.value[0]
		    = val2 & SMA1307_TDM_SLOT1_TX_POS_MASK;
		sma1307->tdm_slot1_tx = ucontrol->value.integer.value[0];
	} else {
		return -EINVAL;
	}

	return 0;
}

static int sma1307_tdm_slot_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
	    snd_soc_kcontrol_component(kcontrol);
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);
	int val = (int)ucontrol->value.integer.value[0];
	bool change;

	if (!strcmp(kcontrol->id.name, SMA1307_TDM_RX0_POS_NAME)) {
		if (sma1307->tdm_slot0_rx == val)
			change = false;
		else {
			change = true;
			sma1307->tdm_slot0_rx = val;
			regmap_update_bits(sma1307->regmap, SMA1307_A5_TDM1,
				SMA1307_TDM_SLOT0_RX_POS_MASK, val << 3);
		}
	} else if (!strcmp(kcontrol->id.name, SMA1307_TDM_RX1_POS_NAME)) {
		if (sma1307->tdm_slot1_rx == val)
			change = false;
		else {
			change = true;
			sma1307->tdm_slot1_rx = val;
			regmap_update_bits(sma1307->regmap, SMA1307_A5_TDM1,
				SMA1307_TDM_SLOT1_RX_POS_MASK, val);
		}
	} else if (!strcmp(kcontrol->id.name, SMA1307_TDM_TX0_POS_NAME)) {
		if (sma1307->tdm_slot0_tx == val)
			change = false;
		else {
			change = true;
			sma1307->tdm_slot0_tx = val;
			regmap_update_bits(sma1307->regmap, SMA1307_A6_TDM2,
				SMA1307_TDM_SLOT0_TX_POS_MASK, val << 3);
		}
	} else if (!strcmp(kcontrol->id.name, SMA1307_TDM_TX1_POS_NAME)) {
		if (sma1307->tdm_slot1_tx == val)
			change = false;
		else {
			change = true;
			sma1307->tdm_slot1_tx = val;
			regmap_update_bits(sma1307->regmap, SMA1307_A6_TDM2,
				SMA1307_TDM_SLOT1_TX_POS_MASK, val);
		}
	} else {
		dev_err(sma1307->dev, "%s: Invalid Control ID - %s\n",
			__func__, kcontrol->id.name);
		return -EINVAL;
	}

	return change;
}

static int sma1307_sw_ot1_prot_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
	    snd_soc_kcontrol_component(kcontrol);
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = (int)sma1307->sw_ot1_prot;

	return 0;
}

static int sma1307_sw_ot1_prot_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
	    snd_soc_kcontrol_component(kcontrol);
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);
	bool change = false, val = (bool)ucontrol->value.integer.value[0];

	if (sma1307->sw_ot1_prot == val)
		change = false;
	else {
		change = true;
		sma1307->sw_ot1_prot = val;
	}

	return change;
}

static int sma1307_check_fault_status_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
	    snd_soc_kcontrol_component(kcontrol);
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = (int)sma1307->check_fault_status;

	return 0;
}

static int sma1307_check_fault_status_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
	    snd_soc_kcontrol_component(kcontrol);
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);
	bool change = false, val = (bool)ucontrol->value.integer.value[0];

	if (sma1307->check_fault_status == val) {
		change = false;
	} else {
		change = true;
		sma1307->check_fault_status = val;
	}

	return change;
}

static int sma1307_check_fault_period_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
	    snd_soc_kcontrol_component(kcontrol);
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = sma1307->check_fault_period;

	return 0;
}

static int sma1307_check_fault_period_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
	    snd_soc_kcontrol_component(kcontrol);
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	bool change = false;
	int val  = ucontrol->value.integer.value[0];

	if (val < mc->min || val > mc->max)
		return -EINVAL;
	if (sma1307->check_fault_period == val) {
		change = false;
	} else {
		change = true;
		sma1307->check_fault_period = val;
	}

	return change;
}

static int sma1307_reset_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
	    snd_soc_kcontrol_component(kcontrol);
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);

	regmap_update_bits(sma1307->regmap, SMA1307_00_SYSTEM_CTRL,
				   SMA1307_RESET_MASK, SMA1307_RESET_ON);
	sma1307_reset(component);

	snd_ctl_notify(component->card->snd_card, SNDRV_CTL_EVENT_MASK_VALUE,
					&kcontrol->id);

	return true;
}

static int sma1307_binary_mode_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
	    snd_soc_kcontrol_component(kcontrol);
	struct sma1307_priv *sma1307 = snd_kcontrol_chip(kcontrol);

	sma1307->binary_mode = (int)ucontrol->value.enumerated.item[0];
	if (sma1307->set.status)
		sma1307_set_binary(component);

	return snd_soc_put_enum_double(kcontrol, ucontrol);
}

static void sma1307_startup(struct snd_soc_component *component)
{
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);

	regmap_update_bits(sma1307->regmap, SMA1307_A2_TOP_MAN1,
				   SMA1307_PLL_MASK, SMA1307_PLL_ON);
	regmap_update_bits(sma1307->regmap, SMA1307_00_SYSTEM_CTRL,
				   SMA1307_POWER_MASK, SMA1307_POWER_ON);

	if (sma1307->amp_mode == SMA1307_MONO_MODE) {
		regmap_update_bits(sma1307->regmap,
					   SMA1307_10_SYSTEM_CTRL1,
					   SMA1307_SPK_MODE_MASK,
					   SMA1307_SPK_MONO);
	} else {
		regmap_update_bits(sma1307->regmap,
					   SMA1307_10_SYSTEM_CTRL1,
					   SMA1307_SPK_MODE_MASK,
					   SMA1307_SPK_STEREO);
	}

	if (sma1307->check_fault_status) {
		if (sma1307->check_fault_period > 0)
			queue_delayed_work(system_freezable_wq,
					   &sma1307->check_fault_work,
					   sma1307->check_fault_period * HZ);
		else
			queue_delayed_work(system_freezable_wq,
					   &sma1307->check_fault_work,
					   CHECK_PERIOD_TIME * HZ);
	}
}

static void sma1307_shutdown(struct snd_soc_component *component)
{
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);

	/* for SMA1307A */
	cancel_delayed_work_sync(&sma1307->check_fault_work);

	regmap_update_bits(sma1307->regmap, SMA1307_0E_MUTE_VOL_CTRL,
				   SMA1307_SPK_MUTE_MASK, SMA1307_SPK_MUTE);
	/* Need to wait time for mute slope */
	msleep(55);

	regmap_update_bits(sma1307->regmap, SMA1307_10_SYSTEM_CTRL1,
				   SMA1307_SPK_MODE_MASK, SMA1307_SPK_OFF);
	regmap_update_bits(sma1307->regmap, SMA1307_A2_TOP_MAN1,
				   SMA1307_PLL_MASK, SMA1307_PLL_OFF);
	regmap_update_bits(sma1307->regmap, SMA1307_00_SYSTEM_CTRL,
				   SMA1307_POWER_MASK, SMA1307_POWER_OFF);
}

static int sma1307_aif_in_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
	    snd_soc_dapm_to_component(w->dapm);
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);
	unsigned int mux = sma1307->dapm_aif_in;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		switch (mux) {
		case SMA1307_MONO_MODE:
			regmap_update_bits(sma1307->regmap,
						   SMA1307_11_SYSTEM_CTRL2,
						   SMA1307_MONOMIX_MASK,
						   SMA1307_MONOMIX_ON);
			break;
		case SMA1307_LEFT_MODE:
			regmap_update_bits(sma1307->regmap,
						   SMA1307_11_SYSTEM_CTRL2,
						   SMA1307_MONOMIX_MASK,
						   SMA1307_MONOMIX_OFF);
			regmap_update_bits(sma1307->regmap,
						   SMA1307_11_SYSTEM_CTRL2,
						   SMA1307_LR_DATA_SW_MASK,
						   SMA1307_LR_DATA_SW_NORMAL);
			break;
		case SMA1307_RIGHT_MODE:
			regmap_update_bits(sma1307->regmap,
						   SMA1307_11_SYSTEM_CTRL2,
						   SMA1307_MONOMIX_MASK,
						   SMA1307_MONOMIX_OFF);
			regmap_update_bits(sma1307->regmap,
						   SMA1307_11_SYSTEM_CTRL2,
						   SMA1307_LR_DATA_SW_MASK,
						   SMA1307_LR_DATA_SW_SWAP);
			break;
		default:

			dev_err(sma1307->dev, "%s: Invalid value (%d)\n",
				__func__, mux);
			return -EINVAL;
		}
		sma1307->amp_mode = mux;
		break;
	}
	return 0;
}

static int sma1307_sdo_setting_event(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
	    snd_soc_dapm_to_component(w->dapm);
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);
	unsigned int mux = sma1307->dapm_sdo_setting;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		switch (mux) {
		case SMA1307_OUT_DATA_ONE_48K:
			regmap_update_bits(sma1307->regmap,
						   SMA1307_A2_TOP_MAN1,
						   SMA1307_SDO_OUTPUT2_MASK,
						   SMA1307_ONE_SDO_PER_CH);
			regmap_update_bits(sma1307->regmap,
						   SMA1307_A3_TOP_MAN2,
						   SMA1307_SDO_OUTPUT3_MASK
						   |
						   SMA1307_DATA_CLK_SEL_MASK,
						   SMA1307_SDO_OUTPUT3_DIS
						   | SMA1307_SDO_DATA);
			break;
		case SMA1307_OUT_DATA_TWO_48K:
			regmap_update_bits(sma1307->regmap,
						   SMA1307_A2_TOP_MAN1,
						   SMA1307_SDO_OUTPUT2_MASK,
						   SMA1307_TWO_SDO_PER_CH);
			regmap_update_bits(sma1307->regmap,
						   SMA1307_A3_TOP_MAN2,
						   SMA1307_SDO_OUTPUT3_MASK
						   |
						   SMA1307_DATA_CLK_SEL_MASK,
						   SMA1307_SDO_OUTPUT3_DIS
						   | SMA1307_SDO_DATA);
			break;
		case SMA1307_OUT_DATA_TWO_24K:
			regmap_update_bits(sma1307->regmap,
						   SMA1307_A2_TOP_MAN1,
						   SMA1307_SDO_OUTPUT2_MASK,
						   SMA1307_TWO_SDO_PER_CH);
			regmap_update_bits(sma1307->regmap,
						   SMA1307_A3_TOP_MAN2,
						   SMA1307_SDO_OUTPUT3_MASK
						   |
						   SMA1307_DATA_CLK_SEL_MASK,
						   SMA1307_TWO_SDO_PER_CH_24K
						   | SMA1307_SDO_DATA);
			break;
		case SMA1307_OUT_CLK_PLL:
			regmap_update_bits(sma1307->regmap,
						   SMA1307_A3_TOP_MAN2,
						   SMA1307_DATA_CLK_SEL_MASK,
						   SMA1307_SDO_CLK_PLL);

			break;
		case SMA1307_OUT_CLK_OSC:
			regmap_update_bits(sma1307->regmap,
						   SMA1307_A3_TOP_MAN2,
						   SMA1307_DATA_CLK_SEL_MASK,
						   SMA1307_SDO_CLK_OSC);

			break;
		default:
			dev_err(sma1307->dev, "%s: Invalid value (%d)\n",
				__func__, mux);
			return -EINVAL;
		}
		break;
	}
	return 0;
}

static int sma1307_aif_out_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
	    snd_soc_dapm_to_component(w->dapm);
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);
	unsigned int mux = 0, val = 0, mask = 0;

	if (!strcmp(w->name, SMA1307_AIF_OUT0_NAME)) {
		mux = sma1307->dapm_aif_out0;
		val = mux;
		mask = SMA1307_SDO_OUT0_SEL_MASK;
	} else if (!strcmp(w->name, SMA1307_AIF_OUT1_NAME)) {
		mux = sma1307->dapm_aif_out1;
		val = mux << 3;
		mask = SMA1307_SDO_OUT1_SEL_MASK;
	} else {
		dev_err(sma1307->dev, "%s: Invalid widget - %s\n",
			__func__, w->name);
		return -EINVAL;
	}
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_update_bits(sma1307->regmap, SMA1307_09_OUTPUT_CTRL,
					   mask, val);
		break;
	}
	return 0;
}

static int sma1307_sdo_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
	    snd_soc_dapm_to_component(w->dapm);
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_update_bits(sma1307->regmap,
					   SMA1307_09_OUTPUT_CTRL,
					   SMA1307_PORT_CONFIG_MASK,
					   SMA1307_OUTPUT_PORT_ENABLE);
		regmap_update_bits(sma1307->regmap,
					   SMA1307_A3_TOP_MAN2,
					   SMA1307_SDO_OUTPUT_MASK,
					   SMA1307_LOGIC_OUTPUT);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(sma1307->regmap,
					   SMA1307_09_OUTPUT_CTRL,
					   SMA1307_PORT_CONFIG_MASK,
					   SMA1307_INPUT_PORT_ONLY);
		regmap_update_bits(sma1307->regmap,
					   SMA1307_A3_TOP_MAN2,
					   SMA1307_SDO_OUTPUT_MASK,
					   SMA1307_HIGH_Z_OUTPUT);
		break;
	}
	return 0;
}

static int sma1307_power_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
	    snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		sma1307_startup(component);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		sma1307_shutdown(component);
		break;
	}
	return 0;
}

static int sma1307_dapm_aif_in_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm =
	    snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct sma1307_priv *sma1307 =
	    snd_soc_component_get_drvdata(dapm->component);

	ucontrol->value.enumerated.item[0] = (unsigned int)sma1307->dapm_aif_in;
	snd_soc_dapm_put_enum_double(kcontrol, ucontrol);

	return 0;
}

static int sma1307_dapm_aif_in_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm =
	    snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct sma1307_priv *sma1307 =
	    snd_soc_component_get_drvdata(dapm->component);
	int val = (int)ucontrol->value.enumerated.item[0];
	bool change;

	if ((val < 0) || (val >= ARRAY_SIZE(sma1307_aif_in_source_text))) {
		dev_err(sma1307->dev, "%s: Out of range\n", __func__);
		return -EINVAL;
	}

	if (sma1307->dapm_aif_in != val) {
		change = true;
		sma1307->dapm_aif_in = val;
	} else
		change = false;

	snd_soc_dapm_put_enum_double(kcontrol, ucontrol);

	return change;
}

static int sma1307_dapm_sdo_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm =
	    snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct sma1307_priv *sma1307 =
	    snd_soc_component_get_drvdata(dapm->component);

	ucontrol->value.enumerated.item[0] =
	    (unsigned int)sma1307->dapm_sdo_setting;
	snd_soc_dapm_put_enum_double(kcontrol, ucontrol);

	return 0;
}

static int sma1307_dapm_sdo_setting_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm =
	    snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct sma1307_priv *sma1307 =
	    snd_soc_component_get_drvdata(dapm->component);
	int val = (int)ucontrol->value.enumerated.item[0];
	bool change;

	if ((val < 0) || (val >= ARRAY_SIZE(sma1307_sdo_setting_text))) {
		dev_err(sma1307->dev, "%s: Out of range\n", __func__);
		return -EINVAL;
	}

	if (sma1307->dapm_sdo_setting != val) {
		change = true;
		sma1307->dapm_sdo_setting = val;
	} else
		change = false;

	snd_soc_dapm_put_enum_double(kcontrol, ucontrol);

	return change;
}

static int sma1307_dapm_aif_out_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm =
	    snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct sma1307_priv *sma1307 =
	    snd_soc_component_get_drvdata(dapm->component);
	unsigned int val = 0;

	if (!strcmp(kcontrol->id.name, SMA1307_AIF_OUT0_NAME)) {
		val = (unsigned int)sma1307->dapm_aif_out0;
	} else if (!strcmp(kcontrol->id.name, SMA1307_AIF_OUT1_NAME)) {
		val = (unsigned int)sma1307->dapm_aif_out1;
	} else {
		dev_err(sma1307->dev, "%s: Invalid Control ID - %s\n",
			__func__, kcontrol->id.name);
		return -EINVAL;
	}
	ucontrol->value.enumerated.item[0] = val;
	snd_soc_dapm_put_enum_double(kcontrol, ucontrol);

	return 0;
}

static int sma1307_dapm_aif_out_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm =
	    snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct sma1307_priv *sma1307 =
	    snd_soc_component_get_drvdata(dapm->component);
	int val = (int)ucontrol->value.enumerated.item[0];
	bool change;

	if ((val < 0) || (val >= ARRAY_SIZE(sma1307_aif_out_source_text))) {
		dev_err(sma1307->dev, "%s: Out of range\n", __func__);
		return -EINVAL;
	}

	if (!strcmp(kcontrol->id.name, SMA1307_AIF_OUT0_NAME)) {
		if (sma1307->dapm_aif_out0 != val) {
			change = true;
			sma1307->dapm_aif_out0 = val;
		} else
			change = false;
	} else if (!strcmp(kcontrol->id.name, SMA1307_AIF_OUT1_NAME)) {
		if (sma1307->dapm_aif_out1 != val) {
			change = true;
			sma1307->dapm_aif_out1 = val;
		} else
			change = false;
	} else {
		dev_err(sma1307->dev, "%s: Invalid Control ID - %s\n",
			__func__, kcontrol->id.name);
		return -EINVAL;
	}

	snd_soc_dapm_put_enum_double(kcontrol, ucontrol);

	return change;
}

static int sma1307_dapm_sdo_enable_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm =
	    snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct sma1307_priv *sma1307 =
	    snd_soc_component_get_drvdata(dapm->component);

	ucontrol->value.integer.value[0] = (long)sma1307->dapm_sdo_en;
	snd_soc_dapm_put_volsw(kcontrol, ucontrol);

	return 0;
}

static int sma1307_dapm_sdo_enable_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm =
	    snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct sma1307_priv *sma1307 =
	    snd_soc_component_get_drvdata(dapm->component);
	int val = (int)ucontrol->value.integer.value[0];
	bool change;

	if ((val < 0) || (val > 1)) {
		dev_err(sma1307->dev, "%s: Out of range\n", __func__);
		return -EINVAL;
	}

	if (sma1307->dapm_sdo_en != val) {
		change = true;
		sma1307->dapm_sdo_en = val;
	} else
		change = false;

	snd_soc_dapm_put_volsw(kcontrol, ucontrol);

	return change;
}

static const struct snd_kcontrol_new sma1307_aif_in_source_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = SMA1307_AIF_IN_NAME,
	.info = snd_soc_info_enum_double,
	.get = sma1307_dapm_aif_in_get,
	.put = sma1307_dapm_aif_in_put,
	.private_value = (unsigned long)&sma1307_aif_in_source_enum
};

static const struct snd_kcontrol_new sma1307_sdo_setting_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "SDO Setting",
	.info = snd_soc_info_enum_double,
	.get = sma1307_dapm_sdo_setting_get,
	.put = sma1307_dapm_sdo_setting_put,
	.private_value = (unsigned long)&sma1307_sdo_setting_enum
};

static const struct snd_kcontrol_new sma1307_aif_out0_source_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = SMA1307_AIF_OUT0_NAME,
	.info = snd_soc_info_enum_double,
	.get = sma1307_dapm_aif_out_get,
	.put = sma1307_dapm_aif_out_put,
	.private_value = (unsigned long)&sma1307_aif_out_source_enum
};

static const struct snd_kcontrol_new sma1307_aif_out1_source_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = SMA1307_AIF_OUT1_NAME,
	.info = snd_soc_info_enum_double,
	.get = sma1307_dapm_aif_out_get,
	.put = sma1307_dapm_aif_out_put,
	.private_value = (unsigned long)&sma1307_aif_out_source_enum
};

static const struct snd_kcontrol_new sma1307_sdo_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Switch",
	.info = snd_soc_info_volsw,
	.get = sma1307_dapm_sdo_enable_get,
	.put = sma1307_dapm_sdo_enable_put,
	.private_value = SOC_SINGLE_VALUE(SND_SOC_NOPM, 0, 1, 0, 0)
};

static const struct snd_kcontrol_new sma1307_enable_control =
	SOC_DAPM_SINGLE("Switch", SMA1307_00_SYSTEM_CTRL, 0, 1, 0);

static const struct snd_kcontrol_new sma1307_binary_mode_control[] = {
	SOC_ENUM_EXT("Binary Mode", sma1307_binary_mode_enum,
			snd_soc_get_enum_double, sma1307_binary_mode_put),
};

static const struct snd_kcontrol_new sma1307_snd_controls[] = {
	SOC_SINGLE_TLV(SMA1307_VOL_CTRL_NAME, SMA1307_0A_SPK_VOL,
		       0, 167, 1, sma1307_spk_tlv),
	SOC_ENUM_EXT(SMA1307_TDM_RX0_POS_NAME, sma1307_tdm_slot_enum,
		     sma1307_tdm_slot_get, sma1307_tdm_slot_put),
	SOC_ENUM_EXT(SMA1307_TDM_RX1_POS_NAME, sma1307_tdm_slot_enum,
		     sma1307_tdm_slot_get, sma1307_tdm_slot_put),
	SOC_ENUM_EXT(SMA1307_TDM_TX0_POS_NAME, sma1307_tdm_slot_enum,
		     sma1307_tdm_slot_get, sma1307_tdm_slot_put),
	SOC_ENUM_EXT(SMA1307_TDM_TX1_POS_NAME, sma1307_tdm_slot_enum,
		     sma1307_tdm_slot_get, sma1307_tdm_slot_put),
	SOC_ENUM_EXT(SMA1307_RESET_CTRL_NAME, sma1307_reset_enum,
			    snd_soc_get_enum_double, sma1307_reset_put),
	SOC_SINGLE_BOOL_EXT(SMA1307_FORCE_MUTE_CTRL_NAME, 0,
			    sma1307_force_mute_get, sma1307_force_mute_put),
	SOC_SINGLE_BOOL_EXT(SMA1307_OT1_SW_PROT_CTRL_NAME, 0,
			    sma1307_sw_ot1_prot_get, sma1307_sw_ot1_prot_put),
	SOC_SINGLE_BOOL_EXT(SMA1307_CHECK_FAULT_STATUS_NAME, 0,
			sma1307_check_fault_status_get,
			sma1307_check_fault_status_put),
	SOC_SINGLE_EXT(SMA1307_CHECK_FAULT_PERIOD_NAME, SND_SOC_NOPM, 0, 600, 0,
			sma1307_check_fault_period_get,
			sma1307_check_fault_period_put),
};

static const struct snd_soc_dapm_widget sma1307_dapm_widgets[] = {
	/* platform domain */
	SND_SOC_DAPM_OUTPUT("SPK"),
	SND_SOC_DAPM_INPUT("SDO"),

	/* path domain */
	SND_SOC_DAPM_MUX_E(SMA1307_AIF_IN_NAME, SND_SOC_NOPM, 0, 0,
			   &sma1307_aif_in_source_control,
			   sma1307_aif_in_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MUX_E("SDO Setting", SND_SOC_NOPM, 0, 0,
			   &sma1307_sdo_setting_control,
			   sma1307_sdo_setting_event,
			   SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_MUX_E(SMA1307_AIF_OUT0_NAME, SND_SOC_NOPM, 0, 0,
			   &sma1307_aif_out0_source_control,
			   sma1307_aif_out_event,
			   SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_MUX_E(SMA1307_AIF_OUT1_NAME, SND_SOC_NOPM, 0, 0,
			   &sma1307_aif_out1_source_control,
			   sma1307_aif_out_event,
			   SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SWITCH_E("SDO Enable", SND_SOC_NOPM, 0, 0,
			      &sma1307_sdo_control,
			      sma1307_sdo_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER("Entry", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV_E("AMP Power", SND_SOC_NOPM, 0, 0, NULL, 0,
			       sma1307_power_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD |
			       SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SWITCH("AMP Enable", SND_SOC_NOPM, 0, 0,
			    &sma1307_enable_control),

	/* stream domain */
	SND_SOC_DAPM_AIF_IN("AIF IN", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF OUT", "Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route sma1307_audio_map[] = {
	/* Playback */
	{ "AIF IN Source", "Mono", "AIF IN" },
	{ "AIF IN Source", "Left", "AIF IN" },
	{ "AIF IN Source", "Right", "AIF IN" },

	{ "SDO Enable", "Switch", "AIF IN" },

	{ "SDO Setting", "Data_One_48k", "SDO Enable" },
	{ "SDO Setting", "Data_Two_48k", "SDO Enable" },
	{ "SDO Setting", "Data_Two_24k", "SDO Enable" },
	{ "SDO Setting", "Clk_PLL", "SDO Enable" },
	{ "SDO Setting", "Clk_OSC", "SDO Enable" },

	{ "AIF OUT0 Source", "Disable", "SDO Setting" },
	{ "AIF OUT0 Source", "After_FmtC", "SDO Setting" },
	{ "AIF OUT0 Source", "After_Mixer", "SDO Setting" },
	{ "AIF OUT0 Source", "After_DSP", "SDO Setting" },
	{ "AIF OUT0 Source", "Vrms2_Avg", "SDO Setting" },
	{ "AIF OUT0 Source", "Battery", "SDO Setting" },
	{ "AIF OUT0 Source", "Temperature", "SDO Setting" },
	{ "AIF OUT0 Source", "After_Delay", "SDO Setting" },

	{ "AIF OUT1 Source", "Disable", "SDO Setting" },
	{ "AIF OUT1 Source", "After_FmtC", "SDO Setting" },
	{ "AIF OUT1 Source", "After_Mixer", "SDO Setting" },
	{ "AIF OUT1 Source", "After_DSP", "SDO Setting" },
	{ "AIF OUT1 Source", "Vrms2_Avg", "SDO Setting" },
	{ "AIF OUT1 Source", "Battery", "SDO Setting" },
	{ "AIF OUT1 Source", "Temperature", "SDO Setting" },
	{ "AIF OUT1 Source", "After_Delay", "SDO Setting" },

	{ "Entry", NULL, "AIF OUT0 Source" },
	{ "Entry", NULL, "AIF OUT1 Source" },
	{ "Entry", NULL, "AIF IN Source" },

	{ "AMP Power", NULL, "Entry" },

	{ "AMP Enable", "Switch", "AMP Power" },
	{ "SPK", NULL, "AMP Enable" },

	/* Capture */
	{ "AIF OUT", NULL, "AMP Enable" },
};

static void sma1307_setup_pll(struct snd_soc_component *component,
			      unsigned int bclk)
{
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);

	int i = 0;

	dev_dbg(component->dev, "%s: BCLK = %dHz\n", __func__, bclk);

	if (sma1307->sys_clk_id == SMA1307_PLL_CLKIN_MCLK) {
		dev_warn(component->dev, "%s: MCLK is not supported\n",
			 __func__);
	} else if (sma1307->sys_clk_id == SMA1307_PLL_CLKIN_BCLK) {
		for (i = 0; i < sma1307->num_of_pll_matches; i++) {
			if (sma1307->pll_matches[i].input_clk == bclk)
				break;
		}
		if (i == sma1307->num_of_pll_matches) {
			dev_warn(component->dev,
				 "%s: No matching value between pll table and SCK\n",
				 __func__);
			return;
		}

		regmap_update_bits(sma1307->regmap,
					   SMA1307_A2_TOP_MAN1,
					   SMA1307_PLL_MASK, SMA1307_PLL_ON);
	}

	regmap_write(sma1307->regmap, SMA1307_8B_PLL_POST_N,
			     sma1307->pll_matches[i].post_n);
	regmap_write(sma1307->regmap, SMA1307_8C_PLL_N,
			     sma1307->pll_matches[i].n);
	regmap_write(sma1307->regmap, SMA1307_8D_PLL_A_SETTING,
			     sma1307->pll_matches[i].vco);
	regmap_write(sma1307->regmap, SMA1307_8E_PLL_P_CP,
			     sma1307->pll_matches[i].p_cp);
}

static int sma1307_dai_hw_params_amp(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);
	unsigned int bclk = 0;

	if (sma1307->format == SND_SOC_DAIFMT_DSP_A)
		bclk = params_rate(params) * sma1307->frame_size;
	else
		bclk = params_rate(params) * params_physical_width(params)
		    * params_channels(params);

	dev_dbg(component->dev,
		 "%s: rate = %d : bit size = %d : channel = %d\n",
		 __func__, params_rate(params), params_width(params),
		 params_channels(params));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (sma1307->sys_clk_id == SMA1307_PLL_CLKIN_BCLK) {
			if (sma1307->last_bclk != bclk) {
				sma1307_setup_pll(component, bclk);
				sma1307->last_bclk = bclk;
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
			break;

		case 96000:
			dev_warn(component->dev,
				 "%s: %d rate not support SDO\n", __func__,
				 params_rate(params));
			break;

		default:
			dev_err(component->dev, "%s: not support rate : %d\n",
				__func__, params_rate(params));

			return -EINVAL;
		}

		/* substream->stream is SNDRV_PCM_STREAM_CAPTURE */
	} else {

		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
			regmap_update_bits(sma1307->regmap,
						   SMA1307_A4_TOP_MAN3,
						   SMA1307_SCK_RATE_MASK
						   |
						   SMA1307_DATA_WIDTH_MASK,
						   SMA1307_SCK_32FS |
						   SMA1307_DATA_16BIT);
			break;

		case SNDRV_PCM_FORMAT_S24_LE:
			regmap_update_bits(sma1307->regmap,
						   SMA1307_A4_TOP_MAN3,
						   SMA1307_SCK_RATE_MASK
						   |
						   SMA1307_DATA_WIDTH_MASK,
						   SMA1307_SCK_64FS |
						   SMA1307_DATA_24BIT);
			break;

		case SNDRV_PCM_FORMAT_S32_LE:
			regmap_update_bits(sma1307->regmap,
						   SMA1307_A4_TOP_MAN3,
						   SMA1307_SCK_RATE_MASK
						   |
						   SMA1307_DATA_WIDTH_MASK,
						   SMA1307_SCK_64FS |
						   SMA1307_DATA_24BIT);
			break;
		default:
			dev_err(component->dev,
				"%s: not support data bit : %d\n", __func__,
				params_format(params));
			return -EINVAL;
		}
	}

	switch (sma1307->format) {
	case SND_SOC_DAIFMT_I2S:
		regmap_update_bits(sma1307->regmap,
					   SMA1307_01_INPUT_CTRL1,
					   SMA1307_I2S_MODE_MASK,
					   SMA1307_STANDARD_I2S);
		regmap_update_bits(sma1307->regmap,
					   SMA1307_A4_TOP_MAN3,
					   SMA1307_INTERFACE_MASK,
					   SMA1307_I2S_FORMAT);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		regmap_update_bits(sma1307->regmap,
					   SMA1307_01_INPUT_CTRL1,
					   SMA1307_I2S_MODE_MASK, SMA1307_LJ);
		regmap_update_bits(sma1307->regmap,
					   SMA1307_A4_TOP_MAN3,
					   SMA1307_INTERFACE_MASK,
					   SMA1307_LJ_FORMAT);
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		switch (params_width(params)) {
		case 16:
			regmap_update_bits(sma1307->regmap,
						   SMA1307_01_INPUT_CTRL1,
						   SMA1307_I2S_MODE_MASK,
						   SMA1307_RJ_16BIT);
			break;
		case 24:
		case 32:
			regmap_update_bits(sma1307->regmap,
						   SMA1307_01_INPUT_CTRL1,
						   SMA1307_I2S_MODE_MASK,
						   SMA1307_RJ_24BIT);
			break;
		}
		break;
	case SND_SOC_DAIFMT_DSP_A:
		regmap_update_bits(sma1307->regmap,
					   SMA1307_01_INPUT_CTRL1,
					   SMA1307_I2S_MODE_MASK,
					   SMA1307_STANDARD_I2S);
		regmap_update_bits(sma1307->regmap,
					   SMA1307_A4_TOP_MAN3,
					   SMA1307_INTERFACE_MASK,
					   SMA1307_TDM_FORMAT);
		break;
	}

	switch (params_width(params)) {
	case 16:
	case 24:
	case 32:
		break;
	default:
		dev_err(component->dev,
			"%s: not support data bit : %d\n", __func__,
			params_format(params));
		return -EINVAL;
	}

	return 0;
}

static int sma1307_dai_set_sysclk_amp(struct snd_soc_dai *dai,
				      int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);

	switch (clk_id) {
	case SMA1307_EXTERNAL_CLOCK_19_2:
	case SMA1307_EXTERNAL_CLOCK_24_576:
	case SMA1307_PLL_CLKIN_MCLK:
	case SMA1307_PLL_CLKIN_BCLK:
		break;
	default:
		dev_err(component->dev, "%s: Invalid clk id: %d\n",
			__func__, clk_id);
		return -EINVAL;
	}
	sma1307->sys_clk_id = clk_id;

	return 0;
}

static int sma1307_dai_set_fmt_amp(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {

	case SND_SOC_DAIFMT_CBC_CFC:
		dev_dbg(component->dev,
			 "%s: %s\n", __func__, "I2S/TDM Device mode");
		regmap_update_bits(sma1307->regmap,
					   SMA1307_01_INPUT_CTRL1,
					   SMA1307_CONTROLLER_DEVICE_MASK,
					   SMA1307_DEVICE_MODE);
		break;

	case SND_SOC_DAIFMT_CBP_CFP:
		dev_dbg(component->dev,
			 "%s: %s\n", __func__, "I2S/TDM Controller mode");
		regmap_update_bits(sma1307->regmap,
					   SMA1307_01_INPUT_CTRL1,
					   SMA1307_CONTROLLER_DEVICE_MASK,
					   SMA1307_CONTROLLER_MODE);
		break;

	default:
		dev_err(component->dev,
			"%s: Unsupported Controller/Device : 0x%x\n",
			__func__, fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		sma1307->format = fmt & SND_SOC_DAIFMT_FORMAT_MASK;
		break;
	default:
		dev_err(component->dev,
			"%s: Unsupported Audio Interface Format : 0x%x\n",
			__func__, fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {

	case SND_SOC_DAIFMT_IB_NF:
		dev_dbg(component->dev, "%s: %s\n",
			 __func__, "Invert BCLK + Normal Frame");
		regmap_update_bits(sma1307->regmap,
					   SMA1307_01_INPUT_CTRL1,
					   SMA1307_SCK_RISING_MASK,
					   SMA1307_SCK_RISING_EDGE);
		break;
	case SND_SOC_DAIFMT_IB_IF:
		dev_dbg(component->dev, "%s: %s\n",
			 __func__, "Invert BCLK + Invert Frame");
		regmap_update_bits(sma1307->regmap,
					   SMA1307_01_INPUT_CTRL1,
					   SMA1307_LEFTPOL_MASK
					   | SMA1307_SCK_RISING_MASK,
					   SMA1307_HIGH_FIRST_CH
					   | SMA1307_SCK_RISING_EDGE);
		break;
	case SND_SOC_DAIFMT_NB_IF:
		dev_dbg(component->dev, "%s: %s\n",
			 __func__, "Normal BCLK + Invert Frame");
		regmap_update_bits(sma1307->regmap,
					   SMA1307_01_INPUT_CTRL1,
					   SMA1307_LEFTPOL_MASK,
					   SMA1307_HIGH_FIRST_CH);
		break;
	case SND_SOC_DAIFMT_NB_NF:
		dev_dbg(component->dev, "%s: %s\n",
			 __func__, "Normal BCLK + Normal Frame");
		break;
	default:
		dev_err(component->dev,
			"%s: Unsupported Bit & Frameclock : 0x%x\n",
			__func__, fmt);
		return -EINVAL;
	}

	return 0;
}

static int sma1307_dai_set_tdm_slot(struct snd_soc_dai *dai,
				    unsigned int tx_mask, unsigned int rx_mask,
				    int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "%s: slots = %d, slot_width - %d\n",
		 __func__, slots, slot_width);

	sma1307->frame_size = slot_width * slots;

	regmap_update_bits(sma1307->regmap,
				   SMA1307_A4_TOP_MAN3,
				   SMA1307_INTERFACE_MASK, SMA1307_TDM_FORMAT);

	regmap_update_bits(sma1307->regmap,
				   SMA1307_A5_TDM1,
				   SMA1307_TDM_TX_MODE_MASK,
				   SMA1307_TDM_TX_MONO);

	switch (slot_width) {
	case 16:
		regmap_update_bits(sma1307->regmap,
					   SMA1307_A6_TDM2,
					   SMA1307_TDM_DL_MASK,
					   SMA1307_TDM_DL_16);
		break;
	case 32:
		regmap_update_bits(sma1307->regmap,
					   SMA1307_A6_TDM2,
					   SMA1307_TDM_DL_MASK,
					   SMA1307_TDM_DL_32);
		break;
	default:
		dev_err(component->dev, "%s: not support TDM %d slot_width\n",
			__func__, slot_width);
		return -EINVAL;
	}

	switch (slots) {
	case 4:
		regmap_update_bits(sma1307->regmap,
					   SMA1307_A6_TDM2,
					   SMA1307_TDM_N_SLOT_MASK,
					   SMA1307_TDM_N_SLOT_4);
		break;
	case 8:
		regmap_update_bits(sma1307->regmap,
					   SMA1307_A6_TDM2,
					   SMA1307_TDM_N_SLOT_MASK,
					   SMA1307_TDM_N_SLOT_8);
		break;
	default:
		dev_err(component->dev, "%s: not support TDM %d slots\n",
			__func__, slots);
		return -EINVAL;
	}

	if (sma1307->tdm_slot0_rx < slots)
		regmap_update_bits(sma1307->regmap,
					   SMA1307_A5_TDM1,
					   SMA1307_TDM_SLOT0_RX_POS_MASK,
					   sma1307->tdm_slot0_rx << 3);
	else
		dev_err(component->dev, "%s: Incorrect tdm-slot0-rx %d set\n",
			__func__, sma1307->tdm_slot0_rx);

	if (sma1307->tdm_slot1_rx < slots)
		regmap_update_bits(sma1307->regmap,
					   SMA1307_A5_TDM1,
					   SMA1307_TDM_SLOT1_RX_POS_MASK,
					   sma1307->tdm_slot1_rx);
	else
		dev_err(component->dev, "%s: Incorrect tdm-slot1-rx %d set\n",
			__func__, sma1307->tdm_slot1_rx);

	if (sma1307->tdm_slot0_tx < slots)
		regmap_update_bits(sma1307->regmap,
					   SMA1307_A6_TDM2,
					   SMA1307_TDM_SLOT0_TX_POS_MASK,
					   sma1307->tdm_slot0_tx << 3);
	else
		dev_err(component->dev, "%s: Incorrect tdm-slot0-tx %d set\n",
			__func__, sma1307->tdm_slot0_tx);

	if (sma1307->tdm_slot1_tx < slots)
		regmap_update_bits(sma1307->regmap,
					   SMA1307_A6_TDM2,
					   SMA1307_TDM_SLOT1_TX_POS_MASK,
					   sma1307->tdm_slot1_tx);
	else
		dev_err(component->dev, "%s: Incorrect tdm-slot1-tx %d set\n",
			__func__, sma1307->tdm_slot1_tx);

	return 0;
}

static int sma1307_dai_mute_stream(struct snd_soc_dai *dai, int mute,
				   int stream)
{
	struct snd_soc_component *component = dai->component;
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);

	if (stream == SNDRV_PCM_STREAM_CAPTURE)
		return 0;
	if (mute) {
		dev_dbg(component->dev, "%s: %s\n", __func__, "MUTE");
		regmap_update_bits(sma1307->regmap,
					   SMA1307_0E_MUTE_VOL_CTRL,
					   SMA1307_SPK_MUTE_MASK,
					   SMA1307_SPK_MUTE);
	} else {
		if (!sma1307->force_mute_status) {
			dev_dbg(component->dev, "%s: %s\n", __func__,
					 "UNMUTE");
			regmap_update_bits(sma1307->regmap,
						SMA1307_0E_MUTE_VOL_CTRL,
						SMA1307_SPK_MUTE_MASK,
						SMA1307_SPK_UNMUTE);
		} else {
			dev_dbg(sma1307->dev, "%s: FORCE MUTE!!!\n", __func__);
		}
	}

	return 0;
}

static const struct snd_soc_dai_ops sma1307_dai_ops_amp = {
	.hw_params = sma1307_dai_hw_params_amp,
	.set_fmt = sma1307_dai_set_fmt_amp,
	.set_sysclk = sma1307_dai_set_sysclk_amp,
	.set_tdm_slot = sma1307_dai_set_tdm_slot,
	.mute_stream = sma1307_dai_mute_stream,
};

#define SMA1307_RATES_PLAYBACK SNDRV_PCM_RATE_8000_96000
#define SMA1307_RATES_CAPTURE SNDRV_PCM_RATE_8000_48000
#define SMA1307_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | \
		SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver sma1307_dai[] = {
	{
	 .name = "sma1307-amplifier",
	 .id = 0,
	 .playback = {
		      .stream_name = "Playback",
		      .channels_min = 1,
		      .channels_max = 2,
		      .rates = SMA1307_RATES_PLAYBACK,
		      .formats = SMA1307_FORMATS,
		      },
	 .capture = {
		     .stream_name = "Capture",
		     .channels_min = 1,
		     .channels_max = 2,
		     .rates = SMA1307_RATES_CAPTURE,
		     .formats = SMA1307_FORMATS,
		     },
	 .ops = &sma1307_dai_ops_amp,
	  },
};

static void sma1307_check_fault_worker(struct work_struct *work)
{
	struct sma1307_priv *sma1307 =
	    container_of(work, struct sma1307_priv, check_fault_work.work);
	unsigned int status1_val, status2_val;
	char *envp[3] = { NULL, NULL, NULL };

	if (sma1307->tsdw_cnt)
		regmap_read(sma1307->regmap,
			SMA1307_0A_SPK_VOL, &sma1307->cur_vol);
	else
		regmap_read(sma1307->regmap,
			SMA1307_0A_SPK_VOL, &sma1307->init_vol);

	regmap_read(sma1307->regmap, SMA1307_FA_STATUS1, &status1_val);
	regmap_read(sma1307->regmap, SMA1307_FB_STATUS2, &status2_val);

	if (~status1_val & SMA1307_OT1_OK_STATUS) {
		dev_crit(sma1307->dev,
			 "%s: OT1(Over Temperature Level 1)\n", __func__);
		envp[0] = kasprintf(GFP_KERNEL, "STATUS=OT1");
		if (sma1307->sw_ot1_prot) {
			/* Volume control (Current Volume -3dB) */
			if ((sma1307->cur_vol + 6) <= 0xFA) {
				sma1307->cur_vol += 6;
				regmap_write(sma1307->regmap,
						     SMA1307_0A_SPK_VOL,
						     sma1307->cur_vol);
				envp[1] = kasprintf(GFP_KERNEL,
					"VOLUME=0x%02X", sma1307->cur_vol);
			}
		}
		sma1307->tsdw_cnt++;
	} else if (sma1307->tsdw_cnt) {
		regmap_write(sma1307->regmap,
				     SMA1307_0A_SPK_VOL, sma1307->init_vol);
		sma1307->tsdw_cnt = 0;
		sma1307->cur_vol = sma1307->init_vol;
		envp[0] = kasprintf(GFP_KERNEL, "STATUS=OT1_CLEAR");
		envp[1] = kasprintf(GFP_KERNEL,
				"VOLUME=0x%02X", sma1307->cur_vol);
	}

	if (~status1_val & SMA1307_OT2_OK_STATUS) {
		dev_crit(sma1307->dev,
			 "%s: OT2(Over Temperature Level 2)\n", __func__);
		envp[0] = kasprintf(GFP_KERNEL, "STATUS=OT2");
	}
	if (status1_val & SMA1307_UVLO_STATUS) {
		dev_crit(sma1307->dev,
			 "%s: UVLO(Under Voltage Lock Out)\n", __func__);
		envp[0] = kasprintf(GFP_KERNEL, "STATUS=UVLO");
	}
	if (status1_val & SMA1307_OVP_BST_STATUS) {
		dev_crit(sma1307->dev,
			 "%s: OVP_BST(Over Voltage Protection)\n", __func__);
		envp[0] = kasprintf(GFP_KERNEL, "STATUS=OVP_BST");
	}
	if (status2_val & SMA1307_OCP_SPK_STATUS) {
		dev_crit(sma1307->dev,
			 "%s: OCP_SPK(Over Current Protect SPK)\n", __func__);
		envp[0] = kasprintf(GFP_KERNEL, "STATUS=OCP_SPK");
	}
	if (status2_val & SMA1307_OCP_BST_STATUS) {
		dev_crit(sma1307->dev,
			 "%s: OCP_BST(Over Current Protect Boost)\n", __func__);
		envp[0] = kasprintf(GFP_KERNEL, "STATUS=OCP_BST");
	}
	if (status2_val & SMA1307_CLK_MON_STATUS) {
		dev_crit(sma1307->dev,
			 "%s: CLK_FAULT(No clock input)\n", __func__);
		envp[0] = kasprintf(GFP_KERNEL, "STATUS=CLK_FAULT");
	}

	if (envp[0] != NULL) {
		if (kobject_uevent_env(sma1307->kobj, KOBJ_CHANGE, envp))
			dev_err(sma1307->dev,
				"%s: Error sending uevent\n", __func__);
		kfree(envp[0]);
		kfree(envp[1]);
	}

	if (sma1307->check_fault_status) {
		if (sma1307->check_fault_period > 0)
			queue_delayed_work(system_freezable_wq,
					   &sma1307->check_fault_work,
					   sma1307->check_fault_period * HZ);
		else
			queue_delayed_work(system_freezable_wq,
					   &sma1307->check_fault_work,
					   CHECK_PERIOD_TIME * HZ);
	}
}

static void sma1307_setting_loaded(struct sma1307_priv *sma1307, const char *file)
{
	const struct firmware *fw;
	int *data, size, offset, num_mode;
	int ret;

	ret = request_firmware(&fw, file, sma1307->dev);

	if (ret) {
		dev_err(sma1307->dev, "%s: failed to read \"%s\": %pe\n",
			__func__, setting_file, ERR_PTR(ret));
		sma1307->set.status = false;
		return;
	} else if ((fw->size) < SMA1307_SETTING_HEADER_SIZE) {
		dev_err(sma1307->dev, "%s: Invalid file\n", __func__);
		release_firmware(fw);
		sma1307->set.status = false;
		return;
	}

	data = kzalloc(fw->size, GFP_KERNEL);
	size = fw->size >> 2;
	memcpy(data, fw->data, fw->size);

	release_firmware(fw);

	/* HEADER */
	sma1307->set.header_size = SMA1307_SETTING_HEADER_SIZE;
	sma1307->set.checksum = data[sma1307->set.header_size - 2];
	sma1307->set.num_mode = data[sma1307->set.header_size - 1];
	num_mode = sma1307->set.num_mode;
	sma1307->set.header = devm_kzalloc(sma1307->dev,
					   sma1307->set.header_size,
					   GFP_KERNEL);
	memcpy(sma1307->set.header, data,
	       sma1307->set.header_size * sizeof(int));

	if ((sma1307->set.checksum >> 8) != SMA1307_SETTING_CHECKSUM) {
		dev_err(sma1307->dev, "%s: failed by dismatch \"%s\"\n",
			__func__, setting_file);
		sma1307->set.status = false;
		return;
	}

	/* DEFAULT */
	sma1307->set.def_size = SMA1307_SETTING_DEFAULT_SIZE;
	sma1307->set.def
	    = devm_kzalloc(sma1307->dev,
			   sma1307->set.def_size * sizeof(int), GFP_KERNEL);
	memcpy(sma1307->set.def,
	       &data[sma1307->set.header_size],
	       sma1307->set.def_size * sizeof(int));

	/* MODE */
	offset = sma1307->set.header_size + sma1307->set.def_size;
	sma1307->set.mode_size = DIV_ROUND_CLOSEST(size - offset, num_mode + 1);
	for (int i = 0; i < num_mode; i++) {
		sma1307->set.mode_set[i]
		    = devm_kzalloc(sma1307->dev,
				   sma1307->set.mode_size * 2 * sizeof(int),
				   GFP_KERNEL);
		for (int j = 0; j < sma1307->set.mode_size; j++) {
			sma1307->set.mode_set[i][2 * j]
			    = data[offset + ((num_mode + 1) * j)];
			sma1307->set.mode_set[i][2 * j + 1]
			    = data[offset + ((num_mode + 1) * j + i + 1)];
		}
	}

	kfree(data);
	sma1307->set.status = true;

}

static void sma1307_reset(struct snd_soc_component *component)
{
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);
	unsigned int status = 0;

	regmap_read(sma1307->regmap, SMA1307_FF_DEVICE_INDEX, &status);

	sma1307->rev_num = status & SMA1307_REV_NUM_STATUS;
	dev_dbg(component->dev, "%s: SMA1307 Revision %d\n",
			 __func__, sma1307->rev_num);
	regmap_read(sma1307->regmap, SMA1307_99_OTP_TRM2, &sma1307->otp_trm2);
	regmap_read(sma1307->regmap, SMA1307_9A_OTP_TRM3, &sma1307->otp_trm3);

	if ((sma1307->otp_trm2 & SMA1307_OTP_STAT_MASK) != SMA1307_OTP_STAT_1)
		dev_warn(component->dev, "%s: SMA1307 OTP Status Fail\n",
			 __func__);

	/* Register Initial Value Setting */
	sma1307_setting_loaded(sma1307, setting_file);
	if (sma1307->set.status)
		sma1307_set_binary(component);
	else
		sma1307_set_default(component);

	regmap_update_bits(sma1307->regmap,
				   SMA1307_93_INT_CTRL,
				   SMA1307_DIS_INT_MASK, SMA1307_HIGH_Z_INT);
	regmap_write(sma1307->regmap, SMA1307_0A_SPK_VOL, sma1307->init_vol);
}

static void sma1307_set_binary(struct snd_soc_component *component)
{
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);
	int i = 0, mode = 0;

	for (i = 0; i < (sma1307->set.def_size); i++) {
		if (sma1307_writeable_register(sma1307->dev, i)
		    && ((i < SMA1307_97_OTP_TRM0)
			|| (i > SMA1307_9A_OTP_TRM3))) {
			regmap_write(sma1307->regmap, i, sma1307->set.def[i]);

		}
	}
	for (i = 0; i < (sma1307->set.mode_size); i++) {
		if (sma1307_writeable_register(sma1307->dev, i)
		    && ((i < SMA1307_97_OTP_TRM0)
			|| (i > SMA1307_9A_OTP_TRM3))) {
			mode = sma1307->binary_mode;
			regmap_write(sma1307->regmap,
					     sma1307->set.mode_set[mode][2 * i],
					     sma1307->set.mode_set[mode][2 * i +
									 1]);
		}
	}
}

static void sma1307_set_default(struct snd_soc_component *component)
{
	struct sma1307_priv *sma1307 = snd_soc_component_get_drvdata(component);
	int i = 0;

	for (i = 0; i < (unsigned int)ARRAY_SIZE(sma1307_reg_def); i++)
		regmap_write(sma1307->regmap,
				     sma1307_reg_def[i].reg,
				     sma1307_reg_def[i].def);

	if (!strcmp(sma1307->name, DEVICE_NAME_SMA1307AQ))
		sma1307->data->init(sma1307->regmap);
}

static int sma1307_probe(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm =
	    snd_soc_component_get_dapm(component);

	snd_soc_dapm_sync(dapm);

	sma1307_amp_component = component;

	snd_soc_add_component_controls(component, sma1307_binary_mode_control,
			ARRAY_SIZE(sma1307_binary_mode_control));
	sma1307_reset(component);

	return 0;
}

static const struct snd_soc_component_driver sma1307_component = {
	.probe = sma1307_probe,
	.controls = sma1307_snd_controls,
	.num_controls = ARRAY_SIZE(sma1307_snd_controls),
	.dapm_widgets = sma1307_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sma1307_dapm_widgets),
	.dapm_routes = sma1307_audio_map,
	.num_dapm_routes = ARRAY_SIZE(sma1307_audio_map),
};

static const struct regmap_config sma_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = SMA1307_FF_DEVICE_INDEX,
	.readable_reg = sma1307_readable_register,
	.writeable_reg = sma1307_writeable_register,
	.volatile_reg = sma1307_volatile_register,

	.reg_defaults = sma1307_reg_def,
	.num_reg_defaults = ARRAY_SIZE(sma1307_reg_def),
};

static void sma1307aq_init(struct regmap *regmap)
{
	/* Guidelines for driving 4ohm load */
	/* Brown Out Protection */
	regmap_write(regmap, SMA1307_02_BROWN_OUT_PROT1, 0x62);
	regmap_write(regmap, SMA1307_03_BROWN_OUT_PROT2, 0x5D);
	regmap_write(regmap, SMA1307_04_BROWN_OUT_PROT3, 0x57);
	regmap_write(regmap, SMA1307_05_BROWN_OUT_PROT8, 0x54);
	regmap_write(regmap, SMA1307_06_BROWN_OUT_PROT9, 0x51);
	regmap_write(regmap,
			     SMA1307_07_BROWN_OUT_PROT10, 0x4D);
	regmap_write(regmap,
			     SMA1307_08_BROWN_OUT_PROT11, 0x4B);
	regmap_write(regmap, SMA1307_27_BROWN_OUT_PROT4, 0x3C);
	regmap_write(regmap, SMA1307_28_BROWN_OUT_PROT5, 0x5B);
	regmap_write(regmap,
			     SMA1307_29_BROWN_OUT_PROT12, 0x78);
	regmap_write(regmap,
			     SMA1307_2A_BROWN_OUT_PROT13, 0x96);
	regmap_write(regmap,
			     SMA1307_2B_BROWN_OUT_PROT14, 0xB4);
	regmap_write(regmap,
			     SMA1307_2C_BROWN_OUT_PROT15, 0xD3);
	/* FDPEC Gain */
	regmap_write(regmap, SMA1307_35_FDPEC_CTRL0, 0x16);
	/* FLT Vdd */
	regmap_write(regmap, SMA1307_92_FDPEC_CTRL1, 0xA0);
	/* Boost Max */
	regmap_write(regmap, SMA1307_AB_BOOST_CTRL4, 0x0F);
}

static const struct sma1307_data sma1307aq_data = {
	.name = DEVICE_NAME_SMA1307AQ,
	.init = sma1307aq_init,
};

static int sma1307_i2c_probe(struct i2c_client *client)
{
	struct sma1307_priv *sma1307;
	const struct sma1307_data *data;
	int ret = 0;
	unsigned int device_info;

	sma1307 = devm_kzalloc(&client->dev,
			       sizeof(*sma1307), GFP_KERNEL);
	if (!sma1307)
		return -ENOMEM;

	sma1307->regmap = devm_regmap_init_i2c(client, &sma_i2c_regmap);
	if (IS_ERR(sma1307->regmap)) {
		return dev_err_probe(&client->dev, PTR_ERR(sma1307->regmap),
			"%s: failed to allocate register map\n", __func__);
	}

	data = device_get_match_data(&client->dev);
	if (!data)
		return -ENODEV;

	sma1307->data = data;

	/* set initial value as normal AMP IC status */
	sma1307->name = client->name;
	sma1307->format = SND_SOC_DAIFMT_I2S;
	sma1307->sys_clk_id = SMA1307_PLL_CLKIN_BCLK;
	sma1307->num_of_pll_matches = ARRAY_SIZE(sma1307_pll_matches);

	sma1307->check_fault_period = CHECK_PERIOD_TIME;
	sma1307->check_fault_status = true;
	sma1307->init_vol = 0x32;
	sma1307->cur_vol = sma1307->init_vol;
	sma1307->sw_ot1_prot = true;

	mutex_init(&sma1307->default_lock);

	INIT_DELAYED_WORK(&sma1307->check_fault_work,
			  sma1307_check_fault_worker);

	sma1307->dev = &client->dev;
	sma1307->kobj = &client->dev.kobj;

	i2c_set_clientdata(client, sma1307);

	sma1307->pll_matches = sma1307_pll_matches;

	regmap_read(sma1307->regmap,
				  SMA1307_FF_DEVICE_INDEX, &device_info);

	if ((device_info & 0xF8) != SMA1307_DEVICE_ID) {
		dev_err(&client->dev,
			"%s: device initialization error (0x%02X)",
				__func__, device_info);
		return -ENODEV;
	}
	dev_dbg(&client->dev, "%s: chip version 0x%02X\n",
		 __func__, device_info);

	i2c_set_clientdata(client, sma1307);

	ret = devm_snd_soc_register_component(&client->dev,
					      &sma1307_component, sma1307_dai,
					      1);

	if (ret) {
		dev_err(&client->dev, "%s: failed to register component\n",
			__func__);

		return ret;
	}

	return ret;
}

static void sma1307_i2c_remove(struct i2c_client *client)
{
	struct sma1307_priv *sma1307 =
	    (struct sma1307_priv *)i2c_get_clientdata(client);

	cancel_delayed_work_sync(&sma1307->check_fault_work);
}

static const struct i2c_device_id sma1307_i2c_id[] = {
	{ "sma1307a", 0 },
	{ "sma1307aq", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, sma1307_i2c_id);

static const struct of_device_id sma1307_of_match[] = {
	{
		.compatible = "irondevice,sma1307a",
	},
	{
		.compatible = "irondevice,sma1307aq",
		.data = &sma1307aq_data //AEC-Q100 Qualificated
	},
	{ }
};

MODULE_DEVICE_TABLE(of, sma1307_of_match);

static struct i2c_driver sma1307_i2c_driver = {
	.driver = {
		   .name = "sma1307",
		   .of_match_table = sma1307_of_match,
		    },
	.probe = sma1307_i2c_probe,
	.remove = sma1307_i2c_remove,
	.id_table = sma1307_i2c_id,
};

module_i2c_driver(sma1307_i2c_driver);

MODULE_DESCRIPTION("ALSA SoC SMA1307 driver");
MODULE_AUTHOR("Gyuhwa Park, <gyuhwa.park@irondevice.com>");
MODULE_AUTHOR("KS Jo, <kiseok.jo@irondevice.com>");
MODULE_LICENSE("GPL");
