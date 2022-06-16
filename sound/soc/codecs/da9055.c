// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DA9055 ALSA Soc codec driver
 *
 * Copyright (c) 2012 Dialog Semiconductor
 *
 * Tested on (Samsung SMDK6410 board + DA9055 EVB) using I2S and I2C
 * Written by David Chen <david.chen@diasemi.com> and
 * Ashish Chavan <ashish.chavan@kpitcummins.com>
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/da9055.h>

/* DA9055 register space */

/* Status Registers */
#define DA9055_STATUS1			0x02
#define DA9055_PLL_STATUS		0x03
#define DA9055_AUX_L_GAIN_STATUS	0x04
#define DA9055_AUX_R_GAIN_STATUS	0x05
#define DA9055_MIC_L_GAIN_STATUS	0x06
#define DA9055_MIC_R_GAIN_STATUS	0x07
#define DA9055_MIXIN_L_GAIN_STATUS	0x08
#define DA9055_MIXIN_R_GAIN_STATUS	0x09
#define DA9055_ADC_L_GAIN_STATUS	0x0A
#define DA9055_ADC_R_GAIN_STATUS	0x0B
#define DA9055_DAC_L_GAIN_STATUS	0x0C
#define DA9055_DAC_R_GAIN_STATUS	0x0D
#define DA9055_HP_L_GAIN_STATUS		0x0E
#define DA9055_HP_R_GAIN_STATUS		0x0F
#define DA9055_LINE_GAIN_STATUS		0x10

/* System Initialisation Registers */
#define DA9055_CIF_CTRL			0x20
#define DA9055_DIG_ROUTING_AIF		0X21
#define DA9055_SR			0x22
#define DA9055_REFERENCES		0x23
#define DA9055_PLL_FRAC_TOP		0x24
#define DA9055_PLL_FRAC_BOT		0x25
#define DA9055_PLL_INTEGER		0x26
#define DA9055_PLL_CTRL			0x27
#define DA9055_AIF_CLK_MODE		0x28
#define DA9055_AIF_CTRL			0x29
#define DA9055_DIG_ROUTING_DAC		0x2A
#define DA9055_ALC_CTRL1		0x2B

/* Input - Gain, Select and Filter Registers */
#define DA9055_AUX_L_GAIN		0x30
#define DA9055_AUX_R_GAIN		0x31
#define DA9055_MIXIN_L_SELECT		0x32
#define DA9055_MIXIN_R_SELECT		0x33
#define DA9055_MIXIN_L_GAIN		0x34
#define DA9055_MIXIN_R_GAIN		0x35
#define DA9055_ADC_L_GAIN		0x36
#define DA9055_ADC_R_GAIN		0x37
#define DA9055_ADC_FILTERS1		0x38
#define DA9055_MIC_L_GAIN		0x39
#define DA9055_MIC_R_GAIN		0x3A

/* Output - Gain, Select and Filter Registers */
#define DA9055_DAC_FILTERS5		0x40
#define DA9055_DAC_FILTERS2		0x41
#define DA9055_DAC_FILTERS3		0x42
#define DA9055_DAC_FILTERS4		0x43
#define DA9055_DAC_FILTERS1		0x44
#define DA9055_DAC_L_GAIN		0x45
#define DA9055_DAC_R_GAIN		0x46
#define DA9055_CP_CTRL			0x47
#define DA9055_HP_L_GAIN		0x48
#define DA9055_HP_R_GAIN		0x49
#define DA9055_LINE_GAIN		0x4A
#define DA9055_MIXOUT_L_SELECT		0x4B
#define DA9055_MIXOUT_R_SELECT		0x4C

/* System Controller Registers */
#define DA9055_SYSTEM_MODES_INPUT	0x50
#define DA9055_SYSTEM_MODES_OUTPUT	0x51

/* Control Registers */
#define DA9055_AUX_L_CTRL		0x60
#define DA9055_AUX_R_CTRL		0x61
#define DA9055_MIC_BIAS_CTRL		0x62
#define DA9055_MIC_L_CTRL		0x63
#define DA9055_MIC_R_CTRL		0x64
#define DA9055_MIXIN_L_CTRL		0x65
#define DA9055_MIXIN_R_CTRL		0x66
#define DA9055_ADC_L_CTRL		0x67
#define DA9055_ADC_R_CTRL		0x68
#define DA9055_DAC_L_CTRL		0x69
#define DA9055_DAC_R_CTRL		0x6A
#define DA9055_HP_L_CTRL		0x6B
#define DA9055_HP_R_CTRL		0x6C
#define DA9055_LINE_CTRL		0x6D
#define DA9055_MIXOUT_L_CTRL		0x6E
#define DA9055_MIXOUT_R_CTRL		0x6F

/* Configuration Registers */
#define DA9055_LDO_CTRL			0x90
#define DA9055_IO_CTRL			0x91
#define DA9055_GAIN_RAMP_CTRL		0x92
#define DA9055_MIC_CONFIG		0x93
#define DA9055_PC_COUNT			0x94
#define DA9055_CP_VOL_THRESHOLD1	0x95
#define DA9055_CP_DELAY			0x96
#define DA9055_CP_DETECTOR		0x97
#define DA9055_AIF_OFFSET		0x98
#define DA9055_DIG_CTRL			0x99
#define DA9055_ALC_CTRL2		0x9A
#define DA9055_ALC_CTRL3		0x9B
#define DA9055_ALC_NOISE		0x9C
#define DA9055_ALC_TARGET_MIN		0x9D
#define DA9055_ALC_TARGET_MAX		0x9E
#define DA9055_ALC_GAIN_LIMITS		0x9F
#define DA9055_ALC_ANA_GAIN_LIMITS	0xA0
#define DA9055_ALC_ANTICLIP_CTRL	0xA1
#define DA9055_ALC_ANTICLIP_LEVEL	0xA2
#define DA9055_ALC_OFFSET_OP2M_L	0xA6
#define DA9055_ALC_OFFSET_OP2U_L	0xA7
#define DA9055_ALC_OFFSET_OP2M_R	0xAB
#define DA9055_ALC_OFFSET_OP2U_R	0xAC
#define DA9055_ALC_CIC_OP_LVL_CTRL	0xAD
#define DA9055_ALC_CIC_OP_LVL_DATA	0xAE
#define DA9055_DAC_NG_SETUP_TIME	0xAF
#define DA9055_DAC_NG_OFF_THRESHOLD	0xB0
#define DA9055_DAC_NG_ON_THRESHOLD	0xB1
#define DA9055_DAC_NG_CTRL		0xB2

/* SR bit fields */
#define DA9055_SR_8000			(0x1 << 0)
#define DA9055_SR_11025			(0x2 << 0)
#define DA9055_SR_12000			(0x3 << 0)
#define DA9055_SR_16000			(0x5 << 0)
#define DA9055_SR_22050			(0x6 << 0)
#define DA9055_SR_24000			(0x7 << 0)
#define DA9055_SR_32000			(0x9 << 0)
#define DA9055_SR_44100			(0xA << 0)
#define DA9055_SR_48000			(0xB << 0)
#define DA9055_SR_88200			(0xE << 0)
#define DA9055_SR_96000			(0xF << 0)

/* REFERENCES bit fields */
#define DA9055_BIAS_EN			(1 << 3)
#define DA9055_VMID_EN			(1 << 7)

/* PLL_CTRL bit fields */
#define DA9055_PLL_INDIV_10_20_MHZ	(1 << 2)
#define DA9055_PLL_SRM_EN		(1 << 6)
#define DA9055_PLL_EN			(1 << 7)

/* AIF_CLK_MODE bit fields */
#define DA9055_AIF_BCLKS_PER_WCLK_32	(0 << 0)
#define DA9055_AIF_BCLKS_PER_WCLK_64	(1 << 0)
#define DA9055_AIF_BCLKS_PER_WCLK_128	(2 << 0)
#define DA9055_AIF_BCLKS_PER_WCLK_256	(3 << 0)
#define DA9055_AIF_CLK_EN_SLAVE_MODE	(0 << 7)
#define DA9055_AIF_CLK_EN_MASTER_MODE	(1 << 7)

/* AIF_CTRL bit fields */
#define DA9055_AIF_FORMAT_I2S_MODE	(0 << 0)
#define DA9055_AIF_FORMAT_LEFT_J	(1 << 0)
#define DA9055_AIF_FORMAT_RIGHT_J	(2 << 0)
#define DA9055_AIF_FORMAT_DSP		(3 << 0)
#define DA9055_AIF_WORD_S16_LE		(0 << 2)
#define DA9055_AIF_WORD_S20_3LE		(1 << 2)
#define DA9055_AIF_WORD_S24_LE		(2 << 2)
#define DA9055_AIF_WORD_S32_LE		(3 << 2)

/* MIC_L_CTRL bit fields */
#define DA9055_MIC_L_MUTE_EN		(1 << 6)

/* MIC_R_CTRL bit fields */
#define DA9055_MIC_R_MUTE_EN		(1 << 6)

/* MIXIN_L_CTRL bit fields */
#define DA9055_MIXIN_L_MIX_EN		(1 << 3)

/* MIXIN_R_CTRL bit fields */
#define DA9055_MIXIN_R_MIX_EN		(1 << 3)

/* ADC_L_CTRL bit fields */
#define DA9055_ADC_L_EN			(1 << 7)

/* ADC_R_CTRL bit fields */
#define DA9055_ADC_R_EN			(1 << 7)

/* DAC_L_CTRL bit fields */
#define DA9055_DAC_L_MUTE_EN		(1 << 6)

/* DAC_R_CTRL bit fields */
#define DA9055_DAC_R_MUTE_EN		(1 << 6)

/* HP_L_CTRL bit fields */
#define DA9055_HP_L_AMP_OE		(1 << 3)

/* HP_R_CTRL bit fields */
#define DA9055_HP_R_AMP_OE		(1 << 3)

/* LINE_CTRL bit fields */
#define DA9055_LINE_AMP_OE		(1 << 3)

/* MIXOUT_L_CTRL bit fields */
#define DA9055_MIXOUT_L_MIX_EN		(1 << 3)

/* MIXOUT_R_CTRL bit fields */
#define DA9055_MIXOUT_R_MIX_EN		(1 << 3)

/* MIC bias select bit fields */
#define DA9055_MICBIAS2_EN		(1 << 6)

/* ALC_CIC_OP_LEVEL_CTRL bit fields */
#define DA9055_ALC_DATA_MIDDLE		(2 << 0)
#define DA9055_ALC_DATA_TOP		(3 << 0)
#define DA9055_ALC_CIC_OP_CHANNEL_LEFT	(0 << 7)
#define DA9055_ALC_CIC_OP_CHANNEL_RIGHT	(1 << 7)

#define DA9055_AIF_BCLK_MASK		(3 << 0)
#define DA9055_AIF_CLK_MODE_MASK	(1 << 7)
#define DA9055_AIF_FORMAT_MASK		(3 << 0)
#define DA9055_AIF_WORD_LENGTH_MASK	(3 << 2)
#define DA9055_GAIN_RAMPING_EN		(1 << 5)
#define DA9055_MICBIAS_LEVEL_MASK	(3 << 4)

#define DA9055_ALC_OFFSET_15_8		0x00FF00
#define DA9055_ALC_OFFSET_17_16		0x030000
#define DA9055_ALC_AVG_ITERATIONS	5

struct pll_div {
	int fref;
	int fout;
	u8 frac_top;
	u8 frac_bot;
	u8 integer;
	u8 mode;	/* 0 = slave, 1 = master */
};

/* PLL divisor table */
static const struct pll_div da9055_pll_div[] = {
	/* for MASTER mode, fs = 44.1Khz and its harmonics */
	{11289600, 2822400, 0x00, 0x00, 0x20, 1},	/* MCLK=11.2896Mhz */
	{12000000, 2822400, 0x03, 0x61, 0x1E, 1},	/* MCLK=12Mhz */
	{12288000, 2822400, 0x0C, 0xCC, 0x1D, 1},	/* MCLK=12.288Mhz */
	{13000000, 2822400, 0x19, 0x45, 0x1B, 1},	/* MCLK=13Mhz */
	{13500000, 2822400, 0x18, 0x56, 0x1A, 1},	/* MCLK=13.5Mhz */
	{14400000, 2822400, 0x02, 0xD0, 0x19, 1},	/* MCLK=14.4Mhz */
	{19200000, 2822400, 0x1A, 0x1C, 0x12, 1},	/* MCLK=19.2Mhz */
	{19680000, 2822400, 0x0B, 0x6D, 0x12, 1},	/* MCLK=19.68Mhz */
	{19800000, 2822400, 0x07, 0xDD, 0x12, 1},	/* MCLK=19.8Mhz */
	/* for MASTER mode, fs = 48Khz and its harmonics */
	{11289600, 3072000, 0x1A, 0x8E, 0x22, 1},	/* MCLK=11.2896Mhz */
	{12000000, 3072000, 0x18, 0x93, 0x20, 1},	/* MCLK=12Mhz */
	{12288000, 3072000, 0x00, 0x00, 0x20, 1},	/* MCLK=12.288Mhz */
	{13000000, 3072000, 0x07, 0xEA, 0x1E, 1},	/* MCLK=13Mhz */
	{13500000, 3072000, 0x04, 0x11, 0x1D, 1},	/* MCLK=13.5Mhz */
	{14400000, 3072000, 0x09, 0xD0, 0x1B, 1},	/* MCLK=14.4Mhz */
	{19200000, 3072000, 0x0F, 0x5C, 0x14, 1},	/* MCLK=19.2Mhz */
	{19680000, 3072000, 0x1F, 0x60, 0x13, 1},	/* MCLK=19.68Mhz */
	{19800000, 3072000, 0x1B, 0x80, 0x13, 1},	/* MCLK=19.8Mhz */
	/* for SLAVE mode with SRM */
	{11289600, 2822400, 0x0D, 0x47, 0x21, 0},	/* MCLK=11.2896Mhz */
	{12000000, 2822400, 0x0D, 0xFA, 0x1F, 0},	/* MCLK=12Mhz */
	{12288000, 2822400, 0x16, 0x66, 0x1E, 0},	/* MCLK=12.288Mhz */
	{13000000, 2822400, 0x00, 0x98, 0x1D, 0},	/* MCLK=13Mhz */
	{13500000, 2822400, 0x1E, 0x33, 0x1B, 0},	/* MCLK=13.5Mhz */
	{14400000, 2822400, 0x06, 0x50, 0x1A, 0},	/* MCLK=14.4Mhz */
	{19200000, 2822400, 0x14, 0xBC, 0x13, 0},	/* MCLK=19.2Mhz */
	{19680000, 2822400, 0x05, 0x66, 0x13, 0},	/* MCLK=19.68Mhz */
	{19800000, 2822400, 0x01, 0xAE, 0x13, 0},	/* MCLK=19.8Mhz  */
};

enum clk_src {
	DA9055_CLKSRC_MCLK
};

/* Gain and Volume */

static const DECLARE_TLV_DB_RANGE(aux_vol_tlv,
	0x0, 0x10, TLV_DB_SCALE_ITEM(-5400, 0, 0),
	/* -54dB to 15dB */
	0x11, 0x3f, TLV_DB_SCALE_ITEM(-5400, 150, 0)
);

static const DECLARE_TLV_DB_RANGE(digital_gain_tlv,
	0x0, 0x07, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 1),
	/* -78dB to 12dB */
	0x08, 0x7f, TLV_DB_SCALE_ITEM(-7800, 75, 0)
);

static const DECLARE_TLV_DB_RANGE(alc_analog_gain_tlv,
	0x0, 0x0, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 1),
	/* 0dB to 36dB */
	0x01, 0x07, TLV_DB_SCALE_ITEM(0, 600, 0)
);

static const DECLARE_TLV_DB_SCALE(mic_vol_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(mixin_gain_tlv, -450, 150, 0);
static const DECLARE_TLV_DB_SCALE(eq_gain_tlv, -1050, 150, 0);
static const DECLARE_TLV_DB_SCALE(hp_vol_tlv, -5700, 100, 0);
static const DECLARE_TLV_DB_SCALE(lineout_vol_tlv, -4800, 100, 0);
static const DECLARE_TLV_DB_SCALE(alc_threshold_tlv, -9450, 150, 0);
static const DECLARE_TLV_DB_SCALE(alc_gain_tlv, 0, 600, 0);

/* ADC and DAC high pass filter cutoff value */
static const char * const da9055_hpf_cutoff_txt[] = {
	"Fs/24000", "Fs/12000", "Fs/6000", "Fs/3000"
};

static SOC_ENUM_SINGLE_DECL(da9055_dac_hpf_cutoff,
			    DA9055_DAC_FILTERS1, 4, da9055_hpf_cutoff_txt);

static SOC_ENUM_SINGLE_DECL(da9055_adc_hpf_cutoff,
			    DA9055_ADC_FILTERS1, 4, da9055_hpf_cutoff_txt);

/* ADC and DAC voice mode (8kHz) high pass cutoff value */
static const char * const da9055_vf_cutoff_txt[] = {
	"2.5Hz", "25Hz", "50Hz", "100Hz", "150Hz", "200Hz", "300Hz", "400Hz"
};

static SOC_ENUM_SINGLE_DECL(da9055_dac_vf_cutoff,
			    DA9055_DAC_FILTERS1, 0, da9055_vf_cutoff_txt);

static SOC_ENUM_SINGLE_DECL(da9055_adc_vf_cutoff,
			    DA9055_ADC_FILTERS1, 0, da9055_vf_cutoff_txt);

/* Gain ramping rate value */
static const char * const da9055_gain_ramping_txt[] = {
	"nominal rate", "nominal rate * 4", "nominal rate * 8",
	"nominal rate / 8"
};

static SOC_ENUM_SINGLE_DECL(da9055_gain_ramping_rate,
			    DA9055_GAIN_RAMP_CTRL, 0, da9055_gain_ramping_txt);

/* DAC noise gate setup time value */
static const char * const da9055_dac_ng_setup_time_txt[] = {
	"256 samples", "512 samples", "1024 samples", "2048 samples"
};

static SOC_ENUM_SINGLE_DECL(da9055_dac_ng_setup_time,
			    DA9055_DAC_NG_SETUP_TIME, 0,
			    da9055_dac_ng_setup_time_txt);

/* DAC noise gate rampup rate value */
static const char * const da9055_dac_ng_rampup_txt[] = {
	"0.02 ms/dB", "0.16 ms/dB"
};

static SOC_ENUM_SINGLE_DECL(da9055_dac_ng_rampup_rate,
			    DA9055_DAC_NG_SETUP_TIME, 2,
			    da9055_dac_ng_rampup_txt);

/* DAC noise gate rampdown rate value */
static const char * const da9055_dac_ng_rampdown_txt[] = {
	"0.64 ms/dB", "20.48 ms/dB"
};

static SOC_ENUM_SINGLE_DECL(da9055_dac_ng_rampdown_rate,
			    DA9055_DAC_NG_SETUP_TIME, 3,
			    da9055_dac_ng_rampdown_txt);

/* DAC soft mute rate value */
static const char * const da9055_dac_soft_mute_rate_txt[] = {
	"1", "2", "4", "8", "16", "32", "64"
};

static SOC_ENUM_SINGLE_DECL(da9055_dac_soft_mute_rate,
			    DA9055_DAC_FILTERS5, 4,
			    da9055_dac_soft_mute_rate_txt);

/* DAC routing select */
static const char * const da9055_dac_src_txt[] = {
	"ADC output left", "ADC output right", "AIF input left",
	"AIF input right"
};

static SOC_ENUM_SINGLE_DECL(da9055_dac_l_src,
			    DA9055_DIG_ROUTING_DAC, 0, da9055_dac_src_txt);

static SOC_ENUM_SINGLE_DECL(da9055_dac_r_src,
			    DA9055_DIG_ROUTING_DAC, 4, da9055_dac_src_txt);

/* MIC PGA Left source select */
static const char * const da9055_mic_l_src_txt[] = {
	"MIC1_P_N", "MIC1_P", "MIC1_N", "MIC2_L"
};

static SOC_ENUM_SINGLE_DECL(da9055_mic_l_src,
			    DA9055_MIXIN_L_SELECT, 4, da9055_mic_l_src_txt);

/* MIC PGA Right source select */
static const char * const da9055_mic_r_src_txt[] = {
	"MIC2_R_L", "MIC2_R", "MIC2_L"
};

static SOC_ENUM_SINGLE_DECL(da9055_mic_r_src,
			    DA9055_MIXIN_R_SELECT, 4, da9055_mic_r_src_txt);

/* ALC Input Signal Tracking rate select */
static const char * const da9055_signal_tracking_rate_txt[] = {
	"1/4", "1/16", "1/256", "1/65536"
};

static SOC_ENUM_SINGLE_DECL(da9055_integ_attack_rate,
			    DA9055_ALC_CTRL3, 4,
			    da9055_signal_tracking_rate_txt);

static SOC_ENUM_SINGLE_DECL(da9055_integ_release_rate,
			    DA9055_ALC_CTRL3, 6,
			    da9055_signal_tracking_rate_txt);

/* ALC Attack Rate select */
static const char * const da9055_attack_rate_txt[] = {
	"44/fs", "88/fs", "176/fs", "352/fs", "704/fs", "1408/fs", "2816/fs",
	"5632/fs", "11264/fs", "22528/fs", "45056/fs", "90112/fs", "180224/fs"
};

static SOC_ENUM_SINGLE_DECL(da9055_attack_rate,
			    DA9055_ALC_CTRL2, 0, da9055_attack_rate_txt);

/* ALC Release Rate select */
static const char * const da9055_release_rate_txt[] = {
	"176/fs", "352/fs", "704/fs", "1408/fs", "2816/fs", "5632/fs",
	"11264/fs", "22528/fs", "45056/fs", "90112/fs", "180224/fs"
};

static SOC_ENUM_SINGLE_DECL(da9055_release_rate,
			    DA9055_ALC_CTRL2, 4, da9055_release_rate_txt);

/* ALC Hold Time select */
static const char * const da9055_hold_time_txt[] = {
	"62/fs", "124/fs", "248/fs", "496/fs", "992/fs", "1984/fs", "3968/fs",
	"7936/fs", "15872/fs", "31744/fs", "63488/fs", "126976/fs",
	"253952/fs", "507904/fs", "1015808/fs", "2031616/fs"
};

static SOC_ENUM_SINGLE_DECL(da9055_hold_time,
			    DA9055_ALC_CTRL3, 0, da9055_hold_time_txt);

static int da9055_get_alc_data(struct snd_soc_component *component, u8 reg_val)
{
	int mid_data, top_data;
	int sum = 0;
	u8 iteration;

	for (iteration = 0; iteration < DA9055_ALC_AVG_ITERATIONS;
	     iteration++) {
		/* Select the left or right channel and capture data */
		snd_soc_component_write(component, DA9055_ALC_CIC_OP_LVL_CTRL, reg_val);

		/* Select middle 8 bits for read back from data register */
		snd_soc_component_write(component, DA9055_ALC_CIC_OP_LVL_CTRL,
			      reg_val | DA9055_ALC_DATA_MIDDLE);
		mid_data = snd_soc_component_read(component, DA9055_ALC_CIC_OP_LVL_DATA);

		/* Select top 8 bits for read back from data register */
		snd_soc_component_write(component, DA9055_ALC_CIC_OP_LVL_CTRL,
			      reg_val | DA9055_ALC_DATA_TOP);
		top_data = snd_soc_component_read(component, DA9055_ALC_CIC_OP_LVL_DATA);

		sum += ((mid_data << 8) | (top_data << 16));
	}

	return sum / DA9055_ALC_AVG_ITERATIONS;
}

static int da9055_put_alc_sw(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	u8 reg_val, adc_left, adc_right, mic_left, mic_right;
	int avg_left_data, avg_right_data, offset_l, offset_r;

	if (ucontrol->value.integer.value[0]) {
		/*
		 * While enabling ALC (or ALC sync mode), calibration of the DC
		 * offsets must be done first
		 */

		/* Save current values from Mic control registers */
		mic_left = snd_soc_component_read(component, DA9055_MIC_L_CTRL);
		mic_right = snd_soc_component_read(component, DA9055_MIC_R_CTRL);

		/* Mute Mic PGA Left and Right */
		snd_soc_component_update_bits(component, DA9055_MIC_L_CTRL,
				    DA9055_MIC_L_MUTE_EN, DA9055_MIC_L_MUTE_EN);
		snd_soc_component_update_bits(component, DA9055_MIC_R_CTRL,
				    DA9055_MIC_R_MUTE_EN, DA9055_MIC_R_MUTE_EN);

		/* Save current values from ADC control registers */
		adc_left = snd_soc_component_read(component, DA9055_ADC_L_CTRL);
		adc_right = snd_soc_component_read(component, DA9055_ADC_R_CTRL);

		/* Enable ADC Left and Right */
		snd_soc_component_update_bits(component, DA9055_ADC_L_CTRL,
				    DA9055_ADC_L_EN, DA9055_ADC_L_EN);
		snd_soc_component_update_bits(component, DA9055_ADC_R_CTRL,
				    DA9055_ADC_R_EN, DA9055_ADC_R_EN);

		/* Calculate average for Left and Right data */
		/* Left Data */
		avg_left_data = da9055_get_alc_data(component,
				DA9055_ALC_CIC_OP_CHANNEL_LEFT);
		/* Right Data */
		avg_right_data = da9055_get_alc_data(component,
				 DA9055_ALC_CIC_OP_CHANNEL_RIGHT);

		/* Calculate DC offset */
		offset_l = -avg_left_data;
		offset_r = -avg_right_data;

		reg_val = (offset_l & DA9055_ALC_OFFSET_15_8) >> 8;
		snd_soc_component_write(component, DA9055_ALC_OFFSET_OP2M_L, reg_val);
		reg_val = (offset_l & DA9055_ALC_OFFSET_17_16) >> 16;
		snd_soc_component_write(component, DA9055_ALC_OFFSET_OP2U_L, reg_val);

		reg_val = (offset_r & DA9055_ALC_OFFSET_15_8) >> 8;
		snd_soc_component_write(component, DA9055_ALC_OFFSET_OP2M_R, reg_val);
		reg_val = (offset_r & DA9055_ALC_OFFSET_17_16) >> 16;
		snd_soc_component_write(component, DA9055_ALC_OFFSET_OP2U_R, reg_val);

		/* Restore original values of ADC control registers */
		snd_soc_component_write(component, DA9055_ADC_L_CTRL, adc_left);
		snd_soc_component_write(component, DA9055_ADC_R_CTRL, adc_right);

		/* Restore original values of Mic control registers */
		snd_soc_component_write(component, DA9055_MIC_L_CTRL, mic_left);
		snd_soc_component_write(component, DA9055_MIC_R_CTRL, mic_right);
	}

	return snd_soc_put_volsw(kcontrol, ucontrol);
}

static const struct snd_kcontrol_new da9055_snd_controls[] = {

	/* Volume controls */
	SOC_DOUBLE_R_TLV("Mic Volume",
			 DA9055_MIC_L_GAIN, DA9055_MIC_R_GAIN,
			 0, 0x7, 0, mic_vol_tlv),
	SOC_DOUBLE_R_TLV("Aux Volume",
			 DA9055_AUX_L_GAIN, DA9055_AUX_R_GAIN,
			 0, 0x3f, 0, aux_vol_tlv),
	SOC_DOUBLE_R_TLV("Mixin PGA Volume",
			 DA9055_MIXIN_L_GAIN, DA9055_MIXIN_R_GAIN,
			 0, 0xf, 0, mixin_gain_tlv),
	SOC_DOUBLE_R_TLV("ADC Volume",
			 DA9055_ADC_L_GAIN, DA9055_ADC_R_GAIN,
			 0, 0x7f, 0, digital_gain_tlv),

	SOC_DOUBLE_R_TLV("DAC Volume",
			 DA9055_DAC_L_GAIN, DA9055_DAC_R_GAIN,
			 0, 0x7f, 0, digital_gain_tlv),
	SOC_DOUBLE_R_TLV("Headphone Volume",
			 DA9055_HP_L_GAIN, DA9055_HP_R_GAIN,
			 0, 0x3f, 0, hp_vol_tlv),
	SOC_SINGLE_TLV("Lineout Volume", DA9055_LINE_GAIN, 0, 0x3f, 0,
		       lineout_vol_tlv),

	/* DAC Equalizer controls */
	SOC_SINGLE("DAC EQ Switch", DA9055_DAC_FILTERS4, 7, 1, 0),
	SOC_SINGLE_TLV("DAC EQ1 Volume", DA9055_DAC_FILTERS2, 0, 0xf, 0,
		       eq_gain_tlv),
	SOC_SINGLE_TLV("DAC EQ2 Volume", DA9055_DAC_FILTERS2, 4, 0xf, 0,
		       eq_gain_tlv),
	SOC_SINGLE_TLV("DAC EQ3 Volume", DA9055_DAC_FILTERS3, 0, 0xf, 0,
		       eq_gain_tlv),
	SOC_SINGLE_TLV("DAC EQ4 Volume", DA9055_DAC_FILTERS3, 4, 0xf, 0,
		       eq_gain_tlv),
	SOC_SINGLE_TLV("DAC EQ5 Volume", DA9055_DAC_FILTERS4, 0, 0xf, 0,
		       eq_gain_tlv),

	/* High Pass Filter and Voice Mode controls */
	SOC_SINGLE("ADC HPF Switch", DA9055_ADC_FILTERS1, 7, 1, 0),
	SOC_ENUM("ADC HPF Cutoff", da9055_adc_hpf_cutoff),
	SOC_SINGLE("ADC Voice Mode Switch", DA9055_ADC_FILTERS1, 3, 1, 0),
	SOC_ENUM("ADC Voice Cutoff", da9055_adc_vf_cutoff),

	SOC_SINGLE("DAC HPF Switch", DA9055_DAC_FILTERS1, 7, 1, 0),
	SOC_ENUM("DAC HPF Cutoff", da9055_dac_hpf_cutoff),
	SOC_SINGLE("DAC Voice Mode Switch", DA9055_DAC_FILTERS1, 3, 1, 0),
	SOC_ENUM("DAC Voice Cutoff", da9055_dac_vf_cutoff),

	/* Mute controls */
	SOC_DOUBLE_R("Mic Switch", DA9055_MIC_L_CTRL,
		     DA9055_MIC_R_CTRL, 6, 1, 0),
	SOC_DOUBLE_R("Aux Switch", DA9055_AUX_L_CTRL,
		     DA9055_AUX_R_CTRL, 6, 1, 0),
	SOC_DOUBLE_R("Mixin PGA Switch", DA9055_MIXIN_L_CTRL,
		     DA9055_MIXIN_R_CTRL, 6, 1, 0),
	SOC_DOUBLE_R("ADC Switch", DA9055_ADC_L_CTRL,
		     DA9055_ADC_R_CTRL, 6, 1, 0),
	SOC_DOUBLE_R("Headphone Switch", DA9055_HP_L_CTRL,
		     DA9055_HP_R_CTRL, 6, 1, 0),
	SOC_SINGLE("Lineout Switch", DA9055_LINE_CTRL, 6, 1, 0),
	SOC_SINGLE("DAC Soft Mute Switch", DA9055_DAC_FILTERS5, 7, 1, 0),
	SOC_ENUM("DAC Soft Mute Rate", da9055_dac_soft_mute_rate),

	/* Zero Cross controls */
	SOC_DOUBLE_R("Aux ZC Switch", DA9055_AUX_L_CTRL,
		     DA9055_AUX_R_CTRL, 4, 1, 0),
	SOC_DOUBLE_R("Mixin PGA ZC Switch", DA9055_MIXIN_L_CTRL,
		     DA9055_MIXIN_R_CTRL, 4, 1, 0),
	SOC_DOUBLE_R("Headphone ZC Switch", DA9055_HP_L_CTRL,
		     DA9055_HP_R_CTRL, 4, 1, 0),
	SOC_SINGLE("Lineout ZC Switch", DA9055_LINE_CTRL, 4, 1, 0),

	/* Gain Ramping controls */
	SOC_DOUBLE_R("Aux Gain Ramping Switch", DA9055_AUX_L_CTRL,
		     DA9055_AUX_R_CTRL, 5, 1, 0),
	SOC_DOUBLE_R("Mixin Gain Ramping Switch", DA9055_MIXIN_L_CTRL,
		     DA9055_MIXIN_R_CTRL, 5, 1, 0),
	SOC_DOUBLE_R("ADC Gain Ramping Switch", DA9055_ADC_L_CTRL,
		     DA9055_ADC_R_CTRL, 5, 1, 0),
	SOC_DOUBLE_R("DAC Gain Ramping Switch", DA9055_DAC_L_CTRL,
		     DA9055_DAC_R_CTRL, 5, 1, 0),
	SOC_DOUBLE_R("Headphone Gain Ramping Switch", DA9055_HP_L_CTRL,
		     DA9055_HP_R_CTRL, 5, 1, 0),
	SOC_SINGLE("Lineout Gain Ramping Switch", DA9055_LINE_CTRL, 5, 1, 0),
	SOC_ENUM("Gain Ramping Rate", da9055_gain_ramping_rate),

	/* DAC Noise Gate controls */
	SOC_SINGLE("DAC NG Switch", DA9055_DAC_NG_CTRL, 7, 1, 0),
	SOC_SINGLE("DAC NG ON Threshold", DA9055_DAC_NG_ON_THRESHOLD,
		   0, 0x7, 0),
	SOC_SINGLE("DAC NG OFF Threshold", DA9055_DAC_NG_OFF_THRESHOLD,
		   0, 0x7, 0),
	SOC_ENUM("DAC NG Setup Time", da9055_dac_ng_setup_time),
	SOC_ENUM("DAC NG Rampup Rate", da9055_dac_ng_rampup_rate),
	SOC_ENUM("DAC NG Rampdown Rate", da9055_dac_ng_rampdown_rate),

	/* DAC Invertion control */
	SOC_SINGLE("DAC Left Invert", DA9055_DIG_CTRL, 3, 1, 0),
	SOC_SINGLE("DAC Right Invert", DA9055_DIG_CTRL, 7, 1, 0),

	/* DMIC controls */
	SOC_DOUBLE_R("DMIC Switch", DA9055_MIXIN_L_SELECT,
		     DA9055_MIXIN_R_SELECT, 7, 1, 0),

	/* ALC Controls */
	SOC_DOUBLE_EXT("ALC Switch", DA9055_ALC_CTRL1, 3, 7, 1, 0,
		       snd_soc_get_volsw, da9055_put_alc_sw),
	SOC_SINGLE_EXT("ALC Sync Mode Switch", DA9055_ALC_CTRL1, 1, 1, 0,
		       snd_soc_get_volsw, da9055_put_alc_sw),
	SOC_SINGLE("ALC Offset Switch", DA9055_ALC_CTRL1, 0, 1, 0),
	SOC_SINGLE("ALC Anticlip Mode Switch", DA9055_ALC_ANTICLIP_CTRL,
		   7, 1, 0),
	SOC_SINGLE("ALC Anticlip Level", DA9055_ALC_ANTICLIP_LEVEL,
		   0, 0x7f, 0),
	SOC_SINGLE_TLV("ALC Min Threshold Volume", DA9055_ALC_TARGET_MIN,
		       0, 0x3f, 1, alc_threshold_tlv),
	SOC_SINGLE_TLV("ALC Max Threshold Volume", DA9055_ALC_TARGET_MAX,
		       0, 0x3f, 1, alc_threshold_tlv),
	SOC_SINGLE_TLV("ALC Noise Threshold Volume", DA9055_ALC_NOISE,
		       0, 0x3f, 1, alc_threshold_tlv),
	SOC_SINGLE_TLV("ALC Max Gain Volume", DA9055_ALC_GAIN_LIMITS,
		       4, 0xf, 0, alc_gain_tlv),
	SOC_SINGLE_TLV("ALC Max Attenuation Volume", DA9055_ALC_GAIN_LIMITS,
		       0, 0xf, 0, alc_gain_tlv),
	SOC_SINGLE_TLV("ALC Min Analog Gain Volume",
		       DA9055_ALC_ANA_GAIN_LIMITS,
		       0, 0x7, 0, alc_analog_gain_tlv),
	SOC_SINGLE_TLV("ALC Max Analog Gain Volume",
		       DA9055_ALC_ANA_GAIN_LIMITS,
		       4, 0x7, 0, alc_analog_gain_tlv),
	SOC_ENUM("ALC Attack Rate", da9055_attack_rate),
	SOC_ENUM("ALC Release Rate", da9055_release_rate),
	SOC_ENUM("ALC Hold Time", da9055_hold_time),
	/*
	 * Rate at which input signal envelope is tracked as the signal gets
	 * larger
	 */
	SOC_ENUM("ALC Integ Attack Rate", da9055_integ_attack_rate),
	/*
	 * Rate at which input signal envelope is tracked as the signal gets
	 * smaller
	 */
	SOC_ENUM("ALC Integ Release Rate", da9055_integ_release_rate),
};

/* DAPM Controls */

/* Mic PGA Left Source */
static const struct snd_kcontrol_new da9055_mic_l_mux_controls =
SOC_DAPM_ENUM("Route", da9055_mic_l_src);

/* Mic PGA Right Source */
static const struct snd_kcontrol_new da9055_mic_r_mux_controls =
SOC_DAPM_ENUM("Route", da9055_mic_r_src);

/* In Mixer Left */
static const struct snd_kcontrol_new da9055_dapm_mixinl_controls[] = {
	SOC_DAPM_SINGLE("Aux Left Switch", DA9055_MIXIN_L_SELECT, 0, 1, 0),
	SOC_DAPM_SINGLE("Mic Left Switch", DA9055_MIXIN_L_SELECT, 1, 1, 0),
	SOC_DAPM_SINGLE("Mic Right Switch", DA9055_MIXIN_L_SELECT, 2, 1, 0),
};

/* In Mixer Right */
static const struct snd_kcontrol_new da9055_dapm_mixinr_controls[] = {
	SOC_DAPM_SINGLE("Aux Right Switch", DA9055_MIXIN_R_SELECT, 0, 1, 0),
	SOC_DAPM_SINGLE("Mic Right Switch", DA9055_MIXIN_R_SELECT, 1, 1, 0),
	SOC_DAPM_SINGLE("Mic Left Switch", DA9055_MIXIN_R_SELECT, 2, 1, 0),
	SOC_DAPM_SINGLE("Mixin Left Switch", DA9055_MIXIN_R_SELECT, 3, 1, 0),
};

/* DAC Left Source */
static const struct snd_kcontrol_new da9055_dac_l_mux_controls =
SOC_DAPM_ENUM("Route", da9055_dac_l_src);

/* DAC Right Source */
static const struct snd_kcontrol_new da9055_dac_r_mux_controls =
SOC_DAPM_ENUM("Route", da9055_dac_r_src);

/* Out Mixer Left */
static const struct snd_kcontrol_new da9055_dapm_mixoutl_controls[] = {
	SOC_DAPM_SINGLE("Aux Left Switch", DA9055_MIXOUT_L_SELECT, 0, 1, 0),
	SOC_DAPM_SINGLE("Mixin Left Switch", DA9055_MIXOUT_L_SELECT, 1, 1, 0),
	SOC_DAPM_SINGLE("Mixin Right Switch", DA9055_MIXOUT_L_SELECT, 2, 1, 0),
	SOC_DAPM_SINGLE("DAC Left Switch", DA9055_MIXOUT_L_SELECT, 3, 1, 0),
	SOC_DAPM_SINGLE("Aux Left Invert Switch", DA9055_MIXOUT_L_SELECT,
			4, 1, 0),
	SOC_DAPM_SINGLE("Mixin Left Invert Switch", DA9055_MIXOUT_L_SELECT,
			5, 1, 0),
	SOC_DAPM_SINGLE("Mixin Right Invert Switch", DA9055_MIXOUT_L_SELECT,
			6, 1, 0),
};

/* Out Mixer Right */
static const struct snd_kcontrol_new da9055_dapm_mixoutr_controls[] = {
	SOC_DAPM_SINGLE("Aux Right Switch", DA9055_MIXOUT_R_SELECT, 0, 1, 0),
	SOC_DAPM_SINGLE("Mixin Right Switch", DA9055_MIXOUT_R_SELECT, 1, 1, 0),
	SOC_DAPM_SINGLE("Mixin Left Switch", DA9055_MIXOUT_R_SELECT, 2, 1, 0),
	SOC_DAPM_SINGLE("DAC Right Switch", DA9055_MIXOUT_R_SELECT, 3, 1, 0),
	SOC_DAPM_SINGLE("Aux Right Invert Switch", DA9055_MIXOUT_R_SELECT,
			4, 1, 0),
	SOC_DAPM_SINGLE("Mixin Right Invert Switch", DA9055_MIXOUT_R_SELECT,
			5, 1, 0),
	SOC_DAPM_SINGLE("Mixin Left Invert Switch", DA9055_MIXOUT_R_SELECT,
			6, 1, 0),
};

/* Headphone Output Enable */
static const struct snd_kcontrol_new da9055_dapm_hp_l_control =
SOC_DAPM_SINGLE("Switch", DA9055_HP_L_CTRL, 3, 1, 0);

static const struct snd_kcontrol_new da9055_dapm_hp_r_control =
SOC_DAPM_SINGLE("Switch", DA9055_HP_R_CTRL, 3, 1, 0);

/* Lineout Output Enable */
static const struct snd_kcontrol_new da9055_dapm_lineout_control =
SOC_DAPM_SINGLE("Switch", DA9055_LINE_CTRL, 3, 1, 0);

/* DAPM widgets */
static const struct snd_soc_dapm_widget da9055_dapm_widgets[] = {
	/* Input Side */

	/* Input Lines */
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("AUXL"),
	SND_SOC_DAPM_INPUT("AUXR"),

	/* MUXs for Mic PGA source selection */
	SND_SOC_DAPM_MUX("Mic Left Source", SND_SOC_NOPM, 0, 0,
			 &da9055_mic_l_mux_controls),
	SND_SOC_DAPM_MUX("Mic Right Source", SND_SOC_NOPM, 0, 0,
			 &da9055_mic_r_mux_controls),

	/* Input PGAs */
	SND_SOC_DAPM_PGA("Mic Left", DA9055_MIC_L_CTRL, 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Mic Right", DA9055_MIC_R_CTRL, 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Aux Left", DA9055_AUX_L_CTRL, 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Aux Right", DA9055_AUX_R_CTRL, 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MIXIN Left", DA9055_MIXIN_L_CTRL, 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MIXIN Right", DA9055_MIXIN_R_CTRL, 7, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("Mic Bias", DA9055_MIC_BIAS_CTRL, 7, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("AIF", DA9055_AIF_CTRL, 7, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Charge Pump", DA9055_CP_CTRL, 7, 0, NULL, 0),

	/* Input Mixers */
	SND_SOC_DAPM_MIXER("In Mixer Left", SND_SOC_NOPM, 0, 0,
			   &da9055_dapm_mixinl_controls[0],
			   ARRAY_SIZE(da9055_dapm_mixinl_controls)),
	SND_SOC_DAPM_MIXER("In Mixer Right", SND_SOC_NOPM, 0, 0,
			   &da9055_dapm_mixinr_controls[0],
			   ARRAY_SIZE(da9055_dapm_mixinr_controls)),

	/* ADCs */
	SND_SOC_DAPM_ADC("ADC Left", "Capture", DA9055_ADC_L_CTRL, 7, 0),
	SND_SOC_DAPM_ADC("ADC Right", "Capture", DA9055_ADC_R_CTRL, 7, 0),

	/* Output Side */

	/* MUXs for DAC source selection */
	SND_SOC_DAPM_MUX("DAC Left Source", SND_SOC_NOPM, 0, 0,
			 &da9055_dac_l_mux_controls),
	SND_SOC_DAPM_MUX("DAC Right Source", SND_SOC_NOPM, 0, 0,
			 &da9055_dac_r_mux_controls),

	/* AIF input */
	SND_SOC_DAPM_AIF_IN("AIFIN Left", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIFIN Right", "Playback", 0, SND_SOC_NOPM, 0, 0),

	/* DACs */
	SND_SOC_DAPM_DAC("DAC Left", "Playback", DA9055_DAC_L_CTRL, 7, 0),
	SND_SOC_DAPM_DAC("DAC Right", "Playback", DA9055_DAC_R_CTRL, 7, 0),

	/* Output Mixers */
	SND_SOC_DAPM_MIXER("Out Mixer Left", SND_SOC_NOPM, 0, 0,
			   &da9055_dapm_mixoutl_controls[0],
			   ARRAY_SIZE(da9055_dapm_mixoutl_controls)),
	SND_SOC_DAPM_MIXER("Out Mixer Right", SND_SOC_NOPM, 0, 0,
			   &da9055_dapm_mixoutr_controls[0],
			   ARRAY_SIZE(da9055_dapm_mixoutr_controls)),

	/* Output Enable Switches */
	SND_SOC_DAPM_SWITCH("Headphone Left Enable", SND_SOC_NOPM, 0, 0,
			    &da9055_dapm_hp_l_control),
	SND_SOC_DAPM_SWITCH("Headphone Right Enable", SND_SOC_NOPM, 0, 0,
			    &da9055_dapm_hp_r_control),
	SND_SOC_DAPM_SWITCH("Lineout Enable", SND_SOC_NOPM, 0, 0,
			    &da9055_dapm_lineout_control),

	/* Output PGAs */
	SND_SOC_DAPM_PGA("MIXOUT Left", DA9055_MIXOUT_L_CTRL, 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MIXOUT Right", DA9055_MIXOUT_R_CTRL, 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Lineout", DA9055_LINE_CTRL, 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Headphone Left", DA9055_HP_L_CTRL, 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Headphone Right", DA9055_HP_R_CTRL, 7, 0, NULL, 0),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),
	SND_SOC_DAPM_OUTPUT("LINE"),
};

/* DAPM audio route definition */
static const struct snd_soc_dapm_route da9055_audio_map[] = {
	/* Dest       Connecting Widget    source */

	/* Input path */
	{"Mic Left Source", "MIC1_P_N", "MIC1"},
	{"Mic Left Source", "MIC1_P", "MIC1"},
	{"Mic Left Source", "MIC1_N", "MIC1"},
	{"Mic Left Source", "MIC2_L", "MIC2"},

	{"Mic Right Source", "MIC2_R_L", "MIC2"},
	{"Mic Right Source", "MIC2_R", "MIC2"},
	{"Mic Right Source", "MIC2_L", "MIC2"},

	{"Mic Left", NULL, "Mic Left Source"},
	{"Mic Right", NULL, "Mic Right Source"},

	{"Aux Left", NULL, "AUXL"},
	{"Aux Right", NULL, "AUXR"},

	{"In Mixer Left", "Mic Left Switch", "Mic Left"},
	{"In Mixer Left", "Mic Right Switch", "Mic Right"},
	{"In Mixer Left", "Aux Left Switch", "Aux Left"},

	{"In Mixer Right", "Mic Right Switch", "Mic Right"},
	{"In Mixer Right", "Mic Left Switch", "Mic Left"},
	{"In Mixer Right", "Aux Right Switch", "Aux Right"},
	{"In Mixer Right", "Mixin Left Switch", "MIXIN Left"},

	{"MIXIN Left", NULL, "In Mixer Left"},
	{"ADC Left", NULL, "MIXIN Left"},

	{"MIXIN Right", NULL, "In Mixer Right"},
	{"ADC Right", NULL, "MIXIN Right"},

	{"ADC Left", NULL, "AIF"},
	{"ADC Right", NULL, "AIF"},

	/* Output path */
	{"AIFIN Left", NULL, "AIF"},
	{"AIFIN Right", NULL, "AIF"},

	{"DAC Left Source", "ADC output left", "ADC Left"},
	{"DAC Left Source", "ADC output right", "ADC Right"},
	{"DAC Left Source", "AIF input left", "AIFIN Left"},
	{"DAC Left Source", "AIF input right", "AIFIN Right"},

	{"DAC Right Source", "ADC output left", "ADC Left"},
	{"DAC Right Source", "ADC output right", "ADC Right"},
	{"DAC Right Source", "AIF input left", "AIFIN Left"},
	{"DAC Right Source", "AIF input right", "AIFIN Right"},

	{"DAC Left", NULL, "DAC Left Source"},
	{"DAC Right", NULL, "DAC Right Source"},

	{"Out Mixer Left", "Aux Left Switch", "Aux Left"},
	{"Out Mixer Left", "Mixin Left Switch", "MIXIN Left"},
	{"Out Mixer Left", "Mixin Right Switch", "MIXIN Right"},
	{"Out Mixer Left", "Aux Left Invert Switch", "Aux Left"},
	{"Out Mixer Left", "Mixin Left Invert Switch", "MIXIN Left"},
	{"Out Mixer Left", "Mixin Right Invert Switch", "MIXIN Right"},
	{"Out Mixer Left", "DAC Left Switch", "DAC Left"},

	{"Out Mixer Right", "Aux Right Switch", "Aux Right"},
	{"Out Mixer Right", "Mixin Right Switch", "MIXIN Right"},
	{"Out Mixer Right", "Mixin Left Switch", "MIXIN Left"},
	{"Out Mixer Right", "Aux Right Invert Switch", "Aux Right"},
	{"Out Mixer Right", "Mixin Right Invert Switch", "MIXIN Right"},
	{"Out Mixer Right", "Mixin Left Invert Switch", "MIXIN Left"},
	{"Out Mixer Right", "DAC Right Switch", "DAC Right"},

	{"MIXOUT Left", NULL, "Out Mixer Left"},
	{"Headphone Left Enable", "Switch", "MIXOUT Left"},
	{"Headphone Left", NULL, "Headphone Left Enable"},
	{"Headphone Left", NULL, "Charge Pump"},
	{"HPL", NULL, "Headphone Left"},

	{"MIXOUT Right", NULL, "Out Mixer Right"},
	{"Headphone Right Enable", "Switch", "MIXOUT Right"},
	{"Headphone Right", NULL, "Headphone Right Enable"},
	{"Headphone Right", NULL, "Charge Pump"},
	{"HPR", NULL, "Headphone Right"},

	{"MIXOUT Right", NULL, "Out Mixer Right"},
	{"Lineout Enable", "Switch", "MIXOUT Right"},
	{"Lineout", NULL, "Lineout Enable"},
	{"LINE", NULL, "Lineout"},
};

/* Codec private data */
struct da9055_priv {
	struct regmap *regmap;
	unsigned int mclk_rate;
	int master;
	struct da9055_platform_data *pdata;
};

static const struct reg_default da9055_reg_defaults[] = {
	{ 0x21, 0x10 },
	{ 0x22, 0x0A },
	{ 0x23, 0x00 },
	{ 0x24, 0x00 },
	{ 0x25, 0x00 },
	{ 0x26, 0x00 },
	{ 0x27, 0x0C },
	{ 0x28, 0x01 },
	{ 0x29, 0x08 },
	{ 0x2A, 0x32 },
	{ 0x2B, 0x00 },
	{ 0x30, 0x35 },
	{ 0x31, 0x35 },
	{ 0x32, 0x00 },
	{ 0x33, 0x00 },
	{ 0x34, 0x03 },
	{ 0x35, 0x03 },
	{ 0x36, 0x6F },
	{ 0x37, 0x6F },
	{ 0x38, 0x80 },
	{ 0x39, 0x01 },
	{ 0x3A, 0x01 },
	{ 0x40, 0x00 },
	{ 0x41, 0x88 },
	{ 0x42, 0x88 },
	{ 0x43, 0x08 },
	{ 0x44, 0x80 },
	{ 0x45, 0x6F },
	{ 0x46, 0x6F },
	{ 0x47, 0x61 },
	{ 0x48, 0x35 },
	{ 0x49, 0x35 },
	{ 0x4A, 0x35 },
	{ 0x4B, 0x00 },
	{ 0x4C, 0x00 },
	{ 0x60, 0x44 },
	{ 0x61, 0x44 },
	{ 0x62, 0x00 },
	{ 0x63, 0x40 },
	{ 0x64, 0x40 },
	{ 0x65, 0x40 },
	{ 0x66, 0x40 },
	{ 0x67, 0x40 },
	{ 0x68, 0x40 },
	{ 0x69, 0x48 },
	{ 0x6A, 0x40 },
	{ 0x6B, 0x41 },
	{ 0x6C, 0x40 },
	{ 0x6D, 0x40 },
	{ 0x6E, 0x10 },
	{ 0x6F, 0x10 },
	{ 0x90, 0x80 },
	{ 0x92, 0x02 },
	{ 0x93, 0x00 },
	{ 0x99, 0x00 },
	{ 0x9A, 0x00 },
	{ 0x9B, 0x00 },
	{ 0x9C, 0x3F },
	{ 0x9D, 0x00 },
	{ 0x9E, 0x3F },
	{ 0x9F, 0xFF },
	{ 0xA0, 0x71 },
	{ 0xA1, 0x00 },
	{ 0xA2, 0x00 },
	{ 0xA6, 0x00 },
	{ 0xA7, 0x00 },
	{ 0xAB, 0x00 },
	{ 0xAC, 0x00 },
	{ 0xAD, 0x00 },
	{ 0xAF, 0x08 },
	{ 0xB0, 0x00 },
	{ 0xB1, 0x00 },
	{ 0xB2, 0x00 },
};

static bool da9055_volatile_register(struct device *dev,
				     unsigned int reg)
{
	switch (reg) {
	case DA9055_STATUS1:
	case DA9055_PLL_STATUS:
	case DA9055_AUX_L_GAIN_STATUS:
	case DA9055_AUX_R_GAIN_STATUS:
	case DA9055_MIC_L_GAIN_STATUS:
	case DA9055_MIC_R_GAIN_STATUS:
	case DA9055_MIXIN_L_GAIN_STATUS:
	case DA9055_MIXIN_R_GAIN_STATUS:
	case DA9055_ADC_L_GAIN_STATUS:
	case DA9055_ADC_R_GAIN_STATUS:
	case DA9055_DAC_L_GAIN_STATUS:
	case DA9055_DAC_R_GAIN_STATUS:
	case DA9055_HP_L_GAIN_STATUS:
	case DA9055_HP_R_GAIN_STATUS:
	case DA9055_LINE_GAIN_STATUS:
	case DA9055_ALC_CIC_OP_LVL_DATA:
		return true;
	default:
		return false;
	}
}

/* Set DAI word length */
static int da9055_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct da9055_priv *da9055 = snd_soc_component_get_drvdata(component);
	u8 aif_ctrl, fs;
	u32 sysclk;

	switch (params_width(params)) {
	case 16:
		aif_ctrl = DA9055_AIF_WORD_S16_LE;
		break;
	case 20:
		aif_ctrl = DA9055_AIF_WORD_S20_3LE;
		break;
	case 24:
		aif_ctrl = DA9055_AIF_WORD_S24_LE;
		break;
	case 32:
		aif_ctrl = DA9055_AIF_WORD_S32_LE;
		break;
	default:
		return -EINVAL;
	}

	/* Set AIF format */
	snd_soc_component_update_bits(component, DA9055_AIF_CTRL, DA9055_AIF_WORD_LENGTH_MASK,
			    aif_ctrl);

	switch (params_rate(params)) {
	case 8000:
		fs		= DA9055_SR_8000;
		sysclk		= 3072000;
		break;
	case 11025:
		fs		= DA9055_SR_11025;
		sysclk		= 2822400;
		break;
	case 12000:
		fs		= DA9055_SR_12000;
		sysclk		= 3072000;
		break;
	case 16000:
		fs		= DA9055_SR_16000;
		sysclk		= 3072000;
		break;
	case 22050:
		fs		= DA9055_SR_22050;
		sysclk		= 2822400;
		break;
	case 32000:
		fs		= DA9055_SR_32000;
		sysclk		= 3072000;
		break;
	case 44100:
		fs		= DA9055_SR_44100;
		sysclk		= 2822400;
		break;
	case 48000:
		fs		= DA9055_SR_48000;
		sysclk		= 3072000;
		break;
	case 88200:
		fs		= DA9055_SR_88200;
		sysclk		= 2822400;
		break;
	case 96000:
		fs		= DA9055_SR_96000;
		sysclk		= 3072000;
		break;
	default:
		return -EINVAL;
	}

	if (da9055->mclk_rate) {
		/* PLL Mode, Write actual FS */
		snd_soc_component_write(component, DA9055_SR, fs);
	} else {
		/*
		 * Non-PLL Mode
		 * When PLL is bypassed, chip assumes constant MCLK of
		 * 12.288MHz and uses sample rate value to divide this MCLK
		 * to derive its sys clk. As sys clk has to be 256 * Fs, we
		 * need to write constant sample rate i.e. 48KHz.
		 */
		snd_soc_component_write(component, DA9055_SR, DA9055_SR_48000);
	}

	if (da9055->mclk_rate && (da9055->mclk_rate != sysclk)) {
		/* PLL Mode */
		if (!da9055->master) {
			/* PLL slave mode, enable PLL and also SRM */
			snd_soc_component_update_bits(component, DA9055_PLL_CTRL,
					    DA9055_PLL_EN | DA9055_PLL_SRM_EN,
					    DA9055_PLL_EN | DA9055_PLL_SRM_EN);
		} else {
			/* PLL master mode, only enable PLL */
			snd_soc_component_update_bits(component, DA9055_PLL_CTRL,
					    DA9055_PLL_EN, DA9055_PLL_EN);
		}
	} else {
		/* Non PLL Mode, disable PLL */
		snd_soc_component_update_bits(component, DA9055_PLL_CTRL, DA9055_PLL_EN, 0);
	}

	return 0;
}

/* Set DAI mode and Format */
static int da9055_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct da9055_priv *da9055 = snd_soc_component_get_drvdata(component);
	u8 aif_clk_mode, aif_ctrl, mode;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		/* DA9055 in I2S Master Mode */
		mode = 1;
		aif_clk_mode = DA9055_AIF_CLK_EN_MASTER_MODE;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		/* DA9055 in I2S Slave Mode */
		mode = 0;
		aif_clk_mode = DA9055_AIF_CLK_EN_SLAVE_MODE;
		break;
	default:
		return -EINVAL;
	}

	/* Don't allow change of mode if PLL is enabled */
	if ((snd_soc_component_read(component, DA9055_PLL_CTRL) & DA9055_PLL_EN) &&
	    (da9055->master != mode))
		return -EINVAL;

	da9055->master = mode;

	/* Only I2S is supported */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		aif_ctrl = DA9055_AIF_FORMAT_I2S_MODE;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aif_ctrl = DA9055_AIF_FORMAT_LEFT_J;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		aif_ctrl = DA9055_AIF_FORMAT_RIGHT_J;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		aif_ctrl = DA9055_AIF_FORMAT_DSP;
		break;
	default:
		return -EINVAL;
	}

	/* By default only 32 BCLK per WCLK is supported */
	aif_clk_mode |= DA9055_AIF_BCLKS_PER_WCLK_32;

	snd_soc_component_update_bits(component, DA9055_AIF_CLK_MODE,
			    (DA9055_AIF_CLK_MODE_MASK | DA9055_AIF_BCLK_MASK),
			    aif_clk_mode);
	snd_soc_component_update_bits(component, DA9055_AIF_CTRL, DA9055_AIF_FORMAT_MASK,
			    aif_ctrl);
	return 0;
}

static int da9055_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;

	if (mute) {
		snd_soc_component_update_bits(component, DA9055_DAC_L_CTRL,
				    DA9055_DAC_L_MUTE_EN, DA9055_DAC_L_MUTE_EN);
		snd_soc_component_update_bits(component, DA9055_DAC_R_CTRL,
				    DA9055_DAC_R_MUTE_EN, DA9055_DAC_R_MUTE_EN);
	} else {
		snd_soc_component_update_bits(component, DA9055_DAC_L_CTRL,
				    DA9055_DAC_L_MUTE_EN, 0);
		snd_soc_component_update_bits(component, DA9055_DAC_R_CTRL,
				    DA9055_DAC_R_MUTE_EN, 0);
	}

	return 0;
}

#define DA9055_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static int da9055_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct da9055_priv *da9055 = snd_soc_component_get_drvdata(component);

	switch (clk_id) {
	case DA9055_CLKSRC_MCLK:
		switch (freq) {
		case 11289600:
		case 12000000:
		case 12288000:
		case 13000000:
		case 13500000:
		case 14400000:
		case 19200000:
		case 19680000:
		case 19800000:
			da9055->mclk_rate = freq;
			return 0;
		default:
			dev_err(codec_dai->dev, "Unsupported MCLK value %d\n",
				freq);
			return -EINVAL;
		}
		break;
	default:
		dev_err(codec_dai->dev, "Unknown clock source %d\n", clk_id);
		return -EINVAL;
	}
}

/*
 * da9055_set_dai_pll	: Configure the codec PLL
 * @param codec_dai	: Pointer to codec DAI
 * @param pll_id	: da9055 has only one pll, so pll_id is always zero
 * @param fref		: Input MCLK frequency
 * @param fout		: FsDM value
 * @return int		: Zero for success, negative error code for error
 *
 * Note: Supported PLL input frequencies are 11.2896MHz, 12MHz, 12.288MHz,
 *	 13MHz, 13.5MHz, 14.4MHz, 19.2MHz, 19.6MHz and 19.8MHz
 */
static int da9055_set_dai_pll(struct snd_soc_dai *codec_dai, int pll_id,
			      int source, unsigned int fref, unsigned int fout)
{
	struct snd_soc_component *component = codec_dai->component;
	struct da9055_priv *da9055 = snd_soc_component_get_drvdata(component);

	u8 pll_frac_top, pll_frac_bot, pll_integer, cnt;

	/* Disable PLL before setting the divisors */
	snd_soc_component_update_bits(component, DA9055_PLL_CTRL, DA9055_PLL_EN, 0);

	/* In slave mode, there is only one set of divisors */
	if (!da9055->master && (fout != 2822400))
		goto pll_err;

	/* Search pll div array for correct divisors */
	for (cnt = 0; cnt < ARRAY_SIZE(da9055_pll_div); cnt++) {
		/* Check fref, mode  and fout */
		if ((fref == da9055_pll_div[cnt].fref) &&
		    (da9055->master ==  da9055_pll_div[cnt].mode) &&
		    (fout == da9055_pll_div[cnt].fout)) {
			/* All match, pick up divisors */
			pll_frac_top = da9055_pll_div[cnt].frac_top;
			pll_frac_bot = da9055_pll_div[cnt].frac_bot;
			pll_integer = da9055_pll_div[cnt].integer;
			break;
		}
	}
	if (cnt >= ARRAY_SIZE(da9055_pll_div))
		goto pll_err;

	/* Write PLL dividers */
	snd_soc_component_write(component, DA9055_PLL_FRAC_TOP, pll_frac_top);
	snd_soc_component_write(component, DA9055_PLL_FRAC_BOT, pll_frac_bot);
	snd_soc_component_write(component, DA9055_PLL_INTEGER, pll_integer);

	return 0;
pll_err:
	dev_err(codec_dai->dev, "Error in setting up PLL\n");
	return -EINVAL;
}

/* DAI operations */
static const struct snd_soc_dai_ops da9055_dai_ops = {
	.hw_params	= da9055_hw_params,
	.set_fmt	= da9055_set_dai_fmt,
	.set_sysclk	= da9055_set_dai_sysclk,
	.set_pll	= da9055_set_dai_pll,
	.mute_stream	= da9055_mute,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver da9055_dai = {
	.name = "da9055-hifi",
	/* Playback Capabilities */
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = DA9055_FORMATS,
	},
	/* Capture Capabilities */
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = DA9055_FORMATS,
	},
	.ops = &da9055_dai_ops,
	.symmetric_rate = 1,
};

static int da9055_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			/* Enable VMID reference & master bias */
			snd_soc_component_update_bits(component, DA9055_REFERENCES,
					    DA9055_VMID_EN | DA9055_BIAS_EN,
					    DA9055_VMID_EN | DA9055_BIAS_EN);
		}
		break;
	case SND_SOC_BIAS_OFF:
		/* Disable VMID reference & master bias */
		snd_soc_component_update_bits(component, DA9055_REFERENCES,
				    DA9055_VMID_EN | DA9055_BIAS_EN, 0);
		break;
	}
	return 0;
}

static int da9055_probe(struct snd_soc_component *component)
{
	struct da9055_priv *da9055 = snd_soc_component_get_drvdata(component);

	/* Enable all Gain Ramps */
	snd_soc_component_update_bits(component, DA9055_AUX_L_CTRL,
			    DA9055_GAIN_RAMPING_EN, DA9055_GAIN_RAMPING_EN);
	snd_soc_component_update_bits(component, DA9055_AUX_R_CTRL,
			    DA9055_GAIN_RAMPING_EN, DA9055_GAIN_RAMPING_EN);
	snd_soc_component_update_bits(component, DA9055_MIXIN_L_CTRL,
			    DA9055_GAIN_RAMPING_EN, DA9055_GAIN_RAMPING_EN);
	snd_soc_component_update_bits(component, DA9055_MIXIN_R_CTRL,
			    DA9055_GAIN_RAMPING_EN, DA9055_GAIN_RAMPING_EN);
	snd_soc_component_update_bits(component, DA9055_ADC_L_CTRL,
			    DA9055_GAIN_RAMPING_EN, DA9055_GAIN_RAMPING_EN);
	snd_soc_component_update_bits(component, DA9055_ADC_R_CTRL,
			    DA9055_GAIN_RAMPING_EN, DA9055_GAIN_RAMPING_EN);
	snd_soc_component_update_bits(component, DA9055_DAC_L_CTRL,
			    DA9055_GAIN_RAMPING_EN, DA9055_GAIN_RAMPING_EN);
	snd_soc_component_update_bits(component, DA9055_DAC_R_CTRL,
			    DA9055_GAIN_RAMPING_EN, DA9055_GAIN_RAMPING_EN);
	snd_soc_component_update_bits(component, DA9055_HP_L_CTRL,
			    DA9055_GAIN_RAMPING_EN, DA9055_GAIN_RAMPING_EN);
	snd_soc_component_update_bits(component, DA9055_HP_R_CTRL,
			    DA9055_GAIN_RAMPING_EN, DA9055_GAIN_RAMPING_EN);
	snd_soc_component_update_bits(component, DA9055_LINE_CTRL,
			    DA9055_GAIN_RAMPING_EN, DA9055_GAIN_RAMPING_EN);

	/*
	 * There are two separate control bits for input and output mixers.
	 * One to enable corresponding amplifier and other to enable its
	 * output. As amplifier bits are related to power control, they are
	 * being managed by DAPM while other (non power related) bits are
	 * enabled here
	 */
	snd_soc_component_update_bits(component, DA9055_MIXIN_L_CTRL,
			    DA9055_MIXIN_L_MIX_EN, DA9055_MIXIN_L_MIX_EN);
	snd_soc_component_update_bits(component, DA9055_MIXIN_R_CTRL,
			    DA9055_MIXIN_R_MIX_EN, DA9055_MIXIN_R_MIX_EN);

	snd_soc_component_update_bits(component, DA9055_MIXOUT_L_CTRL,
			    DA9055_MIXOUT_L_MIX_EN, DA9055_MIXOUT_L_MIX_EN);
	snd_soc_component_update_bits(component, DA9055_MIXOUT_R_CTRL,
			    DA9055_MIXOUT_R_MIX_EN, DA9055_MIXOUT_R_MIX_EN);

	/* Set this as per your system configuration */
	snd_soc_component_write(component, DA9055_PLL_CTRL, DA9055_PLL_INDIV_10_20_MHZ);

	/* Set platform data values */
	if (da9055->pdata) {
		/* set mic bias source */
		if (da9055->pdata->micbias_source) {
			snd_soc_component_update_bits(component, DA9055_MIXIN_R_SELECT,
					    DA9055_MICBIAS2_EN,
					    DA9055_MICBIAS2_EN);
		} else {
			snd_soc_component_update_bits(component, DA9055_MIXIN_R_SELECT,
					    DA9055_MICBIAS2_EN, 0);
		}
		/* set mic bias voltage */
		switch (da9055->pdata->micbias) {
		case DA9055_MICBIAS_2_2V:
		case DA9055_MICBIAS_2_1V:
		case DA9055_MICBIAS_1_8V:
		case DA9055_MICBIAS_1_6V:
			snd_soc_component_update_bits(component, DA9055_MIC_CONFIG,
					    DA9055_MICBIAS_LEVEL_MASK,
					    (da9055->pdata->micbias) << 4);
			break;
		}
	}
	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_da9055 = {
	.probe			= da9055_probe,
	.set_bias_level		= da9055_set_bias_level,
	.controls		= da9055_snd_controls,
	.num_controls		= ARRAY_SIZE(da9055_snd_controls),
	.dapm_widgets		= da9055_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(da9055_dapm_widgets),
	.dapm_routes		= da9055_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(da9055_audio_map),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config da9055_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.reg_defaults = da9055_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(da9055_reg_defaults),
	.volatile_reg = da9055_volatile_register,
	.cache_type = REGCACHE_RBTREE,
};

static int da9055_i2c_probe(struct i2c_client *i2c)
{
	struct da9055_priv *da9055;
	struct da9055_platform_data *pdata = dev_get_platdata(&i2c->dev);
	int ret;

	da9055 = devm_kzalloc(&i2c->dev, sizeof(struct da9055_priv),
			      GFP_KERNEL);
	if (!da9055)
		return -ENOMEM;

	if (pdata)
		da9055->pdata = pdata;

	i2c_set_clientdata(i2c, da9055);

	da9055->regmap = devm_regmap_init_i2c(i2c, &da9055_regmap_config);
	if (IS_ERR(da9055->regmap)) {
		ret = PTR_ERR(da9055->regmap);
		dev_err(&i2c->dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_da9055, &da9055_dai, 1);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to register da9055 component: %d\n",
			ret);
	}
	return ret;
}

/*
 * DO NOT change the device Ids. The naming is intentionally specific as both
 * the CODEC and PMIC parts of this chip are instantiated separately as I2C
 * devices (both have configurable I2C addresses, and are to all intents and
 * purposes separate). As a result there are specific DA9055 Ids for CODEC
 * and PMIC, which must be different to operate together.
 */
static const struct i2c_device_id da9055_i2c_id[] = {
	{ "da9055-codec", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, da9055_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id da9055_of_match[] = {
	{ .compatible = "dlg,da9055-codec", },
	{ }
};
MODULE_DEVICE_TABLE(of, da9055_of_match);
#endif

/* I2C codec control layer */
static struct i2c_driver da9055_i2c_driver = {
	.driver = {
		.name = "da9055-codec",
		.of_match_table = of_match_ptr(da9055_of_match),
	},
	.probe_new	= da9055_i2c_probe,
	.id_table	= da9055_i2c_id,
};

module_i2c_driver(da9055_i2c_driver);

MODULE_DESCRIPTION("ASoC DA9055 Codec driver");
MODULE_AUTHOR("David Chen, Ashish Chavan");
MODULE_LICENSE("GPL");
