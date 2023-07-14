// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm8961.c  --  WM8961 ALSA SoC Audio driver
 *
 * Copyright 2009-10 Wolfson Microelectronics, plc
 *
 * Author: Mark Brown
 *
 * Currently unimplemented features:
 *  - ALC
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8961.h"

#define WM8961_MAX_REGISTER                     0xFC

static const struct reg_default wm8961_reg_defaults[] = {
	{  0, 0x009F },     /* R0   - Left Input volume */
	{  1, 0x009F },     /* R1   - Right Input volume */
	{  2, 0x0000 },     /* R2   - LOUT1 volume */
	{  3, 0x0000 },     /* R3   - ROUT1 volume */
	{  4, 0x0020 },     /* R4   - Clocking1 */
	{  5, 0x0008 },     /* R5   - ADC & DAC Control 1 */
	{  6, 0x0000 },     /* R6   - ADC & DAC Control 2 */
	{  7, 0x000A },     /* R7   - Audio Interface 0 */
	{  8, 0x01F4 },     /* R8   - Clocking2 */
	{  9, 0x0000 },     /* R9   - Audio Interface 1 */
	{ 10, 0x00FF },     /* R10  - Left DAC volume */
	{ 11, 0x00FF },     /* R11  - Right DAC volume */

	{ 14, 0x0040 },     /* R14  - Audio Interface 2 */

	{ 17, 0x007B },     /* R17  - ALC1 */
	{ 18, 0x0000 },     /* R18  - ALC2 */
	{ 19, 0x0032 },     /* R19  - ALC3 */
	{ 20, 0x0000 },     /* R20  - Noise Gate */
	{ 21, 0x00C0 },     /* R21  - Left ADC volume */
	{ 22, 0x00C0 },     /* R22  - Right ADC volume */
	{ 23, 0x0120 },     /* R23  - Additional control(1) */
	{ 24, 0x0000 },     /* R24  - Additional control(2) */
	{ 25, 0x0000 },     /* R25  - Pwr Mgmt (1) */
	{ 26, 0x0000 },     /* R26  - Pwr Mgmt (2) */
	{ 27, 0x0000 },     /* R27  - Additional Control (3) */
	{ 28, 0x0000 },     /* R28  - Anti-pop */

	{ 30, 0x005F },     /* R30  - Clocking 3 */

	{ 32, 0x0000 },     /* R32  - ADCL signal path */
	{ 33, 0x0000 },     /* R33  - ADCR signal path */

	{ 40, 0x0000 },     /* R40  - LOUT2 volume */
	{ 41, 0x0000 },     /* R41  - ROUT2 volume */

	{ 47, 0x0000 },     /* R47  - Pwr Mgmt (3) */
	{ 48, 0x0023 },     /* R48  - Additional Control (4) */
	{ 49, 0x0000 },     /* R49  - Class D Control 1 */

	{ 51, 0x0003 },     /* R51  - Class D Control 2 */

	{ 56, 0x0106 },     /* R56  - Clocking 4 */
	{ 57, 0x0000 },     /* R57  - DSP Sidetone 0 */
	{ 58, 0x0000 },     /* R58  - DSP Sidetone 1 */

	{ 60, 0x0000 },     /* R60  - DC Servo 0 */
	{ 61, 0x0000 },     /* R61  - DC Servo 1 */

	{ 63, 0x015E },     /* R63  - DC Servo 3 */

	{ 65, 0x0010 },     /* R65  - DC Servo 5 */

	{ 68, 0x0003 },     /* R68  - Analogue PGA Bias */
	{ 69, 0x0000 },     /* R69  - Analogue HP 0 */

	{ 71, 0x01FB },     /* R71  - Analogue HP 2 */
	{ 72, 0x0000 },     /* R72  - Charge Pump 1 */

	{ 82, 0x0000 },     /* R82  - Charge Pump B */

	{ 87, 0x0000 },     /* R87  - Write Sequencer 1 */
	{ 88, 0x0000 },     /* R88  - Write Sequencer 2 */
	{ 89, 0x0000 },     /* R89  - Write Sequencer 3 */
	{ 90, 0x0000 },     /* R90  - Write Sequencer 4 */
	{ 91, 0x0000 },     /* R91  - Write Sequencer 5 */
	{ 92, 0x0000 },     /* R92  - Write Sequencer 6 */
	{ 93, 0x0000 },     /* R93  - Write Sequencer 7 */

	{ 252, 0x0001 },     /* R252 - General test 1 */
};

struct wm8961_priv {
	struct regmap *regmap;
	int sysclk;
};

static bool wm8961_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8961_SOFTWARE_RESET:
	case WM8961_WRITE_SEQUENCER_7:
	case WM8961_DC_SERVO_1:
		return true;

	default:
		return false;
	}
}

static bool wm8961_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8961_LEFT_INPUT_VOLUME:
	case WM8961_RIGHT_INPUT_VOLUME:
	case WM8961_LOUT1_VOLUME:
	case WM8961_ROUT1_VOLUME:
	case WM8961_CLOCKING1:
	case WM8961_ADC_DAC_CONTROL_1:
	case WM8961_ADC_DAC_CONTROL_2:
	case WM8961_AUDIO_INTERFACE_0:
	case WM8961_CLOCKING2:
	case WM8961_AUDIO_INTERFACE_1:
	case WM8961_LEFT_DAC_VOLUME:
	case WM8961_RIGHT_DAC_VOLUME:
	case WM8961_AUDIO_INTERFACE_2:
	case WM8961_SOFTWARE_RESET:
	case WM8961_ALC1:
	case WM8961_ALC2:
	case WM8961_ALC3:
	case WM8961_NOISE_GATE:
	case WM8961_LEFT_ADC_VOLUME:
	case WM8961_RIGHT_ADC_VOLUME:
	case WM8961_ADDITIONAL_CONTROL_1:
	case WM8961_ADDITIONAL_CONTROL_2:
	case WM8961_PWR_MGMT_1:
	case WM8961_PWR_MGMT_2:
	case WM8961_ADDITIONAL_CONTROL_3:
	case WM8961_ANTI_POP:
	case WM8961_CLOCKING_3:
	case WM8961_ADCL_SIGNAL_PATH:
	case WM8961_ADCR_SIGNAL_PATH:
	case WM8961_LOUT2_VOLUME:
	case WM8961_ROUT2_VOLUME:
	case WM8961_PWR_MGMT_3:
	case WM8961_ADDITIONAL_CONTROL_4:
	case WM8961_CLASS_D_CONTROL_1:
	case WM8961_CLASS_D_CONTROL_2:
	case WM8961_CLOCKING_4:
	case WM8961_DSP_SIDETONE_0:
	case WM8961_DSP_SIDETONE_1:
	case WM8961_DC_SERVO_0:
	case WM8961_DC_SERVO_1:
	case WM8961_DC_SERVO_3:
	case WM8961_DC_SERVO_5:
	case WM8961_ANALOGUE_PGA_BIAS:
	case WM8961_ANALOGUE_HP_0:
	case WM8961_ANALOGUE_HP_2:
	case WM8961_CHARGE_PUMP_1:
	case WM8961_CHARGE_PUMP_B:
	case WM8961_WRITE_SEQUENCER_1:
	case WM8961_WRITE_SEQUENCER_2:
	case WM8961_WRITE_SEQUENCER_3:
	case WM8961_WRITE_SEQUENCER_4:
	case WM8961_WRITE_SEQUENCER_5:
	case WM8961_WRITE_SEQUENCER_6:
	case WM8961_WRITE_SEQUENCER_7:
	case WM8961_GENERAL_TEST_1:
		return true;
	default:
		return false;
	}
}

/*
 * The headphone output supports special anti-pop sequences giving
 * silent power up and power down.
 */
static int wm8961_hp_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u16 hp_reg = snd_soc_component_read(component, WM8961_ANALOGUE_HP_0);
	u16 cp_reg = snd_soc_component_read(component, WM8961_CHARGE_PUMP_1);
	u16 pwr_reg = snd_soc_component_read(component, WM8961_PWR_MGMT_2);
	u16 dcs_reg = snd_soc_component_read(component, WM8961_DC_SERVO_1);
	int timeout = 500;

	if (event & SND_SOC_DAPM_POST_PMU) {
		/* Make sure the output is shorted */
		hp_reg &= ~(WM8961_HPR_RMV_SHORT | WM8961_HPL_RMV_SHORT);
		snd_soc_component_write(component, WM8961_ANALOGUE_HP_0, hp_reg);

		/* Enable the charge pump */
		cp_reg |= WM8961_CP_ENA;
		snd_soc_component_write(component, WM8961_CHARGE_PUMP_1, cp_reg);
		mdelay(5);

		/* Enable the PGA */
		pwr_reg |= WM8961_LOUT1_PGA | WM8961_ROUT1_PGA;
		snd_soc_component_write(component, WM8961_PWR_MGMT_2, pwr_reg);

		/* Enable the amplifier */
		hp_reg |= WM8961_HPR_ENA | WM8961_HPL_ENA;
		snd_soc_component_write(component, WM8961_ANALOGUE_HP_0, hp_reg);

		/* Second stage enable */
		hp_reg |= WM8961_HPR_ENA_DLY | WM8961_HPL_ENA_DLY;
		snd_soc_component_write(component, WM8961_ANALOGUE_HP_0, hp_reg);

		/* Enable the DC servo & trigger startup */
		dcs_reg |=
			WM8961_DCS_ENA_CHAN_HPR | WM8961_DCS_TRIG_STARTUP_HPR |
			WM8961_DCS_ENA_CHAN_HPL | WM8961_DCS_TRIG_STARTUP_HPL;
		dev_dbg(component->dev, "Enabling DC servo\n");

		snd_soc_component_write(component, WM8961_DC_SERVO_1, dcs_reg);
		do {
			msleep(1);
			dcs_reg = snd_soc_component_read(component, WM8961_DC_SERVO_1);
		} while (--timeout &&
			 dcs_reg & (WM8961_DCS_TRIG_STARTUP_HPR |
				WM8961_DCS_TRIG_STARTUP_HPL));
		if (dcs_reg & (WM8961_DCS_TRIG_STARTUP_HPR |
			       WM8961_DCS_TRIG_STARTUP_HPL))
			dev_err(component->dev, "DC servo timed out\n");
		else
			dev_dbg(component->dev, "DC servo startup complete\n");

		/* Enable the output stage */
		hp_reg |= WM8961_HPR_ENA_OUTP | WM8961_HPL_ENA_OUTP;
		snd_soc_component_write(component, WM8961_ANALOGUE_HP_0, hp_reg);

		/* Remove the short on the output stage */
		hp_reg |= WM8961_HPR_RMV_SHORT | WM8961_HPL_RMV_SHORT;
		snd_soc_component_write(component, WM8961_ANALOGUE_HP_0, hp_reg);
	}

	if (event & SND_SOC_DAPM_PRE_PMD) {
		/* Short the output */
		hp_reg &= ~(WM8961_HPR_RMV_SHORT | WM8961_HPL_RMV_SHORT);
		snd_soc_component_write(component, WM8961_ANALOGUE_HP_0, hp_reg);

		/* Disable the output stage */
		hp_reg &= ~(WM8961_HPR_ENA_OUTP | WM8961_HPL_ENA_OUTP);
		snd_soc_component_write(component, WM8961_ANALOGUE_HP_0, hp_reg);

		/* Disable DC offset cancellation */
		dcs_reg &= ~(WM8961_DCS_ENA_CHAN_HPR |
			     WM8961_DCS_ENA_CHAN_HPL);
		snd_soc_component_write(component, WM8961_DC_SERVO_1, dcs_reg);

		/* Finish up */
		hp_reg &= ~(WM8961_HPR_ENA_DLY | WM8961_HPR_ENA |
			    WM8961_HPL_ENA_DLY | WM8961_HPL_ENA);
		snd_soc_component_write(component, WM8961_ANALOGUE_HP_0, hp_reg);

		/* Disable the PGA */
		pwr_reg &= ~(WM8961_LOUT1_PGA | WM8961_ROUT1_PGA);
		snd_soc_component_write(component, WM8961_PWR_MGMT_2, pwr_reg);

		/* Disable the charge pump */
		dev_dbg(component->dev, "Disabling charge pump\n");
		snd_soc_component_write(component, WM8961_CHARGE_PUMP_1,
			     cp_reg & ~WM8961_CP_ENA);
	}

	return 0;
}

static int wm8961_spk_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u16 pwr_reg = snd_soc_component_read(component, WM8961_PWR_MGMT_2);
	u16 spk_reg = snd_soc_component_read(component, WM8961_CLASS_D_CONTROL_1);

	if (event & SND_SOC_DAPM_POST_PMU) {
		/* Enable the PGA */
		pwr_reg |= WM8961_SPKL_PGA | WM8961_SPKR_PGA;
		snd_soc_component_write(component, WM8961_PWR_MGMT_2, pwr_reg);

		/* Enable the amplifier */
		spk_reg |= WM8961_SPKL_ENA | WM8961_SPKR_ENA;
		snd_soc_component_write(component, WM8961_CLASS_D_CONTROL_1, spk_reg);
	}

	if (event & SND_SOC_DAPM_PRE_PMD) {
		/* Disable the amplifier */
		spk_reg &= ~(WM8961_SPKL_ENA | WM8961_SPKR_ENA);
		snd_soc_component_write(component, WM8961_CLASS_D_CONTROL_1, spk_reg);

		/* Disable the PGA */
		pwr_reg &= ~(WM8961_SPKL_PGA | WM8961_SPKR_PGA);
		snd_soc_component_write(component, WM8961_PWR_MGMT_2, pwr_reg);
	}

	return 0;
}

static const char *adc_hpf_text[] = {
	"Hi-fi", "Voice 1", "Voice 2", "Voice 3",
};

static SOC_ENUM_SINGLE_DECL(adc_hpf,
			    WM8961_ADC_DAC_CONTROL_2, 7, adc_hpf_text);

static const char *dac_deemph_text[] = {
	"None", "32kHz", "44.1kHz", "48kHz",
};

static SOC_ENUM_SINGLE_DECL(dac_deemph,
			    WM8961_ADC_DAC_CONTROL_1, 1, dac_deemph_text);

static const DECLARE_TLV_DB_SCALE(out_tlv, -12100, 100, 1);
static const DECLARE_TLV_DB_SCALE(hp_sec_tlv, -700, 100, 0);
static const DECLARE_TLV_DB_SCALE(adc_tlv, -7200, 75, 1);
static const DECLARE_TLV_DB_SCALE(sidetone_tlv, -3600, 300, 0);
static const DECLARE_TLV_DB_RANGE(boost_tlv,
	0, 0, TLV_DB_SCALE_ITEM(0,  0, 0),
	1, 1, TLV_DB_SCALE_ITEM(13, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(20, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(29, 0, 0)
);
static const DECLARE_TLV_DB_SCALE(pga_tlv, -2325, 75, 0);

static const struct snd_kcontrol_new wm8961_snd_controls[] = {
SOC_DOUBLE_R_TLV("Headphone Volume", WM8961_LOUT1_VOLUME, WM8961_ROUT1_VOLUME,
		 0, 127, 0, out_tlv),
SOC_DOUBLE_TLV("Headphone Secondary Volume", WM8961_ANALOGUE_HP_2,
	       6, 3, 7, 0, hp_sec_tlv),
SOC_DOUBLE_R("Headphone ZC Switch", WM8961_LOUT1_VOLUME, WM8961_ROUT1_VOLUME,
	     7, 1, 0),

SOC_DOUBLE_R_TLV("Speaker Volume", WM8961_LOUT2_VOLUME, WM8961_ROUT2_VOLUME,
		 0, 127, 0, out_tlv),
SOC_DOUBLE_R("Speaker ZC Switch", WM8961_LOUT2_VOLUME, WM8961_ROUT2_VOLUME,
	   7, 1, 0),
SOC_SINGLE("Speaker AC Gain", WM8961_CLASS_D_CONTROL_2, 0, 7, 0),

SOC_SINGLE("DAC x128 OSR Switch", WM8961_ADC_DAC_CONTROL_2, 0, 1, 0),
SOC_ENUM("DAC Deemphasis", dac_deemph),
SOC_SINGLE("DAC Soft Mute Switch", WM8961_ADC_DAC_CONTROL_2, 3, 1, 0),

SOC_DOUBLE_R_TLV("Sidetone Volume", WM8961_DSP_SIDETONE_0,
		 WM8961_DSP_SIDETONE_1, 4, 12, 0, sidetone_tlv),

SOC_SINGLE("ADC High Pass Filter Switch", WM8961_ADC_DAC_CONTROL_1, 0, 1, 0),
SOC_ENUM("ADC High Pass Filter Mode", adc_hpf),

SOC_DOUBLE_R_TLV("Capture Volume",
		 WM8961_LEFT_ADC_VOLUME, WM8961_RIGHT_ADC_VOLUME,
		 1, 119, 0, adc_tlv),
SOC_DOUBLE_R_TLV("Capture Boost Volume",
		 WM8961_ADCL_SIGNAL_PATH, WM8961_ADCR_SIGNAL_PATH,
		 4, 3, 0, boost_tlv),
SOC_DOUBLE_R_TLV("Capture PGA Volume",
		 WM8961_LEFT_INPUT_VOLUME, WM8961_RIGHT_INPUT_VOLUME,
		 0, 62, 0, pga_tlv),
SOC_DOUBLE_R("Capture PGA ZC Switch",
	     WM8961_LEFT_INPUT_VOLUME, WM8961_RIGHT_INPUT_VOLUME,
	     6, 1, 1),
SOC_DOUBLE_R("Capture PGA Switch",
	     WM8961_LEFT_INPUT_VOLUME, WM8961_RIGHT_INPUT_VOLUME,
	     7, 1, 1),
};

static const char *sidetone_text[] = {
	"None", "Left", "Right"
};

static SOC_ENUM_SINGLE_DECL(dacl_sidetone,
			    WM8961_DSP_SIDETONE_0, 2, sidetone_text);

static SOC_ENUM_SINGLE_DECL(dacr_sidetone,
			    WM8961_DSP_SIDETONE_1, 2, sidetone_text);

static const struct snd_kcontrol_new dacl_mux =
	SOC_DAPM_ENUM("DACL Sidetone", dacl_sidetone);

static const struct snd_kcontrol_new dacr_mux =
	SOC_DAPM_ENUM("DACR Sidetone", dacr_sidetone);

static const struct snd_soc_dapm_widget wm8961_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("LINPUT"),
SND_SOC_DAPM_INPUT("RINPUT"),

SND_SOC_DAPM_SUPPLY("CLK_DSP", WM8961_CLOCKING2, 4, 0, NULL, 0),

SND_SOC_DAPM_PGA("Left Input", WM8961_PWR_MGMT_1, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Input", WM8961_PWR_MGMT_1, 4, 0, NULL, 0),

SND_SOC_DAPM_ADC("ADCL", "HiFi Capture", WM8961_PWR_MGMT_1, 3, 0),
SND_SOC_DAPM_ADC("ADCR", "HiFi Capture", WM8961_PWR_MGMT_1, 2, 0),

SND_SOC_DAPM_SUPPLY("MICBIAS", WM8961_PWR_MGMT_1, 1, 0, NULL, 0),

SND_SOC_DAPM_MUX("DACL Sidetone", SND_SOC_NOPM, 0, 0, &dacl_mux),
SND_SOC_DAPM_MUX("DACR Sidetone", SND_SOC_NOPM, 0, 0, &dacr_mux),

SND_SOC_DAPM_DAC("DACL", "HiFi Playback", WM8961_PWR_MGMT_2, 8, 0),
SND_SOC_DAPM_DAC("DACR", "HiFi Playback", WM8961_PWR_MGMT_2, 7, 0),

/* Handle as a mono path for DCS */
SND_SOC_DAPM_PGA_E("Headphone Output", SND_SOC_NOPM,
		   4, 0, NULL, 0, wm8961_hp_event,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_PGA_E("Speaker Output", SND_SOC_NOPM,
		   4, 0, NULL, 0, wm8961_spk_event,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

SND_SOC_DAPM_OUTPUT("HP_L"),
SND_SOC_DAPM_OUTPUT("HP_R"),
SND_SOC_DAPM_OUTPUT("SPK_LN"),
SND_SOC_DAPM_OUTPUT("SPK_LP"),
SND_SOC_DAPM_OUTPUT("SPK_RN"),
SND_SOC_DAPM_OUTPUT("SPK_RP"),
};


static const struct snd_soc_dapm_route audio_paths[] = {
	{ "DACL", NULL, "CLK_DSP" },
	{ "DACL", NULL, "DACL Sidetone" },
	{ "DACR", NULL, "CLK_DSP" },
	{ "DACR", NULL, "DACR Sidetone" },

	{ "DACL Sidetone", "Left", "ADCL" },
	{ "DACL Sidetone", "Right", "ADCR" },

	{ "DACR Sidetone", "Left", "ADCL" },
	{ "DACR Sidetone", "Right", "ADCR" },

	{ "HP_L", NULL, "Headphone Output" },
	{ "HP_R", NULL, "Headphone Output" },
	{ "Headphone Output", NULL, "DACL" },
	{ "Headphone Output", NULL, "DACR" },

	{ "SPK_LN", NULL, "Speaker Output" },
	{ "SPK_LP", NULL, "Speaker Output" },
	{ "SPK_RN", NULL, "Speaker Output" },
	{ "SPK_RP", NULL, "Speaker Output" },

	{ "Speaker Output", NULL, "DACL" },
	{ "Speaker Output", NULL, "DACR" },

	{ "ADCL", NULL, "Left Input" },
	{ "ADCL", NULL, "CLK_DSP" },
	{ "ADCR", NULL, "Right Input" },
	{ "ADCR", NULL, "CLK_DSP" },

	{ "Left Input", NULL, "LINPUT" },
	{ "Right Input", NULL, "RINPUT" },

};

/* Values for CLK_SYS_RATE */
static struct {
	int ratio;
	u16 val;
} wm8961_clk_sys_ratio[] = {
	{  64,  0 },
	{  128, 1 },
	{  192, 2 },
	{  256, 3 },
	{  384, 4 },
	{  512, 5 },
	{  768, 6 },
	{ 1024, 7 },
	{ 1408, 8 },
	{ 1536, 9 },
};

/* Values for SAMPLE_RATE */
static struct {
	int rate;
	u16 val;
} wm8961_srate[] = {
	{ 48000, 0 },
	{ 44100, 0 },
	{ 32000, 1 },
	{ 22050, 2 },
	{ 24000, 2 },
	{ 16000, 3 },
	{ 11250, 4 },
	{ 12000, 4 },
	{  8000, 5 },
};

static int wm8961_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct wm8961_priv *wm8961 = snd_soc_component_get_drvdata(component);
	int i, best, target, fs;
	u16 reg;

	fs = params_rate(params);

	if (!wm8961->sysclk) {
		dev_err(component->dev, "MCLK has not been specified\n");
		return -EINVAL;
	}

	/* Find the closest sample rate for the filters */
	best = 0;
	for (i = 0; i < ARRAY_SIZE(wm8961_srate); i++) {
		if (abs(wm8961_srate[i].rate - fs) <
		    abs(wm8961_srate[best].rate - fs))
			best = i;
	}
	reg = snd_soc_component_read(component, WM8961_ADDITIONAL_CONTROL_3);
	reg &= ~WM8961_SAMPLE_RATE_MASK;
	reg |= wm8961_srate[best].val;
	snd_soc_component_write(component, WM8961_ADDITIONAL_CONTROL_3, reg);
	dev_dbg(component->dev, "Selected SRATE %dHz for %dHz\n",
		wm8961_srate[best].rate, fs);

	/* Select a CLK_SYS/fs ratio equal to or higher than required */
	target = wm8961->sysclk / fs;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK && target < 64) {
		dev_err(component->dev,
			"SYSCLK must be at least 64*fs for DAC\n");
		return -EINVAL;
	}
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE && target < 256) {
		dev_err(component->dev,
			"SYSCLK must be at least 256*fs for ADC\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(wm8961_clk_sys_ratio); i++) {
		if (wm8961_clk_sys_ratio[i].ratio >= target)
			break;
	}
	if (i == ARRAY_SIZE(wm8961_clk_sys_ratio)) {
		dev_err(component->dev, "Unable to generate CLK_SYS_RATE\n");
		return -EINVAL;
	}
	dev_dbg(component->dev, "Selected CLK_SYS_RATE of %d for %d/%d=%d\n",
		wm8961_clk_sys_ratio[i].ratio, wm8961->sysclk, fs,
		wm8961->sysclk / fs);

	reg = snd_soc_component_read(component, WM8961_CLOCKING_4);
	reg &= ~WM8961_CLK_SYS_RATE_MASK;
	reg |= wm8961_clk_sys_ratio[i].val << WM8961_CLK_SYS_RATE_SHIFT;
	snd_soc_component_write(component, WM8961_CLOCKING_4, reg);

	reg = snd_soc_component_read(component, WM8961_AUDIO_INTERFACE_0);
	reg &= ~WM8961_WL_MASK;
	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		reg |= 1 << WM8961_WL_SHIFT;
		break;
	case 24:
		reg |= 2 << WM8961_WL_SHIFT;
		break;
	case 32:
		reg |= 3 << WM8961_WL_SHIFT;
		break;
	default:
		return -EINVAL;
	}
	snd_soc_component_write(component, WM8961_AUDIO_INTERFACE_0, reg);

	/* Sloping stop-band filter is recommended for <= 24kHz */
	reg = snd_soc_component_read(component, WM8961_ADC_DAC_CONTROL_2);
	if (fs <= 24000)
		reg |= WM8961_DACSLOPE;
	else
		reg &= ~WM8961_DACSLOPE;
	snd_soc_component_write(component, WM8961_ADC_DAC_CONTROL_2, reg);

	return 0;
}

static int wm8961_set_sysclk(struct snd_soc_dai *dai, int clk_id,
			     unsigned int freq,
			     int dir)
{
	struct snd_soc_component *component = dai->component;
	struct wm8961_priv *wm8961 = snd_soc_component_get_drvdata(component);
	u16 reg = snd_soc_component_read(component, WM8961_CLOCKING1);

	if (freq > 33000000) {
		dev_err(component->dev, "MCLK must be <33MHz\n");
		return -EINVAL;
	}

	if (freq > 16500000) {
		dev_dbg(component->dev, "Using MCLK/2 for %dHz MCLK\n", freq);
		reg |= WM8961_MCLKDIV;
		freq /= 2;
	} else {
		dev_dbg(component->dev, "Using MCLK/1 for %dHz MCLK\n", freq);
		reg &= ~WM8961_MCLKDIV;
	}

	snd_soc_component_write(component, WM8961_CLOCKING1, reg);

	wm8961->sysclk = freq;

	return 0;
}

static int wm8961_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	u16 aif = snd_soc_component_read(component, WM8961_AUDIO_INTERFACE_0);

	aif &= ~(WM8961_BCLKINV | WM8961_LRP |
		 WM8961_MS | WM8961_FORMAT_MASK);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		aif |= WM8961_MS;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		break;

	case SND_SOC_DAIFMT_LEFT_J:
		aif |= 1;
		break;

	case SND_SOC_DAIFMT_I2S:
		aif |= 2;
		break;

	case SND_SOC_DAIFMT_DSP_B:
		aif |= WM8961_LRP;
		fallthrough;
	case SND_SOC_DAIFMT_DSP_A:
		aif |= 3;
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
		case SND_SOC_DAIFMT_IB_NF:
			break;
		default:
			return -EINVAL;
		}
		break;

	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_NB_IF:
		aif |= WM8961_LRP;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		aif |= WM8961_BCLKINV;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		aif |= WM8961_BCLKINV | WM8961_LRP;
		break;
	default:
		return -EINVAL;
	}

	return snd_soc_component_write(component, WM8961_AUDIO_INTERFACE_0, aif);
}

static int wm8961_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	struct snd_soc_component *component = dai->component;
	u16 reg = snd_soc_component_read(component, WM8961_ADDITIONAL_CONTROL_2);

	if (tristate)
		reg |= WM8961_TRIS;
	else
		reg &= ~WM8961_TRIS;

	return snd_soc_component_write(component, WM8961_ADDITIONAL_CONTROL_2, reg);
}

static int wm8961_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	u16 reg = snd_soc_component_read(component, WM8961_ADC_DAC_CONTROL_1);

	if (mute)
		reg |= WM8961_DACMU;
	else
		reg &= ~WM8961_DACMU;

	msleep(17);

	return snd_soc_component_write(component, WM8961_ADC_DAC_CONTROL_1, reg);
}

static int wm8961_set_clkdiv(struct snd_soc_dai *dai, int div_id, int div)
{
	struct snd_soc_component *component = dai->component;
	u16 reg;

	switch (div_id) {
	case WM8961_BCLK:
		reg = snd_soc_component_read(component, WM8961_CLOCKING2);
		reg &= ~WM8961_BCLKDIV_MASK;
		reg |= div;
		snd_soc_component_write(component, WM8961_CLOCKING2, reg);
		break;

	case WM8961_LRCLK:
		reg = snd_soc_component_read(component, WM8961_AUDIO_INTERFACE_2);
		reg &= ~WM8961_LRCLK_RATE_MASK;
		reg |= div;
		snd_soc_component_write(component, WM8961_AUDIO_INTERFACE_2, reg);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int wm8961_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	u16 reg;

	/* This is all slightly unusual since we have no bypass paths
	 * and the output amplifier structure means we can just slam
	 * the biases straight up rather than having to ramp them
	 * slowly.
	 */
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_STANDBY) {
			/* Enable bias generation */
			reg = snd_soc_component_read(component, WM8961_ANTI_POP);
			reg |= WM8961_BUFIOEN | WM8961_BUFDCOPEN;
			snd_soc_component_write(component, WM8961_ANTI_POP, reg);

			/* VMID=2*50k, VREF */
			reg = snd_soc_component_read(component, WM8961_PWR_MGMT_1);
			reg &= ~WM8961_VMIDSEL_MASK;
			reg |= (1 << WM8961_VMIDSEL_SHIFT) | WM8961_VREF;
			snd_soc_component_write(component, WM8961_PWR_MGMT_1, reg);
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_PREPARE) {
			/* VREF off */
			reg = snd_soc_component_read(component, WM8961_PWR_MGMT_1);
			reg &= ~WM8961_VREF;
			snd_soc_component_write(component, WM8961_PWR_MGMT_1, reg);

			/* Bias generation off */
			reg = snd_soc_component_read(component, WM8961_ANTI_POP);
			reg &= ~(WM8961_BUFIOEN | WM8961_BUFDCOPEN);
			snd_soc_component_write(component, WM8961_ANTI_POP, reg);

			/* VMID off */
			reg = snd_soc_component_read(component, WM8961_PWR_MGMT_1);
			reg &= ~WM8961_VMIDSEL_MASK;
			snd_soc_component_write(component, WM8961_PWR_MGMT_1, reg);
		}
		break;

	case SND_SOC_BIAS_OFF:
		break;
	}

	return 0;
}


#define WM8961_RATES SNDRV_PCM_RATE_8000_48000

#define WM8961_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
	SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops wm8961_dai_ops = {
	.hw_params = wm8961_hw_params,
	.set_sysclk = wm8961_set_sysclk,
	.set_fmt = wm8961_set_fmt,
	.mute_stream = wm8961_mute,
	.set_tristate = wm8961_set_tristate,
	.set_clkdiv = wm8961_set_clkdiv,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver wm8961_dai = {
	.name = "wm8961-hifi",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8961_RATES,
		.formats = WM8961_FORMATS,},
	.capture = {
		.stream_name = "HiFi Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8961_RATES,
		.formats = WM8961_FORMATS,},
	.ops = &wm8961_dai_ops,
};

static int wm8961_probe(struct snd_soc_component *component)
{
	u16 reg;

	/* Enable class W */
	reg = snd_soc_component_read(component, WM8961_CHARGE_PUMP_B);
	reg |= WM8961_CP_DYN_PWR_MASK;
	snd_soc_component_write(component, WM8961_CHARGE_PUMP_B, reg);

	/* Latch volume update bits (right channel only, we always
	 * write both out) and default ZC on. */
	reg = snd_soc_component_read(component, WM8961_ROUT1_VOLUME);
	snd_soc_component_write(component, WM8961_ROUT1_VOLUME,
		     reg | WM8961_LO1ZC | WM8961_OUT1VU);
	snd_soc_component_write(component, WM8961_LOUT1_VOLUME, reg | WM8961_LO1ZC);
	reg = snd_soc_component_read(component, WM8961_ROUT2_VOLUME);
	snd_soc_component_write(component, WM8961_ROUT2_VOLUME,
		     reg | WM8961_SPKRZC | WM8961_SPKVU);
	snd_soc_component_write(component, WM8961_LOUT2_VOLUME, reg | WM8961_SPKLZC);

	reg = snd_soc_component_read(component, WM8961_RIGHT_ADC_VOLUME);
	snd_soc_component_write(component, WM8961_RIGHT_ADC_VOLUME, reg | WM8961_ADCVU);
	reg = snd_soc_component_read(component, WM8961_RIGHT_INPUT_VOLUME);
	snd_soc_component_write(component, WM8961_RIGHT_INPUT_VOLUME, reg | WM8961_IPVU);

	/* Use soft mute by default */
	reg = snd_soc_component_read(component, WM8961_ADC_DAC_CONTROL_2);
	reg |= WM8961_DACSMM;
	snd_soc_component_write(component, WM8961_ADC_DAC_CONTROL_2, reg);

	/* Use automatic clocking mode by default; for now this is all
	 * we support.
	 */
	reg = snd_soc_component_read(component, WM8961_CLOCKING_3);
	reg &= ~WM8961_MANUAL_MODE;
	snd_soc_component_write(component, WM8961_CLOCKING_3, reg);

	return 0;
}

#ifdef CONFIG_PM

static int wm8961_resume(struct snd_soc_component *component)
{
	snd_soc_component_cache_sync(component);

	return 0;
}
#else
#define wm8961_resume NULL
#endif

static const struct snd_soc_component_driver soc_component_dev_wm8961 = {
	.probe			= wm8961_probe,
	.resume			= wm8961_resume,
	.set_bias_level		= wm8961_set_bias_level,
	.controls		= wm8961_snd_controls,
	.num_controls		= ARRAY_SIZE(wm8961_snd_controls),
	.dapm_widgets		= wm8961_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm8961_dapm_widgets),
	.dapm_routes		= audio_paths,
	.num_dapm_routes	= ARRAY_SIZE(audio_paths),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config wm8961_regmap = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = WM8961_MAX_REGISTER,

	.reg_defaults = wm8961_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8961_reg_defaults),
	.cache_type = REGCACHE_RBTREE,

	.volatile_reg = wm8961_volatile,
	.readable_reg = wm8961_readable,
};

static int wm8961_i2c_probe(struct i2c_client *i2c)
{
	struct wm8961_priv *wm8961;
	unsigned int val;
	int ret;

	wm8961 = devm_kzalloc(&i2c->dev, sizeof(struct wm8961_priv),
			      GFP_KERNEL);
	if (wm8961 == NULL)
		return -ENOMEM;

	wm8961->regmap = devm_regmap_init_i2c(i2c, &wm8961_regmap);
	if (IS_ERR(wm8961->regmap))
		return PTR_ERR(wm8961->regmap);

	ret = regmap_read(wm8961->regmap, WM8961_SOFTWARE_RESET, &val);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to read chip ID: %d\n", ret);
		return ret;
	}

	if (val != 0x1801) {
		dev_err(&i2c->dev, "Device is not a WM8961: ID=0x%x\n", val);
		return -EINVAL;
	}

	/* This isn't volatile - readback doesn't correspond to write */
	regcache_cache_bypass(wm8961->regmap, true);
	ret = regmap_read(wm8961->regmap, WM8961_RIGHT_INPUT_VOLUME, &val);
	regcache_cache_bypass(wm8961->regmap, false);

	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to read chip revision: %d\n", ret);
		return ret;
	}

	dev_info(&i2c->dev, "WM8961 family %d revision %c\n",
		 (val & WM8961_DEVICE_ID_MASK) >> WM8961_DEVICE_ID_SHIFT,
		 ((val & WM8961_CHIP_REV_MASK) >> WM8961_CHIP_REV_SHIFT)
		 + 'A');

	ret = regmap_write(wm8961->regmap, WM8961_SOFTWARE_RESET, 0x1801);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to issue reset: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(i2c, wm8961);

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_wm8961, &wm8961_dai, 1);

	return ret;
}

static const struct i2c_device_id wm8961_i2c_id[] = {
	{ "wm8961", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8961_i2c_id);

static const struct of_device_id wm8961_of_match[] __maybe_unused = {
	{ .compatible = "wlf,wm8961", },
	{ }
};
MODULE_DEVICE_TABLE(of, wm8961_of_match);

static struct i2c_driver wm8961_i2c_driver = {
	.driver = {
		.name = "wm8961",
		.of_match_table = of_match_ptr(wm8961_of_match),
	},
	.probe = wm8961_i2c_probe,
	.id_table = wm8961_i2c_id,
};

module_i2c_driver(wm8961_i2c_driver);

MODULE_DESCRIPTION("ASoC WM8961 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
