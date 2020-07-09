// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Common code for ADAU1X61 and ADAU1X81 codecs
 *
 * Copyright 2011-2014 Analog Devices Inc.
 * Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/gcd.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <asm/unaligned.h>

#include "sigmadsp.h"
#include "adau17x1.h"
#include "adau-utils.h"

#define ADAU17X1_SAFELOAD_TARGET_ADDRESS 0x0006
#define ADAU17X1_SAFELOAD_TRIGGER 0x0007
#define ADAU17X1_SAFELOAD_DATA 0x0001
#define ADAU17X1_SAFELOAD_DATA_SIZE 20
#define ADAU17X1_WORD_SIZE 4

static const char * const adau17x1_capture_mixer_boost_text[] = {
	"Normal operation", "Boost Level 1", "Boost Level 2", "Boost Level 3",
};

static SOC_ENUM_SINGLE_DECL(adau17x1_capture_boost_enum,
	ADAU17X1_REC_POWER_MGMT, 5, adau17x1_capture_mixer_boost_text);

static const char * const adau17x1_mic_bias_mode_text[] = {
	"Normal operation", "High performance",
};

static SOC_ENUM_SINGLE_DECL(adau17x1_mic_bias_mode_enum,
	ADAU17X1_MICBIAS, 3, adau17x1_mic_bias_mode_text);

static const DECLARE_TLV_DB_MINMAX(adau17x1_digital_tlv, -9563, 0);

static const struct snd_kcontrol_new adau17x1_controls[] = {
	SOC_DOUBLE_R_TLV("Digital Capture Volume",
		ADAU17X1_LEFT_INPUT_DIGITAL_VOL,
		ADAU17X1_RIGHT_INPUT_DIGITAL_VOL,
		0, 0xff, 1, adau17x1_digital_tlv),
	SOC_DOUBLE_R_TLV("Digital Playback Volume", ADAU17X1_DAC_CONTROL1,
		ADAU17X1_DAC_CONTROL2, 0, 0xff, 1, adau17x1_digital_tlv),

	SOC_SINGLE("ADC High Pass Filter Switch", ADAU17X1_ADC_CONTROL,
		5, 1, 0),
	SOC_SINGLE("Playback De-emphasis Switch", ADAU17X1_DAC_CONTROL0,
		2, 1, 0),

	SOC_ENUM("Capture Boost", adau17x1_capture_boost_enum),

	SOC_ENUM("Mic Bias Mode", adau17x1_mic_bias_mode_enum),
};

static int adau17x1_setup_firmware(struct snd_soc_component *component,
	unsigned int rate);

static int adau17x1_pll_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct adau *adau = snd_soc_component_get_drvdata(component);

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		adau->pll_regs[5] = 1;
	} else {
		adau->pll_regs[5] = 0;
		/* Bypass the PLL when disabled, otherwise registers will become
		 * inaccessible. */
		regmap_update_bits(adau->regmap, ADAU17X1_CLOCK_CONTROL,
			ADAU17X1_CLOCK_CONTROL_CORECLK_SRC_PLL, 0);
	}

	/* The PLL register is 6 bytes long and can only be written at once. */
	regmap_raw_write(adau->regmap, ADAU17X1_PLL_CONTROL,
			adau->pll_regs, ARRAY_SIZE(adau->pll_regs));

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		mdelay(5);
		regmap_update_bits(adau->regmap, ADAU17X1_CLOCK_CONTROL,
			ADAU17X1_CLOCK_CONTROL_CORECLK_SRC_PLL,
			ADAU17X1_CLOCK_CONTROL_CORECLK_SRC_PLL);
	}

	return 0;
}

static int adau17x1_adc_fixup(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct adau *adau = snd_soc_component_get_drvdata(component);

	/*
	 * If we are capturing, toggle the ADOSR bit in Converter Control 0 to
	 * avoid losing SNR (workaround from ADI). This must be done after
	 * the ADC(s) have been enabled. According to the data sheet, it is
	 * normally illegal to set this bit when the sampling rate is 96 kHz,
	 * but according to ADI it is acceptable for this workaround.
	 */
	regmap_update_bits(adau->regmap, ADAU17X1_CONVERTER0,
		ADAU17X1_CONVERTER0_ADOSR, ADAU17X1_CONVERTER0_ADOSR);
	regmap_update_bits(adau->regmap, ADAU17X1_CONVERTER0,
		ADAU17X1_CONVERTER0_ADOSR, 0);

	return 0;
}

static const char * const adau17x1_mono_stereo_text[] = {
	"Stereo",
	"Mono Left Channel (L+R)",
	"Mono Right Channel (L+R)",
	"Mono (L+R)",
};

static SOC_ENUM_SINGLE_DECL(adau17x1_dac_mode_enum,
	ADAU17X1_DAC_CONTROL0, 6, adau17x1_mono_stereo_text);

static const struct snd_kcontrol_new adau17x1_dac_mode_mux =
	SOC_DAPM_ENUM("DAC Mono-Stereo-Mode", adau17x1_dac_mode_enum);

static const struct snd_soc_dapm_widget adau17x1_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY_S("PLL", 3, SND_SOC_NOPM, 0, 0, adau17x1_pll_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY("AIFCLK", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("MICBIAS", ADAU17X1_MICBIAS, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("Left Playback Enable", ADAU17X1_PLAY_POWER_MGMT,
		0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Right Playback Enable", ADAU17X1_PLAY_POWER_MGMT,
		1, 0, NULL, 0),

	SND_SOC_DAPM_MUX("Left DAC Mode Mux", SND_SOC_NOPM, 0, 0,
		&adau17x1_dac_mode_mux),
	SND_SOC_DAPM_MUX("Right DAC Mode Mux", SND_SOC_NOPM, 0, 0,
		&adau17x1_dac_mode_mux),

	SND_SOC_DAPM_ADC_E("Left Decimator", NULL, ADAU17X1_ADC_CONTROL, 0, 0,
			   adau17x1_adc_fixup, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_ADC("Right Decimator", NULL, ADAU17X1_ADC_CONTROL, 1, 0),
	SND_SOC_DAPM_DAC("Left DAC", NULL, ADAU17X1_DAC_CONTROL0, 0, 0),
	SND_SOC_DAPM_DAC("Right DAC", NULL, ADAU17X1_DAC_CONTROL0, 1, 0),
};

static const struct snd_soc_dapm_route adau17x1_dapm_routes[] = {
	{ "Left Decimator", NULL, "SYSCLK" },
	{ "Right Decimator", NULL, "SYSCLK" },
	{ "Left DAC", NULL, "SYSCLK" },
	{ "Right DAC", NULL, "SYSCLK" },
	{ "Capture", NULL, "SYSCLK" },
	{ "Playback", NULL, "SYSCLK" },

	{ "Left DAC", NULL, "Left DAC Mode Mux" },
	{ "Right DAC", NULL, "Right DAC Mode Mux" },

	{ "Capture", NULL, "AIFCLK" },
	{ "Playback", NULL, "AIFCLK" },
};

static const struct snd_soc_dapm_route adau17x1_dapm_pll_route = {
	"SYSCLK", NULL, "PLL",
};

/*
 * The MUX register for the Capture and Playback MUXs selects either DSP as
 * source/destination or one of the TDM slots. The TDM slot is selected via
 * snd_soc_dai_set_tdm_slot(), so we only expose whether to go to the DSP or
 * directly to the DAI interface with this control.
 */
static int adau17x1_dsp_mux_enum_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct adau *adau = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_dapm_update update = {};
	unsigned int stream = e->shift_l;
	unsigned int val, change;
	int reg;

	if (ucontrol->value.enumerated.item[0] >= e->items)
		return -EINVAL;

	switch (ucontrol->value.enumerated.item[0]) {
	case 0:
		val = 0;
		adau->dsp_bypass[stream] = false;
		break;
	default:
		val = (adau->tdm_slot[stream] * 2) + 1;
		adau->dsp_bypass[stream] = true;
		break;
	}

	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		reg = ADAU17X1_SERIAL_INPUT_ROUTE;
	else
		reg = ADAU17X1_SERIAL_OUTPUT_ROUTE;

	change = snd_soc_component_test_bits(component, reg, 0xff, val);
	if (change) {
		update.kcontrol = kcontrol;
		update.mask = 0xff;
		update.reg = reg;
		update.val = val;

		snd_soc_dapm_mux_update_power(dapm, kcontrol,
				ucontrol->value.enumerated.item[0], e, &update);
	}

	return change;
}

static int adau17x1_dsp_mux_enum_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_dapm_kcontrol_component(kcontrol);
	struct adau *adau = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int stream = e->shift_l;
	unsigned int reg, val;
	int ret;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		reg = ADAU17X1_SERIAL_INPUT_ROUTE;
	else
		reg = ADAU17X1_SERIAL_OUTPUT_ROUTE;

	ret = regmap_read(adau->regmap, reg, &val);
	if (ret)
		return ret;

	if (val != 0)
		val = 1;
	ucontrol->value.enumerated.item[0] = val;

	return 0;
}

#define DECLARE_ADAU17X1_DSP_MUX_CTRL(_name, _label, _stream, _text) \
	const struct snd_kcontrol_new _name = \
		SOC_DAPM_ENUM_EXT(_label, (const struct soc_enum)\
			SOC_ENUM_SINGLE(SND_SOC_NOPM, _stream, \
				ARRAY_SIZE(_text), _text), \
			adau17x1_dsp_mux_enum_get, adau17x1_dsp_mux_enum_put)

static const char * const adau17x1_dac_mux_text[] = {
	"DSP",
	"AIFIN",
};

static const char * const adau17x1_capture_mux_text[] = {
	"DSP",
	"Decimator",
};

static DECLARE_ADAU17X1_DSP_MUX_CTRL(adau17x1_dac_mux, "DAC Playback Mux",
	SNDRV_PCM_STREAM_PLAYBACK, adau17x1_dac_mux_text);

static DECLARE_ADAU17X1_DSP_MUX_CTRL(adau17x1_capture_mux, "Capture Mux",
	SNDRV_PCM_STREAM_CAPTURE, adau17x1_capture_mux_text);

static const struct snd_soc_dapm_widget adau17x1_dsp_dapm_widgets[] = {
	SND_SOC_DAPM_PGA("DSP", ADAU17X1_DSP_RUN, 0, 0, NULL, 0),
	SND_SOC_DAPM_SIGGEN("DSP Siggen"),

	SND_SOC_DAPM_MUX("DAC Playback Mux", SND_SOC_NOPM, 0, 0,
		&adau17x1_dac_mux),
	SND_SOC_DAPM_MUX("Capture Mux", SND_SOC_NOPM, 0, 0,
		&adau17x1_capture_mux),
};

static const struct snd_soc_dapm_route adau17x1_dsp_dapm_routes[] = {
	{ "DAC Playback Mux", "DSP", "DSP" },
	{ "DAC Playback Mux", "AIFIN", "Playback" },

	{ "Left DAC Mode Mux", "Stereo", "DAC Playback Mux" },
	{ "Left DAC Mode Mux", "Mono (L+R)", "DAC Playback Mux" },
	{ "Left DAC Mode Mux", "Mono Left Channel (L+R)", "DAC Playback Mux" },
	{ "Right DAC Mode Mux", "Stereo", "DAC Playback Mux" },
	{ "Right DAC Mode Mux", "Mono (L+R)", "DAC Playback Mux" },
	{ "Right DAC Mode Mux", "Mono Right Channel (L+R)", "DAC Playback Mux" },

	{ "Capture Mux", "DSP", "DSP" },
	{ "Capture Mux", "Decimator", "Left Decimator" },
	{ "Capture Mux", "Decimator", "Right Decimator" },

	{ "Capture", NULL, "Capture Mux" },

	{ "DSP", NULL, "DSP Siggen" },

	{ "DSP", NULL, "Left Decimator" },
	{ "DSP", NULL, "Right Decimator" },
	{ "DSP", NULL, "Playback" },
};

static const struct snd_soc_dapm_route adau17x1_no_dsp_dapm_routes[] = {
	{ "Left DAC Mode Mux", "Stereo", "Playback" },
	{ "Left DAC Mode Mux", "Mono (L+R)", "Playback" },
	{ "Left DAC Mode Mux", "Mono Left Channel (L+R)", "Playback" },
	{ "Right DAC Mode Mux", "Stereo", "Playback" },
	{ "Right DAC Mode Mux", "Mono (L+R)", "Playback" },
	{ "Right DAC Mode Mux", "Mono Right Channel (L+R)", "Playback" },
	{ "Capture", NULL, "Left Decimator" },
	{ "Capture", NULL, "Right Decimator" },
};

static bool adau17x1_has_dsp(struct adau *adau)
{
	switch (adau->type) {
	case ADAU1761:
	case ADAU1381:
	case ADAU1781:
		return true;
	default:
		return false;
	}
}

static bool adau17x1_has_safeload(struct adau *adau)
{
	switch (adau->type) {
	case ADAU1761:
	case ADAU1781:
		return true;
	default:
		return false;
	}
}

static int adau17x1_set_dai_pll(struct snd_soc_dai *dai, int pll_id,
	int source, unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_component *component = dai->component;
	struct adau *adau = snd_soc_component_get_drvdata(component);
	int ret;

	if (freq_in < 8000000 || freq_in > 27000000)
		return -EINVAL;

	ret = adau_calc_pll_cfg(freq_in, freq_out, adau->pll_regs);
	if (ret < 0)
		return ret;

	/* The PLL register is 6 bytes long and can only be written at once. */
	ret = regmap_raw_write(adau->regmap, ADAU17X1_PLL_CONTROL,
			adau->pll_regs, ARRAY_SIZE(adau->pll_regs));
	if (ret)
		return ret;

	adau->pll_freq = freq_out;

	return 0;
}

static int adau17x1_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(dai->component);
	struct adau *adau = snd_soc_component_get_drvdata(dai->component);
	bool is_pll;
	bool was_pll;

	switch (clk_id) {
	case ADAU17X1_CLK_SRC_MCLK:
		is_pll = false;
		break;
	case ADAU17X1_CLK_SRC_PLL_AUTO:
		if (!adau->mclk)
			return -EINVAL;
		fallthrough;
	case ADAU17X1_CLK_SRC_PLL:
		is_pll = true;
		break;
	default:
		return -EINVAL;
	}

	switch (adau->clk_src) {
	case ADAU17X1_CLK_SRC_MCLK:
		was_pll = false;
		break;
	case ADAU17X1_CLK_SRC_PLL:
	case ADAU17X1_CLK_SRC_PLL_AUTO:
		was_pll = true;
		break;
	default:
		return -EINVAL;
	}

	adau->sysclk = freq;

	if (is_pll != was_pll) {
		if (is_pll) {
			snd_soc_dapm_add_routes(dapm,
				&adau17x1_dapm_pll_route, 1);
		} else {
			snd_soc_dapm_del_routes(dapm,
				&adau17x1_dapm_pll_route, 1);
		}
	}

	adau->clk_src = clk_id;

	return 0;
}

static int adau17x1_auto_pll(struct snd_soc_dai *dai,
	struct snd_pcm_hw_params *params)
{
	struct adau *adau = snd_soc_dai_get_drvdata(dai);
	unsigned int pll_rate;

	switch (params_rate(params)) {
	case 48000:
	case 8000:
	case 12000:
	case 16000:
	case 24000:
	case 32000:
	case 96000:
		pll_rate = 48000 * 1024;
		break;
	case 44100:
	case 7350:
	case 11025:
	case 14700:
	case 22050:
	case 29400:
	case 88200:
		pll_rate = 44100 * 1024;
		break;
	default:
		return -EINVAL;
	}

	return adau17x1_set_dai_pll(dai, ADAU17X1_PLL, ADAU17X1_PLL_SRC_MCLK,
		clk_get_rate(adau->mclk), pll_rate);
}

static int adau17x1_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct adau *adau = snd_soc_component_get_drvdata(component);
	unsigned int val, div, dsp_div;
	unsigned int freq;
	int ret;

	switch (adau->clk_src) {
	case ADAU17X1_CLK_SRC_PLL_AUTO:
		ret = adau17x1_auto_pll(dai, params);
		if (ret)
			return ret;
		fallthrough;
	case ADAU17X1_CLK_SRC_PLL:
		freq = adau->pll_freq;
		break;
	default:
		freq = adau->sysclk;
		break;
	}

	if (freq % params_rate(params) != 0)
		return -EINVAL;

	switch (freq / params_rate(params)) {
	case 1024: /* fs */
		div = 0;
		dsp_div = 1;
		break;
	case 6144: /* fs / 6 */
		div = 1;
		dsp_div = 6;
		break;
	case 4096: /* fs / 4 */
		div = 2;
		dsp_div = 5;
		break;
	case 3072: /* fs / 3 */
		div = 3;
		dsp_div = 4;
		break;
	case 2048: /* fs / 2 */
		div = 4;
		dsp_div = 3;
		break;
	case 1536: /* fs / 1.5 */
		div = 5;
		dsp_div = 2;
		break;
	case 512: /* fs / 0.5 */
		div = 6;
		dsp_div = 0;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(adau->regmap, ADAU17X1_CONVERTER0,
		ADAU17X1_CONVERTER0_CONVSR_MASK, div);
	if (adau17x1_has_dsp(adau)) {
		regmap_write(adau->regmap, ADAU17X1_SERIAL_SAMPLING_RATE, div);
		regmap_write(adau->regmap, ADAU17X1_DSP_SAMPLING_RATE, dsp_div);
	}

	if (adau->sigmadsp) {
		ret = adau17x1_setup_firmware(component, params_rate(params));
		if (ret < 0)
			return ret;
	}

	if (adau->dai_fmt != SND_SOC_DAIFMT_RIGHT_J)
		return 0;

	switch (params_width(params)) {
	case 16:
		val = ADAU17X1_SERIAL_PORT1_DELAY16;
		break;
	case 24:
		val = ADAU17X1_SERIAL_PORT1_DELAY8;
		break;
	case 32:
		val = ADAU17X1_SERIAL_PORT1_DELAY0;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(adau->regmap, ADAU17X1_SERIAL_PORT1,
			ADAU17X1_SERIAL_PORT1_DELAY_MASK, val);
}

static int adau17x1_set_dai_fmt(struct snd_soc_dai *dai,
		unsigned int fmt)
{
	struct adau *adau = snd_soc_component_get_drvdata(dai->component);
	unsigned int ctrl0, ctrl1;
	int lrclk_pol;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		ctrl0 = ADAU17X1_SERIAL_PORT0_MASTER;
		adau->master = true;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		ctrl0 = 0;
		adau->master = false;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		lrclk_pol = 0;
		ctrl1 = ADAU17X1_SERIAL_PORT1_DELAY1;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_RIGHT_J:
		lrclk_pol = 1;
		ctrl1 = ADAU17X1_SERIAL_PORT1_DELAY0;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		lrclk_pol = 1;
		ctrl0 |= ADAU17X1_SERIAL_PORT0_PULSE_MODE;
		ctrl1 = ADAU17X1_SERIAL_PORT1_DELAY1;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		lrclk_pol = 1;
		ctrl0 |= ADAU17X1_SERIAL_PORT0_PULSE_MODE;
		ctrl1 = ADAU17X1_SERIAL_PORT1_DELAY0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		ctrl0 |= ADAU17X1_SERIAL_PORT0_BCLK_POL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		lrclk_pol = !lrclk_pol;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		ctrl0 |= ADAU17X1_SERIAL_PORT0_BCLK_POL;
		lrclk_pol = !lrclk_pol;
		break;
	default:
		return -EINVAL;
	}

	if (lrclk_pol)
		ctrl0 |= ADAU17X1_SERIAL_PORT0_LRCLK_POL;

	regmap_write(adau->regmap, ADAU17X1_SERIAL_PORT0, ctrl0);
	regmap_write(adau->regmap, ADAU17X1_SERIAL_PORT1, ctrl1);

	adau->dai_fmt = fmt & SND_SOC_DAIFMT_FORMAT_MASK;

	return 0;
}

static int adau17x1_set_dai_tdm_slot(struct snd_soc_dai *dai,
	unsigned int tx_mask, unsigned int rx_mask, int slots, int slot_width)
{
	struct adau *adau = snd_soc_component_get_drvdata(dai->component);
	unsigned int ser_ctrl0, ser_ctrl1;
	unsigned int conv_ctrl0, conv_ctrl1;

	/* I2S mode */
	if (slots == 0) {
		slots = 2;
		rx_mask = 3;
		tx_mask = 3;
		slot_width = 32;
	}

	switch (slots) {
	case 2:
		ser_ctrl0 = ADAU17X1_SERIAL_PORT0_STEREO;
		break;
	case 4:
		ser_ctrl0 = ADAU17X1_SERIAL_PORT0_TDM4;
		break;
	case 8:
		if (adau->type == ADAU1361)
			return -EINVAL;

		ser_ctrl0 = ADAU17X1_SERIAL_PORT0_TDM8;
		break;
	default:
		return -EINVAL;
	}

	switch (slot_width * slots) {
	case 32:
		if (adau->type == ADAU1761)
			return -EINVAL;

		ser_ctrl1 = ADAU17X1_SERIAL_PORT1_BCLK32;
		break;
	case 64:
		ser_ctrl1 = ADAU17X1_SERIAL_PORT1_BCLK64;
		break;
	case 48:
		ser_ctrl1 = ADAU17X1_SERIAL_PORT1_BCLK48;
		break;
	case 128:
		ser_ctrl1 = ADAU17X1_SERIAL_PORT1_BCLK128;
		break;
	case 256:
		if (adau->type == ADAU1361)
			return -EINVAL;

		ser_ctrl1 = ADAU17X1_SERIAL_PORT1_BCLK256;
		break;
	default:
		return -EINVAL;
	}

	switch (rx_mask) {
	case 0x03:
		conv_ctrl1 = ADAU17X1_CONVERTER1_ADC_PAIR(1);
		adau->tdm_slot[SNDRV_PCM_STREAM_CAPTURE] = 0;
		break;
	case 0x0c:
		conv_ctrl1 = ADAU17X1_CONVERTER1_ADC_PAIR(2);
		adau->tdm_slot[SNDRV_PCM_STREAM_CAPTURE] = 1;
		break;
	case 0x30:
		conv_ctrl1 = ADAU17X1_CONVERTER1_ADC_PAIR(3);
		adau->tdm_slot[SNDRV_PCM_STREAM_CAPTURE] = 2;
		break;
	case 0xc0:
		conv_ctrl1 = ADAU17X1_CONVERTER1_ADC_PAIR(4);
		adau->tdm_slot[SNDRV_PCM_STREAM_CAPTURE] = 3;
		break;
	default:
		return -EINVAL;
	}

	switch (tx_mask) {
	case 0x03:
		conv_ctrl0 = ADAU17X1_CONVERTER0_DAC_PAIR(1);
		adau->tdm_slot[SNDRV_PCM_STREAM_PLAYBACK] = 0;
		break;
	case 0x0c:
		conv_ctrl0 = ADAU17X1_CONVERTER0_DAC_PAIR(2);
		adau->tdm_slot[SNDRV_PCM_STREAM_PLAYBACK] = 1;
		break;
	case 0x30:
		conv_ctrl0 = ADAU17X1_CONVERTER0_DAC_PAIR(3);
		adau->tdm_slot[SNDRV_PCM_STREAM_PLAYBACK] = 2;
		break;
	case 0xc0:
		conv_ctrl0 = ADAU17X1_CONVERTER0_DAC_PAIR(4);
		adau->tdm_slot[SNDRV_PCM_STREAM_PLAYBACK] = 3;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(adau->regmap, ADAU17X1_CONVERTER0,
		ADAU17X1_CONVERTER0_DAC_PAIR_MASK, conv_ctrl0);
	regmap_update_bits(adau->regmap, ADAU17X1_CONVERTER1,
		ADAU17X1_CONVERTER1_ADC_PAIR_MASK, conv_ctrl1);
	regmap_update_bits(adau->regmap, ADAU17X1_SERIAL_PORT0,
		ADAU17X1_SERIAL_PORT0_TDM_MASK, ser_ctrl0);
	regmap_update_bits(adau->regmap, ADAU17X1_SERIAL_PORT1,
		ADAU17X1_SERIAL_PORT1_BCLK_MASK, ser_ctrl1);

	if (!adau17x1_has_dsp(adau))
		return 0;

	if (adau->dsp_bypass[SNDRV_PCM_STREAM_PLAYBACK]) {
		regmap_write(adau->regmap, ADAU17X1_SERIAL_INPUT_ROUTE,
			(adau->tdm_slot[SNDRV_PCM_STREAM_PLAYBACK] * 2) + 1);
	}

	if (adau->dsp_bypass[SNDRV_PCM_STREAM_CAPTURE]) {
		regmap_write(adau->regmap, ADAU17X1_SERIAL_OUTPUT_ROUTE,
			(adau->tdm_slot[SNDRV_PCM_STREAM_CAPTURE] * 2) + 1);
	}

	return 0;
}

static int adau17x1_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct adau *adau = snd_soc_component_get_drvdata(dai->component);

	if (adau->sigmadsp)
		return sigmadsp_restrict_params(adau->sigmadsp, substream);

	return 0;
}

const struct snd_soc_dai_ops adau17x1_dai_ops = {
	.hw_params	= adau17x1_hw_params,
	.set_sysclk	= adau17x1_set_dai_sysclk,
	.set_fmt	= adau17x1_set_dai_fmt,
	.set_pll	= adau17x1_set_dai_pll,
	.set_tdm_slot	= adau17x1_set_dai_tdm_slot,
	.startup	= adau17x1_startup,
};
EXPORT_SYMBOL_GPL(adau17x1_dai_ops);

int adau17x1_set_micbias_voltage(struct snd_soc_component *component,
	enum adau17x1_micbias_voltage micbias)
{
	struct adau *adau = snd_soc_component_get_drvdata(component);

	switch (micbias) {
	case ADAU17X1_MICBIAS_0_90_AVDD:
	case ADAU17X1_MICBIAS_0_65_AVDD:
		break;
	default:
		return -EINVAL;
	}

	return regmap_write(adau->regmap, ADAU17X1_MICBIAS, micbias << 2);
}
EXPORT_SYMBOL_GPL(adau17x1_set_micbias_voltage);

bool adau17x1_precious_register(struct device *dev, unsigned int reg)
{
	/* SigmaDSP parameter memory */
	if (reg < 0x400)
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(adau17x1_precious_register);

bool adau17x1_readable_register(struct device *dev, unsigned int reg)
{
	/* SigmaDSP parameter memory */
	if (reg < 0x400)
		return true;

	switch (reg) {
	case ADAU17X1_CLOCK_CONTROL:
	case ADAU17X1_PLL_CONTROL:
	case ADAU17X1_REC_POWER_MGMT:
	case ADAU17X1_MICBIAS:
	case ADAU17X1_SERIAL_PORT0:
	case ADAU17X1_SERIAL_PORT1:
	case ADAU17X1_CONVERTER0:
	case ADAU17X1_CONVERTER1:
	case ADAU17X1_LEFT_INPUT_DIGITAL_VOL:
	case ADAU17X1_RIGHT_INPUT_DIGITAL_VOL:
	case ADAU17X1_ADC_CONTROL:
	case ADAU17X1_PLAY_POWER_MGMT:
	case ADAU17X1_DAC_CONTROL0:
	case ADAU17X1_DAC_CONTROL1:
	case ADAU17X1_DAC_CONTROL2:
	case ADAU17X1_SERIAL_PORT_PAD:
	case ADAU17X1_CONTROL_PORT_PAD0:
	case ADAU17X1_CONTROL_PORT_PAD1:
	case ADAU17X1_DSP_SAMPLING_RATE:
	case ADAU17X1_SERIAL_INPUT_ROUTE:
	case ADAU17X1_SERIAL_OUTPUT_ROUTE:
	case ADAU17X1_DSP_ENABLE:
	case ADAU17X1_DSP_RUN:
	case ADAU17X1_SERIAL_SAMPLING_RATE:
		return true;
	default:
		break;
	}
	return false;
}
EXPORT_SYMBOL_GPL(adau17x1_readable_register);

bool adau17x1_volatile_register(struct device *dev, unsigned int reg)
{
	/* SigmaDSP parameter and program memory */
	if (reg < 0x4000)
		return true;

	switch (reg) {
	/* The PLL register is 6 bytes long */
	case ADAU17X1_PLL_CONTROL:
	case ADAU17X1_PLL_CONTROL + 1:
	case ADAU17X1_PLL_CONTROL + 2:
	case ADAU17X1_PLL_CONTROL + 3:
	case ADAU17X1_PLL_CONTROL + 4:
	case ADAU17X1_PLL_CONTROL + 5:
		return true;
	default:
		break;
	}

	return false;
}
EXPORT_SYMBOL_GPL(adau17x1_volatile_register);

static int adau17x1_setup_firmware(struct snd_soc_component *component,
	unsigned int rate)
{
	int ret;
	int dspsr, dsp_run;
	struct adau *adau = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);

	/* Check if sample rate is the same as before. If it is there is no
	 * point in performing the below steps as the call to
	 * sigmadsp_setup(...) will return directly when it finds the sample
	 * rate to be the same as before. By checking this we can prevent an
	 * audiable popping noise which occours when toggling DSP_RUN.
	 */
	if (adau->sigmadsp->current_samplerate == rate)
		return 0;

	snd_soc_dapm_mutex_lock(dapm);

	ret = regmap_read(adau->regmap, ADAU17X1_DSP_SAMPLING_RATE, &dspsr);
	if (ret)
		goto err;

	ret = regmap_read(adau->regmap, ADAU17X1_DSP_RUN, &dsp_run);
	if (ret)
		goto err;

	regmap_write(adau->regmap, ADAU17X1_DSP_ENABLE, 1);
	regmap_write(adau->regmap, ADAU17X1_DSP_SAMPLING_RATE, 0xf);
	regmap_write(adau->regmap, ADAU17X1_DSP_RUN, 0);

	ret = sigmadsp_setup(adau->sigmadsp, rate);
	if (ret) {
		regmap_write(adau->regmap, ADAU17X1_DSP_ENABLE, 0);
		goto err;
	}
	regmap_write(adau->regmap, ADAU17X1_DSP_SAMPLING_RATE, dspsr);
	regmap_write(adau->regmap, ADAU17X1_DSP_RUN, dsp_run);

err:
	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}

int adau17x1_add_widgets(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct adau *adau = snd_soc_component_get_drvdata(component);
	int ret;

	ret = snd_soc_add_component_controls(component, adau17x1_controls,
		ARRAY_SIZE(adau17x1_controls));
	if (ret)
		return ret;
	ret = snd_soc_dapm_new_controls(dapm, adau17x1_dapm_widgets,
		ARRAY_SIZE(adau17x1_dapm_widgets));
	if (ret)
		return ret;

	if (adau17x1_has_dsp(adau)) {
		ret = snd_soc_dapm_new_controls(dapm, adau17x1_dsp_dapm_widgets,
			ARRAY_SIZE(adau17x1_dsp_dapm_widgets));
		if (ret)
			return ret;

		if (!adau->sigmadsp)
			return 0;

		ret = sigmadsp_attach(adau->sigmadsp, component);
		if (ret) {
			dev_err(component->dev, "Failed to attach firmware: %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(adau17x1_add_widgets);

int adau17x1_add_routes(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct adau *adau = snd_soc_component_get_drvdata(component);
	int ret;

	ret = snd_soc_dapm_add_routes(dapm, adau17x1_dapm_routes,
		ARRAY_SIZE(adau17x1_dapm_routes));
	if (ret)
		return ret;

	if (adau17x1_has_dsp(adau)) {
		ret = snd_soc_dapm_add_routes(dapm, adau17x1_dsp_dapm_routes,
			ARRAY_SIZE(adau17x1_dsp_dapm_routes));
	} else {
		ret = snd_soc_dapm_add_routes(dapm, adau17x1_no_dsp_dapm_routes,
			ARRAY_SIZE(adau17x1_no_dsp_dapm_routes));
	}

	if (adau->clk_src != ADAU17X1_CLK_SRC_MCLK)
		snd_soc_dapm_add_routes(dapm, &adau17x1_dapm_pll_route, 1);

	return ret;
}
EXPORT_SYMBOL_GPL(adau17x1_add_routes);

int adau17x1_resume(struct snd_soc_component *component)
{
	struct adau *adau = snd_soc_component_get_drvdata(component);

	if (adau->switch_mode)
		adau->switch_mode(component->dev);

	regcache_sync(adau->regmap);

	return 0;
}
EXPORT_SYMBOL_GPL(adau17x1_resume);

static int adau17x1_safeload(struct sigmadsp *sigmadsp, unsigned int addr,
	const uint8_t bytes[], size_t len)
{
	uint8_t buf[ADAU17X1_WORD_SIZE];
	uint8_t data[ADAU17X1_SAFELOAD_DATA_SIZE];
	unsigned int addr_offset;
	unsigned int nbr_words;
	int ret;

	/* write data to safeload addresses. Check if len is not a multiple of
	 * 4 bytes, if so we need to zero pad.
	 */
	nbr_words = len / ADAU17X1_WORD_SIZE;
	if ((len - nbr_words * ADAU17X1_WORD_SIZE) == 0) {
		ret = regmap_raw_write(sigmadsp->control_data,
			ADAU17X1_SAFELOAD_DATA, bytes, len);
	} else {
		nbr_words++;
		memset(data, 0, ADAU17X1_SAFELOAD_DATA_SIZE);
		memcpy(data, bytes, len);
		ret = regmap_raw_write(sigmadsp->control_data,
			ADAU17X1_SAFELOAD_DATA, data,
			nbr_words * ADAU17X1_WORD_SIZE);
	}

	if (ret < 0)
		return ret;

	/* Write target address, target address is offset by 1 */
	addr_offset = addr - 1;
	put_unaligned_be32(addr_offset, buf);
	ret = regmap_raw_write(sigmadsp->control_data,
		ADAU17X1_SAFELOAD_TARGET_ADDRESS, buf, ADAU17X1_WORD_SIZE);
	if (ret < 0)
		return ret;

	/* write nbr of words to trigger address */
	put_unaligned_be32(nbr_words, buf);
	ret = regmap_raw_write(sigmadsp->control_data,
		ADAU17X1_SAFELOAD_TRIGGER, buf, ADAU17X1_WORD_SIZE);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct sigmadsp_ops adau17x1_sigmadsp_ops = {
	.safeload = adau17x1_safeload,
};

int adau17x1_probe(struct device *dev, struct regmap *regmap,
	enum adau17x1_type type, void (*switch_mode)(struct device *dev),
	const char *firmware_name)
{
	struct adau *adau;
	int ret;

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	adau = devm_kzalloc(dev, sizeof(*adau), GFP_KERNEL);
	if (!adau)
		return -ENOMEM;

	adau->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(adau->mclk)) {
		if (PTR_ERR(adau->mclk) != -ENOENT)
			return PTR_ERR(adau->mclk);
		/* Clock is optional (for the driver) */
		adau->mclk = NULL;
	} else if (adau->mclk) {
		adau->clk_src = ADAU17X1_CLK_SRC_PLL_AUTO;

		/*
		 * Any valid PLL output rate will work at this point, use one
		 * that is likely to be chosen later as well. The register will
		 * be written when the PLL is powered up for the first time.
		 */
		ret = adau_calc_pll_cfg(clk_get_rate(adau->mclk), 48000 * 1024,
				adau->pll_regs);
		if (ret < 0)
			return ret;

		ret = clk_prepare_enable(adau->mclk);
		if (ret)
			return ret;
	}

	adau->regmap = regmap;
	adau->switch_mode = switch_mode;
	adau->type = type;

	dev_set_drvdata(dev, adau);

	if (firmware_name) {
		if (adau17x1_has_safeload(adau)) {
			adau->sigmadsp = devm_sigmadsp_init_regmap(dev, regmap,
				&adau17x1_sigmadsp_ops, firmware_name);
		} else {
			adau->sigmadsp = devm_sigmadsp_init_regmap(dev, regmap,
				NULL, firmware_name);
		}
		if (IS_ERR(adau->sigmadsp)) {
			dev_warn(dev, "Could not find firmware file: %ld\n",
				PTR_ERR(adau->sigmadsp));
			adau->sigmadsp = NULL;
		}
	}

	if (switch_mode)
		switch_mode(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(adau17x1_probe);

void adau17x1_remove(struct device *dev)
{
	struct adau *adau = dev_get_drvdata(dev);

	if (adau->mclk)
		clk_disable_unprepare(adau->mclk);
}
EXPORT_SYMBOL_GPL(adau17x1_remove);

MODULE_DESCRIPTION("ASoC ADAU1X61/ADAU1X81 common code");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
