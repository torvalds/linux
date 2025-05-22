/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Cirrus Logic CS48L32 audio DSP.
 *
 * Copyright (C) 2016-2018, 2020, 2022, 2025 Cirrus Logic, Inc. and
 *               Cirrus Logic International Semiconductor Ltd.
 */
#ifndef SND_SOC_CS48L32_H
#define SND_SOC_CS48L32_H

#include <linux/bits.h>
#include <sound/soc.h>
#include "wm_adsp.h"

#define CS48L32_SILICON_ID	0x48a32

#define CS48L32_32K_MCLK1		0

#define CS48L32_SFT_RESET_MAGIC		0x5a000000
#define CS48L32_SOFT_RESET_US		2000
#define CS48L32_HARD_RESET_MIN_US	1000

#define CS48L32_SEEN_BOOT_DONE		BIT(0)
#define CS48L32_BOOT_TIMEOUT_US		25000

#define CS48L32_ASP_ENABLES1			0x00
#define CS48L32_ASP_CONTROL1			0x04
#define CS48L32_ASP_CONTROL2			0x08
#define CS48L32_ASP_CONTROL3			0x0c
#define CS48L32_ASP_FRAME_CONTROL1		0x10
#define CS48L32_ASP_FRAME_CONTROL2		0x14
#define CS48L32_ASP_FRAME_CONTROL5		0x20
#define CS48L32_ASP_FRAME_CONTROL6		0x24
#define CS48L32_ASP_DATA_CONTROL1		0x30
#define CS48L32_ASP_DATA_CONTROL5		0x40
#define CS48L32_SYSCLK_RATE_6MHZ		0
#define CS48L32_SYSCLK_RATE_12MHZ		1
#define CS48L32_SYSCLK_RATE_24MHZ		2
#define CS48L32_SYSCLK_RATE_49MHZ		3
#define CS48L32_SYSCLK_RATE_98MHZ		4
#define CS48L32_FLLHJ_INT_MAX_N			1023
#define CS48L32_FLLHJ_INT_MIN_N			1
#define CS48L32_FLLHJ_FRAC_MAX_N		255
#define CS48L32_FLLHJ_FRAC_MIN_N		2
#define CS48L32_FLLHJ_LP_INT_MODE_THRESH	100000
#define CS48L32_FLLHJ_LOW_THRESH		192000
#define CS48L32_FLLHJ_MID_THRESH		1152000
#define CS48L32_FLLHJ_MAX_THRESH		13000000
#define CS48L32_FLLHJ_LOW_GAINS			0x23f0
#define CS48L32_FLLHJ_MID_GAINS			0x22f2
#define CS48L32_FLLHJ_HIGH_GAINS		0x21f0
#define CS48L32_FLL_MAX_FOUT			50000000
#define CS48L32_FLL_MAX_REFDIV			8
#define CS48L32_FLL_CONTROL1_OFFS		0x00
#define CS48L32_FLL_CONTROL2_OFFS		0x04
#define CS48L32_FLL_CONTROL3_OFFS		0x08
#define CS48L32_FLL_CONTROL4_OFFS		0x0c
#define CS48L32_FLL_CONTROL5_OFFS		0x10
#define CS48L32_FLL_CONTROL6_OFFS		0x14
#define CS48L32_FLL_DIGITAL_TEST2_OFFS		0x34
#define CS48L32_FLL_GPIO_CLOCK_OFFS		0xa0
#define CS48L32_DSP_CLOCK_FREQ_OFFS		0x00000
#define CS48L32_ASP_FMT_DSP_MODE_A		0
#define CS48L32_ASP_FMT_DSP_MODE_B		1
#define CS48L32_ASP_FMT_I2S_MODE		2
#define CS48L32_ASP_FMT_LEFT_JUSTIFIED_MODE	3
#define CS48L32_HALO_SAMPLE_RATE_RX1		0x00080
#define CS48L32_HALO_SAMPLE_RATE_TX1		0x00280
#define CS48L32_HALO_DSP_RATE_MASK		0x1f

#define CS48L32_PDMCLK_SRC_IN1_PDMCLK		0x0
#define CS48L32_PDMCLK_SRC_IN2_PDMCLK		0x1
#define CS48L32_PDMCLK_SRC_IN3_PDMCLK		0x2
#define CS48L32_PDMCLK_SRC_IN4_PDMCLK		0x3
#define CS48L32_PDMCLK_SRC_AUXPDM1_CLK		0x8
#define CS48L32_PDMCLK_SRC_AUXPDM2_CLK		0x9

#define CS48L32_MAX_DAI				6
#define CS48L32_MAX_INPUT			4
#define CS48L32_MAX_ANALOG_INPUT		2
#define CS48L32_MAX_IN_MUX_WAYS			2
#define CS48L32_MAX_ASP				2

#define CS48L32_EQ_BLOCK_SZ			60
#define CS48L32_N_EQ_BLOCKS			4

#define CS48L32_DSP_N_RX_CHANNELS		8
#define CS48L32_DSP_N_TX_CHANNELS		8

#define CS48L32_LHPF_MAX_COEFF			4095
#define CS48L32_EQ_MAX_COEFF			4095

#define CS48L32_MIXER_CONTROLS(name, base) \
	SOC_SINGLE_RANGE_TLV(name " Input 1 Volume", base,		\
			     CS48L32_MIXER_VOL_SHIFT, 0x20, 0x50, 0,	\
			     cs48l32_mixer_tlv),				\
	SOC_SINGLE_RANGE_TLV(name " Input 2 Volume", base + 4,		\
			     CS48L32_MIXER_VOL_SHIFT, 0x20, 0x50, 0,	\
			     cs48l32_mixer_tlv),				\
	SOC_SINGLE_RANGE_TLV(name " Input 3 Volume", base + 8,		\
			     CS48L32_MIXER_VOL_SHIFT, 0x20, 0x50, 0,	\
			     cs48l32_mixer_tlv),				\
	SOC_SINGLE_RANGE_TLV(name " Input 4 Volume", base + 12,		\
			     CS48L32_MIXER_VOL_SHIFT, 0x20, 0x50, 0,	\
			     cs48l32_mixer_tlv)

#define CS48L32_MUX_ENUM_DECL(name, reg) \
	SOC_VALUE_ENUM_SINGLE_DECL( \
		name, reg, 0, CS48L32_MIXER_SRC_MASK, \
		cs48l32_mixer_texts, cs48l32_mixer_values)

#define CS48L32_MUX_CTL_DECL(name) \
	const struct snd_kcontrol_new name##_mux = SOC_DAPM_ENUM("Route", name##_enum)

#define CS48L32_MUX_ENUMS(name, base_reg) \
	static CS48L32_MUX_ENUM_DECL(name##_enum, base_reg);	\
	static CS48L32_MUX_CTL_DECL(name)

#define CS48L32_MIXER_ENUMS(name, base_reg) \
	CS48L32_MUX_ENUMS(name##_in1, base_reg);     \
	CS48L32_MUX_ENUMS(name##_in2, base_reg + 4); \
	CS48L32_MUX_ENUMS(name##_in3, base_reg + 8); \
	CS48L32_MUX_ENUMS(name##_in4, base_reg + 12)

#define CS48L32_MUX(name, ctrl) SND_SOC_DAPM_MUX(name, SND_SOC_NOPM, 0, 0, ctrl)

#define CS48L32_MUX_WIDGETS(name, name_str) CS48L32_MUX(name_str " Input 1", &name##_mux)

#define CS48L32_MIXER_WIDGETS(name, name_str)	\
	CS48L32_MUX(name_str " Input 1", &name##_in1_mux), \
	CS48L32_MUX(name_str " Input 2", &name##_in2_mux), \
	CS48L32_MUX(name_str " Input 3", &name##_in3_mux), \
	CS48L32_MUX(name_str " Input 4", &name##_in4_mux), \
	SND_SOC_DAPM_MIXER(name_str " Mixer", SND_SOC_NOPM, 0, 0, NULL, 0)

#define CS48L32_MUX_ROUTES(widget, name) \
	{ widget, NULL, name " Input 1" }, \
	CS48L32_MIXER_INPUT_ROUTES(name " Input 1")

#define CS48L32_MIXER_ROUTES(widget, name)		\
	{ widget, NULL, name " Mixer" },		\
	{ name " Mixer", NULL, name " Input 1" },	\
	{ name " Mixer", NULL, name " Input 2" },	\
	{ name " Mixer", NULL, name " Input 3" },	\
	{ name " Mixer", NULL, name " Input 4" },	\
	CS48L32_MIXER_INPUT_ROUTES(name " Input 1"),	\
	CS48L32_MIXER_INPUT_ROUTES(name " Input 2"),	\
	CS48L32_MIXER_INPUT_ROUTES(name " Input 3"),	\
	CS48L32_MIXER_INPUT_ROUTES(name " Input 4")

#define CS48L32_DSP_ROUTES_1_8_SYSCLK(name)		\
	{ name, NULL, name " Preloader" },		\
	{ name, NULL, "SYSCLK" },		\
	{ name " Preload", NULL, name " Preloader" },	\
	CS48L32_MIXER_ROUTES(name, name "RX1"),		\
	CS48L32_MIXER_ROUTES(name, name "RX2"),		\
	CS48L32_MIXER_ROUTES(name, name "RX3"),		\
	CS48L32_MIXER_ROUTES(name, name "RX4"),		\
	CS48L32_MIXER_ROUTES(name, name "RX5"),		\
	CS48L32_MIXER_ROUTES(name, name "RX6"),		\
	CS48L32_MIXER_ROUTES(name, name "RX7"),		\
	CS48L32_MIXER_ROUTES(name, name "RX8")		\

#define CS48L32_DSP_ROUTES_1_8(name)			\
	{ name, NULL, "DSPCLK" },		\
	CS48L32_DSP_ROUTES_1_8_SYSCLK(name)		\

#define CS48L32_RATE_CONTROL(name, domain) SOC_ENUM(name, cs48l32_sample_rate[(domain) - 1])

#define CS48L32_RATE_ENUM(name, enum) \
	SOC_ENUM_EXT(name, enum, snd_soc_get_enum_double, cs48l32_rate_put)

#define CS48L32_DSP_RATE_CONTROL(name, num)			\
	SOC_ENUM_EXT(name " Rate", cs48l32_dsp_rate_enum[num],	\
		     cs48l32_dsp_rate_get, cs48l32_dsp_rate_put)

#define CS48L32_EQ_COEFF_CONTROL(xname, xreg, xbase, xshift)	\
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,	\
	.info = cs48l32_eq_coeff_info, .get = cs48l32_eq_coeff_get,	\
	.put = cs48l32_eq_coeff_put, .private_value =		\
	(unsigned long)&(struct cs48l32_eq_control) { .reg = xreg,\
	.shift = xshift, .block_base = xbase, .max = 65535 } }

#define CS48L32_EQ_REG_NAME_PASTER(eq, band, type) \
	CS48L32_ ## eq ## _ ## band ## _ ## type

#define CS48L32_EQ_BAND_COEFF_CONTROLS(name, band)		\
	CS48L32_EQ_COEFF_CONTROL(#name " " #band " A",		\
		CS48L32_EQ_REG_NAME_PASTER(name, band, COEFF1),	\
		CS48L32_EQ_REG_NAME_PASTER(name, BAND1, COEFF1),	\
		0),				\
	CS48L32_EQ_COEFF_CONTROL(#name " " #band " B",		\
		CS48L32_EQ_REG_NAME_PASTER(name, band, COEFF1),	\
		CS48L32_EQ_REG_NAME_PASTER(name, BAND1, COEFF1),	\
		16),				\
	CS48L32_EQ_COEFF_CONTROL(#name " " #band " C",		\
		CS48L32_EQ_REG_NAME_PASTER(name, band, COEFF2),	\
		CS48L32_EQ_REG_NAME_PASTER(name, BAND1, COEFF1),	\
		0),				\
	CS48L32_EQ_COEFF_CONTROL(#name " " #band " PG",		\
		CS48L32_EQ_REG_NAME_PASTER(name, band, PG),	\
		CS48L32_EQ_REG_NAME_PASTER(name, BAND1, COEFF1),	\
		0)

#define CS48L32_EQ_COEFF_CONTROLS(name)				\
	CS48L32_EQ_BAND_COEFF_CONTROLS(name, BAND1),		\
	CS48L32_EQ_BAND_COEFF_CONTROLS(name, BAND2),		\
	CS48L32_EQ_BAND_COEFF_CONTROLS(name, BAND3),		\
	CS48L32_EQ_BAND_COEFF_CONTROLS(name, BAND4),		\
	CS48L32_EQ_COEFF_CONTROL(#name " BAND5 A",		\
		CS48L32_EQ_REG_NAME_PASTER(name, BAND5, COEFF1),	\
		CS48L32_EQ_REG_NAME_PASTER(name, BAND1, COEFF1),	\
		0),				\
	CS48L32_EQ_COEFF_CONTROL(#name " BAND5 B",		\
		CS48L32_EQ_REG_NAME_PASTER(name, BAND5, COEFF1),	\
		CS48L32_EQ_REG_NAME_PASTER(name, BAND1, COEFF1),	\
		16),				\
	CS48L32_EQ_COEFF_CONTROL(#name " BAND5 PG",		\
		CS48L32_EQ_REG_NAME_PASTER(name, BAND5, PG),	\
		CS48L32_EQ_REG_NAME_PASTER(name, BAND1, COEFF1),	\
		0)

#define CS48L32_LHPF_CONTROL(xname, xbase)			\
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,	\
	.info = snd_soc_bytes_info, .get = snd_soc_bytes_get,	\
	.put = cs48l32_lhpf_coeff_put, .private_value =		\
	((unsigned long)&(struct soc_bytes) { .base = xbase,	\
	 .num_regs = 1 }) }

/* these have a subseq number so they run after SYSCLK and DSPCLK widgets */
#define CS48L32_DSP_FREQ_WIDGET_EV(name, num, event)			\
	SND_SOC_DAPM_SUPPLY_S(name "FREQ", 100, SND_SOC_NOPM, num, 0,	\
			      event, SND_SOC_DAPM_POST_PMU)

#define CS48L32_RATES SNDRV_PCM_RATE_KNOT

#define CS48L32_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

#define CS48L32_MIXER_INPUT_ROUTES(name) \
	{ name, "Tone Generator 1", "Tone Generator 1" }, \
	{ name, "Tone Generator 2", "Tone Generator 2" }, \
	{ name, "Noise Generator", "Noise Generator" }, \
	{ name, "IN1L", "IN1L PGA" }, \
	{ name, "IN1R", "IN1R PGA" }, \
	{ name, "IN2L", "IN2L PGA" }, \
	{ name, "IN2R", "IN2R PGA" }, \
	{ name, "ASP1RX1", "ASP1RX1" }, \
	{ name, "ASP1RX2", "ASP1RX2" }, \
	{ name, "ASP1RX3", "ASP1RX3" }, \
	{ name, "ASP1RX4", "ASP1RX4" }, \
	{ name, "ASP1RX5", "ASP1RX5" }, \
	{ name, "ASP1RX6", "ASP1RX6" }, \
	{ name, "ASP1RX7", "ASP1RX7" }, \
	{ name, "ASP1RX8", "ASP1RX8" }, \
	{ name, "ASP2RX1", "ASP2RX1" }, \
	{ name, "ASP2RX2", "ASP2RX2" }, \
	{ name, "ASP2RX3", "ASP2RX3" }, \
	{ name, "ASP2RX4", "ASP2RX4" }, \
	{ name, "ISRC1DEC1", "ISRC1DEC1" }, \
	{ name, "ISRC1DEC2", "ISRC1DEC2" }, \
	{ name, "ISRC1DEC3", "ISRC1DEC3" }, \
	{ name, "ISRC1DEC4", "ISRC1DEC4" }, \
	{ name, "ISRC1INT1", "ISRC1INT1" }, \
	{ name, "ISRC1INT2", "ISRC1INT2" }, \
	{ name, "ISRC1INT3", "ISRC1INT3" }, \
	{ name, "ISRC1INT4", "ISRC1INT4" }, \
	{ name, "ISRC2DEC1", "ISRC2DEC1" }, \
	{ name, "ISRC2DEC2", "ISRC2DEC2" }, \
	{ name, "ISRC2INT1", "ISRC2INT1" }, \
	{ name, "ISRC2INT2", "ISRC2INT2" }, \
	{ name, "ISRC3DEC1", "ISRC3DEC1" }, \
	{ name, "ISRC3DEC2", "ISRC3DEC2" }, \
	{ name, "ISRC3INT1", "ISRC3INT1" }, \
	{ name, "ISRC3INT2", "ISRC3INT2" }, \
	{ name, "EQ1", "EQ1" }, \
	{ name, "EQ2", "EQ2" }, \
	{ name, "EQ3", "EQ3" }, \
	{ name, "EQ4", "EQ4" }, \
	{ name, "DRC1L", "DRC1L" }, \
	{ name, "DRC1R", "DRC1R" }, \
	{ name, "DRC2L", "DRC2L" }, \
	{ name, "DRC2R", "DRC2R" }, \
	{ name, "LHPF1", "LHPF1" }, \
	{ name, "LHPF2", "LHPF2" }, \
	{ name, "LHPF3", "LHPF3" }, \
	{ name, "LHPF4", "LHPF4" }, \
	{ name, "Ultrasonic 1", "Ultrasonic 1" }, \
	{ name, "Ultrasonic 2", "Ultrasonic 2" }, \
	{ name, "DSP1.1", "DSP1" }, \
	{ name, "DSP1.2", "DSP1" }, \
	{ name, "DSP1.3", "DSP1" }, \
	{ name, "DSP1.4", "DSP1" }, \
	{ name, "DSP1.5", "DSP1" }, \
	{ name, "DSP1.6", "DSP1" }, \
	{ name, "DSP1.7", "DSP1" }, \
	{ name, "DSP1.8", "DSP1" }

struct cs48l32_enum {
	struct soc_enum mixer_enum;
	int val;
};

struct cs48l32_eq_control {
	unsigned int reg;
	unsigned int shift;
	unsigned int block_base;
	unsigned int max;
};

struct cs48l32_dai_priv {
	int clk;
	struct snd_pcm_hw_constraint_list constraint;
};

struct cs48l32_dsp_power_reg_block {
	unsigned int start;
	unsigned int end;
};

struct cs48l32_dsp_power_regs {
	const unsigned int *pwd;
	unsigned int n_pwd;
	const struct cs48l32_dsp_power_reg_block *ext;
	unsigned int n_ext;
};

struct cs48l32;
struct cs48l32_codec;
struct spi_device;

struct cs48l32_fll_cfg {
	int n;
	unsigned int theta;
	unsigned int lambda;
	int refdiv;
	int fratio;
	int gain;
	int alt_gain;
};

struct cs48l32_fll {
	struct cs48l32_codec *codec;
	int id;
	unsigned int base;

	unsigned int sts_addr;
	unsigned int sts_mask;
	unsigned int fout;
	int ref_src;
	unsigned int ref_freq;

	struct cs48l32_fll_cfg ref_cfg;
};

struct cs48l32_codec {
	struct wm_adsp dsp;	/* must be first */
	struct cs48l32 core;
	int sysclk;
	int dspclk;
	struct cs48l32_dai_priv dai[CS48L32_MAX_DAI];
	struct cs48l32_fll fll;

	unsigned int in_up_pending;
	unsigned int in_vu_reg;

	struct mutex rate_lock;

	u8 dsp_dma_rates[CS48L32_DSP_N_RX_CHANNELS + CS48L32_DSP_N_TX_CHANNELS];

	u8 in_type[CS48L32_MAX_ANALOG_INPUT][CS48L32_MAX_IN_MUX_WAYS];
	u8 pdm_sup[CS48L32_MAX_ANALOG_INPUT];
	u8 tdm_width[CS48L32_MAX_ASP];
	u8 tdm_slots[CS48L32_MAX_ASP];

	unsigned int eq_mode[CS48L32_N_EQ_BLOCKS];
	__be16 eq_coefficients[CS48L32_N_EQ_BLOCKS][CS48L32_EQ_BLOCK_SZ / 2];

	const struct cs48l32_dsp_power_regs *dsp_power_regs;
};

#define cs48l32_fll_err(_fll, fmt, ...) \
	dev_err(_fll->codec->core.dev, "FLL%d: " fmt, _fll->id, ##__VA_ARGS__)
#define cs48l32_fll_warn(_fll, fmt, ...) \
	dev_warn(_fll->codec->core.dev, "FLL%d: " fmt, _fll->id, ##__VA_ARGS__)
#define cs48l32_fll_dbg(_fll, fmt, ...) \
	dev_dbg(_fll->codec->core.dev, "FLL%d: " fmt, _fll->id, ##__VA_ARGS__)

#define cs48l32_asp_err(_dai, fmt, ...) \
	dev_err(_dai->component->dev, "ASP%d: " fmt, _dai->id, ##__VA_ARGS__)
#define cs48l32_asp_warn(_dai, fmt, ...) \
	dev_warn(_dai->component->dev, "ASP%d: " fmt, _dai->id, ##__VA_ARGS__)
#define cs48l32_asp_dbg(_dai, fmt, ...) \
	dev_dbg(_dai->component->dev, "ASP%d: " fmt, _dai->id, ##__VA_ARGS__)

int cs48l32_apply_patch(struct cs48l32 *cs48l32);
int cs48l32_create_regmap(struct spi_device *spi, struct cs48l32 *cs48l32);
int cs48l32_enable_asp1_pins(struct cs48l32_codec *cs48l32_codec);
int cs48l32_enable_asp2_pins(struct cs48l32_codec *cs48l32_codec);
int cs48l32_micvdd_voltage_index(u32 voltage);
int cs48l32_micbias1_voltage_index(u32 voltage);

#endif
