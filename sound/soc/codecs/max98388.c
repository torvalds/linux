// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022, Analog Devices Inc.

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "max98388.h"

static struct reg_default max98388_reg[] = {
	{MAX98388_R2000_SW_RESET, 0x00},
	{MAX98388_R2001_INT_RAW1, 0x00},
	{MAX98388_R2002_INT_RAW2, 0x00},
	{MAX98388_R2004_INT_STATE1, 0x00},
	{MAX98388_R2005_INT_STATE2, 0x00},
	{MAX98388_R2020_THERM_WARN_THRESH, 0x0A},
	{MAX98388_R2031_SPK_MON_THRESH, 0x58},
	{MAX98388_R2032_SPK_MON_LD_SEL, 0x08},
	{MAX98388_R2033_SPK_MON_DURATION, 0x02},
	{MAX98388_R2037_ERR_MON_CTRL, 0x01},
	{MAX98388_R2040_PCM_MODE_CFG, 0xC0},
	{MAX98388_R2041_PCM_CLK_SETUP, 0x04},
	{MAX98388_R2042_PCM_SR_SETUP, 0x88},
	{MAX98388_R2044_PCM_TX_CTRL1, 0x00},
	{MAX98388_R2045_PCM_TX_CTRL2, 0x00},
	{MAX98388_R2050_PCM_TX_HIZ_CTRL1, 0xFF},
	{MAX98388_R2051_PCM_TX_HIZ_CTRL2, 0xFF},
	{MAX98388_R2052_PCM_TX_HIZ_CTRL3, 0xFF},
	{MAX98388_R2053_PCM_TX_HIZ_CTRL4, 0xFF},
	{MAX98388_R2054_PCM_TX_HIZ_CTRL5, 0xFF},
	{MAX98388_R2055_PCM_TX_HIZ_CTRL6, 0xFF},
	{MAX98388_R2056_PCM_TX_HIZ_CTRL7, 0xFF},
	{MAX98388_R2057_PCM_TX_HIZ_CTRL8, 0xFF},
	{MAX98388_R2058_PCM_RX_SRC1, 0x00},
	{MAX98388_R2059_PCM_RX_SRC2, 0x01},
	{MAX98388_R205C_PCM_TX_DRIVE_STRENGTH, 0x00},
	{MAX98388_R205D_PCM_TX_SRC_EN, 0x00},
	{MAX98388_R205E_PCM_RX_EN, 0x00},
	{MAX98388_R205F_PCM_TX_EN, 0x00},
	{MAX98388_R2090_SPK_CH_VOL_CTRL, 0x00},
	{MAX98388_R2091_SPK_CH_CFG, 0x02},
	{MAX98388_R2092_SPK_AMP_OUT_CFG, 0x03},
	{MAX98388_R2093_SPK_AMP_SSM_CFG, 0x01},
	{MAX98388_R2094_SPK_AMP_ER_CTRL, 0x00},
	{MAX98388_R209E_SPK_CH_PINK_NOISE_EN, 0x00},
	{MAX98388_R209F_SPK_CH_AMP_EN, 0x00},
	{MAX98388_R20A0_IV_DATA_DSP_CTRL, 0x10},
	{MAX98388_R20A7_IV_DATA_EN, 0x00},
	{MAX98388_R20E0_BP_ALC_THRESH, 0x04},
	{MAX98388_R20E1_BP_ALC_RATES, 0x20},
	{MAX98388_R20E2_BP_ALC_ATTEN, 0x06},
	{MAX98388_R20E3_BP_ALC_REL, 0x02},
	{MAX98388_R20E4_BP_ALC_MUTE, 0x33},
	{MAX98388_R20EE_BP_INF_HOLD_REL, 0x00},
	{MAX98388_R20EF_BP_ALC_EN, 0x00},
	{MAX98388_R210E_AUTO_RESTART, 0x00},
	{MAX98388_R210F_GLOBAL_EN, 0x00},
	{MAX98388_R22FF_REV_ID, 0x00},
};

static int max98388_dac_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct max98388_priv *max98388 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(max98388->regmap,
			     MAX98388_R210F_GLOBAL_EN, 1);
		usleep_range(30000, 31000);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(max98388->regmap,
			     MAX98388_R210F_GLOBAL_EN, 0);
		usleep_range(30000, 31000);
		max98388->tdm_mode = false;
		break;
	default:
		return 0;
	}
	return 0;
}

static const char * const max98388_monomix_switch_text[] = {
	"Left", "Right", "LeftRight"};

static const struct soc_enum dai_sel_enum =
	SOC_ENUM_SINGLE(MAX98388_R2058_PCM_RX_SRC1,
			MAX98388_PCM_TO_SPK_MONOMIX_CFG_SHIFT,
			3, max98388_monomix_switch_text);

static const struct snd_kcontrol_new max98388_dai_controls =
	SOC_DAPM_ENUM("DAI Sel", dai_sel_enum);

static const struct snd_kcontrol_new max98388_vi_control =
	SOC_DAPM_SINGLE("Switch", MAX98388_R205F_PCM_TX_EN, 0, 1, 0);

static const struct snd_soc_dapm_widget max98388_dapm_widgets[] = {
	SND_SOC_DAPM_DAC_E("Amp Enable", "HiFi Playback",
			   MAX98388_R205E_PCM_RX_EN, 0, 0, max98388_dac_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MUX("DAI Sel Mux", SND_SOC_NOPM, 0, 0,
			 &max98388_dai_controls),
	SND_SOC_DAPM_OUTPUT("BE_OUT"),
	SND_SOC_DAPM_AIF_OUT("Voltage Sense", "HiFi Capture", 0,
			     MAX98388_R20A7_IV_DATA_EN, 0, 0),
	SND_SOC_DAPM_AIF_OUT("Current Sense", "HiFi Capture", 0,
			     MAX98388_R20A7_IV_DATA_EN, 1, 0),
	SND_SOC_DAPM_ADC("ADC Voltage", NULL,
			 MAX98388_R205D_PCM_TX_SRC_EN, 0, 0),
	SND_SOC_DAPM_ADC("ADC Current", NULL,
			 MAX98388_R205D_PCM_TX_SRC_EN, 1, 0),
	SND_SOC_DAPM_SWITCH("VI Sense", SND_SOC_NOPM, 0, 0,
			    &max98388_vi_control),
	SND_SOC_DAPM_SIGGEN("VMON"),
	SND_SOC_DAPM_SIGGEN("IMON"),
};

static DECLARE_TLV_DB_SCALE(max98388_digital_tlv, -6350, 50, 1);
static DECLARE_TLV_DB_SCALE(max98388_amp_gain_tlv, -300, 300, 0);

static const char * const max98388_alc_max_atten_text[] = {
	"0dBFS", "-1dBFS", "-2dBFS", "-3dBFS", "-4dBFS", "-5dBFS",
	"-6dBFS", "-7dBFS", "-8dBFS", "-9dBFS", "-10dBFS", "-11dBFS",
	"-12dBFS", "-13dBFS", "-14dBFS", "-15dBFS"
};

static SOC_ENUM_SINGLE_DECL(max98388_alc_max_atten_enum,
			    MAX98388_R20E2_BP_ALC_ATTEN,
			    MAX98388_ALC_MAX_ATTEN_SHIFT,
			    max98388_alc_max_atten_text);

static const char * const max98388_thermal_warn_text[] = {
	"95C", "105C", "115C", "125C"
};

static SOC_ENUM_SINGLE_DECL(max98388_thermal_warning_thresh_enum,
			    MAX98388_R2020_THERM_WARN_THRESH,
			    MAX98388_THERM_WARN_THRESH_SHIFT,
			    max98388_thermal_warn_text);

static const char * const max98388_thermal_shutdown_text[] = {
	"135C", "145C", "155C", "165C"
};

static SOC_ENUM_SINGLE_DECL(max98388_thermal_shutdown_thresh_enum,
			    MAX98388_R2020_THERM_WARN_THRESH,
			    MAX98388_THERM_SHDN_THRESH_SHIFT,
			    max98388_thermal_shutdown_text);

static const char * const max98388_alc_thresh_single_text[] = {
	"3.625V", "3.550V", "3.475V", "3.400V", "3.325V", "3.250V",
	"3.175V", "3.100V", "3.025V", "2.950V", "2.875V", "2.800V",
	"2.725V", "2.650V", "2.575V", "2.500V"
};

static SOC_ENUM_SINGLE_DECL(max98388_alc_thresh_single_enum,
			    MAX98388_R20E0_BP_ALC_THRESH,
			    MAX98388_ALC_THRESH_SHIFT,
			    max98388_alc_thresh_single_text);

static const char * const max98388_alc_attack_rate_text[] = {
	"0", "10us", "20us", "40us", "80us", "160us",
	"320us", "640us", "1.28ms", "2.56ms", "5.12ms", "10.24ms",
	"20.48ms", "40.96ms", "81.92ms", "163.84ms"
};

static SOC_ENUM_SINGLE_DECL(max98388_alc_attack_rate_enum,
			    MAX98388_R20E1_BP_ALC_RATES,
			    MAX98388_ALC_ATTACK_RATE_SHIFT,
			    max98388_alc_attack_rate_text);

static const char * const max98388_alc_release_rate_text[] = {
	"20us", "40us", "80us", "160us", "320us", "640us",
	"1.28ms", "2.56ms", "5.12ms", "10.24ms", "20.48ms", "40.96ms",
	"81.92ms", "163.84ms", "327.68ms", "655.36ms"
};

static SOC_ENUM_SINGLE_DECL(max98388_alc_release_rate_enum,
			    MAX98388_R20E1_BP_ALC_RATES,
			    MAX98388_ALC_RELEASE_RATE_SHIFT,
			    max98388_alc_release_rate_text);

static const char * const max98388_alc_debounce_text[] = {
	"0.01ms", "0.1ms", "1ms", "10ms", "100ms", "250ms", "500ms", "hold"
};

static SOC_ENUM_SINGLE_DECL(max98388_alc_debouce_enum,
			    MAX98388_R20E3_BP_ALC_REL,
			    MAX98388_ALC_DEBOUNCE_TIME_SHIFT,
			    max98388_alc_debounce_text);

static const char * const max98388_alc_mute_delay_text[] = {
	"0.01ms", "0.05ms", "0.1ms", "0.5ms", "1ms", "5ms", "25ms", "250ms"
};

static SOC_ENUM_SINGLE_DECL(max98388_alc_mute_delay_enum,
			    MAX98388_R20E4_BP_ALC_MUTE,
			    MAX98388_ALC_MUTE_DELAY_SHIFT,
			    max98388_alc_mute_delay_text);

static const char * const max98388_spkmon_duration_text[] = {
	"10ms", "25ms", "50ms", "75ms", "100ms", "200ms", "300ms", "400ms",
	"500ms", "600ms", "700ms", "800ms", "900ms", "1000ms", "1100ms", "1200ms"
};

static SOC_ENUM_SINGLE_DECL(max98388_spkmon_duration_enum,
			    MAX98388_R2033_SPK_MON_DURATION,
			    MAX98388_SPKMON_DURATION_SHIFT,
			    max98388_spkmon_duration_text);

static const char * const max98388_spkmon_thresh_text[] = {
	"0.03V", "0.06V", "0.09V", "0.12V", "0.15V", "0.18V", "0.20V", "0.23V",
	"0.26V", "0.29V", "0.32V", "0.35V", "0.38V", "0.41V", "0.44V", "0.47V",
	"0.50V", "0.53V", "0.56V", "0.58V", "0.61V", "0.64V", "0.67V", "0.70V",
	"0.73V", "0.76V", "0.79V", "0.82V", "0.85V", "0.88V", "0.91V", "0.94V",
	"0.96V", "0.99V", "1.02V", "1.05V", "1.08V", "1.11V", "1.14V", "1.17V",
	"1.20V", "1.23V", "1.26V", "1.29V", "1.32V", "1.35V", "1.37V", "1.40V",
	"1.43V", "1.46V", "1.49V", "1.52V", "1.55V", "1.58V", "1.61V", "1.64V",
	"1.67V", "1.70V", "1.73V", "1.75V", "1.78V", "1.81V", "1.84V", "1.87V",
	"1.90V", "1.93V", "1.96V", "1.99V", "2.02V", "2.05V", "2.08V", "2.11V",
	"2.13V", "2.16V", "2.19V", "2.22V", "2.25V", "2.28V", "2.31V", "2.34V",
	"2.37V", "2.40V", "2.43V", "2.46V", "2.49V", "2.51V", "2.54V", "2.57V",
	"2.60V", "2.63V", "2.66V", "2.69V", "2.72V", "2.75V", "2.78V", "2.81V",
	"2.84V", "2.87V", "2.89V", "2.92V", "2.95V", "2.98V", "3.01V", "3.04V",
	"3.07V", "3.10V", "3.13V", "3.16V", "3.19V", "3.22V", "3.25V", "3.27V",
	"3.30V", "3.33V", "3.36V", "3.39V", "3.42V", "3.45V", "3.48V", "3.51V",
	"3.54V", "3.57V", "3.60V", "3.63V", "3.66V", "3.68V", "3.71V", "3.74V"
};

static SOC_ENUM_SINGLE_DECL(max98388_spkmon_thresh_enum,
			    MAX98388_R2031_SPK_MON_THRESH,
			    MAX98388_SPKMON_THRESH_SHIFT,
			    max98388_spkmon_thresh_text);

static const char * const max98388_spkmon_load_text[] = {
	"2.00ohm", "2.25ohm", "2.50ohm", "2.75ohm", "3.00ohm", "3.25ohm",
	"3.50ohm", "3.75ohm", "4.00ohm", "4.25ohm", "4.50ohm", "4.75ohm",
	"5.00ohm", "5.25ohm", "5.50ohm", "5.75ohm", "6.00ohm", "6.25ohm",
	"6.50ohm", "6.75ohm", "7.00ohm", "7.25ohm", "7.50ohm", "7.75ohm",
	"8.00ohm", "8.25ohm", "8.50ohm", "8.75ohm", "9.00ohm", "9.25ohm",
	"9.50ohm", "9.75ohm", "10.00ohm", "10.25ohm", "10.50ohm", "10.75ohm",
	"11.00ohm", "11.25ohm", "11.50ohm", "11.75ohm",	"12.00ohm", "12.25ohm",
	"12.50ohm", "12.75ohm", "13.00ohm", "13.25ohm", "13.50ohm", "13.75ohm",
	"14.00ohm", "14.25ohm", "14.50ohm", "14.75ohm", "15.00ohm", "15.25ohm",
	"15.50ohm", "15.75ohm", "16.00ohm", "16.25ohm", "16.50ohm", "16.75ohm",
	"17.00ohm", "17.25ohm", "17.50ohm", "17.75ohm", "18.00ohm", "18.25ohm",
	"18.50ohm", "18.75ohm", "19.00ohm", "19.25ohm", "19.50ohm", "19.75ohm",
	"20.00ohm", "20.25ohm", "20.50ohm", "20.75ohm", "21.00ohm", "21.25ohm",
	"21.50ohm", "21.75ohm",	"22.00ohm", "22.25ohm", "22.50ohm", "22.75ohm",
	"23.00ohm", "23.25ohm", "23.50ohm", "23.75ohm",	"24.00ohm", "24.25ohm",
	"24.50ohm", "24.75ohm", "25.00ohm", "25.25ohm", "25.50ohm", "25.75ohm",
	"26.00ohm", "26.25ohm", "26.50ohm", "26.75ohm", "27.00ohm", "27.25ohm",
	"27.50ohm", "27.75ohm",	"28.00ohm", "28.25ohm", "28.50ohm", "28.75ohm",
	"29.00ohm", "29.25ohm", "29.50ohm", "29.75ohm",	"30.00ohm", "30.25ohm",
	"30.50ohm", "30.75ohm", "31.00ohm", "31.25ohm", "31.50ohm", "31.75ohm",
	"32.00ohm", "32.25ohm", "32.50ohm", "32.75ohm", "33.00ohm", "33.25ohm",
	"33.50ohm", "33.75ohm"
};

static SOC_ENUM_SINGLE_DECL(max98388_spkmon_load_enum,
			    MAX98388_R2032_SPK_MON_LD_SEL,
			    MAX98388_SPKMON_LOAD_SHIFT,
			    max98388_spkmon_load_text);

static const char * const max98388_edge_rate_text[] = {
	"Normal", "Reduced", "Maximum", "Increased",
};

static SOC_ENUM_SINGLE_DECL(max98388_edge_rate_falling_enum,
			    MAX98388_R2094_SPK_AMP_ER_CTRL,
			    MAX98388_EDGE_RATE_FALL_SHIFT,
			    max98388_edge_rate_text);

static SOC_ENUM_SINGLE_DECL(max98388_edge_rate_rising_enum,
			    MAX98388_R2094_SPK_AMP_ER_CTRL,
			    MAX98388_EDGE_RATE_RISE_SHIFT,
			    max98388_edge_rate_text);

static const char * const max98388_ssm_mod_text[] = {
	"1.5%", "3.0%", "4.5%", "6.0%",
};

static SOC_ENUM_SINGLE_DECL(max98388_ssm_mod_enum,
			    MAX98388_R2093_SPK_AMP_SSM_CFG,
			    MAX98388_SPK_AMP_SSM_MOD_SHIFT,
			    max98388_ssm_mod_text);

static const struct snd_kcontrol_new max98388_snd_controls[] = {
	SOC_SINGLE("Ramp Up Switch", MAX98388_R2091_SPK_CH_CFG,
		   MAX98388_SPK_CFG_VOL_RMPUP_SHIFT, 1, 0),
	SOC_SINGLE("Ramp Down Switch", MAX98388_R2091_SPK_CH_CFG,
		   MAX98388_SPK_CFG_VOL_RMPDN_SHIFT, 1, 0),
	/* Two Cell Mode Enable */
	SOC_SINGLE("OP Mode Switch", MAX98388_R2092_SPK_AMP_OUT_CFG,
		   MAX98388_SPK_AMP_OUT_MODE_SHIFT, 1, 0),
	/* Speaker Amplifier Overcurrent Automatic Restart Enable */
	SOC_SINGLE("OVC Autorestart Switch", MAX98388_R210E_AUTO_RESTART,
		   MAX98388_OVC_AUTORESTART_SHIFT, 1, 0),
	/* Thermal Shutdown Automatic Restart Enable */
	SOC_SINGLE("THERM Autorestart Switch", MAX98388_R210E_AUTO_RESTART,
		   MAX98388_THERM_AUTORESTART_SHIFT, 1, 0),
	/* PVDD UVLO Auto Restart */
	SOC_SINGLE("UVLO Autorestart Switch", MAX98388_R210E_AUTO_RESTART,
		   MAX98388_PVDD_UVLO_AUTORESTART_SHIFT, 1, 0),
	/* Clock Monitor Automatic Restart Enable */
	SOC_SINGLE("CMON Autorestart Switch", MAX98388_R210E_AUTO_RESTART,
		   MAX98388_CMON_AUTORESTART_SHIFT, 1, 0),
	SOC_SINGLE("CLK Monitor Switch", MAX98388_R2037_ERR_MON_CTRL,
		   MAX98388_CLOCK_MON_SHIFT, 1, 0),
	/* Pinknoise Generator Enable */
	SOC_SINGLE("Pinknoise Gen Switch", MAX98388_R209E_SPK_CH_PINK_NOISE_EN,
		   MAX98388_PINK_NOISE_GEN_SHIFT, 1, 0),
	/* Dither Enable */
	SOC_SINGLE("Dither Switch", MAX98388_R2091_SPK_CH_CFG,
		   MAX98388_SPK_CFG_DITH_EN_SHIFT, 1, 0),
	SOC_SINGLE("VI Dither Switch", MAX98388_R20A0_IV_DATA_DSP_CTRL,
		   MAX98388_AMP_DSP_CTRL_DITH_SHIFT, 1, 0),
	/* DC Blocker Enable */
	SOC_SINGLE("DC Blocker Switch", MAX98388_R2091_SPK_CH_CFG,
		   MAX98388_SPK_CFG_DCBLK_SHIFT, 1, 0),
	SOC_SINGLE("Voltage DC Blocker Switch", MAX98388_R20A0_IV_DATA_DSP_CTRL,
		   MAX98388_AMP_DSP_CTRL_VOL_DCBLK_SHIFT, 1, 0),
	SOC_SINGLE("Current DC Blocker Switch", MAX98388_R20A0_IV_DATA_DSP_CTRL,
		   MAX98388_AMP_DSP_CTRL_CUR_DCBLK_SHIFT, 1, 0),
	/* Digital Volume */
	SOC_SINGLE_TLV("Digital Volume", MAX98388_R2090_SPK_CH_VOL_CTRL,
		       0, 0x7F, 1, max98388_digital_tlv),
	/* Speaker Volume */
	SOC_SINGLE_TLV("Speaker Volume", MAX98388_R2092_SPK_AMP_OUT_CFG,
		       0, 5, 0, max98388_amp_gain_tlv),
	SOC_ENUM("Thermal Warn Thresh", max98388_thermal_warning_thresh_enum),
	SOC_ENUM("Thermal SHDN Thresh", max98388_thermal_shutdown_thresh_enum),
	/* Brownout Protection Automatic Level Control */
	SOC_SINGLE("ALC Switch", MAX98388_R20EF_BP_ALC_EN, 0, 1, 0),
	SOC_ENUM("ALC Thresh", max98388_alc_thresh_single_enum),
	SOC_ENUM("ALC Attack Rate", max98388_alc_attack_rate_enum),
	SOC_ENUM("ALC Release Rate", max98388_alc_release_rate_enum),
	SOC_ENUM("ALC Max Atten", max98388_alc_max_atten_enum),
	SOC_ENUM("ALC Debounce Time", max98388_alc_debouce_enum),
	SOC_SINGLE("ALC Unmute Ramp Switch", MAX98388_R20E4_BP_ALC_MUTE,
		   MAX98388_ALC_UNMUTE_RAMP_EN_SHIFT, 1, 0),
	SOC_SINGLE("ALC Mute Ramp Switch", MAX98388_R20E4_BP_ALC_MUTE,
		   MAX98388_ALC_MUTE_RAMP_EN_SHIFT, 1, 0),
	SOC_SINGLE("ALC Mute Switch", MAX98388_R20E4_BP_ALC_MUTE,
		   MAX98388_ALC_MUTE_EN_SHIFT, 1, 0),
	SOC_ENUM("ALC Mute Delay", max98388_alc_mute_delay_enum),
	/* Speaker Monitor */
	SOC_SINGLE("SPKMON Switch", MAX98388_R2037_ERR_MON_CTRL,
		   MAX98388_SPK_MON_SHIFT, 1, 0),
	SOC_ENUM("SPKMON Thresh", max98388_spkmon_thresh_enum),
	SOC_ENUM("SPKMON Load", max98388_spkmon_load_enum),
	SOC_ENUM("SPKMON Duration", max98388_spkmon_duration_enum),
	/* General Parameters */
	SOC_ENUM("Fall Slew Rate", max98388_edge_rate_falling_enum),
	SOC_ENUM("Rise Slew Rate", max98388_edge_rate_rising_enum),
	SOC_SINGLE("AMP SSM Switch", MAX98388_R2093_SPK_AMP_SSM_CFG,
		   MAX98388_SPK_AMP_SSM_EN_SHIFT, 1, 0),
	SOC_ENUM("AMP SSM Mod", max98388_ssm_mod_enum),
};

static const struct snd_soc_dapm_route max98388_audio_map[] = {
	/* Plabyack */
	{"DAI Sel Mux", "Left", "Amp Enable"},
	{"DAI Sel Mux", "Right", "Amp Enable"},
	{"DAI Sel Mux", "LeftRight", "Amp Enable"},
	{"BE_OUT", NULL, "DAI Sel Mux"},
	/* Capture */
	{ "ADC Voltage", NULL, "VMON"},
	{ "ADC Current", NULL, "IMON"},
	{ "VI Sense", "Switch", "ADC Voltage"},
	{ "VI Sense", "Switch", "ADC Current"},
	{ "Voltage Sense", NULL, "VI Sense"},
	{ "Current Sense", NULL, "VI Sense"},
};

static void max98388_reset(struct max98388_priv *max98388, struct device *dev)
{
	int ret, reg, count;

	/* Software Reset */
	ret = regmap_update_bits(max98388->regmap,
				 MAX98388_R2000_SW_RESET,
				 MAX98388_SOFT_RESET,
				 MAX98388_SOFT_RESET);
	if (ret)
		dev_err(dev, "Reset command failed. (ret:%d)\n", ret);

	count = 0;
	while (count < 3) {
		usleep_range(10000, 11000);
		/* Software Reset Verification */
		ret = regmap_read(max98388->regmap,
				  MAX98388_R22FF_REV_ID, &reg);
		if (!ret) {
			dev_info(dev, "Reset completed (retry:%d)\n", count);
			return;
		}
		count++;
	}
	dev_err(dev, "Reset failed. (ret:%d)\n", ret);
}

static int max98388_probe(struct snd_soc_component *component)
{
	struct max98388_priv *max98388 = snd_soc_component_get_drvdata(component);

	/* Software Reset */
	max98388_reset(max98388, component->dev);

	/* General channel source configuration */
	regmap_write(max98388->regmap,
		     MAX98388_R2059_PCM_RX_SRC2,
		     0x10);

	/* Enable DC blocker */
	regmap_write(max98388->regmap,
		     MAX98388_R2091_SPK_CH_CFG,
		     0x1);
	/* Enable IMON VMON DC blocker */
	regmap_write(max98388->regmap,
		     MAX98388_R20A0_IV_DATA_DSP_CTRL,
		     0x3);
	/* TX slot configuration */
	regmap_write(max98388->regmap,
		     MAX98388_R2044_PCM_TX_CTRL1,
		     max98388->v_slot);

	regmap_write(max98388->regmap,
		     MAX98388_R2045_PCM_TX_CTRL2,
		     max98388->i_slot);
	/* Enable Auto-restart behavior by default */
	regmap_write(max98388->regmap,
		     MAX98388_R210E_AUTO_RESTART, 0xF);
	/* Set interleave mode */
	if (max98388->interleave_mode)
		regmap_update_bits(max98388->regmap,
				   MAX98388_R2040_PCM_MODE_CFG,
				   MAX98388_PCM_TX_CH_INTERLEAVE_MASK,
				   MAX98388_PCM_TX_CH_INTERLEAVE_MASK);

	/* Speaker Amplifier Channel Enable */
	regmap_update_bits(max98388->regmap,
			   MAX98388_R209F_SPK_CH_AMP_EN,
			   MAX98388_SPK_EN_MASK, 1);

	return 0;
}

static int max98388_dai_set_fmt(struct snd_soc_dai *codec_dai,
				unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct max98388_priv *max98388 = snd_soc_component_get_drvdata(component);
	unsigned int format = 0;
	unsigned int invert = 0;

	dev_dbg(component->dev, "%s: fmt 0x%08X\n", __func__, fmt);

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		invert = MAX98388_PCM_MODE_CFG_PCM_BCLKEDGE;
		break;
	default:
		dev_err(component->dev, "DAI invert mode unsupported\n");
		return -EINVAL;
	}

	regmap_update_bits(max98388->regmap,
			   MAX98388_R2041_PCM_CLK_SETUP,
			   MAX98388_PCM_MODE_CFG_PCM_BCLKEDGE,
			   invert);

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format = MAX98388_PCM_FORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format = MAX98388_PCM_FORMAT_LJ;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		format = MAX98388_PCM_FORMAT_TDM_MODE1;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		format = MAX98388_PCM_FORMAT_TDM_MODE0;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(max98388->regmap,
			   MAX98388_R2040_PCM_MODE_CFG,
			   MAX98388_PCM_MODE_CFG_FORMAT_MASK,
			   format << MAX98388_PCM_MODE_CFG_FORMAT_SHIFT);

	return 0;
}

/* BCLKs per LRCLK */
static const int bclk_sel_table[] = {
	32, 48, 64, 96, 128, 192, 256, 384, 512, 320,
};

static int max98388_get_bclk_sel(int bclk)
{
	int i;
	/* match BCLKs per LRCLK */
	for (i = 0; i < ARRAY_SIZE(bclk_sel_table); i++) {
		if (bclk_sel_table[i] == bclk)
			return i + 2;
	}
	return 0;
}

static int max98388_set_clock(struct snd_soc_component *component,
			      struct snd_pcm_hw_params *params)
{
	struct max98388_priv *max98388 = snd_soc_component_get_drvdata(component);
	/* BCLK/LRCLK ratio calculation */
	int blr_clk_ratio = params_channels(params) * max98388->ch_size;
	int value;

	if (!max98388->tdm_mode) {
		/* BCLK configuration */
		value = max98388_get_bclk_sel(blr_clk_ratio);
		if (!value) {
			dev_err(component->dev, "format unsupported %d\n",
				params_format(params));
			return -EINVAL;
		}

		regmap_update_bits(max98388->regmap,
				   MAX98388_R2041_PCM_CLK_SETUP,
				   MAX98388_PCM_CLK_SETUP_BSEL_MASK,
				   value);
	}
	return 0;
}

static int max98388_dai_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct max98388_priv *max98388 = snd_soc_component_get_drvdata(component);
	unsigned int sampling_rate = 0;
	unsigned int chan_sz = 0;
	int ret, reg;
	int status = 0;

	/* pcm mode configuration */
	switch (snd_pcm_format_width(params_format(params))) {
	case 16:
		chan_sz = MAX98388_PCM_MODE_CFG_CHANSZ_16;
		break;
	case 24:
		chan_sz = MAX98388_PCM_MODE_CFG_CHANSZ_24;
		break;
	case 32:
		chan_sz = MAX98388_PCM_MODE_CFG_CHANSZ_32;
		break;
	default:
		dev_err(component->dev, "format unsupported %d\n",
			params_format(params));
		goto err;
	}

	max98388->ch_size = snd_pcm_format_width(params_format(params));

	ret = regmap_read(max98388->regmap,
			  MAX98388_R2040_PCM_MODE_CFG, &reg);
	if (ret < 0)
		goto err;

	/* GLOBAL_EN OFF prior to the channel size re-configure */
	if (chan_sz != (reg & MAX98388_PCM_MODE_CFG_CHANSZ_MASK))	{
		ret = regmap_read(max98388->regmap,
				  MAX98388_R210F_GLOBAL_EN, &status);
		if (ret < 0)
			goto err;

		if (status) {
			regmap_write(max98388->regmap,
				     MAX98388_R210F_GLOBAL_EN, 0);
			usleep_range(30000, 31000);
		}
		regmap_update_bits(max98388->regmap,
				   MAX98388_R2040_PCM_MODE_CFG,
				   MAX98388_PCM_MODE_CFG_CHANSZ_MASK, chan_sz);
	}
	dev_dbg(component->dev, "format supported %d",
		params_format(params));

	/* sampling rate configuration */
	switch (params_rate(params)) {
	case 8000:
		sampling_rate = MAX98388_PCM_SR_8000;
		break;
	case 11025:
		sampling_rate = MAX98388_PCM_SR_11025;
		break;
	case 12000:
		sampling_rate = MAX98388_PCM_SR_12000;
		break;
	case 16000:
		sampling_rate = MAX98388_PCM_SR_16000;
		break;
	case 22050:
		sampling_rate = MAX98388_PCM_SR_22050;
		break;
	case 24000:
		sampling_rate = MAX98388_PCM_SR_24000;
		break;
	case 32000:
		sampling_rate = MAX98388_PCM_SR_32000;
		break;
	case 44100:
		sampling_rate = MAX98388_PCM_SR_44100;
		break;
	case 48000:
		sampling_rate = MAX98388_PCM_SR_48000;
		break;
	case 88200:
		sampling_rate = MAX98388_PCM_SR_88200;
		break;
	case 96000:
		sampling_rate = MAX98388_PCM_SR_96000;
		break;
	default:
		dev_err(component->dev, "rate %d not supported\n",
			params_rate(params));
		goto err;
	}

	/* set DAI_SR to correct LRCLK frequency */
	regmap_update_bits(max98388->regmap,
			   MAX98388_R2042_PCM_SR_SETUP,
			   MAX98388_PCM_SR_MASK,
			   sampling_rate);

	/* set sampling rate of IV */
	if (max98388->interleave_mode &&
	    sampling_rate > MAX98388_PCM_SR_16000)
		regmap_update_bits(max98388->regmap,
				   MAX98388_R2042_PCM_SR_SETUP,
				   MAX98388_PCM_SR_IV_MASK,
				   (sampling_rate - 3) << MAX98388_PCM_SR_IV_SHIFT);
	else
		regmap_update_bits(max98388->regmap,
				   MAX98388_R2042_PCM_SR_SETUP,
				   MAX98388_PCM_SR_IV_MASK,
				   sampling_rate << MAX98388_PCM_SR_IV_SHIFT);

	ret = max98388_set_clock(component, params);

	if (status) {
		regmap_write(max98388->regmap,
			     MAX98388_R210F_GLOBAL_EN, 1);
		usleep_range(30000, 31000);
	}

	return ret;

err:
	return -EINVAL;
}

#define MAX_NUM_SLOTS 16
#define MAX_NUM_CH 2

static int max98388_dai_tdm_slot(struct snd_soc_dai *dai,
				 unsigned int tx_mask, unsigned int rx_mask,
				 int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct max98388_priv *max98388 = snd_soc_component_get_drvdata(component);
	int bsel = 0;
	unsigned int chan_sz = 0;
	unsigned int mask;
	int cnt, slot_found;
	int addr, bits;

	if (!tx_mask && !rx_mask && !slots && !slot_width)
		max98388->tdm_mode = false;
	else
		max98388->tdm_mode = true;

	/* BCLK configuration */
	bsel = max98388_get_bclk_sel(slots * slot_width);
	if (bsel == 0) {
		dev_err(component->dev, "BCLK %d not supported\n",
			slots * slot_width);
		return -EINVAL;
	}

	regmap_update_bits(max98388->regmap,
			   MAX98388_R2041_PCM_CLK_SETUP,
			   MAX98388_PCM_CLK_SETUP_BSEL_MASK,
			   bsel);

	/* Channel size configuration */
	switch (slot_width) {
	case 16:
		chan_sz = MAX98388_PCM_MODE_CFG_CHANSZ_16;
		break;
	case 24:
		chan_sz = MAX98388_PCM_MODE_CFG_CHANSZ_24;
		break;
	case 32:
		chan_sz = MAX98388_PCM_MODE_CFG_CHANSZ_32;
		break;
	default:
		dev_err(component->dev, "format unsupported %d\n",
			slot_width);
		return -EINVAL;
	}

	regmap_update_bits(max98388->regmap,
			   MAX98388_R2040_PCM_MODE_CFG,
			   MAX98388_PCM_MODE_CFG_CHANSZ_MASK, chan_sz);

	/* Rx slot configuration */
	slot_found = 0;
	mask = rx_mask;
	for (cnt = 0 ; cnt < MAX_NUM_SLOTS ; cnt++, mask >>= 1) {
		if (mask & 0x1) {
			if (slot_found == 0)
				regmap_update_bits(max98388->regmap,
						   MAX98388_R2059_PCM_RX_SRC2,
						   MAX98388_RX_SRC_CH0_SHIFT,
						   cnt);
			else
				regmap_update_bits(max98388->regmap,
						   MAX98388_R2059_PCM_RX_SRC2,
						   MAX98388_RX_SRC_CH1_SHIFT,
						   cnt);
			slot_found++;
			if (slot_found >= MAX_NUM_CH)
				break;
		}
	}

	/* speaker feedback slot configuration */
	slot_found = 0;
	mask = tx_mask;
	for (cnt = 0 ; cnt < MAX_NUM_SLOTS ; cnt++, mask >>= 1) {
		if (mask & 0x1) {
			addr = MAX98388_R2044_PCM_TX_CTRL1 + (cnt / 8);
			bits = cnt % 8;
			regmap_update_bits(max98388->regmap, addr, bits, bits);
			if (slot_found >= MAX_NUM_CH)
				break;
		}
	}

	return 0;
}

#define MAX98388_RATES SNDRV_PCM_RATE_8000_96000

#define MAX98388_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops max98388_dai_ops = {
	.set_fmt = max98388_dai_set_fmt,
	.hw_params = max98388_dai_hw_params,
	.set_tdm_slot = max98388_dai_tdm_slot,
};

static bool max98388_readable_register(struct device *dev,
				       unsigned int reg)
{
	switch (reg) {
	case MAX98388_R2001_INT_RAW1 ... MAX98388_R2002_INT_RAW2:
	case MAX98388_R2004_INT_STATE1... MAX98388_R2005_INT_STATE2:
	case MAX98388_R2020_THERM_WARN_THRESH:
	case MAX98388_R2031_SPK_MON_THRESH
		... MAX98388_R2033_SPK_MON_DURATION:
	case MAX98388_R2037_ERR_MON_CTRL:
	case MAX98388_R2040_PCM_MODE_CFG
		... MAX98388_R2042_PCM_SR_SETUP:
	case MAX98388_R2044_PCM_TX_CTRL1
		... MAX98388_R2045_PCM_TX_CTRL2:
	case MAX98388_R2050_PCM_TX_HIZ_CTRL1
		... MAX98388_R2059_PCM_RX_SRC2:
	case MAX98388_R205C_PCM_TX_DRIVE_STRENGTH
		... MAX98388_R205F_PCM_TX_EN:
	case MAX98388_R2090_SPK_CH_VOL_CTRL
		... MAX98388_R2094_SPK_AMP_ER_CTRL:
	case MAX98388_R209E_SPK_CH_PINK_NOISE_EN
		... MAX98388_R209F_SPK_CH_AMP_EN:
	case MAX98388_R20A0_IV_DATA_DSP_CTRL:
	case MAX98388_R20A7_IV_DATA_EN:
	case MAX98388_R20E0_BP_ALC_THRESH ... MAX98388_R20E4_BP_ALC_MUTE:
	case MAX98388_R20EE_BP_INF_HOLD_REL ... MAX98388_R20EF_BP_ALC_EN:
	case MAX98388_R210E_AUTO_RESTART:
	case MAX98388_R210F_GLOBAL_EN:
	case MAX98388_R22FF_REV_ID:
		return true;
	default:
		return false;
	}
};

static bool max98388_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98388_R2001_INT_RAW1 ... MAX98388_R2005_INT_STATE2:
	case MAX98388_R210F_GLOBAL_EN:
	case MAX98388_R22FF_REV_ID:
		return true;
	default:
		return false;
	}
}

static struct snd_soc_dai_driver max98388_dai[] = {
	{
		.name = "max98388-aif1",
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MAX98388_RATES,
			.formats = MAX98388_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MAX98388_RATES,
			.formats = MAX98388_FORMATS,
		},
		.ops = &max98388_dai_ops,
	}
};

static int max98388_suspend(struct device *dev)
{
	struct max98388_priv *max98388 = dev_get_drvdata(dev);

	regcache_cache_only(max98388->regmap, true);
	regcache_mark_dirty(max98388->regmap);

	return 0;
}

static int max98388_resume(struct device *dev)
{
	struct max98388_priv *max98388 = dev_get_drvdata(dev);

	regcache_cache_only(max98388->regmap, false);
	max98388_reset(max98388, dev);
	regcache_sync(max98388->regmap);

	return 0;
}

static const struct dev_pm_ops max98388_pm = {
	SYSTEM_SLEEP_PM_OPS(max98388_suspend, max98388_resume)
};

static const struct regmap_config max98388_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = MAX98388_R22FF_REV_ID,
	.reg_defaults  = max98388_reg,
	.num_reg_defaults = ARRAY_SIZE(max98388_reg),
	.readable_reg = max98388_readable_register,
	.volatile_reg = max98388_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

static const struct snd_soc_component_driver soc_codec_dev_max98388 = {
	.probe			= max98388_probe,
	.controls		= max98388_snd_controls,
	.num_controls		= ARRAY_SIZE(max98388_snd_controls),
	.dapm_widgets		= max98388_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(max98388_dapm_widgets),
	.dapm_routes		= max98388_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(max98388_audio_map),
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static void max98388_read_deveice_property(struct device *dev,
					   struct max98388_priv *max98388)
{
	int value;

	if (!device_property_read_u32(dev, "adi,vmon-slot-no", &value))
		max98388->v_slot = value & 0xF;
	else
		max98388->v_slot = 0;

	if (!device_property_read_u32(dev, "adi,imon-slot-no", &value))
		max98388->i_slot = value & 0xF;
	else
		max98388->i_slot = 1;

	if (device_property_read_bool(dev, "adi,interleave-mode"))
		max98388->interleave_mode = true;
	else
		max98388->interleave_mode = false;
}

static int max98388_i2c_probe(struct i2c_client *i2c)
{
	int ret = 0;
	int reg = 0;

	struct max98388_priv *max98388 = NULL;

	max98388 = devm_kzalloc(&i2c->dev, sizeof(*max98388), GFP_KERNEL);
	if (!max98388)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max98388);

	/* regmap initialization */
	max98388->regmap = devm_regmap_init_i2c(i2c, &max98388_regmap);
	if (IS_ERR(max98388->regmap))
		return dev_err_probe(&i2c->dev, PTR_ERR(max98388->regmap),
				     "Failed to allocate register map.\n");

	/* voltage/current slot & gpio configuration */
	max98388_read_deveice_property(&i2c->dev, max98388);

	/* Device Reset */
	max98388->reset_gpio = devm_gpiod_get_optional(&i2c->dev,
						       "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(max98388->reset_gpio))
		return dev_err_probe(&i2c->dev, PTR_ERR(max98388->reset_gpio),
				     "Unable to request GPIO\n");

	if (max98388->reset_gpio) {
		usleep_range(5000, 6000);
		gpiod_set_value_cansleep(max98388->reset_gpio, 0);
		/* Wait for the hw reset done */
		usleep_range(5000, 6000);
	}

	/* Read Revision ID */
	ret = regmap_read(max98388->regmap,
			  MAX98388_R22FF_REV_ID, &reg);
	if (ret < 0)
		return dev_err_probe(&i2c->dev, ret,
				     "Failed to read the revision ID\n");

	dev_info(&i2c->dev, "MAX98388 revisionID: 0x%02X\n", reg);

	/* codec registration */
	ret = devm_snd_soc_register_component(&i2c->dev,
					      &soc_codec_dev_max98388,
					      max98388_dai,
					      ARRAY_SIZE(max98388_dai));
	if (ret < 0)
		dev_err(&i2c->dev, "Failed to register codec: %d\n", ret);

	return ret;
}

static const struct i2c_device_id max98388_i2c_id[] = {
	{ "max98388", 0},
	{ },
};

MODULE_DEVICE_TABLE(i2c, max98388_i2c_id);

static const struct of_device_id max98388_of_match[] = {
	{ .compatible = "adi,max98388", },
	{ }
};
MODULE_DEVICE_TABLE(of, max98388_of_match);

static const struct acpi_device_id max98388_acpi_match[] = {
	{ "ADS8388", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, max98388_acpi_match);

static struct i2c_driver max98388_i2c_driver = {
	.driver = {
		.name = "max98388",
		.of_match_table = max98388_of_match,
		.acpi_match_table = max98388_acpi_match,
		.pm = pm_sleep_ptr(&max98388_pm),
	},
	.probe = max98388_i2c_probe,
	.id_table = max98388_i2c_id,
};

module_i2c_driver(max98388_i2c_driver)

MODULE_DESCRIPTION("ALSA SoC MAX98388 driver");
MODULE_AUTHOR("Ryan Lee <ryans.lee@analog.com>");
MODULE_LICENSE("GPL");
