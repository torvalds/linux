// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * linux/sound/soc/codecs/tlv320aic32x4.c
 *
 * Copyright 2011 Vista Silicon S.L.
 *
 * Author: Javier Martin <javier.martin@vista-silicon.com>
 *
 * Based on sound/soc/codecs/wm8974 and TI driver for kernel 2.6.27.
 */

#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of_clk.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <sound/tlv320aic32x4.h>

#include "tlv320aic32x4.h"

struct aic32x4_priv {
	struct regmap *regmap;
	u32 power_cfg;
	u32 micpga_routing;
	bool swapdacs;
	struct gpio_desc *rstn_gpio;
	const char *mclk_name;

	struct regulator *supply_ldo;
	struct regulator *supply_iov;
	struct regulator *supply_dv;
	struct regulator *supply_av;

	struct aic32x4_setup_data *setup;
	struct device *dev;
	enum aic32x4_type type;

	unsigned int fmt;
};

static int aic32x4_reset_adc(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u32 adc_reg;

	/*
	 * Workaround: the datasheet does not mention a required programming
	 * sequence but experiments show the ADC needs to be reset after each
	 * capture to avoid audible artifacts.
	 */
	switch (event) {
	case SND_SOC_DAPM_POST_PMD:
		adc_reg = snd_soc_component_read(component, AIC32X4_ADCSETUP);
		snd_soc_component_write(component, AIC32X4_ADCSETUP, adc_reg |
					AIC32X4_LADC_EN | AIC32X4_RADC_EN);
		snd_soc_component_write(component, AIC32X4_ADCSETUP, adc_reg);
		break;
	}
	return 0;
};

static int mic_bias_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* Change Mic Bias Registor */
		snd_soc_component_update_bits(component, AIC32X4_MICBIAS,
				AIC32x4_MICBIAS_MASK,
				AIC32X4_MICBIAS_LDOIN |
				AIC32X4_MICBIAS_2075V);
		printk(KERN_DEBUG "%s: Mic Bias will be turned ON\n", __func__);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, AIC32X4_MICBIAS,
				AIC32x4_MICBIAS_MASK, 0);
		printk(KERN_DEBUG "%s: Mic Bias will be turned OFF\n",
				__func__);
		break;
	}

	return 0;
}


static int aic32x4_get_mfp1_gpio(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	u8 val;

	val = snd_soc_component_read(component, AIC32X4_DINCTL);

	ucontrol->value.integer.value[0] = (val & 0x01);

	return 0;
};

static int aic32x4_set_mfp2_gpio(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	u8 val;
	u8 gpio_check;

	val = snd_soc_component_read(component, AIC32X4_DOUTCTL);
	gpio_check = (val & AIC32X4_MFP_GPIO_ENABLED);
	if (gpio_check != AIC32X4_MFP_GPIO_ENABLED) {
		printk(KERN_ERR "%s: MFP2 is not configure as a GPIO output\n",
			__func__);
		return -EINVAL;
	}

	if (ucontrol->value.integer.value[0] == (val & AIC32X4_MFP2_GPIO_OUT_HIGH))
		return 0;

	if (ucontrol->value.integer.value[0])
		val |= ucontrol->value.integer.value[0];
	else
		val &= ~AIC32X4_MFP2_GPIO_OUT_HIGH;

	snd_soc_component_write(component, AIC32X4_DOUTCTL, val);

	return 0;
};

static int aic32x4_get_mfp3_gpio(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	u8 val;

	val = snd_soc_component_read(component, AIC32X4_SCLKCTL);

	ucontrol->value.integer.value[0] = (val & 0x01);

	return 0;
};

static int aic32x4_set_mfp4_gpio(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	u8 val;
	u8 gpio_check;

	val = snd_soc_component_read(component, AIC32X4_MISOCTL);
	gpio_check = (val & AIC32X4_MFP_GPIO_ENABLED);
	if (gpio_check != AIC32X4_MFP_GPIO_ENABLED) {
		printk(KERN_ERR "%s: MFP4 is not configure as a GPIO output\n",
			__func__);
		return -EINVAL;
	}

	if (ucontrol->value.integer.value[0] == (val & AIC32X4_MFP5_GPIO_OUT_HIGH))
		return 0;

	if (ucontrol->value.integer.value[0])
		val |= ucontrol->value.integer.value[0];
	else
		val &= ~AIC32X4_MFP5_GPIO_OUT_HIGH;

	snd_soc_component_write(component, AIC32X4_MISOCTL, val);

	return 0;
};

static int aic32x4_get_mfp5_gpio(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	u8 val;

	val = snd_soc_component_read(component, AIC32X4_GPIOCTL);
	ucontrol->value.integer.value[0] = ((val & 0x2) >> 1);

	return 0;
};

static int aic32x4_set_mfp5_gpio(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	u8 val;
	u8 gpio_check;

	val = snd_soc_component_read(component, AIC32X4_GPIOCTL);
	gpio_check = (val & AIC32X4_MFP5_GPIO_OUTPUT);
	if (gpio_check != AIC32X4_MFP5_GPIO_OUTPUT) {
		printk(KERN_ERR "%s: MFP5 is not configure as a GPIO output\n",
			__func__);
		return -EINVAL;
	}

	if (ucontrol->value.integer.value[0] == (val & 0x1))
		return 0;

	if (ucontrol->value.integer.value[0])
		val |= ucontrol->value.integer.value[0];
	else
		val &= 0xfe;

	snd_soc_component_write(component, AIC32X4_GPIOCTL, val);

	return 0;
};

static const struct snd_kcontrol_new aic32x4_mfp1[] = {
	SOC_SINGLE_BOOL_EXT("MFP1 GPIO", 0, aic32x4_get_mfp1_gpio, NULL),
};

static const struct snd_kcontrol_new aic32x4_mfp2[] = {
	SOC_SINGLE_BOOL_EXT("MFP2 GPIO", 0, NULL, aic32x4_set_mfp2_gpio),
};

static const struct snd_kcontrol_new aic32x4_mfp3[] = {
	SOC_SINGLE_BOOL_EXT("MFP3 GPIO", 0, aic32x4_get_mfp3_gpio, NULL),
};

static const struct snd_kcontrol_new aic32x4_mfp4[] = {
	SOC_SINGLE_BOOL_EXT("MFP4 GPIO", 0, NULL, aic32x4_set_mfp4_gpio),
};

static const struct snd_kcontrol_new aic32x4_mfp5[] = {
	SOC_SINGLE_BOOL_EXT("MFP5 GPIO", 0, aic32x4_get_mfp5_gpio,
		aic32x4_set_mfp5_gpio),
};

/* 0dB min, 0.5dB steps */
static DECLARE_TLV_DB_SCALE(tlv_step_0_5, 0, 50, 0);
/* -63.5dB min, 0.5dB steps */
static DECLARE_TLV_DB_SCALE(tlv_pcm, -6350, 50, 0);
/* -6dB min, 1dB steps */
static DECLARE_TLV_DB_SCALE(tlv_driver_gain, -600, 100, 0);
/* -12dB min, 0.5dB steps */
static DECLARE_TLV_DB_SCALE(tlv_adc_vol, -1200, 50, 0);
/* -6dB min, 1dB steps */
static DECLARE_TLV_DB_SCALE(tlv_tas_driver_gain, -5850, 50, 0);
static DECLARE_TLV_DB_SCALE(tlv_amp_vol, 0, 600, 1);

static const char * const lo_cm_text[] = {
	"Full Chip", "1.65V",
};

static SOC_ENUM_SINGLE_DECL(lo_cm_enum, AIC32X4_CMMODE, 3, lo_cm_text);

static const char * const ptm_text[] = {
	"P3", "P2", "P1",
};

static SOC_ENUM_SINGLE_DECL(l_ptm_enum, AIC32X4_LPLAYBACK, 2, ptm_text);
static SOC_ENUM_SINGLE_DECL(r_ptm_enum, AIC32X4_RPLAYBACK, 2, ptm_text);

static const struct snd_kcontrol_new aic32x4_snd_controls[] = {
	SOC_DOUBLE_R_S_TLV("PCM Playback Volume", AIC32X4_LDACVOL,
			AIC32X4_RDACVOL, 0, -0x7f, 0x30, 7, 0, tlv_pcm),
	SOC_ENUM("DAC Left Playback PowerTune Switch", l_ptm_enum),
	SOC_ENUM("DAC Right Playback PowerTune Switch", r_ptm_enum),
	SOC_DOUBLE_R_S_TLV("HP Driver Gain Volume", AIC32X4_HPLGAIN,
			AIC32X4_HPRGAIN, 0, -0x6, 0x1d, 5, 0,
			tlv_driver_gain),
	SOC_DOUBLE_R_S_TLV("LO Driver Gain Volume", AIC32X4_LOLGAIN,
			AIC32X4_LORGAIN, 0, -0x6, 0x1d, 5, 0,
			tlv_driver_gain),
	SOC_DOUBLE_R("HP DAC Playback Switch", AIC32X4_HPLGAIN,
			AIC32X4_HPRGAIN, 6, 0x01, 1),
	SOC_DOUBLE_R("LO DAC Playback Switch", AIC32X4_LOLGAIN,
			AIC32X4_LORGAIN, 6, 0x01, 1),
	SOC_ENUM("LO Playback Common Mode Switch", lo_cm_enum),
	SOC_DOUBLE_R("Mic PGA Switch", AIC32X4_LMICPGAVOL,
			AIC32X4_RMICPGAVOL, 7, 0x01, 1),

	SOC_SINGLE("ADCFGA Left Mute Switch", AIC32X4_ADCFGA, 7, 1, 0),
	SOC_SINGLE("ADCFGA Right Mute Switch", AIC32X4_ADCFGA, 3, 1, 0),

	SOC_DOUBLE_R_S_TLV("ADC Level Volume", AIC32X4_LADCVOL,
			AIC32X4_RADCVOL, 0, -0x18, 0x28, 6, 0, tlv_adc_vol),
	SOC_DOUBLE_R_TLV("PGA Level Volume", AIC32X4_LMICPGAVOL,
			AIC32X4_RMICPGAVOL, 0, 0x5f, 0, tlv_step_0_5),

	SOC_SINGLE("Auto-mute Switch", AIC32X4_DACMUTE, 4, 7, 0),

	SOC_SINGLE("AGC Left Switch", AIC32X4_LAGC1, 7, 1, 0),
	SOC_SINGLE("AGC Right Switch", AIC32X4_RAGC1, 7, 1, 0),
	SOC_DOUBLE_R("AGC Target Level", AIC32X4_LAGC1, AIC32X4_RAGC1,
			4, 0x07, 0),
	SOC_DOUBLE_R("AGC Gain Hysteresis", AIC32X4_LAGC1, AIC32X4_RAGC1,
			0, 0x03, 0),
	SOC_DOUBLE_R("AGC Hysteresis", AIC32X4_LAGC2, AIC32X4_RAGC2,
			6, 0x03, 0),
	SOC_DOUBLE_R("AGC Noise Threshold", AIC32X4_LAGC2, AIC32X4_RAGC2,
			1, 0x1F, 0),
	SOC_DOUBLE_R("AGC Max PGA", AIC32X4_LAGC3, AIC32X4_RAGC3,
			0, 0x7F, 0),
	SOC_DOUBLE_R("AGC Attack Time", AIC32X4_LAGC4, AIC32X4_RAGC4,
			3, 0x1F, 0),
	SOC_DOUBLE_R("AGC Decay Time", AIC32X4_LAGC5, AIC32X4_RAGC5,
			3, 0x1F, 0),
	SOC_DOUBLE_R("AGC Noise Debounce", AIC32X4_LAGC6, AIC32X4_RAGC6,
			0, 0x1F, 0),
	SOC_DOUBLE_R("AGC Signal Debounce", AIC32X4_LAGC7, AIC32X4_RAGC7,
			0, 0x0F, 0),
};

static const struct snd_kcontrol_new hpl_output_mixer_controls[] = {
	SOC_DAPM_SINGLE("L_DAC Switch", AIC32X4_HPLROUTE, 3, 1, 0),
	SOC_DAPM_SINGLE("IN1_L Switch", AIC32X4_HPLROUTE, 2, 1, 0),
};

static const struct snd_kcontrol_new hpr_output_mixer_controls[] = {
	SOC_DAPM_SINGLE("R_DAC Switch", AIC32X4_HPRROUTE, 3, 1, 0),
	SOC_DAPM_SINGLE("IN1_R Switch", AIC32X4_HPRROUTE, 2, 1, 0),
};

static const struct snd_kcontrol_new lol_output_mixer_controls[] = {
	SOC_DAPM_SINGLE("L_DAC Switch", AIC32X4_LOLROUTE, 3, 1, 0),
};

static const struct snd_kcontrol_new lor_output_mixer_controls[] = {
	SOC_DAPM_SINGLE("R_DAC Switch", AIC32X4_LORROUTE, 3, 1, 0),
};

static const char * const resistor_text[] = {
	"Off", "10 kOhm", "20 kOhm", "40 kOhm",
};

/* Left mixer pins */
static SOC_ENUM_SINGLE_DECL(in1l_lpga_p_enum, AIC32X4_LMICPGAPIN, 6, resistor_text);
static SOC_ENUM_SINGLE_DECL(in2l_lpga_p_enum, AIC32X4_LMICPGAPIN, 4, resistor_text);
static SOC_ENUM_SINGLE_DECL(in3l_lpga_p_enum, AIC32X4_LMICPGAPIN, 2, resistor_text);
static SOC_ENUM_SINGLE_DECL(in1r_lpga_p_enum, AIC32X4_LMICPGAPIN, 0, resistor_text);

static SOC_ENUM_SINGLE_DECL(cml_lpga_n_enum, AIC32X4_LMICPGANIN, 6, resistor_text);
static SOC_ENUM_SINGLE_DECL(in2r_lpga_n_enum, AIC32X4_LMICPGANIN, 4, resistor_text);
static SOC_ENUM_SINGLE_DECL(in3r_lpga_n_enum, AIC32X4_LMICPGANIN, 2, resistor_text);

static const struct snd_kcontrol_new in1l_to_lmixer_controls[] = {
	SOC_DAPM_ENUM("IN1_L L+ Switch", in1l_lpga_p_enum),
};
static const struct snd_kcontrol_new in2l_to_lmixer_controls[] = {
	SOC_DAPM_ENUM("IN2_L L+ Switch", in2l_lpga_p_enum),
};
static const struct snd_kcontrol_new in3l_to_lmixer_controls[] = {
	SOC_DAPM_ENUM("IN3_L L+ Switch", in3l_lpga_p_enum),
};
static const struct snd_kcontrol_new in1r_to_lmixer_controls[] = {
	SOC_DAPM_ENUM("IN1_R L+ Switch", in1r_lpga_p_enum),
};
static const struct snd_kcontrol_new cml_to_lmixer_controls[] = {
	SOC_DAPM_ENUM("CM_L L- Switch", cml_lpga_n_enum),
};
static const struct snd_kcontrol_new in2r_to_lmixer_controls[] = {
	SOC_DAPM_ENUM("IN2_R L- Switch", in2r_lpga_n_enum),
};
static const struct snd_kcontrol_new in3r_to_lmixer_controls[] = {
	SOC_DAPM_ENUM("IN3_R L- Switch", in3r_lpga_n_enum),
};

/*	Right mixer pins */
static SOC_ENUM_SINGLE_DECL(in1r_rpga_p_enum, AIC32X4_RMICPGAPIN, 6, resistor_text);
static SOC_ENUM_SINGLE_DECL(in2r_rpga_p_enum, AIC32X4_RMICPGAPIN, 4, resistor_text);
static SOC_ENUM_SINGLE_DECL(in3r_rpga_p_enum, AIC32X4_RMICPGAPIN, 2, resistor_text);
static SOC_ENUM_SINGLE_DECL(in2l_rpga_p_enum, AIC32X4_RMICPGAPIN, 0, resistor_text);
static SOC_ENUM_SINGLE_DECL(cmr_rpga_n_enum, AIC32X4_RMICPGANIN, 6, resistor_text);
static SOC_ENUM_SINGLE_DECL(in1l_rpga_n_enum, AIC32X4_RMICPGANIN, 4, resistor_text);
static SOC_ENUM_SINGLE_DECL(in3l_rpga_n_enum, AIC32X4_RMICPGANIN, 2, resistor_text);

static const struct snd_kcontrol_new in1r_to_rmixer_controls[] = {
	SOC_DAPM_ENUM("IN1_R R+ Switch", in1r_rpga_p_enum),
};
static const struct snd_kcontrol_new in2r_to_rmixer_controls[] = {
	SOC_DAPM_ENUM("IN2_R R+ Switch", in2r_rpga_p_enum),
};
static const struct snd_kcontrol_new in3r_to_rmixer_controls[] = {
	SOC_DAPM_ENUM("IN3_R R+ Switch", in3r_rpga_p_enum),
};
static const struct snd_kcontrol_new in2l_to_rmixer_controls[] = {
	SOC_DAPM_ENUM("IN2_L R+ Switch", in2l_rpga_p_enum),
};
static const struct snd_kcontrol_new cmr_to_rmixer_controls[] = {
	SOC_DAPM_ENUM("CM_R R- Switch", cmr_rpga_n_enum),
};
static const struct snd_kcontrol_new in1l_to_rmixer_controls[] = {
	SOC_DAPM_ENUM("IN1_L R- Switch", in1l_rpga_n_enum),
};
static const struct snd_kcontrol_new in3l_to_rmixer_controls[] = {
	SOC_DAPM_ENUM("IN3_L R- Switch", in3l_rpga_n_enum),
};

static const struct snd_soc_dapm_widget aic32x4_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback", AIC32X4_DACSETUP, 7, 0),
	SND_SOC_DAPM_MIXER("HPL Output Mixer", SND_SOC_NOPM, 0, 0,
			   &hpl_output_mixer_controls[0],
			   ARRAY_SIZE(hpl_output_mixer_controls)),
	SND_SOC_DAPM_PGA("HPL Power", AIC32X4_OUTPWRCTL, 5, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("LOL Output Mixer", SND_SOC_NOPM, 0, 0,
			   &lol_output_mixer_controls[0],
			   ARRAY_SIZE(lol_output_mixer_controls)),
	SND_SOC_DAPM_PGA("LOL Power", AIC32X4_OUTPWRCTL, 3, 0, NULL, 0),

	SND_SOC_DAPM_DAC("Right DAC", "Right Playback", AIC32X4_DACSETUP, 6, 0),
	SND_SOC_DAPM_MIXER("HPR Output Mixer", SND_SOC_NOPM, 0, 0,
			   &hpr_output_mixer_controls[0],
			   ARRAY_SIZE(hpr_output_mixer_controls)),
	SND_SOC_DAPM_PGA("HPR Power", AIC32X4_OUTPWRCTL, 4, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("LOR Output Mixer", SND_SOC_NOPM, 0, 0,
			   &lor_output_mixer_controls[0],
			   ARRAY_SIZE(lor_output_mixer_controls)),
	SND_SOC_DAPM_PGA("LOR Power", AIC32X4_OUTPWRCTL, 2, 0, NULL, 0),

	SND_SOC_DAPM_ADC("Right ADC", "Right Capture", AIC32X4_ADCSETUP, 6, 0),
	SND_SOC_DAPM_MUX("IN1_R to Right Mixer Positive Resistor", SND_SOC_NOPM, 0, 0,
			in1r_to_rmixer_controls),
	SND_SOC_DAPM_MUX("IN2_R to Right Mixer Positive Resistor", SND_SOC_NOPM, 0, 0,
			in2r_to_rmixer_controls),
	SND_SOC_DAPM_MUX("IN3_R to Right Mixer Positive Resistor", SND_SOC_NOPM, 0, 0,
			in3r_to_rmixer_controls),
	SND_SOC_DAPM_MUX("IN2_L to Right Mixer Positive Resistor", SND_SOC_NOPM, 0, 0,
			in2l_to_rmixer_controls),
	SND_SOC_DAPM_MUX("CM_R to Right Mixer Negative Resistor", SND_SOC_NOPM, 0, 0,
			cmr_to_rmixer_controls),
	SND_SOC_DAPM_MUX("IN1_L to Right Mixer Negative Resistor", SND_SOC_NOPM, 0, 0,
			in1l_to_rmixer_controls),
	SND_SOC_DAPM_MUX("IN3_L to Right Mixer Negative Resistor", SND_SOC_NOPM, 0, 0,
			in3l_to_rmixer_controls),

	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", AIC32X4_ADCSETUP, 7, 0),
	SND_SOC_DAPM_MUX("IN1_L to Left Mixer Positive Resistor", SND_SOC_NOPM, 0, 0,
			in1l_to_lmixer_controls),
	SND_SOC_DAPM_MUX("IN2_L to Left Mixer Positive Resistor", SND_SOC_NOPM, 0, 0,
			in2l_to_lmixer_controls),
	SND_SOC_DAPM_MUX("IN3_L to Left Mixer Positive Resistor", SND_SOC_NOPM, 0, 0,
			in3l_to_lmixer_controls),
	SND_SOC_DAPM_MUX("IN1_R to Left Mixer Positive Resistor", SND_SOC_NOPM, 0, 0,
			in1r_to_lmixer_controls),
	SND_SOC_DAPM_MUX("CM_L to Left Mixer Negative Resistor", SND_SOC_NOPM, 0, 0,
			cml_to_lmixer_controls),
	SND_SOC_DAPM_MUX("IN2_R to Left Mixer Negative Resistor", SND_SOC_NOPM, 0, 0,
			in2r_to_lmixer_controls),
	SND_SOC_DAPM_MUX("IN3_R to Left Mixer Negative Resistor", SND_SOC_NOPM, 0, 0,
			in3r_to_lmixer_controls),

	SND_SOC_DAPM_SUPPLY("Mic Bias", AIC32X4_MICBIAS, 6, 0, mic_bias_event,
			SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_POST("ADC Reset", aic32x4_reset_adc),

	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),
	SND_SOC_DAPM_OUTPUT("LOL"),
	SND_SOC_DAPM_OUTPUT("LOR"),
	SND_SOC_DAPM_INPUT("IN1_L"),
	SND_SOC_DAPM_INPUT("IN1_R"),
	SND_SOC_DAPM_INPUT("IN2_L"),
	SND_SOC_DAPM_INPUT("IN2_R"),
	SND_SOC_DAPM_INPUT("IN3_L"),
	SND_SOC_DAPM_INPUT("IN3_R"),
	SND_SOC_DAPM_INPUT("CM_L"),
	SND_SOC_DAPM_INPUT("CM_R"),
};

static const struct snd_soc_dapm_route aic32x4_dapm_routes[] = {
	/* Left Output */
	{"HPL Output Mixer", "L_DAC Switch", "Left DAC"},
	{"HPL Output Mixer", "IN1_L Switch", "IN1_L"},

	{"HPL Power", NULL, "HPL Output Mixer"},
	{"HPL", NULL, "HPL Power"},

	{"LOL Output Mixer", "L_DAC Switch", "Left DAC"},

	{"LOL Power", NULL, "LOL Output Mixer"},
	{"LOL", NULL, "LOL Power"},

	/* Right Output */
	{"HPR Output Mixer", "R_DAC Switch", "Right DAC"},
	{"HPR Output Mixer", "IN1_R Switch", "IN1_R"},

	{"HPR Power", NULL, "HPR Output Mixer"},
	{"HPR", NULL, "HPR Power"},

	{"LOR Output Mixer", "R_DAC Switch", "Right DAC"},

	{"LOR Power", NULL, "LOR Output Mixer"},
	{"LOR", NULL, "LOR Power"},

	/* Right Input */
	{"Right ADC", NULL, "IN1_R to Right Mixer Positive Resistor"},
	{"IN1_R to Right Mixer Positive Resistor", "10 kOhm", "IN1_R"},
	{"IN1_R to Right Mixer Positive Resistor", "20 kOhm", "IN1_R"},
	{"IN1_R to Right Mixer Positive Resistor", "40 kOhm", "IN1_R"},

	{"Right ADC", NULL, "IN2_R to Right Mixer Positive Resistor"},
	{"IN2_R to Right Mixer Positive Resistor", "10 kOhm", "IN2_R"},
	{"IN2_R to Right Mixer Positive Resistor", "20 kOhm", "IN2_R"},
	{"IN2_R to Right Mixer Positive Resistor", "40 kOhm", "IN2_R"},

	{"Right ADC", NULL, "IN3_R to Right Mixer Positive Resistor"},
	{"IN3_R to Right Mixer Positive Resistor", "10 kOhm", "IN3_R"},
	{"IN3_R to Right Mixer Positive Resistor", "20 kOhm", "IN3_R"},
	{"IN3_R to Right Mixer Positive Resistor", "40 kOhm", "IN3_R"},

	{"Right ADC", NULL, "IN2_L to Right Mixer Positive Resistor"},
	{"IN2_L to Right Mixer Positive Resistor", "10 kOhm", "IN2_L"},
	{"IN2_L to Right Mixer Positive Resistor", "20 kOhm", "IN2_L"},
	{"IN2_L to Right Mixer Positive Resistor", "40 kOhm", "IN2_L"},

	{"Right ADC", NULL, "CM_R to Right Mixer Negative Resistor"},
	{"CM_R to Right Mixer Negative Resistor", "10 kOhm", "CM_R"},
	{"CM_R to Right Mixer Negative Resistor", "20 kOhm", "CM_R"},
	{"CM_R to Right Mixer Negative Resistor", "40 kOhm", "CM_R"},

	{"Right ADC", NULL, "IN1_L to Right Mixer Negative Resistor"},
	{"IN1_L to Right Mixer Negative Resistor", "10 kOhm", "IN1_L"},
	{"IN1_L to Right Mixer Negative Resistor", "20 kOhm", "IN1_L"},
	{"IN1_L to Right Mixer Negative Resistor", "40 kOhm", "IN1_L"},

	{"Right ADC", NULL, "IN3_L to Right Mixer Negative Resistor"},
	{"IN3_L to Right Mixer Negative Resistor", "10 kOhm", "IN3_L"},
	{"IN3_L to Right Mixer Negative Resistor", "20 kOhm", "IN3_L"},
	{"IN3_L to Right Mixer Negative Resistor", "40 kOhm", "IN3_L"},

	/* Left Input */
	{"Left ADC", NULL, "IN1_L to Left Mixer Positive Resistor"},
	{"IN1_L to Left Mixer Positive Resistor", "10 kOhm", "IN1_L"},
	{"IN1_L to Left Mixer Positive Resistor", "20 kOhm", "IN1_L"},
	{"IN1_L to Left Mixer Positive Resistor", "40 kOhm", "IN1_L"},

	{"Left ADC", NULL, "IN2_L to Left Mixer Positive Resistor"},
	{"IN2_L to Left Mixer Positive Resistor", "10 kOhm", "IN2_L"},
	{"IN2_L to Left Mixer Positive Resistor", "20 kOhm", "IN2_L"},
	{"IN2_L to Left Mixer Positive Resistor", "40 kOhm", "IN2_L"},

	{"Left ADC", NULL, "IN3_L to Left Mixer Positive Resistor"},
	{"IN3_L to Left Mixer Positive Resistor", "10 kOhm", "IN3_L"},
	{"IN3_L to Left Mixer Positive Resistor", "20 kOhm", "IN3_L"},
	{"IN3_L to Left Mixer Positive Resistor", "40 kOhm", "IN3_L"},

	{"Left ADC", NULL, "IN1_R to Left Mixer Positive Resistor"},
	{"IN1_R to Left Mixer Positive Resistor", "10 kOhm", "IN1_R"},
	{"IN1_R to Left Mixer Positive Resistor", "20 kOhm", "IN1_R"},
	{"IN1_R to Left Mixer Positive Resistor", "40 kOhm", "IN1_R"},

	{"Left ADC", NULL, "CM_L to Left Mixer Negative Resistor"},
	{"CM_L to Left Mixer Negative Resistor", "10 kOhm", "CM_L"},
	{"CM_L to Left Mixer Negative Resistor", "20 kOhm", "CM_L"},
	{"CM_L to Left Mixer Negative Resistor", "40 kOhm", "CM_L"},

	{"Left ADC", NULL, "IN2_R to Left Mixer Negative Resistor"},
	{"IN2_R to Left Mixer Negative Resistor", "10 kOhm", "IN2_R"},
	{"IN2_R to Left Mixer Negative Resistor", "20 kOhm", "IN2_R"},
	{"IN2_R to Left Mixer Negative Resistor", "40 kOhm", "IN2_R"},

	{"Left ADC", NULL, "IN3_R to Left Mixer Negative Resistor"},
	{"IN3_R to Left Mixer Negative Resistor", "10 kOhm", "IN3_R"},
	{"IN3_R to Left Mixer Negative Resistor", "20 kOhm", "IN3_R"},
	{"IN3_R to Left Mixer Negative Resistor", "40 kOhm", "IN3_R"},
};

static const struct regmap_range_cfg aic32x4_regmap_pages[] = {
	{
		.selector_reg = 0,
		.selector_mask	= 0xff,
		.window_start = 0,
		.window_len = 128,
		.range_min = 0,
		.range_max = AIC32X4_REFPOWERUP,
	},
};

const struct regmap_config aic32x4_regmap_config = {
	.max_register = AIC32X4_REFPOWERUP,
	.ranges = aic32x4_regmap_pages,
	.num_ranges = ARRAY_SIZE(aic32x4_regmap_pages),
};
EXPORT_SYMBOL(aic32x4_regmap_config);

static int aic32x4_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct clk *mclk;
	struct clk *pll;

	pll = devm_clk_get(component->dev, "pll");
	if (IS_ERR(pll))
		return PTR_ERR(pll);

	mclk = clk_get_parent(pll);

	return clk_set_rate(mclk, freq);
}

static int aic32x4_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct aic32x4_priv *aic32x4 = snd_soc_component_get_drvdata(component);
	u8 iface_reg_1 = 0;
	u8 iface_reg_2 = 0;
	u8 iface_reg_3 = 0;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		iface_reg_1 |= AIC32X4_BCLKMASTER | AIC32X4_WCLKMASTER;
		break;
	case SND_SOC_DAIFMT_CBC_CFC:
		break;
	default:
		printk(KERN_ERR "aic32x4: invalid clock provider\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface_reg_1 |= (AIC32X4_DSP_MODE <<
				AIC32X4_IFACE1_DATATYPE_SHIFT);
		iface_reg_3 |= AIC32X4_BCLKINV_MASK; /* invert bit clock */
		iface_reg_2 = 0x01; /* add offset 1 */
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface_reg_1 |= (AIC32X4_DSP_MODE <<
				AIC32X4_IFACE1_DATATYPE_SHIFT);
		iface_reg_3 |= AIC32X4_BCLKINV_MASK; /* invert bit clock */
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		iface_reg_1 |= (AIC32X4_RIGHT_JUSTIFIED_MODE <<
				AIC32X4_IFACE1_DATATYPE_SHIFT);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface_reg_1 |= (AIC32X4_LEFT_JUSTIFIED_MODE <<
				AIC32X4_IFACE1_DATATYPE_SHIFT);
		break;
	default:
		printk(KERN_ERR "aic32x4: invalid DAI interface format\n");
		return -EINVAL;
	}

	aic32x4->fmt = fmt;

	snd_soc_component_update_bits(component, AIC32X4_IFACE1,
				AIC32X4_IFACE1_DATATYPE_MASK |
				AIC32X4_IFACE1_MASTER_MASK, iface_reg_1);
	snd_soc_component_update_bits(component, AIC32X4_IFACE2,
				AIC32X4_DATA_OFFSET_MASK, iface_reg_2);
	snd_soc_component_update_bits(component, AIC32X4_IFACE3,
				AIC32X4_BCLKINV_MASK, iface_reg_3);

	return 0;
}

static int aic32x4_set_aosr(struct snd_soc_component *component, u8 aosr)
{
	return snd_soc_component_write(component, AIC32X4_AOSR, aosr);
}

static int aic32x4_set_dosr(struct snd_soc_component *component, u16 dosr)
{
	snd_soc_component_write(component, AIC32X4_DOSRMSB, dosr >> 8);
	snd_soc_component_write(component, AIC32X4_DOSRLSB,
		      (dosr & 0xff));

	return 0;
}

static int aic32x4_set_processing_blocks(struct snd_soc_component *component,
						u8 r_block, u8 p_block)
{
	struct aic32x4_priv *aic32x4 = snd_soc_component_get_drvdata(component);

	if (aic32x4->type == AIC32X4_TYPE_TAS2505) {
		if (r_block || p_block > 3)
			return -EINVAL;

		snd_soc_component_write(component, AIC32X4_DACSPB, p_block);
	} else { /* AIC32x4 */
		if (r_block > 18 || p_block > 25)
			return -EINVAL;

		snd_soc_component_write(component, AIC32X4_ADCSPB, r_block);
		snd_soc_component_write(component, AIC32X4_DACSPB, p_block);
	}

	return 0;
}

static int aic32x4_setup_clocks(struct snd_soc_component *component,
				unsigned int sample_rate, unsigned int channels,
				unsigned int bit_depth)
{
	struct aic32x4_priv *aic32x4 = snd_soc_component_get_drvdata(component);
	u8 aosr;
	u16 dosr;
	u8 adc_resource_class, dac_resource_class;
	u8 madc, nadc, mdac, ndac, max_nadc, min_mdac, max_ndac;
	u8 dosr_increment;
	u16 max_dosr, min_dosr;
	unsigned long adc_clock_rate, dac_clock_rate;
	int ret;

	static struct clk_bulk_data clocks[] = {
		{ .id = "pll" },
		{ .id = "nadc" },
		{ .id = "madc" },
		{ .id = "ndac" },
		{ .id = "mdac" },
		{ .id = "bdiv" },
	};
	ret = devm_clk_bulk_get(component->dev, ARRAY_SIZE(clocks), clocks);
	if (ret)
		return ret;

	if (sample_rate <= 48000) {
		aosr = 128;
		adc_resource_class = 6;
		dac_resource_class = 8;
		dosr_increment = 8;
		if (aic32x4->type == AIC32X4_TYPE_TAS2505)
			aic32x4_set_processing_blocks(component, 0, 1);
		else
			aic32x4_set_processing_blocks(component, 1, 1);
	} else if (sample_rate <= 96000) {
		aosr = 64;
		adc_resource_class = 6;
		dac_resource_class = 8;
		dosr_increment = 4;
		if (aic32x4->type == AIC32X4_TYPE_TAS2505)
			aic32x4_set_processing_blocks(component, 0, 1);
		else
			aic32x4_set_processing_blocks(component, 1, 9);
	} else if (sample_rate == 192000) {
		aosr = 32;
		adc_resource_class = 3;
		dac_resource_class = 4;
		dosr_increment = 2;
		if (aic32x4->type == AIC32X4_TYPE_TAS2505)
			aic32x4_set_processing_blocks(component, 0, 1);
		else
			aic32x4_set_processing_blocks(component, 13, 19);
	} else {
		dev_err(component->dev, "Sampling rate not supported\n");
		return -EINVAL;
	}

	/* PCM over I2S is always 2-channel */
	if ((aic32x4->fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_I2S)
		channels = 2;

	madc = DIV_ROUND_UP((32 * adc_resource_class), aosr);
	max_dosr = (AIC32X4_MAX_DOSR_FREQ / sample_rate / dosr_increment) *
			dosr_increment;
	min_dosr = (AIC32X4_MIN_DOSR_FREQ / sample_rate / dosr_increment) *
			dosr_increment;
	max_nadc = AIC32X4_MAX_CODEC_CLKIN_FREQ / (madc * aosr * sample_rate);

	for (nadc = max_nadc; nadc > 0; --nadc) {
		adc_clock_rate = nadc * madc * aosr * sample_rate;
		for (dosr = max_dosr; dosr >= min_dosr;
				dosr -= dosr_increment) {
			min_mdac = DIV_ROUND_UP((32 * dac_resource_class), dosr);
			max_ndac = AIC32X4_MAX_CODEC_CLKIN_FREQ /
					(min_mdac * dosr * sample_rate);
			for (mdac = min_mdac; mdac <= 128; ++mdac) {
				for (ndac = max_ndac; ndac > 0; --ndac) {
					dac_clock_rate = ndac * mdac * dosr *
							sample_rate;
					if (dac_clock_rate == adc_clock_rate) {
						if (clk_round_rate(clocks[0].clk, dac_clock_rate) == 0)
							continue;

						clk_set_rate(clocks[0].clk,
							dac_clock_rate);

						clk_set_rate(clocks[1].clk,
							sample_rate * aosr *
							madc);
						clk_set_rate(clocks[2].clk,
							sample_rate * aosr);
						aic32x4_set_aosr(component,
							aosr);

						clk_set_rate(clocks[3].clk,
							sample_rate * dosr *
							mdac);
						clk_set_rate(clocks[4].clk,
							sample_rate * dosr);
						aic32x4_set_dosr(component,
							dosr);

						clk_set_rate(clocks[5].clk,
							sample_rate * channels *
							bit_depth);

						return 0;
					}
				}
			}
		}
	}

	dev_err(component->dev,
		"Could not set clocks to support sample rate.\n");
	return -EINVAL;
}

static int aic32x4_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct aic32x4_priv *aic32x4 = snd_soc_component_get_drvdata(component);
	u8 iface1_reg = 0;
	u8 dacsetup_reg = 0;

	aic32x4_setup_clocks(component, params_rate(params),
			     params_channels(params),
			     params_physical_width(params));

	switch (params_physical_width(params)) {
	case 16:
		iface1_reg |= (AIC32X4_WORD_LEN_16BITS <<
				   AIC32X4_IFACE1_DATALEN_SHIFT);
		break;
	case 20:
		iface1_reg |= (AIC32X4_WORD_LEN_20BITS <<
				   AIC32X4_IFACE1_DATALEN_SHIFT);
		break;
	case 24:
		iface1_reg |= (AIC32X4_WORD_LEN_24BITS <<
				   AIC32X4_IFACE1_DATALEN_SHIFT);
		break;
	case 32:
		iface1_reg |= (AIC32X4_WORD_LEN_32BITS <<
				   AIC32X4_IFACE1_DATALEN_SHIFT);
		break;
	}
	snd_soc_component_update_bits(component, AIC32X4_IFACE1,
				AIC32X4_IFACE1_DATALEN_MASK, iface1_reg);

	if (params_channels(params) == 1) {
		dacsetup_reg = AIC32X4_RDAC2LCHN | AIC32X4_LDAC2LCHN;
	} else {
		if (aic32x4->swapdacs)
			dacsetup_reg = AIC32X4_RDAC2LCHN | AIC32X4_LDAC2RCHN;
		else
			dacsetup_reg = AIC32X4_LDAC2LCHN | AIC32X4_RDAC2RCHN;
	}
	snd_soc_component_update_bits(component, AIC32X4_DACSETUP,
				AIC32X4_DAC_CHAN_MASK, dacsetup_reg);

	return 0;
}

static int aic32x4_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;

	snd_soc_component_update_bits(component, AIC32X4_DACMUTE,
				AIC32X4_MUTEON, mute ? AIC32X4_MUTEON : 0);

	return 0;
}

static int aic32x4_set_bias_level(struct snd_soc_component *component,
				  enum snd_soc_bias_level level)
{
	int ret;

	static struct clk_bulk_data clocks[] = {
		{ .id = "madc" },
		{ .id = "mdac" },
		{ .id = "bdiv" },
	};

	ret = devm_clk_bulk_get(component->dev, ARRAY_SIZE(clocks), clocks);
	if (ret)
		return ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		ret = clk_bulk_prepare_enable(ARRAY_SIZE(clocks), clocks);
		if (ret) {
			dev_err(component->dev, "Failed to enable clocks\n");
			return ret;
		}
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		/* Initial cold start */
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF)
			break;

		clk_bulk_disable_unprepare(ARRAY_SIZE(clocks), clocks);
		break;
	case SND_SOC_BIAS_OFF:
		break;
	}
	return 0;
}

#define AIC32X4_RATES	SNDRV_PCM_RATE_8000_192000
#define AIC32X4_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE \
			 | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_3LE \
			 | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops aic32x4_ops = {
	.hw_params = aic32x4_hw_params,
	.mute_stream = aic32x4_mute,
	.set_fmt = aic32x4_set_dai_fmt,
	.set_sysclk = aic32x4_set_dai_sysclk,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver aic32x4_dai = {
	.name = "tlv320aic32x4-hifi",
	.playback = {
			 .stream_name = "Playback",
			 .channels_min = 1,
			 .channels_max = 2,
			 .rates = AIC32X4_RATES,
			 .formats = AIC32X4_FORMATS,},
	.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = AIC32X4_RATES,
			.formats = AIC32X4_FORMATS,},
	.ops = &aic32x4_ops,
	.symmetric_rate = 1,
};

static void aic32x4_setup_gpios(struct snd_soc_component *component)
{
	struct aic32x4_priv *aic32x4 = snd_soc_component_get_drvdata(component);

	/* setup GPIO functions */
	/* MFP1 */
	if (aic32x4->setup->gpio_func[0] != AIC32X4_MFPX_DEFAULT_VALUE) {
		snd_soc_component_write(component, AIC32X4_DINCTL,
			  aic32x4->setup->gpio_func[0]);
		snd_soc_add_component_controls(component, aic32x4_mfp1,
			ARRAY_SIZE(aic32x4_mfp1));
	}

	/* MFP2 */
	if (aic32x4->setup->gpio_func[1] != AIC32X4_MFPX_DEFAULT_VALUE) {
		snd_soc_component_write(component, AIC32X4_DOUTCTL,
			  aic32x4->setup->gpio_func[1]);
		snd_soc_add_component_controls(component, aic32x4_mfp2,
			ARRAY_SIZE(aic32x4_mfp2));
	}

	/* MFP3 */
	if (aic32x4->setup->gpio_func[2] != AIC32X4_MFPX_DEFAULT_VALUE) {
		snd_soc_component_write(component, AIC32X4_SCLKCTL,
			  aic32x4->setup->gpio_func[2]);
		snd_soc_add_component_controls(component, aic32x4_mfp3,
			ARRAY_SIZE(aic32x4_mfp3));
	}

	/* MFP4 */
	if (aic32x4->setup->gpio_func[3] != AIC32X4_MFPX_DEFAULT_VALUE) {
		snd_soc_component_write(component, AIC32X4_MISOCTL,
			  aic32x4->setup->gpio_func[3]);
		snd_soc_add_component_controls(component, aic32x4_mfp4,
			ARRAY_SIZE(aic32x4_mfp4));
	}

	/* MFP5 */
	if (aic32x4->setup->gpio_func[4] != AIC32X4_MFPX_DEFAULT_VALUE) {
		snd_soc_component_write(component, AIC32X4_GPIOCTL,
			  aic32x4->setup->gpio_func[4]);
		snd_soc_add_component_controls(component, aic32x4_mfp5,
			ARRAY_SIZE(aic32x4_mfp5));
	}
}

static int aic32x4_component_probe(struct snd_soc_component *component)
{
	struct aic32x4_priv *aic32x4 = snd_soc_component_get_drvdata(component);
	u32 tmp_reg;
	int ret;

	static struct clk_bulk_data clocks[] = {
		{ .id = "codec_clkin" },
		{ .id = "pll" },
		{ .id = "bdiv" },
		{ .id = "mdac" },
	};

	ret = devm_clk_bulk_get(component->dev, ARRAY_SIZE(clocks), clocks);
	if (ret)
		return ret;

	if (aic32x4->setup)
		aic32x4_setup_gpios(component);

	clk_set_parent(clocks[0].clk, clocks[1].clk);
	clk_set_parent(clocks[2].clk, clocks[3].clk);

	/* Power platform configuration */
	if (aic32x4->power_cfg & AIC32X4_PWR_MICBIAS_2075_LDOIN) {
		snd_soc_component_write(component, AIC32X4_MICBIAS,
				AIC32X4_MICBIAS_LDOIN | AIC32X4_MICBIAS_2075V);
	}
	if (aic32x4->power_cfg & AIC32X4_PWR_AVDD_DVDD_WEAK_DISABLE)
		snd_soc_component_write(component, AIC32X4_PWRCFG, AIC32X4_AVDDWEAKDISABLE);

	tmp_reg = (aic32x4->power_cfg & AIC32X4_PWR_AIC32X4_LDO_ENABLE) ?
			AIC32X4_LDOCTLEN : 0;
	snd_soc_component_write(component, AIC32X4_LDOCTL, tmp_reg);

	tmp_reg = snd_soc_component_read(component, AIC32X4_CMMODE);
	if (aic32x4->power_cfg & AIC32X4_PWR_CMMODE_LDOIN_RANGE_18_36)
		tmp_reg |= AIC32X4_LDOIN_18_36;
	if (aic32x4->power_cfg & AIC32X4_PWR_CMMODE_HP_LDOIN_POWERED)
		tmp_reg |= AIC32X4_LDOIN2HP;
	snd_soc_component_write(component, AIC32X4_CMMODE, tmp_reg);

	/* Mic PGA routing */
	if (aic32x4->micpga_routing & AIC32X4_MICPGA_ROUTE_LMIC_IN2R_10K)
		snd_soc_component_write(component, AIC32X4_LMICPGANIN,
				AIC32X4_LMICPGANIN_IN2R_10K);
	else
		snd_soc_component_write(component, AIC32X4_LMICPGANIN,
				AIC32X4_LMICPGANIN_CM1L_10K);
	if (aic32x4->micpga_routing & AIC32X4_MICPGA_ROUTE_RMIC_IN1L_10K)
		snd_soc_component_write(component, AIC32X4_RMICPGANIN,
				AIC32X4_RMICPGANIN_IN1L_10K);
	else
		snd_soc_component_write(component, AIC32X4_RMICPGANIN,
				AIC32X4_RMICPGANIN_CM1R_10K);

	/*
	 * Workaround: for an unknown reason, the ADC needs to be powered up
	 * and down for the first capture to work properly. It seems related to
	 * a HW BUG or some kind of behavior not documented in the datasheet.
	 */
	tmp_reg = snd_soc_component_read(component, AIC32X4_ADCSETUP);
	snd_soc_component_write(component, AIC32X4_ADCSETUP, tmp_reg |
				AIC32X4_LADC_EN | AIC32X4_RADC_EN);
	snd_soc_component_write(component, AIC32X4_ADCSETUP, tmp_reg);

	/*
	 * Enable the fast charging feature and ensure the needed 40ms ellapsed
	 * before using the analog circuits.
	 */
	snd_soc_component_write(component, AIC32X4_REFPOWERUP,
				AIC32X4_REFPOWERUP_40MS);
	msleep(40);

	return 0;
}

static int aic32x4_of_xlate_dai_id(struct snd_soc_component *component,
				   struct device_node *endpoint)
{
	/* return dai id 0, whatever the endpoint index */
	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_aic32x4 = {
	.probe			= aic32x4_component_probe,
	.set_bias_level		= aic32x4_set_bias_level,
	.controls		= aic32x4_snd_controls,
	.num_controls		= ARRAY_SIZE(aic32x4_snd_controls),
	.dapm_widgets		= aic32x4_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(aic32x4_dapm_widgets),
	.dapm_routes		= aic32x4_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(aic32x4_dapm_routes),
	.of_xlate_dai_id	= aic32x4_of_xlate_dai_id,
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct snd_kcontrol_new aic32x4_tas2505_snd_controls[] = {
	SOC_SINGLE_S8_TLV("PCM Playback Volume",
			  AIC32X4_LDACVOL, -0x7f, 0x30, tlv_pcm),
	SOC_ENUM("DAC Playback PowerTune Switch", l_ptm_enum),

	SOC_SINGLE_TLV("HP Driver Gain Volume",
			AIC32X4_HPLGAIN, 0, 0x74, 1, tlv_tas_driver_gain),
	SOC_SINGLE("HP DAC Playback Switch", AIC32X4_HPLGAIN, 6, 1, 1),

	SOC_SINGLE_TLV("Speaker Driver Playback Volume",
			TAS2505_SPKVOL1, 0, 0x74, 1, tlv_tas_driver_gain),
	SOC_SINGLE_TLV("Speaker Amplifier Playback Volume",
			TAS2505_SPKVOL2, 4, 5, 0, tlv_amp_vol),

	SOC_SINGLE("Auto-mute Switch", AIC32X4_DACMUTE, 4, 7, 0),
};

static const struct snd_kcontrol_new hp_output_mixer_controls[] = {
	SOC_DAPM_SINGLE("DAC Switch", AIC32X4_HPLROUTE, 3, 1, 0),
};

static const struct snd_soc_dapm_widget aic32x4_tas2505_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", "Playback", AIC32X4_DACSETUP, 7, 0),
	SND_SOC_DAPM_MIXER("HP Output Mixer", SND_SOC_NOPM, 0, 0,
			   &hp_output_mixer_controls[0],
			   ARRAY_SIZE(hp_output_mixer_controls)),
	SND_SOC_DAPM_PGA("HP Power", AIC32X4_OUTPWRCTL, 5, 0, NULL, 0),

	SND_SOC_DAPM_PGA("Speaker Driver", TAS2505_SPK, 1, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("HP"),
	SND_SOC_DAPM_OUTPUT("Speaker"),
};

static const struct snd_soc_dapm_route aic32x4_tas2505_dapm_routes[] = {
	/* Left Output */
	{"HP Output Mixer", "DAC Switch", "DAC"},

	{"HP Power", NULL, "HP Output Mixer"},
	{"HP", NULL, "HP Power"},

	{"Speaker Driver", NULL, "DAC"},
	{"Speaker", NULL, "Speaker Driver"},
};

static struct snd_soc_dai_driver aic32x4_tas2505_dai = {
	.name = "tas2505-hifi",
	.playback = {
			 .stream_name = "Playback",
			 .channels_min = 1,
			 .channels_max = 2,
			 .rates = SNDRV_PCM_RATE_8000_96000,
			 .formats = AIC32X4_FORMATS,},
	.ops = &aic32x4_ops,
	.symmetric_rate = 1,
};

static int aic32x4_tas2505_component_probe(struct snd_soc_component *component)
{
	struct aic32x4_priv *aic32x4 = snd_soc_component_get_drvdata(component);
	u32 tmp_reg;
	int ret;

	static struct clk_bulk_data clocks[] = {
		{ .id = "codec_clkin" },
		{ .id = "pll" },
		{ .id = "bdiv" },
		{ .id = "mdac" },
	};

	ret = devm_clk_bulk_get(component->dev, ARRAY_SIZE(clocks), clocks);
	if (ret)
		return ret;

	if (aic32x4->setup)
		aic32x4_setup_gpios(component);

	clk_set_parent(clocks[0].clk, clocks[1].clk);
	clk_set_parent(clocks[2].clk, clocks[3].clk);

	/* Power platform configuration */
	if (aic32x4->power_cfg & AIC32X4_PWR_AVDD_DVDD_WEAK_DISABLE)
		snd_soc_component_write(component, AIC32X4_PWRCFG, AIC32X4_AVDDWEAKDISABLE);

	tmp_reg = (aic32x4->power_cfg & AIC32X4_PWR_AIC32X4_LDO_ENABLE) ?
			AIC32X4_LDOCTLEN : 0;
	snd_soc_component_write(component, AIC32X4_LDOCTL, tmp_reg);

	tmp_reg = snd_soc_component_read(component, AIC32X4_CMMODE);
	if (aic32x4->power_cfg & AIC32X4_PWR_CMMODE_LDOIN_RANGE_18_36)
		tmp_reg |= AIC32X4_LDOIN_18_36;
	if (aic32x4->power_cfg & AIC32X4_PWR_CMMODE_HP_LDOIN_POWERED)
		tmp_reg |= AIC32X4_LDOIN2HP;
	snd_soc_component_write(component, AIC32X4_CMMODE, tmp_reg);

	/*
	 * Enable the fast charging feature and ensure the needed 40ms ellapsed
	 * before using the analog circuits.
	 */
	snd_soc_component_write(component, TAS2505_REFPOWERUP,
				AIC32X4_REFPOWERUP_40MS);
	msleep(40);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_aic32x4_tas2505 = {
	.probe			= aic32x4_tas2505_component_probe,
	.set_bias_level		= aic32x4_set_bias_level,
	.controls		= aic32x4_tas2505_snd_controls,
	.num_controls		= ARRAY_SIZE(aic32x4_tas2505_snd_controls),
	.dapm_widgets		= aic32x4_tas2505_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(aic32x4_tas2505_dapm_widgets),
	.dapm_routes		= aic32x4_tas2505_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(aic32x4_tas2505_dapm_routes),
	.of_xlate_dai_id	= aic32x4_of_xlate_dai_id,
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int aic32x4_parse_dt(struct aic32x4_priv *aic32x4,
		struct device_node *np)
{
	struct aic32x4_setup_data *aic32x4_setup;
	int ret;

	aic32x4_setup = devm_kzalloc(aic32x4->dev, sizeof(*aic32x4_setup),
							GFP_KERNEL);
	if (!aic32x4_setup)
		return -ENOMEM;

	ret = of_property_match_string(np, "clock-names", "mclk");
	if (ret < 0)
		return -EINVAL;
	aic32x4->mclk_name = of_clk_get_parent_name(np, ret);

	aic32x4->swapdacs = false;
	aic32x4->micpga_routing = 0;
	/* Assert reset using GPIOD_OUT_HIGH, because reset is GPIO_ACTIVE_LOW */
	aic32x4->rstn_gpio = devm_gpiod_get_optional(aic32x4->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(aic32x4->rstn_gpio)) {
		return dev_err_probe(aic32x4->dev, PTR_ERR(aic32x4->rstn_gpio),
				     "Failed to get reset gpio\n");
	} else {
		gpiod_set_consumer_name(aic32x4->rstn_gpio, "tlv320aic32x4_rstn");
	}

	if (of_property_read_u32_array(np, "aic32x4-gpio-func",
				aic32x4_setup->gpio_func, 5) >= 0)
		aic32x4->setup = aic32x4_setup;
	return 0;
}

static void aic32x4_disable_regulators(struct aic32x4_priv *aic32x4)
{
	regulator_disable(aic32x4->supply_iov);

	if (!IS_ERR(aic32x4->supply_ldo))
		regulator_disable(aic32x4->supply_ldo);

	if (!IS_ERR(aic32x4->supply_dv))
		regulator_disable(aic32x4->supply_dv);

	if (!IS_ERR(aic32x4->supply_av))
		regulator_disable(aic32x4->supply_av);
}

static int aic32x4_setup_regulators(struct device *dev,
		struct aic32x4_priv *aic32x4)
{
	int ret = 0;

	aic32x4->supply_ldo = devm_regulator_get_optional(dev, "ldoin");
	aic32x4->supply_iov = devm_regulator_get(dev, "iov");
	aic32x4->supply_dv = devm_regulator_get_optional(dev, "dv");
	aic32x4->supply_av = devm_regulator_get_optional(dev, "av");

	/* Check if the regulator requirements are fulfilled */

	if (IS_ERR(aic32x4->supply_iov)) {
		dev_err(dev, "Missing supply 'iov'\n");
		return PTR_ERR(aic32x4->supply_iov);
	}

	if (IS_ERR(aic32x4->supply_ldo)) {
		if (PTR_ERR(aic32x4->supply_ldo) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		if (IS_ERR(aic32x4->supply_dv)) {
			dev_err(dev, "Missing supply 'dv' or 'ldoin'\n");
			return PTR_ERR(aic32x4->supply_dv);
		}
		if (IS_ERR(aic32x4->supply_av)) {
			dev_err(dev, "Missing supply 'av' or 'ldoin'\n");
			return PTR_ERR(aic32x4->supply_av);
		}
	} else {
		if (PTR_ERR(aic32x4->supply_dv) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		if (PTR_ERR(aic32x4->supply_av) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	}

	ret = regulator_enable(aic32x4->supply_iov);
	if (ret) {
		dev_err(dev, "Failed to enable regulator iov\n");
		return ret;
	}

	if (!IS_ERR(aic32x4->supply_ldo)) {
		ret = regulator_enable(aic32x4->supply_ldo);
		if (ret) {
			dev_err(dev, "Failed to enable regulator ldo\n");
			goto error_ldo;
		}
	}

	if (!IS_ERR(aic32x4->supply_dv)) {
		ret = regulator_enable(aic32x4->supply_dv);
		if (ret) {
			dev_err(dev, "Failed to enable regulator dv\n");
			goto error_dv;
		}
	}

	if (!IS_ERR(aic32x4->supply_av)) {
		ret = regulator_enable(aic32x4->supply_av);
		if (ret) {
			dev_err(dev, "Failed to enable regulator av\n");
			goto error_av;
		}
	}

	if (!IS_ERR(aic32x4->supply_ldo) && IS_ERR(aic32x4->supply_av))
		aic32x4->power_cfg |= AIC32X4_PWR_AIC32X4_LDO_ENABLE;

	return 0;

error_av:
	if (!IS_ERR(aic32x4->supply_dv))
		regulator_disable(aic32x4->supply_dv);

error_dv:
	if (!IS_ERR(aic32x4->supply_ldo))
		regulator_disable(aic32x4->supply_ldo);

error_ldo:
	regulator_disable(aic32x4->supply_iov);
	return ret;
}

int aic32x4_probe(struct device *dev, struct regmap *regmap,
		  enum aic32x4_type type)
{
	struct aic32x4_priv *aic32x4;
	struct device_node *np = dev->of_node;
	int ret;

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	aic32x4 = devm_kzalloc(dev, sizeof(struct aic32x4_priv),
				   GFP_KERNEL);
	if (aic32x4 == NULL)
		return -ENOMEM;

	aic32x4->dev = dev;
	aic32x4->type = type;

	dev_set_drvdata(dev, aic32x4);

	if (np) {
		ret = aic32x4_parse_dt(aic32x4, np);
		if (ret) {
			dev_err(dev, "Failed to parse DT node\n");
			return ret;
		}
	} else {
		aic32x4->power_cfg = 0;
		aic32x4->swapdacs = false;
		aic32x4->micpga_routing = 0;
		aic32x4->rstn_gpio = NULL;
		aic32x4->mclk_name = "mclk";
	}

	ret = aic32x4_setup_regulators(dev, aic32x4);
	if (ret) {
		dev_err(dev, "Failed to setup regulators\n");
		return ret;
	}

	if (aic32x4->rstn_gpio) {
		ndelay(10);
		/* deassert reset */
		gpiod_set_value_cansleep(aic32x4->rstn_gpio, 0);
		mdelay(1);
	}

	ret = regmap_write(regmap, AIC32X4_RESET, 0x01);
	if (ret)
		goto err_disable_regulators;

	ret = aic32x4_register_clocks(dev, aic32x4->mclk_name);
	if (ret)
		goto err_disable_regulators;

	switch (aic32x4->type) {
	case AIC32X4_TYPE_TAS2505:
		ret = devm_snd_soc_register_component(dev,
			&soc_component_dev_aic32x4_tas2505, &aic32x4_tas2505_dai, 1);
		break;
	default:
		ret = devm_snd_soc_register_component(dev,
			&soc_component_dev_aic32x4, &aic32x4_dai, 1);
	}

	if (ret) {
		dev_err(dev, "Failed to register component\n");
		goto err_disable_regulators;
	}

	return 0;

err_disable_regulators:
	aic32x4_disable_regulators(aic32x4);

	return ret;
}
EXPORT_SYMBOL(aic32x4_probe);

void aic32x4_remove(struct device *dev)
{
	struct aic32x4_priv *aic32x4 = dev_get_drvdata(dev);

	aic32x4_disable_regulators(aic32x4);
}
EXPORT_SYMBOL(aic32x4_remove);

MODULE_DESCRIPTION("ASoC tlv320aic32x4 codec driver");
MODULE_AUTHOR("Javier Martin <javier.martin@vista-silicon.com>");
MODULE_LICENSE("GPL");
