// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * rk730.c -- RK730 ALSA SoC Audio driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co.,Ltd
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "rk730.h"

enum rk730_mix_mode {
	RK730_MIX_MODE_1_PATH,
	RK730_MIX_MODE_2_PATHS,
	RK730_MIX_MODE_3_PATHS,
};

enum rk730_chop_freq {
	RK730_CHOP_FREQ_NONE,
	RK730_CHOP_FREQ_200KHZ,
	RK730_CHOP_FREQ_400KHZ,
	RK730_CHOP_FREQ_800KHZ,
};

struct rk730_priv {
	struct regmap *regmap;
	struct clk *mclk;
	atomic_t mix_mode;
};

/* ADC Digital Volume */
static const DECLARE_TLV_DB_SCALE(adc_dig_tlv, -95625, 375, 0);
/* DAC Digital Volume */
static const DECLARE_TLV_DB_SCALE(dac_dig_tlv, -95625, 375, 0);
/* D2S Volume */
static const DECLARE_TLV_DB_SCALE(d2s_tlv, -1800, 300, 0);
/* ADC Volume */
static const DECLARE_TLV_DB_SCALE(adc_tlv, -1200, 300, 0);
/* MUX Volume */
static const DECLARE_TLV_DB_SCALE(mux_tlv, -600, 600, 0);
/* MIX Buf Volume */
static const DECLARE_TLV_DB_SCALE(mix_buf_tlv, -1800, 300, 0);
/* HP Volume */
static const DECLARE_TLV_DB_SCALE(hp_tlv, 0, 300, 0);
/* LINEOUT Volume */
static const DECLARE_TLV_DB_SCALE(lineout_tlv, 0, 300, 0);
/* MIC Boost Volume */
static const DECLARE_TLV_DB_RANGE(micboost_tlv,
	0, 2, TLV_DB_SCALE_ITEM(0, 600, 0),
	3, 4, TLV_DB_SCALE_ITEM(2400, 1200, 0),
	5, 7, TLV_DB_SCALE_ITEM(4200, 600, 0),
	8, 8, TLV_DB_SCALE_ITEM(-300, 0, 0),
	16, 16, TLV_DB_SCALE_ITEM(-600, 0, 0),
	24, 24, TLV_DB_SCALE_ITEM(-900, 0, 0)
);

static const char * const mux_out_l_text[] = { "DIFF", "MIC1N", "MIC2N" };
static const char * const mux_out_r_text[] = { "DIFF", "MIC2P", "MIC1P" };
static const char * const mux_input_l_text[] = { "DIFF", "VINP1", "VINN1" };
static const char * const mux_input_r_text[] = { "DIFF", "VINP2", "VINN2" };

static SOC_ENUM_SINGLE_DECL(mux_out_l_enum, RK730_MUXER_0, 2, mux_out_l_text);
static SOC_ENUM_SINGLE_DECL(mux_out_r_enum, RK730_MUXER_0, 6, mux_out_r_text);
static SOC_ENUM_SINGLE_DECL(mux_input_l_enum, RK730_ADC_PGA_BLOCK_0,
			    4, mux_input_l_text);
static SOC_ENUM_SINGLE_DECL(mux_input_r_enum, RK730_ADC_PGA_BLOCK_1,
			    4, mux_input_r_text);

static const struct snd_kcontrol_new mux_out_l =
	SOC_DAPM_ENUM("Left Out Mux", mux_out_l_enum);
static const struct snd_kcontrol_new mux_out_r =
	SOC_DAPM_ENUM("Right Out Mux", mux_out_r_enum);
static const struct snd_kcontrol_new mux_input_l =
	SOC_DAPM_ENUM("Left Input Mux", mux_input_l_enum);
static const struct snd_kcontrol_new mux_input_r =
	SOC_DAPM_ENUM("Right Input Mux", mux_input_r_enum);

static const struct snd_kcontrol_new mix_ctls[] = {
	SOC_DAPM_SINGLE("Left Out Mux Switch", RK730_MUXER_0, 0, 1, 1),
	SOC_DAPM_SINGLE("Right Out Mux Switch", RK730_MUXER_0, 4, 1, 1),
};

static const char * const adc_hpf_cutoff_text[] = {
	"3.79Hz", "60Hz", "243Hz", "493Hz",
};

static const char * const dac_hpf_cutoff_text[] = {
	"80Hz", "100Hz", "120Hz", "140Hz",
};

static SOC_ENUM_SINGLE_DECL(adc_hpf_cutoff_enum, RK730_DADC_HPF,
			    4, adc_hpf_cutoff_text);
static SOC_ENUM_SINGLE_DECL(dac_hpf_cutoff_enum, RK730_DDAC_MUTE_MIXCTL,
			    5, dac_hpf_cutoff_text);

static const char * const chop_freq_text[] = {
	"Disabled", "200kHz", "400kHz", "800kHz",
};

static SOC_ENUM_SINGLE_DECL(dac_ref_buf_chop_freq_enum, RK730_HK_TOP_1,
			    6, chop_freq_text);
static SOC_ENUM_SINGLE_DECL(mic_chop_freq_enum, RK730_MIC_BOOST_3,
			    6, chop_freq_text);
static SOC_ENUM_SINGLE_DECL(adc_pga_chop_freq_enum, RK730_ADC_PGA_BLOCK_1,
			    6, chop_freq_text);
static SOC_ENUM_SINGLE_DECL(mux_out_chop_freq_enum, RK730_MUXER_1,
			    0, chop_freq_text);
static SOC_ENUM_SINGLE_DECL(mix_chop_freq_enum, RK730_MIXER_2,
			    6, chop_freq_text);
static SOC_ENUM_SINGLE_DECL(hp_lo_chop_freq_enum, RK730_HP_1,
			    5, chop_freq_text);

static const char * const micbias_volt_text[] = {
	"2.0v", "2.2v", "2.5v", "2.8v",
};

static const char * const charge_pump_volt_text[] = {
	"2.1v", "2.3v", "2.5v", "2.7v",
};

static SOC_ENUM_SINGLE_DECL(micbias_volt_enum, RK730_MIC_BIAS,
			    2, micbias_volt_text);
static SOC_ENUM_SINGLE_DECL(charge_pump_volt_enum, RK730_CHARGE_PUMP,
			    1, charge_pump_volt_text);

static int rk730_adc_vol_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int val = snd_soc_component_read(component, mc->reg);
	unsigned int sign = snd_soc_component_read(component, RK730_DADC_SR_ACL);
	unsigned int mask = (1 << fls(mc->max)) - 1;
	unsigned int shift = mc->shift;
	int mid = mc->max / 2;
	int uv;

	uv = (val >> shift) & mask;
	sign &= RK730_DADC_SR_ACL_VOLL_POL_MASK;
	if (sign)
		uv = mid + uv;
	else
		uv = mid - uv;

	ucontrol->value.integer.value[0] = uv;
	ucontrol->value.integer.value[1] = uv;

	return 0;
}

static int rk730_adc_vol_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int rreg = mc->rreg;
	unsigned int shift = mc->shift;
	unsigned int mask = (1 << fls(mc->max)) - 1;
	unsigned int val, val_mask, sign, sign_mask;
	int uv = ucontrol->value.integer.value[0];
	int min = mc->min;
	int mid = mc->max / 2;

	sign_mask = RK730_DADC_SR_ACL_VOLL_POL_MASK | RK730_DADC_SR_ACL_VOLR_POL_MASK;

	if (uv > mid) {
		sign = RK730_DADC_SR_ACL_VOLL_POS | RK730_DADC_SR_ACL_VOLR_POS;
		uv = uv - mid;
	} else {
		sign = RK730_DADC_SR_ACL_VOLL_NEG | RK730_DADC_SR_ACL_VOLR_NEG;
		uv = mid - uv;
	}

	val = ((uv + min) & mask);
	val_mask = mask << shift;
	val = val << shift;

	snd_soc_component_update_bits(component, reg, val_mask, val);
	snd_soc_component_update_bits(component, rreg, val_mask, val);
	snd_soc_component_update_bits(component, RK730_DADC_SR_ACL, sign_mask, sign);

	return 1;
}

static int rk730_dac_vol_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int val = snd_soc_component_read(component, mc->reg);
	unsigned int sign = snd_soc_component_read(component, RK730_DDAC_SR_LMT);
	unsigned int mask = (1 << fls(mc->max)) - 1;
	unsigned int shift = mc->shift;
	int mid = mc->max / 2;
	int uv;

	uv = (val >> shift) & mask;
	sign &= RK730_DDAC_SR_LMT_VOLL_POL_MASK;
	if (sign)
		uv = mid + uv;
	else
		uv = mid - uv;

	ucontrol->value.integer.value[0] = uv;
	ucontrol->value.integer.value[1] = uv;

	return 0;
}

static int rk730_dac_vol_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int rreg = mc->rreg;
	unsigned int shift = mc->shift;
	unsigned int mask = (1 << fls(mc->max)) - 1;
	unsigned int val, val_mask, sign, sign_mask;
	int uv = ucontrol->value.integer.value[0];
	int min = mc->min;
	int mid = mc->max / 2;

	sign_mask = RK730_DDAC_SR_LMT_VOLL_POL_MASK | RK730_DDAC_SR_LMT_VOLR_POL_MASK;

	if (uv > mid) {
		sign = RK730_DDAC_SR_LMT_VOLL_POS | RK730_DDAC_SR_LMT_VOLR_POS;
		uv = uv - mid;
	} else {
		sign = RK730_DDAC_SR_LMT_VOLL_NEG | RK730_DDAC_SR_LMT_VOLR_NEG;
		uv = mid - uv;
	}

	val = ((uv + min) & mask);
	val_mask = mask << shift;
	val = val << shift;

	snd_soc_component_update_bits(component, reg, val_mask, val);
	snd_soc_component_update_bits(component, rreg, val_mask, val);
	snd_soc_component_update_bits(component, RK730_DDAC_SR_LMT, sign_mask, sign);

	return 1;
}

static int rk730_cp_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		usleep_range(5000, 5100);

	return 0;
}

static int rk730_pll_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	if (SND_SOC_DAPM_EVENT_ON(event))
		snd_soc_component_write(component, RK730_SYSPLL_0, 0x00);
	else
		snd_soc_component_write(component, RK730_SYSPLL_0, 0xff);

	return 0;
}

static int rk730_adc_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		snd_soc_component_update_bits(component, RK730_ADC_0,
					      RK730_ADC_0_DEM_EN_MASK,
					      RK730_ADC_0_DEM_EN);
		snd_soc_component_update_bits(component, RK730_DTOP_DIGEN_CLKE,
					      RK730_DTOP_DIGEN_CLKE_ADC_CKE_MASK |
					      RK730_DTOP_DIGEN_CLKE_I2STX_CKE_MASK |
					      RK730_DTOP_DIGEN_CLKE_ADC_EN_MASK |
					      RK730_DTOP_DIGEN_CLKE_I2STX_EN_MASK,
					      RK730_DTOP_DIGEN_CLKE_ADC_CKE_EN |
					      RK730_DTOP_DIGEN_CLKE_I2STX_CKE_EN |
					      RK730_DTOP_DIGEN_CLKE_ADC_EN |
					      RK730_DTOP_DIGEN_CLKE_I2STX_EN);
		usleep_range(20000, 21000);
		snd_soc_component_update_bits(component, RK730_DI2S_TXCR_3_TXCMD,
					      RK730_DI2S_TXCR_3_TXCMD_TXS_MASK,
					      RK730_DI2S_TXCR_3_TXCMD_TXS_EN);
	} else {
		snd_soc_component_update_bits(component, RK730_DI2S_TXCR_3_TXCMD,
					      RK730_DI2S_TXCR_3_TXCMD_TXS_MASK,
					      RK730_DI2S_TXCR_3_TXCMD_TXS_DIS);
		snd_soc_component_update_bits(component, RK730_ADC_0,
					      RK730_ADC_0_DEM_EN_MASK,
					      RK730_ADC_0_DEM_DIS);
		snd_soc_component_update_bits(component, RK730_DTOP_DIGEN_CLKE,
					      RK730_DTOP_DIGEN_CLKE_ADC_CKE_MASK |
					      RK730_DTOP_DIGEN_CLKE_I2STX_CKE_MASK |
					      RK730_DTOP_DIGEN_CLKE_ADC_EN_MASK |
					      RK730_DTOP_DIGEN_CLKE_I2STX_EN_MASK,
					      RK730_DTOP_DIGEN_CLKE_ADC_CKE_DIS |
					      RK730_DTOP_DIGEN_CLKE_I2STX_CKE_DIS |
					      RK730_DTOP_DIGEN_CLKE_ADC_DIS |
					      RK730_DTOP_DIGEN_CLKE_I2STX_DIS);
	}

	return 0;
}

static int rk730_dac_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		snd_soc_component_update_bits(component, RK730_DTOP_DIGEN_CLKE,
					      RK730_DTOP_DIGEN_CLKE_DAC_CKE_MASK |
					      RK730_DTOP_DIGEN_CLKE_I2SRX_CKE_MASK |
					      RK730_DTOP_DIGEN_CLKE_DAC_EN_MASK |
					      RK730_DTOP_DIGEN_CLKE_I2SRX_EN_MASK,
					      RK730_DTOP_DIGEN_CLKE_DAC_CKE_EN |
					      RK730_DTOP_DIGEN_CLKE_I2SRX_CKE_EN |
					      RK730_DTOP_DIGEN_CLKE_DAC_EN |
					      RK730_DTOP_DIGEN_CLKE_I2SRX_EN);
		snd_soc_component_update_bits(component, RK730_DI2S_RXCMD_TSD,
					      RK730_DI2S_RXCMD_TSD_RXS_MASK,
					      RK730_DI2S_RXCMD_TSD_RXS_EN);
	} else {
		snd_soc_component_update_bits(component, RK730_DI2S_RXCMD_TSD,
					      RK730_DI2S_RXCMD_TSD_RXS_MASK,
					      RK730_DI2S_RXCMD_TSD_RXS_DIS);
		snd_soc_component_update_bits(component, RK730_DTOP_DIGEN_CLKE,
					      RK730_DTOP_DIGEN_CLKE_DAC_CKE_MASK |
					      RK730_DTOP_DIGEN_CLKE_I2SRX_CKE_MASK |
					      RK730_DTOP_DIGEN_CLKE_DAC_EN_MASK |
					      RK730_DTOP_DIGEN_CLKE_I2SRX_EN_MASK,
					      RK730_DTOP_DIGEN_CLKE_DAC_CKE_DIS |
					      RK730_DTOP_DIGEN_CLKE_I2SRX_CKE_DIS |
					      RK730_DTOP_DIGEN_CLKE_DAC_DIS |
					      RK730_DTOP_DIGEN_CLKE_I2SRX_DIS);
	}

	return 0;
}

static int rk730_mux_out_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rk730_priv *rk730 = snd_soc_component_get_drvdata(component);
	unsigned int val;

	if (SND_SOC_DAPM_EVENT_ON(event))
		val = atomic_inc_return(&rk730->mix_mode);
	else
		val = atomic_dec_return(&rk730->mix_mode);

	snd_soc_component_update_bits(component, RK730_MIXER_2,
				      RK730_MIXER_2_MIX_R_MODE_MASK |
				      RK730_MIXER_2_MIX_L_MODE_MASK,
				      RK730_MIXER_2_MIX_R_MODE(val) |
				      RK730_MIXER_2_MIX_L_MODE(val));
	return 0;
}

static const struct snd_kcontrol_new rk730_snd_controls[] = {
	SOC_DOUBLE_R_TLV("ADC Volume", RK730_ADC_PGA_BLOCK_0, RK730_ADC_PGA_BLOCK_1,
			 1, 0x7, 1, adc_tlv),
	SOC_DOUBLE_R_TLV("D2S Volume", RK730_DAC_1, RK730_DAC_2,
			 1, 0x6, 1, d2s_tlv),

	SOC_DOUBLE_R_TLV("MIC1 Boost Volume", RK730_MIC_BOOST_0, RK730_MIC_BOOST_1,
			 1, 0x18, 0, micboost_tlv),
	SOC_DOUBLE_R_TLV("MIC2 Boost Volume", RK730_MIC_BOOST_2, RK730_MIC_BOOST_3,
			 1, 0x18, 0, micboost_tlv),

	SOC_DOUBLE_TLV("Out Mux Volume", RK730_MUXER_0, 1, 5, 0x1, 0, mux_tlv),

	SOC_SINGLE_TLV("Left Out Mux -> Left Out Mixer Volume",
		       RK730_MIXER_0, 1, 0x6, 1, mix_buf_tlv),
	SOC_SINGLE_TLV("Left Out Mux -> Right Out Mixer Volume",
		       RK730_MIXER_0, 5, 0x6, 1, mix_buf_tlv),
	SOC_SINGLE_TLV("Right Out Mux -> Left Out Mixer Volume",
		       RK730_MIXER_1, 1, 0x6, 1, mix_buf_tlv),
	SOC_SINGLE_TLV("Right Out Mux -> Right Out Mixer Volume",
		       RK730_MIXER_1, 5, 0x6, 1, mix_buf_tlv),

	SOC_SINGLE_TLV("HP Volume", RK730_HP_0, 6, 0x3, 0, hp_tlv),
	SOC_SINGLE_TLV("Line Out Volume", RK730_LINEOUT_1, 2, 0x3, 0, lineout_tlv),

	SOC_DOUBLE_R_EXT_TLV("ADC Digital Volume",
			     RK730_DADC_VOLL, RK730_DADC_VOLR, 0, 0x1fe, 0,
			     rk730_adc_vol_get,
			     rk730_adc_vol_put,
			     adc_dig_tlv),
	SOC_DOUBLE_R_EXT_TLV("DAC Digital Volume",
			     RK730_DDAC_VOLL, RK730_DDAC_VOLR, 0, 0x1fe, 0,
			     rk730_dac_vol_get,
			     rk730_dac_vol_put,
			     dac_dig_tlv),

	SOC_ENUM("ADC HPF Cutoff", adc_hpf_cutoff_enum),
	SOC_ENUM("DAC HPF Cutoff", dac_hpf_cutoff_enum),
	SOC_ENUM("DAC Ref Buf Chop Freq", dac_ref_buf_chop_freq_enum),
	SOC_ENUM("MIC Chop Freq", mic_chop_freq_enum),
	SOC_ENUM("ADC PGA Chop Freq", adc_pga_chop_freq_enum),
	SOC_ENUM("Out Mux Chop Freq", mux_out_chop_freq_enum),
	SOC_ENUM("Mixer Chop Freq", mix_chop_freq_enum),
	SOC_ENUM("HP / Lineout Chop Freq", hp_lo_chop_freq_enum),
	SOC_ENUM("Mic Bias Volt", micbias_volt_enum),
	SOC_ENUM("Charge Pump Volt", charge_pump_volt_enum),

	SOC_SINGLE("ADCL HPF Switch", RK730_DADC_HPF, 7, 1, 0),
	SOC_SINGLE("ADCR HPF Switch", RK730_DADC_HPF, 6, 1, 0),
	SOC_SINGLE("DAC HPF Switch", RK730_DDAC_MUTE_MIXCTL, 7, 1, 0),
	SOC_SINGLE("ADC Volume Bypass Switch", RK730_DTOP_VUCTL, 7, 1, 0),
	SOC_SINGLE("DAC Volume Bypass Switch", RK730_DTOP_VUCTL, 6, 1, 0),
	SOC_SINGLE("ADC Fade Switch", RK730_DTOP_VUCTL, 5, 1, 0),
	SOC_SINGLE("DAC Fade Switch", RK730_DTOP_VUCTL, 4, 1, 0),
	SOC_SINGLE("ADC Zero Crossing Switch", RK730_DTOP_VUCTL, 1, 1, 0),
	SOC_SINGLE("DAC Zero Crossing Switch", RK730_DTOP_VUCTL, 0, 1, 0),
	SOC_SINGLE("MIC1N / MIC2P Exchanged Switch", RK730_MIC_BOOST_2, 7, 1, 0),
};

static const struct snd_soc_dapm_widget rk730_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY_S("ANA LDO", 0, RK730_LDO, 7, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("OSC CLK", 1, RK730_HK_TOP_2, 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("VAG BUF", 1, RK730_HK_TOP_2, 2, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC BUF", 1, RK730_HK_TOP_2, 1, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC BUF", 1, RK730_HK_TOP_2, 0, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("MICBIAS", 1, RK730_MIC_BIAS, 0, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("Charge Pump", 1, RK730_CHARGE_PUMP, 0, 0,
			      rk730_cp_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY_S("PLL", 2, SND_SOC_NOPM, 0, 0, rk730_pll_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("DAC Bias", 2, RK730_DAC_0, 2, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("HP Bias", 2, RK730_HP_0, 5, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("Line Out Bias", 2, RK730_LINEOUT_1, 7, 1, NULL, 0),

	SND_SOC_DAPM_ADC_E("ADCL", "HiFi Capture", RK730_ADC_0, 0, 1,
			   rk730_adc_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADCR", "HiFi Capture", RK730_ADC_0, 1, 1,
			   rk730_adc_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_DAC_E("DACL", "HiFi Playback", RK730_DAC_0, 0, 1,
			   rk730_dac_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("DACR", "HiFi Playback", RK730_DAC_0, 1, 1,
			   rk730_dac_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_PGA("ADCL PGA", RK730_ADC_PGA_BLOCK_0, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("ADCR PGA", RK730_ADC_PGA_BLOCK_1, 0, 1, NULL, 0),

	SND_SOC_DAPM_PGA("D2SL", RK730_DAC_1, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("D2SR", RK730_DAC_2, 0, 1, NULL, 0),

	SND_SOC_DAPM_PGA("MIC1P", RK730_MIC_BOOST_0, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("MIC1N", RK730_MIC_BOOST_1, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("MIC2P", RK730_MIC_BOOST_2, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("MIC2N", RK730_MIC_BOOST_3, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("DIFFL", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DIFFR", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_PGA("HP Out", RK730_HP_0, 2, 1, NULL, 0),
	SND_SOC_DAPM_PGA("HP Out Stage", RK730_HP_0, 3, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Line Out", RK730_LINEOUT_0, 2, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Line Out Stage", RK730_LINEOUT_0, 3, 1, NULL, 0),

	SND_SOC_DAPM_MUX_E("Left Out Mux", SND_SOC_NOPM, 0, 0, &mux_out_l,
			   rk730_mux_out_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("Right Out Mux", SND_SOC_NOPM, 0, 0, &mux_out_r,
			   rk730_mux_out_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX("Left Input Mux", SND_SOC_NOPM, 0, 0, &mux_input_l),
	SND_SOC_DAPM_MUX("Right Input Mux", SND_SOC_NOPM, 0, 0, &mux_input_r),

	SND_SOC_DAPM_MIXER("Left Out Mixer", RK730_MIXER_2, 0, 1,
			   mix_ctls, ARRAY_SIZE(mix_ctls)),
	SND_SOC_DAPM_MIXER("Right Out Mixer", RK730_MIXER_2, 3, 1,
			   mix_ctls, ARRAY_SIZE(mix_ctls)),

	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),
	SND_SOC_DAPM_OUTPUT("LINEOUTL"),
	SND_SOC_DAPM_OUTPUT("LINEOUTR"),

	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
};

static const struct snd_soc_dapm_route rk730_dapm_routes[] = {
	{ "DACL", NULL, "ANA LDO" },
	{ "DACR", NULL, "ANA LDO" },
	{ "DACL", NULL, "OSC CLK" },
	{ "DACR", NULL, "OSC CLK" },
	{ "DACL", NULL, "VAG BUF" },
	{ "DACR", NULL, "VAG BUF" },
	{ "DACL", NULL, "DAC BUF" },
	{ "DACR", NULL, "DAC BUF" },
	{ "DACL", NULL, "PLL" },
	{ "DACR", NULL, "PLL" },
	{ "DACL", NULL, "DAC Bias" },
	{ "DACR", NULL, "DAC Bias" },

	{ "D2SL", NULL, "DACL" },
	{ "D2SR", NULL, "DACR" },

	{ "Left Out Mixer", NULL, "D2SL" },
	{ "Left Out Mixer", "Left Out Mux Switch", "Left Out Mux" },
	{ "Left Out Mixer", "Right Out Mux Switch", "Right Out Mux" },
	{ "Right Out Mixer", NULL, "D2SR" },
	{ "Right Out Mixer", "Left Out Mux Switch", "Left Out Mux" },
	{ "Right Out Mixer", "Right Out Mux Switch", "Right Out Mux" },

	{ "Left Out Mux", "DIFF", "DIFFL" },
	{ "Left Out Mux", "MIC1N", "MIC1N" },
	{ "Left Out Mux", "MIC2N", "MIC2N" },

	{ "Right Out Mux", "DIFF", "DIFFR" },
	{ "Right Out Mux", "MIC1P", "MIC1P" },
	{ "Right Out Mux", "MIC2P", "MIC2P" },

	{ "HP Out", NULL, "HP Bias" },
	{ "HP Out", NULL, "HP Bias" },
	{ "Line Out", NULL, "Line Out Bias" },

	{ "HP Out", NULL, "Left Out Mixer" },
	{ "HP Out", NULL, "Right Out Mixer" },
	{ "Line Out", NULL, "Left Out Mixer" },
	{ "Line Out", NULL, "Right Out Mixer" },

	{ "HP Out Stage", NULL, "HP Out" },
	{ "Line Out Stage", NULL, "Line Out" },

	{ "HPL", NULL, "HP Out Stage" },
	{ "HPR", NULL, "HP Out Stage" },
	{ "HPL", NULL, "Charge Pump" },
	{ "HPR", NULL, "Charge Pump" },

	{ "LINEOUTL", NULL, "Line Out Stage" },
	{ "LINEOUTR", NULL, "Line Out Stage" },
	{ "LINEOUTL", NULL, "Charge Pump" },
	{ "LINEOUTR", NULL, "Charge Pump" },

	{ "ADCL", NULL, "ANA LDO" },
	{ "ADCR", NULL, "ANA LDO" },
	{ "ADCL", NULL, "OSC CLK" },
	{ "ADCR", NULL, "OSC CLK" },
	{ "ADCL", NULL, "ADC BUF" },
	{ "ADCR", NULL, "ADC BUF" },
	{ "ADCL", NULL, "VAG BUF" },
	{ "ADCR", NULL, "VAG BUF" },
	{ "ADCL", NULL, "PLL" },
	{ "ADCR", NULL, "PLL" },

	{ "ADCL", NULL, "ADCL PGA" },
	{ "ADCR", NULL, "ADCR PGA" },
	{ "ADCL PGA", NULL, "Left Input Mux" },
	{ "ADCR PGA", NULL, "Right Input Mux" },

	{ "Left Input Mux", "DIFF", "DIFFL" },
	{ "Left Input Mux", "VINP1", "MIC1P" },
	{ "Left Input Mux", "VINN1", "MIC1N" },
	{ "Right Input Mux", "DIFF", "DIFFR" },
	{ "Right Input Mux", "VINP2", "MIC2P" },
	{ "Right Input Mux", "VINN2", "MIC2N" },

	{ "DIFFL", NULL, "MIC1P" },
	{ "DIFFL", NULL, "MIC1N" },
	{ "DIFFR", NULL, "MIC2P" },
	{ "DIFFR", NULL, "MIC2N" },

	{ "MIC1P", NULL, "MIC1" },
	{ "MIC1N", NULL, "MIC1" },
	{ "MIC2P", NULL, "MIC2" },
	{ "MIC2N", NULL, "MIC2" },

	{ "MIC1", NULL, "MICBIAS" },
	{ "MIC2", NULL, "MICBIAS" },
};

static unsigned int samplerate_to_bit(unsigned int samplerate)
{
	switch (samplerate) {
	case 8000:
	case 11025:
	case 12000:
		return 0;
	case 16000:
	case 22050:
	case 24000:
		return 1;
	case 32000:
	case 44100:
	case 48000:
		return 2;
	case 64000:
	case 88200:
	case 96000:
		return 3;
	case 128000:
	case 176400:
	case 192000:
		return 4;
	default:
		return 2;
	}
}

static int rk730_dai_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	unsigned int width, rate;

	width = min(params_width(params), 24);
	rate = samplerate_to_bit(params_rate(params));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		snd_soc_component_update_bits(component, RK730_DI2S_RXCR_2,
					      RK730_DI2S_XCR2_VDW_MASK,
					      RK730_DI2S_XCR2_VDW(width));
		snd_soc_component_update_bits(component, RK730_DDAC_SR_LMT,
					      RK730_DDAC_SR_LMT_SRT_MASK,
					      RK730_DDAC_SR_LMT_SRT(rate));
	} else {
		snd_soc_component_update_bits(component, RK730_DI2S_TXCR_2,
					      RK730_DI2S_XCR2_VDW_MASK,
					      RK730_DI2S_XCR2_VDW(width));
		snd_soc_component_update_bits(component, RK730_DADC_SR_ACL,
					      RK730_DADC_SR_ACL_SRT_MASK,
					      RK730_DADC_SR_ACL_SRT(rate));
	}

	return 0;
}

static int rk730_dai_set_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		snd_soc_component_update_bits(component, RK730_DI2S_CKM,
					      RK730_DI2S_CKM_MST_MASK,
					      RK730_DI2S_CKM_MST_SLAVE);
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		snd_soc_component_update_bits(component, RK730_DI2S_CKM,
					      RK730_DI2S_CKM_MST_MASK,
					      RK730_DI2S_CKM_MST_MASTER);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rk730_dai_mute(struct snd_soc_dai *codec_dai, int mute, int direction)
{
	struct snd_soc_component *component = codec_dai->component;

	if (mute)
		snd_soc_component_update_bits(component, RK730_DDAC_MUTE_MIXCTL,
					      RK730_DDAC_MUTE_MIXCTL_MUTE_MASK,
					      RK730_DDAC_MUTE_MIXCTL_MUTE);
	else
		snd_soc_component_update_bits(component, RK730_DDAC_MUTE_MIXCTL,
					      RK730_DDAC_MUTE_MIXCTL_MUTE_MASK,
					      RK730_DDAC_MUTE_MIXCTL_UNMUTE);

	return 0;
}

static int rk730_set_bias_level(struct snd_soc_component *component,
				enum snd_soc_bias_level level)
{
	struct rk730_priv *rk730 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/*
		 * SND_SOC_BIAS_PREPARE is called while preparing for a
		 * transition to ON or away from ON. If current bias_level
		 * is SND_SOC_BIAS_ON, then it is preparing for a transition
		 * away from ON. Disable the clock in that case, otherwise
		 * enable it.
		 */
		if (!IS_ERR(rk730->mclk)) {
			if (snd_soc_component_get_bias_level(component) ==
			    SND_SOC_BIAS_ON)
				clk_disable_unprepare(rk730->mclk);
			else
				clk_prepare_enable(rk730->mclk);
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF)
			regcache_sync(rk730->regmap);
		break;

	case SND_SOC_BIAS_OFF:
		regcache_mark_dirty(rk730->regmap);
		break;
	}
	return 0;
}

#define RK730_RATES	SNDRV_PCM_RATE_8000_192000
#define RK730_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | \
			 SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops rk730_dai_ops = {
	.set_fmt = rk730_dai_set_fmt,
	.hw_params = rk730_dai_hw_params,
	.mute_stream = rk730_dai_mute,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver rk730_dai = {
	.name = "HiFi",
	.playback = {
		 .stream_name = "HiFi Playback",
		 .channels_min = 1,
		 .channels_max = 2,
		 .rates = RK730_RATES,
		 .formats = RK730_FORMATS,
	},
	.capture = {
		 .stream_name = "HiFi Capture",
		 .channels_min = 1,
		 .channels_max = 2,
		 .rates = RK730_RATES,
		 .formats = RK730_FORMATS,
	},
	.ops = &rk730_dai_ops,
};

static int rk730_reset(struct snd_soc_component *component)
{
	struct rk730_priv *rk730 = snd_soc_component_get_drvdata(component);

	clk_prepare_enable(rk730->mclk);
	snd_soc_component_write(component, RK730_DTOP_LPT_SRST, 0x40);
	udelay(10);
	/* WA: Initial micbias default, ADC stopped with micbias(>2.5v) */
	snd_soc_component_update_bits(component, RK730_MIC_BIAS,
			RK730_MIC_BIAS_VOLT_MASK,
			RK730_MIC_BIAS_VOLT_2_2V);
	/* PF: Use the maximum bias current for better performance */
	snd_soc_component_update_bits(component, RK730_HK_TOP_1,
			RK730_HK_TOP_1_IBIAS_STD_SEL_MASK |
			RK730_HK_TOP_1_IBIAS_GAIN_SEL_MASK,
			RK730_HK_TOP_1_IBIAS_STD_SEL_27_5UA |
			RK730_HK_TOP_1_IBIAS_GAIN_SEL_200);
	/* PF: Use the chop 400kHz for better ADC noise performance */
	snd_soc_component_update_bits(component, RK730_MIC_BOOST_3,
			RK730_MIC_BOOST_3_MIC_CHOP_MASK,
			RK730_MIC_BOOST_3_MIC_CHOP(RK730_CHOP_FREQ_400KHZ));
	snd_soc_component_update_bits(component, RK730_ADC_PGA_BLOCK_1,
			RK730_ADC_PGA_BLOCK_1_PGA_CHOP_MASK,
			RK730_ADC_PGA_BLOCK_1_PGA_CHOP(RK730_CHOP_FREQ_400KHZ));

	clk_disable_unprepare(rk730->mclk);

	return 0;
}

static int rk730_probe(struct snd_soc_component *component)
{
	struct rk730_priv *rk730 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	regcache_mark_dirty(rk730->regmap);

	/* initialize private data */
	atomic_set(&rk730->mix_mode, RK730_MIX_MODE_1_PATH);

	ret = snd_soc_component_read(component, RK730_HK_TOP_0);
	if (ret < 0) {
		dev_err(component->dev, "Failed to read register: %d\n", ret);
		return ret;
	}

	rk730_reset(component);

	return ret;
}

static const struct snd_soc_component_driver rk730_component_driver = {
	.probe			= rk730_probe,
	.set_bias_level		= rk730_set_bias_level,
	.controls		= rk730_snd_controls,
	.num_controls		= ARRAY_SIZE(rk730_snd_controls),
	.dapm_widgets		= rk730_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(rk730_dapm_widgets),
	.dapm_routes		= rk730_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(rk730_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct reg_default rk730_reg_defaults[] = {
	{  0x00, 0x40 },
	{  0x02, 0x17 },
	{  0x05, 0x03 },
	{  0x06, 0x22 },
	{  0x07, 0x02 },
	{  0x08, 0x07 },
	{  0x09, 0x01 },
	{  0x0a, 0x01 },
	{  0x0b, 0x01 },
	{  0x0c, 0x01 },
	{  0x0d, 0x01 },
	{  0x0e, 0x01 },
	{  0x0f, 0x07 },
	{  0x10, 0x07 },
	{  0x11, 0xff },
	{  0x12, 0x07 },
	{  0x13, 0x54 },
	{  0x14, 0x04 },
	{  0x15, 0x23 },
	{  0x16, 0x35 },
	{  0x17, 0x67 },
	{  0x18, 0x1e },
	{  0x19, 0xc0 },
	{  0x1a, 0x13 },
	{  0x1b, 0x04 },
	{  0x1c, 0x20 },
	{  0x1f, 0x90 },
	{  0x20, 0x11 },
	{  0x21, 0x09 },
	{  0x22, 0x33 },
	{  0x24, 0x11 },
	{  0x25, 0x11 },
	{  0x26, 0x09 },
	{  0x27, 0x02 },
	{  0x28, 0x2c },
	{  0x2a, 0x0c },
	{  0x2b, 0x80 },
	{  0x40, 0x03 },
	{  0x42, 0x20 },
	{  0x47, 0xe6 },
	{  0x48, 0xd0 },
	{  0x49, 0x17 },
	{  0x4a, 0x26 },
	{  0x4b, 0x01 },
	{  0x4c, 0x05 },
	{  0x4d, 0x0e },
	{  0x4e, 0x09 },
	{  0x4f, 0x02 },
	{  0x5b, 0xe6 },
	{  0x5c, 0xd0 },
	{  0x5d, 0x17 },
	{  0x5e, 0x26 },
	{  0x5f, 0x01 },
	{  0x60, 0x05 },
	{  0x61, 0x0e },
	{  0x62, 0x09 },
	{  0x63, 0x20 },
	{  0x66, 0x01 },
	{  0x69, 0x17 },
	{  0x6c, 0x17 },
};

static bool rk730_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RK730_DTOP_LPT_SRST:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rk730_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_reg = rk730_volatile_register,
	.max_register = RK730_DAC_ATTN,
	.reg_defaults = rk730_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rk730_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};

static int rk730_i2c_probe(struct i2c_client *i2c,
			   const struct i2c_device_id *id)
{
	struct rk730_priv *rk730;
	int ret;

	rk730 = devm_kzalloc(&i2c->dev, sizeof(struct rk730_priv), GFP_KERNEL);
	if (!rk730)
		return -ENOMEM;

	rk730->regmap = devm_regmap_init_i2c(i2c, &rk730_regmap);
	if (IS_ERR(rk730->regmap))
		return PTR_ERR(rk730->regmap);

	rk730->mclk = devm_clk_get(&i2c->dev, "mclk");
	if (IS_ERR(rk730->mclk))
		return PTR_ERR(rk730->mclk);

	i2c_set_clientdata(i2c, rk730);

	ret = devm_snd_soc_register_component(&i2c->dev,
			&rk730_component_driver, &rk730_dai, 1);
	return ret;
}

static const struct i2c_device_id rk730_i2c_id[] = {
	{ "rk730", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rk730_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id rk730_of_match[] = {
	{ .compatible = "rockchip,rk730" },
	{ }
};
MODULE_DEVICE_TABLE(of, rk730_of_match);
#endif

static struct i2c_driver rk730_i2c_driver = {
	.driver = {
		.name = "rk730",
		.of_match_table = of_match_ptr(rk730_of_match),
	},
	.probe  = rk730_i2c_probe,
	.id_table = rk730_i2c_id,
};

module_i2c_driver(rk730_i2c_driver);

MODULE_DESCRIPTION("ASoC RK730 driver");
MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_LICENSE("GPL");
