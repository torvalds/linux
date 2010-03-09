/*
 * wm8961.c  --  WM8961 ALSA SoC Audio driver
 *
 * Author: Mark Brown
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8961.h"

#define WM8961_MAX_REGISTER                     0xFC

static u16 wm8961_reg_defaults[] = {
	0x009F,     /* R0   - Left Input volume */
	0x009F,     /* R1   - Right Input volume */
	0x0000,     /* R2   - LOUT1 volume */
	0x0000,     /* R3   - ROUT1 volume */
	0x0020,     /* R4   - Clocking1 */
	0x0008,     /* R5   - ADC & DAC Control 1 */
	0x0000,     /* R6   - ADC & DAC Control 2 */
	0x000A,     /* R7   - Audio Interface 0 */
	0x01F4,     /* R8   - Clocking2 */
	0x0000,     /* R9   - Audio Interface 1 */
	0x00FF,     /* R10  - Left DAC volume */
	0x00FF,     /* R11  - Right DAC volume */
	0x0000,     /* R12 */
	0x0000,     /* R13 */
	0x0040,     /* R14  - Audio Interface 2 */
	0x0000,     /* R15  - Software Reset */
	0x0000,     /* R16 */
	0x007B,     /* R17  - ALC1 */
	0x0000,     /* R18  - ALC2 */
	0x0032,     /* R19  - ALC3 */
	0x0000,     /* R20  - Noise Gate */
	0x00C0,     /* R21  - Left ADC volume */
	0x00C0,     /* R22  - Right ADC volume */
	0x0120,     /* R23  - Additional control(1) */
	0x0000,     /* R24  - Additional control(2) */
	0x0000,     /* R25  - Pwr Mgmt (1) */
	0x0000,     /* R26  - Pwr Mgmt (2) */
	0x0000,     /* R27  - Additional Control (3) */
	0x0000,     /* R28  - Anti-pop */
	0x0000,     /* R29 */
	0x005F,     /* R30  - Clocking 3 */
	0x0000,     /* R31 */
	0x0000,     /* R32  - ADCL signal path */
	0x0000,     /* R33  - ADCR signal path */
	0x0000,     /* R34 */
	0x0000,     /* R35 */
	0x0000,     /* R36 */
	0x0000,     /* R37 */
	0x0000,     /* R38 */
	0x0000,     /* R39 */
	0x0000,     /* R40  - LOUT2 volume */
	0x0000,     /* R41  - ROUT2 volume */
	0x0000,     /* R42 */
	0x0000,     /* R43 */
	0x0000,     /* R44 */
	0x0000,     /* R45 */
	0x0000,     /* R46 */
	0x0000,     /* R47  - Pwr Mgmt (3) */
	0x0023,     /* R48  - Additional Control (4) */
	0x0000,     /* R49  - Class D Control 1 */
	0x0000,     /* R50 */
	0x0003,     /* R51  - Class D Control 2 */
	0x0000,     /* R52 */
	0x0000,     /* R53 */
	0x0000,     /* R54 */
	0x0000,     /* R55 */
	0x0106,     /* R56  - Clocking 4 */
	0x0000,     /* R57  - DSP Sidetone 0 */
	0x0000,     /* R58  - DSP Sidetone 1 */
	0x0000,     /* R59 */
	0x0000,     /* R60  - DC Servo 0 */
	0x0000,     /* R61  - DC Servo 1 */
	0x0000,     /* R62 */
	0x015E,     /* R63  - DC Servo 3 */
	0x0010,     /* R64 */
	0x0010,     /* R65  - DC Servo 5 */
	0x0000,     /* R66 */
	0x0001,     /* R67 */
	0x0003,     /* R68  - Analogue PGA Bias */
	0x0000,     /* R69  - Analogue HP 0 */
	0x0060,     /* R70 */
	0x01FB,     /* R71  - Analogue HP 2 */
	0x0000,     /* R72  - Charge Pump 1 */
	0x0065,     /* R73 */
	0x005F,     /* R74 */
	0x0059,     /* R75 */
	0x006B,     /* R76 */
	0x0038,     /* R77 */
	0x000C,     /* R78 */
	0x000A,     /* R79 */
	0x006B,     /* R80 */
	0x0000,     /* R81 */
	0x0000,     /* R82  - Charge Pump B */
	0x0087,     /* R83 */
	0x0000,     /* R84 */
	0x005C,     /* R85 */
	0x0000,     /* R86 */
	0x0000,     /* R87  - Write Sequencer 1 */
	0x0000,     /* R88  - Write Sequencer 2 */
	0x0000,     /* R89  - Write Sequencer 3 */
	0x0000,     /* R90  - Write Sequencer 4 */
	0x0000,     /* R91  - Write Sequencer 5 */
	0x0000,     /* R92  - Write Sequencer 6 */
	0x0000,     /* R93  - Write Sequencer 7 */
	0x0000,     /* R94 */
	0x0000,     /* R95 */
	0x0000,     /* R96 */
	0x0000,     /* R97 */
	0x0000,     /* R98 */
	0x0000,     /* R99 */
	0x0000,     /* R100 */
	0x0000,     /* R101 */
	0x0000,     /* R102 */
	0x0000,     /* R103 */
	0x0000,     /* R104 */
	0x0000,     /* R105 */
	0x0000,     /* R106 */
	0x0000,     /* R107 */
	0x0000,     /* R108 */
	0x0000,     /* R109 */
	0x0000,     /* R110 */
	0x0000,     /* R111 */
	0x0000,     /* R112 */
	0x0000,     /* R113 */
	0x0000,     /* R114 */
	0x0000,     /* R115 */
	0x0000,     /* R116 */
	0x0000,     /* R117 */
	0x0000,     /* R118 */
	0x0000,     /* R119 */
	0x0000,     /* R120 */
	0x0000,     /* R121 */
	0x0000,     /* R122 */
	0x0000,     /* R123 */
	0x0000,     /* R124 */
	0x0000,     /* R125 */
	0x0000,     /* R126 */
	0x0000,     /* R127 */
	0x0000,     /* R128 */
	0x0000,     /* R129 */
	0x0000,     /* R130 */
	0x0000,     /* R131 */
	0x0000,     /* R132 */
	0x0000,     /* R133 */
	0x0000,     /* R134 */
	0x0000,     /* R135 */
	0x0000,     /* R136 */
	0x0000,     /* R137 */
	0x0000,     /* R138 */
	0x0000,     /* R139 */
	0x0000,     /* R140 */
	0x0000,     /* R141 */
	0x0000,     /* R142 */
	0x0000,     /* R143 */
	0x0000,     /* R144 */
	0x0000,     /* R145 */
	0x0000,     /* R146 */
	0x0000,     /* R147 */
	0x0000,     /* R148 */
	0x0000,     /* R149 */
	0x0000,     /* R150 */
	0x0000,     /* R151 */
	0x0000,     /* R152 */
	0x0000,     /* R153 */
	0x0000,     /* R154 */
	0x0000,     /* R155 */
	0x0000,     /* R156 */
	0x0000,     /* R157 */
	0x0000,     /* R158 */
	0x0000,     /* R159 */
	0x0000,     /* R160 */
	0x0000,     /* R161 */
	0x0000,     /* R162 */
	0x0000,     /* R163 */
	0x0000,     /* R164 */
	0x0000,     /* R165 */
	0x0000,     /* R166 */
	0x0000,     /* R167 */
	0x0000,     /* R168 */
	0x0000,     /* R169 */
	0x0000,     /* R170 */
	0x0000,     /* R171 */
	0x0000,     /* R172 */
	0x0000,     /* R173 */
	0x0000,     /* R174 */
	0x0000,     /* R175 */
	0x0000,     /* R176 */
	0x0000,     /* R177 */
	0x0000,     /* R178 */
	0x0000,     /* R179 */
	0x0000,     /* R180 */
	0x0000,     /* R181 */
	0x0000,     /* R182 */
	0x0000,     /* R183 */
	0x0000,     /* R184 */
	0x0000,     /* R185 */
	0x0000,     /* R186 */
	0x0000,     /* R187 */
	0x0000,     /* R188 */
	0x0000,     /* R189 */
	0x0000,     /* R190 */
	0x0000,     /* R191 */
	0x0000,     /* R192 */
	0x0000,     /* R193 */
	0x0000,     /* R194 */
	0x0000,     /* R195 */
	0x0030,     /* R196 */
	0x0006,     /* R197 */
	0x0000,     /* R198 */
	0x0060,     /* R199 */
	0x0000,     /* R200 */
	0x003F,     /* R201 */
	0x0000,     /* R202 */
	0x0000,     /* R203 */
	0x0000,     /* R204 */
	0x0001,     /* R205 */
	0x0000,     /* R206 */
	0x0181,     /* R207 */
	0x0005,     /* R208 */
	0x0008,     /* R209 */
	0x0008,     /* R210 */
	0x0000,     /* R211 */
	0x013B,     /* R212 */
	0x0000,     /* R213 */
	0x0000,     /* R214 */
	0x0000,     /* R215 */
	0x0000,     /* R216 */
	0x0070,     /* R217 */
	0x0000,     /* R218 */
	0x0000,     /* R219 */
	0x0000,     /* R220 */
	0x0000,     /* R221 */
	0x0000,     /* R222 */
	0x0003,     /* R223 */
	0x0000,     /* R224 */
	0x0000,     /* R225 */
	0x0001,     /* R226 */
	0x0008,     /* R227 */
	0x0000,     /* R228 */
	0x0000,     /* R229 */
	0x0000,     /* R230 */
	0x0000,     /* R231 */
	0x0004,     /* R232 */
	0x0000,     /* R233 */
	0x0000,     /* R234 */
	0x0000,     /* R235 */
	0x0000,     /* R236 */
	0x0000,     /* R237 */
	0x0080,     /* R238 */
	0x0000,     /* R239 */
	0x0000,     /* R240 */
	0x0000,     /* R241 */
	0x0000,     /* R242 */
	0x0000,     /* R243 */
	0x0000,     /* R244 */
	0x0052,     /* R245 */
	0x0110,     /* R246 */
	0x0040,     /* R247 */
	0x0000,     /* R248 */
	0x0030,     /* R249 */
	0x0000,     /* R250 */
	0x0000,     /* R251 */
	0x0001,     /* R252 - General test 1 */
};

struct wm8961_priv {
	struct snd_soc_codec codec;
	int sysclk;
	u16 reg_cache[WM8961_MAX_REGISTER];
};

static int wm8961_volatile_register(unsigned int reg)
{
	switch (reg) {
	case WM8961_SOFTWARE_RESET:
	case WM8961_WRITE_SEQUENCER_7:
	case WM8961_DC_SERVO_1:
		return 1;

	default:
		return 0;
	}
}

static int wm8961_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, WM8961_SOFTWARE_RESET, 0);
}

/*
 * The headphone output supports special anti-pop sequences giving
 * silent power up and power down.
 */
static int wm8961_hp_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	u16 hp_reg = snd_soc_read(codec, WM8961_ANALOGUE_HP_0);
	u16 cp_reg = snd_soc_read(codec, WM8961_CHARGE_PUMP_1);
	u16 pwr_reg = snd_soc_read(codec, WM8961_PWR_MGMT_2);
	u16 dcs_reg = snd_soc_read(codec, WM8961_DC_SERVO_1);
	int timeout = 500;

	if (event & SND_SOC_DAPM_POST_PMU) {
		/* Make sure the output is shorted */
		hp_reg &= ~(WM8961_HPR_RMV_SHORT | WM8961_HPL_RMV_SHORT);
		snd_soc_write(codec, WM8961_ANALOGUE_HP_0, hp_reg);

		/* Enable the charge pump */
		cp_reg |= WM8961_CP_ENA;
		snd_soc_write(codec, WM8961_CHARGE_PUMP_1, cp_reg);
		mdelay(5);

		/* Enable the PGA */
		pwr_reg |= WM8961_LOUT1_PGA | WM8961_ROUT1_PGA;
		snd_soc_write(codec, WM8961_PWR_MGMT_2, pwr_reg);

		/* Enable the amplifier */
		hp_reg |= WM8961_HPR_ENA | WM8961_HPL_ENA;
		snd_soc_write(codec, WM8961_ANALOGUE_HP_0, hp_reg);

		/* Second stage enable */
		hp_reg |= WM8961_HPR_ENA_DLY | WM8961_HPL_ENA_DLY;
		snd_soc_write(codec, WM8961_ANALOGUE_HP_0, hp_reg);

		/* Enable the DC servo & trigger startup */
		dcs_reg |=
			WM8961_DCS_ENA_CHAN_HPR | WM8961_DCS_TRIG_STARTUP_HPR |
			WM8961_DCS_ENA_CHAN_HPL | WM8961_DCS_TRIG_STARTUP_HPL;
		dev_dbg(codec->dev, "Enabling DC servo\n");

		snd_soc_write(codec, WM8961_DC_SERVO_1, dcs_reg);
		do {
			msleep(1);
			dcs_reg = snd_soc_read(codec, WM8961_DC_SERVO_1);
		} while (--timeout &&
			 dcs_reg & (WM8961_DCS_TRIG_STARTUP_HPR |
				WM8961_DCS_TRIG_STARTUP_HPL));
		if (dcs_reg & (WM8961_DCS_TRIG_STARTUP_HPR |
			       WM8961_DCS_TRIG_STARTUP_HPL))
			dev_err(codec->dev, "DC servo timed out\n");
		else
			dev_dbg(codec->dev, "DC servo startup complete\n");

		/* Enable the output stage */
		hp_reg |= WM8961_HPR_ENA_OUTP | WM8961_HPL_ENA_OUTP;
		snd_soc_write(codec, WM8961_ANALOGUE_HP_0, hp_reg);

		/* Remove the short on the output stage */
		hp_reg |= WM8961_HPR_RMV_SHORT | WM8961_HPL_RMV_SHORT;
		snd_soc_write(codec, WM8961_ANALOGUE_HP_0, hp_reg);
	}

	if (event & SND_SOC_DAPM_PRE_PMD) {
		/* Short the output */
		hp_reg &= ~(WM8961_HPR_RMV_SHORT | WM8961_HPL_RMV_SHORT);
		snd_soc_write(codec, WM8961_ANALOGUE_HP_0, hp_reg);

		/* Disable the output stage */
		hp_reg &= ~(WM8961_HPR_ENA_OUTP | WM8961_HPL_ENA_OUTP);
		snd_soc_write(codec, WM8961_ANALOGUE_HP_0, hp_reg);

		/* Disable DC offset cancellation */
		dcs_reg &= ~(WM8961_DCS_ENA_CHAN_HPR |
			     WM8961_DCS_ENA_CHAN_HPL);
		snd_soc_write(codec, WM8961_DC_SERVO_1, dcs_reg);

		/* Finish up */
		hp_reg &= ~(WM8961_HPR_ENA_DLY | WM8961_HPR_ENA |
			    WM8961_HPL_ENA_DLY | WM8961_HPL_ENA);
		snd_soc_write(codec, WM8961_ANALOGUE_HP_0, hp_reg);

		/* Disable the PGA */
		pwr_reg &= ~(WM8961_LOUT1_PGA | WM8961_ROUT1_PGA);
		snd_soc_write(codec, WM8961_PWR_MGMT_2, pwr_reg);

		/* Disable the charge pump */
		dev_dbg(codec->dev, "Disabling charge pump\n");
		snd_soc_write(codec, WM8961_CHARGE_PUMP_1,
			     cp_reg & ~WM8961_CP_ENA);
	}

	return 0;
}

static int wm8961_spk_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	u16 pwr_reg = snd_soc_read(codec, WM8961_PWR_MGMT_2);
	u16 spk_reg = snd_soc_read(codec, WM8961_CLASS_D_CONTROL_1);

	if (event & SND_SOC_DAPM_POST_PMU) {
		/* Enable the PGA */
		pwr_reg |= WM8961_SPKL_PGA | WM8961_SPKR_PGA;
		snd_soc_write(codec, WM8961_PWR_MGMT_2, pwr_reg);

		/* Enable the amplifier */
		spk_reg |= WM8961_SPKL_ENA | WM8961_SPKR_ENA;
		snd_soc_write(codec, WM8961_CLASS_D_CONTROL_1, spk_reg);
	}

	if (event & SND_SOC_DAPM_PRE_PMD) {
		/* Enable the amplifier */
		spk_reg &= ~(WM8961_SPKL_ENA | WM8961_SPKR_ENA);
		snd_soc_write(codec, WM8961_CLASS_D_CONTROL_1, spk_reg);

		/* Enable the PGA */
		pwr_reg &= ~(WM8961_SPKL_PGA | WM8961_SPKR_PGA);
		snd_soc_write(codec, WM8961_PWR_MGMT_2, pwr_reg);
	}

	return 0;
}

static const char *adc_hpf_text[] = {
	"Hi-fi", "Voice 1", "Voice 2", "Voice 3",
};

static const struct soc_enum adc_hpf =
	SOC_ENUM_SINGLE(WM8961_ADC_DAC_CONTROL_2, 7, 4, adc_hpf_text);

static const char *dac_deemph_text[] = {
	"None", "32kHz", "44.1kHz", "48kHz",
};

static const struct soc_enum dac_deemph =
	SOC_ENUM_SINGLE(WM8961_ADC_DAC_CONTROL_1, 1, 4, dac_deemph_text);

static const DECLARE_TLV_DB_SCALE(out_tlv, -12100, 100, 1);
static const DECLARE_TLV_DB_SCALE(hp_sec_tlv, -700, 100, 0);
static const DECLARE_TLV_DB_SCALE(adc_tlv, -7200, 75, 1);
static const DECLARE_TLV_DB_SCALE(sidetone_tlv, -3600, 300, 0);
static unsigned int boost_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0, 0, TLV_DB_SCALE_ITEM(0,  0, 0),
	1, 1, TLV_DB_SCALE_ITEM(13, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(20, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(29, 0, 0),
};
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

static const struct soc_enum dacl_sidetone =
	SOC_ENUM_SINGLE(WM8961_DSP_SIDETONE_0, 2, 3, sidetone_text);

static const struct soc_enum dacr_sidetone =
	SOC_ENUM_SINGLE(WM8961_DSP_SIDETONE_1, 2, 3, sidetone_text);

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

SND_SOC_DAPM_MICBIAS("MICBIAS", WM8961_PWR_MGMT_1, 1, 0),

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
	struct snd_soc_codec *codec = dai->codec;
	struct wm8961_priv *wm8961 = codec->private_data;
	int i, best, target, fs;
	u16 reg;

	fs = params_rate(params);

	if (!wm8961->sysclk) {
		dev_err(codec->dev, "MCLK has not been specified\n");
		return -EINVAL;
	}

	/* Find the closest sample rate for the filters */
	best = 0;
	for (i = 0; i < ARRAY_SIZE(wm8961_srate); i++) {
		if (abs(wm8961_srate[i].rate - fs) <
		    abs(wm8961_srate[best].rate - fs))
			best = i;
	}
	reg = snd_soc_read(codec, WM8961_ADDITIONAL_CONTROL_3);
	reg &= ~WM8961_SAMPLE_RATE_MASK;
	reg |= wm8961_srate[best].val;
	snd_soc_write(codec, WM8961_ADDITIONAL_CONTROL_3, reg);
	dev_dbg(codec->dev, "Selected SRATE %dHz for %dHz\n",
		wm8961_srate[best].rate, fs);

	/* Select a CLK_SYS/fs ratio equal to or higher than required */
	target = wm8961->sysclk / fs;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK && target < 64) {
		dev_err(codec->dev,
			"SYSCLK must be at least 64*fs for DAC\n");
		return -EINVAL;
	}
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE && target < 256) {
		dev_err(codec->dev,
			"SYSCLK must be at least 256*fs for ADC\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(wm8961_clk_sys_ratio); i++) {
		if (wm8961_clk_sys_ratio[i].ratio >= target)
			break;
	}
	if (i == ARRAY_SIZE(wm8961_clk_sys_ratio)) {
		dev_err(codec->dev, "Unable to generate CLK_SYS_RATE\n");
		return -EINVAL;
	}
	dev_dbg(codec->dev, "Selected CLK_SYS_RATE of %d for %d/%d=%d\n",
		wm8961_clk_sys_ratio[i].ratio, wm8961->sysclk, fs,
		wm8961->sysclk / fs);

	reg = snd_soc_read(codec, WM8961_CLOCKING_4);
	reg &= ~WM8961_CLK_SYS_RATE_MASK;
	reg |= wm8961_clk_sys_ratio[i].val << WM8961_CLK_SYS_RATE_SHIFT;
	snd_soc_write(codec, WM8961_CLOCKING_4, reg);

	reg = snd_soc_read(codec, WM8961_AUDIO_INTERFACE_0);
	reg &= ~WM8961_WL_MASK;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		reg |= 1 << WM8961_WL_SHIFT;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		reg |= 2 << WM8961_WL_SHIFT;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		reg |= 3 << WM8961_WL_SHIFT;
		break;
	default:
		return -EINVAL;
	}
	snd_soc_write(codec, WM8961_AUDIO_INTERFACE_0, reg);

	/* Sloping stop-band filter is recommended for <= 24kHz */
	reg = snd_soc_read(codec, WM8961_ADC_DAC_CONTROL_2);
	if (fs <= 24000)
		reg |= WM8961_DACSLOPE;
	else
		reg &= WM8961_DACSLOPE;
	snd_soc_write(codec, WM8961_ADC_DAC_CONTROL_2, reg);

	return 0;
}

static int wm8961_set_sysclk(struct snd_soc_dai *dai, int clk_id,
			     unsigned int freq,
			     int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8961_priv *wm8961 = codec->private_data;
	u16 reg = snd_soc_read(codec, WM8961_CLOCKING1);

	if (freq > 33000000) {
		dev_err(codec->dev, "MCLK must be <33MHz\n");
		return -EINVAL;
	}

	if (freq > 16500000) {
		dev_dbg(codec->dev, "Using MCLK/2 for %dHz MCLK\n", freq);
		reg |= WM8961_MCLKDIV;
		freq /= 2;
	} else {
		dev_dbg(codec->dev, "Using MCLK/1 for %dHz MCLK\n", freq);
		reg &= WM8961_MCLKDIV;
	}

	snd_soc_write(codec, WM8961_CLOCKING1, reg);

	wm8961->sysclk = freq;

	return 0;
}

static int wm8961_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 aif = snd_soc_read(codec, WM8961_AUDIO_INTERFACE_0);

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

	return snd_soc_write(codec, WM8961_AUDIO_INTERFACE_0, aif);
}

static int wm8961_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 reg = snd_soc_read(codec, WM8961_ADDITIONAL_CONTROL_2);

	if (tristate)
		reg |= WM8961_TRIS;
	else
		reg &= ~WM8961_TRIS;

	return snd_soc_write(codec, WM8961_ADDITIONAL_CONTROL_2, reg);
}

static int wm8961_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 reg = snd_soc_read(codec, WM8961_ADC_DAC_CONTROL_1);

	if (mute)
		reg |= WM8961_DACMU;
	else
		reg &= ~WM8961_DACMU;

	msleep(17);

	return snd_soc_write(codec, WM8961_ADC_DAC_CONTROL_1, reg);
}

static int wm8961_set_clkdiv(struct snd_soc_dai *dai, int div_id, int div)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 reg;

	switch (div_id) {
	case WM8961_BCLK:
		reg = snd_soc_read(codec, WM8961_CLOCKING2);
		reg &= ~WM8961_BCLKDIV_MASK;
		reg |= div;
		snd_soc_write(codec, WM8961_CLOCKING2, reg);
		break;

	case WM8961_LRCLK:
		reg = snd_soc_read(codec, WM8961_AUDIO_INTERFACE_2);
		reg &= ~WM8961_LRCLK_RATE_MASK;
		reg |= div;
		snd_soc_write(codec, WM8961_AUDIO_INTERFACE_2, reg);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int wm8961_set_bias_level(struct snd_soc_codec *codec,
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
		if (codec->bias_level == SND_SOC_BIAS_STANDBY) {
			/* Enable bias generation */
			reg = snd_soc_read(codec, WM8961_ANTI_POP);
			reg |= WM8961_BUFIOEN | WM8961_BUFDCOPEN;
			snd_soc_write(codec, WM8961_ANTI_POP, reg);

			/* VMID=2*50k, VREF */
			reg = snd_soc_read(codec, WM8961_PWR_MGMT_1);
			reg &= ~WM8961_VMIDSEL_MASK;
			reg |= (1 << WM8961_VMIDSEL_SHIFT) | WM8961_VREF;
			snd_soc_write(codec, WM8961_PWR_MGMT_1, reg);
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->bias_level == SND_SOC_BIAS_PREPARE) {
			/* VREF off */
			reg = snd_soc_read(codec, WM8961_PWR_MGMT_1);
			reg &= ~WM8961_VREF;
			snd_soc_write(codec, WM8961_PWR_MGMT_1, reg);

			/* Bias generation off */
			reg = snd_soc_read(codec, WM8961_ANTI_POP);
			reg &= ~(WM8961_BUFIOEN | WM8961_BUFDCOPEN);
			snd_soc_write(codec, WM8961_ANTI_POP, reg);

			/* VMID off */
			reg = snd_soc_read(codec, WM8961_PWR_MGMT_1);
			reg &= ~WM8961_VMIDSEL_MASK;
			snd_soc_write(codec, WM8961_PWR_MGMT_1, reg);
		}
		break;

	case SND_SOC_BIAS_OFF:
		break;
	}

	codec->bias_level = level;

	return 0;
}


#define WM8961_RATES SNDRV_PCM_RATE_8000_48000

#define WM8961_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
	SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops wm8961_dai_ops = {
	.hw_params = wm8961_hw_params,
	.set_sysclk = wm8961_set_sysclk,
	.set_fmt = wm8961_set_fmt,
	.digital_mute = wm8961_digital_mute,
	.set_tristate = wm8961_set_tristate,
	.set_clkdiv = wm8961_set_clkdiv,
};

struct snd_soc_dai wm8961_dai = {
	.name = "WM8961",
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
EXPORT_SYMBOL_GPL(wm8961_dai);


static struct snd_soc_codec *wm8961_codec;

static int wm8961_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret = 0;

	if (wm8961_codec == NULL) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = wm8961_codec;
	codec = wm8961_codec;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms: %d\n", ret);
		goto pcm_err;
	}

	snd_soc_add_controls(codec, wm8961_snd_controls,
				ARRAY_SIZE(wm8961_snd_controls));
	snd_soc_dapm_new_controls(codec, wm8961_dapm_widgets,
				  ARRAY_SIZE(wm8961_dapm_widgets));
	snd_soc_dapm_add_routes(codec, audio_paths, ARRAY_SIZE(audio_paths));

	return ret;

pcm_err:
	return ret;
}

static int wm8961_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}

#ifdef CONFIG_PM
static int wm8961_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	wm8961_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int wm8961_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	u16 *reg_cache = codec->reg_cache;
	int i;

	for (i = 0; i < codec->reg_cache_size; i++) {
		if (reg_cache[i] == wm8961_reg_defaults[i])
			continue;

		if (i == WM8961_SOFTWARE_RESET)
			continue;

		snd_soc_write(codec, i, reg_cache[i]);
	}

	wm8961_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}
#else
#define wm8961_suspend NULL
#define wm8961_resume NULL
#endif

struct snd_soc_codec_device soc_codec_dev_wm8961 = {
	.probe = 	wm8961_probe,
	.remove = 	wm8961_remove,
	.suspend =	wm8961_suspend,
	.resume =	wm8961_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_wm8961);

static int wm8961_register(struct wm8961_priv *wm8961)
{
	struct snd_soc_codec *codec = &wm8961->codec;
	int ret;
	u16 reg;

	if (wm8961_codec) {
		dev_err(codec->dev, "Another WM8961 is registered\n");
		ret = -EINVAL;
		goto err;
	}

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	codec->private_data = wm8961;
	codec->name = "WM8961";
	codec->owner = THIS_MODULE;
	codec->dai = &wm8961_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = ARRAY_SIZE(wm8961->reg_cache);
	codec->reg_cache = &wm8961->reg_cache;
	codec->bias_level = SND_SOC_BIAS_OFF;
	codec->set_bias_level = wm8961_set_bias_level;
	codec->volatile_register = wm8961_volatile_register;

	memcpy(codec->reg_cache, wm8961_reg_defaults,
	       sizeof(wm8961_reg_defaults));

	ret = snd_soc_codec_set_cache_io(codec, 8, 16, SND_SOC_I2C);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		goto err;
	}

	reg = snd_soc_read(codec, WM8961_SOFTWARE_RESET);
	if (reg != 0x1801) {
		dev_err(codec->dev, "Device is not a WM8961: ID=0x%x\n", reg);
		ret = -EINVAL;
		goto err;
	}

	/* This isn't volatile - readback doesn't correspond to write */
	reg = codec->hw_read(codec, WM8961_RIGHT_INPUT_VOLUME);
	dev_info(codec->dev, "WM8961 family %d revision %c\n",
		 (reg & WM8961_DEVICE_ID_MASK) >> WM8961_DEVICE_ID_SHIFT,
		 ((reg & WM8961_CHIP_REV_MASK) >> WM8961_CHIP_REV_SHIFT)
		 + 'A');

	ret = wm8961_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		return ret;
	}

	/* Enable class W */
	reg = snd_soc_read(codec, WM8961_CHARGE_PUMP_B);
	reg |= WM8961_CP_DYN_PWR_MASK;
	snd_soc_write(codec, WM8961_CHARGE_PUMP_B, reg);

	/* Latch volume update bits (right channel only, we always
	 * write both out) and default ZC on. */
	reg = snd_soc_read(codec, WM8961_ROUT1_VOLUME);
	snd_soc_write(codec, WM8961_ROUT1_VOLUME,
		     reg | WM8961_LO1ZC | WM8961_OUT1VU);
	snd_soc_write(codec, WM8961_LOUT1_VOLUME, reg | WM8961_LO1ZC);
	reg = snd_soc_read(codec, WM8961_ROUT2_VOLUME);
	snd_soc_write(codec, WM8961_ROUT2_VOLUME,
		     reg | WM8961_SPKRZC | WM8961_SPKVU);
	snd_soc_write(codec, WM8961_LOUT2_VOLUME, reg | WM8961_SPKLZC);

	reg = snd_soc_read(codec, WM8961_RIGHT_ADC_VOLUME);
	snd_soc_write(codec, WM8961_RIGHT_ADC_VOLUME, reg | WM8961_ADCVU);
	reg = snd_soc_read(codec, WM8961_RIGHT_INPUT_VOLUME);
	snd_soc_write(codec, WM8961_RIGHT_INPUT_VOLUME, reg | WM8961_IPVU);

	/* Use soft mute by default */
	reg = snd_soc_read(codec, WM8961_ADC_DAC_CONTROL_2);
	reg |= WM8961_DACSMM;
	snd_soc_write(codec, WM8961_ADC_DAC_CONTROL_2, reg);

	/* Use automatic clocking mode by default; for now this is all
	 * we support.
	 */
	reg = snd_soc_read(codec, WM8961_CLOCKING_3);
	reg &= ~WM8961_MANUAL_MODE;
	snd_soc_write(codec, WM8961_CLOCKING_3, reg);

	wm8961_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	wm8961_dai.dev = codec->dev;

	wm8961_codec = codec;

	ret = snd_soc_register_codec(codec);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register codec: %d\n", ret);
		return ret;
	}

	ret = snd_soc_register_dai(&wm8961_dai);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register DAI: %d\n", ret);
		snd_soc_unregister_codec(codec);
		return ret;
	}

	return 0;

err:
	kfree(wm8961);
	return ret;
}

static void wm8961_unregister(struct wm8961_priv *wm8961)
{
	wm8961_set_bias_level(&wm8961->codec, SND_SOC_BIAS_OFF);
	snd_soc_unregister_dai(&wm8961_dai);
	snd_soc_unregister_codec(&wm8961->codec);
	kfree(wm8961);
	wm8961_codec = NULL;
}

static __devinit int wm8961_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8961_priv *wm8961;
	struct snd_soc_codec *codec;

	wm8961 = kzalloc(sizeof(struct wm8961_priv), GFP_KERNEL);
	if (wm8961 == NULL)
		return -ENOMEM;

	codec = &wm8961->codec;

	i2c_set_clientdata(i2c, wm8961);
	codec->control_data = i2c;

	codec->dev = &i2c->dev;

	return wm8961_register(wm8961);
}

static __devexit int wm8961_i2c_remove(struct i2c_client *client)
{
	struct wm8961_priv *wm8961 = i2c_get_clientdata(client);
	wm8961_unregister(wm8961);
	return 0;
}

static const struct i2c_device_id wm8961_i2c_id[] = {
	{ "wm8961", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8961_i2c_id);

static struct i2c_driver wm8961_i2c_driver = {
	.driver = {
		.name = "wm8961",
		.owner = THIS_MODULE,
	},
	.probe =    wm8961_i2c_probe,
	.remove =   __devexit_p(wm8961_i2c_remove),
	.id_table = wm8961_i2c_id,
};

static int __init wm8961_modinit(void)
{
	int ret;

	ret = i2c_add_driver(&wm8961_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8961 I2C driver: %d\n",
		       ret);
	}

	return ret;
}
module_init(wm8961_modinit);

static void __exit wm8961_exit(void)
{
	i2c_del_driver(&wm8961_i2c_driver);
}
module_exit(wm8961_exit);


MODULE_DESCRIPTION("ASoC WM8961 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
