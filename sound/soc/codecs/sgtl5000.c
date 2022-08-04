// SPDX-License-Identifier: GPL-2.0
//
// sgtl5000.c  --  SGTL5000 ALSA SoC Audio driver
//
// Copyright 2010-2011 Freescale Semiconductor, Inc. All Rights Reserved.

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/log2.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/tlv.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include "sgtl5000.h"

#define SGTL5000_DAP_REG_OFFSET	0x0100
#define SGTL5000_MAX_REG_OFFSET	0x013A

/* Delay for the VAG ramp up */
#define SGTL5000_VAG_POWERUP_DELAY 500 /* ms */
/* Delay for the VAG ramp down */
#define SGTL5000_VAG_POWERDOWN_DELAY 500 /* ms */

#define SGTL5000_OUTPUTS_MUTE (SGTL5000_HP_MUTE | SGTL5000_LINE_OUT_MUTE)

/* default value of sgtl5000 registers */
static const struct reg_default sgtl5000_reg_defaults[] = {
	{ SGTL5000_CHIP_DIG_POWER,		0x0000 },
	{ SGTL5000_CHIP_I2S_CTRL,		0x0010 },
	{ SGTL5000_CHIP_SSS_CTRL,		0x0010 },
	{ SGTL5000_CHIP_ADCDAC_CTRL,		0x020c },
	{ SGTL5000_CHIP_DAC_VOL,		0x3c3c },
	{ SGTL5000_CHIP_PAD_STRENGTH,		0x015f },
	{ SGTL5000_CHIP_ANA_ADC_CTRL,		0x0000 },
	{ SGTL5000_CHIP_ANA_HP_CTRL,		0x1818 },
	{ SGTL5000_CHIP_ANA_CTRL,		0x0111 },
	{ SGTL5000_CHIP_REF_CTRL,		0x0000 },
	{ SGTL5000_CHIP_MIC_CTRL,		0x0000 },
	{ SGTL5000_CHIP_LINE_OUT_CTRL,		0x0000 },
	{ SGTL5000_CHIP_LINE_OUT_VOL,		0x0404 },
	{ SGTL5000_CHIP_PLL_CTRL,		0x5000 },
	{ SGTL5000_CHIP_CLK_TOP_CTRL,		0x0000 },
	{ SGTL5000_CHIP_ANA_STATUS,		0x0000 },
	{ SGTL5000_CHIP_SHORT_CTRL,		0x0000 },
	{ SGTL5000_CHIP_ANA_TEST2,		0x0000 },
	{ SGTL5000_DAP_CTRL,			0x0000 },
	{ SGTL5000_DAP_PEQ,			0x0000 },
	{ SGTL5000_DAP_BASS_ENHANCE,		0x0040 },
	{ SGTL5000_DAP_BASS_ENHANCE_CTRL,	0x051f },
	{ SGTL5000_DAP_AUDIO_EQ,		0x0000 },
	{ SGTL5000_DAP_SURROUND,		0x0040 },
	{ SGTL5000_DAP_EQ_BASS_BAND0,		0x002f },
	{ SGTL5000_DAP_EQ_BASS_BAND1,		0x002f },
	{ SGTL5000_DAP_EQ_BASS_BAND2,		0x002f },
	{ SGTL5000_DAP_EQ_BASS_BAND3,		0x002f },
	{ SGTL5000_DAP_EQ_BASS_BAND4,		0x002f },
	{ SGTL5000_DAP_MAIN_CHAN,		0x8000 },
	{ SGTL5000_DAP_MIX_CHAN,		0x0000 },
	{ SGTL5000_DAP_AVC_CTRL,		0x5100 },
	{ SGTL5000_DAP_AVC_THRESHOLD,		0x1473 },
	{ SGTL5000_DAP_AVC_ATTACK,		0x0028 },
	{ SGTL5000_DAP_AVC_DECAY,		0x0050 },
};

/* AVC: Threshold dB -> register: pre-calculated values */
static const u16 avc_thr_db2reg[97] = {
	0x5168, 0x488E, 0x40AA, 0x39A1, 0x335D, 0x2DC7, 0x28CC, 0x245D, 0x2068,
	0x1CE2, 0x19BE, 0x16F1, 0x1472, 0x1239, 0x103E, 0x0E7A, 0x0CE6, 0x0B7F,
	0x0A3F, 0x0922, 0x0824, 0x0741, 0x0677, 0x05C3, 0x0522, 0x0493, 0x0414,
	0x03A2, 0x033D, 0x02E3, 0x0293, 0x024B, 0x020B, 0x01D2, 0x019F, 0x0172,
	0x014A, 0x0126, 0x0106, 0x00E9, 0x00D0, 0x00B9, 0x00A5, 0x0093, 0x0083,
	0x0075, 0x0068, 0x005D, 0x0052, 0x0049, 0x0041, 0x003A, 0x0034, 0x002E,
	0x0029, 0x0025, 0x0021, 0x001D, 0x001A, 0x0017, 0x0014, 0x0012, 0x0010,
	0x000E, 0x000D, 0x000B, 0x000A, 0x0009, 0x0008, 0x0007, 0x0006, 0x0005,
	0x0005, 0x0004, 0x0004, 0x0003, 0x0003, 0x0002, 0x0002, 0x0002, 0x0002,
	0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};

/* regulator supplies for sgtl5000, VDDD is an optional external supply */
enum sgtl5000_regulator_supplies {
	VDDA,
	VDDIO,
	VDDD,
	SGTL5000_SUPPLY_NUM
};

/* vddd is optional supply */
static const char *supply_names[SGTL5000_SUPPLY_NUM] = {
	"VDDA",
	"VDDIO",
	"VDDD"
};

#define LDO_VOLTAGE		1200000
#define LINREG_VDDD	((1600 - LDO_VOLTAGE / 1000) / 50)

enum sgtl5000_micbias_resistor {
	SGTL5000_MICBIAS_OFF = 0,
	SGTL5000_MICBIAS_2K = 2,
	SGTL5000_MICBIAS_4K = 4,
	SGTL5000_MICBIAS_8K = 8,
};

enum  {
	I2S_LRCLK_STRENGTH_DISABLE,
	I2S_LRCLK_STRENGTH_LOW,
	I2S_LRCLK_STRENGTH_MEDIUM,
	I2S_LRCLK_STRENGTH_HIGH,
};

enum  {
	I2S_SCLK_STRENGTH_DISABLE,
	I2S_SCLK_STRENGTH_LOW,
	I2S_SCLK_STRENGTH_MEDIUM,
	I2S_SCLK_STRENGTH_HIGH,
};

enum {
	HP_POWER_EVENT,
	DAC_POWER_EVENT,
	ADC_POWER_EVENT,
	LAST_POWER_EVENT = ADC_POWER_EVENT
};

/* sgtl5000 private structure in codec */
struct sgtl5000_priv {
	int sysclk;	/* sysclk rate */
	int master;	/* i2s master or not */
	int fmt;	/* i2s data format */
	struct regulator_bulk_data supplies[SGTL5000_SUPPLY_NUM];
	int num_supplies;
	struct regmap *regmap;
	struct clk *mclk;
	int revision;
	u8 micbias_resistor;
	u8 micbias_voltage;
	u8 lrclk_strength;
	u8 sclk_strength;
	u16 mute_state[LAST_POWER_EVENT + 1];
};

static inline int hp_sel_input(struct snd_soc_component *component)
{
	return (snd_soc_component_read(component, SGTL5000_CHIP_ANA_CTRL) &
		SGTL5000_HP_SEL_MASK) >> SGTL5000_HP_SEL_SHIFT;
}

static inline u16 mute_output(struct snd_soc_component *component,
			      u16 mute_mask)
{
	u16 mute_reg = snd_soc_component_read(component,
					      SGTL5000_CHIP_ANA_CTRL);

	snd_soc_component_update_bits(component, SGTL5000_CHIP_ANA_CTRL,
			    mute_mask, mute_mask);
	return mute_reg;
}

static inline void restore_output(struct snd_soc_component *component,
				  u16 mute_mask, u16 mute_reg)
{
	snd_soc_component_update_bits(component, SGTL5000_CHIP_ANA_CTRL,
		mute_mask, mute_reg);
}

static void vag_power_on(struct snd_soc_component *component, u32 source)
{
	if (snd_soc_component_read(component, SGTL5000_CHIP_ANA_POWER) &
	    SGTL5000_VAG_POWERUP)
		return;

	snd_soc_component_update_bits(component, SGTL5000_CHIP_ANA_POWER,
			    SGTL5000_VAG_POWERUP, SGTL5000_VAG_POWERUP);

	/* When VAG powering on to get local loop from Line-In, the sleep
	 * is required to avoid loud pop.
	 */
	if (hp_sel_input(component) == SGTL5000_HP_SEL_LINE_IN &&
	    source == HP_POWER_EVENT)
		msleep(SGTL5000_VAG_POWERUP_DELAY);
}

static int vag_power_consumers(struct snd_soc_component *component,
			       u16 ana_pwr_reg, u32 source)
{
	int consumers = 0;

	/* count dac/adc consumers unconditional */
	if (ana_pwr_reg & SGTL5000_DAC_POWERUP)
		consumers++;
	if (ana_pwr_reg & SGTL5000_ADC_POWERUP)
		consumers++;

	/*
	 * If the event comes from HP and Line-In is selected,
	 * current action is 'DAC to be powered down'.
	 * As HP_POWERUP is not set when HP muxed to line-in,
	 * we need to keep VAG power ON.
	 */
	if (source == HP_POWER_EVENT) {
		if (hp_sel_input(component) == SGTL5000_HP_SEL_LINE_IN)
			consumers++;
	} else {
		if (ana_pwr_reg & SGTL5000_HP_POWERUP)
			consumers++;
	}

	return consumers;
}

static void vag_power_off(struct snd_soc_component *component, u32 source)
{
	u16 ana_pwr = snd_soc_component_read(component,
					     SGTL5000_CHIP_ANA_POWER);

	if (!(ana_pwr & SGTL5000_VAG_POWERUP))
		return;

	/*
	 * This function calls when any of VAG power consumers is disappearing.
	 * Thus, if there is more than one consumer at the moment, as minimum
	 * one consumer will definitely stay after the end of the current
	 * event.
	 * Don't clear VAG_POWERUP if 2 or more consumers of VAG present:
	 * - LINE_IN (for HP events) / HP (for DAC/ADC events)
	 * - DAC
	 * - ADC
	 * (the current consumer is disappearing right now)
	 */
	if (vag_power_consumers(component, ana_pwr, source) >= 2)
		return;

	snd_soc_component_update_bits(component, SGTL5000_CHIP_ANA_POWER,
		SGTL5000_VAG_POWERUP, 0);
	/* In power down case, we need wait 400-1000 ms
	 * when VAG fully ramped down.
	 * As longer we wait, as smaller pop we've got.
	 */
	msleep(SGTL5000_VAG_POWERDOWN_DELAY);
}

/*
 * mic_bias power on/off share the same register bits with
 * output impedance of mic bias, when power on mic bias, we
 * need reclaim it to impedance value.
 * 0x0 = Powered off
 * 0x1 = 2Kohm
 * 0x2 = 4Kohm
 * 0x3 = 8Kohm
 */
static int mic_bias_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct sgtl5000_priv *sgtl5000 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* change mic bias resistor */
		snd_soc_component_update_bits(component, SGTL5000_CHIP_MIC_CTRL,
			SGTL5000_BIAS_R_MASK,
			sgtl5000->micbias_resistor << SGTL5000_BIAS_R_SHIFT);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, SGTL5000_CHIP_MIC_CTRL,
				SGTL5000_BIAS_R_MASK, 0);
		break;
	}
	return 0;
}

static int vag_and_mute_control(struct snd_soc_component *component,
				 int event, int event_source)
{
	static const u16 mute_mask[] = {
		/*
		 * Mask for HP_POWER_EVENT.
		 * Muxing Headphones have to be wrapped with mute/unmute
		 * headphones only.
		 */
		SGTL5000_HP_MUTE,
		/*
		 * Masks for DAC_POWER_EVENT/ADC_POWER_EVENT.
		 * Muxing DAC or ADC block have to wrapped with mute/unmute
		 * both headphones and line-out.
		 */
		SGTL5000_OUTPUTS_MUTE,
		SGTL5000_OUTPUTS_MUTE
	};

	struct sgtl5000_priv *sgtl5000 =
		snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		sgtl5000->mute_state[event_source] =
			mute_output(component, mute_mask[event_source]);
		break;
	case SND_SOC_DAPM_POST_PMU:
		vag_power_on(component, event_source);
		restore_output(component, mute_mask[event_source],
			       sgtl5000->mute_state[event_source]);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		sgtl5000->mute_state[event_source] =
			mute_output(component, mute_mask[event_source]);
		vag_power_off(component, event_source);
		break;
	case SND_SOC_DAPM_POST_PMD:
		restore_output(component, mute_mask[event_source],
			       sgtl5000->mute_state[event_source]);
		break;
	default:
		break;
	}

	return 0;
}

/*
 * Mute Headphone when power it up/down.
 * Control VAG power on HP power path.
 */
static int headphone_pga_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	return vag_and_mute_control(component, event, HP_POWER_EVENT);
}

/* As manual describes, ADC/DAC powering up/down requires
 * to mute outputs to avoid pops.
 * Control VAG power on ADC/DAC power path.
 */
static int adc_updown_depop(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	return vag_and_mute_control(component, event, ADC_POWER_EVENT);
}

static int dac_updown_depop(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	return vag_and_mute_control(component, event, DAC_POWER_EVENT);
}

/* input sources for ADC */
static const char *adc_mux_text[] = {
	"MIC_IN", "LINE_IN"
};

static SOC_ENUM_SINGLE_DECL(adc_enum,
			    SGTL5000_CHIP_ANA_CTRL, 2,
			    adc_mux_text);

static const struct snd_kcontrol_new adc_mux =
SOC_DAPM_ENUM("Capture Mux", adc_enum);

/* input sources for headphone */
static const char *hp_mux_text[] = {
	"DAC", "LINE_IN"
};

static SOC_ENUM_SINGLE_DECL(hp_enum,
			    SGTL5000_CHIP_ANA_CTRL, 6,
			    hp_mux_text);

static const struct snd_kcontrol_new hp_mux =
SOC_DAPM_ENUM("Headphone Mux", hp_enum);

/* input sources for DAC */
static const char *dac_mux_text[] = {
	"ADC", "I2S", "Rsvrd", "DAP"
};

static SOC_ENUM_SINGLE_DECL(dac_enum,
			    SGTL5000_CHIP_SSS_CTRL, SGTL5000_DAC_SEL_SHIFT,
			    dac_mux_text);

static const struct snd_kcontrol_new dac_mux =
SOC_DAPM_ENUM("Digital Input Mux", dac_enum);

/* input sources for DAP */
static const char *dap_mux_text[] = {
	"ADC", "I2S"
};

static SOC_ENUM_SINGLE_DECL(dap_enum,
			    SGTL5000_CHIP_SSS_CTRL, SGTL5000_DAP_SEL_SHIFT,
			    dap_mux_text);

static const struct snd_kcontrol_new dap_mux =
SOC_DAPM_ENUM("DAP Mux", dap_enum);

/* input sources for DAP mix */
static const char *dapmix_mux_text[] = {
	"ADC", "I2S"
};

static SOC_ENUM_SINGLE_DECL(dapmix_enum,
			    SGTL5000_CHIP_SSS_CTRL, SGTL5000_DAP_MIX_SEL_SHIFT,
			    dapmix_mux_text);

static const struct snd_kcontrol_new dapmix_mux =
SOC_DAPM_ENUM("DAP MIX Mux", dapmix_enum);


static const struct snd_soc_dapm_widget sgtl5000_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("LINE_IN"),
	SND_SOC_DAPM_INPUT("MIC_IN"),

	SND_SOC_DAPM_OUTPUT("HP_OUT"),
	SND_SOC_DAPM_OUTPUT("LINE_OUT"),

	SND_SOC_DAPM_SUPPLY("Mic Bias", SGTL5000_CHIP_MIC_CTRL, 8, 0,
			    mic_bias_event,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_PGA_E("HP", SGTL5000_CHIP_ANA_POWER, 4, 0, NULL, 0,
			   headphone_pga_event,
			   SND_SOC_DAPM_PRE_POST_PMU |
			   SND_SOC_DAPM_PRE_POST_PMD),
	SND_SOC_DAPM_PGA("LO", SGTL5000_CHIP_ANA_POWER, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("Capture Mux", SND_SOC_NOPM, 0, 0, &adc_mux),
	SND_SOC_DAPM_MUX("Headphone Mux", SND_SOC_NOPM, 0, 0, &hp_mux),
	SND_SOC_DAPM_MUX("Digital Input Mux", SND_SOC_NOPM, 0, 0, &dac_mux),
	SND_SOC_DAPM_MUX("DAP Mux", SGTL5000_DAP_CTRL, 0, 0, &dap_mux),
	SND_SOC_DAPM_MUX("DAP MIX Mux", SGTL5000_DAP_CTRL, 4, 0, &dapmix_mux),
	SND_SOC_DAPM_MIXER("DAP", SGTL5000_CHIP_DIG_POWER, 4, 0, NULL, 0),


	/* aif for i2s input */
	SND_SOC_DAPM_AIF_IN("AIFIN", "Playback",
				0, SGTL5000_CHIP_DIG_POWER,
				0, 0),

	/* aif for i2s output */
	SND_SOC_DAPM_AIF_OUT("AIFOUT", "Capture",
				0, SGTL5000_CHIP_DIG_POWER,
				1, 0),

	SND_SOC_DAPM_ADC_E("ADC", "Capture", SGTL5000_CHIP_ANA_POWER, 1, 0,
			   adc_updown_depop, SND_SOC_DAPM_PRE_POST_PMU |
			   SND_SOC_DAPM_PRE_POST_PMD),
	SND_SOC_DAPM_DAC_E("DAC", "Playback", SGTL5000_CHIP_ANA_POWER, 3, 0,
			   dac_updown_depop, SND_SOC_DAPM_PRE_POST_PMU |
			   SND_SOC_DAPM_PRE_POST_PMD),
};

/* routes for sgtl5000 */
static const struct snd_soc_dapm_route sgtl5000_dapm_routes[] = {
	{"Capture Mux", "LINE_IN", "LINE_IN"},	/* line_in --> adc_mux */
	{"Capture Mux", "MIC_IN", "MIC_IN"},	/* mic_in --> adc_mux */

	{"ADC", NULL, "Capture Mux"},		/* adc_mux --> adc */
	{"AIFOUT", NULL, "ADC"},		/* adc --> i2s_out */

	{"DAP Mux", "ADC", "ADC"},		/* adc --> DAP mux */
	{"DAP Mux", NULL, "AIFIN"},		/* i2s --> DAP mux */
	{"DAP", NULL, "DAP Mux"},		/* DAP mux --> dap */

	{"DAP MIX Mux", "ADC", "ADC"},		/* adc --> DAP MIX mux */
	{"DAP MIX Mux", NULL, "AIFIN"},		/* i2s --> DAP MIX mux */
	{"DAP", NULL, "DAP MIX Mux"},		/* DAP MIX mux --> dap */

	{"Digital Input Mux", "ADC", "ADC"},	/* adc --> audio mux */
	{"Digital Input Mux", NULL, "AIFIN"},	/* i2s --> audio mux */
	{"Digital Input Mux", NULL, "DAP"},	/* dap --> audio mux */
	{"DAC", NULL, "Digital Input Mux"},	/* audio mux --> dac */

	{"Headphone Mux", "DAC", "DAC"},	/* dac --> hp_mux */
	{"LO", NULL, "DAC"},			/* dac --> line_out */

	{"Headphone Mux", "LINE_IN", "LINE_IN"},/* line_in --> hp_mux */
	{"HP", NULL, "Headphone Mux"},		/* hp_mux --> hp */

	{"LINE_OUT", NULL, "LO"},
	{"HP_OUT", NULL, "HP"},
};

/* custom function to fetch info of PCM playback volume */
static int dac_info_volsw(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0xfc - 0x3c;
	return 0;
}

/*
 * custom function to get of PCM playback volume
 *
 * dac volume register
 * 15-------------8-7--------------0
 * | R channel vol | L channel vol |
 *  -------------------------------
 *
 * PCM volume with 0.5017 dB steps from 0 to -90 dB
 *
 * register values map to dB
 * 0x3B and less = Reserved
 * 0x3C = 0 dB
 * 0x3D = -0.5 dB
 * 0xF0 = -90 dB
 * 0xFC and greater = Muted
 *
 * register value map to userspace value
 *
 * register value	0x3c(0dB)	  0xf0(-90dB)0xfc
 *			------------------------------
 * userspace value	0xc0			     0
 */
static int dac_get_volsw(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int reg;
	int l;
	int r;

	reg = snd_soc_component_read(component, SGTL5000_CHIP_DAC_VOL);

	/* get left channel volume */
	l = (reg & SGTL5000_DAC_VOL_LEFT_MASK) >> SGTL5000_DAC_VOL_LEFT_SHIFT;

	/* get right channel volume */
	r = (reg & SGTL5000_DAC_VOL_RIGHT_MASK) >> SGTL5000_DAC_VOL_RIGHT_SHIFT;

	/* make sure value fall in (0x3c,0xfc) */
	l = clamp(l, 0x3c, 0xfc);
	r = clamp(r, 0x3c, 0xfc);

	/* invert it and map to userspace value */
	l = 0xfc - l;
	r = 0xfc - r;

	ucontrol->value.integer.value[0] = l;
	ucontrol->value.integer.value[1] = r;

	return 0;
}

/*
 * custom function to put of PCM playback volume
 *
 * dac volume register
 * 15-------------8-7--------------0
 * | R channel vol | L channel vol |
 *  -------------------------------
 *
 * PCM volume with 0.5017 dB steps from 0 to -90 dB
 *
 * register values map to dB
 * 0x3B and less = Reserved
 * 0x3C = 0 dB
 * 0x3D = -0.5 dB
 * 0xF0 = -90 dB
 * 0xFC and greater = Muted
 *
 * userspace value map to register value
 *
 * userspace value	0xc0			     0
 *			------------------------------
 * register value	0x3c(0dB)	0xf0(-90dB)0xfc
 */
static int dac_put_volsw(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int reg;
	int l;
	int r;

	l = ucontrol->value.integer.value[0];
	r = ucontrol->value.integer.value[1];

	/* make sure userspace volume fall in (0, 0xfc-0x3c) */
	l = clamp(l, 0, 0xfc - 0x3c);
	r = clamp(r, 0, 0xfc - 0x3c);

	/* invert it, get the value can be set to register */
	l = 0xfc - l;
	r = 0xfc - r;

	/* shift to get the register value */
	reg = l << SGTL5000_DAC_VOL_LEFT_SHIFT |
		r << SGTL5000_DAC_VOL_RIGHT_SHIFT;

	snd_soc_component_write(component, SGTL5000_CHIP_DAC_VOL, reg);

	return 0;
}

/*
 * custom function to get AVC threshold
 *
 * The threshold dB is calculated by rearranging the calculation from the
 * avc_put_threshold function: register_value = 10^(dB/20) * 0.636 * 2^15 ==>
 * dB = ( fls(register_value) - 14.347 ) * 6.02
 *
 * As this calculation is expensive and the threshold dB values may not exceed
 * 0 to 96 we use pre-calculated values.
 */
static int avc_get_threshold(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int db, i;
	u16 reg = snd_soc_component_read(component, SGTL5000_DAP_AVC_THRESHOLD);

	/* register value 0 => -96dB */
	if (!reg) {
		ucontrol->value.integer.value[0] = 96;
		ucontrol->value.integer.value[1] = 96;
		return 0;
	}

	/* get dB from register value (rounded down) */
	for (i = 0; avc_thr_db2reg[i] > reg; i++)
		;
	db = i;

	ucontrol->value.integer.value[0] = db;
	ucontrol->value.integer.value[1] = db;

	return 0;
}

/*
 * custom function to put AVC threshold
 *
 * The register value is calculated by following formula:
 *                                    register_value = 10^(dB/20) * 0.636 * 2^15
 * As this calculation is expensive and the threshold dB values may not exceed
 * 0 to 96 we use pre-calculated values.
 */
static int avc_put_threshold(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int db;
	u16 reg;

	db = (int)ucontrol->value.integer.value[0];
	if (db < 0 || db > 96)
		return -EINVAL;
	reg = avc_thr_db2reg[db];
	snd_soc_component_write(component, SGTL5000_DAP_AVC_THRESHOLD, reg);

	return 0;
}

static const DECLARE_TLV_DB_SCALE(capture_6db_attenuate, -600, 600, 0);

/* tlv for mic gain, 0db 20db 30db 40db */
static const DECLARE_TLV_DB_RANGE(mic_gain_tlv,
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 3, TLV_DB_SCALE_ITEM(2000, 1000, 0)
);

/* tlv for DAP channels, 0% - 100% - 200% */
static const DECLARE_TLV_DB_SCALE(dap_volume, 0, 1, 0);

/* tlv for bass bands, -11.75db to 12.0db, step .25db */
static const DECLARE_TLV_DB_SCALE(bass_band, -1175, 25, 0);

/* tlv for hp volume, -51.5db to 12.0db, step .5db */
static const DECLARE_TLV_DB_SCALE(headphone_volume, -5150, 50, 0);

/* tlv for lineout volume, 31 steps of .5db each */
static const DECLARE_TLV_DB_SCALE(lineout_volume, -1550, 50, 0);

/* tlv for dap avc max gain, 0db, 6db, 12db */
static const DECLARE_TLV_DB_SCALE(avc_max_gain, 0, 600, 0);

/* tlv for dap avc threshold, */
static const DECLARE_TLV_DB_MINMAX(avc_threshold, 0, 9600);

static const struct snd_kcontrol_new sgtl5000_snd_controls[] = {
	/* SOC_DOUBLE_S8_TLV with invert */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "PCM Playback Volume",
		.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |
			SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dac_info_volsw,
		.get = dac_get_volsw,
		.put = dac_put_volsw,
	},

	SOC_DOUBLE("Capture Volume", SGTL5000_CHIP_ANA_ADC_CTRL, 0, 4, 0xf, 0),
	SOC_SINGLE_TLV("Capture Attenuate Switch (-6dB)",
			SGTL5000_CHIP_ANA_ADC_CTRL,
			8, 1, 0, capture_6db_attenuate),
	SOC_SINGLE("Capture ZC Switch", SGTL5000_CHIP_ANA_CTRL, 1, 1, 0),
	SOC_SINGLE("Capture Switch", SGTL5000_CHIP_ANA_CTRL, 0, 1, 1),

	SOC_DOUBLE_TLV("Headphone Playback Volume",
			SGTL5000_CHIP_ANA_HP_CTRL,
			0, 8,
			0x7f, 1,
			headphone_volume),
	SOC_SINGLE("Headphone Playback Switch", SGTL5000_CHIP_ANA_CTRL,
			4, 1, 1),
	SOC_SINGLE("Headphone Playback ZC Switch", SGTL5000_CHIP_ANA_CTRL,
			5, 1, 0),

	SOC_SINGLE_TLV("Mic Volume", SGTL5000_CHIP_MIC_CTRL,
			0, 3, 0, mic_gain_tlv),

	SOC_DOUBLE_TLV("Lineout Playback Volume",
			SGTL5000_CHIP_LINE_OUT_VOL,
			SGTL5000_LINE_OUT_VOL_LEFT_SHIFT,
			SGTL5000_LINE_OUT_VOL_RIGHT_SHIFT,
			0x1f, 1,
			lineout_volume),
	SOC_SINGLE("Lineout Playback Switch", SGTL5000_CHIP_ANA_CTRL, 8, 1, 1),

	SOC_SINGLE_TLV("DAP Main channel", SGTL5000_DAP_MAIN_CHAN,
	0, 0xffff, 0, dap_volume),

	SOC_SINGLE_TLV("DAP Mix channel", SGTL5000_DAP_MIX_CHAN,
	0, 0xffff, 0, dap_volume),
	/* Automatic Volume Control (DAP AVC) */
	SOC_SINGLE("AVC Switch", SGTL5000_DAP_AVC_CTRL, 0, 1, 0),
	SOC_SINGLE("AVC Hard Limiter Switch", SGTL5000_DAP_AVC_CTRL, 5, 1, 0),
	SOC_SINGLE_TLV("AVC Max Gain Volume", SGTL5000_DAP_AVC_CTRL, 12, 2, 0,
			avc_max_gain),
	SOC_SINGLE("AVC Integrator Response", SGTL5000_DAP_AVC_CTRL, 8, 3, 0),
	SOC_SINGLE_EXT_TLV("AVC Threshold Volume", SGTL5000_DAP_AVC_THRESHOLD,
			0, 96, 0, avc_get_threshold, avc_put_threshold,
			avc_threshold),

	SOC_SINGLE_TLV("BASS 0", SGTL5000_DAP_EQ_BASS_BAND0,
	0, 0x5F, 0, bass_band),

	SOC_SINGLE_TLV("BASS 1", SGTL5000_DAP_EQ_BASS_BAND1,
	0, 0x5F, 0, bass_band),

	SOC_SINGLE_TLV("BASS 2", SGTL5000_DAP_EQ_BASS_BAND2,
	0, 0x5F, 0, bass_band),

	SOC_SINGLE_TLV("BASS 3", SGTL5000_DAP_EQ_BASS_BAND3,
	0, 0x5F, 0, bass_band),

	SOC_SINGLE_TLV("BASS 4", SGTL5000_DAP_EQ_BASS_BAND4,
	0, 0x5F, 0, bass_band),
};

/* mute the codec used by alsa core */
static int sgtl5000_mute_stream(struct snd_soc_dai *codec_dai, int mute, int direction)
{
	struct snd_soc_component *component = codec_dai->component;
	u16 i2s_pwr = SGTL5000_I2S_IN_POWERUP;

	/*
	 * During 'digital mute' do not mute DAC
	 * because LINE_IN would be muted aswell. We want to mute
	 * only I2S block - this can be done by powering it off
	 */
	snd_soc_component_update_bits(component, SGTL5000_CHIP_DIG_POWER,
			i2s_pwr, mute ? 0 : i2s_pwr);

	return 0;
}

/* set codec format */
static int sgtl5000_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct sgtl5000_priv *sgtl5000 = snd_soc_component_get_drvdata(component);
	u16 i2sctl = 0;

	sgtl5000->master = 0;
	/*
	 * i2s clock and frame master setting.
	 * ONLY support:
	 *  - clock and frame slave,
	 *  - clock and frame master
	 */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		i2sctl |= SGTL5000_I2S_MASTER;
		sgtl5000->master = 1;
		break;
	default:
		return -EINVAL;
	}

	/* setting i2s data format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		i2sctl |= SGTL5000_I2S_MODE_PCM << SGTL5000_I2S_MODE_SHIFT;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		i2sctl |= SGTL5000_I2S_MODE_PCM << SGTL5000_I2S_MODE_SHIFT;
		i2sctl |= SGTL5000_I2S_LRALIGN;
		break;
	case SND_SOC_DAIFMT_I2S:
		i2sctl |= SGTL5000_I2S_MODE_I2S_LJ << SGTL5000_I2S_MODE_SHIFT;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		i2sctl |= SGTL5000_I2S_MODE_RJ << SGTL5000_I2S_MODE_SHIFT;
		i2sctl |= SGTL5000_I2S_LRPOL;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		i2sctl |= SGTL5000_I2S_MODE_I2S_LJ << SGTL5000_I2S_MODE_SHIFT;
		i2sctl |= SGTL5000_I2S_LRALIGN;
		break;
	default:
		return -EINVAL;
	}

	sgtl5000->fmt = fmt & SND_SOC_DAIFMT_FORMAT_MASK;

	/* Clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		i2sctl |= SGTL5000_I2S_SCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_write(component, SGTL5000_CHIP_I2S_CTRL, i2sctl);

	return 0;
}

/* set codec sysclk */
static int sgtl5000_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				   int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct sgtl5000_priv *sgtl5000 = snd_soc_component_get_drvdata(component);

	switch (clk_id) {
	case SGTL5000_SYSCLK:
		sgtl5000->sysclk = freq;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * set clock according to i2s frame clock,
 * sgtl5000 provides 2 clock sources:
 * 1. sys_mclk: sample freq can only be configured to
 *	1/256, 1/384, 1/512 of sys_mclk.
 * 2. pll: can derive any audio clocks.
 *
 * clock setting rules:
 * 1. in slave mode, only sys_mclk can be used
 * 2. as constraint by sys_mclk, sample freq should be set to 32 kHz, 44.1 kHz
 * and above.
 * 3. usage of sys_mclk is preferred over pll to save power.
 */
static int sgtl5000_set_clock(struct snd_soc_component *component, int frame_rate)
{
	struct sgtl5000_priv *sgtl5000 = snd_soc_component_get_drvdata(component);
	int clk_ctl = 0;
	int sys_fs;	/* sample freq */

	/*
	 * sample freq should be divided by frame clock,
	 * if frame clock is lower than 44.1 kHz, sample freq should be set to
	 * 32 kHz or 44.1 kHz.
	 */
	switch (frame_rate) {
	case 8000:
	case 16000:
		sys_fs = 32000;
		break;
	case 11025:
	case 22050:
		sys_fs = 44100;
		break;
	default:
		sys_fs = frame_rate;
		break;
	}

	/* set divided factor of frame clock */
	switch (sys_fs / frame_rate) {
	case 4:
		clk_ctl |= SGTL5000_RATE_MODE_DIV_4 << SGTL5000_RATE_MODE_SHIFT;
		break;
	case 2:
		clk_ctl |= SGTL5000_RATE_MODE_DIV_2 << SGTL5000_RATE_MODE_SHIFT;
		break;
	case 1:
		clk_ctl |= SGTL5000_RATE_MODE_DIV_1 << SGTL5000_RATE_MODE_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	/* set the sys_fs according to frame rate */
	switch (sys_fs) {
	case 32000:
		clk_ctl |= SGTL5000_SYS_FS_32k << SGTL5000_SYS_FS_SHIFT;
		break;
	case 44100:
		clk_ctl |= SGTL5000_SYS_FS_44_1k << SGTL5000_SYS_FS_SHIFT;
		break;
	case 48000:
		clk_ctl |= SGTL5000_SYS_FS_48k << SGTL5000_SYS_FS_SHIFT;
		break;
	case 96000:
		clk_ctl |= SGTL5000_SYS_FS_96k << SGTL5000_SYS_FS_SHIFT;
		break;
	default:
		dev_err(component->dev, "frame rate %d not supported\n",
			frame_rate);
		return -EINVAL;
	}

	/*
	 * calculate the divider of mclk/sample_freq,
	 * factor of freq = 96 kHz can only be 256, since mclk is in the range
	 * of 8 MHz - 27 MHz
	 */
	switch (sgtl5000->sysclk / frame_rate) {
	case 256:
		clk_ctl |= SGTL5000_MCLK_FREQ_256FS <<
			SGTL5000_MCLK_FREQ_SHIFT;
		break;
	case 384:
		clk_ctl |= SGTL5000_MCLK_FREQ_384FS <<
			SGTL5000_MCLK_FREQ_SHIFT;
		break;
	case 512:
		clk_ctl |= SGTL5000_MCLK_FREQ_512FS <<
			SGTL5000_MCLK_FREQ_SHIFT;
		break;
	default:
		/* if mclk does not satisfy the divider, use pll */
		if (sgtl5000->master) {
			clk_ctl |= SGTL5000_MCLK_FREQ_PLL <<
				SGTL5000_MCLK_FREQ_SHIFT;
		} else {
			dev_err(component->dev,
				"PLL not supported in slave mode\n");
			dev_err(component->dev, "%d ratio is not supported. "
				"SYS_MCLK needs to be 256, 384 or 512 * fs\n",
				sgtl5000->sysclk / frame_rate);
			return -EINVAL;
		}
	}

	/* if using pll, please check manual 6.4.2 for detail */
	if ((clk_ctl & SGTL5000_MCLK_FREQ_MASK) == SGTL5000_MCLK_FREQ_PLL) {
		u64 out, t;
		int div2;
		int pll_ctl;
		unsigned int in, int_div, frac_div;

		if (sgtl5000->sysclk > 17000000) {
			div2 = 1;
			in = sgtl5000->sysclk / 2;
		} else {
			div2 = 0;
			in = sgtl5000->sysclk;
		}
		if (sys_fs == 44100)
			out = 180633600;
		else
			out = 196608000;
		t = do_div(out, in);
		int_div = out;
		t *= 2048;
		do_div(t, in);
		frac_div = t;
		pll_ctl = int_div << SGTL5000_PLL_INT_DIV_SHIFT |
		    frac_div << SGTL5000_PLL_FRAC_DIV_SHIFT;

		snd_soc_component_write(component, SGTL5000_CHIP_PLL_CTRL, pll_ctl);
		if (div2)
			snd_soc_component_update_bits(component,
				SGTL5000_CHIP_CLK_TOP_CTRL,
				SGTL5000_INPUT_FREQ_DIV2,
				SGTL5000_INPUT_FREQ_DIV2);
		else
			snd_soc_component_update_bits(component,
				SGTL5000_CHIP_CLK_TOP_CTRL,
				SGTL5000_INPUT_FREQ_DIV2,
				0);

		/* power up pll */
		snd_soc_component_update_bits(component, SGTL5000_CHIP_ANA_POWER,
			SGTL5000_PLL_POWERUP | SGTL5000_VCOAMP_POWERUP,
			SGTL5000_PLL_POWERUP | SGTL5000_VCOAMP_POWERUP);

		/* if using pll, clk_ctrl must be set after pll power up */
		snd_soc_component_write(component, SGTL5000_CHIP_CLK_CTRL, clk_ctl);
	} else {
		/* otherwise, clk_ctrl must be set before pll power down */
		snd_soc_component_write(component, SGTL5000_CHIP_CLK_CTRL, clk_ctl);

		/* power down pll */
		snd_soc_component_update_bits(component, SGTL5000_CHIP_ANA_POWER,
			SGTL5000_PLL_POWERUP | SGTL5000_VCOAMP_POWERUP,
			0);
	}

	return 0;
}

/*
 * Set PCM DAI bit size and sample rate.
 * input: params_rate, params_fmt
 */
static int sgtl5000_pcm_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct sgtl5000_priv *sgtl5000 = snd_soc_component_get_drvdata(component);
	int channels = params_channels(params);
	int i2s_ctl = 0;
	int stereo;
	int ret;

	/* sysclk should already set */
	if (!sgtl5000->sysclk) {
		dev_err(component->dev, "%s: set sysclk first!\n", __func__);
		return -EFAULT;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		stereo = SGTL5000_DAC_STEREO;
	else
		stereo = SGTL5000_ADC_STEREO;

	/* set mono to save power */
	snd_soc_component_update_bits(component, SGTL5000_CHIP_ANA_POWER, stereo,
			channels == 1 ? 0 : stereo);

	/* set codec clock base on lrclk */
	ret = sgtl5000_set_clock(component, params_rate(params));
	if (ret)
		return ret;

	/* set i2s data format */
	switch (params_width(params)) {
	case 16:
		if (sgtl5000->fmt == SND_SOC_DAIFMT_RIGHT_J)
			return -EINVAL;
		i2s_ctl |= SGTL5000_I2S_DLEN_16 << SGTL5000_I2S_DLEN_SHIFT;
		i2s_ctl |= SGTL5000_I2S_SCLKFREQ_32FS <<
		    SGTL5000_I2S_SCLKFREQ_SHIFT;
		break;
	case 20:
		i2s_ctl |= SGTL5000_I2S_DLEN_20 << SGTL5000_I2S_DLEN_SHIFT;
		i2s_ctl |= SGTL5000_I2S_SCLKFREQ_64FS <<
		    SGTL5000_I2S_SCLKFREQ_SHIFT;
		break;
	case 24:
		i2s_ctl |= SGTL5000_I2S_DLEN_24 << SGTL5000_I2S_DLEN_SHIFT;
		i2s_ctl |= SGTL5000_I2S_SCLKFREQ_64FS <<
		    SGTL5000_I2S_SCLKFREQ_SHIFT;
		break;
	case 32:
		if (sgtl5000->fmt == SND_SOC_DAIFMT_RIGHT_J)
			return -EINVAL;
		i2s_ctl |= SGTL5000_I2S_DLEN_32 << SGTL5000_I2S_DLEN_SHIFT;
		i2s_ctl |= SGTL5000_I2S_SCLKFREQ_64FS <<
		    SGTL5000_I2S_SCLKFREQ_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, SGTL5000_CHIP_I2S_CTRL,
			    SGTL5000_I2S_DLEN_MASK | SGTL5000_I2S_SCLKFREQ_MASK,
			    i2s_ctl);

	return 0;
}

/*
 * set dac bias
 * common state changes:
 * startup:
 * off --> standby --> prepare --> on
 * standby --> prepare --> on
 *
 * stop:
 * on --> prepare --> standby
 */
static int sgtl5000_set_bias_level(struct snd_soc_component *component,
				   enum snd_soc_bias_level level)
{
	struct sgtl5000_priv *sgtl = snd_soc_component_get_drvdata(component);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
	case SND_SOC_BIAS_STANDBY:
		regcache_cache_only(sgtl->regmap, false);
		ret = regcache_sync(sgtl->regmap);
		if (ret) {
			regcache_cache_only(sgtl->regmap, true);
			return ret;
		}

		snd_soc_component_update_bits(component, SGTL5000_CHIP_ANA_POWER,
				    SGTL5000_REFTOP_POWERUP,
				    SGTL5000_REFTOP_POWERUP);
		break;
	case SND_SOC_BIAS_OFF:
		regcache_cache_only(sgtl->regmap, true);
		snd_soc_component_update_bits(component, SGTL5000_CHIP_ANA_POWER,
				    SGTL5000_REFTOP_POWERUP, 0);
		break;
	}

	return 0;
}

#define SGTL5000_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops sgtl5000_ops = {
	.hw_params = sgtl5000_pcm_hw_params,
	.mute_stream = sgtl5000_mute_stream,
	.set_fmt = sgtl5000_set_dai_fmt,
	.set_sysclk = sgtl5000_set_dai_sysclk,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver sgtl5000_dai = {
	.name = "sgtl5000",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		/*
		 * only support 8~48K + 96K,
		 * TODO modify hw_param to support more
		 */
		.rates = SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_96000,
		.formats = SGTL5000_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_96000,
		.formats = SGTL5000_FORMATS,
	},
	.ops = &sgtl5000_ops,
	.symmetric_rate = 1,
};

static bool sgtl5000_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SGTL5000_CHIP_ID:
	case SGTL5000_CHIP_ADCDAC_CTRL:
	case SGTL5000_CHIP_ANA_STATUS:
		return true;
	}

	return false;
}

static bool sgtl5000_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SGTL5000_CHIP_ID:
	case SGTL5000_CHIP_DIG_POWER:
	case SGTL5000_CHIP_CLK_CTRL:
	case SGTL5000_CHIP_I2S_CTRL:
	case SGTL5000_CHIP_SSS_CTRL:
	case SGTL5000_CHIP_ADCDAC_CTRL:
	case SGTL5000_CHIP_DAC_VOL:
	case SGTL5000_CHIP_PAD_STRENGTH:
	case SGTL5000_CHIP_ANA_ADC_CTRL:
	case SGTL5000_CHIP_ANA_HP_CTRL:
	case SGTL5000_CHIP_ANA_CTRL:
	case SGTL5000_CHIP_LINREG_CTRL:
	case SGTL5000_CHIP_REF_CTRL:
	case SGTL5000_CHIP_MIC_CTRL:
	case SGTL5000_CHIP_LINE_OUT_CTRL:
	case SGTL5000_CHIP_LINE_OUT_VOL:
	case SGTL5000_CHIP_ANA_POWER:
	case SGTL5000_CHIP_PLL_CTRL:
	case SGTL5000_CHIP_CLK_TOP_CTRL:
	case SGTL5000_CHIP_ANA_STATUS:
	case SGTL5000_CHIP_SHORT_CTRL:
	case SGTL5000_CHIP_ANA_TEST2:
	case SGTL5000_DAP_CTRL:
	case SGTL5000_DAP_PEQ:
	case SGTL5000_DAP_BASS_ENHANCE:
	case SGTL5000_DAP_BASS_ENHANCE_CTRL:
	case SGTL5000_DAP_AUDIO_EQ:
	case SGTL5000_DAP_SURROUND:
	case SGTL5000_DAP_FLT_COEF_ACCESS:
	case SGTL5000_DAP_COEF_WR_B0_MSB:
	case SGTL5000_DAP_COEF_WR_B0_LSB:
	case SGTL5000_DAP_EQ_BASS_BAND0:
	case SGTL5000_DAP_EQ_BASS_BAND1:
	case SGTL5000_DAP_EQ_BASS_BAND2:
	case SGTL5000_DAP_EQ_BASS_BAND3:
	case SGTL5000_DAP_EQ_BASS_BAND4:
	case SGTL5000_DAP_MAIN_CHAN:
	case SGTL5000_DAP_MIX_CHAN:
	case SGTL5000_DAP_AVC_CTRL:
	case SGTL5000_DAP_AVC_THRESHOLD:
	case SGTL5000_DAP_AVC_ATTACK:
	case SGTL5000_DAP_AVC_DECAY:
	case SGTL5000_DAP_COEF_WR_B1_MSB:
	case SGTL5000_DAP_COEF_WR_B1_LSB:
	case SGTL5000_DAP_COEF_WR_B2_MSB:
	case SGTL5000_DAP_COEF_WR_B2_LSB:
	case SGTL5000_DAP_COEF_WR_A1_MSB:
	case SGTL5000_DAP_COEF_WR_A1_LSB:
	case SGTL5000_DAP_COEF_WR_A2_MSB:
	case SGTL5000_DAP_COEF_WR_A2_LSB:
		return true;

	default:
		return false;
	}
}

/*
 * This precalculated table contains all (vag_val * 100 / lo_calcntrl) results
 * to select an appropriate lo_vol_* in SGTL5000_CHIP_LINE_OUT_VOL
 * The calculatation was done for all possible register values which
 * is the array index and the following formula: 10^((idx−15)/40) * 100
 */
static const u8 vol_quot_table[] = {
	42, 45, 47, 50, 53, 56, 60, 63,
	67, 71, 75, 79, 84, 89, 94, 100,
	106, 112, 119, 126, 133, 141, 150, 158,
	168, 178, 188, 200, 211, 224, 237, 251
};

/*
 * sgtl5000 has 3 internal power supplies:
 * 1. VAG, normally set to vdda/2
 * 2. charge pump, set to different value
 *	according to voltage of vdda and vddio
 * 3. line out VAG, normally set to vddio/2
 *
 * and should be set according to:
 * 1. vddd provided by external or not
 * 2. vdda and vddio voltage value. > 3.1v or not
 */
static int sgtl5000_set_power_regs(struct snd_soc_component *component)
{
	int vddd;
	int vdda;
	int vddio;
	u16 ana_pwr;
	u16 lreg_ctrl;
	int vag;
	int lo_vag;
	int vol_quot;
	int lo_vol;
	size_t i;
	struct sgtl5000_priv *sgtl5000 = snd_soc_component_get_drvdata(component);

	vdda  = regulator_get_voltage(sgtl5000->supplies[VDDA].consumer);
	vddio = regulator_get_voltage(sgtl5000->supplies[VDDIO].consumer);
	vddd  = (sgtl5000->num_supplies > VDDD)
		? regulator_get_voltage(sgtl5000->supplies[VDDD].consumer)
		: LDO_VOLTAGE;

	vdda  = vdda / 1000;
	vddio = vddio / 1000;
	vddd  = vddd / 1000;

	if (vdda <= 0 || vddio <= 0 || vddd < 0) {
		dev_err(component->dev, "regulator voltage not set correctly\n");

		return -EINVAL;
	}

	/* according to datasheet, maximum voltage of supplies */
	if (vdda > 3600 || vddio > 3600 || vddd > 1980) {
		dev_err(component->dev,
			"exceed max voltage vdda %dmV vddio %dmV vddd %dmV\n",
			vdda, vddio, vddd);

		return -EINVAL;
	}

	/* reset value */
	ana_pwr = snd_soc_component_read(component, SGTL5000_CHIP_ANA_POWER);
	ana_pwr |= SGTL5000_DAC_STEREO |
			SGTL5000_ADC_STEREO |
			SGTL5000_REFTOP_POWERUP;
	lreg_ctrl = snd_soc_component_read(component, SGTL5000_CHIP_LINREG_CTRL);

	if (vddio < 3100 && vdda < 3100) {
		/* enable internal oscillator used for charge pump */
		snd_soc_component_update_bits(component, SGTL5000_CHIP_CLK_TOP_CTRL,
					SGTL5000_INT_OSC_EN,
					SGTL5000_INT_OSC_EN);
		/* Enable VDDC charge pump */
		ana_pwr |= SGTL5000_VDDC_CHRGPMP_POWERUP;
	} else {
		ana_pwr &= ~SGTL5000_VDDC_CHRGPMP_POWERUP;
		/*
		 * if vddio == vdda the source of charge pump should be
		 * assigned manually to VDDIO
		 */
		if (regulator_is_equal(sgtl5000->supplies[VDDA].consumer,
				       sgtl5000->supplies[VDDIO].consumer)) {
			lreg_ctrl |= SGTL5000_VDDC_ASSN_OVRD;
			lreg_ctrl |= SGTL5000_VDDC_MAN_ASSN_VDDIO <<
				    SGTL5000_VDDC_MAN_ASSN_SHIFT;
		}
	}

	snd_soc_component_write(component, SGTL5000_CHIP_LINREG_CTRL, lreg_ctrl);

	snd_soc_component_write(component, SGTL5000_CHIP_ANA_POWER, ana_pwr);

	/*
	 * set ADC/DAC VAG to vdda / 2,
	 * should stay in range (0.8v, 1.575v)
	 */
	vag = vdda / 2;
	if (vag <= SGTL5000_ANA_GND_BASE)
		vag = 0;
	else if (vag >= SGTL5000_ANA_GND_BASE + SGTL5000_ANA_GND_STP *
		 (SGTL5000_ANA_GND_MASK >> SGTL5000_ANA_GND_SHIFT))
		vag = SGTL5000_ANA_GND_MASK >> SGTL5000_ANA_GND_SHIFT;
	else
		vag = (vag - SGTL5000_ANA_GND_BASE) / SGTL5000_ANA_GND_STP;

	snd_soc_component_update_bits(component, SGTL5000_CHIP_REF_CTRL,
			SGTL5000_ANA_GND_MASK, vag << SGTL5000_ANA_GND_SHIFT);

	/* set line out VAG to vddio / 2, in range (0.8v, 1.675v) */
	lo_vag = vddio / 2;
	if (lo_vag <= SGTL5000_LINE_OUT_GND_BASE)
		lo_vag = 0;
	else if (lo_vag >= SGTL5000_LINE_OUT_GND_BASE +
		SGTL5000_LINE_OUT_GND_STP * SGTL5000_LINE_OUT_GND_MAX)
		lo_vag = SGTL5000_LINE_OUT_GND_MAX;
	else
		lo_vag = (lo_vag - SGTL5000_LINE_OUT_GND_BASE) /
		    SGTL5000_LINE_OUT_GND_STP;

	snd_soc_component_update_bits(component, SGTL5000_CHIP_LINE_OUT_CTRL,
			SGTL5000_LINE_OUT_CURRENT_MASK |
			SGTL5000_LINE_OUT_GND_MASK,
			lo_vag << SGTL5000_LINE_OUT_GND_SHIFT |
			SGTL5000_LINE_OUT_CURRENT_360u <<
				SGTL5000_LINE_OUT_CURRENT_SHIFT);

	/*
	 * Set lineout output level in range (0..31)
	 * the same value is used for right and left channel
	 *
	 * Searching for a suitable index solving this formula:
	 * idx = 40 * log10(vag_val / lo_cagcntrl) + 15
	 */
	vol_quot = lo_vag ? (vag * 100) / lo_vag : 0;
	lo_vol = 0;
	for (i = 0; i < ARRAY_SIZE(vol_quot_table); i++) {
		if (vol_quot >= vol_quot_table[i])
			lo_vol = i;
		else
			break;
	}

	snd_soc_component_update_bits(component, SGTL5000_CHIP_LINE_OUT_VOL,
		SGTL5000_LINE_OUT_VOL_RIGHT_MASK |
		SGTL5000_LINE_OUT_VOL_LEFT_MASK,
		lo_vol << SGTL5000_LINE_OUT_VOL_RIGHT_SHIFT |
		lo_vol << SGTL5000_LINE_OUT_VOL_LEFT_SHIFT);

	return 0;
}

static int sgtl5000_enable_regulators(struct i2c_client *client)
{
	int ret;
	int i;
	int external_vddd = 0;
	struct regulator *vddd;
	struct sgtl5000_priv *sgtl5000 = i2c_get_clientdata(client);

	for (i = 0; i < ARRAY_SIZE(sgtl5000->supplies); i++)
		sgtl5000->supplies[i].supply = supply_names[i];

	vddd = regulator_get_optional(&client->dev, "VDDD");
	if (IS_ERR(vddd)) {
		/* See if it's just not registered yet */
		if (PTR_ERR(vddd) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	} else {
		external_vddd = 1;
		regulator_put(vddd);
	}

	sgtl5000->num_supplies = ARRAY_SIZE(sgtl5000->supplies)
				 - 1 + external_vddd;
	ret = regulator_bulk_get(&client->dev, sgtl5000->num_supplies,
				 sgtl5000->supplies);
	if (ret)
		return ret;

	ret = regulator_bulk_enable(sgtl5000->num_supplies,
				    sgtl5000->supplies);
	if (!ret)
		usleep_range(10, 20);
	else
		regulator_bulk_free(sgtl5000->num_supplies,
				    sgtl5000->supplies);

	return ret;
}

static int sgtl5000_probe(struct snd_soc_component *component)
{
	int ret;
	u16 reg;
	struct sgtl5000_priv *sgtl5000 = snd_soc_component_get_drvdata(component);
	unsigned int zcd_mask = SGTL5000_HP_ZCD_EN | SGTL5000_ADC_ZCD_EN;

	/* power up sgtl5000 */
	ret = sgtl5000_set_power_regs(component);
	if (ret)
		goto err;

	/* enable small pop, introduce 400ms delay in turning off */
	snd_soc_component_update_bits(component, SGTL5000_CHIP_REF_CTRL,
				SGTL5000_SMALL_POP, SGTL5000_SMALL_POP);

	/* disable short cut detector */
	snd_soc_component_write(component, SGTL5000_CHIP_SHORT_CTRL, 0);

	snd_soc_component_write(component, SGTL5000_CHIP_DIG_POWER,
			SGTL5000_ADC_EN | SGTL5000_DAC_EN);

	/* enable dac volume ramp by default */
	snd_soc_component_write(component, SGTL5000_CHIP_ADCDAC_CTRL,
			SGTL5000_DAC_VOL_RAMP_EN |
			SGTL5000_DAC_MUTE_RIGHT |
			SGTL5000_DAC_MUTE_LEFT);

	reg = ((sgtl5000->lrclk_strength) << SGTL5000_PAD_I2S_LRCLK_SHIFT |
	       (sgtl5000->sclk_strength) << SGTL5000_PAD_I2S_SCLK_SHIFT |
	       0x1f);
	snd_soc_component_write(component, SGTL5000_CHIP_PAD_STRENGTH, reg);

	snd_soc_component_update_bits(component, SGTL5000_CHIP_ANA_CTRL,
		zcd_mask, zcd_mask);

	snd_soc_component_update_bits(component, SGTL5000_CHIP_MIC_CTRL,
			SGTL5000_BIAS_R_MASK,
			sgtl5000->micbias_resistor << SGTL5000_BIAS_R_SHIFT);

	snd_soc_component_update_bits(component, SGTL5000_CHIP_MIC_CTRL,
			SGTL5000_BIAS_VOLT_MASK,
			sgtl5000->micbias_voltage << SGTL5000_BIAS_VOLT_SHIFT);
	/*
	 * enable DAP Graphic EQ
	 * TODO:
	 * Add control for changing between PEQ/Tone Control/GEQ
	 */
	snd_soc_component_write(component, SGTL5000_DAP_AUDIO_EQ, SGTL5000_DAP_SEL_GEQ);

	/* Unmute DAC after start */
	snd_soc_component_update_bits(component, SGTL5000_CHIP_ADCDAC_CTRL,
		SGTL5000_DAC_MUTE_LEFT | SGTL5000_DAC_MUTE_RIGHT, 0);

	return 0;

err:
	return ret;
}

static int sgtl5000_of_xlate_dai_id(struct snd_soc_component *component,
				    struct device_node *endpoint)
{
	/* return dai id 0, whatever the endpoint index */
	return 0;
}

static const struct snd_soc_component_driver sgtl5000_driver = {
	.probe			= sgtl5000_probe,
	.set_bias_level		= sgtl5000_set_bias_level,
	.controls		= sgtl5000_snd_controls,
	.num_controls		= ARRAY_SIZE(sgtl5000_snd_controls),
	.dapm_widgets		= sgtl5000_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sgtl5000_dapm_widgets),
	.dapm_routes		= sgtl5000_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(sgtl5000_dapm_routes),
	.of_xlate_dai_id	= sgtl5000_of_xlate_dai_id,
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config sgtl5000_regmap = {
	.reg_bits = 16,
	.val_bits = 16,
	.reg_stride = 2,

	.max_register = SGTL5000_MAX_REG_OFFSET,
	.volatile_reg = sgtl5000_volatile,
	.readable_reg = sgtl5000_readable,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = sgtl5000_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(sgtl5000_reg_defaults),
};

/*
 * Write all the default values from sgtl5000_reg_defaults[] array into the
 * sgtl5000 registers, to make sure we always start with the sane registers
 * values as stated in the datasheet.
 *
 * Since sgtl5000 does not have a reset line, nor a reset command in software,
 * we follow this approach to guarantee we always start from the default values
 * and avoid problems like, not being able to probe after an audio playback
 * followed by a system reset or a 'reboot' command in Linux
 */
static void sgtl5000_fill_defaults(struct i2c_client *client)
{
	struct sgtl5000_priv *sgtl5000 = i2c_get_clientdata(client);
	int i, ret, val, index;

	for (i = 0; i < ARRAY_SIZE(sgtl5000_reg_defaults); i++) {
		val = sgtl5000_reg_defaults[i].def;
		index = sgtl5000_reg_defaults[i].reg;
		ret = regmap_write(sgtl5000->regmap, index, val);
		if (ret)
			dev_err(&client->dev,
				"%s: error %d setting reg 0x%02x to 0x%04x\n",
				__func__, ret, index, val);
	}
}

static int sgtl5000_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct sgtl5000_priv *sgtl5000;
	int ret, reg, rev;
	struct device_node *np = client->dev.of_node;
	u32 value;
	u16 ana_pwr;

	sgtl5000 = devm_kzalloc(&client->dev, sizeof(*sgtl5000), GFP_KERNEL);
	if (!sgtl5000)
		return -ENOMEM;

	i2c_set_clientdata(client, sgtl5000);

	ret = sgtl5000_enable_regulators(client);
	if (ret)
		return ret;

	sgtl5000->regmap = devm_regmap_init_i2c(client, &sgtl5000_regmap);
	if (IS_ERR(sgtl5000->regmap)) {
		ret = PTR_ERR(sgtl5000->regmap);
		dev_err(&client->dev, "Failed to allocate regmap: %d\n", ret);
		goto disable_regs;
	}

	sgtl5000->mclk = devm_clk_get(&client->dev, NULL);
	if (IS_ERR(sgtl5000->mclk)) {
		ret = PTR_ERR(sgtl5000->mclk);
		/* Defer the probe to see if the clk will be provided later */
		if (ret == -ENOENT)
			ret = -EPROBE_DEFER;

		if (ret != -EPROBE_DEFER)
			dev_err(&client->dev, "Failed to get mclock: %d\n",
				ret);
		goto disable_regs;
	}

	ret = clk_prepare_enable(sgtl5000->mclk);
	if (ret) {
		dev_err(&client->dev, "Error enabling clock %d\n", ret);
		goto disable_regs;
	}

	/* Need 8 clocks before I2C accesses */
	udelay(1);

	/* read chip information */
	ret = regmap_read(sgtl5000->regmap, SGTL5000_CHIP_ID, &reg);
	if (ret) {
		dev_err(&client->dev, "Error reading chip id %d\n", ret);
		goto disable_clk;
	}

	if (((reg & SGTL5000_PARTID_MASK) >> SGTL5000_PARTID_SHIFT) !=
	    SGTL5000_PARTID_PART_ID) {
		dev_err(&client->dev,
			"Device with ID register %x is not a sgtl5000\n", reg);
		ret = -ENODEV;
		goto disable_clk;
	}

	rev = (reg & SGTL5000_REVID_MASK) >> SGTL5000_REVID_SHIFT;
	dev_info(&client->dev, "sgtl5000 revision 0x%x\n", rev);
	sgtl5000->revision = rev;

	/* reconfigure the clocks in case we're using the PLL */
	ret = regmap_write(sgtl5000->regmap,
			   SGTL5000_CHIP_CLK_CTRL,
			   SGTL5000_CHIP_CLK_CTRL_DEFAULT);
	if (ret)
		dev_err(&client->dev,
			"Error %d initializing CHIP_CLK_CTRL\n", ret);

	/* Mute everything to avoid pop from the following power-up */
	ret = regmap_write(sgtl5000->regmap, SGTL5000_CHIP_ANA_CTRL,
			   SGTL5000_CHIP_ANA_CTRL_DEFAULT);
	if (ret) {
		dev_err(&client->dev,
			"Error %d muting outputs via CHIP_ANA_CTRL\n", ret);
		goto disable_clk;
	}

	/*
	 * If VAG is powered-on (e.g. from previous boot), it would be disabled
	 * by the write to ANA_POWER in later steps of the probe code. This
	 * may create a loud pop even with all outputs muted. The proper way
	 * to circumvent this is disabling the bit first and waiting the proper
	 * cool-down time.
	 */
	ret = regmap_read(sgtl5000->regmap, SGTL5000_CHIP_ANA_POWER, &value);
	if (ret) {
		dev_err(&client->dev, "Failed to read ANA_POWER: %d\n", ret);
		goto disable_clk;
	}
	if (value & SGTL5000_VAG_POWERUP) {
		ret = regmap_update_bits(sgtl5000->regmap,
					 SGTL5000_CHIP_ANA_POWER,
					 SGTL5000_VAG_POWERUP,
					 0);
		if (ret) {
			dev_err(&client->dev, "Error %d disabling VAG\n", ret);
			goto disable_clk;
		}

		msleep(SGTL5000_VAG_POWERDOWN_DELAY);
	}

	/* Follow section 2.2.1.1 of AN3663 */
	ana_pwr = SGTL5000_ANA_POWER_DEFAULT;
	if (sgtl5000->num_supplies <= VDDD) {
		/* internal VDDD at 1.2V */
		ret = regmap_update_bits(sgtl5000->regmap,
					 SGTL5000_CHIP_LINREG_CTRL,
					 SGTL5000_LINREG_VDDD_MASK,
					 LINREG_VDDD);
		if (ret)
			dev_err(&client->dev,
				"Error %d setting LINREG_VDDD\n", ret);

		ana_pwr |= SGTL5000_LINEREG_D_POWERUP;
		dev_info(&client->dev,
			 "Using internal LDO instead of VDDD: check ER1 erratum\n");
	} else {
		/* using external LDO for VDDD
		 * Clear startup powerup and simple powerup
		 * bits to save power
		 */
		ana_pwr &= ~(SGTL5000_STARTUP_POWERUP
			     | SGTL5000_LINREG_SIMPLE_POWERUP);
		dev_dbg(&client->dev, "Using external VDDD\n");
	}
	ret = regmap_write(sgtl5000->regmap, SGTL5000_CHIP_ANA_POWER, ana_pwr);
	if (ret)
		dev_err(&client->dev,
			"Error %d setting CHIP_ANA_POWER to %04x\n",
			ret, ana_pwr);

	if (np) {
		if (!of_property_read_u32(np,
			"micbias-resistor-k-ohms", &value)) {
			switch (value) {
			case SGTL5000_MICBIAS_OFF:
				sgtl5000->micbias_resistor = 0;
				break;
			case SGTL5000_MICBIAS_2K:
				sgtl5000->micbias_resistor = 1;
				break;
			case SGTL5000_MICBIAS_4K:
				sgtl5000->micbias_resistor = 2;
				break;
			case SGTL5000_MICBIAS_8K:
				sgtl5000->micbias_resistor = 3;
				break;
			default:
				sgtl5000->micbias_resistor = 2;
				dev_err(&client->dev,
					"Unsuitable MicBias resistor\n");
			}
		} else {
			/* default is 4Kohms */
			sgtl5000->micbias_resistor = 2;
		}
		if (!of_property_read_u32(np,
			"micbias-voltage-m-volts", &value)) {
			/* 1250mV => 0 */
			/* steps of 250mV */
			if ((value >= 1250) && (value <= 3000))
				sgtl5000->micbias_voltage = (value / 250) - 5;
			else {
				sgtl5000->micbias_voltage = 0;
				dev_err(&client->dev,
					"Unsuitable MicBias voltage\n");
			}
		} else {
			sgtl5000->micbias_voltage = 0;
		}
	}

	sgtl5000->lrclk_strength = I2S_LRCLK_STRENGTH_LOW;
	if (!of_property_read_u32(np, "lrclk-strength", &value)) {
		if (value > I2S_LRCLK_STRENGTH_HIGH)
			value = I2S_LRCLK_STRENGTH_LOW;
		sgtl5000->lrclk_strength = value;
	}

	sgtl5000->sclk_strength = I2S_SCLK_STRENGTH_LOW;
	if (!of_property_read_u32(np, "sclk-strength", &value)) {
		if (value > I2S_SCLK_STRENGTH_HIGH)
			value = I2S_SCLK_STRENGTH_LOW;
		sgtl5000->sclk_strength = value;
	}

	/* Ensure sgtl5000 will start with sane register values */
	sgtl5000_fill_defaults(client);

	ret = devm_snd_soc_register_component(&client->dev,
			&sgtl5000_driver, &sgtl5000_dai, 1);
	if (ret)
		goto disable_clk;

	return 0;

disable_clk:
	clk_disable_unprepare(sgtl5000->mclk);

disable_regs:
	regulator_bulk_disable(sgtl5000->num_supplies, sgtl5000->supplies);
	regulator_bulk_free(sgtl5000->num_supplies, sgtl5000->supplies);

	return ret;
}

static int sgtl5000_i2c_remove(struct i2c_client *client)
{
	struct sgtl5000_priv *sgtl5000 = i2c_get_clientdata(client);

	regmap_write(sgtl5000->regmap, SGTL5000_CHIP_DIG_POWER, SGTL5000_DIG_POWER_DEFAULT);
	regmap_write(sgtl5000->regmap, SGTL5000_CHIP_ANA_POWER, SGTL5000_ANA_POWER_DEFAULT);

	clk_disable_unprepare(sgtl5000->mclk);
	regulator_bulk_disable(sgtl5000->num_supplies, sgtl5000->supplies);
	regulator_bulk_free(sgtl5000->num_supplies, sgtl5000->supplies);

	return 0;
}

static void sgtl5000_i2c_shutdown(struct i2c_client *client)
{
	sgtl5000_i2c_remove(client);
}

static const struct i2c_device_id sgtl5000_id[] = {
	{"sgtl5000", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, sgtl5000_id);

static const struct of_device_id sgtl5000_dt_ids[] = {
	{ .compatible = "fsl,sgtl5000", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sgtl5000_dt_ids);

static struct i2c_driver sgtl5000_i2c_driver = {
	.driver = {
		.name = "sgtl5000",
		.of_match_table = sgtl5000_dt_ids,
	},
	.probe = sgtl5000_i2c_probe,
	.remove = sgtl5000_i2c_remove,
	.shutdown = sgtl5000_i2c_shutdown,
	.id_table = sgtl5000_id,
};

module_i2c_driver(sgtl5000_i2c_driver);

MODULE_DESCRIPTION("Freescale SGTL5000 ALSA SoC Codec Driver");
MODULE_AUTHOR("Zeng Zhaoming <zengzm.kernel@gmail.com>");
MODULE_LICENSE("GPL");
