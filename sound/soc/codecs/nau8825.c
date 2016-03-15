/*
 * Nuvoton NAU8825 audio codec driver
 *
 * Copyright 2015 Google Chromium project.
 *  Author: Anatol Pomozov <anatol@chromium.org>
 * Copyright 2015 Nuvoton Technology Corp.
 *  Co-author: Meng-Huang Kuo <mhkuo@nuvoton.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/acpi.h>
#include <linux/math64.h>

#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>


#include "nau8825.h"

#define NAU_FREF_MAX 13500000
#define NAU_FVCO_MAX 124000000
#define NAU_FVCO_MIN 90000000

struct nau8825_fll {
	int mclk_src;
	int ratio;
	int fll_frac;
	int fll_int;
	int clk_ref_div;
};

struct nau8825_fll_attr {
	unsigned int param;
	unsigned int val;
};

/* scaling for mclk from sysclk_src output */
static const struct nau8825_fll_attr mclk_src_scaling[] = {
	{ 1, 0x0 },
	{ 2, 0x2 },
	{ 4, 0x3 },
	{ 8, 0x4 },
	{ 16, 0x5 },
	{ 32, 0x6 },
	{ 3, 0x7 },
	{ 6, 0xa },
	{ 12, 0xb },
	{ 24, 0xc },
	{ 48, 0xd },
	{ 96, 0xe },
	{ 5, 0xf },
};

/* ratio for input clk freq */
static const struct nau8825_fll_attr fll_ratio[] = {
	{ 512000, 0x01 },
	{ 256000, 0x02 },
	{ 128000, 0x04 },
	{ 64000, 0x08 },
	{ 32000, 0x10 },
	{ 8000, 0x20 },
	{ 4000, 0x40 },
};

static const struct nau8825_fll_attr fll_pre_scalar[] = {
	{ 1, 0x0 },
	{ 2, 0x1 },
	{ 4, 0x2 },
	{ 8, 0x3 },
};

static const struct reg_default nau8825_reg_defaults[] = {
	{ NAU8825_REG_ENA_CTRL, 0x00ff },
	{ NAU8825_REG_IIC_ADDR_SET, 0x0 },
	{ NAU8825_REG_CLK_DIVIDER, 0x0050 },
	{ NAU8825_REG_FLL1, 0x0 },
	{ NAU8825_REG_FLL2, 0x3126 },
	{ NAU8825_REG_FLL3, 0x0008 },
	{ NAU8825_REG_FLL4, 0x0010 },
	{ NAU8825_REG_FLL5, 0x0 },
	{ NAU8825_REG_FLL6, 0x6000 },
	{ NAU8825_REG_FLL_VCO_RSV, 0xf13c },
	{ NAU8825_REG_HSD_CTRL, 0x000c },
	{ NAU8825_REG_JACK_DET_CTRL, 0x0 },
	{ NAU8825_REG_INTERRUPT_MASK, 0x0 },
	{ NAU8825_REG_INTERRUPT_DIS_CTRL, 0xffff },
	{ NAU8825_REG_SAR_CTRL, 0x0015 },
	{ NAU8825_REG_KEYDET_CTRL, 0x0110 },
	{ NAU8825_REG_VDET_THRESHOLD_1, 0x0 },
	{ NAU8825_REG_VDET_THRESHOLD_2, 0x0 },
	{ NAU8825_REG_VDET_THRESHOLD_3, 0x0 },
	{ NAU8825_REG_VDET_THRESHOLD_4, 0x0 },
	{ NAU8825_REG_GPIO34_CTRL, 0x0 },
	{ NAU8825_REG_GPIO12_CTRL, 0x0 },
	{ NAU8825_REG_TDM_CTRL, 0x0 },
	{ NAU8825_REG_I2S_PCM_CTRL1, 0x000b },
	{ NAU8825_REG_I2S_PCM_CTRL2, 0x8010 },
	{ NAU8825_REG_LEFT_TIME_SLOT, 0x0 },
	{ NAU8825_REG_RIGHT_TIME_SLOT, 0x0 },
	{ NAU8825_REG_BIQ_CTRL, 0x0 },
	{ NAU8825_REG_BIQ_COF1, 0x0 },
	{ NAU8825_REG_BIQ_COF2, 0x0 },
	{ NAU8825_REG_BIQ_COF3, 0x0 },
	{ NAU8825_REG_BIQ_COF4, 0x0 },
	{ NAU8825_REG_BIQ_COF5, 0x0 },
	{ NAU8825_REG_BIQ_COF6, 0x0 },
	{ NAU8825_REG_BIQ_COF7, 0x0 },
	{ NAU8825_REG_BIQ_COF8, 0x0 },
	{ NAU8825_REG_BIQ_COF9, 0x0 },
	{ NAU8825_REG_BIQ_COF10, 0x0 },
	{ NAU8825_REG_ADC_RATE, 0x0010 },
	{ NAU8825_REG_DAC_CTRL1, 0x0001 },
	{ NAU8825_REG_DAC_CTRL2, 0x0 },
	{ NAU8825_REG_DAC_DGAIN_CTRL, 0x0 },
	{ NAU8825_REG_ADC_DGAIN_CTRL, 0x00cf },
	{ NAU8825_REG_MUTE_CTRL, 0x0 },
	{ NAU8825_REG_HSVOL_CTRL, 0x0 },
	{ NAU8825_REG_DACL_CTRL, 0x02cf },
	{ NAU8825_REG_DACR_CTRL, 0x00cf },
	{ NAU8825_REG_ADC_DRC_KNEE_IP12, 0x1486 },
	{ NAU8825_REG_ADC_DRC_KNEE_IP34, 0x0f12 },
	{ NAU8825_REG_ADC_DRC_SLOPES, 0x25ff },
	{ NAU8825_REG_ADC_DRC_ATKDCY, 0x3457 },
	{ NAU8825_REG_DAC_DRC_KNEE_IP12, 0x1486 },
	{ NAU8825_REG_DAC_DRC_KNEE_IP34, 0x0f12 },
	{ NAU8825_REG_DAC_DRC_SLOPES, 0x25f9 },
	{ NAU8825_REG_DAC_DRC_ATKDCY, 0x3457 },
	{ NAU8825_REG_IMM_MODE_CTRL, 0x0 },
	{ NAU8825_REG_CLASSG_CTRL, 0x0 },
	{ NAU8825_REG_OPT_EFUSE_CTRL, 0x0 },
	{ NAU8825_REG_MISC_CTRL, 0x0 },
	{ NAU8825_REG_BIAS_ADJ, 0x0 },
	{ NAU8825_REG_TRIM_SETTINGS, 0x0 },
	{ NAU8825_REG_ANALOG_CONTROL_1, 0x0 },
	{ NAU8825_REG_ANALOG_CONTROL_2, 0x0 },
	{ NAU8825_REG_ANALOG_ADC_1, 0x0011 },
	{ NAU8825_REG_ANALOG_ADC_2, 0x0020 },
	{ NAU8825_REG_RDAC, 0x0008 },
	{ NAU8825_REG_MIC_BIAS, 0x0006 },
	{ NAU8825_REG_BOOST, 0x0 },
	{ NAU8825_REG_FEPGA, 0x0 },
	{ NAU8825_REG_POWER_UP_CONTROL, 0x0 },
	{ NAU8825_REG_CHARGE_PUMP, 0x0 },
};

static bool nau8825_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NAU8825_REG_ENA_CTRL ... NAU8825_REG_FLL_VCO_RSV:
	case NAU8825_REG_HSD_CTRL ... NAU8825_REG_JACK_DET_CTRL:
	case NAU8825_REG_INTERRUPT_MASK ... NAU8825_REG_KEYDET_CTRL:
	case NAU8825_REG_VDET_THRESHOLD_1 ... NAU8825_REG_DACR_CTRL:
	case NAU8825_REG_ADC_DRC_KNEE_IP12 ... NAU8825_REG_ADC_DRC_ATKDCY:
	case NAU8825_REG_DAC_DRC_KNEE_IP12 ... NAU8825_REG_DAC_DRC_ATKDCY:
	case NAU8825_REG_IMM_MODE_CTRL ... NAU8825_REG_IMM_RMS_R:
	case NAU8825_REG_CLASSG_CTRL ... NAU8825_REG_OPT_EFUSE_CTRL:
	case NAU8825_REG_MISC_CTRL:
	case NAU8825_REG_I2C_DEVICE_ID ... NAU8825_REG_SARDOUT_RAM_STATUS:
	case NAU8825_REG_BIAS_ADJ:
	case NAU8825_REG_TRIM_SETTINGS ... NAU8825_REG_ANALOG_CONTROL_2:
	case NAU8825_REG_ANALOG_ADC_1 ... NAU8825_REG_MIC_BIAS:
	case NAU8825_REG_BOOST ... NAU8825_REG_FEPGA:
	case NAU8825_REG_POWER_UP_CONTROL ... NAU8825_REG_GENERAL_STATUS:
		return true;
	default:
		return false;
	}

}

static bool nau8825_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NAU8825_REG_RESET ... NAU8825_REG_FLL_VCO_RSV:
	case NAU8825_REG_HSD_CTRL ... NAU8825_REG_JACK_DET_CTRL:
	case NAU8825_REG_INTERRUPT_MASK:
	case NAU8825_REG_INT_CLR_KEY_STATUS ... NAU8825_REG_KEYDET_CTRL:
	case NAU8825_REG_VDET_THRESHOLD_1 ... NAU8825_REG_DACR_CTRL:
	case NAU8825_REG_ADC_DRC_KNEE_IP12 ... NAU8825_REG_ADC_DRC_ATKDCY:
	case NAU8825_REG_DAC_DRC_KNEE_IP12 ... NAU8825_REG_DAC_DRC_ATKDCY:
	case NAU8825_REG_IMM_MODE_CTRL:
	case NAU8825_REG_CLASSG_CTRL ... NAU8825_REG_OPT_EFUSE_CTRL:
	case NAU8825_REG_MISC_CTRL:
	case NAU8825_REG_BIAS_ADJ:
	case NAU8825_REG_TRIM_SETTINGS ... NAU8825_REG_ANALOG_CONTROL_2:
	case NAU8825_REG_ANALOG_ADC_1 ... NAU8825_REG_MIC_BIAS:
	case NAU8825_REG_BOOST ... NAU8825_REG_FEPGA:
	case NAU8825_REG_POWER_UP_CONTROL ... NAU8825_REG_CHARGE_PUMP:
		return true;
	default:
		return false;
	}
}

static bool nau8825_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NAU8825_REG_RESET:
	case NAU8825_REG_IRQ_STATUS:
	case NAU8825_REG_INT_CLR_KEY_STATUS:
	case NAU8825_REG_IMM_RMS_L:
	case NAU8825_REG_IMM_RMS_R:
	case NAU8825_REG_I2C_DEVICE_ID:
	case NAU8825_REG_SARDOUT_RAM_STATUS:
	case NAU8825_REG_CHARGE_PUMP_INPUT_READ:
	case NAU8825_REG_GENERAL_STATUS:
		return true;
	default:
		return false;
	}
}

static int nau8825_pump_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* Prevent startup click by letting charge pump to ramp up */
		msleep(10);
		regmap_update_bits(nau8825->regmap, NAU8825_REG_CHARGE_PUMP,
			NAU8825_JAMNODCLOW, NAU8825_JAMNODCLOW);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_update_bits(nau8825->regmap, NAU8825_REG_CHARGE_PUMP,
			NAU8825_JAMNODCLOW, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int nau8825_output_dac_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Disables the TESTDAC to let DAC signal pass through. */
		regmap_update_bits(nau8825->regmap, NAU8825_REG_BIAS_ADJ,
			NAU8825_BIAS_TESTDAC_EN, 0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(nau8825->regmap, NAU8825_REG_BIAS_ADJ,
			NAU8825_BIAS_TESTDAC_EN, NAU8825_BIAS_TESTDAC_EN);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const char * const nau8825_adc_decimation[] = {
	"32", "64", "128", "256"
};

static const struct soc_enum nau8825_adc_decimation_enum =
	SOC_ENUM_SINGLE(NAU8825_REG_ADC_RATE, NAU8825_ADC_SYNC_DOWN_SFT,
		ARRAY_SIZE(nau8825_adc_decimation), nau8825_adc_decimation);

static const char * const nau8825_dac_oversampl[] = {
	"64", "256", "128", "", "32"
};

static const struct soc_enum nau8825_dac_oversampl_enum =
	SOC_ENUM_SINGLE(NAU8825_REG_DAC_CTRL1, NAU8825_DAC_OVERSAMPLE_SFT,
		ARRAY_SIZE(nau8825_dac_oversampl), nau8825_dac_oversampl);

static const DECLARE_TLV_DB_MINMAX_MUTE(adc_vol_tlv, -10300, 2400);
static const DECLARE_TLV_DB_MINMAX_MUTE(sidetone_vol_tlv, -4200, 0);
static const DECLARE_TLV_DB_MINMAX(dac_vol_tlv, -5400, 0);
static const DECLARE_TLV_DB_MINMAX(fepga_gain_tlv, -100, 3600);
static const DECLARE_TLV_DB_MINMAX_MUTE(crosstalk_vol_tlv, -9600, 2400);

static const struct snd_kcontrol_new nau8825_controls[] = {
	SOC_SINGLE_TLV("Mic Volume", NAU8825_REG_ADC_DGAIN_CTRL,
		0, 0xff, 0, adc_vol_tlv),
	SOC_DOUBLE_TLV("Headphone Bypass Volume", NAU8825_REG_ADC_DGAIN_CTRL,
		12, 8, 0x0f, 0, sidetone_vol_tlv),
	SOC_DOUBLE_TLV("Headphone Volume", NAU8825_REG_HSVOL_CTRL,
		6, 0, 0x3f, 1, dac_vol_tlv),
	SOC_SINGLE_TLV("Frontend PGA Volume", NAU8825_REG_POWER_UP_CONTROL,
		8, 37, 0, fepga_gain_tlv),
	SOC_DOUBLE_TLV("Headphone Crosstalk Volume", NAU8825_REG_DAC_DGAIN_CTRL,
		0, 8, 0xff, 0, crosstalk_vol_tlv),

	SOC_ENUM("ADC Decimation Rate", nau8825_adc_decimation_enum),
	SOC_ENUM("DAC Oversampling Rate", nau8825_dac_oversampl_enum),
};

/* DAC Mux 0x33[9] and 0x34[9] */
static const char * const nau8825_dac_src[] = {
	"DACL", "DACR",
};

static SOC_ENUM_SINGLE_DECL(
	nau8825_dacl_enum, NAU8825_REG_DACL_CTRL,
	NAU8825_DACL_CH_SEL_SFT, nau8825_dac_src);

static SOC_ENUM_SINGLE_DECL(
	nau8825_dacr_enum, NAU8825_REG_DACR_CTRL,
	NAU8825_DACR_CH_SEL_SFT, nau8825_dac_src);

static const struct snd_kcontrol_new nau8825_dacl_mux =
	SOC_DAPM_ENUM("DACL Source", nau8825_dacl_enum);

static const struct snd_kcontrol_new nau8825_dacr_mux =
	SOC_DAPM_ENUM("DACR Source", nau8825_dacr_enum);


static const struct snd_soc_dapm_widget nau8825_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_OUT("AIFTX", "Capture", 0, NAU8825_REG_I2S_PCM_CTRL2,
		15, 1),

	SND_SOC_DAPM_INPUT("MIC"),
	SND_SOC_DAPM_MICBIAS("MICBIAS", NAU8825_REG_MIC_BIAS, 8, 0),

	SND_SOC_DAPM_PGA("Frontend PGA", NAU8825_REG_POWER_UP_CONTROL, 14, 0,
		NULL, 0),

	SND_SOC_DAPM_ADC("ADC", NULL, NAU8825_REG_ENA_CTRL, 8, 0),
	SND_SOC_DAPM_SUPPLY("ADC Clock", NAU8825_REG_ENA_CTRL, 7, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC Power", NAU8825_REG_ANALOG_ADC_2, 6, 0, NULL,
		0),

	/* ADC for button press detection. A dapm supply widget is used to
	 * prevent dapm_power_widgets keeping the codec at SND_SOC_BIAS_ON
	 * during suspend.
	 */
	SND_SOC_DAPM_SUPPLY("SAR", NAU8825_REG_SAR_CTRL,
		NAU8825_SAR_ADC_EN_SFT, 0, NULL, 0),

	SND_SOC_DAPM_PGA_S("ADACL", 2, NAU8825_REG_RDAC, 12, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("ADACR", 2, NAU8825_REG_RDAC, 13, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("ADACL Clock", 3, NAU8825_REG_RDAC, 8, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("ADACR Clock", 3, NAU8825_REG_RDAC, 9, 0, NULL, 0),

	SND_SOC_DAPM_DAC("DDACR", NULL, NAU8825_REG_ENA_CTRL,
		NAU8825_ENABLE_DACR_SFT, 0),
	SND_SOC_DAPM_DAC("DDACL", NULL, NAU8825_REG_ENA_CTRL,
		NAU8825_ENABLE_DACL_SFT, 0),
	SND_SOC_DAPM_SUPPLY("DDAC Clock", NAU8825_REG_ENA_CTRL, 6, 0, NULL, 0),

	SND_SOC_DAPM_MUX("DACL Mux", SND_SOC_NOPM, 0, 0, &nau8825_dacl_mux),
	SND_SOC_DAPM_MUX("DACR Mux", SND_SOC_NOPM, 0, 0, &nau8825_dacr_mux),

	SND_SOC_DAPM_PGA_S("HP amp L", 0,
		NAU8825_REG_CLASSG_CTRL, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("HP amp R", 0,
		NAU8825_REG_CLASSG_CTRL, 2, 0, NULL, 0),

	SND_SOC_DAPM_PGA_S("Charge Pump", 1, NAU8825_REG_CHARGE_PUMP, 5, 0,
		nau8825_pump_event, SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_PGA_S("Output Driver R Stage 1", 4,
		NAU8825_REG_POWER_UP_CONTROL, 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output Driver L Stage 1", 4,
		NAU8825_REG_POWER_UP_CONTROL, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output Driver R Stage 2", 5,
		NAU8825_REG_POWER_UP_CONTROL, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output Driver L Stage 2", 5,
		NAU8825_REG_POWER_UP_CONTROL, 2, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output Driver R Stage 3", 6,
		NAU8825_REG_POWER_UP_CONTROL, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Output Driver L Stage 3", 6,
		NAU8825_REG_POWER_UP_CONTROL, 0, 0, NULL, 0),

	SND_SOC_DAPM_PGA_S("Output DACL", 7,
		NAU8825_REG_CHARGE_PUMP, 8, 1, nau8825_output_dac_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_S("Output DACR", 7,
		NAU8825_REG_CHARGE_PUMP, 9, 1, nau8825_output_dac_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* HPOL/R are ungrounded by disabling 16 Ohm pull-downs on playback */
	SND_SOC_DAPM_PGA_S("HPOL Pulldown", 8,
		NAU8825_REG_HSD_CTRL, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA_S("HPOR Pulldown", 8,
		NAU8825_REG_HSD_CTRL, 1, 1, NULL, 0),

	/* High current HPOL/R boost driver */
	SND_SOC_DAPM_PGA_S("HP Boost Driver", 9,
		NAU8825_REG_BOOST, 9, 1, NULL, 0),

	/* Class G operation control*/
	SND_SOC_DAPM_PGA_S("Class G", 10,
		NAU8825_REG_CLASSG_CTRL, 0, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
};

static const struct snd_soc_dapm_route nau8825_dapm_routes[] = {
	{"Frontend PGA", NULL, "MIC"},
	{"ADC", NULL, "Frontend PGA"},
	{"ADC", NULL, "ADC Clock"},
	{"ADC", NULL, "ADC Power"},
	{"AIFTX", NULL, "ADC"},

	{"DDACL", NULL, "Playback"},
	{"DDACR", NULL, "Playback"},
	{"DDACL", NULL, "DDAC Clock"},
	{"DDACR", NULL, "DDAC Clock"},
	{"DACL Mux", "DACL", "DDACL"},
	{"DACL Mux", "DACR", "DDACR"},
	{"DACR Mux", "DACL", "DDACL"},
	{"DACR Mux", "DACR", "DDACR"},
	{"HP amp L", NULL, "DACL Mux"},
	{"HP amp R", NULL, "DACR Mux"},
	{"Charge Pump", NULL, "HP amp L"},
	{"Charge Pump", NULL, "HP amp R"},
	{"ADACL", NULL, "Charge Pump"},
	{"ADACR", NULL, "Charge Pump"},
	{"ADACL Clock", NULL, "ADACL"},
	{"ADACR Clock", NULL, "ADACR"},
	{"Output Driver L Stage 1", NULL, "ADACL Clock"},
	{"Output Driver R Stage 1", NULL, "ADACR Clock"},
	{"Output Driver L Stage 2", NULL, "Output Driver L Stage 1"},
	{"Output Driver R Stage 2", NULL, "Output Driver R Stage 1"},
	{"Output Driver L Stage 3", NULL, "Output Driver L Stage 2"},
	{"Output Driver R Stage 3", NULL, "Output Driver R Stage 2"},
	{"Output DACL", NULL, "Output Driver L Stage 3"},
	{"Output DACR", NULL, "Output Driver R Stage 3"},
	{"HPOL Pulldown", NULL, "Output DACL"},
	{"HPOR Pulldown", NULL, "Output DACR"},
	{"HP Boost Driver", NULL, "HPOL Pulldown"},
	{"HP Boost Driver", NULL, "HPOR Pulldown"},
	{"Class G", NULL, "HP Boost Driver"},
	{"HPOL", NULL, "Class G"},
	{"HPOR", NULL, "Class G"},
};

static int nau8825_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);
	unsigned int val_len = 0;

	switch (params_width(params)) {
	case 16:
		val_len |= NAU8825_I2S_DL_16;
		break;
	case 20:
		val_len |= NAU8825_I2S_DL_20;
		break;
	case 24:
		val_len |= NAU8825_I2S_DL_24;
		break;
	case 32:
		val_len |= NAU8825_I2S_DL_32;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(nau8825->regmap, NAU8825_REG_I2S_PCM_CTRL1,
		NAU8825_I2S_DL_MASK, val_len);

	return 0;
}

static int nau8825_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);
	unsigned int ctrl1_val = 0, ctrl2_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		ctrl2_val |= NAU8825_I2S_MS_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		ctrl1_val |= NAU8825_I2S_BP_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		ctrl1_val |= NAU8825_I2S_DF_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ctrl1_val |= NAU8825_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		ctrl1_val |= NAU8825_I2S_DF_RIGTH;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		ctrl1_val |= NAU8825_I2S_DF_PCM_AB;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		ctrl1_val |= NAU8825_I2S_DF_PCM_AB;
		ctrl1_val |= NAU8825_I2S_PCMB_EN;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(nau8825->regmap, NAU8825_REG_I2S_PCM_CTRL1,
		NAU8825_I2S_DL_MASK | NAU8825_I2S_DF_MASK |
		NAU8825_I2S_BP_MASK | NAU8825_I2S_PCMB_MASK,
		ctrl1_val);
	regmap_update_bits(nau8825->regmap, NAU8825_REG_I2S_PCM_CTRL2,
		NAU8825_I2S_MS_MASK, ctrl2_val);

	return 0;
}

static const struct snd_soc_dai_ops nau8825_dai_ops = {
	.hw_params	= nau8825_hw_params,
	.set_fmt	= nau8825_set_dai_fmt,
};

#define NAU8825_RATES	SNDRV_PCM_RATE_8000_192000
#define NAU8825_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE \
			 | SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver nau8825_dai = {
	.name = "nau8825-hifi",
	.playback = {
		.stream_name	 = "Playback",
		.channels_min	 = 1,
		.channels_max	 = 2,
		.rates		 = NAU8825_RATES,
		.formats	 = NAU8825_FORMATS,
	},
	.capture = {
		.stream_name	 = "Capture",
		.channels_min	 = 1,
		.channels_max	 = 1,
		.rates		 = NAU8825_RATES,
		.formats	 = NAU8825_FORMATS,
	},
	.ops = &nau8825_dai_ops,
};

/**
 * nau8825_enable_jack_detect - Specify a jack for event reporting
 *
 * @component:  component to register the jack with
 * @jack: jack to use to report headset and button events on
 *
 * After this function has been called the headset insert/remove and button
 * events will be routed to the given jack.  Jack can be null to stop
 * reporting.
 */
int nau8825_enable_jack_detect(struct snd_soc_codec *codec,
				struct snd_soc_jack *jack)
{
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);
	struct regmap *regmap = nau8825->regmap;

	nau8825->jack = jack;

	/* Ground HP Outputs[1:0], needed for headset auto detection
	 * Enable Automatic Mic/Gnd switching reading on insert interrupt[6]
	 */
	regmap_update_bits(regmap, NAU8825_REG_HSD_CTRL,
		NAU8825_HSD_AUTO_MODE | NAU8825_SPKR_DWN1R | NAU8825_SPKR_DWN1L,
		NAU8825_HSD_AUTO_MODE | NAU8825_SPKR_DWN1R | NAU8825_SPKR_DWN1L);

	regmap_update_bits(regmap, NAU8825_REG_INTERRUPT_MASK,
		NAU8825_IRQ_HEADSET_COMPLETE_EN | NAU8825_IRQ_EJECT_EN, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(nau8825_enable_jack_detect);


static bool nau8825_is_jack_inserted(struct regmap *regmap)
{
	int status;

	regmap_read(regmap, NAU8825_REG_I2C_DEVICE_ID, &status);
	return !(status & NAU8825_GPIO2JD1);
}

static void nau8825_restart_jack_detection(struct regmap *regmap)
{
	/* Chip needs one FSCLK cycle in order to generate interrupts,
	 * as we cannot guarantee one will be provided by the system. Turning
	 * master mode on then off enables us to generate that FSCLK cycle
	 * with a minimum of contention on the clock bus.
	 */
	regmap_update_bits(regmap, NAU8825_REG_I2S_PCM_CTRL2,
		NAU8825_I2S_MS_MASK, NAU8825_I2S_MS_MASTER);
	regmap_update_bits(regmap, NAU8825_REG_I2S_PCM_CTRL2,
		NAU8825_I2S_MS_MASK, NAU8825_I2S_MS_SLAVE);

	/* this will restart the entire jack detection process including MIC/GND
	 * switching and create interrupts. We have to go from 0 to 1 and back
	 * to 0 to restart.
	 */
	regmap_update_bits(regmap, NAU8825_REG_JACK_DET_CTRL,
		NAU8825_JACK_DET_RESTART, NAU8825_JACK_DET_RESTART);
	regmap_update_bits(regmap, NAU8825_REG_JACK_DET_CTRL,
		NAU8825_JACK_DET_RESTART, 0);
}

static void nau8825_eject_jack(struct nau8825 *nau8825)
{
	struct snd_soc_dapm_context *dapm = nau8825->dapm;
	struct regmap *regmap = nau8825->regmap;

	snd_soc_dapm_disable_pin(dapm, "SAR");
	snd_soc_dapm_disable_pin(dapm, "MICBIAS");
	/* Detach 2kOhm Resistors from MICBIAS to MICGND1/2 */
	regmap_update_bits(regmap, NAU8825_REG_MIC_BIAS,
		NAU8825_MICBIAS_JKSLV | NAU8825_MICBIAS_JKR2, 0);
	/* ground HPL/HPR, MICGRND1/2 */
	regmap_update_bits(regmap, NAU8825_REG_HSD_CTRL, 0xf, 0xf);

	snd_soc_dapm_sync(dapm);
}

static int nau8825_button_decode(int value)
{
	int buttons = 0;

	/* The chip supports up to 8 buttons, but ALSA defines only 6 buttons */
	if (value & BIT(0))
		buttons |= SND_JACK_BTN_0;
	if (value & BIT(1))
		buttons |= SND_JACK_BTN_1;
	if (value & BIT(2))
		buttons |= SND_JACK_BTN_2;
	if (value & BIT(3))
		buttons |= SND_JACK_BTN_3;
	if (value & BIT(4))
		buttons |= SND_JACK_BTN_4;
	if (value & BIT(5))
		buttons |= SND_JACK_BTN_5;

	return buttons;
}

static int nau8825_jack_insert(struct nau8825 *nau8825)
{
	struct regmap *regmap = nau8825->regmap;
	struct snd_soc_dapm_context *dapm = nau8825->dapm;
	int jack_status_reg, mic_detected;
	int type = 0;

	regmap_read(regmap, NAU8825_REG_GENERAL_STATUS, &jack_status_reg);
	mic_detected = (jack_status_reg >> 10) & 3;

	switch (mic_detected) {
	case 0:
		/* no mic */
		type = SND_JACK_HEADPHONE;
		break;
	case 1:
		dev_dbg(nau8825->dev, "OMTP (micgnd1) mic connected\n");
		type = SND_JACK_HEADSET;

		/* Unground MICGND1 */
		regmap_update_bits(regmap, NAU8825_REG_HSD_CTRL, 3 << 2,
			1 << 2);
		/* Attach 2kOhm Resistor from MICBIAS to MICGND1 */
		regmap_update_bits(regmap, NAU8825_REG_MIC_BIAS,
			NAU8825_MICBIAS_JKSLV | NAU8825_MICBIAS_JKR2,
			NAU8825_MICBIAS_JKR2);
		/* Attach SARADC to MICGND1 */
		regmap_update_bits(regmap, NAU8825_REG_SAR_CTRL,
			NAU8825_SAR_INPUT_MASK,
			NAU8825_SAR_INPUT_JKR2);

		snd_soc_dapm_force_enable_pin(dapm, "MICBIAS");
		snd_soc_dapm_force_enable_pin(dapm, "SAR");
		snd_soc_dapm_sync(dapm);
		break;
	case 2:
	case 3:
		dev_dbg(nau8825->dev, "CTIA (micgnd2) mic connected\n");
		type = SND_JACK_HEADSET;

		/* Unground MICGND2 */
		regmap_update_bits(regmap, NAU8825_REG_HSD_CTRL, 3 << 2,
			2 << 2);
		/* Attach 2kOhm Resistor from MICBIAS to MICGND2 */
		regmap_update_bits(regmap, NAU8825_REG_MIC_BIAS,
			NAU8825_MICBIAS_JKSLV | NAU8825_MICBIAS_JKR2,
			NAU8825_MICBIAS_JKSLV);
		/* Attach SARADC to MICGND2 */
		regmap_update_bits(regmap, NAU8825_REG_SAR_CTRL,
			NAU8825_SAR_INPUT_MASK,
			NAU8825_SAR_INPUT_JKSLV);

		snd_soc_dapm_force_enable_pin(dapm, "MICBIAS");
		snd_soc_dapm_force_enable_pin(dapm, "SAR");
		snd_soc_dapm_sync(dapm);
		break;
	}

	/* Leaving HPOL/R grounded after jack insert by default. They will be
	 * ungrounded as part of the widget power up sequence at the beginning
	 * of playback to reduce pop.
	 */
	return type;
}

#define NAU8825_BUTTONS (SND_JACK_BTN_0 | SND_JACK_BTN_1 | \
		SND_JACK_BTN_2 | SND_JACK_BTN_3)

static irqreturn_t nau8825_interrupt(int irq, void *data)
{
	struct nau8825 *nau8825 = (struct nau8825 *)data;
	struct regmap *regmap = nau8825->regmap;
	int active_irq, clear_irq = 0, event = 0, event_mask = 0;

	if (regmap_read(regmap, NAU8825_REG_IRQ_STATUS, &active_irq)) {
		dev_err(nau8825->dev, "failed to read irq status\n");
		return IRQ_NONE;
	}

	if ((active_irq & NAU8825_JACK_EJECTION_IRQ_MASK) ==
		NAU8825_JACK_EJECTION_DETECTED) {

		nau8825_eject_jack(nau8825);
		event_mask |= SND_JACK_HEADSET;
		clear_irq = NAU8825_JACK_EJECTION_IRQ_MASK;
	} else if (active_irq & NAU8825_KEY_SHORT_PRESS_IRQ) {
		int key_status;

		regmap_read(regmap, NAU8825_REG_INT_CLR_KEY_STATUS,
			&key_status);

		/* upper 8 bits of the register are for short pressed keys,
		 * lower 8 bits - for long pressed buttons
		 */
		nau8825->button_pressed = nau8825_button_decode(
			key_status >> 8);

		event |= nau8825->button_pressed;
		event_mask |= NAU8825_BUTTONS;
		clear_irq = NAU8825_KEY_SHORT_PRESS_IRQ;
	} else if (active_irq & NAU8825_KEY_RELEASE_IRQ) {
		event_mask = NAU8825_BUTTONS;
		clear_irq = NAU8825_KEY_RELEASE_IRQ;
	} else if (active_irq & NAU8825_HEADSET_COMPLETION_IRQ) {
		if (nau8825_is_jack_inserted(regmap)) {
			event |= nau8825_jack_insert(nau8825);
		} else {
			dev_warn(nau8825->dev, "Headset completion IRQ fired but no headset connected\n");
			nau8825_eject_jack(nau8825);
		}

		event_mask |= SND_JACK_HEADSET;
		clear_irq = NAU8825_HEADSET_COMPLETION_IRQ;
	}

	if (!clear_irq)
		clear_irq = active_irq;
	/* clears the rightmost interruption */
	regmap_write(regmap, NAU8825_REG_INT_CLR_KEY_STATUS, clear_irq);

	if (event_mask)
		snd_soc_jack_report(nau8825->jack, event, event_mask);

	return IRQ_HANDLED;
}

static void nau8825_setup_buttons(struct nau8825 *nau8825)
{
	struct regmap *regmap = nau8825->regmap;

	regmap_update_bits(regmap, NAU8825_REG_SAR_CTRL,
		NAU8825_SAR_TRACKING_GAIN_MASK,
		nau8825->sar_voltage << NAU8825_SAR_TRACKING_GAIN_SFT);
	regmap_update_bits(regmap, NAU8825_REG_SAR_CTRL,
		NAU8825_SAR_COMPARE_TIME_MASK,
		nau8825->sar_compare_time << NAU8825_SAR_COMPARE_TIME_SFT);
	regmap_update_bits(regmap, NAU8825_REG_SAR_CTRL,
		NAU8825_SAR_SAMPLING_TIME_MASK,
		nau8825->sar_sampling_time << NAU8825_SAR_SAMPLING_TIME_SFT);

	regmap_update_bits(regmap, NAU8825_REG_KEYDET_CTRL,
		NAU8825_KEYDET_LEVELS_NR_MASK,
		(nau8825->sar_threshold_num - 1) << NAU8825_KEYDET_LEVELS_NR_SFT);
	regmap_update_bits(regmap, NAU8825_REG_KEYDET_CTRL,
		NAU8825_KEYDET_HYSTERESIS_MASK,
		nau8825->sar_hysteresis << NAU8825_KEYDET_HYSTERESIS_SFT);
	regmap_update_bits(regmap, NAU8825_REG_KEYDET_CTRL,
		NAU8825_KEYDET_SHORTKEY_DEBOUNCE_MASK,
		nau8825->key_debounce << NAU8825_KEYDET_SHORTKEY_DEBOUNCE_SFT);

	regmap_write(regmap, NAU8825_REG_VDET_THRESHOLD_1,
		(nau8825->sar_threshold[0] << 8) | nau8825->sar_threshold[1]);
	regmap_write(regmap, NAU8825_REG_VDET_THRESHOLD_2,
		(nau8825->sar_threshold[2] << 8) | nau8825->sar_threshold[3]);
	regmap_write(regmap, NAU8825_REG_VDET_THRESHOLD_3,
		(nau8825->sar_threshold[4] << 8) | nau8825->sar_threshold[5]);
	regmap_write(regmap, NAU8825_REG_VDET_THRESHOLD_4,
		(nau8825->sar_threshold[6] << 8) | nau8825->sar_threshold[7]);

	/* Enable short press and release interruptions */
	regmap_update_bits(regmap, NAU8825_REG_INTERRUPT_MASK,
		NAU8825_IRQ_KEY_SHORT_PRESS_EN | NAU8825_IRQ_KEY_RELEASE_EN,
		0);
}

static void nau8825_init_regs(struct nau8825 *nau8825)
{
	struct regmap *regmap = nau8825->regmap;

	/* Latch IIC LSB value */
	regmap_write(regmap, NAU8825_REG_IIC_ADDR_SET, 0x0001);
	/* Enable Bias/Vmid */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_BIAS_ADJ,
		NAU8825_BIAS_VMID, NAU8825_BIAS_VMID);
	regmap_update_bits(nau8825->regmap, NAU8825_REG_BOOST,
		NAU8825_GLOBAL_BIAS_EN, NAU8825_GLOBAL_BIAS_EN);

	/* VMID Tieoff */
	regmap_update_bits(regmap, NAU8825_REG_BIAS_ADJ,
		NAU8825_BIAS_VMID_SEL_MASK,
		nau8825->vref_impedance << NAU8825_BIAS_VMID_SEL_SFT);
	/* Disable Boost Driver, Automatic Short circuit protection enable */
	regmap_update_bits(regmap, NAU8825_REG_BOOST,
		NAU8825_PRECHARGE_DIS | NAU8825_HP_BOOST_DIS |
		NAU8825_HP_BOOST_G_DIS | NAU8825_SHORT_SHUTDOWN_EN,
		NAU8825_PRECHARGE_DIS | NAU8825_HP_BOOST_DIS |
		NAU8825_HP_BOOST_G_DIS | NAU8825_SHORT_SHUTDOWN_EN);

	regmap_update_bits(regmap, NAU8825_REG_GPIO12_CTRL,
		NAU8825_JKDET_OUTPUT_EN,
		nau8825->jkdet_enable ? 0 : NAU8825_JKDET_OUTPUT_EN);
	regmap_update_bits(regmap, NAU8825_REG_GPIO12_CTRL,
		NAU8825_JKDET_PULL_EN,
		nau8825->jkdet_pull_enable ? 0 : NAU8825_JKDET_PULL_EN);
	regmap_update_bits(regmap, NAU8825_REG_GPIO12_CTRL,
		NAU8825_JKDET_PULL_UP,
		nau8825->jkdet_pull_up ? NAU8825_JKDET_PULL_UP : 0);
	regmap_update_bits(regmap, NAU8825_REG_JACK_DET_CTRL,
		NAU8825_JACK_POLARITY,
		/* jkdet_polarity - 1  is for active-low */
		nau8825->jkdet_polarity ? 0 : NAU8825_JACK_POLARITY);

	regmap_update_bits(regmap, NAU8825_REG_JACK_DET_CTRL,
		NAU8825_JACK_INSERT_DEBOUNCE_MASK,
		nau8825->jack_insert_debounce << NAU8825_JACK_INSERT_DEBOUNCE_SFT);
	regmap_update_bits(regmap, NAU8825_REG_JACK_DET_CTRL,
		NAU8825_JACK_EJECT_DEBOUNCE_MASK,
		nau8825->jack_eject_debounce << NAU8825_JACK_EJECT_DEBOUNCE_SFT);

	/* Mask unneeded IRQs: 1 - disable, 0 - enable */
	regmap_update_bits(regmap, NAU8825_REG_INTERRUPT_MASK, 0x7ff, 0x7ff);

	regmap_update_bits(regmap, NAU8825_REG_MIC_BIAS,
		NAU8825_MICBIAS_VOLTAGE_MASK, nau8825->micbias_voltage);

	if (nau8825->sar_threshold_num)
		nau8825_setup_buttons(nau8825);

	/* Default oversampling/decimations settings are unusable
	 * (audible hiss). Set it to something better.
	 */
	regmap_update_bits(regmap, NAU8825_REG_ADC_RATE,
		NAU8825_ADC_SYNC_DOWN_MASK, NAU8825_ADC_SYNC_DOWN_128);
	regmap_update_bits(regmap, NAU8825_REG_DAC_CTRL1,
		NAU8825_DAC_OVERSAMPLE_MASK, NAU8825_DAC_OVERSAMPLE_128);
	/* Disable DACR/L power */
	regmap_update_bits(regmap, NAU8825_REG_CHARGE_PUMP,
		NAU8825_POWER_DOWN_DACR | NAU8825_POWER_DOWN_DACL,
		NAU8825_POWER_DOWN_DACR | NAU8825_POWER_DOWN_DACL);
	/* Enable TESTDAC. This sets the analog DAC inputs to a '0' input
	 * signal to avoid any glitches due to power up transients in both
	 * the analog and digital DAC circuit.
	 */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_BIAS_ADJ,
		NAU8825_BIAS_TESTDAC_EN, NAU8825_BIAS_TESTDAC_EN);
	/* CICCLP off */
	regmap_update_bits(regmap, NAU8825_REG_DAC_CTRL1,
		NAU8825_DAC_CLIP_OFF, NAU8825_DAC_CLIP_OFF);

	/* Class AB bias current to 2x, DAC Capacitor enable MSB/LSB */
	regmap_update_bits(regmap, NAU8825_REG_ANALOG_CONTROL_2,
		NAU8825_HP_NON_CLASSG_CURRENT_2xADJ |
		NAU8825_DAC_CAPACITOR_MSB | NAU8825_DAC_CAPACITOR_LSB,
		NAU8825_HP_NON_CLASSG_CURRENT_2xADJ |
		NAU8825_DAC_CAPACITOR_MSB | NAU8825_DAC_CAPACITOR_LSB);
	/* Class G timer 64ms */
	regmap_update_bits(regmap, NAU8825_REG_CLASSG_CTRL,
		NAU8825_CLASSG_TIMER_MASK,
		0x20 << NAU8825_CLASSG_TIMER_SFT);
	/* DAC clock delay 2ns, VREF */
	regmap_update_bits(regmap, NAU8825_REG_RDAC,
		NAU8825_RDAC_CLK_DELAY_MASK | NAU8825_RDAC_VREF_MASK,
		(0x2 << NAU8825_RDAC_CLK_DELAY_SFT) |
		(0x3 << NAU8825_RDAC_VREF_SFT));
}

static const struct regmap_config nau8825_regmap_config = {
	.val_bits = 16,
	.reg_bits = 16,

	.max_register = NAU8825_REG_MAX,
	.readable_reg = nau8825_readable_reg,
	.writeable_reg = nau8825_writeable_reg,
	.volatile_reg = nau8825_volatile_reg,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = nau8825_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(nau8825_reg_defaults),
};

static int nau8825_codec_probe(struct snd_soc_codec *codec)
{
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);

	nau8825->dapm = dapm;

	/* The interrupt clock is gated by x1[10:8],
	 * one of them needs to be enabled all the time for
	 * interrupts to happen.
	 */
	snd_soc_dapm_force_enable_pin(dapm, "DDACR");
	snd_soc_dapm_sync(dapm);

	/* Unmask interruptions. Handler uses dapm object so we can enable
	 * interruptions only after dapm is fully initialized.
	 */
	regmap_write(nau8825->regmap, NAU8825_REG_INTERRUPT_DIS_CTRL, 0);
	nau8825_restart_jack_detection(nau8825->regmap);

	return 0;
}

/**
 * nau8825_calc_fll_param - Calculate FLL parameters.
 * @fll_in: external clock provided to codec.
 * @fs: sampling rate.
 * @fll_param: Pointer to structure of FLL parameters.
 *
 * Calculate FLL parameters to configure codec.
 *
 * Returns 0 for success or negative error code.
 */
static int nau8825_calc_fll_param(unsigned int fll_in, unsigned int fs,
		struct nau8825_fll *fll_param)
{
	u64 fvco, fvco_max;
	unsigned int fref, i, fvco_sel;

	/* Ensure the reference clock frequency (FREF) is <= 13.5MHz by dividing
	 * freq_in by 1, 2, 4, or 8 using FLL pre-scalar.
	 * FREF = freq_in / NAU8825_FLL_REF_DIV_MASK
	 */
	for (i = 0; i < ARRAY_SIZE(fll_pre_scalar); i++) {
		fref = fll_in / fll_pre_scalar[i].param;
		if (fref <= NAU_FREF_MAX)
			break;
	}
	if (i == ARRAY_SIZE(fll_pre_scalar))
		return -EINVAL;
	fll_param->clk_ref_div = fll_pre_scalar[i].val;

	/* Choose the FLL ratio based on FREF */
	for (i = 0; i < ARRAY_SIZE(fll_ratio); i++) {
		if (fref >= fll_ratio[i].param)
			break;
	}
	if (i == ARRAY_SIZE(fll_ratio))
		return -EINVAL;
	fll_param->ratio = fll_ratio[i].val;

	/* Calculate the frequency of DCO (FDCO) given freq_out = 256 * Fs.
	 * FDCO must be within the 90MHz - 124MHz or the FFL cannot be
	 * guaranteed across the full range of operation.
	 * FDCO = freq_out * 2 * mclk_src_scaling
	 */
	fvco_max = 0;
	fvco_sel = ARRAY_SIZE(mclk_src_scaling);
	for (i = 0; i < ARRAY_SIZE(mclk_src_scaling); i++) {
		fvco = 256 * fs * 2 * mclk_src_scaling[i].param;
		if (fvco > NAU_FVCO_MIN && fvco < NAU_FVCO_MAX &&
			fvco_max < fvco) {
			fvco_max = fvco;
			fvco_sel = i;
		}
	}
	if (ARRAY_SIZE(mclk_src_scaling) == fvco_sel)
		return -EINVAL;
	fll_param->mclk_src = mclk_src_scaling[fvco_sel].val;

	/* Calculate the FLL 10-bit integer input and the FLL 16-bit fractional
	 * input based on FDCO, FREF and FLL ratio.
	 */
	fvco = div_u64(fvco << 16, fref * fll_param->ratio);
	fll_param->fll_int = (fvco >> 16) & 0x3FF;
	fll_param->fll_frac = fvco & 0xFFFF;
	return 0;
}

static void nau8825_fll_apply(struct nau8825 *nau8825,
		struct nau8825_fll *fll_param)
{
	regmap_update_bits(nau8825->regmap, NAU8825_REG_CLK_DIVIDER,
		NAU8825_CLK_SRC_MASK | NAU8825_CLK_MCLK_SRC_MASK,
		NAU8825_CLK_SRC_MCLK | fll_param->mclk_src);
	regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL1,
			NAU8825_FLL_RATIO_MASK, fll_param->ratio);
	/* FLL 16-bit fractional input */
	regmap_write(nau8825->regmap, NAU8825_REG_FLL2, fll_param->fll_frac);
	/* FLL 10-bit integer input */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL3,
			NAU8825_FLL_INTEGER_MASK, fll_param->fll_int);
	/* FLL pre-scaler */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL4,
			NAU8825_FLL_REF_DIV_MASK, fll_param->clk_ref_div);
	/* select divided VCO input */
	regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL5,
		NAU8825_FLL_CLK_SW_MASK, NAU8825_FLL_CLK_SW_REF);
	/* Disable free-running mode */
	regmap_update_bits(nau8825->regmap,
		NAU8825_REG_FLL6, NAU8825_DCO_EN, 0);
	if (fll_param->fll_frac) {
		regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL5,
			NAU8825_FLL_PDB_DAC_EN | NAU8825_FLL_LOOP_FTR_EN |
			NAU8825_FLL_FTR_SW_MASK,
			NAU8825_FLL_PDB_DAC_EN | NAU8825_FLL_LOOP_FTR_EN |
			NAU8825_FLL_FTR_SW_FILTER);
		regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL6,
			NAU8825_SDM_EN, NAU8825_SDM_EN);
	} else {
		regmap_update_bits(nau8825->regmap, NAU8825_REG_FLL5,
			NAU8825_FLL_PDB_DAC_EN | NAU8825_FLL_LOOP_FTR_EN |
			NAU8825_FLL_FTR_SW_MASK, NAU8825_FLL_FTR_SW_ACCU);
		regmap_update_bits(nau8825->regmap,
			NAU8825_REG_FLL6, NAU8825_SDM_EN, 0);
	}
}

/* freq_out must be 256*Fs in order to achieve the best performance */
static int nau8825_set_pll(struct snd_soc_codec *codec, int pll_id, int source,
		unsigned int freq_in, unsigned int freq_out)
{
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);
	struct nau8825_fll fll_param;
	int ret, fs;

	fs = freq_out / 256;
	ret = nau8825_calc_fll_param(freq_in, fs, &fll_param);
	if (ret < 0) {
		dev_err(codec->dev, "Unsupported input clock %d\n", freq_in);
		return ret;
	}
	dev_dbg(codec->dev, "mclk_src=%x ratio=%x fll_frac=%x fll_int=%x clk_ref_div=%x\n",
		fll_param.mclk_src, fll_param.ratio, fll_param.fll_frac,
		fll_param.fll_int, fll_param.clk_ref_div);

	nau8825_fll_apply(nau8825, &fll_param);
	mdelay(2);
	regmap_update_bits(nau8825->regmap, NAU8825_REG_CLK_DIVIDER,
			NAU8825_CLK_SRC_MASK, NAU8825_CLK_SRC_VCO);
	return 0;
}

static int nau8825_mclk_prepare(struct nau8825 *nau8825, unsigned int freq)
{
	int ret = 0;

	nau8825->mclk = devm_clk_get(nau8825->dev, "mclk");
	if (IS_ERR(nau8825->mclk)) {
		dev_info(nau8825->dev, "No 'mclk' clock found, assume MCLK is managed externally");
		return 0;
	}

	if (!nau8825->mclk_freq) {
		ret = clk_prepare_enable(nau8825->mclk);
		if (ret) {
			dev_err(nau8825->dev, "Unable to prepare codec mclk\n");
			return ret;
		}
	}

	if (nau8825->mclk_freq != freq) {
		freq = clk_round_rate(nau8825->mclk, freq);
		ret = clk_set_rate(nau8825->mclk, freq);
		if (ret) {
			dev_err(nau8825->dev, "Unable to set mclk rate\n");
			return ret;
		}
		nau8825->mclk_freq = freq;
	}

	return 0;
}

static int nau8825_configure_sysclk(struct nau8825 *nau8825, int clk_id,
	unsigned int freq)
{
	struct regmap *regmap = nau8825->regmap;
	int ret;

	switch (clk_id) {
	case NAU8825_CLK_MCLK:
		regmap_update_bits(regmap, NAU8825_REG_CLK_DIVIDER,
			NAU8825_CLK_SRC_MASK, NAU8825_CLK_SRC_MCLK);
		regmap_update_bits(regmap, NAU8825_REG_FLL6, NAU8825_DCO_EN, 0);
		ret = nau8825_mclk_prepare(nau8825, freq);
		if (ret)
			return ret;

		break;
	case NAU8825_CLK_INTERNAL:
		regmap_update_bits(regmap, NAU8825_REG_FLL6, NAU8825_DCO_EN,
			NAU8825_DCO_EN);
		regmap_update_bits(regmap, NAU8825_REG_CLK_DIVIDER,
			NAU8825_CLK_SRC_MASK, NAU8825_CLK_SRC_VCO);
		if (nau8825->mclk_freq) {
			clk_disable_unprepare(nau8825->mclk);
			nau8825->mclk_freq = 0;
		}

		break;
	case NAU8825_CLK_FLL_MCLK:
		regmap_update_bits(regmap, NAU8825_REG_FLL3,
			NAU8825_FLL_CLK_SRC_MASK, NAU8825_FLL_CLK_SRC_MCLK);
		ret = nau8825_mclk_prepare(nau8825, freq);
		if (ret)
			return ret;

		break;
	case NAU8825_CLK_FLL_BLK:
		regmap_update_bits(regmap, NAU8825_REG_FLL3,
			NAU8825_FLL_CLK_SRC_MASK, NAU8825_FLL_CLK_SRC_BLK);
		if (nau8825->mclk_freq) {
			clk_disable_unprepare(nau8825->mclk);
			nau8825->mclk_freq = 0;
		}

		break;
	case NAU8825_CLK_FLL_FS:
		regmap_update_bits(regmap, NAU8825_REG_FLL3,
			NAU8825_FLL_CLK_SRC_MASK, NAU8825_FLL_CLK_SRC_FS);
		if (nau8825->mclk_freq) {
			clk_disable_unprepare(nau8825->mclk);
			nau8825->mclk_freq = 0;
		}

		break;
	default:
		dev_err(nau8825->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}

	dev_dbg(nau8825->dev, "Sysclk is %dHz and clock id is %d\n", freq,
		clk_id);
	return 0;
}

static int nau8825_set_sysclk(struct snd_soc_codec *codec, int clk_id,
	int source, unsigned int freq, int dir)
{
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);

	return nau8825_configure_sysclk(nau8825, clk_id, freq);
}

static int nau8825_set_bias_level(struct snd_soc_codec *codec,
				   enum snd_soc_bias_level level)
{
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_OFF) {
			if (nau8825->mclk_freq) {
				ret = clk_prepare_enable(nau8825->mclk);
				if (ret) {
					dev_err(nau8825->dev, "Unable to prepare codec mclk\n");
					return ret;
				}
			}
		}
		break;

	case SND_SOC_BIAS_OFF:
		if (nau8825->mclk_freq)
			clk_disable_unprepare(nau8825->mclk);
		break;
	}
	return 0;
}

#ifdef CONFIG_PM
static int nau8825_suspend(struct snd_soc_codec *codec)
{
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);

	disable_irq(nau8825->irq);
	regcache_cache_only(nau8825->regmap, true);
	regcache_mark_dirty(nau8825->regmap);

	return 0;
}

static int nau8825_resume(struct snd_soc_codec *codec)
{
	struct nau8825 *nau8825 = snd_soc_codec_get_drvdata(codec);

	/* The chip may lose power and reset in S3. regcache_sync restores
	 * register values including configurations for sysclk, irq, and
	 * jack/button detection.
	 */
	regcache_cache_only(nau8825->regmap, false);
	regcache_sync(nau8825->regmap);

	/* Check the jack plug status directly. If the headset is unplugged
	 * during S3 when the chip has no power, there will be no jack
	 * detection irq even after the nau8825_restart_jack_detection below,
	 * because the chip just thinks no headset has ever been plugged in.
	 */
	if (!nau8825_is_jack_inserted(nau8825->regmap)) {
		nau8825_eject_jack(nau8825);
		snd_soc_jack_report(nau8825->jack, 0, SND_JACK_HEADSET);
	}

	enable_irq(nau8825->irq);

	/* Run jack detection to check the type (OMTP or CTIA) of the headset
	 * if there is one. This handles the case where a different type of
	 * headset is plugged in during S3. This triggers an IRQ iff a headset
	 * is already plugged in.
	 */
	nau8825_restart_jack_detection(nau8825->regmap);

	return 0;
}
#else
#define nau8825_suspend NULL
#define nau8825_resume NULL
#endif

static struct snd_soc_codec_driver nau8825_codec_driver = {
	.probe = nau8825_codec_probe,
	.set_sysclk = nau8825_set_sysclk,
	.set_pll = nau8825_set_pll,
	.set_bias_level = nau8825_set_bias_level,
	.suspend_bias_off = true,
	.suspend = nau8825_suspend,
	.resume = nau8825_resume,

	.controls = nau8825_controls,
	.num_controls = ARRAY_SIZE(nau8825_controls),
	.dapm_widgets = nau8825_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(nau8825_dapm_widgets),
	.dapm_routes = nau8825_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(nau8825_dapm_routes),
};

static void nau8825_reset_chip(struct regmap *regmap)
{
	regmap_write(regmap, NAU8825_REG_RESET, 0x00);
	regmap_write(regmap, NAU8825_REG_RESET, 0x00);
}

static void nau8825_print_device_properties(struct nau8825 *nau8825)
{
	int i;
	struct device *dev = nau8825->dev;

	dev_dbg(dev, "jkdet-enable:         %d\n", nau8825->jkdet_enable);
	dev_dbg(dev, "jkdet-pull-enable:    %d\n", nau8825->jkdet_pull_enable);
	dev_dbg(dev, "jkdet-pull-up:        %d\n", nau8825->jkdet_pull_up);
	dev_dbg(dev, "jkdet-polarity:       %d\n", nau8825->jkdet_polarity);
	dev_dbg(dev, "micbias-voltage:      %d\n", nau8825->micbias_voltage);
	dev_dbg(dev, "vref-impedance:       %d\n", nau8825->vref_impedance);

	dev_dbg(dev, "sar-threshold-num:    %d\n", nau8825->sar_threshold_num);
	for (i = 0; i < nau8825->sar_threshold_num; i++)
		dev_dbg(dev, "sar-threshold[%d]=%d\n", i,
				nau8825->sar_threshold[i]);

	dev_dbg(dev, "sar-hysteresis:       %d\n", nau8825->sar_hysteresis);
	dev_dbg(dev, "sar-voltage:          %d\n", nau8825->sar_voltage);
	dev_dbg(dev, "sar-compare-time:     %d\n", nau8825->sar_compare_time);
	dev_dbg(dev, "sar-sampling-time:    %d\n", nau8825->sar_sampling_time);
	dev_dbg(dev, "short-key-debounce:   %d\n", nau8825->key_debounce);
	dev_dbg(dev, "jack-insert-debounce: %d\n",
			nau8825->jack_insert_debounce);
	dev_dbg(dev, "jack-eject-debounce:  %d\n",
			nau8825->jack_eject_debounce);
}

static int nau8825_read_device_properties(struct device *dev,
	struct nau8825 *nau8825) {

	nau8825->jkdet_enable = device_property_read_bool(dev,
		"nuvoton,jkdet-enable");
	nau8825->jkdet_pull_enable = device_property_read_bool(dev,
		"nuvoton,jkdet-pull-enable");
	nau8825->jkdet_pull_up = device_property_read_bool(dev,
		"nuvoton,jkdet-pull-up");
	device_property_read_u32(dev, "nuvoton,jkdet-polarity",
		&nau8825->jkdet_polarity);
	device_property_read_u32(dev, "nuvoton,micbias-voltage",
		&nau8825->micbias_voltage);
	device_property_read_u32(dev, "nuvoton,vref-impedance",
		&nau8825->vref_impedance);
	device_property_read_u32(dev, "nuvoton,sar-threshold-num",
		&nau8825->sar_threshold_num);
	device_property_read_u32_array(dev, "nuvoton,sar-threshold",
		nau8825->sar_threshold, nau8825->sar_threshold_num);
	device_property_read_u32(dev, "nuvoton,sar-hysteresis",
		&nau8825->sar_hysteresis);
	device_property_read_u32(dev, "nuvoton,sar-voltage",
		&nau8825->sar_voltage);
	device_property_read_u32(dev, "nuvoton,sar-compare-time",
		&nau8825->sar_compare_time);
	device_property_read_u32(dev, "nuvoton,sar-sampling-time",
		&nau8825->sar_sampling_time);
	device_property_read_u32(dev, "nuvoton,short-key-debounce",
		&nau8825->key_debounce);
	device_property_read_u32(dev, "nuvoton,jack-insert-debounce",
		&nau8825->jack_insert_debounce);
	device_property_read_u32(dev, "nuvoton,jack-eject-debounce",
		&nau8825->jack_eject_debounce);

	nau8825->mclk = devm_clk_get(dev, "mclk");
	if (PTR_ERR(nau8825->mclk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (PTR_ERR(nau8825->mclk) == -ENOENT) {
		/* The MCLK is managed externally or not used at all */
		nau8825->mclk = NULL;
		dev_info(dev, "No 'mclk' clock found, assume MCLK is managed externally");
	} else if (IS_ERR(nau8825->mclk)) {
		return -EINVAL;
	}

	return 0;
}

static int nau8825_setup_irq(struct nau8825 *nau8825)
{
	struct regmap *regmap = nau8825->regmap;
	int ret;

	/* IRQ Output Enable */
	regmap_update_bits(regmap, NAU8825_REG_INTERRUPT_MASK,
		NAU8825_IRQ_OUTPUT_EN, NAU8825_IRQ_OUTPUT_EN);

	/* Enable internal VCO needed for interruptions */
	nau8825_configure_sysclk(nau8825, NAU8825_CLK_INTERNAL, 0);

	/* Enable DDACR needed for interrupts
	 * It is the same as force_enable_pin("DDACR") we do later
	 */
	regmap_update_bits(regmap, NAU8825_REG_ENA_CTRL,
		NAU8825_ENABLE_DACR, NAU8825_ENABLE_DACR);

	ret = devm_request_threaded_irq(nau8825->dev, nau8825->irq, NULL,
		nau8825_interrupt, IRQF_TRIGGER_LOW | IRQF_ONESHOT,
		"nau8825", nau8825);

	if (ret) {
		dev_err(nau8825->dev, "Cannot request irq %d (%d)\n",
			nau8825->irq, ret);
		return ret;
	}

	return 0;
}

static int nau8825_i2c_probe(struct i2c_client *i2c,
	const struct i2c_device_id *id)
{
	struct device *dev = &i2c->dev;
	struct nau8825 *nau8825 = dev_get_platdata(&i2c->dev);
	int ret, value;

	if (!nau8825) {
		nau8825 = devm_kzalloc(dev, sizeof(*nau8825), GFP_KERNEL);
		if (!nau8825)
			return -ENOMEM;
		ret = nau8825_read_device_properties(dev, nau8825);
		if (ret)
			return ret;
	}

	i2c_set_clientdata(i2c, nau8825);

	nau8825->regmap = devm_regmap_init_i2c(i2c, &nau8825_regmap_config);
	if (IS_ERR(nau8825->regmap))
		return PTR_ERR(nau8825->regmap);
	nau8825->dev = dev;
	nau8825->irq = i2c->irq;

	nau8825_print_device_properties(nau8825);

	nau8825_reset_chip(nau8825->regmap);
	ret = regmap_read(nau8825->regmap, NAU8825_REG_I2C_DEVICE_ID, &value);
	if (ret < 0) {
		dev_err(dev, "Failed to read device id from the NAU8825: %d\n",
			ret);
		return ret;
	}
	if ((value & NAU8825_SOFTWARE_ID_MASK) !=
			NAU8825_SOFTWARE_ID_NAU8825) {
		dev_err(dev, "Not a NAU8825 chip\n");
		return -ENODEV;
	}

	nau8825_init_regs(nau8825);

	if (i2c->irq)
		nau8825_setup_irq(nau8825);

	return snd_soc_register_codec(&i2c->dev, &nau8825_codec_driver,
		&nau8825_dai, 1);
}

static int nau8825_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id nau8825_i2c_ids[] = {
	{ "nau8825", 0 },
	{ }
};

#ifdef CONFIG_OF
static const struct of_device_id nau8825_of_ids[] = {
	{ .compatible = "nuvoton,nau8825", },
	{}
};
MODULE_DEVICE_TABLE(of, nau8825_of_ids);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id nau8825_acpi_match[] = {
	{ "10508825", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, nau8825_acpi_match);
#endif

static struct i2c_driver nau8825_driver = {
	.driver = {
		.name = "nau8825",
		.of_match_table = of_match_ptr(nau8825_of_ids),
		.acpi_match_table = ACPI_PTR(nau8825_acpi_match),
	},
	.probe = nau8825_i2c_probe,
	.remove = nau8825_i2c_remove,
	.id_table = nau8825_i2c_ids,
};
module_i2c_driver(nau8825_driver);

MODULE_DESCRIPTION("ASoC nau8825 driver");
MODULE_AUTHOR("Anatol Pomozov <anatol@chromium.org>");
MODULE_LICENSE("GPL");
