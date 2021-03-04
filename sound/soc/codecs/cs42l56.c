// SPDX-License-Identifier: GPL-2.0-only
/*
 * cs42l56.c -- CS42L56 ALSA SoC audio driver
 *
 * Copyright 2014 CirrusLogic, Inc.
 *
 * Author: Brian Austin <brian.austin@cirrus.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/cs42l56.h>
#include "cs42l56.h"

#define CS42L56_NUM_SUPPLIES 3
static const char *const cs42l56_supply_names[CS42L56_NUM_SUPPLIES] = {
	"VA",
	"VCP",
	"VLDO",
};

struct  cs42l56_private {
	struct regmap *regmap;
	struct snd_soc_component *component;
	struct device *dev;
	struct cs42l56_platform_data pdata;
	struct regulator_bulk_data supplies[CS42L56_NUM_SUPPLIES];
	u32 mclk;
	u8 mclk_prediv;
	u8 mclk_div2;
	u8 mclk_ratio;
	u8 iface;
	u8 iface_fmt;
	u8 iface_inv;
#if IS_ENABLED(CONFIG_INPUT)
	struct input_dev *beep;
	struct work_struct beep_work;
	int beep_rate;
#endif
};

static const struct reg_default cs42l56_reg_defaults[] = {
	{ 3, 0x7f },	/* r03	- Power Ctl 1 */
	{ 4, 0xff },	/* r04	- Power Ctl 2 */
	{ 5, 0x00 },	/* ro5	- Clocking Ctl 1 */
	{ 6, 0x0b },	/* r06	- Clocking Ctl 2 */
	{ 7, 0x00 },	/* r07	- Serial Format */
	{ 8, 0x05 },	/* r08	- Class H Ctl */
	{ 9, 0x0c },	/* r09	- Misc Ctl */
	{ 10, 0x80 },	/* r0a	- INT Status */
	{ 11, 0x00 },	/* r0b	- Playback Ctl */
	{ 12, 0x0c },	/* r0c	- DSP Mute Ctl */
	{ 13, 0x00 },	/* r0d	- ADCA Mixer Volume */
	{ 14, 0x00 },	/* r0e	- ADCB Mixer Volume */
	{ 15, 0x00 },	/* r0f	- PCMA Mixer Volume */
	{ 16, 0x00 },	/* r10	- PCMB Mixer Volume */
	{ 17, 0x00 },	/* r11	- Analog Input Advisory Volume */
	{ 18, 0x00 },	/* r12	- Digital Input Advisory Volume */
	{ 19, 0x00 },	/* r13	- Master A Volume */
	{ 20, 0x00 },	/* r14	- Master B Volume */
	{ 21, 0x00 },	/* r15	- Beep Freq / On Time */
	{ 22, 0x00 },	/* r16	- Beep Volume / Off Time */
	{ 23, 0x00 },	/* r17	- Beep Tone Ctl */
	{ 24, 0x88 },	/* r18	- Tone Ctl */
	{ 25, 0x00 },	/* r19	- Channel Mixer & Swap */
	{ 26, 0x00 },	/* r1a	- AIN Ref Config / ADC Mux */
	{ 27, 0xa0 },	/* r1b	- High-Pass Filter Ctl */
	{ 28, 0x00 },	/* r1c	- Misc ADC Ctl */
	{ 29, 0x00 },	/* r1d	- Gain & Bias Ctl */
	{ 30, 0x00 },	/* r1e	- PGAA Mux & Volume */
	{ 31, 0x00 },	/* r1f	- PGAB Mux & Volume */
	{ 32, 0x00 },	/* r20	- ADCA Attenuator */
	{ 33, 0x00 },	/* r21	- ADCB Attenuator */
	{ 34, 0x00 },	/* r22	- ALC Enable & Attack Rate */
	{ 35, 0xbf },	/* r23	- ALC Release Rate */
	{ 36, 0x00 },	/* r24	- ALC Threshold */
	{ 37, 0x00 },	/* r25	- Noise Gate Ctl */
	{ 38, 0x00 },	/* r26	- ALC, Limiter, SFT, ZeroCross */
	{ 39, 0x00 },	/* r27	- Analog Mute, LO & HP Mux */
	{ 40, 0x00 },	/* r28	- HP A Volume */
	{ 41, 0x00 },	/* r29	- HP B Volume */
	{ 42, 0x00 },	/* r2a	- LINEOUT A Volume */
	{ 43, 0x00 },	/* r2b	- LINEOUT B Volume */
	{ 44, 0x00 },	/* r2c	- Limit Threshold Ctl */
	{ 45, 0x7f },	/* r2d	- Limiter Ctl & Release Rate */
	{ 46, 0x00 },	/* r2e	- Limiter Attack Rate */
};

static bool cs42l56_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS42L56_CHIP_ID_1 ... CS42L56_LIM_ATTACK_RATE:
		return true;
	default:
		return false;
	}
}

static bool cs42l56_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS42L56_INT_STATUS:
		return true;
	default:
		return false;
	}
}

static DECLARE_TLV_DB_SCALE(beep_tlv, -5000, 200, 0);
static DECLARE_TLV_DB_SCALE(hl_tlv, -6000, 50, 0);
static DECLARE_TLV_DB_SCALE(adv_tlv, -10200, 50, 0);
static DECLARE_TLV_DB_SCALE(adc_tlv, -9600, 100, 0);
static DECLARE_TLV_DB_SCALE(tone_tlv, -1050, 150, 0);
static DECLARE_TLV_DB_SCALE(preamp_tlv, 0, 1000, 0);
static DECLARE_TLV_DB_SCALE(pga_tlv, -600, 50, 0);

static const DECLARE_TLV_DB_RANGE(ngnb_tlv,
	0, 1, TLV_DB_SCALE_ITEM(-8200, 600, 0),
	2, 5, TLV_DB_SCALE_ITEM(-7600, 300, 0)
);
static const DECLARE_TLV_DB_RANGE(ngb_tlv,
	0, 2, TLV_DB_SCALE_ITEM(-6400, 600, 0),
	3, 7, TLV_DB_SCALE_ITEM(-4600, 300, 0)
);
static const DECLARE_TLV_DB_RANGE(alc_tlv,
	0, 2, TLV_DB_SCALE_ITEM(-3000, 600, 0),
	3, 7, TLV_DB_SCALE_ITEM(-1200, 300, 0)
);

static const char * const beep_config_text[] = {
	"Off", "Single", "Multiple", "Continuous"
};

static const struct soc_enum beep_config_enum =
	SOC_ENUM_SINGLE(CS42L56_BEEP_TONE_CFG, 6,
			ARRAY_SIZE(beep_config_text), beep_config_text);

static const char * const beep_pitch_text[] = {
	"C4", "C5", "D5", "E5", "F5", "G5", "A5", "B5",
	"C6", "D6", "E6", "F6", "G6", "A6", "B6", "C7"
};

static const struct soc_enum beep_pitch_enum =
	SOC_ENUM_SINGLE(CS42L56_BEEP_FREQ_ONTIME, 4,
			ARRAY_SIZE(beep_pitch_text), beep_pitch_text);

static const char * const beep_ontime_text[] = {
	"86 ms", "430 ms", "780 ms", "1.20 s", "1.50 s",
	"1.80 s", "2.20 s", "2.50 s", "2.80 s", "3.20 s",
	"3.50 s", "3.80 s", "4.20 s", "4.50 s", "4.80 s", "5.20 s"
};

static const struct soc_enum beep_ontime_enum =
	SOC_ENUM_SINGLE(CS42L56_BEEP_FREQ_ONTIME, 0,
			ARRAY_SIZE(beep_ontime_text), beep_ontime_text);

static const char * const beep_offtime_text[] = {
	"1.23 s", "2.58 s", "3.90 s", "5.20 s",
	"6.60 s", "8.05 s", "9.35 s", "10.80 s"
};

static const struct soc_enum beep_offtime_enum =
	SOC_ENUM_SINGLE(CS42L56_BEEP_FREQ_OFFTIME, 5,
			ARRAY_SIZE(beep_offtime_text), beep_offtime_text);

static const char * const beep_treble_text[] = {
	"5kHz", "7kHz", "10kHz", "15kHz"
};

static const struct soc_enum beep_treble_enum =
	SOC_ENUM_SINGLE(CS42L56_BEEP_TONE_CFG, 3,
			ARRAY_SIZE(beep_treble_text), beep_treble_text);

static const char * const beep_bass_text[] = {
	"50Hz", "100Hz", "200Hz", "250Hz"
};

static const struct soc_enum beep_bass_enum =
	SOC_ENUM_SINGLE(CS42L56_BEEP_TONE_CFG, 1,
			ARRAY_SIZE(beep_bass_text), beep_bass_text);

static const char * const pgaa_mux_text[] = {
	"AIN1A", "AIN2A", "AIN3A"};

static const struct soc_enum pgaa_mux_enum =
	SOC_ENUM_SINGLE(CS42L56_PGAA_MUX_VOLUME, 0,
			      ARRAY_SIZE(pgaa_mux_text),
			      pgaa_mux_text);

static const struct snd_kcontrol_new pgaa_mux =
	SOC_DAPM_ENUM("Route", pgaa_mux_enum);

static const char * const pgab_mux_text[] = {
	"AIN1B", "AIN2B", "AIN3B"};

static const struct soc_enum pgab_mux_enum =
	SOC_ENUM_SINGLE(CS42L56_PGAB_MUX_VOLUME, 0,
			      ARRAY_SIZE(pgab_mux_text),
			      pgab_mux_text);

static const struct snd_kcontrol_new pgab_mux =
	SOC_DAPM_ENUM("Route", pgab_mux_enum);

static const char * const adca_mux_text[] = {
	"PGAA", "AIN1A", "AIN2A", "AIN3A"};

static const struct soc_enum adca_mux_enum =
	SOC_ENUM_SINGLE(CS42L56_AIN_REFCFG_ADC_MUX, 0,
			      ARRAY_SIZE(adca_mux_text),
			      adca_mux_text);

static const struct snd_kcontrol_new adca_mux =
	SOC_DAPM_ENUM("Route", adca_mux_enum);

static const char * const adcb_mux_text[] = {
	"PGAB", "AIN1B", "AIN2B", "AIN3B"};

static const struct soc_enum adcb_mux_enum =
	SOC_ENUM_SINGLE(CS42L56_AIN_REFCFG_ADC_MUX, 2,
			      ARRAY_SIZE(adcb_mux_text),
			      adcb_mux_text);

static const struct snd_kcontrol_new adcb_mux =
	SOC_DAPM_ENUM("Route", adcb_mux_enum);

static const char * const left_swap_text[] = {
	"Left", "LR 2", "Right"};

static const char * const right_swap_text[] = {
	"Right", "LR 2", "Left"};

static const unsigned int swap_values[] = { 0, 1, 3 };

static const struct soc_enum adca_swap_enum =
	SOC_VALUE_ENUM_SINGLE(CS42L56_CHAN_MIX_SWAP, 0, 3,
			      ARRAY_SIZE(left_swap_text),
			      left_swap_text,
			      swap_values);
static const struct snd_kcontrol_new adca_swap_mux =
	SOC_DAPM_ENUM("Route", adca_swap_enum);

static const struct soc_enum pcma_swap_enum =
	SOC_VALUE_ENUM_SINGLE(CS42L56_CHAN_MIX_SWAP, 4, 3,
			      ARRAY_SIZE(left_swap_text),
			      left_swap_text,
			      swap_values);
static const struct snd_kcontrol_new pcma_swap_mux =
	SOC_DAPM_ENUM("Route", pcma_swap_enum);

static const struct soc_enum adcb_swap_enum =
	SOC_VALUE_ENUM_SINGLE(CS42L56_CHAN_MIX_SWAP, 2, 3,
			      ARRAY_SIZE(right_swap_text),
			      right_swap_text,
			      swap_values);
static const struct snd_kcontrol_new adcb_swap_mux =
	SOC_DAPM_ENUM("Route", adcb_swap_enum);

static const struct soc_enum pcmb_swap_enum =
	SOC_VALUE_ENUM_SINGLE(CS42L56_CHAN_MIX_SWAP, 6, 3,
			      ARRAY_SIZE(right_swap_text),
			      right_swap_text,
			      swap_values);
static const struct snd_kcontrol_new pcmb_swap_mux =
	SOC_DAPM_ENUM("Route", pcmb_swap_enum);

static const struct snd_kcontrol_new hpa_switch =
	SOC_DAPM_SINGLE("Switch", CS42L56_PWRCTL_2, 6, 1, 1);

static const struct snd_kcontrol_new hpb_switch =
	SOC_DAPM_SINGLE("Switch", CS42L56_PWRCTL_2, 4, 1, 1);

static const struct snd_kcontrol_new loa_switch =
	SOC_DAPM_SINGLE("Switch", CS42L56_PWRCTL_2, 2, 1, 1);

static const struct snd_kcontrol_new lob_switch =
	SOC_DAPM_SINGLE("Switch", CS42L56_PWRCTL_2, 0, 1, 1);

static const char * const hploa_input_text[] = {
	"DACA", "PGAA"};

static const struct soc_enum lineouta_input_enum =
	SOC_ENUM_SINGLE(CS42L56_AMUTE_HPLO_MUX, 2,
			      ARRAY_SIZE(hploa_input_text),
			      hploa_input_text);

static const struct snd_kcontrol_new lineouta_input =
	SOC_DAPM_ENUM("Route", lineouta_input_enum);

static const struct soc_enum hpa_input_enum =
	SOC_ENUM_SINGLE(CS42L56_AMUTE_HPLO_MUX, 0,
			      ARRAY_SIZE(hploa_input_text),
			      hploa_input_text);

static const struct snd_kcontrol_new hpa_input =
	SOC_DAPM_ENUM("Route", hpa_input_enum);

static const char * const hplob_input_text[] = {
	"DACB", "PGAB"};

static const struct soc_enum lineoutb_input_enum =
	SOC_ENUM_SINGLE(CS42L56_AMUTE_HPLO_MUX, 3,
			      ARRAY_SIZE(hplob_input_text),
			      hplob_input_text);

static const struct snd_kcontrol_new lineoutb_input =
	SOC_DAPM_ENUM("Route", lineoutb_input_enum);

static const struct soc_enum hpb_input_enum =
	SOC_ENUM_SINGLE(CS42L56_AMUTE_HPLO_MUX, 1,
			      ARRAY_SIZE(hplob_input_text),
			      hplob_input_text);

static const struct snd_kcontrol_new hpb_input =
	SOC_DAPM_ENUM("Route", hpb_input_enum);

static const char * const dig_mux_text[] = {
	"ADC", "DSP"};

static const struct soc_enum dig_mux_enum =
	SOC_ENUM_SINGLE(CS42L56_MISC_CTL, 7,
			      ARRAY_SIZE(dig_mux_text),
			      dig_mux_text);

static const struct snd_kcontrol_new dig_mux =
	SOC_DAPM_ENUM("Route", dig_mux_enum);

static const char * const hpf_freq_text[] = {
	"1.8Hz", "119Hz", "236Hz", "464Hz"
};

static const struct soc_enum hpfa_freq_enum =
	SOC_ENUM_SINGLE(CS42L56_HPF_CTL, 0,
			ARRAY_SIZE(hpf_freq_text), hpf_freq_text);

static const struct soc_enum hpfb_freq_enum =
	SOC_ENUM_SINGLE(CS42L56_HPF_CTL, 2,
			ARRAY_SIZE(hpf_freq_text), hpf_freq_text);

static const char * const ng_delay_text[] = {
	"50ms", "100ms", "150ms", "200ms"
};

static const struct soc_enum ng_delay_enum =
	SOC_ENUM_SINGLE(CS42L56_NOISE_GATE_CTL, 0,
			ARRAY_SIZE(ng_delay_text), ng_delay_text);

static const struct snd_kcontrol_new cs42l56_snd_controls[] = {

	SOC_DOUBLE_R_SX_TLV("Master Volume", CS42L56_MASTER_A_VOLUME,
			      CS42L56_MASTER_B_VOLUME, 0, 0x34, 0xE4, adv_tlv),
	SOC_DOUBLE("Master Mute Switch", CS42L56_DSP_MUTE_CTL, 0, 1, 1, 1),

	SOC_DOUBLE_R_SX_TLV("ADC Mixer Volume", CS42L56_ADCA_MIX_VOLUME,
			      CS42L56_ADCB_MIX_VOLUME, 0, 0x88, 0x90, hl_tlv),
	SOC_DOUBLE("ADC Mixer Mute Switch", CS42L56_DSP_MUTE_CTL, 6, 7, 1, 1),

	SOC_DOUBLE_R_SX_TLV("PCM Mixer Volume", CS42L56_PCMA_MIX_VOLUME,
			      CS42L56_PCMB_MIX_VOLUME, 0, 0x88, 0x90, hl_tlv),
	SOC_DOUBLE("PCM Mixer Mute Switch", CS42L56_DSP_MUTE_CTL, 4, 5, 1, 1),

	SOC_SINGLE_TLV("Analog Advisory Volume",
			  CS42L56_ANAINPUT_ADV_VOLUME, 0, 0x00, 1, adv_tlv),
	SOC_SINGLE_TLV("Digital Advisory Volume",
			  CS42L56_DIGINPUT_ADV_VOLUME, 0, 0x00, 1, adv_tlv),

	SOC_DOUBLE_R_SX_TLV("PGA Volume", CS42L56_PGAA_MUX_VOLUME,
			      CS42L56_PGAB_MUX_VOLUME, 0, 0x34, 0x24, pga_tlv),
	SOC_DOUBLE_R_TLV("ADC Volume", CS42L56_ADCA_ATTENUATOR,
			      CS42L56_ADCB_ATTENUATOR, 0, 0x00, 1, adc_tlv),
	SOC_DOUBLE("ADC Mute Switch", CS42L56_MISC_ADC_CTL, 2, 3, 1, 1),
	SOC_DOUBLE("ADC Boost Switch", CS42L56_GAIN_BIAS_CTL, 3, 2, 1, 1),

	SOC_DOUBLE_R_SX_TLV("Headphone Volume", CS42L56_HPA_VOLUME,
			      CS42L56_HPB_VOLUME, 0, 0x84, 0x48, hl_tlv),
	SOC_DOUBLE_R_SX_TLV("LineOut Volume", CS42L56_LOA_VOLUME,
			      CS42L56_LOB_VOLUME, 0, 0x84, 0x48, hl_tlv),

	SOC_SINGLE_TLV("Bass Shelving Volume", CS42L56_TONE_CTL,
			0, 0x00, 1, tone_tlv),
	SOC_SINGLE_TLV("Treble Shelving Volume", CS42L56_TONE_CTL,
			4, 0x00, 1, tone_tlv),

	SOC_DOUBLE_TLV("PGA Preamp Volume", CS42L56_GAIN_BIAS_CTL,
			4, 6, 0x02, 1, preamp_tlv),

	SOC_SINGLE("DSP Switch", CS42L56_PLAYBACK_CTL, 7, 1, 1),
	SOC_SINGLE("Gang Playback Switch", CS42L56_PLAYBACK_CTL, 4, 1, 1),
	SOC_SINGLE("Gang ADC Switch", CS42L56_MISC_ADC_CTL, 7, 1, 1),
	SOC_SINGLE("Gang PGA Switch", CS42L56_MISC_ADC_CTL, 6, 1, 1),

	SOC_SINGLE("PCMA Invert", CS42L56_PLAYBACK_CTL, 2, 1, 1),
	SOC_SINGLE("PCMB Invert", CS42L56_PLAYBACK_CTL, 3, 1, 1),
	SOC_SINGLE("ADCA Invert", CS42L56_MISC_ADC_CTL, 2, 1, 1),
	SOC_SINGLE("ADCB Invert", CS42L56_MISC_ADC_CTL, 3, 1, 1),

	SOC_DOUBLE("HPF Switch", CS42L56_HPF_CTL, 5, 7, 1, 1),
	SOC_DOUBLE("HPF Freeze Switch", CS42L56_HPF_CTL, 4, 6, 1, 1),
	SOC_ENUM("HPFA Corner Freq", hpfa_freq_enum),
	SOC_ENUM("HPFB Corner Freq", hpfb_freq_enum),

	SOC_SINGLE("Analog Soft Ramp", CS42L56_MISC_CTL, 4, 1, 1),
	SOC_DOUBLE("Analog Soft Ramp Disable", CS42L56_ALC_LIM_SFT_ZC,
		7, 5, 1, 1),
	SOC_SINGLE("Analog Zero Cross", CS42L56_MISC_CTL, 3, 1, 1),
	SOC_DOUBLE("Analog Zero Cross Disable", CS42L56_ALC_LIM_SFT_ZC,
		6, 4, 1, 1),
	SOC_SINGLE("Digital Soft Ramp", CS42L56_MISC_CTL, 2, 1, 1),
	SOC_SINGLE("Digital Soft Ramp Disable", CS42L56_ALC_LIM_SFT_ZC,
		3, 1, 1),

	SOC_SINGLE("HL Deemphasis", CS42L56_PLAYBACK_CTL, 6, 1, 1),

	SOC_SINGLE("ALC Switch", CS42L56_ALC_EN_ATTACK_RATE, 6, 1, 1),
	SOC_SINGLE("ALC Limit All Switch", CS42L56_ALC_RELEASE_RATE, 7, 1, 1),
	SOC_SINGLE_RANGE("ALC Attack", CS42L56_ALC_EN_ATTACK_RATE,
			0, 0, 0x3f, 0),
	SOC_SINGLE_RANGE("ALC Release", CS42L56_ALC_RELEASE_RATE,
			0, 0x3f, 0, 0),
	SOC_SINGLE_TLV("ALC MAX", CS42L56_ALC_THRESHOLD,
			5, 0x07, 1, alc_tlv),
	SOC_SINGLE_TLV("ALC MIN", CS42L56_ALC_THRESHOLD,
			2, 0x07, 1, alc_tlv),

	SOC_SINGLE("Limiter Switch", CS42L56_LIM_CTL_RELEASE_RATE, 7, 1, 1),
	SOC_SINGLE("Limit All Switch", CS42L56_LIM_CTL_RELEASE_RATE, 6, 1, 1),
	SOC_SINGLE_RANGE("Limiter Attack", CS42L56_LIM_ATTACK_RATE,
			0, 0, 0x3f, 0),
	SOC_SINGLE_RANGE("Limiter Release", CS42L56_LIM_CTL_RELEASE_RATE,
			0, 0x3f, 0, 0),
	SOC_SINGLE_TLV("Limiter MAX", CS42L56_LIM_THRESHOLD_CTL,
			5, 0x07, 1, alc_tlv),
	SOC_SINGLE_TLV("Limiter Cushion", CS42L56_ALC_THRESHOLD,
			2, 0x07, 1, alc_tlv),

	SOC_SINGLE("NG Switch", CS42L56_NOISE_GATE_CTL, 6, 1, 1),
	SOC_SINGLE("NG All Switch", CS42L56_NOISE_GATE_CTL, 7, 1, 1),
	SOC_SINGLE("NG Boost Switch", CS42L56_NOISE_GATE_CTL, 5, 1, 1),
	SOC_SINGLE_TLV("NG Unboost Threshold", CS42L56_NOISE_GATE_CTL,
			2, 0x07, 1, ngnb_tlv),
	SOC_SINGLE_TLV("NG Boost Threshold", CS42L56_NOISE_GATE_CTL,
			2, 0x07, 1, ngb_tlv),
	SOC_ENUM("NG Delay", ng_delay_enum),

	SOC_ENUM("Beep Config", beep_config_enum),
	SOC_ENUM("Beep Pitch", beep_pitch_enum),
	SOC_ENUM("Beep on Time", beep_ontime_enum),
	SOC_ENUM("Beep off Time", beep_offtime_enum),
	SOC_SINGLE_SX_TLV("Beep Volume", CS42L56_BEEP_FREQ_OFFTIME,
			0, 0x07, 0x23, beep_tlv),
	SOC_SINGLE("Beep Tone Ctl Switch", CS42L56_BEEP_TONE_CFG, 0, 1, 1),
	SOC_ENUM("Beep Treble Corner Freq", beep_treble_enum),
	SOC_ENUM("Beep Bass Corner Freq", beep_bass_enum),

};

static const struct snd_soc_dapm_widget cs42l56_dapm_widgets[] = {

	SND_SOC_DAPM_SIGGEN("Beep"),
	SND_SOC_DAPM_SUPPLY("VBUF", CS42L56_PWRCTL_1, 5, 1, NULL, 0),
	SND_SOC_DAPM_MICBIAS("MIC1 Bias", CS42L56_PWRCTL_1, 4, 1),
	SND_SOC_DAPM_SUPPLY("Charge Pump", CS42L56_PWRCTL_1, 3, 1, NULL, 0),

	SND_SOC_DAPM_INPUT("AIN1A"),
	SND_SOC_DAPM_INPUT("AIN2A"),
	SND_SOC_DAPM_INPUT("AIN1B"),
	SND_SOC_DAPM_INPUT("AIN2B"),
	SND_SOC_DAPM_INPUT("AIN3A"),
	SND_SOC_DAPM_INPUT("AIN3B"),

	SND_SOC_DAPM_AIF_OUT("SDOUT", NULL,  0,
			SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_IN("SDIN", NULL,  0,
			SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_MUX("Digital Output Mux", SND_SOC_NOPM,
			 0, 0, &dig_mux),

	SND_SOC_DAPM_PGA("PGAA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PGAB", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MUX("PGAA Input Mux",
			SND_SOC_NOPM, 0, 0, &pgaa_mux),
	SND_SOC_DAPM_MUX("PGAB Input Mux",
			SND_SOC_NOPM, 0, 0, &pgab_mux),

	SND_SOC_DAPM_MUX("ADCA Mux", SND_SOC_NOPM,
			 0, 0, &adca_mux),
	SND_SOC_DAPM_MUX("ADCB Mux", SND_SOC_NOPM,
			 0, 0, &adcb_mux),

	SND_SOC_DAPM_ADC("ADCA", NULL, CS42L56_PWRCTL_1, 1, 1),
	SND_SOC_DAPM_ADC("ADCB", NULL, CS42L56_PWRCTL_1, 2, 1),

	SND_SOC_DAPM_MUX("ADCA Swap Mux", SND_SOC_NOPM, 0, 0,
		&adca_swap_mux),
	SND_SOC_DAPM_MUX("ADCB Swap Mux", SND_SOC_NOPM, 0, 0,
		&adcb_swap_mux),

	SND_SOC_DAPM_MUX("PCMA Swap Mux", SND_SOC_NOPM, 0, 0,
		&pcma_swap_mux),
	SND_SOC_DAPM_MUX("PCMB Swap Mux", SND_SOC_NOPM, 0, 0,
		&pcmb_swap_mux),

	SND_SOC_DAPM_DAC("DACA", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DACB", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_OUTPUT("HPA"),
	SND_SOC_DAPM_OUTPUT("LOA"),
	SND_SOC_DAPM_OUTPUT("HPB"),
	SND_SOC_DAPM_OUTPUT("LOB"),

	SND_SOC_DAPM_SWITCH("Headphone Right",
			    CS42L56_PWRCTL_2, 4, 1, &hpb_switch),
	SND_SOC_DAPM_SWITCH("Headphone Left",
			    CS42L56_PWRCTL_2, 6, 1, &hpa_switch),

	SND_SOC_DAPM_SWITCH("Lineout Right",
			    CS42L56_PWRCTL_2, 0, 1, &lob_switch),
	SND_SOC_DAPM_SWITCH("Lineout Left",
			    CS42L56_PWRCTL_2, 2, 1, &loa_switch),

	SND_SOC_DAPM_MUX("LINEOUTA Input Mux", SND_SOC_NOPM,
			 0, 0, &lineouta_input),
	SND_SOC_DAPM_MUX("LINEOUTB Input Mux", SND_SOC_NOPM,
			 0, 0, &lineoutb_input),
	SND_SOC_DAPM_MUX("HPA Input Mux", SND_SOC_NOPM,
			 0, 0, &hpa_input),
	SND_SOC_DAPM_MUX("HPB Input Mux", SND_SOC_NOPM,
			 0, 0, &hpb_input),

};

static const struct snd_soc_dapm_route cs42l56_audio_map[] = {

	{"HiFi Capture", "DSP", "Digital Output Mux"},
	{"HiFi Capture", "ADC", "Digital Output Mux"},

	{"Digital Output Mux", NULL, "ADCA"},
	{"Digital Output Mux", NULL, "ADCB"},

	{"ADCB", NULL, "ADCB Swap Mux"},
	{"ADCA", NULL, "ADCA Swap Mux"},

	{"ADCA Swap Mux", NULL, "ADCA"},
	{"ADCB Swap Mux", NULL, "ADCB"},

	{"DACA", "Left", "ADCA Swap Mux"},
	{"DACA", "LR 2", "ADCA Swap Mux"},
	{"DACA", "Right", "ADCA Swap Mux"},

	{"DACB", "Left", "ADCB Swap Mux"},
	{"DACB", "LR 2", "ADCB Swap Mux"},
	{"DACB", "Right", "ADCB Swap Mux"},

	{"ADCA Mux", NULL, "AIN3A"},
	{"ADCA Mux", NULL, "AIN2A"},
	{"ADCA Mux", NULL, "AIN1A"},
	{"ADCA Mux", NULL, "PGAA"},
	{"ADCB Mux", NULL, "AIN3B"},
	{"ADCB Mux", NULL, "AIN2B"},
	{"ADCB Mux", NULL, "AIN1B"},
	{"ADCB Mux", NULL, "PGAB"},

	{"PGAA", "AIN1A", "PGAA Input Mux"},
	{"PGAA", "AIN2A", "PGAA Input Mux"},
	{"PGAA", "AIN3A", "PGAA Input Mux"},
	{"PGAB", "AIN1B", "PGAB Input Mux"},
	{"PGAB", "AIN2B", "PGAB Input Mux"},
	{"PGAB", "AIN3B", "PGAB Input Mux"},

	{"PGAA Input Mux", NULL, "AIN1A"},
	{"PGAA Input Mux", NULL, "AIN2A"},
	{"PGAA Input Mux", NULL, "AIN3A"},
	{"PGAB Input Mux", NULL, "AIN1B"},
	{"PGAB Input Mux", NULL, "AIN2B"},
	{"PGAB Input Mux", NULL, "AIN3B"},

	{"LOB", "Switch", "LINEOUTB Input Mux"},
	{"LOA", "Switch", "LINEOUTA Input Mux"},

	{"LINEOUTA Input Mux", "PGAA", "PGAA"},
	{"LINEOUTB Input Mux", "PGAB", "PGAB"},
	{"LINEOUTA Input Mux", "DACA", "DACA"},
	{"LINEOUTB Input Mux", "DACB", "DACB"},

	{"HPA", "Switch", "HPB Input Mux"},
	{"HPB", "Switch", "HPA Input Mux"},

	{"HPA Input Mux", "PGAA", "PGAA"},
	{"HPB Input Mux", "PGAB", "PGAB"},
	{"HPA Input Mux", "DACA", "DACA"},
	{"HPB Input Mux", "DACB", "DACB"},

	{"DACA", NULL, "PCMA Swap Mux"},
	{"DACB", NULL, "PCMB Swap Mux"},

	{"PCMB Swap Mux", "Left", "HiFi Playback"},
	{"PCMB Swap Mux", "LR 2", "HiFi Playback"},
	{"PCMB Swap Mux", "Right", "HiFi Playback"},

	{"PCMA Swap Mux", "Left", "HiFi Playback"},
	{"PCMA Swap Mux", "LR 2", "HiFi Playback"},
	{"PCMA Swap Mux", "Right", "HiFi Playback"},

};

struct cs42l56_clk_para {
	u32 mclk;
	u32 srate;
	u8 ratio;
};

static const struct cs42l56_clk_para clk_ratio_table[] = {
	/* 8k */
	{ 6000000, 8000, CS42L56_MCLK_LRCLK_768 },
	{ 6144000, 8000, CS42L56_MCLK_LRCLK_750 },
	{ 12000000, 8000, CS42L56_MCLK_LRCLK_768 },
	{ 12288000, 8000, CS42L56_MCLK_LRCLK_750 },
	{ 24000000, 8000, CS42L56_MCLK_LRCLK_768 },
	{ 24576000, 8000, CS42L56_MCLK_LRCLK_750 },
	/* 11.025k */
	{ 5644800, 11025, CS42L56_MCLK_LRCLK_512},
	{ 11289600, 11025, CS42L56_MCLK_LRCLK_512},
	{ 22579200, 11025, CS42L56_MCLK_LRCLK_512 },
	/* 11.0294k */
	{ 6000000, 110294, CS42L56_MCLK_LRCLK_544 },
	{ 12000000, 110294, CS42L56_MCLK_LRCLK_544 },
	{ 24000000, 110294, CS42L56_MCLK_LRCLK_544 },
	/* 12k */
	{ 6000000, 12000, CS42L56_MCLK_LRCLK_500 },
	{ 6144000, 12000, CS42L56_MCLK_LRCLK_512 },
	{ 12000000, 12000, CS42L56_MCLK_LRCLK_500 },
	{ 12288000, 12000, CS42L56_MCLK_LRCLK_512 },
	{ 24000000, 12000, CS42L56_MCLK_LRCLK_500 },
	{ 24576000, 12000, CS42L56_MCLK_LRCLK_512 },
	/* 16k */
	{ 6000000, 16000, CS42L56_MCLK_LRCLK_375 },
	{ 6144000, 16000, CS42L56_MCLK_LRCLK_384 },
	{ 12000000, 16000, CS42L56_MCLK_LRCLK_375 },
	{ 12288000, 16000, CS42L56_MCLK_LRCLK_384 },
	{ 24000000, 16000, CS42L56_MCLK_LRCLK_375 },
	{ 24576000, 16000, CS42L56_MCLK_LRCLK_384 },
	/* 22.050k */
	{ 5644800, 22050, CS42L56_MCLK_LRCLK_256 },
	{ 11289600, 22050, CS42L56_MCLK_LRCLK_256 },
	{ 22579200, 22050, CS42L56_MCLK_LRCLK_256 },
	/* 22.0588k */
	{ 6000000, 220588, CS42L56_MCLK_LRCLK_272 },
	{ 12000000, 220588, CS42L56_MCLK_LRCLK_272 },
	{ 24000000, 220588, CS42L56_MCLK_LRCLK_272 },
	/* 24k */
	{ 6000000, 24000, CS42L56_MCLK_LRCLK_250 },
	{ 6144000, 24000, CS42L56_MCLK_LRCLK_256 },
	{ 12000000, 24000, CS42L56_MCLK_LRCLK_250 },
	{ 12288000, 24000, CS42L56_MCLK_LRCLK_256 },
	{ 24000000, 24000, CS42L56_MCLK_LRCLK_250 },
	{ 24576000, 24000, CS42L56_MCLK_LRCLK_256 },
	/* 32k */
	{ 6000000, 32000, CS42L56_MCLK_LRCLK_187P5 },
	{ 6144000, 32000, CS42L56_MCLK_LRCLK_192 },
	{ 12000000, 32000, CS42L56_MCLK_LRCLK_187P5 },
	{ 12288000, 32000, CS42L56_MCLK_LRCLK_192 },
	{ 24000000, 32000, CS42L56_MCLK_LRCLK_187P5 },
	{ 24576000, 32000, CS42L56_MCLK_LRCLK_192 },
	/* 44.118k */
	{ 6000000, 44118, CS42L56_MCLK_LRCLK_136 },
	{ 12000000, 44118, CS42L56_MCLK_LRCLK_136 },
	{ 24000000, 44118, CS42L56_MCLK_LRCLK_136 },
	/* 44.1k */
	{ 5644800, 44100, CS42L56_MCLK_LRCLK_128 },
	{ 11289600, 44100, CS42L56_MCLK_LRCLK_128 },
	{ 22579200, 44100, CS42L56_MCLK_LRCLK_128 },
	/* 48k */
	{ 6000000, 48000, CS42L56_MCLK_LRCLK_125 },
	{ 6144000, 48000, CS42L56_MCLK_LRCLK_128 },
	{ 12000000, 48000, CS42L56_MCLK_LRCLK_125 },
	{ 12288000, 48000, CS42L56_MCLK_LRCLK_128 },
	{ 24000000, 48000, CS42L56_MCLK_LRCLK_125 },
	{ 24576000, 48000, CS42L56_MCLK_LRCLK_128 },
};

static int cs42l56_get_mclk_ratio(int mclk, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(clk_ratio_table); i++) {
		if (clk_ratio_table[i].mclk == mclk &&
		    clk_ratio_table[i].srate == rate)
			return clk_ratio_table[i].ratio;
	}
	return -EINVAL;
}

static int cs42l56_set_sysclk(struct snd_soc_dai *codec_dai,
			int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct cs42l56_private *cs42l56 = snd_soc_component_get_drvdata(component);

	switch (freq) {
	case CS42L56_MCLK_5P6448MHZ:
	case CS42L56_MCLK_6MHZ:
	case CS42L56_MCLK_6P144MHZ:
		cs42l56->mclk_div2 = 0;
		cs42l56->mclk_prediv = 0;
		break;
	case CS42L56_MCLK_11P2896MHZ:
	case CS42L56_MCLK_12MHZ:
	case CS42L56_MCLK_12P288MHZ:
		cs42l56->mclk_div2 = CS42L56_MCLK_DIV2;
		cs42l56->mclk_prediv = 0;
		break;
	case CS42L56_MCLK_22P5792MHZ:
	case CS42L56_MCLK_24MHZ:
	case CS42L56_MCLK_24P576MHZ:
		cs42l56->mclk_div2 = CS42L56_MCLK_DIV2;
		cs42l56->mclk_prediv = CS42L56_MCLK_PREDIV;
		break;
	default:
		return -EINVAL;
	}
	cs42l56->mclk = freq;

	snd_soc_component_update_bits(component, CS42L56_CLKCTL_1,
			    CS42L56_MCLK_PREDIV_MASK,
				cs42l56->mclk_prediv);
	snd_soc_component_update_bits(component, CS42L56_CLKCTL_1,
			    CS42L56_MCLK_DIV2_MASK,
				cs42l56->mclk_div2);

	return 0;
}

static int cs42l56_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct cs42l56_private *cs42l56 = snd_soc_component_get_drvdata(component);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		cs42l56->iface = CS42L56_MASTER_MODE;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		cs42l56->iface = CS42L56_SLAVE_MODE;
		break;
	default:
		return -EINVAL;
	}

	 /* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		cs42l56->iface_fmt = CS42L56_DIG_FMT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		cs42l56->iface_fmt = CS42L56_DIG_FMT_LEFT_J;
		break;
	default:
		return -EINVAL;
	}

	/* sclk inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		cs42l56->iface_inv = 0;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		cs42l56->iface_inv = CS42L56_SCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, CS42L56_CLKCTL_1,
			    CS42L56_MS_MODE_MASK, cs42l56->iface);
	snd_soc_component_update_bits(component, CS42L56_SERIAL_FMT,
			    CS42L56_DIG_FMT_MASK, cs42l56->iface_fmt);
	snd_soc_component_update_bits(component, CS42L56_CLKCTL_1,
			    CS42L56_SCLK_INV_MASK, cs42l56->iface_inv);
	return 0;
}

static int cs42l56_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;

	if (mute) {
		/* Hit the DSP Mixer first */
		snd_soc_component_update_bits(component, CS42L56_DSP_MUTE_CTL,
				    CS42L56_ADCAMIX_MUTE_MASK |
				    CS42L56_ADCBMIX_MUTE_MASK |
				    CS42L56_PCMAMIX_MUTE_MASK |
				    CS42L56_PCMBMIX_MUTE_MASK |
				    CS42L56_MSTB_MUTE_MASK |
				    CS42L56_MSTA_MUTE_MASK,
				    CS42L56_MUTE_ALL);
		/* Mute ADC's */
		snd_soc_component_update_bits(component, CS42L56_MISC_ADC_CTL,
				    CS42L56_ADCA_MUTE_MASK |
				    CS42L56_ADCB_MUTE_MASK,
				    CS42L56_MUTE_ALL);
		/* HP And LO */
		snd_soc_component_update_bits(component, CS42L56_HPA_VOLUME,
				    CS42L56_HP_MUTE_MASK, CS42L56_MUTE_ALL);
		snd_soc_component_update_bits(component, CS42L56_HPB_VOLUME,
				    CS42L56_HP_MUTE_MASK, CS42L56_MUTE_ALL);
		snd_soc_component_update_bits(component, CS42L56_LOA_VOLUME,
				    CS42L56_LO_MUTE_MASK, CS42L56_MUTE_ALL);
		snd_soc_component_update_bits(component, CS42L56_LOB_VOLUME,
				    CS42L56_LO_MUTE_MASK, CS42L56_MUTE_ALL);
	} else {
		snd_soc_component_update_bits(component, CS42L56_DSP_MUTE_CTL,
				    CS42L56_ADCAMIX_MUTE_MASK |
				    CS42L56_ADCBMIX_MUTE_MASK |
				    CS42L56_PCMAMIX_MUTE_MASK |
				    CS42L56_PCMBMIX_MUTE_MASK |
				    CS42L56_MSTB_MUTE_MASK |
				    CS42L56_MSTA_MUTE_MASK,
				    CS42L56_UNMUTE);

		snd_soc_component_update_bits(component, CS42L56_MISC_ADC_CTL,
				    CS42L56_ADCA_MUTE_MASK |
				    CS42L56_ADCB_MUTE_MASK,
				    CS42L56_UNMUTE);

		snd_soc_component_update_bits(component, CS42L56_HPA_VOLUME,
				    CS42L56_HP_MUTE_MASK, CS42L56_UNMUTE);
		snd_soc_component_update_bits(component, CS42L56_HPB_VOLUME,
				    CS42L56_HP_MUTE_MASK, CS42L56_UNMUTE);
		snd_soc_component_update_bits(component, CS42L56_LOA_VOLUME,
				    CS42L56_LO_MUTE_MASK, CS42L56_UNMUTE);
		snd_soc_component_update_bits(component, CS42L56_LOB_VOLUME,
				    CS42L56_LO_MUTE_MASK, CS42L56_UNMUTE);
	}
	return 0;
}

static int cs42l56_pcm_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs42l56_private *cs42l56 = snd_soc_component_get_drvdata(component);
	int ratio;

	ratio = cs42l56_get_mclk_ratio(cs42l56->mclk, params_rate(params));
	if (ratio >= 0) {
		snd_soc_component_update_bits(component, CS42L56_CLKCTL_2,
				    CS42L56_CLK_RATIO_MASK, ratio);
	} else {
		dev_err(component->dev, "unsupported mclk/sclk/lrclk ratio\n");
		return -EINVAL;
	}

	return 0;
}

static int cs42l56_set_bias_level(struct snd_soc_component *component,
					enum snd_soc_bias_level level)
{
	struct cs42l56_private *cs42l56 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		snd_soc_component_update_bits(component, CS42L56_CLKCTL_1,
				    CS42L56_MCLK_DIS_MASK, 0);
		snd_soc_component_update_bits(component, CS42L56_PWRCTL_1,
				    CS42L56_PDN_ALL_MASK, 0);
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			regcache_cache_only(cs42l56->regmap, false);
			regcache_sync(cs42l56->regmap);
			ret = regulator_bulk_enable(ARRAY_SIZE(cs42l56->supplies),
						    cs42l56->supplies);
			if (ret != 0) {
				dev_err(cs42l56->dev,
					"Failed to enable regulators: %d\n",
					ret);
				return ret;
			}
		}
		snd_soc_component_update_bits(component, CS42L56_PWRCTL_1,
				    CS42L56_PDN_ALL_MASK, 1);
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_component_update_bits(component, CS42L56_PWRCTL_1,
				    CS42L56_PDN_ALL_MASK, 1);
		snd_soc_component_update_bits(component, CS42L56_CLKCTL_1,
				    CS42L56_MCLK_DIS_MASK, 1);
		regcache_cache_only(cs42l56->regmap, true);
		regulator_bulk_disable(ARRAY_SIZE(cs42l56->supplies),
						    cs42l56->supplies);
		break;
	}

	return 0;
}

#define CS42L56_RATES (SNDRV_PCM_RATE_8000_48000)

#define CS42L56_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S18_3LE | \
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE)


static const struct snd_soc_dai_ops cs42l56_ops = {
	.hw_params	= cs42l56_pcm_hw_params,
	.mute_stream	= cs42l56_mute,
	.set_fmt	= cs42l56_set_dai_fmt,
	.set_sysclk	= cs42l56_set_sysclk,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver cs42l56_dai = {
		.name = "cs42l56",
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = CS42L56_RATES,
			.formats = CS42L56_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = CS42L56_RATES,
			.formats = CS42L56_FORMATS,
		},
		.ops = &cs42l56_ops,
};

static int beep_freq[] = {
	261, 522, 585, 667, 706, 774, 889, 1000,
	1043, 1200, 1333, 1412, 1600, 1714, 2000, 2182
};

static void cs42l56_beep_work(struct work_struct *work)
{
	struct cs42l56_private *cs42l56 =
		container_of(work, struct cs42l56_private, beep_work);
	struct snd_soc_component *component = cs42l56->component;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	int i;
	int val = 0;
	int best = 0;

	if (cs42l56->beep_rate) {
		for (i = 0; i < ARRAY_SIZE(beep_freq); i++) {
			if (abs(cs42l56->beep_rate - beep_freq[i]) <
			    abs(cs42l56->beep_rate - beep_freq[best]))
				best = i;
		}

		dev_dbg(component->dev, "Set beep rate %dHz for requested %dHz\n",
			beep_freq[best], cs42l56->beep_rate);

		val = (best << CS42L56_BEEP_RATE_SHIFT);

		snd_soc_dapm_enable_pin(dapm, "Beep");
	} else {
		dev_dbg(component->dev, "Disabling beep\n");
		snd_soc_dapm_disable_pin(dapm, "Beep");
	}

	snd_soc_component_update_bits(component, CS42L56_BEEP_FREQ_ONTIME,
			    CS42L56_BEEP_FREQ_MASK, val);

	snd_soc_dapm_sync(dapm);
}

/* For usability define a way of injecting beep events for the device -
 * many systems will not have a keyboard.
 */
static int cs42l56_beep_event(struct input_dev *dev, unsigned int type,
			     unsigned int code, int hz)
{
	struct snd_soc_component *component = input_get_drvdata(dev);
	struct cs42l56_private *cs42l56 = snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "Beep event %x %x\n", code, hz);

	switch (code) {
	case SND_BELL:
		if (hz)
			hz = 261;
	case SND_TONE:
		break;
	default:
		return -1;
	}

	/* Kick the beep from a workqueue */
	cs42l56->beep_rate = hz;
	schedule_work(&cs42l56->beep_work);
	return 0;
}

static ssize_t cs42l56_beep_set(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct cs42l56_private *cs42l56 = dev_get_drvdata(dev);
	long int time;
	int ret;

	ret = kstrtol(buf, 10, &time);
	if (ret != 0)
		return ret;

	input_event(cs42l56->beep, EV_SND, SND_TONE, time);

	return count;
}

static DEVICE_ATTR(beep, 0200, NULL, cs42l56_beep_set);

static void cs42l56_init_beep(struct snd_soc_component *component)
{
	struct cs42l56_private *cs42l56 = snd_soc_component_get_drvdata(component);
	int ret;

	cs42l56->beep = devm_input_allocate_device(component->dev);
	if (!cs42l56->beep) {
		dev_err(component->dev, "Failed to allocate beep device\n");
		return;
	}

	INIT_WORK(&cs42l56->beep_work, cs42l56_beep_work);
	cs42l56->beep_rate = 0;

	cs42l56->beep->name = "CS42L56 Beep Generator";
	cs42l56->beep->phys = dev_name(component->dev);
	cs42l56->beep->id.bustype = BUS_I2C;

	cs42l56->beep->evbit[0] = BIT_MASK(EV_SND);
	cs42l56->beep->sndbit[0] = BIT_MASK(SND_BELL) | BIT_MASK(SND_TONE);
	cs42l56->beep->event = cs42l56_beep_event;
	cs42l56->beep->dev.parent = component->dev;
	input_set_drvdata(cs42l56->beep, component);

	ret = input_register_device(cs42l56->beep);
	if (ret != 0) {
		cs42l56->beep = NULL;
		dev_err(component->dev, "Failed to register beep device\n");
	}

	ret = device_create_file(component->dev, &dev_attr_beep);
	if (ret != 0) {
		dev_err(component->dev, "Failed to create keyclick file: %d\n",
			ret);
	}
}

static void cs42l56_free_beep(struct snd_soc_component *component)
{
	struct cs42l56_private *cs42l56 = snd_soc_component_get_drvdata(component);

	device_remove_file(component->dev, &dev_attr_beep);
	cancel_work_sync(&cs42l56->beep_work);
	cs42l56->beep = NULL;

	snd_soc_component_update_bits(component, CS42L56_BEEP_TONE_CFG,
			    CS42L56_BEEP_EN_MASK, 0);
}

static int cs42l56_probe(struct snd_soc_component *component)
{
	cs42l56_init_beep(component);

	return 0;
}

static void cs42l56_remove(struct snd_soc_component *component)
{
	cs42l56_free_beep(component);
}

static const struct snd_soc_component_driver soc_component_dev_cs42l56 = {
	.probe			= cs42l56_probe,
	.remove			= cs42l56_remove,
	.set_bias_level		= cs42l56_set_bias_level,
	.controls		= cs42l56_snd_controls,
	.num_controls		= ARRAY_SIZE(cs42l56_snd_controls),
	.dapm_widgets		= cs42l56_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs42l56_dapm_widgets),
	.dapm_routes		= cs42l56_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(cs42l56_audio_map),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config cs42l56_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = CS42L56_MAX_REGISTER,
	.reg_defaults = cs42l56_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs42l56_reg_defaults),
	.readable_reg = cs42l56_readable_register,
	.volatile_reg = cs42l56_volatile_register,
	.cache_type = REGCACHE_RBTREE,
};

static int cs42l56_handle_of_data(struct i2c_client *i2c_client,
				    struct cs42l56_platform_data *pdata)
{
	struct device_node *np = i2c_client->dev.of_node;
	u32 val32;

	if (of_property_read_bool(np, "cirrus,ain1a-reference-cfg"))
		pdata->ain1a_ref_cfg = true;

	if (of_property_read_bool(np, "cirrus,ain2a-reference-cfg"))
		pdata->ain2a_ref_cfg = true;

	if (of_property_read_bool(np, "cirrus,ain1b-reference-cfg"))
		pdata->ain1b_ref_cfg = true;

	if (of_property_read_bool(np, "cirrus,ain2b-reference-cfg"))
		pdata->ain2b_ref_cfg = true;

	if (of_property_read_u32(np, "cirrus,micbias-lvl", &val32) >= 0)
		pdata->micbias_lvl = val32;

	if (of_property_read_u32(np, "cirrus,chgfreq-divisor", &val32) >= 0)
		pdata->chgfreq = val32;

	if (of_property_read_u32(np, "cirrus,adaptive-pwr-cfg", &val32) >= 0)
		pdata->adaptive_pwr = val32;

	if (of_property_read_u32(np, "cirrus,hpf-left-freq", &val32) >= 0)
		pdata->hpfa_freq = val32;

	if (of_property_read_u32(np, "cirrus,hpf-left-freq", &val32) >= 0)
		pdata->hpfb_freq = val32;

	pdata->gpio_nreset = of_get_named_gpio(np, "cirrus,gpio-nreset", 0);

	return 0;
}

static int cs42l56_i2c_probe(struct i2c_client *i2c_client,
			     const struct i2c_device_id *id)
{
	struct cs42l56_private *cs42l56;
	struct cs42l56_platform_data *pdata =
		dev_get_platdata(&i2c_client->dev);
	int ret, i;
	unsigned int devid = 0;
	unsigned int alpha_rev, metal_rev;
	unsigned int reg;

	cs42l56 = devm_kzalloc(&i2c_client->dev, sizeof(*cs42l56), GFP_KERNEL);
	if (cs42l56 == NULL)
		return -ENOMEM;
	cs42l56->dev = &i2c_client->dev;

	cs42l56->regmap = devm_regmap_init_i2c(i2c_client, &cs42l56_regmap);
	if (IS_ERR(cs42l56->regmap)) {
		ret = PTR_ERR(cs42l56->regmap);
		dev_err(&i2c_client->dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}

	if (pdata) {
		cs42l56->pdata = *pdata;
	} else {
		pdata = devm_kzalloc(&i2c_client->dev, sizeof(*pdata),
				     GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		if (i2c_client->dev.of_node) {
			ret = cs42l56_handle_of_data(i2c_client,
						     &cs42l56->pdata);
			if (ret != 0)
				return ret;
		}
		cs42l56->pdata = *pdata;
	}

	if (cs42l56->pdata.gpio_nreset) {
		ret = gpio_request_one(cs42l56->pdata.gpio_nreset,
				       GPIOF_OUT_INIT_HIGH, "CS42L56 /RST");
		if (ret < 0) {
			dev_err(&i2c_client->dev,
				"Failed to request /RST %d: %d\n",
				cs42l56->pdata.gpio_nreset, ret);
			return ret;
		}
		gpio_set_value_cansleep(cs42l56->pdata.gpio_nreset, 0);
		gpio_set_value_cansleep(cs42l56->pdata.gpio_nreset, 1);
	}


	i2c_set_clientdata(i2c_client, cs42l56);

	for (i = 0; i < ARRAY_SIZE(cs42l56->supplies); i++)
		cs42l56->supplies[i].supply = cs42l56_supply_names[i];

	ret = devm_regulator_bulk_get(&i2c_client->dev,
				      ARRAY_SIZE(cs42l56->supplies),
				      cs42l56->supplies);
	if (ret != 0) {
		dev_err(&i2c_client->dev,
			"Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(cs42l56->supplies),
				    cs42l56->supplies);
	if (ret != 0) {
		dev_err(&i2c_client->dev,
			"Failed to enable supplies: %d\n", ret);
		return ret;
	}

	ret = regmap_read(cs42l56->regmap, CS42L56_CHIP_ID_1, &reg);
	devid = reg & CS42L56_CHIP_ID_MASK;
	if (devid != CS42L56_DEVID) {
		dev_err(&i2c_client->dev,
			"CS42L56 Device ID (%X). Expected %X\n",
			devid, CS42L56_DEVID);
		ret = -EINVAL;
		goto err_enable;
	}
	alpha_rev = reg & CS42L56_AREV_MASK;
	metal_rev = reg & CS42L56_MTLREV_MASK;

	dev_info(&i2c_client->dev, "Cirrus Logic CS42L56 ");
	dev_info(&i2c_client->dev, "Alpha Rev %X Metal Rev %X\n",
		 alpha_rev, metal_rev);

	if (cs42l56->pdata.ain1a_ref_cfg)
		regmap_update_bits(cs42l56->regmap, CS42L56_AIN_REFCFG_ADC_MUX,
				   CS42L56_AIN1A_REF_MASK,
				   CS42L56_AIN1A_REF_MASK);

	if (cs42l56->pdata.ain1b_ref_cfg)
		regmap_update_bits(cs42l56->regmap, CS42L56_AIN_REFCFG_ADC_MUX,
				   CS42L56_AIN1B_REF_MASK,
				   CS42L56_AIN1B_REF_MASK);

	if (cs42l56->pdata.ain2a_ref_cfg)
		regmap_update_bits(cs42l56->regmap, CS42L56_AIN_REFCFG_ADC_MUX,
				   CS42L56_AIN2A_REF_MASK,
				   CS42L56_AIN2A_REF_MASK);

	if (cs42l56->pdata.ain2b_ref_cfg)
		regmap_update_bits(cs42l56->regmap, CS42L56_AIN_REFCFG_ADC_MUX,
				   CS42L56_AIN2B_REF_MASK,
				   CS42L56_AIN2B_REF_MASK);

	if (cs42l56->pdata.micbias_lvl)
		regmap_update_bits(cs42l56->regmap, CS42L56_GAIN_BIAS_CTL,
				   CS42L56_MIC_BIAS_MASK,
				cs42l56->pdata.micbias_lvl);

	if (cs42l56->pdata.chgfreq)
		regmap_update_bits(cs42l56->regmap, CS42L56_CLASSH_CTL,
				   CS42L56_CHRG_FREQ_MASK,
				cs42l56->pdata.chgfreq);

	if (cs42l56->pdata.hpfb_freq)
		regmap_update_bits(cs42l56->regmap, CS42L56_HPF_CTL,
				   CS42L56_HPFB_FREQ_MASK,
				cs42l56->pdata.hpfb_freq);

	if (cs42l56->pdata.hpfa_freq)
		regmap_update_bits(cs42l56->regmap, CS42L56_HPF_CTL,
				   CS42L56_HPFA_FREQ_MASK,
				cs42l56->pdata.hpfa_freq);

	if (cs42l56->pdata.adaptive_pwr)
		regmap_update_bits(cs42l56->regmap, CS42L56_CLASSH_CTL,
				   CS42L56_ADAPT_PWR_MASK,
				cs42l56->pdata.adaptive_pwr);

	ret =  devm_snd_soc_register_component(&i2c_client->dev,
			&soc_component_dev_cs42l56, &cs42l56_dai, 1);
	if (ret < 0)
		goto err_enable;

	return 0;

err_enable:
	regulator_bulk_disable(ARRAY_SIZE(cs42l56->supplies),
			       cs42l56->supplies);
	return ret;
}

static int cs42l56_i2c_remove(struct i2c_client *client)
{
	struct cs42l56_private *cs42l56 = i2c_get_clientdata(client);

	regulator_bulk_disable(ARRAY_SIZE(cs42l56->supplies),
			       cs42l56->supplies);
	return 0;
}

static const struct of_device_id cs42l56_of_match[] = {
	{ .compatible = "cirrus,cs42l56", },
	{ }
};
MODULE_DEVICE_TABLE(of, cs42l56_of_match);


static const struct i2c_device_id cs42l56_id[] = {
	{ "cs42l56", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cs42l56_id);

static struct i2c_driver cs42l56_i2c_driver = {
	.driver = {
		.name = "cs42l56",
		.of_match_table = cs42l56_of_match,
	},
	.id_table = cs42l56_id,
	.probe =    cs42l56_i2c_probe,
	.remove =   cs42l56_i2c_remove,
};

module_i2c_driver(cs42l56_i2c_driver);

MODULE_DESCRIPTION("ASoC CS42L56 driver");
MODULE_AUTHOR("Brian Austin, Cirrus Logic Inc, <brian.austin@cirrus.com>");
MODULE_LICENSE("GPL");
