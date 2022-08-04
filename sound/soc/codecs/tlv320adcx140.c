// SPDX-License-Identifier: GPL-2.0
// TLV320ADCX140 Sound driver
// Copyright (C) 2020 Texas Instruments Incorporated - https://www.ti.com/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "tlv320adcx140.h"

struct adcx140_priv {
	struct snd_soc_component *component;
	struct regulator *supply_areg;
	struct gpio_desc *gpio_reset;
	struct regmap *regmap;
	struct device *dev;

	bool micbias_vg;

	unsigned int dai_fmt;
	unsigned int tdm_delay;
	unsigned int slot_width;
};

static const char * const gpo_config_names[] = {
	"ti,gpo-config-1",
	"ti,gpo-config-2",
	"ti,gpo-config-3",
	"ti,gpo-config-4",
};

static const struct reg_default adcx140_reg_defaults[] = {
	{ ADCX140_PAGE_SELECT, 0x00 },
	{ ADCX140_SW_RESET, 0x00 },
	{ ADCX140_SLEEP_CFG, 0x00 },
	{ ADCX140_SHDN_CFG, 0x05 },
	{ ADCX140_ASI_CFG0, 0x30 },
	{ ADCX140_ASI_CFG1, 0x00 },
	{ ADCX140_ASI_CFG2, 0x00 },
	{ ADCX140_ASI_CH1, 0x00 },
	{ ADCX140_ASI_CH2, 0x01 },
	{ ADCX140_ASI_CH3, 0x02 },
	{ ADCX140_ASI_CH4, 0x03 },
	{ ADCX140_ASI_CH5, 0x04 },
	{ ADCX140_ASI_CH6, 0x05 },
	{ ADCX140_ASI_CH7, 0x06 },
	{ ADCX140_ASI_CH8, 0x07 },
	{ ADCX140_MST_CFG0, 0x02 },
	{ ADCX140_MST_CFG1, 0x48 },
	{ ADCX140_ASI_STS, 0xff },
	{ ADCX140_CLK_SRC, 0x10 },
	{ ADCX140_PDMCLK_CFG, 0x40 },
	{ ADCX140_PDM_CFG, 0x00 },
	{ ADCX140_GPIO_CFG0, 0x22 },
	{ ADCX140_GPO_CFG0, 0x00 },
	{ ADCX140_GPO_CFG1, 0x00 },
	{ ADCX140_GPO_CFG2, 0x00 },
	{ ADCX140_GPO_CFG3, 0x00 },
	{ ADCX140_GPO_VAL, 0x00 },
	{ ADCX140_GPIO_MON, 0x00 },
	{ ADCX140_GPI_CFG0, 0x00 },
	{ ADCX140_GPI_CFG1, 0x00 },
	{ ADCX140_GPI_MON, 0x00 },
	{ ADCX140_INT_CFG, 0x00 },
	{ ADCX140_INT_MASK0, 0xff },
	{ ADCX140_INT_LTCH0, 0x00 },
	{ ADCX140_BIAS_CFG, 0x00 },
	{ ADCX140_CH1_CFG0, 0x00 },
	{ ADCX140_CH1_CFG1, 0x00 },
	{ ADCX140_CH1_CFG2, 0xc9 },
	{ ADCX140_CH1_CFG3, 0x80 },
	{ ADCX140_CH1_CFG4, 0x00 },
	{ ADCX140_CH2_CFG0, 0x00 },
	{ ADCX140_CH2_CFG1, 0x00 },
	{ ADCX140_CH2_CFG2, 0xc9 },
	{ ADCX140_CH2_CFG3, 0x80 },
	{ ADCX140_CH2_CFG4, 0x00 },
	{ ADCX140_CH3_CFG0, 0x00 },
	{ ADCX140_CH3_CFG1, 0x00 },
	{ ADCX140_CH3_CFG2, 0xc9 },
	{ ADCX140_CH3_CFG3, 0x80 },
	{ ADCX140_CH3_CFG4, 0x00 },
	{ ADCX140_CH4_CFG0, 0x00 },
	{ ADCX140_CH4_CFG1, 0x00 },
	{ ADCX140_CH4_CFG2, 0xc9 },
	{ ADCX140_CH4_CFG3, 0x80 },
	{ ADCX140_CH4_CFG4, 0x00 },
	{ ADCX140_CH5_CFG2, 0xc9 },
	{ ADCX140_CH5_CFG3, 0x80 },
	{ ADCX140_CH5_CFG4, 0x00 },
	{ ADCX140_CH6_CFG2, 0xc9 },
	{ ADCX140_CH6_CFG3, 0x80 },
	{ ADCX140_CH6_CFG4, 0x00 },
	{ ADCX140_CH7_CFG2, 0xc9 },
	{ ADCX140_CH7_CFG3, 0x80 },
	{ ADCX140_CH7_CFG4, 0x00 },
	{ ADCX140_CH8_CFG2, 0xc9 },
	{ ADCX140_CH8_CFG3, 0x80 },
	{ ADCX140_CH8_CFG4, 0x00 },
	{ ADCX140_DSP_CFG0, 0x01 },
	{ ADCX140_DSP_CFG1, 0x40 },
	{ ADCX140_DRE_CFG0, 0x7b },
	{ ADCX140_AGC_CFG0, 0xe7 },
	{ ADCX140_IN_CH_EN, 0xf0 },
	{ ADCX140_ASI_OUT_CH_EN, 0x00 },
	{ ADCX140_PWR_CFG, 0x00 },
	{ ADCX140_DEV_STS0, 0x00 },
	{ ADCX140_DEV_STS1, 0x80 },
};

static const struct regmap_range_cfg adcx140_ranges[] = {
	{
		.range_min = 0,
		.range_max = 12 * 128,
		.selector_reg = ADCX140_PAGE_SELECT,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 128,
	},
};

static bool adcx140_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADCX140_SW_RESET:
	case ADCX140_DEV_STS0:
	case ADCX140_DEV_STS1:
	case ADCX140_ASI_STS:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config adcx140_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_defaults = adcx140_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(adcx140_reg_defaults),
	.cache_type = REGCACHE_FLAT,
	.ranges = adcx140_ranges,
	.num_ranges = ARRAY_SIZE(adcx140_ranges),
	.max_register = 12 * 128,
	.volatile_reg = adcx140_volatile,
};

/* Digital Volume control. From -100 to 27 dB in 0.5 dB steps */
static DECLARE_TLV_DB_SCALE(dig_vol_tlv, -10050, 50, 0);

/* ADC gain. From 0 to 42 dB in 1 dB steps */
static DECLARE_TLV_DB_SCALE(adc_tlv, 0, 100, 0);

/* DRE Level. From -12 dB to -66 dB in 1 dB steps */
static DECLARE_TLV_DB_SCALE(dre_thresh_tlv, -6600, 100, 0);
/* DRE Max Gain. From 2 dB to 26 dB in 2 dB steps */
static DECLARE_TLV_DB_SCALE(dre_gain_tlv, 200, 200, 0);

/* AGC Level. From -6 dB to -36 dB in 2 dB steps */
static DECLARE_TLV_DB_SCALE(agc_thresh_tlv, -3600, 200, 0);
/* AGC Max Gain. From 3 dB to 42 dB in 3 dB steps */
static DECLARE_TLV_DB_SCALE(agc_gain_tlv, 300, 300, 0);

static const char * const decimation_filter_text[] = {
	"Linear Phase", "Low Latency", "Ultra-low Latency"
};

static SOC_ENUM_SINGLE_DECL(decimation_filter_enum, ADCX140_DSP_CFG0, 4,
			    decimation_filter_text);

static const struct snd_kcontrol_new decimation_filter_controls[] = {
	SOC_DAPM_ENUM("Decimation Filter", decimation_filter_enum),
};

static const char * const pdmclk_text[] = {
	"2.8224 MHz", "1.4112 MHz", "705.6 kHz", "5.6448 MHz"
};

static SOC_ENUM_SINGLE_DECL(pdmclk_select_enum, ADCX140_PDMCLK_CFG, 0,
			    pdmclk_text);

static const struct snd_kcontrol_new pdmclk_div_controls[] = {
	SOC_DAPM_ENUM("PDM Clk Divider Select", pdmclk_select_enum),
};

static const char * const resistor_text[] = {
	"2.5 kOhm", "10 kOhm", "20 kOhm"
};

static SOC_ENUM_SINGLE_DECL(in1_resistor_enum, ADCX140_CH1_CFG0, 2,
			    resistor_text);
static SOC_ENUM_SINGLE_DECL(in2_resistor_enum, ADCX140_CH2_CFG0, 2,
			    resistor_text);
static SOC_ENUM_SINGLE_DECL(in3_resistor_enum, ADCX140_CH3_CFG0, 2,
			    resistor_text);
static SOC_ENUM_SINGLE_DECL(in4_resistor_enum, ADCX140_CH4_CFG0, 2,
			    resistor_text);

static const struct snd_kcontrol_new in1_resistor_controls[] = {
	SOC_DAPM_ENUM("CH1 Resistor Select", in1_resistor_enum),
};
static const struct snd_kcontrol_new in2_resistor_controls[] = {
	SOC_DAPM_ENUM("CH2 Resistor Select", in2_resistor_enum),
};
static const struct snd_kcontrol_new in3_resistor_controls[] = {
	SOC_DAPM_ENUM("CH3 Resistor Select", in3_resistor_enum),
};
static const struct snd_kcontrol_new in4_resistor_controls[] = {
	SOC_DAPM_ENUM("CH4 Resistor Select", in4_resistor_enum),
};

/* Analog/Digital Selection */
static const char * const adcx140_mic_sel_text[] = {"Analog", "Line In", "Digital"};
static const char * const adcx140_analog_sel_text[] = {"Analog", "Line In"};

static SOC_ENUM_SINGLE_DECL(adcx140_mic1p_enum,
			    ADCX140_CH1_CFG0, 5,
			    adcx140_mic_sel_text);

static const struct snd_kcontrol_new adcx140_dapm_mic1p_control =
SOC_DAPM_ENUM("MIC1P MUX", adcx140_mic1p_enum);

static SOC_ENUM_SINGLE_DECL(adcx140_mic1_analog_enum,
			    ADCX140_CH1_CFG0, 7,
			    adcx140_analog_sel_text);

static const struct snd_kcontrol_new adcx140_dapm_mic1_analog_control =
SOC_DAPM_ENUM("MIC1 Analog MUX", adcx140_mic1_analog_enum);

static SOC_ENUM_SINGLE_DECL(adcx140_mic1m_enum,
			    ADCX140_CH1_CFG0, 5,
			    adcx140_mic_sel_text);

static const struct snd_kcontrol_new adcx140_dapm_mic1m_control =
SOC_DAPM_ENUM("MIC1M MUX", adcx140_mic1m_enum);

static SOC_ENUM_SINGLE_DECL(adcx140_mic2p_enum,
			    ADCX140_CH2_CFG0, 5,
			    adcx140_mic_sel_text);

static const struct snd_kcontrol_new adcx140_dapm_mic2p_control =
SOC_DAPM_ENUM("MIC2P MUX", adcx140_mic2p_enum);

static SOC_ENUM_SINGLE_DECL(adcx140_mic2_analog_enum,
			    ADCX140_CH2_CFG0, 7,
			    adcx140_analog_sel_text);

static const struct snd_kcontrol_new adcx140_dapm_mic2_analog_control =
SOC_DAPM_ENUM("MIC2 Analog MUX", adcx140_mic2_analog_enum);

static SOC_ENUM_SINGLE_DECL(adcx140_mic2m_enum,
			    ADCX140_CH2_CFG0, 5,
			    adcx140_mic_sel_text);

static const struct snd_kcontrol_new adcx140_dapm_mic2m_control =
SOC_DAPM_ENUM("MIC2M MUX", adcx140_mic2m_enum);

static SOC_ENUM_SINGLE_DECL(adcx140_mic3p_enum,
			    ADCX140_CH3_CFG0, 5,
			    adcx140_mic_sel_text);

static const struct snd_kcontrol_new adcx140_dapm_mic3p_control =
SOC_DAPM_ENUM("MIC3P MUX", adcx140_mic3p_enum);

static SOC_ENUM_SINGLE_DECL(adcx140_mic3_analog_enum,
			    ADCX140_CH3_CFG0, 7,
			    adcx140_analog_sel_text);

static const struct snd_kcontrol_new adcx140_dapm_mic3_analog_control =
SOC_DAPM_ENUM("MIC3 Analog MUX", adcx140_mic3_analog_enum);

static SOC_ENUM_SINGLE_DECL(adcx140_mic3m_enum,
			    ADCX140_CH3_CFG0, 5,
			    adcx140_mic_sel_text);

static const struct snd_kcontrol_new adcx140_dapm_mic3m_control =
SOC_DAPM_ENUM("MIC3M MUX", adcx140_mic3m_enum);

static SOC_ENUM_SINGLE_DECL(adcx140_mic4p_enum,
			    ADCX140_CH4_CFG0, 5,
			    adcx140_mic_sel_text);

static const struct snd_kcontrol_new adcx140_dapm_mic4p_control =
SOC_DAPM_ENUM("MIC4P MUX", adcx140_mic4p_enum);

static SOC_ENUM_SINGLE_DECL(adcx140_mic4_analog_enum,
			    ADCX140_CH4_CFG0, 7,
			    adcx140_analog_sel_text);

static const struct snd_kcontrol_new adcx140_dapm_mic4_analog_control =
SOC_DAPM_ENUM("MIC4 Analog MUX", adcx140_mic4_analog_enum);

static SOC_ENUM_SINGLE_DECL(adcx140_mic4m_enum,
			    ADCX140_CH4_CFG0, 5,
			    adcx140_mic_sel_text);

static const struct snd_kcontrol_new adcx140_dapm_mic4m_control =
SOC_DAPM_ENUM("MIC4M MUX", adcx140_mic4m_enum);

static const struct snd_kcontrol_new adcx140_dapm_ch1_en_switch =
	SOC_DAPM_SINGLE("Switch", ADCX140_ASI_OUT_CH_EN, 7, 1, 0);
static const struct snd_kcontrol_new adcx140_dapm_ch2_en_switch =
	SOC_DAPM_SINGLE("Switch", ADCX140_ASI_OUT_CH_EN, 6, 1, 0);
static const struct snd_kcontrol_new adcx140_dapm_ch3_en_switch =
	SOC_DAPM_SINGLE("Switch", ADCX140_ASI_OUT_CH_EN, 5, 1, 0);
static const struct snd_kcontrol_new adcx140_dapm_ch4_en_switch =
	SOC_DAPM_SINGLE("Switch", ADCX140_ASI_OUT_CH_EN, 4, 1, 0);
static const struct snd_kcontrol_new adcx140_dapm_ch5_en_switch =
	SOC_DAPM_SINGLE("Switch", ADCX140_ASI_OUT_CH_EN, 3, 1, 0);
static const struct snd_kcontrol_new adcx140_dapm_ch6_en_switch =
	SOC_DAPM_SINGLE("Switch", ADCX140_ASI_OUT_CH_EN, 2, 1, 0);
static const struct snd_kcontrol_new adcx140_dapm_ch7_en_switch =
	SOC_DAPM_SINGLE("Switch", ADCX140_ASI_OUT_CH_EN, 1, 1, 0);
static const struct snd_kcontrol_new adcx140_dapm_ch8_en_switch =
	SOC_DAPM_SINGLE("Switch", ADCX140_ASI_OUT_CH_EN, 0, 1, 0);

static const struct snd_kcontrol_new adcx140_dapm_ch1_dre_en_switch =
	SOC_DAPM_SINGLE("Switch", ADCX140_CH1_CFG0, 0, 1, 0);
static const struct snd_kcontrol_new adcx140_dapm_ch2_dre_en_switch =
	SOC_DAPM_SINGLE("Switch", ADCX140_CH2_CFG0, 0, 1, 0);
static const struct snd_kcontrol_new adcx140_dapm_ch3_dre_en_switch =
	SOC_DAPM_SINGLE("Switch", ADCX140_CH3_CFG0, 0, 1, 0);
static const struct snd_kcontrol_new adcx140_dapm_ch4_dre_en_switch =
	SOC_DAPM_SINGLE("Switch", ADCX140_CH4_CFG0, 0, 1, 0);

static const struct snd_kcontrol_new adcx140_dapm_dre_en_switch =
	SOC_DAPM_SINGLE("Switch", ADCX140_DSP_CFG1, 3, 1, 0);

/* Output Mixer */
static const struct snd_kcontrol_new adcx140_output_mixer_controls[] = {
	SOC_DAPM_SINGLE("Digital CH1 Switch", 0, 0, 0, 0),
	SOC_DAPM_SINGLE("Digital CH2 Switch", 0, 0, 0, 0),
	SOC_DAPM_SINGLE("Digital CH3 Switch", 0, 0, 0, 0),
	SOC_DAPM_SINGLE("Digital CH4 Switch", 0, 0, 0, 0),
};

static const struct snd_soc_dapm_widget adcx140_dapm_widgets[] = {
	/* Analog Differential Inputs */
	SND_SOC_DAPM_INPUT("MIC1P"),
	SND_SOC_DAPM_INPUT("MIC1M"),
	SND_SOC_DAPM_INPUT("MIC2P"),
	SND_SOC_DAPM_INPUT("MIC2M"),
	SND_SOC_DAPM_INPUT("MIC3P"),
	SND_SOC_DAPM_INPUT("MIC3M"),
	SND_SOC_DAPM_INPUT("MIC4P"),
	SND_SOC_DAPM_INPUT("MIC4M"),

	SND_SOC_DAPM_OUTPUT("CH1_OUT"),
	SND_SOC_DAPM_OUTPUT("CH2_OUT"),
	SND_SOC_DAPM_OUTPUT("CH3_OUT"),
	SND_SOC_DAPM_OUTPUT("CH4_OUT"),
	SND_SOC_DAPM_OUTPUT("CH5_OUT"),
	SND_SOC_DAPM_OUTPUT("CH6_OUT"),
	SND_SOC_DAPM_OUTPUT("CH7_OUT"),
	SND_SOC_DAPM_OUTPUT("CH8_OUT"),

	SND_SOC_DAPM_MIXER("Output Mixer", SND_SOC_NOPM, 0, 0,
		&adcx140_output_mixer_controls[0],
		ARRAY_SIZE(adcx140_output_mixer_controls)),

	/* Input Selection to MIC_PGA */
	SND_SOC_DAPM_MUX("MIC1P Input Mux", SND_SOC_NOPM, 0, 0,
			 &adcx140_dapm_mic1p_control),
	SND_SOC_DAPM_MUX("MIC2P Input Mux", SND_SOC_NOPM, 0, 0,
			 &adcx140_dapm_mic2p_control),
	SND_SOC_DAPM_MUX("MIC3P Input Mux", SND_SOC_NOPM, 0, 0,
			 &adcx140_dapm_mic3p_control),
	SND_SOC_DAPM_MUX("MIC4P Input Mux", SND_SOC_NOPM, 0, 0,
			 &adcx140_dapm_mic4p_control),

	/* Input Selection to MIC_PGA */
	SND_SOC_DAPM_MUX("MIC1 Analog Mux", SND_SOC_NOPM, 0, 0,
			 &adcx140_dapm_mic1_analog_control),
	SND_SOC_DAPM_MUX("MIC2 Analog Mux", SND_SOC_NOPM, 0, 0,
			 &adcx140_dapm_mic2_analog_control),
	SND_SOC_DAPM_MUX("MIC3 Analog Mux", SND_SOC_NOPM, 0, 0,
			 &adcx140_dapm_mic3_analog_control),
	SND_SOC_DAPM_MUX("MIC4 Analog Mux", SND_SOC_NOPM, 0, 0,
			 &adcx140_dapm_mic4_analog_control),

	SND_SOC_DAPM_MUX("MIC1M Input Mux", SND_SOC_NOPM, 0, 0,
			 &adcx140_dapm_mic1m_control),
	SND_SOC_DAPM_MUX("MIC2M Input Mux", SND_SOC_NOPM, 0, 0,
			 &adcx140_dapm_mic2m_control),
	SND_SOC_DAPM_MUX("MIC3M Input Mux", SND_SOC_NOPM, 0, 0,
			 &adcx140_dapm_mic3m_control),
	SND_SOC_DAPM_MUX("MIC4M Input Mux", SND_SOC_NOPM, 0, 0,
			 &adcx140_dapm_mic4m_control),

	SND_SOC_DAPM_PGA("MIC_GAIN_CTL_CH1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MIC_GAIN_CTL_CH2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MIC_GAIN_CTL_CH3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MIC_GAIN_CTL_CH4", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_ADC("CH1_ADC", "CH1 Capture", ADCX140_IN_CH_EN, 7, 0),
	SND_SOC_DAPM_ADC("CH2_ADC", "CH2 Capture", ADCX140_IN_CH_EN, 6, 0),
	SND_SOC_DAPM_ADC("CH3_ADC", "CH3 Capture", ADCX140_IN_CH_EN, 5, 0),
	SND_SOC_DAPM_ADC("CH4_ADC", "CH4 Capture", ADCX140_IN_CH_EN, 4, 0),

	SND_SOC_DAPM_ADC("CH1_DIG", "CH1 Capture", ADCX140_IN_CH_EN, 7, 0),
	SND_SOC_DAPM_ADC("CH2_DIG", "CH2 Capture", ADCX140_IN_CH_EN, 6, 0),
	SND_SOC_DAPM_ADC("CH3_DIG", "CH3 Capture", ADCX140_IN_CH_EN, 5, 0),
	SND_SOC_DAPM_ADC("CH4_DIG", "CH4 Capture", ADCX140_IN_CH_EN, 4, 0),
	SND_SOC_DAPM_ADC("CH5_DIG", "CH5 Capture", ADCX140_IN_CH_EN, 3, 0),
	SND_SOC_DAPM_ADC("CH6_DIG", "CH6 Capture", ADCX140_IN_CH_EN, 2, 0),
	SND_SOC_DAPM_ADC("CH7_DIG", "CH7 Capture", ADCX140_IN_CH_EN, 1, 0),
	SND_SOC_DAPM_ADC("CH8_DIG", "CH8 Capture", ADCX140_IN_CH_EN, 0, 0),


	SND_SOC_DAPM_SWITCH("CH1_ASI_EN", SND_SOC_NOPM, 0, 0,
			    &adcx140_dapm_ch1_en_switch),
	SND_SOC_DAPM_SWITCH("CH2_ASI_EN", SND_SOC_NOPM, 0, 0,
			    &adcx140_dapm_ch2_en_switch),
	SND_SOC_DAPM_SWITCH("CH3_ASI_EN", SND_SOC_NOPM, 0, 0,
			    &adcx140_dapm_ch3_en_switch),
	SND_SOC_DAPM_SWITCH("CH4_ASI_EN", SND_SOC_NOPM, 0, 0,
			    &adcx140_dapm_ch4_en_switch),

	SND_SOC_DAPM_SWITCH("CH5_ASI_EN", SND_SOC_NOPM, 0, 0,
			    &adcx140_dapm_ch5_en_switch),
	SND_SOC_DAPM_SWITCH("CH6_ASI_EN", SND_SOC_NOPM, 0, 0,
			    &adcx140_dapm_ch6_en_switch),
	SND_SOC_DAPM_SWITCH("CH7_ASI_EN", SND_SOC_NOPM, 0, 0,
			    &adcx140_dapm_ch7_en_switch),
	SND_SOC_DAPM_SWITCH("CH8_ASI_EN", SND_SOC_NOPM, 0, 0,
			    &adcx140_dapm_ch8_en_switch),

	SND_SOC_DAPM_SWITCH("DRE_ENABLE", SND_SOC_NOPM, 0, 0,
			    &adcx140_dapm_dre_en_switch),

	SND_SOC_DAPM_SWITCH("CH1_DRE_EN", SND_SOC_NOPM, 0, 0,
			    &adcx140_dapm_ch1_dre_en_switch),
	SND_SOC_DAPM_SWITCH("CH2_DRE_EN", SND_SOC_NOPM, 0, 0,
			    &adcx140_dapm_ch2_dre_en_switch),
	SND_SOC_DAPM_SWITCH("CH3_DRE_EN", SND_SOC_NOPM, 0, 0,
			    &adcx140_dapm_ch3_dre_en_switch),
	SND_SOC_DAPM_SWITCH("CH4_DRE_EN", SND_SOC_NOPM, 0, 0,
			    &adcx140_dapm_ch4_dre_en_switch),

	SND_SOC_DAPM_MUX("IN1 Analog Mic Resistor", SND_SOC_NOPM, 0, 0,
			in1_resistor_controls),
	SND_SOC_DAPM_MUX("IN2 Analog Mic Resistor", SND_SOC_NOPM, 0, 0,
			in2_resistor_controls),
	SND_SOC_DAPM_MUX("IN3 Analog Mic Resistor", SND_SOC_NOPM, 0, 0,
			in3_resistor_controls),
	SND_SOC_DAPM_MUX("IN4 Analog Mic Resistor", SND_SOC_NOPM, 0, 0,
			in4_resistor_controls),

	SND_SOC_DAPM_MUX("PDM Clk Div Select", SND_SOC_NOPM, 0, 0,
			pdmclk_div_controls),

	SND_SOC_DAPM_MUX("Decimation Filter", SND_SOC_NOPM, 0, 0,
			decimation_filter_controls),
};

static const struct snd_soc_dapm_route adcx140_audio_map[] = {
	/* Outputs */
	{"CH1_OUT", NULL, "Output Mixer"},
	{"CH2_OUT", NULL, "Output Mixer"},
	{"CH3_OUT", NULL, "Output Mixer"},
	{"CH4_OUT", NULL, "Output Mixer"},

	{"CH1_ASI_EN", "Switch", "CH1_ADC"},
	{"CH2_ASI_EN", "Switch", "CH2_ADC"},
	{"CH3_ASI_EN", "Switch", "CH3_ADC"},
	{"CH4_ASI_EN", "Switch", "CH4_ADC"},

	{"CH1_ASI_EN", "Switch", "CH1_DIG"},
	{"CH2_ASI_EN", "Switch", "CH2_DIG"},
	{"CH3_ASI_EN", "Switch", "CH3_DIG"},
	{"CH4_ASI_EN", "Switch", "CH4_DIG"},
	{"CH5_ASI_EN", "Switch", "CH5_DIG"},
	{"CH6_ASI_EN", "Switch", "CH6_DIG"},
	{"CH7_ASI_EN", "Switch", "CH7_DIG"},
	{"CH8_ASI_EN", "Switch", "CH8_DIG"},

	{"CH5_ASI_EN", "Switch", "CH5_OUT"},
	{"CH6_ASI_EN", "Switch", "CH6_OUT"},
	{"CH7_ASI_EN", "Switch", "CH7_OUT"},
	{"CH8_ASI_EN", "Switch", "CH8_OUT"},

	{"Decimation Filter", "Linear Phase", "DRE_ENABLE"},
	{"Decimation Filter", "Low Latency", "DRE_ENABLE"},
	{"Decimation Filter", "Ultra-low Latency", "DRE_ENABLE"},

	{"DRE_ENABLE", "Switch", "CH1_DRE_EN"},
	{"DRE_ENABLE", "Switch", "CH2_DRE_EN"},
	{"DRE_ENABLE", "Switch", "CH3_DRE_EN"},
	{"DRE_ENABLE", "Switch", "CH4_DRE_EN"},

	{"CH1_DRE_EN", "Switch", "CH1_ADC"},
	{"CH2_DRE_EN", "Switch", "CH2_ADC"},
	{"CH3_DRE_EN", "Switch", "CH3_ADC"},
	{"CH4_DRE_EN", "Switch", "CH4_ADC"},

	/* Mic input */
	{"CH1_ADC", NULL, "MIC_GAIN_CTL_CH1"},
	{"CH2_ADC", NULL, "MIC_GAIN_CTL_CH2"},
	{"CH3_ADC", NULL, "MIC_GAIN_CTL_CH3"},
	{"CH4_ADC", NULL, "MIC_GAIN_CTL_CH4"},

	{"MIC_GAIN_CTL_CH1", NULL, "IN1 Analog Mic Resistor"},
	{"MIC_GAIN_CTL_CH1", NULL, "IN1 Analog Mic Resistor"},
	{"MIC_GAIN_CTL_CH2", NULL, "IN2 Analog Mic Resistor"},
	{"MIC_GAIN_CTL_CH2", NULL, "IN2 Analog Mic Resistor"},
	{"MIC_GAIN_CTL_CH3", NULL, "IN3 Analog Mic Resistor"},
	{"MIC_GAIN_CTL_CH3", NULL, "IN3 Analog Mic Resistor"},
	{"MIC_GAIN_CTL_CH4", NULL, "IN4 Analog Mic Resistor"},
	{"MIC_GAIN_CTL_CH4", NULL, "IN4 Analog Mic Resistor"},

	{"IN1 Analog Mic Resistor", "2.5 kOhm", "MIC1P Input Mux"},
	{"IN1 Analog Mic Resistor", "10 kOhm", "MIC1P Input Mux"},
	{"IN1 Analog Mic Resistor", "20 kOhm", "MIC1P Input Mux"},

	{"IN1 Analog Mic Resistor", "2.5 kOhm", "MIC1M Input Mux"},
	{"IN1 Analog Mic Resistor", "10 kOhm", "MIC1M Input Mux"},
	{"IN1 Analog Mic Resistor", "20 kOhm", "MIC1M Input Mux"},

	{"IN2 Analog Mic Resistor", "2.5 kOhm", "MIC2P Input Mux"},
	{"IN2 Analog Mic Resistor", "10 kOhm", "MIC2P Input Mux"},
	{"IN2 Analog Mic Resistor", "20 kOhm", "MIC2P Input Mux"},

	{"IN2 Analog Mic Resistor", "2.5 kOhm", "MIC2M Input Mux"},
	{"IN2 Analog Mic Resistor", "10 kOhm", "MIC2M Input Mux"},
	{"IN2 Analog Mic Resistor", "20 kOhm", "MIC2M Input Mux"},

	{"IN3 Analog Mic Resistor", "2.5 kOhm", "MIC3P Input Mux"},
	{"IN3 Analog Mic Resistor", "10 kOhm", "MIC3P Input Mux"},
	{"IN3 Analog Mic Resistor", "20 kOhm", "MIC3P Input Mux"},

	{"IN3 Analog Mic Resistor", "2.5 kOhm", "MIC3M Input Mux"},
	{"IN3 Analog Mic Resistor", "10 kOhm", "MIC3M Input Mux"},
	{"IN3 Analog Mic Resistor", "20 kOhm", "MIC3M Input Mux"},

	{"IN4 Analog Mic Resistor", "2.5 kOhm", "MIC4P Input Mux"},
	{"IN4 Analog Mic Resistor", "10 kOhm", "MIC4P Input Mux"},
	{"IN4 Analog Mic Resistor", "20 kOhm", "MIC4P Input Mux"},

	{"IN4 Analog Mic Resistor", "2.5 kOhm", "MIC4M Input Mux"},
	{"IN4 Analog Mic Resistor", "10 kOhm", "MIC4M Input Mux"},
	{"IN4 Analog Mic Resistor", "20 kOhm", "MIC4M Input Mux"},

	{"PDM Clk Div Select", "2.8224 MHz", "MIC1P Input Mux"},
	{"PDM Clk Div Select", "1.4112 MHz", "MIC1P Input Mux"},
	{"PDM Clk Div Select", "705.6 kHz", "MIC1P Input Mux"},
	{"PDM Clk Div Select", "5.6448 MHz", "MIC1P Input Mux"},

	{"MIC1P Input Mux", NULL, "CH1_DIG"},
	{"MIC1M Input Mux", NULL, "CH2_DIG"},
	{"MIC2P Input Mux", NULL, "CH3_DIG"},
	{"MIC2M Input Mux", NULL, "CH4_DIG"},
	{"MIC3P Input Mux", NULL, "CH5_DIG"},
	{"MIC3M Input Mux", NULL, "CH6_DIG"},
	{"MIC4P Input Mux", NULL, "CH7_DIG"},
	{"MIC4M Input Mux", NULL, "CH8_DIG"},

	{"MIC1 Analog Mux", "Line In", "MIC1P"},
	{"MIC2 Analog Mux", "Line In", "MIC2P"},
	{"MIC3 Analog Mux", "Line In", "MIC3P"},
	{"MIC4 Analog Mux", "Line In", "MIC4P"},

	{"MIC1P Input Mux", "Analog", "MIC1P"},
	{"MIC1M Input Mux", "Analog", "MIC1M"},
	{"MIC2P Input Mux", "Analog", "MIC2P"},
	{"MIC2M Input Mux", "Analog", "MIC2M"},
	{"MIC3P Input Mux", "Analog", "MIC3P"},
	{"MIC3M Input Mux", "Analog", "MIC3M"},
	{"MIC4P Input Mux", "Analog", "MIC4P"},
	{"MIC4M Input Mux", "Analog", "MIC4M"},

	{"MIC1P Input Mux", "Digital", "MIC1P"},
	{"MIC1M Input Mux", "Digital", "MIC1M"},
	{"MIC2P Input Mux", "Digital", "MIC2P"},
	{"MIC2M Input Mux", "Digital", "MIC2M"},
	{"MIC3P Input Mux", "Digital", "MIC3P"},
	{"MIC3M Input Mux", "Digital", "MIC3M"},
	{"MIC4P Input Mux", "Digital", "MIC4P"},
	{"MIC4M Input Mux", "Digital", "MIC4M"},
};

static const struct snd_kcontrol_new adcx140_snd_controls[] = {
	SOC_SINGLE_TLV("Analog CH1 Mic Gain Volume", ADCX140_CH1_CFG1, 2, 42, 0,
			adc_tlv),
	SOC_SINGLE_TLV("Analog CH2 Mic Gain Volume", ADCX140_CH2_CFG1, 2, 42, 0,
			adc_tlv),
	SOC_SINGLE_TLV("Analog CH3 Mic Gain Volume", ADCX140_CH3_CFG1, 2, 42, 0,
			adc_tlv),
	SOC_SINGLE_TLV("Analog CH4 Mic Gain Volume", ADCX140_CH4_CFG1, 2, 42, 0,
			adc_tlv),

	SOC_SINGLE_TLV("DRE Threshold", ADCX140_DRE_CFG0, 4, 9, 0,
		       dre_thresh_tlv),
	SOC_SINGLE_TLV("DRE Max Gain", ADCX140_DRE_CFG0, 0, 12, 0,
		       dre_gain_tlv),

	SOC_SINGLE_TLV("AGC Threshold", ADCX140_AGC_CFG0, 4, 15, 0,
		       agc_thresh_tlv),
	SOC_SINGLE_TLV("AGC Max Gain", ADCX140_AGC_CFG0, 0, 13, 0,
		       agc_gain_tlv),

	SOC_SINGLE_TLV("Digital CH1 Out Volume", ADCX140_CH1_CFG2,
			0, 0xff, 0, dig_vol_tlv),
	SOC_SINGLE_TLV("Digital CH2 Out Volume", ADCX140_CH2_CFG2,
			0, 0xff, 0, dig_vol_tlv),
	SOC_SINGLE_TLV("Digital CH3 Out Volume", ADCX140_CH3_CFG2,
			0, 0xff, 0, dig_vol_tlv),
	SOC_SINGLE_TLV("Digital CH4 Out Volume", ADCX140_CH4_CFG2,
			0, 0xff, 0, dig_vol_tlv),
	SOC_SINGLE_TLV("Digital CH5 Out Volume", ADCX140_CH5_CFG2,
			0, 0xff, 0, dig_vol_tlv),
	SOC_SINGLE_TLV("Digital CH6 Out Volume", ADCX140_CH6_CFG2,
			0, 0xff, 0, dig_vol_tlv),
	SOC_SINGLE_TLV("Digital CH7 Out Volume", ADCX140_CH7_CFG2,
			0, 0xff, 0, dig_vol_tlv),
	SOC_SINGLE_TLV("Digital CH8 Out Volume", ADCX140_CH8_CFG2,
			0, 0xff, 0, dig_vol_tlv),
};

static int adcx140_reset(struct adcx140_priv *adcx140)
{
	int ret = 0;

	if (adcx140->gpio_reset) {
		gpiod_direction_output(adcx140->gpio_reset, 0);
		/* 8.4.1: wait for hw shutdown (25ms) + >= 1ms */
		usleep_range(30000, 100000);
		gpiod_direction_output(adcx140->gpio_reset, 1);
	} else {
		ret = regmap_write(adcx140->regmap, ADCX140_SW_RESET,
				   ADCX140_RESET);
	}

	/* 8.4.2: wait >= 10 ms after entering sleep mode. */
	usleep_range(10000, 100000);

	return ret;
}

static void adcx140_pwr_ctrl(struct adcx140_priv *adcx140, bool power_state)
{
	int pwr_ctrl = 0;

	if (power_state)
		pwr_ctrl = ADCX140_PWR_CFG_ADC_PDZ | ADCX140_PWR_CFG_PLL_PDZ;

	if (adcx140->micbias_vg && power_state)
		pwr_ctrl |= ADCX140_PWR_CFG_BIAS_PDZ;

	regmap_update_bits(adcx140->regmap, ADCX140_PWR_CFG,
			   ADCX140_PWR_CTRL_MSK, pwr_ctrl);
}

static int adcx140_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct adcx140_priv *adcx140 = snd_soc_component_get_drvdata(component);
	u8 data = 0;

	switch (params_width(params)) {
	case 16:
		data = ADCX140_16_BIT_WORD;
		break;
	case 20:
		data = ADCX140_20_BIT_WORD;
		break;
	case 24:
		data = ADCX140_24_BIT_WORD;
		break;
	case 32:
		data = ADCX140_32_BIT_WORD;
		break;
	default:
		dev_err(component->dev, "%s: Unsupported width %d\n",
			__func__, params_width(params));
		return -EINVAL;
	}

	adcx140_pwr_ctrl(adcx140, false);

	snd_soc_component_update_bits(component, ADCX140_ASI_CFG0,
			    ADCX140_WORD_LEN_MSK, data);

	adcx140_pwr_ctrl(adcx140, true);

	return 0;
}

static int adcx140_set_dai_fmt(struct snd_soc_dai *codec_dai,
			       unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct adcx140_priv *adcx140 = snd_soc_component_get_drvdata(component);
	u8 iface_reg1 = 0;
	u8 iface_reg2 = 0;
	int offset = 0;
	bool inverted_bclk = false;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface_reg2 |= ADCX140_BCLK_FSYNC_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
	case SND_SOC_DAIFMT_CBM_CFS:
	default:
		dev_err(component->dev, "Invalid DAI master/slave interface\n");
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface_reg1 |= ADCX140_I2S_MODE_BIT;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface_reg1 |= ADCX140_LEFT_JUST_BIT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		offset = 1;
		inverted_bclk = true;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		inverted_bclk = true;
		break;
	default:
		dev_err(component->dev, "Invalid DAI interface format\n");
		return -EINVAL;
	}

	/* signal polarity */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_NF:
	case SND_SOC_DAIFMT_IB_IF:
		inverted_bclk = !inverted_bclk;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface_reg1 |= ADCX140_FSYNCINV_BIT;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		dev_err(component->dev, "Invalid DAI clock signal polarity\n");
		return -EINVAL;
	}

	if (inverted_bclk)
		iface_reg1 |= ADCX140_BCLKINV_BIT;

	adcx140->dai_fmt = fmt & SND_SOC_DAIFMT_FORMAT_MASK;

	adcx140_pwr_ctrl(adcx140, false);

	snd_soc_component_update_bits(component, ADCX140_ASI_CFG0,
				      ADCX140_FSYNCINV_BIT |
				      ADCX140_BCLKINV_BIT |
				      ADCX140_ASI_FORMAT_MSK,
				      iface_reg1);
	snd_soc_component_update_bits(component, ADCX140_MST_CFG0,
				      ADCX140_BCLK_FSYNC_MASTER, iface_reg2);

	/* Configure data offset */
	snd_soc_component_update_bits(component, ADCX140_ASI_CFG1,
				      ADCX140_TX_OFFSET_MASK, offset);

	adcx140_pwr_ctrl(adcx140, true);

	return 0;
}

static int adcx140_set_dai_tdm_slot(struct snd_soc_dai *codec_dai,
				  unsigned int tx_mask, unsigned int rx_mask,
				  int slots, int slot_width)
{
	struct snd_soc_component *component = codec_dai->component;
	struct adcx140_priv *adcx140 = snd_soc_component_get_drvdata(component);
	unsigned int lsb;

	/* TDM based on DSP mode requires slots to be adjacent */
	lsb = __ffs(tx_mask);
	if ((lsb + 1) != __fls(tx_mask)) {
		dev_err(component->dev, "Invalid mask, slots must be adjacent\n");
		return -EINVAL;
	}

	switch (slot_width) {
	case 16:
	case 20:
	case 24:
	case 32:
		break;
	default:
		dev_err(component->dev, "Unsupported slot width %d\n", slot_width);
		return -EINVAL;
	}

	adcx140->tdm_delay = lsb;
	adcx140->slot_width = slot_width;

	return 0;
}

static const struct snd_soc_dai_ops adcx140_dai_ops = {
	.hw_params	= adcx140_hw_params,
	.set_fmt	= adcx140_set_dai_fmt,
	.set_tdm_slot	= adcx140_set_dai_tdm_slot,
};

static int adcx140_configure_gpo(struct adcx140_priv *adcx140)
{
	u32 gpo_outputs[ADCX140_NUM_GPOS];
	u32 gpo_output_val = 0;
	int ret;
	int i;

	for (i = 0; i < ADCX140_NUM_GPOS; i++) {
		ret = device_property_read_u32_array(adcx140->dev,
						     gpo_config_names[i],
						     gpo_outputs,
						     ADCX140_NUM_GPO_CFGS);
		if (ret)
			continue;

		if (gpo_outputs[0] > ADCX140_GPO_CFG_MAX) {
			dev_err(adcx140->dev, "GPO%d config out of range\n", i + 1);
			return -EINVAL;
		}

		if (gpo_outputs[1] > ADCX140_GPO_DRV_MAX) {
			dev_err(adcx140->dev, "GPO%d drive out of range\n", i + 1);
			return -EINVAL;
		}

		gpo_output_val = gpo_outputs[0] << ADCX140_GPO_SHIFT |
				 gpo_outputs[1];
		ret = regmap_write(adcx140->regmap, ADCX140_GPO_CFG0 + i,
				   gpo_output_val);
		if (ret)
			return ret;
	}

	return 0;

}

static int adcx140_configure_gpio(struct adcx140_priv *adcx140)
{
	int gpio_count = 0;
	u32 gpio_outputs[ADCX140_NUM_GPIO_CFGS];
	u32 gpio_output_val = 0;
	int ret;

	gpio_count = device_property_count_u32(adcx140->dev,
			"ti,gpio-config");
	if (gpio_count == 0)
		return 0;

	if (gpio_count != ADCX140_NUM_GPIO_CFGS)
		return -EINVAL;

	ret = device_property_read_u32_array(adcx140->dev, "ti,gpio-config",
			gpio_outputs, gpio_count);
	if (ret)
		return ret;

	if (gpio_outputs[0] > ADCX140_GPIO_CFG_MAX) {
		dev_err(adcx140->dev, "GPIO config out of range\n");
		return -EINVAL;
	}

	if (gpio_outputs[1] > ADCX140_GPIO_DRV_MAX) {
		dev_err(adcx140->dev, "GPIO drive out of range\n");
		return -EINVAL;
	}

	gpio_output_val = gpio_outputs[0] << ADCX140_GPIO_SHIFT
		| gpio_outputs[1];

	return regmap_write(adcx140->regmap, ADCX140_GPIO_CFG0, gpio_output_val);
}

static int adcx140_codec_probe(struct snd_soc_component *component)
{
	struct adcx140_priv *adcx140 = snd_soc_component_get_drvdata(component);
	int sleep_cfg_val = ADCX140_WAKE_DEV;
	u32 bias_source;
	u32 vref_source;
	u8 bias_cfg;
	int pdm_count;
	u32 pdm_edges[ADCX140_NUM_PDM_EDGES];
	u32 pdm_edge_val = 0;
	int gpi_count;
	u32 gpi_inputs[ADCX140_NUM_GPI_PINS];
	u32 gpi_input_val = 0;
	int i;
	int ret;
	bool tx_high_z;

	ret = device_property_read_u32(adcx140->dev, "ti,mic-bias-source",
				      &bias_source);
	if (ret || bias_source > ADCX140_MIC_BIAS_VAL_AVDD) {
		bias_source = ADCX140_MIC_BIAS_VAL_VREF;
		adcx140->micbias_vg = false;
	} else {
		adcx140->micbias_vg = true;
	}

	ret = device_property_read_u32(adcx140->dev, "ti,vref-source",
				      &vref_source);
	if (ret)
		vref_source = ADCX140_MIC_BIAS_VREF_275V;

	if (vref_source > ADCX140_MIC_BIAS_VREF_1375V) {
		dev_err(adcx140->dev, "Mic Bias source value is invalid\n");
		return -EINVAL;
	}

	bias_cfg = bias_source << ADCX140_MIC_BIAS_SHIFT | vref_source;

	ret = adcx140_reset(adcx140);
	if (ret)
		goto out;

	if (adcx140->supply_areg == NULL)
		sleep_cfg_val |= ADCX140_AREG_INTERNAL;

	ret = regmap_write(adcx140->regmap, ADCX140_SLEEP_CFG, sleep_cfg_val);
	if (ret) {
		dev_err(adcx140->dev, "setting sleep config failed %d\n", ret);
		goto out;
	}

	/* 8.4.3: Wait >= 1ms after entering active mode. */
	usleep_range(1000, 100000);

	pdm_count = device_property_count_u32(adcx140->dev,
					      "ti,pdm-edge-select");
	if (pdm_count <= ADCX140_NUM_PDM_EDGES && pdm_count > 0) {
		ret = device_property_read_u32_array(adcx140->dev,
						     "ti,pdm-edge-select",
						     pdm_edges, pdm_count);
		if (ret)
			return ret;

		for (i = 0; i < pdm_count; i++)
			pdm_edge_val |= pdm_edges[i] << (ADCX140_PDM_EDGE_SHIFT - i);

		ret = regmap_write(adcx140->regmap, ADCX140_PDM_CFG,
				   pdm_edge_val);
		if (ret)
			return ret;
	}

	gpi_count = device_property_count_u32(adcx140->dev, "ti,gpi-config");
	if (gpi_count <= ADCX140_NUM_GPI_PINS && gpi_count > 0) {
		ret = device_property_read_u32_array(adcx140->dev,
						     "ti,gpi-config",
						     gpi_inputs, gpi_count);
		if (ret)
			return ret;

		gpi_input_val = gpi_inputs[ADCX140_GPI1_INDEX] << ADCX140_GPI_SHIFT |
				gpi_inputs[ADCX140_GPI2_INDEX];

		ret = regmap_write(adcx140->regmap, ADCX140_GPI_CFG0,
				   gpi_input_val);
		if (ret)
			return ret;

		gpi_input_val = gpi_inputs[ADCX140_GPI3_INDEX] << ADCX140_GPI_SHIFT |
				gpi_inputs[ADCX140_GPI4_INDEX];

		ret = regmap_write(adcx140->regmap, ADCX140_GPI_CFG1,
				   gpi_input_val);
		if (ret)
			return ret;
	}

	ret = adcx140_configure_gpio(adcx140);
	if (ret)
		return ret;

	ret = adcx140_configure_gpo(adcx140);
	if (ret)
		goto out;

	ret = regmap_update_bits(adcx140->regmap, ADCX140_BIAS_CFG,
				ADCX140_MIC_BIAS_VAL_MSK |
				ADCX140_MIC_BIAS_VREF_MSK, bias_cfg);
	if (ret)
		dev_err(adcx140->dev, "setting MIC bias failed %d\n", ret);

	tx_high_z = device_property_read_bool(adcx140->dev, "ti,asi-tx-drive");
	if (tx_high_z) {
		ret = regmap_update_bits(adcx140->regmap, ADCX140_ASI_CFG0,
				 ADCX140_TX_FILL, ADCX140_TX_FILL);
		if (ret) {
			dev_err(adcx140->dev, "Setting Tx drive failed %d\n", ret);
			goto out;
		}
	}

	adcx140_pwr_ctrl(adcx140, true);
out:
	return ret;
}

static int adcx140_set_bias_level(struct snd_soc_component *component,
				  enum snd_soc_bias_level level)
{
	struct adcx140_priv *adcx140 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
	case SND_SOC_BIAS_STANDBY:
		adcx140_pwr_ctrl(adcx140, true);
		break;
	case SND_SOC_BIAS_OFF:
		adcx140_pwr_ctrl(adcx140, false);
		break;
	}

	return 0;
}

static const struct snd_soc_component_driver soc_codec_driver_adcx140 = {
	.probe			= adcx140_codec_probe,
	.set_bias_level		= adcx140_set_bias_level,
	.controls		= adcx140_snd_controls,
	.num_controls		= ARRAY_SIZE(adcx140_snd_controls),
	.dapm_widgets		= adcx140_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(adcx140_dapm_widgets),
	.dapm_routes		= adcx140_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(adcx140_audio_map),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 0,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static struct snd_soc_dai_driver adcx140_dai_driver[] = {
	{
		.name = "tlv320adcx140-codec",
		.capture = {
			.stream_name	 = "Capture",
			.channels_min	 = 2,
			.channels_max	 = ADCX140_MAX_CHANNELS,
			.rates		 = ADCX140_RATES,
			.formats	 = ADCX140_FORMATS,
		},
		.ops = &adcx140_dai_ops,
		.symmetric_rates = 1,
	}
};

#ifdef CONFIG_OF
static const struct of_device_id tlv320adcx140_of_match[] = {
	{ .compatible = "ti,tlv320adc3140" },
	{ .compatible = "ti,tlv320adc5140" },
	{ .compatible = "ti,tlv320adc6140" },
	{},
};
MODULE_DEVICE_TABLE(of, tlv320adcx140_of_match);
#endif

static int adcx140_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct adcx140_priv *adcx140;
	int ret;

	adcx140 = devm_kzalloc(&i2c->dev, sizeof(*adcx140), GFP_KERNEL);
	if (!adcx140)
		return -ENOMEM;

	adcx140->dev = &i2c->dev;

	adcx140->gpio_reset = devm_gpiod_get_optional(adcx140->dev,
						      "reset", GPIOD_OUT_LOW);
	if (IS_ERR(adcx140->gpio_reset))
		dev_info(&i2c->dev, "Reset GPIO not defined\n");

	adcx140->supply_areg = devm_regulator_get_optional(adcx140->dev,
							   "areg");
	if (IS_ERR(adcx140->supply_areg)) {
		if (PTR_ERR(adcx140->supply_areg) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		adcx140->supply_areg = NULL;
	} else {
		ret = regulator_enable(adcx140->supply_areg);
		if (ret) {
			dev_err(adcx140->dev, "Failed to enable areg\n");
			return ret;
		}
	}

	adcx140->regmap = devm_regmap_init_i2c(i2c, &adcx140_i2c_regmap);
	if (IS_ERR(adcx140->regmap)) {
		ret = PTR_ERR(adcx140->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	i2c_set_clientdata(i2c, adcx140);

	return devm_snd_soc_register_component(&i2c->dev,
					       &soc_codec_driver_adcx140,
					       adcx140_dai_driver, 1);
}

static const struct i2c_device_id adcx140_i2c_id[] = {
	{ "tlv320adc3140", 0 },
	{ "tlv320adc5140", 1 },
	{ "tlv320adc6140", 2 },
	{}
};
MODULE_DEVICE_TABLE(i2c, adcx140_i2c_id);

static struct i2c_driver adcx140_i2c_driver = {
	.driver = {
		.name	= "tlv320adcx140-codec",
		.of_match_table = of_match_ptr(tlv320adcx140_of_match),
	},
	.probe		= adcx140_i2c_probe,
	.id_table	= adcx140_i2c_id,
};
module_i2c_driver(adcx140_i2c_driver);

MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_DESCRIPTION("ASoC TLV320ADCX140 CODEC Driver");
MODULE_LICENSE("GPL v2");
