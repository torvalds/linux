/*
 * Analog Devices ADAU1373 Audio Codec drive
 *
 * Copyright 2011 Analog Devices Inc.
 * Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/gcd.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/adau1373.h>

#include "adau1373.h"

struct adau1373_dai {
	unsigned int clk_src;
	unsigned int sysclk;
	bool enable_src;
	bool master;
};

struct adau1373 {
	struct adau1373_dai dais[3];
};

#define ADAU1373_INPUT_MODE	0x00
#define ADAU1373_AINL_CTRL(x)	(0x01 + (x) * 2)
#define ADAU1373_AINR_CTRL(x)	(0x02 + (x) * 2)
#define ADAU1373_LLINE_OUT(x)	(0x9 + (x) * 2)
#define ADAU1373_RLINE_OUT(x)	(0xa + (x) * 2)
#define ADAU1373_LSPK_OUT	0x0d
#define ADAU1373_RSPK_OUT	0x0e
#define ADAU1373_LHP_OUT	0x0f
#define ADAU1373_RHP_OUT	0x10
#define ADAU1373_ADC_GAIN	0x11
#define ADAU1373_LADC_MIXER	0x12
#define ADAU1373_RADC_MIXER	0x13
#define ADAU1373_LLINE1_MIX	0x14
#define ADAU1373_RLINE1_MIX	0x15
#define ADAU1373_LLINE2_MIX	0x16
#define ADAU1373_RLINE2_MIX	0x17
#define ADAU1373_LSPK_MIX	0x18
#define ADAU1373_RSPK_MIX	0x19
#define ADAU1373_LHP_MIX	0x1a
#define ADAU1373_RHP_MIX	0x1b
#define ADAU1373_EP_MIX		0x1c
#define ADAU1373_HP_CTRL	0x1d
#define ADAU1373_HP_CTRL2	0x1e
#define ADAU1373_LS_CTRL	0x1f
#define ADAU1373_EP_CTRL	0x21
#define ADAU1373_MICBIAS_CTRL1	0x22
#define ADAU1373_MICBIAS_CTRL2	0x23
#define ADAU1373_OUTPUT_CTRL	0x24
#define ADAU1373_PWDN_CTRL1	0x25
#define ADAU1373_PWDN_CTRL2	0x26
#define ADAU1373_PWDN_CTRL3	0x27
#define ADAU1373_DPLL_CTRL(x)	(0x28 + (x) * 7)
#define ADAU1373_PLL_CTRL1(x)	(0x29 + (x) * 7)
#define ADAU1373_PLL_CTRL2(x)	(0x2a + (x) * 7)
#define ADAU1373_PLL_CTRL3(x)	(0x2b + (x) * 7)
#define ADAU1373_PLL_CTRL4(x)	(0x2c + (x) * 7)
#define ADAU1373_PLL_CTRL5(x)	(0x2d + (x) * 7)
#define ADAU1373_PLL_CTRL6(x)	(0x2e + (x) * 7)
#define ADAU1373_PLL_CTRL7(x)	(0x2f + (x) * 7)
#define ADAU1373_HEADDECT	0x36
#define ADAU1373_ADC_DAC_STATUS	0x37
#define ADAU1373_ADC_CTRL	0x3c
#define ADAU1373_DAI(x)		(0x44 + (x))
#define ADAU1373_CLK_SRC_DIV(x)	(0x40 + (x) * 2)
#define ADAU1373_BCLKDIV(x)	(0x47 + (x))
#define ADAU1373_SRC_RATIOA(x)	(0x4a + (x) * 2)
#define ADAU1373_SRC_RATIOB(x)	(0x4b + (x) * 2)
#define ADAU1373_DEEMP_CTRL	0x50
#define ADAU1373_SRC_DAI_CTRL(x) (0x51 + (x))
#define ADAU1373_DIN_MIX_CTRL(x) (0x56 + (x))
#define ADAU1373_DOUT_MIX_CTRL(x) (0x5b + (x))
#define ADAU1373_DAI_PBL_VOL(x)	(0x62 + (x) * 2)
#define ADAU1373_DAI_PBR_VOL(x)	(0x63 + (x) * 2)
#define ADAU1373_DAI_RECL_VOL(x) (0x68 + (x) * 2)
#define ADAU1373_DAI_RECR_VOL(x) (0x69 + (x) * 2)
#define ADAU1373_DAC1_PBL_VOL	0x6e
#define ADAU1373_DAC1_PBR_VOL	0x6f
#define ADAU1373_DAC2_PBL_VOL	0x70
#define ADAU1373_DAC2_PBR_VOL	0x71
#define ADAU1373_ADC_RECL_VOL	0x72
#define ADAU1373_ADC_RECR_VOL	0x73
#define ADAU1373_DMIC_RECL_VOL	0x74
#define ADAU1373_DMIC_RECR_VOL	0x75
#define ADAU1373_VOL_GAIN1	0x76
#define ADAU1373_VOL_GAIN2	0x77
#define ADAU1373_VOL_GAIN3	0x78
#define ADAU1373_HPF_CTRL	0x7d
#define ADAU1373_BASS1		0x7e
#define ADAU1373_BASS2		0x7f
#define ADAU1373_DRC(x)		(0x80 + (x) * 0x10)
#define ADAU1373_3D_CTRL1	0xc0
#define ADAU1373_3D_CTRL2	0xc1
#define ADAU1373_FDSP_SEL1	0xdc
#define ADAU1373_FDSP_SEL2	0xdd
#define ADAU1373_FDSP_SEL3	0xde
#define ADAU1373_FDSP_SEL4	0xdf
#define ADAU1373_DIGMICCTRL	0xe2
#define ADAU1373_DIGEN		0xeb
#define ADAU1373_SOFT_RESET	0xff


#define ADAU1373_PLL_CTRL6_DPLL_BYPASS	BIT(1)
#define ADAU1373_PLL_CTRL6_PLL_EN	BIT(0)

#define ADAU1373_DAI_INVERT_BCLK	BIT(7)
#define ADAU1373_DAI_MASTER		BIT(6)
#define ADAU1373_DAI_INVERT_LRCLK	BIT(4)
#define ADAU1373_DAI_WLEN_16		0x0
#define ADAU1373_DAI_WLEN_20		0x4
#define ADAU1373_DAI_WLEN_24		0x8
#define ADAU1373_DAI_WLEN_32		0xc
#define ADAU1373_DAI_WLEN_MASK		0xc
#define ADAU1373_DAI_FORMAT_RIGHT_J	0x0
#define ADAU1373_DAI_FORMAT_LEFT_J	0x1
#define ADAU1373_DAI_FORMAT_I2S		0x2
#define ADAU1373_DAI_FORMAT_DSP		0x3

#define ADAU1373_BCLKDIV_SOURCE		BIT(5)
#define ADAU1373_BCLKDIV_SR_MASK	(0x07 << 2)
#define ADAU1373_BCLKDIV_BCLK_MASK	0x03
#define ADAU1373_BCLKDIV_32		0x03
#define ADAU1373_BCLKDIV_64		0x02
#define ADAU1373_BCLKDIV_128		0x01
#define ADAU1373_BCLKDIV_256		0x00

#define ADAU1373_ADC_CTRL_PEAK_DETECT	BIT(0)
#define ADAU1373_ADC_CTRL_RESET		BIT(1)
#define ADAU1373_ADC_CTRL_RESET_FORCE	BIT(2)

#define ADAU1373_OUTPUT_CTRL_LDIFF	BIT(3)
#define ADAU1373_OUTPUT_CTRL_LNFBEN	BIT(2)

#define ADAU1373_PWDN_CTRL3_PWR_EN BIT(0)

#define ADAU1373_EP_CTRL_MICBIAS1_OFFSET 4
#define ADAU1373_EP_CTRL_MICBIAS2_OFFSET 2

static const uint8_t adau1373_default_regs[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x00 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x10 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x20 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, /* 0x30 */
	0x00, 0x00, 0x00, 0x80, 0x00, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x0a, 0x0a, 0x0a, 0x00, /* 0x40 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, /* 0x50 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x60 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x70 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x78, 0x18, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, /* 0x80 */
	0x00, 0xc0, 0x88, 0x7a, 0xdf, 0x20, 0x00, 0x00,
	0x78, 0x18, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, /* 0x90 */
	0x00, 0xc0, 0x88, 0x7a, 0xdf, 0x20, 0x00, 0x00,
	0x78, 0x18, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, /* 0xa0 */
	0x00, 0xc0, 0x88, 0x7a, 0xdf, 0x20, 0x00, 0x00,
	0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, /* 0xb0 */
	0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xc0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0xd0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, /* 0xe0 */
	0x00, 0x1f, 0x0f, 0x00, 0x00,
};

static const unsigned int adau1373_out_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0, 7, TLV_DB_SCALE_ITEM(-7900, 400, 1),
	8, 15, TLV_DB_SCALE_ITEM(-4700, 300, 0),
	16, 23, TLV_DB_SCALE_ITEM(-2300, 200, 0),
	24, 31, TLV_DB_SCALE_ITEM(-700, 100, 0),
};

static const DECLARE_TLV_DB_MINMAX(adau1373_digital_tlv, -9563, 0);
static const DECLARE_TLV_DB_SCALE(adau1373_in_pga_tlv, -1300, 100, 1);
static const DECLARE_TLV_DB_SCALE(adau1373_ep_tlv, -600, 600, 1);

static const DECLARE_TLV_DB_SCALE(adau1373_input_boost_tlv, 0, 2000, 0);
static const DECLARE_TLV_DB_SCALE(adau1373_gain_boost_tlv, 0, 600, 0);
static const DECLARE_TLV_DB_SCALE(adau1373_speaker_boost_tlv, 1200, 600, 0);

static const char *adau1373_fdsp_sel_text[] = {
	"None",
	"Channel 1",
	"Channel 2",
	"Channel 3",
	"Channel 4",
	"Channel 5",
};

static const SOC_ENUM_SINGLE_DECL(adau1373_drc1_channel_enum,
	ADAU1373_FDSP_SEL1, 4, adau1373_fdsp_sel_text);
static const SOC_ENUM_SINGLE_DECL(adau1373_drc2_channel_enum,
	ADAU1373_FDSP_SEL1, 0, adau1373_fdsp_sel_text);
static const SOC_ENUM_SINGLE_DECL(adau1373_drc3_channel_enum,
	ADAU1373_FDSP_SEL2, 0, adau1373_fdsp_sel_text);
static const SOC_ENUM_SINGLE_DECL(adau1373_hpf_channel_enum,
	ADAU1373_FDSP_SEL3, 0, adau1373_fdsp_sel_text);
static const SOC_ENUM_SINGLE_DECL(adau1373_bass_channel_enum,
	ADAU1373_FDSP_SEL4, 4, adau1373_fdsp_sel_text);

static const char *adau1373_hpf_cutoff_text[] = {
	"3.7Hz", "50Hz", "100Hz", "150Hz", "200Hz", "250Hz", "300Hz", "350Hz",
	"400Hz", "450Hz", "500Hz", "550Hz", "600Hz", "650Hz", "700Hz", "750Hz",
	"800Hz",
};

static const SOC_ENUM_SINGLE_DECL(adau1373_hpf_cutoff_enum,
	ADAU1373_HPF_CTRL, 3, adau1373_hpf_cutoff_text);

static const char *adau1373_bass_lpf_cutoff_text[] = {
	"801Hz", "1001Hz",
};

static const char *adau1373_bass_clip_level_text[] = {
	"0.125", "0.250", "0.370", "0.500", "0.625", "0.750", "0.875",
};

static const unsigned int adau1373_bass_clip_level_values[] = {
	1, 2, 3, 4, 5, 6, 7,
};

static const char *adau1373_bass_hpf_cutoff_text[] = {
	"158Hz", "232Hz", "347Hz", "520Hz",
};

static const unsigned int adau1373_bass_tlv[] = {
	TLV_DB_RANGE_HEAD(3),
	0, 2, TLV_DB_SCALE_ITEM(-600, 600, 1),
	3, 4, TLV_DB_SCALE_ITEM(950, 250, 0),
	5, 7, TLV_DB_SCALE_ITEM(1400, 150, 0),
};

static const SOC_ENUM_SINGLE_DECL(adau1373_bass_lpf_cutoff_enum,
	ADAU1373_BASS1, 5, adau1373_bass_lpf_cutoff_text);

static const SOC_VALUE_ENUM_SINGLE_DECL(adau1373_bass_clip_level_enum,
	ADAU1373_BASS1, 2, 7, adau1373_bass_clip_level_text,
	adau1373_bass_clip_level_values);

static const SOC_ENUM_SINGLE_DECL(adau1373_bass_hpf_cutoff_enum,
	ADAU1373_BASS1, 0, adau1373_bass_hpf_cutoff_text);

static const char *adau1373_3d_level_text[] = {
	"0%", "6.67%", "13.33%", "20%", "26.67%", "33.33%",
	"40%", "46.67%", "53.33%", "60%", "66.67%", "73.33%",
	"80%", "86.67", "99.33%", "100%"
};

static const char *adau1373_3d_cutoff_text[] = {
	"No 3D", "0.03125 fs", "0.04583 fs", "0.075 fs", "0.11458 fs",
	"0.16875 fs", "0.27083 fs"
};

static const SOC_ENUM_SINGLE_DECL(adau1373_3d_level_enum,
	ADAU1373_3D_CTRL1, 4, adau1373_3d_level_text);
static const SOC_ENUM_SINGLE_DECL(adau1373_3d_cutoff_enum,
	ADAU1373_3D_CTRL1, 0, adau1373_3d_cutoff_text);

static const unsigned int adau1373_3d_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 7, TLV_DB_LINEAR_ITEM(-1800, -120),
};

static const char *adau1373_lr_mux_text[] = {
	"Mute",
	"Right Channel (L+R)",
	"Left Channel (L+R)",
	"Stereo",
};

static const SOC_ENUM_SINGLE_DECL(adau1373_lineout1_lr_mux_enum,
	ADAU1373_OUTPUT_CTRL, 4, adau1373_lr_mux_text);
static const SOC_ENUM_SINGLE_DECL(adau1373_lineout2_lr_mux_enum,
	ADAU1373_OUTPUT_CTRL, 6, adau1373_lr_mux_text);
static const SOC_ENUM_SINGLE_DECL(adau1373_speaker_lr_mux_enum,
	ADAU1373_LS_CTRL, 4, adau1373_lr_mux_text);

static const struct snd_kcontrol_new adau1373_controls[] = {
	SOC_DOUBLE_R_TLV("AIF1 Capture Volume", ADAU1373_DAI_RECL_VOL(0),
		ADAU1373_DAI_RECR_VOL(0), 0, 0xff, 1, adau1373_digital_tlv),
	SOC_DOUBLE_R_TLV("AIF2 Capture Volume", ADAU1373_DAI_RECL_VOL(1),
		ADAU1373_DAI_RECR_VOL(1), 0, 0xff, 1, adau1373_digital_tlv),
	SOC_DOUBLE_R_TLV("AIF3 Capture Volume", ADAU1373_DAI_RECL_VOL(2),
		ADAU1373_DAI_RECR_VOL(2), 0, 0xff, 1, adau1373_digital_tlv),

	SOC_DOUBLE_R_TLV("ADC Capture Volume", ADAU1373_ADC_RECL_VOL,
		ADAU1373_ADC_RECR_VOL, 0, 0xff, 1, adau1373_digital_tlv),
	SOC_DOUBLE_R_TLV("DMIC Capture Volume", ADAU1373_DMIC_RECL_VOL,
		ADAU1373_DMIC_RECR_VOL, 0, 0xff, 1, adau1373_digital_tlv),

	SOC_DOUBLE_R_TLV("AIF1 Playback Volume", ADAU1373_DAI_PBL_VOL(0),
		ADAU1373_DAI_PBR_VOL(0), 0, 0xff, 1, adau1373_digital_tlv),
	SOC_DOUBLE_R_TLV("AIF2 Playback Volume", ADAU1373_DAI_PBL_VOL(1),
		ADAU1373_DAI_PBR_VOL(1), 0, 0xff, 1, adau1373_digital_tlv),
	SOC_DOUBLE_R_TLV("AIF3 Playback Volume", ADAU1373_DAI_PBL_VOL(2),
		ADAU1373_DAI_PBR_VOL(2), 0, 0xff, 1, adau1373_digital_tlv),

	SOC_DOUBLE_R_TLV("DAC1 Playback Volume", ADAU1373_DAC1_PBL_VOL,
		ADAU1373_DAC1_PBR_VOL, 0, 0xff, 1, adau1373_digital_tlv),
	SOC_DOUBLE_R_TLV("DAC2 Playback Volume", ADAU1373_DAC2_PBL_VOL,
		ADAU1373_DAC2_PBR_VOL, 0, 0xff, 1, adau1373_digital_tlv),

	SOC_DOUBLE_R_TLV("Lineout1 Playback Volume", ADAU1373_LLINE_OUT(0),
		ADAU1373_RLINE_OUT(0), 0, 0x1f, 0, adau1373_out_tlv),
	SOC_DOUBLE_R_TLV("Speaker Playback Volume", ADAU1373_LSPK_OUT,
		ADAU1373_RSPK_OUT, 0, 0x1f, 0, adau1373_out_tlv),
	SOC_DOUBLE_R_TLV("Headphone Playback Volume", ADAU1373_LHP_OUT,
		ADAU1373_RHP_OUT, 0, 0x1f, 0, adau1373_out_tlv),

	SOC_DOUBLE_R_TLV("Input 1 Capture Volume", ADAU1373_AINL_CTRL(0),
		ADAU1373_AINR_CTRL(0), 0, 0x1f, 0, adau1373_in_pga_tlv),
	SOC_DOUBLE_R_TLV("Input 2 Capture Volume", ADAU1373_AINL_CTRL(1),
		ADAU1373_AINR_CTRL(1), 0, 0x1f, 0, adau1373_in_pga_tlv),
	SOC_DOUBLE_R_TLV("Input 3 Capture Volume", ADAU1373_AINL_CTRL(2),
		ADAU1373_AINR_CTRL(2), 0, 0x1f, 0, adau1373_in_pga_tlv),
	SOC_DOUBLE_R_TLV("Input 4 Capture Volume", ADAU1373_AINL_CTRL(3),
		ADAU1373_AINR_CTRL(3), 0, 0x1f, 0, adau1373_in_pga_tlv),

	SOC_SINGLE_TLV("Earpiece Playback Volume", ADAU1373_EP_CTRL, 0, 3, 0,
		adau1373_ep_tlv),

	SOC_DOUBLE_TLV("AIF3 Boost Playback Volume", ADAU1373_VOL_GAIN1, 4, 5,
		1, 0, adau1373_gain_boost_tlv),
	SOC_DOUBLE_TLV("AIF2 Boost Playback Volume", ADAU1373_VOL_GAIN1, 2, 3,
		1, 0, adau1373_gain_boost_tlv),
	SOC_DOUBLE_TLV("AIF1 Boost Playback Volume", ADAU1373_VOL_GAIN1, 0, 1,
		1, 0, adau1373_gain_boost_tlv),
	SOC_DOUBLE_TLV("AIF3 Boost Capture Volume", ADAU1373_VOL_GAIN2, 4, 5,
		1, 0, adau1373_gain_boost_tlv),
	SOC_DOUBLE_TLV("AIF2 Boost Capture Volume", ADAU1373_VOL_GAIN2, 2, 3,
		1, 0, adau1373_gain_boost_tlv),
	SOC_DOUBLE_TLV("AIF1 Boost Capture Volume", ADAU1373_VOL_GAIN2, 0, 1,
		1, 0, adau1373_gain_boost_tlv),
	SOC_DOUBLE_TLV("DMIC Boost Capture Volume", ADAU1373_VOL_GAIN3, 6, 7,
		1, 0, adau1373_gain_boost_tlv),
	SOC_DOUBLE_TLV("ADC Boost Capture Volume", ADAU1373_VOL_GAIN3, 4, 5,
		1, 0, adau1373_gain_boost_tlv),
	SOC_DOUBLE_TLV("DAC2 Boost Playback Volume", ADAU1373_VOL_GAIN3, 2, 3,
		1, 0, adau1373_gain_boost_tlv),
	SOC_DOUBLE_TLV("DAC1 Boost Playback Volume", ADAU1373_VOL_GAIN3, 0, 1,
		1, 0, adau1373_gain_boost_tlv),

	SOC_DOUBLE_TLV("Input 1 Boost Capture Volume", ADAU1373_ADC_GAIN, 0, 4,
		1, 0, adau1373_input_boost_tlv),
	SOC_DOUBLE_TLV("Input 2 Boost Capture Volume", ADAU1373_ADC_GAIN, 1, 5,
		1, 0, adau1373_input_boost_tlv),
	SOC_DOUBLE_TLV("Input 3 Boost Capture Volume", ADAU1373_ADC_GAIN, 2, 6,
		1, 0, adau1373_input_boost_tlv),
	SOC_DOUBLE_TLV("Input 4 Boost Capture Volume", ADAU1373_ADC_GAIN, 3, 7,
		1, 0, adau1373_input_boost_tlv),

	SOC_DOUBLE_TLV("Speaker Boost Playback Volume", ADAU1373_LS_CTRL, 2, 3,
		1, 0, adau1373_speaker_boost_tlv),

	SOC_ENUM("Lineout1 LR Mux", adau1373_lineout1_lr_mux_enum),
	SOC_ENUM("Speaker LR Mux", adau1373_speaker_lr_mux_enum),

	SOC_ENUM("HPF Cutoff", adau1373_hpf_cutoff_enum),
	SOC_DOUBLE("HPF Switch", ADAU1373_HPF_CTRL, 1, 0, 1, 0),
	SOC_ENUM("HPF Channel", adau1373_hpf_channel_enum),

	SOC_ENUM("Bass HPF Cutoff", adau1373_bass_hpf_cutoff_enum),
	SOC_VALUE_ENUM("Bass Clip Level Threshold",
	    adau1373_bass_clip_level_enum),
	SOC_ENUM("Bass LPF Cutoff", adau1373_bass_lpf_cutoff_enum),
	SOC_DOUBLE("Bass Playback Switch", ADAU1373_BASS2, 0, 1, 1, 0),
	SOC_SINGLE_TLV("Bass Playback Volume", ADAU1373_BASS2, 2, 7, 0,
	    adau1373_bass_tlv),
	SOC_ENUM("Bass Channel", adau1373_bass_channel_enum),

	SOC_ENUM("3D Freq", adau1373_3d_cutoff_enum),
	SOC_ENUM("3D Level", adau1373_3d_level_enum),
	SOC_SINGLE("3D Playback Switch", ADAU1373_3D_CTRL2, 0, 1, 0),
	SOC_SINGLE_TLV("3D Playback Volume", ADAU1373_3D_CTRL2, 2, 7, 0,
		adau1373_3d_tlv),
	SOC_ENUM("3D Channel", adau1373_bass_channel_enum),

	SOC_SINGLE("Zero Cross Switch", ADAU1373_PWDN_CTRL3, 7, 1, 0),
};

static const struct snd_kcontrol_new adau1373_lineout2_controls[] = {
	SOC_DOUBLE_R_TLV("Lineout2 Playback Volume", ADAU1373_LLINE_OUT(1),
		ADAU1373_RLINE_OUT(1), 0, 0x1f, 0, adau1373_out_tlv),
	SOC_ENUM("Lineout2 LR Mux", adau1373_lineout2_lr_mux_enum),
};

static const struct snd_kcontrol_new adau1373_drc_controls[] = {
	SOC_ENUM("DRC1 Channel", adau1373_drc1_channel_enum),
	SOC_ENUM("DRC2 Channel", adau1373_drc2_channel_enum),
	SOC_ENUM("DRC3 Channel", adau1373_drc3_channel_enum),
};

static int adau1373_pll_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	unsigned int pll_id = w->name[3] - '1';
	unsigned int val;

	if (SND_SOC_DAPM_EVENT_ON(event))
		val = ADAU1373_PLL_CTRL6_PLL_EN;
	else
		val = 0;

	snd_soc_update_bits(codec, ADAU1373_PLL_CTRL6(pll_id),
		ADAU1373_PLL_CTRL6_PLL_EN, val);

	if (SND_SOC_DAPM_EVENT_ON(event))
		mdelay(5);

	return 0;
}

static const char *adau1373_decimator_text[] = {
	"ADC",
	"DMIC1",
};

static const struct soc_enum adau1373_decimator_enum =
	SOC_ENUM_SINGLE(0, 0, 2, adau1373_decimator_text);

static const struct snd_kcontrol_new adau1373_decimator_mux =
	SOC_DAPM_ENUM_VIRT("Decimator Mux", adau1373_decimator_enum);

static const struct snd_kcontrol_new adau1373_left_adc_mixer_controls[] = {
	SOC_DAPM_SINGLE("DAC1 Switch", ADAU1373_LADC_MIXER, 4, 1, 0),
	SOC_DAPM_SINGLE("Input 4 Switch", ADAU1373_LADC_MIXER, 3, 1, 0),
	SOC_DAPM_SINGLE("Input 3 Switch", ADAU1373_LADC_MIXER, 2, 1, 0),
	SOC_DAPM_SINGLE("Input 2 Switch", ADAU1373_LADC_MIXER, 1, 1, 0),
	SOC_DAPM_SINGLE("Input 1 Switch", ADAU1373_LADC_MIXER, 0, 1, 0),
};

static const struct snd_kcontrol_new adau1373_right_adc_mixer_controls[] = {
	SOC_DAPM_SINGLE("DAC1 Switch", ADAU1373_RADC_MIXER, 4, 1, 0),
	SOC_DAPM_SINGLE("Input 4 Switch", ADAU1373_RADC_MIXER, 3, 1, 0),
	SOC_DAPM_SINGLE("Input 3 Switch", ADAU1373_RADC_MIXER, 2, 1, 0),
	SOC_DAPM_SINGLE("Input 2 Switch", ADAU1373_RADC_MIXER, 1, 1, 0),
	SOC_DAPM_SINGLE("Input 1 Switch", ADAU1373_RADC_MIXER, 0, 1, 0),
};

#define DECLARE_ADAU1373_OUTPUT_MIXER_CTRLS(_name, _reg) \
const struct snd_kcontrol_new _name[] = { \
	SOC_DAPM_SINGLE("Left DAC2 Switch", _reg, 7, 1, 0), \
	SOC_DAPM_SINGLE("Right DAC2 Switch", _reg, 6, 1, 0), \
	SOC_DAPM_SINGLE("Left DAC1 Switch", _reg, 5, 1, 0), \
	SOC_DAPM_SINGLE("Right DAC1 Switch", _reg, 4, 1, 0), \
	SOC_DAPM_SINGLE("Input 4 Bypass Switch", _reg, 3, 1, 0), \
	SOC_DAPM_SINGLE("Input 3 Bypass Switch", _reg, 2, 1, 0), \
	SOC_DAPM_SINGLE("Input 2 Bypass Switch", _reg, 1, 1, 0), \
	SOC_DAPM_SINGLE("Input 1 Bypass Switch", _reg, 0, 1, 0), \
}

static DECLARE_ADAU1373_OUTPUT_MIXER_CTRLS(adau1373_left_line1_mixer_controls,
	ADAU1373_LLINE1_MIX);
static DECLARE_ADAU1373_OUTPUT_MIXER_CTRLS(adau1373_right_line1_mixer_controls,
	ADAU1373_RLINE1_MIX);
static DECLARE_ADAU1373_OUTPUT_MIXER_CTRLS(adau1373_left_line2_mixer_controls,
	ADAU1373_LLINE2_MIX);
static DECLARE_ADAU1373_OUTPUT_MIXER_CTRLS(adau1373_right_line2_mixer_controls,
	ADAU1373_RLINE2_MIX);
static DECLARE_ADAU1373_OUTPUT_MIXER_CTRLS(adau1373_left_spk_mixer_controls,
	ADAU1373_LSPK_MIX);
static DECLARE_ADAU1373_OUTPUT_MIXER_CTRLS(adau1373_right_spk_mixer_controls,
	ADAU1373_RSPK_MIX);
static DECLARE_ADAU1373_OUTPUT_MIXER_CTRLS(adau1373_ep_mixer_controls,
	ADAU1373_EP_MIX);

static const struct snd_kcontrol_new adau1373_left_hp_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left DAC1 Switch", ADAU1373_LHP_MIX, 5, 1, 0),
	SOC_DAPM_SINGLE("Left DAC2 Switch", ADAU1373_LHP_MIX, 4, 1, 0),
	SOC_DAPM_SINGLE("Input 4 Bypass Switch", ADAU1373_LHP_MIX, 3, 1, 0),
	SOC_DAPM_SINGLE("Input 3 Bypass Switch", ADAU1373_LHP_MIX, 2, 1, 0),
	SOC_DAPM_SINGLE("Input 2 Bypass Switch", ADAU1373_LHP_MIX, 1, 1, 0),
	SOC_DAPM_SINGLE("Input 1 Bypass Switch", ADAU1373_LHP_MIX, 0, 1, 0),
};

static const struct snd_kcontrol_new adau1373_right_hp_mixer_controls[] = {
	SOC_DAPM_SINGLE("Right DAC1 Switch", ADAU1373_RHP_MIX, 5, 1, 0),
	SOC_DAPM_SINGLE("Right DAC2 Switch", ADAU1373_RHP_MIX, 4, 1, 0),
	SOC_DAPM_SINGLE("Input 4 Bypass Switch", ADAU1373_RHP_MIX, 3, 1, 0),
	SOC_DAPM_SINGLE("Input 3 Bypass Switch", ADAU1373_RHP_MIX, 2, 1, 0),
	SOC_DAPM_SINGLE("Input 2 Bypass Switch", ADAU1373_RHP_MIX, 1, 1, 0),
	SOC_DAPM_SINGLE("Input 1 Bypass Switch", ADAU1373_RHP_MIX, 0, 1, 0),
};

#define DECLARE_ADAU1373_DSP_CHANNEL_MIXER_CTRLS(_name, _reg) \
const struct snd_kcontrol_new _name[] = { \
	SOC_DAPM_SINGLE("DMIC2 Swapped Switch", _reg, 6, 1, 0), \
	SOC_DAPM_SINGLE("DMIC2 Switch", _reg, 5, 1, 0), \
	SOC_DAPM_SINGLE("ADC/DMIC1 Swapped Switch", _reg, 4, 1, 0), \
	SOC_DAPM_SINGLE("ADC/DMIC1 Switch", _reg, 3, 1, 0), \
	SOC_DAPM_SINGLE("AIF3 Switch", _reg, 2, 1, 0), \
	SOC_DAPM_SINGLE("AIF2 Switch", _reg, 1, 1, 0), \
	SOC_DAPM_SINGLE("AIF1 Switch", _reg, 0, 1, 0), \
}

static DECLARE_ADAU1373_DSP_CHANNEL_MIXER_CTRLS(adau1373_dsp_channel1_mixer_controls,
	ADAU1373_DIN_MIX_CTRL(0));
static DECLARE_ADAU1373_DSP_CHANNEL_MIXER_CTRLS(adau1373_dsp_channel2_mixer_controls,
	ADAU1373_DIN_MIX_CTRL(1));
static DECLARE_ADAU1373_DSP_CHANNEL_MIXER_CTRLS(adau1373_dsp_channel3_mixer_controls,
	ADAU1373_DIN_MIX_CTRL(2));
static DECLARE_ADAU1373_DSP_CHANNEL_MIXER_CTRLS(adau1373_dsp_channel4_mixer_controls,
	ADAU1373_DIN_MIX_CTRL(3));
static DECLARE_ADAU1373_DSP_CHANNEL_MIXER_CTRLS(adau1373_dsp_channel5_mixer_controls,
	ADAU1373_DIN_MIX_CTRL(4));

#define DECLARE_ADAU1373_DSP_OUTPUT_MIXER_CTRLS(_name, _reg) \
const struct snd_kcontrol_new _name[] = { \
	SOC_DAPM_SINGLE("DSP Channel5 Switch", _reg, 4, 1, 0), \
	SOC_DAPM_SINGLE("DSP Channel4 Switch", _reg, 3, 1, 0), \
	SOC_DAPM_SINGLE("DSP Channel3 Switch", _reg, 2, 1, 0), \
	SOC_DAPM_SINGLE("DSP Channel2 Switch", _reg, 1, 1, 0), \
	SOC_DAPM_SINGLE("DSP Channel1 Switch", _reg, 0, 1, 0), \
}

static DECLARE_ADAU1373_DSP_OUTPUT_MIXER_CTRLS(adau1373_aif1_mixer_controls,
	ADAU1373_DOUT_MIX_CTRL(0));
static DECLARE_ADAU1373_DSP_OUTPUT_MIXER_CTRLS(adau1373_aif2_mixer_controls,
	ADAU1373_DOUT_MIX_CTRL(1));
static DECLARE_ADAU1373_DSP_OUTPUT_MIXER_CTRLS(adau1373_aif3_mixer_controls,
	ADAU1373_DOUT_MIX_CTRL(2));
static DECLARE_ADAU1373_DSP_OUTPUT_MIXER_CTRLS(adau1373_dac1_mixer_controls,
	ADAU1373_DOUT_MIX_CTRL(3));
static DECLARE_ADAU1373_DSP_OUTPUT_MIXER_CTRLS(adau1373_dac2_mixer_controls,
	ADAU1373_DOUT_MIX_CTRL(4));

static const struct snd_soc_dapm_widget adau1373_dapm_widgets[] = {
	/* Datasheet claims Left ADC is bit 6 and Right ADC is bit 7, but that
	 * doesn't seem to be the case. */
	SND_SOC_DAPM_ADC("Left ADC", NULL, ADAU1373_PWDN_CTRL1, 7, 0),
	SND_SOC_DAPM_ADC("Right ADC", NULL, ADAU1373_PWDN_CTRL1, 6, 0),

	SND_SOC_DAPM_ADC("DMIC1", NULL, ADAU1373_DIGMICCTRL, 0, 0),
	SND_SOC_DAPM_ADC("DMIC2", NULL, ADAU1373_DIGMICCTRL, 2, 0),

	SND_SOC_DAPM_VIRT_MUX("Decimator Mux", SND_SOC_NOPM, 0, 0,
		&adau1373_decimator_mux),

	SND_SOC_DAPM_SUPPLY("MICBIAS2", ADAU1373_PWDN_CTRL1, 5, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS1", ADAU1373_PWDN_CTRL1, 4, 0, NULL, 0),

	SND_SOC_DAPM_PGA("IN4PGA", ADAU1373_PWDN_CTRL1, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IN3PGA", ADAU1373_PWDN_CTRL1, 2, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IN2PGA", ADAU1373_PWDN_CTRL1, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IN1PGA", ADAU1373_PWDN_CTRL1, 0, 0, NULL, 0),

	SND_SOC_DAPM_DAC("Left DAC2", NULL, ADAU1373_PWDN_CTRL2, 7, 0),
	SND_SOC_DAPM_DAC("Right DAC2", NULL, ADAU1373_PWDN_CTRL2, 6, 0),
	SND_SOC_DAPM_DAC("Left DAC1", NULL, ADAU1373_PWDN_CTRL2, 5, 0),
	SND_SOC_DAPM_DAC("Right DAC1", NULL, ADAU1373_PWDN_CTRL2, 4, 0),

	SOC_MIXER_ARRAY("Left ADC Mixer", SND_SOC_NOPM, 0, 0,
		adau1373_left_adc_mixer_controls),
	SOC_MIXER_ARRAY("Right ADC Mixer", SND_SOC_NOPM, 0, 0,
		adau1373_right_adc_mixer_controls),

	SOC_MIXER_ARRAY("Left Lineout2 Mixer", ADAU1373_PWDN_CTRL2, 3, 0,
		adau1373_left_line2_mixer_controls),
	SOC_MIXER_ARRAY("Right Lineout2 Mixer", ADAU1373_PWDN_CTRL2, 2, 0,
		adau1373_right_line2_mixer_controls),
	SOC_MIXER_ARRAY("Left Lineout1 Mixer", ADAU1373_PWDN_CTRL2, 1, 0,
		adau1373_left_line1_mixer_controls),
	SOC_MIXER_ARRAY("Right Lineout1 Mixer", ADAU1373_PWDN_CTRL2, 0, 0,
		adau1373_right_line1_mixer_controls),

	SOC_MIXER_ARRAY("Earpiece Mixer", ADAU1373_PWDN_CTRL3, 4, 0,
		adau1373_ep_mixer_controls),
	SOC_MIXER_ARRAY("Left Speaker Mixer", ADAU1373_PWDN_CTRL3, 3, 0,
		adau1373_left_spk_mixer_controls),
	SOC_MIXER_ARRAY("Right Speaker Mixer", ADAU1373_PWDN_CTRL3, 2, 0,
		adau1373_right_spk_mixer_controls),
	SOC_MIXER_ARRAY("Left Headphone Mixer", SND_SOC_NOPM, 0, 0,
		adau1373_left_hp_mixer_controls),
	SOC_MIXER_ARRAY("Right Headphone Mixer", SND_SOC_NOPM, 0, 0,
		adau1373_right_hp_mixer_controls),
	SND_SOC_DAPM_SUPPLY("Headphone Enable", ADAU1373_PWDN_CTRL3, 1, 0,
		NULL, 0),

	SND_SOC_DAPM_SUPPLY("AIF1 CLK", ADAU1373_SRC_DAI_CTRL(0), 0, 0,
	    NULL, 0),
	SND_SOC_DAPM_SUPPLY("AIF2 CLK", ADAU1373_SRC_DAI_CTRL(1), 0, 0,
	    NULL, 0),
	SND_SOC_DAPM_SUPPLY("AIF3 CLK", ADAU1373_SRC_DAI_CTRL(2), 0, 0,
	    NULL, 0),
	SND_SOC_DAPM_SUPPLY("AIF1 IN SRC", ADAU1373_SRC_DAI_CTRL(0), 2, 0,
	    NULL, 0),
	SND_SOC_DAPM_SUPPLY("AIF1 OUT SRC", ADAU1373_SRC_DAI_CTRL(0), 1, 0,
	    NULL, 0),
	SND_SOC_DAPM_SUPPLY("AIF2 IN SRC", ADAU1373_SRC_DAI_CTRL(1), 2, 0,
	    NULL, 0),
	SND_SOC_DAPM_SUPPLY("AIF2 OUT SRC", ADAU1373_SRC_DAI_CTRL(1), 1, 0,
	    NULL, 0),
	SND_SOC_DAPM_SUPPLY("AIF3 IN SRC", ADAU1373_SRC_DAI_CTRL(2), 2, 0,
	    NULL, 0),
	SND_SOC_DAPM_SUPPLY("AIF3 OUT SRC", ADAU1373_SRC_DAI_CTRL(2), 1, 0,
	    NULL, 0),

	SND_SOC_DAPM_AIF_IN("AIF1 IN", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1 OUT", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2 IN", "AIF2 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2 OUT", "AIF2 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF3 IN", "AIF3 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF3 OUT", "AIF3 Capture", 0, SND_SOC_NOPM, 0, 0),

	SOC_MIXER_ARRAY("DSP Channel1 Mixer", SND_SOC_NOPM, 0, 0,
		adau1373_dsp_channel1_mixer_controls),
	SOC_MIXER_ARRAY("DSP Channel2 Mixer", SND_SOC_NOPM, 0, 0,
		adau1373_dsp_channel2_mixer_controls),
	SOC_MIXER_ARRAY("DSP Channel3 Mixer", SND_SOC_NOPM, 0, 0,
		adau1373_dsp_channel3_mixer_controls),
	SOC_MIXER_ARRAY("DSP Channel4 Mixer", SND_SOC_NOPM, 0, 0,
		adau1373_dsp_channel4_mixer_controls),
	SOC_MIXER_ARRAY("DSP Channel5 Mixer", SND_SOC_NOPM, 0, 0,
		adau1373_dsp_channel5_mixer_controls),

	SOC_MIXER_ARRAY("AIF1 Mixer", SND_SOC_NOPM, 0, 0,
		adau1373_aif1_mixer_controls),
	SOC_MIXER_ARRAY("AIF2 Mixer", SND_SOC_NOPM, 0, 0,
		adau1373_aif2_mixer_controls),
	SOC_MIXER_ARRAY("AIF3 Mixer", SND_SOC_NOPM, 0, 0,
		adau1373_aif3_mixer_controls),
	SOC_MIXER_ARRAY("DAC1 Mixer", SND_SOC_NOPM, 0, 0,
		adau1373_dac1_mixer_controls),
	SOC_MIXER_ARRAY("DAC2 Mixer", SND_SOC_NOPM, 0, 0,
		adau1373_dac2_mixer_controls),

	SND_SOC_DAPM_SUPPLY("DSP", ADAU1373_DIGEN, 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Recording Engine B", ADAU1373_DIGEN, 3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Recording Engine A", ADAU1373_DIGEN, 2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Playback Engine B", ADAU1373_DIGEN, 1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Playback Engine A", ADAU1373_DIGEN, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("PLL1", SND_SOC_NOPM, 0, 0, adau1373_pll_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("PLL2", SND_SOC_NOPM, 0, 0, adau1373_pll_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("SYSCLK1", ADAU1373_CLK_SRC_DIV(0), 7, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SYSCLK2", ADAU1373_CLK_SRC_DIV(1), 7, 0, NULL, 0),

	SND_SOC_DAPM_INPUT("AIN1L"),
	SND_SOC_DAPM_INPUT("AIN1R"),
	SND_SOC_DAPM_INPUT("AIN2L"),
	SND_SOC_DAPM_INPUT("AIN2R"),
	SND_SOC_DAPM_INPUT("AIN3L"),
	SND_SOC_DAPM_INPUT("AIN3R"),
	SND_SOC_DAPM_INPUT("AIN4L"),
	SND_SOC_DAPM_INPUT("AIN4R"),

	SND_SOC_DAPM_INPUT("DMIC1DAT"),
	SND_SOC_DAPM_INPUT("DMIC2DAT"),

	SND_SOC_DAPM_OUTPUT("LOUT1L"),
	SND_SOC_DAPM_OUTPUT("LOUT1R"),
	SND_SOC_DAPM_OUTPUT("LOUT2L"),
	SND_SOC_DAPM_OUTPUT("LOUT2R"),
	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),
	SND_SOC_DAPM_OUTPUT("SPKL"),
	SND_SOC_DAPM_OUTPUT("SPKR"),
	SND_SOC_DAPM_OUTPUT("EP"),
};

static int adau1373_check_aif_clk(struct snd_soc_dapm_widget *source,
	struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_codec *codec = source->codec;
	struct adau1373 *adau1373 = snd_soc_codec_get_drvdata(codec);
	unsigned int dai;
	const char *clk;

	dai = sink->name[3] - '1';

	if (!adau1373->dais[dai].master)
		return 0;

	if (adau1373->dais[dai].clk_src == ADAU1373_CLK_SRC_PLL1)
		clk = "SYSCLK1";
	else
		clk = "SYSCLK2";

	return strcmp(source->name, clk) == 0;
}

static int adau1373_check_src(struct snd_soc_dapm_widget *source,
	struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_codec *codec = source->codec;
	struct adau1373 *adau1373 = snd_soc_codec_get_drvdata(codec);
	unsigned int dai;

	dai = sink->name[3] - '1';

	return adau1373->dais[dai].enable_src;
}

#define DSP_CHANNEL_MIXER_ROUTES(_sink) \
	{ _sink, "DMIC2 Swapped Switch", "DMIC2" }, \
	{ _sink, "DMIC2 Switch", "DMIC2" }, \
	{ _sink, "ADC/DMIC1 Swapped Switch", "Decimator Mux" }, \
	{ _sink, "ADC/DMIC1 Switch", "Decimator Mux" }, \
	{ _sink, "AIF1 Switch", "AIF1 IN" }, \
	{ _sink, "AIF2 Switch", "AIF2 IN" }, \
	{ _sink, "AIF3 Switch", "AIF3 IN" }

#define DSP_OUTPUT_MIXER_ROUTES(_sink) \
	{ _sink, "DSP Channel1 Switch", "DSP Channel1 Mixer" }, \
	{ _sink, "DSP Channel2 Switch", "DSP Channel2 Mixer" }, \
	{ _sink, "DSP Channel3 Switch", "DSP Channel3 Mixer" }, \
	{ _sink, "DSP Channel4 Switch", "DSP Channel4 Mixer" }, \
	{ _sink, "DSP Channel5 Switch", "DSP Channel5 Mixer" }

#define LEFT_OUTPUT_MIXER_ROUTES(_sink) \
	{ _sink, "Right DAC2 Switch", "Right DAC2" }, \
	{ _sink, "Left DAC2 Switch", "Left DAC2" }, \
	{ _sink, "Right DAC1 Switch", "Right DAC1" }, \
	{ _sink, "Left DAC1 Switch", "Left DAC1" }, \
	{ _sink, "Input 1 Bypass Switch", "IN1PGA" }, \
	{ _sink, "Input 2 Bypass Switch", "IN2PGA" }, \
	{ _sink, "Input 3 Bypass Switch", "IN3PGA" }, \
	{ _sink, "Input 4 Bypass Switch", "IN4PGA" }

#define RIGHT_OUTPUT_MIXER_ROUTES(_sink) \
	{ _sink, "Right DAC2 Switch", "Right DAC2" }, \
	{ _sink, "Left DAC2 Switch", "Left DAC2" }, \
	{ _sink, "Right DAC1 Switch", "Right DAC1" }, \
	{ _sink, "Left DAC1 Switch", "Left DAC1" }, \
	{ _sink, "Input 1 Bypass Switch", "IN1PGA" }, \
	{ _sink, "Input 2 Bypass Switch", "IN2PGA" }, \
	{ _sink, "Input 3 Bypass Switch", "IN3PGA" }, \
	{ _sink, "Input 4 Bypass Switch", "IN4PGA" }

static const struct snd_soc_dapm_route adau1373_dapm_routes[] = {
	{ "Left ADC Mixer", "DAC1 Switch", "Left DAC1" },
	{ "Left ADC Mixer", "Input 1 Switch", "IN1PGA" },
	{ "Left ADC Mixer", "Input 2 Switch", "IN2PGA" },
	{ "Left ADC Mixer", "Input 3 Switch", "IN3PGA" },
	{ "Left ADC Mixer", "Input 4 Switch", "IN4PGA" },

	{ "Right ADC Mixer", "DAC1 Switch", "Right DAC1" },
	{ "Right ADC Mixer", "Input 1 Switch", "IN1PGA" },
	{ "Right ADC Mixer", "Input 2 Switch", "IN2PGA" },
	{ "Right ADC Mixer", "Input 3 Switch", "IN3PGA" },
	{ "Right ADC Mixer", "Input 4 Switch", "IN4PGA" },

	{ "Left ADC", NULL, "Left ADC Mixer" },
	{ "Right ADC", NULL, "Right ADC Mixer" },

	{ "Decimator Mux", "ADC", "Left ADC" },
	{ "Decimator Mux", "ADC", "Right ADC" },
	{ "Decimator Mux", "DMIC1", "DMIC1" },

	DSP_CHANNEL_MIXER_ROUTES("DSP Channel1 Mixer"),
	DSP_CHANNEL_MIXER_ROUTES("DSP Channel2 Mixer"),
	DSP_CHANNEL_MIXER_ROUTES("DSP Channel3 Mixer"),
	DSP_CHANNEL_MIXER_ROUTES("DSP Channel4 Mixer"),
	DSP_CHANNEL_MIXER_ROUTES("DSP Channel5 Mixer"),

	DSP_OUTPUT_MIXER_ROUTES("AIF1 Mixer"),
	DSP_OUTPUT_MIXER_ROUTES("AIF2 Mixer"),
	DSP_OUTPUT_MIXER_ROUTES("AIF3 Mixer"),
	DSP_OUTPUT_MIXER_ROUTES("DAC1 Mixer"),
	DSP_OUTPUT_MIXER_ROUTES("DAC2 Mixer"),

	{ "AIF1 OUT", NULL, "AIF1 Mixer" },
	{ "AIF2 OUT", NULL, "AIF2 Mixer" },
	{ "AIF3 OUT", NULL, "AIF3 Mixer" },
	{ "Left DAC1", NULL, "DAC1 Mixer" },
	{ "Right DAC1", NULL, "DAC1 Mixer" },
	{ "Left DAC2", NULL, "DAC2 Mixer" },
	{ "Right DAC2", NULL, "DAC2 Mixer" },

	LEFT_OUTPUT_MIXER_ROUTES("Left Lineout1 Mixer"),
	RIGHT_OUTPUT_MIXER_ROUTES("Right Lineout1 Mixer"),
	LEFT_OUTPUT_MIXER_ROUTES("Left Lineout2 Mixer"),
	RIGHT_OUTPUT_MIXER_ROUTES("Right Lineout2 Mixer"),
	LEFT_OUTPUT_MIXER_ROUTES("Left Speaker Mixer"),
	RIGHT_OUTPUT_MIXER_ROUTES("Right Speaker Mixer"),

	{ "Left Headphone Mixer", "Left DAC2 Switch", "Left DAC2" },
	{ "Left Headphone Mixer", "Left DAC1 Switch", "Left DAC1" },
	{ "Left Headphone Mixer", "Input 1 Bypass Switch", "IN1PGA" },
	{ "Left Headphone Mixer", "Input 2 Bypass Switch", "IN2PGA" },
	{ "Left Headphone Mixer", "Input 3 Bypass Switch", "IN3PGA" },
	{ "Left Headphone Mixer", "Input 4 Bypass Switch", "IN4PGA" },
	{ "Right Headphone Mixer", "Right DAC2 Switch", "Right DAC2" },
	{ "Right Headphone Mixer", "Right DAC1 Switch", "Right DAC1" },
	{ "Right Headphone Mixer", "Input 1 Bypass Switch", "IN1PGA" },
	{ "Right Headphone Mixer", "Input 2 Bypass Switch", "IN2PGA" },
	{ "Right Headphone Mixer", "Input 3 Bypass Switch", "IN3PGA" },
	{ "Right Headphone Mixer", "Input 4 Bypass Switch", "IN4PGA" },

	{ "Left Headphone Mixer", NULL, "Headphone Enable" },
	{ "Right Headphone Mixer", NULL, "Headphone Enable" },

	{ "Earpiece Mixer", "Right DAC2 Switch", "Right DAC2" },
	{ "Earpiece Mixer", "Left DAC2 Switch", "Left DAC2" },
	{ "Earpiece Mixer", "Right DAC1 Switch", "Right DAC1" },
	{ "Earpiece Mixer", "Left DAC1 Switch", "Left DAC1" },
	{ "Earpiece Mixer", "Input 1 Bypass Switch", "IN1PGA" },
	{ "Earpiece Mixer", "Input 2 Bypass Switch", "IN2PGA" },
	{ "Earpiece Mixer", "Input 3 Bypass Switch", "IN3PGA" },
	{ "Earpiece Mixer", "Input 4 Bypass Switch", "IN4PGA" },

	{ "LOUT1L", NULL, "Left Lineout1 Mixer" },
	{ "LOUT1R", NULL, "Right Lineout1 Mixer" },
	{ "LOUT2L", NULL, "Left Lineout2 Mixer" },
	{ "LOUT2R", NULL, "Right Lineout2 Mixer" },
	{ "SPKL", NULL, "Left Speaker Mixer" },
	{ "SPKR", NULL, "Right Speaker Mixer" },
	{ "HPL", NULL, "Left Headphone Mixer" },
	{ "HPR", NULL, "Right Headphone Mixer" },
	{ "EP", NULL, "Earpiece Mixer" },

	{ "IN1PGA", NULL, "AIN1L" },
	{ "IN2PGA", NULL, "AIN2L" },
	{ "IN3PGA", NULL, "AIN3L" },
	{ "IN4PGA", NULL, "AIN4L" },
	{ "IN1PGA", NULL, "AIN1R" },
	{ "IN2PGA", NULL, "AIN2R" },
	{ "IN3PGA", NULL, "AIN3R" },
	{ "IN4PGA", NULL, "AIN4R" },

	{ "SYSCLK1", NULL, "PLL1" },
	{ "SYSCLK2", NULL, "PLL2" },

	{ "Left DAC1", NULL, "SYSCLK1" },
	{ "Right DAC1", NULL, "SYSCLK1" },
	{ "Left DAC2", NULL, "SYSCLK1" },
	{ "Right DAC2", NULL, "SYSCLK1" },
	{ "Left ADC", NULL, "SYSCLK1" },
	{ "Right ADC", NULL, "SYSCLK1" },

	{ "DSP", NULL, "SYSCLK1" },

	{ "AIF1 Mixer", NULL, "DSP" },
	{ "AIF2 Mixer", NULL, "DSP" },
	{ "AIF3 Mixer", NULL, "DSP" },
	{ "DAC1 Mixer", NULL, "DSP" },
	{ "DAC2 Mixer", NULL, "DSP" },
	{ "DAC1 Mixer", NULL, "Playback Engine A" },
	{ "DAC2 Mixer", NULL, "Playback Engine B" },
	{ "Left ADC Mixer", NULL, "Recording Engine A" },
	{ "Right ADC Mixer", NULL, "Recording Engine A" },

	{ "AIF1 CLK", NULL, "SYSCLK1", adau1373_check_aif_clk },
	{ "AIF2 CLK", NULL, "SYSCLK1", adau1373_check_aif_clk },
	{ "AIF3 CLK", NULL, "SYSCLK1", adau1373_check_aif_clk },
	{ "AIF1 CLK", NULL, "SYSCLK2", adau1373_check_aif_clk },
	{ "AIF2 CLK", NULL, "SYSCLK2", adau1373_check_aif_clk },
	{ "AIF3 CLK", NULL, "SYSCLK2", adau1373_check_aif_clk },

	{ "AIF1 IN", NULL, "AIF1 CLK" },
	{ "AIF1 OUT", NULL, "AIF1 CLK" },
	{ "AIF2 IN", NULL, "AIF2 CLK" },
	{ "AIF2 OUT", NULL, "AIF2 CLK" },
	{ "AIF3 IN", NULL, "AIF3 CLK" },
	{ "AIF3 OUT", NULL, "AIF3 CLK" },
	{ "AIF1 IN", NULL, "AIF1 IN SRC", adau1373_check_src },
	{ "AIF1 OUT", NULL, "AIF1 OUT SRC", adau1373_check_src },
	{ "AIF2 IN", NULL, "AIF2 IN SRC", adau1373_check_src },
	{ "AIF2 OUT", NULL, "AIF2 OUT SRC", adau1373_check_src },
	{ "AIF3 IN", NULL, "AIF3 IN SRC", adau1373_check_src },
	{ "AIF3 OUT", NULL, "AIF3 OUT SRC", adau1373_check_src },

	{ "DMIC1", NULL, "DMIC1DAT" },
	{ "DMIC1", NULL, "SYSCLK1" },
	{ "DMIC1", NULL, "Recording Engine A" },
	{ "DMIC2", NULL, "DMIC2DAT" },
	{ "DMIC2", NULL, "SYSCLK1" },
	{ "DMIC2", NULL, "Recording Engine B" },
};

static int adau1373_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct adau1373 *adau1373 = snd_soc_codec_get_drvdata(codec);
	struct adau1373_dai *adau1373_dai = &adau1373->dais[dai->id];
	unsigned int div;
	unsigned int freq;
	unsigned int ctrl;

	freq = adau1373_dai->sysclk;

	if (freq % params_rate(params) != 0)
		return -EINVAL;

	switch (freq / params_rate(params)) {
	case 1024: /* sysclk / 256 */
		div = 0;
		break;
	case 1536: /* 2/3 sysclk / 256 */
		div = 1;
		break;
	case 2048: /* 1/2 sysclk / 256 */
		div = 2;
		break;
	case 3072: /* 1/3 sysclk / 256 */
		div = 3;
		break;
	case 4096: /* 1/4 sysclk / 256 */
		div = 4;
		break;
	case 6144: /* 1/6 sysclk / 256 */
		div = 5;
		break;
	case 5632: /* 2/11 sysclk / 256 */
		div = 6;
		break;
	default:
		return -EINVAL;
	}

	adau1373_dai->enable_src = (div != 0);

	snd_soc_update_bits(codec, ADAU1373_BCLKDIV(dai->id),
		ADAU1373_BCLKDIV_SR_MASK | ADAU1373_BCLKDIV_BCLK_MASK,
		(div << 2) | ADAU1373_BCLKDIV_64);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		ctrl = ADAU1373_DAI_WLEN_16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		ctrl = ADAU1373_DAI_WLEN_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		ctrl = ADAU1373_DAI_WLEN_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		ctrl = ADAU1373_DAI_WLEN_32;
		break;
	default:
		return -EINVAL;
	}

	return snd_soc_update_bits(codec, ADAU1373_DAI(dai->id),
			ADAU1373_DAI_WLEN_MASK, ctrl);
}

static int adau1373_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct adau1373 *adau1373 = snd_soc_codec_get_drvdata(codec);
	struct adau1373_dai *adau1373_dai = &adau1373->dais[dai->id];
	unsigned int ctrl;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		ctrl = ADAU1373_DAI_MASTER;
		adau1373_dai->master = true;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		ctrl = 0;
		adau1373_dai->master = false;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		ctrl |= ADAU1373_DAI_FORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ctrl |= ADAU1373_DAI_FORMAT_LEFT_J;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		ctrl |= ADAU1373_DAI_FORMAT_RIGHT_J;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		ctrl |= ADAU1373_DAI_FORMAT_DSP;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		ctrl |= ADAU1373_DAI_INVERT_BCLK;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		ctrl |= ADAU1373_DAI_INVERT_LRCLK;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		ctrl |= ADAU1373_DAI_INVERT_LRCLK | ADAU1373_DAI_INVERT_BCLK;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, ADAU1373_DAI(dai->id),
		~ADAU1373_DAI_WLEN_MASK, ctrl);

	return 0;
}

static int adau1373_set_dai_sysclk(struct snd_soc_dai *dai,
	int clk_id, unsigned int freq, int dir)
{
	struct adau1373 *adau1373 = snd_soc_codec_get_drvdata(dai->codec);
	struct adau1373_dai *adau1373_dai = &adau1373->dais[dai->id];

	switch (clk_id) {
	case ADAU1373_CLK_SRC_PLL1:
	case ADAU1373_CLK_SRC_PLL2:
		break;
	default:
		return -EINVAL;
	}

	adau1373_dai->sysclk = freq;
	adau1373_dai->clk_src = clk_id;

	snd_soc_update_bits(dai->codec, ADAU1373_BCLKDIV(dai->id),
		ADAU1373_BCLKDIV_SOURCE, clk_id << 5);

	return 0;
}

static const struct snd_soc_dai_ops adau1373_dai_ops = {
	.hw_params	= adau1373_hw_params,
	.set_sysclk	= adau1373_set_dai_sysclk,
	.set_fmt	= adau1373_set_dai_fmt,
};

#define ADAU1373_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver adau1373_dai_driver[] = {
	{
		.id = 0,
		.name = "adau1373-aif1",
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = ADAU1373_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = ADAU1373_FORMATS,
		},
		.ops = &adau1373_dai_ops,
		.symmetric_rates = 1,
	},
	{
		.id = 1,
		.name = "adau1373-aif2",
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = ADAU1373_FORMATS,
		},
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = ADAU1373_FORMATS,
		},
		.ops = &adau1373_dai_ops,
		.symmetric_rates = 1,
	},
	{
		.id = 2,
		.name = "adau1373-aif3",
		.playback = {
			.stream_name = "AIF3 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = ADAU1373_FORMATS,
		},
		.capture = {
			.stream_name = "AIF3 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = ADAU1373_FORMATS,
		},
		.ops = &adau1373_dai_ops,
		.symmetric_rates = 1,
	},
};

static int adau1373_set_pll(struct snd_soc_codec *codec, int pll_id,
	int source, unsigned int freq_in, unsigned int freq_out)
{
	unsigned int dpll_div = 0;
	unsigned int x, r, n, m, i, j, mode;

	switch (pll_id) {
	case ADAU1373_PLL1:
	case ADAU1373_PLL2:
		break;
	default:
		return -EINVAL;
	}

	switch (source) {
	case ADAU1373_PLL_SRC_BCLK1:
	case ADAU1373_PLL_SRC_BCLK2:
	case ADAU1373_PLL_SRC_BCLK3:
	case ADAU1373_PLL_SRC_LRCLK1:
	case ADAU1373_PLL_SRC_LRCLK2:
	case ADAU1373_PLL_SRC_LRCLK3:
	case ADAU1373_PLL_SRC_MCLK1:
	case ADAU1373_PLL_SRC_MCLK2:
	case ADAU1373_PLL_SRC_GPIO1:
	case ADAU1373_PLL_SRC_GPIO2:
	case ADAU1373_PLL_SRC_GPIO3:
	case ADAU1373_PLL_SRC_GPIO4:
		break;
	default:
		return -EINVAL;
	}

	if (freq_in < 7813 || freq_in > 27000000)
		return -EINVAL;

	if (freq_out < 45158000 || freq_out > 49152000)
		return -EINVAL;

	/* APLL input needs to be >= 8Mhz, so in case freq_in is less we use the
	 * DPLL to get it there. DPLL_out = (DPLL_in / div) * 1024 */
	while (freq_in < 8000000) {
		freq_in *= 2;
		dpll_div++;
	}

	if (freq_out % freq_in != 0) {
		/* fout = fin * (r + (n/m)) / x */
		x = DIV_ROUND_UP(freq_in, 13500000);
		freq_in /= x;
		r = freq_out / freq_in;
		i = freq_out % freq_in;
		j = gcd(i, freq_in);
		n = i / j;
		m = freq_in / j;
		x--;
		mode = 1;
	} else {
		/* fout = fin / r */
		r = freq_out / freq_in;
		n = 0;
		m = 0;
		x = 0;
		mode = 0;
	}

	if (r < 2 || r > 8 || x > 3 || m > 0xffff || n > 0xffff)
		return -EINVAL;

	if (dpll_div) {
		dpll_div = 11 - dpll_div;
		snd_soc_update_bits(codec, ADAU1373_PLL_CTRL6(pll_id),
			ADAU1373_PLL_CTRL6_DPLL_BYPASS, 0);
	} else {
		snd_soc_update_bits(codec, ADAU1373_PLL_CTRL6(pll_id),
			ADAU1373_PLL_CTRL6_DPLL_BYPASS,
			ADAU1373_PLL_CTRL6_DPLL_BYPASS);
	}

	snd_soc_write(codec, ADAU1373_DPLL_CTRL(pll_id),
		(source << 4) | dpll_div);
	snd_soc_write(codec, ADAU1373_PLL_CTRL1(pll_id), (m >> 8) & 0xff);
	snd_soc_write(codec, ADAU1373_PLL_CTRL2(pll_id), m & 0xff);
	snd_soc_write(codec, ADAU1373_PLL_CTRL3(pll_id), (n >> 8) & 0xff);
	snd_soc_write(codec, ADAU1373_PLL_CTRL4(pll_id), n & 0xff);
	snd_soc_write(codec, ADAU1373_PLL_CTRL5(pll_id),
		(r << 3) | (x << 1) | mode);

	/* Set sysclk to pll_rate / 4 */
	snd_soc_update_bits(codec, ADAU1373_CLK_SRC_DIV(pll_id), 0x3f, 0x09);

	return 0;
}

static void adau1373_load_drc_settings(struct snd_soc_codec *codec,
	unsigned int nr, uint8_t *drc)
{
	unsigned int i;

	for (i = 0; i < ADAU1373_DRC_SIZE; ++i)
		snd_soc_write(codec, ADAU1373_DRC(nr) + i, drc[i]);
}

static bool adau1373_valid_micbias(enum adau1373_micbias_voltage micbias)
{
	switch (micbias) {
	case ADAU1373_MICBIAS_2_9V:
	case ADAU1373_MICBIAS_2_2V:
	case ADAU1373_MICBIAS_2_6V:
	case ADAU1373_MICBIAS_1_8V:
		return true;
	default:
		break;
	}
	return false;
}

static int adau1373_probe(struct snd_soc_codec *codec)
{
	struct adau1373_platform_data *pdata = codec->dev->platform_data;
	bool lineout_differential = false;
	unsigned int val;
	int ret;
	int i;

	ret = snd_soc_codec_set_cache_io(codec, 8, 8, SND_SOC_I2C);
	if (ret) {
		dev_err(codec->dev, "failed to set cache I/O: %d\n", ret);
		return ret;
	}

	if (pdata) {
		if (pdata->num_drc > ARRAY_SIZE(pdata->drc_setting))
			return -EINVAL;

		if (!adau1373_valid_micbias(pdata->micbias1) ||
			!adau1373_valid_micbias(pdata->micbias2))
			return -EINVAL;

		for (i = 0; i < pdata->num_drc; ++i) {
			adau1373_load_drc_settings(codec, i,
				pdata->drc_setting[i]);
		}

		snd_soc_add_codec_controls(codec, adau1373_drc_controls,
			pdata->num_drc);

		val = 0;
		for (i = 0; i < 4; ++i) {
			if (pdata->input_differential[i])
				val |= BIT(i);
		}
		snd_soc_write(codec, ADAU1373_INPUT_MODE, val);

		val = 0;
		if (pdata->lineout_differential)
			val |= ADAU1373_OUTPUT_CTRL_LDIFF;
		if (pdata->lineout_ground_sense)
			val |= ADAU1373_OUTPUT_CTRL_LNFBEN;
		snd_soc_write(codec, ADAU1373_OUTPUT_CTRL, val);

		lineout_differential = pdata->lineout_differential;

		snd_soc_write(codec, ADAU1373_EP_CTRL,
			(pdata->micbias1 << ADAU1373_EP_CTRL_MICBIAS1_OFFSET) |
			(pdata->micbias2 << ADAU1373_EP_CTRL_MICBIAS2_OFFSET));
	}

	if (!lineout_differential) {
		snd_soc_add_codec_controls(codec, adau1373_lineout2_controls,
			ARRAY_SIZE(adau1373_lineout2_controls));
	}

	snd_soc_write(codec, ADAU1373_ADC_CTRL,
	    ADAU1373_ADC_CTRL_RESET_FORCE | ADAU1373_ADC_CTRL_PEAK_DETECT);

	return 0;
}

static int adau1373_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		snd_soc_update_bits(codec, ADAU1373_PWDN_CTRL3,
			ADAU1373_PWDN_CTRL3_PWR_EN, ADAU1373_PWDN_CTRL3_PWR_EN);
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_update_bits(codec, ADAU1373_PWDN_CTRL3,
			ADAU1373_PWDN_CTRL3_PWR_EN, 0);
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

static int adau1373_remove(struct snd_soc_codec *codec)
{
	adau1373_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int adau1373_suspend(struct snd_soc_codec *codec)
{
	return adau1373_set_bias_level(codec, SND_SOC_BIAS_OFF);
}

static int adau1373_resume(struct snd_soc_codec *codec)
{
	adau1373_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	snd_soc_cache_sync(codec);

	return 0;
}

static struct snd_soc_codec_driver adau1373_codec_driver = {
	.probe =	adau1373_probe,
	.remove =	adau1373_remove,
	.suspend =	adau1373_suspend,
	.resume =	adau1373_resume,
	.set_bias_level = adau1373_set_bias_level,
	.idle_bias_off = true,
	.reg_cache_size = ARRAY_SIZE(adau1373_default_regs),
	.reg_cache_default = adau1373_default_regs,
	.reg_word_size = sizeof(uint8_t),

	.set_pll = adau1373_set_pll,

	.controls = adau1373_controls,
	.num_controls = ARRAY_SIZE(adau1373_controls),
	.dapm_widgets = adau1373_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(adau1373_dapm_widgets),
	.dapm_routes = adau1373_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(adau1373_dapm_routes),
};

static int adau1373_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct adau1373 *adau1373;
	int ret;

	adau1373 = devm_kzalloc(&client->dev, sizeof(*adau1373), GFP_KERNEL);
	if (!adau1373)
		return -ENOMEM;

	dev_set_drvdata(&client->dev, adau1373);

	ret = snd_soc_register_codec(&client->dev, &adau1373_codec_driver,
			adau1373_dai_driver, ARRAY_SIZE(adau1373_dai_driver));
	return ret;
}

static int adau1373_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id adau1373_i2c_id[] = {
	{ "adau1373", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adau1373_i2c_id);

static struct i2c_driver adau1373_i2c_driver = {
	.driver = {
		.name = "adau1373",
		.owner = THIS_MODULE,
	},
	.probe = adau1373_i2c_probe,
	.remove = adau1373_i2c_remove,
	.id_table = adau1373_i2c_id,
};

module_i2c_driver(adau1373_i2c_driver);

MODULE_DESCRIPTION("ASoC ADAU1373 driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
