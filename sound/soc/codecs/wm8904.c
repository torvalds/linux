/*
 * wm8904.c  --  WM8904 ALSA SoC Audio driver
 *
 * Copyright 2009-12 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/wm8904.h>

#include "wm8904.h"

enum wm8904_type {
	WM8904,
	WM8912,
};

#define WM8904_NUM_DCS_CHANNELS 4

#define WM8904_NUM_SUPPLIES 5
static const char *wm8904_supply_names[WM8904_NUM_SUPPLIES] = {
	"DCVDD",
	"DBVDD",
	"AVDD",
	"CPVDD",
	"MICVDD",
};

/* codec private data */
struct wm8904_priv {
	struct regmap *regmap;
	struct clk *mclk;

	enum wm8904_type devtype;

	struct regulator_bulk_data supplies[WM8904_NUM_SUPPLIES];

	struct wm8904_pdata *pdata;

	int deemph;

	/* Platform provided DRC configuration */
	const char **drc_texts;
	int drc_cfg;
	struct soc_enum drc_enum;

	/* Platform provided ReTune mobile configuration */
	int num_retune_mobile_texts;
	const char **retune_mobile_texts;
	int retune_mobile_cfg;
	struct soc_enum retune_mobile_enum;

	/* FLL setup */
	int fll_src;
	int fll_fref;
	int fll_fout;

	/* Clocking configuration */
	unsigned int mclk_rate;
	int sysclk_src;
	unsigned int sysclk_rate;

	int tdm_width;
	int tdm_slots;
	int bclk;
	int fs;

	/* DC servo configuration - cached offset values */
	int dcs_state[WM8904_NUM_DCS_CHANNELS];
};

static const struct reg_default wm8904_reg_defaults[] = {
	{ 4,   0x0018 },     /* R4   - Bias Control 0 */
	{ 5,   0x0000 },     /* R5   - VMID Control 0 */
	{ 6,   0x0000 },     /* R6   - Mic Bias Control 0 */
	{ 7,   0x0000 },     /* R7   - Mic Bias Control 1 */
	{ 8,   0x0001 },     /* R8   - Analogue DAC 0 */
	{ 9,   0x9696 },     /* R9   - mic Filter Control */
	{ 10,  0x0001 },     /* R10  - Analogue ADC 0 */
	{ 12,  0x0000 },     /* R12  - Power Management 0 */
	{ 14,  0x0000 },     /* R14  - Power Management 2 */
	{ 15,  0x0000 },     /* R15  - Power Management 3 */
	{ 18,  0x0000 },     /* R18  - Power Management 6 */
	{ 20,  0x945E },     /* R20  - Clock Rates 0 */
	{ 21,  0x0C05 },     /* R21  - Clock Rates 1 */
	{ 22,  0x0006 },     /* R22  - Clock Rates 2 */
	{ 24,  0x0050 },     /* R24  - Audio Interface 0 */
	{ 25,  0x000A },     /* R25  - Audio Interface 1 */
	{ 26,  0x00E4 },     /* R26  - Audio Interface 2 */
	{ 27,  0x0040 },     /* R27  - Audio Interface 3 */
	{ 30,  0x00C0 },     /* R30  - DAC Digital Volume Left */
	{ 31,  0x00C0 },     /* R31  - DAC Digital Volume Right */
	{ 32,  0x0000 },     /* R32  - DAC Digital 0 */
	{ 33,  0x0008 },     /* R33  - DAC Digital 1 */
	{ 36,  0x00C0 },     /* R36  - ADC Digital Volume Left */
	{ 37,  0x00C0 },     /* R37  - ADC Digital Volume Right */
	{ 38,  0x0010 },     /* R38  - ADC Digital 0 */
	{ 39,  0x0000 },     /* R39  - Digital Microphone 0 */
	{ 40,  0x01AF },     /* R40  - DRC 0 */
	{ 41,  0x3248 },     /* R41  - DRC 1 */
	{ 42,  0x0000 },     /* R42  - DRC 2 */
	{ 43,  0x0000 },     /* R43  - DRC 3 */
	{ 44,  0x0085 },     /* R44  - Analogue Left Input 0 */
	{ 45,  0x0085 },     /* R45  - Analogue Right Input 0 */
	{ 46,  0x0044 },     /* R46  - Analogue Left Input 1 */
	{ 47,  0x0044 },     /* R47  - Analogue Right Input 1 */
	{ 57,  0x002D },     /* R57  - Analogue OUT1 Left */
	{ 58,  0x002D },     /* R58  - Analogue OUT1 Right */
	{ 59,  0x0039 },     /* R59  - Analogue OUT2 Left */
	{ 60,  0x0039 },     /* R60  - Analogue OUT2 Right */
	{ 61,  0x0000 },     /* R61  - Analogue OUT12 ZC */
	{ 67,  0x0000 },     /* R67  - DC Servo 0 */
	{ 69,  0xAAAA },     /* R69  - DC Servo 2 */
	{ 71,  0xAAAA },     /* R71  - DC Servo 4 */
	{ 72,  0xAAAA },     /* R72  - DC Servo 5 */
	{ 90,  0x0000 },     /* R90  - Analogue HP 0 */
	{ 94,  0x0000 },     /* R94  - Analogue Lineout 0 */
	{ 98,  0x0000 },     /* R98  - Charge Pump 0 */
	{ 104, 0x0004 },     /* R104 - Class W 0 */
	{ 108, 0x0000 },     /* R108 - Write Sequencer 0 */
	{ 109, 0x0000 },     /* R109 - Write Sequencer 1 */
	{ 110, 0x0000 },     /* R110 - Write Sequencer 2 */
	{ 111, 0x0000 },     /* R111 - Write Sequencer 3 */
	{ 112, 0x0000 },     /* R112 - Write Sequencer 4 */
	{ 116, 0x0000 },     /* R116 - FLL Control 1 */
	{ 117, 0x0007 },     /* R117 - FLL Control 2 */
	{ 118, 0x0000 },     /* R118 - FLL Control 3 */
	{ 119, 0x2EE0 },     /* R119 - FLL Control 4 */
	{ 120, 0x0004 },     /* R120 - FLL Control 5 */
	{ 121, 0x0014 },     /* R121 - GPIO Control 1 */
	{ 122, 0x0010 },     /* R122 - GPIO Control 2 */
	{ 123, 0x0010 },     /* R123 - GPIO Control 3 */
	{ 124, 0x0000 },     /* R124 - GPIO Control 4 */
	{ 126, 0x0000 },     /* R126 - Digital Pulls */
	{ 128, 0xFFFF },     /* R128 - Interrupt Status Mask */
	{ 129, 0x0000 },     /* R129 - Interrupt Polarity */
	{ 130, 0x0000 },     /* R130 - Interrupt Debounce */
	{ 134, 0x0000 },     /* R134 - EQ1 */
	{ 135, 0x000C },     /* R135 - EQ2 */
	{ 136, 0x000C },     /* R136 - EQ3 */
	{ 137, 0x000C },     /* R137 - EQ4 */
	{ 138, 0x000C },     /* R138 - EQ5 */
	{ 139, 0x000C },     /* R139 - EQ6 */
	{ 140, 0x0FCA },     /* R140 - EQ7 */
	{ 141, 0x0400 },     /* R141 - EQ8 */
	{ 142, 0x00D8 },     /* R142 - EQ9 */
	{ 143, 0x1EB5 },     /* R143 - EQ10 */
	{ 144, 0xF145 },     /* R144 - EQ11 */
	{ 145, 0x0B75 },     /* R145 - EQ12 */
	{ 146, 0x01C5 },     /* R146 - EQ13 */
	{ 147, 0x1C58 },     /* R147 - EQ14 */
	{ 148, 0xF373 },     /* R148 - EQ15 */
	{ 149, 0x0A54 },     /* R149 - EQ16 */
	{ 150, 0x0558 },     /* R150 - EQ17 */
	{ 151, 0x168E },     /* R151 - EQ18 */
	{ 152, 0xF829 },     /* R152 - EQ19 */
	{ 153, 0x07AD },     /* R153 - EQ20 */
	{ 154, 0x1103 },     /* R154 - EQ21 */
	{ 155, 0x0564 },     /* R155 - EQ22 */
	{ 156, 0x0559 },     /* R156 - EQ23 */
	{ 157, 0x4000 },     /* R157 - EQ24 */
	{ 161, 0x0000 },     /* R161 - Control Interface Test 1 */
	{ 204, 0x0000 },     /* R204 - Analogue Output Bias 0 */
	{ 247, 0x0000 },     /* R247 - FLL NCO Test 0 */
	{ 248, 0x0019 },     /* R248 - FLL NCO Test 1 */
};

static bool wm8904_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8904_SW_RESET_AND_ID:
	case WM8904_REVISION:
	case WM8904_DC_SERVO_1:
	case WM8904_DC_SERVO_6:
	case WM8904_DC_SERVO_7:
	case WM8904_DC_SERVO_8:
	case WM8904_DC_SERVO_9:
	case WM8904_DC_SERVO_READBACK_0:
	case WM8904_INTERRUPT_STATUS:
		return true;
	default:
		return false;
	}
}

static bool wm8904_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8904_SW_RESET_AND_ID:
	case WM8904_REVISION:
	case WM8904_BIAS_CONTROL_0:
	case WM8904_VMID_CONTROL_0:
	case WM8904_MIC_BIAS_CONTROL_0:
	case WM8904_MIC_BIAS_CONTROL_1:
	case WM8904_ANALOGUE_DAC_0:
	case WM8904_MIC_FILTER_CONTROL:
	case WM8904_ANALOGUE_ADC_0:
	case WM8904_POWER_MANAGEMENT_0:
	case WM8904_POWER_MANAGEMENT_2:
	case WM8904_POWER_MANAGEMENT_3:
	case WM8904_POWER_MANAGEMENT_6:
	case WM8904_CLOCK_RATES_0:
	case WM8904_CLOCK_RATES_1:
	case WM8904_CLOCK_RATES_2:
	case WM8904_AUDIO_INTERFACE_0:
	case WM8904_AUDIO_INTERFACE_1:
	case WM8904_AUDIO_INTERFACE_2:
	case WM8904_AUDIO_INTERFACE_3:
	case WM8904_DAC_DIGITAL_VOLUME_LEFT:
	case WM8904_DAC_DIGITAL_VOLUME_RIGHT:
	case WM8904_DAC_DIGITAL_0:
	case WM8904_DAC_DIGITAL_1:
	case WM8904_ADC_DIGITAL_VOLUME_LEFT:
	case WM8904_ADC_DIGITAL_VOLUME_RIGHT:
	case WM8904_ADC_DIGITAL_0:
	case WM8904_DIGITAL_MICROPHONE_0:
	case WM8904_DRC_0:
	case WM8904_DRC_1:
	case WM8904_DRC_2:
	case WM8904_DRC_3:
	case WM8904_ANALOGUE_LEFT_INPUT_0:
	case WM8904_ANALOGUE_RIGHT_INPUT_0:
	case WM8904_ANALOGUE_LEFT_INPUT_1:
	case WM8904_ANALOGUE_RIGHT_INPUT_1:
	case WM8904_ANALOGUE_OUT1_LEFT:
	case WM8904_ANALOGUE_OUT1_RIGHT:
	case WM8904_ANALOGUE_OUT2_LEFT:
	case WM8904_ANALOGUE_OUT2_RIGHT:
	case WM8904_ANALOGUE_OUT12_ZC:
	case WM8904_DC_SERVO_0:
	case WM8904_DC_SERVO_1:
	case WM8904_DC_SERVO_2:
	case WM8904_DC_SERVO_4:
	case WM8904_DC_SERVO_5:
	case WM8904_DC_SERVO_6:
	case WM8904_DC_SERVO_7:
	case WM8904_DC_SERVO_8:
	case WM8904_DC_SERVO_9:
	case WM8904_DC_SERVO_READBACK_0:
	case WM8904_ANALOGUE_HP_0:
	case WM8904_ANALOGUE_LINEOUT_0:
	case WM8904_CHARGE_PUMP_0:
	case WM8904_CLASS_W_0:
	case WM8904_WRITE_SEQUENCER_0:
	case WM8904_WRITE_SEQUENCER_1:
	case WM8904_WRITE_SEQUENCER_2:
	case WM8904_WRITE_SEQUENCER_3:
	case WM8904_WRITE_SEQUENCER_4:
	case WM8904_FLL_CONTROL_1:
	case WM8904_FLL_CONTROL_2:
	case WM8904_FLL_CONTROL_3:
	case WM8904_FLL_CONTROL_4:
	case WM8904_FLL_CONTROL_5:
	case WM8904_GPIO_CONTROL_1:
	case WM8904_GPIO_CONTROL_2:
	case WM8904_GPIO_CONTROL_3:
	case WM8904_GPIO_CONTROL_4:
	case WM8904_DIGITAL_PULLS:
	case WM8904_INTERRUPT_STATUS:
	case WM8904_INTERRUPT_STATUS_MASK:
	case WM8904_INTERRUPT_POLARITY:
	case WM8904_INTERRUPT_DEBOUNCE:
	case WM8904_EQ1:
	case WM8904_EQ2:
	case WM8904_EQ3:
	case WM8904_EQ4:
	case WM8904_EQ5:
	case WM8904_EQ6:
	case WM8904_EQ7:
	case WM8904_EQ8:
	case WM8904_EQ9:
	case WM8904_EQ10:
	case WM8904_EQ11:
	case WM8904_EQ12:
	case WM8904_EQ13:
	case WM8904_EQ14:
	case WM8904_EQ15:
	case WM8904_EQ16:
	case WM8904_EQ17:
	case WM8904_EQ18:
	case WM8904_EQ19:
	case WM8904_EQ20:
	case WM8904_EQ21:
	case WM8904_EQ22:
	case WM8904_EQ23:
	case WM8904_EQ24:
	case WM8904_CONTROL_INTERFACE_TEST_1:
	case WM8904_ADC_TEST_0:
	case WM8904_ANALOGUE_OUTPUT_BIAS_0:
	case WM8904_FLL_NCO_TEST_0:
	case WM8904_FLL_NCO_TEST_1:
		return true;
	default:
		return true;
	}
}

static int wm8904_configure_clocking(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	unsigned int clock0, clock2, rate;

	/* Gate the clock while we're updating to avoid misclocking */
	clock2 = snd_soc_read(codec, WM8904_CLOCK_RATES_2);
	snd_soc_update_bits(codec, WM8904_CLOCK_RATES_2,
			    WM8904_SYSCLK_SRC, 0);

	/* This should be done on init() for bypass paths */
	switch (wm8904->sysclk_src) {
	case WM8904_CLK_MCLK:
		dev_dbg(codec->dev, "Using %dHz MCLK\n", wm8904->mclk_rate);

		clock2 &= ~WM8904_SYSCLK_SRC;
		rate = wm8904->mclk_rate;

		/* Ensure the FLL is stopped */
		snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
				    WM8904_FLL_OSC_ENA | WM8904_FLL_ENA, 0);
		break;

	case WM8904_CLK_FLL:
		dev_dbg(codec->dev, "Using %dHz FLL clock\n",
			wm8904->fll_fout);

		clock2 |= WM8904_SYSCLK_SRC;
		rate = wm8904->fll_fout;
		break;

	default:
		dev_err(codec->dev, "System clock not configured\n");
		return -EINVAL;
	}

	/* SYSCLK shouldn't be over 13.5MHz */
	if (rate > 13500000) {
		clock0 = WM8904_MCLK_DIV;
		wm8904->sysclk_rate = rate / 2;
	} else {
		clock0 = 0;
		wm8904->sysclk_rate = rate;
	}

	snd_soc_update_bits(codec, WM8904_CLOCK_RATES_0, WM8904_MCLK_DIV,
			    clock0);

	snd_soc_update_bits(codec, WM8904_CLOCK_RATES_2,
			    WM8904_CLK_SYS_ENA | WM8904_SYSCLK_SRC, clock2);

	dev_dbg(codec->dev, "CLK_SYS is %dHz\n", wm8904->sysclk_rate);

	return 0;
}

static void wm8904_set_drc(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct wm8904_pdata *pdata = wm8904->pdata;
	int save, i;

	/* Save any enables; the configuration should clear them. */
	save = snd_soc_read(codec, WM8904_DRC_0);

	for (i = 0; i < WM8904_DRC_REGS; i++)
		snd_soc_update_bits(codec, WM8904_DRC_0 + i, 0xffff,
				    pdata->drc_cfgs[wm8904->drc_cfg].regs[i]);

	/* Reenable the DRC */
	snd_soc_update_bits(codec, WM8904_DRC_0,
			    WM8904_DRC_ENA | WM8904_DRC_DAC_PATH, save);
}

static int wm8904_put_drc_enum(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct wm8904_pdata *pdata = wm8904->pdata;
	int value = ucontrol->value.integer.value[0];

	if (value >= pdata->num_drc_cfgs)
		return -EINVAL;

	wm8904->drc_cfg = value;

	wm8904_set_drc(codec);

	return 0;
}

static int wm8904_get_drc_enum(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = wm8904->drc_cfg;

	return 0;
}

static void wm8904_set_retune_mobile(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct wm8904_pdata *pdata = wm8904->pdata;
	int best, best_val, save, i, cfg;

	if (!pdata || !wm8904->num_retune_mobile_texts)
		return;

	/* Find the version of the currently selected configuration
	 * with the nearest sample rate. */
	cfg = wm8904->retune_mobile_cfg;
	best = 0;
	best_val = INT_MAX;
	for (i = 0; i < pdata->num_retune_mobile_cfgs; i++) {
		if (strcmp(pdata->retune_mobile_cfgs[i].name,
			   wm8904->retune_mobile_texts[cfg]) == 0 &&
		    abs(pdata->retune_mobile_cfgs[i].rate
			- wm8904->fs) < best_val) {
			best = i;
			best_val = abs(pdata->retune_mobile_cfgs[i].rate
				       - wm8904->fs);
		}
	}

	dev_dbg(codec->dev, "ReTune Mobile %s/%dHz for %dHz sample rate\n",
		pdata->retune_mobile_cfgs[best].name,
		pdata->retune_mobile_cfgs[best].rate,
		wm8904->fs);

	/* The EQ will be disabled while reconfiguring it, remember the
	 * current configuration. 
	 */
	save = snd_soc_read(codec, WM8904_EQ1);

	for (i = 0; i < WM8904_EQ_REGS; i++)
		snd_soc_update_bits(codec, WM8904_EQ1 + i, 0xffff,
				pdata->retune_mobile_cfgs[best].regs[i]);

	snd_soc_update_bits(codec, WM8904_EQ1, WM8904_EQ_ENA, save);
}

static int wm8904_put_retune_mobile_enum(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct wm8904_pdata *pdata = wm8904->pdata;
	int value = ucontrol->value.integer.value[0];

	if (value >= pdata->num_retune_mobile_cfgs)
		return -EINVAL;

	wm8904->retune_mobile_cfg = value;

	wm8904_set_retune_mobile(codec);

	return 0;
}

static int wm8904_get_retune_mobile_enum(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = wm8904->retune_mobile_cfg;

	return 0;
}

static int deemph_settings[] = { 0, 32000, 44100, 48000 };

static int wm8904_set_deemph(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	int val, i, best;

	/* If we're using deemphasis select the nearest available sample 
	 * rate.
	 */
	if (wm8904->deemph) {
		best = 1;
		for (i = 2; i < ARRAY_SIZE(deemph_settings); i++) {
			if (abs(deemph_settings[i] - wm8904->fs) <
			    abs(deemph_settings[best] - wm8904->fs))
				best = i;
		}

		val = best << WM8904_DEEMPH_SHIFT;
	} else {
		val = 0;
	}

	dev_dbg(codec->dev, "Set deemphasis %d\n", val);

	return snd_soc_update_bits(codec, WM8904_DAC_DIGITAL_1,
				   WM8904_DEEMPH_MASK, val);
}

static int wm8904_get_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = wm8904->deemph;
	return 0;
}

static int wm8904_put_deemph(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	int deemph = ucontrol->value.integer.value[0];

	if (deemph > 1)
		return -EINVAL;

	wm8904->deemph = deemph;

	return wm8904_set_deemph(codec);
}

static const DECLARE_TLV_DB_SCALE(dac_boost_tlv, 0, 600, 0);
static const DECLARE_TLV_DB_SCALE(digital_tlv, -7200, 75, 1);
static const DECLARE_TLV_DB_SCALE(out_tlv, -5700, 100, 0);
static const DECLARE_TLV_DB_SCALE(sidetone_tlv, -3600, 300, 0);
static const DECLARE_TLV_DB_SCALE(eq_tlv, -1200, 100, 0);

static const char *input_mode_text[] = {
	"Single-Ended", "Differential Line", "Differential Mic"
};

static SOC_ENUM_SINGLE_DECL(lin_mode,
			    WM8904_ANALOGUE_LEFT_INPUT_1, 0,
			    input_mode_text);

static SOC_ENUM_SINGLE_DECL(rin_mode,
			    WM8904_ANALOGUE_RIGHT_INPUT_1, 0,
			    input_mode_text);

static const char *hpf_mode_text[] = {
	"Hi-fi", "Voice 1", "Voice 2", "Voice 3"
};

static SOC_ENUM_SINGLE_DECL(hpf_mode, WM8904_ADC_DIGITAL_0, 5,
			    hpf_mode_text);

static int wm8904_adc_osr_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	unsigned int val;
	int ret;

	ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret < 0)
		return ret;

	if (ucontrol->value.integer.value[0])
		val = 0;
	else
		val = WM8904_ADC_128_OSR_TST_MODE | WM8904_ADC_BIASX1P5;

	snd_soc_update_bits(codec, WM8904_ADC_TEST_0,
			    WM8904_ADC_128_OSR_TST_MODE | WM8904_ADC_BIASX1P5,
			    val);

	return ret;
}

static const struct snd_kcontrol_new wm8904_adc_snd_controls[] = {
SOC_DOUBLE_R_TLV("Digital Capture Volume", WM8904_ADC_DIGITAL_VOLUME_LEFT,
		 WM8904_ADC_DIGITAL_VOLUME_RIGHT, 1, 119, 0, digital_tlv),

SOC_ENUM("Left Caputure Mode", lin_mode),
SOC_ENUM("Right Capture Mode", rin_mode),

/* No TLV since it depends on mode */
SOC_DOUBLE_R("Capture Volume", WM8904_ANALOGUE_LEFT_INPUT_0,
	     WM8904_ANALOGUE_RIGHT_INPUT_0, 0, 31, 0),
SOC_DOUBLE_R("Capture Switch", WM8904_ANALOGUE_LEFT_INPUT_0,
	     WM8904_ANALOGUE_RIGHT_INPUT_0, 7, 1, 1),

SOC_SINGLE("High Pass Filter Switch", WM8904_ADC_DIGITAL_0, 4, 1, 0),
SOC_ENUM("High Pass Filter Mode", hpf_mode),
SOC_SINGLE_EXT("ADC 128x OSR Switch", WM8904_ANALOGUE_ADC_0, 0, 1, 0,
	snd_soc_get_volsw, wm8904_adc_osr_put),
};

static const char *drc_path_text[] = {
	"ADC", "DAC"
};

static SOC_ENUM_SINGLE_DECL(drc_path, WM8904_DRC_0, 14, drc_path_text);

static const struct snd_kcontrol_new wm8904_dac_snd_controls[] = {
SOC_SINGLE_TLV("Digital Playback Boost Volume", 
	       WM8904_AUDIO_INTERFACE_0, 9, 3, 0, dac_boost_tlv),
SOC_DOUBLE_R_TLV("Digital Playback Volume", WM8904_DAC_DIGITAL_VOLUME_LEFT,
		 WM8904_DAC_DIGITAL_VOLUME_RIGHT, 1, 96, 0, digital_tlv),

SOC_DOUBLE_R_TLV("Headphone Volume", WM8904_ANALOGUE_OUT1_LEFT,
		 WM8904_ANALOGUE_OUT1_RIGHT, 0, 63, 0, out_tlv),
SOC_DOUBLE_R("Headphone Switch", WM8904_ANALOGUE_OUT1_LEFT,
	     WM8904_ANALOGUE_OUT1_RIGHT, 8, 1, 1),
SOC_DOUBLE_R("Headphone ZC Switch", WM8904_ANALOGUE_OUT1_LEFT,
	     WM8904_ANALOGUE_OUT1_RIGHT, 6, 1, 0),

SOC_DOUBLE_R_TLV("Line Output Volume", WM8904_ANALOGUE_OUT2_LEFT,
		 WM8904_ANALOGUE_OUT2_RIGHT, 0, 63, 0, out_tlv),
SOC_DOUBLE_R("Line Output Switch", WM8904_ANALOGUE_OUT2_LEFT,
	     WM8904_ANALOGUE_OUT2_RIGHT, 8, 1, 1),
SOC_DOUBLE_R("Line Output ZC Switch", WM8904_ANALOGUE_OUT2_LEFT,
	     WM8904_ANALOGUE_OUT2_RIGHT, 6, 1, 0),

SOC_SINGLE("EQ Switch", WM8904_EQ1, 0, 1, 0),
SOC_SINGLE("DRC Switch", WM8904_DRC_0, 15, 1, 0),
SOC_ENUM("DRC Path", drc_path),
SOC_SINGLE("DAC OSRx2 Switch", WM8904_DAC_DIGITAL_1, 6, 1, 0),
SOC_SINGLE_BOOL_EXT("DAC Deemphasis Switch", 0,
		    wm8904_get_deemph, wm8904_put_deemph),
};

static const struct snd_kcontrol_new wm8904_snd_controls[] = {
SOC_DOUBLE_TLV("Digital Sidetone Volume", WM8904_DAC_DIGITAL_0, 4, 8, 15, 0,
	       sidetone_tlv),
};

static const struct snd_kcontrol_new wm8904_eq_controls[] = {
SOC_SINGLE_TLV("EQ1 Volume", WM8904_EQ2, 0, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ2 Volume", WM8904_EQ3, 0, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ3 Volume", WM8904_EQ4, 0, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ4 Volume", WM8904_EQ5, 0, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ5 Volume", WM8904_EQ6, 0, 24, 0, eq_tlv),
};

static int cp_event(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	if (WARN_ON(event != SND_SOC_DAPM_POST_PMU))
		return -EINVAL;

	/* Maximum startup time */
	udelay(500);

	return 0;
}

static int sysclk_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* If we're using the FLL then we only start it when
		 * required; we assume that the configuration has been
		 * done previously and all we need to do is kick it
		 * off.
		 */
		switch (wm8904->sysclk_src) {
		case WM8904_CLK_FLL:
			snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
					    WM8904_FLL_OSC_ENA,
					    WM8904_FLL_OSC_ENA);

			snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
					    WM8904_FLL_ENA,
					    WM8904_FLL_ENA);
			break;

		default:
			break;
		}
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
				    WM8904_FLL_OSC_ENA | WM8904_FLL_ENA, 0);
		break;
	}

	return 0;
}

static int out_pga_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	int reg, val;
	int dcs_mask;
	int dcs_l, dcs_r;
	int dcs_l_reg, dcs_r_reg;
	int timeout;
	int pwr_reg;

	/* This code is shared between HP and LINEOUT; we do all our
	 * power management in stereo pairs to avoid latency issues so
	 * we reuse shift to identify which rather than strcmp() the
	 * name. */
	reg = w->shift;

	switch (reg) {
	case WM8904_ANALOGUE_HP_0:
		pwr_reg = WM8904_POWER_MANAGEMENT_2;
		dcs_mask = WM8904_DCS_ENA_CHAN_0 | WM8904_DCS_ENA_CHAN_1;
		dcs_r_reg = WM8904_DC_SERVO_8;
		dcs_l_reg = WM8904_DC_SERVO_9;
		dcs_l = 0;
		dcs_r = 1;
		break;
	case WM8904_ANALOGUE_LINEOUT_0:
		pwr_reg = WM8904_POWER_MANAGEMENT_3;
		dcs_mask = WM8904_DCS_ENA_CHAN_2 | WM8904_DCS_ENA_CHAN_3;
		dcs_r_reg = WM8904_DC_SERVO_6;
		dcs_l_reg = WM8904_DC_SERVO_7;
		dcs_l = 2;
		dcs_r = 3;
		break;
	default:
		WARN(1, "Invalid reg %d\n", reg);
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Power on the PGAs */
		snd_soc_update_bits(codec, pwr_reg,
				    WM8904_HPL_PGA_ENA | WM8904_HPR_PGA_ENA,
				    WM8904_HPL_PGA_ENA | WM8904_HPR_PGA_ENA);

		/* Power on the amplifier */
		snd_soc_update_bits(codec, reg,
				    WM8904_HPL_ENA | WM8904_HPR_ENA,
				    WM8904_HPL_ENA | WM8904_HPR_ENA);


		/* Enable the first stage */
		snd_soc_update_bits(codec, reg,
				    WM8904_HPL_ENA_DLY | WM8904_HPR_ENA_DLY,
				    WM8904_HPL_ENA_DLY | WM8904_HPR_ENA_DLY);

		/* Power up the DC servo */
		snd_soc_update_bits(codec, WM8904_DC_SERVO_0,
				    dcs_mask, dcs_mask);

		/* Either calibrate the DC servo or restore cached state
		 * if we have that.
		 */
		if (wm8904->dcs_state[dcs_l] || wm8904->dcs_state[dcs_r]) {
			dev_dbg(codec->dev, "Restoring DC servo state\n");

			snd_soc_write(codec, dcs_l_reg,
				      wm8904->dcs_state[dcs_l]);
			snd_soc_write(codec, dcs_r_reg,
				      wm8904->dcs_state[dcs_r]);

			snd_soc_write(codec, WM8904_DC_SERVO_1, dcs_mask);

			timeout = 20;
		} else {
			dev_dbg(codec->dev, "Calibrating DC servo\n");

			snd_soc_write(codec, WM8904_DC_SERVO_1,
				dcs_mask << WM8904_DCS_TRIG_STARTUP_0_SHIFT);

			timeout = 500;
		}

		/* Wait for DC servo to complete */
		dcs_mask <<= WM8904_DCS_CAL_COMPLETE_SHIFT;
		do {
			val = snd_soc_read(codec, WM8904_DC_SERVO_READBACK_0);
			if ((val & dcs_mask) == dcs_mask)
				break;

			msleep(1);
		} while (--timeout);

		if ((val & dcs_mask) != dcs_mask)
			dev_warn(codec->dev, "DC servo timed out\n");
		else
			dev_dbg(codec->dev, "DC servo ready\n");

		/* Enable the output stage */
		snd_soc_update_bits(codec, reg,
				    WM8904_HPL_ENA_OUTP | WM8904_HPR_ENA_OUTP,
				    WM8904_HPL_ENA_OUTP | WM8904_HPR_ENA_OUTP);
		break;

	case SND_SOC_DAPM_POST_PMU:
		/* Unshort the output itself */
		snd_soc_update_bits(codec, reg,
				    WM8904_HPL_RMV_SHORT |
				    WM8904_HPR_RMV_SHORT,
				    WM8904_HPL_RMV_SHORT |
				    WM8904_HPR_RMV_SHORT);

		break;

	case SND_SOC_DAPM_PRE_PMD:
		/* Short the output */
		snd_soc_update_bits(codec, reg,
				    WM8904_HPL_RMV_SHORT |
				    WM8904_HPR_RMV_SHORT, 0);
		break;

	case SND_SOC_DAPM_POST_PMD:
		/* Cache the DC servo configuration; this will be
		 * invalidated if we change the configuration. */
		wm8904->dcs_state[dcs_l] = snd_soc_read(codec, dcs_l_reg);
		wm8904->dcs_state[dcs_r] = snd_soc_read(codec, dcs_r_reg);

		snd_soc_update_bits(codec, WM8904_DC_SERVO_0,
				    dcs_mask, 0);

		/* Disable the amplifier input and output stages */
		snd_soc_update_bits(codec, reg,
				    WM8904_HPL_ENA | WM8904_HPR_ENA |
				    WM8904_HPL_ENA_DLY | WM8904_HPR_ENA_DLY |
				    WM8904_HPL_ENA_OUTP | WM8904_HPR_ENA_OUTP,
				    0);

		/* PGAs too */
		snd_soc_update_bits(codec, pwr_reg,
				    WM8904_HPL_PGA_ENA | WM8904_HPR_PGA_ENA,
				    0);
		break;
	}

	return 0;
}

static const char *lin_text[] = {
	"IN1L", "IN2L", "IN3L"
};

static SOC_ENUM_SINGLE_DECL(lin_enum, WM8904_ANALOGUE_LEFT_INPUT_1, 2,
			    lin_text);

static const struct snd_kcontrol_new lin_mux =
	SOC_DAPM_ENUM("Left Capture Mux", lin_enum);

static SOC_ENUM_SINGLE_DECL(lin_inv_enum, WM8904_ANALOGUE_LEFT_INPUT_1, 4,
			    lin_text);

static const struct snd_kcontrol_new lin_inv_mux =
	SOC_DAPM_ENUM("Left Capture Inveting Mux", lin_inv_enum);

static const char *rin_text[] = {
	"IN1R", "IN2R", "IN3R"
};

static SOC_ENUM_SINGLE_DECL(rin_enum, WM8904_ANALOGUE_RIGHT_INPUT_1, 2,
			    rin_text);

static const struct snd_kcontrol_new rin_mux =
	SOC_DAPM_ENUM("Right Capture Mux", rin_enum);

static SOC_ENUM_SINGLE_DECL(rin_inv_enum, WM8904_ANALOGUE_RIGHT_INPUT_1, 4,
			    rin_text);

static const struct snd_kcontrol_new rin_inv_mux =
	SOC_DAPM_ENUM("Right Capture Inveting Mux", rin_inv_enum);

static const char *aif_text[] = {
	"Left", "Right"
};

static SOC_ENUM_SINGLE_DECL(aifoutl_enum, WM8904_AUDIO_INTERFACE_0, 7,
			    aif_text);

static const struct snd_kcontrol_new aifoutl_mux =
	SOC_DAPM_ENUM("AIFOUTL Mux", aifoutl_enum);

static SOC_ENUM_SINGLE_DECL(aifoutr_enum, WM8904_AUDIO_INTERFACE_0, 6,
			    aif_text);

static const struct snd_kcontrol_new aifoutr_mux =
	SOC_DAPM_ENUM("AIFOUTR Mux", aifoutr_enum);

static SOC_ENUM_SINGLE_DECL(aifinl_enum, WM8904_AUDIO_INTERFACE_0, 5,
			    aif_text);

static const struct snd_kcontrol_new aifinl_mux =
	SOC_DAPM_ENUM("AIFINL Mux", aifinl_enum);

static SOC_ENUM_SINGLE_DECL(aifinr_enum, WM8904_AUDIO_INTERFACE_0, 4,
			    aif_text);

static const struct snd_kcontrol_new aifinr_mux =
	SOC_DAPM_ENUM("AIFINR Mux", aifinr_enum);

static const struct snd_soc_dapm_widget wm8904_core_dapm_widgets[] = {
SND_SOC_DAPM_SUPPLY("SYSCLK", WM8904_CLOCK_RATES_2, 2, 0, sysclk_event,
		    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_SUPPLY("CLK_DSP", WM8904_CLOCK_RATES_2, 1, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("TOCLK", WM8904_CLOCK_RATES_2, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_widget wm8904_adc_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("IN1L"),
SND_SOC_DAPM_INPUT("IN1R"),
SND_SOC_DAPM_INPUT("IN2L"),
SND_SOC_DAPM_INPUT("IN2R"),
SND_SOC_DAPM_INPUT("IN3L"),
SND_SOC_DAPM_INPUT("IN3R"),

SND_SOC_DAPM_SUPPLY("MICBIAS", WM8904_MIC_BIAS_CONTROL_0, 0, 0, NULL, 0),

SND_SOC_DAPM_MUX("Left Capture Mux", SND_SOC_NOPM, 0, 0, &lin_mux),
SND_SOC_DAPM_MUX("Left Capture Inverting Mux", SND_SOC_NOPM, 0, 0,
		 &lin_inv_mux),
SND_SOC_DAPM_MUX("Right Capture Mux", SND_SOC_NOPM, 0, 0, &rin_mux),
SND_SOC_DAPM_MUX("Right Capture Inverting Mux", SND_SOC_NOPM, 0, 0,
		 &rin_inv_mux),

SND_SOC_DAPM_PGA("Left Capture PGA", WM8904_POWER_MANAGEMENT_0, 1, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("Right Capture PGA", WM8904_POWER_MANAGEMENT_0, 0, 0,
		 NULL, 0),

SND_SOC_DAPM_ADC("ADCL", NULL, WM8904_POWER_MANAGEMENT_6, 1, 0),
SND_SOC_DAPM_ADC("ADCR", NULL, WM8904_POWER_MANAGEMENT_6, 0, 0),

SND_SOC_DAPM_MUX("AIFOUTL Mux", SND_SOC_NOPM, 0, 0, &aifoutl_mux),
SND_SOC_DAPM_MUX("AIFOUTR Mux", SND_SOC_NOPM, 0, 0, &aifoutr_mux),

SND_SOC_DAPM_AIF_OUT("AIFOUTL", "Capture", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_OUT("AIFOUTR", "Capture", 1, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_widget wm8904_dac_dapm_widgets[] = {
SND_SOC_DAPM_AIF_IN("AIFINL", "Playback", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_IN("AIFINR", "Playback", 1, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_MUX("DACL Mux", SND_SOC_NOPM, 0, 0, &aifinl_mux),
SND_SOC_DAPM_MUX("DACR Mux", SND_SOC_NOPM, 0, 0, &aifinr_mux),

SND_SOC_DAPM_DAC("DACL", NULL, WM8904_POWER_MANAGEMENT_6, 3, 0),
SND_SOC_DAPM_DAC("DACR", NULL, WM8904_POWER_MANAGEMENT_6, 2, 0),

SND_SOC_DAPM_SUPPLY("Charge pump", WM8904_CHARGE_PUMP_0, 0, 0, cp_event,
		    SND_SOC_DAPM_POST_PMU),

SND_SOC_DAPM_PGA("HPL PGA", SND_SOC_NOPM, 1, 0, NULL, 0),
SND_SOC_DAPM_PGA("HPR PGA", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_PGA("LINEL PGA", SND_SOC_NOPM, 1, 0, NULL, 0),
SND_SOC_DAPM_PGA("LINER PGA", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_PGA_E("Headphone Output", SND_SOC_NOPM, WM8904_ANALOGUE_HP_0,
		   0, NULL, 0, out_pga_event,
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_PGA_E("Line Output", SND_SOC_NOPM, WM8904_ANALOGUE_LINEOUT_0,
		   0, NULL, 0, out_pga_event,
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

SND_SOC_DAPM_OUTPUT("HPOUTL"),
SND_SOC_DAPM_OUTPUT("HPOUTR"),
SND_SOC_DAPM_OUTPUT("LINEOUTL"),
SND_SOC_DAPM_OUTPUT("LINEOUTR"),
};

static const char *out_mux_text[] = {
	"DAC", "Bypass"
};

static SOC_ENUM_SINGLE_DECL(hpl_enum, WM8904_ANALOGUE_OUT12_ZC, 3,
			    out_mux_text);

static const struct snd_kcontrol_new hpl_mux =
	SOC_DAPM_ENUM("HPL Mux", hpl_enum);

static SOC_ENUM_SINGLE_DECL(hpr_enum, WM8904_ANALOGUE_OUT12_ZC, 2,
			    out_mux_text);

static const struct snd_kcontrol_new hpr_mux =
	SOC_DAPM_ENUM("HPR Mux", hpr_enum);

static SOC_ENUM_SINGLE_DECL(linel_enum, WM8904_ANALOGUE_OUT12_ZC, 1,
			    out_mux_text);

static const struct snd_kcontrol_new linel_mux =
	SOC_DAPM_ENUM("LINEL Mux", linel_enum);

static SOC_ENUM_SINGLE_DECL(liner_enum, WM8904_ANALOGUE_OUT12_ZC, 0,
			    out_mux_text);

static const struct snd_kcontrol_new liner_mux =
	SOC_DAPM_ENUM("LINER Mux", liner_enum);

static const char *sidetone_text[] = {
	"None", "Left", "Right"
};

static SOC_ENUM_SINGLE_DECL(dacl_sidetone_enum, WM8904_DAC_DIGITAL_0, 2,
			    sidetone_text);

static const struct snd_kcontrol_new dacl_sidetone_mux =
	SOC_DAPM_ENUM("Left Sidetone Mux", dacl_sidetone_enum);

static SOC_ENUM_SINGLE_DECL(dacr_sidetone_enum, WM8904_DAC_DIGITAL_0, 0,
			    sidetone_text);

static const struct snd_kcontrol_new dacr_sidetone_mux =
	SOC_DAPM_ENUM("Right Sidetone Mux", dacr_sidetone_enum);

static const struct snd_soc_dapm_widget wm8904_dapm_widgets[] = {
SND_SOC_DAPM_SUPPLY("Class G", WM8904_CLASS_W_0, 0, 1, NULL, 0),
SND_SOC_DAPM_PGA("Left Bypass", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Bypass", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_MUX("Left Sidetone", SND_SOC_NOPM, 0, 0, &dacl_sidetone_mux),
SND_SOC_DAPM_MUX("Right Sidetone", SND_SOC_NOPM, 0, 0, &dacr_sidetone_mux),

SND_SOC_DAPM_MUX("HPL Mux", SND_SOC_NOPM, 0, 0, &hpl_mux),
SND_SOC_DAPM_MUX("HPR Mux", SND_SOC_NOPM, 0, 0, &hpr_mux),
SND_SOC_DAPM_MUX("LINEL Mux", SND_SOC_NOPM, 0, 0, &linel_mux),
SND_SOC_DAPM_MUX("LINER Mux", SND_SOC_NOPM, 0, 0, &liner_mux),
};

static const struct snd_soc_dapm_route core_intercon[] = {
	{ "CLK_DSP", NULL, "SYSCLK" },
	{ "TOCLK", NULL, "SYSCLK" },
};

static const struct snd_soc_dapm_route adc_intercon[] = {
	{ "Left Capture Mux", "IN1L", "IN1L" },
	{ "Left Capture Mux", "IN2L", "IN2L" },
	{ "Left Capture Mux", "IN3L", "IN3L" },

	{ "Left Capture Inverting Mux", "IN1L", "IN1L" },
	{ "Left Capture Inverting Mux", "IN2L", "IN2L" },
	{ "Left Capture Inverting Mux", "IN3L", "IN3L" },

	{ "Right Capture Mux", "IN1R", "IN1R" },
	{ "Right Capture Mux", "IN2R", "IN2R" },
	{ "Right Capture Mux", "IN3R", "IN3R" },

	{ "Right Capture Inverting Mux", "IN1R", "IN1R" },
	{ "Right Capture Inverting Mux", "IN2R", "IN2R" },
	{ "Right Capture Inverting Mux", "IN3R", "IN3R" },

	{ "Left Capture PGA", NULL, "Left Capture Mux" },
	{ "Left Capture PGA", NULL, "Left Capture Inverting Mux" },

	{ "Right Capture PGA", NULL, "Right Capture Mux" },
	{ "Right Capture PGA", NULL, "Right Capture Inverting Mux" },

	{ "AIFOUTL Mux", "Left", "ADCL" },
	{ "AIFOUTL Mux", "Right", "ADCR" },
	{ "AIFOUTR Mux", "Left", "ADCL" },
	{ "AIFOUTR Mux", "Right", "ADCR" },

	{ "AIFOUTL", NULL, "AIFOUTL Mux" },
	{ "AIFOUTR", NULL, "AIFOUTR Mux" },

	{ "ADCL", NULL, "CLK_DSP" },
	{ "ADCL", NULL, "Left Capture PGA" },

	{ "ADCR", NULL, "CLK_DSP" },
	{ "ADCR", NULL, "Right Capture PGA" },
};

static const struct snd_soc_dapm_route dac_intercon[] = {
	{ "DACL Mux", "Left", "AIFINL" },
	{ "DACL Mux", "Right", "AIFINR" },

	{ "DACR Mux", "Left", "AIFINL" },
	{ "DACR Mux", "Right", "AIFINR" },

	{ "DACL", NULL, "DACL Mux" },
	{ "DACL", NULL, "CLK_DSP" },

	{ "DACR", NULL, "DACR Mux" },
	{ "DACR", NULL, "CLK_DSP" },

	{ "Charge pump", NULL, "SYSCLK" },

	{ "Headphone Output", NULL, "HPL PGA" },
	{ "Headphone Output", NULL, "HPR PGA" },
	{ "Headphone Output", NULL, "Charge pump" },
	{ "Headphone Output", NULL, "TOCLK" },

	{ "Line Output", NULL, "LINEL PGA" },
	{ "Line Output", NULL, "LINER PGA" },
	{ "Line Output", NULL, "Charge pump" },
	{ "Line Output", NULL, "TOCLK" },

	{ "HPOUTL", NULL, "Headphone Output" },
	{ "HPOUTR", NULL, "Headphone Output" },

	{ "LINEOUTL", NULL, "Line Output" },
	{ "LINEOUTR", NULL, "Line Output" },
};

static const struct snd_soc_dapm_route wm8904_intercon[] = {
	{ "Left Sidetone", "Left", "ADCL" },
	{ "Left Sidetone", "Right", "ADCR" },
	{ "DACL", NULL, "Left Sidetone" },
	
	{ "Right Sidetone", "Left", "ADCL" },
	{ "Right Sidetone", "Right", "ADCR" },
	{ "DACR", NULL, "Right Sidetone" },

	{ "Left Bypass", NULL, "Class G" },
	{ "Left Bypass", NULL, "Left Capture PGA" },

	{ "Right Bypass", NULL, "Class G" },
	{ "Right Bypass", NULL, "Right Capture PGA" },

	{ "HPL Mux", "DAC", "DACL" },
	{ "HPL Mux", "Bypass", "Left Bypass" },

	{ "HPR Mux", "DAC", "DACR" },
	{ "HPR Mux", "Bypass", "Right Bypass" },

	{ "LINEL Mux", "DAC", "DACL" },
	{ "LINEL Mux", "Bypass", "Left Bypass" },

	{ "LINER Mux", "DAC", "DACR" },
	{ "LINER Mux", "Bypass", "Right Bypass" },

	{ "HPL PGA", NULL, "HPL Mux" },
	{ "HPR PGA", NULL, "HPR Mux" },

	{ "LINEL PGA", NULL, "LINEL Mux" },
	{ "LINER PGA", NULL, "LINER Mux" },
};

static const struct snd_soc_dapm_route wm8912_intercon[] = {
	{ "HPL PGA", NULL, "DACL" },
	{ "HPR PGA", NULL, "DACR" },

	{ "LINEL PGA", NULL, "DACL" },
	{ "LINER PGA", NULL, "DACR" },
};

static int wm8904_add_widgets(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);

	snd_soc_dapm_new_controls(dapm, wm8904_core_dapm_widgets,
				  ARRAY_SIZE(wm8904_core_dapm_widgets));
	snd_soc_dapm_add_routes(dapm, core_intercon,
				ARRAY_SIZE(core_intercon));

	switch (wm8904->devtype) {
	case WM8904:
		snd_soc_add_codec_controls(codec, wm8904_adc_snd_controls,
				     ARRAY_SIZE(wm8904_adc_snd_controls));
		snd_soc_add_codec_controls(codec, wm8904_dac_snd_controls,
				     ARRAY_SIZE(wm8904_dac_snd_controls));
		snd_soc_add_codec_controls(codec, wm8904_snd_controls,
				     ARRAY_SIZE(wm8904_snd_controls));

		snd_soc_dapm_new_controls(dapm, wm8904_adc_dapm_widgets,
					  ARRAY_SIZE(wm8904_adc_dapm_widgets));
		snd_soc_dapm_new_controls(dapm, wm8904_dac_dapm_widgets,
					  ARRAY_SIZE(wm8904_dac_dapm_widgets));
		snd_soc_dapm_new_controls(dapm, wm8904_dapm_widgets,
					  ARRAY_SIZE(wm8904_dapm_widgets));

		snd_soc_dapm_add_routes(dapm, adc_intercon,
					ARRAY_SIZE(adc_intercon));
		snd_soc_dapm_add_routes(dapm, dac_intercon,
					ARRAY_SIZE(dac_intercon));
		snd_soc_dapm_add_routes(dapm, wm8904_intercon,
					ARRAY_SIZE(wm8904_intercon));
		break;

	case WM8912:
		snd_soc_add_codec_controls(codec, wm8904_dac_snd_controls,
				     ARRAY_SIZE(wm8904_dac_snd_controls));

		snd_soc_dapm_new_controls(dapm, wm8904_dac_dapm_widgets,
					  ARRAY_SIZE(wm8904_dac_dapm_widgets));

		snd_soc_dapm_add_routes(dapm, dac_intercon,
					ARRAY_SIZE(dac_intercon));
		snd_soc_dapm_add_routes(dapm, wm8912_intercon,
					ARRAY_SIZE(wm8912_intercon));
		break;
	}

	return 0;
}

static struct {
	int ratio;
	unsigned int clk_sys_rate;
} clk_sys_rates[] = {
	{   64,  0 },
	{  128,  1 },
	{  192,  2 },
	{  256,  3 },
	{  384,  4 },
	{  512,  5 },
	{  786,  6 },
	{ 1024,  7 },
	{ 1408,  8 },
	{ 1536,  9 },
};

static struct {
	int rate;
	int sample_rate;
} sample_rates[] = {
	{ 8000,  0  },
	{ 11025, 1  },
	{ 12000, 1  },
	{ 16000, 2  },
	{ 22050, 3  },
	{ 24000, 3  },
	{ 32000, 4  },
	{ 44100, 5  },
	{ 48000, 5  },
};

static struct {
	int div; /* *10 due to .5s */
	int bclk_div;
} bclk_divs[] = {
	{ 10,  0  },
	{ 15,  1  },
	{ 20,  2  },
	{ 30,  3  },
	{ 40,  4  },
	{ 50,  5  },
	{ 55,  6  },
	{ 60,  7  },
	{ 80,  8  },
	{ 100, 9  },
	{ 110, 10 },
	{ 120, 11 },
	{ 160, 12 },
	{ 200, 13 },
	{ 220, 14 },
	{ 240, 16 },
	{ 200, 17 },
	{ 320, 18 },
	{ 440, 19 },
	{ 480, 20 },
};


static int wm8904_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	int ret, i, best, best_val, cur_val;
	unsigned int aif1 = 0;
	unsigned int aif2 = 0;
	unsigned int aif3 = 0;
	unsigned int clock1 = 0;
	unsigned int dac_digital1 = 0;

	/* What BCLK do we need? */
	wm8904->fs = params_rate(params);
	if (wm8904->tdm_slots) {
		dev_dbg(codec->dev, "Configuring for %d %d bit TDM slots\n",
			wm8904->tdm_slots, wm8904->tdm_width);
		wm8904->bclk = snd_soc_calc_bclk(wm8904->fs,
						 wm8904->tdm_width, 2,
						 wm8904->tdm_slots);
	} else {
		wm8904->bclk = snd_soc_params_to_bclk(params);
	}

	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		aif1 |= 0x40;
		break;
	case 24:
		aif1 |= 0x80;
		break;
	case 32:
		aif1 |= 0xc0;
		break;
	default:
		return -EINVAL;
	}


	dev_dbg(codec->dev, "Target BCLK is %dHz\n", wm8904->bclk);

	ret = wm8904_configure_clocking(codec);
	if (ret != 0)
		return ret;

	/* Select nearest CLK_SYS_RATE */
	best = 0;
	best_val = abs((wm8904->sysclk_rate / clk_sys_rates[0].ratio)
		       - wm8904->fs);
	for (i = 1; i < ARRAY_SIZE(clk_sys_rates); i++) {
		cur_val = abs((wm8904->sysclk_rate /
			       clk_sys_rates[i].ratio) - wm8904->fs);
		if (cur_val < best_val) {
			best = i;
			best_val = cur_val;
		}
	}
	dev_dbg(codec->dev, "Selected CLK_SYS_RATIO of %d\n",
		clk_sys_rates[best].ratio);
	clock1 |= (clk_sys_rates[best].clk_sys_rate
		   << WM8904_CLK_SYS_RATE_SHIFT);

	/* SAMPLE_RATE */
	best = 0;
	best_val = abs(wm8904->fs - sample_rates[0].rate);
	for (i = 1; i < ARRAY_SIZE(sample_rates); i++) {
		/* Closest match */
		cur_val = abs(wm8904->fs - sample_rates[i].rate);
		if (cur_val < best_val) {
			best = i;
			best_val = cur_val;
		}
	}
	dev_dbg(codec->dev, "Selected SAMPLE_RATE of %dHz\n",
		sample_rates[best].rate);
	clock1 |= (sample_rates[best].sample_rate
		   << WM8904_SAMPLE_RATE_SHIFT);

	/* Enable sloping stopband filter for low sample rates */
	if (wm8904->fs <= 24000)
		dac_digital1 |= WM8904_DAC_SB_FILT;

	/* BCLK_DIV */
	best = 0;
	best_val = INT_MAX;
	for (i = 0; i < ARRAY_SIZE(bclk_divs); i++) {
		cur_val = ((wm8904->sysclk_rate * 10) / bclk_divs[i].div)
			- wm8904->bclk;
		if (cur_val < 0) /* Table is sorted */
			break;
		if (cur_val < best_val) {
			best = i;
			best_val = cur_val;
		}
	}
	wm8904->bclk = (wm8904->sysclk_rate * 10) / bclk_divs[best].div;
	dev_dbg(codec->dev, "Selected BCLK_DIV of %d for %dHz BCLK\n",
		bclk_divs[best].div, wm8904->bclk);
	aif2 |= bclk_divs[best].bclk_div;

	/* LRCLK is a simple fraction of BCLK */
	dev_dbg(codec->dev, "LRCLK_RATE is %d\n", wm8904->bclk / wm8904->fs);
	aif3 |= wm8904->bclk / wm8904->fs;

	/* Apply the settings */
	snd_soc_update_bits(codec, WM8904_DAC_DIGITAL_1,
			    WM8904_DAC_SB_FILT, dac_digital1);
	snd_soc_update_bits(codec, WM8904_AUDIO_INTERFACE_1,
			    WM8904_AIF_WL_MASK, aif1);
	snd_soc_update_bits(codec, WM8904_AUDIO_INTERFACE_2,
			    WM8904_BCLK_DIV_MASK, aif2);
	snd_soc_update_bits(codec, WM8904_AUDIO_INTERFACE_3,
			    WM8904_LRCLK_RATE_MASK, aif3);
	snd_soc_update_bits(codec, WM8904_CLOCK_RATES_1,
			    WM8904_SAMPLE_RATE_MASK |
			    WM8904_CLK_SYS_RATE_MASK, clock1);

	/* Update filters for the new settings */
	wm8904_set_retune_mobile(codec);
	wm8904_set_deemph(codec);

	return 0;
}


static int wm8904_set_sysclk(struct snd_soc_dai *dai, int clk_id,
			     unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8904_priv *priv = snd_soc_codec_get_drvdata(codec);

	switch (clk_id) {
	case WM8904_CLK_MCLK:
		priv->sysclk_src = clk_id;
		priv->mclk_rate = freq;
		break;

	case WM8904_CLK_FLL:
		priv->sysclk_src = clk_id;
		break;

	default:
		return -EINVAL;
	}

	dev_dbg(dai->dev, "Clock source is %d at %uHz\n", clk_id, freq);

	wm8904_configure_clocking(codec);

	return 0;
}

static int wm8904_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int aif1 = 0;
	unsigned int aif3 = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		aif3 |= WM8904_LRCLK_DIR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		aif1 |= WM8904_BCLK_DIR;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		aif1 |= WM8904_BCLK_DIR;
		aif3 |= WM8904_LRCLK_DIR;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_B:
		aif1 |= 0x3 | WM8904_AIF_LRCLK_INV;
	case SND_SOC_DAIFMT_DSP_A:
		aif1 |= 0x3;
		break;
	case SND_SOC_DAIFMT_I2S:
		aif1 |= 0x2;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aif1 |= 0x1;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		/* frame inversion not valid for DSP modes */
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif1 |= WM8904_AIF_BCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;

	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			aif1 |= WM8904_AIF_BCLK_INV | WM8904_AIF_LRCLK_INV;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif1 |= WM8904_AIF_BCLK_INV;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			aif1 |= WM8904_AIF_LRCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM8904_AUDIO_INTERFACE_1,
			    WM8904_AIF_BCLK_INV | WM8904_AIF_LRCLK_INV |
			    WM8904_AIF_FMT_MASK | WM8904_BCLK_DIR, aif1);
	snd_soc_update_bits(codec, WM8904_AUDIO_INTERFACE_3,
			    WM8904_LRCLK_DIR, aif3);

	return 0;
}


static int wm8904_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			       unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	int aif1 = 0;

	/* Don't need to validate anything if we're turning off TDM */
	if (slots == 0)
		goto out;

	/* Note that we allow configurations we can't handle ourselves - 
	 * for example, we can generate clocks for slots 2 and up even if
	 * we can't use those slots ourselves.
	 */
	aif1 |= WM8904_AIFADC_TDM | WM8904_AIFDAC_TDM;

	switch (rx_mask) {
	case 3:
		break;
	case 0xc:
		aif1 |= WM8904_AIFADC_TDM_CHAN;
		break;
	default:
		return -EINVAL;
	}


	switch (tx_mask) {
	case 3:
		break;
	case 0xc:
		aif1 |= WM8904_AIFDAC_TDM_CHAN;
		break;
	default:
		return -EINVAL;
	}

out:
	wm8904->tdm_width = slot_width;
	wm8904->tdm_slots = slots / 2;

	snd_soc_update_bits(codec, WM8904_AUDIO_INTERFACE_1,
			    WM8904_AIFADC_TDM | WM8904_AIFADC_TDM_CHAN |
			    WM8904_AIFDAC_TDM | WM8904_AIFDAC_TDM_CHAN, aif1);

	return 0;
}

struct _fll_div {
	u16 fll_fratio;
	u16 fll_outdiv;
	u16 fll_clk_ref_div;
	u16 n;
	u16 k;
};

/* The size in bits of the FLL divide multiplied by 10
 * to allow rounding later */
#define FIXED_FLL_SIZE ((1 << 16) * 10)

static struct {
	unsigned int min;
	unsigned int max;
	u16 fll_fratio;
	int ratio;
} fll_fratios[] = {
	{       0,    64000, 4, 16 },
	{   64000,   128000, 3,  8 },
	{  128000,   256000, 2,  4 },
	{  256000,  1000000, 1,  2 },
	{ 1000000, 13500000, 0,  1 },
};

static int fll_factors(struct _fll_div *fll_div, unsigned int Fref,
		       unsigned int Fout)
{
	u64 Kpart;
	unsigned int K, Ndiv, Nmod, target;
	unsigned int div;
	int i;

	/* Fref must be <=13.5MHz */
	div = 1;
	fll_div->fll_clk_ref_div = 0;
	while ((Fref / div) > 13500000) {
		div *= 2;
		fll_div->fll_clk_ref_div++;

		if (div > 8) {
			pr_err("Can't scale %dMHz input down to <=13.5MHz\n",
			       Fref);
			return -EINVAL;
		}
	}

	pr_debug("Fref=%u Fout=%u\n", Fref, Fout);

	/* Apply the division for our remaining calculations */
	Fref /= div;

	/* Fvco should be 90-100MHz; don't check the upper bound */
	div = 4;
	while (Fout * div < 90000000) {
		div++;
		if (div > 64) {
			pr_err("Unable to find FLL_OUTDIV for Fout=%uHz\n",
			       Fout);
			return -EINVAL;
		}
	}
	target = Fout * div;
	fll_div->fll_outdiv = div - 1;

	pr_debug("Fvco=%dHz\n", target);

	/* Find an appropriate FLL_FRATIO and factor it out of the target */
	for (i = 0; i < ARRAY_SIZE(fll_fratios); i++) {
		if (fll_fratios[i].min <= Fref && Fref <= fll_fratios[i].max) {
			fll_div->fll_fratio = fll_fratios[i].fll_fratio;
			target /= fll_fratios[i].ratio;
			break;
		}
	}
	if (i == ARRAY_SIZE(fll_fratios)) {
		pr_err("Unable to find FLL_FRATIO for Fref=%uHz\n", Fref);
		return -EINVAL;
	}

	/* Now, calculate N.K */
	Ndiv = target / Fref;

	fll_div->n = Ndiv;
	Nmod = target % Fref;
	pr_debug("Nmod=%d\n", Nmod);

	/* Calculate fractional part - scale up so we can round. */
	Kpart = FIXED_FLL_SIZE * (long long)Nmod;

	do_div(Kpart, Fref);

	K = Kpart & 0xFFFFFFFF;

	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	fll_div->k = K / 10;

	pr_debug("N=%x K=%x FLL_FRATIO=%x FLL_OUTDIV=%x FLL_CLK_REF_DIV=%x\n",
		 fll_div->n, fll_div->k,
		 fll_div->fll_fratio, fll_div->fll_outdiv,
		 fll_div->fll_clk_ref_div);

	return 0;
}

static int wm8904_set_fll(struct snd_soc_dai *dai, int fll_id, int source,
			  unsigned int Fref, unsigned int Fout)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct _fll_div fll_div;
	int ret, val;
	int clock2, fll1;

	/* Any change? */
	if (source == wm8904->fll_src && Fref == wm8904->fll_fref &&
	    Fout == wm8904->fll_fout)
		return 0;

	clock2 = snd_soc_read(codec, WM8904_CLOCK_RATES_2);

	if (Fout == 0) {
		dev_dbg(codec->dev, "FLL disabled\n");

		wm8904->fll_fref = 0;
		wm8904->fll_fout = 0;

		/* Gate SYSCLK to avoid glitches */
		snd_soc_update_bits(codec, WM8904_CLOCK_RATES_2,
				    WM8904_CLK_SYS_ENA, 0);

		snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
				    WM8904_FLL_OSC_ENA | WM8904_FLL_ENA, 0);

		goto out;
	}

	/* Validate the FLL ID */
	switch (source) {
	case WM8904_FLL_MCLK:
	case WM8904_FLL_LRCLK:
	case WM8904_FLL_BCLK:
		ret = fll_factors(&fll_div, Fref, Fout);
		if (ret != 0)
			return ret;
		break;

	case WM8904_FLL_FREE_RUNNING:
		dev_dbg(codec->dev, "Using free running FLL\n");
		/* Force 12MHz and output/4 for now */
		Fout = 12000000;
		Fref = 12000000;

		memset(&fll_div, 0, sizeof(fll_div));
		fll_div.fll_outdiv = 3;
		break;

	default:
		dev_err(codec->dev, "Unknown FLL ID %d\n", fll_id);
		return -EINVAL;
	}

	/* Save current state then disable the FLL and SYSCLK to avoid
	 * misclocking */
	fll1 = snd_soc_read(codec, WM8904_FLL_CONTROL_1);
	snd_soc_update_bits(codec, WM8904_CLOCK_RATES_2,
			    WM8904_CLK_SYS_ENA, 0);
	snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
			    WM8904_FLL_OSC_ENA | WM8904_FLL_ENA, 0);

	/* Unlock forced oscilator control to switch it on/off */
	snd_soc_update_bits(codec, WM8904_CONTROL_INTERFACE_TEST_1,
			    WM8904_USER_KEY, WM8904_USER_KEY);

	if (fll_id == WM8904_FLL_FREE_RUNNING) {
		val = WM8904_FLL_FRC_NCO;
	} else {
		val = 0;
	}

	snd_soc_update_bits(codec, WM8904_FLL_NCO_TEST_1, WM8904_FLL_FRC_NCO,
			    val);
	snd_soc_update_bits(codec, WM8904_CONTROL_INTERFACE_TEST_1,
			    WM8904_USER_KEY, 0);

	switch (fll_id) {
	case WM8904_FLL_MCLK:
		snd_soc_update_bits(codec, WM8904_FLL_CONTROL_5,
				    WM8904_FLL_CLK_REF_SRC_MASK, 0);
		break;

	case WM8904_FLL_LRCLK:
		snd_soc_update_bits(codec, WM8904_FLL_CONTROL_5,
				    WM8904_FLL_CLK_REF_SRC_MASK, 1);
		break;

	case WM8904_FLL_BCLK:
		snd_soc_update_bits(codec, WM8904_FLL_CONTROL_5,
				    WM8904_FLL_CLK_REF_SRC_MASK, 2);
		break;
	}

	if (fll_div.k)
		val = WM8904_FLL_FRACN_ENA;
	else
		val = 0;
	snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
			    WM8904_FLL_FRACN_ENA, val);

	snd_soc_update_bits(codec, WM8904_FLL_CONTROL_2,
			    WM8904_FLL_OUTDIV_MASK | WM8904_FLL_FRATIO_MASK,
			    (fll_div.fll_outdiv << WM8904_FLL_OUTDIV_SHIFT) |
			    (fll_div.fll_fratio << WM8904_FLL_FRATIO_SHIFT));

	snd_soc_write(codec, WM8904_FLL_CONTROL_3, fll_div.k);

	snd_soc_update_bits(codec, WM8904_FLL_CONTROL_4, WM8904_FLL_N_MASK,
			    fll_div.n << WM8904_FLL_N_SHIFT);

	snd_soc_update_bits(codec, WM8904_FLL_CONTROL_5,
			    WM8904_FLL_CLK_REF_DIV_MASK,
			    fll_div.fll_clk_ref_div 
			    << WM8904_FLL_CLK_REF_DIV_SHIFT);

	dev_dbg(codec->dev, "FLL configured for %dHz->%dHz\n", Fref, Fout);

	wm8904->fll_fref = Fref;
	wm8904->fll_fout = Fout;
	wm8904->fll_src = source;

	/* Enable the FLL if it was previously active */
	snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
			    WM8904_FLL_OSC_ENA, fll1);
	snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
			    WM8904_FLL_ENA, fll1);

out:
	/* Reenable SYSCLK if it was previously active */
	snd_soc_update_bits(codec, WM8904_CLOCK_RATES_2,
			    WM8904_CLK_SYS_ENA, clock2);

	return 0;
}

static int wm8904_digital_mute(struct snd_soc_dai *codec_dai, int mute)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int val;

	if (mute)
		val = WM8904_DAC_MUTE;
	else
		val = 0;

	snd_soc_update_bits(codec, WM8904_DAC_DIGITAL_1, WM8904_DAC_MUTE, val);

	return 0;
}

static int wm8904_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		clk_prepare_enable(wm8904->mclk);
		break;

	case SND_SOC_BIAS_PREPARE:
		/* VMID resistance 2*50k */
		snd_soc_update_bits(codec, WM8904_VMID_CONTROL_0,
				    WM8904_VMID_RES_MASK,
				    0x1 << WM8904_VMID_RES_SHIFT);

		/* Normal bias current */
		snd_soc_update_bits(codec, WM8904_BIAS_CONTROL_0,
				    WM8904_ISEL_MASK, 2 << WM8904_ISEL_SHIFT);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(wm8904->supplies),
						    wm8904->supplies);
			if (ret != 0) {
				dev_err(codec->dev,
					"Failed to enable supplies: %d\n",
					ret);
				return ret;
			}

			regcache_cache_only(wm8904->regmap, false);
			regcache_sync(wm8904->regmap);

			/* Enable bias */
			snd_soc_update_bits(codec, WM8904_BIAS_CONTROL_0,
					    WM8904_BIAS_ENA, WM8904_BIAS_ENA);

			/* Enable VMID, VMID buffering, 2*5k resistance */
			snd_soc_update_bits(codec, WM8904_VMID_CONTROL_0,
					    WM8904_VMID_ENA |
					    WM8904_VMID_RES_MASK,
					    WM8904_VMID_ENA |
					    0x3 << WM8904_VMID_RES_SHIFT);

			/* Let VMID ramp */
			msleep(1);
		}

		/* Maintain VMID with 2*250k */
		snd_soc_update_bits(codec, WM8904_VMID_CONTROL_0,
				    WM8904_VMID_RES_MASK,
				    0x2 << WM8904_VMID_RES_SHIFT);

		/* Bias current *0.5 */
		snd_soc_update_bits(codec, WM8904_BIAS_CONTROL_0,
				    WM8904_ISEL_MASK, 0);
		break;

	case SND_SOC_BIAS_OFF:
		/* Turn off VMID */
		snd_soc_update_bits(codec, WM8904_VMID_CONTROL_0,
				    WM8904_VMID_RES_MASK | WM8904_VMID_ENA, 0);

		/* Stop bias generation */
		snd_soc_update_bits(codec, WM8904_BIAS_CONTROL_0,
				    WM8904_BIAS_ENA, 0);

		regcache_cache_only(wm8904->regmap, true);
		regcache_mark_dirty(wm8904->regmap);

		regulator_bulk_disable(ARRAY_SIZE(wm8904->supplies),
				       wm8904->supplies);
		clk_disable_unprepare(wm8904->mclk);
		break;
	}
	return 0;
}

#define WM8904_RATES SNDRV_PCM_RATE_8000_96000

#define WM8904_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops wm8904_dai_ops = {
	.set_sysclk = wm8904_set_sysclk,
	.set_fmt = wm8904_set_fmt,
	.set_tdm_slot = wm8904_set_tdm_slot,
	.set_pll = wm8904_set_fll,
	.hw_params = wm8904_hw_params,
	.digital_mute = wm8904_digital_mute,
};

static struct snd_soc_dai_driver wm8904_dai = {
	.name = "wm8904-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8904_RATES,
		.formats = WM8904_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8904_RATES,
		.formats = WM8904_FORMATS,
	},
	.ops = &wm8904_dai_ops,
	.symmetric_rates = 1,
};

static void wm8904_handle_retune_mobile_pdata(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct wm8904_pdata *pdata = wm8904->pdata;
	struct snd_kcontrol_new control =
		SOC_ENUM_EXT("EQ Mode",
			     wm8904->retune_mobile_enum,
			     wm8904_get_retune_mobile_enum,
			     wm8904_put_retune_mobile_enum);
	int ret, i, j;
	const char **t;

	/* We need an array of texts for the enum API but the number
	 * of texts is likely to be less than the number of
	 * configurations due to the sample rate dependency of the
	 * configurations. */
	wm8904->num_retune_mobile_texts = 0;
	wm8904->retune_mobile_texts = NULL;
	for (i = 0; i < pdata->num_retune_mobile_cfgs; i++) {
		for (j = 0; j < wm8904->num_retune_mobile_texts; j++) {
			if (strcmp(pdata->retune_mobile_cfgs[i].name,
				   wm8904->retune_mobile_texts[j]) == 0)
				break;
		}

		if (j != wm8904->num_retune_mobile_texts)
			continue;

		/* Expand the array... */
		t = krealloc(wm8904->retune_mobile_texts,
			     sizeof(char *) * 
			     (wm8904->num_retune_mobile_texts + 1),
			     GFP_KERNEL);
		if (t == NULL)
			continue;

		/* ...store the new entry... */
		t[wm8904->num_retune_mobile_texts] = 
			pdata->retune_mobile_cfgs[i].name;

		/* ...and remember the new version. */
		wm8904->num_retune_mobile_texts++;
		wm8904->retune_mobile_texts = t;
	}

	dev_dbg(codec->dev, "Allocated %d unique ReTune Mobile names\n",
		wm8904->num_retune_mobile_texts);

	wm8904->retune_mobile_enum.items = wm8904->num_retune_mobile_texts;
	wm8904->retune_mobile_enum.texts = wm8904->retune_mobile_texts;

	ret = snd_soc_add_codec_controls(codec, &control, 1);
	if (ret != 0)
		dev_err(codec->dev,
			"Failed to add ReTune Mobile control: %d\n", ret);
}

static void wm8904_handle_pdata(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct wm8904_pdata *pdata = wm8904->pdata;
	int ret, i;

	if (!pdata) {
		snd_soc_add_codec_controls(codec, wm8904_eq_controls,
				     ARRAY_SIZE(wm8904_eq_controls));
		return;
	}

	dev_dbg(codec->dev, "%d DRC configurations\n", pdata->num_drc_cfgs);

	if (pdata->num_drc_cfgs) {
		struct snd_kcontrol_new control =
			SOC_ENUM_EXT("DRC Mode", wm8904->drc_enum,
				     wm8904_get_drc_enum, wm8904_put_drc_enum);

		/* We need an array of texts for the enum API */
		wm8904->drc_texts = kmalloc(sizeof(char *)
					    * pdata->num_drc_cfgs, GFP_KERNEL);
		if (!wm8904->drc_texts)
			return;

		for (i = 0; i < pdata->num_drc_cfgs; i++)
			wm8904->drc_texts[i] = pdata->drc_cfgs[i].name;

		wm8904->drc_enum.items = pdata->num_drc_cfgs;
		wm8904->drc_enum.texts = wm8904->drc_texts;

		ret = snd_soc_add_codec_controls(codec, &control, 1);
		if (ret != 0)
			dev_err(codec->dev,
				"Failed to add DRC mode control: %d\n", ret);

		wm8904_set_drc(codec);
	}

	dev_dbg(codec->dev, "%d ReTune Mobile configurations\n",
		pdata->num_retune_mobile_cfgs);

	if (pdata->num_retune_mobile_cfgs)
		wm8904_handle_retune_mobile_pdata(codec);
	else
		snd_soc_add_codec_controls(codec, wm8904_eq_controls,
				     ARRAY_SIZE(wm8904_eq_controls));
}


static int wm8904_probe(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);

	switch (wm8904->devtype) {
	case WM8904:
		break;
	case WM8912:
		memset(&wm8904_dai.capture, 0, sizeof(wm8904_dai.capture));
		break;
	default:
		dev_err(codec->dev, "Unknown device type %d\n",
			wm8904->devtype);
		return -EINVAL;
	}

	wm8904_handle_pdata(codec);

	wm8904_add_widgets(codec);

	return 0;
}

static int wm8904_remove(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);

	kfree(wm8904->retune_mobile_texts);
	kfree(wm8904->drc_texts);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8904 = {
	.probe =	wm8904_probe,
	.remove =	wm8904_remove,
	.set_bias_level = wm8904_set_bias_level,
	.idle_bias_off = true,
};

static const struct regmap_config wm8904_regmap = {
	.reg_bits = 8,
	.val_bits = 16,

	.max_register = WM8904_MAX_REGISTER,
	.volatile_reg = wm8904_volatile_register,
	.readable_reg = wm8904_readable_register,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = wm8904_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8904_reg_defaults),
};

#ifdef CONFIG_OF
static enum wm8904_type wm8904_data = WM8904;
static enum wm8904_type wm8912_data = WM8912;

static const struct of_device_id wm8904_of_match[] = {
	{
		.compatible = "wlf,wm8904",
		.data = &wm8904_data,
	}, {
		.compatible = "wlf,wm8912",
		.data = &wm8912_data,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, wm8904_of_match);
#endif

static int wm8904_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8904_priv *wm8904;
	unsigned int val;
	int ret, i;

	wm8904 = devm_kzalloc(&i2c->dev, sizeof(struct wm8904_priv),
			      GFP_KERNEL);
	if (wm8904 == NULL)
		return -ENOMEM;

	wm8904->mclk = devm_clk_get(&i2c->dev, "mclk");
	if (IS_ERR(wm8904->mclk)) {
		ret = PTR_ERR(wm8904->mclk);
		dev_err(&i2c->dev, "Failed to get MCLK\n");
		return ret;
	}

	wm8904->regmap = devm_regmap_init_i2c(i2c, &wm8904_regmap);
	if (IS_ERR(wm8904->regmap)) {
		ret = PTR_ERR(wm8904->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	if (i2c->dev.of_node) {
		const struct of_device_id *match;

		match = of_match_node(wm8904_of_match, i2c->dev.of_node);
		if (match == NULL)
			return -EINVAL;
		wm8904->devtype = *((enum wm8904_type *)match->data);
	} else {
		wm8904->devtype = id->driver_data;
	}

	i2c_set_clientdata(i2c, wm8904);
	wm8904->pdata = i2c->dev.platform_data;

	for (i = 0; i < ARRAY_SIZE(wm8904->supplies); i++)
		wm8904->supplies[i].supply = wm8904_supply_names[i];

	ret = devm_regulator_bulk_get(&i2c->dev, ARRAY_SIZE(wm8904->supplies),
				      wm8904->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8904->supplies),
				    wm8904->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	ret = regmap_read(wm8904->regmap, WM8904_SW_RESET_AND_ID, &val);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read ID register: %d\n", ret);
		goto err_enable;
	}
	if (val != 0x8904) {
		dev_err(&i2c->dev, "Device is not a WM8904, ID is %x\n", val);
		ret = -EINVAL;
		goto err_enable;
	}

	ret = regmap_read(wm8904->regmap, WM8904_REVISION, &val);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read device revision: %d\n",
			ret);
		goto err_enable;
	}
	dev_info(&i2c->dev, "revision %c\n", val + 'A');

	ret = regmap_write(wm8904->regmap, WM8904_SW_RESET_AND_ID, 0);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to issue reset: %d\n", ret);
		goto err_enable;
	}

	/* Change some default settings - latch VU and enable ZC */
	regmap_update_bits(wm8904->regmap, WM8904_ADC_DIGITAL_VOLUME_LEFT,
			   WM8904_ADC_VU, WM8904_ADC_VU);
	regmap_update_bits(wm8904->regmap, WM8904_ADC_DIGITAL_VOLUME_RIGHT,
			   WM8904_ADC_VU, WM8904_ADC_VU);
	regmap_update_bits(wm8904->regmap, WM8904_DAC_DIGITAL_VOLUME_LEFT,
			   WM8904_DAC_VU, WM8904_DAC_VU);
	regmap_update_bits(wm8904->regmap, WM8904_DAC_DIGITAL_VOLUME_RIGHT,
			   WM8904_DAC_VU, WM8904_DAC_VU);
	regmap_update_bits(wm8904->regmap, WM8904_ANALOGUE_OUT1_LEFT,
			   WM8904_HPOUT_VU | WM8904_HPOUTLZC,
			   WM8904_HPOUT_VU | WM8904_HPOUTLZC);
	regmap_update_bits(wm8904->regmap, WM8904_ANALOGUE_OUT1_RIGHT,
			   WM8904_HPOUT_VU | WM8904_HPOUTRZC,
			   WM8904_HPOUT_VU | WM8904_HPOUTRZC);
	regmap_update_bits(wm8904->regmap, WM8904_ANALOGUE_OUT2_LEFT,
			   WM8904_LINEOUT_VU | WM8904_LINEOUTLZC,
			   WM8904_LINEOUT_VU | WM8904_LINEOUTLZC);
	regmap_update_bits(wm8904->regmap, WM8904_ANALOGUE_OUT2_RIGHT,
			   WM8904_LINEOUT_VU | WM8904_LINEOUTRZC,
			   WM8904_LINEOUT_VU | WM8904_LINEOUTRZC);
	regmap_update_bits(wm8904->regmap, WM8904_CLOCK_RATES_0,
			   WM8904_SR_MODE, 0);

	/* Apply configuration from the platform data. */
	if (wm8904->pdata) {
		for (i = 0; i < WM8904_GPIO_REGS; i++) {
			if (!wm8904->pdata->gpio_cfg[i])
				continue;

			regmap_update_bits(wm8904->regmap,
					   WM8904_GPIO_CONTROL_1 + i,
					   0xffff,
					   wm8904->pdata->gpio_cfg[i]);
		}

		/* Zero is the default value for these anyway */
		for (i = 0; i < WM8904_MIC_REGS; i++)
			regmap_update_bits(wm8904->regmap,
					   WM8904_MIC_BIAS_CONTROL_0 + i,
					   0xffff,
					   wm8904->pdata->mic_cfg[i]);
	}

	/* Set Class W by default - this will be managed by the Class
	 * G widget at runtime where bypass paths are available.
	 */
	regmap_update_bits(wm8904->regmap, WM8904_CLASS_W_0,
			    WM8904_CP_DYN_PWR, WM8904_CP_DYN_PWR);

	/* Use normal bias source */
	regmap_update_bits(wm8904->regmap, WM8904_BIAS_CONTROL_0,
			    WM8904_POBCTRL, 0);

	/* Can leave the device powered off until we need it */
	regcache_cache_only(wm8904->regmap, true);
	regulator_bulk_disable(ARRAY_SIZE(wm8904->supplies), wm8904->supplies);

	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_wm8904, &wm8904_dai, 1);
	if (ret != 0)
		return ret;

	return 0;

err_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8904->supplies), wm8904->supplies);
	return ret;
}

static int wm8904_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id wm8904_i2c_id[] = {
	{ "wm8904", WM8904 },
	{ "wm8912", WM8912 },
	{ "wm8918", WM8904 },   /* Actually a subset, updates to follow */
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8904_i2c_id);

static struct i2c_driver wm8904_i2c_driver = {
	.driver = {
		.name = "wm8904",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(wm8904_of_match),
	},
	.probe =    wm8904_i2c_probe,
	.remove =   wm8904_i2c_remove,
	.id_table = wm8904_i2c_id,
};

module_i2c_driver(wm8904_i2c_driver);

MODULE_DESCRIPTION("ASoC WM8904 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
