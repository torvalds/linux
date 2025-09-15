// SPDX-License-Identifier: GPL-2.0-only
//
// Cirrus Logic CS48L32 audio DSP.
//
// Copyright (C) 2016-2018, 2020, 2022, 2025 Cirrus Logic, Inc. and
//               Cirrus Logic International Semiconductor Ltd.

#include <dt-bindings/sound/cs48l32.h>
#include <linux/array_size.h>
#include <linux/build_bug.h>
#include <linux/clk.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gcd.h>
#include <linux/gpio/consumer.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/string_choices.h>
#include <sound/cs48l32.h>
#include <sound/cs48l32_registers.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-component.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "cs48l32.h"

static const char * const cs48l32_core_supplies[] = { "vdd-a", "vdd-io" };

static const struct cs_dsp_region cs48l32_dsp1_regions[] = {
	{ .type = WMFW_HALO_PM_PACKED, .base = 0x3800000 },
	{ .type = WMFW_HALO_XM_PACKED, .base = 0x2000000 },
	{ .type = WMFW_ADSP2_XM, .base = 0x2800000 },
	{ .type = WMFW_HALO_YM_PACKED, .base = 0x2C00000 },
	{ .type = WMFW_ADSP2_YM, .base = 0x3400000 },
};

static const struct cs48l32_dsp_power_reg_block cs48l32_dsp1_sram_ext_regs[] = {
	{ CS48L32_DSP1_XM_SRAM_IBUS_SETUP_1, CS48L32_DSP1_XM_SRAM_IBUS_SETUP_24 },
	{ CS48L32_DSP1_YM_SRAM_IBUS_SETUP_1, CS48L32_DSP1_YM_SRAM_IBUS_SETUP_8 },
	{ CS48L32_DSP1_PM_SRAM_IBUS_SETUP_1, CS48L32_DSP1_PM_SRAM_IBUS_SETUP_7 },
};

static const unsigned int cs48l32_dsp1_sram_pwd_regs[] = {
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_0,
	CS48L32_DSP1_YM_SRAM_IBUS_SETUP_0,
	CS48L32_DSP1_PM_SRAM_IBUS_SETUP_0,
};

static const struct cs48l32_dsp_power_regs cs48l32_dsp_sram_regs = {
	.ext = cs48l32_dsp1_sram_ext_regs,
	.n_ext = ARRAY_SIZE(cs48l32_dsp1_sram_ext_regs),
	.pwd = cs48l32_dsp1_sram_pwd_regs,
	.n_pwd = ARRAY_SIZE(cs48l32_dsp1_sram_pwd_regs),
};

static const char * const cs48l32_mixer_texts[] = {
	"None",
	"Tone Generator 1",
	"Tone Generator 2",
	"Noise Generator",
	"IN1L",
	"IN1R",
	"IN2L",
	"IN2R",
	"ASP1RX1",
	"ASP1RX2",
	"ASP1RX3",
	"ASP1RX4",
	"ASP1RX5",
	"ASP1RX6",
	"ASP1RX7",
	"ASP1RX8",
	"ASP2RX1",
	"ASP2RX2",
	"ASP2RX3",
	"ASP2RX4",
	"ISRC1INT1",
	"ISRC1INT2",
	"ISRC1INT3",
	"ISRC1INT4",
	"ISRC1DEC1",
	"ISRC1DEC2",
	"ISRC1DEC3",
	"ISRC1DEC4",
	"ISRC2INT1",
	"ISRC2INT2",
	"ISRC2DEC1",
	"ISRC2DEC2",
	"ISRC3INT1",
	"ISRC3INT2",
	"ISRC3DEC1",
	"ISRC3DEC2",
	"EQ1",
	"EQ2",
	"EQ3",
	"EQ4",
	"DRC1L",
	"DRC1R",
	"DRC2L",
	"DRC2R",
	"LHPF1",
	"LHPF2",
	"LHPF3",
	"LHPF4",
	"Ultrasonic 1",
	"Ultrasonic 2",
	"DSP1.1",
	"DSP1.2",
	"DSP1.3",
	"DSP1.4",
	"DSP1.5",
	"DSP1.6",
	"DSP1.7",
	"DSP1.8",
};

static unsigned int cs48l32_mixer_values[] = {
	0x000, /* Silence (mute) */
	0x004, /* Tone generator 1 */
	0x005, /* Tone generator 2 */
	0x00C, /* Noise Generator */
	0x010, /* IN1L signal path */
	0x011, /* IN1R signal path */
	0x012, /* IN2L signal path */
	0x013, /* IN2R signal path */
	0x020, /* ASP1 RX1 */
	0x021, /* ASP1 RX2 */
	0x022, /* ASP1 RX3 */
	0x023, /* ASP1 RX4 */
	0x024, /* ASP1 RX5 */
	0x025, /* ASP1 RX6 */
	0x026, /* ASP1 RX7 */
	0x027, /* ASP1 RX8 */
	0x030, /* ASP2 RX1 */
	0x031, /* ASP2 RX2 */
	0x032, /* ASP2 RX3 */
	0x033, /* ASP2 RX4 */
	0x098, /* ISRC1 INT1 */
	0x099, /* ISRC1 INT2 */
	0x09a, /* ISRC1 INT3 */
	0x09b, /* ISRC1 INT4 */
	0x09C, /* ISRC1 DEC1 */
	0x09D, /* ISRC1 DEC2 */
	0x09e, /* ISRC1 DEC3 */
	0x09f, /* ISRC1 DEC4 */
	0x0A0, /* ISRC2 INT1 */
	0x0A1, /* ISRC2 INT2 */
	0x0A4, /* ISRC2 DEC1 */
	0x0A5, /* ISRC2 DEC2 */
	0x0A8, /* ISRC3 INT1 */
	0x0A9, /* ISRC3 INT2 */
	0x0AC, /* ISRC3 DEC1 */
	0x0AD, /* ISRC3 DEC2 */
	0x0B8, /* EQ1 */
	0x0B9, /* EQ2 */
	0x0BA, /* EQ3 */
	0x0BB, /* EQ4 */
	0x0C0, /* DRC1 Left */
	0x0C1, /* DRC1 Right */
	0x0C2, /* DRC2 Left */
	0x0C3, /* DRC2 Right */
	0x0C8, /* LHPF1 */
	0x0C9, /* LHPF2 */
	0x0CA, /* LHPF3 */
	0x0CB, /* LHPF4 */
	0x0D8, /* Ultrasonic 1 */
	0x0D9, /* Ultrasonic 2 */
	0x100, /* DSP1 channel 1 */
	0x101, /* DSP1 channel 2 */
	0x102, /* DSP1 channel 3 */
	0x103, /* DSP1 channel 4 */
	0x104, /* DSP1 channel 5 */
	0x105, /* DSP1 channel 6 */
	0x106, /* DSP1 channel 7 */
	0x107, /* DSP1 channel 8 */
};
static_assert(ARRAY_SIZE(cs48l32_mixer_texts) == ARRAY_SIZE(cs48l32_mixer_values));
#define CS48L32_NUM_MIXER_INPUTS	ARRAY_SIZE(cs48l32_mixer_values)

static const DECLARE_TLV_DB_SCALE(cs48l32_ana_tlv, 0, 100, 0);
static const DECLARE_TLV_DB_SCALE(cs48l32_eq_tlv, -1200, 100, 0);
static const DECLARE_TLV_DB_SCALE(cs48l32_digital_tlv, -6400, 50, 0);
static const DECLARE_TLV_DB_SCALE(cs48l32_noise_tlv, -10800, 600, 0);
static const DECLARE_TLV_DB_SCALE(cs48l32_mixer_tlv, -3200, 100, 0);
static const DECLARE_TLV_DB_SCALE(cs48l32_us_tlv, 0, 600, 0);

static void cs48l32_spin_sysclk(struct cs48l32_codec *cs48l32_codec)
{
	struct cs48l32 *cs48l32 = &cs48l32_codec->core;
	unsigned int val;
	int ret, i;

	/* Skip this if the chip is down */
	if (pm_runtime_suspended(cs48l32->dev))
		return;

	/*
	 * Just read a register a few times to ensure the internal
	 * oscillator sends out some clocks.
	 */
	for (i = 0; i < 4; i++) {
		ret = regmap_read(cs48l32->regmap, CS48L32_DEVID, &val);
		if (ret)
			dev_err(cs48l32_codec->core.dev, "%s Failed to read register: %d (%d)\n",
				__func__, ret, i);
	}

	udelay(300);
}

static const char * const cs48l32_rate_text[] = {
	"Sample Rate 1", "Sample Rate 2", "Sample Rate 3", "Sample Rate 4",
};

static const unsigned int cs48l32_rate_val[] = {
	0x0, 0x1, 0x2, 0x3,
};
static_assert(ARRAY_SIZE(cs48l32_rate_val) == ARRAY_SIZE(cs48l32_rate_text));

static int cs48l32_rate_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	int ret;

	/* Prevent any mixer mux changes while we do this */
	mutex_lock(&cs48l32_codec->rate_lock);

	/* The write must be guarded by a number of SYSCLK cycles */
	cs48l32_spin_sysclk(cs48l32_codec);
	ret = snd_soc_put_enum_double(kcontrol, ucontrol);
	cs48l32_spin_sysclk(cs48l32_codec);

	mutex_unlock(&cs48l32_codec->rate_lock);

	return ret;
}

static const char * const cs48l32_sample_rate_text[] = {
	"12kHz",
	"24kHz",
	"48kHz",
	"96kHz",
	"192kHz",
	"384kHz",
	"768kHz",
	"11.025kHz",
	"22.05kHz",
	"44.1kHz",
	"88.2kHz",
	"176.4kHz",
	"352.8kHz",
	"705.6kHz",
	"8kHz",
	"16kHz",
	"32kHz",
};

static const unsigned int cs48l32_sample_rate_val[] = {
	0x01, /* 12kHz */
	0x02, /* 24kHz */
	0x03, /* 48kHz */
	0x04, /* 96kHz */
	0x05, /* 192kHz */
	0x06, /* 384kHz */
	0x07, /* 768kHz */
	0x09, /* 11.025kHz */
	0x0a, /* 22.05kHz */
	0x0b, /* 44.1kHz */
	0x0c, /* 88.2kHz */
	0x0d, /* 176.4kHz */
	0x0e, /* 352.8kHz */
	0x0f, /* 705.6kHz */
	0x11, /* 8kHz */
	0x12, /* 16kHz */
	0x13, /* 32kHz */
};
static_assert(ARRAY_SIZE(cs48l32_sample_rate_val) == ARRAY_SIZE(cs48l32_sample_rate_text));
#define CS48L32_SAMPLE_RATE_ENUM_SIZE	ARRAY_SIZE(cs48l32_sample_rate_val)

static const struct soc_enum cs48l32_sample_rate[] = {
	SOC_VALUE_ENUM_SINGLE(CS48L32_SAMPLE_RATE1,
			      CS48L32_SAMPLE_RATE_1_SHIFT,
			      CS48L32_SAMPLE_RATE_1_MASK >> CS48L32_SAMPLE_RATE_1_SHIFT,
			      CS48L32_SAMPLE_RATE_ENUM_SIZE,
			      cs48l32_sample_rate_text,
			      cs48l32_sample_rate_val),
	SOC_VALUE_ENUM_SINGLE(CS48L32_SAMPLE_RATE2,
			      CS48L32_SAMPLE_RATE_1_SHIFT,
			      CS48L32_SAMPLE_RATE_1_MASK >> CS48L32_SAMPLE_RATE_1_SHIFT,
			      CS48L32_SAMPLE_RATE_ENUM_SIZE,
			      cs48l32_sample_rate_text,
			      cs48l32_sample_rate_val),
	SOC_VALUE_ENUM_SINGLE(CS48L32_SAMPLE_RATE3,
			      CS48L32_SAMPLE_RATE_1_SHIFT,
			      CS48L32_SAMPLE_RATE_1_MASK >> CS48L32_SAMPLE_RATE_1_SHIFT,
			      CS48L32_SAMPLE_RATE_ENUM_SIZE,
			      cs48l32_sample_rate_text,
			      cs48l32_sample_rate_val),
	SOC_VALUE_ENUM_SINGLE(CS48L32_SAMPLE_RATE4,
			      CS48L32_SAMPLE_RATE_1_SHIFT,
			      CS48L32_SAMPLE_RATE_1_MASK >> CS48L32_SAMPLE_RATE_1_SHIFT,
			      CS48L32_SAMPLE_RATE_ENUM_SIZE,
			      cs48l32_sample_rate_text,
			      cs48l32_sample_rate_val),
};

static int cs48l32_inmux_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	unsigned int mux, src_val, in_type;
	int ret;

	mux = ucontrol->value.enumerated.item[0];
	if (mux > 1)
		return -EINVAL;

	switch (e->reg) {
	case CS48L32_IN1L_CONTROL1:
		in_type = cs48l32_codec->in_type[0][mux];
		break;
	case CS48L32_IN1R_CONTROL1:
		in_type = cs48l32_codec->in_type[1][mux];
		break;
	default:
		return -EINVAL;
	}

	src_val = mux << e->shift_l;

	if (in_type == CS48L32_IN_TYPE_SE)
		src_val |= 1 << CS48L32_INx_SRC_SHIFT;

	ret = snd_soc_component_update_bits(dapm->component,
					    e->reg,
					    CS48L32_INx_SRC_MASK,
					    src_val);
	if (ret > 0)
		snd_soc_dapm_mux_update_power(dapm, kcontrol, mux, e, NULL);

	return ret;
}

static const char * const cs48l32_inmux_texts[] = {
	"Analog 1", "Analog 2",
};

static SOC_ENUM_SINGLE_DECL(cs48l32_in1muxl_enum,
			    CS48L32_IN1L_CONTROL1,
			    CS48L32_INx_SRC_SHIFT + 1,
			    cs48l32_inmux_texts);

static SOC_ENUM_SINGLE_DECL(cs48l32_in1muxr_enum,
			    CS48L32_IN1R_CONTROL1,
			    CS48L32_INx_SRC_SHIFT + 1,
			    cs48l32_inmux_texts);

static const struct snd_kcontrol_new cs48l32_inmux[] = {
	SOC_DAPM_ENUM_EXT("IN1L Mux", cs48l32_in1muxl_enum,
			  snd_soc_dapm_get_enum_double, cs48l32_inmux_put),
	SOC_DAPM_ENUM_EXT("IN1R Mux", cs48l32_in1muxr_enum,
			  snd_soc_dapm_get_enum_double, cs48l32_inmux_put),
};

static const char * const cs48l32_dmode_texts[] = {
	"Analog", "Digital",
};

static int cs48l32_dmode_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_component *component = snd_soc_dapm_to_component(dapm);
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	unsigned int mode;
	int ret, result;

	mode = ucontrol->value.enumerated.item[0];
	switch (mode) {
	case 0:
		ret = snd_soc_component_update_bits(component,
						    CS48L32_ADC1L_ANA_CONTROL1,
						    CS48L32_ADC1x_INT_ENA_FRC_MASK,
						    CS48L32_ADC1x_INT_ENA_FRC_MASK);
		if (ret < 0) {
			dev_err(component->dev,
				"Failed to set ADC1L_INT_ENA_FRC: %d\n", ret);
			return ret;
		}

		ret = snd_soc_component_update_bits(component,
						    CS48L32_ADC1R_ANA_CONTROL1,
						    CS48L32_ADC1x_INT_ENA_FRC_MASK,
						    CS48L32_ADC1x_INT_ENA_FRC_MASK);
		if (ret < 0) {
			dev_err(component->dev,
				"Failed to set ADC1R_INT_ENA_FRC: %d\n", ret);
			return ret;
		}

		result = snd_soc_component_update_bits(component,
						       e->reg,
						       BIT(CS48L32_IN1_MODE_SHIFT),
						       0);
		if (result < 0) {
			dev_err(component->dev, "Failed to set input mode: %d\n", result);
			return result;
		}

		usleep_range(200, 300);

		ret = snd_soc_component_update_bits(component,
						    CS48L32_ADC1L_ANA_CONTROL1,
						    CS48L32_ADC1x_INT_ENA_FRC_MASK,
						    0);
		if (ret < 0) {
			dev_err(component->dev,
				"Failed to clear ADC1L_INT_ENA_FRC: %d\n", ret);
			return ret;
		}

		ret = snd_soc_component_update_bits(component,
						    CS48L32_ADC1R_ANA_CONTROL1,
						    CS48L32_ADC1x_INT_ENA_FRC_MASK,
						    0);
		if (ret < 0) {
			dev_err(component->dev,
				"Failed to clear ADC1R_INT_ENA_FRC: %d\n", ret);
			return ret;
		}

		if (result > 0)
			snd_soc_dapm_mux_update_power(dapm, kcontrol, mode, e, NULL);

		return result;
	case 1:
		return snd_soc_dapm_put_enum_double(kcontrol, ucontrol);
	default:
		return -EINVAL;
	}
}

static SOC_ENUM_SINGLE_DECL(cs48l32_in1dmode_enum,
			    CS48L32_INPUT1_CONTROL1,
			    CS48L32_IN1_MODE_SHIFT,
			    cs48l32_dmode_texts);

static const struct snd_kcontrol_new cs48l32_dmode_mux[] = {
	SOC_DAPM_ENUM_EXT("IN1 Mode", cs48l32_in1dmode_enum,
			  snd_soc_dapm_get_enum_double, cs48l32_dmode_put),
};

static const char * const cs48l32_in_texts[] = {
	"IN1L", "IN1R", "IN2L", "IN2R",
};
static_assert(ARRAY_SIZE(cs48l32_in_texts) == CS48L32_MAX_INPUT);

static const char * const cs48l32_us_freq_texts[] = {
	"16-24kHz", "20-28kHz",
};

static const unsigned int cs48l32_us_freq_val[] = {
	0x2, 0x3,
};

static const struct soc_enum cs48l32_us_freq[] = {
	SOC_VALUE_ENUM_SINGLE(CS48L32_US1_CONTROL,
			      CS48L32_US1_FREQ_SHIFT,
			      CS48L32_US1_FREQ_MASK >> CS48L32_US1_FREQ_SHIFT,
			      ARRAY_SIZE(cs48l32_us_freq_val),
			      cs48l32_us_freq_texts,
			      cs48l32_us_freq_val),
	SOC_VALUE_ENUM_SINGLE(CS48L32_US2_CONTROL,
			      CS48L32_US1_FREQ_SHIFT,
			      CS48L32_US1_FREQ_MASK >> CS48L32_US1_FREQ_SHIFT,
			      ARRAY_SIZE(cs48l32_us_freq_val),
			      cs48l32_us_freq_texts,
			      cs48l32_us_freq_val),
};

static const unsigned int cs48l32_us_in_val[] = {
	0x0, 0x1, 0x2, 0x3,
};

static const struct soc_enum cs48l32_us_inmux_enum[] = {
	SOC_VALUE_ENUM_SINGLE(CS48L32_US1_CONTROL,
			      CS48L32_US1_SRC_SHIFT,
			      CS48L32_US1_SRC_MASK >> CS48L32_US1_SRC_SHIFT,
			      ARRAY_SIZE(cs48l32_us_in_val),
			      cs48l32_in_texts,
			      cs48l32_us_in_val),
	SOC_VALUE_ENUM_SINGLE(CS48L32_US2_CONTROL,
			      CS48L32_US1_SRC_SHIFT,
			      CS48L32_US1_SRC_MASK >> CS48L32_US1_SRC_SHIFT,
			      ARRAY_SIZE(cs48l32_us_in_val),
			      cs48l32_in_texts,
			      cs48l32_us_in_val),
};

static const struct snd_kcontrol_new cs48l32_us_inmux[] = {
	SOC_DAPM_ENUM("Ultrasonic 1 Input", cs48l32_us_inmux_enum[0]),
	SOC_DAPM_ENUM("Ultrasonic 2 Input", cs48l32_us_inmux_enum[1]),
};

static const char * const cs48l32_us_det_thr_texts[] = {
	"-6dB", "-9dB", "-12dB", "-15dB", "-18dB", "-21dB", "-24dB", "-27dB",
};

static const struct soc_enum cs48l32_us_det_thr[] = {
	SOC_ENUM_SINGLE(CS48L32_US1_DET_CONTROL,
			CS48L32_US1_DET_THR_SHIFT,
			ARRAY_SIZE(cs48l32_us_det_thr_texts),
			cs48l32_us_det_thr_texts),
	SOC_ENUM_SINGLE(CS48L32_US2_DET_CONTROL,
			CS48L32_US1_DET_THR_SHIFT,
			ARRAY_SIZE(cs48l32_us_det_thr_texts),
			cs48l32_us_det_thr_texts),
};

static const char * const cs48l32_us_det_num_texts[] = {
	"1 Sample",
	"2 Samples",
	"4 Samples",
	"8 Samples",
	"16 Samples",
	"32 Samples",
	"64 Samples",
	"128 Samples",
	"256 Samples",
	"512 Samples",
	"1024 Samples",
	"2048 Samples",
	"4096 Samples",
	"8192 Samples",
	"16384 Samples",
	"32768 Samples",
};

static const struct soc_enum cs48l32_us_det_num[] = {
	SOC_ENUM_SINGLE(CS48L32_US1_DET_CONTROL,
			CS48L32_US1_DET_NUM_SHIFT,
			ARRAY_SIZE(cs48l32_us_det_num_texts),
			cs48l32_us_det_num_texts),
	SOC_ENUM_SINGLE(CS48L32_US2_DET_CONTROL,
			CS48L32_US1_DET_NUM_SHIFT,
			ARRAY_SIZE(cs48l32_us_det_num_texts),
			cs48l32_us_det_num_texts),
};

static const char * const cs48l32_us_det_hold_texts[] = {
	"0 Samples",
	"31 Samples",
	"63 Samples",
	"127 Samples",
	"255 Samples",
	"511 Samples",
	"1023 Samples",
	"2047 Samples",
	"4095 Samples",
	"8191 Samples",
	"16383 Samples",
	"32767 Samples",
	"65535 Samples",
	"131071 Samples",
	"262143 Samples",
	"524287 Samples",
};

static const struct soc_enum cs48l32_us_det_hold[] = {
	SOC_ENUM_SINGLE(CS48L32_US1_DET_CONTROL,
			CS48L32_US1_DET_HOLD_SHIFT,
			ARRAY_SIZE(cs48l32_us_det_hold_texts),
			cs48l32_us_det_hold_texts),
	SOC_ENUM_SINGLE(CS48L32_US2_DET_CONTROL,
			CS48L32_US1_DET_HOLD_SHIFT,
			ARRAY_SIZE(cs48l32_us_det_hold_texts),
			cs48l32_us_det_hold_texts),
};

static const struct soc_enum cs48l32_us_output_rate[] = {
	SOC_VALUE_ENUM_SINGLE(CS48L32_US1_CONTROL,
			      CS48L32_US1_RATE_SHIFT,
			      CS48L32_US1_RATE_MASK >> CS48L32_US1_RATE_SHIFT,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text,
			      cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(CS48L32_US2_CONTROL,
			      CS48L32_US1_RATE_SHIFT,
			      CS48L32_US1_RATE_MASK >> CS48L32_US1_RATE_SHIFT,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text,
			      cs48l32_rate_val),
};

static const char * const cs48l32_us_det_lpf_cut_texts[] = {
	"1722Hz", "833Hz", "408Hz", "203Hz",
};

static const struct soc_enum cs48l32_us_det_lpf_cut[] = {
	SOC_ENUM_SINGLE(CS48L32_US1_DET_CONTROL,
			CS48L32_US1_DET_LPF_CUT_SHIFT,
			ARRAY_SIZE(cs48l32_us_det_lpf_cut_texts),
			cs48l32_us_det_lpf_cut_texts),
	SOC_ENUM_SINGLE(CS48L32_US2_DET_CONTROL,
			CS48L32_US1_DET_LPF_CUT_SHIFT,
			ARRAY_SIZE(cs48l32_us_det_lpf_cut_texts),
			cs48l32_us_det_lpf_cut_texts),
};

static const char * const cs48l32_us_det_dcy_texts[] = {
	"0 ms", "0.79 ms", "1.58 ms", "3.16 ms", "6.33 ms", "12.67 ms", "25.34 ms", "50.69 ms",
};

static const struct soc_enum cs48l32_us_det_dcy[] = {
	SOC_ENUM_SINGLE(CS48L32_US1_DET_CONTROL,
			CS48L32_US1_DET_DCY_SHIFT,
			ARRAY_SIZE(cs48l32_us_det_dcy_texts),
			cs48l32_us_det_dcy_texts),
	SOC_ENUM_SINGLE(CS48L32_US2_DET_CONTROL,
			CS48L32_US1_DET_DCY_SHIFT,
			ARRAY_SIZE(cs48l32_us_det_dcy_texts),
			cs48l32_us_det_dcy_texts),
};

static const struct snd_kcontrol_new cs48l32_us_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
};

static const char * const cs48l32_vol_ramp_text[] = {
	"0ms/6dB", "0.5ms/6dB", "1ms/6dB", "2ms/6dB", "4ms/6dB", "8ms/6dB", "16ms/6dB", "32ms/6dB",
};

static SOC_ENUM_SINGLE_DECL(cs48l32_in_vd_ramp,
			    CS48L32_INPUT_VOL_CONTROL,
			    CS48L32_IN_VD_RAMP_SHIFT,
			    cs48l32_vol_ramp_text);

static SOC_ENUM_SINGLE_DECL(cs48l32_in_vi_ramp,
			    CS48L32_INPUT_VOL_CONTROL,
			    CS48L32_IN_VI_RAMP_SHIFT,
			    cs48l32_vol_ramp_text);

static const char * const cs48l32_in_hpf_cut_text[] = {
	"2.5Hz", "5Hz", "10Hz", "20Hz", "40Hz"
};

static SOC_ENUM_SINGLE_DECL(cs48l32_in_hpf_cut_enum,
			    CS48L32_INPUT_HPF_CONTROL,
			    CS48L32_IN_HPF_CUT_SHIFT,
			    cs48l32_in_hpf_cut_text);

static const char * const cs48l32_in_dmic_osr_text[] = {
	"384kHz", "768kHz", "1.536MHz", "2.048MHz", "2.4576MHz", "3.072MHz", "6.144MHz",
};

static const struct soc_enum cs48l32_in_dmic_osr[] = {
	SOC_ENUM_SINGLE(CS48L32_INPUT1_CONTROL1,
			CS48L32_IN1_OSR_SHIFT,
			ARRAY_SIZE(cs48l32_in_dmic_osr_text),
			cs48l32_in_dmic_osr_text),
	SOC_ENUM_SINGLE(CS48L32_INPUT2_CONTROL1,
			CS48L32_IN1_OSR_SHIFT,
			ARRAY_SIZE(cs48l32_in_dmic_osr_text),
			cs48l32_in_dmic_osr_text),
};

static bool cs48l32_is_input_enabled(struct snd_soc_component *component,
				     unsigned int reg)
{
	unsigned int input_active;

	input_active = snd_soc_component_read(component, CS48L32_INPUT_CONTROL);
	switch (reg) {
	case CS48L32_IN1L_CONTROL1:
		return input_active & BIT(CS48L32_IN1L_EN_SHIFT);
	case CS48L32_IN1R_CONTROL1:
		return input_active & BIT(CS48L32_IN1R_EN_SHIFT);
	case CS48L32_IN2L_CONTROL1:
		return input_active & BIT(CS48L32_IN2L_EN_SHIFT);
	case CS48L32_IN2R_CONTROL1:
		return input_active & BIT(CS48L32_IN2R_EN_SHIFT);
	default:
		return false;
	}
}

static int cs48l32_in_rate_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int ret;

	snd_soc_dapm_mutex_lock(dapm);

	/* Cannot change rate on an active input */
	if (cs48l32_is_input_enabled(component, e->reg)) {
		ret = -EBUSY;
		goto exit;
	}

	ret = snd_soc_put_enum_double(kcontrol, ucontrol);
exit:
	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}

static const struct soc_enum cs48l32_input_rate[] = {
	SOC_VALUE_ENUM_SINGLE(CS48L32_IN1L_CONTROL1,
			      CS48L32_INx_RATE_SHIFT,
			      CS48L32_INx_RATE_MASK >> CS48L32_INx_RATE_SHIFT,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text,
			      cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(CS48L32_IN1R_CONTROL1,
			      CS48L32_INx_RATE_SHIFT,
			      CS48L32_INx_RATE_MASK >> CS48L32_INx_RATE_SHIFT,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text,
			      cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(CS48L32_IN2L_CONTROL1,
			      CS48L32_INx_RATE_SHIFT,
			      CS48L32_INx_RATE_MASK >> CS48L32_INx_RATE_SHIFT,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text,
			      cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(CS48L32_IN2R_CONTROL1,
			      CS48L32_INx_RATE_SHIFT,
			      CS48L32_INx_RATE_MASK >> CS48L32_INx_RATE_SHIFT,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text,
			      cs48l32_rate_val),
};

static int cs48l32_low_power_mode_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	int ret;

	snd_soc_dapm_mutex_lock(dapm);

	/* Cannot change rate on an active input */
	if (cs48l32_is_input_enabled(component, mc->reg)) {
		ret = -EBUSY;
		goto exit;
	}

	ret = snd_soc_put_volsw(kcontrol, ucontrol);

exit:
	snd_soc_dapm_mutex_unlock(dapm);
	return ret;
}

static const struct soc_enum noise_gen_rate =
	SOC_VALUE_ENUM_SINGLE(CS48L32_COMFORT_NOISE_GENERATOR,
			      CS48L32_NOISE_GEN_RATE_SHIFT,
			      CS48L32_NOISE_GEN_RATE_MASK >> CS48L32_NOISE_GEN_RATE_SHIFT,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text,
			      cs48l32_rate_val);

static const char * const cs48l32_auxpdm_freq_texts[] = {
	"3.072MHz", "2.048MHz", "1.536MHz", "768kHz",
};

static SOC_ENUM_SINGLE_DECL(cs48l32_auxpdm1_freq,
			    CS48L32_AUXPDM1_CONTROL1,
			    CS48L32_AUXPDM1_FREQ_SHIFT,
			    cs48l32_auxpdm_freq_texts);

static SOC_ENUM_SINGLE_DECL(cs48l32_auxpdm2_freq,
			    CS48L32_AUXPDM2_CONTROL1,
			    CS48L32_AUXPDM1_FREQ_SHIFT,
			    cs48l32_auxpdm_freq_texts);

static const char * const cs48l32_auxpdm_src_texts[] = {
	"Analog", "IN1 Digital", "IN2 Digital",
};

static SOC_ENUM_SINGLE_DECL(cs48l32_auxpdm1_in,
			    CS48L32_AUXPDM_CTRL2,
			    CS48L32_AUXPDMDAT1_SRC_SHIFT,
			    cs48l32_auxpdm_src_texts);

static SOC_ENUM_SINGLE_DECL(cs48l32_auxpdm2_in,
			    CS48L32_AUXPDM_CTRL2,
			    CS48L32_AUXPDMDAT2_SRC_SHIFT,
			    cs48l32_auxpdm_src_texts);

static const struct snd_kcontrol_new cs48l32_auxpdm_inmux[] = {
	SOC_DAPM_ENUM("AUXPDM1 Input", cs48l32_auxpdm1_in),
	SOC_DAPM_ENUM("AUXPDM2 Input", cs48l32_auxpdm2_in),
};

static const unsigned int cs48l32_auxpdm_analog_in_val[] = {
	0x0, 0x1,
};

static const struct soc_enum cs48l32_auxpdm_analog_inmux_enum[] = {
	SOC_VALUE_ENUM_SINGLE(CS48L32_AUXPDM1_CONTROL1,
			      CS48L32_AUXPDM1_SRC_SHIFT,
			      CS48L32_AUXPDM1_SRC_MASK >> CS48L32_AUXPDM1_SRC_SHIFT,
			      ARRAY_SIZE(cs48l32_auxpdm_analog_in_val),
			      cs48l32_in_texts,
			      cs48l32_auxpdm_analog_in_val),
	SOC_VALUE_ENUM_SINGLE(CS48L32_AUXPDM2_CONTROL1,
			      CS48L32_AUXPDM1_SRC_SHIFT,
			      CS48L32_AUXPDM1_SRC_MASK >> CS48L32_AUXPDM1_SRC_SHIFT,
			      ARRAY_SIZE(cs48l32_auxpdm_analog_in_val),
			      cs48l32_in_texts,
			      cs48l32_auxpdm_analog_in_val),
};

static const struct snd_kcontrol_new cs48l32_auxpdm_analog_inmux[] = {
	SOC_DAPM_ENUM("AUXPDM1 Analog Input", cs48l32_auxpdm_analog_inmux_enum[0]),
	SOC_DAPM_ENUM("AUXPDM2 Analog Input", cs48l32_auxpdm_analog_inmux_enum[1]),
};

static const struct snd_kcontrol_new cs48l32_auxpdm_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
};

static const struct soc_enum cs48l32_isrc_fsh[] = {
	SOC_VALUE_ENUM_SINGLE(CS48L32_ISRC1_CONTROL1,
			      CS48L32_ISRC1_FSH_SHIFT,
			      CS48L32_ISRC1_FSH_MASK >> CS48L32_ISRC1_FSH_SHIFT,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text,
			      cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(CS48L32_ISRC2_CONTROL1,
			      CS48L32_ISRC1_FSH_SHIFT,
			      CS48L32_ISRC1_FSH_MASK >> CS48L32_ISRC1_FSH_SHIFT,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text,
			      cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(CS48L32_ISRC3_CONTROL1,
			      CS48L32_ISRC1_FSH_SHIFT,
			      CS48L32_ISRC1_FSH_MASK >> CS48L32_ISRC1_FSH_SHIFT,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text,
			      cs48l32_rate_val),
};

static const struct soc_enum cs48l32_isrc_fsl[] = {
	SOC_VALUE_ENUM_SINGLE(CS48L32_ISRC1_CONTROL1,
			      CS48L32_ISRC1_FSL_SHIFT,
			      CS48L32_ISRC1_FSL_MASK >> CS48L32_ISRC1_FSL_SHIFT,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text,
			      cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(CS48L32_ISRC2_CONTROL1,
			      CS48L32_ISRC1_FSL_SHIFT,
			      CS48L32_ISRC1_FSL_MASK >> CS48L32_ISRC1_FSL_SHIFT,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text,
			      cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(CS48L32_ISRC3_CONTROL1,
			      CS48L32_ISRC1_FSL_SHIFT,
			      CS48L32_ISRC1_FSL_MASK >> CS48L32_ISRC1_FSL_SHIFT,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text,
			      cs48l32_rate_val),
};

static const struct soc_enum cs48l32_fx_rate =
	SOC_VALUE_ENUM_SINGLE(CS48L32_FX_SAMPLE_RATE,
			      CS48L32_FX_RATE_SHIFT,
			      CS48L32_FX_RATE_MASK >> CS48L32_FX_RATE_SHIFT,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text,
			      cs48l32_rate_val);

static const char * const cs48l32_lhpf_mode_text[] = {
	"Low-pass", "High-pass"
};

static const struct soc_enum cs48l32_lhpf_mode[] = {
	SOC_ENUM_SINGLE(CS48L32_LHPF_CONTROL2, 0,
			ARRAY_SIZE(cs48l32_lhpf_mode_text), cs48l32_lhpf_mode_text),
	SOC_ENUM_SINGLE(CS48L32_LHPF_CONTROL2, 1,
			ARRAY_SIZE(cs48l32_lhpf_mode_text), cs48l32_lhpf_mode_text),
	SOC_ENUM_SINGLE(CS48L32_LHPF_CONTROL2, 2,
			ARRAY_SIZE(cs48l32_lhpf_mode_text), cs48l32_lhpf_mode_text),
	SOC_ENUM_SINGLE(CS48L32_LHPF_CONTROL2, 3,
			ARRAY_SIZE(cs48l32_lhpf_mode_text), cs48l32_lhpf_mode_text),
};

static int cs48l32_lhpf_coeff_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	__be32 *data = (__be32 *)ucontrol->value.bytes.data;
	s16 val = (s16)be32_to_cpu(*data);

	if (abs(val) > CS48L32_LHPF_MAX_COEFF) {
		dev_err(cs48l32_codec->core.dev, "Rejecting unstable LHPF coefficients\n");
		return -EINVAL;
	}

	return snd_soc_bytes_put(kcontrol, ucontrol);
}

static const char * const cs48l32_eq_mode_text[] = {
	"Low-pass", "High-pass",
};

static const struct soc_enum cs48l32_eq_mode[] = {
	SOC_ENUM_SINGLE(CS48L32_EQ_CONTROL2, 0,
			ARRAY_SIZE(cs48l32_eq_mode_text),
			cs48l32_eq_mode_text),
	SOC_ENUM_SINGLE(CS48L32_EQ_CONTROL2, 1,
			ARRAY_SIZE(cs48l32_eq_mode_text),
			cs48l32_eq_mode_text),
	SOC_ENUM_SINGLE(CS48L32_EQ_CONTROL2, 2,
			ARRAY_SIZE(cs48l32_eq_mode_text),
			cs48l32_eq_mode_text),
	SOC_ENUM_SINGLE(CS48L32_EQ_CONTROL2, 3,
			ARRAY_SIZE(cs48l32_eq_mode_text),
			cs48l32_eq_mode_text),
};

static int cs48l32_eq_mode_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	unsigned int item;

	item = snd_soc_enum_val_to_item(e, cs48l32_codec->eq_mode[e->shift_l]);
	ucontrol->value.enumerated.item[0] = item;

	return 0;
}

static int cs48l32_eq_mode_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	unsigned int val;
	bool changed = false;

	if (item[0] >= e->items)
		return -EINVAL;

	val = snd_soc_enum_item_to_val(e, item[0]);

	snd_soc_dapm_mutex_lock(dapm);
	if (cs48l32_codec->eq_mode[e->shift_l] != val) {
		cs48l32_codec->eq_mode[e->shift_l] = val;
		changed = true;
	}
	snd_soc_dapm_mutex_unlock(dapm);

	return changed;
}

static int cs48l32_eq_coeff_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	struct cs48l32_eq_control *ctl = (void *) kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = ctl->max;

	return 0;
}

static int cs48l32_eq_coeff_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct cs48l32_eq_control *params = (void *)kcontrol->private_value;
	__be16 *coeffs;
	unsigned int coeff_idx;
	int block_idx;

	block_idx = ((int) params->block_base - (int) CS48L32_EQ1_BAND1_COEFF1);
	block_idx /= (CS48L32_EQ2_BAND1_COEFF1 - CS48L32_EQ1_BAND1_COEFF1);

	coeffs = &cs48l32_codec->eq_coefficients[block_idx][0];
	coeff_idx = (params->reg - params->block_base) / 2;

	/* High __be16 is in [coeff_idx] and low __be16 in [coeff_idx + 1] */
	if (params->shift == 0)
		coeff_idx++;

	ucontrol->value.integer.value[0] = be16_to_cpu(coeffs[coeff_idx]);

	return 0;
}

static int cs48l32_eq_coeff_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct cs48l32_eq_control *params = (void *)kcontrol->private_value;
	__be16 *coeffs;
	unsigned int coeff_idx;
	int block_idx;

	block_idx = ((int) params->block_base - (int) CS48L32_EQ1_BAND1_COEFF1);
	block_idx /= (CS48L32_EQ2_BAND1_COEFF1 - CS48L32_EQ1_BAND1_COEFF1);

	coeffs = &cs48l32_codec->eq_coefficients[block_idx][0];
	coeff_idx = (params->reg - params->block_base) / 2;

	/* Put high __be16 in [coeff_idx] and low __be16 in [coeff_idx + 1] */
	if (params->shift == 0)
		coeff_idx++;

	snd_soc_dapm_mutex_lock(dapm);
	coeffs[coeff_idx] = cpu_to_be16(ucontrol->value.integer.value[0]);
	snd_soc_dapm_mutex_unlock(dapm);

	return 0;
}

static const struct snd_kcontrol_new cs48l32_drc_activity_output_mux[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
};

static const struct snd_kcontrol_new cs48l32_dsp_trigger_output_mux[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
};

static int cs48l32_dsp_rate_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	unsigned int cached_rate;
	const unsigned int rate_num = e->mask;
	int item;

	if (rate_num >= ARRAY_SIZE(cs48l32_codec->dsp_dma_rates))
		return -EINVAL;

	cached_rate = cs48l32_codec->dsp_dma_rates[rate_num];
	item = snd_soc_enum_val_to_item(e, cached_rate);
	ucontrol->value.enumerated.item[0] = item;

	return 0;
}

static int cs48l32_dsp_rate_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	const unsigned int rate_num = e->mask;
	const unsigned int item = ucontrol->value.enumerated.item[0];
	unsigned int val;
	bool changed = false;

	if (item >= e->items)
		return -EINVAL;

	if (rate_num >= ARRAY_SIZE(cs48l32_codec->dsp_dma_rates))
		return -EINVAL;

	val = snd_soc_enum_item_to_val(e, item);

	snd_soc_dapm_mutex_lock(dapm);
	if (cs48l32_codec->dsp_dma_rates[rate_num] != val) {
		cs48l32_codec->dsp_dma_rates[rate_num] = val;
		changed = true;
	}
	snd_soc_dapm_mutex_unlock(dapm);

	return changed;
}

static const struct soc_enum cs48l32_dsp_rate_enum[] = {
	/* RX rates */
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      0,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text, cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      1,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text, cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      2,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text, cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      3,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text, cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      4,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text,  cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      5,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text, cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      6,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text, cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      7,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text, cs48l32_rate_val),
	/* TX rates */
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      8,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text, cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      9,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text, cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      10,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text, cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      11,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text, cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      12,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text, cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      13,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text, cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      14,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text, cs48l32_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0,
			      15,
			      ARRAY_SIZE(cs48l32_rate_text),
			      cs48l32_rate_text, cs48l32_rate_val),
};

static int cs48l32_dsp_pre_run(struct wm_adsp *dsp)
{
	struct cs48l32_codec *cs48l32_codec = container_of(dsp, struct cs48l32_codec, dsp);
	unsigned int reg;
	const u8 *rate = cs48l32_codec->dsp_dma_rates;
	int i;

	reg = dsp->cs_dsp.base + CS48L32_HALO_SAMPLE_RATE_RX1;
	for (i = 0; i < CS48L32_DSP_N_RX_CHANNELS; ++i) {
		regmap_update_bits(dsp->cs_dsp.regmap, reg, CS48L32_HALO_DSP_RATE_MASK, *rate);
		reg += 8;
		rate++;
	}

	reg = dsp->cs_dsp.base + CS48L32_HALO_SAMPLE_RATE_TX1;
	for (i = 0; i < CS48L32_DSP_N_TX_CHANNELS; ++i) {
		regmap_update_bits(dsp->cs_dsp.regmap, reg, CS48L32_HALO_DSP_RATE_MASK, *rate);
		reg += 8;
		rate++;
	}

	usleep_range(300, 600);

	return 0;
}

static void cs48l32_dsp_memory_disable(struct cs48l32_codec *cs48l32_codec,
				       const struct cs48l32_dsp_power_regs *regs)
{
	struct regmap *regmap = cs48l32_codec->core.regmap;
	int i, j, ret;

	for (i = 0; i < regs->n_pwd; ++i) {
		ret = regmap_write(regmap, regs->pwd[i], 0);
		if (ret)
			goto err;
	}

	for (i = 0; i < regs->n_ext; ++i) {
		for (j = regs->ext[i].start; j <= regs->ext[i].end; j += 4) {
			ret = regmap_write(regmap, j, 0);
			if (ret)
				goto err;
		}
	}

	return;

err:
	dev_warn(cs48l32_codec->core.dev, "Failed to write SRAM enables (%d)\n", ret);
}

static int cs48l32_dsp_memory_enable(struct cs48l32_codec *cs48l32_codec,
				     const struct cs48l32_dsp_power_regs *regs)
{
	struct regmap *regmap = cs48l32_codec->core.regmap;
	int i, j, ret;

	/* disable power-off */
	for (i = 0; i < regs->n_ext; ++i) {
		for (j = regs->ext[i].start; j <= regs->ext[i].end; j += 4) {
			ret = regmap_write(regmap, j, 0x3);
			if (ret)
				goto err;
		}
	}

	/* power-up the banks in sequence */
	for (i = 0; i < regs->n_pwd; ++i) {
		ret = regmap_write(regmap, regs->pwd[i], 0x1);
		if (ret)
			goto err;

		udelay(1); /* allow bank to power-up */

		ret = regmap_write(regmap, regs->pwd[i], 0x3);
		if (ret)
			goto err;

		udelay(1); /* allow bank to power-up */
	}

	return 0;

err:
	dev_err(cs48l32_codec->core.dev, "Failed to write SRAM enables (%d)\n", ret);
	cs48l32_dsp_memory_disable(cs48l32_codec, regs);

	return ret;
}

static int cs48l32_dsp_freq_update(struct snd_soc_dapm_widget *w, unsigned int freq_reg,
				   unsigned int freqsel_reg)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs48l32_codec->core.regmap;
	struct wm_adsp *dsp = &cs48l32_codec->dsp;
	int ret;
	unsigned int freq, freq_sel, freq_sts;

	if (!freq_reg)
		return -EINVAL;

	ret = regmap_read(regmap, freq_reg, &freq);
	if (ret) {
		dev_err(component->dev, "Failed to read #%x: %d\n", freq_reg, ret);
		return ret;
	}

	if (freqsel_reg) {
		freq_sts = (freq & CS48L32_SYSCLK_FREQ_STS_MASK) >> CS48L32_SYSCLK_FREQ_STS_SHIFT;

		ret = regmap_read(regmap, freqsel_reg, &freq_sel);
		if (ret) {
			dev_err(component->dev, "Failed to read #%x: %d\n", freqsel_reg, ret);
			return ret;
		}
		freq_sel = (freq_sel & CS48L32_SYSCLK_FREQ_MASK) >> CS48L32_SYSCLK_FREQ_SHIFT;

		if (freq_sts != freq_sel) {
			dev_err(component->dev, "SYSCLK FREQ (#%x) != FREQ STS (#%x)\n",
				freq_sel, freq_sts);
			return -ETIMEDOUT;
		}
	}

	freq &= CS48L32_DSP_CLK_FREQ_MASK;
	freq >>= CS48L32_DSP_CLK_FREQ_SHIFT;

	ret = regmap_write(dsp->cs_dsp.regmap,
			   dsp->cs_dsp.base + CS48L32_DSP_CLOCK_FREQ_OFFS, freq);
	if (ret) {
		dev_err(component->dev, "Failed to set HALO clock freq: %d\n", ret);
		return ret;
	}

	return 0;
}

static int cs48l32_dsp_freq_ev(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		return cs48l32_dsp_freq_update(w, CS48L32_SYSTEM_CLOCK2, CS48L32_SYSTEM_CLOCK1);
	default:
		return 0;
	}
}

static irqreturn_t cs48l32_irq(int irq, void *data)
{
	static const unsigned int eint1_regs[] = {
		CS48L32_IRQ1_EINT_9, CS48L32_IRQ1_MASK_9,
		CS48L32_IRQ1_EINT_7, CS48L32_IRQ1_MASK_7
	};
	u32 reg_vals[4];
	struct cs48l32_codec *cs48l32_codec = data;
	struct regmap *regmap = cs48l32_codec->core.regmap;
	irqreturn_t result = IRQ_NONE;
	unsigned int eint_pending;
	int i, ret;

	static_assert(ARRAY_SIZE(eint1_regs) == ARRAY_SIZE(reg_vals));

	ret = pm_runtime_resume_and_get(cs48l32_codec->core.dev);
	if (ret) {
		dev_warn(cs48l32_codec->core.dev, "irq could not get pm runtime: %d\n", ret);
		return IRQ_NONE;
	}

	ret = regmap_read(regmap, CS48L32_IRQ1_STATUS, &eint_pending);
	if (ret) {
		dev_warn(cs48l32_codec->core.dev, "Read IRQ1_STATUS failed: %d\n", ret);
		return IRQ_NONE;
	}
	if ((eint_pending & CS48L32_IRQ1_STS_MASK) == 0)
		goto out;

	ret = regmap_multi_reg_read(regmap, eint1_regs, reg_vals, ARRAY_SIZE(reg_vals));
	if (ret) {
		dev_warn(cs48l32_codec->core.dev, "Read IRQ regs failed: %d\n", ret);
		return IRQ_NONE;
	}

	for (i = 0; i < ARRAY_SIZE(reg_vals); i += 2) {
		reg_vals[i] &= ~reg_vals[i + 1];
		regmap_write(regmap, eint1_regs[i], reg_vals[i]);
	}

	if (reg_vals[0] & CS48L32_DSP1_IRQ0_EINT1_MASK)
		wm_adsp_compr_handle_irq(&cs48l32_codec->dsp);

	if (reg_vals[2] & CS48L32_DSP1_MPU_ERR_EINT1_MASK) {
		dev_warn(cs48l32_codec->core.dev, "MPU err IRQ\n");
		wm_halo_bus_error(irq, &cs48l32_codec->dsp);
	}

	if (reg_vals[2] & CS48L32_DSP1_WDT_EXPIRE_EINT1_MASK) {
		dev_warn(cs48l32_codec->core.dev, "WDT expire IRQ\n");
		wm_halo_wdt_expire(irq, &cs48l32_codec->dsp);
	}

	result = IRQ_HANDLED;

out:
	pm_runtime_put_autosuspend(cs48l32_codec->core.dev);

	return result;
}

static int cs48l32_get_dspclk_setting(struct cs48l32_codec *cs48l32_codec, unsigned int freq,
				      int src, unsigned int *val)
{
	freq /= 15625; /* convert to 1/64ths of 1MHz */
	*val |= freq << CS48L32_DSP_CLK_FREQ_SHIFT;

	return 0;
}

static int cs48l32_get_sysclk_setting(unsigned int freq)
{
	switch (freq) {
	case 0:
	case 5644800:
	case 6144000:
		return CS48L32_SYSCLK_RATE_6MHZ;
	case 11289600:
	case 12288000:
		return CS48L32_SYSCLK_RATE_12MHZ << CS48L32_SYSCLK_FREQ_SHIFT;
	case 22579200:
	case 24576000:
		return CS48L32_SYSCLK_RATE_24MHZ << CS48L32_SYSCLK_FREQ_SHIFT;
	case 45158400:
	case 49152000:
		return CS48L32_SYSCLK_RATE_49MHZ << CS48L32_SYSCLK_FREQ_SHIFT;
	case 90316800:
	case 98304000:
		return CS48L32_SYSCLK_RATE_98MHZ << CS48L32_SYSCLK_FREQ_SHIFT;
	default:
		return -EINVAL;
	}
}

static int cs48l32_set_pdm_fllclk(struct snd_soc_component *component, int source)
{
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs48l32_codec->core.regmap;
	unsigned int val;

	switch (source) {
	case CS48L32_PDMCLK_SRC_IN1_PDMCLK:
	case CS48L32_PDMCLK_SRC_IN2_PDMCLK:
	case CS48L32_PDMCLK_SRC_IN3_PDMCLK:
	case CS48L32_PDMCLK_SRC_IN4_PDMCLK:
	case CS48L32_PDMCLK_SRC_AUXPDM1_CLK:
	case CS48L32_PDMCLK_SRC_AUXPDM2_CLK:
		val = source << CS48L32_PDM_FLLCLK_SRC_SHIFT;
		break;
	default:
		dev_err(cs48l32_codec->core.dev, "Invalid PDM FLLCLK src %d\n", source);
		return -EINVAL;
	}

	return regmap_update_bits(regmap, CS48L32_INPUT_CONTROL2,
				  CS48L32_PDM_FLLCLK_SRC_MASK, val);
}

static int cs48l32_set_sysclk(struct snd_soc_component *component, int clk_id, int source,
			      unsigned int freq, int dir)
{
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs48l32_codec->core.regmap;
	char *name;
	unsigned int reg;
	unsigned int mask = CS48L32_SYSCLK_SRC_MASK;
	unsigned int val = source << CS48L32_SYSCLK_SRC_SHIFT;
	int clk_freq_sel, *clk;

	switch (clk_id) {
	case CS48L32_CLK_SYSCLK_1:
		name = "SYSCLK";
		reg = CS48L32_SYSTEM_CLOCK1;
		clk = &cs48l32_codec->sysclk;
		clk_freq_sel = cs48l32_get_sysclk_setting(freq);
		mask |= CS48L32_SYSCLK_FREQ_MASK | CS48L32_SYSCLK_FRAC_MASK;
		break;
	case CS48L32_CLK_DSPCLK:
		name = "DSPCLK";
		reg = CS48L32_DSP_CLOCK1;
		clk = &cs48l32_codec->dspclk;
		clk_freq_sel = cs48l32_get_dspclk_setting(cs48l32_codec, freq, source, &val);
		mask |= CS48L32_DSP_CLK_FREQ_MASK;
		break;
	case CS48L32_CLK_PDM_FLLCLK:
		return cs48l32_set_pdm_fllclk(component, source);
	default:
		return -EINVAL;
	}

	if (clk_freq_sel < 0) {
		dev_err(cs48l32_codec->core.dev, "Failed to get %s setting for %dHZ\n", name, freq);
		return clk_freq_sel;
	}

	*clk = freq;

	if (freq == 0) {
		dev_dbg(cs48l32_codec->core.dev, "%s cleared\n", name);
		return 0;
	}

	val |= clk_freq_sel;

	if (freq % 6144000)
		val |= CS48L32_SYSCLK_FRAC_MASK;

	dev_dbg(cs48l32_codec->core.dev, "%s set to %uHz", name, freq);

	return regmap_update_bits(regmap, reg, mask, val);
}

static int cs48l32_is_enabled_fll(struct cs48l32_fll *fll, int base)
{
	struct regmap *regmap = fll->codec->core.regmap;
	unsigned int reg;
	int ret;

	ret = regmap_read(regmap, base + CS48L32_FLL_CONTROL1_OFFS, &reg);
	if (ret != 0) {
		cs48l32_fll_err(fll, "Failed to read current state: %d\n", ret);
		return ret;
	}

	return reg & CS48L32_FLL_EN_MASK;
}

static int cs48l32_wait_for_fll(struct cs48l32_fll *fll, bool requested)
{
	struct regmap *regmap = fll->codec->core.regmap;
	unsigned int val = 0;
	int i;

	cs48l32_fll_dbg(fll, "Waiting for FLL...\n");

	for (i = 0; i < 30; i++) {
		regmap_read(regmap, fll->sts_addr, &val);
		if (!!(val & fll->sts_mask) == requested)
			return 0;

		switch (i) {
		case 0 ... 5:
			usleep_range(75, 125);
			break;
		case 6 ... 20:
			usleep_range(750, 1250);
			break;
		default:
			fsleep(20000);
			break;
		}
	}

	cs48l32_fll_warn(fll, "Timed out waiting for %s\n", requested ? "lock" : "unlock");

	return -ETIMEDOUT;
}

static int cs48l32_fllhj_disable(struct cs48l32_fll *fll)
{
	struct cs48l32 *cs48l32 = &fll->codec->core;
	bool change;

	cs48l32_fll_dbg(fll, "Disabling FLL\n");

	/*
	 * Disable lockdet, but don't set ctrl_upd update bit. This allows the
	 * lock status bit to clear as normal, but should the FLL be enabled
	 * again due to a control clock being required, the lock won't re-assert
	 * as the FLL config registers are automatically applied when the FLL
	 * enables.
	 */
	regmap_set_bits(cs48l32->regmap,
			fll->base + CS48L32_FLL_CONTROL1_OFFS,
			CS48L32_FLL_HOLD_MASK);
	regmap_clear_bits(cs48l32->regmap,
			  fll->base + CS48L32_FLL_CONTROL2_OFFS,
			  CS48L32_FLL_LOCKDET_MASK);
	regmap_set_bits(cs48l32->regmap,
			fll->base + CS48L32_FLL_CONTROL5_OFFS,
			CS48L32_FLL_FRC_INTEG_UPD_MASK);
	regmap_update_bits_check(cs48l32->regmap,
				 fll->base + CS48L32_FLL_CONTROL1_OFFS,
				 CS48L32_FLL_EN_MASK,
				 0,
				 &change);

	cs48l32_wait_for_fll(fll, false);

	/*
	 * ctrl_up gates the writes to all the fll's registers, setting it to 0
	 * here ensures that after a runtime suspend/resume cycle when one
	 * enables the fll then ctrl_up is the last bit that is configured
	 * by the fll enable code rather than the cache sync operation which
	 * would have updated it much earlier before writing out all fll
	 * registers
	 */
	regmap_clear_bits(cs48l32->regmap,
			  fll->base + CS48L32_FLL_CONTROL1_OFFS,
			  CS48L32_FLL_CTRL_UPD_MASK);

	if (change)
		pm_runtime_put_autosuspend(cs48l32->dev);

	return 0;
}

static int cs48l32_fllhj_apply(struct cs48l32_fll *fll, int fin)
{
	struct regmap *regmap = fll->codec->core.regmap;
	int refdiv, fref, fout, lockdet_thr, fbdiv, fllgcd;
	bool frac = false;
	unsigned int fll_n, min_n, max_n, ratio, theta, lambda, hp;
	unsigned int gains, num;

	cs48l32_fll_dbg(fll, "fin=%d, fout=%d\n", fin, fll->fout);

	for (refdiv = 0; refdiv < 4; refdiv++) {
		if ((fin / (1 << refdiv)) <= CS48L32_FLLHJ_MAX_THRESH)
			break;
	}

	fref = fin / (1 << refdiv);
	fout = fll->fout;
	frac = fout % fref;

	/*
	 * Use simple heuristic approach to find a configuration that
	 * should work for most input clocks.
	 */
	if (fref < CS48L32_FLLHJ_LOW_THRESH) {
		lockdet_thr = 2;
		gains = CS48L32_FLLHJ_LOW_GAINS;

		if (frac)
			fbdiv = 256;
		else
			fbdiv = 4;
	} else if (fref < CS48L32_FLLHJ_MID_THRESH) {
		lockdet_thr = 8;
		gains = CS48L32_FLLHJ_MID_GAINS;
		fbdiv = (frac) ? 16 : 2;
	} else {
		lockdet_thr = 8;
		gains = CS48L32_FLLHJ_HIGH_GAINS;
		fbdiv = 1;
	}
	/* Use high performance mode for fractional configurations. */
	if (frac) {
		hp = 3;
		min_n = CS48L32_FLLHJ_FRAC_MIN_N;
		max_n = CS48L32_FLLHJ_FRAC_MAX_N;
	} else {
		if (fref < CS48L32_FLLHJ_LP_INT_MODE_THRESH)
			hp = 0;
		else
			hp = 1;

		min_n = CS48L32_FLLHJ_INT_MIN_N;
		max_n = CS48L32_FLLHJ_INT_MAX_N;
	}

	ratio = fout / fref;

	cs48l32_fll_dbg(fll, "refdiv=%d, fref=%d, frac:%d\n", refdiv, fref, frac);

	while (ratio / fbdiv < min_n) {
		fbdiv /= 2;
		if (fbdiv < min_n) {
			cs48l32_fll_err(fll, "FBDIV (%u) < minimum N (%u)\n", fbdiv, min_n);
			return -EINVAL;
		}
	}
	while (frac && (ratio / fbdiv > max_n)) {
		fbdiv *= 2;
		if (fbdiv >= 1024) {
			cs48l32_fll_err(fll, "FBDIV (%u) >= 1024\n", fbdiv);
			return -EINVAL;
		}
	}

	cs48l32_fll_dbg(fll, "lockdet=%d, hp=#%x, fbdiv:%d\n", lockdet_thr, hp, fbdiv);

	/* Calculate N.K values */
	fllgcd = gcd(fout, fbdiv * fref);
	num = fout / fllgcd;
	lambda = (fref * fbdiv) / fllgcd;
	fll_n = num / lambda;
	theta = num % lambda;

	cs48l32_fll_dbg(fll, "fll_n=%d, gcd=%d, theta=%d, lambda=%d\n",
			fll_n, fllgcd, theta, lambda);

	/* Some sanity checks before any registers are written. */
	if (fll_n < min_n || fll_n > max_n) {
		cs48l32_fll_err(fll, "N not in valid %s mode range %d-%d: %d\n",
				frac ? "fractional" : "integer", min_n, max_n, fll_n);
		return -EINVAL;
	}
	if (fbdiv < 1 || (frac && fbdiv >= 1024) || (!frac && fbdiv >= 256)) {
		cs48l32_fll_err(fll, "Invalid fbdiv for %s mode (%u)\n",
				frac ? "fractional" : "integer", fbdiv);
		return -EINVAL;
	}

	/* clear the ctrl_upd bit to guarantee we write to it later. */
	regmap_update_bits(regmap,
			   fll->base + CS48L32_FLL_CONTROL2_OFFS,
			   CS48L32_FLL_LOCKDET_THR_MASK |
			   CS48L32_FLL_PHASEDET_MASK |
			   CS48L32_FLL_REFCLK_DIV_MASK |
			   CS48L32_FLL_N_MASK |
			   CS48L32_FLL_CTRL_UPD_MASK,
			   (lockdet_thr << CS48L32_FLL_LOCKDET_THR_SHIFT) |
			   (1 << CS48L32_FLL_PHASEDET_SHIFT) |
			   (refdiv << CS48L32_FLL_REFCLK_DIV_SHIFT) |
			   (fll_n << CS48L32_FLL_N_SHIFT));

	regmap_update_bits(regmap,
			   fll->base + CS48L32_FLL_CONTROL3_OFFS,
			   CS48L32_FLL_LAMBDA_MASK |
			   CS48L32_FLL_THETA_MASK,
			   (lambda << CS48L32_FLL_LAMBDA_SHIFT) |
			   (theta << CS48L32_FLL_THETA_SHIFT));

	regmap_update_bits(regmap,
			   fll->base + CS48L32_FLL_CONTROL4_OFFS,
			   (0xffff << CS48L32_FLL_FD_GAIN_COARSE_SHIFT) |
			   CS48L32_FLL_HP_MASK |
			   CS48L32_FLL_FB_DIV_MASK,
			   (gains << CS48L32_FLL_FD_GAIN_COARSE_SHIFT) |
			   (hp << CS48L32_FLL_HP_SHIFT) |
			   (fbdiv << CS48L32_FLL_FB_DIV_SHIFT));

	return 0;
}

static int cs48l32_fllhj_enable(struct cs48l32_fll *fll)
{
	struct cs48l32 *cs48l32 = &fll->codec->core;
	int already_enabled = cs48l32_is_enabled_fll(fll, fll->base);
	int ret;

	if (already_enabled < 0)
		return already_enabled;

	if (!already_enabled)
		pm_runtime_get_sync(cs48l32->dev);

	cs48l32_fll_dbg(fll, "Enabling FLL, initially %s\n",
			str_enabled_disabled(already_enabled));

	/* FLLn_HOLD must be set before configuring any registers */
	regmap_set_bits(cs48l32->regmap,
			fll->base + CS48L32_FLL_CONTROL1_OFFS,
			CS48L32_FLL_HOLD_MASK);

	/* Apply refclk */
	ret = cs48l32_fllhj_apply(fll, fll->ref_freq);
	if (ret) {
		cs48l32_fll_err(fll, "Failed to set FLL: %d\n", ret);
		goto out;
	}
	regmap_update_bits(cs48l32->regmap,
			   fll->base + CS48L32_FLL_CONTROL2_OFFS,
			   CS48L32_FLL_REFCLK_SRC_MASK,
			   fll->ref_src << CS48L32_FLL_REFCLK_SRC_SHIFT);

	regmap_set_bits(cs48l32->regmap,
			fll->base + CS48L32_FLL_CONTROL1_OFFS,
			CS48L32_FLL_EN_MASK);

out:
	regmap_set_bits(cs48l32->regmap,
			fll->base + CS48L32_FLL_CONTROL2_OFFS,
			CS48L32_FLL_LOCKDET_MASK);

	regmap_set_bits(cs48l32->regmap,
			fll->base + CS48L32_FLL_CONTROL1_OFFS,
			CS48L32_FLL_CTRL_UPD_MASK);

	/* Release the hold so that flln locks to external frequency */
	regmap_clear_bits(cs48l32->regmap,
			  fll->base + CS48L32_FLL_CONTROL1_OFFS,
			  CS48L32_FLL_HOLD_MASK);

	if (!already_enabled)
		cs48l32_wait_for_fll(fll, true);

	return 0;
}

static int cs48l32_fllhj_validate(struct cs48l32_fll *fll,
				  unsigned int ref_in,
				  unsigned int fout)
{
	if (fout && !ref_in) {
		cs48l32_fll_err(fll, "fllout set without valid input clk\n");
		return -EINVAL;
	}

	if (fll->fout && fout != fll->fout) {
		cs48l32_fll_err(fll, "Can't change output on active FLL\n");
		return -EINVAL;
	}

	if (ref_in / CS48L32_FLL_MAX_REFDIV > CS48L32_FLLHJ_MAX_THRESH) {
		cs48l32_fll_err(fll, "Can't scale %dMHz to <=13MHz\n", ref_in);
		return -EINVAL;
	}

	if (fout > CS48L32_FLL_MAX_FOUT) {
		cs48l32_fll_err(fll, "Fout=%dMHz exceeds maximum %dMHz\n",
				fout, CS48L32_FLL_MAX_FOUT);
		return -EINVAL;
	}

	return 0;
}

static int cs48l32_fllhj_set_refclk(struct cs48l32_fll *fll, int source,
				    unsigned int fin, unsigned int fout)
{
	int ret = 0;

	if (fll->ref_src == source && fll->ref_freq == fin && fll->fout == fout)
		return 0;

	if (fin && fout && cs48l32_fllhj_validate(fll, fin, fout))
		return -EINVAL;

	fll->ref_src = source;
	fll->ref_freq = fin;
	fll->fout = fout;

	if (fout)
		ret = cs48l32_fllhj_enable(fll);
	else
		cs48l32_fllhj_disable(fll);

	return ret;
}

static int cs48l32_init_fll(struct cs48l32_fll *fll)
{
	fll->ref_src = CS48L32_FLL_SRC_NONE;

	return 0;
}

static int cs48l32_set_fll(struct snd_soc_component *component, int fll_id,
			   int source, unsigned int fref, unsigned int fout)
{
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);

	switch (fll_id) {
	case CS48L32_FLL1_REFCLK:
		break;
	default:
		return -EINVAL;
	}

	return cs48l32_fllhj_set_refclk(&cs48l32_codec->fll, source, fref, fout);
}

static int cs48l32_asp_dai_probe(struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs48l32_codec->core.regmap;
	unsigned int pin_reg, last_pin_reg, hiz_reg;

	switch (dai->id) {
	case 1:
		pin_reg = CS48L32_GPIO3_CTRL1;
		hiz_reg = CS48L32_ASP1_CONTROL3;
		break;
	case 2:
		pin_reg = CS48L32_GPIO7_CTRL1;
		hiz_reg = CS48L32_ASP2_CONTROL3;
		break;
	default:
		return -EINVAL;
	}

	for (last_pin_reg = pin_reg + 12; pin_reg <= last_pin_reg; ++pin_reg)
		regmap_clear_bits(regmap, pin_reg, CS48L32_GPIOX_CTRL1_FN_MASK);

	/* DOUT high-impendance when not transmitting */
	regmap_set_bits(regmap, hiz_reg, CS48L32_ASP_DOUT_HIZ_MASK);

	return 0;
}

static int cs48l32_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs48l32_codec->core.regmap;
	unsigned int val = 0U;
	unsigned int base = dai->driver->base;
	unsigned int mask = CS48L32_ASP_FMT_MASK | CS48L32_ASP_BCLK_INV_MASK |
			    CS48L32_ASP_BCLK_MSTR_MASK |
			    CS48L32_ASP_FSYNC_INV_MASK |
			    CS48L32_ASP_FSYNC_MSTR_MASK;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		val |= (CS48L32_ASP_FMT_DSP_MODE_A << CS48L32_ASP_FMT_SHIFT);
		break;
	case SND_SOC_DAIFMT_DSP_B:
		if ((fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) != SND_SOC_DAIFMT_BP_FP) {
			cs48l32_asp_err(dai, "DSP_B cannot be clock consumer\n");
			return -EINVAL;
		}
		val |= (CS48L32_ASP_FMT_DSP_MODE_B << CS48L32_ASP_FMT_SHIFT);
		break;
	case SND_SOC_DAIFMT_I2S:
		val |= (CS48L32_ASP_FMT_I2S_MODE << CS48L32_ASP_FMT_SHIFT);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		if ((fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) != SND_SOC_DAIFMT_BP_FP) {
			cs48l32_asp_err(dai, "LEFT_J cannot be clock consumer\n");
			return -EINVAL;
		}
		val |= (CS48L32_ASP_FMT_LEFT_JUSTIFIED_MODE << CS48L32_ASP_FMT_SHIFT);
		break;
	default:
		cs48l32_asp_err(dai, "Unsupported DAI format %d\n",
				fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BC_FC:
		break;
	case SND_SOC_DAIFMT_BC_FP:
		val |= CS48L32_ASP_FSYNC_MSTR_MASK;
		break;
	case SND_SOC_DAIFMT_BP_FC:
		val |= CS48L32_ASP_BCLK_MSTR_MASK;
		break;
	case SND_SOC_DAIFMT_BP_FP:
		val |= CS48L32_ASP_BCLK_MSTR_MASK;
		val |= CS48L32_ASP_FSYNC_MSTR_MASK;
		break;
	default:
		cs48l32_asp_err(dai, "Unsupported clock direction %d\n",
				fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		val |= CS48L32_ASP_BCLK_INV_MASK;
		val |= CS48L32_ASP_FSYNC_INV_MASK;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		val |= CS48L32_ASP_BCLK_INV_MASK;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		val |= CS48L32_ASP_FSYNC_INV_MASK;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(regmap, base + CS48L32_ASP_CONTROL2, mask, val);

	return 0;
}

static const struct {
	u32 freq;
	u32 id;
} cs48l32_sclk_rates[] = {
	{ 128000,   12 },
	{ 176400,   13 },
	{ 192000,   14 },
	{ 256000,   15 },
	{ 352800,   16 },
	{ 384000,   17 },
	{ 512000,   18 },
	{ 705600,   19 },
	{ 768000,   21 },
	{ 1024000,  23 },
	{ 1411200,  25 },
	{ 1536000,  27 },
	{ 2048000,  29 },
	{ 2822400,  31 },
	{ 3072000,  33 },
	{ 4096000,  36 },
	{ 5644800,  38 },
	{ 6144000,  40 },
	{ 8192000,  47 },
	{ 11289600, 49 },
	{ 12288000, 51 },
	{ 22579200, 57 },
	{ 24576000, 59 },
};

#define CS48L32_48K_RATE_MASK	0x0e00fe
#define CS48L32_44K1_RATE_MASK	0x00fe00
#define CS48L32_RATE_MASK	(CS48L32_48K_RATE_MASK | CS48L32_44K1_RATE_MASK)

static const unsigned int cs48l32_sr_vals[] = {
	0,
	12000,  /* CS48L32_48K_RATE_MASK */
	24000,  /* CS48L32_48K_RATE_MASK */
	48000,  /* CS48L32_48K_RATE_MASK */
	96000,  /* CS48L32_48K_RATE_MASK */
	192000, /* CS48L32_48K_RATE_MASK */
	384000, /* CS48L32_48K_RATE_MASK */
	768000, /* CS48L32_48K_RATE_MASK */
	0,
	11025,  /* CS48L32_44K1_RATE_MASK */
	22050,  /* CS48L32_44K1_RATE_MASK */
	44100,  /* CS48L32_44K1_RATE_MASK */
	88200,  /* CS48L32_44K1_RATE_MASK */
	176400, /* CS48L32_44K1_RATE_MASK */
	352800, /* CS48L32_44K1_RATE_MASK */
	705600, /* CS48L32_44K1_RATE_MASK */
	0,
	8000,   /* CS48L32_48K_RATE_MASK */
	16000,  /* CS48L32_48K_RATE_MASK */
	32000,  /* CS48L32_48K_RATE_MASK */
};

static const struct snd_pcm_hw_constraint_list cs48l32_constraint = {
	.count	= ARRAY_SIZE(cs48l32_sr_vals),
	.list	= cs48l32_sr_vals,
};

static int cs48l32_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct cs48l32_dai_priv *dai_priv = &cs48l32_codec->dai[dai->id - 1];
	unsigned int base_rate;

	if (!substream->runtime)
		return 0;

	switch (dai_priv->clk) {
	case CS48L32_CLK_SYSCLK_1:
	case CS48L32_CLK_SYSCLK_2:
	case CS48L32_CLK_SYSCLK_3:
	case CS48L32_CLK_SYSCLK_4:
		base_rate = cs48l32_codec->sysclk;
		break;
	default:
		return 0;
	}

	if (base_rate == 0)
		dai_priv->constraint.mask = CS48L32_RATE_MASK;
	else if (base_rate % 4000)
		dai_priv->constraint.mask = CS48L32_44K1_RATE_MASK;
	else
		dai_priv->constraint.mask = CS48L32_48K_RATE_MASK;

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  &dai_priv->constraint);
}

static int cs48l32_hw_params_rate(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct cs48l32_dai_priv *dai_priv = &cs48l32_codec->dai[dai->id - 1];
	unsigned int sr_val, sr_reg, rate;

	rate = params_rate(params);
	for (sr_val = 0; sr_val < ARRAY_SIZE(cs48l32_sr_vals); sr_val++)
		if (cs48l32_sr_vals[sr_val] == rate)
			break;

	if (sr_val == ARRAY_SIZE(cs48l32_sr_vals)) {
		cs48l32_asp_err(dai, "Unsupported sample rate %dHz\n", rate);
		return -EINVAL;
	}

	switch (dai_priv->clk) {
	case CS48L32_CLK_SYSCLK_1:
		sr_reg = CS48L32_SAMPLE_RATE1;
		break;
	case CS48L32_CLK_SYSCLK_2:
		sr_reg = CS48L32_SAMPLE_RATE2;
		break;
	case CS48L32_CLK_SYSCLK_3:
		sr_reg = CS48L32_SAMPLE_RATE3;
		break;
	case CS48L32_CLK_SYSCLK_4:
		sr_reg = CS48L32_SAMPLE_RATE4;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, sr_reg, CS48L32_SAMPLE_RATE_1_MASK, sr_val);

	return 0;
}

static bool cs48l32_asp_cfg_changed(struct snd_soc_component *component,
				    unsigned int base, unsigned int sclk,
				    unsigned int slotws, unsigned int dataw)
{
	unsigned int val;

	val = snd_soc_component_read(component, base + CS48L32_ASP_CONTROL1);
	if (sclk != (val & CS48L32_ASP_BCLK_FREQ_MASK))
		return true;

	val = snd_soc_component_read(component, base + CS48L32_ASP_CONTROL2);
	if (slotws != (val & (CS48L32_ASP_RX_WIDTH_MASK | CS48L32_ASP_TX_WIDTH_MASK)))
		return true;

	val = snd_soc_component_read(component, base + CS48L32_ASP_DATA_CONTROL1);
	if (dataw != (val & (CS48L32_ASP_TX_WL_MASK)))
		return true;

	val = snd_soc_component_read(component, base + CS48L32_ASP_DATA_CONTROL5);
	if (dataw != (val & (CS48L32_ASP_RX_WL_MASK)))
		return true;

	return false;
}

static int cs48l32_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs48l32_codec->core.regmap;
	int base = dai->driver->base;
	int dai_id = dai->id - 1;
	unsigned int rate = params_rate(params);
	unsigned int dataw = snd_pcm_format_width(params_format(params));
	unsigned int asp_state = 0;
	int sclk, sclk_target;
	unsigned int slotw, n_slots, n_slots_multiple, val;
	int i, ret;

	cs48l32_asp_dbg(dai, "hwparams in: ch:%u dataw:%u rate:%u\n",
			params_channels(params), dataw, rate);
	/*
	 * The following calculations hold only under the assumption that
	 * symmetric_[rates|channels|samplebits] are set to 1
	 */
	if (cs48l32_codec->tdm_slots[dai_id]) {
		n_slots = cs48l32_codec->tdm_slots[dai_id];
		slotw = cs48l32_codec->tdm_width[dai_id];
	} else {
		n_slots = params_channels(params);
		slotw = dataw;
	}

	val = snd_soc_component_read(component, base + CS48L32_ASP_CONTROL2);
	val = (val & CS48L32_ASP_FMT_MASK) >> CS48L32_ASP_FMT_SHIFT;
	if (val == CS48L32_ASP_FMT_I2S_MODE)
		n_slots_multiple = 2;
	else
		n_slots_multiple = 1;

	sclk_target = snd_soc_tdm_params_to_bclk(params, slotw, n_slots, n_slots_multiple);
	if (sclk_target < 0) {
		cs48l32_asp_err(dai, "Invalid parameters\n");
		return sclk_target;
	}

	for (i = 0; i < ARRAY_SIZE(cs48l32_sclk_rates); i++) {
		if ((cs48l32_sclk_rates[i].freq >= sclk_target) &&
		    (cs48l32_sclk_rates[i].freq % rate == 0)) {
			sclk = cs48l32_sclk_rates[i].id;
			break;
		}
	}
	if (i == ARRAY_SIZE(cs48l32_sclk_rates)) {
		cs48l32_asp_err(dai, "Unsupported sample rate %dHz\n", rate);
		return -EINVAL;
	}

	cs48l32_asp_dbg(dai, "hwparams out: n_slots:%u dataw:%u slotw:%u bclk:%u bclkid:%u\n",
			n_slots, dataw, slotw, sclk_target, sclk);

	slotw = (slotw << CS48L32_ASP_TX_WIDTH_SHIFT) |
		(slotw << CS48L32_ASP_RX_WIDTH_SHIFT);

	if (!cs48l32_asp_cfg_changed(component, base, sclk, slotw, dataw))
		return cs48l32_hw_params_rate(substream, params, dai);

	/* ASP must be disabled while changing configuration */
	asp_state = snd_soc_component_read(component, base + CS48L32_ASP_ENABLES1);
	regmap_clear_bits(regmap, base + CS48L32_ASP_ENABLES1, 0xff00ff);

	ret = cs48l32_hw_params_rate(substream, params, dai);
	if (ret != 0)
		goto restore_asp;

	regmap_update_bits_async(regmap,
				 base + CS48L32_ASP_CONTROL1,
				 CS48L32_ASP_BCLK_FREQ_MASK,
				 sclk);
	regmap_update_bits_async(regmap,
				 base + CS48L32_ASP_CONTROL2,
				 CS48L32_ASP_RX_WIDTH_MASK | CS48L32_ASP_TX_WIDTH_MASK,
				 slotw);
	regmap_update_bits_async(regmap,
				 base + CS48L32_ASP_DATA_CONTROL1,
				 CS48L32_ASP_TX_WL_MASK,
				 dataw);
	regmap_update_bits(regmap,
			   base + CS48L32_ASP_DATA_CONTROL5,
			   CS48L32_ASP_RX_WL_MASK,
			   dataw);

restore_asp:
	/* Restore ASP TX/RX enable state */
	regmap_update_bits(regmap,
			   base + CS48L32_ASP_ENABLES1,
			   0xff00ff,
			   asp_state);
	return ret;
}

static const char *cs48l32_dai_clk_str(int clk_id)
{
	switch (clk_id) {
	case CS48L32_CLK_SYSCLK_1:
	case CS48L32_CLK_SYSCLK_2:
	case CS48L32_CLK_SYSCLK_3:
	case CS48L32_CLK_SYSCLK_4:
		return "SYSCLK";
	default:
		return "Unknown clock";
	}
}

static int cs48l32_dai_set_sysclk(struct snd_soc_dai *dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct cs48l32_dai_priv *dai_priv = &cs48l32_codec->dai[dai->id - 1];
	unsigned int base = dai->driver->base;
	unsigned int current_asp_rate, target_asp_rate;
	bool change_rate_domain = false;
	int ret;

	if (clk_id == dai_priv->clk)
		return 0;

	if (snd_soc_dai_active(dai)) {
		cs48l32_asp_err(dai, "Can't change clock on active DAI\n");
		return -EBUSY;
	}

	switch (clk_id) {
	case CS48L32_CLK_SYSCLK_1:
		target_asp_rate = 0U << CS48L32_ASP_RATE_SHIFT;
		break;
	case CS48L32_CLK_SYSCLK_2:
		target_asp_rate = 1U << CS48L32_ASP_RATE_SHIFT;
		break;
	case CS48L32_CLK_SYSCLK_3:
		target_asp_rate = 2U << CS48L32_ASP_RATE_SHIFT;
		break;
	case CS48L32_CLK_SYSCLK_4:
		target_asp_rate = 3U << CS48L32_ASP_RATE_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	dai_priv->clk = clk_id;
	cs48l32_asp_dbg(dai, "Setting to %s\n", cs48l32_dai_clk_str(clk_id));

	if (base) {
		ret = regmap_read(cs48l32_codec->core.regmap,
				  base + CS48L32_ASP_CONTROL1,
				  &current_asp_rate);
		if (ret != 0) {
			cs48l32_asp_err(dai, "Failed to check rate: %d\n", ret);
			return ret;
		}

		if ((current_asp_rate & CS48L32_ASP_RATE_MASK) !=
		    (target_asp_rate & CS48L32_ASP_RATE_MASK)) {
			change_rate_domain = true;

			mutex_lock(&cs48l32_codec->rate_lock);
			/* Guard the rate change with SYSCLK cycles */
			cs48l32_spin_sysclk(cs48l32_codec);
		}

		snd_soc_component_update_bits(component, base + CS48L32_ASP_CONTROL1,
					      CS48L32_ASP_RATE_MASK, target_asp_rate);

		if (change_rate_domain) {
			cs48l32_spin_sysclk(cs48l32_codec);
			mutex_unlock(&cs48l32_codec->rate_lock);
		}
	}

	return 0;
}

static void cs48l32_set_channels_to_mask(struct snd_soc_dai *dai,
					 unsigned int base,
					 int channels, unsigned int mask)
{
	struct snd_soc_component *component = dai->component;
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs48l32_codec->core.regmap;
	int slot, i, j = 0, shift;
	unsigned int frame_ctls[2] = {0, 0};

	for (i = 0; i < channels; ++i) {
		slot = ffs(mask) - 1;
		if (slot < 0)
			return;

		if (i - (j * 4) >= 4) {
			++j;
			if (j >= 2)
				break;
		}

		shift = (8 * (i - j * 4));

		frame_ctls[j] |= slot << shift;

		mask &= ~(1 << slot); /* ? mask ^= 1 << slot ? */
	}

	regmap_write(regmap, base, frame_ctls[0]);
	regmap_write(regmap, base + 0x4, frame_ctls[1]);

	if (mask)
		cs48l32_asp_warn(dai, "Too many channels in TDM mask\n");
}

static int cs48l32_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
				unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	int base = dai->driver->base;
	int rx_max_chan = dai->driver->playback.channels_max;
	int tx_max_chan = dai->driver->capture.channels_max;

	/* Only support TDM for the physical ASPs */
	if (dai->id > CS48L32_MAX_ASP)
		return -EINVAL;

	if (slots == 0) {
		tx_mask = (1 << tx_max_chan) - 1;
		rx_mask = (1 << rx_max_chan) - 1;
	}

	cs48l32_set_channels_to_mask(dai, base + CS48L32_ASP_FRAME_CONTROL1,
				   tx_max_chan, tx_mask);
	cs48l32_set_channels_to_mask(dai, base + CS48L32_ASP_FRAME_CONTROL5,
				   rx_max_chan, rx_mask);

	cs48l32_codec->tdm_width[dai->id - 1] = slot_width;
	cs48l32_codec->tdm_slots[dai->id - 1] = slots;

	return 0;
}

static const struct snd_soc_dai_ops cs48l32_dai_ops = {
	.probe = &cs48l32_asp_dai_probe,
	.startup = &cs48l32_startup,
	.set_fmt = &cs48l32_set_fmt,
	.set_tdm_slot = &cs48l32_set_tdm_slot,
	.hw_params = &cs48l32_hw_params,
	.set_sysclk = &cs48l32_dai_set_sysclk,
};

static int cs48l32_sysclk_ev(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);

	cs48l32_spin_sysclk(cs48l32_codec);

	return 0;
}

static int cs48l32_in_ev(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	unsigned int reg;

	if (w->shift % 2)
		reg = CS48L32_IN1L_CONTROL2;
	else
		reg = CS48L32_IN1R_CONTROL2;

	reg += (w->shift / 2) * (CS48L32_IN2L_CONTROL2 - CS48L32_IN1L_CONTROL2);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		switch (w->shift) {
		case CS48L32_IN1L_EN_SHIFT:
			snd_soc_component_update_bits(component,
						      CS48L32_ADC1L_ANA_CONTROL1,
						      CS48L32_ADC1x_INT_ENA_FRC_MASK,
						      CS48L32_ADC1x_INT_ENA_FRC_MASK);
			break;
		case CS48L32_IN1R_EN_SHIFT:
			snd_soc_component_update_bits(component,
						      CS48L32_ADC1R_ANA_CONTROL1,
						      CS48L32_ADC1x_INT_ENA_FRC_MASK,
						      CS48L32_ADC1x_INT_ENA_FRC_MASK);
			break;
		default:
			break;
		}
		cs48l32_codec->in_up_pending++;
		break;
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(200, 300);

		switch (w->shift) {
		case CS48L32_IN1L_EN_SHIFT:
			snd_soc_component_update_bits(component,
						      CS48L32_ADC1L_ANA_CONTROL1,
						      CS48L32_ADC1x_INT_ENA_FRC_MASK,
						      0);
			break;
		case CS48L32_IN1R_EN_SHIFT:
			snd_soc_component_update_bits(component,
						      CS48L32_ADC1R_ANA_CONTROL1,
						      CS48L32_ADC1x_INT_ENA_FRC_MASK,
						      0);
			break;

		default:
			break;
		}
		cs48l32_codec->in_up_pending--;
		snd_soc_component_update_bits(component, reg, CS48L32_INx_MUTE_MASK, 0);

		/* Uncached write-only register, no need for update_bits */
		if (!cs48l32_codec->in_up_pending) {
			snd_soc_component_write(component, cs48l32_codec->in_vu_reg,
						CS48L32_IN_VU_MASK);
		}
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, reg,
					      CS48L32_INx_MUTE_MASK, CS48L32_INx_MUTE_MASK);
		snd_soc_component_write(component, cs48l32_codec->in_vu_reg,
					CS48L32_IN_VU_MASK);
		break;
	default:
		break;
	}

	return 0;
}

static int cs48l32_in_put_volsw(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	int ret;

	ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret < 0)
		return ret;

	/*
	 * Uncached write-only register, no need for update_bits.
	 * Will fail if codec is off but that will be handled by cs48l32_in_ev
	 */
	snd_soc_component_write(component, cs48l32_codec->in_vu_reg, CS48L32_IN_VU);

	return ret;
}

static bool cs48l32_eq_filter_unstable(bool mode, __be16 in_a, __be16 in_b)
{
	s16 a = be16_to_cpu(in_a);
	s16 b = be16_to_cpu(in_b);

	if (!mode)
		return abs(a) > CS48L32_EQ_MAX_COEFF;

	if (abs(b) > CS48L32_EQ_MAX_COEFF)
		return true;

	if (abs((a << 16) / (CS48L32_EQ_MAX_COEFF + 1 - b)) >= ((CS48L32_EQ_MAX_COEFF + 1) << 4))
		return true;

	return false;
}

static int cs48l32_eq_ev(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs48l32_codec->core.regmap;
	unsigned int mode = cs48l32_codec->eq_mode[w->shift];
	unsigned int reg;
	__be16 *data = &cs48l32_codec->eq_coefficients[w->shift][0];
	int ret = 0;

	reg = CS48L32_EQ1_BAND1_COEFF1;
	reg += w->shift * (CS48L32_EQ2_BAND1_COEFF1 - CS48L32_EQ1_BAND1_COEFF1);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (cs48l32_eq_filter_unstable(!!mode, data[1], data[0]) ||
		    cs48l32_eq_filter_unstable(true, data[7], data[6]) ||
		    cs48l32_eq_filter_unstable(true, data[13], data[12]) ||
		    cs48l32_eq_filter_unstable(true, data[19], data[18]) ||
		    cs48l32_eq_filter_unstable(false, data[25], data[24])) {
			dev_err(cs48l32_codec->core.dev, "Rejecting unstable EQ coefficients.\n");
			ret = -EINVAL;
		} else {
			ret = regmap_raw_write(regmap, reg, data, CS48L32_EQ_BLOCK_SZ);
			if (ret < 0) {
				dev_err(cs48l32_codec->core.dev,
					"Error writing EQ coefficients: %d\n", ret);
				goto out;
			}

			ret = snd_soc_component_update_bits(component,
							    CS48L32_EQ_CONTROL2,
							    w->mask,
							    mode << w->shift);
			if (ret < 0) {
				dev_err(cs48l32_codec->core.dev,
					"Error writing EQ mode: %d\n", ret);
			}
		}
		break;
	default:
		break;
	}

out:
	return ret;
}

static const struct snd_kcontrol_new cs48l32_snd_controls[] = {
SOC_ENUM("IN1 OSR", cs48l32_in_dmic_osr[0]),
SOC_ENUM("IN2 OSR", cs48l32_in_dmic_osr[1]),

SOC_SINGLE_RANGE_TLV("IN1L Volume", CS48L32_IN1L_CONTROL2,
		     CS48L32_INx_PGA_VOL_SHIFT, 0x40, 0x5f, 0, cs48l32_ana_tlv),
SOC_SINGLE_RANGE_TLV("IN1R Volume", CS48L32_IN1R_CONTROL2,
		     CS48L32_INx_PGA_VOL_SHIFT, 0x40, 0x5f, 0, cs48l32_ana_tlv),

SOC_ENUM("IN HPF Cutoff Frequency", cs48l32_in_hpf_cut_enum),

SOC_SINGLE_EXT("IN1L LP Switch", CS48L32_IN1L_CONTROL1, CS48L32_INx_LP_MODE_SHIFT,
	       1, 0, snd_soc_get_volsw, cs48l32_low_power_mode_put),
SOC_SINGLE_EXT("IN1R LP Switch", CS48L32_IN1R_CONTROL1, CS48L32_INx_LP_MODE_SHIFT,
	       1, 0, snd_soc_get_volsw, cs48l32_low_power_mode_put),

SOC_SINGLE("IN1L HPF Switch", CS48L32_IN1L_CONTROL1, CS48L32_INx_HPF_SHIFT, 1, 0),
SOC_SINGLE("IN1R HPF Switch", CS48L32_IN1R_CONTROL1, CS48L32_INx_HPF_SHIFT, 1, 0),
SOC_SINGLE("IN2L HPF Switch", CS48L32_IN2L_CONTROL1, CS48L32_INx_HPF_SHIFT, 1, 0),
SOC_SINGLE("IN2R HPF Switch", CS48L32_IN2R_CONTROL1, CS48L32_INx_HPF_SHIFT, 1, 0),

SOC_SINGLE_EXT_TLV("IN1L Digital Volume", CS48L32_IN1L_CONTROL2,
		   CS48L32_INx_VOL_SHIFT, 0xbf, 0, snd_soc_get_volsw,
		   cs48l32_in_put_volsw, cs48l32_digital_tlv),
SOC_SINGLE_EXT_TLV("IN1R Digital Volume", CS48L32_IN1R_CONTROL2,
		   CS48L32_INx_VOL_SHIFT, 0xbf, 0, snd_soc_get_volsw,
		   cs48l32_in_put_volsw, cs48l32_digital_tlv),
SOC_SINGLE_EXT_TLV("IN2L Digital Volume", CS48L32_IN2L_CONTROL2,
		   CS48L32_INx_VOL_SHIFT, 0xbf, 0, snd_soc_get_volsw,
		   cs48l32_in_put_volsw, cs48l32_digital_tlv),
SOC_SINGLE_EXT_TLV("IN2R Digital Volume", CS48L32_IN2R_CONTROL2,
		   CS48L32_INx_VOL_SHIFT, 0xbf, 0, snd_soc_get_volsw,
		   cs48l32_in_put_volsw, cs48l32_digital_tlv),

SOC_ENUM("Input Ramp Up", cs48l32_in_vi_ramp),
SOC_ENUM("Input Ramp Down", cs48l32_in_vd_ramp),

CS48L32_RATE_ENUM("Ultrasonic 1 Rate", cs48l32_us_output_rate[0]),
CS48L32_RATE_ENUM("Ultrasonic 2 Rate", cs48l32_us_output_rate[1]),

SOC_ENUM("Ultrasonic 1 Freq", cs48l32_us_freq[0]),
SOC_ENUM("Ultrasonic 2 Freq", cs48l32_us_freq[1]),

SOC_SINGLE_TLV("Ultrasonic 1 Volume", CS48L32_US1_CONTROL, CS48L32_US1_GAIN_SHIFT,
	       3, 0, cs48l32_us_tlv),
SOC_SINGLE_TLV("Ultrasonic 2 Volume", CS48L32_US2_CONTROL, CS48L32_US1_GAIN_SHIFT,
	       3, 0, cs48l32_us_tlv),

SOC_ENUM("Ultrasonic 1 Detect Threshold", cs48l32_us_det_thr[0]),
SOC_ENUM("Ultrasonic 2 Detect Threshold", cs48l32_us_det_thr[1]),

SOC_ENUM("Ultrasonic 1 Detect Pulse Length", cs48l32_us_det_num[0]),
SOC_ENUM("Ultrasonic 2 Detect Pulse Length", cs48l32_us_det_num[1]),

SOC_ENUM("Ultrasonic 1 Detect Hold", cs48l32_us_det_hold[0]),
SOC_ENUM("Ultrasonic 2 Detect Hold", cs48l32_us_det_hold[1]),

SOC_ENUM("Ultrasonic 1 Detect Decay", cs48l32_us_det_dcy[0]),
SOC_ENUM("Ultrasonic 2 Detect Decay", cs48l32_us_det_dcy[1]),

SOC_SINGLE("Ultrasonic 1 Detect LPF Switch",
	   CS48L32_US1_DET_CONTROL, CS48L32_US1_DET_LPF_SHIFT, 1, 0),
SOC_SINGLE("Ultrasonic 2 Detect LPF Switch",
	   CS48L32_US2_DET_CONTROL, CS48L32_US1_DET_LPF_SHIFT, 1, 0),

SOC_ENUM("Ultrasonic 1 Detect LPF Cut-off", cs48l32_us_det_lpf_cut[0]),
SOC_ENUM("Ultrasonic 2 Detect LPF Cut-off", cs48l32_us_det_lpf_cut[1]),

CS48L32_MIXER_CONTROLS("EQ1", CS48L32_EQ1_INPUT1),
CS48L32_MIXER_CONTROLS("EQ2", CS48L32_EQ2_INPUT1),
CS48L32_MIXER_CONTROLS("EQ3", CS48L32_EQ3_INPUT1),
CS48L32_MIXER_CONTROLS("EQ4", CS48L32_EQ4_INPUT1),

SOC_ENUM_EXT("EQ1 Mode", cs48l32_eq_mode[0], cs48l32_eq_mode_get, cs48l32_eq_mode_put),

CS48L32_EQ_COEFF_CONTROLS(EQ1),

SOC_SINGLE_TLV("EQ1 B1 Volume", CS48L32_EQ1_GAIN1, 0, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ1 B2 Volume", CS48L32_EQ1_GAIN1, 8, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ1 B3 Volume", CS48L32_EQ1_GAIN1, 16, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ1 B4 Volume", CS48L32_EQ1_GAIN1, 24, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ1 B5 Volume", CS48L32_EQ1_GAIN2, 0,  24, 0, cs48l32_eq_tlv),

SOC_ENUM_EXT("EQ2 Mode", cs48l32_eq_mode[1], cs48l32_eq_mode_get, cs48l32_eq_mode_put),
CS48L32_EQ_COEFF_CONTROLS(EQ2),
SOC_SINGLE_TLV("EQ2 B1 Volume", CS48L32_EQ2_GAIN1, 0, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ2 B2 Volume", CS48L32_EQ2_GAIN1, 8, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ2 B3 Volume", CS48L32_EQ2_GAIN1, 16, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ2 B4 Volume", CS48L32_EQ2_GAIN1, 24, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ2 B5 Volume", CS48L32_EQ2_GAIN2, 0,  24, 0, cs48l32_eq_tlv),

SOC_ENUM_EXT("EQ3 Mode", cs48l32_eq_mode[2], cs48l32_eq_mode_get, cs48l32_eq_mode_put),
CS48L32_EQ_COEFF_CONTROLS(EQ3),
SOC_SINGLE_TLV("EQ3 B1 Volume", CS48L32_EQ3_GAIN1, 0, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ3 B2 Volume", CS48L32_EQ3_GAIN1, 8, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ3 B3 Volume", CS48L32_EQ3_GAIN1, 16, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ3 B4 Volume", CS48L32_EQ3_GAIN1, 24, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ3 B5 Volume", CS48L32_EQ3_GAIN2, 0,  24, 0, cs48l32_eq_tlv),

SOC_ENUM_EXT("EQ4 Mode", cs48l32_eq_mode[3], cs48l32_eq_mode_get, cs48l32_eq_mode_put),
CS48L32_EQ_COEFF_CONTROLS(EQ4),
SOC_SINGLE_TLV("EQ4 B1 Volume", CS48L32_EQ4_GAIN1, 0, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ4 B2 Volume", CS48L32_EQ4_GAIN1, 8, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ4 B3 Volume", CS48L32_EQ4_GAIN1, 16, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ4 B4 Volume", CS48L32_EQ4_GAIN1, 24, 24, 0, cs48l32_eq_tlv),
SOC_SINGLE_TLV("EQ4 B5 Volume", CS48L32_EQ4_GAIN2, 0,  24, 0, cs48l32_eq_tlv),

CS48L32_MIXER_CONTROLS("DRC1L", CS48L32_DRC1L_INPUT1),
CS48L32_MIXER_CONTROLS("DRC1R", CS48L32_DRC1R_INPUT1),
CS48L32_MIXER_CONTROLS("DRC2L", CS48L32_DRC2L_INPUT1),
CS48L32_MIXER_CONTROLS("DRC2R", CS48L32_DRC2R_INPUT1),

SND_SOC_BYTES_MASK("DRC1 Coefficients", CS48L32_DRC1_CONTROL1, 4,
		   BIT(CS48L32_DRC1R_EN_SHIFT) | BIT(CS48L32_DRC1L_EN_SHIFT)),
SND_SOC_BYTES_MASK("DRC2 Coefficients", CS48L32_DRC2_CONTROL1, 4,
		   BIT(CS48L32_DRC1R_EN_SHIFT) | BIT(CS48L32_DRC1L_EN_SHIFT)),

CS48L32_MIXER_CONTROLS("LHPF1", CS48L32_LHPF1_INPUT1),
CS48L32_MIXER_CONTROLS("LHPF2", CS48L32_LHPF2_INPUT1),
CS48L32_MIXER_CONTROLS("LHPF3", CS48L32_LHPF3_INPUT1),
CS48L32_MIXER_CONTROLS("LHPF4", CS48L32_LHPF4_INPUT1),

CS48L32_LHPF_CONTROL("LHPF1 Coefficients", CS48L32_LHPF1_COEFF),
CS48L32_LHPF_CONTROL("LHPF2 Coefficients", CS48L32_LHPF2_COEFF),
CS48L32_LHPF_CONTROL("LHPF3 Coefficients", CS48L32_LHPF3_COEFF),
CS48L32_LHPF_CONTROL("LHPF4 Coefficients", CS48L32_LHPF4_COEFF),

SOC_ENUM("LHPF1 Mode", cs48l32_lhpf_mode[0]),
SOC_ENUM("LHPF2 Mode", cs48l32_lhpf_mode[1]),
SOC_ENUM("LHPF3 Mode", cs48l32_lhpf_mode[2]),
SOC_ENUM("LHPF4 Mode", cs48l32_lhpf_mode[3]),

CS48L32_RATE_CONTROL("Sample Rate 1", 1),
CS48L32_RATE_CONTROL("Sample Rate 2", 2),
CS48L32_RATE_CONTROL("Sample Rate 3", 3),
CS48L32_RATE_CONTROL("Sample Rate 4", 4),

CS48L32_RATE_ENUM("FX Rate", cs48l32_fx_rate),

CS48L32_RATE_ENUM("ISRC1 FSL", cs48l32_isrc_fsl[0]),
CS48L32_RATE_ENUM("ISRC2 FSL", cs48l32_isrc_fsl[1]),
CS48L32_RATE_ENUM("ISRC3 FSL", cs48l32_isrc_fsl[2]),
CS48L32_RATE_ENUM("ISRC1 FSH", cs48l32_isrc_fsh[0]),
CS48L32_RATE_ENUM("ISRC2 FSH", cs48l32_isrc_fsh[1]),
CS48L32_RATE_ENUM("ISRC3 FSH", cs48l32_isrc_fsh[2]),

SOC_ENUM("AUXPDM1 Rate", cs48l32_auxpdm1_freq),
SOC_ENUM("AUXPDM2 Rate", cs48l32_auxpdm2_freq),

SOC_ENUM_EXT("IN1L Rate", cs48l32_input_rate[0], snd_soc_get_enum_double, cs48l32_in_rate_put),
SOC_ENUM_EXT("IN1R Rate", cs48l32_input_rate[1], snd_soc_get_enum_double, cs48l32_in_rate_put),
SOC_ENUM_EXT("IN2L Rate", cs48l32_input_rate[2], snd_soc_get_enum_double, cs48l32_in_rate_put),
SOC_ENUM_EXT("IN2R Rate", cs48l32_input_rate[3], snd_soc_get_enum_double, cs48l32_in_rate_put),

CS48L32_RATE_ENUM("Noise Generator Rate", noise_gen_rate),

SOC_SINGLE_TLV("Noise Generator Volume", CS48L32_COMFORT_NOISE_GENERATOR,
	       CS48L32_NOISE_GEN_GAIN_SHIFT, 0x12, 0, cs48l32_noise_tlv),

CS48L32_MIXER_CONTROLS("ASP1TX1", CS48L32_ASP1TX1_INPUT1),
CS48L32_MIXER_CONTROLS("ASP1TX2", CS48L32_ASP1TX2_INPUT1),
CS48L32_MIXER_CONTROLS("ASP1TX3", CS48L32_ASP1TX3_INPUT1),
CS48L32_MIXER_CONTROLS("ASP1TX4", CS48L32_ASP1TX4_INPUT1),
CS48L32_MIXER_CONTROLS("ASP1TX5", CS48L32_ASP1TX5_INPUT1),
CS48L32_MIXER_CONTROLS("ASP1TX6", CS48L32_ASP1TX6_INPUT1),
CS48L32_MIXER_CONTROLS("ASP1TX7", CS48L32_ASP1TX7_INPUT1),
CS48L32_MIXER_CONTROLS("ASP1TX8", CS48L32_ASP1TX8_INPUT1),

CS48L32_MIXER_CONTROLS("ASP2TX1", CS48L32_ASP2TX1_INPUT1),
CS48L32_MIXER_CONTROLS("ASP2TX2", CS48L32_ASP2TX2_INPUT1),
CS48L32_MIXER_CONTROLS("ASP2TX3", CS48L32_ASP2TX3_INPUT1),
CS48L32_MIXER_CONTROLS("ASP2TX4", CS48L32_ASP2TX4_INPUT1),

WM_ADSP2_PRELOAD_SWITCH("DSP1", 1),

CS48L32_MIXER_CONTROLS("DSP1RX1", CS48L32_DSP1RX1_INPUT1),
CS48L32_MIXER_CONTROLS("DSP1RX2", CS48L32_DSP1RX2_INPUT1),
CS48L32_MIXER_CONTROLS("DSP1RX3", CS48L32_DSP1RX3_INPUT1),
CS48L32_MIXER_CONTROLS("DSP1RX4", CS48L32_DSP1RX4_INPUT1),
CS48L32_MIXER_CONTROLS("DSP1RX5", CS48L32_DSP1RX5_INPUT1),
CS48L32_MIXER_CONTROLS("DSP1RX6", CS48L32_DSP1RX6_INPUT1),
CS48L32_MIXER_CONTROLS("DSP1RX7", CS48L32_DSP1RX7_INPUT1),
CS48L32_MIXER_CONTROLS("DSP1RX8", CS48L32_DSP1RX8_INPUT1),

WM_ADSP_FW_CONTROL("DSP1", 0),

CS48L32_DSP_RATE_CONTROL("DSP1RX1", 0),
CS48L32_DSP_RATE_CONTROL("DSP1RX2", 1),
CS48L32_DSP_RATE_CONTROL("DSP1RX3", 2),
CS48L32_DSP_RATE_CONTROL("DSP1RX4", 3),
CS48L32_DSP_RATE_CONTROL("DSP1RX5", 4),
CS48L32_DSP_RATE_CONTROL("DSP1RX6", 5),
CS48L32_DSP_RATE_CONTROL("DSP1RX7", 6),
CS48L32_DSP_RATE_CONTROL("DSP1RX8", 7),
CS48L32_DSP_RATE_CONTROL("DSP1TX1", 8),
CS48L32_DSP_RATE_CONTROL("DSP1TX2", 9),
CS48L32_DSP_RATE_CONTROL("DSP1TX3", 10),
CS48L32_DSP_RATE_CONTROL("DSP1TX4", 11),
CS48L32_DSP_RATE_CONTROL("DSP1TX5", 12),
CS48L32_DSP_RATE_CONTROL("DSP1TX6", 13),
CS48L32_DSP_RATE_CONTROL("DSP1TX7", 14),
CS48L32_DSP_RATE_CONTROL("DSP1TX8", 15),
};

CS48L32_MIXER_ENUMS(EQ1, CS48L32_EQ1_INPUT1);
CS48L32_MIXER_ENUMS(EQ2, CS48L32_EQ2_INPUT1);
CS48L32_MIXER_ENUMS(EQ3, CS48L32_EQ3_INPUT1);
CS48L32_MIXER_ENUMS(EQ4, CS48L32_EQ4_INPUT1);

CS48L32_MIXER_ENUMS(DRC1L, CS48L32_DRC1L_INPUT1);
CS48L32_MIXER_ENUMS(DRC1R, CS48L32_DRC1R_INPUT1);
CS48L32_MIXER_ENUMS(DRC2L, CS48L32_DRC2L_INPUT1);
CS48L32_MIXER_ENUMS(DRC2R, CS48L32_DRC2R_INPUT1);

CS48L32_MIXER_ENUMS(LHPF1, CS48L32_LHPF1_INPUT1);
CS48L32_MIXER_ENUMS(LHPF2, CS48L32_LHPF2_INPUT1);
CS48L32_MIXER_ENUMS(LHPF3, CS48L32_LHPF3_INPUT1);
CS48L32_MIXER_ENUMS(LHPF4, CS48L32_LHPF4_INPUT1);

CS48L32_MIXER_ENUMS(ASP1TX1, CS48L32_ASP1TX1_INPUT1);
CS48L32_MIXER_ENUMS(ASP1TX2, CS48L32_ASP1TX2_INPUT1);
CS48L32_MIXER_ENUMS(ASP1TX3, CS48L32_ASP1TX3_INPUT1);
CS48L32_MIXER_ENUMS(ASP1TX4, CS48L32_ASP1TX4_INPUT1);
CS48L32_MIXER_ENUMS(ASP1TX5, CS48L32_ASP1TX5_INPUT1);
CS48L32_MIXER_ENUMS(ASP1TX6, CS48L32_ASP1TX6_INPUT1);
CS48L32_MIXER_ENUMS(ASP1TX7, CS48L32_ASP1TX7_INPUT1);
CS48L32_MIXER_ENUMS(ASP1TX8, CS48L32_ASP1TX8_INPUT1);

CS48L32_MIXER_ENUMS(ASP2TX1, CS48L32_ASP2TX1_INPUT1);
CS48L32_MIXER_ENUMS(ASP2TX2, CS48L32_ASP2TX2_INPUT1);
CS48L32_MIXER_ENUMS(ASP2TX3, CS48L32_ASP2TX3_INPUT1);
CS48L32_MIXER_ENUMS(ASP2TX4, CS48L32_ASP2TX4_INPUT1);

CS48L32_MUX_ENUMS(ISRC1INT1, CS48L32_ISRC1INT1_INPUT1);
CS48L32_MUX_ENUMS(ISRC1INT2, CS48L32_ISRC1INT2_INPUT1);
CS48L32_MUX_ENUMS(ISRC1INT3, CS48L32_ISRC1INT3_INPUT1);
CS48L32_MUX_ENUMS(ISRC1INT4, CS48L32_ISRC1INT4_INPUT1);

CS48L32_MUX_ENUMS(ISRC1DEC1, CS48L32_ISRC1DEC1_INPUT1);
CS48L32_MUX_ENUMS(ISRC1DEC2, CS48L32_ISRC1DEC2_INPUT1);
CS48L32_MUX_ENUMS(ISRC1DEC3, CS48L32_ISRC1DEC3_INPUT1);
CS48L32_MUX_ENUMS(ISRC1DEC4, CS48L32_ISRC1DEC4_INPUT1);

CS48L32_MUX_ENUMS(ISRC2INT1, CS48L32_ISRC2INT1_INPUT1);
CS48L32_MUX_ENUMS(ISRC2INT2, CS48L32_ISRC2INT2_INPUT1);

CS48L32_MUX_ENUMS(ISRC2DEC1, CS48L32_ISRC2DEC1_INPUT1);
CS48L32_MUX_ENUMS(ISRC2DEC2, CS48L32_ISRC2DEC2_INPUT1);

CS48L32_MUX_ENUMS(ISRC3INT1, CS48L32_ISRC3INT1_INPUT1);
CS48L32_MUX_ENUMS(ISRC3INT2, CS48L32_ISRC3INT2_INPUT1);

CS48L32_MUX_ENUMS(ISRC3DEC1, CS48L32_ISRC3DEC1_INPUT1);
CS48L32_MUX_ENUMS(ISRC3DEC2, CS48L32_ISRC3DEC2_INPUT1);

CS48L32_MIXER_ENUMS(DSP1RX1, CS48L32_DSP1RX1_INPUT1);
CS48L32_MIXER_ENUMS(DSP1RX2, CS48L32_DSP1RX2_INPUT1);
CS48L32_MIXER_ENUMS(DSP1RX3, CS48L32_DSP1RX3_INPUT1);
CS48L32_MIXER_ENUMS(DSP1RX4, CS48L32_DSP1RX4_INPUT1);
CS48L32_MIXER_ENUMS(DSP1RX5, CS48L32_DSP1RX5_INPUT1);
CS48L32_MIXER_ENUMS(DSP1RX6, CS48L32_DSP1RX6_INPUT1);
CS48L32_MIXER_ENUMS(DSP1RX7, CS48L32_DSP1RX7_INPUT1);
CS48L32_MIXER_ENUMS(DSP1RX8, CS48L32_DSP1RX8_INPUT1);

static int cs48l32_dsp_mem_ev(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		return cs48l32_dsp_memory_enable(cs48l32_codec, &cs48l32_dsp_sram_regs);
	case SND_SOC_DAPM_PRE_PMD:
		cs48l32_dsp_memory_disable(cs48l32_codec, &cs48l32_dsp_sram_regs);
		return 0;
	default:
		return 0;
	}
}

static const struct snd_soc_dapm_widget cs48l32_dapm_widgets[] = {
SND_SOC_DAPM_SUPPLY("SYSCLK", CS48L32_SYSTEM_CLOCK1, CS48L32_SYSCLK_EN_SHIFT, 0,
		    cs48l32_sysclk_ev, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

SND_SOC_DAPM_REGULATOR_SUPPLY("vdd-cp", 20, 0),

SND_SOC_DAPM_SUPPLY("VOUT_MIC", CS48L32_CHARGE_PUMP1, CS48L32_CP2_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("VOUT_MIC_REGULATED", CS48L32_CHARGE_PUMP1, CS48L32_CP2_BYPASS_SHIFT,
		    1, NULL, 0),
SND_SOC_DAPM_SUPPLY("MICBIAS1", CS48L32_MICBIAS_CTRL1, CS48L32_MICB1_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("MICBIAS1A", CS48L32_MICBIAS_CTRL5, CS48L32_MICB1A_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("MICBIAS1B", CS48L32_MICBIAS_CTRL5, CS48L32_MICB1B_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("MICBIAS1C", CS48L32_MICBIAS_CTRL5, CS48L32_MICB1C_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_SUPPLY("DSP1MEM", SND_SOC_NOPM, 0, 0, cs48l32_dsp_mem_ev,
		    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

CS48L32_DSP_FREQ_WIDGET_EV("DSP1", 0, cs48l32_dsp_freq_ev),

SND_SOC_DAPM_SIGGEN("TONE"),
SND_SOC_DAPM_SIGGEN("NOISE"),

SND_SOC_DAPM_INPUT("IN1LN_1"),
SND_SOC_DAPM_INPUT("IN1LN_2"),
SND_SOC_DAPM_INPUT("IN1LP_1"),
SND_SOC_DAPM_INPUT("IN1LP_2"),
SND_SOC_DAPM_INPUT("IN1RN_1"),
SND_SOC_DAPM_INPUT("IN1RN_2"),
SND_SOC_DAPM_INPUT("IN1RP_1"),
SND_SOC_DAPM_INPUT("IN1RP_2"),
SND_SOC_DAPM_INPUT("IN1_PDMCLK"),
SND_SOC_DAPM_INPUT("IN1_PDMDATA"),

SND_SOC_DAPM_INPUT("IN2_PDMCLK"),
SND_SOC_DAPM_INPUT("IN2_PDMDATA"),

SND_SOC_DAPM_MUX("Ultrasonic 1 Input", SND_SOC_NOPM, 0, 0, &cs48l32_us_inmux[0]),
SND_SOC_DAPM_MUX("Ultrasonic 2 Input", SND_SOC_NOPM, 0, 0, &cs48l32_us_inmux[1]),

SND_SOC_DAPM_OUTPUT("DRC1 Signal Activity"),
SND_SOC_DAPM_OUTPUT("DRC2 Signal Activity"),

SND_SOC_DAPM_OUTPUT("DSP Trigger Out"),

SND_SOC_DAPM_MUX("IN1L Mux", SND_SOC_NOPM, 0, 0, &cs48l32_inmux[0]),
SND_SOC_DAPM_MUX("IN1R Mux", SND_SOC_NOPM, 0, 0, &cs48l32_inmux[1]),

SND_SOC_DAPM_MUX("IN1L Mode", SND_SOC_NOPM, 0, 0, &cs48l32_dmode_mux[0]),
SND_SOC_DAPM_MUX("IN1R Mode", SND_SOC_NOPM, 0, 0, &cs48l32_dmode_mux[0]),

SND_SOC_DAPM_AIF_OUT("ASP1TX1", NULL, 0, CS48L32_ASP1_ENABLES1, 0, 0),
SND_SOC_DAPM_AIF_OUT("ASP1TX2", NULL, 1, CS48L32_ASP1_ENABLES1, 1, 0),
SND_SOC_DAPM_AIF_OUT("ASP1TX3", NULL, 2, CS48L32_ASP1_ENABLES1, 2, 0),
SND_SOC_DAPM_AIF_OUT("ASP1TX4", NULL, 3, CS48L32_ASP1_ENABLES1, 3, 0),
SND_SOC_DAPM_AIF_OUT("ASP1TX5", NULL, 4, CS48L32_ASP1_ENABLES1, 4, 0),
SND_SOC_DAPM_AIF_OUT("ASP1TX6", NULL, 5, CS48L32_ASP1_ENABLES1, 5, 0),
SND_SOC_DAPM_AIF_OUT("ASP1TX7", NULL, 6, CS48L32_ASP1_ENABLES1, 6, 0),
SND_SOC_DAPM_AIF_OUT("ASP1TX8", NULL, 7, CS48L32_ASP1_ENABLES1, 7, 0),

SND_SOC_DAPM_AIF_OUT("ASP2TX1", NULL, 0, CS48L32_ASP2_ENABLES1, 0, 0),
SND_SOC_DAPM_AIF_OUT("ASP2TX2", NULL, 1, CS48L32_ASP2_ENABLES1, 1, 0),
SND_SOC_DAPM_AIF_OUT("ASP2TX3", NULL, 2, CS48L32_ASP2_ENABLES1, 2, 0),
SND_SOC_DAPM_AIF_OUT("ASP2TX4", NULL, 3, CS48L32_ASP2_ENABLES1, 3, 0),

SND_SOC_DAPM_SWITCH("AUXPDM1 Output", CS48L32_AUXPDM_CONTROL1, 0, 0, &cs48l32_auxpdm_switch[0]),
SND_SOC_DAPM_SWITCH("AUXPDM2 Output", CS48L32_AUXPDM_CONTROL1, 1, 0, &cs48l32_auxpdm_switch[1]),

SND_SOC_DAPM_MUX("AUXPDM1 Input", SND_SOC_NOPM, 0, 0, &cs48l32_auxpdm_inmux[0]),
SND_SOC_DAPM_MUX("AUXPDM2 Input", SND_SOC_NOPM, 0, 0, &cs48l32_auxpdm_inmux[1]),

SND_SOC_DAPM_MUX("AUXPDM1 Analog Input", SND_SOC_NOPM, 0, 0,
		 &cs48l32_auxpdm_analog_inmux[0]),
SND_SOC_DAPM_MUX("AUXPDM2 Analog Input", SND_SOC_NOPM, 0, 0,
		 &cs48l32_auxpdm_analog_inmux[1]),

SND_SOC_DAPM_SWITCH("Ultrasonic 1 Detect", CS48L32_US_CONTROL,
		    CS48L32_US1_DET_EN_SHIFT, 0, &cs48l32_us_switch[0]),
SND_SOC_DAPM_SWITCH("Ultrasonic 2 Detect", CS48L32_US_CONTROL,
		    CS48L32_US1_DET_EN_SHIFT, 0, &cs48l32_us_switch[1]),

/*
 * mux_in widgets : arranged in the order of sources
 * specified in CS48L32_MIXER_INPUT_ROUTES
 */
SND_SOC_DAPM_PGA("Tone Generator 1", CS48L32_TONE_GENERATOR1, 0, 0, NULL, 0),
SND_SOC_DAPM_PGA("Tone Generator 2", CS48L32_TONE_GENERATOR1, 1, 0, NULL, 0),

SND_SOC_DAPM_PGA("Noise Generator", CS48L32_COMFORT_NOISE_GENERATOR,
		 CS48L32_NOISE_GEN_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA_E("IN1L PGA", CS48L32_INPUT_CONTROL, CS48L32_IN1L_EN_SHIFT,
		   0, NULL, 0, cs48l32_in_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("IN1R PGA", CS48L32_INPUT_CONTROL, CS48L32_IN1R_EN_SHIFT,
		   0, NULL, 0, cs48l32_in_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("IN2L PGA", CS48L32_INPUT_CONTROL, CS48L32_IN2L_EN_SHIFT,
		   0, NULL, 0, cs48l32_in_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("IN2R PGA", CS48L32_INPUT_CONTROL, CS48L32_IN2R_EN_SHIFT,
		   0, NULL, 0, cs48l32_in_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),

SND_SOC_DAPM_AIF_IN("ASP1RX1", NULL, 0, CS48L32_ASP1_ENABLES1, 16, 0),
SND_SOC_DAPM_AIF_IN("ASP1RX2", NULL, 1, CS48L32_ASP1_ENABLES1, 17, 0),
SND_SOC_DAPM_AIF_IN("ASP1RX3", NULL, 2, CS48L32_ASP1_ENABLES1, 18, 0),
SND_SOC_DAPM_AIF_IN("ASP1RX4", NULL, 3, CS48L32_ASP1_ENABLES1, 19, 0),
SND_SOC_DAPM_AIF_IN("ASP1RX5", NULL, 4, CS48L32_ASP1_ENABLES1, 20, 0),
SND_SOC_DAPM_AIF_IN("ASP1RX6", NULL, 5, CS48L32_ASP1_ENABLES1, 21, 0),
SND_SOC_DAPM_AIF_IN("ASP1RX7", NULL, 6, CS48L32_ASP1_ENABLES1, 22, 0),
SND_SOC_DAPM_AIF_IN("ASP1RX8", NULL, 7, CS48L32_ASP1_ENABLES1, 23, 0),

SND_SOC_DAPM_AIF_IN("ASP2RX1", NULL, 0, CS48L32_ASP2_ENABLES1, 16, 0),
SND_SOC_DAPM_AIF_IN("ASP2RX2", NULL, 1, CS48L32_ASP2_ENABLES1, 17, 0),
SND_SOC_DAPM_AIF_IN("ASP2RX3", NULL, 2, CS48L32_ASP2_ENABLES1, 18, 0),
SND_SOC_DAPM_AIF_IN("ASP2RX4", NULL, 3, CS48L32_ASP2_ENABLES1, 19, 0),

SND_SOC_DAPM_PGA("ISRC1DEC1", CS48L32_ISRC1_CONTROL2, CS48L32_ISRC1_DEC1_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1DEC2", CS48L32_ISRC1_CONTROL2, CS48L32_ISRC1_DEC2_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1DEC3", CS48L32_ISRC1_CONTROL2, CS48L32_ISRC1_DEC3_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1DEC4", CS48L32_ISRC1_CONTROL2, CS48L32_ISRC1_DEC4_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("ISRC1INT1", CS48L32_ISRC1_CONTROL2, CS48L32_ISRC1_INT1_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1INT2", CS48L32_ISRC1_CONTROL2, CS48L32_ISRC1_INT2_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1INT3", CS48L32_ISRC1_CONTROL2, CS48L32_ISRC1_INT3_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1INT4", CS48L32_ISRC1_CONTROL2, CS48L32_ISRC1_INT4_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("ISRC2DEC1", CS48L32_ISRC2_CONTROL2, CS48L32_ISRC1_DEC1_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC2DEC2", CS48L32_ISRC2_CONTROL2, CS48L32_ISRC1_DEC2_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("ISRC2INT1", CS48L32_ISRC2_CONTROL2, CS48L32_ISRC1_INT1_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC2INT2", CS48L32_ISRC2_CONTROL2, CS48L32_ISRC1_INT2_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("ISRC3DEC1", CS48L32_ISRC3_CONTROL2, CS48L32_ISRC1_DEC1_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC3DEC2", CS48L32_ISRC3_CONTROL2, CS48L32_ISRC1_DEC2_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("ISRC3INT1", CS48L32_ISRC3_CONTROL2, CS48L32_ISRC1_INT1_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC3INT2", CS48L32_ISRC3_CONTROL2, CS48L32_ISRC1_INT2_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA_E("EQ1", CS48L32_EQ_CONTROL1, 0, 0, NULL, 0, cs48l32_eq_ev, SND_SOC_DAPM_PRE_PMU),
SND_SOC_DAPM_PGA_E("EQ2", CS48L32_EQ_CONTROL1, 1, 0, NULL, 0, cs48l32_eq_ev, SND_SOC_DAPM_PRE_PMU),
SND_SOC_DAPM_PGA_E("EQ3", CS48L32_EQ_CONTROL1, 2, 0, NULL, 0, cs48l32_eq_ev, SND_SOC_DAPM_PRE_PMU),
SND_SOC_DAPM_PGA_E("EQ4", CS48L32_EQ_CONTROL1, 3, 0, NULL, 0, cs48l32_eq_ev, SND_SOC_DAPM_PRE_PMU),

SND_SOC_DAPM_PGA("DRC1L", CS48L32_DRC1_CONTROL1, CS48L32_DRC1L_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("DRC1R", CS48L32_DRC1_CONTROL1, CS48L32_DRC1R_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("DRC2L", CS48L32_DRC2_CONTROL1, CS48L32_DRC1L_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("DRC2R", CS48L32_DRC2_CONTROL1, CS48L32_DRC1R_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("LHPF1", CS48L32_LHPF_CONTROL1, 0, 0, NULL, 0),
SND_SOC_DAPM_PGA("LHPF2", CS48L32_LHPF_CONTROL1, 1, 0, NULL, 0),
SND_SOC_DAPM_PGA("LHPF3", CS48L32_LHPF_CONTROL1, 2, 0, NULL, 0),
SND_SOC_DAPM_PGA("LHPF4", CS48L32_LHPF_CONTROL1, 3, 0, NULL, 0),

SND_SOC_DAPM_PGA("Ultrasonic 1", CS48L32_US_CONTROL, 0, 0, NULL, 0),
SND_SOC_DAPM_PGA("Ultrasonic 2", CS48L32_US_CONTROL, 1, 0, NULL, 0),

WM_ADSP2("DSP1", 0, wm_adsp_early_event),

/* end of ordered widget list */

CS48L32_MIXER_WIDGETS(EQ1, "EQ1"),
CS48L32_MIXER_WIDGETS(EQ2, "EQ2"),
CS48L32_MIXER_WIDGETS(EQ3, "EQ3"),
CS48L32_MIXER_WIDGETS(EQ4, "EQ4"),

CS48L32_MIXER_WIDGETS(DRC1L, "DRC1L"),
CS48L32_MIXER_WIDGETS(DRC1R, "DRC1R"),
CS48L32_MIXER_WIDGETS(DRC2L, "DRC2L"),
CS48L32_MIXER_WIDGETS(DRC2R, "DRC2R"),

SND_SOC_DAPM_SWITCH("DRC1 Activity Output", SND_SOC_NOPM, 0, 0,
		    &cs48l32_drc_activity_output_mux[0]),
SND_SOC_DAPM_SWITCH("DRC2 Activity Output", SND_SOC_NOPM, 0, 0,
		    &cs48l32_drc_activity_output_mux[1]),

CS48L32_MIXER_WIDGETS(LHPF1, "LHPF1"),
CS48L32_MIXER_WIDGETS(LHPF2, "LHPF2"),
CS48L32_MIXER_WIDGETS(LHPF3, "LHPF3"),
CS48L32_MIXER_WIDGETS(LHPF4, "LHPF4"),

CS48L32_MIXER_WIDGETS(ASP1TX1, "ASP1TX1"),
CS48L32_MIXER_WIDGETS(ASP1TX2, "ASP1TX2"),
CS48L32_MIXER_WIDGETS(ASP1TX3, "ASP1TX3"),
CS48L32_MIXER_WIDGETS(ASP1TX4, "ASP1TX4"),
CS48L32_MIXER_WIDGETS(ASP1TX5, "ASP1TX5"),
CS48L32_MIXER_WIDGETS(ASP1TX6, "ASP1TX6"),
CS48L32_MIXER_WIDGETS(ASP1TX7, "ASP1TX7"),
CS48L32_MIXER_WIDGETS(ASP1TX8, "ASP1TX8"),

CS48L32_MIXER_WIDGETS(ASP2TX1, "ASP2TX1"),
CS48L32_MIXER_WIDGETS(ASP2TX2, "ASP2TX2"),
CS48L32_MIXER_WIDGETS(ASP2TX3, "ASP2TX3"),
CS48L32_MIXER_WIDGETS(ASP2TX4, "ASP2TX4"),

CS48L32_MUX_WIDGETS(ISRC1DEC1, "ISRC1DEC1"),
CS48L32_MUX_WIDGETS(ISRC1DEC2, "ISRC1DEC2"),
CS48L32_MUX_WIDGETS(ISRC1DEC3, "ISRC1DEC3"),
CS48L32_MUX_WIDGETS(ISRC1DEC4, "ISRC1DEC4"),

CS48L32_MUX_WIDGETS(ISRC1INT1, "ISRC1INT1"),
CS48L32_MUX_WIDGETS(ISRC1INT2, "ISRC1INT2"),
CS48L32_MUX_WIDGETS(ISRC1INT3, "ISRC1INT3"),
CS48L32_MUX_WIDGETS(ISRC1INT4, "ISRC1INT4"),

CS48L32_MUX_WIDGETS(ISRC2DEC1, "ISRC2DEC1"),
CS48L32_MUX_WIDGETS(ISRC2DEC2, "ISRC2DEC2"),

CS48L32_MUX_WIDGETS(ISRC2INT1, "ISRC2INT1"),
CS48L32_MUX_WIDGETS(ISRC2INT2, "ISRC2INT2"),

CS48L32_MUX_WIDGETS(ISRC3DEC1, "ISRC3DEC1"),
CS48L32_MUX_WIDGETS(ISRC3DEC2, "ISRC3DEC2"),

CS48L32_MUX_WIDGETS(ISRC3INT1, "ISRC3INT1"),
CS48L32_MUX_WIDGETS(ISRC3INT2, "ISRC3INT2"),

CS48L32_MIXER_WIDGETS(DSP1RX1, "DSP1RX1"),
CS48L32_MIXER_WIDGETS(DSP1RX2, "DSP1RX2"),
CS48L32_MIXER_WIDGETS(DSP1RX3, "DSP1RX3"),
CS48L32_MIXER_WIDGETS(DSP1RX4, "DSP1RX4"),
CS48L32_MIXER_WIDGETS(DSP1RX5, "DSP1RX5"),
CS48L32_MIXER_WIDGETS(DSP1RX6, "DSP1RX6"),
CS48L32_MIXER_WIDGETS(DSP1RX7, "DSP1RX7"),
CS48L32_MIXER_WIDGETS(DSP1RX8, "DSP1RX8"),

SND_SOC_DAPM_SWITCH("DSP1 Trigger Output", SND_SOC_NOPM, 0, 0,
		    &cs48l32_dsp_trigger_output_mux[0]),

SND_SOC_DAPM_OUTPUT("AUXPDM1_CLK"),
SND_SOC_DAPM_OUTPUT("AUXPDM1_DOUT"),
SND_SOC_DAPM_OUTPUT("AUXPDM2_CLK"),
SND_SOC_DAPM_OUTPUT("AUXPDM2_DOUT"),

SND_SOC_DAPM_OUTPUT("MICSUPP"),

SND_SOC_DAPM_OUTPUT("Ultrasonic Dummy Output"),
};

static const struct snd_soc_dapm_route cs48l32_dapm_routes[] = {
	{ "IN1LN_1", NULL, "SYSCLK" },
	{ "IN1LN_2", NULL, "SYSCLK" },
	{ "IN1LP_1", NULL, "SYSCLK" },
	{ "IN1LP_2", NULL, "SYSCLK" },
	{ "IN1RN_1", NULL, "SYSCLK" },
	{ "IN1RN_2", NULL, "SYSCLK" },
	{ "IN1RP_1", NULL, "SYSCLK" },
	{ "IN1RP_2", NULL, "SYSCLK" },

	{ "IN1_PDMCLK", NULL, "SYSCLK" },
	{ "IN1_PDMDATA", NULL, "SYSCLK" },
	{ "IN2_PDMCLK", NULL, "SYSCLK" },
	{ "IN2_PDMDATA", NULL, "SYSCLK" },

	{ "DSP1 Preloader", NULL, "DSP1MEM" },
	{ "DSP1", NULL, "DSP1FREQ" },

	{ "Audio Trace DSP", NULL, "DSP1" },
	{ "Voice Ctrl DSP", NULL, "DSP1" },

	{ "VOUT_MIC_REGULATED", NULL, "VOUT_MIC" },
	{ "MICBIAS1", NULL, "VOUT_MIC_REGULATED" },
	{ "MICBIAS1A", NULL, "MICBIAS1" },
	{ "MICBIAS1B", NULL, "MICBIAS1" },
	{ "MICBIAS1C", NULL, "MICBIAS1" },

	{ "Tone Generator 1", NULL, "SYSCLK" },
	{ "Tone Generator 2", NULL, "SYSCLK" },
	{ "Noise Generator", NULL, "SYSCLK" },

	{ "Tone Generator 1", NULL, "TONE" },
	{ "Tone Generator 2", NULL, "TONE" },
	{ "Noise Generator", NULL, "NOISE" },

	{ "ASP1 Capture", NULL, "ASP1TX1" },
	{ "ASP1 Capture", NULL, "ASP1TX2" },
	{ "ASP1 Capture", NULL, "ASP1TX3" },
	{ "ASP1 Capture", NULL, "ASP1TX4" },
	{ "ASP1 Capture", NULL, "ASP1TX5" },
	{ "ASP1 Capture", NULL, "ASP1TX6" },
	{ "ASP1 Capture", NULL, "ASP1TX7" },
	{ "ASP1 Capture", NULL, "ASP1TX8" },

	{ "ASP1RX1", NULL, "ASP1 Playback" },
	{ "ASP1RX2", NULL, "ASP1 Playback" },
	{ "ASP1RX3", NULL, "ASP1 Playback" },
	{ "ASP1RX4", NULL, "ASP1 Playback" },
	{ "ASP1RX5", NULL, "ASP1 Playback" },
	{ "ASP1RX6", NULL, "ASP1 Playback" },
	{ "ASP1RX7", NULL, "ASP1 Playback" },
	{ "ASP1RX8", NULL, "ASP1 Playback" },

	{ "ASP2 Capture", NULL, "ASP2TX1" },
	{ "ASP2 Capture", NULL, "ASP2TX2" },
	{ "ASP2 Capture", NULL, "ASP2TX3" },
	{ "ASP2 Capture", NULL, "ASP2TX4" },

	{ "ASP2RX1", NULL, "ASP2 Playback" },
	{ "ASP2RX2", NULL, "ASP2 Playback" },
	{ "ASP2RX3", NULL, "ASP2 Playback" },
	{ "ASP2RX4", NULL, "ASP2 Playback" },

	{ "ASP1 Playback", NULL, "SYSCLK" },
	{ "ASP2 Playback", NULL, "SYSCLK" },

	{ "ASP1 Capture", NULL, "SYSCLK" },
	{ "ASP2 Capture", NULL, "SYSCLK" },

	{ "IN1L Mux", "Analog 1", "IN1LN_1" },
	{ "IN1L Mux", "Analog 2", "IN1LN_2" },
	{ "IN1L Mux", "Analog 1", "IN1LP_1" },
	{ "IN1L Mux", "Analog 2", "IN1LP_2" },
	{ "IN1R Mux", "Analog 1", "IN1RN_1" },
	{ "IN1R Mux", "Analog 2", "IN1RN_2" },
	{ "IN1R Mux", "Analog 1", "IN1RP_1" },
	{ "IN1R Mux", "Analog 2", "IN1RP_2" },

	{ "IN1L PGA", NULL, "IN1L Mode" },
	{ "IN1R PGA", NULL, "IN1R Mode" },

	{ "IN1L Mode", "Analog", "IN1L Mux" },
	{ "IN1R Mode", "Analog", "IN1R Mux" },

	{ "IN1L Mode", "Digital", "IN1_PDMCLK" },
	{ "IN1L Mode", "Digital", "IN1_PDMDATA" },
	{ "IN1R Mode", "Digital", "IN1_PDMCLK" },
	{ "IN1R Mode", "Digital", "IN1_PDMDATA" },

	{ "IN1L PGA", NULL, "VOUT_MIC" },
	{ "IN1R PGA", NULL, "VOUT_MIC" },

	{ "IN2L PGA", NULL, "VOUT_MIC" },
	{ "IN2R PGA", NULL, "VOUT_MIC" },

	{ "IN2L PGA", NULL, "IN2_PDMCLK" },
	{ "IN2R PGA", NULL, "IN2_PDMCLK" },
	{ "IN2L PGA", NULL, "IN2_PDMDATA" },
	{ "IN2R PGA", NULL, "IN2_PDMDATA" },

	{ "Ultrasonic 1", NULL, "Ultrasonic 1 Input" },
	{ "Ultrasonic 2", NULL, "Ultrasonic 2 Input" },

	{ "Ultrasonic 1 Input", "IN1L", "IN1L PGA" },
	{ "Ultrasonic 1 Input", "IN1R", "IN1R PGA" },
	{ "Ultrasonic 1 Input", "IN2L", "IN2L PGA" },
	{ "Ultrasonic 1 Input", "IN2R", "IN2R PGA" },

	{ "Ultrasonic 2 Input", "IN1L", "IN1L PGA" },
	{ "Ultrasonic 2 Input", "IN1R", "IN1R PGA" },
	{ "Ultrasonic 2 Input", "IN2L", "IN2L PGA" },
	{ "Ultrasonic 2 Input", "IN2R", "IN2R PGA" },

	{ "Ultrasonic 1 Detect", "Switch", "Ultrasonic 1 Input" },
	{ "Ultrasonic 2 Detect", "Switch", "Ultrasonic 2 Input" },

	{ "Ultrasonic Dummy Output", NULL, "Ultrasonic 1 Detect" },
	{ "Ultrasonic Dummy Output", NULL, "Ultrasonic 2 Detect" },

	CS48L32_MIXER_ROUTES("ASP1TX1", "ASP1TX1"),
	CS48L32_MIXER_ROUTES("ASP1TX2", "ASP1TX2"),
	CS48L32_MIXER_ROUTES("ASP1TX3", "ASP1TX3"),
	CS48L32_MIXER_ROUTES("ASP1TX4", "ASP1TX4"),
	CS48L32_MIXER_ROUTES("ASP1TX5", "ASP1TX5"),
	CS48L32_MIXER_ROUTES("ASP1TX6", "ASP1TX6"),
	CS48L32_MIXER_ROUTES("ASP1TX7", "ASP1TX7"),
	CS48L32_MIXER_ROUTES("ASP1TX8", "ASP1TX8"),

	CS48L32_MIXER_ROUTES("ASP2TX1", "ASP2TX1"),
	CS48L32_MIXER_ROUTES("ASP2TX2", "ASP2TX2"),
	CS48L32_MIXER_ROUTES("ASP2TX3", "ASP2TX3"),
	CS48L32_MIXER_ROUTES("ASP2TX4", "ASP2TX4"),

	CS48L32_MIXER_ROUTES("EQ1", "EQ1"),
	CS48L32_MIXER_ROUTES("EQ2", "EQ2"),
	CS48L32_MIXER_ROUTES("EQ3", "EQ3"),
	CS48L32_MIXER_ROUTES("EQ4", "EQ4"),

	CS48L32_MIXER_ROUTES("DRC1L", "DRC1L"),
	CS48L32_MIXER_ROUTES("DRC1R", "DRC1R"),
	CS48L32_MIXER_ROUTES("DRC2L", "DRC2L"),
	CS48L32_MIXER_ROUTES("DRC2R", "DRC2R"),

	CS48L32_MIXER_ROUTES("LHPF1", "LHPF1"),
	CS48L32_MIXER_ROUTES("LHPF2", "LHPF2"),
	CS48L32_MIXER_ROUTES("LHPF3", "LHPF3"),
	CS48L32_MIXER_ROUTES("LHPF4", "LHPF4"),

	CS48L32_MUX_ROUTES("ISRC1INT1", "ISRC1INT1"),
	CS48L32_MUX_ROUTES("ISRC1INT2", "ISRC1INT2"),
	CS48L32_MUX_ROUTES("ISRC1INT3", "ISRC1INT3"),
	CS48L32_MUX_ROUTES("ISRC1INT4", "ISRC1INT4"),

	CS48L32_MUX_ROUTES("ISRC1DEC1", "ISRC1DEC1"),
	CS48L32_MUX_ROUTES("ISRC1DEC2", "ISRC1DEC2"),
	CS48L32_MUX_ROUTES("ISRC1DEC3", "ISRC1DEC3"),
	CS48L32_MUX_ROUTES("ISRC1DEC4", "ISRC1DEC4"),

	CS48L32_MUX_ROUTES("ISRC2INT1", "ISRC2INT1"),
	CS48L32_MUX_ROUTES("ISRC2INT2", "ISRC2INT2"),

	CS48L32_MUX_ROUTES("ISRC2DEC1", "ISRC2DEC1"),
	CS48L32_MUX_ROUTES("ISRC2DEC2", "ISRC2DEC2"),

	CS48L32_MUX_ROUTES("ISRC3INT1", "ISRC3INT1"),
	CS48L32_MUX_ROUTES("ISRC3INT2", "ISRC3INT2"),

	CS48L32_MUX_ROUTES("ISRC3DEC1", "ISRC3DEC1"),
	CS48L32_MUX_ROUTES("ISRC3DEC2", "ISRC3DEC2"),

	CS48L32_DSP_ROUTES_1_8_SYSCLK("DSP1"),

	{ "DSP Trigger Out", NULL, "DSP1 Trigger Output" },

	{ "DSP1 Trigger Output", "Switch", "DSP1" },

	{ "AUXPDM1 Analog Input", "IN1L", "IN1L PGA" },
	{ "AUXPDM1 Analog Input", "IN1R", "IN1R PGA" },

	{ "AUXPDM2 Analog Input", "IN1L", "IN1L PGA" },
	{ "AUXPDM2 Analog Input", "IN1R", "IN1R PGA" },

	{ "AUXPDM1 Input", "Analog", "AUXPDM1 Analog Input" },
	{ "AUXPDM1 Input", "IN1 Digital", "IN1L PGA" },
	{ "AUXPDM1 Input", "IN1 Digital", "IN1R PGA" },
	{ "AUXPDM1 Input", "IN2 Digital", "IN2L PGA" },
	{ "AUXPDM1 Input", "IN2 Digital", "IN2R PGA" },

	{ "AUXPDM2 Input", "Analog", "AUXPDM2 Analog Input" },
	{ "AUXPDM2 Input", "IN1 Digital", "IN1L PGA" },
	{ "AUXPDM2 Input", "IN1 Digital", "IN1R PGA" },
	{ "AUXPDM2 Input", "IN2 Digital", "IN2L PGA" },
	{ "AUXPDM2 Input", "IN2 Digital", "IN2R PGA" },

	{ "AUXPDM1 Output", "Switch", "AUXPDM1 Input" },
	{ "AUXPDM1_CLK", NULL, "AUXPDM1 Output" },
	{ "AUXPDM1_DOUT", NULL, "AUXPDM1 Output" },

	{ "AUXPDM2 Output", "Switch", "AUXPDM2 Input" },
	{ "AUXPDM2_CLK", NULL, "AUXPDM2 Output" },
	{ "AUXPDM2_DOUT", NULL, "AUXPDM2 Output" },

	{ "MICSUPP", NULL, "SYSCLK" },

	{ "DRC1 Signal Activity", NULL, "DRC1 Activity Output" },
	{ "DRC2 Signal Activity", NULL, "DRC2 Activity Output" },
	{ "DRC1 Activity Output", "Switch", "DRC1L" },
	{ "DRC1 Activity Output", "Switch", "DRC1R" },
	{ "DRC2 Activity Output", "Switch", "DRC2L" },
	{ "DRC2 Activity Output", "Switch", "DRC2R" },
};

static int cs48l32_compr_open(struct snd_soc_component *component,
			      struct snd_compr_stream *stream)
{
	struct snd_soc_pcm_runtime *rtd = stream->private_data;
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);

	if (strcmp(snd_soc_rtd_to_codec(rtd, 0)->name, "cs48l32-dsp-trace") &&
	    strcmp(snd_soc_rtd_to_codec(rtd, 0)->name, "cs48l32-dsp-voicectrl")) {
		dev_err(cs48l32_codec->core.dev, "No suitable compressed stream for DAI '%s'\n",
			snd_soc_rtd_to_codec(rtd, 0)->name);
		return -EINVAL;
	}

	return wm_adsp_compr_open(&cs48l32_codec->dsp, stream);
}

static const struct snd_compress_ops cs48l32_compress_ops = {
	.open = &cs48l32_compr_open,
	.free = &wm_adsp_compr_free,
	.set_params = &wm_adsp_compr_set_params,
	.get_caps = &wm_adsp_compr_get_caps,
	.trigger = &wm_adsp_compr_trigger,
	.pointer = &wm_adsp_compr_pointer,
	.copy = &wm_adsp_compr_copy,
};

static const struct snd_soc_dai_ops cs48l32_compress_dai_ops = {
	.compress_new = snd_soc_new_compress,
};

static struct snd_soc_dai_driver cs48l32_dai[] = {
	{
		.name = "cs48l32-asp1",
		.id = 1,
		.base = CS48L32_ASP1_ENABLES1,
		.playback = {
			.stream_name = "ASP1 Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = CS48L32_RATES,
			.formats = CS48L32_FORMATS,
		},
		.capture = {
			.stream_name = "ASP1 Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = CS48L32_RATES,
			.formats = CS48L32_FORMATS,
		 },
		.ops = &cs48l32_dai_ops,
		.symmetric_rate = 1,
		.symmetric_sample_bits = 1,
	},
	{
		.name = "cs48l32-asp2",
		.id = 2,
		.base = CS48L32_ASP2_ENABLES1,
		.playback = {
			.stream_name = "ASP2 Playback",
			.channels_min = 1,
			.channels_max = 4,
			.rates = CS48L32_RATES,
			.formats = CS48L32_FORMATS,
		},
		.capture = {
			.stream_name = "ASP2 Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = CS48L32_RATES,
			.formats = CS48L32_FORMATS,
		 },
		.ops = &cs48l32_dai_ops,
		.symmetric_rate = 1,
		.symmetric_sample_bits = 1,
	},
	{
		.name = "cs48l32-cpu-trace",
		.id = 3,
		.capture = {
			.stream_name = "Audio Trace CPU",
			.channels_min = 1,
			.channels_max = 8,
			.rates = CS48L32_RATES,
			.formats = CS48L32_FORMATS,
		},
		.ops = &cs48l32_compress_dai_ops,
	},
	{
		.name = "cs48l32-dsp-trace",
		.id = 4,
		.capture = {
			.stream_name = "Audio Trace DSP",
			.channels_min = 1,
			.channels_max = 8,
			.rates = CS48L32_RATES,
			.formats = CS48L32_FORMATS,
		},
	},
	{
		.name = "cs48l32-cpu-voicectrl",
		.id = 5,
		.capture = {
			.stream_name = "Voice Ctrl CPU",
			.channels_min = 1,
			.channels_max = 8,
			.rates = CS48L32_RATES,
			.formats = CS48L32_FORMATS,
		},
		.ops = &cs48l32_compress_dai_ops,
	},
	{
		.name = "cs48l32-dsp-voicectrl",
		.id = 6,
		.capture = {
			.stream_name = "Voice Ctrl DSP",
			.channels_min = 1,
			.channels_max = 8,
			.rates = CS48L32_RATES,
			.formats = CS48L32_FORMATS,
		},
	},
};

static int cs48l32_init_inputs(struct snd_soc_component *component)
{
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs48l32_codec->core.regmap;
	unsigned int ana_mode_l, ana_mode_r, dig_mode;
	int i;

	/*
	 * Initialize input modes from the A settings. For muxed inputs the
	 * B settings will be applied if the mux is changed
	 */
	switch (cs48l32_codec->in_type[0][0]) {
	default:
	case CS48L32_IN_TYPE_DIFF:
		ana_mode_l = 0;
		break;
	case CS48L32_IN_TYPE_SE:
		ana_mode_l = 1 << CS48L32_INx_SRC_SHIFT;
		break;
	}

	switch (cs48l32_codec->in_type[1][0]) {
	default:
	case CS48L32_IN_TYPE_DIFF:
		ana_mode_r = 0;
		break;
	case CS48L32_IN_TYPE_SE:
		ana_mode_r = 1 << CS48L32_INx_SRC_SHIFT;
		break;
	}

	dev_dbg(cs48l32_codec->core.dev, "IN1_1 Analogue mode=#%x,#%x\n",
		ana_mode_l, ana_mode_r);

	regmap_update_bits(regmap,
			   CS48L32_IN1L_CONTROL1,
			   CS48L32_INx_SRC_MASK,
			   ana_mode_l);

	regmap_update_bits(regmap,
			   CS48L32_IN1R_CONTROL1,
			   CS48L32_INx_SRC_MASK,
			   ana_mode_r);

	for (i = 0; i < ARRAY_SIZE(cs48l32_codec->pdm_sup); i++) {
		dig_mode = cs48l32_codec->pdm_sup[i] << CS48L32_IN1_PDM_SUP_SHIFT;

		dev_dbg(cs48l32_codec->core.dev, "IN%d PDM_SUP=#%x\n", i + 1, dig_mode);

		regmap_update_bits(regmap,
				   CS48L32_INPUT1_CONTROL1 + (i * 0x40),
				   CS48L32_IN1_PDM_SUP_MASK, dig_mode);
	}

	return 0;
}

static int cs48l32_init_dai(struct cs48l32_codec *cs48l32_codec, int id)
{
	struct cs48l32_dai_priv *dai_priv = &cs48l32_codec->dai[id];

	dai_priv->clk = CS48L32_CLK_SYSCLK_1;
	dai_priv->constraint = cs48l32_constraint;

	return 0;
}

static int cs48l32_init_eq(struct cs48l32_codec *cs48l32_codec)
{
	struct regmap *regmap = cs48l32_codec->core.regmap;
	unsigned int reg = CS48L32_EQ1_BAND1_COEFF1, mode;
	__be16 *data;
	int i, ret;

	ret = regmap_read(regmap, CS48L32_EQ_CONTROL2, &mode);
	if (ret < 0) {
		dev_err(cs48l32_codec->core.dev, "Error reading EQ mode: %d\n", ret);
		goto out;
	}

	for (i = 0; i < 4; ++i) {
		cs48l32_codec->eq_mode[i] = (mode >> i) & 0x1;

		data = &cs48l32_codec->eq_coefficients[i][0];
		ret = regmap_raw_read(regmap, reg + (i * 68), data,
				      CS48L32_EQ_BLOCK_SZ);
		if (ret < 0) {
			dev_err(cs48l32_codec->core.dev,
				"Error reading EQ coefficients: %d\n", ret);
			goto out;
		}
	}

out:
	return ret;
}

static int cs48l32_component_probe(struct snd_soc_component *component)
{
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);
	int i, ret;

	snd_soc_component_init_regmap(component, cs48l32_codec->core.regmap);

	ret = cs48l32_init_inputs(component);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(cs48l32_dai); i++)
		cs48l32_init_dai(cs48l32_codec, i);

	ret = cs48l32_init_eq(cs48l32_codec);
	if (ret)
		return ret;

	wm_adsp2_component_probe(&cs48l32_codec->dsp, component);

	/* Unmask DSP IRQs */
	regmap_clear_bits(cs48l32_codec->core.regmap, CS48L32_IRQ1_MASK_7,
			  CS48L32_DSP1_MPU_ERR_EINT1_MASK | CS48L32_DSP1_WDT_EXPIRE_EINT1_MASK);
	regmap_clear_bits(cs48l32_codec->core.regmap, CS48L32_IRQ1_MASK_9,
			  CS48L32_DSP1_IRQ0_EINT1_MASK);

	return 0;
}

static void cs48l32_component_remove(struct snd_soc_component *component)
{
	struct cs48l32_codec *cs48l32_codec = snd_soc_component_get_drvdata(component);

	/* Mask DSP IRQs */
	regmap_set_bits(cs48l32_codec->core.regmap, CS48L32_IRQ1_MASK_7,
			CS48L32_DSP1_MPU_ERR_EINT1_MASK | CS48L32_DSP1_WDT_EXPIRE_EINT1_MASK);
	regmap_set_bits(cs48l32_codec->core.regmap, CS48L32_IRQ1_MASK_9,
			CS48L32_DSP1_IRQ0_EINT1_MASK);

	wm_adsp2_component_remove(&cs48l32_codec->dsp, component);
}

static const struct snd_soc_component_driver cs48l32_soc_component_drv = {
	.probe			= &cs48l32_component_probe,
	.remove			= &cs48l32_component_remove,
	.set_sysclk		= &cs48l32_set_sysclk,
	.set_pll		= &cs48l32_set_fll,
	.name			= "cs48l32-codec",
	.compress_ops		= &cs48l32_compress_ops,
	.controls		= cs48l32_snd_controls,
	.num_controls		= ARRAY_SIZE(cs48l32_snd_controls),
	.dapm_widgets		= cs48l32_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs48l32_dapm_widgets),
	.dapm_routes		= cs48l32_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(cs48l32_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int cs48l32_prop_read_u32_array(struct cs48l32_codec *cs48l32_codec,
				       const char *propname,
				       u32 *dest,
				       int n_max)
{
	struct cs48l32 *cs48l32 = &cs48l32_codec->core;
	int ret;

	ret = device_property_read_u32_array(cs48l32->dev, propname, dest, n_max);
	if (ret == -EINVAL)
		return -ENOENT;

	if (ret < 0)
		return dev_err_probe(cs48l32->dev, ret, "%s malformed\n", propname);

	return 0;
}

static void cs48l32_prop_get_in_type(struct cs48l32_codec *cs48l32_codec)
{
	const char *propname = "cirrus,in-type";
	u32 tmp[CS48L32_MAX_ANALOG_INPUT * CS48L32_MAX_IN_MUX_WAYS];
	int i, in_idx, mux_way_idx, ret;

	static_assert(ARRAY_SIZE(tmp) ==
		      ARRAY_SIZE(cs48l32_codec->in_type) * ARRAY_SIZE(cs48l32_codec->in_type[0]));

	ret = cs48l32_prop_read_u32_array(cs48l32_codec, propname, tmp, ARRAY_SIZE(tmp));
	if (ret < 0)
		return;

	in_idx = 0;
	mux_way_idx = 0;
	for (i = 0; i < ARRAY_SIZE(tmp); ++i) {
		switch (tmp[i]) {
		case CS48L32_IN_TYPE_DIFF:
		case CS48L32_IN_TYPE_SE:
			cs48l32_codec->in_type[in_idx][mux_way_idx] = tmp[i];
			break;
		default:
			dev_warn(cs48l32_codec->core.dev, "Illegal %s value %d ignored\n",
				 propname, tmp[i]);
			break;
		}

		/*
		 * Property array is [mux_way][in_channel]. Swap to
		 * [in_channel][mux_way] for convenience.
		 */
		if (++in_idx == ARRAY_SIZE(cs48l32_codec->in_type)) {
			in_idx = 0;
			++mux_way_idx;
		}
	}
}

static void cs48l32_prop_get_pdm_sup(struct cs48l32_codec *cs48l32_codec)
{
	const char *propname = "cirrus,pdm-sup";
	u32 tmp[CS48L32_MAX_ANALOG_INPUT];
	int i;

	static_assert(ARRAY_SIZE(tmp) == ARRAY_SIZE(cs48l32_codec->pdm_sup));

	cs48l32_prop_read_u32_array(cs48l32_codec, propname, tmp, ARRAY_SIZE(tmp));

	for (i = 0; i < ARRAY_SIZE(cs48l32_codec->pdm_sup); i++) {
		switch (tmp[i]) {
		case CS48L32_PDM_SUP_VOUT_MIC:
		case CS48L32_PDM_SUP_MICBIAS1:
			cs48l32_codec->pdm_sup[i] = tmp[i];
			break;
		default:
			dev_warn(cs48l32_codec->core.dev, "Illegal %s value %d ignored\n",
				 propname, cs48l32_codec->pdm_sup[i]);
			break;
		}
	}
}

static void cs48l32_handle_properties(struct cs48l32_codec *cs48l32_codec)
{
	cs48l32_prop_get_in_type(cs48l32_codec);
	cs48l32_prop_get_pdm_sup(cs48l32_codec);
}

static int cs48l32_request_interrupt(struct cs48l32_codec *cs48l32_codec)
{
	int irq = cs48l32_codec->core.irq;
	int ret;

	if (irq < 1)
		return 0;

	/*
	 * Don't use devm because this must be freed before destroying the
	 * rest of the driver
	 */
	ret = request_threaded_irq(irq, NULL, cs48l32_irq,
				   IRQF_ONESHOT | IRQF_SHARED | IRQF_TRIGGER_LOW,
				   "cs48l32", cs48l32_codec);
	if (ret)
		return dev_err_probe(cs48l32_codec->core.dev, ret, "Failed to get IRQ\n");

	return 0;
}

static int cs48l32_create_codec_component(struct cs48l32_codec *cs48l32_codec)
{
	struct wm_adsp *dsp;
	int ret;

	ASSERT_STRUCT_OFFSET(struct cs48l32_codec, dsp, 0);
	static_assert(ARRAY_SIZE(cs48l32_dai) == ARRAY_SIZE(cs48l32_codec->dai));

	cs48l32_handle_properties(cs48l32_codec);

	dsp = &cs48l32_codec->dsp;
	dsp->part = "cs48l32";
	dsp->cs_dsp.num = 1;
	dsp->cs_dsp.type = WMFW_HALO;
	dsp->cs_dsp.rev = 0;
	dsp->cs_dsp.dev = cs48l32_codec->core.dev;
	dsp->cs_dsp.regmap = cs48l32_codec->core.regmap;
	dsp->cs_dsp.base = CS48L32_DSP1_CLOCK_FREQ;
	dsp->cs_dsp.base_sysinfo = CS48L32_DSP1_SYS_INFO_ID;
	dsp->cs_dsp.mem = cs48l32_dsp1_regions;
	dsp->cs_dsp.num_mems = ARRAY_SIZE(cs48l32_dsp1_regions);
	dsp->pre_run = cs48l32_dsp_pre_run;

	ret = wm_halo_init(dsp);
	if (ret != 0)
		return ret;

	cs48l32_codec->fll.codec = cs48l32_codec;
	cs48l32_codec->fll.id = 1;
	cs48l32_codec->fll.base = CS48L32_FLL1_CONTROL1;
	cs48l32_codec->fll.sts_addr = CS48L32_IRQ1_STS_6;
	cs48l32_codec->fll.sts_mask = CS48L32_FLL1_LOCK_STS1_MASK;
	cs48l32_init_fll(&cs48l32_codec->fll);

	ret = cs48l32_request_interrupt(cs48l32_codec);
	if (ret)
		goto err_dsp;

	ret = devm_snd_soc_register_component(cs48l32_codec->core.dev,
					      &cs48l32_soc_component_drv,
					      cs48l32_dai,
					      ARRAY_SIZE(cs48l32_dai));
	if (ret < 0) {
		dev_err_probe(cs48l32_codec->core.dev, ret, "Failed to register component\n");
		goto err_dsp;
	}

	return 0;

err_dsp:
	wm_adsp2_remove(&cs48l32_codec->dsp);

	return ret;
}

static int cs48l32_wait_for_boot(struct cs48l32 *cs48l32)
{
	unsigned int val;
	int ret;

	ret = regmap_read_poll_timeout(cs48l32->regmap, CS48L32_IRQ1_EINT_2, val,
				       ((val < 0xffffffff) && (val & CS48L32_BOOT_DONE_EINT1_MASK)),
				       1000, CS48L32_BOOT_TIMEOUT_US);
	if (ret) {
		dev_err(cs48l32->dev, "BOOT_DONE timed out\n");
		return -ETIMEDOUT;
	}

	ret = regmap_read(cs48l32->regmap, CS48L32_MCU_CTRL1, &val);
	if (ret) {
		dev_err(cs48l32->dev, "Failed to read MCU_CTRL1: %d\n", ret);
		return ret;
	}

	if (val & BIT(CS48L32_MCU_STS_SHIFT)) {
		dev_err(cs48l32->dev, "MCU boot failed\n");
		return -EIO;
	}

	pm_runtime_mark_last_busy(cs48l32->dev);

	return 0;
}

static int cs48l32_soft_reset(struct cs48l32 *cs48l32)
{
	int ret;

	ret = regmap_write(cs48l32->regmap, CS48L32_SFT_RESET, CS48L32_SFT_RESET_MAGIC);
	if (ret != 0) {
		dev_err(cs48l32->dev, "Failed to write soft reset: %d\n", ret);
		return ret;
	}

	usleep_range(CS48L32_SOFT_RESET_US, CS48L32_SOFT_RESET_US + 1000);

	return 0;
}

static void cs48l32_enable_hard_reset(struct cs48l32 *cs48l32)
{
	if (cs48l32->reset_gpio)
		gpiod_set_raw_value_cansleep(cs48l32->reset_gpio, 0);
}

static void cs48l32_disable_hard_reset(struct cs48l32 *cs48l32)
{
	if (cs48l32->reset_gpio) {
		gpiod_set_raw_value_cansleep(cs48l32->reset_gpio, 1);
		usleep_range(CS48L32_HARD_RESET_MIN_US, CS48L32_HARD_RESET_MIN_US + 1000);
	}
}

static int cs48l32_runtime_resume(struct device *dev)
{
	struct cs48l32_codec *cs48l32_codec = dev_get_drvdata(dev);
	struct cs48l32 *cs48l32 = &cs48l32_codec->core;
	unsigned int val;
	int ret;

	ret = regulator_enable(cs48l32->vdd_d);
	if (ret) {
		dev_err(cs48l32->dev, "Failed to enable VDD_D: %d\n", ret);
		return ret;
	}

	usleep_range(CS48L32_SOFT_RESET_US, CS48L32_SOFT_RESET_US + 1000);

	regcache_cache_only(cs48l32->regmap, false);

	ret = cs48l32_wait_for_boot(cs48l32);
	if (ret)
		goto err;

	/* Check whether registers reset during suspend */
	regmap_read(cs48l32->regmap, CS48L32_CTRL_IF_DEBUG3, &val);
	if (!val)
		regcache_mark_dirty(cs48l32->regmap);
	else
		dev_dbg(cs48l32->dev, "Did not reset during suspend\n");

	ret = regcache_sync(cs48l32->regmap);
	if (ret) {
		dev_err(cs48l32->dev, "Failed to restore register cache\n");
		goto err;
	}

	return 0;

err:
	regcache_cache_only(cs48l32->regmap, true);
	regulator_disable(cs48l32->vdd_d);

	return ret;
}

static int cs48l32_runtime_suspend(struct device *dev)
{
	struct cs48l32_codec *cs48l32_codec = dev_get_drvdata(dev);
	struct cs48l32 *cs48l32 = &cs48l32_codec->core;

	/* Flag to detect if the registers reset during suspend */
	regmap_write(cs48l32->regmap, CS48L32_CTRL_IF_DEBUG3, 1);

	regcache_cache_only(cs48l32->regmap, true);
	regulator_disable(cs48l32->vdd_d);

	return 0;
}

static const struct dev_pm_ops cs48l32_pm_ops = {
	RUNTIME_PM_OPS(cs48l32_runtime_suspend, cs48l32_runtime_resume, NULL)
};

static int cs48l32_configure_clk32k(struct cs48l32 *cs48l32)
{
	int ret = 0;

	ret = clk_prepare_enable(cs48l32->mclk1);
	if (ret)
		return dev_err_probe(cs48l32->dev, ret, "Failed to enable 32k clock\n");

	ret = regmap_update_bits(cs48l32->regmap, CS48L32_CLOCK32K,
				 CS48L32_CLK_32K_EN_MASK | CS48L32_CLK_32K_SRC_MASK,
				 CS48L32_CLK_32K_EN_MASK | CS48L32_32K_MCLK1);
	if (ret) {
		clk_disable_unprepare(cs48l32->mclk1);
		return dev_err_probe(cs48l32->dev, ret, "Failed to init 32k clock\n");
	}

	return 0;
}

static int cs48l32_get_clocks(struct cs48l32 *cs48l32)
{
	cs48l32->mclk1 = devm_clk_get_optional(cs48l32->dev, "mclk1");
	if (IS_ERR(cs48l32->mclk1))
		return dev_err_probe(cs48l32->dev, PTR_ERR(cs48l32->mclk1),
				     "Failed to get mclk1\n");

	return 0;
}

static int cs48l32_get_reset_gpio(struct cs48l32 *cs48l32)
{
	struct gpio_desc *reset;

	reset = devm_gpiod_get_optional(cs48l32->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(reset))
		return dev_err_probe(cs48l32->dev, PTR_ERR(reset), "Failed to request /RESET\n");

	/* ACPI can override the GPIOD_OUT_LOW so ensure it starts low */
	gpiod_set_raw_value_cansleep(reset, 0);

	cs48l32->reset_gpio = reset;

	return 0;
}

static int cs48l32_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct cs48l32_codec *cs48l32_codec;
	struct cs48l32 *cs48l32;
	unsigned int hwid, rev, otp_rev;
	int i, ret;

	cs48l32_codec = devm_kzalloc(&spi->dev, sizeof(*cs48l32_codec), GFP_KERNEL);
	if (!cs48l32_codec)
		return -ENOMEM;

	cs48l32 = &cs48l32_codec->core;
	cs48l32->dev = dev;
	cs48l32->irq = spi->irq;
	mutex_init(&cs48l32_codec->rate_lock);
	cs48l32_codec->in_vu_reg = CS48L32_INPUT_CONTROL3;

	dev_set_drvdata(cs48l32->dev, cs48l32_codec);

	ret = cs48l32_create_regmap(spi, cs48l32);
	if (ret)
		return dev_err_probe(&spi->dev, ret, "Failed to allocate regmap\n");

	regcache_cache_only(cs48l32->regmap, true);

	ret = cs48l32_get_reset_gpio(cs48l32);
	if (ret)
		return ret;

	ret = cs48l32_get_clocks(cs48l32);
	if (ret)
		return ret;

	static_assert(ARRAY_SIZE(cs48l32_core_supplies) == ARRAY_SIZE(cs48l32->core_supplies));
	for (i = 0; i < ARRAY_SIZE(cs48l32->core_supplies); i++)
		cs48l32->core_supplies[i].supply = cs48l32_core_supplies[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(cs48l32->core_supplies),
				      cs48l32->core_supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request core supplies\n");

	cs48l32->vdd_d = devm_regulator_get(cs48l32->dev, "vdd-d");
	if (IS_ERR(cs48l32->vdd_d))
		return dev_err_probe(dev, PTR_ERR(cs48l32->vdd_d), "Failed to request vdd-d\n");

	ret = regulator_bulk_enable(ARRAY_SIZE(cs48l32->core_supplies), cs48l32->core_supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable core supplies\n");

	ret = regulator_enable(cs48l32->vdd_d);
	if (ret) {
		dev_err(dev, "Failed to enable vdd-d: %d\n", ret);
		goto err_enable;
	}

	cs48l32_disable_hard_reset(cs48l32);

	regcache_cache_only(cs48l32->regmap, false);

	/* If we don't have a reset GPIO use a soft reset */
	if (!cs48l32->reset_gpio) {
		ret = cs48l32_soft_reset(cs48l32);
		if (ret)
			goto err_reset;
	}

	ret = cs48l32_wait_for_boot(cs48l32);
	if (ret) {
		dev_err(cs48l32->dev, "Device failed initial boot: %d\n", ret);
		goto err_reset;
	}

	ret = regmap_read(cs48l32->regmap, CS48L32_DEVID, &hwid);
	if (ret) {
		dev_err(dev, "Failed to read ID register: %d\n", ret);
		goto err_reset;
	}
	hwid &= CS48L32_DEVID_MASK;

	switch (hwid) {
	case CS48L32_SILICON_ID:
		break;
	default:
		ret = -ENODEV;
		dev_err_probe(cs48l32->dev, ret, "Unknown device ID: %#x\n", hwid);
		goto err_reset;
	}

	ret = regmap_read(cs48l32->regmap, CS48L32_REVID, &rev);
	if (ret) {
		dev_err(dev, "Failed to read revision register: %d\n", ret);
		goto err_reset;
	}
	rev &= CS48L32_AREVID_MASK | CS48L32_MTLREVID_MASK;

	ret = regmap_read(cs48l32->regmap, CS48L32_OTPID, &otp_rev);
	if (ret) {
		dev_err(dev, "Failed to read OTP revision register: %d\n", ret);
		goto err_reset;
	}
	otp_rev &= CS48L32_OTPID_MASK;

	dev_info(dev, "CS48L%x revision %X%u OTP%u\n", hwid & 0xff,
		 rev >> CS48L32_AREVID_SHIFT, rev & CS48L32_MTLREVID_MASK, otp_rev);

	/* Apply hardware patch */
	ret = cs48l32_apply_patch(cs48l32);
	if (ret) {
		dev_err(cs48l32->dev, "Failed to apply patch %d\n", ret);
		goto err_reset;
	}

	/* BOOT_DONE interrupt is unmasked by default, so mask it */
	ret = regmap_set_bits(cs48l32->regmap, CS48L32_IRQ1_MASK_2, CS48L32_BOOT_DONE_EINT1_MASK);

	ret = cs48l32_configure_clk32k(cs48l32);
	if (ret)
		goto err_reset;

	pm_runtime_set_active(cs48l32->dev);
	pm_runtime_set_autosuspend_delay(cs48l32->dev, 100);
	pm_runtime_use_autosuspend(cs48l32->dev);
	pm_runtime_enable(cs48l32->dev);

	ret = cs48l32_create_codec_component(cs48l32_codec);
	if (ret)
		goto err_clk32k;

	return 0;

err_clk32k:
	clk_disable_unprepare(cs48l32->mclk1);
err_reset:
	cs48l32_enable_hard_reset(cs48l32);
	regulator_disable(cs48l32->vdd_d);
err_enable:
	regulator_bulk_disable(ARRAY_SIZE(cs48l32->core_supplies), cs48l32->core_supplies);

	return ret;
}

static void cs48l32_spi_remove(struct spi_device *spi)
{
	struct cs48l32_codec *cs48l32_codec = spi_get_drvdata(spi);
	struct cs48l32 *cs48l32 = &cs48l32_codec->core;

	/* Remove IRQ handler before destroying anything else */
	if (cs48l32->irq >= 1)
		free_irq(cs48l32->irq, cs48l32_codec);

	pm_runtime_disable(cs48l32->dev);
	regulator_disable(cs48l32->vdd_d);
	clk_disable_unprepare(cs48l32->mclk1);
	cs48l32_enable_hard_reset(cs48l32);
	regulator_bulk_disable(ARRAY_SIZE(cs48l32->core_supplies), cs48l32->core_supplies);

	mutex_destroy(&cs48l32_codec->rate_lock);
}

static const struct of_device_id cs48l32_of_match[] = {
	{ .compatible = "cirrus,cs48l32", },
	{},
};

static const struct spi_device_id cs48l32_spi_ids[] = {
	{ "cs48l32", },
	{ },
};
MODULE_DEVICE_TABLE(spi, cs48l32_spi_ids);

static struct spi_driver cs48l32_spi_driver = {
	.driver = {
		.name		= "cs48l32",
		.pm		= pm_ptr(&cs48l32_pm_ops),
		.of_match_table	= cs48l32_of_match,
	},
	.probe		= &cs48l32_spi_probe,
	.remove		= &cs48l32_spi_remove,
	.id_table	= cs48l32_spi_ids,
};
module_spi_driver(cs48l32_spi_driver);

MODULE_DESCRIPTION("CS48L32 ASoC codec driver");
MODULE_AUTHOR("Stuart Henderson <stuarth@opensource.cirrus.com>");
MODULE_AUTHOR("Piotr Stankiewicz <piotrs@opensource.cirrus.com>");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
